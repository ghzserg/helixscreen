// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_happy_hare.h"

#include "hh_defaults.h"
#include "moonraker_api.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <sstream>

using namespace helix;

// ============================================================================
// Construction / Destruction
// ============================================================================

AmsBackendHappyHare::AmsBackendHappyHare(MoonrakerAPI* api, MoonrakerClient* client)
    : AmsSubscriptionBackend(api, client) {
    // Initialize system info with Happy Hare defaults
    system_info_.type = AmsType::HAPPY_HARE;
    system_info_.type_name = "Happy Hare";
    system_info_.supports_endless_spool = true;
    system_info_.supports_tool_mapping = true;
    // Bypass support is determined at runtime from mmu.has_bypass status field.
    // Default to true; will be updated when first status arrives.
    system_info_.supports_bypass = true;
    // Happy Hare bypass is always positional (selector moves to bypass position), never a sensor
    system_info_.has_hardware_bypass_sensor = false;
    // Default to TIP_FORM -- Happy Hare's default macro is _MMU_FORM_TIP.
    // Overridden by query_tip_method_from_config() once configfile response arrives.
    system_info_.tip_method = TipMethod::TIP_FORM;

    spdlog::debug("[AMS HappyHare] Backend created");
}

AmsBackendHappyHare::~AmsBackendHappyHare() {
    // Invalidate alive guard FIRST — prevents in-flight async callbacks from
    // dereferencing this after destruction (e.g., query_tip_method_from_config).
    // Store false then reset the shared_ptr so weak_ptr::lock() fails for
    // any callbacks that haven't yet checked the guard.
    alive_->store(false);
    alive_.reset();
}

// ============================================================================
// Lifecycle Management
// ============================================================================

void AmsBackendHappyHare::on_started() {
    // Query configfile to determine tip method (cutter vs tip-forming).
    // Happy Hare determines this from form_tip_macro: if it contains "cut",
    // it's a cutter system; otherwise it's tip-forming or none.
    query_tip_method_from_config();
}

// stop(), release_subscriptions(), is_running() provided by AmsSubscriptionBackend

// ============================================================================
// Event System
// ============================================================================

// set_event_callback() and emit_event() provided by AmsSubscriptionBackend

// ============================================================================
// State Queries
// ============================================================================

AmsSystemInfo AmsBackendHappyHare::get_system_info() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!slots_.is_initialized()) {
        return system_info_;
    }

    // Build slot data from registry, then overlay non-slot metadata
    auto info = slots_.build_system_info();

    // Copy system-level fields not managed by registry
    info.type = system_info_.type;
    info.type_name = system_info_.type_name;
    info.version = system_info_.version;
    info.action = system_info_.action;
    info.operation_detail = system_info_.operation_detail;
    info.current_slot = system_info_.current_slot;
    info.current_tool = system_info_.current_tool;
    info.pending_target_slot = system_info_.pending_target_slot;
    info.current_toolchange = system_info_.current_toolchange;
    info.number_of_toolchanges = system_info_.number_of_toolchanges;
    info.filament_loaded = system_info_.filament_loaded;
    info.supports_endless_spool = system_info_.supports_endless_spool;
    info.supports_tool_mapping = system_info_.supports_tool_mapping;
    info.supports_bypass = system_info_.supports_bypass;
    info.has_hardware_bypass_sensor = system_info_.has_hardware_bypass_sensor;
    info.tip_method = system_info_.tip_method;
    info.supports_purge = system_info_.supports_purge;

    // Happy Hare v4 extended fields
    info.spoolman_mode = system_info_.spoolman_mode;
    info.pending_spool_id = system_info_.pending_spool_id;
    info.espooler_state = system_info_.espooler_state;
    info.sync_feedback_state = system_info_.sync_feedback_state;
    info.sync_drive = system_info_.sync_drive;
    info.clog_detection = system_info_.clog_detection;
    info.encoder_flow_rate = system_info_.encoder_flow_rate;
    info.toolchange_purge_volume = system_info_.toolchange_purge_volume;

    // Copy unit-level metadata not managed by registry
    for (size_t u = 0; u < info.units.size() && u < system_info_.units.size(); ++u) {
        info.units[u].connected = system_info_.units[u].connected;
        info.units[u].has_encoder = system_info_.units[u].has_encoder;
        info.units[u].has_toolhead_sensor = system_info_.units[u].has_toolhead_sensor;
        info.units[u].has_slot_sensors = system_info_.units[u].has_slot_sensors;
        info.units[u].topology = system_info_.units[u].topology;
        info.units[u].has_hub_sensor = system_info_.units[u].has_hub_sensor;
        info.units[u].hub_sensor_triggered = system_info_.units[u].hub_sensor_triggered;
    }

    return info;
}

AmsType AmsBackendHappyHare::get_type() const {
    return AmsType::HAPPY_HARE;
}

bool AmsBackendHappyHare::manages_active_spool() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.spoolman_mode != SpoolmanMode::OFF;
}

SlotInfo AmsBackendHappyHare::get_slot_info(int slot_index) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto* entry = slots_.get(slot_index);
    if (entry) {
        return entry->info;
    }

    // Return empty slot info for invalid index
    SlotInfo empty;
    empty.slot_index = -1;
    empty.global_index = -1;
    return empty;
}

// get_current_action(), get_current_tool(), get_current_slot(), is_filament_loaded()
// provided by AmsSubscriptionBackend

PathTopology AmsBackendHappyHare::get_topology() const {
    // Happy Hare uses a linear selector topology (ERCF-style)
    return PathTopology::LINEAR;
}

PathSegment AmsBackendHappyHare::get_filament_segment() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Convert Happy Hare filament_pos to unified PathSegment
    return path_segment_from_happy_hare_pos(filament_pos_);
}

PathSegment AmsBackendHappyHare::get_slot_filament_segment(int slot_index) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if this is the active slot - return the current filament segment
    if (slot_index == system_info_.current_slot && system_info_.filament_loaded) {
        return path_segment_from_happy_hare_pos(filament_pos_);
    }

    // For non-active slots, check pre-gate sensor first for better visualization
    const auto* entry = slots_.get(slot_index);
    if (entry) {
        if (entry->sensors.has_pre_gate_sensor && entry->sensors.pre_gate_triggered) {
            return PathSegment::PREP; // Filament detected at pre-gate sensor
        }

        // Fall back to gate_status for slots without pre-gate sensors
        if (entry->info.status == SlotStatus::AVAILABLE ||
            entry->info.status == SlotStatus::FROM_BUFFER) {
            return PathSegment::SPOOL; // Filament at spool ready position
        }
    }

    return PathSegment::NONE;
}

PathSegment AmsBackendHappyHare::infer_error_segment() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_segment_;
}

