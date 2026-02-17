// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_toolchanger.h"

#include "ams_error.h"
#include "moonraker_api.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <sstream>

using namespace helix;

// ============================================================================
// Construction / Destruction
// ============================================================================

AmsBackendToolChanger::AmsBackendToolChanger(MoonrakerAPI* api, MoonrakerClient* client)
    : api_(api), client_(client) {
    // Initialize system info with tool changer defaults
    system_info_.type = AmsType::TOOL_CHANGER;
    system_info_.type_name = "Tool Changer";
    system_info_.version = "unknown";
    system_info_.current_tool = -1;
    system_info_.current_slot = -1;
    system_info_.filament_loaded = false;
    system_info_.action = AmsAction::IDLE;
    system_info_.total_slots = 0;

    // Tool changer capabilities
    system_info_.supports_endless_spool = false; // Not applicable
    system_info_.supports_tool_mapping = false;  // Tools ARE the slots
    system_info_.supports_bypass = false;        // No bypass on tool changers
    system_info_.has_hardware_bypass_sensor = false;

    spdlog::debug("[AMS ToolChanger] Backend created");
}

void AmsBackendToolChanger::set_discovered_tools(std::vector<std::string> tool_names) {
    std::lock_guard<std::mutex> lock(mutex_);

    tool_names_ = std::move(tool_names);

    // Initialize tool structures now that we have tool names
    if (!tool_names_.empty()) {
        initialize_tools();
    }

    spdlog::info("[AMS ToolChanger] Set {} discovered tools", tool_names_.size());
}

AmsBackendToolChanger::~AmsBackendToolChanger() {
    // Release subscription guard without trying to unsubscribe
    // (MoonrakerClient may already be destroyed during static destruction)
    subscription_.release();
}

// ============================================================================
// Lifecycle Management
// ============================================================================

AmsError AmsBackendToolChanger::start() {
    bool should_emit = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (running_) {
            return AmsErrorHelper::success();
        }

        if (!client_) {
            spdlog::error("[AMS ToolChanger] Cannot start: MoonrakerClient is null");
            return AmsErrorHelper::not_connected("MoonrakerClient not provided");
        }

        if (!api_) {
            spdlog::error("[AMS ToolChanger] Cannot start: MoonrakerAPI is null");
            return AmsErrorHelper::not_connected("MoonrakerAPI not provided");
        }

        if (tool_names_.empty()) {
            spdlog::error("[AMS ToolChanger] Cannot start: No tools discovered. "
                          "Call set_discovered_tools() before start()");
            return AmsErrorHelper::not_connected("No tools discovered");
        }

        // Register for status update notifications from Moonraker
        // Tool changer state comes via notify_status_update when toolchanger.* changes
        SubscriptionId id = client_->register_notify_update(
            [this](const nlohmann::json& notification) { handle_status_update(notification); });

        if (id == INVALID_SUBSCRIPTION_ID) {
            spdlog::error("[AMS ToolChanger] Failed to register for status updates");
            return AmsErrorHelper::not_connected("Failed to subscribe to Moonraker updates");
        }

        // RAII guard - automatically unsubscribes when backend is destroyed or stop() called
        subscription_ = SubscriptionGuard(client_, id);

        running_ = true;
        spdlog::info("[AMS ToolChanger] Backend started, subscription ID: {}", id);

        should_emit = true;
    } // Release lock before emitting

    // Emit initial state event OUTSIDE the lock to avoid deadlock
    if (should_emit) {
        emit_event(EVENT_STATE_CHANGED);
    }

    return AmsErrorHelper::success();
}

void AmsBackendToolChanger::stop() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!running_) {
        return;
    }

    // RAII guard handles unsubscription automatically
    subscription_.reset();

    running_ = false;
    spdlog::info("[AMS ToolChanger] Backend stopped");
}

void AmsBackendToolChanger::release_subscriptions() {
    subscription_.release();
}

bool AmsBackendToolChanger::is_running() const {
    return running_;
}

