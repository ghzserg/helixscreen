// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_afc.h"

#include "moonraker_api.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

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
    system_info_.current_slot = -1;
    system_info_.filament_loaded = false;
    system_info_.action = AmsAction::IDLE;
    system_info_.total_slots = 0;
    // AFC capabilities - may vary by configuration
    system_info_.supports_endless_spool = true;
    system_info_.supports_spoolman = true;
    system_info_.supports_tool_mapping = true;
    system_info_.supports_bypass = true; // AFC supports bypass via bypass_state
    // Default to hardware sensor - AFC BoxTurtle typically has physical bypass sensor
    // TODO: Detect from AFC configuration whether bypass sensor is virtual or hardware
    system_info_.has_hardware_bypass_sensor = true;

    spdlog::debug("[AMS AFC] Backend created");
}

AmsBackendAfc::~AmsBackendAfc() {
    // During static destruction (e.g., program exit), the mutex and client may be
    // in an invalid state. Release the subscription guard WITHOUT trying to
    // unsubscribe - the MoonrakerClient may already be destroyed.
    subscription_.release();
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

SlotInfo AmsBackendAfc::get_slot_info(int slot_index) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    const auto* slot = system_info_.get_slot_global(slot_index);
    if (slot) {
        return *slot;
    }

    // Return empty slot info for invalid index
    SlotInfo empty;
    empty.slot_index = -1;
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

int AmsBackendAfc::get_current_slot() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return system_info_.current_slot;
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

PathSegment AmsBackendAfc::get_slot_filament_segment(int slot_index) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Check if this is the active slot - return the current filament segment
    if (slot_index == system_info_.current_slot && system_info_.filament_loaded) {
        return compute_filament_segment_unlocked();
    }

    // For non-active slots, check lane sensors to determine filament position
    if (slot_index < 0 || slot_index >= static_cast<int>(lane_sensors_.size())) {
        return PathSegment::NONE;
    }

    const LaneSensors& sensors = lane_sensors_[slot_index];

    // Check sensors from furthest to nearest
    if (sensors.loaded_to_hub) {
        return PathSegment::HUB; // Filament reached hub sensor
    }
    if (sensors.load) {
        return PathSegment::LANE; // Filament in lane (load sensor triggered)
    }
    if (sensors.prep) {
        return PathSegment::PREP; // Filament at prep sensor
    }

    // Check slot status - if available, assume filament at spool
    const SlotInfo* slot = system_info_.get_slot_global(slot_index);
    if (slot &&
        (slot->status == SlotStatus::AVAILABLE || slot->status == SlotStatus::FROM_BUFFER)) {
        return PathSegment::SPOOL;
    }

    return PathSegment::NONE;
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
    if (system_info_.filament_loaded || system_info_.current_slot >= 0) {
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
    // Parse current lane (AFC reports this as "current_lane")
    if (afc_data.contains("current_lane") && afc_data["current_lane"].is_string()) {
        std::string lane_name = afc_data["current_lane"].get<std::string>();
        auto it = lane_name_to_index_.find(lane_name);
        if (it != lane_name_to_index_.end()) {
            system_info_.current_slot = it->second;
            spdlog::trace("[AMS AFC] Current lane: {} (slot {})", lane_name,
                          system_info_.current_slot);
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
            system_info_.current_slot = -2; // -2 = bypass mode
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
    int slot_index = it->second;

    if (slot_index < 0 || slot_index >= static_cast<int>(lane_sensors_.size())) {
        return;
    }

    // Update sensor state for this lane
    LaneSensors& sensors = lane_sensors_[slot_index];
    if (data.contains("prep") && data["prep"].is_boolean()) {
        sensors.prep = data["prep"].get<bool>();
    }
    if (data.contains("load") && data["load"].is_boolean()) {
        sensors.load = data["load"].get<bool>();
    }
    if (data.contains("loaded_to_hub") && data["loaded_to_hub"].is_boolean()) {
        sensors.loaded_to_hub = data["loaded_to_hub"].get<bool>();
    }

    // Get slot info for filament data update
    SlotInfo* slot = system_info_.get_slot_global(slot_index);
    if (!slot)
        return;

    // Parse color
    if (data.contains("color") && data["color"].is_string()) {
        std::string color_str = data["color"].get<std::string>();
        // Remove '#' prefix if present
        if (!color_str.empty() && color_str[0] == '#') {
            color_str = color_str.substr(1);
        }
        try {
            slot->color_rgb = std::stoul(color_str, nullptr, 16);
        } catch (...) {
            // Keep existing color on parse failure
        }
    }

    // Parse material
    if (data.contains("material") && data["material"].is_string()) {
        slot->material = data["material"].get<std::string>();
    }

    // Parse Spoolman ID
    if (data.contains("spool_id") && data["spool_id"].is_number_integer()) {
        slot->spoolman_id = data["spool_id"].get<int>();
    }

    // Parse weight
    if (data.contains("weight") && data["weight"].is_number()) {
        slot->remaining_weight_g = data["weight"].get<float>();
    }

    // Derive slot status from sensors and status string
    bool tool_loaded = false;
    if (data.contains("tool_loaded") && data["tool_loaded"].is_boolean()) {
        tool_loaded = data["tool_loaded"].get<bool>();
    }

    std::string status_str;
    if (data.contains("status") && data["status"].is_string()) {
        status_str = data["status"].get<std::string>();
    }

    if (status_str == "Loaded" || tool_loaded) {
        slot->status = SlotStatus::LOADED;
    } else if (sensors.prep || sensors.load) {
        slot->status = SlotStatus::AVAILABLE;
    } else if (status_str == "None" || status_str.empty()) {
        slot->status = SlotStatus::EMPTY;
    } else {
        slot->status = SlotStatus::AVAILABLE; // Default for other states like "Ready"
    }

    spdlog::trace("[AMS AFC] Lane {} (slot {}): prep={} load={} hub={} status={}", lane_name,
                  slot_index, sensors.prep, sensors.load, sensors.loaded_to_hub,
                  slot_status_to_string(slot->status));
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
                system_info_.current_slot = it->second;
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
        auto& slot = system_info_.units[0].slots[i];

        // Parse color (AFC uses hex string without 0x prefix)
        if (lane.contains("color") && lane["color"].is_string()) {
            std::string color_str = lane["color"].get<std::string>();
            try {
                slot.color_rgb = static_cast<uint32_t>(std::stoul(color_str, nullptr, 16));
            } catch (...) {
                slot.color_rgb = AMS_DEFAULT_SLOT_COLOR;
            }
        }

        // Parse material
        if (lane.contains("material") && lane["material"].is_string()) {
            slot.material = lane["material"].get<std::string>();
        }

        // Parse loaded state
        if (lane.contains("loaded") && lane["loaded"].is_boolean()) {
            bool loaded = lane["loaded"].get<bool>();
            if (loaded) {
                slot.status = SlotStatus::LOADED;
                system_info_.current_slot = static_cast<int>(i);
                system_info_.filament_loaded = true;
            } else {
                // Check if filament is available (not loaded but present)
                if (lane.contains("available") && lane["available"].is_boolean() &&
                    lane["available"].get<bool>()) {
                    slot.status = SlotStatus::AVAILABLE;
                } else if (lane.contains("empty") && lane["empty"].is_boolean() &&
                           lane["empty"].get<bool>()) {
                    slot.status = SlotStatus::EMPTY;
                } else {
                    // Default to available if not explicitly empty
                    slot.status = SlotStatus::AVAILABLE;
                }
            }
        }

        // Parse spool information if available
        if (lane.contains("spool_id") && lane["spool_id"].is_number_integer()) {
            slot.spoolman_id = lane["spool_id"].get<int>();
        }

        if (lane.contains("brand") && lane["brand"].is_string()) {
            slot.brand = lane["brand"].get<std::string>();
        }

        if (lane.contains("remaining_weight") && lane["remaining_weight"].is_number()) {
            slot.remaining_weight_g = lane["remaining_weight"].get<float>();
        }

        if (lane.contains("total_weight") && lane["total_weight"].is_number()) {
            slot.total_weight_g = lane["total_weight"].get<float>();
        }
    }
}

void AmsBackendAfc::initialize_lanes(const std::vector<std::string>& lane_names) {
    int lane_count = static_cast<int>(lane_names.size());
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
    unit.slot_count = lane_count;
    unit.first_slot_global_index = 0;
    unit.connected = true;
    unit.has_encoder = false;        // AFC typically uses optical sensors, not encoders
    unit.has_toolhead_sensor = true; // Most AFC setups have toolhead sensor
    unit.has_slot_sensors = true;    // AFC has per-lane sensors

    // Initialize gates with defaults
    for (int i = 0; i < lane_count; ++i) {
        SlotInfo slot;
        slot.slot_index = i;
        slot.global_index = i;
        slot.status = SlotStatus::UNKNOWN;
        slot.mapped_tool = i; // Default 1:1 mapping
        slot.color_rgb = AMS_DEFAULT_SLOT_COLOR;
        unit.slots.push_back(slot);
    }

    system_info_.units.clear();
    system_info_.units.push_back(unit);
    system_info_.total_slots = lane_count;

    // Initialize tool-to-lane mapping (1:1 default)
    system_info_.tool_to_slot_map.clear();
    system_info_.tool_to_slot_map.reserve(lane_count);
    for (int i = 0; i < lane_count; ++i) {
        system_info_.tool_to_slot_map.push_back(i);
    }

    // Initialize endless spool configs (no backup by default)
    endless_spool_configs_.clear();
    endless_spool_configs_.reserve(lane_count);
    for (int i = 0; i < lane_count; ++i) {
        helix::printer::EndlessSpoolConfig config;
        config.slot_index = i;
        config.backup_slot = -1; // No backup by default
        endless_spool_configs_.push_back(config);
    }

    lanes_initialized_ = true;
}

std::string AmsBackendAfc::get_lane_name(int slot_index) const {
    if (slot_index >= 0 && slot_index < static_cast<int>(lane_names_.size())) {
        return lane_names_[slot_index];
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

AmsError AmsBackendAfc::validate_slot_index(int slot_index) const {
    if (slot_index < 0 || slot_index >= system_info_.total_slots) {
        return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
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

AmsError AmsBackendAfc::load_filament(int slot_index) {
    std::string lane_name;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        AmsError gate_valid = validate_slot_index(slot_index);
        if (!gate_valid) {
            return gate_valid;
        }

        // Check if lane has filament available
        const auto* slot = system_info_.get_slot_global(slot_index);
        if (slot && slot->status == SlotStatus::EMPTY) {
            return AmsErrorHelper::slot_not_available(slot_index);
        }

        lane_name = get_lane_name(slot_index);
        if (lane_name.empty()) {
            return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
        }
    }

    // Send AFC_LOAD LANE={name} command
    std::ostringstream cmd;
    cmd << "AFC_LOAD LANE=" << lane_name;

    spdlog::info("[AMS AFC] Loading from lane {} (slot {})", lane_name, slot_index);
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

AmsError AmsBackendAfc::select_slot(int slot_index) {
    std::string lane_name;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        AmsError gate_valid = validate_slot_index(slot_index);
        if (!gate_valid) {
            return gate_valid;
        }

        lane_name = get_lane_name(slot_index);
        if (lane_name.empty()) {
            return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
        }
    }

    // AFC may not have a direct "select without load" command
    // Some AFC configurations use AFC_SELECT, others may require different approach
    std::ostringstream cmd;
    cmd << "AFC_SELECT LANE=" << lane_name;

    spdlog::info("[AMS AFC] Selecting lane {} (slot {})", lane_name, slot_index);
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
            tool_number >= static_cast<int>(system_info_.tool_to_slot_map.size())) {
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

AmsError AmsBackendAfc::set_slot_info(int slot_index, const SlotInfo& info) {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        if (slot_index < 0 || slot_index >= system_info_.total_slots) {
            return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
        }

        auto* slot = system_info_.get_slot_global(slot_index);
        if (!slot) {
            return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
        }

        // Capture old spoolman_id before updating for clear detection
        int old_spoolman_id = slot->spoolman_id;

        // Update local state
        slot->color_name = info.color_name;
        slot->color_rgb = info.color_rgb;
        slot->material = info.material;
        slot->brand = info.brand;
        slot->spoolman_id = info.spoolman_id;
        slot->spool_name = info.spool_name;
        slot->remaining_weight_g = info.remaining_weight_g;
        slot->total_weight_g = info.total_weight_g;
        slot->nozzle_temp_min = info.nozzle_temp_min;
        slot->nozzle_temp_max = info.nozzle_temp_max;
        slot->bed_temp = info.bed_temp;

        spdlog::info("[AMS AFC] Updated slot {} info: {} {}", slot_index, info.material,
                     info.color_name);

        // Persist via G-code commands if AFC version supports it (v1.0.20+)
        if (version_at_least("1.0.20")) {
            std::string lane_name = get_lane_name(slot_index);
            if (!lane_name.empty()) {
                // Color (only if changed and valid - not 0 or default grey)
                if (info.color_rgb != 0 && info.color_rgb != AMS_DEFAULT_SLOT_COLOR) {
                    char color_hex[8];
                    snprintf(color_hex, sizeof(color_hex), "%06X", info.color_rgb & 0xFFFFFF);
                    execute_gcode(fmt::format("SET_COLOR LANE={} COLOR={}", lane_name, color_hex));
                }

                // Material (validate to prevent command injection)
                if (!info.material.empty() && MoonrakerAPI::is_safe_gcode_param(info.material)) {
                    execute_gcode(
                        fmt::format("SET_MATERIAL LANE={} MATERIAL={}", lane_name, info.material));
                } else if (!info.material.empty()) {
                    spdlog::warn("[AMS AFC] Skipping SET_MATERIAL - unsafe characters in: {}",
                                 info.material);
                }

                // Weight (if valid)
                if (info.remaining_weight_g > 0) {
                    execute_gcode(fmt::format("SET_WEIGHT LANE={} WEIGHT={:.0f}", lane_name,
                                              info.remaining_weight_g));
                }

                // Spoolman ID
                if (info.spoolman_id > 0) {
                    execute_gcode(fmt::format("SET_SPOOL_ID LANE={} SPOOL_ID={}", lane_name,
                                              info.spoolman_id));
                } else if (info.spoolman_id == 0 && old_spoolman_id > 0) {
                    // Clear Spoolman link with empty string (not -1)
                    execute_gcode(fmt::format("SET_SPOOL_ID LANE={} SPOOL_ID=", lane_name));
                }
            }
        } else if (afc_version_ != "unknown" && !afc_version_.empty()) {
            spdlog::info("[AMS AFC] Version {} - slot changes stored locally only (upgrade to "
                         "1.0.20+ for persistence)",
                         afc_version_);
        }
    }

    // Emit OUTSIDE the lock to avoid deadlock with callbacks
    emit_event(EVENT_SLOT_CHANGED, std::to_string(slot_index));

    return AmsErrorHelper::success();
}

AmsError AmsBackendAfc::set_tool_mapping(int tool_number, int slot_index) {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        if (tool_number < 0 ||
            tool_number >= static_cast<int>(system_info_.tool_to_slot_map.size())) {
            return AmsError(AmsResult::INVALID_TOOL,
                            "Tool " + std::to_string(tool_number) + " out of range",
                            "Invalid tool number", "");
        }

        if (slot_index < 0 || slot_index >= system_info_.total_slots) {
            return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
        }

        // Update local mapping
        system_info_.tool_to_slot_map[tool_number] = slot_index;

        // Update lane's mapped_tool reference
        for (auto& unit : system_info_.units) {
            for (auto& slot : unit.slots) {
                if (slot.mapped_tool == tool_number) {
                    slot.mapped_tool = -1; // Clear old mapping
                }
            }
        }
        auto* slot = system_info_.get_slot_global(slot_index);
        if (slot) {
            slot->mapped_tool = tool_number;
        }
    }

    // AFC may use a G-code command to set tool mapping
    // This varies by AFC version/configuration
    std::string lane_name = get_lane_name(slot_index);
    if (!lane_name.empty()) {
        std::ostringstream cmd;
        cmd << "AFC_MAP TOOL=" << tool_number << " LANE=" << lane_name;
        spdlog::info("[AMS AFC] Mapping T{} to lane {} (slot {})", tool_number, lane_name,
                     slot_index);
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

// ============================================================================
// Endless Spool Operations
// ============================================================================

helix::printer::EndlessSpoolCapabilities AmsBackendAfc::get_endless_spool_capabilities() const {
    // AFC supports per-slot backup configuration via SET_RUNOUT G-code
    return {true, true, "AFC per-slot backup"};
}

// ============================================================================
// Tool Mapping Operations
// ============================================================================

helix::printer::ToolMappingCapabilities AmsBackendAfc::get_tool_mapping_capabilities() const {
    // AFC supports per-lane tool assignment via SET_MAP G-code
    return {true, true, "Per-lane tool assignment via SET_MAP"};
}

std::vector<int> AmsBackendAfc::get_tool_mapping() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return system_info_.tool_to_slot_map;
}

std::vector<helix::printer::EndlessSpoolConfig> AmsBackendAfc::get_endless_spool_config() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return endless_spool_configs_;
}

AmsError AmsBackendAfc::set_endless_spool_backup(int slot_index, int backup_slot) {
    std::string lane_name;
    std::string backup_lane_name;
    int lane_count = 0;

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        lane_count = static_cast<int>(lane_names_.size());

        // Validate slot_index (0 to lane_names_.size()-1)
        if (slot_index < 0 || slot_index >= lane_count) {
            return AmsErrorHelper::invalid_slot(slot_index, lane_count > 0 ? lane_count - 1 : 0);
        }

        // Validate backup_slot (-1 or 0 to lane_names_.size()-1, not equal to slot_index)
        if (backup_slot != -1) {
            if (backup_slot < 0 || backup_slot >= lane_count) {
                return AmsErrorHelper::invalid_slot(backup_slot,
                                                    lane_count > 0 ? lane_count - 1 : 0);
            }
            if (backup_slot == slot_index) {
                return AmsError(AmsResult::INVALID_SLOT, "Cannot use slot as its own backup",
                                "A slot cannot be set as its own endless spool backup",
                                "Select a different backup slot");
            }
        }

        // Get lane names
        lane_name = lane_names_[slot_index];
        if (backup_slot >= 0) {
            backup_lane_name = lane_names_[backup_slot];
        }

        // Update cached config
        if (slot_index < static_cast<int>(endless_spool_configs_.size())) {
            endless_spool_configs_[slot_index].backup_slot = backup_slot;
        }
    }

    // Validate lane names to prevent command injection
    if (!MoonrakerAPI::is_safe_gcode_param(lane_name)) {
        spdlog::warn("[AMS AFC] Unsafe lane name characters in endless spool config");
        return AmsError(AmsResult::MAPPING_ERROR, "Invalid lane name",
                        "Lane name contains invalid characters", "Check AFC configuration");
    }
    if (backup_slot >= 0 && !MoonrakerAPI::is_safe_gcode_param(backup_lane_name)) {
        spdlog::warn("[AMS AFC] Unsafe backup lane name characters");
        return AmsError(AmsResult::MAPPING_ERROR, "Invalid backup lane name",
                        "Backup lane name contains invalid characters", "Check AFC configuration");
    }

    // Build and send G-code command
    // SET_RUNOUT LANE={lane_name} RUNOUT_LANE={backup_lane_name}
    // If backup_slot == -1, send empty RUNOUT_LANE= to disable
    std::string gcode;
    if (backup_slot >= 0) {
        gcode = fmt::format("SET_RUNOUT LANE={} RUNOUT_LANE={}", lane_name, backup_lane_name);
        spdlog::info("[AMS AFC] Setting endless spool backup: {} -> {}", lane_name,
                     backup_lane_name);
    } else {
        gcode = fmt::format("SET_RUNOUT LANE={} RUNOUT_LANE=", lane_name);
        spdlog::info("[AMS AFC] Disabling endless spool backup for {}", lane_name);
    }

    return execute_gcode(gcode);
}