bool AmsBackendHappyHare::slot_has_prep_sensor(int slot_index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto* entry = slots_.get(slot_index);
    if (!entry) {
        return false;
    }
    return entry->sensors.has_pre_gate_sensor;
}

// ============================================================================
// Moonraker Status Update Handling
// ============================================================================

void AmsBackendHappyHare::handle_status_update(const nlohmann::json& notification) {
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

    // Check if this notification contains MMU data
    if (!params.contains("mmu")) {
        return;
    }

    const auto& mmu_data = params["mmu"];
    if (!mmu_data.is_object()) {
        return;
    }

    spdlog::trace("[AMS HappyHare] Received MMU status update");

    {
        std::lock_guard<std::mutex> lock(mutex_);
        parse_mmu_state(mmu_data);
    }

    emit_event(EVENT_STATE_CHANGED);
}

void AmsBackendHappyHare::parse_mmu_state(const nlohmann::json& mmu_data) {
    // Parse current gate: printer.mmu.gate
    // -1 = no gate selected, -2 = bypass
    if (mmu_data.contains("gate") && mmu_data["gate"].is_number_integer()) {
        system_info_.current_slot = mmu_data["gate"].get<int>();
        spdlog::trace("[AMS HappyHare] Current slot: {}", system_info_.current_slot);
    }

    // Parse current tool: printer.mmu.tool
    if (mmu_data.contains("tool") && mmu_data["tool"].is_number_integer()) {
        system_info_.current_tool = mmu_data["tool"].get<int>();
        spdlog::trace("[AMS HappyHare] Current tool: {}", system_info_.current_tool);
    }

    // Parse filament loaded state: printer.mmu.filament
    // Values: "Loaded", "Unloaded"
    if (mmu_data.contains("filament") && mmu_data["filament"].is_string()) {
        std::string filament_state = mmu_data["filament"].get<std::string>();
        system_info_.filament_loaded = (filament_state == "Loaded");
        spdlog::trace("[AMS HappyHare] Filament loaded: {}", system_info_.filament_loaded);
    }

    // Parse reason_for_pause: descriptive error message from Happy Hare
    if (mmu_data.contains("reason_for_pause") && mmu_data["reason_for_pause"].is_string()) {
        reason_for_pause_ = mmu_data["reason_for_pause"].get<std::string>();
        spdlog::trace("[AMS HappyHare] Reason for pause: {}", reason_for_pause_);
    }

    // Parse action: printer.mmu.action
    // Values: "Idle", "Loading", "Unloading", "Forming Tip", "Heating", "Checking", etc.
    if (mmu_data.contains("action") && mmu_data["action"].is_string()) {
        std::string action_str = mmu_data["action"].get<std::string>();
        AmsAction prev_action = system_info_.action;
        system_info_.action = ams_action_from_string(action_str);
        system_info_.operation_detail = action_str;
        spdlog::trace("[AMS HappyHare] Action: {} ({})", ams_action_to_string(system_info_.action),
                      action_str);

        // Clear error segment when recovering to idle
        if (prev_action == AmsAction::ERROR && system_info_.action == AmsAction::IDLE) {
            error_segment_ = PathSegment::NONE;
            reason_for_pause_.clear();

            // Clear slot errors on all slots
            for (int i = 0; i < slots_.slot_count(); ++i) {
                auto* entry = slots_.get_mut(i);
                if (entry && entry->info.error.has_value()) {
                    entry->info.error.reset();
                    spdlog::debug("[AMS HappyHare] Cleared error on slot {}", i);
                }
            }
        }

        // Set slot error when entering error state
        if (system_info_.action == AmsAction::ERROR && prev_action != AmsAction::ERROR) {
            error_segment_ = path_segment_from_happy_hare_pos(filament_pos_);

            // Set error on current slot (if valid)
            if (system_info_.current_slot >= 0) {
                auto* entry = slots_.get_mut(system_info_.current_slot);
                if (entry) {
                    SlotError err;
                    // Use reason_for_pause if available; fall back to operation_detail
                    if (!reason_for_pause_.empty()) {
                        err.message = reason_for_pause_;
                    } else {
                        err.message = action_str;
                    }
                    err.severity = SlotError::ERROR;
                    entry->info.error = err;
                    spdlog::debug("[AMS HappyHare] Error on slot {}: {}", system_info_.current_slot,
                                  err.message);
                }
            }
        }
    }

    // Parse filament_pos: printer.mmu.filament_pos
    // Values: 0=unloaded, 1-2=gate area, 3=in bowden, 4=end bowden, 5=homed extruder,
    //         6=extruder entry, 7-8=loaded
    if (mmu_data.contains("filament_pos") && mmu_data["filament_pos"].is_number_integer()) {
        filament_pos_ = mmu_data["filament_pos"].get<int>();
        spdlog::trace("[AMS HappyHare] Filament pos: {} -> {}", filament_pos_,
                      path_segment_to_string(path_segment_from_happy_hare_pos(filament_pos_)));

        // Update hub_sensor_triggered on units based on filament position
        // pos >= 3 means filament is in bowden or further (past the selector/hub)
        bool past_hub = (filament_pos_ >= 3);
        for (auto& unit : system_info_.units) {
            // Active unit: determined by current_slot falling within this unit's range
            int slot = system_info_.current_slot;
            if (slot >= unit.first_slot_global_index &&
                slot < unit.first_slot_global_index + unit.slot_count) {
                unit.hub_sensor_triggered = past_hub;
            } else {
                unit.hub_sensor_triggered = false;
            }
        }
    }

    // Parse bowden_progress: printer.mmu.bowden_progress (v4)
    // 0-100 = loading progress percentage, -1 = not applicable
    if (mmu_data.contains("bowden_progress") && mmu_data["bowden_progress"].is_number_integer()) {
        bowden_progress_ = std::clamp(mmu_data["bowden_progress"].get<int>(), -1, 100);
        spdlog::trace("[AMS HappyHare] Bowden progress: {}%", bowden_progress_);
    }

    // Parse has_bypass: printer.mmu.has_bypass
    // Not all MMU types support bypass (e.g., ERCF/Tradrack do, BoxTurtle does not)
    if (mmu_data.contains("has_bypass") && mmu_data["has_bypass"].is_boolean()) {
        system_info_.supports_bypass = mmu_data["has_bypass"].get<bool>();
        spdlog::trace("[AMS HappyHare] Bypass supported: {}", system_info_.supports_bypass);
    }

    // Parse num_units if available (multi-unit Happy Hare setups)
    if (mmu_data.contains("num_units") && mmu_data["num_units"].is_number_integer()) {
        num_units_ = mmu_data["num_units"].get<int>();
        if (num_units_ < 1)
            num_units_ = 1;
        spdlog::trace("[AMS HappyHare] Number of units: {}", num_units_);
    }

    // Parse num_gates for dissimilar multi-unit (v4)
    // Can be a string like "6,4" for 6-gate ERCF + 4-gate Box Turtle, or plain int
    if (mmu_data.contains("num_gates")) {
        const auto& ng = mmu_data["num_gates"];
        if (ng.is_string()) {
            // Parse comma-separated per-unit gate counts (v4 dissimilar multi-MMU)
            std::string ng_str = ng.get<std::string>();
            std::vector<int> counts;
            std::istringstream iss(ng_str);
            std::string token;
            while (std::getline(iss, token, ',')) {
                try {
                    int count = std::stoi(token);
                    if (count > 0) {
                        counts.push_back(count);
                    } else {
                        spdlog::warn("[AMS HappyHare] Ignoring non-positive gate count {} in "
                                     "num_gates string",
                                     count);
                    }
                } catch (...) {
                    spdlog::warn("[AMS HappyHare] Ignoring invalid token in num_gates string");
                }
            }
            if (!counts.empty()) {
                per_unit_gate_counts_ = counts;
                spdlog::debug("[AMS HappyHare] Per-unit gate counts from num_gates string: {}",
                              ng_str);
            }
        }
    }

    // Parse unit_gate_counts array if present (future-proof for v4)
    if (mmu_data.contains("unit_gate_counts") && mmu_data["unit_gate_counts"].is_array()) {
        std::vector<int> counts;
        for (const auto& c : mmu_data["unit_gate_counts"]) {
            if (c.is_number_integer()) {
                counts.push_back(c.get<int>());
            }
        }
        if (!counts.empty()) {
            per_unit_gate_counts_ = counts;
            spdlog::debug("[AMS HappyHare] Per-unit gate counts from unit_gate_counts array");
        }
    }

    // Parse active unit: printer.mmu.unit (v4)
    if (mmu_data.contains("unit") && mmu_data["unit"].is_number_integer()) {
        active_unit_ = mmu_data["unit"].get<int>();
        spdlog::trace("[AMS HappyHare] Active unit: {}", active_unit_);
    }

    // Parse gate_status array: printer.mmu.gate_status
    // Values: -1 = unknown, 0 = empty, 1 = available, 2 = from_buffer
    if (mmu_data.contains("gate_status") && mmu_data["gate_status"].is_array()) {
        const auto& gate_status = mmu_data["gate_status"];
        int gate_count = static_cast<int>(gate_status.size());

        // Initialize gates if this is the first time we see gate_status
        if (!slots_.is_initialized() && gate_count > 0) {
            initialize_slots(gate_count);
        }

        // Update gate status values via SlotRegistry
        for (size_t i = 0; i < gate_status.size(); ++i) {
            if (gate_status[i].is_number_integer()) {
                int hh_status = gate_status[i].get<int>();
                SlotStatus status = slot_status_from_happy_hare(hh_status);

                // Mark the currently loaded slot as LOADED instead of AVAILABLE
                if (system_info_.filament_loaded &&
                    static_cast<int>(i) == system_info_.current_slot &&
                    status == SlotStatus::AVAILABLE) {
                    status = SlotStatus::LOADED;
                }

                auto* entry = slots_.get_mut(static_cast<int>(i));
                if (entry) {
                    entry->info.status = status;
                }
            }
        }
    }

    // Parse gate_color_rgb array: printer.mmu.gate_color_rgb
    // Values are RGB integers like 0xFF0000 for red
    if (mmu_data.contains("gate_color_rgb") && mmu_data["gate_color_rgb"].is_array()) {
        const auto& colors = mmu_data["gate_color_rgb"];
        for (size_t i = 0; i < colors.size(); ++i) {
            if (colors[i].is_number_integer()) {
                auto* entry = slots_.get_mut(static_cast<int>(i));
                if (entry) {
                    entry->info.color_rgb = static_cast<uint32_t>(colors[i].get<int>());
                }
            }
        }
    }

    // Parse gate_material array: printer.mmu.gate_material
    // Values are strings like "PLA", "PETG", "ABS"
    if (mmu_data.contains("gate_material") && mmu_data["gate_material"].is_array()) {
        const auto& materials = mmu_data["gate_material"];
        for (size_t i = 0; i < materials.size(); ++i) {
            if (materials[i].is_string()) {
                auto* entry = slots_.get_mut(static_cast<int>(i));
                if (entry) {
                    entry->info.material = materials[i].get<std::string>();
                }
            }
        }
    }

    // === Happy Hare v4 extended status fields ===

    // Parse espooler_active: printer.mmu.espooler_active (v4)
    if (mmu_data.contains("espooler_active") && mmu_data["espooler_active"].is_string()) {
        system_info_.espooler_state = mmu_data["espooler_active"].get<std::string>();
        spdlog::trace("[AMS HappyHare] eSpooler state: {}", system_info_.espooler_state);
    }

    // Parse sync_feedback_state: printer.mmu.sync_feedback_state (v4)
    if (mmu_data.contains("sync_feedback_state") && mmu_data["sync_feedback_state"].is_string()) {
        system_info_.sync_feedback_state = mmu_data["sync_feedback_state"].get<std::string>();
        spdlog::trace("[AMS HappyHare] Sync feedback: {}", system_info_.sync_feedback_state);
    }

    // Parse sync_drive: printer.mmu.sync_drive (v4)
    if (mmu_data.contains("sync_drive") && mmu_data["sync_drive"].is_boolean()) {
        system_info_.sync_drive = mmu_data["sync_drive"].get<bool>();
        spdlog::trace("[AMS HappyHare] Sync drive: {}", system_info_.sync_drive);
    }

    // Parse clog_detection_enabled: printer.mmu.clog_detection_enabled (v4)
    // 0=off, 1=manual, 2=auto
    if (mmu_data.contains("clog_detection_enabled") &&
        mmu_data["clog_detection_enabled"].is_number_integer()) {
        system_info_.clog_detection = mmu_data["clog_detection_enabled"].get<int>();
        spdlog::trace("[AMS HappyHare] Clog detection: {}", system_info_.clog_detection);
    }

    // Parse encoder.flow_rate: printer.mmu.encoder.flow_rate (v4, nested)
    if (mmu_data.contains("encoder") && mmu_data["encoder"].is_object()) {
        const auto& encoder = mmu_data["encoder"];
        if (encoder.contains("flow_rate") && encoder["flow_rate"].is_number_integer()) {
            system_info_.encoder_flow_rate = encoder["flow_rate"].get<int>();
            spdlog::trace("[AMS HappyHare] Encoder flow rate: {}", system_info_.encoder_flow_rate);
        }
    }

    // Parse toolchange_purge_volume: printer.mmu.toolchange_purge_volume (v4)
    if (mmu_data.contains("toolchange_purge_volume") &&
        mmu_data["toolchange_purge_volume"].is_number()) {
        system_info_.toolchange_purge_volume = mmu_data["toolchange_purge_volume"].get<float>();
        spdlog::trace("[AMS HappyHare] Toolchange purge volume: {:.1f}",
                      system_info_.toolchange_purge_volume);
    }

    // Parse spoolman_support: printer.mmu.spoolman_support (v4)
    if (mmu_data.contains("spoolman_support") && mmu_data["spoolman_support"].is_string()) {
        system_info_.spoolman_mode =
            spoolman_mode_from_string(mmu_data["spoolman_support"].get<std::string>());
        spdlog::trace("[AMS HappyHare] Spoolman mode: {}",
                      spoolman_mode_to_string(system_info_.spoolman_mode));
    }

    // Parse pending_spool_id: printer.mmu.pending_spool_id (v4)
    if (mmu_data.contains("pending_spool_id") && mmu_data["pending_spool_id"].is_number_integer()) {
        system_info_.pending_spool_id = mmu_data["pending_spool_id"].get<int>();
        spdlog::trace("[AMS HappyHare] Pending spool ID: {}", system_info_.pending_spool_id);
    }

    // Parse gate_temperature array: printer.mmu.gate_temperature (v4)
    // Per-gate nozzle temperature recommendations
    if (mmu_data.contains("gate_temperature") && mmu_data["gate_temperature"].is_array()) {
        const auto& gate_temps = mmu_data["gate_temperature"];
        for (size_t i = 0; i < gate_temps.size(); ++i) {
            if (gate_temps[i].is_number()) {
                auto* entry = slots_.get_mut(static_cast<int>(i));
                if (entry) {
                    int temp = gate_temps[i].get<int>();
                    entry->info.nozzle_temp_min = temp;
                    entry->info.nozzle_temp_max = temp;
                }
            }
        }
        spdlog::trace("[AMS HappyHare] Parsed gate_temperature for {} gates", gate_temps.size());
    }

    // Parse gate_name array: printer.mmu.gate_name (v4)
    // Per-gate filament names
    if (mmu_data.contains("gate_name") && mmu_data["gate_name"].is_array()) {
        const auto& gate_names = mmu_data["gate_name"];
        for (size_t i = 0; i < gate_names.size(); ++i) {
            if (gate_names[i].is_string()) {
                auto* entry = slots_.get_mut(static_cast<int>(i));
                if (entry) {
                    entry->info.color_name = gate_names[i].get<std::string>();
                }
            }
        }
        spdlog::trace("[AMS HappyHare] Parsed gate_name for {} gates", gate_names.size());
    }

    // Parse ttg_map (tool-to-gate mapping) if available
    if (mmu_data.contains("ttg_map") && mmu_data["ttg_map"].is_array()) {
        const auto& ttg_map = mmu_data["ttg_map"];
        std::vector<int> ttg_vec;
        ttg_vec.reserve(ttg_map.size());

        for (const auto& mapping : ttg_map) {
            if (mapping.is_number_integer()) {
                ttg_vec.push_back(mapping.get<int>());
            }
        }

        // Update both legacy and registry tool maps
        system_info_.tool_to_slot_map = ttg_vec;
        slots_.set_tool_map(ttg_vec);
    }

    // Parse sensors dict: printer.mmu.sensors
    // Keys matching "mmu_pre_gate_X" indicate pre-gate sensors per gate.
    // Values: true (triggered/filament present), false (not triggered), null (error/unknown)
    if (mmu_data.contains("sensors") && mmu_data["sensors"].is_object()) {
        const auto& sensors = mmu_data["sensors"];
        const std::string prefix = "mmu_pre_gate_";
        bool any_sensor = false;

        for (auto it = sensors.begin(); it != sensors.end(); ++it) {
            const std::string& key = it.key();
            if (key.rfind(prefix, 0) != 0) {
                continue; // Not a pre-gate sensor key
            }

            // Extract gate index from key suffix
            std::string index_str = key.substr(prefix.size());
            int gate_idx = -1;
            try {
                gate_idx = std::stoi(index_str);
            } catch (...) {
                continue; // Not a valid integer suffix
            }

            if (gate_idx < 0) {
                continue;
            }

            auto* entry = slots_.get_mut(gate_idx);
            if (!entry) {
                continue;
            }

            entry->sensors.has_pre_gate_sensor = true;
            entry->sensors.pre_gate_triggered = it.value().is_boolean() && it.value().get<bool>();
            any_sensor = true;

            spdlog::trace("[AMS HappyHare] Pre-gate sensor {}: present=true, triggered={}",
                          gate_idx, entry->sensors.pre_gate_triggered);
        }

        // Update has_slot_sensors flag on units based on actual sensor data
        for (auto& unit : system_info_.units) {
            unit.has_slot_sensors = any_sensor;
        }
    }

    // Parse drying_state: printer.mmu.drying_state (v4 - KMS/EMU hardware)
    if (mmu_data.contains("drying_state") && mmu_data["drying_state"].is_object()) {
        const auto& drying = mmu_data["drying_state"];
        dryer_info_.supported = true;

        if (drying.contains("active") && drying["active"].is_boolean()) {
            dryer_info_.active = drying["active"].get<bool>();
        }
        if (drying.contains("current_temp") && drying["current_temp"].is_number()) {
            dryer_info_.current_temp_c = drying["current_temp"].get<float>();
        }
        if (drying.contains("target_temp") && drying["target_temp"].is_number()) {
            dryer_info_.target_temp_c = drying["target_temp"].get<float>();
        }
        if (drying.contains("remaining_min") && drying["remaining_min"].is_number_integer()) {
            dryer_info_.remaining_min = drying["remaining_min"].get<int>();
        }
        if (drying.contains("duration_min") && drying["duration_min"].is_number_integer()) {
            dryer_info_.duration_min = drying["duration_min"].get<int>();
        }
        if (drying.contains("fan_pct") && drying["fan_pct"].is_number_integer()) {
            dryer_info_.fan_pct = drying["fan_pct"].get<int>();
        }

        spdlog::trace("[AMS HappyHare] Dryer state: active={}, temp={:.1f}→{:.1f}°C, {}min left",
                      dryer_info_.active, dryer_info_.current_temp_c, dryer_info_.target_temp_c,
                      dryer_info_.remaining_min);
    }

    // Parse endless_spool_groups if available
    if (mmu_data.contains("endless_spool_groups") && mmu_data["endless_spool_groups"].is_array()) {
        const auto& es_groups = mmu_data["endless_spool_groups"];
        for (size_t i = 0; i < es_groups.size(); ++i) {
            if (es_groups[i].is_number_integer()) {
                auto* entry = slots_.get_mut(static_cast<int>(i));
                if (entry) {
                    entry->info.endless_spool_group = es_groups[i].get<int>();
                }
            }
        }
    }
}

