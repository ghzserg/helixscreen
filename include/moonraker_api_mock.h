// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "moonraker_api.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Forward declaration for shared state
class MockPrinterState;

/**
 * @brief Simulated bed screw state for mock bed leveling
 *
 * Tracks the "physical" state of bed screws to simulate a realistic
 * iterative bed leveling session. Each probe shows current deviations,
 * and after each probe the user is assumed to make adjustments that
 * bring the bed closer to level.
 */
struct MockBedScrew {
    std::string name;            ///< Screw identifier (e.g., "front_left")
    float x_pos = 0.0f;          ///< Bed X coordinate (mm)
    float y_pos = 0.0f;          ///< Bed Y coordinate (mm)
    float current_offset = 0.0f; ///< Current Z deviation from level (mm)
    bool is_reference = false;   ///< True for the reference screw (always level)
};

/**
 * @brief Mock bed leveling state machine
 *
 * Simulates a realistic bed leveling session:
 * 1. Initial state has screws out of level (0.05-0.20mm deviations)
 * 2. After each probe, user "adjusts" screws (70-90% correction)
 * 3. Typically reaches level state after 2-4 iterations
 */
class MockScrewsTiltState {
  public:
    MockScrewsTiltState();

    /**
     * @brief Reset bed to initial out-of-level state
     */
    void reset();

    /**
     * @brief Simulate probing the bed and return results
     * @return Vector of screw results with current deviations
     */
    std::vector<ScrewTiltResult> probe();

    /**
     * @brief Simulate user making adjustments based on probe results
     *
     * After seeing probe results, user turns screws. This applies
     * a 70-90% correction with some randomness to simulate imperfect adjustment.
     */
    void simulate_user_adjustments();

    /**
     * @brief Check if all screws are within tolerance
     * @param tolerance_mm Maximum acceptable deviation (default 0.02mm)
     * @return true if bed is considered level
     */
    [[nodiscard]] bool is_level(float tolerance_mm = 0.02f) const;

    /**
     * @brief Get the number of probe iterations performed
     */
    [[nodiscard]] int get_probe_count() const {
        return probe_count_;
    }

  private:
    std::vector<MockBedScrew> screws_;
    int probe_count_ = 0;

    /**
     * @brief Convert Z offset to turns:minutes adjustment string
     * @param offset_mm Z deviation in mm (positive = too high, need CW)
     * @return Adjustment string like "CW 01:15" or "CCW 00:30"
     */
    static std::string offset_to_adjustment(float offset_mm);
};

/**
 * @brief Mock MoonrakerAPI for testing without real printer connection
 *
 * Overrides HTTP file transfer methods to use local test files instead
 * of making actual HTTP requests to a Moonraker server.
 *
 * Path Resolution:
 * The mock tries multiple paths to find test files, supporting both:
 * - Running from project root: assets/test_gcodes/
 * - Running from build/bin/: ../../assets/test_gcodes/
 *
 * Usage:
 *   MoonrakerClientMock mock_client;
 *   PrinterState state;
 *   MoonrakerAPIMock mock_api(mock_client, state);
 *   // mock_api.download_file() now reads from assets/test_gcodes/
 */
class MoonrakerAPIMock : public MoonrakerAPI {
  public:
    /**
     * @brief Construct mock API
     *
     * @param client MoonrakerClient instance (typically MoonrakerClientMock)
     * @param state PrinterState instance
     */
    MoonrakerAPIMock(MoonrakerClient& client, PrinterState& state);

    ~MoonrakerAPIMock() override = default;

    // ========================================================================
    // Overridden Connection/Subscription/Database Proxies (no-ops for mock)
    // ========================================================================

