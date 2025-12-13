// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_afc.h"

#include "moonraker_api.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <sstream>

// ============================================================================
// Construction / Destruction
// ============================================================================

AmsBackendAfc::AmsBackendAfc(MoonrakerAPI* api, MoonrakerClient* client)
    : api_(api), client_(client) {
    // Initialize system info with AFC defaults
    system_info_.type = AmsType::AFC;
    system_info_.type_name = "AFC";
    system_info_.version = "unknown";
    system_info_.current_tool = -1;
    system_info_.current_gate = -1;
    system_info_.filament_loaded = false;
    system_info_.action = AmsAction::IDLE;
    system_info_.total_gates = 0;
    // AFC capabilities - may vary by configuration
    system_info_.supports_endless_spool = false;
    system_info_.supports_spoolman = true;
    system_info_.supports_tool_mapping = true;
    system_info_.supports_bypass = true; // AFC supports bypass via bypass_state
    // Default to hardware sensor - AFC BoxTurtle typically has physical bypass sensor
    // TODO: Detect from AFC configuration whether bypass sensor is virtual or hardware
    system_info_.has_hardware_bypass_sensor = true;

    spdlog::debug("[AMS AFC] Backend created");
}

AmsBackendAfc::~AmsBackendAfc() {
    // Clear event callback first to prevent any callbacks during destruction
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        event_callback_ = nullptr;
    }
    stop();
}

// ============================================================================
// Lifecycle Management
// ============================================================================

AmsError AmsBackendAfc::start() {
    bool should_emit = false;

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        if (running_) {
            return AmsErrorHelper::success();
        }

        if (!client_) {
            spdlog::error("[AMS AFC] Cannot start: MoonrakerClient is null");
            return AmsErrorHelper::not_connected("MoonrakerClient not provided");
        }

        if (!api_) {
            spdlog::error("[AMS AFC] Cannot start: MoonrakerAPI is null");
            return AmsErrorHelper::not_connected("MoonrakerAPI not provided");
        }

        // Register for status update notifications from Moonraker
        // AFC state comes via notify_status_update when printer.afc.* changes
        SubscriptionId id = client_->register_notify_update(
            [this](const nlohmann::json& notification) { handle_status_update(notification); });

        if (id == INVALID_SUBSCRIPTION_ID) {
            spdlog::error("[AMS AFC] Failed to register for status updates");
            return AmsErrorHelper::not_connected("Failed to subscribe to Moonraker updates");
        }

        // RAII guard - automatically unsubscribes when backend is destroyed or stop() called
        subscription_ = SubscriptionGuard(client_, id);

        running_ = true;
        spdlog::info("[AMS AFC] Backend started, subscription ID: {}", id);

        // Detect AFC version (async - results come via callback)
        // This will set has_lane_data_db_ for v1.0.32+
        detect_afc_version();

        // If we have discovered lanes (from PrinterCapabilities), initialize them now.
        // This provides immediate lane data for ALL AFC versions (including < 1.0.32).
        // For v1.0.32+, query_lane_data() may later supplement this with richer data.
        if (!lane_names_.empty() && !lanes_initialized_) {
            spdlog::info("[AMS AFC] Initializing {} lanes from discovery", lane_names_.size());
            initialize_lanes(lane_names_);
        }

        should_emit = true;
    } // Release lock before emitting

    // Note: With the early hardware discovery callback architecture, this backend is
    // created and started BEFORE printer.objects.subscribe is called. The notification
    // handler registered above will naturally receive the initial state when the
    // subscription response arrives. No explicit query_initial_state() needed.

    // Emit initial state event OUTSIDE the lock to avoid deadlock
    if (should_emit) {
        emit_event(EVENT_STATE_CHANGED);
    }

    return AmsErrorHelper::success();
}

void AmsBackendAfc::set_discovered_lanes(const std::vector<std::string>& lane_names,
                                         const std::vector<std::string>& hub_names) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Store discovered lane and hub names (from printer.objects.list)
    // These will be used as a fallback for AFC versions < 1.0.32
    if (!lane_names.empty()) {
        lane_names_ = lane_names;
        spdlog::debug("[AMS AFC] Set {} discovered lanes", lane_names_.size());
    }

    if (!hub_names.empty()) {
        hub_names_ = hub_names;
        spdlog::debug("[AMS AFC] Set {} discovered hubs", hub_names_.size());
    }
}

void AmsBackendAfc::stop() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!running_) {
        return;
    }

    // RAII guard handles unsubscription automatically
    subscription_.reset();

    running_ = false;
    spdlog::info("[AMS AFC] Backend stopped");
}

bool AmsBackendAfc::is_running() const {
    return running_;
}

// ============================================================================
// Event System
// ============================================================================

void AmsBackendAfc::set_event_callback(EventCallback callback) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    event_callback_ = std::move(callback);
}

void AmsBackendAfc::emit_event(const std::string& event, const std::string& data) {
    EventCallback cb;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        cb = event_callback_;
    }

    if (cb) {
        cb(event, data);
    }
}

