// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MOCK_PRINTER_STATE_H
#define MOCK_PRINTER_STATE_H

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <vector>

/**
 * @brief Shared state between MoonrakerClientMock and MoonrakerAPIMock
 *
 * Provides consistent mock behavior across both the transport layer mock
 * (MoonrakerClientMock) and the HTTP API mock (MoonrakerAPIMock).
 *
 * Thread-safe for concurrent access from both mocks and the simulation thread.
 *
 * Usage:
 * @code
 *   auto shared_state = std::make_shared<MockPrinterState>();
 *   MoonrakerClientMock client_mock(PrinterType::VORON_24);
 *   client_mock.set_mock_state(shared_state);
 *   MoonrakerAPIMock api_mock(client_mock, printer_state);
 *   api_mock.set_mock_state(shared_state);
 *
 *   // Now EXCLUDE_OBJECT via client_mock is visible in api_mock queries
 *   client_mock.gcode_script("EXCLUDE_OBJECT NAME=Part_1");
 *   auto excluded = api_mock.get_excluded_objects(); // Contains "Part_1"
 * @endcode
 */
class MockPrinterState {
  public:
    // ========================================================================
    // Temperature state (atomic for lock-free access)
    // ========================================================================

    std::atomic<double> extruder_temp{25.0};
    std::atomic<double> extruder_target{0.0};
    std::atomic<double> bed_temp{25.0};
    std::atomic<double> bed_target{0.0};

    // ========================================================================
    // Print job state
    // ========================================================================

    std::atomic<int> print_state{0}; ///< Maps to PrintJobState enum (0=standby, 1=printing, etc.)
    std::atomic<double> print_progress{0.0}; ///< Progress from 0.0 to 1.0
    std::string current_filename;            ///< Currently printing file (protected by filename_mutex)

    // ========================================================================
    // Object exclusion (needs mutex for set/vector operations)
    // ========================================================================

    /**
     * @brief Add an object to the exclusion list
     *
     * Called by MoonrakerClientMock when processing EXCLUDE_OBJECT G-code.
     *
     * @param name Object name to exclude
     */
    void add_excluded_object(const std::string& name) {
        std::lock_guard<std::mutex> lock(objects_mutex_);
        excluded_objects_.insert(name);
    }

    /**
     * @brief Get current excluded objects (thread-safe copy)
     *
     * @return Set of excluded object names
     */
    std::set<std::string> get_excluded_objects() const {
        std::lock_guard<std::mutex> lock(objects_mutex_);
        return excluded_objects_;
    }

    /**
     * @brief Clear all excluded objects
     *
     * Called on print start or Klipper restart to reset the exclusion list.
     */
    void clear_excluded_objects() {
        std::lock_guard<std::mutex> lock(objects_mutex_);
        excluded_objects_.clear();
    }

    /**
     * @brief Set available objects for the current print
     *
     * Typically populated from G-code EXCLUDE_OBJECT_DEFINE commands.
     *
     * @param objects Vector of available object names
     */
    void set_available_objects(const std::vector<std::string>& objects) {
        std::lock_guard<std::mutex> lock(objects_mutex_);
        available_objects_ = objects;
    }

    /**
     * @brief Get available objects (thread-safe copy)
     *
     * @return Vector of available object names
     */
    std::vector<std::string> get_available_objects() const {
        std::lock_guard<std::mutex> lock(objects_mutex_);
        return available_objects_;
    }

    /**
     * @brief Set current filename (thread-safe)
     *
     * @param filename The currently printing file name
     */
    void set_current_filename(const std::string& filename) {
        std::lock_guard<std::mutex> lock(filename_mutex_);
        current_filename = filename;
    }

    /**
     * @brief Get current filename (thread-safe copy)
     *
     * @return Current filename being printed
     */
    std::string get_current_filename() const {
        std::lock_guard<std::mutex> lock(filename_mutex_);
        return current_filename;
    }

    /**
     * @brief Reset all mock state to defaults
     *
     * Useful for test setup/teardown to ensure clean state.
     */
    void reset() {
        extruder_temp = 25.0;
        extruder_target = 0.0;
        bed_temp = 25.0;
        bed_target = 0.0;
        print_state = 0;
        print_progress = 0.0;

        {
            std::lock_guard<std::mutex> lock(filename_mutex_);
            current_filename.clear();
        }

        {
            std::lock_guard<std::mutex> lock(objects_mutex_);
            excluded_objects_.clear();
            available_objects_.clear();
        }
    }

  private:
    std::set<std::string> excluded_objects_;
    std::vector<std::string> available_objects_;
    mutable std::mutex objects_mutex_;
    mutable std::mutex filename_mutex_;
};

#endif // MOCK_PRINTER_STATE_H