    SubscriptionId subscribe_notifications(std::function<void(json)> callback) override;
    bool unsubscribe_notifications(SubscriptionId id) override;
    void register_method_callback(const std::string& method, const std::string& name,
                                  std::function<void(json)> callback) override;
    bool unregister_method_callback(const std::string& method, const std::string& name) override;
    void suppress_disconnect_modal(uint32_t duration_ms) override;
    void get_gcode_store(int count,
                         std::function<void(const std::vector<GcodeStoreEntry>&)> on_success,
                         std::function<void(const MoonrakerError&)> on_error) override;
    void database_get_item(const std::string& namespace_name, const std::string& key,
                           std::function<void(const json&)> on_success,
                           ErrorCallback on_error = nullptr) override;
    void database_post_item(const std::string& namespace_name, const std::string& key,
                            const json& value, std::function<void()> on_success = nullptr,
                            ErrorCallback on_error = nullptr) override;

    // ========================================================================
    // Overridden Helix Plugin Methods (return mock data)
    // ========================================================================

    void get_phase_tracking_status(std::function<void(bool enabled)> on_success,
                                   ErrorCallback on_error = nullptr) override;
    void set_phase_tracking_enabled(bool enabled, std::function<void(bool success)> on_success,
                                    ErrorCallback on_error = nullptr) override;

    // ========================================================================
    // Overridden HTTP File Transfer Methods (use local files instead of HTTP)
    // ========================================================================

    /**
     * @brief Download file from local test directory
     *
     * Instead of making HTTP request, reads from assets/test_gcodes/{filename}.
     * Uses fallback path search to work regardless of current working directory.
     *
     * @param root Root directory (ignored in mock - always uses test_gcodes)
     * @param path File path (directory components stripped, only filename used)
     * @param on_success Callback with file content
     * @param on_error Error callback (FILE_NOT_FOUND if file doesn't exist)
     */
    void download_file(const std::string& root, const std::string& path, StringCallback on_success,
                       ErrorCallback on_error) override;

    /**
     * @brief Mock partial file download (first N bytes)
     *
     * Reads first max_bytes of a local test file from assets/test_gcodes/
     * Useful for testing G-code preamble scanning without loading entire files.
     *
     * @param root Root directory (ignored in mock)
     * @param path File path - only the filename is used
     * @param max_bytes Maximum bytes to return
     * @param on_success Success callback with partial content
     * @param on_error Error callback (FILE_NOT_FOUND if file doesn't exist)
     */
    void download_file_partial(const std::string& root, const std::string& path, size_t max_bytes,
                               StringCallback on_success, ErrorCallback on_error) override;

    /**
     * @brief Mock streaming file download to disk
     *
     * Instead of making HTTP request, copies from assets/test_gcodes/{filename}
     * to the specified destination path.
     *
     * @param root Root directory (ignored in mock - always uses test_gcodes)
     * @param path File path (directory components stripped, only filename used)
     * @param dest_path Local filesystem path to write to
     * @param on_success Callback with dest_path on success
     * @param on_error Error callback (FILE_NOT_FOUND if source doesn't exist)
     * @param on_progress Optional progress callback (ignored in mock)
     */
    void download_file_to_path(const std::string& root, const std::string& path,
                               const std::string& dest_path, StringCallback on_success,
                               ErrorCallback on_error,
                               ProgressCallback on_progress = nullptr) override;

    /**
     * @brief Mock file upload (logs but doesn't write)
     *
     * Logs the upload request but doesn't actually write files.
     * Always calls success callback.
     *
     * @param root Root directory
     * @param path Destination path
     * @param content File content
     * @param on_success Success callback
     * @param on_error Error callback (never called - mock always succeeds)
     */
    void upload_file(const std::string& root, const std::string& path, const std::string& content,
                     SuccessCallback on_success, ErrorCallback on_error) override;

    /**
     * @brief Mock file upload with custom filename (logs but doesn't write)
     *
     * Logs the upload request but doesn't actually write files.
     * Always calls success callback.
     */
    void upload_file_with_name(const std::string& root, const std::string& path,
                               const std::string& filename, const std::string& content,
                               SuccessCallback on_success, ErrorCallback on_error) override;

