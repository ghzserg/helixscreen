// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "advanced_panel_types.h"
#include "moonraker_client.h"
#include "moonraker_domain_service.h"
#include "moonraker_error.h"
#include "moonraker_types.h"
#include "print_history_data.h"
#include "printer_state.h"

#include <atomic>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
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
    using SuccessCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const MoonrakerError&)>;
    using FileListCallback = std::function<void(const std::vector<FileInfo>&)>;
    using FileMetadataCallback = std::function<void(const FileMetadata&)>;
    using BoolCallback = std::function<void(bool)>;
    using StringCallback = std::function<void(const std::string&)>;
    using HistoryListCallback =
        std::function<void(const std::vector<PrintHistoryJob>&, uint64_t total_count)>;
    using HistoryTotalsCallback = std::function<void(const PrintHistoryTotals&)>;
    using TimelapseSettingsCallback = std::function<void(const TimelapseSettings&)>;
    using SpoolCallback = std::function<void(const std::optional<SpoolInfo>&)>;
    using SpoolListCallback = std::function<void(const std::vector<SpoolInfo>&)>;

    /**
     * @brief Constructor
     *
     * @param client MoonrakerClient instance (must remain valid during API lifetime)
     * @param state PrinterState instance (must remain valid during API lifetime)
     */
    MoonrakerAPI(MoonrakerClient& client, PrinterState& state);
    virtual ~MoonrakerAPI();

    // ========================================================================
    // File Management Operations
    // ========================================================================

    /**
     * @brief List files in a directory
     *
     * @param root Root directory ("gcodes", "config", "timelapse")
     * @param path Subdirectory path (empty for root)
     * @param recursive Include subdirectories
     * @param on_success Callback with file list
     * @param on_error Error callback
     */
    void list_files(const std::string& root, const std::string& path, bool recursive,
                    FileListCallback on_success, ErrorCallback on_error);

    /**
     * @brief Get detailed metadata for a file
     *
     * @param filename Full path to file (relative to gcodes root)
     * @param on_success Callback with metadata
     * @param on_error Error callback
     */
    void get_file_metadata(const std::string& filename, FileMetadataCallback on_success,
                           ErrorCallback on_error);

    /**
     * @brief Delete a file
     *
     * @param filename Full path to file (relative to gcodes root)
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void delete_file(const std::string& filename, SuccessCallback on_success,
                     ErrorCallback on_error);

    /**
     * @brief Move or rename a file
     *
     * @param source Source path (e.g., "gcodes/old_dir/file.gcode")
     * @param dest Destination path (e.g., "gcodes/new_dir/file.gcode")
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void move_file(const std::string& source, const std::string& dest, SuccessCallback on_success,
                   ErrorCallback on_error);

    /**
     * @brief Copy a file
     *
     * @param source Source path (e.g., "gcodes/original.gcode")
     * @param dest Destination path (e.g., "gcodes/copy.gcode")
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void copy_file(const std::string& source, const std::string& dest, SuccessCallback on_success,
                   ErrorCallback on_error);

    /**
     * @brief Create a directory
     *
     * @param path Directory path (e.g., "gcodes/my_folder")
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void create_directory(const std::string& path, SuccessCallback on_success,
                          ErrorCallback on_error);

    /**
     * @brief Delete a directory
     *
     * @param path Directory path (e.g., "gcodes/old_folder")
     * @param force Force deletion even if not empty
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void delete_directory(const std::string& path, bool force, SuccessCallback on_success,
                          ErrorCallback on_error);

    // ========================================================================
    // Job Control Operations
    // ========================================================================

    /**
     * @brief Start printing a file
     *
     * @param filename Full path to G-code file
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void start_print(const std::string& filename, SuccessCallback on_success,
                     ErrorCallback on_error);

    // ModifiedPrintResult is defined in moonraker_types.h
    using ModifiedPrintCallback = std::function<void(const ModifiedPrintResult&)>;

    /**
     * @brief Start printing modified G-code via helix_print plugin
     *
     * Sends modified G-code content to the helix_print Moonraker plugin,
     * which handles:
     * - Saving the modified content to a temp file
     * - Creating a symlink with the original filename
     * - Starting the print via the symlink
     * - Patching history to record the original filename
     *
     * Falls back to legacy upload+start flow if plugin is not available.
     * Use has_helix_plugin() to check availability.
     *
     * @param original_filename Path to the original G-code file
     * @param modified_content Complete modified G-code content
     * @param modifications List of modification identifiers (e.g., "bed_leveling_disabled")
     * @param on_success Callback with print result
     * @param on_error Error callback
     */
    virtual void start_modified_print(const std::string& original_filename,
                                      const std::string& modified_content,
                                      const std::vector<std::string>& modifications,
                                      ModifiedPrintCallback on_success, ErrorCallback on_error);

    /**
     * @brief Check if helix_print plugin is available
     *
     * Queries /server/helix/status to detect plugin availability.
     * Call this before using start_modified_print() to decide on flow.
     *
     * @param on_result Callback with availability (true if plugin detected)
     * @param on_error Error callback (also means plugin not available)
     */
    virtual void check_helix_plugin(BoolCallback on_result, ErrorCallback on_error);

    /**
     * @brief Check if helix_print plugin is available (cached)
     *
     * Returns cached result from previous check_helix_plugin() call.
     * Returns false if check hasn't been performed yet.
     *
     * @return true if plugin is available and detected
     */
    bool has_helix_plugin() const {
        return helix_plugin_available_;
    }

    /**
     * @brief Pause the current print
     *
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void pause_print(SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Resume a paused print
     *
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void resume_print(SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Cancel the current print
     *
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void cancel_print(SuccessCallback on_success, ErrorCallback on_error);

    // ========================================================================
    // Motion Control Operations
    // ========================================================================

    /**
     * @brief Home one or more axes
     *
     * @param axes Axes to home (e.g., "XY", "Z", "XYZ", empty for all)
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void home_axes(const std::string& axes, SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Move an axis by a relative amount
     *
     * @param axis Axis name ('X', 'Y', 'Z', 'E')
     * @param distance Distance to move in mm
     * @param feedrate Movement speed in mm/min (0 for default)
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void move_axis(char axis, double distance, double feedrate, SuccessCallback on_success,
                   ErrorCallback on_error);

    /**
     * @brief Set absolute position for an axis
     *
     * @param axis Axis name ('X', 'Y', 'Z')
     * @param position Absolute position in mm
     * @param feedrate Movement speed in mm/min (0 for default)
     * @param on_success Success callback
     * @param on_error Error callback
     */
    void move_to_position(char axis, double position, double feedrate, SuccessCallback on_success,
                          ErrorCallback on_error);

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
    void execute_gcode(const std::string& gcode, SuccessCallback on_success,
                       ErrorCallback on_error);

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

    // ========================================================================
    // Generic REST Endpoint Operations (for Moonraker extensions)
    // ========================================================================

    // RestResponse is defined in moonraker_types.h
    using RestCallback = std::function<void(const RestResponse&)>;

    /**
     * @brief Call a Moonraker extension REST endpoint with GET
     *
     * Makes an HTTP GET request to a Moonraker extension endpoint.
     * Used for plugins like ValgACE that expose REST APIs at /server/xxx/.
     *
     * Example: call_rest_get("/server/ace/status", callback, error_callback)
     *
     * @param endpoint REST endpoint path (e.g., "/server/ace/status")
     * @param on_complete Callback with response (success or failure)
     */
    virtual void call_rest_get(const std::string& endpoint, RestCallback on_complete);

    /**
     * @brief Call a Moonraker extension REST endpoint with POST
     *
     * Makes an HTTP POST request to a Moonraker extension endpoint.
     * Used for plugins like ValgACE that accept commands via REST.
     *
     * Example: call_rest_post("/server/ace/command", {"action": "load"}, callback)
     *
     * @param endpoint REST endpoint path (e.g., "/server/ace/command")
     * @param params JSON parameters to POST
     * @param on_complete Callback with response (success or failure)
     */
    virtual void call_rest_post(const std::string& endpoint, const json& params,
                                RestCallback on_complete);

    // ========================================================================
    // HTTP File Transfer Operations
    // ========================================================================

    /**
     * @brief Download a file's content from the printer via HTTP
     *
     * Uses GET request to /server/files/{root}/{path} endpoint.
     * The file content is returned as a string in the callback.
     *
     * Virtual to allow mocking in tests (MoonrakerAPIMock reads local files).
     *
     * @param root Root directory ("gcodes", "config", etc.)
     * @param path File path relative to root
     * @param on_success Callback with file content as string
     * @param on_error Error callback
     */
    virtual void download_file(const std::string& root, const std::string& path,
                               StringCallback on_success, ErrorCallback on_error);

    /**
     * @brief Download a file directly to disk (streaming, low memory)
     *
     * Unlike download_file() which loads entire content into memory,
     * this streams chunks directly to disk as they arrive. Essential
     * for large G-code files on memory-constrained devices like AD5M.
     *
     * Uses libhv's streaming download which writes chunks to disk
     * as they are received, avoiding memory spikes.
     *
     * Virtual to allow mocking in tests.
     *
     * @param root Root directory ("gcodes", "config", etc.)
     * @param path File path relative to root
     * @param dest_path Local filesystem path to write to
     * @param on_success Callback with dest_path on success
     * @param on_error Error callback
     */
    virtual void download_file_to_path(const std::string& root, const std::string& path,
                                       const std::string& dest_path, StringCallback on_success,
                                       ErrorCallback on_error);

    /**
     * @brief Download a thumbnail image and cache it locally
     *
     * Downloads thumbnail from Moonraker's HTTP server and saves to a local cache file.
     * The callback receives the local file path (suitable for LVGL image loading).
     *
     * Virtual to allow mocking in tests.
     *
     * @param thumbnail_path Relative path from metadata (e.g., ".thumbnails/file.png")
     * @param cache_path Local filesystem path to save the thumbnail
     * @param on_success Callback with local cache path
     * @param on_error Error callback
     */
    virtual void download_thumbnail(const std::string& thumbnail_path,
                                    const std::string& cache_path, StringCallback on_success,
                                    ErrorCallback on_error);

    /**
     * @brief Upload file content to the printer via HTTP multipart form
     *
     * Uses POST request to /server/files/upload endpoint with multipart form data.
     * Suitable for G-code files, config files, and macro files.
     *
     * Virtual to allow mocking in tests (MoonrakerAPIMock logs but doesn't write).
     *
     * @param root Root directory ("gcodes", "config", etc.)
     * @param path Destination path relative to root
     * @param content File content to upload
     * @param on_success Success callback
     * @param on_error Error callback
     */
    virtual void upload_file(const std::string& root, const std::string& path,
                             const std::string& content, SuccessCallback on_success,
                             ErrorCallback on_error);

    /**
     * @brief Upload file content with custom filename
     *
     * Like upload_file() but allows specifying a different filename for the
     * multipart form than the path. Useful when uploading to a subdirectory.
     *
     * Virtual to allow mocking in tests (MoonrakerAPIMock logs but doesn't write).
     *
     * @param root Root directory ("gcodes", "config", etc.)
     * @param path Destination path relative to root (e.g., ".helix_temp/foo.gcode")
     * @param filename Filename for form (e.g., ".helix_temp/foo.gcode")
     * @param content File content to upload
     * @param on_success Success callback
     * @param on_error Error callback
     */
    virtual void upload_file_with_name(const std::string& root, const std::string& path,
                                       const std::string& filename, const std::string& content,
                                       SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Set the HTTP base URL for file transfers
     *
     * Must be called before using download_file/upload_file.
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
    // Print History Operations
    // ========================================================================

    /**
     * @brief Get paginated list of print history jobs
     *
     * Calls server.history.list Moonraker endpoint.
     *
     * @param limit Maximum number of jobs to return (default 50)
     * @param start Offset for pagination (0-based)
     * @param since Unix timestamp - only include jobs after this time (0 = no filter)
     * @param before Unix timestamp - only include jobs before this time (0 = no filter)
     * @param on_success Callback with parsed job list and total count
     * @param on_error Error callback
     */
    void get_history_list(int limit, int start, double since, double before,
                          HistoryListCallback on_success, ErrorCallback on_error);

    /**
     * @brief Get aggregated history totals/statistics
     *
     * Calls server.history.totals Moonraker endpoint.
     *
     * @param on_success Callback with totals struct
     * @param on_error Error callback
     */
    void get_history_totals(HistoryTotalsCallback on_success, ErrorCallback on_error);

    /**
     * @brief Delete a job from history by its unique ID
     *
     * Calls server.history.delete_job Moonraker endpoint.
     *
     * @param job_id Unique job identifier from PrintHistoryJob::job_id
     * @param on_success Success callback (job deleted)
     * @param on_error Error callback
     */
    void delete_history_job(const std::string& job_id, SuccessCallback on_success,
                            ErrorCallback on_error);

    // ========================================================================
    // Timelapse Operations (Moonraker-Timelapse Plugin)
    // ========================================================================

    /**
     * @brief Get current timelapse settings
     *
     * Queries the Moonraker-Timelapse plugin for its current configuration.
     * Only available if has_timelapse capability is detected.
     *
     * @param on_success Callback with current settings
     * @param on_error Error callback
     */
    virtual void get_timelapse_settings(TimelapseSettingsCallback on_success,
                                        ErrorCallback on_error);

    /**
     * @brief Update timelapse settings
     *
     * Configures the Moonraker-Timelapse plugin with new settings.
     * Changes take effect for the next print.
     *
     * @param settings New settings to apply
     * @param on_success Success callback
     * @param on_error Error callback
     */
    virtual void set_timelapse_settings(const TimelapseSettings& settings,
                                        SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Enable or disable timelapse for current/next print
     *
     * Convenience method to toggle just the enabled state without
     * changing other settings.
     *
     * @param enabled true to enable, false to disable
     * @param on_success Success callback
     * @param on_error Error callback
     */
    virtual void set_timelapse_enabled(bool enabled, SuccessCallback on_success,
                                       ErrorCallback on_error);

    // ========================================================================
    // Domain Service Operations (Bed Mesh, Object Exclusion)
    // ========================================================================

    /**
     * @brief Get currently active bed mesh profile
     *
     * Returns pointer to the active mesh profile loaded from Moonraker's
     * bed_mesh object. The probed_matrix field contains the 2D Z-height
     * array ready for rendering.
     *
     * @return Pointer to active mesh profile, or nullptr if none loaded
     */
    const BedMeshProfile* get_active_bed_mesh() const;

    /**
     * @brief Get list of available mesh profile names
     *
     * Returns profile names from bed_mesh.profiles (e.g., "default",
     * "adaptive", "calibration"). Empty vector if no profiles available
     * or discovery hasn't completed.
     *
     * @return Vector of profile names
     */
    std::vector<std::string> get_bed_mesh_profiles() const;

    /**
     * @brief Check if bed mesh data is available
     *
     * @return true if a mesh profile with valid probed_matrix is loaded
     */
    bool has_bed_mesh() const;

    /**
     * @brief Get set of currently excluded object names (async)
     *
     * Queries Klipper's exclude_object module for the list of objects
     * that have been excluded from the current print.
     *
     * @param on_success Callback with set of excluded object names
     * @param on_error Error callback
     */
    void get_excluded_objects(std::function<void(const std::set<std::string>&)> on_success,
                              ErrorCallback on_error);

    /**
     * @brief Get list of available objects in current print (async)
     *
     * Queries Klipper's exclude_object module for the list of objects
     * defined in the current G-code file (from EXCLUDE_OBJECT_DEFINE).
     *
     * @param on_success Callback with vector of available object names
     * @param on_error Error callback
     */
    void get_available_objects(std::function<void(const std::vector<std::string>&)> on_success,
                               ErrorCallback on_error);

    // ========================================================================
    // Internal Access (for CommandSequencer integration)
    // ========================================================================

    /**
     * @brief Get reference to underlying MoonrakerClient
     *
     * Required by CommandSequencer which needs direct client access for
     * G-code execution and state observation.
     *
     * @return Reference to MoonrakerClient
     */
    MoonrakerClient& get_client() {
        return client_;
    }

    // ========================================================================
    // Advanced Panel Operations - Bed Leveling
    // ========================================================================

    /**
     * @brief Start automatic bed mesh calibration
     *
     * Executes BED_MESH_CALIBRATE command with optional profile name.
     * The operation runs asynchronously; completion is signaled via callback.
     *
     * @param profile_name Profile name to save (empty for "default")
     * @param on_success Called when calibration completes
     * @param on_error Called on failure
     */
    virtual void start_bed_mesh_calibrate(const std::string& profile_name,
                                          SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Calculate screw adjustments for manual bed leveling
     *
     * Executes SCREWS_TILT_CALCULATE command. Requires [screws_tilt_adjust]
     * section in printer.cfg.
     *
     * @param on_success Called with screw adjustment results
     * @param on_error Called on failure
     */
    virtual void calculate_screws_tilt(ScrewTiltCallback on_success, ErrorCallback on_error);

    /**
     * @brief Run Quad Gantry Level
     *
     * Executes QUAD_GANTRY_LEVEL command for Voron-style printers.
     *
     * @param on_success Called when leveling completes
     * @param on_error Called on failure
     */
    virtual void run_qgl(SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Run Z-Tilt Adjust
     *
     * Executes Z_TILT_ADJUST command for multi-motor Z printers.
     *
     * @param on_success Called when adjustment completes
     * @param on_error Called on failure
     */
    virtual void run_z_tilt_adjust(SuccessCallback on_success, ErrorCallback on_error);

    // ========================================================================
    // Advanced Panel Operations - Input Shaping
    // ========================================================================

    /**
     * @brief Start resonance test for input shaper calibration
     *
     * Executes TEST_RESONANCES command for the specified axis.
     * Requires accelerometer configuration in printer.cfg.
     *
     * @param axis Axis to test ('X' or 'Y')
     * @param on_progress Called with progress percentage (0-100)
     * @param on_complete Called with test results
     * @param on_error Called on failure
     */
    virtual void start_resonance_test(char axis, AdvancedProgressCallback on_progress,
                                      InputShaperCallback on_complete, ErrorCallback on_error);

    /**
     * @brief Start Klippain Shake&Tune calibration
     *
     * Executes AXES_SHAPER_CALIBRATION macro from Klippain.
     * Provides enhanced calibration with graphs.
     *
     * @param axis Axis to calibrate ("X", "Y", or "all")
     * @param on_success Called when calibration completes
     * @param on_error Called on failure
     */
    virtual void start_klippain_shaper_calibration(const std::string& axis,
                                                   SuccessCallback on_success,
                                                   ErrorCallback on_error);

    /**
     * @brief Apply input shaper settings
     *
     * Sets the shaper type and frequency via SET_INPUT_SHAPER command.
     *
     * @param axis Axis to configure ('X' or 'Y')
     * @param shaper_type Shaper algorithm (e.g., "mzv", "ei")
     * @param freq_hz Shaper frequency in Hz
     * @param on_success Called when settings are applied
     * @param on_error Called on failure
     */
    virtual void set_input_shaper(char axis, const std::string& shaper_type, double freq_hz,
                                  SuccessCallback on_success, ErrorCallback on_error);

    // ========================================================================
    // Advanced Panel Operations - Spoolman
    // ========================================================================

    /**
     * @brief Get Spoolman connection status
     *
     * @param on_success Called with (connected, active_spool_id)
     * @param on_error Called on failure
     */
    virtual void
    get_spoolman_status(std::function<void(bool connected, int active_spool_id)> on_success,
                        ErrorCallback on_error);

    /**
     * @brief Get list of spools from Spoolman
     *
     * @param on_success Called with spool list
     * @param on_error Called on failure
     */
    virtual void get_spoolman_spools(SpoolListCallback on_success, ErrorCallback on_error);

    /**
     * @brief Get a single spool's details by ID
     *
     * Fetches full spool information from Spoolman for a specific spool ID.
     * Used when assigning a Spoolman spool to an AMS slot - the backend
     * fetches the spool details to enrich the slot display.
     *
     * @param spool_id Spoolman spool ID
     * @param on_success Called with spool info (empty optional if not found)
     * @param on_error Called on failure (network error, etc.)
     */
    virtual void get_spoolman_spool(int spool_id, SpoolCallback on_success, ErrorCallback on_error);

    /**
     * @brief Set the active spool for filament tracking
     *
     * @param spool_id Spoolman spool ID (0 to clear)
     * @param on_success Called when spool is set
     * @param on_error Called on failure
     */
    virtual void set_active_spool(int spool_id, SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Get usage history for a spool
     *
     * @param spool_id Spoolman spool ID
     * @param on_success Called with usage records
     * @param on_error Called on failure
     */
    virtual void
    get_spool_usage_history(int spool_id,
                            std::function<void(const std::vector<FilamentUsageRecord>&)> on_success,
                            ErrorCallback on_error);

    /**
     * @brief Update a spool's remaining weight in Spoolman
     *
     * Uses Moonraker's Spoolman proxy to PATCH /v1/spool/{id}.
     * This updates the spool's remaining_weight field.
     *
     * @param spool_id Spoolman spool ID
     * @param remaining_weight_g New remaining weight in grams
     * @param on_success Called when update succeeds
     * @param on_error Called on failure
     */
    virtual void update_spoolman_spool_weight(int spool_id, double remaining_weight_g,
                                              SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Update a filament's color in Spoolman
     *
     * Uses Moonraker's Spoolman proxy to PATCH /v1/filament/{id}.
     * WARNING: This affects ALL spools using this filament definition.
     *
     * @param filament_id Spoolman filament ID (not spool ID!)
     * @param color_hex New color as hex string (e.g., "#FF0000")
     * @param on_success Called when update succeeds
     * @param on_error Called on failure
     */
    virtual void update_spoolman_filament_color(int filament_id, const std::string& color_hex,
                                                SuccessCallback on_success, ErrorCallback on_error);

    // ========================================================================
    // Advanced Panel Operations - Machine Limits
    // ========================================================================

    /**
     * @brief Get current machine limits
     *
     * Queries toolhead object for velocity and acceleration limits.
     *
     * @param on_success Called with current limits
     * @param on_error Called on failure
     */
    virtual void get_machine_limits(MachineLimitsCallback on_success, ErrorCallback on_error);

    /**
     * @brief Set machine limits (temporary, not saved to config)
     *
     * Uses SET_VELOCITY_LIMIT command. Changes are lost on Klipper restart.
     *
     * @param limits New limits to apply
     * @param on_success Called when limits are applied
     * @param on_error Called on failure
     */
    virtual void set_machine_limits(const MachineLimits& limits, SuccessCallback on_success,
                                    ErrorCallback on_error);

    /**
     * @brief Save current configuration to printer.cfg
     *
     * Executes SAVE_CONFIG command. This will restart Klipper.
     *
     * @param on_success Called when save is initiated
     * @param on_error Called on failure
     */
    virtual void save_config(SuccessCallback on_success, ErrorCallback on_error);

    // ========================================================================
    // Advanced Panel Operations - Macros
    // ========================================================================

    /**
     * @brief Execute a G-code macro with optional parameters
     *
     * @param name Macro name (e.g., "CLEAN_NOZZLE")
     * @param params Parameter map (e.g., {"TEMP": "210"})
     * @param on_success Called when macro execution starts
     * @param on_error Called on failure
     */
    virtual void execute_macro(const std::string& name,
                               const std::map<std::string, std::string>& params,
                               SuccessCallback on_success, ErrorCallback on_error);

    /**
     * @brief Get list of user-visible macros
     *
     * Returns macros filtered by category, excluding system macros
     * (those starting with _) unless explicitly requested.
     *
     * @param include_system Include _* system macros
     * @return Vector of macro information
     */
    std::vector<MacroInfo> get_user_macros(bool include_system = false) const;

  private:
    std::string http_base_url_; ///< HTTP base URL for file transfers
    MoonrakerClient& client_;

    SafetyLimits safety_limits_;
    bool limits_explicitly_set_ = false;

    // HelixPrint plugin detection
    std::atomic<bool> helix_plugin_available_{false};
    std::atomic<bool> helix_plugin_checked_{false};

    // Track pending HTTP request threads to ensure clean shutdown
    // IMPORTANT: Prevents use-after-free when threads outlive the API object
    mutable std::mutex http_threads_mutex_;
    std::list<std::thread> http_threads_;
    std::atomic<bool> shutting_down_{false};

    /**
     * @brief Launch an HTTP request thread with automatic lifecycle management
     *
     * Spawns a thread for async HTTP operations and tracks it for cleanup.
     * Thread is automatically removed from tracking when it completes.
     *
     * @param func The function to execute in the thread
     */
    void launch_http_thread(std::function<void()> func);

    /**
     * @brief Parse file list response from server.files.list
     */
    std::vector<FileInfo> parse_file_list(const json& response);

    /**
     * @brief Parse metadata response from server.files.metadata
     */
    FileMetadata parse_file_metadata(const json& response);

    /**
     * @brief Generate G-code for homing axes
     */
    std::string generate_home_gcode(const std::string& axes);

    /**
     * @brief Generate G-code for relative movement
     */
    std::string generate_move_gcode(char axis, double distance, double feedrate);

    /**
     * @brief Generate G-code for absolute movement
     */
    std::string generate_absolute_move_gcode(char axis, double position, double feedrate);
};