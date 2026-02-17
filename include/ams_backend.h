// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ams_backend.h
 * @brief Abstract platform-independent interface for multi-filament system operations
 *
 * @pattern Pure virtual interface + static create()/create_auto() factory methods
 * @threading Implementation-dependent; see concrete implementations
 *
 * @see ams_backend_happyhare.cpp, ams_backend_afc.cpp
 */

#pragma once

#include "ams_error.h"
#include "ams_types.h"

class MoonrakerAPI;
namespace helix {
class MoonrakerClient;
}

#include <any>
#include <functional>
#include <memory>
#include <string>

/**
 * @brief Abstract interface for AMS/MMU backend implementations
 *
 * Provides a platform-agnostic API for multi-filament operations.
 * Concrete implementations handle system-specific details:
 * - AmsBackendHappyHare: Happy Hare MMU via Moonraker
 * - AmsBackendAfc: AFC-Klipper-Add-On via Moonraker
 * - AmsBackendMock: Simulator mode with fake data
 *
 * Design principles:
 * - Hide all backend-specific commands/protocols from AmsManager
 * - Provide async operations with event-based completion
 * - Thread-safe operations where needed
 * - Clean error handling with user-friendly messages
 */
class AmsBackend {
  public:
    virtual ~AmsBackend() = default;

    // ========================================================================
    // Event Types
    // ========================================================================

    /**
     * @brief Standard AMS event types
     *
     * Events are delivered asynchronously via registered callbacks.
     * Event names are strings to allow backend-specific extensions.
     */
    static constexpr const char* EVENT_STATE_CHANGED = "STATE_CHANGED"; ///< System state updated
    static constexpr const char* EVENT_SLOT_CHANGED = "SLOT_CHANGED";   ///< Slot info updated
    static constexpr const char* EVENT_LOAD_COMPLETE = "LOAD_COMPLETE"; ///< Load operation finished
    static constexpr const char* EVENT_UNLOAD_COMPLETE =
        "UNLOAD_COMPLETE";                                            ///< Unload operation finished
    static constexpr const char* EVENT_TOOL_CHANGED = "TOOL_CHANGED"; ///< Tool change completed
    static constexpr const char* EVENT_ERROR = "ERROR";               ///< Error occurred
    static constexpr const char* EVENT_ATTENTION_REQUIRED =
        "ATTENTION"; ///< User intervention needed

    // ========================================================================
    // Lifecycle Management
    // ========================================================================

    /**
     * @brief Initialize and start the AMS backend
     *
     * Connects to the underlying AMS system and starts monitoring state.
     * For real backends, this initiates Moonraker subscriptions.
     * For mock backend, this sets up simulated state.
     *
     * @return AmsError with detailed status information
     */
    virtual AmsError start() = 0;

    /**
     * @brief Stop the AMS backend
     *
     * Cleanly shuts down monitoring and releases resources.
     * Safe to call even if not started.
     */
    virtual void stop() = 0;

    /**
     * @brief Release subscriptions without unsubscribing
     *
     * Use during shutdown when the helix::MoonrakerClient may already be destroyed.
     * This abandons the subscription rather than trying to call into the client.
     * Backends that hold SubscriptionGuards should call release() on them.
     */
    virtual void release_subscriptions() {}

    /**
     * @brief Check if backend is currently running/initialized
     * @return true if backend is active and ready for operations
     */
    [[nodiscard]] virtual bool is_running() const = 0;

    // ========================================================================
    // Event System
    // ========================================================================

    /**
     * @brief Callback type for AMS events
     *
     * @param event_name Event identifier (EVENT_* constants)
     * @param data Event-specific payload (JSON string or empty)
     */
    using EventCallback =
        std::function<void(const std::string& event_name, const std::string& data)>;

    /**
     * @brief Register callback for AMS events
     *
     * Events are delivered asynchronously and may arrive from background threads.
     * The callback should be thread-safe or post to main thread.
     *
     * @param callback Handler function for events
     */
    virtual void set_event_callback(EventCallback callback) = 0;

    // ========================================================================
    // State Queries
    // ========================================================================

