// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file moonraker_api.h
 * @brief MoonrakerAPI - Domain Operations Layer
 *
 * ## Responsibilities
 *
 * - High-level printer operations (print, move, heat, home, etc.)
 * - Input validation and safety checks (temperature limits, movement bounds)
 * - HTTP file upload/download (G-code files, thumbnails, config)
 * - Response parsing and error handling
 * - Domain-specific callbacks (progress, completion, errors)
 * - Bed mesh operations (delegating to MoonrakerClient for storage)
 * - Print history and timelapse management
 * - Spoolman filament tracking integration
 *
 * ## NOT Responsible For
 *
 * - WebSocket connection management (done by MoonrakerClient)
 * - JSON-RPC protocol details (done by MoonrakerClient)
 * - Hardware discovery (done by MoonrakerClient)
 * - Raw subscription handling (done by MoonrakerClient)
 *
 * ## Architecture Notes
 *
 * MoonrakerAPI is the domain layer that provides a clean, high-level interface
 * for printer operations. It uses MoonrakerClient for transport (WebSocket
 * communication) and adds:
 *
 * - Safety validation (temperature limits, movement bounds)
 * - HTTP file transfers (multipart uploads, range downloads)
 * - Response transformation (JSON -> domain types)
 * - Error handling with domain-specific error types
 *
 * Application code should prefer MoonrakerAPI for all printer interactions.
 * Direct MoonrakerClient access should only be needed for low-level operations
 * like custom G-code execution or subscription management.
 *
 * @see MoonrakerClient for transport layer details
 * @see SafetyLimits for input validation configuration
 */

#pragma once

#include "moonraker_advanced_api.h"
#include "moonraker_client.h"
#include "moonraker_error.h"
#include "moonraker_file_api.h"
#include "moonraker_file_transfer_api.h"
#include "moonraker_history_api.h"
#include "moonraker_job_api.h"
#include "moonraker_motion_api.h"
#include "moonraker_rest_api.h"
#include "moonraker_spoolman_api.h"
#include "moonraker_timelapse_api.h"
#include "moonraker_types.h"
#include "print_history_data.h"
#include "printer_discovery.h"
#include "printer_state.h"

#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

/**
 * @brief High-level Moonraker API facade
 *
 * Provides simplified, domain-specific operations on top of MoonrakerClient.
 * All methods are asynchronous with success/error callbacks.
 */
class MoonrakerAPI {
  public:
    // ========== G-code execute_gcode timeout constants ==========
    // Default is 30s (in MoonrakerClient). These are for long-running commands.
    // Advanced calibration timeouts are in MoonrakerAdvancedAPI.
    static constexpr uint32_t HOMING_TIMEOUT_MS = 300000;        // 5 min — G28 on large printers
    static constexpr uint32_t AMS_OPERATION_TIMEOUT_MS = 300000; // 5 min — MMU/AFC/tool change ops
    static constexpr uint32_t EXTRUSION_TIMEOUT_MS =
        120000; // 2 min — filament purge/load at slow feedrate

