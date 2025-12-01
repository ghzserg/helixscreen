// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MOONRAKER_API_MOCK_H
#define MOONRAKER_API_MOCK_H

#include "moonraker_api.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

// Forward declaration for shared state
class MockPrinterState;

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
    void download_file(const std::string& root, const std::string& path,
                       StringCallback on_success, ErrorCallback on_error) override;

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
    void upload_file(const std::string& root, const std::string& path,
                     const std::string& content, SuccessCallback on_success,
                     ErrorCallback on_error) override;

    /**
     * @brief Mock file upload with custom filename (logs but doesn't write)
     *
     * Logs the upload request but doesn't actually write files.
     * Always calls success callback.
     */
    void upload_file_with_name(const std::string& root, const std::string& path,
                               const std::string& filename, const std::string& content,
                               SuccessCallback on_success, ErrorCallback on_error) override;

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
    std::shared_ptr<MockPrinterState> get_mock_state() const { return mock_state_; }

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

    /// Base directory name for test G-code files
    static constexpr const char* TEST_GCODE_DIR = "assets/test_gcodes";

    /// Fallback path prefixes to search (from various CWDs)
    static const std::vector<std::string> PATH_PREFIXES;
};

#endif // MOONRAKER_API_MOCK_H