void AmsBackendHappyHare::initialize_slots(int gate_count) {
    spdlog::info("[AMS HappyHare] Initializing {} slots across {} units", gate_count, num_units_);

    system_info_.units.clear();

    // Determine per-unit gate counts:
    // 1. Use per_unit_gate_counts_ if available (v4 dissimilar multi-MMU)
    // 2. Fall back to even split (v3 or identical units)
    std::vector<int> unit_counts;
    if (!per_unit_gate_counts_.empty() &&
        static_cast<int>(per_unit_gate_counts_.size()) == num_units_) {
        // Verify total matches
        int total = 0;
        for (int c : per_unit_gate_counts_)
            total += c;
        if (total == gate_count) {
            unit_counts = per_unit_gate_counts_;
            spdlog::info("[AMS HappyHare] Using dissimilar per-unit gate counts");
        } else {
            spdlog::warn(
                "[AMS HappyHare] Per-unit gate counts sum ({}) != gate_count ({}), falling back",
                total, gate_count);
        }
    }

    // Fallback: even split
    if (unit_counts.empty()) {
        int gates_per_unit = (num_units_ > 1) ? (gate_count / num_units_) : gate_count;
        int remaining = gate_count;
        for (int u = 0; u < num_units_; ++u) {
            int count = (u == num_units_ - 1) ? remaining : gates_per_unit;
            unit_counts.push_back(count);
            remaining -= count;
        }
    }

    int global_offset = 0;
    for (int u = 0; u < num_units_; ++u) {
        int unit_gates = unit_counts[u];

        AmsUnit unit;
        unit.unit_index = u;
        if (num_units_ > 1) {
            unit.name = fmt::format("MMU Unit {}", u + 1);
        } else {
            unit.name = "Happy Hare MMU";
        }
        unit.slot_count = unit_gates;
        unit.first_slot_global_index = global_offset;
        unit.connected = true;
        unit.has_encoder = true;
        unit.has_toolhead_sensor = true;
        // has_slot_sensors starts false; updated when sensor data arrives in parse_mmu_state()
        unit.has_slot_sensors = false;
        unit.has_hub_sensor = true; // HH selector functions as hub equivalent

        for (int i = 0; i < unit_gates; ++i) {
            SlotInfo slot;
            slot.slot_index = i;
            slot.global_index = global_offset + i;
            slot.status = SlotStatus::UNKNOWN;
            slot.mapped_tool = global_offset + i;
            slot.color_rgb = AMS_DEFAULT_SLOT_COLOR;
            unit.slots.push_back(slot);
        }

        system_info_.units.push_back(unit);
        global_offset += unit_gates;
    }

    system_info_.total_slots = gate_count;

    // Initialize tool-to-gate mapping (1:1 default)
    system_info_.tool_to_slot_map.clear();
    system_info_.tool_to_slot_map.reserve(gate_count);
    for (int i = 0; i < gate_count; ++i) {
        system_info_.tool_to_slot_map.push_back(i);
    }

    // Initialize SlotRegistry alongside legacy state (uses same unit_counts)
    {
        std::vector<std::pair<std::string, std::vector<std::string>>> sr_units;
        int sr_offset = 0;
        for (int u = 0; u < num_units_; ++u) {
            int count = unit_counts[u];
            std::vector<std::string> names;
            for (int g = 0; g < count; ++g) {
                names.push_back(std::to_string(sr_offset + g));
            }
            std::string unit_name = "Unit " + std::to_string(u + 1);
            if (num_units_ == 1) {
                unit_name = "MMU";
            }
            sr_units.push_back({unit_name, names});
            sr_offset += count;
        }
        slots_.initialize_units(sr_units);
    }
}