    /**
     * @brief Mock thumbnail download (reads from local test assets)
     *
     * Instead of downloading from Moonraker, looks for thumbnails in
     * assets/test_thumbnails/ or assets/test_gcodes/. For mock mode,
     * simply returns a placeholder path since we don't have real thumbnails.
     *
     * @param thumbnail_path Relative path from metadata
     * @param cache_path Destination cache path (ignored - uses placeholder)
     * @param on_success Callback with placeholder image path
     * @param on_error Error callback (never called - mock always returns placeholder)
     */
    void download_thumbnail(const std::string& thumbnail_path, const std::string& cache_path,
                            StringCallback on_success, ErrorCallback on_error) override;

    // ========================================================================
    // Overridden Timelapse Methods (mock render/frame operations)
    // ========================================================================

    void render_timelapse(SuccessCallback on_success, ErrorCallback on_error) override;
    void save_timelapse_frames(SuccessCallback on_success, ErrorCallback on_error) override;
    void get_last_frame_info(std::function<void(const LastFrameInfo&)> on_success,
                             ErrorCallback on_error) override;

    // ========================================================================
    // Overridden Power Device Methods (return mock data)
    // ========================================================================

    /**
     * @brief Get mock power devices for testing
     *
     * Returns a predefined list of power devices to test the Power Panel UI
     * without needing a real Moonraker connection.
     *
     * @param on_success Callback with list of mock power devices
     * @param on_error Error callback (never called - mock always succeeds)
     */
    void get_power_devices(PowerDevicesCallback on_success, ErrorCallback on_error) override;

    /**
     * @brief Mock set power device (logs but doesn't control hardware)
     *
     * Logs the command and updates internal mock state for testing.
     * Always calls success callback.
     *
     * @param device Device name
     * @param action Action ("on", "off", "toggle")
     * @param on_success Success callback (always called)
     * @param on_error Error callback (never called)
     */
    void set_device_power(const std::string& device, const std::string& action,
                          SuccessCallback on_success, ErrorCallback on_error) override;

    // WLED control (mock: logs and calls on_success)
    void wled_get_strips(RestCallback on_success, ErrorCallback on_error) override;
    void wled_set_strip(const std::string& strip, const std::string& action, int brightness,
                        int preset, SuccessCallback on_success, ErrorCallback on_error) override;
    void wled_get_status(RestCallback on_success, ErrorCallback on_error) override;
    void get_server_config(RestCallback on_success, ErrorCallback on_error) override;

    // ========================================================================
    // Shared State Methods
    // ========================================================================

    /**
     * @brief Set shared mock state for coordination with MoonrakerClientMock
     *
     * When set, queries for excluded objects and available objects will
     * return data from the shared state, which is also updated by
     * MoonrakerClientMock when processing G-code commands.
     *
     * @param state Shared state pointer (can be nullptr to disable)
     */
    void set_mock_state(std::shared_ptr<MockPrinterState> state);

    /**
     * @brief Get shared mock state (may be nullptr)
     *
     * @return Shared state pointer, or nullptr if not set
     */
    std::shared_ptr<MockPrinterState> get_mock_state() const {
        return mock_state_;
    }

    /**
     * @brief Get excluded objects from shared state
     *
     * Returns objects excluded via EXCLUDE_OBJECT commands processed by
     * MoonrakerClientMock. If no shared state is set, returns empty set.
     *
     * @return Set of excluded object names
     */
    std::set<std::string> get_excluded_objects_from_mock() const;

    /**
     * @brief Get available objects from shared state
     *
     * Returns objects defined via EXCLUDE_OBJECT_DEFINE commands.
     * If no shared state is set, returns empty vector.
     *
     * @return Vector of available object names
     */
    std::vector<std::string> get_available_objects_from_mock() const;

