// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "capability_overrides.h"
#include "lvgl/lvgl.h"
#include "spdlog/spdlog.h"

#include <mutex>
#include <string>
#include <vector>

#include "hv/json.hpp" // libhv's nlohmann json (via cpputil/)

using json = nlohmann::json;

/**
 * @brief Network connection status states
 */
enum class NetworkStatus {
    DISCONNECTED, ///< No network connection
    CONNECTING,   ///< Connecting to network
    CONNECTED     ///< Connected to network
};

/**
 * @brief Printer connection status states
 */
enum class PrinterStatus {
    DISCONNECTED, ///< Printer not connected
    READY,        ///< Printer connected and ready
    PRINTING,     ///< Printer actively printing
    ERROR         ///< Printer in error state
};

/**
 * @brief Klipper firmware state (klippy_state from Moonraker)
 *
 * Represents the state of the Klipper firmware service, independent of
 * the Moonraker WebSocket connection. When klippy_state is not READY,
 * the printer cannot accept G-code commands even if Moonraker is connected.
 */
enum class KlippyState {
    READY = 0,    ///< Normal operation, printer ready for commands
    STARTUP = 1,  ///< Klipper is starting up (during RESTART/FIRMWARE_RESTART)
    SHUTDOWN = 2, ///< Emergency shutdown (M112)
    ERROR = 3     ///< Klipper error state (check klippy.log)
};

/**
 * @brief Print job state (from Moonraker print_stats.state)
 *
 * Represents the state of the current print job as reported by Klipper/Moonraker.
 * This is the canonical enum for print job state throughout HelixScreen.
 *
 * @note Values are chosen to match the integer representation used internally
 *       by MoonrakerClientMock for backward compatibility.
 */
enum class PrintJobState {
    STANDBY = 0,   ///< No active print, printer idle (Moonraker: "standby")
    PRINTING = 1,  ///< Actively printing (Moonraker: "printing")
    PAUSED = 2,    ///< Print paused (Moonraker: "paused")
    COMPLETE = 3,  ///< Print finished successfully (Moonraker: "complete")
    CANCELLED = 4, ///< Print cancelled by user (Moonraker: "cancelled")
    ERROR = 5      ///< Print failed with error (Moonraker: "error")
};

/**
 * @brief Parse Moonraker print state string to PrintJobState enum
 *
 * Converts Moonraker's print_stats.state string to the corresponding enum.
 * Unknown strings default to STANDBY.
 *
 * @param state_str Moonraker state string (e.g., "printing", "paused")
 * @return Corresponding PrintJobState enum value
 */
PrintJobState parse_print_job_state(const char* state_str);

/**
 * @brief Print start initialization phase (detected from G-code response output)
 *
 * Represents the current phase during PRINT_START macro execution.
 * Used to show progress to the user during the initialization sequence
 * before actual printing begins.
 *
 * @note Phases are detected via best-effort pattern matching on G-code responses.
 *       Not all macros output all phases - progress estimation handles missing phases.
 */
enum class PrintStartPhase {
    IDLE = 0,           ///< Not in PRINT_START (normal operation)
    INITIALIZING = 1,   ///< PRINT_START detected, waiting for phases
    HOMING = 2,         ///< G28 / Home All Axes detected
    HEATING_BED = 3,    ///< M140/M190 / Heating bed detected
    HEATING_NOZZLE = 4, ///< M104/M109 / Heating nozzle detected
    QGL = 5,            ///< QUAD_GANTRY_LEVEL detected
    Z_TILT = 6,         ///< Z_TILT_ADJUST detected
    BED_MESH = 7,       ///< BED_MESH_CALIBRATE or BED_MESH_PROFILE LOAD detected
    CLEANING = 8,       ///< CLEAN_NOZZLE / nozzle wipe detected
    PURGING = 9,        ///< VORON_PURGE / LINE_PURGE detected
    COMPLETE = 10       ///< Transitioning to PRINTING state
};

/**
 * @brief Convert PrintJobState enum to display string
 *
 * Returns a human-readable string for UI display.
 *
 * @param state PrintJobState enum value
 * @return Display string (e.g., "Printing", "Paused")
 */
const char* print_job_state_to_string(PrintJobState state);

// ============================================================================
// MULTI-FAN TRACKING
// ============================================================================

/**
 * @brief Fan type classification for display and control
 */
enum class FanType {
    PART_COOLING,   ///< Main part cooling fan ("fan")
    HEATER_FAN,     ///< Hotend cooling fan (auto-controlled, not user-adjustable)
    CONTROLLER_FAN, ///< Electronics cooling (auto-controlled)
    GENERIC_FAN     ///< User-controllable generic fan (fan_generic)
};

/**
 * @brief Fan information for multi-fan display
 *
 * Holds display name, current speed, and controllability for each fan
 * discovered from Moonraker.
 */