void AmsBackendHappyHare::query_tip_method_from_config() {
    if (!client_) {
        return;
    }

    // Query configfile.settings.mmu to read form_tip_macro.
    // Happy Hare uses the same logic internally: if the macro name contains "cut",
    // it's a cutter system (e.g., _MMU_CUT_TIP). Otherwise it's tip-forming.
    nlohmann::json params = {{"objects", nlohmann::json::object({{"configfile", {"settings"}}})}};

    std::weak_ptr<std::atomic<bool>> weak_alive = alive_;
    client_->send_jsonrpc(
        "printer.objects.query", params,
        [this, weak_alive](nlohmann::json response) {
            auto alive_lock = weak_alive.lock();
            if (!alive_lock || !alive_lock->load())
                return;
            try {
                const auto& settings = response["result"]["status"]["configfile"]["settings"];

                if (!settings.contains("mmu") || !settings["mmu"].is_object()) {
                    spdlog::debug("[AMS HappyHare] No mmu section in configfile settings");
                    return;
                }

                const auto& mmu_cfg = settings["mmu"];
                TipMethod method = TipMethod::NONE;

                if (mmu_cfg.contains("form_tip_macro") && mmu_cfg["form_tip_macro"].is_string()) {
                    std::string macro = mmu_cfg["form_tip_macro"].get<std::string>();

                    // Convert to lowercase for comparison (same as Happy Hare)
                    std::string lower_macro = macro;
                    for (auto& c : lower_macro) {
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }

                    if (lower_macro.find("cut") != std::string::npos) {
                        method = TipMethod::CUT;
                    } else {
                        method = TipMethod::TIP_FORM;
                    }

                    spdlog::info("[AMS HappyHare] Tip method from config: {} (form_tip_macro={})",
                                 tip_method_to_string(method), macro);
                } else {
                    // No form_tip_macro configured — default to tip-forming
                    // (Happy Hare default macro is _MMU_FORM_TIP, not a cutter)
                    method = TipMethod::TIP_FORM;
                    spdlog::info(
                        "[AMS HappyHare] No form_tip_macro in config, defaulting to TIP_FORM");
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    system_info_.tip_method = method;
                }

                emit_event(EVENT_STATE_CHANGED);
            } catch (const nlohmann::json::exception& e) {
                spdlog::warn("[AMS HappyHare] Failed to parse configfile for tip method: {}",
                             e.what());
            }
        },
        [](const MoonrakerError& err) {
            spdlog::warn("[AMS HappyHare] Failed to query configfile for tip method: {}",
                         err.message);
        });
}

