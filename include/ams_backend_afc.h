// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_subscription_guard.h"

#include "afc_config_manager.h"
#include "ams_backend.h"
#include "moonraker_client.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// Forward declaration
class MoonrakerAPI;

/**
 * @file ams_backend_afc.h
 * @brief AFC-Klipper-Add-On backend implementation
 *
 * Implements the AmsBackend interface for AFC (Armored Turtle / Box Turtle)
 * multi-filament systems. Communicates with Moonraker to control AFC via
 * G-code commands and receives state updates via printer.afc.* subscriptions
 * and database lane_data queries.
 *
 * AFC Terminology Differences from Happy Hare:
 * - "Lanes" instead of "Gates"
 * - "Units" are typically called "Box Turtles" or "AFC units"
 * - Lane names may be configurable (lane1, lane2... or custom names)
 *
 * AFC State Sources:
 * - Printer object: printer.afc with status info
 * - Moonraker database: lane_data (via server.database.get_item)
 *
 * Lane Data Structure (from database):
 * {
 *   "lane1": {"color": "FF0000", "material": "PLA", "loaded": false},
 *   "lane2": {"color": "00FF00", "material": "PETG", "loaded": true}
 * }
 *
 * G-code Commands:
 * - AFC_LOAD LANE={name}   - Load filament from specified lane
 * - AFC_UNLOAD             - Unload current filament
 * - AFC_CUT LANE={name}    - Cut filament (if cutter supported)
 * - AFC_HOME               - Home the AFC system
 * - T{n}                   - Tool change (unload + load)
 */
/**
 * @brief Per-extruder info for toolchanger configurations
 *
 * When AFC detects a toolchanger (num_extruders > 1), the webhook status
 * includes per-extruder data: which lane is loaded and which lanes can
 * feed each extruder.
 */
struct AfcExtruderInfo {
    std::string name;                         ///< Extruder name ("extruder", "extruder1")
    std::string lane_loaded;                  ///< Currently loaded lane (or empty)
    std::vector<std::string> available_lanes; ///< Lanes that can feed this extruder
};

/**
 * @brief Per-unit info parsed from flat string units and unit-level Klipper objects
 *
 * When AFC reports units as flat strings (e.g., "OpenAMS AMS_1", "Box_Turtle Turtle_1"),
 * this struct stores the parsed type/name and the Klipper object key used to receive
 * unit-level status updates (e.g., "AFC_OpenAMS AMS_1"). The lanes, extruders, hubs,
 * and buffers arrays are populated from the unit-level Klipper object data.
 */
struct AfcUnitInfo {
    std::string klipper_key; ///< Klipper object key (e.g., "AFC_BoxTurtle Turtle_1")
    std::string name;        ///< Unit instance name (e.g., "Turtle_1", "AMS_1")
    std::string type;        ///< Unit type (e.g., "Box_Turtle", "OpenAMS")

    std::vector<std::string> lanes;     ///< Lane names belonging to this unit
    std::vector<std::string> extruders; ///< Extruder names for this unit
    std::vector<std::string> hubs;      ///< Hub names for this unit
    std::vector<std::string> buffers;   ///< Buffer names for this unit

    PathTopology topology = PathTopology::HUB; ///< Derived topology for this unit
};

class AmsBackendAfc : public AmsBackend {
  public:
    /**
     * @brief Construct AFC backend
     *
     * @param api Pointer to MoonrakerAPI (for sending G-code commands)
     * @param client Pointer to helix::MoonrakerClient (for subscribing to updates)
     *
     * @note Both pointers must remain valid for the lifetime of this backend.
     */
    AmsBackendAfc(MoonrakerAPI* api, helix::MoonrakerClient* client);

    ~AmsBackendAfc() override;

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
    [[nodiscard]] PathTopology get_unit_topology(int unit_index) const override;
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
    AmsError reset_lane(int slot_index) override;
    AmsError cancel() override;