// ============================================================================
// Event System
// ============================================================================

void AmsBackendToolChanger::set_event_callback(EventCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_callback_ = std::move(callback);
}

void AmsBackendToolChanger::emit_event(const std::string& event, const std::string& data) {
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

AmsSystemInfo AmsBackendToolChanger::get_system_info() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_;
}

AmsType AmsBackendToolChanger::get_type() const {
    return AmsType::TOOL_CHANGER;
}

SlotInfo AmsBackendToolChanger::get_slot_info(int slot_index) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto* slot = system_info_.get_slot_global(slot_index);
    if (slot) {
        return *slot;
    }

    // Return empty slot info for invalid index
    SlotInfo empty;
    empty.slot_index = -1;
    return empty;
}

AmsAction AmsBackendToolChanger::get_current_action() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.action;
}

int AmsBackendToolChanger::get_current_tool() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.current_tool;
}

int AmsBackendToolChanger::get_current_slot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // For tool changers, slot == tool
    return system_info_.current_slot;
}

bool AmsBackendToolChanger::is_filament_loaded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // For tool changers, "loaded" means a tool is mounted
    return system_info_.filament_loaded;
}

// ============================================================================
// Path Visualization
// ============================================================================

PathTopology AmsBackendToolChanger::get_topology() const {
    return PathTopology::PARALLEL; // Each tool has its own independent path
}

PathSegment AmsBackendToolChanger::get_filament_segment() const {
    std::lock_guard<std::mutex> lock(mutex_);

    // For tool changers, filament segment depends on whether a tool is mounted
    if (system_info_.current_tool >= 0 && system_info_.filament_loaded) {
        return PathSegment::NOZZLE; // Tool is mounted and active
    }
    return PathSegment::SPOOL; // No tool mounted (all tools in docks)
}

PathSegment AmsBackendToolChanger::get_slot_filament_segment(int slot_index) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (slot_index < 0 || slot_index >= static_cast<int>(tool_mounted_.size())) {
        return PathSegment::NONE;
    }

    // For tool changers, each slot represents a complete tool
    if (tool_mounted_[slot_index]) {
        return PathSegment::NOZZLE; // This tool is mounted
    }
    return PathSegment::SPOOL; // Tool is docked (has spool attached)
}

PathSegment AmsBackendToolChanger::infer_error_segment() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (system_info_.action == AmsAction::ERROR) {
        // For tool changers, errors typically occur at the dock or carriage
        return PathSegment::HUB; // Use HUB to represent the docking area
    }
    return PathSegment::NONE;
}

// ============================================================================
// Moonraker Status Update Handling
// ============================================================================

void AmsBackendToolChanger::handle_status_update(const nlohmann::json& notification) {
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
        std::lock_guard<std::mutex> lock(mutex_);

        // Check for toolchanger object updates
        if (params.contains("toolchanger")) {
            const auto& tc_data = params["toolchanger"];
            if (tc_data.is_object()) {
                spdlog::trace("[AMS ToolChanger] Received toolchanger status update");
                parse_toolchanger_state(tc_data);
                state_changed = true;
            }
        }

        // Check for individual tool updates (e.g., "tool T0", "tool T1")
        for (const auto& tool_name : tool_names_) {
            std::string key = "tool " + tool_name;
            if (params.contains(key)) {
                const auto& tool_data = params[key];
                if (tool_data.is_object()) {
                    spdlog::trace("[AMS ToolChanger] Received {} status update", key);
                    parse_tool_state(tool_name, tool_data);
                    state_changed = true;
                }
            }
        }
    }

    // Emit event OUTSIDE the lock to avoid deadlock if the callback
    // queries backend state (e.g., calls get_system_info() which acquires mutex_)
    if (state_changed) {
        emit_event(EVENT_STATE_CHANGED);
    }
}