// ============================================================================
// Filament Operations
// ============================================================================

// check_preconditions() provided by AmsSubscriptionBackend

AmsError AmsBackendHappyHare::validate_slot_index(int gate_index) const {
    if (!slots_.is_valid_index(gate_index)) {
        return AmsErrorHelper::invalid_slot(gate_index,
                                            slots_.slot_count() > 0 ? slots_.slot_count() - 1 : 0);
    }
    return AmsErrorHelper::success();
}

// execute_gcode() provided by AmsSubscriptionBackend

AmsError AmsBackendHappyHare::load_filament(int slot_index) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        AmsError gate_valid = validate_slot_index(slot_index);
        if (!gate_valid) {
            return gate_valid;
        }

        // Check if slot has filament available
        const auto* entry = slots_.get(slot_index);
        if (entry && entry->info.status == SlotStatus::EMPTY) {
            return AmsErrorHelper::slot_not_available(slot_index);
        }
    }

    // Send MMU_LOAD GATE={n} command (Happy Hare uses "gate" in its API)
    std::ostringstream cmd;
    cmd << "MMU_LOAD GATE=" << slot_index;

    spdlog::info("[AMS HappyHare] Loading from slot {}", slot_index);
    return execute_gcode(cmd.str());
}

AmsError AmsBackendHappyHare::unload_filament() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        if (!system_info_.filament_loaded) {
            return AmsError(AmsResult::WRONG_STATE, "No filament loaded", "No filament to unload",
                            "Load filament first");
        }
    }

    spdlog::info("[AMS HappyHare] Unloading filament");
    return execute_gcode("MMU_UNLOAD");
}

