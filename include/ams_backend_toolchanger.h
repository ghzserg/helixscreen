// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_subscription_guard.h"

#include "ams_backend.h"
#include "moonraker_client.h"

#include <atomic>
#include <mutex>
#include <vector>

// Forward declaration
class MoonrakerAPI;

/**
 * @file ams_backend_toolchanger.h
 * @brief Physical tool changer backend implementation
 *
 * Implements the AmsBackend interface for physical tool changers using
 * viesturz/klipper-toolchanger. Unlike filament-switching systems (Happy Hare,
 * AFC), tool changers have multiple physical toolheads that are swapped.
 *
 * Key differences from filament systems:
 * - Each "slot" is a complete toolhead with its own extruder
 * - No hub/selector - path topology is PARALLEL
 * - "Loading" means mounting the tool to the carriage
 * - No bypass mode (each tool IS the path)
 *
 * Klipper Objects (viesturz/klipper-toolchanger):
 * - toolchanger.status     (string): "ready", "changing", "error", "uninitialized"
 * - toolchanger.tool       (string): Current tool name ("T0") or null
 * - toolchanger.tool_number (int): Current tool number (-1 if none)
 * - toolchanger.tool_numbers (array[int]): All tool numbers [0, 1, 2]
 * - toolchanger.tool_names  (array[string]): All tool names ["T0", "T1", "T2"]
 *
 * Per-tool Objects:
 * - tool T0.active         (bool): Is this tool selected?
 * - tool T0.mounted        (bool): Is this tool mounted on carriage?
 * - tool T0.gcode_x_offset (float): X offset for tool
 * - tool T0.gcode_y_offset (float): Y offset for tool
 * - tool T0.gcode_z_offset (float): Z offset for tool
 * - tool T0.extruder       (string): Associated extruder name
 * - tool T0.fan            (string): Associated fan name
 *
 * G-code Commands:
 * - SELECT_TOOL TOOL=T{n}  - Mount specified tool
 * - UNSELECT_TOOL          - Unmount current tool (park it)
 * - T{n}                   - Tool change macro (same as SELECT_TOOL)
 */
class AmsBackendToolChanger : public AmsBackend {
  public:
    /**
     * @brief Construct tool changer backend
     *
     * @param api Pointer to MoonrakerAPI (for sending G-code commands)
     * @param client Pointer to MoonrakerClient (for subscribing to updates)
     *
     * @note Pointers must remain valid for the lifetime of this backend.
     * @note Call set_discovered_tools() before start() to set tool names.
     */
    AmsBackendToolChanger(MoonrakerAPI* api, MoonrakerClient* client);

    ~AmsBackendToolChanger() override;

    /**
     * @brief Set discovered tool names from PrinterCapabilities
     *
     * Must be called before start() to initialize tool structures.
     * Tool names are extracted from printer.objects.list (e.g., "T0", "T1").
     *
     * @param tool_names Vector of tool names
     */
    void set_discovered_tools(std::vector<std::string> tool_names) override;

    // Lifecycle
    AmsError start() override;
    void stop() override;
    [[nodiscard]] bool is_running() const override;

    // Events
    void set_event_callback(EventCallback callback) override;

    // State queries
    [[nodiscard]] AmsSystemInfo get_system_info() const override;
    [[nodiscard]] AmsType get_type() const override;
    [[nodiscard]] SlotInfo get_slot_info(int slot_index) const override;
    [[nodiscard]] AmsAction get_current_action() const override;
    [[nodiscard]] int get_current_tool() const override;
    [[nodiscard]] int get_current_slot() const override;
    [[nodiscard]] bool is_filament_loaded() const override;

    // Path visualization (PARALLEL topology for tool changers)
    [[nodiscard]] PathTopology get_topology() const override;
    [[nodiscard]] PathSegment get_filament_segment() const override;
    [[nodiscard]] PathSegment get_slot_filament_segment(int slot_index) const override;
    [[nodiscard]] PathSegment infer_error_segment() const override;