// ============================================================================
// State Queries
// ============================================================================

AmsSystemInfo AmsBackendAfc::get_system_info() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return system_info_;
}

AmsType AmsBackendAfc::get_type() const {
    return AmsType::AFC;
}

GateInfo AmsBackendAfc::get_gate_info(int global_index) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    const auto* gate = system_info_.get_gate_global(global_index);
    if (gate) {
        return *gate;
    }

    // Return empty gate info for invalid index
    GateInfo empty;
    empty.gate_index = -1;
    empty.global_index = -1;
    return empty;
}

AmsAction AmsBackendAfc::get_current_action() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return system_info_.action;
}

int AmsBackendAfc::get_current_tool() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return system_info_.current_tool;
}

int AmsBackendAfc::get_current_gate() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return system_info_.current_gate;
}

bool AmsBackendAfc::is_filament_loaded() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return system_info_.filament_loaded;
}

PathTopology AmsBackendAfc::get_topology() const {
    // AFC uses a hub topology (Box Turtle / Armored Turtle style)
    return PathTopology::HUB;
}

PathSegment AmsBackendAfc::get_filament_segment() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return compute_filament_segment_unlocked();
}

PathSegment AmsBackendAfc::infer_error_segment() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return error_segment_;
}

PathSegment AmsBackendAfc::compute_filament_segment_unlocked() const {
    // Must be called with mutex_ held!
    // Returns the furthest point filament has reached based on sensor states.
    //
    // Sensor progression (AFC hub topology):
    //   SPOOL → PREP → LANE → HUB → OUTPUT → TOOLHEAD → NOZZLE
    //
    // Mapping from sensors:
    //   tool_end_sensor   → NOZZLE (filament at nozzle tip)
    //   tool_start_sensor → TOOLHEAD (filament entered toolhead)
    //   hub_sensor        → OUTPUT (filament past hub, heading to toolhead)
    //   loaded_to_hub     → HUB (filament reached hub merger)
    //   load              → LANE (filament in lane between prep and hub)
    //   prep              → PREP (filament at prep sensor, past spool)
    //   (no sensors)      → NONE or SPOOL depending on context

    // Check toolhead sensors first (furthest along path)
    if (tool_end_sensor_) {
        return PathSegment::NOZZLE;
    }

    if (tool_start_sensor_) {
        return PathSegment::TOOLHEAD;
    }

    // Check hub sensor
    if (hub_sensor_) {
        return PathSegment::OUTPUT;
    }

    // Check per-lane sensors for the current lane
    // If no current lane is set, check all lanes for any activity
    int lane_to_check = -1;
    if (!current_lane_name_.empty()) {
        auto it = lane_name_to_index_.find(current_lane_name_);
        if (it != lane_name_to_index_.end()) {
            lane_to_check = it->second;
        }
    }

    // If we have a current lane, check its sensors
    if (lane_to_check >= 0 && lane_to_check < static_cast<int>(lane_sensors_.size())) {
        const LaneSensors& sensors = lane_sensors_[lane_to_check];

        if (sensors.loaded_to_hub) {
            return PathSegment::HUB;
        }

        if (sensors.load) {
            return PathSegment::LANE;
        }

        if (sensors.prep) {
            return PathSegment::PREP;
        }
    }

    // Fallback: check all lanes for any sensor activity
    for (size_t i = 0; i < lane_names_.size() && i < lane_sensors_.size(); ++i) {
        const LaneSensors& sensors = lane_sensors_[i];

        if (sensors.loaded_to_hub) {
            return PathSegment::HUB;
        }

        if (sensors.load) {
            return PathSegment::LANE;
        }

        if (sensors.prep) {
            return PathSegment::PREP;
        }
    }

    // No sensors triggered - filament either at spool or absent
    // If we know filament is loaded somewhere, assume SPOOL
    if (system_info_.filament_loaded || system_info_.current_gate >= 0) {
        return PathSegment::SPOOL;
    }

    return PathSegment::NONE;
}

// ============================================================================
// Moonraker Status Update Handling
// ============================================================================

void AmsBackendAfc::handle_status_update(const nlohmann::json& notification) {
    // notify_status_update has format: { "method": "notify_status_update", "params": [{ ... },
    // timestamp] }
    if (!notification.contains("params") || !notification["params"].is_array() ||
        notification["params"].empty()) {
        return;
    }

    const auto& params = notification["params"][0];
    if (!params.is_object()) {
        return;
    }

    bool state_changed = false;

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        // Parse global AFC state if present
        if (params.contains("AFC") && params["AFC"].is_object()) {
            parse_afc_state(params["AFC"]);
            state_changed = true;
        }

        // Legacy: also check for lowercase "afc" (older AFC versions)
        if (params.contains("afc") && params["afc"].is_object()) {
            parse_afc_state(params["afc"]);
            state_changed = true;
        }

        // Parse AFC_stepper lane objects for sensor states
        // Keys like "AFC_stepper lane1", "AFC_stepper lane2", etc.
        for (const auto& lane_name : lane_names_) {
            std::string key = "AFC_stepper " + lane_name;
            if (params.contains(key) && params[key].is_object()) {
                parse_afc_stepper(lane_name, params[key]);
                state_changed = true;
            }
        }

        // Parse AFC_hub objects for hub sensor state
        // Keys like "AFC_hub Turtle_1"
        for (const auto& hub_name : hub_names_) {
            std::string key = "AFC_hub " + hub_name;
            if (params.contains(key) && params[key].is_object()) {
                parse_afc_hub(params[key]);
                state_changed = true;
            }
        }

        // Parse AFC_extruder for toolhead sensors
        if (params.contains("AFC_extruder extruder") &&
            params["AFC_extruder extruder"].is_object()) {
            parse_afc_extruder(params["AFC_extruder extruder"]);
            state_changed = true;
        }
    }

    if (state_changed) {
        emit_event(EVENT_STATE_CHANGED);
    }
}