    // Configuration
    AmsError set_slot_info(int slot_index, const SlotInfo& info) override;
    AmsError set_tool_mapping(int tool_number, int slot_index) override;

    // Bypass mode
    AmsError enable_bypass() override;
    AmsError disable_bypass() override;
    [[nodiscard]] bool is_bypass_active() const override;

    // Capability queries
    /**
     * @brief AFC automatically heats extruder using default_material_temps
     * @return true - AFC handles preheat via its configuration
     */
    [[nodiscard]] bool supports_auto_heat_on_load() const override {
        return true;
    }

    [[nodiscard]] bool has_firmware_spool_persistence() const override {
        return true; // AFC uses SET_SPOOL_ID gcode for persistence
    }

    // Endless Spool support
    /**
     * @brief Get endless spool capabilities for AFC
     *
     * AFC supports per-slot backup configuration via SET_RUNOUT G-code.
     *
     * @return Capabilities with supported=true, editable=true
     */
    [[nodiscard]] helix::printer::EndlessSpoolCapabilities
    get_endless_spool_capabilities() const override;

    /**
     * @brief Get endless spool configuration for all lanes
     *
     * Returns the backup slot configuration for each lane.
     *
     * @return Vector of configs, one per lane
     */
    [[nodiscard]] std::vector<helix::printer::EndlessSpoolConfig>
    get_endless_spool_config() const override;

    /**
     * @brief Set backup slot for endless spool
     *
     * Sends SET_RUNOUT G-code to configure which lane will be used as backup
     * when the specified lane runs out of filament.
     *
     * @param slot_index Source lane (0 to lane_names_.size()-1)
     * @param backup_slot Backup lane (-1 to disable)
     * @return AmsError with result
     */
    AmsError set_endless_spool_backup(int slot_index, int backup_slot) override;

    /**
     * @brief Reset all tool mappings to defaults
     *
     * Uses RESET_AFC_MAPPING RUNOUT=no to reset tool-to-lane mappings
     * while preserving existing endless spool configuration.
     *
     * @return AmsError with result
     */
    AmsError reset_tool_mappings() override;

    /**
     * @brief Reset all endless spool backup mappings
     *
     * Iterates through all lanes and sets each backup to -1 (disabled)
     * via SET_RUNOUT G-code commands.
     *
     * @return AmsError with result
     */
    AmsError reset_endless_spool() override;

    // Tool Mapping support
    /**
     * @brief Get tool mapping capabilities for AFC
     *
     * AFC supports per-lane tool assignment via SET_MAP G-code.
     *
     * @return Capabilities with supported=true, editable=true
     */
    [[nodiscard]] helix::printer::ToolMappingCapabilities
    get_tool_mapping_capabilities() const override;

    /**
     * @brief Get current tool-to-slot mapping
     *
     * Returns the tool_to_slot_map from system_info_.
     *
     * @return Vector where index=tool, value=slot
     */
    [[nodiscard]] std::vector<int> get_tool_mapping() const override;

    /**
     * @brief Set discovered lane and hub names from PrinterCapabilities
     *
     * Called before start() to provide lane names discovered from printer.objects.list.
     * These are used as a fallback when the lane_data database is not available
     * (AFC versions < 1.0.32).
     *
     * For v1.0.32+, query_lane_data() may override/supplement this data.
     *
     * @param lane_names Lane names from PrinterCapabilities::get_afc_lane_names()
     * @param hub_names Hub names from PrinterCapabilities::get_afc_hub_names()
     */
    void set_discovered_lanes(const std::vector<std::string>& lane_names,
                              const std::vector<std::string>& hub_names) override;

    void set_discovered_sensors(const std::vector<std::string>& sensor_names) override;

    // Device-Specific Actions
    /**
     * @brief Get available device sections for AFC backend
     *
     * AFC exposes calibration and speed settings sections.
     *
     * @return Vector of DeviceSection for UI grouping
     */
    [[nodiscard]] std::vector<helix::printer::DeviceSection> get_device_sections() const override;

