// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_happy_hare.h"

#include "moonraker_api.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <sstream>

// ============================================================================
// Construction / Destruction
// ============================================================================

AmsBackendHappyHare::AmsBackendHappyHare(MoonrakerAPI* api, MoonrakerClient* client)
    : api_(api), client_(client) {
    // Initialize system info with Happy Hare defaults
    system_info_.type = AmsType::HAPPY_HARE;
    system_info_.type_name = "Happy Hare";
    system_info_.version = "unknown";
    system_info_.current_tool = -1;
    system_info_.current_slot = -1;
    system_info_.filament_loaded = false;
    system_info_.action = AmsAction::IDLE;
    system_info_.total_slots = 0;
    system_info_.supports_endless_spool = true;
    system_info_.supports_spoolman = true;
    system_info_.supports_tool_mapping = true;
    system_info_.supports_bypass = true;
    // Default to virtual bypass - Happy Hare typically uses selector movement to bypass position
    // TODO: Detect from Happy Hare configuration if hardware bypass sensor is present
    system_info_.has_hardware_bypass_sensor = false;

    spdlog::debug("[AMS HappyHare] Backend created");
}

AmsBackendHappyHare::~AmsBackendHappyHare() {
    // During static destruction (e.g., program exit), the mutex and client may be
    // in an invalid state. Release the subscription guard WITHOUT trying to
    // unsubscribe - the MoonrakerClient may already be destroyed.
    subscription_.release();
}

// ============================================================================
// Lifecycle Management
// ============================================================================

AmsError AmsBackendHappyHare::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_) {
        return AmsErrorHelper::success();
    }

    if (!client_) {
        spdlog::error("[AMS HappyHare] Cannot start: MoonrakerClient is null");
        return AmsErrorHelper::not_connected("MoonrakerClient not provided");
    }

    if (!api_) {
        spdlog::error("[AMS HappyHare] Cannot start: MoonrakerAPI is null");
        return AmsErrorHelper::not_connected("MoonrakerAPI not provided");
    }

    // Register for status update notifications from Moonraker
    // The MMU state comes via notify_status_update when printer.mmu.* changes
    SubscriptionId id = client_->register_notify_update(
        [this](const nlohmann::json& notification) { handle_status_update(notification); });

    if (id == INVALID_SUBSCRIPTION_ID) {
        spdlog::error("[AMS HappyHare] Failed to register for status updates");
        return AmsErrorHelper::not_connected("Failed to subscribe to Moonraker updates");
    }

    // RAII guard - automatically unsubscribes when backend is destroyed or stop() called
    subscription_ = SubscriptionGuard(client_, id);

    running_ = true;
    spdlog::info("[AMS HappyHare] Backend started, subscription ID: {}", id);

    // Emit initial state event (state may be empty until first Moonraker update)
    emit_event(EVENT_STATE_CHANGED);

    return AmsErrorHelper::success();
}

void AmsBackendHappyHare::stop() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!running_) {
        return;
    }

    // RAII guard handles unsubscription automatically
    subscription_.reset();

    running_ = false;
    spdlog::info("[AMS HappyHare] Backend stopped");
}

bool AmsBackendHappyHare::is_running() const {
    return running_;
}

// ============================================================================
// Event System
// ============================================================================

void AmsBackendHappyHare::set_event_callback(EventCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_callback_ = std::move(callback);
}

void AmsBackendHappyHare::emit_event(const std::string& event, const std::string& data) {
    EventCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = event_callback_;
    }

    if (cb) {
        cb(event, data);
    }
}

// ============================================================================
// State Queries
// ============================================================================

AmsSystemInfo AmsBackendHappyHare::get_system_info() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_;
}

AmsType AmsBackendHappyHare::get_type() const {
    return AmsType::HAPPY_HARE;
}

SlotInfo AmsBackendHappyHare::get_slot_info(int slot_index) const {
    std::lock_guard<std::mutex> lock(mutex_);

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

AmsAction AmsBackendHappyHare::get_current_action() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.action;
}

int AmsBackendHappyHare::get_current_tool() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.current_tool;
}

int AmsBackendHappyHare::get_current_slot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.current_slot;
}

bool AmsBackendHappyHare::is_filament_loaded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.filament_loaded;
}

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

    // For non-active slots in Happy Hare (linear topology), check slot status
    // Slots with available filament are assumed to have filament ready at the selector
    const SlotInfo* slot = system_info_.get_slot_global(slot_index);
    if (slot &&
        (slot->status == SlotStatus::AVAILABLE || slot->status == SlotStatus::FROM_BUFFER)) {
        return PathSegment::SPOOL; // Filament at spool ready position
    }

    return PathSegment::NONE;
}