struct FanInfo {
    std::string object_name;  ///< Full Moonraker object name (e.g., "heater_fan hotend_fan")
    std::string display_name; ///< Human-readable name (e.g., "Hotend Fan")
    FanType type = FanType::GENERIC_FAN;
    int speed_percent = 0;        ///< Current speed 0-100%
    bool is_controllable = false; ///< true for fan_generic, false for heater_fan/controller_fan
};

/**
 * @brief Printer state manager with LVGL 9 reactive subjects
 *
 * Implements hybrid architecture:
 * - LVGL subjects for UI-bound data (automatic reactive updates)
 * - JSON cache for complex data (file lists, capabilities, metadata)
 *
 * @note Thread Safety: Public setters that update LVGL subjects (set_printer_capabilities,
 *       set_klipper_version, etc.) use lv_async_call internally to defer updates to the
 *       main thread. This allows safe calls from WebSocket callbacks without risking
 *       "Invalidate area not allowed during rendering" assertions.
 */
class PrinterState {
  public:
    /**
     * @brief Construct printer state manager
     *
     * Initializes internal data structures. Call init_subjects() before
     * creating XML components.
     */
    PrinterState();

    /**
     * @brief Destroy printer state manager
     *
     * Cleans up LVGL subjects and releases resources.
     */
    ~PrinterState();

    /**
     * @brief Initialize all LVGL subjects
     *
     * MUST be called BEFORE creating XML components that bind to these subjects.
     * Can be called multiple times safely - subsequent calls are ignored.
     *
     * @param register_xml If true, registers subjects with LVGL XML system (default).
     *                     Set to false in tests to avoid XML observer creation.
     */
    void init_subjects(bool register_xml = true);

    /**
     * @brief Reset initialization state for testing
     *
     * FOR TESTING ONLY. Clears the initialization flag so init_subjects()
     * can be called again after lv_init() creates a new LVGL context.
     */
    void reset_for_testing();

    /**
     * @brief Update state from Moonraker notification
     *
     * Extracts values from notify_status_update messages and updates subjects.
     * Also maintains JSON cache for complex data.
     *
     * @param notification Parsed JSON notification from Moonraker
     */
    void update_from_notification(const json& notification);

    /**
     * @brief Update state from raw status data
     *
     * Updates subjects from a printer status object. Can be called directly
     * with subscription response data or extracted from notifications.
     * This is the core update logic used by both initial state and notifications.
     *
     * @param status Printer status object (e.g., from result.status or params[0])
     */
    void update_from_status(const json& status);

    /**
     * @brief Get raw JSON state for complex queries
     *
     * Thread-safe access to cached printer state.
     *
     * @return Reference to JSON state object
     */
    json& get_json_state();

    //
    // Subject accessors for XML binding
    //

    // Temperature subjects (centidegrees: value * 10 for 0.1°C resolution)
    // Example: 205.3°C is stored as 2053. Divide by 10 for display.
    lv_subject_t* get_extruder_temp_subject() {
        return &extruder_temp_;
    }
    lv_subject_t* get_extruder_target_subject() {
        return &extruder_target_;
    }
    lv_subject_t* get_bed_temp_subject() {
        return &bed_temp_;
    }
    lv_subject_t* get_bed_target_subject() {
        return &bed_target_;
    }

    // Print progress subjects
    lv_subject_t* get_print_progress_subject() {
        return &print_progress_;
    } // 0-100
    lv_subject_t* get_print_filename_subject() {
        return &print_filename_;
    }
    lv_subject_t* get_print_state_subject() {
        return &print_state_;
    } // "standby", "printing", "paused", "complete" (string for UI display)

    /**
     * @brief Get print thumbnail path subject for UI binding
     *
     * String subject holding the LVGL path to the current print's thumbnail.
     * Set by PrintStatusPanel when thumbnail loads, cleared when print ends.
     * HomePanel observes this to show the same thumbnail on the print card.
     *
     * @return Pointer to string subject
     */
    lv_subject_t* get_print_thumbnail_path_subject() {
        return &print_thumbnail_path_;
    }

    /**
     * @brief Set the current print's thumbnail path
     *
     * Called by PrintStatusPanel after successfully loading a thumbnail.
     * This allows other UI components (e.g., HomePanel) to display the
     * same thumbnail without duplicating the loading logic.
     *
     * @param path LVGL-compatible path (e.g., "A:/tmp/thumbnail_xxx.bin")
     */
    void set_print_thumbnail_path(const std::string& path);

    /**
     * @brief Get print job state enum subject
     *
     * Integer subject holding PrintJobState enum value for type-safe comparisons.
     * Use this for logic, use get_print_state_subject() for UI display binding.
     *
     * @return Pointer to integer subject (cast value to PrintJobState)
     */
    lv_subject_t* get_print_state_enum_subject() {
        return &print_state_enum_;
    }

    /**
     * @brief Get print active subject for UI binding
     *
     * Integer subject: 1 when PRINTING or PAUSED, 0 otherwise.
     * Derived from print_state_enum for simpler XML bindings (avoids OR logic).
     * Use for card visibility that should show during any active print.
     *
     * @return Pointer to integer subject (0 or 1)
     */
    lv_subject_t* get_print_active_subject() {
        return &print_active_;
    }