    // ========================================================================
    // Overridden Calibration Methods (simulate realistic behavior)
    // ========================================================================

    /**
     * @brief Mock bed mesh calibration with progress simulation
     *
     * Logs the call and immediately calls on_complete for now.
     * Phase 6 will add proper progress simulation.
     *
     * @param on_progress Progress callback (current, total)
     * @param on_complete Completion callback
     * @param on_error Error callback (rarely called - mock usually succeeds)
     */
    void start_bed_mesh_calibrate(BedMeshProgressCallback on_progress, SuccessCallback on_complete,
                                  ErrorCallback on_error) override;

    /**
     * @brief Simulate SCREWS_TILT_CALCULATE with iterative bed leveling
     *
     * First call returns screws out of level. Subsequent calls show
     * progressively better alignment as user "adjusts" screws.
     * Typically reaches level state after 2-4 probes.
     *
     * @param on_success Callback with screw adjustment results
     * @param on_error Error callback (rarely called - mock usually succeeds)
     */
    void calculate_screws_tilt(ScrewTiltCallback on_success, ErrorCallback on_error) override;

    /**
     * @brief Reset the mock bed to initial out-of-level state
     *
     * Call this to restart the bed leveling simulation from scratch.
     */
    void reset_mock_bed_state();

    /**
     * @brief Get the mock bed state for inspection/testing
     */
    MockScrewsTiltState& get_mock_bed_state() {
        return mock_bed_state_;
    }

    // ========================================================================
    // Overridden Spoolman Methods (return mock filament inventory)
    // ========================================================================

    /**
     * @brief Get mock Spoolman status
     *
     * Returns mock connection status and active spool ID for testing
     * Spoolman integration without a real Spoolman server.
     *
     * @param on_success Callback with (connected, active_spool_id)
     * @param on_error Error callback (never called - mock always succeeds)
     */
    void get_spoolman_status(std::function<void(bool, int)> on_success,
                             ErrorCallback on_error) override;

    /**
     * @brief Get mock spool list
     *
     * Returns a predefined list of spools with realistic data for testing
     * the Spoolman UI without a real Spoolman server.
     *
     * @param on_success Callback with list of mock SpoolInfo
     * @param on_error Error callback (never called - mock always succeeds)
     */
    void get_spoolman_spools(SpoolListCallback on_success, ErrorCallback on_error) override;

    /**
     * @brief Get a single spool by ID from mock inventory
     *
     * Looks up the spool in mock_spools_ and returns it if found.
     * Used by AmsBackend to enrich slot info when a Spoolman spool is assigned.
     *
     * @param spool_id Spoolman spool ID to lookup
     * @param on_success Callback with SpoolInfo (empty optional if not found)
     * @param on_error Error callback (never called - mock always succeeds)
     */
    void get_spoolman_spool(int spool_id, SpoolCallback on_success,
                            ErrorCallback on_error) override;

    /**
     * @brief Mock set active spool (updates internal state)
     *
     * Updates mock_active_spool_id_ and marks the corresponding spool as active.
     * Always calls success callback.
     *
     * @param spool_id Spool ID to set as active (0 or negative to clear)
     * @param on_success Success callback (always called)
     * @param on_error Error callback (never called)
     */
    void set_active_spool(int spool_id, SuccessCallback on_success,
                          ErrorCallback on_error) override;

    /**
     * @brief Update spool remaining weight in mock storage
     *
     * Updates the remaining_weight_g field for the specified spool.
     *
     * @param spool_id Spool ID to update
     * @param remaining_weight_g New remaining weight in grams
     * @param on_success Success callback (always called)
     * @param on_error Error callback (never called)
     */
    void update_spoolman_spool_weight(int spool_id, double remaining_weight_g,
                                      SuccessCallback on_success, ErrorCallback on_error) override;