PathSegment AmsBackendHappyHare::infer_error_segment() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_segment_;
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
        }
        // Infer error segment on error state
        if (system_info_.action == AmsAction::ERROR && prev_action != AmsAction::ERROR) {
            error_segment_ = path_segment_from_happy_hare_pos(filament_pos_);
        }
    }

    // Parse filament_pos: printer.mmu.filament_pos
    // Values: 0=unloaded, 1-2=gate area, 3=in bowden, 4=end bowden, 5=homed extruder,
    //         6=extruder entry, 7-8=loaded
    if (mmu_data.contains("filament_pos") && mmu_data["filament_pos"].is_number_integer()) {
        filament_pos_ = mmu_data["filament_pos"].get<int>();
        spdlog::trace("[AMS HappyHare] Filament pos: {} -> {}", filament_pos_,
                      path_segment_to_string(path_segment_from_happy_hare_pos(filament_pos_)));
    }

    // Parse gate_status array: printer.mmu.gate_status
    // Values: -1 = unknown, 0 = empty, 1 = available, 2 = from_buffer
    if (mmu_data.contains("gate_status") && mmu_data["gate_status"].is_array()) {
        const auto& gate_status = mmu_data["gate_status"];
        int gate_count = static_cast<int>(gate_status.size());

        // Initialize gates if this is the first time we see gate_status
        if (!gates_initialized_ && gate_count > 0) {
            initialize_gates(gate_count);
        }

        // Update gate status values
        for (size_t i = 0; i < gate_status.size() && i < system_info_.units[0].slots.size(); ++i) {
            if (gate_status[i].is_number_integer()) {
                int hh_status = gate_status[i].get<int>();
                SlotStatus status = slot_status_from_happy_hare(hh_status);

                // Mark the currently loaded slot as LOADED instead of AVAILABLE
                if (system_info_.filament_loaded &&
                    static_cast<int>(i) == system_info_.current_slot &&
                    status == SlotStatus::AVAILABLE) {
                    status = SlotStatus::LOADED;
                }

                system_info_.units[0].slots[i].status = status;
            }
        }
    }

    // Parse gate_color_rgb array: printer.mmu.gate_color_rgb
    // Values are RGB integers like 0xFF0000 for red
    if (mmu_data.contains("gate_color_rgb") && mmu_data["gate_color_rgb"].is_array()) {
        const auto& colors = mmu_data["gate_color_rgb"];
        for (size_t i = 0; i < colors.size() && !system_info_.units.empty() &&
                           i < system_info_.units[0].slots.size();
             ++i) {
            if (colors[i].is_number_integer()) {
                system_info_.units[0].slots[i].color_rgb =
                    static_cast<uint32_t>(colors[i].get<int>());
            }
        }
    }

    // Parse gate_material array: printer.mmu.gate_material
    // Values are strings like "PLA", "PETG", "ABS"
    if (mmu_data.contains("gate_material") && mmu_data["gate_material"].is_array()) {
        const auto& materials = mmu_data["gate_material"];
        for (size_t i = 0; i < materials.size() && !system_info_.units.empty() &&
                           i < system_info_.units[0].slots.size();
             ++i) {
            if (materials[i].is_string()) {
                system_info_.units[0].slots[i].material = materials[i].get<std::string>();
            }
        }
    }

    // Parse ttg_map (tool-to-gate mapping) if available
    if (mmu_data.contains("ttg_map") && mmu_data["ttg_map"].is_array()) {
        const auto& ttg_map = mmu_data["ttg_map"];
        system_info_.tool_to_slot_map.clear();
        system_info_.tool_to_slot_map.reserve(ttg_map.size());

        for (const auto& mapping : ttg_map) {
            if (mapping.is_number_integer()) {
                system_info_.tool_to_slot_map.push_back(mapping.get<int>());
            }
        }

        // Update gate mapped_tool references
        if (!system_info_.units.empty()) {
            for (auto& slot : system_info_.units[0].slots) {
                slot.mapped_tool = -1; // Reset
            }
            for (size_t tool = 0; tool < system_info_.tool_to_slot_map.size(); ++tool) {
                int slot_idx = system_info_.tool_to_slot_map[tool];
                if (slot_idx >= 0 &&
                    slot_idx < static_cast<int>(system_info_.units[0].slots.size())) {
                    system_info_.units[0].slots[slot_idx].mapped_tool = static_cast<int>(tool);
                }
            }
        }
    }

    // Parse endless_spool_groups if available
    if (mmu_data.contains("endless_spool_groups") && mmu_data["endless_spool_groups"].is_array()) {
        const auto& es_groups = mmu_data["endless_spool_groups"];
        for (size_t i = 0; i < es_groups.size() && !system_info_.units.empty() &&
                           i < system_info_.units[0].slots.size();
             ++i) {
            if (es_groups[i].is_number_integer()) {
                system_info_.units[0].slots[i].endless_spool_group = es_groups[i].get<int>();
            }
        }
    }
}