AmsError AmsBackendHappyHare::select_slot(int slot_index) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        AmsError gate_valid = validate_slot_index(slot_index);
        if (!gate_valid) {
            return gate_valid;
        }
    }

    // Send MMU_SELECT GATE={n} command (Happy Hare uses "gate" in its API)
    std::ostringstream cmd;
    cmd << "MMU_SELECT GATE=" << slot_index;

    spdlog::info("[AMS HappyHare] Selecting slot {}", slot_index);
    return execute_gcode(cmd.str());
}

AmsError AmsBackendHappyHare::change_tool(int tool_number) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

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

    spdlog::info("[AMS HappyHare] Tool change to T{}", tool_number);
    return execute_gcode(cmd.str());
}

// ============================================================================
// Recovery Operations
// ============================================================================

AmsError AmsBackendHappyHare::recover() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("Happy Hare backend not started");
        }
    }

    spdlog::info("[AMS HappyHare] Initiating recovery");
    return execute_gcode("MMU_RECOVER");
}

AmsError AmsBackendHappyHare::reset() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }
    }

    // Happy Hare uses MMU_HOME to reset to a known state
    spdlog::info("[AMS HappyHare] Resetting (homing selector)");
    return execute_gcode("MMU_HOME");
}

AmsError AmsBackendHappyHare::reset_lane(int slot_index) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("Happy Hare backend not started");
        }

        AmsError slot_err = validate_slot_index(slot_index);
        if (!slot_err) {
            return slot_err;
        }
    }

    // MMU_RECOVER with GATE parameter recovers a specific gate's state
    spdlog::info("[AMS HappyHare] Recovering gate {}", slot_index);
    return execute_gcode("MMU_RECOVER GATE=" + std::to_string(slot_index));
}

AmsError AmsBackendHappyHare::eject_lane(int slot_index) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        AmsError slot_err = validate_slot_index(slot_index);
        if (!slot_err) {
            return slot_err;
        }
    }

    // MMU_EJECT fully ejects filament from the gate so the spool can be removed.
    // If filament is loaded it acts like MMU_UNLOAD first, then ejects from gate.
    spdlog::info("[AMS HappyHare] Ejecting gate {}", slot_index);
    return execute_gcode("MMU_EJECT GATE=" + std::to_string(slot_index));
}

AmsError AmsBackendHappyHare::cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("Happy Hare backend not started");
        }

        if (system_info_.action == AmsAction::IDLE) {
            return AmsErrorHelper::success(); // Nothing to cancel
        }
    }

    // MMU_PAUSE can be used to stop current operation
    spdlog::info("[AMS HappyHare] Cancelling current operation");
    return execute_gcode("MMU_PAUSE");
}

// ============================================================================
// Configuration Operations
// ============================================================================

AmsError AmsBackendHappyHare::set_slot_info(int slot_index, const SlotInfo& info, bool persist) {
    int old_spoolman_id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!slots_.is_valid_index(slot_index)) {
            return AmsErrorHelper::invalid_slot(
                slot_index, slots_.slot_count() > 0 ? slots_.slot_count() - 1 : 0);
        }

        auto* entry = slots_.get_mut(slot_index);
        if (!entry) {
            return AmsErrorHelper::invalid_slot(
                slot_index, slots_.slot_count() > 0 ? slots_.slot_count() - 1 : 0);
        }

        auto& slot = entry->info;

        // Capture old spoolman_id BEFORE updating (needed to detect clearing)
        old_spoolman_id = slot.spoolman_id;

        // Detect whether anything actually changed
        bool changed = slot.color_name != info.color_name || slot.color_rgb != info.color_rgb ||
                       slot.material != info.material || slot.brand != info.brand ||
                       slot.spoolman_id != info.spoolman_id || slot.spool_name != info.spool_name ||
                       slot.remaining_weight_g != info.remaining_weight_g ||
                       slot.total_weight_g != info.total_weight_g ||
                       slot.nozzle_temp_min != info.nozzle_temp_min ||
                       slot.nozzle_temp_max != info.nozzle_temp_max ||
                       slot.bed_temp != info.bed_temp;

        // Update local state
        slot.color_name = info.color_name;
        slot.color_rgb = info.color_rgb;
        slot.material = info.material;
        slot.brand = info.brand;
        slot.spoolman_id = info.spoolman_id;
        slot.spool_name = info.spool_name;
        slot.remaining_weight_g = info.remaining_weight_g;
        slot.total_weight_g = info.total_weight_g;
        slot.nozzle_temp_min = info.nozzle_temp_min;
        slot.nozzle_temp_max = info.nozzle_temp_max;
        slot.bed_temp = info.bed_temp;

        if (changed) {
            spdlog::info("[AMS HappyHare] Updated slot {} info: {} {}", slot_index, info.material,
                         info.color_name);
        }
    }

    // Persist via MMU_GATE_MAP command (Happy Hare stores in mmu_vars.cfg automatically).
    // Skip persistence when persist=false — used by Spoolman weight polling to update
    // in-memory state without sending G-code back to firmware. Without this guard,
    // weight updates would trigger MMU_GATE_MAP → firmware status_update WebSocket
    // event → sync_from_backend → refresh_spoolman_weights → set_slot_info again,
    // creating an infinite feedback loop.
    if (persist) {
        bool has_changes = false;
        std::string cmd = fmt::format("MMU_GATE_MAP GATE={}", slot_index);

        // Color (hex format, no # prefix)
        if (info.color_rgb != 0 && info.color_rgb != AMS_DEFAULT_SLOT_COLOR) {
            cmd += fmt::format(" COLOR={:06X}", info.color_rgb & 0xFFFFFF);
            has_changes = true;
        }

        // Material (validate to prevent command injection)
        if (!info.material.empty() && MoonrakerAPI::is_safe_gcode_param(info.material)) {
            cmd += fmt::format(" MATERIAL={}", info.material);
            has_changes = true;
        } else if (!info.material.empty()) {
            spdlog::warn("[AMS HappyHare] Skipping MATERIAL - unsafe characters in: {}",
                         info.material);
        }

        // Spoolman ID (-1 to clear)
        if (info.spoolman_id > 0) {
            cmd += fmt::format(" SPOOLID={}", info.spoolman_id);
            has_changes = true;
        } else if (info.spoolman_id == 0 && old_spoolman_id > 0) {
            cmd += " SPOOLID=-1"; // Clear existing link
            has_changes = true;
        }

        // Only send command if there are actual changes to persist
        if (has_changes) {
            execute_gcode(cmd);
            spdlog::debug("[AMS HappyHare] Sent: {}", cmd);
        }
    }

    // Emit OUTSIDE the lock to avoid deadlock with callbacks
    emit_event(EVENT_SLOT_CHANGED, std::to_string(slot_index));

    return AmsErrorHelper::success();
}