    /**
     * @brief Get subject for showing print progress card on home panel
     *
     * Combined subject: 1 when print_active==1 AND print_start_phase==0.
     * Simplifies XML bindings by avoiding conflicting multi-binding logic.
     *
     * @return Pointer to integer subject (0 or 1)
     */
    lv_subject_t* get_print_show_progress_subject() {
        return &print_show_progress_;
    }

    /**
     * @brief Get subject for display-ready print filename
     *
     * Clean filename without path or .helix_temp prefix, suitable for UI display.
     * Set by PrintStatusPanel when processing raw print_filename.
     *
     * @return Pointer to string subject
     */
    lv_subject_t* get_print_display_filename_subject() {
        return &print_display_filename_;
    }

    /**
     * @brief Set display-ready print filename for UI binding
     *
     * Called by PrintStatusPanel after cleaning up the raw filename.
     *
     * @param name Clean display name (e.g., "Body1" not ".helix_temp/modified_123_Body1.gcode")
     */
    void set_print_display_filename(const std::string& name);

    /**
     * @brief Get current print job state as enum
     *
     * Convenience method for direct enum access without subject lookup.
     *
     * @return Current PrintJobState
     */
    PrintJobState get_print_job_state() const;

    /**
     * @brief Check if a new print can be started
     *
     * Returns true if the printer is in a state that allows starting a new print.
     * A print can be started when the printer is idle (STANDBY), a previous print
     * finished (COMPLETE, CANCELLED), or the printer recovered from an error (ERROR).
     * Also checks that no print workflow is currently in progress (e.g., G-code
     * downloading/modifying/uploading).
     *
     * @return true if start_print() can be called safely
     */
    [[nodiscard]] bool can_start_new_print() const;

    /**
     * @brief Set the print-in-progress flag (UI workflow state)
     *
     * Call with true when starting the print preparation workflow
     * (downloading/modifying/uploading G-code), and false when complete.
     * This flag is checked by can_start_new_print() to prevent:
     * - Double-tap issues during long G-code modification workflows
     * - UI elements from indicating "ready to print" during preparation
     * - Race conditions from concurrent print requests
     *
     * Updates the print_in_progress_ subject so UI observers can react.
     */
    void set_print_in_progress(bool in_progress) {
        int new_value = in_progress ? 1 : 0;
        if (lv_subject_get_int(&print_in_progress_) != new_value) {
            lv_subject_set_int(&print_in_progress_, new_value);
        }
    }

    /**
     * @brief Check if a print workflow is currently in progress
     *
     * Returns true during print preparation (G-code download/modify/upload),
     * even though the printer's physical state may still be STANDBY.
     */
    [[nodiscard]] bool is_print_in_progress() const {
        return lv_subject_get_int(const_cast<lv_subject_t*>(&print_in_progress_)) != 0;
    }

    /**
     * @brief Reset UI state when starting a new print
     *
     * Clears the print_complete flag and resets progress to prepare for
     * a new print. Call this BEFORE navigating to print status panel.
     */
    void reset_for_new_print();

    /**
     * @brief Get the print-in-progress subject for observing workflow state
     *
     * Value is 1 when print preparation is in progress, 0 otherwise.
     */
    lv_subject_t* get_print_in_progress_subject() {
        return &print_in_progress_;
    }

    // Layer tracking subjects (from print_stats.info.current_layer/total_layer)
    lv_subject_t* get_print_layer_current_subject() {
        return &print_layer_current_;
    }
    lv_subject_t* get_print_layer_total_subject() {
        return &print_layer_total_;
    }

    /**
     * @brief Set total layer count from file metadata
     *
     * Called when print starts to initialize total layers from file metadata.
     * Moonraker notifications may update this later via SET_PRINT_STATS_INFO.
     */
    void set_print_layer_total(int total) {
        lv_subject_set_int(&print_layer_total_, total);
    }

    // Print time tracking subjects (in seconds)
    lv_subject_t* get_print_duration_subject() {
        return &print_duration_;
    }
    lv_subject_t* get_print_time_left_subject() {
        return &print_time_left_;
    }

    // ========================================================================
    // PRINT START PROGRESS (detected from G-code response during PRINT_START)
    // ========================================================================

    /**
     * @brief Get print start phase subject for UI binding
     *
     * Integer subject holding PrintStartPhase enum value.
     * Use with bind_flag_if_eq/not_eq in XML to show/hide progress overlay.
     */
    lv_subject_t* get_print_start_phase_subject() {
        return &print_start_phase_;
    }

    /**
     * @brief Get print start message subject for UI binding
     *
     * String subject with human-readable phase description (e.g., "Heating Nozzle...").
     * Use with bind_text in XML.
     */
    lv_subject_t* get_print_start_message_subject() {
        return &print_start_message_;
    }