    /**
     * @brief Get available device actions for AFC backend
     *
     * Returns AFC-specific actions including:
     * - Calibration wizard
     * - Bowden length configuration
     * - Speed multipliers (forward/reverse)
     *
     * @return Vector of DeviceAction for UI rendering
     */
    [[nodiscard]] std::vector<helix::printer::DeviceAction> get_device_actions() const override;

    /**
     * @brief Execute an AFC-specific device action
     *
     * @param action_id Action identifier from get_device_actions()
     * @param value Optional value for sliders/toggles
     * @return AmsError indicating success or failure
     */
    AmsError execute_device_action(const std::string& action_id,
                                   const std::any& value = {}) override;

  protected:
    // Allow test helper access to private members
    friend class AmsBackendAfcTestHelper;
    friend class AmsBackendAfcEndlessSpoolHelper;
    friend class AmsBackendAfcMultiUnitHelper;
    friend class HubSensorTestHelper;
    friend class AmsBackendAfcMultiExtruderHelper;
    friend class AmsBackendAfcConfigHelper;
    friend class AfcErrorHandlingHelper;
    friend class AfcErrorStateHelper;

  private:
    /**
     * @brief Handle status update notifications from Moonraker
     *
     * Called when printer.afc.* values change via notify_status_update.
     * Parses the JSON and updates internal state.
     *
     * @param notification JSON notification from Moonraker
     */
    void handle_status_update(const nlohmann::json& notification);

    /**
     * @brief Parse AFC state from Moonraker JSON
     *
     * Extracts afc object from notification and updates system_info_.
     *
     * @param afc_data JSON object containing printer.afc data
     */
    void parse_afc_state(const nlohmann::json& afc_data);

    /**
     * @brief Query current AFC state from Moonraker
     *
     * Queries the current state of all AFC objects via printer.objects.query.
     * With the early hardware discovery callback architecture, this is typically
     * NOT needed - the backend receives initial state naturally from the
     * printer.objects.subscribe response.
     *
     * Available for manual re-query scenarios (e.g., recovery from errors).
     */
    void query_initial_state();

    /**
     * @brief Query lane data from Moonraker database
     *
     * AFC stores lane configuration in Moonraker's database under the
     * "AFC" namespace with key "lane_data".
     */
    void query_lane_data();

    /**
     * @brief Parse lane data from database response
     *
     * Processes the lane_data JSON object and updates system_info_.gates.
     *
     * @param lane_data JSON object containing lane configurations
     */
    void parse_lane_data(const nlohmann::json& lane_data);

    /**
     * @brief Detect AFC version by querying afc-install database namespace
     *
     * Queries Moonraker's database for the afc-install namespace which
     * contains version information. Sets afc_version_ and capability flags.
     */
    void detect_afc_version();

    /**
     * @brief Check if installed AFC version meets minimum requirement
     *
     * @param required Minimum version string (e.g., "1.0.32")
     * @return true if installed version >= required version
     */
    bool version_at_least(const std::string& required) const;

    /**
     * @brief Parse AFC_stepper lane object for sensor states and filament info
     *
     * @param lane_name Lane identifier (e.g., "lane1")
     * @param data JSON object from AFC_stepper lane{N}
     */
    void parse_afc_stepper(const std::string& lane_name, const nlohmann::json& data);

    /**
     * @brief Parse AFC_hub object for per-hub sensor state
     *
     * @param hub_name Name of the hub (e.g., "Turtle_1")
     * @param data JSON object from AFC_hub
     */
    void parse_afc_hub(const std::string& hub_name, const nlohmann::json& data);

    /**
     * @brief Parse AFC_buffer object for buffer health and fault data
     *
     * Extracts fault_detection_enabled, distance_to_fault, state, and lane mapping
     * from the buffer status object. Populates buffer_health on mapped slots and
     * creates WARNING-level SlotError when faults are detected.
     *
     * @param buffer_name Name of the buffer (e.g., "Turtle_1")
     * @param data JSON object from AFC_buffer
     */
    void parse_afc_buffer(const std::string& buffer_name, const nlohmann::json& data);