AmsError AmsBackendHappyHare::set_tool_mapping(int tool_number, int slot_index) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (tool_number < 0 ||
            tool_number >= static_cast<int>(system_info_.tool_to_slot_map.size())) {
            return AmsError(AmsResult::INVALID_TOOL,
                            "Tool " + std::to_string(tool_number) + " out of range",
                            "Invalid tool number", "");
        }

        if (!slots_.is_valid_index(slot_index)) {
            return AmsErrorHelper::invalid_slot(
                slot_index, slots_.slot_count() > 0 ? slots_.slot_count() - 1 : 0);
        }

        // Check if another tool already maps to this slot
        for (size_t i = 0; i < system_info_.tool_to_slot_map.size(); ++i) {
            if (i != static_cast<size_t>(tool_number) &&
                system_info_.tool_to_slot_map[i] == slot_index) {
                spdlog::warn("[AMS HappyHare] Tool {} will share slot {} with tool {}", tool_number,
                             slot_index, i);
                break;
            }
        }
    }

    // Send MMU_TTG_MAP command to update tool-to-gate mapping (Happy Hare uses "gate" in its API)
    std::ostringstream cmd;
    cmd << "MMU_TTG_MAP TOOL=" << tool_number << " GATE=" << slot_index;

    spdlog::info("[AMS HappyHare] Mapping T{} to slot {}", tool_number, slot_index);
    return execute_gcode(cmd.str());
}

// ============================================================================
// Bypass Mode Operations
// ============================================================================

AmsError AmsBackendHappyHare::enable_bypass() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        if (!system_info_.supports_bypass) {
            return AmsError(AmsResult::WRONG_STATE, "Bypass not supported",
                            "This Happy Hare system does not support bypass mode", "");
        }
    }

    // Happy Hare uses MMU_SELECT_BYPASS to select bypass
    spdlog::info("[AMS HappyHare] Enabling bypass mode");
    return execute_gcode("MMU_SELECT_BYPASS");
}

AmsError AmsBackendHappyHare::disable_bypass() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("Happy Hare backend not started");
        }

        if (system_info_.current_slot != -2) {
            return AmsError(AmsResult::WRONG_STATE, "Bypass not active",
                            "Bypass mode is not currently active", "");
        }
    }

    // To disable bypass, select a gate or unload
    // MMU_SELECT GATE=0 or MMU_HOME will deselect bypass
    spdlog::info("[AMS HappyHare] Disabling bypass mode (homing selector)");
    return execute_gcode("MMU_HOME");
}

bool AmsBackendHappyHare::is_bypass_active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.current_slot == -2;
}

// ============================================================================
// Endless Spool Operations (Read-Only)
// ============================================================================

helix::printer::EndlessSpoolCapabilities
AmsBackendHappyHare::get_endless_spool_capabilities() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Happy Hare uses group-based endless spool configured in mmu_vars.cfg
    // UI can read but not modify the configuration
    return {true, false, "Happy Hare group-based"};
}

std::vector<helix::printer::EndlessSpoolConfig>
AmsBackendHappyHare::get_endless_spool_config() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<helix::printer::EndlessSpoolConfig> configs;

    if (!slots_.is_initialized()) {
        return configs;
    }

    // Iterate through all slots and find backup slots based on endless_spool_group
    for (int i = 0; i < slots_.slot_count(); ++i) {
        const auto* entry = slots_.get(i);
        if (!entry) {
            continue;
        }

        helix::printer::EndlessSpoolConfig config;
        config.slot_index = entry->info.global_index;
        config.backup_slot = -1; // Default: no backup

        if (entry->info.endless_spool_group >= 0) {
            // Find another slot in the same group
            for (int j = 0; j < slots_.slot_count(); ++j) {
                if (j == i) {
                    continue;
                }
                const auto* other = slots_.get(j);
                if (other && other->info.endless_spool_group == entry->info.endless_spool_group) {
                    config.backup_slot = other->info.global_index;
                    break; // Use first match
                }
            }
        }
        configs.push_back(config);
    }

    return configs;
}

AmsError AmsBackendHappyHare::set_endless_spool_backup(int slot_index, int backup_slot) {
    // Happy Hare endless spool is configured in mmu_vars.cfg, not via runtime G-code
    (void)slot_index;
    (void)backup_slot;
    return AmsErrorHelper::not_supported("Endless spool configuration");
}

AmsError AmsBackendHappyHare::reset_tool_mappings() {
    spdlog::info("[AMS HappyHare] Resetting tool mappings to 1:1");

    int tool_count = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tool_count = static_cast<int>(system_info_.tool_to_slot_map.size());
    }

    // Reset to 1:1 mapping (T0→Gate0, T1→Gate1, etc.)
    // Continue on failure to reset as many as possible, return first error
    AmsError first_error = AmsErrorHelper::success();
    for (int tool = 0; tool < tool_count; tool++) {
        AmsError result = set_tool_mapping(tool, tool);
        if (!result.success()) {
            spdlog::error("[AMS HappyHare] Failed to reset tool {} mapping: {}", tool,
                          result.technical_msg);
            if (first_error.success()) {
                first_error = result;
            }
        }
    }

    return first_error;
}

AmsError AmsBackendHappyHare::reset_endless_spool() {
    // Happy Hare endless spool is read-only (configured in mmu_vars.cfg)
    spdlog::warn("[AMS HappyHare] Endless spool reset not supported (read-only)");
    return AmsErrorHelper::not_supported("Happy Hare endless spool is read-only");
}