    // Operations
    AmsError load_filament(int slot_index) override;
    AmsError unload_filament() override;
    AmsError select_slot(int slot_index) override;
    AmsError change_tool(int tool_number) override;

    // Recovery
    AmsError recover() override;
    AmsError reset() override;
    AmsError cancel() override;

    // Configuration
    AmsError set_slot_info(int slot_index, const SlotInfo& info) override;
    AmsError set_tool_mapping(int tool_number, int slot_index) override;

    // Tool mapping - Tool changers have fixed mapping (tools ARE slots)
    [[nodiscard]] helix::printer::ToolMappingCapabilities
    get_tool_mapping_capabilities() const override;
    [[nodiscard]] std::vector<int> get_tool_mapping() const override;

    // Bypass mode (not applicable for tool changers)
    AmsError enable_bypass() override;
    AmsError disable_bypass() override;
    [[nodiscard]] bool is_bypass_active() const override;

  private:
    /**
     * @brief Handle status update notifications from Moonraker
     *
     * Called when toolchanger.* or tool.* values change via notify_status_update.
     * Parses the JSON and updates internal state.
     *
     * @param notification JSON notification from Moonraker
     */
    void handle_status_update(const nlohmann::json& notification);

    /**
     * @brief Parse toolchanger state from Moonraker JSON
     *
     * Extracts toolchanger object and updates system_info_.
     *
     * @param tc_data JSON object containing toolchanger data
     */
    void parse_toolchanger_state(const nlohmann::json& tc_data);

    /**
     * @brief Parse individual tool state from Moonraker JSON
     *
     * Updates the slot corresponding to this tool.
     *
     * @param tool_name Tool name (e.g., "T0")
     * @param tool_data JSON object containing tool data
     */
    void parse_tool_state(const std::string& tool_name, const nlohmann::json& tool_data);

    /**
     * @brief Convert toolchanger status string to AmsAction
     *
     * @param status Status string from toolchanger.status
     * @return Corresponding AmsAction enum value
     */
    static AmsAction status_to_action(const std::string& status);

    /**
     * @brief Initialize tool structures based on discovered tool names
     *
     * Creates SlotInfo entries for each tool.
     */
    void initialize_tools();

    /**
     * @brief Find slot index for a tool name
     *
     * @param tool_name Tool name (e.g., "T0", "T1")
     * @return Slot index or -1 if not found
     */
    [[nodiscard]] int find_slot_for_tool(const std::string& tool_name) const;

    /**
     * @brief Emit event to registered callback
     * @param event Event name
     * @param data Event data (JSON or empty)
     */
    void emit_event(const std::string& event, const std::string& data = "");

    /**
     * @brief Execute a G-code command via MoonrakerAPI
     *
     * @param gcode The G-code command to execute
     * @return AmsError indicating success or failure to queue command
     */
    AmsError execute_gcode(const std::string& gcode);

    /**
     * @brief Check common preconditions before operations
     *
     * Validates:
     * - Backend is running
     * - System is not busy
     *
     * @return AmsError (SUCCESS if ok, appropriate error otherwise)
     */
    AmsError check_preconditions() const;

    /**
     * @brief Validate slot index is within range
     *
     * @param slot_index Slot index to validate
     * @return AmsError (SUCCESS if valid, INVALID_SLOT otherwise)
     */
    AmsError validate_slot_index(int slot_index) const;

    // Dependencies
    MoonrakerAPI* api_;                   ///< For sending G-code commands
    MoonrakerClient* client_;             ///< For subscribing to updates
    std::vector<std::string> tool_names_; ///< Tool names from discovery

    // State
    mutable std::mutex mutex_;         ///< Protects state access
    std::atomic<bool> running_{false}; ///< Backend running state
    EventCallback event_callback_;     ///< Registered event handler
    SubscriptionGuard subscription_;   ///< RAII subscription (auto-unsubscribes)

    // Cached toolchanger state
    AmsSystemInfo system_info_;     ///< Current system state
    bool tools_initialized_{false}; ///< Have we received initial state?

    // Per-tool mounted state (for quick lookup)
    std::vector<bool> tool_mounted_; ///< Which tools are mounted
};
