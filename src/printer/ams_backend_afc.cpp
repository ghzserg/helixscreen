// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_afc.h"

#include "ui_error_reporting.h"
#include "ui_notification.h"

#include "action_prompt_manager.h"
#include "afc_defaults.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "moonraker_api.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <sstream>

using namespace helix;

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
    // AFC capabilities from shared defaults
    auto caps = helix::printer::afc_default_capabilities();
    system_info_.supports_endless_spool = caps.supports_endless_spool;
    system_info_.supports_tool_mapping = caps.supports_tool_mapping;
    system_info_.supports_bypass = caps.supports_bypass;
    system_info_.supports_purge = caps.supports_purge;
    system_info_.tip_method = caps.tip_method;
    // Default to hardware sensor. Actual detection happens in set_discovered_sensors()
    // which checks for "filament_switch_sensor virtual_bypass" in the Klipper objects list.
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

    // Load AFC config files for device settings
    load_afc_configs();

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

void AmsBackendAfc::set_discovered_sensors(const std::vector<std::string>& sensor_names) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Detect hardware vs virtual bypass from Klipper object names.
    // AFC creates "filament_switch_sensor virtual_bypass" when no hardware sensor is configured,
    // or uses the existing "filament_switch_sensor bypass" when hardware is present.
    bool has_virtual = false;
    bool has_hardware = false;
    for (const auto& name : sensor_names) {
        if (name == "filament_switch_sensor virtual_bypass") {
            has_virtual = true;
        } else if (name == "filament_switch_sensor bypass") {
            has_hardware = true;
        }
    }

    if (has_virtual) {
        system_info_.has_hardware_bypass_sensor = false;
        spdlog::info("[AMS AFC] Virtual bypass sensor detected");
    } else if (has_hardware) {
        system_info_.has_hardware_bypass_sensor = true;
        spdlog::info("[AMS AFC] Hardware bypass sensor detected");
    }
    // If neither found, keep the default (true — assumes hardware)
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