    /**
     * @brief Get current AMS system information
     *
     * Returns a snapshot of the current system state including:
     * - System type and version
     * - Current tool/slot selection
     * - All unit and slot information
     * - Capability flags
     *
     * @return Current AmsSystemInfo (copy, safe for caller to hold)
     */
    [[nodiscard]] virtual AmsSystemInfo get_system_info() const = 0;

    /**
     * @brief Get the detected AMS type
     * @return AmsType enum value
     */
    [[nodiscard]] virtual AmsType get_type() const = 0;

    /**
     * @brief Get information about a specific slot
     * @param slot_index Slot index (0 to total_slots-1)
     * @return SlotInfo struct (copy, safe for caller to hold)
     */
    [[nodiscard]] virtual SlotInfo get_slot_info(int slot_index) const = 0;

    /**
     * @brief Get current action/operation status
     * @return Current AmsAction enum value
     */
    [[nodiscard]] virtual AmsAction get_current_action() const = 0;

    /**
     * @brief Get currently selected tool number
     * @return Tool number (-1 if none, -2 for bypass on Happy Hare)
     */
    [[nodiscard]] virtual int get_current_tool() const = 0;

    /**
     * @brief Get currently selected slot number
     * @return Slot number (-1 if none, -2 for bypass on Happy Hare)
     */
    [[nodiscard]] virtual int get_current_slot() const = 0;

    /**
     * @brief Check if filament is currently loaded in extruder
     * @return true if filament is loaded
     */
    [[nodiscard]] virtual bool is_filament_loaded() const = 0;

    // ========================================================================
    // Filament Path Visualization
    // ========================================================================

    /**
     * @brief Get the path topology for this AMS system
     *
     * Determines how the filament path is rendered:
     * - LINEAR: Selector picks from multiple gates (Happy Hare ERCF)
     * - HUB: Multiple lanes merge through a hub (AFC Box Turtle)
     *
     * @return PathTopology enum value
     */
    [[nodiscard]] virtual PathTopology get_topology() const = 0;

    /**
     * @brief Get the path topology for a specific unit
     *
     * In mixed-topology systems (e.g., Box Turtle + OpenAMS), different units
     * may have different topologies. This method returns the topology for a
     * specific unit by index.
     *
     * Default implementation falls back to get_topology() for backward compat.
     *
     * @param unit_index Index of the unit (0-based)
     * @return PathTopology for this unit, or system-wide topology if unknown
     */
    [[nodiscard]] virtual PathTopology get_unit_topology(int unit_index) const {
        (void)unit_index;
        return get_topology();
    }

    /**
     * @brief Get current filament position in the path
     *
     * Returns which segment the filament is currently at/in.
     * Used for highlighting the active portion of the path visualization.
     *
     * @return PathSegment enum value (NONE if no filament in system)
     */
    [[nodiscard]] virtual PathSegment get_filament_segment() const = 0;

    /**
     * @brief Get filament position for a specific slot
     *
     * Returns how far filament from a specific slot extends into the path.
     * Used for visualizing all installed filaments, not just the active one.
     * For non-active slots, this typically shows filament up to the prep sensor.
     *
     * @param slot_index Slot index (0 to total_slots-1)
     * @return PathSegment enum value (NONE if no filament installed at slot)
     */
    [[nodiscard]] virtual PathSegment get_slot_filament_segment(int slot_index) const = 0;

    /**
     * @brief Infer which segment has an error
     *
     * When an error occurs, this determines which segment of the path
     * is most likely the problem area based on sensor states and
     * current operation. Used for visual error highlighting.
     *
     * @return PathSegment enum value (NONE if no error or can't determine)
     */
    [[nodiscard]] virtual PathSegment infer_error_segment() const = 0;

    // ========================================================================
    // Filament Operations
    // ========================================================================