    /**
     * @brief Get print start progress subject for UI binding
     *
     * Integer subject with 0-100% progress based on weighted phase completion.
     * Use with bind_value on lv_bar in XML.
     */
    lv_subject_t* get_print_start_progress_subject() {
        return &print_start_progress_;
    }

    /**
     * @brief Check if currently in print start phase
     *
     * Convenience method to check if we're showing PRINT_START progress.
     *
     * @return true if phase is not IDLE
     */
    bool is_in_print_start() const;

    /**
     * @brief Set print start phase and update message/progress
     *
     * Called by PrintStartCollector when phases are detected.
     * Updates all three subjects: phase, message, and progress.
     *
     * @param phase Current PrintStartPhase
     * @param message Human-readable message (e.g., "Heating Nozzle...")
     * @param progress Estimated progress 0-100%
     */
    void set_print_start_state(PrintStartPhase phase, const char* message, int progress);

    /**
     * @brief Reset print start to IDLE
     *
     * Called when print initialization completes or print is cancelled.
     */
    void reset_print_start_state();

    // Motion subjects
    lv_subject_t* get_position_x_subject() {
        return &position_x_;
    }
    lv_subject_t* get_position_y_subject() {
        return &position_y_;
    }
    lv_subject_t* get_position_z_subject() {
        return &position_z_;
    }
    lv_subject_t* get_homed_axes_subject() {
        return &homed_axes_;
    } // "xyz", "xy", etc.
    // Note: Derived subjects (xy_homed, z_homed, all_homed) are panel-local in ControlsPanel

    // Speed/Flow subjects (percentages, 0-100)
    lv_subject_t* get_speed_factor_subject() {
        return &speed_factor_;
    }
    lv_subject_t* get_flow_factor_subject() {
        return &flow_factor_;
    }
    lv_subject_t* get_fan_speed_subject() {
        return &fan_speed_;
    }

    // ========================================================================
    // MULTI-FAN API
    // ========================================================================

    /**
     * @brief Get all tracked fans
     * @return Const reference to fan info vector
     */
    const std::vector<FanInfo>& get_fans() const {
        return fans_;
    }

    /**
     * @brief Get fans version subject for UI change notification
     *
     * Incremented when fan list changes or speeds update.
     * UI should observe this to rebuild dynamic fan list.
     */
    lv_subject_t* get_fans_version_subject() {
        return &fans_version_;
    }

    /**
     * @brief Initialize fan list from discovered fan objects
     * @param fan_objects List of Moonraker fan object names
     */
    void init_fans(const std::vector<std::string>& fan_objects);

    /**
     * @brief Update speed for a specific fan
     * @param object_name Moonraker object name (e.g., "heater_fan hotend_fan")
     * @param speed Speed as 0.0-1.0 (Moonraker format)
     */
    void update_fan_speed(const std::string& object_name, double speed);

    /**
     * @brief Get G-code Z offset subject for tune panel
     *
     * Returns current Z-offset from gcode_move.homing_origin[2] in microns.
     * Divide by 1000.0 to get mm value (e.g., 200 = 0.200mm).
     * Used for live baby-stepping display during prints.
     */
    lv_subject_t* get_gcode_z_offset_subject() {
        return &gcode_z_offset_;
    }

    // ========================================================================
    // PENDING Z-OFFSET DELTA (for tracking adjustments made during print)
    // ========================================================================

    /**
     * @brief Get pending Z-offset delta subject
     *
     * Returns accumulated Z-offset adjustment made during print tuning (microns).
     * Use this to show "unsaved adjustment" notification in Controls panel.
     */
    lv_subject_t* get_pending_z_offset_delta_subject() {
        return &pending_z_offset_delta_;
    }

    /**
     * @brief Add to pending Z-offset delta (called when user adjusts Z during print)
     * @param delta_microns Adjustment in microns (positive = farther, negative = closer)
     */
    void add_pending_z_offset_delta(int delta_microns);

    /**
     * @brief Get current pending Z-offset delta in microns
     */
    int get_pending_z_offset_delta() const;

    /**
     * @brief Check if there's a pending Z-offset adjustment
     */
    bool has_pending_z_offset_adjustment() const;

    /**
     * @brief Clear pending Z-offset delta (after save or dismiss)
     */
    void clear_pending_z_offset_delta();

    // Printer connection state subjects (Moonraker WebSocket)
    lv_subject_t* get_printer_connection_state_subject() {
        return &printer_connection_state_;
    } // 0=disconnected, 1=connecting, 2=connected, 3=reconnecting, 4=failed
    lv_subject_t* get_printer_connection_message_subject() {
        return &printer_connection_message_;
    } // Status message

    // Network connectivity subject (WiFi/Ethernet)
    lv_subject_t* get_network_status_subject() {
        return &network_status_;
    } // 0=disconnected, 1=connecting, 2=connected (matches NetworkStatus enum)

