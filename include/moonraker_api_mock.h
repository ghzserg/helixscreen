// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "moonraker_api.h"

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

  private:
    // Shared mock state for coordination with MoonrakerClientMock
    std::shared_ptr<MockPrinterState> mock_state_;
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
};