    /**
     * @brief Load filament from specified slot (async)
     *
     * Initiates filament load from the specified slot to the extruder.
     * Results delivered via EVENT_LOAD_COMPLETE or EVENT_ERROR.
     *
     * Requires:
     * - System not busy with another operation
     * - Slot has filament available
     * - Extruder at appropriate temperature
     *
     * @param slot_index Slot to load from (0-based)
     * @return AmsError indicating if operation was started successfully
     */
    virtual AmsError load_filament(int slot_index) = 0;

    /**
     * @brief Unload current filament (async)
     *
     * Initiates filament unload from extruder back to current slot.
     * Results delivered via EVENT_UNLOAD_COMPLETE or EVENT_ERROR.
     *
     * Requires:
     * - Filament currently loaded
     * - System not busy with another operation
     * - Extruder at appropriate temperature
     *
     * @return AmsError indicating if operation was started successfully
     */
    virtual AmsError unload_filament() = 0;

    /**
     * @brief Select tool/slot without loading (async)
     *
     * Moves the selector to the specified slot without loading filament.
     * Used for preparation or manual operations.
     *
     * @param slot_index Slot to select (0-based)
     * @return AmsError indicating if operation was started successfully
     */
    virtual AmsError select_slot(int slot_index) = 0;

    /**
     * @brief Perform tool change (async)
     *
     * Complete tool change sequence: unload current, load new.
     * Equivalent to sending T{tool_number} command.
     * Results delivered via EVENT_TOOL_CHANGED or EVENT_ERROR.
     *
     * @param tool_number Tool to change to (0-based)
     * @return AmsError indicating if operation was started successfully
     */
    virtual AmsError change_tool(int tool_number) = 0;

    // ========================================================================
    // Recovery Operations
    // ========================================================================

    /**
     * @brief Attempt recovery from error state
     *
     * Initiates system recovery procedure appropriate to current error.
     * For Happy Hare, this typically invokes MMU_RECOVER.
     *
     * @return AmsError indicating if recovery was started
     */
    virtual AmsError recover() = 0;

    /**
     * @brief Reset the AMS system (async)
     *
     * Resets the system to a known good state.
     * - Happy Hare: Calls MMU_HOME to home the selector
     * - AFC: Calls AFC_RESET to reset the system
     *
     * @return AmsError indicating if operation was started
     */
    virtual AmsError reset() = 0;

    /**
     * @brief Reset a specific lane/slot
     *
     * Resets an individual lane to a known good state without affecting others.
     * Default implementation returns NOT_SUPPORTED.
     *
     * @param slot_index Lane to reset (0-based)
     * @return AmsError indicating if operation was started
     */
    virtual AmsError reset_lane(int slot_index) {
        (void)slot_index;
        return AmsErrorHelper::not_supported("Per-lane reset not supported");
    }

    /**
     * @brief Cancel current operation
     *
     * Attempts to safely abort the current operation.
     * Not all operations can be cancelled.
     *
     * @return AmsError indicating if cancellation was accepted
     */
    virtual AmsError cancel() = 0;

    // ========================================================================
    // Configuration Operations
    // ========================================================================

    /**
     * @brief Update slot filament information
     *
     * Sets the color, material, and other filament info for a slot.
     * Changes are persisted via Moonraker/Spoolman as appropriate.
     *
     * @param slot_index Slot to update (0-based)
     * @param info New slot information (only filament fields used)
     * @return AmsError indicating if update succeeded
     */
    virtual AmsError set_slot_info(int slot_index, const SlotInfo& info) = 0;

    /**
     * @brief Set tool-to-slot mapping
     *
     * Configures which slot a tool number maps to.
     * Happy Hare specific - may not be supported on all backends.
     *
     * @param tool_number Tool number (0-based)
     * @param slot_index Slot to map to (0-based)
     * @return AmsError indicating if mapping was set
     */
    virtual AmsError set_tool_mapping(int tool_number, int slot_index) = 0;

    // ========================================================================
    // Bypass Mode Operations
    // ========================================================================

    /**
     * @brief Enable bypass mode
     *
     * Activates bypass mode where an external spool feeds directly to the
     * toolhead, bypassing the MMU/hub system. Sets current_slot to -2.
     *
     * Not all backends support bypass mode - check supports_bypass flag.
     *
     * @return AmsError indicating if bypass was enabled
     */
    virtual AmsError enable_bypass() = 0;