    // Klipper firmware state subject
    lv_subject_t* get_klippy_state_subject() {
        return &klippy_state_;
    } // 0=ready, 1=startup, 2=shutdown, 3=error (matches KlippyState enum)

    // LED state subject (for home panel light control)
    lv_subject_t* get_led_state_subject() {
        return &led_state_;
    } // 0=off, 1=on (derived from LED color data)

    // LED RGBW channel subjects (0-255 integer range)
    lv_subject_t* get_led_r_subject() {
        return &led_r_;
    }
    lv_subject_t* get_led_g_subject() {
        return &led_g_;
    }
    lv_subject_t* get_led_b_subject() {
        return &led_b_;
    }
    lv_subject_t* get_led_w_subject() {
        return &led_w_;
    }
    lv_subject_t* get_led_brightness_subject() {
        return &led_brightness_;
    } // 0-100 (max of RGBW channels)

    /**
     * @brief Get excluded objects version subject
     *
     * This subject is incremented whenever the excluded objects list changes.
     * Observers should watch this subject and call get_excluded_objects() to
     * get the updated list when notified.
     *
     * @return Subject pointer (integer, incremented on each change)
     */
    lv_subject_t* get_excluded_objects_version_subject() {
        return &excluded_objects_version_;
    }

    /**
     * @brief Get the current set of excluded objects
     *
     * Returns object names that have been excluded from printing via Klipper's
     * EXCLUDE_OBJECT feature. Updated from Moonraker notify_status_update.
     *
     * @return Reference to the set of excluded object names
     */
    const std::unordered_set<std::string>& get_excluded_objects() const {
        return excluded_objects_;
    }

    /**
     * @brief Update excluded objects from Moonraker status update
     *
     * Called by status update handler when exclude_object.excluded_objects changes.
     * Increments the version subject to notify observers.
     *
     * @param objects Set of object names that are currently excluded
     */
    void set_excluded_objects(const std::unordered_set<std::string>& objects);

    /**
     * @brief Set which LED to track for state updates
     *
     * Call this after loading config to tell PrinterState which LED object
     * to monitor from Moonraker notifications. The LED name should match
     * the Klipper config (e.g., "neopixel chamber_light", "led status_led").
     *
     * @param led_name Full LED name including type prefix, or empty to disable
     */
    void set_tracked_led(const std::string& led_name);

    /**
     * @brief Get the currently tracked LED name
     *
     * @return LED name being tracked, or empty string if none
     */
    const std::string& get_tracked_led() const {
        return tracked_led_name_;
    }

    /**
     * @brief Check if an LED is configured for tracking
     *
     * @return true if a LED name has been set
     */
    bool has_tracked_led() const {
        return !tracked_led_name_.empty();
    }

    /**
     * @brief Set printer connection state (Moonraker WebSocket)
     *
     * Updates both printer_connection_state and printer_connection_message subjects.
     * Called by main.cpp WebSocket callbacks.
     *
     * @param state 0=disconnected, 1=connecting, 2=connected, 3=reconnecting, 4=failed
     * @param message Status message ("Connecting...", "Ready", "Disconnected", etc.)
     */
    void set_printer_connection_state(int state, const char* message);

    /**
     * @brief Internal: set connection state on main thread
     * @note Called via ui_async_call from set_printer_connection_state()
     */
    void set_printer_connection_state_internal(int state, const char* message);

    /**
     * @brief Check if printer has ever connected this session
     *
     * Returns true if we've successfully connected to Moonraker at least once.
     * Used to distinguish "never connected" (gray icon) from "disconnected after
     * being connected" (yellow warning icon).
     */
    bool was_ever_connected() const {
        return was_ever_connected_;
    }

    /**
     * @brief Set Klipper firmware state (thread-safe, async)
     *
     * Updates klippy_state subject via lv_async_call to ensure thread safety.
     * Called when Moonraker sends klippy state notifications from WebSocket
     * callbacks (notify_klippy_ready, notify_klippy_disconnected).
     *
     * @param state KlippyState enum value
     */
    void set_klippy_state(KlippyState state);

    /**
     * @brief Set Klipper firmware state (synchronous, main-thread only)
     *
     * Directly updates klippy_state subject without async deferral.
     * Only call this from the main LVGL thread. Use for testing or when
     * already on the main thread.
     *
     * @param state KlippyState enum value
     */
    void set_klippy_state_sync(KlippyState state);

    /**
     * @brief Set network connectivity status
     *
     * Updates network_status_ subject based on WiFi/Ethernet availability.
     * Called periodically from main.cpp to reflect actual network state.
     *
     * @param status 0=DISCONNECTED, 1=CONNECTING, 2=CONNECTED (NetworkStatus enum)
     */
    void set_network_status(int status);

    /**
     * @brief Update printer capability subjects from PrinterCapabilities
     *
     * Updates subjects that control visibility of pre-print option checkboxes.
     * Applies user-configured overrides from helixconfig.json before updating subjects.
     * Called by main.cpp after MoonrakerClient::discover_printer() completes.
     *
     * @param caps PrinterCapabilities populated from printer.objects.list
     */
    void set_printer_capabilities(const PrinterCapabilities& caps);