    using SuccessCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const MoonrakerError&)>;
    using BoolCallback = std::function<void(bool)>;
    using StringCallback = std::function<void(const std::string&)>;
    using JsonCallback = std::function<void(const json&)>;

    /**
     * @brief Constructor
     *
     * @param client MoonrakerClient instance (must remain valid during API lifetime)
     * @param state PrinterState instance (must remain valid during API lifetime)
     */
    MoonrakerAPI(helix::MoonrakerClient& client, helix::PrinterState& state);
    virtual ~MoonrakerAPI();

    // ========================================================================
    // Temperature Control Operations
    // ========================================================================

    /**
     * @brief Set target temperature for a heater
     *
     * @param heater Heater name ("extruder", "heater_bed", etc.)
     * @param temperature Target temperature in Celsius
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void set_temperature(const std::string& heater, double temperature, SuccessCallback on_success,
                         ErrorCallback on_error);

    /**
     * @brief Set fan speed
     *
     * @param fan Fan name ("fan", "fan_generic cooling_fan", etc.)
     * @param speed Speed percentage (0-100)
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void set_fan_speed(const std::string& fan, double speed, SuccessCallback on_success,
                       ErrorCallback on_error);

    /**
     * @brief Set LED color/brightness
     *
     * Controls LED output by name. For simple on/off control, use brightness 1.0 or 0.0.
     * Supports neopixel, dotstar, led, and pca9632 LED types.
     *
     * @param led LED name (e.g., "neopixel chamber_light", "led status_led")
     * @param red Red component (0.0-1.0)
     * @param green Green component (0.0-1.0)
     * @param blue Blue component (0.0-1.0)
     * @param white Optional white component for RGBW LEDs (0.0-1.0, default 0.0)
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void set_led(const std::string& led, double red, double green, double blue, double white,
                 SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Turn LED on (full white)
     *
     * Convenience method to turn LED on at full brightness.
     *
     * @param led LED name
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void set_led_on(const std::string& led, SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Turn LED off
     *
     * Convenience method to turn LED off.
     *
     * @param led LED name
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void set_led_off(const std::string& led, SuccessCallback on_success, ErrorCallback on_error);

    // ========================================================================
    // Power Device Control Operations
    // ========================================================================

    // PowerDevice is defined in moonraker_types.h
    using PowerDevicesCallback = std::function<void(const std::vector<PowerDevice>&)>;

    /**
     * @brief Get list of all configured power devices
     *
     * Queries Moonraker's /machine/device_power/devices endpoint
     *
     * @param on_success Callback with list of power devices
     * @param on_error Error callback
     */
    virtual void get_power_devices(PowerDevicesCallback on_success, ErrorCallback on_error);

    /**
     * @brief Set power device state
     *
     * @param device Device name
     * @param action Action to perform ("on", "off", "toggle")
     * @param on_success Success callback
     * @param on_error Error callback
     */
    virtual void set_device_power(const std::string& device, const std::string& action,
                                  SuccessCallback on_success, ErrorCallback on_error);

    // ========================================================================
    // System Control Operations
    // ========================================================================

    /**
     * @brief Execute custom G-code command
     *
     * @param gcode G-code command string
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void execute_gcode(const std::string& gcode, SuccessCallback on_success, ErrorCallback on_error,
                       uint32_t timeout_ms = 0, bool silent = false);

    /**
     * @brief Check if a string is safe to use as a G-code parameter
     *
     * Allows alphanumeric, underscore, and space. Rejects newlines, semicolons,
     * and other characters that could enable G-code injection.
     *
     * @param str String to validate
     * @return true if safe for G-code parameter use, false otherwise
     */
    static bool is_safe_gcode_param(const std::string& str);

    // ========================================================================
    // Object Exclusion Operations
    // ========================================================================

    /**
     * @brief Exclude an object from the current print
     *
     * Sends EXCLUDE_OBJECT command to Klipper to skip printing a specific object.
     * Object must be defined in the G-code file metadata (EXCLUDE_OBJECT_DEFINE).
     * Requires [exclude_object] section in printer.cfg.
     *
     * @param object_name Object name from G-code metadata (e.g., "Part_1")
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void exclude_object(const std::string& object_name, SuccessCallback on_success,
                        ErrorCallback on_error);

    /**
     * @brief Emergency stop
     *
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void emergency_stop(SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Restart Klipper firmware
     *
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void restart_firmware(SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Restart Klipper host process
     *
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void restart_klipper(SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Restart the Moonraker service
     *
     * POST /server/restart - Restarts the Moonraker service itself.
     * This will cause a temporary WebSocket disconnect.
     *
     * @param on_success Success callback (called before disconnect)
     * @param on_error Error callback
     */
    void restart_moonraker(SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Shut down the host machine
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void machine_shutdown(SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Reboot the host machine
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void machine_reboot(SuccessCallback on_success, ErrorCallback on_error);

    // ========================================================================
    // Query Operations
    // ========================================================================

    /**
     * @brief Query if printer is ready for commands
     *
     * @param on_result Callback with ready state
     * @param on_error Error callback
     */
    void is_printer_ready(BoolCallback on_result, ErrorCallback on_error);

    /**
     * @brief Get current print state
     *
     * @param on_result Callback with state ("standby", "printing", "paused", "complete", "error")
     * @param on_error Error callback
     */
    void get_print_state(StringCallback on_result, ErrorCallback on_error);

    // ========================================================================
    // Safety Limits Configuration
    // ========================================================================

    /**
     * @brief Set safety limits explicitly (overrides auto-detection)
     *
     * When called, prevents update_safety_limits_from_printer() from modifying limits.
     * Use this to enforce project-specific constraints regardless of printer configuration.
     *
     * @param limits Safety limits to apply
     */
    void set_safety_limits(const SafetyLimits& limits) {
        safety_limits_ = limits;
        limits_explicitly_set_ = true;
    }

    /**
     * @brief Get current safety limits
     *
     * @return Current safety limits (explicit, auto-detected, or defaults)
     */
    const SafetyLimits& get_safety_limits() const {
        return safety_limits_;
    }

    /**
     * @brief Update safety limits from printer configuration via Moonraker API
     *
     * Queries printer.objects.query for configfile.settings and extracts:
     * - max_velocity → max_feedrate_mm_min
     * - stepper_* position_min/max → absolute position limits
     * - extruder/heater_* min_temp/max_temp → temperature limits
     *
     * Only updates limits if set_safety_limits() has NOT been called (explicit config takes
     * priority). Fallback to defaults if Moonraker query fails or values unavailable.
     *
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void update_safety_limits_from_printer(SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Query the printer's configfile object
     *
     * Fetches the raw configuration from Klipper's configfile object.
     * This includes all sections and their raw string values, which is useful
     * for parsing macro definitions (gcode_macro sections contain the raw gcode).
     *
     * The response is the "config" portion of configfile, not "settings".
     * - "config": Raw strings as written in config files
     * - "settings": Parsed/typed values (not useful for macro text)
     *
     * @param on_success Callback with parsed JSON config object
     * @param on_error Error callback
     */
    void query_configfile(JsonCallback on_success, ErrorCallback on_error);

    // ========================================================================
    // HTTP Base URL Configuration
    // ========================================================================

    /**
     * @brief Set the HTTP base URL for file transfers and REST operations
     *
     * Must be called before using transfer/REST methods.
     * Typically derived from WebSocket URL: ws://host:port -> http://host:port
     *
     * @param base_url HTTP base URL (e.g., "http://192.168.1.100:7125")
     */
    void set_http_base_url(const std::string& base_url) {
        http_base_url_ = base_url;
    }

    /**
     * @brief Get the current HTTP base URL
     */
    const std::string& get_http_base_url() const {
        return http_base_url_;
    }

    /**
     * @brief Ensure HTTP base URL is set, auto-deriving from WebSocket if needed
     *
     * If http_base_url_ is empty, attempts to derive it from the client's
     * WebSocket URL: ws://host:port/websocket -> http://host:port
     *
     * @return true if HTTP base URL is available, false if derivation failed
     */
    bool ensure_http_base_url();

    // ========================================================================
    // Connection and Subscription Proxies
    // ========================================================================

    /// Check if the client is currently connected to Moonraker
    virtual bool is_connected() const;

    /// Get current connection state
    virtual helix::ConnectionState get_connection_state() const;

    /// Get the WebSocket URL used for the current connection
    virtual std::string get_websocket_url() const;

    /// Subscribe to status update notifications (mirrors MoonrakerClient::register_notify_update)
    virtual helix::SubscriptionId subscribe_notifications(std::function<void(json)> callback);

    /// Unsubscribe from status update notifications
    virtual bool unsubscribe_notifications(helix::SubscriptionId id);

    /// Get client lifetime guard (for SubscriptionGuard safety)
    std::weak_ptr<bool> client_lifetime_weak() const;

    /// Register a persistent callback for a specific notification method
    virtual void register_method_callback(const std::string& method, const std::string& name,
                                          std::function<void(json)> callback);

    /// Unregister a method-specific callback
    virtual bool unregister_method_callback(const std::string& method, const std::string& name);

    /// Temporarily suppress disconnect modal notifications
    virtual void suppress_disconnect_modal(uint32_t duration_ms);

    /// Retrieve recent G-code commands/responses from Moonraker's store
    virtual void
    get_gcode_store(int count, std::function<void(const std::vector<GcodeStoreEntry>&)> on_success,
                    std::function<void(const MoonrakerError&)> on_error);

    // ========================================================================
    // Helix Plugin Operations
    // ========================================================================

    /// Get phase tracking plugin status
    virtual void get_phase_tracking_status(std::function<void(bool enabled)> on_success,
                                           ErrorCallback on_error = nullptr);

    /// Enable or disable phase tracking plugin
    virtual void set_phase_tracking_enabled(bool enabled,
                                            std::function<void(bool success)> on_success,
                                            ErrorCallback on_error = nullptr);

    // ========================================================================
    // Database Operations
    // ========================================================================

    /// Get a value from Moonraker's database
    virtual void database_get_item(const std::string& namespace_name, const std::string& key,
                                   std::function<void(const json&)> on_success,
                                   ErrorCallback on_error = nullptr);

    /// Store a value in Moonraker's database
    virtual void database_post_item(const std::string& namespace_name, const std::string& key,
                                    const json& value, std::function<void()> on_success = nullptr,
                                    ErrorCallback on_error = nullptr);

    // ========================================================================
    // Internal Access
    // ========================================================================

    /**
     * @brief Get reference to underlying MoonrakerClient
     *
     * Provides direct access to the WebSocket client for advanced operations
     * requiring direct G-code execution or state observation.
     *
     * @return Reference to MoonrakerClient
     */
    helix::MoonrakerClient& get_client() {
        return client_;
    }

    /**
     * @brief Get const reference to discovered hardware
     *
     * Provides read-only access to the printer hardware discovery data,
     * including heaters, fans, sensors, LEDs, and capability flags.
     * This data is populated during printer discovery via MoonrakerClient.
     *
     * @return Const reference to PrinterDiscovery
     */
    [[nodiscard]] const helix::PrinterDiscovery& hardware() const {
        return hardware_;
    }

    /**
     * @brief Get non-const reference to hardware for internal updates
     *
     * Used internally by discovery callbacks to populate hardware data.
     * Application code should use the const accessor instead.
     *
     * @return Reference to PrinterDiscovery
     */
    helix::PrinterDiscovery& hardware() {
        return hardware_;
    }

    /**
     * @brief Get build volume version subject for change notifications
     *
     * This integer subject is incremented whenever build_volume is updated
     * (e.g., when stepper config loads). Observers can watch this to refresh
     * UI that depends on build_volume dimensions.
     *
     * @return Pointer to the version subject
     */
    lv_subject_t* get_build_volume_version_subject() {
        return &build_volume_version_;
    }

    /**
     * @brief Notify that build_volume has changed
     *
     * Call this after updating hardware().set_build_volume() to notify
     * observers that they should refresh any cached build volume data.
     * Increments the build_volume_version_ subject.
     */
    void notify_build_volume_changed();

    // ========================================================================
    // Sub-API Accessors (Delegated)
    // ========================================================================

    /**
     * @brief Get Advanced API for calibration, bed mesh, macros, etc.
     *
     * All advanced panel methods (bed mesh, input shaper, PID calibration,
     * machine limits, macros, save_config) are available through this accessor.
     *
     * @return Reference to MoonrakerAdvancedAPI
     */
    MoonrakerAdvancedAPI& advanced() {
        return *advanced_api_;
    }

    /**
     * @brief Get File Transfer API for HTTP download/upload operations
     *
     * All file transfer methods (download_file, download_thumbnail, upload_file, etc.)
     * are available through this accessor.
     *
     * @return Reference to MoonrakerFileTransferAPI
     */
    MoonrakerFileTransferAPI& transfers() {
        return *file_transfer_api_;
    }

    /**
     * @brief Get History API for print history operations
     *
     * All history methods (get_history_list, get_history_totals, delete_history_job)
     * are available through this accessor.
     *
     * @return Reference to MoonrakerHistoryAPI
     */
    MoonrakerHistoryAPI& history() {
        return *history_api_;
    }

    /**
     * @brief Get Job API for print job control operations
     *
     * All job methods (start_print, pause_print, resume_print, cancel_print,
     * start_modified_print, check_helix_plugin) are available through this accessor.
     *
     * @return Reference to MoonrakerJobAPI
     */
    MoonrakerJobAPI& job() {
        return *job_api_;
    }

    /**
     * @brief Get Timelapse API for timelapse and webcam operations
     *
     * All timelapse methods (get/set settings, render, frames) and
     * webcam queries are available through this accessor.
     *
     * @return Reference to MoonrakerTimelapseAPI
     */
    MoonrakerTimelapseAPI& timelapse() {
        return *timelapse_api_;
    }

    /**
     * @brief Get Motion API for axis control operations
     *
     * All motion methods (home_axes, move_axis, move_to_position)
     * are available through this accessor.
     *
     * @return Reference to MoonrakerMotionAPI
     */
    MoonrakerMotionAPI& motion() {
        return *motion_api_;
    }

    /**
     * @brief Get REST API for generic REST endpoint and WLED operations
     *
     * All REST methods (call_rest_get, call_rest_post, wled_get_strips,
     * wled_set_strip, wled_get_status, get_server_config) are available
     * through this accessor.
     *
     * @return Reference to MoonrakerRestAPI
     */
    MoonrakerRestAPI& rest() {
        return *rest_api_;
    }

    /**
     * @brief Get Spoolman API for filament tracking operations
     *
     * All Spoolman methods (get_spoolman_spools, set_active_spool, etc.)
     * are available through this accessor.
     *
     * @return Reference to MoonrakerSpoolmanAPI
     */
    MoonrakerSpoolmanAPI& spoolman() {
        return *spoolman_api_;
    }

    /**
     * @brief Get File API for file management operations
     *
     * All file management methods (list_files, get_directory, get_file_metadata,
     * metascan_file, delete_file, move_file, copy_file, create_directory,
     * delete_directory) are available through this accessor.
     *
     * @return Reference to MoonrakerFileAPI
     */
    MoonrakerFileAPI& files() {
        return *file_api_;
    }

  private:
    // Data members MUST be declared BEFORE sub-API unique_ptrs.
    // C++ destroys members in reverse declaration order, so data members
    // (declared first) are destroyed LAST, after sub-APIs that reference them.
    std::string http_base_url_; ///< HTTP base URL for file transfers
    helix::MoonrakerClient& client_;

    /// Discovered printer hardware (heaters, fans, sensors, LEDs, capabilities)
    helix::PrinterDiscovery hardware_;

    /// Subject for notifying when build_volume changes (version counter)
    lv_subject_t build_volume_version_;
    std::atomic<int> build_volume_version_counter_{0};

    SafetyLimits safety_limits_;
    bool limits_explicitly_set_ = false;

    /// Launch an HTTP thread with lifecycle tracking (joined on destruction)
    void launch_http_thread(std::function<void()> func);

    mutable std::mutex http_threads_mutex_;
    std::list<std::thread> http_threads_;
    std::atomic<bool> shutting_down_{false};

  protected:
    // Sub-API unique_ptrs declared AFTER data members they reference
    // (http_base_url_, safety_limits_) so they are destroyed FIRST.
    std::unique_ptr<MoonrakerAdvancedAPI> advanced_api_;          ///< Advanced panel operations API
    std::unique_ptr<MoonrakerFileTransferAPI> file_transfer_api_; ///< HTTP file transfer API
    std::unique_ptr<MoonrakerFileAPI> file_api_;                  ///< File management API
    std::unique_ptr<MoonrakerHistoryAPI> history_api_;            ///< Print history API
    std::unique_ptr<MoonrakerJobAPI> job_api_;                    ///< Job control API
    std::unique_ptr<MoonrakerMotionAPI> motion_api_;              ///< Motion control API
    std::unique_ptr<MoonrakerRestAPI> rest_api_;                  ///< REST endpoint & WLED API
    std::unique_ptr<MoonrakerSpoolmanAPI> spoolman_api_;   ///< Spoolman filament tracking API
    std::unique_ptr<MoonrakerTimelapseAPI> timelapse_api_; ///< Timelapse & webcam API
};