void AmsBackendHappyHare::initialize_gates(int gate_count) {
    spdlog::info("[AMS HappyHare] Initializing {} gates", gate_count);

    // Create a single unit with all gates
    AmsUnit unit;
    unit.unit_index = 0;
    unit.name = "Happy Hare MMU";
    unit.slot_count = gate_count;
    unit.first_slot_global_index = 0;
    unit.connected = true;
    unit.has_encoder = true; // Happy Hare typically has encoder
    unit.has_toolhead_sensor = true;
    unit.has_slot_sensors = true;

    // Initialize slots with defaults
    for (int i = 0; i < gate_count; ++i) {
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
    system_info_.total_slots = gate_count;

    // Initialize tool-to-gate mapping (1:1 default)
    system_info_.tool_to_slot_map.clear();
    system_info_.tool_to_slot_map.reserve(gate_count);
    for (int i = 0; i < gate_count; ++i) {
        system_info_.tool_to_slot_map.push_back(i);
    }

    gates_initialized_ = true;
}

// ============================================================================
// Filament Operations
// ============================================================================

AmsError AmsBackendHappyHare::check_preconditions() const {
    if (!running_) {
        return AmsErrorHelper::not_connected("Happy Hare backend not started");
    }

    if (system_info_.is_busy()) {
        return AmsErrorHelper::busy(ams_action_to_string(system_info_.action));
    }

    return AmsErrorHelper::success();
}

AmsError AmsBackendHappyHare::validate_slot_index(int gate_index) const {
    if (gate_index < 0 || gate_index >= system_info_.total_slots) {
        return AmsErrorHelper::invalid_slot(gate_index, system_info_.total_slots - 1);
    }
    return AmsErrorHelper::success();
}

AmsError AmsBackendHappyHare::execute_gcode(const std::string& gcode) {
    if (!api_) {
        return AmsErrorHelper::not_connected("MoonrakerAPI not available");
    }

    spdlog::info("[AMS HappyHare] Executing G-code: {}", gcode);

    // Execute G-code asynchronously via MoonrakerAPI
    // Errors will be reported via Moonraker's notify_gcode_response
    api_->execute_gcode(
        gcode, []() { spdlog::debug("[AMS HappyHare] G-code executed successfully"); },
        [gcode](const MoonrakerError& err) {
            spdlog::error("[AMS HappyHare] G-code failed: {} - {}", gcode, err.message);
        });

    return AmsErrorHelper::success();
}

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
        const auto* slot = system_info_.get_slot_global(slot_index);
        if (slot && slot->status == SlotStatus::EMPTY) {
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

AmsError AmsBackendHappyHare::set_slot_info(int slot_index, const SlotInfo& info) {
    int old_spoolman_id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (slot_index < 0 || slot_index >= system_info_.total_slots) {
            return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
        }

        auto* slot = system_info_.get_slot_global(slot_index);
        if (!slot) {
            return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
        }

        // Capture old spoolman_id BEFORE updating (needed to detect clearing)
        old_spoolman_id = slot->spoolman_id;

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

        spdlog::info("[AMS HappyHare] Updated slot {} info: {} {}", slot_index, info.material,
                     info.color_name);
    }

    // Persist via MMU_GATE_MAP command (Happy Hare stores in mmu_vars.cfg automatically)
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
        spdlog::warn("[AMS HappyHare] Skipping MATERIAL - unsafe characters in: {}", info.material);
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

        if (slot_index < 0 || slot_index >= system_info_.total_slots) {
            return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
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

    // Need at least one unit with slots
    if (system_info_.units.empty()) {
        return configs;
    }

    // Iterate through slots and find backup slots based on endless_spool_group
    for (const auto& slot : system_info_.units[0].slots) {
        helix::printer::EndlessSpoolConfig config;
        config.slot_index = slot.slot_index;
        config.backup_slot = -1; // Default: no backup

        if (slot.endless_spool_group >= 0) {
            // Find another slot in the same group
            for (const auto& other : system_info_.units[0].slots) {
                if (other.slot_index != slot.slot_index &&
                    other.endless_spool_group == slot.endless_spool_group) {
                    config.backup_slot = other.slot_index;
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