void AmsBackendAfc::release_subscriptions() {
    subscription_.release();
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

PathTopology AmsBackendAfc::get_unit_topology(int unit_index) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (unit_index >= 0 && unit_index < static_cast<int>(unit_infos_.size())) {
        return unit_infos_[unit_index].topology;
    }
    return get_topology(); // Fallback to system-wide topology
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

    // Check hub sensors (any hub triggered means filament past hub)
    for (const auto& [name, triggered] : hub_sensors_) {
        if (triggered) {
            return PathSegment::OUTPUT;
        }
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

        // Parse AFC_lane objects (OpenAMS lanes use this prefix instead of AFC_stepper)
        // Same JSON schema as AFC_stepper, so reuse parse_afc_stepper
        for (const auto& lane_name : lane_names_) {
            std::string key = "AFC_lane " + lane_name;
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
                parse_afc_hub(hub_name, params[key]);
                state_changed = true;
            }
        }

        // Parse AFC_extruder for toolhead sensors (multi-extruder support)
        if (!extruder_names_.empty()) {
            for (const auto& ext_name : extruder_names_) {
                std::string key = "AFC_extruder " + ext_name;
                if (params.contains(key) && params[key].is_object()) {
                    parse_afc_extruder(params[key]);
                    state_changed = true;
                }
            }
        } else {
            // Backward compat: single extruder fallback
            if (params.contains("AFC_extruder extruder") &&
                params["AFC_extruder extruder"].is_object()) {
                parse_afc_extruder(params["AFC_extruder extruder"]);
                state_changed = true;
            }
        }

        // Parse AFC_buffer objects for buffer health and fault data
        for (const auto& buf_name : buffer_names_) {
            std::string key = "AFC_buffer " + buf_name;
            if (params.contains(key) && params[key].is_object()) {
                parse_afc_buffer(buf_name, params[key]);
                state_changed = true;
            }
        }

        // Parse unit-level Klipper objects (AFC_BoxTurtle, AFC_OpenAMS)
        for (auto& unit_info : unit_infos_) {
            if (params.contains(unit_info.klipper_key) &&
                params[unit_info.klipper_key].is_object()) {
                parse_afc_unit_object(unit_info, params[unit_info.klipper_key]);
                state_changed = true;
            }
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

    // Parse current_state field (preferred over status when present)
    if (afc_data.contains("current_state") && afc_data["current_state"].is_string()) {
        std::string state_str = afc_data["current_state"].get<std::string>();
        system_info_.action = ams_action_from_string(state_str);
        system_info_.operation_detail = state_str;
        spdlog::trace("[AMS AFC] Current state: {} ({})", ams_action_to_string(system_info_.action),
                      state_str);
    }

    // Parse message object for operation detail, error events, and toast notifications
    if (afc_data.contains("message") && afc_data["message"].is_object()) {
        const auto& msg = afc_data["message"];
        if (msg.contains("message") && msg["message"].is_string()) {
            std::string msg_text = msg["message"].get<std::string>();
            if (!msg_text.empty()) {
                system_info_.operation_detail = msg_text;
            }

            // Get message type (error, warning, or empty)
            std::string msg_type;
            if (msg.contains("type") && msg["type"].is_string()) {
                msg_type = msg["type"].get<std::string>();
            }

            // Track message type for per-lane error severity mapping
            last_message_type_ = msg_type;

            // Handle message text changes for toast/notification dispatch
            if (msg_text.empty()) {
                // Error cleared - reset dedup tracking
                last_seen_message_.clear();
                last_error_msg_.clear();
                last_message_type_.clear();
            } else if (msg_text != last_seen_message_) {
                // New or changed message - update dedup tracker
                last_seen_message_ = msg_text;

                // Emit error event for backward compatibility
                if (msg_type == "error" && msg_text != last_error_msg_) {
                    last_error_msg_ = msg_text;
                    emit_event(EVENT_ERROR, msg_text);
                }

                // Determine if an AFC action:prompt is currently active
                // If so, suppress the toast (user already sees the modal) but
                // still add to notification history
                bool afc_prompt_active = helix::ActionPromptManager::is_showing() &&
                                         helix::ActionPromptManager::current_prompt_name().find(
                                             "AFC") != std::string::npos;

                if (afc_prompt_active) {
                    // Notification history only (no toast) - user already has the modal
                    spdlog::debug("[AMS AFC] Toast suppressed (AFC prompt active): {}", msg_text);
                    ui_notification_info_with_action("AFC", msg_text.c_str(), "afc_message");
                } else {
                    // Show toast based on message type
                    if (msg_type == "error") {
                        NOTIFY_ERROR_T("AFC", "{}", msg_text);
                    } else if (msg_type == "warning") {
                        NOTIFY_WARNING_T("AFC", "{}", msg_text);
                    } else {
                        NOTIFY_INFO_T("AFC", "{}", msg_text);
                    }
                }
            }
        }
    }

    // Parse current_load field (overrides current_lane when present)
    if (afc_data.contains("current_load") && afc_data["current_load"].is_string()) {
        std::string load_lane = afc_data["current_load"].get<std::string>();
        auto it = lane_name_to_index_.find(load_lane);
        if (it != lane_name_to_index_.end()) {
            system_info_.current_slot = it->second;
            spdlog::trace("[AMS AFC] Current load: {} (slot {})", load_lane, it->second);
        }
    }

    // Parse lanes array if present (some AFC versions provide this)
    if (afc_data.contains("lanes") && afc_data["lanes"].is_object()) {
        parse_lane_data(afc_data["lanes"]);
    }

    // Parse unit information if available
    if (afc_data.contains("units") && afc_data["units"].is_array()) {
        const auto& units_json = afc_data["units"];

        // Capture unit-to-lane mapping for multi-unit reorganization
        unit_lane_map_.clear();
        unit_infos_.clear();

        for (const auto& unit_json : units_json) {
            // Handle flat string format: "OpenAMS AMS_1", "Box_Turtle Turtle_1"
            if (unit_json.is_string()) {
                std::string unit_str = unit_json.get<std::string>();
                auto space_pos = unit_str.find(' ');
                if (space_pos != std::string::npos) {
                    AfcUnitInfo info;
                    info.type = unit_str.substr(0, space_pos);
                    info.name = unit_str.substr(space_pos + 1);
                    // AFC convention: Klipper object prefix is "AFC_" + type with underscores
                    // removed. Known mappings: "Box_Turtle" → "AFC_BoxTurtle", "OpenAMS" →
                    // "AFC_OpenAMS" If this convention breaks for future types, use an explicit
                    // mapping table.
                    std::string klipper_type = info.type;
                    klipper_type.erase(std::remove(klipper_type.begin(), klipper_type.end(), '_'),
                                       klipper_type.end());
                    info.klipper_key = "AFC_" + klipper_type + " " + info.name;
                    unit_infos_.push_back(std::move(info));
                    spdlog::debug("[AMS AFC] Parsed string unit: type='{}' name='{}' key='{}'",
                                  unit_infos_.back().type, unit_infos_.back().name,
                                  unit_infos_.back().klipper_key);
                } else {
                    spdlog::debug("[AMS AFC] Skipping unit string with no space: '{}'", unit_str);
                }
                continue;
            }

            // Handle object format (backward compat): {"name": "...", "lanes": [...]}
            if (!unit_json.is_object()) {
                continue;
            }

            std::string unit_name;
            if (unit_json.contains("name") && unit_json["name"].is_string()) {
                unit_name = unit_json["name"].get<std::string>();
            }

            // Capture per-unit lane list
            if (unit_json.contains("lanes") && unit_json["lanes"].is_array()) {
                std::vector<std::string> lanes;
                for (const auto& lane : unit_json["lanes"]) {
                    if (lane.is_string()) {
                        lanes.push_back(lane.get<std::string>());
                    }
                }
                if (!unit_name.empty() && !lanes.empty()) {
                    unit_lane_map_[unit_name] = lanes;
                }
            }
        }

        // Update existing unit names and connection status (backward compat for object format)
        for (size_t i = 0; i < units_json.size() && i < system_info_.units.size(); ++i) {
            if (units_json[i].is_object()) {
                if (units_json[i].contains("name") && units_json[i]["name"].is_string()) {
                    system_info_.units[i].name = units_json[i]["name"].get<std::string>();
                }
                if (units_json[i].contains("connected") &&
                    units_json[i]["connected"].is_boolean()) {
                    system_info_.units[i].connected = units_json[i]["connected"].get<bool>();
                }
            }
        }

        // If we got unit-lane data from object format, re-organize into multi-unit layout.
        // NOTE: This runs under mutex_ lock (held by handle_status_update caller),
        // so system_info_ modifications are safe from concurrent get_system_info() reads.
        if (!unit_lane_map_.empty()) {
            if (!lanes_initialized_ && !lane_names_.empty()) {
                initialize_lanes(lane_names_);
            }
            if (lanes_initialized_) {
                reorganize_units_from_map();
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

    // Extract buffer names from AFC.buffers array
    if (afc_data.contains("buffers") && afc_data["buffers"].is_array()) {
        buffer_names_.clear();
        for (const auto& buf : afc_data["buffers"]) {
            if (buf.is_string()) {
                buffer_names_.push_back(buf.get<std::string>());
            }
        }
    }

    // Extract extruder names from top-level AFC.extruders array (for multi-extruder iteration)
    // This is a flat string array: ["extruder", "extruder1", ..., "extruder5"]
    if (afc_data.contains("extruders") && afc_data["extruders"].is_array()) {
        extruder_names_.clear();
        for (const auto& ext : afc_data["extruders"]) {
            if (ext.is_string()) {
                extruder_names_.push_back(ext.get<std::string>());
            }
        }
        spdlog::debug("[AMS AFC] Discovered {} extruder names from AFC state",
                      extruder_names_.size());
    }

    // Parse global quiet_mode and LED state
    if (afc_data.contains("quiet_mode") && afc_data["quiet_mode"].is_boolean()) {
        afc_quiet_mode_ = afc_data["quiet_mode"].get<bool>();
    }
    if (afc_data.contains("led_state") && afc_data["led_state"].is_boolean()) {
        afc_led_state_ = afc_data["led_state"].get<bool>();
    }

    // Parse system.num_extruders and system.extruders (toolchanger support)
    if (afc_data.contains("system") && afc_data["system"].is_object()) {
        const auto& system = afc_data["system"];

        if (system.contains("num_extruders") && system["num_extruders"].is_number_integer()) {
            num_extruders_ = system["num_extruders"].get<int>();
            spdlog::debug("[AMS AFC] num_extruders: {}", num_extruders_);
        }

        if (system.contains("extruders") && system["extruders"].is_object()) {
            extruders_.clear();
            const auto& extruders_json = system["extruders"];

            // Collect extruder names and sort for deterministic ordering
            std::vector<std::string> extruder_names;
            for (auto it = extruders_json.begin(); it != extruders_json.end(); ++it) {
                extruder_names.push_back(it.key());
            }
            std::sort(extruder_names.begin(), extruder_names.end());

            for (const auto& ext_name : extruder_names) {
                const auto& ext_data = extruders_json[ext_name];
                if (!ext_data.is_object()) {
                    continue;
                }

                AfcExtruderInfo info;
                info.name = ext_name;

                // Parse lane_loaded (can be string or null)
                if (ext_data.contains("lane_loaded")) {
                    if (ext_data["lane_loaded"].is_string()) {
                        info.lane_loaded = ext_data["lane_loaded"].get<std::string>();
                    }
                    // null or other types result in empty string (default)
                }

                // Parse lanes array
                if (ext_data.contains("lanes") && ext_data["lanes"].is_array()) {
                    for (const auto& lane : ext_data["lanes"]) {
                        if (lane.is_string()) {
                            info.available_lanes.push_back(lane.get<std::string>());
                        }
                    }
                }

                spdlog::debug("[AMS AFC] Extruder '{}': lane_loaded='{}', {} lanes", ext_name,
                              info.lane_loaded, info.available_lanes.size());
                extruders_.push_back(std::move(info));
            }
        }
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
    if (data.contains("buffer_status") && data["buffer_status"].is_string()) {
        sensors.buffer_status = data["buffer_status"].get<std::string>();
    }
    if (data.contains("filament_status") && data["filament_status"].is_string()) {
        sensors.filament_status = data["filament_status"].get<std::string>();
    }
    if (data.contains("dist_hub") && data["dist_hub"].is_number()) {
        sensors.dist_hub = data["dist_hub"].get<float>();
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

    // Populate or clear per-slot error based on lane status
    if (status_str == "Error") {
        SlotError err;
        // Use system message text if available, otherwise default
        if (!last_seen_message_.empty()) {
            err.message = last_seen_message_;
        } else {
            err.message = "Lane error";
        }
        // Map severity from system message type
        if (last_message_type_ == "error") {
            err.severity = SlotError::ERROR;
        } else if (last_message_type_ == "warning") {
            err.severity = SlotError::WARNING;
        } else {
            err.severity = SlotError::ERROR; // Default to ERROR for lane errors
        }
        slot->error = err;
        spdlog::debug("[AMS AFC] Lane {} (slot {}): error state - {}", lane_name, slot_index,
                      err.message);
    } else if (slot->error.has_value()) {
        // Lane exited error state - clear the error
        spdlog::debug("[AMS AFC] Lane {} (slot {}): error cleared", lane_name, slot_index);
        slot->error.reset();
    }

    spdlog::trace("[AMS AFC] Lane {} (slot {}): prep={} load={} hub={} status={}", lane_name,
                  slot_index, sensors.prep, sensors.load, sensors.loaded_to_hub,
                  slot_status_to_string(slot->status));

    // Parse tool mapping from "map" field (e.g., "T0", "T1")
    if (data.contains("map") && data["map"].is_string()) {
        std::string map_str = data["map"].get<std::string>();
        // Parse "T{N}" format
        if (map_str.size() >= 2 && map_str[0] == 'T') {
            try {
                int tool_num = std::stoi(map_str.substr(1));
                if (tool_num >= 0 && tool_num <= 64) {
                    // Update slot's mapped_tool
                    if (slot) {
                        slot->mapped_tool = tool_num;
                    }
                    // Update tool_to_slot_map — ensure map is large enough
                    if (tool_num >= static_cast<int>(system_info_.tool_to_slot_map.size())) {
                        system_info_.tool_to_slot_map.resize(tool_num + 1, -1);
                    }
                    // Clear old mapping for this slot (another tool may have pointed here)
                    for (auto& mapping : system_info_.tool_to_slot_map) {
                        if (mapping == slot_index) {
                            mapping = -1;
                        }
                    }
                    system_info_.tool_to_slot_map[tool_num] = slot_index;
                    spdlog::trace("[AMS AFC] Lane {} mapped to tool T{}", lane_name, tool_num);
                }
            } catch (...) {
                // Invalid tool number format
            }
        }
    }

    // Parse endless spool backup from "runout_lane" field
    if (data.contains("runout_lane")) {
        if (slot_index < static_cast<int>(endless_spool_configs_.size())) {
            if (data["runout_lane"].is_string()) {
                std::string backup_lane = data["runout_lane"].get<std::string>();
                auto backup_it = lane_name_to_index_.find(backup_lane);
                if (backup_it != lane_name_to_index_.end()) {
                    endless_spool_configs_[slot_index].backup_slot = backup_it->second;
                    spdlog::trace("[AMS AFC] Lane {} runout backup: {} (slot {})", lane_name,
                                  backup_lane, backup_it->second);
                }
            } else if (data["runout_lane"].is_null()) {
                endless_spool_configs_[slot_index].backup_slot = -1;
                spdlog::trace("[AMS AFC] Lane {} runout backup: disabled", lane_name);
            }
        }
    }
}

void AmsBackendAfc::parse_afc_hub(const std::string& hub_name, const nlohmann::json& data) {
    // Parse AFC_hub object for per-hub sensor state
    // { "state": true }

    if (data.contains("state") && data["state"].is_boolean()) {
        bool state = data["state"].get<bool>();
        hub_sensors_[hub_name] = state;
        spdlog::trace("[AMS AFC] Hub sensor {}: {}", hub_name, state);

        // Update the matching AmsUnit's hub_sensor_triggered for real-time state
        for (auto& unit : system_info_.units) {
            if (unit.name == hub_name) {
                unit.has_hub_sensor = true;
                unit.hub_sensor_triggered = state;
                break;
            }
        }
    }

    // Store bowden length from hub — in multi-hub setups, all hubs share the same
    // bowden tube to the toolhead so last-writer-wins is acceptable here
    if (data.contains("afc_bowden_length") && data["afc_bowden_length"].is_number()) {
        bowden_length_ = data["afc_bowden_length"].get<float>();
        spdlog::trace("[AMS AFC] Hub bowden length: {}mm", bowden_length_);
    }
}

void AmsBackendAfc::parse_afc_buffer(const std::string& buffer_name, const nlohmann::json& data) {
    // Parse AFC_buffer object for buffer health and fault detection
    // {
    //   "fault_detection_enabled": true,
    //   "distance_to_fault": 25.5,
    //   "state": "Advancing",
    //   "lanes": ["lane1", "lane2", "lane3", "lane4"]
    // }

    BufferHealth health;

    if (data.contains("fault_detection_enabled") && data["fault_detection_enabled"].is_boolean()) {
        health.fault_detection_enabled = data["fault_detection_enabled"].get<bool>();
    }

    if (data.contains("distance_to_fault") && data["distance_to_fault"].is_number()) {
        health.distance_to_fault = data["distance_to_fault"].get<float>();
    }

    if (data.contains("state") && data["state"].is_string()) {
        health.state = data["state"].get<std::string>();
    }

    spdlog::trace("[AMS AFC] Buffer {}: fault_detect={} dist={} state={}", buffer_name,
                  health.fault_detection_enabled, health.distance_to_fault, health.state);

    // Store buffer health at unit level (buffer sits between hub and toolhead, not per-lane)
    // Find which unit this buffer belongs to by checking its lane list
    if (data.contains("lanes") && data["lanes"].is_array()) {
        for (const auto& lane_json : data["lanes"]) {
            if (!lane_json.is_string()) {
                continue;
            }
            std::string lane_name = lane_json.get<std::string>();
            auto it = lane_name_to_index_.find(lane_name);
            if (it == lane_name_to_index_.end()) {
                continue;
            }

            // Find the unit containing this lane and set buffer health on the unit
            AmsUnit* unit = system_info_.get_unit_for_slot(it->second);
            if (unit) {
                unit->buffer_health = health;
                spdlog::debug("[AMS AFC] Buffer {} health set on unit {} (via lane {})",
                              buffer_name, unit->unit_index, lane_name);
                break; // One buffer per unit — set once and done
            }
        }
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

void AmsBackendAfc::parse_afc_unit_object(AfcUnitInfo& unit_info, const nlohmann::json& data) {
    // Parse unit-level Klipper object (AFC_BoxTurtle or AFC_OpenAMS)
    // Contains arrays of lanes, extruders, hubs, and buffers belonging to this unit

    if (data.contains("lanes") && data["lanes"].is_array()) {
        unit_info.lanes.clear();
        for (const auto& lane : data["lanes"]) {
            if (lane.is_string()) {
                unit_info.lanes.push_back(lane.get<std::string>());
            }
        }
    }

    if (data.contains("extruders") && data["extruders"].is_array()) {
        unit_info.extruders.clear();
        for (const auto& ext : data["extruders"]) {
            if (ext.is_string()) {
                unit_info.extruders.push_back(ext.get<std::string>());
            }
        }
    }

    if (data.contains("hubs") && data["hubs"].is_array()) {
        unit_info.hubs.clear();
        for (const auto& hub : data["hubs"]) {
            if (hub.is_string()) {
                unit_info.hubs.push_back(hub.get<std::string>());
            }
        }
    }

    if (data.contains("buffers") && data["buffers"].is_array()) {
        unit_info.buffers.clear();
        for (const auto& buf : data["buffers"]) {
            if (buf.is_string()) {
                unit_info.buffers.push_back(buf.get<std::string>());
            }
        }
    }

    // Derive topology from hub/extruder counts:
    // - No hubs + multiple extruders → PARALLEL (Box Turtle: 1:1 lane-to-tool)
    // - Hubs present + single extruder → HUB (OpenAMS: N:1 through hub)
    // - Default: HUB
    if (unit_info.hubs.empty() && unit_info.extruders.size() > 1) {
        unit_info.topology = PathTopology::PARALLEL;
    } else if (!unit_info.hubs.empty() && unit_info.extruders.size() <= 1) {
        unit_info.topology = PathTopology::HUB;
    } else {
        unit_info.topology = PathTopology::HUB;
    }

    spdlog::debug("[AMS AFC] Unit object '{}': {} lanes, {} extruders, {} hubs, {} buffers → {}",
                  unit_info.klipper_key, unit_info.lanes.size(), unit_info.extruders.size(),
                  unit_info.hubs.size(), unit_info.buffers.size(),
                  path_topology_to_string(unit_info.topology));

    // Only reorganize when ALL unit_infos_ have received their lane data.
    // Unit objects may arrive in separate status updates; reorganizing on partial
    // data creates transient incorrect unit structures visible in the UI.
    bool all_have_lanes = !unit_infos_.empty();
    for (const auto& ui : unit_infos_) {
        if (ui.lanes.empty()) {
            all_have_lanes = false;
            break;
        }
    }
    if (all_have_lanes) {
        reorganize_units_from_unit_info();
    }
}

void AmsBackendAfc::reorganize_units_from_unit_info() {
    // Rebuild unit_lane_map_ from unit_infos_ data and trigger reorganization
    unit_lane_map_.clear();
    for (const auto& ui : unit_infos_) {
        if (!ui.lanes.empty()) {
            // Use the full "Type Name" as map key for reorganize_units_from_map
            // which uses unit names for AmsUnit::name
            std::string display_name = ui.type + " " + ui.name;
            unit_lane_map_[display_name] = ui.lanes;
        }
    }

    if (!unit_lane_map_.empty()) {
        if (!lanes_initialized_ && !lane_names_.empty()) {
            initialize_lanes(lane_names_);
        }
        if (lanes_initialized_) {
            reorganize_units_from_map();

            // Set per-unit topology on AmsUnit structs from unit_infos_
            for (size_t i = 0; i < unit_infos_.size() && i < system_info_.units.size(); ++i) {
                system_info_.units[i].topology = unit_infos_[i].topology;
            }

            spdlog::debug("[AMS AFC] Reorganized {} units from unit-level objects",
                          unit_infos_.size());
        }
    }
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
        },
        0,   // default timeout
        true // silent — probe only, don't show toast or log at error level
    );
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

    // Add AFC_lane objects (OpenAMS lanes use this prefix instead of AFC_stepper)
    for (const auto& lane_name : lane_names_) {
        std::string key = "AFC_lane " + lane_name;
        objects_to_query[key] = nullptr;
    }

    // Add AFC_hub objects
    for (const auto& hub_name : hub_names_) {
        std::string key = "AFC_hub " + hub_name;
        objects_to_query[key] = nullptr;
    }

    // Add AFC_extruder objects (multi-extruder support)
    if (!extruder_names_.empty()) {
        for (const auto& ext_name : extruder_names_) {
            std::string key = "AFC_extruder " + ext_name;
            objects_to_query[key] = nullptr;
        }
    } else {
        // Backward compat: single extruder
        objects_to_query["AFC_extruder extruder"] = nullptr;
    }

    // Add unit-level Klipper objects (AFC_BoxTurtle, AFC_OpenAMS)
    for (const auto& unit_info : unit_infos_) {
        objects_to_query[unit_info.klipper_key] = nullptr;
    }

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
        auto* slot_ptr = system_info_.get_slot_global(static_cast<int>(i));
        if (!slot_ptr) {
            continue;
        }
        auto& slot = *slot_ptr;

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
    unit.has_hub_sensor = true;      // AFC hubs have filament sensors

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

/**
 * @brief Reorganize flat slot list into multi-unit structure using unit_lane_map_.
 *
 * Called from parse_afc_state() after the "units" JSON array has been parsed.
 * Rebuilds system_info_.units from unit_lane_map_ (unit_name → [lane_names]),
 * preserving existing slot data (colors, materials, status) by matching lane names.
 *
 * @pre mutex_ must be held by caller (via handle_status_update → parse_afc_state)
 * @pre lanes_initialized_ must be true (slots exist in system_info_.units[0])
 */
void AmsBackendAfc::reorganize_units_from_map() {
    if (unit_lane_map_.size() <= 1) {
        // Single unit - just update the name if available
        if (!unit_lane_map_.empty() && !system_info_.units.empty()) {
            system_info_.units[0].name = unit_lane_map_.begin()->first;
        }
        return;
    }

    // Multi-unit: rebuild units vector with proper lane grouping
    // Preserve existing slot data (colors, materials, etc.)

    // Collect all current slot data by lane name for preservation
    std::unordered_map<std::string, SlotInfo> slot_data_by_lane;
    for (size_t i = 0; i < lane_names_.size(); ++i) {
        const SlotInfo* slot = system_info_.get_slot_global(static_cast<int>(i));
        if (slot) {
            slot_data_by_lane[lane_names_[i]] = *slot;
        }
    }

    // Rebuild units - sort unit names for deterministic ordering
    std::vector<std::string> sorted_unit_names;
    sorted_unit_names.reserve(unit_lane_map_.size());
    for (const auto& [name, lanes] : unit_lane_map_) {
        sorted_unit_names.push_back(name);
    }
    std::sort(sorted_unit_names.begin(), sorted_unit_names.end());

    system_info_.units.clear();
    int global_slot_offset = 0;
    int unit_idx = 0;

    for (const auto& unit_name : sorted_unit_names) {
        const auto& lanes = unit_lane_map_.at(unit_name);

        AmsUnit unit;
        unit.unit_index = unit_idx;
        unit.name = unit_name;
        unit.slot_count = static_cast<int>(lanes.size());
        unit.first_slot_global_index = global_slot_offset;
        unit.connected = true;
        unit.has_toolhead_sensor = true;
        unit.has_slot_sensors = true;
        unit.has_hub_sensor = true; // AFC hubs have filament sensors

        // Set hub sensor triggered state from per-hub map
        auto hub_it = hub_sensors_.find(unit_name);
        if (hub_it != hub_sensors_.end()) {
            unit.hub_sensor_triggered = hub_it->second;
        }

        for (int i = 0; i < unit.slot_count; ++i) {
            SlotInfo slot;
            slot.slot_index = i;
            slot.global_index = global_slot_offset + i;

            // Restore preserved slot data if available
            const std::string& lane_name = lanes[i];
            auto it = slot_data_by_lane.find(lane_name);
            if (it != slot_data_by_lane.end()) {
                slot = it->second;
                // Fix up indices for new unit layout
                slot.slot_index = i;
                slot.global_index = global_slot_offset + i;
            } else {
                slot.status = SlotStatus::UNKNOWN;
                slot.mapped_tool = global_slot_offset + i;
                slot.color_rgb = AMS_DEFAULT_SLOT_COLOR;
            }

            unit.slots.push_back(slot);
        }

        system_info_.units.push_back(unit);
        global_slot_offset += unit.slot_count;
        ++unit_idx;
    }

    system_info_.total_slots = global_slot_offset;

    spdlog::info("[AMS AFC] Reorganized into {} units, {} total slots", system_info_.units.size(),
                 system_info_.total_slots);
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

AmsError AmsBackendAfc::execute_gcode_notify(const std::string& gcode,
                                             const std::string& success_msg,
                                             const std::string& error_prefix) {
    if (!api_) {
        return AmsErrorHelper::not_connected("MoonrakerAPI not available");
    }

    spdlog::info("[AMS AFC] Executing G-code: {}", gcode);

    // Capture messages by value for async callbacks (thread-safe via ui_async_call)
    api_->execute_gcode(
        gcode,
        [success_msg]() {
            if (!success_msg.empty()) {
                NOTIFY_SUCCESS("{}", success_msg);
            }
        },
        [gcode, error_prefix](const MoonrakerError& err) {
            if (!error_prefix.empty()) {
                NOTIFY_ERROR("{}: {}", error_prefix, err.message);
            } else {
                spdlog::error("[AMS AFC] G-code failed: {} - {}", gcode, err.message);
            }
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

        // Only check running_, NOT is_busy() — recovery must work even when
        // the system is stuck in a busy/error state
        if (!running_) {
            return AmsErrorHelper::not_connected("AFC backend not started");
        }
    }

    spdlog::info("[AMS AFC] Initiating recovery");
    return execute_gcode_notify("AFC_RESET", lv_tr("AFC recovery complete"),
                                lv_tr("AFC recovery failed"));
}

AmsError AmsBackendAfc::reset() {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }
    }

    spdlog::info("[AMS AFC] Homing AFC system");
    return execute_gcode_notify("AFC_HOME", lv_tr("AFC homing complete"),
                                lv_tr("AFC homing failed"));
}

AmsError AmsBackendAfc::reset_lane(int slot_index) {
    std::string lane_name;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        if (slot_index < 0 || slot_index >= static_cast<int>(lane_names_.size())) {
            return AmsErrorHelper::invalid_slot(
                slot_index, lane_names_.empty() ? 0 : static_cast<int>(lane_names_.size()) - 1);
        }
        lane_name = lane_names_[slot_index];
    }

    spdlog::info("[AMS AFC] Resetting lane {}", lane_name);
    return execute_gcode("AFC_LANE_RESET LANE=" + lane_name);
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
    return execute_gcode_notify("AFC_ABORT", lv_tr("AFC operation aborted"),
                                lv_tr("AFC abort failed"));
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

        // Detect whether anything actually changed
        bool changed =
            slot->color_name != info.color_name || slot->color_rgb != info.color_rgb ||
            slot->material != info.material || slot->brand != info.brand ||
            slot->spoolman_id != info.spoolman_id || slot->spool_name != info.spool_name ||
            slot->remaining_weight_g != info.remaining_weight_g ||
            slot->total_weight_g != info.total_weight_g ||
            slot->nozzle_temp_min != info.nozzle_temp_min ||
            slot->nozzle_temp_max != info.nozzle_temp_max || slot->bed_temp != info.bed_temp;

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

        if (changed) {
            spdlog::info("[AMS AFC] Updated slot {} info: {} {}", slot_index, info.material,
                         info.color_name);
        }

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
    std::string lane_name; // Declare outside lock for use after release
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

        // Check if another tool already maps to this slot
        for (size_t i = 0; i < system_info_.tool_to_slot_map.size(); ++i) {
            if (i != static_cast<size_t>(tool_number) &&
                system_info_.tool_to_slot_map[i] == slot_index) {
                spdlog::warn("[AMS AFC] Tool {} will share slot {} with tool {}", tool_number,
                             slot_index, i);
                break;
            }
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

        // Get lane name while holding the lock (lane_names_ access)
        lane_name = get_lane_name(slot_index);
    }

    // AFC may use a G-code command to set tool mapping
    // This varies by AFC version/configuration
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

AmsError AmsBackendAfc::reset_tool_mappings() {
    spdlog::info("[AMS AFC] Resetting tool mappings");

    // Use RESET_AFC_MAPPING with RUNOUT=no to only reset tool mappings
    AmsError result = execute_gcode("RESET_AFC_MAPPING RUNOUT=no");

    // Tool mapping will be refreshed from next status update
    return result;
}

AmsError AmsBackendAfc::reset_endless_spool() {
    spdlog::info("[AMS AFC] Resetting endless spool mappings");

    int slot_count = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        slot_count = static_cast<int>(endless_spool_configs_.size());
    }

    // AFC has no command to reset only runout lanes, iterate through slots
    // Continue on failure to reset as many as possible, return first error
    AmsError first_error = AmsErrorHelper::success();
    for (int slot = 0; slot < slot_count; slot++) {
        AmsError result = set_endless_spool_backup(slot, -1);
        if (!result.success()) {
            spdlog::error("[AMS AFC] Failed to reset slot {} endless spool: {}", slot,
                          result.technical_msg);
            if (first_error.success()) {
                first_error = result;
            }
        }
    }

    return first_error;
}

// ============================================================================
// AFC Config File Management
// ============================================================================

void AmsBackendAfc::load_afc_configs() {
    if (configs_loading_.load() || configs_loaded_.load()) {
        return;
    }

    if (!api_) {
        spdlog::warn("[AMS AFC] Cannot load configs: MoonrakerAPI is null");
        return;
    }

    configs_loading_ = true;

    // Create managers if not yet created
    if (!afc_config_) {
        afc_config_ = std::make_unique<AfcConfigManager>(api_);
    }
    if (!macro_vars_config_) {
        macro_vars_config_ = std::make_unique<AfcConfigManager>(api_);
    }

    // Track completion of both loads
    auto loads_remaining = std::make_shared<std::atomic<int>>(2);

    // Callbacks from download_file run on the libhv background thread.
    // configs_loaded_ is std::atomic<bool> — the store (release) after both loads complete
    // synchronizes-with the load (acquire) in get_device_actions() on the main thread,
    // ensuring all parser writes are visible before the main thread reads them.
    auto check_done = [this, loads_remaining]() {
        if (loads_remaining->fetch_sub(1) == 1) {
            // Both loads complete — release barrier ensures parser state is visible
            configs_loading_.store(false, std::memory_order_relaxed);
            configs_loaded_.store(true, std::memory_order_release);
            spdlog::info("[AMS AFC] Config files loaded");
            emit_event(EVENT_STATE_CHANGED);
        }
    };

    afc_config_->load("AFC/AFC.cfg", [check_done](bool ok, const std::string& err) {
        if (!ok) {
            spdlog::warn("[AMS AFC] Failed to load AFC.cfg: {}", err);
        }
        check_done();
    });

    macro_vars_config_->load(
        "AFC/AFC_Macro_Vars.cfg", [check_done](bool ok, const std::string& err) {
            if (!ok) {
                spdlog::warn("[AMS AFC] Failed to load AFC_Macro_Vars.cfg: {}", err);
            }
            check_done();
        });
}

float AmsBackendAfc::get_macro_var_float(const std::string& key, float default_val) const {
    if (!macro_vars_config_ || !macro_vars_config_->is_loaded()) {
        return default_val;
    }
    return macro_vars_config_->parser().get_float("gcode_macro AFC_MacroVars", key, default_val);
}

bool AmsBackendAfc::get_macro_var_bool(const std::string& key, bool default_val) const {
    if (!macro_vars_config_ || !macro_vars_config_->is_loaded()) {
        return default_val;
    }
    return macro_vars_config_->parser().get_bool("gcode_macro AFC_MacroVars", key, default_val);
}

// ============================================================================
// Device Actions (AFC-specific calibration and speed settings)
// ============================================================================

std::vector<helix::printer::DeviceSection> AmsBackendAfc::get_device_sections() const {
    return helix::printer::afc_default_sections();
}

std::vector<helix::printer::DeviceAction> AmsBackendAfc::get_device_actions() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    using helix::printer::ActionType;
    using helix::printer::DeviceAction;

    // Start from shared defaults for static actions
    auto actions = helix::printer::afc_default_actions();

    // Overlay dynamic values onto default actions
    for (auto& a : actions) {
        if (a.id == "bowden_length") {
            a.current_value = bowden_length_;
            a.max_value = std::max(2000.0f, bowden_length_ * 1.5f);
        }
        if (a.id == "led_toggle") {
            a.label = afc_led_state_ ? "Turn Off LEDs" : "Turn On LEDs";
            a.icon = afc_led_state_ ? "lightbulb-off" : "lightbulb-on";
        }
    }

    // Multi-extruder: replace single bowden with per-extruder sliders
    if (num_extruders_ > 1 && !extruders_.empty()) {
        actions.erase(std::remove_if(actions.begin(), actions.end(),
                                     [](const DeviceAction& a) { return a.id == "bowden_length"; }),
                      actions.end());
        for (int i = 0; i < static_cast<int>(extruders_.size()); ++i) {
            std::string id = "bowden_T" + std::to_string(i);
            std::string label = "Bowden Length (T" + std::to_string(i) + ")";
            std::string desc = "Bowden tube length for tool " + std::to_string(i);
            actions.push_back(
                DeviceAction{id,
                             label,
                             "ruler",
                             "setup",
                             desc,
                             ActionType::SLIDER,
                             bowden_length_, // shared default until per-extruder tracking
                             {},
                             100.0f,
                             std::max(2000.0f, bowden_length_ * 1.5f),
                             "mm",
                             -1,
                             true,
                             ""});
        }
    }

    // ---- Overlay dynamic values from config onto default actions ----

    // Acquire barrier pairs with release in load_afc_configs() to ensure
    // parser state written on bg thread is visible here on the main thread.
    bool loaded = configs_loaded_.load(std::memory_order_acquire);
    bool cfg_ready = loaded && afc_config_ && afc_config_->is_loaded();
    bool macro_ready = loaded && macro_vars_config_ && macro_vars_config_->is_loaded();
    std::string not_loaded_reason = "Loading configuration...";

    // Find first AFC_hub section from config (for hub actions)
    std::string hub_section;
    if (cfg_ready) {
        auto hubs = afc_config_->parser().get_sections_matching("AFC_hub");
        if (!hubs.empty()) {
            hub_section = hubs[0];
        }
    }
    bool hub_ready = cfg_ready && !hub_section.empty();

    // Config save state
    bool has_changes = (afc_config_ && afc_config_->has_unsaved_changes()) ||
                       (macro_vars_config_ && macro_vars_config_->has_unsaved_changes());

    for (auto& a : actions) {
        // Hub & Cutter actions — from afc_config_ hub section
        if (a.id == "hub_cut_enabled") {
            if (hub_ready) {
                a.current_value =
                    std::any(afc_config_->parser().get_bool(hub_section, "cut", false));
                a.enabled = true;
            } else {
                a.enabled = false;
                a.disable_reason = not_loaded_reason;
            }
        } else if (a.id == "hub_cut_dist") {
            if (hub_ready) {
                a.current_value =
                    std::any(afc_config_->parser().get_float(hub_section, "cut_dist", 0.0f));
                a.enabled = true;
            } else {
                a.enabled = false;
                a.disable_reason = not_loaded_reason;
            }
        } else if (a.id == "hub_bowden_length") {
            if (hub_ready) {
                a.current_value = std::any(
                    afc_config_->parser().get_float(hub_section, "afc_bowden_length", 450.0f));
                a.enabled = true;
            } else {
                a.enabled = false;
                a.disable_reason = not_loaded_reason;
            }
        } else if (a.id == "assisted_retract") {
            if (hub_ready) {
                a.current_value = std::any(
                    afc_config_->parser().get_bool(hub_section, "assisted_retract", false));
                a.enabled = true;
            } else {
                a.enabled = false;
                a.disable_reason = not_loaded_reason;
            }
        }

        // Tip Forming actions — from macro_vars_config_
        else if (a.id == "ramming_volume") {
            if (macro_ready) {
                a.current_value = std::any(get_macro_var_float("variable_ramming_volume", 0.0f));
                a.enabled = true;
            } else {
                a.enabled = false;
                a.disable_reason = not_loaded_reason;
            }
        } else if (a.id == "unloading_speed_start") {
            if (macro_ready) {
                a.current_value =
                    std::any(get_macro_var_float("variable_unloading_speed_start", 0.0f));
                a.enabled = true;
            } else {
                a.enabled = false;
                a.disable_reason = not_loaded_reason;
            }
        } else if (a.id == "cooling_tube_length") {
            if (macro_ready) {
                a.current_value =
                    std::any(get_macro_var_float("variable_cooling_tube_length", 0.0f));
                a.enabled = true;
            } else {
                a.enabled = false;
                a.disable_reason = not_loaded_reason;
            }
        } else if (a.id == "cooling_tube_retraction") {
            if (macro_ready) {
                a.current_value =
                    std::any(get_macro_var_float("variable_cooling_tube_retraction", 0.0f));
                a.enabled = true;
            } else {
                a.enabled = false;
                a.disable_reason = not_loaded_reason;
            }
        }

        // Purge & Wipe actions — from macro_vars_config_
        else if (a.id == "purge_enabled") {
            if (macro_ready) {
                a.current_value = std::any(get_macro_var_bool("variable_purge_enabled", false));
                a.enabled = true;
            } else {
                a.enabled = false;
                a.disable_reason = not_loaded_reason;
            }
        } else if (a.id == "purge_length") {
            if (macro_ready) {
                a.current_value = std::any(get_macro_var_float("variable_purge_length", 0.0f));
                a.enabled = true;
            } else {
                a.enabled = false;
                a.disable_reason = not_loaded_reason;
            }
        } else if (a.id == "brush_enabled") {
            if (macro_ready) {
                a.current_value = std::any(get_macro_var_bool("variable_brush_enabled", false));
                a.enabled = true;
            } else {
                a.enabled = false;
                a.disable_reason = not_loaded_reason;
            }
        }

        // Config section — save_restart enabled only when there are unsaved changes
        else if (a.id == "save_restart") {
            a.enabled = has_changes;
            a.disable_reason = has_changes ? "" : "No unsaved changes";
        }
    }

    return actions;
}

AmsError AmsBackendAfc::execute_device_action(const std::string& action_id, const std::any& value) {
    spdlog::info("[AMS AFC] Executing device action: {}", action_id);

    if (action_id == "calibration_wizard") {
        return execute_gcode("AFC_CALIBRATION");
    } else if (action_id == "bowden_length") {
        if (!value.has_value()) {
            return AmsError(AmsResult::WRONG_STATE, "Bowden length value required", "Missing value",
                            "Provide a bowden length value");
        }
        try {
            float length = std::any_cast<float>(value);
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            float max_len = std::max(2000.0f, bowden_length_ * 1.5f);
            if (length < 100.0f || length > max_len) {
                return AmsError(AmsResult::WRONG_STATE,
                                fmt::format("Bowden length must be 100-{:.0f}mm", max_len),
                                "Invalid value",
                                fmt::format("Enter a length between 100 and {:.0f}mm", max_len));
            }
            // AFC uses SET_BOWDEN_LENGTH UNIT={unit_name} LENGTH={mm}
            // For simplicity, we'll use the first unit
            if (!system_info_.units.empty()) {
                std::string unit_name = system_info_.units[0].name;
                return execute_gcode("SET_BOWDEN_LENGTH UNIT=" + unit_name +
                                     " LENGTH=" + std::to_string(static_cast<int>(length)));
            }
            return AmsErrorHelper::not_supported("No AFC units configured");
        } catch (const std::bad_any_cast&) {
            return AmsError(AmsResult::WRONG_STATE, "Invalid bowden length type",
                            "Invalid value type", "Provide a numeric value");
        }
    } else if (action_id.rfind("bowden_T", 0) == 0) {
        // Per-extruder bowden length (toolchanger): bowden_T0, bowden_T1, etc.
        if (!value.has_value()) {
            return AmsError(AmsResult::WRONG_STATE, "Bowden length value required", "Missing value",
                            "Provide a bowden length value");
        }
        try {
            float length = std::any_cast<float>(value);
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            float max_len = std::max(2000.0f, bowden_length_ * 1.5f);
            if (length < 100.0f || length > max_len) {
                return AmsError(AmsResult::WRONG_STATE,
                                fmt::format("Bowden length must be 100-{:.0f}mm", max_len),
                                "Invalid value",
                                fmt::format("Enter a length between 100 and {:.0f}mm", max_len));
            }
            // Extract tool index from action_id (e.g., "bowden_T0" -> 0)
            int tool_idx = std::stoi(action_id.substr(8));
            if (tool_idx >= 0 && tool_idx < static_cast<int>(extruders_.size())) {
                // Use extruder name for the command
                return execute_gcode("SET_BOWDEN_LENGTH EXTRUDER=" + extruders_[tool_idx].name +
                                     " LENGTH=" + std::to_string(static_cast<int>(length)));
            }
            return AmsErrorHelper::not_supported("Invalid extruder index: " +
                                                 std::to_string(tool_idx));
        } catch (const std::bad_any_cast&) {
            return AmsError(AmsResult::WRONG_STATE, "Invalid bowden length type",
                            "Invalid value type", "Provide a numeric value");
        }
    } else if (action_id == "speed_fwd" || action_id == "speed_rev") {
        if (!value.has_value()) {
            return AmsError(AmsResult::WRONG_STATE, "Speed multiplier value required",
                            "Missing value", "Provide a speed multiplier value");
        }
        try {
            float multiplier = std::any_cast<float>(value);
            if (multiplier < 0.5f || multiplier > 2.0f) {
                return AmsError(AmsResult::WRONG_STATE, "Speed multiplier must be 0.5-2.0x",
                                "Invalid value", "Enter a multiplier between 0.5 and 2.0");
            }
            // AFC uses SET_LONG_MOVE_SPEED with FWD and REV parameters
            // We'll set just the one being changed
            std::string param = (action_id == "speed_fwd") ? "FWD" : "REV";
            return execute_gcode("SET_LONG_MOVE_SPEED " + param + "=" + std::to_string(multiplier));
        } catch (const std::bad_any_cast&) {
            return AmsError(AmsResult::WRONG_STATE, "Invalid speed multiplier type",
                            "Invalid value type", "Provide a numeric value");
        }
    } else if (action_id == "test_lanes") {
        return execute_gcode("AFC_TEST_LANES");
    } else if (action_id == "change_blade") {
        return execute_gcode("AFC_CHANGE_BLADE");
    } else if (action_id == "park") {
        return execute_gcode("AFC_PARK");
    } else if (action_id == "brush") {
        return execute_gcode("AFC_BRUSH");
    } else if (action_id == "reset_motor") {
        return execute_gcode("AFC_RESET_MOTOR_TIME");
    } else if (action_id == "led_toggle") {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return execute_gcode(afc_led_state_ ? "TURN_OFF_AFC_LED" : "TURN_ON_AFC_LED");
    } else if (action_id == "quiet_mode") {
        return execute_gcode("AFC_QUIET_MODE");
    }

    // ---- Config-backed hub actions (afc_config_) ----
    if (action_id == "hub_cut_enabled" || action_id == "hub_cut_dist" ||
        action_id == "hub_bowden_length" || action_id == "assisted_retract") {
        if (!afc_config_ || !afc_config_->is_loaded()) {
            return AmsError(AmsResult::WRONG_STATE, "AFC config not loaded",
                            "Configuration not available", "Wait for config to load");
        }

        auto hubs = afc_config_->parser().get_sections_matching("AFC_hub");
        if (hubs.empty()) {
            return AmsError(AmsResult::WRONG_STATE, "No hub section found in AFC config",
                            "No hub configured", "Check AFC configuration");
        }
        const std::string& hub_section = hubs[0];

        if (action_id == "hub_cut_enabled") {
            try {
                bool val = std::any_cast<bool>(value);
                afc_config_->parser().set(hub_section, "cut", val ? "True" : "False");
                afc_config_->mark_dirty();
                return AmsErrorHelper::success();
            } catch (const std::bad_any_cast&) {
                return AmsError(AmsResult::WRONG_STATE, "Invalid value type for toggle",
                                "Expected boolean", "");
            }
        } else if (action_id == "hub_cut_dist") {
            try {
                float val = std::any_cast<float>(value);
                afc_config_->parser().set(hub_section, "cut_dist", fmt::format("{:g}", val));
                afc_config_->mark_dirty();
                return AmsErrorHelper::success();
            } catch (const std::bad_any_cast&) {
                return AmsError(AmsResult::WRONG_STATE, "Invalid value type for slider",
                                "Expected float", "");
            }
        } else if (action_id == "hub_bowden_length") {
            try {
                float val = std::any_cast<float>(value);
                afc_config_->parser().set(hub_section, "afc_bowden_length",
                                          fmt::format("{:g}", val));
                afc_config_->mark_dirty();
                return AmsErrorHelper::success();
            } catch (const std::bad_any_cast&) {
                return AmsError(AmsResult::WRONG_STATE, "Invalid value type for slider",
                                "Expected float", "");
            }
        } else if (action_id == "assisted_retract") {
            try {
                bool val = std::any_cast<bool>(value);
                afc_config_->parser().set(hub_section, "assisted_retract", val ? "True" : "False");
                afc_config_->mark_dirty();
                return AmsErrorHelper::success();
            } catch (const std::bad_any_cast&) {
                return AmsError(AmsResult::WRONG_STATE, "Invalid value type for toggle",
                                "Expected boolean", "");
            }
        }
    }

    // ---- Config-backed macro var actions (macro_vars_config_) ----
    static const std::unordered_map<std::string, std::string> macro_var_slider_keys = {
        {"ramming_volume", "variable_ramming_volume"},
        {"unloading_speed_start", "variable_unloading_speed_start"},
        {"cooling_tube_length", "variable_cooling_tube_length"},
        {"cooling_tube_retraction", "variable_cooling_tube_retraction"},
        {"purge_length", "variable_purge_length"},
    };

    static const std::unordered_map<std::string, std::string> macro_var_toggle_keys = {
        {"purge_enabled", "variable_purge_enabled"},
        {"brush_enabled", "variable_brush_enabled"},
    };

    if (auto it = macro_var_slider_keys.find(action_id); it != macro_var_slider_keys.end()) {
        if (!macro_vars_config_ || !macro_vars_config_->is_loaded()) {
            return AmsError(AmsResult::WRONG_STATE, "Macro vars config not loaded",
                            "Configuration not available", "Wait for config to load");
        }
        try {
            float val = std::any_cast<float>(value);
            macro_vars_config_->parser().set("gcode_macro AFC_MacroVars", it->second,
                                             fmt::format("{:g}", val));
            macro_vars_config_->mark_dirty();
            return AmsErrorHelper::success();
        } catch (const std::bad_any_cast&) {
            return AmsError(AmsResult::WRONG_STATE, "Invalid value type for slider",
                            "Expected float", "");
        }
    }

    if (auto it = macro_var_toggle_keys.find(action_id); it != macro_var_toggle_keys.end()) {
        if (!macro_vars_config_ || !macro_vars_config_->is_loaded()) {
            return AmsError(AmsResult::WRONG_STATE, "Macro vars config not loaded",
                            "Configuration not available", "Wait for config to load");
        }
        try {
            bool val = std::any_cast<bool>(value);
            macro_vars_config_->parser().set("gcode_macro AFC_MacroVars", it->second,
                                             val ? "True" : "False");
            macro_vars_config_->mark_dirty();
            return AmsErrorHelper::success();
        } catch (const std::bad_any_cast&) {
            return AmsError(AmsResult::WRONG_STATE, "Invalid value type for toggle",
                            "Expected boolean", "");
        }
    }

    // ---- Save & Restart action ----
    if (action_id == "save_restart") {
        bool has_changes = (afc_config_ && afc_config_->has_unsaved_changes()) ||
                           (macro_vars_config_ && macro_vars_config_->has_unsaved_changes());
        if (!has_changes) {
            return AmsError(AmsResult::WRONG_STATE, "No unsaved changes", "Nothing to save", "");
        }

        auto saves_remaining = std::make_shared<std::atomic<int>>(0);

        if (afc_config_ && afc_config_->has_unsaved_changes()) {
            saves_remaining->fetch_add(1);
        }
        if (macro_vars_config_ && macro_vars_config_->has_unsaved_changes()) {
            saves_remaining->fetch_add(1);
        }

        auto save_errors = std::make_shared<std::vector<std::string>>();

        auto on_save_done = [this, saves_remaining, save_errors](bool ok, const std::string& err) {
            if (!ok) {
                spdlog::error("[AMS AFC] Config save failed: {}", err);
                save_errors->push_back(err);
            }
            if (saves_remaining->fetch_sub(1) == 1) {
                if (save_errors->empty()) {
                    // All saves succeeded — restart Klipper to apply
                    spdlog::info("[AMS AFC] All configs saved, sending RESTART");
                    execute_gcode("RESTART");
                } else {
                    spdlog::error("[AMS AFC] {} config save(s) failed, NOT restarting",
                                  save_errors->size());
                }
            }
        };

        if (afc_config_ && afc_config_->has_unsaved_changes()) {
            afc_config_->save("AFC/AFC.cfg", on_save_done);
        }
        if (macro_vars_config_ && macro_vars_config_->has_unsaved_changes()) {
            macro_vars_config_->save("AFC/AFC_Macro_Vars.cfg", on_save_done);
        }

        return AmsErrorHelper::success();
    }

    return AmsErrorHelper::not_supported("Unknown action: " + action_id);
}