void AmsBackendAfc::parse_afc_state(const nlohmann::json& afc_data) {
    // Parse current lane/gate (AFC may report this as "current_lane" or similar)
    if (afc_data.contains("current_lane") && afc_data["current_lane"].is_string()) {
        std::string lane_name = afc_data["current_lane"].get<std::string>();
        auto it = lane_name_to_index_.find(lane_name);
        if (it != lane_name_to_index_.end()) {
            system_info_.current_gate = it->second;
            spdlog::trace("[AMS AFC] Current lane: {} (gate {})", lane_name,
                          system_info_.current_gate);
        }
    }

    // Parse current tool
    if (afc_data.contains("current_tool") && afc_data["current_tool"].is_number_integer()) {
        system_info_.current_tool = afc_data["current_tool"].get<int>();
        spdlog::trace("[AMS AFC] Current tool: {}", system_info_.current_tool);
    }

    // Parse filament loaded state
    if (afc_data.contains("filament_loaded") && afc_data["filament_loaded"].is_boolean()) {
        system_info_.filament_loaded = afc_data["filament_loaded"].get<bool>();
        spdlog::trace("[AMS AFC] Filament loaded: {}", system_info_.filament_loaded);
    }

    // Parse action/status
    if (afc_data.contains("status") && afc_data["status"].is_string()) {
        std::string status_str = afc_data["status"].get<std::string>();
        system_info_.action = ams_action_from_string(status_str);
        system_info_.operation_detail = status_str;
        spdlog::trace("[AMS AFC] Status: {} ({})", ams_action_to_string(system_info_.action),
                      status_str);
    }

    // Parse lanes array if present (some AFC versions provide this)
    if (afc_data.contains("lanes") && afc_data["lanes"].is_object()) {
        parse_lane_data(afc_data["lanes"]);
    }

    // Parse unit information if available
    if (afc_data.contains("units") && afc_data["units"].is_array()) {
        // AFC may report multiple units (Box Turtles)
        // Update unit names and connection status
        const auto& units = afc_data["units"];
        for (size_t i = 0; i < units.size() && i < system_info_.units.size(); ++i) {
            if (units[i].is_object()) {
                if (units[i].contains("name") && units[i]["name"].is_string()) {
                    system_info_.units[i].name = units[i]["name"].get<std::string>();
                }
                if (units[i].contains("connected") && units[i]["connected"].is_boolean()) {
                    system_info_.units[i].connected = units[i]["connected"].get<bool>();
                }
            }
        }
    }

    // Extract hub names from AFC.hubs array
    if (afc_data.contains("hubs") && afc_data["hubs"].is_array()) {
        hub_names_.clear();
        for (const auto& hub : afc_data["hubs"]) {
            if (hub.is_string()) {
                hub_names_.push_back(hub.get<std::string>());
            }
        }
        spdlog::debug("[AMS AFC] Discovered {} hubs", hub_names_.size());
    }

    // Parse error state
    if (afc_data.contains("error_state") && afc_data["error_state"].is_boolean()) {
        error_state_ = afc_data["error_state"].get<bool>();
        if (error_state_) {
            // Use unlocked helper since we're already holding mutex_
            error_segment_ = compute_filament_segment_unlocked();
        } else {
            error_segment_ = PathSegment::NONE;
        }
    }

    // Parse bypass state (AFC exposes this via printer.AFC.bypass_state)
    // When bypass is active, current_gate = -2 (convention from Happy Hare)
    if (afc_data.contains("bypass_state") && afc_data["bypass_state"].is_boolean()) {
        bypass_active_ = afc_data["bypass_state"].get<bool>();
        if (bypass_active_) {
            system_info_.current_gate = -2; // -2 = bypass mode
            system_info_.filament_loaded = true;
            spdlog::trace("[AMS AFC] Bypass mode active");
        }
    }
}

// ============================================================================
// AFC Object Parsing (AFC_stepper, AFC_hub, AFC_extruder)
// ============================================================================