void AmsBackendToolChanger::parse_toolchanger_state(const nlohmann::json& tc_data) {
    // Parse status: toolchanger.status
    // Values: "ready", "changing", "error", "uninitialized"
    if (tc_data.contains("status") && tc_data["status"].is_string()) {
        std::string status_str = tc_data["status"].get<std::string>();
        system_info_.action = status_to_action(status_str);
        system_info_.operation_detail = status_str;
        spdlog::trace("[AMS ToolChanger] Status: {} -> {}", status_str,
                      ams_action_to_string(system_info_.action));
    }

    // Parse current tool: toolchanger.tool_number
    // -1 = no tool selected
    if (tc_data.contains("tool_number") && tc_data["tool_number"].is_number_integer()) {
        int tool_num = tc_data["tool_number"].get<int>();
        system_info_.current_tool = tool_num;
        system_info_.current_slot = tool_num; // For tool changers, slot == tool
        system_info_.filament_loaded = (tool_num >= 0);
        spdlog::trace("[AMS ToolChanger] Current tool: {}", tool_num);
    }

    // Parse tool list: toolchanger.tool_numbers and toolchanger.tool_names
    // This can be used to dynamically update the tool list
    if (tc_data.contains("tool_numbers") && tc_data["tool_numbers"].is_array()) {
        spdlog::trace("[AMS ToolChanger] Tool numbers: {}", tc_data["tool_numbers"].dump());
    }
}

void AmsBackendToolChanger::parse_tool_state(const std::string& tool_name,
                                             const nlohmann::json& tool_data) {
    int slot_idx = find_slot_for_tool(tool_name);
    if (slot_idx < 0) {
        spdlog::warn("[AMS ToolChanger] Unknown tool: {}", tool_name);
        return;
    }

    // Parse mounted state: tool.mounted
    if (tool_data.contains("mounted") && tool_data["mounted"].is_boolean()) {
        bool mounted = tool_data["mounted"].get<bool>();
        if (slot_idx < static_cast<int>(tool_mounted_.size())) {
            tool_mounted_[slot_idx] = mounted;
        }

        // Update slot status based on mounted state
        if (!system_info_.units.empty() &&
            slot_idx < static_cast<int>(system_info_.units[0].slots.size())) {
            system_info_.units[0].slots[slot_idx].status =
                mounted ? SlotStatus::LOADED : SlotStatus::AVAILABLE;
        }
        spdlog::trace("[AMS ToolChanger] Tool {} mounted: {}", tool_name, mounted);
    }

    // Parse active state: tool.active
    if (tool_data.contains("active") && tool_data["active"].is_boolean()) {
        bool active = tool_data["active"].get<bool>();
        spdlog::trace("[AMS ToolChanger] Tool {} active: {}", tool_name, active);
    }

    // Parse offsets (stored but not currently used in SlotInfo)
    if (tool_data.contains("gcode_x_offset") || tool_data.contains("gcode_y_offset") ||
        tool_data.contains("gcode_z_offset")) {
        spdlog::trace("[AMS ToolChanger] Tool {} has offset data", tool_name);
    }
}

AmsAction AmsBackendToolChanger::status_to_action(const std::string& status) {
    if (status == "ready") {
        return AmsAction::IDLE;
    }
    if (status == "changing") {
        return AmsAction::SELECTING;
    }
    if (status == "error") {
        return AmsAction::ERROR;
    }
    if (status == "uninitialized") {
        return AmsAction::RESETTING;
    }
    return AmsAction::IDLE;
}