    /**
     * @brief Disable bypass mode
     *
     * Deactivates bypass mode. Filament should be unloaded from toolhead first.
     *
     * @return AmsError indicating if bypass was disabled
     */
    virtual AmsError disable_bypass() = 0;

    /**
     * @brief Check if bypass mode is currently active
     * @return true if bypass is active (current_slot == -2)
     */
    [[nodiscard]] virtual bool is_bypass_active() const = 0;

    // ========================================================================
    // Dryer Control (Optional - default implementations return "not supported")
    // ========================================================================

    /**
     * @brief Get dryer state and capabilities
     *
     * Returns current dryer state including temperature, duration, and
     * hardware capabilities. Not all AMS systems have dryers - check
     * DryerInfo::supported before showing dryer UI.
     *
     * @return DryerInfo struct (supported=false if no dryer)
     */
    [[nodiscard]] virtual DryerInfo get_dryer_info() const {
        return DryerInfo{.supported = false};
    }

    /**
     * @brief Start drying operation
     *
     * Initiates filament drying at specified temperature and duration.
     * Not all AMS systems support drying - check get_dryer_info().supported.
     *
     * @param temp_c Target temperature in Celsius (within min_temp_c..max_temp_c)
     * @param duration_min Drying duration in minutes (positive, capped at max_duration_min)
     * @param fan_pct Fan speed percentage (0-100, -1 = use backend default)
     * @return AmsError with SUCCESS result on success, or error with reason
     */
    virtual AmsError start_drying(float temp_c, int duration_min, int fan_pct = -1) {
        (void)temp_c;
        (void)duration_min;
        (void)fan_pct;
        return AmsErrorHelper::not_supported("Dryer");
    }

    /**
     * @brief Stop drying operation
     *
     * Stops any active drying and turns off heater/fan.
     *
     * @return AmsError with SUCCESS result on success, or error with reason
     */
    virtual AmsError stop_drying() {
        return AmsErrorHelper::not_supported("Dryer");
    }

    /**
     * @brief Update drying parameters while running
     *
     * Adjusts temperature, duration, or fan speed during an active dry cycle.
     * Pass -1 to keep current value for any parameter.
     *
     * @param temp_c New target temperature (-1 = no change)
     * @param duration_min New duration (-1 = no change)
     * @param fan_pct New fan speed (-1 = no change)
     * @return AmsError with SUCCESS result on success, or error with reason
     */
    virtual AmsError update_drying(float temp_c = -1, int duration_min = -1, int fan_pct = -1) {
        (void)temp_c;
        (void)duration_min;
        (void)fan_pct;
        return AmsErrorHelper::not_supported("Dryer");
    }

    /**
     * @brief Get available drying presets
     *
     * Returns preset profiles for common filament materials.
     * Backends can override to provide hardware-specific presets.
     * Falls back to get_default_drying_presets() if not overridden.
     *
     * @return Vector of DryingPreset structs
     */
    [[nodiscard]] virtual std::vector<DryingPreset> get_drying_presets() const {
        return get_default_drying_presets();
    }

    // ========================================================================
    // Endless Spool Control
    // ========================================================================

    /**
     * @brief Get endless spool capabilities for this backend
     *
     * Returns information about whether endless spool is supported and
     * whether the configuration can be modified via the UI.
     *
     * @return Capabilities struct with supported/editable flags
     */
    [[nodiscard]] virtual helix::printer::EndlessSpoolCapabilities
    get_endless_spool_capabilities() const {
        return {false, false, ""}; // Default: not supported
    }

    /**
     * @brief Get endless spool configuration for all slots
     *
     * Returns the backup slot configuration for each slot in the system.
     * For Happy Hare, this translates the group-based configuration to
     * per-slot backup mappings.
     *
     * @return Vector of configs, one per slot
     */
    [[nodiscard]] virtual std::vector<helix::printer::EndlessSpoolConfig>
    get_endless_spool_config() const {
        return {}; // Default: empty
    }