    /**
     * @brief Set Klipper software version from printer.info
     *
     * Updates klipper_version_ subject for Settings panel About section.
     * Called by main.cpp after MoonrakerClient::discover_printer() completes.
     *
     * @param version Version string (e.g., "v0.12.0-108-g2c7a9d58")
     */
    void set_klipper_version(const std::string& version);

    /**
     * @brief Set Moonraker software version from server.info
     *
     * Updates moonraker_version_ subject for Settings panel About section.
     * Called by main.cpp after MoonrakerClient::discover_printer() completes.
     *
     * @param version Version string (e.g., "v0.8.0-143-g2c7a9d58")
     */
    void set_moonraker_version(const std::string& version);

    /**
     * @brief Get Klipper version subject for XML binding
     */
    lv_subject_t* get_klipper_version_subject() {
        return &klipper_version_;
    }

    /**
     * @brief Get Moonraker version subject for XML binding
     */
    lv_subject_t* get_moonraker_version_subject() {
        return &moonraker_version_;
    }

    /**
     * @brief Get the capability overrides for external access
     *
     * Allows other components to check effective capability availability
     * with user overrides applied.
     *
     * @return Reference to the CapabilityOverrides instance
     */
    [[nodiscard]] const CapabilityOverrides& get_capability_overrides() const {
        return capability_overrides_;
    }

    /**
     * @brief Set Spoolman availability status
     *
     * Called after checking Moonraker's server.info components and verifying
     * Spoolman connection via get_spoolman_status(). Updates printer_has_spoolman_
     * subject for UI visibility gating.
     *
     * Thread-safe: Can be called from any thread, defers LVGL update to main thread.
     *
     * @param available True if Spoolman is configured and connected
     */
    void set_spoolman_available(bool available);

    /**
     * @brief Set HelixPrint plugin installation status
     *
     * Called after checking Moonraker for the helix_print plugin.
     * Updates helix_plugin_installed_ subject for UI visibility gating.
     *
     * Thread-safe: Can be called from any thread, defers LVGL update to main thread.
     *
     * @param installed True if HelixPrint plugin is installed
     */
    void set_helix_plugin_installed(bool installed);

    /**
     * @brief Check if HelixPrint plugin is available
     *
     * Convenience getter for checking plugin status. This is the preferred
     * way to query plugin availability (vs accessing the subject directly).
     *
     * @return True if the HelixPrint Moonraker plugin is installed
     */
    bool service_has_helix_plugin() const;

    /**
     * @brief Set phase tracking enabled/disabled status
     *
     * Called after querying the plugin's phase tracking status.
     * Updates phase_tracking_enabled_ subject for UI toggle state.
     *
     * Thread-safe: Can be called from any thread, defers LVGL update to main thread.
     *
     * @param enabled True if phase tracking is enabled
     */
    void set_phase_tracking_enabled(bool enabled);

    /**
     * @brief Check if phase tracking is enabled
     *
     * @return True if phase tracking is enabled, false otherwise
     */
    bool is_phase_tracking_enabled() const;

    /**
     * @brief Get helix_plugin_installed subject for observers
     *
     * Use this when you need to observe plugin status changes (e.g., for install prompts).
     *
     * @return Pointer to the helix_plugin_installed_ subject
     */
    lv_subject_t* get_helix_plugin_installed_subject() {
        return &helix_plugin_installed_;
    }

    /**
     * @brief Set printer kinematics type and update bed_moves subject
     *
     * Updates printer_bed_moves_ subject based on kinematics type.
     * CoreXY printers typically have bed moving on Z (Voron 2.4, RatRig).
     * Cartesian/Delta printers typically have gantry moving on Z (Ender 3, Prusa).
     *
     * @param kinematics Kinematics type string from toolhead config
     */
    void set_kinematics(const std::string& kinematics);

    /**
     * @brief Get bed_moves subject for XML binding
     *
     * Returns 1 if the printer's bed moves on Z axis (corexy, corexz),
     * 0 if the printer's gantry/head moves on Z (cartesian, delta).
     * Used for Z-offset UI to show appropriate directional icons.
     */
    lv_subject_t* get_printer_bed_moves_subject() {
        return &printer_bed_moves_;
    }

    /**
     * @brief Get manual probe active subject for Z-offset calibration
     *
     * Returns 1 when Klipper is in manual probe mode (PROBE_CALIBRATE,
     * Z_ENDSTOP_CALIBRATE), 0 otherwise. Used by ZOffsetCalibrationPanel
     * to transition from PROBING to ADJUSTING state.
     */
    lv_subject_t* get_manual_probe_active_subject() {
        return &manual_probe_active_;
    }

    /**
     * @brief Get manual probe Z position subject
     *
     * Returns current Z position during manual probe (in microns, multiply
     * by 0.001 to get mm). Updated in real-time by Klipper as TESTZ
     * commands are executed.
     */
    lv_subject_t* get_manual_probe_z_position_subject() {
        return &manual_probe_z_position_;
    }