void AmsBackendAfc::parse_afc_stepper(const std::string& lane_name, const nlohmann::json& data) {
    // Parse AFC_stepper lane{N} object for sensor states and filament info
    // {
    //   "prep": true,           // Prep sensor
    //   "load": true,           // Load sensor
    //   "loaded_to_hub": true,  // Past hub
    //   "tool_loaded": false,   // At toolhead
    //   "status": "Loaded",
    //   "color": "#00aeff",
    //   "material": "ASA",
    //   "spool_id": 5,
    //   "weight": 931.7
    // }

    auto it = lane_name_to_index_.find(lane_name);
    if (it == lane_name_to_index_.end()) {
        spdlog::trace("[AMS AFC] Unknown lane name: {}", lane_name);
        return;
    }
    int gate_index = it->second;

    if (gate_index < 0 || gate_index >= static_cast<int>(lane_sensors_.size())) {
        return;
    }

    // Update sensor state for this lane
    LaneSensors& sensors = lane_sensors_[gate_index];
    if (data.contains("prep") && data["prep"].is_boolean()) {
        sensors.prep = data["prep"].get<bool>();
    }
    if (data.contains("load") && data["load"].is_boolean()) {
        sensors.load = data["load"].get<bool>();
    }
    if (data.contains("loaded_to_hub") && data["loaded_to_hub"].is_boolean()) {
        sensors.loaded_to_hub = data["loaded_to_hub"].get<bool>();
    }

    // Get gate info for filament data update
    GateInfo* gate = system_info_.get_gate_global(gate_index);
    if (!gate)
        return;

    // Parse color
    if (data.contains("color") && data["color"].is_string()) {
        std::string color_str = data["color"].get<std::string>();
        // Remove '#' prefix if present
        if (!color_str.empty() && color_str[0] == '#') {
            color_str = color_str.substr(1);
        }
        try {
            gate->color_rgb = std::stoul(color_str, nullptr, 16);
        } catch (...) {
            // Keep existing color on parse failure
        }
    }

    // Parse material
    if (data.contains("material") && data["material"].is_string()) {
        gate->material = data["material"].get<std::string>();
    }

    // Parse Spoolman ID
    if (data.contains("spool_id") && data["spool_id"].is_number_integer()) {
        gate->spoolman_id = data["spool_id"].get<int>();
    }

    // Parse weight
    if (data.contains("weight") && data["weight"].is_number()) {
        gate->remaining_weight_g = data["weight"].get<float>();
    }

    // Derive gate status from sensors and status string
    bool tool_loaded = false;
    if (data.contains("tool_loaded") && data["tool_loaded"].is_boolean()) {
        tool_loaded = data["tool_loaded"].get<bool>();
    }

    std::string status_str;
    if (data.contains("status") && data["status"].is_string()) {
        status_str = data["status"].get<std::string>();
    }

    if (status_str == "Loaded" || tool_loaded) {
        gate->status = GateStatus::LOADED;
    } else if (sensors.prep || sensors.load) {
        gate->status = GateStatus::AVAILABLE;
    } else if (status_str == "None" || status_str.empty()) {
        gate->status = GateStatus::EMPTY;
    } else {
        gate->status = GateStatus::AVAILABLE; // Default for other states like "Ready"
    }

    spdlog::trace("[AMS AFC] Lane {} (gate {}): prep={} load={} hub={} status={}", lane_name,
                  gate_index, sensors.prep, sensors.load, sensors.loaded_to_hub,
                  gate_status_to_string(gate->status));
}

void AmsBackendAfc::parse_afc_hub(const nlohmann::json& data) {
    // Parse AFC_hub object for hub sensor state
    // { "state": true }

    if (data.contains("state") && data["state"].is_boolean()) {
        hub_sensor_ = data["state"].get<bool>();
        spdlog::trace("[AMS AFC] Hub sensor: {}", hub_sensor_);
    }
}

void AmsBackendAfc::parse_afc_extruder(const nlohmann::json& data) {
    // Parse AFC_extruder object for toolhead sensors
    // {
    //   "tool_start_status": true,   // Toolhead entry sensor
    //   "tool_end_status": false,    // Toolhead exit/nozzle sensor
    //   "lane_loaded": "lane1"       // Currently loaded lane
    // }

    if (data.contains("tool_start_status") && data["tool_start_status"].is_boolean()) {
        tool_start_sensor_ = data["tool_start_status"].get<bool>();
    }

    if (data.contains("tool_end_status") && data["tool_end_status"].is_boolean()) {
        tool_end_sensor_ = data["tool_end_status"].get<bool>();
    }

    if (data.contains("lane_loaded") && !data["lane_loaded"].is_null()) {
        if (data["lane_loaded"].is_string()) {
            current_lane_name_ = data["lane_loaded"].get<std::string>();
            // Update current_gate from lane name
            auto it = lane_name_to_index_.find(current_lane_name_);
            if (it != lane_name_to_index_.end()) {
                system_info_.current_gate = it->second;
            }
        }
    }

    spdlog::trace("[AMS AFC] Extruder: tool_start={} tool_end={} lane={}", tool_start_sensor_,
                  tool_end_sensor_, current_lane_name_);
}

// ============================================================================
// Version Detection
// ============================================================================