    /**
     * @brief Set backup slot for endless spool
     *
     * Configures which slot will be used as a backup when the specified
     * slot runs out of filament. Pass -1 as backup_slot to disable backup.
     *
     * Not all backends support editing:
     * - AFC: Fully editable via SET_RUNOUT G-code
     * - Happy Hare: Read-only (configured via mmu_vars.cfg)
     *
     * @param slot_index Source slot
     * @param backup_slot Backup slot (-1 to disable)
     * @return AmsError with result
     */
    virtual AmsError set_endless_spool_backup(int slot_index, int backup_slot) {
        (void)slot_index;
        (void)backup_slot;
        return AmsErrorHelper::not_supported("Endless spool");
    }

    /**
     * @brief Reset all tool mappings to defaults
     *
     * Resets tool-to-slot mappings to their original/default configuration.
     * Default behavior is typically 1:1 mapping (T0→Slot0, T1→Slot1, etc.).
     *
     * @return AmsError with result
     */
    virtual AmsError reset_tool_mappings() {
        return AmsErrorHelper::not_supported("Reset tool mappings");
    }

    /**
     * @brief Reset all endless spool backup mappings
     *
     * Clears all endless spool backup slot configurations, setting each
     * slot's backup to -1 (no backup).
     *
     * @return AmsError with result
     */
    virtual AmsError reset_endless_spool() {
        return AmsErrorHelper::not_supported("Reset endless spool");
    }

    // ========================================================================
    // Tool Mapping Control
    // ========================================================================

    /**
     * @brief Get tool mapping capabilities for this backend
     *
     * Returns information about whether tool mapping is supported and
     * whether the configuration can be modified via the UI.
     *
     * @return Capabilities struct with supported/editable flags
     */
    [[nodiscard]] virtual helix::printer::ToolMappingCapabilities
    get_tool_mapping_capabilities() const {
        return {false, false, ""}; // Default: not supported
    }

    /**
     * @brief Get current tool-to-slot mapping
     *
     * Returns the mapping from tool number to slot index.
     * The vector index represents the tool number, and the value at that
     * index is the slot that tool maps to.
     *
     * @return Vector where index=tool, value=slot (empty if not supported)
     */
    [[nodiscard]] virtual std::vector<int> get_tool_mapping() const {
        return {}; // Default: empty
    }

    // ========================================================================
    // Device-Specific Actions
    // ========================================================================

    /**
     * @brief Get available device sections for this backend
     *
     * Sections group related actions (e.g., "Calibration", "Speed Settings").
     * UI renders sections in display_order.
     *
     * @return Vector of DeviceSection (empty if no device-specific features)
     */
    [[nodiscard]] virtual std::vector<helix::printer::DeviceSection> get_device_sections() const {
        return {};
    }

    /**
     * @brief Get available device actions
     *
     * Returns all device-specific actions. UI groups them by section ID.
     *
     * @return Vector of DeviceAction (empty if no device-specific features)
     */
    [[nodiscard]] virtual std::vector<helix::printer::DeviceAction> get_device_actions() const {
        return {};
    }

    /**
     * @brief Execute a device action
     *
     * @param action_id The action ID from get_device_actions()
     * @param value Optional value for toggles/sliders/dropdowns
     * @return AmsError indicating success/failure
     */
    virtual AmsError execute_device_action(const std::string& action_id,
                                           const std::any& value = {}) {
        (void)action_id;
        (void)value;
        return AmsErrorHelper::not_supported("Device actions");
    }

    // ========================================================================
    // Capability Queries
    // ========================================================================

    /**
     * @brief Check if backend automatically heats extruder before loading
     *
     * Some backends (like AFC) use material-specific temperatures from their
     * configuration (e.g., default_material_temps in AFC.cfg) to preheat the
     * extruder before loading filament. This eliminates the need for the UI
     * to manage preheating.
     *
     * @return true if backend handles preheat automatically, false if UI should manage it
     */
    [[nodiscard]] virtual bool supports_auto_heat_on_load() const {
        return false;
    }