    /**
     * @brief Parse AFC_extruder object for toolhead sensor states
     *
     * @param data JSON object from AFC_extruder
     */
    void parse_afc_extruder(const nlohmann::json& data);

    /**
     * @brief Parse a unit-level Klipper object (AFC_BoxTurtle, AFC_OpenAMS)
     *
     * Reads lanes[], extruders[], hubs[], buffers[] arrays from the unit object
     * and derives topology (PARALLEL vs HUB) based on hub/extruder counts.
     *
     * @param unit_info The AfcUnitInfo to populate
     * @param data JSON object from the unit-level Klipper object
     */
    void parse_afc_unit_object(AfcUnitInfo& unit_info, const nlohmann::json& data);

    /**
     * @brief Rebuild unit_lane_map_ and reorganize units from unit_infos_ data
     *
     * Called after all unit-level objects have been parsed. Rebuilds the
     * unit-to-lane mapping from unit_infos_ and triggers reorganize_units_from_map().
     */
    void reorganize_units_from_unit_info();

    /**
     * @brief Initialize lane structures based on discovered lanes
     *
     * Called when we first receive lane data to create the correct
     * number of SlotInfo entries.
     *
     * @param lane_names Vector of lane name strings
     */
    void initialize_lanes(const std::vector<std::string>& lane_names);

    /**
     * @brief Reorganize units based on unit-lane mapping from AFC
     *
     * When AFC reports multiple units with per-unit lane assignments,
     * this method rebuilds system_info_.units to reflect the actual
     * multi-unit hardware topology. Preserves existing slot data
     * (colors, materials, etc.) during reorganization.
     *
     * Called from parse_afc_state() when unit_lane_map_ is populated
     * and lanes are already initialized.
     */
    void reorganize_units_from_map();

    /**
     * @brief Get lane name for a slot index
     *
     * AFC uses lane names (e.g., "lane1", "lane2") instead of numeric indices.
     *
     * @param slot_index Slot/lane index (0-based)
     * @return Lane name or empty string if invalid
     */
    std::string get_lane_name(int slot_index) const;

    /**
     * @brief Compute filament segment from sensor states (no locking)
     *
     * Internal helper called from locked contexts to avoid deadlock.
     * Uses lane_sensors_[], hub_sensors_, tool_start_sensor_, tool_end_sensor_.
     *
     * @return PathSegment indicating filament position
     */
    [[nodiscard]] PathSegment compute_filament_segment_unlocked() const;

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
    virtual AmsError execute_gcode(const std::string& gcode);

    /**
     * @brief Execute a G-code command with user-facing toast notifications
     *
     * Like execute_gcode() but shows a success or error toast when the
     * async Moonraker callback fires. Thread-safe (uses ui_async_call).
     *
     * @param gcode The G-code command to execute
     * @param success_msg Toast message on success (empty = no toast)
     * @param error_prefix Toast prefix on error (shown as "prefix: error details")
     * @return AmsError indicating success or failure to queue command
     */
    virtual AmsError execute_gcode_notify(const std::string& gcode, const std::string& success_msg,
                                          const std::string& error_prefix);

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
    MoonrakerAPI* api_;              ///< For sending G-code commands
    helix::MoonrakerClient* client_; ///< For subscribing to updates

    // State
    mutable std::recursive_mutex mutex_; ///< Protects state access (recursive for callback safety)
    std::atomic<bool> running_{false};   ///< Backend running state
    EventCallback event_callback_;       ///< Registered event handler
    SubscriptionGuard subscription_;     ///< RAII subscription (auto-unsubscribes)

    // Cached AFC state
    AmsSystemInfo system_info_;           ///< Current system state
    bool lanes_initialized_{false};       ///< Have we received lane data yet?
    std::vector<std::string> lane_names_; ///< Ordered list of lane names

    // Lane name to slot index mapping
    std::unordered_map<std::string, int> lane_name_to_index_;