// ============================================================================
// Tool Mapping Operations
// ============================================================================

helix::printer::ToolMappingCapabilities AmsBackendHappyHare::get_tool_mapping_capabilities() const {
    // Happy Hare supports tool-to-gate mapping via MMU_TTG_MAP G-code
    return {true, true, "Tool-to-gate mapping via MMU_TTG_MAP"};
}

std::vector<int> AmsBackendHappyHare::get_tool_mapping() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.tool_to_slot_map;
}

// ============================================================================
// Dryer Control (v4 - KMS/EMU hardware)
// ============================================================================

DryerInfo AmsBackendHappyHare::get_dryer_info() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dryer_info_;
}

AmsError AmsBackendHappyHare::start_drying(float temp_c, int duration_min, int fan_pct) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!dryer_info_.supported) {
            return AmsErrorHelper::not_supported("Dryer not available on this hardware");
        }
    }

    std::string cmd = fmt::format("MMU_HEATER DRY=1 TEMP={:.0f} DURATION={}", temp_c, duration_min);
    if (fan_pct >= 0) {
        cmd += fmt::format(" FAN={}", fan_pct);
    }

    spdlog::info("[AMS HappyHare] Starting dryer: {:.0f}°C for {} min", temp_c, duration_min);
    return execute_gcode(cmd);
}

AmsError AmsBackendHappyHare::stop_drying() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!dryer_info_.supported) {
            return AmsErrorHelper::not_supported("Dryer not available on this hardware");
        }
    }

    spdlog::info("[AMS HappyHare] Stopping dryer");
    return execute_gcode("MMU_HEATER DRY=0");
}

// ============================================================================
// Device Management
// ============================================================================

std::vector<helix::printer::DeviceSection> AmsBackendHappyHare::get_device_sections() const {
    return helix::printer::hh_default_sections();
}

std::vector<helix::printer::DeviceAction> AmsBackendHappyHare::get_device_actions() const {
    return helix::printer::hh_default_actions();
}

AmsError AmsBackendHappyHare::execute_device_action(const std::string& action_id,
                                                    const std::any& value) {
    spdlog::info("[AMS HappyHare] Executing device action: {}", action_id);

    // Helper to extract a typed value from std::any with uniform error handling
    auto require_string = [&](const char* label) -> std::pair<std::string, AmsError> {
        if (!value.has_value()) {
            return {"", AmsError(AmsResult::WRONG_STATE, fmt::format("{} value required", label),
                                 "Missing value", fmt::format("Select a {}", label))};
        }
        try {
            return {std::any_cast<std::string>(value), AmsErrorHelper::success()};
        } catch (const std::bad_any_cast&) {
            return {"", AmsError(AmsResult::WRONG_STATE, fmt::format("Invalid {} type", label),
                                 "Invalid value type", fmt::format("Select a valid {}", label))};
        }
    };

    // --- Simple button actions (no value required) ---
    // clang-format off
    static const std::pair<const char*, const char*> button_actions[] = {
        {"calibrate_bowden",    "MMU_CALIBRATE_BOWDEN"},
        {"calibrate_encoder",   "MMU_CALIBRATE_ENCODER"},
        {"calibrate_gear",      "MMU_CALIBRATE_GEAR"},
        {"calibrate_gates",     "MMU_CALIBRATE_GATES"},
        {"calibrate_servo",     "MMU_SERVO"},
        {"test_grip",           "MMU_TEST_GRIP"},
        {"test_load",           "MMU_TEST_LOAD"},
        {"servo_buzz",          "MMU_SERVO BUZZ=1"},
        {"reset_servo_counter", "MMU_STATS COUNTER=servo RESET=1"},
        {"reset_blade_counter", "MMU_STATS COUNTER=cutter RESET=1"},
    };
    // clang-format on
    for (const auto& [id, gcode] : button_actions) {
        if (action_id == id) {
            return execute_gcode(gcode);
        }
    }

    // --- LED mode dropdown ---
    if (action_id == "led_mode") {
        auto [mode, err] = require_string("LED mode");
        if (!err)
            return err;
        return execute_gcode("MMU_LED EXIT_EFFECT=" + mode);
    }

    // --- Speed sliders ---
    if (action_id == "gear_load_speed" || action_id == "gear_unload_speed" ||
        action_id == "selector_speed") {
        if (!value.has_value()) {
            return AmsError(AmsResult::WRONG_STATE, "Speed value required", "Missing value",
                            "Provide a speed value");
        }
        try {
            float speed = std::any_cast<float>(value);
            if (speed < 10.0f || speed > 300.0f) {
                return AmsError(AmsResult::WRONG_STATE, "Speed must be 10-300 mm/s",
                                "Invalid value", "Enter a speed between 10 and 300 mm/s");
            }
            const char* param = "SELECTOR_MOVE_SPEED";
            if (action_id == "gear_load_speed")
                param = "GEAR_FROM_BUFFER_SPEED";
            else if (action_id == "gear_unload_speed")
                param = "GEAR_UNLOAD_SPEED";
            return execute_gcode(fmt::format("MMU_TEST_CONFIG {}={:.0f}", param, speed));
        } catch (const std::bad_any_cast&) {
            return AmsError(AmsResult::WRONG_STATE, "Invalid speed type", "Invalid value type",
                            "Provide a numeric value");
        }
    }

    // --- eSpooler mode dropdown ---
    if (action_id == "espooler_mode") {
        auto [mode, err] = require_string("eSpooler mode");
        if (!err)
            return err;
        return execute_gcode("MMU_ESPOOLER OPERATION=" + mode);
    }

    // --- Clog detection dropdown ---
    if (action_id == "clog_detection") {
        auto [mode_str, err] = require_string("clog detection mode");
        if (!err)
            return err;
        int mode_int = 0;
        if (mode_str == "Manual")
            mode_int = 1;
        else if (mode_str == "Auto")
            mode_int = 2;
        return execute_gcode(fmt::format("MMU_TEST_CONFIG CLOG_DETECTION={}", mode_int));
    }

    // --- Motors toggle ---
    if (action_id == "motors_toggle") {
        if (!value.has_value()) {
            return AmsError(AmsResult::WRONG_STATE, "Motor state value required", "Missing value",
                            "Provide on/off state");
        }
        try {
            bool enable = std::any_cast<bool>(value);
            return execute_gcode(enable ? "MMU_MOTORS_OFF HOLD=1" : "MMU_MOTORS_OFF");
        } catch (const std::bad_any_cast&) {
            return AmsError(AmsResult::WRONG_STATE, "Invalid motor state type",
                            "Invalid value type", "Provide a boolean value");
        }
    }

    return AmsErrorHelper::not_supported("Unknown action: " + action_id);
}