    /**
     * @brief Check if printer has a probe configured
     *
     * Used by Z-offset calibration to determine whether to use
     * PROBE_CALIBRATE (has probe) or Z_ENDSTOP_CALIBRATE (no probe).
     *
     * @return true if [probe] or [bltouch] section exists in Klipper config
     */
    bool has_probe() {
        return lv_subject_get_int(&printer_has_probe_) != 0;
    }

  private:
    // Temperature subjects
    lv_subject_t extruder_temp_;
    lv_subject_t extruder_target_;
    lv_subject_t bed_temp_;
    lv_subject_t bed_target_;

    // Print progress subjects
    lv_subject_t print_progress_;          // Integer 0-100
    lv_subject_t print_filename_;          // String buffer
    lv_subject_t print_state_;             // String buffer (for UI display binding)
    lv_subject_t print_state_enum_;        // Integer: PrintJobState enum (for type-safe logic)
    bool complete_standby_logged_ = false; // Log spam guard for COMPLETE->STANDBY
    lv_subject_t print_active_;            // Integer: 1 when PRINTING/PAUSED, 0 otherwise
    lv_subject_t print_show_progress_;     // Integer: 1 when active AND not in start phase
    lv_subject_t print_display_filename_;  // String: clean filename for UI display
    lv_subject_t print_thumbnail_path_;    // String: LVGL path to current print thumbnail

    // Layer tracking subjects (from Moonraker print_stats.info)
    lv_subject_t print_layer_current_; // Current layer (0-based)
    lv_subject_t print_layer_total_;   // Total layers from file metadata

    // Print time tracking subjects (in seconds)
    lv_subject_t print_duration_;  // Elapsed print time in seconds
    lv_subject_t print_time_left_; // Estimated remaining time in seconds

    // Print start progress subjects (for PRINT_START macro tracking)
    lv_subject_t print_start_phase_;    // Integer: PrintStartPhase enum value
    lv_subject_t print_start_message_;  // String: human-readable phase message
    lv_subject_t print_start_progress_; // Integer: 0-100% estimated progress

    // Double-tap prevention: 1 while print workflow is executing, 0 otherwise
    // (G-code downloading/modifying/uploading - may take 20+ seconds for large files)
    lv_subject_t print_in_progress_;

    // Motion subjects
    lv_subject_t position_x_;
    lv_subject_t position_y_;
    lv_subject_t position_z_;
    lv_subject_t homed_axes_; // String buffer (derived subjects are panel-local)

    // Speed/Flow/Z-offset subjects (from gcode_move status)
    lv_subject_t speed_factor_;
    lv_subject_t flow_factor_;
    lv_subject_t gcode_z_offset_; // Integer: Z-offset * 1000 (microns) from homing_origin[2]
    lv_subject_t pending_z_offset_delta_; // Integer: accumulated adjustment during print (microns)
    lv_subject_t fan_speed_;

    // Multi-fan tracking
    std::vector<FanInfo> fans_;   ///< All tracked fans (from discovery)
    lv_subject_t fans_version_{}; ///< Incremented on fan list/speed changes

    // Printer connection state subjects (Moonraker WebSocket)
    lv_subject_t printer_connection_state_;   // Integer: uses PrinterStatus enum values
    lv_subject_t printer_connection_message_; // String buffer

    // Network connectivity subject (WiFi/Ethernet)
    lv_subject_t network_status_; // Integer: uses NetworkStatus enum values

    // Klipper firmware state subject
    lv_subject_t klippy_state_; // Integer: uses KlippyState enum values

    // LED state subject
    lv_subject_t led_state_; // Integer: 0=off, 1=on

    // LED RGBW channel subjects (for color picker / brightness slider)
    lv_subject_t led_r_;          // LED red channel 0-255
    lv_subject_t led_g_;          // LED green channel 0-255
    lv_subject_t led_b_;          // LED blue channel 0-255
    lv_subject_t led_w_;          // LED white channel 0-255
    lv_subject_t led_brightness_; // LED brightness 0-100 (max of RGBW channels)

    // Exclude object subjects
    lv_subject_t excluded_objects_version_;            // Integer: incremented on change
    std::unordered_set<std::string> excluded_objects_; // Set of excluded object names