void AmsBackendToolChanger::initialize_tools() {
    int tool_count = static_cast<int>(tool_names_.size());

    AmsUnit unit;
    unit.unit_index = 0;
    unit.name = "Tool Changer";
    unit.slot_count = tool_count;
    unit.first_slot_global_index = 0;
    unit.connected = true;
    unit.has_encoder = false;
    unit.has_toolhead_sensor = false;
    unit.has_slot_sensors = false;

    // Initialize slots for each tool
    tool_mounted_.clear();
    tool_mounted_.resize(tool_count, false);

    for (int i = 0; i < tool_count; ++i) {
        SlotInfo slot;
        slot.slot_index = i;
        slot.global_index = i;
        slot.status = SlotStatus::AVAILABLE; // Tools start as available (docked)
        slot.mapped_tool = i;                // Tool i maps to slot i
        slot.color_rgb = AMS_DEFAULT_SLOT_COLOR;
        slot.spool_name = tool_names_[i]; // Use tool name as placeholder

        unit.slots.push_back(slot);
    }

    system_info_.units.clear();
    system_info_.units.push_back(unit);
    system_info_.total_slots = tool_count;

    // Initialize tool-to-slot mapping (1:1 for tool changers)
    system_info_.tool_to_slot_map.clear();
    system_info_.tool_to_slot_map.reserve(tool_count);
    for (int i = 0; i < tool_count; ++i) {
        system_info_.tool_to_slot_map.push_back(i);
    }

    tools_initialized_ = true;
    spdlog::info("[AMS ToolChanger] Initialized {} tools", tool_count);
}

int AmsBackendToolChanger::find_slot_for_tool(const std::string& tool_name) const {
    auto it = std::find(tool_names_.begin(), tool_names_.end(), tool_name);
    if (it != tool_names_.end()) {
        return static_cast<int>(std::distance(tool_names_.begin(), it));
    }
    return -1;
}

// ============================================================================
// Operations
// ============================================================================

// NOTE: Must be called while holding mutex_ (accesses system_info_ without lock)
AmsError AmsBackendToolChanger::check_preconditions() const {
    if (!running_) {
        return AmsErrorHelper::not_connected("Tool changer backend not started");
    }

    if (system_info_.is_busy()) {
        return AmsErrorHelper::busy(ams_action_to_string(system_info_.action));
    }

    return AmsErrorHelper::success();
}

// NOTE: Must be called while holding mutex_ (accesses system_info_ without lock)
AmsError AmsBackendToolChanger::validate_slot_index(int slot_index) const {
    // Special case: no tools discovered
    if (system_info_.total_slots == 0) {
        return AmsErrorHelper::not_connected("No tools discovered");
    }
    if (slot_index < 0 || slot_index >= system_info_.total_slots) {
        return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
    }
    return AmsErrorHelper::success();
}

AmsError AmsBackendToolChanger::execute_gcode(const std::string& gcode) {
    if (!api_) {
        return AmsErrorHelper::not_connected("MoonrakerAPI not available");
    }

    spdlog::info("[AMS ToolChanger] Executing G-code: {}", gcode);

    // Execute G-code asynchronously via MoonrakerAPI
    api_->execute_gcode(
        gcode, []() { spdlog::debug("[AMS ToolChanger] G-code executed successfully"); },
        [gcode](const MoonrakerError& err) {
            spdlog::error("[AMS ToolChanger] G-code failed: {} - {}", gcode, err.message);
        });

    return AmsErrorHelper::success();
}

AmsError AmsBackendToolChanger::load_filament(int slot_index) {
    // For tool changers, "load filament" means "mount tool"
    return change_tool(slot_index);
}

AmsError AmsBackendToolChanger::unload_filament() {
    // For tool changers, "unload" means unmount current tool
    {
        std::lock_guard<std::mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        if (system_info_.current_tool < 0) {
            return AmsErrorHelper::not_loaded();
        }
    }

    spdlog::info("[AMS ToolChanger] Unmounting current tool");
    return execute_gcode("UNSELECT_TOOL");
}

AmsError AmsBackendToolChanger::select_slot(int slot_index) {
    // For tool changers, selecting a slot means mounting that tool
    return change_tool(slot_index);
}