void AmsBackendAfc::detect_afc_version() {
    if (!client_) {
        spdlog::warn("[AMS AFC] Cannot detect version: client is null");
        return;
    }

    // Query Moonraker database for AFC install version
    // Method: server.database.get_item
    // Namespace: afc-install (contains {"version": "1.0.0"})
    nlohmann::json params = {{"namespace", "afc-install"}};

    client_->send_jsonrpc(
        "server.database.get_item", params,
        [this](const nlohmann::json& response) {
            bool should_query_lane_data = false;

            if (response.contains("value") && response["value"].is_object()) {
                const auto& value = response["value"];
                if (value.contains("version") && value["version"].is_string()) {
                    {
                        std::lock_guard<std::recursive_mutex> lock(mutex_);
                        afc_version_ = value["version"].get<std::string>();
                        system_info_.version = afc_version_;

                        // Set capability flags based on version
                        has_lane_data_db_ = version_at_least("1.0.32");
                        should_query_lane_data = has_lane_data_db_;
                    }
                    spdlog::info("[AMS AFC] Detected AFC version: {} (lane_data DB: {})",
                                 afc_version_, has_lane_data_db_ ? "yes" : "no");
                }
            }

            // For v1.0.32+, query lane_data database for richer data
            // This supplements the basic lane info from printer.objects.list
            if (should_query_lane_data) {
                query_lane_data();
            }
        },
        [this](const MoonrakerError& err) {
            spdlog::warn("[AMS AFC] Could not detect AFC version: {}", err.message);
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            afc_version_ = "unknown";
            system_info_.version = "unknown";
            // Don't query lane_data - we'll rely on discovered lanes from capabilities
        });
}

bool AmsBackendAfc::version_at_least(const std::string& required) const {
    // Parse semantic version strings (e.g., "1.0.32")
    // Returns true if afc_version_ >= required

    if (afc_version_ == "unknown" || afc_version_.empty()) {
        return false;
    }

    auto parse_version = [](const std::string& v) -> std::tuple<int, int, int> {
        int major = 0, minor = 0, patch = 0;
        std::istringstream iss(v);
        char dot;
        iss >> major >> dot >> minor >> dot >> patch;
        return {major, minor, patch};
    };

    auto [cur_maj, cur_min, cur_patch] = parse_version(afc_version_);
    auto [req_maj, req_min, req_patch] = parse_version(required);

    if (cur_maj != req_maj)
        return cur_maj > req_maj;
    if (cur_min != req_min)
        return cur_min > req_min;
    return cur_patch >= req_patch;
}

// ============================================================================
// Initial State Query
// ============================================================================

void AmsBackendAfc::query_initial_state() {
    if (!client_) {
        spdlog::warn("[AMS AFC] Cannot query initial state: client is null");
        return;
    }

    // Build list of AFC objects to query
    // We need to get the current state since we were created after the subscription
    // response was processed
    nlohmann::json objects_to_query;

    // Add main AFC object
    objects_to_query["AFC"] = nullptr;

    // Add AFC_stepper objects for each lane
    for (const auto& lane_name : lane_names_) {
        std::string key = "AFC_stepper " + lane_name;
        objects_to_query[key] = nullptr;
    }

    // Add AFC_hub objects
    for (const auto& hub_name : hub_names_) {
        std::string key = "AFC_hub " + hub_name;
        objects_to_query[key] = nullptr;
    }

    // Add AFC_extruder
    objects_to_query["AFC_extruder extruder"] = nullptr;

    nlohmann::json params = {{"objects", objects_to_query}};

    spdlog::debug("[AMS AFC] Querying initial state for {} objects", objects_to_query.size());

    client_->send_jsonrpc(
        "printer.objects.query", params,
        [this](const nlohmann::json& response) {
            // Response structure: {"jsonrpc": "2.0", "result": {"eventtime": ..., "status": {...}},
            // "id": ...}
            if (response.contains("result") && response["result"].contains("status") &&
                response["result"]["status"].is_object()) {
                // The status object format is the same as notify_status_update params
                // Wrap it in a format that handle_status_update expects
                nlohmann::json notification = {
                    {"params", nlohmann::json::array({response["result"]["status"]})}};
                handle_status_update(notification);
                spdlog::info("[AMS AFC] Initial state loaded");
            } else {
                spdlog::warn("[AMS AFC] Initial state query returned unexpected format");
            }
        },
        [](const MoonrakerError& err) {
            spdlog::warn("[AMS AFC] Failed to query initial state: {}", err.message);
        });
}

// ============================================================================
// Lane Data Queries
// ============================================================================