    // Unit-to-lane mapping (populated from AFC unit data)
    // Key: unit name, Value: lane names belonging to that unit
    std::unordered_map<std::string, std::vector<std::string>> unit_lane_map_;

    // Version detection
    std::string afc_version_{"unknown"}; ///< Detected AFC version (e.g., "1.0.0")
    bool has_lane_data_db_{false};       ///< v1.0.32+ has lane_data in Moonraker DB

    // Per-lane sensor state (from AFC_stepper objects)
    struct LaneSensors {
        bool prep = false;           ///< Prep sensor triggered
        bool load = false;           ///< Load sensor triggered
        bool loaded_to_hub = false;  ///< Filament reached hub
        std::string buffer_status;   ///< Buffer state (e.g., "Advancing")
        std::string filament_status; ///< Filament readiness (e.g., "Ready", "Not Ready")
        float dist_hub = 0.0f;       ///< Distance to hub in mm
    };
    std::array<LaneSensors, 16> lane_sensors_{}; ///< Sensor state for each lane

    // Hub and toolhead sensors (from AFC_hub and AFC_extruder objects)
    std::unordered_map<std::string, bool> hub_sensors_; ///< Per-hub sensor state, keyed by hub name
    bool tool_start_sensor_{false};                     ///< Toolhead entry sensor
    bool tool_end_sensor_{false};                       ///< Toolhead exit/nozzle sensor

    // Global state
    bool error_state_{false};       ///< AFC error state
    bool bypass_active_{false};     ///< Bypass mode active (external spool)
    bool afc_quiet_mode_{false};    ///< AFC quiet mode state
    bool afc_led_state_{false};     ///< AFC LED state
    std::string current_lane_name_; ///< Currently active lane name
    // Two dedup trackers: last_error_msg_ prevents duplicate emit_event(EVENT_ERROR),
    // last_seen_message_ prevents duplicate toast/notification display. Both reset
    // when the AFC message field clears.
    std::string last_error_msg_;
    std::string last_seen_message_;
    std::string last_message_type_;      ///< Type of last system message ("error", "warning", etc.)
    std::vector<std::string> hub_names_; ///< Discovered hub names
    std::vector<std::string> buffer_names_; ///< Discovered buffer names
    float bowden_length_{450.0f};           ///< Bowden tube length from hub (default 450mm)

    // Multi-extruder (toolchanger) state
    int num_extruders_{1}; ///< Number of extruders (1 = standard, 2+ = toolchanger)
    std::vector<AfcExtruderInfo>
        extruders_; ///< Per-extruder info (populated from system.extruders)

    // Unit-level info from flat string units and unit Klipper objects
    std::vector<AfcUnitInfo> unit_infos_; ///< Parsed from flat string "Type Name" units

    // Extruder names from top-level AFC.extruders array (for multi-extruder iteration)
    std::vector<std::string> extruder_names_; ///< e.g., {"extruder", "extruder1", ...}

    // Path visualization state
    PathSegment error_segment_{PathSegment::NONE}; ///< Inferred error location

    // Endless spool configuration
    std::vector<helix::printer::EndlessSpoolConfig>
        endless_spool_configs_; ///< Per-lane backup config

    // helix::Config file managers (lazy-loaded on first device action access)
    std::unique_ptr<AfcConfigManager> afc_config_;        ///< AFC/AFC.cfg
    std::unique_ptr<AfcConfigManager> macro_vars_config_; ///< AFC/AFC_Macro_Vars.cfg
    std::atomic<bool> configs_loading_{false};            ///< Currently loading config files
    std::atomic<bool> configs_loaded_{
        false}; ///< helix::Config files have been loaded (acquire/release barrier)

    /// Load AFC config files from printer
    void load_afc_configs();

    /// Helper to get macro variable as float
    float get_macro_var_float(const std::string& key, float default_val) const;
    /// Helper to get macro variable as bool
    bool get_macro_var_bool(const std::string& key, bool default_val) const;
};