AmsError AmsBackendToolChanger::change_tool(int tool_number) {
    std::string tool_name;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        AmsError precondition = check_preconditions();
        if (!precondition) {
            return precondition;
        }

        AmsError slot_valid = validate_slot_index(tool_number);
        if (!slot_valid) {
            return slot_valid;
        }

        // Get tool name for the command
        if (tool_number >= 0 && tool_number < static_cast<int>(tool_names_.size())) {
            tool_name = tool_names_[tool_number];
        } else {
            tool_name = "T" + std::to_string(tool_number);
        }
    }

    // Send SELECT_TOOL command (klipper-toolchanger command)
    std::ostringstream cmd;
    cmd << "SELECT_TOOL TOOL=" << tool_name;

    spdlog::info("[AMS ToolChanger] Mounting tool {} ({})", tool_number, tool_name);
    return execute_gcode(cmd.str());
}

// ============================================================================
// Recovery Operations
// ============================================================================

AmsError AmsBackendToolChanger::recover() {
    spdlog::info("[AMS ToolChanger] Attempting recovery");
    // klipper-toolchanger doesn't have a dedicated recovery command
    // Try to reinitialize the toolchanger
    return execute_gcode("INITIALIZE_TOOLCHANGER");
}

AmsError AmsBackendToolChanger::reset() {
    spdlog::info("[AMS ToolChanger] Resetting toolchanger");
    return execute_gcode("INITIALIZE_TOOLCHANGER");
}

AmsError AmsBackendToolChanger::cancel() {
    spdlog::info("[AMS ToolChanger] Cancel requested (not implemented for tool changers)");
    // Tool changes typically can't be cancelled mid-operation
    return AmsErrorHelper::not_supported("Cancel");
}

// ============================================================================
// Configuration Operations
// ============================================================================

AmsError AmsBackendToolChanger::set_slot_info(int slot_index, const SlotInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);

    AmsError slot_valid = validate_slot_index(slot_index);
    if (!slot_valid) {
        return slot_valid;
    }

    // Update local state (for UI display)
    if (!system_info_.units.empty() &&
        slot_index < static_cast<int>(system_info_.units[0].slots.size())) {
        auto& slot = system_info_.units[0].slots[slot_index];
        slot.color_rgb = info.color_rgb;
        slot.color_name = info.color_name;
        slot.material = info.material;
        slot.brand = info.brand;
        slot.spoolman_id = info.spoolman_id;
        slot.spool_name = info.spool_name;
        slot.remaining_weight_g = info.remaining_weight_g;
        slot.total_weight_g = info.total_weight_g;
    }

    return AmsErrorHelper::success();
}

AmsError AmsBackendToolChanger::set_tool_mapping(int /*tool_number*/, int /*slot_index*/) {
    // Tool changers don't have tool-to-slot mapping - tools ARE slots
    return AmsErrorHelper::not_supported("Tool mapping");
}

helix::printer::ToolMappingCapabilities
AmsBackendToolChanger::get_tool_mapping_capabilities() const {
    // Tool changers have fixed 1:1 mapping - tools ARE slots, not configurable
    return {false, false, ""};
}

std::vector<int> AmsBackendToolChanger::get_tool_mapping() const {
    // Tool changers have fixed 1:1 mapping - return empty (not supported)
    return {};
}

// ============================================================================
// Bypass Mode (Not Applicable)
// ============================================================================

AmsError AmsBackendToolChanger::enable_bypass() {
    return AmsErrorHelper::not_supported("Bypass mode");
}

AmsError AmsBackendToolChanger::disable_bypass() {
    return AmsErrorHelper::not_supported("Bypass mode");
}

bool AmsBackendToolChanger::is_bypass_active() const {
    return false; // Tool changers never have bypass
}

// ============================================================================
// Device Actions (stub - not applicable for tool changers)
// ============================================================================

std::vector<helix::printer::DeviceSection> AmsBackendToolChanger::get_device_sections() const {
    // Tool changers don't expose device-specific actions
    return {};
}

std::vector<helix::printer::DeviceAction> AmsBackendToolChanger::get_device_actions() const {
    // Tool changers don't expose device-specific actions
    return {};
}

AmsError AmsBackendToolChanger::execute_device_action(const std::string& action_id,
                                                      const std::any& value) {
    (void)action_id;
    (void)value;
    return AmsErrorHelper::not_supported("Device actions");
}