    // Printer capability subjects (for pre-print options visibility)
    lv_subject_t printer_has_qgl_;           // Integer: 0=no, 1=yes
    lv_subject_t printer_has_z_tilt_;        // Integer: 0=no, 1=yes
    lv_subject_t printer_has_bed_mesh_;      // Integer: 0=no, 1=yes
    lv_subject_t printer_has_nozzle_clean_;  // Integer: 0=no, 1=yes
    lv_subject_t printer_has_probe_;         // Integer: 0=no, 1=yes (for Z-offset calibration)
    lv_subject_t printer_has_heater_bed_;    // Integer: 0=no, 1=yes (for PID bed tuning)
    lv_subject_t printer_has_led_;           // Integer: 0=no, 1=yes (for LED light control)
    lv_subject_t printer_has_accelerometer_; // Integer: 0=no, 1=yes (for input shaping)
    lv_subject_t printer_has_spoolman_;      // Integer: 0=no, 1=yes (for filament tracking)
    lv_subject_t printer_has_speaker_;       // Integer: 0=no, 1=yes (for M300 audio feedback)
    lv_subject_t printer_has_timelapse_;  // Integer: 0=no, 1=yes (for Moonraker-Timelapse plugin)
    lv_subject_t helix_plugin_installed_; // Tri-state: -1=unknown, 0=not installed, 1=installed
    lv_subject_t phase_tracking_enabled_; // Tri-state: -1=unknown, 0=disabled, 1=enabled
    lv_subject_t printer_has_firmware_retraction_; // Integer: 0=no, 1=yes (for G10/G11 retraction)
    lv_subject_t printer_bed_moves_; // Integer: 0=no (gantry moves), 1=yes (bed moves on Z)

    // Composite subjects for G-code modification option visibility
    // These combine helix_plugin_installed with individual printer capabilities.
    // An option is shown only when BOTH: plugin installed AND printer has capability.
    lv_subject_t can_show_bed_mesh_;     // helix_plugin_installed && printer_has_bed_mesh
    lv_subject_t can_show_qgl_;          // helix_plugin_installed && printer_has_qgl
    lv_subject_t can_show_z_tilt_;       // helix_plugin_installed && printer_has_z_tilt
    lv_subject_t can_show_nozzle_clean_; // helix_plugin_installed && printer_has_nozzle_clean

    // Firmware retraction settings (from firmware_retraction Klipper module)
    // Lengths stored as centimillimeters (x100) to preserve 0.01mm precision with integers
    lv_subject_t retract_length_;         // centimm (e.g., 80 = 0.8mm)
    lv_subject_t retract_speed_;          // mm/s (integer, e.g., 35)
    lv_subject_t unretract_extra_length_; // centimm (e.g., 0 = 0.0mm)
    lv_subject_t unretract_speed_;        // mm/s (integer, e.g., 35)

    // Manual probe subjects (for Z-offset calibration)
    lv_subject_t manual_probe_active_; // Integer: 0=inactive, 1=active (PROBE_CALIBRATE running)
    lv_subject_t manual_probe_z_position_; // Integer: Z position * 1000 (for 0.001mm resolution)

    // Version subjects (for About section)
    lv_subject_t klipper_version_;
    lv_subject_t moonraker_version_;

    // Tracked LED name (e.g., "neopixel chamber_light")
    std::string tracked_led_name_;

    // String buffers for subject storage
    char print_filename_buf_[256];
    char print_display_filename_buf_[128]; // Clean filename for UI display
    char print_thumbnail_path_buf_[512];   // LVGL path to current print thumbnail
    char print_state_buf_[32];
    char homed_axes_buf_[8];
    char printer_connection_message_buf_[128];
    char klipper_version_buf_[64];
    char moonraker_version_buf_[64];
    char print_start_message_buf_[64]; // "Heating Nozzle...", "Homing...", etc.

    // JSON cache for complex data
    json json_state_;
    std::mutex state_mutex_;

    // Initialization guard to prevent multiple subject initializations
    bool subjects_initialized_ = false;

    // Track if we've ever successfully connected (for UI display)
    bool was_ever_connected_ = false;

    // Capability override layer (user config overrides for auto-detected capabilities)
    CapabilityOverrides capability_overrides_;

    // ============================================================================
    // Thread-safe internal methods (called via lv_async_call from main thread)
    // ============================================================================
    // These methods contain the actual LVGL subject updates and must only be called
    // from the main thread. The public methods (set_printer_capabilities, etc.) use
    // lv_async_call to defer to these internal methods, ensuring thread safety.

    friend void async_capabilities_callback(void* user_data);
    friend void async_klipper_version_callback(void* user_data);
    friend void async_moonraker_version_callback(void* user_data);
    friend void async_klippy_state_callback(void* user_data);

    void set_printer_capabilities_internal(const PrinterCapabilities& caps);
    void set_klipper_version_internal(const std::string& version);
    void set_moonraker_version_internal(const std::string& version);
    void set_klippy_state_internal(KlippyState state);

    /**
     * @brief Update composite visibility subjects for G-code modification options
     *
     * Recalculates can_show_* subjects based on current plugin and capability state.
     * Called whenever helix_plugin_installed or printer_has_* subjects change.
     * Must be called from main thread (typically via async callbacks).
     */
    void update_gcode_modification_visibility();

    /**
     * @brief Update print_show_progress_ combined subject
     *
     * Sets print_show_progress_ to 1 only when print_active==1 AND print_start_phase==IDLE.
     * Called whenever either component subject changes.
     * Must be called from main thread.
     */
    void update_print_show_progress();
};