void AmsBackendAfc::query_lane_data() {
    if (!client_) {
        spdlog::warn("[AMS AFC] Cannot query lane data: client is null");
        return;
    }

    // Query Moonraker database for AFC lane_data
    // Method: server.database.get_item
    // Params: { "namespace": "AFC", "key": "lane_data" }
    nlohmann::json params = {{"namespace", "AFC"}, {"key", "lane_data"}};

    client_->send_jsonrpc(
        "server.database.get_item", params,
        [this](const nlohmann::json& response) {
            if (response.contains("value") && response["value"].is_object()) {
                {
                    std::lock_guard<std::recursive_mutex> lock(mutex_);
                    parse_lane_data(response["value"]);
                }
                // Emit OUTSIDE the lock to avoid deadlock with callbacks
                emit_event(EVENT_STATE_CHANGED);
            }
        },
        [](const MoonrakerError& err) {
            spdlog::warn("[AMS AFC] Failed to query lane_data: {}", err.message);
        });
}

void AmsBackendAfc::parse_lane_data(const nlohmann::json& lane_data) {
    // Lane data format:
    // {
    //   "lane1": {"color": "FF0000", "material": "PLA", "loaded": false, ...},
    //   "lane2": {"color": "00FF00", "material": "PETG", "loaded": true, ...}
    // }

    // Extract lane names and sort them for consistent ordering
    std::vector<std::string> new_lane_names;
    for (auto it = lane_data.begin(); it != lane_data.end(); ++it) {
        new_lane_names.push_back(it.key());
    }
    std::sort(new_lane_names.begin(), new_lane_names.end());

    // Initialize lanes if this is the first time or count changed
    if (!lanes_initialized_ || new_lane_names.size() != lane_names_.size()) {
        initialize_lanes(new_lane_names);
    }

    // Update lane information
    for (size_t i = 0; i < lane_names_.size() && !system_info_.units.empty(); ++i) {
        const std::string& lane_name = lane_names_[i];
        if (!lane_data.contains(lane_name) || !lane_data[lane_name].is_object()) {
            continue;
        }

        const auto& lane = lane_data[lane_name];
        auto& gate = system_info_.units[0].gates[i];

        // Parse color (AFC uses hex string without 0x prefix)
        if (lane.contains("color") && lane["color"].is_string()) {
            std::string color_str = lane["color"].get<std::string>();
            try {
                gate.color_rgb = static_cast<uint32_t>(std::stoul(color_str, nullptr, 16));
            } catch (...) {
                gate.color_rgb = AMS_DEFAULT_GATE_COLOR;
            }
        }

        // Parse material
        if (lane.contains("material") && lane["material"].is_string()) {
            gate.material = lane["material"].get<std::string>();
        }

        // Parse loaded state
        if (lane.contains("loaded") && lane["loaded"].is_boolean()) {
            bool loaded = lane["loaded"].get<bool>();
            if (loaded) {
                gate.status = GateStatus::LOADED;
                system_info_.current_gate = static_cast<int>(i);
                system_info_.filament_loaded = true;
            } else {
                // Check if filament is available (not loaded but present)
                if (lane.contains("available") && lane["available"].is_boolean() &&
                    lane["available"].get<bool>()) {
                    gate.status = GateStatus::AVAILABLE;
                } else if (lane.contains("empty") && lane["empty"].is_boolean() &&
                           lane["empty"].get<bool>()) {
                    gate.status = GateStatus::EMPTY;
                } else {
                    // Default to available if not explicitly empty
                    gate.status = GateStatus::AVAILABLE;
                }
            }
        }

        // Parse spool information if available
        if (lane.contains("spool_id") && lane["spool_id"].is_number_integer()) {
            gate.spoolman_id = lane["spool_id"].get<int>();
        }

        if (lane.contains("brand") && lane["brand"].is_string()) {
            gate.brand = lane["brand"].get<std::string>();
        }

        if (lane.contains("remaining_weight") && lane["remaining_weight"].is_number()) {
            gate.remaining_weight_g = lane["remaining_weight"].get<float>();
        }

        if (lane.contains("total_weight") && lane["total_weight"].is_number()) {
            gate.total_weight_g = lane["total_weight"].get<float>();
        }
    }
}

void AmsBackendAfc::initialize_lanes(const std::vector<std::string>& lane_names) {
    int lane_count = static_cast<int>(lane_names.size());
    spdlog::info("[AMS AFC] Initializing {} lanes", lane_count);

    lane_names_ = lane_names;

    // Build lane name to index mapping
    lane_name_to_index_.clear();
    for (size_t i = 0; i < lane_names_.size(); ++i) {
        lane_name_to_index_[lane_names_[i]] = static_cast<int>(i);
    }

    // Create a single unit with all lanes (AFC units are typically treated as one logical unit)
    AmsUnit unit;
    unit.unit_index = 0;
    unit.name = "AFC Box Turtle";
    unit.gate_count = lane_count;
    unit.first_gate_global_index = 0;
    unit.connected = true;
    unit.has_encoder = false;        // AFC typically uses optical sensors, not encoders
    unit.has_toolhead_sensor = true; // Most AFC setups have toolhead sensor
    unit.has_gate_sensors = true;    // AFC has per-lane sensors

    // Initialize gates with defaults
    for (int i = 0; i < lane_count; ++i) {
        GateInfo gate;
        gate.gate_index = i;
        gate.global_index = i;
        gate.status = GateStatus::UNKNOWN;
        gate.mapped_tool = i; // Default 1:1 mapping
        gate.color_rgb = AMS_DEFAULT_GATE_COLOR;
        unit.gates.push_back(gate);
    }

    system_info_.units.clear();
    system_info_.units.push_back(unit);
    system_info_.total_gates = lane_count;

    // Initialize tool-to-gate mapping (1:1 default)
    system_info_.tool_to_gate_map.clear();
    system_info_.tool_to_gate_map.reserve(lane_count);
    for (int i = 0; i < lane_count; ++i) {
        system_info_.tool_to_gate_map.push_back(i);
    }

    lanes_initialized_ = true;
}