    /**
     * @brief Whether this backend persists spool assignments via firmware
     *
     * Backends like Happy Hare (MMU_GATE_MAP/SPOOLID) and AFC (SET_SPOOL_ID)
     * store spool-to-slot mappings in firmware gcode. For these, ToolState
     * mirrors firmware state but doesn't need to persist separately.
     *
     * Backends without firmware persistence (tool changers, ValgACE) rely on
     * ToolState's own persistence (Moonraker DB + local JSON).
     *
     * @return true if firmware handles spool persistence, false if ToolState should
     */
    [[nodiscard]] virtual bool has_firmware_spool_persistence() const {
        return false;
    }

    // ========================================================================
    // Discovery Configuration (Optional - default implementations are no-ops)
    // ========================================================================

    /**
     * @brief Set discovered lane and hub names from PrinterCapabilities
     *
     * Called before start() to provide lane names discovered from printer.objects.list.
     * Only AFC backend uses this - other backends ignore it.
     *
     * @param lane_names Lane names from PrinterCapabilities::get_afc_lane_names()
     * @param hub_names Hub names from PrinterCapabilities::get_afc_hub_names()
     */
    virtual void set_discovered_lanes(const std::vector<std::string>& lane_names,
                                      const std::vector<std::string>& hub_names) {
        (void)lane_names;
        (void)hub_names;
    }

    /**
     * @brief Set discovered tool names from PrinterCapabilities
     *
     * Called before start() to provide tool names discovered from printer.objects.list.
     * Only tool changer backend uses this - other backends ignore it.
     *
     * @param tool_names Tool names from PrinterCapabilities::get_tool_names()
     */
    virtual void set_discovered_tools(std::vector<std::string> tool_names) {
        (void)tool_names;
    }

    /**
     * @brief Set filament sensor names from PrinterCapabilities
     *
     * Called before start() to provide filament sensor names from printer.objects.list.
     * AFC backend uses this to detect hardware vs virtual bypass sensor.
     *
     * @param sensor_names Sensor names (e.g., "filament_switch_sensor virtual_bypass")
     */
    virtual void set_discovered_sensors(const std::vector<std::string>& sensor_names) {
        (void)sensor_names;
    }

    // ========================================================================
    // Factory Method
    // ========================================================================

    /**
     * @brief Create appropriate backend for detected AMS type (mock only)
     *
     * Factory method that creates a mock backend for testing.
     * For real backends, use the overload that accepts MoonrakerAPI and MoonrakerClient.
     *
     * In mock mode (RuntimeConfig::should_mock_ams()), returns AmsBackendMock.
     *
     * @param detected_type The detected AMS type from printer discovery
     * @return Unique pointer to backend instance, or nullptr if type is NONE
     * @deprecated Use create(AmsType, MoonrakerAPI*, helix::MoonrakerClient*) for real backends
     */
    static std::unique_ptr<AmsBackend> create(AmsType detected_type);

    /**
     * @brief Create appropriate backend for detected AMS type with dependencies
     *
     * Factory method that creates the correct backend implementation:
     * - HAPPY_HARE: AmsBackendHappyHare (requires api and client)
     * - AFC: AmsBackendAfc (requires api and client)
     * - NONE: nullptr (no AMS detected)
     *
     * In mock mode (RuntimeConfig::should_mock_ams()), returns AmsBackendMock.
     *
     * @param detected_type The detected AMS type from printer discovery
     * @param api Pointer to MoonrakerAPI for sending commands
     * @param client Pointer to helix::MoonrakerClient for subscriptions
     * @return Unique pointer to backend instance, or nullptr if type is NONE
     */
    static std::unique_ptr<AmsBackend> create(AmsType detected_type, MoonrakerAPI* api,
                                              helix::MoonrakerClient* client);

    /**
     * @brief Create mock backend for testing
     *
     * Creates a mock backend regardless of actual printer state.
     * Used when --test flag is passed or for development.
     *
     * @param slot_count Number of simulated slots (default 4)
     * @return Unique pointer to mock backend instance
     */
    static std::unique_ptr<AmsBackend> create_mock(int slot_count = 4);
};
