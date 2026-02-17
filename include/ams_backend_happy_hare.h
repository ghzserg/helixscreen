// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_subscription_guard.h"

#include "ams_backend.h"
#include "moonraker_client.h"

#include <atomic>
#include <mutex>
#include <string>

// Forward declaration
class MoonrakerAPI;

/**
 * @file ams_backend_happy_hare.h
 * @brief Happy Hare MMU backend implementation
 *
 * Implements the AmsBackend interface for Happy Hare MMU systems.
 * Communicates with Moonraker to control the MMU via G-code commands
 * and receives state updates via printer.mmu.* subscriptions.
 *
 * Happy Hare Moonraker Variables:
 * - printer.mmu.gate       (int): Current gate (-1=none, -2=bypass)
 * - printer.mmu.tool       (int): Current tool
 * - printer.mmu.filament   (string): "Loaded" or "Unloaded"
 * - printer.mmu.action     (string): "Idle", "Loading", etc.
 * - printer.mmu.gate_status (array[int]): -1=unknown, 0=empty, 1=available, 2=from_buffer
 * - printer.mmu.gate_color_rgb (array[int]): RGB values like 0xFF0000
 * - printer.mmu.gate_material (array[string]): "PLA", "PETG", etc.
 *
 * G-code Commands:
 * - MMU_LOAD GATE={n}   - Load filament from specified gate
 * - MMU_UNLOAD          - Unload current filament
 * - MMU_SELECT GATE={n} - Select gate without loading
 * - T{n}                - Tool change (unload + load)
 * - MMU_HOME            - Home the selector
 * - MMU_RECOVER         - Attempt error recovery
 */
class AmsBackendHappyHare : public AmsBackend {
  public:
    /**
     * @brief Construct Happy Hare backend
     *
     * @param api Pointer to MoonrakerAPI (for sending G-code commands)
     * @param client Pointer to helix::MoonrakerClient (for subscribing to updates)
     *
     * @note Both pointers must remain valid for the lifetime of this backend.
     */
    AmsBackendHappyHare(MoonrakerAPI* api, helix::MoonrakerClient* client);

    ~AmsBackendHappyHare() override;

    // Lifecycle
    AmsError start() override;
    void stop() override;
    void release_subscriptions() override;
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

    // Path visualization
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

    // Bypass mode
    AmsError enable_bypass() override;
    AmsError disable_bypass() override;
    [[nodiscard]] bool is_bypass_active() const override;

    // Endless Spool support (read-only - configured in Happy Hare config)
    [[nodiscard]] helix::printer::EndlessSpoolCapabilities
    get_endless_spool_capabilities() const override;
    [[nodiscard]] std::vector<helix::printer::EndlessSpoolConfig>
    get_endless_spool_config() const override;
    AmsError set_endless_spool_backup(int slot_index, int backup_slot) override;

    /**
     * @brief Reset all tool mappings to defaults
     *
     * Resets tool-to-gate mappings to 1:1 (T0→Gate0, T1→Gate1, etc.)
     * by iterating through all tools and calling set_tool_mapping().
     *
     * @return AmsError with result
     */
    AmsError reset_tool_mappings() override;

    /**
     * @brief Reset all endless spool backup mappings
     *
     * Happy Hare endless spool is read-only (configured in mmu_vars.cfg).
     * Returns not_supported error.
     *
     * @return AmsError with not_supported result
     */
    AmsError reset_endless_spool() override;

    [[nodiscard]] bool has_firmware_spool_persistence() const override {
        return true; // Happy Hare persists via MMU_GATE_MAP SPOOLID
    }

    // Tool Mapping support
    /**
     * @brief Get tool mapping capabilities for Happy Hare
     *
     * Happy Hare supports tool-to-gate mapping via MMU_TTG_MAP G-code.
     *
     * @return Capabilities with supported=true, editable=true
     */
    [[nodiscard]] helix::printer::ToolMappingCapabilities
    get_tool_mapping_capabilities() const override;

    /**
     * @brief Get current tool-to-slot mapping
     *
     * Returns the tool_to_slot_map from system_info_ (populated from ttg_map).
     *
     * @return Vector where index=tool, value=slot
     */
    [[nodiscard]] std::vector<int> get_tool_mapping() const override;

    // Device Management
    [[nodiscard]] std::vector<helix::printer::DeviceSection> get_device_sections() const override;
    [[nodiscard]] std::vector<helix::printer::DeviceAction> get_device_actions() const override;
    AmsError execute_device_action(const std::string& action_id,
                                   const std::any& value = {}) override;

  protected:
    // Allow test helper access to private members
    friend class AmsBackendHappyHareTestHelper;
    friend class AmsBackendHappyHareEndlessSpoolHelper;
    friend class AmsBackendHHMultiUnitHelper;
    friend class HappyHareErrorStateHelper;

  private:
    /**
     * @brief Handle status update notifications from Moonraker
     *
     * Called when printer.mmu.* values change via notify_status_update.
     * Parses the JSON and updates internal state.
     *
     * @param notification JSON notification from Moonraker
     */
    void handle_status_update(const nlohmann::json& notification);

    /**
     * @brief Parse MMU state from Moonraker JSON
     *
     * Extracts mmu object from notification and updates system_info_.
     *
     * @param mmu_data JSON object containing printer.mmu data
     */
    void parse_mmu_state(const nlohmann::json& mmu_data);

    /**
     * @brief Initialize gate structures based on gate_status array size
     *
     * Called when we first receive gate_status to create the correct
     * number of SlotInfo entries.
     *
     * @param gate_count Number of gates detected
     */
    void initialize_gates(int gate_count);

    /**
     * @brief Emit event to registered callback
     * @param event Event name
     * @param data Event data (JSON or empty)
     */
    void emit_event(const std::string& event, const std::string& data = "");

    /**
     * @brief Execute a G-code command via MoonrakerAPI
     *
     * Virtual to allow test overrides for G-code capture.
     *
     * @param gcode The G-code command to execute
     * @return AmsError indicating success or failure to queue command
     */
    virtual AmsError execute_gcode(const std::string& gcode);

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
     * @brief Validate gate index is within range
     *
     * @param gate_index Slot index to validate
     * @return AmsError (SUCCESS if valid, INVALID_GATE otherwise)
     */
    AmsError validate_slot_index(int gate_index) const;

    // Dependencies
    MoonrakerAPI* api_;              ///< For sending G-code commands
    helix::MoonrakerClient* client_; ///< For subscribing to updates

    // State
    mutable std::mutex mutex_;         ///< Protects state access
    std::atomic<bool> running_{false}; ///< Backend running state
    EventCallback event_callback_;     ///< Registered event handler
    SubscriptionGuard subscription_;   ///< RAII subscription (auto-unsubscribes)

    // Cached MMU state
    AmsSystemInfo system_info_;     ///< Current system state
    bool gates_initialized_{false}; ///< Have we seen gate_status yet?
    int num_units_{1};              ///< Number of physical units (default 1)

    // Path visualization state
    int filament_pos_{0};                          ///< Happy Hare filament_pos value
    PathSegment error_segment_{PathSegment::NONE}; ///< Inferred error location

    // Error state tracking
    std::string reason_for_pause_; ///< Last reason_for_pause from MMU (descriptive error text)
    int errored_slot_{-1};         ///< Slot that was in error state (-1 = none)
};