    /**
     * @brief General-purpose spool update in mock storage
     *
     * Updates spool fields from JSON (remaining_weight, price, lot_nr, comment).
     *
     * @param spool_id Spool ID to update
     * @param spool_data JSON with fields to update
     * @param on_success Success callback (always called)
     * @param on_error Error callback (never called)
     */
    void update_spoolman_spool(int spool_id, const nlohmann::json& spool_data,
                               SuccessCallback on_success, ErrorCallback on_error) override;

    /**
     * @brief Update filament color in mock storage
     *
     * Logs the color update (mock doesn't track filament definitions separately).
     *
     * @param filament_id Filament definition ID
     * @param color_hex New color as hex string
     * @param on_success Success callback (always called)
     * @param on_error Error callback (never called)
     */
    void update_spoolman_filament(int filament_id, const nlohmann::json& filament_data,
                                  SuccessCallback on_success, ErrorCallback on_error) override;
    void update_spoolman_filament_color(int filament_id, const std::string& color_hex,
                                        SuccessCallback on_success,
                                        ErrorCallback on_error) override;

    void get_spoolman_vendors(VendorListCallback on_success, ErrorCallback on_error) override;

    void get_spoolman_filaments(FilamentListCallback on_success, ErrorCallback on_error) override;

    void create_spoolman_vendor(const nlohmann::json& vendor_data, VendorCreateCallback on_success,
                                ErrorCallback on_error) override;

    void create_spoolman_filament(const nlohmann::json& filament_data,
                                  FilamentCreateCallback on_success,
                                  ErrorCallback on_error) override;

    void create_spoolman_spool(const nlohmann::json& spool_data, SpoolCreateCallback on_success,
                               ErrorCallback on_error) override;

    void delete_spoolman_spool(int spool_id, SuccessCallback on_success,
                               ErrorCallback on_error) override;

    void get_spoolman_external_vendors(VendorListCallback on_success,
                                       ErrorCallback on_error) override;

    void get_spoolman_external_filaments(const std::string& vendor_name,
                                         FilamentListCallback on_success,
                                         ErrorCallback on_error) override;

    void get_spoolman_filaments(int vendor_id, FilamentListCallback on_success,
                                ErrorCallback on_error) override;

    void delete_spoolman_vendor(int vendor_id, SuccessCallback on_success,
                                ErrorCallback on_error) override;

    void delete_spoolman_filament(int filament_id, SuccessCallback on_success,
                                  ErrorCallback on_error) override;

    /**
     * @brief Enable or disable mock Spoolman integration
     *
     * Controls whether get_spoolman_status returns connected=true or false.
     * When disabled, the Spoolman panel should be hidden.
     *
     * @param enabled true to enable mock Spoolman, false to disable
     */
    void set_mock_spoolman_enabled(bool enabled) {
        mock_spoolman_enabled_ = enabled;
    }

    /**
     * @brief Check if mock Spoolman is enabled
     */
    [[nodiscard]] bool is_mock_spoolman_enabled() const {
        return mock_spoolman_enabled_;
    }

    // ========================================================================
    // Overridden REST Methods (return mock responses)
    // ========================================================================

    /**
     * @brief Mock REST GET request
     *
     * Returns mock responses for known endpoints (e.g., /server/ace/)
     * or a generic success response for unknown endpoints.
     *
     * @param endpoint REST endpoint path (e.g., "/server/ace/info")
     * @param on_complete Callback with RestResponse
     */
    void call_rest_get(const std::string& endpoint, RestCallback on_complete) override;

    /**
     * @brief Mock REST POST request
     *
     * Logs the request and returns a generic success response.
     *
     * @param endpoint REST endpoint path
     * @param params JSON parameters
     * @param on_complete Callback with RestResponse
     */
    void call_rest_post(const std::string& endpoint, const nlohmann::json& params,
                        RestCallback on_complete) override;

  private:
    // Shared mock state for coordination with MoonrakerClientMock
    std::shared_ptr<MockPrinterState> mock_state_;