std::string AmsBackendAfc::get_lane_name(int gate_index) const {
    if (gate_index >= 0 && gate_index < static_cast<int>(lane_names_.size())) {
        return lane_names_[gate_index];
    }
    return "";
}

// ============================================================================
// Filament Operations
// ============================================================================

AmsError AmsBackendAfc::check_preconditions() const {
    if (!running_) {
        return AmsErrorHelper::not_connected("AFC backend not started");
    }

    if (system_info_.is_busy()) {
        return AmsErrorHelper::busy(ams_action_to_string(system_info_.action));
    }

    return AmsErrorHelper::success();
}

AmsError AmsBackendAfc::validate_gate_index(int gate_index) const {
    if (gate_index < 0 || gate_index >= system_info_.total_gates) {
        return AmsErrorHelper::invalid_gate(gate_index, system_info_.total_gates - 1);
    }
    return AmsErrorHelper::success();
}

AmsError AmsBackendAfc::execute_gcode(const std::string& gcode) {
    if (!api_) {
        return AmsErrorHelper::not_connected("MoonrakerAPI not available");
    }

    spdlog::info("[AMS AFC] Executing G-code: {}", gcode);

    // Execute G-code asynchronously via MoonrakerAPI
    api_->execute_gcode(
        gcode, []() { spdlog::debug("[AMS AFC] G-code executed successfully"); },
        [gcode](const MoonrakerError& err) {
            spdlog::error("[AMS AFC] G-code failed: {} - {}", gcode, err.message);
        });

    return AmsErrorHelper::success();
}

AmsError AmsBackendAfc::load_filament(int gate_index) {
    std::string lane_name;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        AmsError gate_valid = validate_gate_index(gate_index);
        if (!gate_valid) {
            return gate_valid;
        }

        // Check if lane has filament available
        const auto* gate = system_info_.get_gate_global(gate_index);
        if (gate && gate->status == GateStatus::EMPTY) {
            return AmsErrorHelper::gate_not_available(gate_index);
        }

        lane_name = get_lane_name(gate_index);
        if (lane_name.empty()) {
            return AmsErrorHelper::invalid_gate(gate_index, system_info_.total_gates - 1);
        }
    }

    // Send AFC_LOAD LANE={name} command
    std::ostringstream cmd;
    cmd << "AFC_LOAD LANE=" << lane_name;

    spdlog::info("[AMS AFC] Loading from lane {} (gate {})", lane_name, gate_index);
    return execute_gcode(cmd.str());
}

AmsError AmsBackendAfc::unload_filament() {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        if (!system_info_.filament_loaded) {
            return AmsError(AmsResult::WRONG_STATE, "No filament loaded", "No filament to unload",
                            "Load filament first");
        }
    }

    spdlog::info("[AMS AFC] Unloading filament");
    return execute_gcode("AFC_UNLOAD");
}

AmsError AmsBackendAfc::select_gate(int gate_index) {
    std::string lane_name;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        AmsError gate_valid = validate_gate_index(gate_index);
        if (!gate_valid) {
            return gate_valid;
        }

        lane_name = get_lane_name(gate_index);
        if (lane_name.empty()) {
            return AmsErrorHelper::invalid_gate(gate_index, system_info_.total_gates - 1);
        }
    }

    // AFC may not have a direct "select without load" command
    // Some AFC configurations use AFC_SELECT, others may require different approach
    std::ostringstream cmd;
    cmd << "AFC_SELECT LANE=" << lane_name;

    spdlog::info("[AMS AFC] Selecting lane {} (gate {})", lane_name, gate_index);
    return execute_gcode(cmd.str());
}

AmsError AmsBackendAfc::change_tool(int tool_number) {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        if (tool_number < 0 ||
            tool_number >= static_cast<int>(system_info_.tool_to_gate_map.size())) {
            return AmsError(AmsResult::INVALID_TOOL,
                            "Tool " + std::to_string(tool_number) + " out of range",
                            "Invalid tool number", "Select a valid tool");
        }
    }

    // Send T{n} command for standard tool change
    std::ostringstream cmd;
    cmd << "T" << tool_number;

    spdlog::info("[AMS AFC] Tool change to T{}", tool_number);
    return execute_gcode(cmd.str());
}

// ============================================================================
// Recovery Operations
// ============================================================================

