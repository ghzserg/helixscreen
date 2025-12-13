// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_happy_hare.h"

#include "moonraker_api.h"

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
    system_info_.current_gate = -1;
    system_info_.filament_loaded = false;
    system_info_.action = AmsAction::IDLE;
    system_info_.total_gates = 0;
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
    // During static destruction, the mutex may be invalid. Don't call stop()
    // which would try to lock it. Let RAII guards handle cleanup automatically.
    // Normal cleanup: AmsState::set_backend() calls stop() before replacing.
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

GateInfo AmsBackendHappyHare::get_gate_info(int global_index) const {
    std::lock_guard<std::mutex> lock(mutex_);

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

AmsAction AmsBackendHappyHare::get_current_action() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.action;
}

int AmsBackendHappyHare::get_current_tool() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.current_tool;
}

int AmsBackendHappyHare::get_current_gate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.current_gate;
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
        system_info_.current_gate = mmu_data["gate"].get<int>();
        spdlog::trace("[AMS HappyHare] Current gate: {}", system_info_.current_gate);
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
        for (size_t i = 0; i < gate_status.size() && i < system_info_.units[0].gates.size(); ++i) {
            if (gate_status[i].is_number_integer()) {
                int hh_status = gate_status[i].get<int>();
                GateStatus status = gate_status_from_happy_hare(hh_status);

                // Mark the currently loaded gate as LOADED instead of AVAILABLE
                if (system_info_.filament_loaded &&
                    static_cast<int>(i) == system_info_.current_gate &&
                    status == GateStatus::AVAILABLE) {
                    status = GateStatus::LOADED;
                }

                system_info_.units[0].gates[i].status = status;
            }
        }
    }

    // Parse gate_color_rgb array: printer.mmu.gate_color_rgb
    // Values are RGB integers like 0xFF0000 for red
    if (mmu_data.contains("gate_color_rgb") && mmu_data["gate_color_rgb"].is_array()) {
        const auto& colors = mmu_data["gate_color_rgb"];
        for (size_t i = 0; i < colors.size() && !system_info_.units.empty() &&
                           i < system_info_.units[0].gates.size();
             ++i) {
            if (colors[i].is_number_integer()) {
                system_info_.units[0].gates[i].color_rgb =
                    static_cast<uint32_t>(colors[i].get<int>());
            }
        }
    }

    // Parse gate_material array: printer.mmu.gate_material
    // Values are strings like "PLA", "PETG", "ABS"
    if (mmu_data.contains("gate_material") && mmu_data["gate_material"].is_array()) {
        const auto& materials = mmu_data["gate_material"];
        for (size_t i = 0; i < materials.size() && !system_info_.units.empty() &&
                           i < system_info_.units[0].gates.size();
             ++i) {
            if (materials[i].is_string()) {
                system_info_.units[0].gates[i].material = materials[i].get<std::string>();
            }
        }
    }

    // Parse ttg_map (tool-to-gate mapping) if available
    if (mmu_data.contains("ttg_map") && mmu_data["ttg_map"].is_array()) {
        const auto& ttg_map = mmu_data["ttg_map"];
        system_info_.tool_to_gate_map.clear();
        system_info_.tool_to_gate_map.reserve(ttg_map.size());

        for (const auto& mapping : ttg_map) {
            if (mapping.is_number_integer()) {
                system_info_.tool_to_gate_map.push_back(mapping.get<int>());
            }
        }

        // Update gate mapped_tool references
        if (!system_info_.units.empty()) {
            for (auto& gate : system_info_.units[0].gates) {
                gate.mapped_tool = -1; // Reset
            }
            for (size_t tool = 0; tool < system_info_.tool_to_gate_map.size(); ++tool) {
                int gate_idx = system_info_.tool_to_gate_map[tool];
                if (gate_idx >= 0 &&
                    gate_idx < static_cast<int>(system_info_.units[0].gates.size())) {
                    system_info_.units[0].gates[gate_idx].mapped_tool = static_cast<int>(tool);
                }
            }
        }
    }

    // Parse endless_spool_groups if available
    if (mmu_data.contains("endless_spool_groups") && mmu_data["endless_spool_groups"].is_array()) {
        const auto& es_groups = mmu_data["endless_spool_groups"];
        for (size_t i = 0; i < es_groups.size() && !system_info_.units.empty() &&
                           i < system_info_.units[0].gates.size();
             ++i) {
            if (es_groups[i].is_number_integer()) {
                system_info_.units[0].gates[i].endless_spool_group = es_groups[i].get<int>();
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
    unit.gate_count = gate_count;
    unit.first_gate_global_index = 0;
    unit.connected = true;
    unit.has_encoder = true; // Happy Hare typically has encoder
    unit.has_toolhead_sensor = true;
    unit.has_gate_sensors = true;

    // Initialize gates with defaults
    for (int i = 0; i < gate_count; ++i) {
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
    system_info_.total_gates = gate_count;

    // Initialize tool-to-gate mapping (1:1 default)
    system_info_.tool_to_gate_map.clear();
    system_info_.tool_to_gate_map.reserve(gate_count);
    for (int i = 0; i < gate_count; ++i) {
        system_info_.tool_to_gate_map.push_back(i);
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

AmsError AmsBackendHappyHare::validate_gate_index(int gate_index) const {
    if (gate_index < 0 || gate_index >= system_info_.total_gates) {
        return AmsErrorHelper::invalid_gate(gate_index, system_info_.total_gates - 1);
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

AmsError AmsBackendHappyHare::load_filament(int gate_index) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        AmsError gate_valid = validate_gate_index(gate_index);
        if (!gate_valid) {
            return gate_valid;
        }

        // Check if gate has filament available
        const auto* gate = system_info_.get_gate_global(gate_index);
        if (gate && gate->status == GateStatus::EMPTY) {
            return AmsErrorHelper::gate_not_available(gate_index);
        }
    }

    // Send MMU_LOAD GATE={n} command
    std::ostringstream cmd;
    cmd << "MMU_LOAD GATE=" << gate_index;

    spdlog::info("[AMS HappyHare] Loading from gate {}", gate_index);
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

AmsError AmsBackendHappyHare::select_gate(int gate_index) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        AmsError gate_valid = validate_gate_index(gate_index);
        if (!gate_valid) {
            return gate_valid;
        }
    }

    // Send MMU_SELECT GATE={n} command
    std::ostringstream cmd;
    cmd << "MMU_SELECT GATE=" << gate_index;

    spdlog::info("[AMS HappyHare] Selecting gate {}", gate_index);
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
            tool_number >= static_cast<int>(system_info_.tool_to_gate_map.size())) {
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

AmsError AmsBackendHappyHare::set_gate_info(int gate_index, const GateInfo& info) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

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

        spdlog::info("[AMS HappyHare] Updated gate {} info: {} {}", gate_index, info.material,
                     info.color_name);
    }

    // Emit OUTSIDE the lock to avoid deadlock with callbacks
    emit_event(EVENT_GATE_CHANGED, std::to_string(gate_index));

    // Happy Hare stores gate info via MMU_GATE_MAP command
    // This could be extended to persist changes, but for now we just update local state
    // The actual gate_material and gate_color_rgb are typically set via Klipper config

    return AmsErrorHelper::success();
}

AmsError AmsBackendHappyHare::set_tool_mapping(int tool_number, int gate_index) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (tool_number < 0 ||
            tool_number >= static_cast<int>(system_info_.tool_to_gate_map.size())) {
            return AmsError(AmsResult::INVALID_TOOL,
                            "Tool " + std::to_string(tool_number) + " out of range",
                            "Invalid tool number", "");
        }

        if (gate_index < 0 || gate_index >= system_info_.total_gates) {
            return AmsErrorHelper::invalid_gate(gate_index, system_info_.total_gates - 1);
        }
    }

    // Send MMU_TTG_MAP command to update tool-to-gate mapping
    std::ostringstream cmd;
    cmd << "MMU_TTG_MAP TOOL=" << tool_number << " GATE=" << gate_index;

    spdlog::info("[AMS HappyHare] Mapping T{} to gate {}", tool_number, gate_index);
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

        if (system_info_.current_gate != -2) {
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
    return system_info_.current_gate == -2;
}