    // Mock power device states (for toggle testing)
    std::map<std::string, bool> mock_power_states_;

    /**
     * @brief Find test file using fallback path search
     *
     * Tries multiple paths to locate test files:
     * - assets/test_gcodes/ (from project root)
     * - ../assets/test_gcodes/ (from build/)
     * - ../../assets/test_gcodes/ (from build/bin/)
     *
     * @param filename Filename to find
     * @return Full path to file if found, empty string otherwise
     */
    std::string find_test_file(const std::string& filename) const;

    /// Fallback path prefixes to search (from various CWDs)
    /// Note: Base directory is RuntimeConfig::TEST_GCODE_DIR (defined in runtime_config.h)
    static const std::vector<std::string> PATH_PREFIXES;

    /// Mock bed state for screws tilt simulation
    MockScrewsTiltState mock_bed_state_;

    /// Mock WLED strip on/off states (strip_id -> is_on)
    std::map<std::string, bool> mock_wled_states_;
    /// Mock WLED active presets (strip_id -> preset_id, -1 = none)
    std::map<std::string, int> mock_wled_presets_;
    /// Mock WLED brightness per strip (strip_id -> 0-255)
    std::map<std::string, int> mock_wled_brightness_;

    // Mock subscription ID counter
    SubscriptionId mock_next_subscription_id_ = 100;

    // Mock Spoolman state
    bool mock_spoolman_enabled_ = true;  ///< Whether Spoolman is "connected"
    int mock_active_spool_id_ = 1;       ///< Currently active spool ID
    std::vector<SpoolInfo> mock_spools_; ///< Mock spool inventory

    /// Slot-to-spool mapping (AMS slot index → Spoolman spool_id)
    std::map<int, int> slot_spool_map_;

    /**
     * @brief Initialize mock spool data with realistic sample inventory
     */
    void init_mock_spools();

  public:
    // ========================================================================
    // Slot-Spool Mapping (for realistic AMS↔Spoolman integration)
    // ========================================================================

    /**
     * @brief Assign a Spoolman spool to an AMS slot
     *
     * Creates a mapping between the slot and spool. The AMS backend should
     * call this when the user assigns a spool via the picker.
     *
     * @param slot_index AMS slot index (0-based)
     * @param spool_id Spoolman spool ID to assign
     */
    void assign_spool_to_slot(int slot_index, int spool_id);

    /**
     * @brief Remove spool assignment from an AMS slot
     *
     * @param slot_index AMS slot index to unassign
     */
    void unassign_spool_from_slot(int slot_index);

    /**
     * @brief Get the Spoolman spool ID assigned to a slot
     *
     * @param slot_index AMS slot index
     * @return Spool ID if assigned, 0 if not assigned
     */
    [[nodiscard]] int get_spool_for_slot(int slot_index) const;

    /**
     * @brief Get SpoolInfo for a slot (if assigned)
     *
     * @param slot_index AMS slot index
     * @return SpoolInfo if assigned, std::nullopt if not
     */
    [[nodiscard]] std::optional<SpoolInfo> get_spool_info_for_slot(int slot_index) const;

    /**
     * @brief Simulate filament consumption during a print
     *
     * Decrements remaining_weight_g on the active spool by the specified amount.
     * Also updates the spool assigned to the given slot if provided.
     *
     * @param grams Amount of filament consumed in grams
     * @param slot_index Optional slot to update (-1 uses active spool only)
     */
    void consume_filament(float grams, int slot_index = -1);

    /**
     * @brief Get mutable reference to mock spools for testing
     */
    std::vector<SpoolInfo>& get_mock_spools() {
        return mock_spools_;
    }

    /**
     * @brief Get const reference to mock spools
     */
    [[nodiscard]] const std::vector<SpoolInfo>& get_mock_spools() const {
        return mock_spools_;
    }
};