AmsError AmsBackendAfc::recover() {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("AFC backend not started");
        }
    }

    // AFC may use AFC_RESET or AFC_RECOVER for error recovery
    spdlog::info("[AMS AFC] Initiating recovery");
    return execute_gcode("AFC_RESET");
}

AmsError AmsBackendAfc::reset() {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }
    }

    spdlog::info("[AMS AFC] Resetting AFC system");
    return execute_gcode("AFC_RESET");
}

AmsError AmsBackendAfc::cancel() {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("AFC backend not started");
        }

        if (system_info_.action == AmsAction::IDLE) {
            return AmsErrorHelper::success(); // Nothing to cancel
        }
    }

    // AFC may use AFC_ABORT or AFC_CANCEL to stop current operation
    spdlog::info("[AMS AFC] Cancelling current operation");
    return execute_gcode("AFC_ABORT");
}

// ============================================================================
// Configuration Operations
// ============================================================================

AmsError AmsBackendAfc::set_gate_info(int gate_index, const GateInfo& info) {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        if (gate_index < 0 || gate_index >= system_info_.total_gates) {
            return AmsErrorHelper::invalid_gate(gate_index, system_info_.total_gates - 1);
        }

        auto* gate = system_info_.get_gate_global(gate_index);
        if (!gate) {
            return AmsErrorHelper::invalid_gate(gate_index, system_info_.total_gates - 1);
        }

        // Update local state
        gate->color_name = info.color_name;
        gate->color_rgb = info.color_rgb;
        gate->material = info.material;
        gate->brand = info.brand;
        gate->spoolman_id = info.spoolman_id;
        gate->spool_name = info.spool_name;
        gate->remaining_weight_g = info.remaining_weight_g;
        gate->total_weight_g = info.total_weight_g;
        gate->nozzle_temp_min = info.nozzle_temp_min;
        gate->nozzle_temp_max = info.nozzle_temp_max;
        gate->bed_temp = info.bed_temp;

        spdlog::info("[AMS AFC] Updated gate {} info: {} {}", gate_index, info.material,
                     info.color_name);
    }

    // Emit OUTSIDE the lock to avoid deadlock with callbacks
    emit_event(EVENT_GATE_CHANGED, std::to_string(gate_index));

    // AFC stores lane info in Moonraker database
    // This could be extended to persist changes via server.database.post_item
    // For now, we just update local state

    return AmsErrorHelper::success();
}

AmsError AmsBackendAfc::set_tool_mapping(int tool_number, int gate_index) {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        if (tool_number < 0 ||
            tool_number >= static_cast<int>(system_info_.tool_to_gate_map.size())) {
            return AmsError(AmsResult::INVALID_TOOL,
                            "Tool " + std::to_string(tool_number) + " out of range",
                            "Invalid tool number", "");
        }

        if (gate_index < 0 || gate_index >= system_info_.total_gates) {
            return AmsErrorHelper::invalid_gate(gate_index, system_info_.total_gates - 1);
        }

        // Update local mapping
        system_info_.tool_to_gate_map[tool_number] = gate_index;

        // Update gate's mapped_tool reference
        for (auto& unit : system_info_.units) {
            for (auto& gate : unit.gates) {
                if (gate.mapped_tool == tool_number) {
                    gate.mapped_tool = -1; // Clear old mapping
                }
            }
        }
        auto* gate = system_info_.get_gate_global(gate_index);
        if (gate) {
            gate->mapped_tool = tool_number;
        }
    }

    // AFC may use a G-code command to set tool mapping
    // This varies by AFC version/configuration
    std::string lane_name = get_lane_name(gate_index);
    if (!lane_name.empty()) {
        std::ostringstream cmd;
        cmd << "AFC_MAP TOOL=" << tool_number << " LANE=" << lane_name;
        spdlog::info("[AMS AFC] Mapping T{} to lane {} (gate {})", tool_number, lane_name,
                     gate_index);
        return execute_gcode(cmd.str());
    }

    return AmsErrorHelper::success();
}

// ============================================================================
// Bypass Mode Operations
// ============================================================================

AmsError AmsBackendAfc::enable_bypass() {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        if (!system_info_.supports_bypass) {
            return AmsError(AmsResult::WRONG_STATE, "Bypass not supported",
                            "This AFC system does not support bypass mode", "");
        }
    }

    // AFC enables bypass via filament sensor control
    // SET_FILAMENT_SENSOR SENSOR=bypass ENABLE=1
    spdlog::info("[AMS AFC] Enabling bypass mode");
    return execute_gcode("SET_FILAMENT_SENSOR SENSOR=bypass ENABLE=1");
}

AmsError AmsBackendAfc::disable_bypass() {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("AFC backend not started");
        }

        if (!bypass_active_) {
            return AmsError(AmsResult::WRONG_STATE, "Bypass not active",
                            "Bypass mode is not currently active", "");
        }
    }

    // Disable bypass sensor
    spdlog::info("[AMS AFC] Disabling bypass mode");
    return execute_gcode("SET_FILAMENT_SENSOR SENSOR=bypass ENABLE=0");
}

bool AmsBackendAfc::is_bypass_active() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return bypass_active_;
}
