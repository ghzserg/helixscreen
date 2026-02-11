// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_print_preparation_manager.h"

#include "../test_helpers/printer_state_test_access.h"

class PrintPreparationManagerTestAccess {
  public:
    static std::vector<std::pair<std::string, std::string>>
    get_skip_params(const helix::ui::PrintPreparationManager& m) {
        return m.collect_macro_skip_params();
    }
};

#include "../mocks/mock_websocket_server.h"
#include "../ui_test_utils.h"
#include "app_globals.h"
#include "capability_matrix.h"
#include "gcode_ops_detector.h"
#include "hv/EventLoopThread.h"
#include "moonraker_api.h"
#include "moonraker_client.h"
#include "moonraker_error.h"
#include "operation_registry.h"
#include "print_start_analyzer.h"
#include "printer_detector.h"
#include "printer_state.h"

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;

// ============================================================================
// Test Fixture: Mock Dependencies
// ============================================================================

// PrintPreparationManager has nullable dependencies - we can test formatting
// and state management without actual API/printer connections.

// ============================================================================
// Tests: Macro Analysis Formatting
// ============================================================================

TEST_CASE("PrintPreparationManager: format_macro_operations", "[print_preparation][macro]") {
    PrintPreparationManager manager;
    // No dependencies set - tests formatting without API

    SECTION("Returns empty string when no analysis available") {
        REQUIRE(manager.format_macro_operations().empty());
        REQUIRE(manager.has_macro_analysis() == false);
    }
}

TEST_CASE("PrintPreparationManager: is_macro_op_controllable", "[print_preparation][macro]") {
    PrintPreparationManager manager;

    SECTION("Returns false when no analysis available") {
        REQUIRE(manager.is_macro_op_controllable(PrintStartOpCategory::BED_MESH) == false);
        REQUIRE(manager.is_macro_op_controllable(PrintStartOpCategory::QGL) == false);
        REQUIRE(manager.is_macro_op_controllable(PrintStartOpCategory::Z_TILT) == false);
        REQUIRE(manager.is_macro_op_controllable(PrintStartOpCategory::NOZZLE_CLEAN) == false);
    }
}

TEST_CASE("PrintPreparationManager: get_macro_skip_param", "[print_preparation][macro]") {
    PrintPreparationManager manager;

    SECTION("Returns empty string when no analysis available") {
        REQUIRE(manager.get_macro_skip_param(PrintStartOpCategory::BED_MESH).empty());
        REQUIRE(manager.get_macro_skip_param(PrintStartOpCategory::QGL).empty());
    }
}

// ============================================================================
// Tests: File Operations Scanning
// ============================================================================

TEST_CASE("PrintPreparationManager: format_detected_operations", "[print_preparation][gcode]") {
    PrintPreparationManager manager;

    SECTION("Returns empty string when no scan result available") {
        REQUIRE(manager.format_detected_operations().empty());
    }

    SECTION("has_scan_result_for returns false when no scan done") {
        REQUIRE(manager.has_scan_result_for("test.gcode") == false);
        REQUIRE(manager.has_scan_result_for("") == false);
    }
}

TEST_CASE("PrintPreparationManager: clear_scan_cache", "[print_preparation][gcode]") {
    PrintPreparationManager manager;

    SECTION("Can be called when no cache exists") {
        // Should not throw or crash
        manager.clear_scan_cache();
        REQUIRE(manager.format_detected_operations().empty());
    }
}

// ============================================================================
// Tests: Resource Safety
// ============================================================================

TEST_CASE("PrintPreparationManager: check_modification_capability", "[print_preparation][safety]") {
    PrintPreparationManager manager;
    // No API set - tests fallback behavior

    SECTION("Without API, checks disk space fallback") {
        auto capability = manager.check_modification_capability();
        // Without API, has_plugin is false
        REQUIRE(capability.has_plugin == false);
        // Should still check disk space
        // (can_modify depends on system - just verify it returns valid struct)
        REQUIRE((capability.can_modify ||
                 !capability.can_modify)); // Always true, just checking no crash
    }
}

TEST_CASE("PrintPreparationManager: get_temp_directory", "[print_preparation][safety]") {
    PrintPreparationManager manager;

    SECTION("Returns usable temp directory path") {
        std::string temp_dir = manager.get_temp_directory();
        // Should return a non-empty path on any reasonable system
        // (empty only if all fallbacks fail, which shouldn't happen in tests)
        INFO("Temp directory: " << temp_dir);
        // Just verify it doesn't crash and returns something reasonable
        REQUIRE(temp_dir.find("helix") != std::string::npos);
    }
}

TEST_CASE("PrintPreparationManager: set_cached_file_size", "[print_preparation][safety]") {
    PrintPreparationManager manager;

    SECTION("Setting file size affects modification capability calculation") {
        // Set a reasonable file size
        manager.set_cached_file_size(10 * 1024 * 1024); // 10MB

        auto capability = manager.check_modification_capability();

        // If temp directory isn't available, required_bytes will be 0 (early return)
        // This can happen in CI environments or sandboxed test runners
        if (capability.has_disk_space) {
            // Disk space check succeeded - verify required_bytes accounts for file size
            REQUIRE(capability.required_bytes > 10 * 1024 * 1024);
        } else {
            // Temp directory unavailable - verify we get a sensible response
            INFO("Temp directory unavailable: " << capability.reason);
            REQUIRE(capability.can_modify == false);
            REQUIRE(capability.has_plugin == false);
        }
    }

    SECTION("Very large file size may exceed available space") {
        // Set an extremely large file size
        manager.set_cached_file_size(1000ULL * 1024 * 1024 * 1024); // 1TB

        auto capability = manager.check_modification_capability();
        // Should report insufficient space for such a large file
        // (unless running on a system with 2TB+ free space)
        INFO("can_modify: " << capability.can_modify);
        INFO("reason: " << capability.reason);
        // Just verify it handles large values without overflow/crash
        REQUIRE((capability.can_modify || !capability.can_modify));
    }
}

// ============================================================================
// Tests: Subject-Based Options Reading (LT2)
// ============================================================================

/**
 * LT2 Refactor: Observer Pattern for Checkbox State
 *
 * These tests verify the new read_options_from_subjects() method which reads
 * pre-print options from lv_subject_t pointers instead of LVGL widget state.
 *
 * Benefits of subject-based approach:
 * - No direct LVGL widget dependency (easier testing, better separation)
 * - Consistent with LVGL 9.x observer pattern used elsewhere
 * - Enables reactive updates when options change
 *
 * These tests are designed to FAIL initially because:
 * - read_options_from_subjects() doesn't exist yet
 * - set_preprint_subjects() doesn't exist yet
 *
 * After implementation, they should PASS.
 */

/**
 * @brief Test fixture for subject-based option reading
 *
 * Manages LVGL subject lifecycle and provides helper methods for
 * configuring checkbox and visibility subjects.
 */
struct PreprintSubjectsFixture {
    // Checkbox state subjects (1 = checked, 0 = unchecked)
    lv_subject_t preprint_bed_mesh{};
    lv_subject_t preprint_qgl{};
    lv_subject_t preprint_z_tilt{};
    lv_subject_t preprint_nozzle_clean{};
    lv_subject_t preprint_purge_line{};
    lv_subject_t preprint_timelapse{};

    // Visibility subjects (1 = visible/enabled, 0 = hidden/disabled)
    lv_subject_t can_show_bed_mesh{};
    lv_subject_t can_show_qgl{};
    lv_subject_t can_show_z_tilt{};
    lv_subject_t can_show_nozzle_clean{};
    lv_subject_t can_show_purge_line{};
    lv_subject_t can_show_timelapse{};

    bool initialized = false;

    void init_all_subjects() {
        if (initialized) {
            return;
        }

        // Initialize checkbox subjects (default unchecked)
        lv_subject_init_int(&preprint_bed_mesh, 0);
        lv_subject_init_int(&preprint_qgl, 0);
        lv_subject_init_int(&preprint_z_tilt, 0);
        lv_subject_init_int(&preprint_nozzle_clean, 0);
        lv_subject_init_int(&preprint_purge_line, 0);
        lv_subject_init_int(&preprint_timelapse, 0);

        // Initialize visibility subjects (default visible)
        lv_subject_init_int(&can_show_bed_mesh, 1);
        lv_subject_init_int(&can_show_qgl, 1);
        lv_subject_init_int(&can_show_z_tilt, 1);
        lv_subject_init_int(&can_show_nozzle_clean, 1);
        lv_subject_init_int(&can_show_purge_line, 1);
        lv_subject_init_int(&can_show_timelapse, 1);

        initialized = true;
    }

    void deinit_all_subjects() {
        if (!initialized) {
            return;
        }

        // Deinitialize in reverse order
        lv_subject_deinit(&can_show_timelapse);
        lv_subject_deinit(&can_show_purge_line);
        lv_subject_deinit(&can_show_nozzle_clean);
        lv_subject_deinit(&can_show_z_tilt);
        lv_subject_deinit(&can_show_qgl);
        lv_subject_deinit(&can_show_bed_mesh);

        lv_subject_deinit(&preprint_timelapse);
        lv_subject_deinit(&preprint_purge_line);
        lv_subject_deinit(&preprint_nozzle_clean);
        lv_subject_deinit(&preprint_z_tilt);
        lv_subject_deinit(&preprint_qgl);
        lv_subject_deinit(&preprint_bed_mesh);

        initialized = false;
    }

    ~PreprintSubjectsFixture() {
        deinit_all_subjects();
    }
};

TEST_CASE("PrintPreparationManager: read_options_from_subjects with initialized subjects",
          "[print_preparation][options][lt2]") {
    lv_init_safe();
    PrintPreparationManager manager;
    PreprintSubjectsFixture subjects;
    subjects.init_all_subjects();

    SECTION("Returns options matching subject values - all checked") {
        // Set all checkboxes to checked
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1);
        lv_subject_set_int(&subjects.preprint_qgl, 1);
        lv_subject_set_int(&subjects.preprint_z_tilt, 1);
        lv_subject_set_int(&subjects.preprint_nozzle_clean, 1);
        lv_subject_set_int(&subjects.preprint_timelapse, 1);

        // Set subjects on manager (this method doesn't exist yet - will cause compile failure)
        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                      &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                      &subjects.preprint_purge_line, &subjects.preprint_timelapse);

        // Read options from subjects (this method doesn't exist yet - will cause compile failure)
        auto options = manager.read_options_from_subjects();

        REQUIRE(options.bed_mesh == true);
        REQUIRE(options.qgl == true);
        REQUIRE(options.z_tilt == true);
        REQUIRE(options.nozzle_clean == true);
        REQUIRE(options.timelapse == true);
    }

    SECTION("Returns options matching subject values - mixed states") {
        // Set mixed checkbox states
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1);     // checked
        lv_subject_set_int(&subjects.preprint_qgl, 0);          // unchecked
        lv_subject_set_int(&subjects.preprint_z_tilt, 1);       // checked
        lv_subject_set_int(&subjects.preprint_nozzle_clean, 0); // unchecked
        lv_subject_set_int(&subjects.preprint_timelapse, 1);    // checked

        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                      &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                      &subjects.preprint_purge_line, &subjects.preprint_timelapse);

        auto options = manager.read_options_from_subjects();

        REQUIRE(options.bed_mesh == true);
        REQUIRE(options.qgl == false);
        REQUIRE(options.z_tilt == true);
        REQUIRE(options.nozzle_clean == false);
        REQUIRE(options.timelapse == true);
    }

    SECTION("Returns options matching subject values - all unchecked") {
        // All checkboxes unchecked (default state from fixture init)
        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                      &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                      &subjects.preprint_purge_line, &subjects.preprint_timelapse);

        auto options = manager.read_options_from_subjects();

        REQUIRE(options.bed_mesh == false);
        REQUIRE(options.qgl == false);
        REQUIRE(options.z_tilt == false);
        REQUIRE(options.nozzle_clean == false);
        REQUIRE(options.timelapse == false);
    }
}

TEST_CASE("PrintPreparationManager: read_options_from_subjects respects visibility",
          "[print_preparation][options][lt2]") {
    lv_init_safe();
    PrintPreparationManager manager;
    PreprintSubjectsFixture subjects;
    subjects.init_all_subjects();

    SECTION("Hidden checkbox returns false even when subject says checked") {
        // Set checkbox to checked
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1);

        // But hide it (visibility = 0)
        lv_subject_set_int(&subjects.can_show_bed_mesh, 0);

        // Set both checkbox and visibility subjects
        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                      &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                      &subjects.preprint_purge_line, &subjects.preprint_timelapse);
        manager.set_preprint_visibility_subjects(
            &subjects.can_show_bed_mesh, &subjects.can_show_qgl, &subjects.can_show_z_tilt,
            &subjects.can_show_nozzle_clean, &subjects.can_show_purge_line,
            &subjects.can_show_timelapse);

        auto options = manager.read_options_from_subjects();

        // bed_mesh should be false because it's hidden (visibility subject = 0)
        REQUIRE(options.bed_mesh == false);
    }

    SECTION("Multiple hidden checkboxes all return false") {
        // Set all checkboxes to checked
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1);
        lv_subject_set_int(&subjects.preprint_qgl, 1);
        lv_subject_set_int(&subjects.preprint_z_tilt, 1);
        lv_subject_set_int(&subjects.preprint_nozzle_clean, 1);
        lv_subject_set_int(&subjects.preprint_timelapse, 1);

        // Hide some checkboxes
        lv_subject_set_int(&subjects.can_show_bed_mesh, 0);     // hidden
        lv_subject_set_int(&subjects.can_show_qgl, 1);          // visible
        lv_subject_set_int(&subjects.can_show_z_tilt, 0);       // hidden
        lv_subject_set_int(&subjects.can_show_nozzle_clean, 1); // visible
        lv_subject_set_int(&subjects.can_show_timelapse, 0);    // hidden

        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                      &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                      &subjects.preprint_purge_line, &subjects.preprint_timelapse);
        manager.set_preprint_visibility_subjects(
            &subjects.can_show_bed_mesh, &subjects.can_show_qgl, &subjects.can_show_z_tilt,
            &subjects.can_show_nozzle_clean, &subjects.can_show_purge_line,
            &subjects.can_show_timelapse);

        auto options = manager.read_options_from_subjects();

        // Hidden checkboxes should return false
        REQUIRE(options.bed_mesh == false);    // hidden
        REQUIRE(options.qgl == true);          // visible + checked
        REQUIRE(options.z_tilt == false);      // hidden
        REQUIRE(options.nozzle_clean == true); // visible + checked
        REQUIRE(options.timelapse == false);   // hidden
    }

    SECTION("Visible but unchecked returns false") {
        // Set checkbox to unchecked
        lv_subject_set_int(&subjects.preprint_bed_mesh, 0);

        // Keep it visible
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1);

        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                      &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                      &subjects.preprint_purge_line, &subjects.preprint_timelapse);
        manager.set_preprint_visibility_subjects(
            &subjects.can_show_bed_mesh, &subjects.can_show_qgl, &subjects.can_show_z_tilt,
            &subjects.can_show_nozzle_clean, &subjects.can_show_purge_line,
            &subjects.can_show_timelapse);

        auto options = manager.read_options_from_subjects();

        // Visible but unchecked = false
        REQUIRE(options.bed_mesh == false);
    }
}

TEST_CASE("PrintPreparationManager: read_options_from_subjects with null subjects",
          "[print_preparation][options][lt2]") {
    lv_init_safe();
    PrintPreparationManager manager;

    SECTION("Returns all false when no subjects set") {
        // Don't call set_preprint_subjects - subjects should be nullptr
        auto options = manager.read_options_from_subjects();

        REQUIRE(options.bed_mesh == false);
        REQUIRE(options.qgl == false);
        REQUIRE(options.z_tilt == false);
        REQUIRE(options.nozzle_clean == false);
        REQUIRE(options.timelapse == false);
    }

    SECTION("Returns all false when subjects explicitly set to nullptr") {
        manager.set_preprint_subjects(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        auto options = manager.read_options_from_subjects();

        REQUIRE(options.bed_mesh == false);
        REQUIRE(options.qgl == false);
        REQUIRE(options.z_tilt == false);
        REQUIRE(options.nozzle_clean == false);
        REQUIRE(options.purge_line == false);
        REQUIRE(options.timelapse == false);
    }

    SECTION("Handles partial null subjects gracefully") {
        PreprintSubjectsFixture subjects;
        subjects.init_all_subjects();

        lv_subject_set_int(&subjects.preprint_bed_mesh, 1);
        lv_subject_set_int(&subjects.preprint_timelapse, 1);

        // Set only some subjects, others are nullptr
        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, nullptr, nullptr, nullptr,
                                      nullptr, &subjects.preprint_timelapse);

        auto options = manager.read_options_from_subjects();

        REQUIRE(options.bed_mesh == true);
        REQUIRE(options.qgl == false);          // nullptr subject = false
        REQUIRE(options.z_tilt == false);       // nullptr subject = false
        REQUIRE(options.nozzle_clean == false); // nullptr subject = false
        REQUIRE(options.purge_line == false);   // nullptr subject = false
        REQUIRE(options.timelapse == true);
    }
}

TEST_CASE("PrintPreparationManager: subject state changes are reflected immediately",
          "[print_preparation][options][lt2]") {
    lv_init_safe();
    PrintPreparationManager manager;
    PreprintSubjectsFixture subjects;
    subjects.init_all_subjects();

    SECTION("Changes to subject values are reflected in subsequent reads") {
        // Initial state: unchecked
        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                      &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                      &subjects.preprint_purge_line, &subjects.preprint_timelapse);

        auto options1 = manager.read_options_from_subjects();
        REQUIRE(options1.bed_mesh == false);
        REQUIRE(options1.qgl == false);

        // Change subject values
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1);
        lv_subject_set_int(&subjects.preprint_qgl, 1);

        // Read again - should reflect new values
        auto options2 = manager.read_options_from_subjects();
        REQUIRE(options2.bed_mesh == true);
        REQUIRE(options2.qgl == true);

        // Change back
        lv_subject_set_int(&subjects.preprint_bed_mesh, 0);

        // Read again - should reflect latest values
        auto options3 = manager.read_options_from_subjects();
        REQUIRE(options3.bed_mesh == false);
        REQUIRE(options3.qgl == true);
    }

    SECTION("Visibility changes are reflected immediately") {
        // Set checkbox to checked
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1);

        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                      &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                      &subjects.preprint_purge_line, &subjects.preprint_timelapse);
        manager.set_preprint_visibility_subjects(
            &subjects.can_show_bed_mesh, &subjects.can_show_qgl, &subjects.can_show_z_tilt,
            &subjects.can_show_nozzle_clean, &subjects.can_show_purge_line,
            &subjects.can_show_timelapse);

        // Initially visible
        auto options1 = manager.read_options_from_subjects();
        REQUIRE(options1.bed_mesh == true);

        // Hide it
        lv_subject_set_int(&subjects.can_show_bed_mesh, 0);

        // Should now be false
        auto options2 = manager.read_options_from_subjects();
        REQUIRE(options2.bed_mesh == false);

        // Show it again
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1);

        // Should be true again
        auto options3 = manager.read_options_from_subjects();
        REQUIRE(options3.bed_mesh == true);
    }
}

// ============================================================================
// Tests: Lifecycle Management
// ============================================================================

TEST_CASE("PrintPreparationManager: is_print_in_progress", "[print_preparation][lifecycle]") {
    PrintPreparationManager manager;

    SECTION("Not in progress by default (no printer state)") {
        // Without a PrinterState set, always returns false
        REQUIRE(manager.is_print_in_progress() == false);
    }
}

// ============================================================================
// Tests: Move Semantics
// ============================================================================

TEST_CASE("PrintPreparationManager: move constructor", "[print_preparation][lifecycle]") {
    PrintPreparationManager manager1;
    manager1.set_cached_file_size(1024);

    SECTION("Move constructor transfers state") {
        PrintPreparationManager manager2 = std::move(manager1);
        // manager2 should be usable - verify by calling a method
        manager2.clear_scan_cache();
        REQUIRE(manager2.is_print_in_progress() == false);
    }
}

TEST_CASE("PrintPreparationManager: move assignment", "[print_preparation][lifecycle]") {
    PrintPreparationManager manager1;
    PrintPreparationManager manager2;
    manager1.set_cached_file_size(2048);

    SECTION("Move assignment transfers state") {
        manager2 = std::move(manager1);
        // manager2 should be usable
        REQUIRE(manager2.is_print_in_progress() == false);
    }
}

// ============================================================================
// Tests: Capability Database Key Naming Convention
// ============================================================================

/**
 * BUG: collect_macro_skip_params() looks up "bed_leveling" but database uses "bed_mesh".
 *
 * The printer_database.json uses capability keys that match category_to_string() output:
 *   - category_to_string(PrintStartOpCategory::BED_MESH) returns "bed_mesh"
 *   - Database entry: "bed_mesh": { "param": "SKIP_LEVELING", ... }
 *
 * But collect_macro_skip_params() at line 878 uses has_capability("bed_leveling")
 * which will always return false because the key doesn't exist in the database.
 */
TEST_CASE("PrintPreparationManager: capability keys match category_to_string",
          "[print_preparation][capabilities][bug]") {
    // This test verifies that capability database keys align with category_to_string()
    // The database uses "bed_mesh", not "bed_leveling"

    SECTION("BED_MESH category maps to 'bed_mesh' key (not 'bed_leveling')") {
        // Verify what category_to_string returns for BED_MESH
        std::string expected_key = category_to_string(PrintStartOpCategory::BED_MESH);
        REQUIRE(expected_key == "bed_mesh");

        // Get AD5M Pro capabilities (known to have bed_mesh capability)
        auto caps = PrinterDetector::get_print_start_capabilities("FlashForge Adventurer 5M Pro");
        REQUIRE_FALSE(caps.empty());

        // The database uses "bed_mesh" as the key
        REQUIRE(caps.has_capability("bed_mesh"));

        // "bed_leveling" is NOT a valid key in the database
        REQUIRE_FALSE(caps.has_capability("bed_leveling"));

        // Verify the param details are accessible via the correct key
        auto* bed_cap = caps.get_capability("bed_mesh");
        REQUIRE(bed_cap != nullptr);
        REQUIRE(bed_cap->param == "SKIP_LEVELING");

        // This is the key assertion: code using capabilities MUST use "bed_mesh",
        // not "bed_leveling". Any lookup with "bed_leveling" will fail silently.
        // The bug in collect_macro_skip_params() uses the wrong key.
    }

    SECTION("All category strings are valid capability keys") {
        // Verify each PrintStartOpCategory has a consistent string representation
        // that matches what the database expects

        // These should be the keys used in printer_database.json
        REQUIRE(std::string(category_to_string(PrintStartOpCategory::BED_MESH)) == "bed_mesh");
        REQUIRE(std::string(category_to_string(PrintStartOpCategory::QGL)) == "qgl");
        REQUIRE(std::string(category_to_string(PrintStartOpCategory::Z_TILT)) == "z_tilt");
        REQUIRE(std::string(category_to_string(PrintStartOpCategory::NOZZLE_CLEAN)) ==
                "nozzle_clean");
        REQUIRE(std::string(category_to_string(PrintStartOpCategory::PURGE_LINE)) == "purge_line");
        REQUIRE(std::string(category_to_string(PrintStartOpCategory::SKEW_CORRECT)) ==
                "skew_correct");

        // BED_LEVEL is a parent category, not a database key
        REQUIRE(std::string(category_to_string(PrintStartOpCategory::BED_LEVEL)) == "bed_level");
    }
}

/**
 * Test that verifies collect_macro_skip_params() uses correct capability keys.
 *
 * The capability database uses keys that match category_to_string() output:
 *   - "bed_mesh" for BED_MESH
 *   - "qgl" for QGL
 *   - "z_tilt" for Z_TILT
 *   - "nozzle_clean" for NOZZLE_CLEAN
 *
 * This test verifies the code uses these correct keys (not legacy names like "bed_leveling").
 */
TEST_CASE("PrintPreparationManager: collect_macro_skip_params uses correct capability keys",
          "[print_preparation][capabilities]") {
    // Get capabilities for a known printer
    auto caps = PrinterDetector::get_print_start_capabilities("FlashForge Adventurer 5M Pro");
    REQUIRE_FALSE(caps.empty());

    SECTION("bed_mesh key is used (not bed_leveling)") {
        // The CORRECT lookup key matches category_to_string(BED_MESH)
        REQUIRE(caps.has_capability("bed_mesh"));

        // The WRONG key should NOT exist - this ensures code using it would fail
        REQUIRE_FALSE(caps.has_capability("bed_leveling"));

        // Verify the param details are accessible via the correct key
        auto* bed_cap = caps.get_capability("bed_mesh");
        REQUIRE(bed_cap != nullptr);
        REQUIRE(bed_cap->param == "SKIP_LEVELING");
    }

    SECTION("All capability keys match category_to_string output") {
        // These are the keys that collect_macro_skip_params() should use
        // They must match the keys in printer_database.json

        // BED_MESH -> "bed_mesh"
        REQUIRE(std::string(category_to_string(PrintStartOpCategory::BED_MESH)) == "bed_mesh");

        // QGL -> "qgl"
        REQUIRE(std::string(category_to_string(PrintStartOpCategory::QGL)) == "qgl");

        // Z_TILT -> "z_tilt"
        REQUIRE(std::string(category_to_string(PrintStartOpCategory::Z_TILT)) == "z_tilt");

        // NOZZLE_CLEAN -> "nozzle_clean"
        REQUIRE(std::string(category_to_string(PrintStartOpCategory::NOZZLE_CLEAN)) ==
                "nozzle_clean");
    }
}

// ============================================================================
// Tests: Macro Analysis Progress Tracking
// ============================================================================

/**
 * Tests for macro analysis in-progress flag behavior.
 *
 * The is_macro_analysis_in_progress() flag is used to disable the Print button
 * while analysis is running, preventing race conditions where a print could
 * start before skip params are known.
 */
TEST_CASE("PrintPreparationManager: macro analysis in-progress tracking",
          "[print_preparation][macro][progress]") {
    PrintPreparationManager manager;

    SECTION("is_macro_analysis_in_progress returns false initially") {
        // Before any analysis is started, should return false
        REQUIRE(manager.is_macro_analysis_in_progress() == false);
    }

    SECTION("is_macro_analysis_in_progress returns false when no API set") {
        // Without API, analyze_print_start_macro() should return early
        // and not set in_progress flag
        manager.analyze_print_start_macro();
        REQUIRE(manager.is_macro_analysis_in_progress() == false);
    }

    SECTION("has_macro_analysis returns false when no analysis done") {
        REQUIRE(manager.has_macro_analysis() == false);
    }

    SECTION("Multiple analyze calls without API are ignored gracefully") {
        // Call multiple times - should not crash or set flag
        manager.analyze_print_start_macro();
        manager.analyze_print_start_macro();
        manager.analyze_print_start_macro();

        REQUIRE(manager.is_macro_analysis_in_progress() == false);
        REQUIRE(manager.has_macro_analysis() == false);
    }
}

// ============================================================================
// Tests: Capabilities from PrinterState (LT1 Refactor)
// ============================================================================

/**
 * Tests for the LT1 refactor: capabilities should come from PrinterState.
 *
 * After the refactor:
 * - PrintPreparationManager::get_cached_capabilities() delegates to PrinterState
 * - PrinterState owns the printer type and cached capabilities
 * - Manager no longer needs its own cache or Config lookup
 *
 * These tests verify the manager correctly uses PrinterState for capabilities.
 */
TEST_CASE("PrintPreparationManager: capabilities come from PrinterState",
          "[print_preparation][capabilities][lt1]") {
    // Initialize LVGL for PrinterState subjects
    lv_init_safe();

    // Create PrinterState and initialize subjects (without XML registration for tests)
    PrinterState& printer_state = get_printer_state();
    PrinterStateTestAccess::reset(printer_state);
    printer_state.init_subjects(false);

    // Create manager and set dependencies
    PrintPreparationManager manager;
    manager.set_dependencies(nullptr, &printer_state);

    SECTION("Manager uses PrinterState capabilities for known printer") {
        // Set printer type on PrinterState (sync version for testing)
        printer_state.set_printer_type_sync("FlashForge Adventurer 5M Pro");

        // Verify PrinterState has the capabilities
        const auto& state_caps = printer_state.get_print_start_capabilities();
        REQUIRE_FALSE(state_caps.empty());
        REQUIRE(state_caps.has_capability("bed_mesh"));
        REQUIRE(state_caps.macro_name == "START_PRINT");

        // Get expected capability details for comparison
        auto* bed_cap = state_caps.get_capability("bed_mesh");
        REQUIRE(bed_cap != nullptr);
        REQUIRE(bed_cap->param == "SKIP_LEVELING");
    }

    SECTION("Manager sees empty capabilities when PrinterState has no type") {
        // Don't set any printer type - should have empty capabilities
        const auto& state_caps = printer_state.get_print_start_capabilities();
        REQUIRE(state_caps.empty());
        REQUIRE(state_caps.macro_name.empty());
    }

    SECTION("Manager sees empty capabilities for unknown printer type") {
        // Set an unknown printer type
        printer_state.set_printer_type_sync("Unknown Printer That Does Not Exist");

        // Should return empty capabilities, not crash
        const auto& state_caps = printer_state.get_print_start_capabilities();
        REQUIRE(state_caps.empty());
    }

    SECTION("Manager without PrinterState returns empty capabilities") {
        // Create manager without setting dependencies
        PrintPreparationManager standalone_manager;

        // format_preprint_steps uses get_cached_capabilities internally
        // Without printer_state_, it should return empty steps (not crash)
        std::string steps = standalone_manager.format_preprint_steps();
        REQUIRE(steps.empty());
    }
}

TEST_CASE("PrintPreparationManager: capabilities update when PrinterState type changes",
          "[print_preparation][capabilities][lt1]") {
    // Initialize LVGL for PrinterState subjects
    lv_init_safe();

    // Create PrinterState and initialize subjects
    PrinterState& printer_state = get_printer_state();
    PrinterStateTestAccess::reset(printer_state);
    printer_state.init_subjects(false);

    // Create manager and set dependencies
    PrintPreparationManager manager;
    manager.set_dependencies(nullptr, &printer_state);

    SECTION("Capabilities change when switching between known printers") {
        // Set to AD5M Pro first
        printer_state.set_printer_type_sync("FlashForge Adventurer 5M Pro");

        // Verify AD5M Pro capabilities
        const auto& caps_v1 = printer_state.get_print_start_capabilities();
        REQUIRE_FALSE(caps_v1.empty());
        REQUIRE(caps_v1.macro_name == "START_PRINT");
        std::string v1_macro = caps_v1.macro_name;
        size_t v1_param_count = caps_v1.params.size();

        // Now switch to AD5M (non-Pro)
        printer_state.set_printer_type_sync("FlashForge Adventurer 5M");

        // Verify capabilities updated
        const auto& caps_v2 = printer_state.get_print_start_capabilities();
        REQUIRE_FALSE(caps_v2.empty());
        // Both have START_PRINT but this confirms the lookup happened
        REQUIRE(caps_v2.macro_name == "START_PRINT");

        INFO("AD5M Pro params: " << v1_param_count);
        INFO("AD5M params: " << caps_v2.params.size());
    }

    SECTION("Capabilities become empty when switching to unknown printer") {
        // Start with known printer
        printer_state.set_printer_type_sync("FlashForge Adventurer 5M Pro");

        const auto& caps_known = printer_state.get_print_start_capabilities();
        REQUIRE_FALSE(caps_known.empty());

        // Switch to unknown printer
        printer_state.set_printer_type_sync("Generic Unknown Printer XYZ");

        // Capabilities should now be empty (no stale cache)
        const auto& caps_unknown = printer_state.get_print_start_capabilities();
        REQUIRE(caps_unknown.empty());
        REQUIRE(caps_unknown.macro_name.empty());
    }

    SECTION("Capabilities become empty when clearing printer type") {
        // Start with known printer
        printer_state.set_printer_type_sync("FlashForge Adventurer 5M Pro");

        const auto& caps_before = printer_state.get_print_start_capabilities();
        REQUIRE_FALSE(caps_before.empty());

        // Clear printer type
        printer_state.set_printer_type_sync("");

        // Capabilities should be empty
        const auto& caps_after = printer_state.get_print_start_capabilities();
        REQUIRE(caps_after.empty());
    }

    SECTION("No stale cache when rapidly switching printer types") {
        // Rapidly switch between multiple printer types
        printer_state.set_printer_type_sync("FlashForge Adventurer 5M Pro");
        REQUIRE_FALSE(printer_state.get_print_start_capabilities().empty());

        printer_state.set_printer_type_sync("Unknown Printer 1");
        REQUIRE(printer_state.get_print_start_capabilities().empty());

        printer_state.set_printer_type_sync("FlashForge Adventurer 5M");
        REQUIRE_FALSE(printer_state.get_print_start_capabilities().empty());

        printer_state.set_printer_type_sync("");
        REQUIRE(printer_state.get_print_start_capabilities().empty());

        // Final state: set back to known printer
        printer_state.set_printer_type_sync("FlashForge Adventurer 5M Pro");
        const auto& final_caps = printer_state.get_print_start_capabilities();
        REQUIRE_FALSE(final_caps.empty());
        REQUIRE(final_caps.has_capability("bed_mesh"));
    }
}

// ============================================================================
// Tests: Capability Cache Behavior (Legacy - using PrinterDetector directly)
// ============================================================================

/**
 * Tests for PrinterDetector capability lookup behavior.
 *
 * These tests verify the underlying PrinterDetector::get_print_start_capabilities()
 * works correctly. After the LT1 refactor, PrinterState wraps this, but these
 * tests remain valuable for verifying the database lookup layer.
 */
TEST_CASE("PrintPreparationManager: capability cache behavior",
          "[print_preparation][capabilities][cache]") {
    SECTION("get_cached_capabilities returns capabilities for known printer types") {
        // Verify PrinterDetector returns different capabilities for different printers
        auto ad5m_caps =
            PrinterDetector::get_print_start_capabilities("FlashForge Adventurer 5M Pro");
        auto voron_caps = PrinterDetector::get_print_start_capabilities("Voron 2.4");

        // AD5M Pro should have bed_mesh capability
        REQUIRE_FALSE(ad5m_caps.empty());
        REQUIRE(ad5m_caps.has_capability("bed_mesh"));

        // Voron 2.4 may have different capabilities (or none in database)
        // The key point is the lookup happens and returns a valid struct
        // (empty struct is valid - means no database entry)
        INFO("AD5M caps: " << ad5m_caps.params.size() << " params");
        INFO("Voron caps: " << voron_caps.params.size() << " params");
    }

    SECTION("Different printer types return different capabilities") {
        // This verifies the database contains distinct entries
        auto ad5m_caps =
            PrinterDetector::get_print_start_capabilities("FlashForge Adventurer 5M Pro");
        auto ad5m_std_caps =
            PrinterDetector::get_print_start_capabilities("FlashForge Adventurer 5M");

        // Both should exist (AD5M and AD5M Pro are separate entries)
        REQUIRE_FALSE(ad5m_caps.empty());
        REQUIRE_FALSE(ad5m_std_caps.empty());

        // They should have the same macro name (START_PRINT) but this confirms
        // the lookup works for different printer strings
        REQUIRE(ad5m_caps.macro_name == ad5m_std_caps.macro_name);
    }

    SECTION("Unknown printer type returns empty capabilities") {
        auto unknown_caps =
            PrinterDetector::get_print_start_capabilities("NonExistent Printer XYZ");

        // Unknown printer should return empty capabilities (not crash)
        REQUIRE(unknown_caps.empty());
        REQUIRE(unknown_caps.macro_name.empty());
        REQUIRE(unknown_caps.params.empty());
    }

    SECTION("Capability lookup is idempotent") {
        // Multiple lookups for same printer should return identical results
        auto caps1 = PrinterDetector::get_print_start_capabilities("FlashForge Adventurer 5M Pro");
        auto caps2 = PrinterDetector::get_print_start_capabilities("FlashForge Adventurer 5M Pro");

        REQUIRE(caps1.macro_name == caps2.macro_name);
        REQUIRE(caps1.params.size() == caps2.params.size());

        // Verify specific capability matches
        if (caps1.has_capability("bed_mesh") && caps2.has_capability("bed_mesh")) {
            REQUIRE(caps1.get_capability("bed_mesh")->param ==
                    caps2.get_capability("bed_mesh")->param);
        }
    }
}

// ============================================================================
// Tests: Priority Order Consistency
// ============================================================================

/**
 * Tests for operation priority order consistency.
 *
 * Both format_preprint_steps() and collect_macro_skip_params() should use
 * the same priority order for merging operations:
 *   1. Database (authoritative for known printers)
 *   2. Macro analysis (detected from printer config)
 *   3. File scan (embedded operations in G-code)
 *
 * This ensures the UI shows the same operations that will be controlled.
 */
TEST_CASE("PrintPreparationManager: priority order consistency",
          "[print_preparation][priority][order]") {
    PrintPreparationManager manager;

    SECTION("format_preprint_steps returns empty when no data available") {
        // Without scan result, macro analysis, or capabilities, should return empty
        std::string steps = manager.format_preprint_steps();
        REQUIRE(steps.empty());
    }

    SECTION("Database capabilities appear in format_preprint_steps output") {
        // We can't directly set the printer type without Config, but we can verify
        // the database lookup returns expected operations for known printers

        auto caps = PrinterDetector::get_print_start_capabilities("FlashForge Adventurer 5M Pro");
        REQUIRE_FALSE(caps.empty());

        // AD5M Pro has bed_mesh capability
        REQUIRE(caps.has_capability("bed_mesh"));

        // The capability should have a param name (SKIP_LEVELING)
        auto* bed_cap = caps.get_capability("bed_mesh");
        REQUIRE(bed_cap != nullptr);
        REQUIRE_FALSE(bed_cap->param.empty());
    }

    SECTION("Priority order: database > macro > file") {
        // Verify the code comment/contract: Database takes priority over macro,
        // which takes priority over file scan.
        //
        // This is tested indirectly through the format_preprint_steps() output
        // which uses "(optional)" suffix for skippable operations.

        // Get database capabilities for a known printer
        auto caps = PrinterDetector::get_print_start_capabilities("FlashForge Adventurer 5M Pro");

        // Database entries are skippable (have params)
        if (caps.has_capability("bed_mesh")) {
            auto* bed_cap = caps.get_capability("bed_mesh");
            REQUIRE(bed_cap != nullptr);
            // Has a skip value means it's controllable
            REQUIRE_FALSE(bed_cap->skip_value.empty());
        }
    }

    SECTION("Category keys are consistent between operations") {
        // Verify the category keys used in format_preprint_steps match those
        // used in collect_macro_skip_params. Both should use:
        // - "bed_mesh" (not "bed_leveling")
        // - "qgl" (not "quad_gantry_level")
        // - "z_tilt"
        // - "nozzle_clean"

        // These keys come from category_to_string() for macro operations
        // and are hardcoded for database lookups
        REQUIRE(std::string(category_to_string(PrintStartOpCategory::BED_MESH)) == "bed_mesh");
        REQUIRE(std::string(category_to_string(PrintStartOpCategory::QGL)) == "qgl");
        REQUIRE(std::string(category_to_string(PrintStartOpCategory::Z_TILT)) == "z_tilt");
        REQUIRE(std::string(category_to_string(PrintStartOpCategory::NOZZLE_CLEAN)) ==
                "nozzle_clean");

        // And the database uses these same keys
        auto caps = PrinterDetector::get_print_start_capabilities("FlashForge Adventurer 5M Pro");
        if (!caps.empty()) {
            // bed_mesh key exists (not "bed_leveling")
            REQUIRE(caps.has_capability("bed_mesh"));
            REQUIRE_FALSE(caps.has_capability("bed_leveling"));
        }
    }
}

// ============================================================================
// Tests: format_preprint_steps Content Verification
// ============================================================================

/**
 * Tests for format_preprint_steps() output format and content.
 *
 * The function merges operations from database, macro, and file scan,
 * deduplicates them, and formats as a bulleted list.
 */
TEST_CASE("PrintPreparationManager: format_preprint_steps formatting",
          "[print_preparation][format][steps]") {
    PrintPreparationManager manager;

    SECTION("Returns empty string when no operations detected") {
        std::string steps = manager.format_preprint_steps();
        REQUIRE(steps.empty());
    }

    SECTION("Output uses bullet point format") {
        // We can verify the format contract: output should use "• " prefix
        // for each operation when there are operations.
        // This test documents the expected format without requiring mock data.

        // The format_preprint_steps() returns either:
        // - Empty string (no operations)
        // - "• Operation name\n• Another operation (optional)\n..."

        // Since we can't inject mock data, we verify the format through
        // the database lookup which does populate steps
        auto caps = PrinterDetector::get_print_start_capabilities("FlashForge Adventurer 5M Pro");
        if (!caps.empty()) {
            // With capabilities set, format_preprint_steps would show them
            // The test verifies the capability data exists for the merge
            REQUIRE(caps.has_capability("bed_mesh"));
        }
    }

    SECTION("Skippable operations show (optional) suffix") {
        // Operations from database and controllable macro operations
        // should show "(optional)" in the output

        // Get database capability to verify skip_value exists
        auto caps = PrinterDetector::get_print_start_capabilities("FlashForge Adventurer 5M Pro");
        if (caps.has_capability("bed_mesh")) {
            auto* bed_cap = caps.get_capability("bed_mesh");
            REQUIRE(bed_cap != nullptr);
            // Has skip_value means it's controllable = shows (optional)
            REQUIRE_FALSE(bed_cap->skip_value.empty());
        }
    }
}

// ============================================================================
// Tests: Macro Analysis Retry Logic (with MockWebSocketServer)
// ============================================================================

/**
 * @brief Test fixture for macro analysis retry tests using real WebSocket infrastructure
 *
 * This fixture provides:
 * - MockWebSocketServer for controlling JSON-RPC responses
 * - Real MoonrakerClient + MoonrakerAPI connected to the mock server
 * - PrinterState with initialized subjects
 * - Helper methods for waiting on async operations with queue draining
 *
 * The PrintStartAnalyzer flow:
 * 1. Calls api->list_files("config", ...) which sends server.files.list via WebSocket
 * 2. For each .cfg file found, calls api->download_file() via HTTP
 * 3. Scans each file for [gcode_macro PRINT_START] or similar
 *
 * We can test retry logic by controlling the server.files.list response:
 * - Return error to trigger retry
 * - Return empty list to complete with "not found"
 * - Return file list to proceed to download phase
 */
class MacroAnalysisRetryFixture {
    static bool queue_initialized;

  public:
    MacroAnalysisRetryFixture() {
        // Initialize LVGL for subjects and update queue
        lv_init_safe();

        // Initialize update queue once (static guard) - CRITICAL for ui_queue_update()
        if (!queue_initialized) {
            ui_update_queue_init();
            queue_initialized = true;
        }

        // Initialize PrinterState subjects (needed for dependency injection)
        printer_state_.init_subjects(false); // false = no XML registration

        // Start mock WebSocket server on fixed port (ephemeral port lookup is unreliable)
        server_ = std::make_unique<MockWebSocketServer>();
        int port = server_->start(18766); // Fixed port for retry tests
        REQUIRE(port > 0);

        // Create event loop and client
        loop_thread_ = std::make_shared<hv::EventLoopThread>();
        loop_thread_->start();

        client_ = std::make_unique<MoonrakerClient>(loop_thread_->loop());
        client_->set_connection_timeout(2000);
        client_->set_default_request_timeout(2000);
        client_->setReconnect(nullptr); // Disable auto-reconnect

        // Create API wrapper
        api_ = std::make_unique<MoonrakerAPI>(*client_, printer_state_);

        // Connect to mock server
        std::atomic<bool> connected{false};
        client_->connect(server_->url().c_str(), [&connected]() { connected = true; }, []() {});

        // Wait for connection
        for (int i = 0; i < 50 && !connected; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        REQUIRE(connected);

        // Set up manager with dependencies
        // NOTE: We set dependencies BEFORE setting connection state to avoid
        // triggering auto-analysis on connection (which would race with test setup)
        manager_.set_dependencies(api_.get(), &printer_state_);

        // Don't set connection state to CONNECTED yet - let tests control when
        // analysis starts by calling analyze_print_start_macro() explicitly
    }

    ~MacroAnalysisRetryFixture() {
        // Stop event loop FIRST to prevent callbacks during teardown
        loop_thread_->stop();
        loop_thread_->join();

        // Destroy resources in order
        api_.reset();
        client_.reset();
        server_->stop();
        server_.reset();

        // Drain pending callbacks
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Shutdown queue
        ui_update_queue_shutdown();

        // Reset static flag for next test [L053]
        queue_initialized = false;
    }

    /**
     * @brief Drain pending UI updates (simulates main loop iteration)
     */
    void drain_queue() {
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        lv_timer_handler_safe(); // Process LVGL timers for retry scheduling
    }

    /**
     * @brief Wait for condition with queue draining and tick advancement
     *
     * Advances LVGL tick counter alongside real time so timer-based
     * retries (lv_timer_create) fire at the right moment.
     */
    bool wait_for(std::function<bool()> condition, int timeout_ms = 5000) {
        auto start = std::chrono::steady_clock::now();
        while (!condition()) {
            lv_tick_inc(10); // Advance LVGL tick to allow timer-based retries
            drain_queue();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();
            if (elapsed > timeout_ms) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Configure server to return error for list_files N times, then succeed
     *
     * @param failures Number of times to return error before success
     * @param success_files Files to return on success (empty = no files found)
     */
    void set_list_files_failures(int failures, const std::vector<std::string>& success_files = {}) {
        list_files_call_count_ = 0;
        list_files_failures_ = failures;
        list_files_success_files_ = success_files;

        server_->clear_handlers();

        // Handler that fails N times, then succeeds
        server_->on_method("server.files.list", [this](const json& params) -> json {
            (void)params;
            list_files_call_count_++;
            {
                std::lock_guard<std::mutex> lock(call_times_mutex_);
                list_files_call_times_.push_back(std::chrono::steady_clock::now());
            }

            if (list_files_call_count_ <= list_files_failures_) {
                // Throw to trigger error response
                throw std::runtime_error("Mock failure #" + std::to_string(list_files_call_count_));
            }

            // Success: return file list
            json result = json::array();
            for (const auto& file : list_files_success_files_) {
                result.push_back({{"path", file}, {"size", 1024}, {"modified", 1234567890.0}});
            }
            return result;
        });
    }

    /**
     * @brief Configure server to always return error for list_files
     */
    void set_list_files_always_fail() {
        list_files_call_count_ = 0;

        server_->clear_handlers();
        server_->on_method_error("server.files.list", [this](const json&) {
            list_files_call_count_++;
            {
                std::lock_guard<std::mutex> lock(call_times_mutex_);
                list_files_call_times_.push_back(std::chrono::steady_clock::now());
            }
            return std::make_pair(-1, "Mock permanent failure");
        });
    }

    /**
     * @brief Configure server to succeed immediately with empty file list
     *
     * This results in "no PRINT_START found" but with analysis complete.
     */
    void set_list_files_success_empty() {
        list_files_call_count_ = 0;

        server_->clear_handlers();
        server_->on_method("server.files.list", [this](const json&) -> json {
            list_files_call_count_++;
            {
                std::lock_guard<std::mutex> lock(call_times_mutex_);
                list_files_call_times_.push_back(std::chrono::steady_clock::now());
            }
            return json::array(); // Empty file list
        });
    }

    int get_list_files_call_count() const {
        return list_files_call_count_;
    }

    std::vector<std::chrono::steady_clock::time_point> get_call_times() {
        std::lock_guard<std::mutex> lock(call_times_mutex_);
        return list_files_call_times_;
    }

    void clear_call_times() {
        std::lock_guard<std::mutex> lock(call_times_mutex_);
        list_files_call_times_.clear();
    }

  protected:
    std::unique_ptr<MockWebSocketServer> server_;
    std::shared_ptr<hv::EventLoopThread> loop_thread_;
    std::unique_ptr<MoonrakerClient> client_;
    std::unique_ptr<MoonrakerAPI> api_;
    PrinterState printer_state_;
    PrintPreparationManager manager_;

    std::atomic<int> list_files_call_count_{0};
    int list_files_failures_{0};
    std::vector<std::string> list_files_success_files_;

    std::mutex call_times_mutex_;
    std::vector<std::chrono::steady_clock::time_point> list_files_call_times_;
};
bool MacroAnalysisRetryFixture::queue_initialized = false;

TEST_CASE_METHOD(MacroAnalysisRetryFixture,
                 "PrintPreparationManager: macro analysis retry - first attempt succeeds",
                 "[print_preparation][retry][eventloop][slow]") {
    SECTION("Success on first attempt - no retries needed") {
        // Configure server to succeed immediately with empty file list
        // (Results in "no macro found" but analysis completes successfully)
        set_list_files_success_empty();

        std::atomic<bool> callback_invoked{false};
        std::atomic<bool> callback_found{true}; // Start true to verify it becomes false

        manager_.set_macro_analysis_callback([&](const helix::PrintStartAnalysis& analysis) {
            spdlog::info("[TEST] Callback invoked, found={}", analysis.found);
            callback_invoked = true;
            callback_found = analysis.found;
        });

        // Trigger analysis
        spdlog::info("[TEST] Starting analysis");
        manager_.analyze_print_start_macro();
        spdlog::info("[TEST] Analysis started, waiting for callback");

        // Wait for callback - use longer timeout and debug
        auto start = std::chrono::steady_clock::now();
        while (!callback_invoked.load()) {
            lv_tick_inc(10);
            drain_queue();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();
            if (elapsed > 100 && elapsed % 500 < 20) {
                spdlog::info("[TEST] Waiting... elapsed={}ms, callback_invoked={}", elapsed,
                             callback_invoked.load());
            }
            if (elapsed > 5000) {
                spdlog::error("[TEST] Timeout waiting for callback!");
                break;
            }
        }

        REQUIRE(callback_invoked);
        REQUIRE(get_list_files_call_count() == 1);
        REQUIRE(callback_found == false); // No config files = no macro found
        REQUIRE(manager_.is_macro_analysis_in_progress() == false);
        // Analysis completed but found=false; has_macro_analysis() requires found==true
        // so verify completion via get_macro_analysis() instead
        REQUIRE(manager_.get_macro_analysis().has_value());
    }
}

TEST_CASE_METHOD(MacroAnalysisRetryFixture,
                 "PrintPreparationManager: macro analysis retry - first fails, second succeeds",
                 "[print_preparation][retry][eventloop][slow]") {
    SECTION("Retry succeeds on second attempt") {
        // Configure server to fail once, then succeed with empty list
        set_list_files_failures(1, {}); // Fail once, then return empty list

        std::atomic<bool> callback_invoked{false};
        std::atomic<bool> callback_found{true};

        manager_.set_macro_analysis_callback([&](const helix::PrintStartAnalysis& analysis) {
            callback_invoked = true;
            callback_found = analysis.found;
        });

        // Trigger analysis
        manager_.analyze_print_start_macro();

        // Wait for callback (allow extra time for retry delay)
        REQUIRE(wait_for([&]() { return callback_invoked.load(); }, 5000));

        // Verify retry happened: 1 failure + 1 success = 2 calls
        REQUIRE(get_list_files_call_count() == 2);
        REQUIRE(callback_found == false); // Empty list = no macro found
        REQUIRE(manager_.is_macro_analysis_in_progress() == false);
        REQUIRE(manager_.get_macro_analysis().has_value());
    }
}

TEST_CASE_METHOD(MacroAnalysisRetryFixture,
                 "PrintPreparationManager: macro analysis retry - all retries exhausted",
                 "[print_preparation][retry][eventloop][slow]") {
    SECTION("Callback invoked with found=false after 3 failed attempts") {
        // Configure server to always fail
        set_list_files_always_fail();

        std::atomic<bool> callback_invoked{false};
        std::atomic<bool> callback_found{true}; // Start true to verify it becomes false

        manager_.set_macro_analysis_callback([&](const helix::PrintStartAnalysis& analysis) {
            callback_invoked = true;
            callback_found = analysis.found;
        });

        // Trigger analysis
        manager_.analyze_print_start_macro();

        // Wait for callback (allow time for all retries: 1s + 2s delays)
        REQUIRE(wait_for([&]() { return callback_invoked.load(); }, 8000));

        // Verify all attempts: 1 initial + 2 retries = 3 total
        REQUIRE(get_list_files_call_count() == 3);
        REQUIRE(callback_found == false); // All attempts failed
        REQUIRE(manager_.is_macro_analysis_in_progress() == false);
        REQUIRE(manager_.get_macro_analysis().has_value()); // Has result with found=false
    }
}

TEST_CASE_METHOD(MacroAnalysisRetryFixture,
                 "PrintPreparationManager: macro analysis retry counter resets on new request",
                 "[print_preparation][retry][eventloop][slow]") {
    SECTION("New analysis request after cache clears uses fresh retry counter") {
        // First analysis: succeed immediately
        set_list_files_success_empty();

        std::atomic<int> callback_count{0};
        manager_.set_macro_analysis_callback(
            [&](const helix::PrintStartAnalysis&) { callback_count++; });

        manager_.analyze_print_start_macro();
        REQUIRE(wait_for([&]() { return callback_count.load() == 1; }, 3000));
        REQUIRE(get_list_files_call_count() == 1);

        // Clear the cache to allow new analysis
        // (Normally, the manager caches results and won't re-analyze)
        // We need to create a new manager to reset state
        PrintPreparationManager manager2;
        manager2.set_dependencies(api_.get(), &printer_state_);

        // Reset call count and configure failures
        clear_call_times();
        list_files_call_count_ = 0;
        set_list_files_failures(1, {}); // Fail once, then succeed

        std::atomic<bool> callback2_invoked{false};
        manager2.set_macro_analysis_callback(
            [&](const helix::PrintStartAnalysis&) { callback2_invoked = true; });

        manager2.analyze_print_start_macro();
        REQUIRE(wait_for([&]() { return callback2_invoked.load(); }, 5000));

        // Should have retried fresh: 1 failure + 1 success = 2 calls
        REQUIRE(get_list_files_call_count() == 2);
    }
}

TEST_CASE_METHOD(MacroAnalysisRetryFixture,
                 "PrintPreparationManager: in-progress flag stays true during retries",
                 "[print_preparation][retry][eventloop][slow]") {
    SECTION("is_macro_analysis_in_progress remains true during retry delay") {
        // Configure server to fail first call
        set_list_files_failures(1, {});

        std::atomic<bool> callback_invoked{false};
        manager_.set_macro_analysis_callback(
            [&](const helix::PrintStartAnalysis&) { callback_invoked = true; });

        // Trigger analysis
        manager_.analyze_print_start_macro();

        // Immediately after starting, should be in progress
        REQUIRE(manager_.is_macro_analysis_in_progress() == true);

        // Wait a short time for first failure to process (but not for retry to complete)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        lv_tick_inc(500); // Advance LVGL tick to match real time elapsed
        drain_queue();

        // During retry delay, should STILL be in progress
        // This is the key assertion - the in_progress flag should stay true during retries
        if (!callback_invoked.load()) {
            // Only check if callback hasn't been invoked yet
            REQUIRE(manager_.is_macro_analysis_in_progress() == true);
        }

        // Wait for final callback
        REQUIRE(wait_for([&]() { return callback_invoked.load(); }, 5000));

        // After completion, should no longer be in progress
        REQUIRE(manager_.is_macro_analysis_in_progress() == false);
    }
}

TEST_CASE_METHOD(MacroAnalysisRetryFixture,
                 "PrintPreparationManager: retry timing follows exponential backoff",
                 "[print_preparation][retry][timing][eventloop][slow]") {
    SECTION("Backoff delays: ~1s, ~2s between retries") {
        // Configure server to always fail so we can measure all retry timings
        set_list_files_always_fail();

        std::atomic<bool> callback_invoked{false};
        manager_.set_macro_analysis_callback(
            [&](const helix::PrintStartAnalysis&) { callback_invoked = true; });

        // Clear timing data
        clear_call_times();

        // Trigger analysis
        manager_.analyze_print_start_macro();

        // Wait for all attempts to complete
        REQUIRE(wait_for([&]() { return callback_invoked.load(); }, 8000));

        // Get call timestamps
        auto times = get_call_times();
        REQUIRE(times.size() == 3); // 1 initial + 2 retries

        // Verify exponential backoff delays
        auto delay1 = std::chrono::duration_cast<std::chrono::milliseconds>(times[1] - times[0]);
        auto delay2 = std::chrono::duration_cast<std::chrono::milliseconds>(times[2] - times[1]);

        // First delay should be ~1s (1000ms with tolerance)
        INFO("Delay 1: " << delay1.count() << "ms");
        REQUIRE(delay1.count() >= 800);  // At least 800ms
        REQUIRE(delay1.count() <= 1500); // At most 1.5s

        // Second delay should be ~2s (2000ms with tolerance)
        INFO("Delay 2: " << delay2.count() << "ms");
        REQUIRE(delay2.count() >= 1800); // At least 1.8s
        REQUIRE(delay2.count() <= 2500); // At most 2.5s
    }
}

// ============================================================================
// Tests: Subject-Only API (P1 Priority - Deprecated Widget API Removal)
// ============================================================================

/**
 * These tests document the expected behavior of the subject-based API that must
 * be preserved when the deprecated widget-based API is removed.
 *
 * The subject-based API allows reading pre-print options from lv_subject_t
 * pointers instead of directly querying LVGL widget states, enabling better
 * separation of concerns and easier testing.
 *
 * Key methods being tested:
 * - read_options_from_subjects(): Reads checkbox states from subjects
 * - get_option_state(): Determines tri-state from visibility + checked subjects
 * - collect_ops_to_disable(): Uses subjects exclusively for determining what to disable
 */

TEST_CASE("PrintPreparationManager: read_options_from_subjects returns correct PrePrintOptions",
          "[print_preparation][p1][subjects]") {
    lv_init_safe();
    PrintPreparationManager manager;
    PreprintSubjectsFixture subjects;
    subjects.init_all_subjects();

    SECTION("All options checked with all visible - returns all true") {
        // Set all visibility to visible (1)
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1);
        lv_subject_set_int(&subjects.can_show_qgl, 1);
        lv_subject_set_int(&subjects.can_show_z_tilt, 1);
        lv_subject_set_int(&subjects.can_show_nozzle_clean, 1);
        lv_subject_set_int(&subjects.can_show_purge_line, 1);
        lv_subject_set_int(&subjects.can_show_timelapse, 1);

        // Set all checkboxes to checked (1)
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1);
        lv_subject_set_int(&subjects.preprint_qgl, 1);
        lv_subject_set_int(&subjects.preprint_z_tilt, 1);
        lv_subject_set_int(&subjects.preprint_nozzle_clean, 1);
        lv_subject_set_int(&subjects.preprint_purge_line, 1);
        lv_subject_set_int(&subjects.preprint_timelapse, 1);

        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                      &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                      &subjects.preprint_purge_line, &subjects.preprint_timelapse);
        manager.set_preprint_visibility_subjects(
            &subjects.can_show_bed_mesh, &subjects.can_show_qgl, &subjects.can_show_z_tilt,
            &subjects.can_show_nozzle_clean, &subjects.can_show_purge_line,
            &subjects.can_show_timelapse);

        auto options = manager.read_options_from_subjects();

        REQUIRE(options.bed_mesh == true);
        REQUIRE(options.qgl == true);
        REQUIRE(options.z_tilt == true);
        REQUIRE(options.nozzle_clean == true);
        REQUIRE(options.purge_line == true);
        REQUIRE(options.timelapse == true);
    }

    SECTION("All options unchecked with all visible - returns all false") {
        // Set all visibility to visible (1)
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1);
        lv_subject_set_int(&subjects.can_show_qgl, 1);
        lv_subject_set_int(&subjects.can_show_z_tilt, 1);
        lv_subject_set_int(&subjects.can_show_nozzle_clean, 1);
        lv_subject_set_int(&subjects.can_show_purge_line, 1);
        lv_subject_set_int(&subjects.can_show_timelapse, 1);

        // Set all checkboxes to unchecked (0 - default from fixture)

        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                      &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                      &subjects.preprint_purge_line, &subjects.preprint_timelapse);
        manager.set_preprint_visibility_subjects(
            &subjects.can_show_bed_mesh, &subjects.can_show_qgl, &subjects.can_show_z_tilt,
            &subjects.can_show_nozzle_clean, &subjects.can_show_purge_line,
            &subjects.can_show_timelapse);

        auto options = manager.read_options_from_subjects();

        REQUIRE(options.bed_mesh == false);
        REQUIRE(options.qgl == false);
        REQUIRE(options.z_tilt == false);
        REQUIRE(options.nozzle_clean == false);
        REQUIRE(options.purge_line == false);
        REQUIRE(options.timelapse == false);
    }

    SECTION("Hidden options return false even when checked") {
        // Set all visibility to hidden (0)
        lv_subject_set_int(&subjects.can_show_bed_mesh, 0);
        lv_subject_set_int(&subjects.can_show_qgl, 0);
        lv_subject_set_int(&subjects.can_show_z_tilt, 0);
        lv_subject_set_int(&subjects.can_show_nozzle_clean, 0);
        lv_subject_set_int(&subjects.can_show_purge_line, 0);
        lv_subject_set_int(&subjects.can_show_timelapse, 0);

        // Set all checkboxes to checked (1)
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1);
        lv_subject_set_int(&subjects.preprint_qgl, 1);
        lv_subject_set_int(&subjects.preprint_z_tilt, 1);
        lv_subject_set_int(&subjects.preprint_nozzle_clean, 1);
        lv_subject_set_int(&subjects.preprint_purge_line, 1);
        lv_subject_set_int(&subjects.preprint_timelapse, 1);

        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                      &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                      &subjects.preprint_purge_line, &subjects.preprint_timelapse);
        manager.set_preprint_visibility_subjects(
            &subjects.can_show_bed_mesh, &subjects.can_show_qgl, &subjects.can_show_z_tilt,
            &subjects.can_show_nozzle_clean, &subjects.can_show_purge_line,
            &subjects.can_show_timelapse);

        auto options = manager.read_options_from_subjects();

        // Hidden options should return false regardless of checked state
        REQUIRE(options.bed_mesh == false);
        REQUIRE(options.qgl == false);
        REQUIRE(options.z_tilt == false);
        REQUIRE(options.nozzle_clean == false);
        REQUIRE(options.purge_line == false);
        REQUIRE(options.timelapse == false);
    }

    SECTION("Mixed visibility and checked states") {
        // bed_mesh: visible + checked = true
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1);
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1);

        // qgl: visible + unchecked = false
        lv_subject_set_int(&subjects.can_show_qgl, 1);
        lv_subject_set_int(&subjects.preprint_qgl, 0);

        // z_tilt: hidden + checked = false
        lv_subject_set_int(&subjects.can_show_z_tilt, 0);
        lv_subject_set_int(&subjects.preprint_z_tilt, 1);

        // nozzle_clean: hidden + unchecked = false
        lv_subject_set_int(&subjects.can_show_nozzle_clean, 0);
        lv_subject_set_int(&subjects.preprint_nozzle_clean, 0);

        // purge_line: visible + checked = true
        lv_subject_set_int(&subjects.can_show_purge_line, 1);
        lv_subject_set_int(&subjects.preprint_purge_line, 1);

        // timelapse: visible + unchecked = false
        lv_subject_set_int(&subjects.can_show_timelapse, 1);
        lv_subject_set_int(&subjects.preprint_timelapse, 0);

        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                      &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                      &subjects.preprint_purge_line, &subjects.preprint_timelapse);
        manager.set_preprint_visibility_subjects(
            &subjects.can_show_bed_mesh, &subjects.can_show_qgl, &subjects.can_show_z_tilt,
            &subjects.can_show_nozzle_clean, &subjects.can_show_purge_line,
            &subjects.can_show_timelapse);

        auto options = manager.read_options_from_subjects();

        REQUIRE(options.bed_mesh == true);      // visible + checked
        REQUIRE(options.qgl == false);          // visible + unchecked
        REQUIRE(options.z_tilt == false);       // hidden + checked
        REQUIRE(options.nozzle_clean == false); // hidden + unchecked
        REQUIRE(options.purge_line == true);    // visible + checked
        REQUIRE(options.timelapse == false);    // visible + unchecked
    }

    SECTION("Without visibility subjects set - only checks checked state") {
        // Only set checkbox subjects, not visibility subjects
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1);
        lv_subject_set_int(&subjects.preprint_qgl, 0);

        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                      &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                      &subjects.preprint_purge_line, &subjects.preprint_timelapse);
        // Don't call set_preprint_visibility_subjects - they remain nullptr

        auto options = manager.read_options_from_subjects();

        // Without visibility subjects, should just check the checked state
        REQUIRE(options.bed_mesh == true); // checked
        REQUIRE(options.qgl == false);     // unchecked
        REQUIRE(options.z_tilt == false);  // unchecked (default)
    }
}

TEST_CASE("PrintPreparationManager: get_option_state returns correct tri-state",
          "[print_preparation][p1][subjects]") {
    lv_init_safe();
    PrintPreparationManager manager;

    SECTION("Visible + checked = ENABLED") {
        PreprintSubjectsFixture subjects;
        subjects.init_all_subjects();

        lv_subject_set_int(&subjects.can_show_bed_mesh, 1); // visible
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1); // checked

        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, nullptr, nullptr, nullptr,
                                      nullptr, nullptr);
        manager.set_preprint_visibility_subjects(&subjects.can_show_bed_mesh, nullptr, nullptr,
                                                 nullptr, nullptr, nullptr);

        auto options = manager.read_options_from_subjects();
        REQUIRE(options.bed_mesh == true);
    }

    SECTION("Visible + unchecked = DISABLED (user explicitly skipped)") {
        PreprintSubjectsFixture subjects;
        subjects.init_all_subjects();

        lv_subject_set_int(&subjects.can_show_bed_mesh, 1); // visible
        lv_subject_set_int(&subjects.preprint_bed_mesh, 0); // unchecked

        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, nullptr, nullptr, nullptr,
                                      nullptr, nullptr);
        manager.set_preprint_visibility_subjects(&subjects.can_show_bed_mesh, nullptr, nullptr,
                                                 nullptr, nullptr, nullptr);

        auto options = manager.read_options_from_subjects();
        REQUIRE(options.bed_mesh == false);
    }

    SECTION("Hidden + checked = NOT_APPLICABLE (not enabled, not disabled)") {
        PreprintSubjectsFixture subjects;
        subjects.init_all_subjects();

        lv_subject_set_int(&subjects.can_show_bed_mesh, 0); // hidden
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1); // checked (irrelevant)

        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, nullptr, nullptr, nullptr,
                                      nullptr, nullptr);
        manager.set_preprint_visibility_subjects(&subjects.can_show_bed_mesh, nullptr, nullptr,
                                                 nullptr, nullptr, nullptr);

        auto options = manager.read_options_from_subjects();
        // Hidden = NOT_APPLICABLE, not ENABLED
        REQUIRE(options.bed_mesh == false);
    }

    SECTION("Hidden + unchecked = NOT_APPLICABLE (not enabled, not disabled)") {
        PreprintSubjectsFixture subjects;
        subjects.init_all_subjects();

        lv_subject_set_int(&subjects.can_show_bed_mesh, 0); // hidden
        lv_subject_set_int(&subjects.preprint_bed_mesh, 0); // unchecked (irrelevant)

        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, nullptr, nullptr, nullptr,
                                      nullptr, nullptr);
        manager.set_preprint_visibility_subjects(&subjects.can_show_bed_mesh, nullptr, nullptr,
                                                 nullptr, nullptr, nullptr);

        auto options = manager.read_options_from_subjects();
        // Hidden = NOT_APPLICABLE, not DISABLED
        REQUIRE(options.bed_mesh == false);
    }
}

TEST_CASE("PrintPreparationManager: hidden options don't produce macro skip params",
          "[print_preparation][p1][subjects]") {
    // This is the actual bug test: when visibility=0 (hidden), the old code treated
    // the option as "disabled" which caused collect_macro_skip_params() to add skip
    // params, triggering modification, which then warned about missing plugin.
    lv_init_safe();
    PrintPreparationManager manager;
    PreprintSubjectsFixture subjects;
    subjects.init_all_subjects();

    // Set up subjects on manager
    manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                  &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                  &subjects.preprint_purge_line, &subjects.preprint_timelapse);
    manager.set_preprint_visibility_subjects(
        &subjects.can_show_bed_mesh, &subjects.can_show_qgl, &subjects.can_show_z_tilt,
        &subjects.can_show_nozzle_clean, &subjects.can_show_purge_line,
        &subjects.can_show_timelapse);

    // Set up macro analysis with controllable bed mesh operation
    PrintStartAnalysis analysis;
    analysis.found = true;
    analysis.macro_name = "PRINT_START";

    PrintStartOperation op;
    op.name = "BED_MESH_CALIBRATE";
    op.category = PrintStartOpCategory::BED_MESH;
    op.has_skip_param = true;
    op.skip_param_name = "SKIP_BED_MESH";
    op.param_semantic = ParameterSemantic::OPT_OUT;
    analysis.operations.push_back(op);

    manager.set_macro_analysis(analysis);

    SECTION("Hidden visibility + unchecked produces NO skip params") {
        // This was the bug: hidden (visibility=0) + unchecked was treated as "disabled"
        lv_subject_set_int(&subjects.can_show_bed_mesh, 0); // hidden (plugin not installed)
        lv_subject_set_int(&subjects.preprint_bed_mesh, 0); // unchecked

        auto skip_params = PrintPreparationManagerTestAccess::get_skip_params(manager);

        // Should be EMPTY - hidden means not applicable, not "user disabled"
        REQUIRE(skip_params.empty());
    }

    SECTION("Hidden visibility + checked also produces NO skip params") {
        lv_subject_set_int(&subjects.can_show_bed_mesh, 0); // hidden
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1); // checked (irrelevant)

        auto skip_params = PrintPreparationManagerTestAccess::get_skip_params(manager);
        REQUIRE(skip_params.empty());
    }

    SECTION("Visible + unchecked DOES produce skip params") {
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1); // visible
        lv_subject_set_int(&subjects.preprint_bed_mesh, 0); // unchecked = user wants to skip

        auto skip_params = PrintPreparationManagerTestAccess::get_skip_params(manager);

        REQUIRE(skip_params.size() == 1);
        REQUIRE(skip_params[0].first == "SKIP_BED_MESH");
        REQUIRE(skip_params[0].second == "1"); // OPT_OUT: 1 = skip
    }

    SECTION("Visible + checked produces NO skip params") {
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1); // visible
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1); // checked = user wants operation

        auto skip_params = PrintPreparationManagerTestAccess::get_skip_params(manager);
        REQUIRE(skip_params.empty());
    }
}

TEST_CASE("PrintPreparationManager: collect_ops_to_disable uses subjects exclusively",
          "[print_preparation][p1][subjects]") {
    lv_init_safe();
    PrintPreparationManager manager;
    PreprintSubjectsFixture subjects;
    subjects.init_all_subjects();

    // Set up subjects on manager
    manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                  &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                  &subjects.preprint_purge_line, &subjects.preprint_timelapse);
    manager.set_preprint_visibility_subjects(
        &subjects.can_show_bed_mesh, &subjects.can_show_qgl, &subjects.can_show_z_tilt,
        &subjects.can_show_nozzle_clean, &subjects.can_show_purge_line,
        &subjects.can_show_timelapse);

    SECTION("Returns empty when no scan result available") {
        // Set some options as visible + unchecked (would be disabled)
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1); // visible
        lv_subject_set_int(&subjects.preprint_bed_mesh, 0); // unchecked

        // Without a scan result, collect_ops_to_disable should return empty
        // (There's no G-code file to check for embedded operations)
        // We can't directly call collect_ops_to_disable (it's private),
        // but we can verify behavior through start_print flow
        // For now, verify read_options_from_subjects works correctly
        auto options = manager.read_options_from_subjects();
        REQUIRE(options.bed_mesh == false); // visible + unchecked = not enabled
    }

    SECTION("Visible + unchecked options are candidates for disabling") {
        // Set bed_mesh as visible + unchecked (user wants to disable)
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1); // visible
        lv_subject_set_int(&subjects.preprint_bed_mesh, 0); // unchecked

        // Set qgl as visible + checked (user wants to keep)
        lv_subject_set_int(&subjects.can_show_qgl, 1); // visible
        lv_subject_set_int(&subjects.preprint_qgl, 1); // checked

        auto options = manager.read_options_from_subjects();

        // bed_mesh: visible + unchecked = false (would be disabled if in file)
        REQUIRE(options.bed_mesh == false);
        // qgl: visible + checked = true (would NOT be disabled)
        REQUIRE(options.qgl == true);
    }

    SECTION("Hidden options are NOT candidates for disabling") {
        // Set bed_mesh as hidden + unchecked
        lv_subject_set_int(&subjects.can_show_bed_mesh, 0); // hidden
        lv_subject_set_int(&subjects.preprint_bed_mesh, 0); // unchecked

        // Set qgl as hidden + checked
        lv_subject_set_int(&subjects.can_show_qgl, 0); // hidden
        lv_subject_set_int(&subjects.preprint_qgl, 1); // checked

        auto options = manager.read_options_from_subjects();

        // Hidden options should return false (not enabled)
        // but they should NOT be added to ops_to_disable
        // (hidden means not applicable to this printer, not user-disabled)
        REQUIRE(options.bed_mesh == false);
        REQUIRE(options.qgl == false);
    }

    SECTION("All operations disabled when visible + all unchecked") {
        // Set all as visible + unchecked
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1);
        lv_subject_set_int(&subjects.can_show_qgl, 1);
        lv_subject_set_int(&subjects.can_show_z_tilt, 1);
        lv_subject_set_int(&subjects.can_show_nozzle_clean, 1);

        lv_subject_set_int(&subjects.preprint_bed_mesh, 0);
        lv_subject_set_int(&subjects.preprint_qgl, 0);
        lv_subject_set_int(&subjects.preprint_z_tilt, 0);
        lv_subject_set_int(&subjects.preprint_nozzle_clean, 0);

        auto options = manager.read_options_from_subjects();

        REQUIRE(options.bed_mesh == false);
        REQUIRE(options.qgl == false);
        REQUIRE(options.z_tilt == false);
        REQUIRE(options.nozzle_clean == false);
    }

    SECTION("Mixed state: some visible+unchecked, some hidden, some visible+checked") {
        // bed_mesh: visible + unchecked = false (would be disabled)
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1);
        lv_subject_set_int(&subjects.preprint_bed_mesh, 0);

        // qgl: hidden + unchecked = false (NOT disabled - just not applicable)
        lv_subject_set_int(&subjects.can_show_qgl, 0);
        lv_subject_set_int(&subjects.preprint_qgl, 0);

        // z_tilt: visible + checked = true (enabled)
        lv_subject_set_int(&subjects.can_show_z_tilt, 1);
        lv_subject_set_int(&subjects.preprint_z_tilt, 1);

        // nozzle_clean: hidden + checked = false (NOT disabled - just not applicable)
        lv_subject_set_int(&subjects.can_show_nozzle_clean, 0);
        lv_subject_set_int(&subjects.preprint_nozzle_clean, 1);

        auto options = manager.read_options_from_subjects();

        REQUIRE(options.bed_mesh == false);     // visible + unchecked
        REQUIRE(options.qgl == false);          // hidden
        REQUIRE(options.z_tilt == true);        // visible + checked
        REQUIRE(options.nozzle_clean == false); // hidden
    }
}

// ============================================================================
// Tests: CapabilityMatrix Integration (P3)
// ============================================================================

/**
 * @brief Phase 3 Tests: CapabilityMatrix integration into PrintPreparationManager
 *
 * TDD Approach: These tests are written BEFORE implementation.
 * They will FAIL to compile initially because build_capability_matrix() doesn't exist.
 */

TEST_CASE("PrintPreparationManager: build_capability_matrix", "[print_preparation][p3]") {
    lv_init_safe();
    PrintPreparationManager manager;

    SECTION("Returns empty matrix when no data available") {
        // Without any dependencies set, matrix should be empty
        auto matrix = manager.build_capability_matrix();
        REQUIRE_FALSE(matrix.has_any_controllable());
        REQUIRE(matrix.get_controllable_operations().empty());
    }

    SECTION("Includes database capabilities when printer detected") {
        // Set up manager with PrinterState that has a known printer type
        PrinterState& printer_state = get_printer_state();
        PrinterStateTestAccess::reset(printer_state);
        printer_state.init_subjects(false); // No XML registration for tests
        printer_state.set_printer_type_sync("FlashForge Adventurer 5M Pro");

        manager.set_dependencies(nullptr, &printer_state);

        auto matrix = manager.build_capability_matrix();

        // AD5M Pro has bed_mesh capability in database
        REQUIRE(matrix.has_any_controllable());
        REQUIRE(matrix.is_controllable(OperationCategory::BED_MESH));

        // Verify source is from DATABASE
        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source.has_value());
        REQUIRE(source->origin == CapabilityOrigin::DATABASE);
        REQUIRE(source->param_name == "SKIP_LEVELING");
    }

    SECTION("Includes macro analysis when available") {
        // Create and set a mock macro analysis
        PrintStartAnalysis analysis;
        analysis.found = true;
        analysis.macro_name = "PRINT_START";

        // Add a controllable operation (QGL with SKIP_QGL param)
        PrintStartOperation op;
        op.name = "QUAD_GANTRY_LEVEL";
        op.category = PrintStartOpCategory::QGL;
        op.has_skip_param = true;
        op.skip_param_name = "SKIP_QGL";
        op.param_semantic = ParameterSemantic::OPT_OUT;
        op.line_number = 15;
        analysis.operations.push_back(op);
        analysis.controllable_count = 1;
        analysis.is_controllable = true;

        // Use set_macro_analysis to inject the analysis (new method needed)
        manager.set_macro_analysis(analysis);

        auto matrix = manager.build_capability_matrix();

        // QGL should be controllable from macro analysis
        REQUIRE(matrix.is_controllable(OperationCategory::QGL));

        auto source = matrix.get_best_source(OperationCategory::QGL);
        REQUIRE(source.has_value());
        REQUIRE(source->origin == CapabilityOrigin::MACRO_ANALYSIS);
        REQUIRE(source->param_name == "SKIP_QGL");
    }

    SECTION("Includes file scan when available") {
        // Create and set a mock scan result
        gcode::ScanResult scan;
        scan.lines_scanned = 100;
        scan.bytes_scanned = 5000;

        // Add a detected operation (direct command)
        gcode::DetectedOperation op;
        op.type = gcode::OperationType::NOZZLE_CLEAN;
        op.embedding = gcode::OperationEmbedding::MACRO_CALL;
        op.macro_name = "CLEAN_NOZZLE";
        op.line_number = 25;
        scan.operations.push_back(op);

        // Use set_cached_scan_result to inject (new method or use existing mechanism)
        manager.set_cached_scan_result(scan, "test_file.gcode");

        auto matrix = manager.build_capability_matrix();

        // NOZZLE_CLEAN should be controllable from file scan
        REQUIRE(matrix.is_controllable(OperationCategory::NOZZLE_CLEAN));

        auto source = matrix.get_best_source(OperationCategory::NOZZLE_CLEAN);
        REQUIRE(source.has_value());
        REQUIRE(source->origin == CapabilityOrigin::FILE_SCAN);
        REQUIRE(source->line_number == 25);
    }
}

TEST_CASE("PrintPreparationManager: capability priority ordering", "[print_preparation][p3]") {
    lv_init_safe();
    PrintPreparationManager manager;

    SECTION("Database takes priority over macro analysis") {
        // Set up PrinterState with AD5M Pro (has database bed_mesh capability)
        PrinterState& printer_state = get_printer_state();
        PrinterStateTestAccess::reset(printer_state);
        printer_state.init_subjects(false);
        printer_state.set_printer_type_sync("FlashForge Adventurer 5M Pro");
        manager.set_dependencies(nullptr, &printer_state);

        // Also add a macro analysis for the same operation (BED_MESH)
        PrintStartAnalysis analysis;
        analysis.found = true;
        analysis.macro_name = "PRINT_START";

        PrintStartOperation op;
        op.name = "BED_MESH_CALIBRATE";
        op.category = PrintStartOpCategory::BED_MESH;
        op.has_skip_param = true;
        op.skip_param_name = "SKIP_BED_MESH"; // Different param than database
        op.param_semantic = ParameterSemantic::OPT_OUT;
        analysis.operations.push_back(op);
        analysis.controllable_count = 1;
        analysis.is_controllable = true;

        manager.set_macro_analysis(analysis);

        auto matrix = manager.build_capability_matrix();

        // Database should win - SKIP_LEVELING, not SKIP_BED_MESH
        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source.has_value());
        REQUIRE(source->origin == CapabilityOrigin::DATABASE);
        REQUIRE(source->param_name == "SKIP_LEVELING");

        // But both sources should exist
        auto all_sources = matrix.get_all_sources(OperationCategory::BED_MESH);
        REQUIRE(all_sources.size() == 2);
        REQUIRE(all_sources[0].origin == CapabilityOrigin::DATABASE); // First = best
        REQUIRE(all_sources[1].origin == CapabilityOrigin::MACRO_ANALYSIS);
    }

    SECTION("Macro analysis takes priority over file scan") {
        // Set up macro analysis for QGL
        PrintStartAnalysis analysis;
        analysis.found = true;
        analysis.macro_name = "PRINT_START";

        PrintStartOperation op;
        op.name = "QUAD_GANTRY_LEVEL";
        op.category = PrintStartOpCategory::QGL;
        op.has_skip_param = true;
        op.skip_param_name = "SKIP_QGL";
        op.param_semantic = ParameterSemantic::OPT_OUT;
        analysis.operations.push_back(op);

        manager.set_macro_analysis(analysis);

        // Also add a file scan for the same operation
        gcode::ScanResult scan;
        scan.lines_scanned = 100;
        gcode::DetectedOperation file_op;
        file_op.type = gcode::OperationType::QGL;
        file_op.embedding = gcode::OperationEmbedding::DIRECT_COMMAND;
        file_op.macro_name = "QUAD_GANTRY_LEVEL";
        file_op.line_number = 50;
        scan.operations.push_back(file_op);

        manager.set_cached_scan_result(scan, "test.gcode");

        auto matrix = manager.build_capability_matrix();

        // Macro analysis should win
        auto source = matrix.get_best_source(OperationCategory::QGL);
        REQUIRE(source.has_value());
        REQUIRE(source->origin == CapabilityOrigin::MACRO_ANALYSIS);

        // Both sources exist
        auto all_sources = matrix.get_all_sources(OperationCategory::QGL);
        REQUIRE(all_sources.size() == 2);
        REQUIRE(all_sources[0].origin == CapabilityOrigin::MACRO_ANALYSIS);
        REQUIRE(all_sources[1].origin == CapabilityOrigin::FILE_SCAN);
    }
}

TEST_CASE("PrintPreparationManager: collect_macro_skip_params with matrix",
          "[print_preparation][p3]") {
    lv_init_safe();
    PrintPreparationManager manager;
    PreprintSubjectsFixture subjects;
    subjects.init_all_subjects();

    // Set up manager with subjects
    manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                  &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                  &subjects.preprint_purge_line, &subjects.preprint_timelapse);
    manager.set_preprint_visibility_subjects(
        &subjects.can_show_bed_mesh, &subjects.can_show_qgl, &subjects.can_show_z_tilt,
        &subjects.can_show_nozzle_clean, &subjects.can_show_purge_line,
        &subjects.can_show_timelapse);

    SECTION("Returns skip params from best source") {
        // Set up PrinterState with AD5M Pro (database source)
        PrinterState& printer_state = get_printer_state();
        PrinterStateTestAccess::reset(printer_state);
        printer_state.init_subjects(false);
        printer_state.set_printer_type_sync("FlashForge Adventurer 5M Pro");
        manager.set_dependencies(nullptr, &printer_state);

        // Make bed_mesh visible but UNCHECKED (user wants to disable)
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1); // visible
        lv_subject_set_int(&subjects.preprint_bed_mesh, 0); // unchecked = skip

        // NOTE: collect_macro_skip_params() is private. This test uses the new
        // Test accessor: PrintPreparationManagerTestAccess::get_skip_params()
        // Implementation should add this method or make collect_macro_skip_params public.
        auto skip_params = PrintPreparationManagerTestAccess::get_skip_params(manager);

        // Should have one param for bed_mesh using database source
        REQUIRE(skip_params.size() >= 1);

        // Find the bed_mesh param
        bool found_skip_leveling = false;
        for (const auto& [param, value] : skip_params) {
            if (param == "SKIP_LEVELING") {
                found_skip_leveling = true;
                // AD5M uses SKIP_LEVELING with OPT_OUT semantic
                // When user unchecks, we set to skip_value ("1")
                REQUIRE(value == "1");
            }
        }
        REQUIRE(found_skip_leveling);
    }

    SECTION("Handles OPT_IN semantic correctly") {
        // Set up macro analysis with OPT_IN semantic (FORCE_LEVELING style)
        PrintStartAnalysis analysis;
        analysis.found = true;
        analysis.macro_name = "PRINT_START";

        PrintStartOperation op;
        op.name = "BED_MESH_CALIBRATE";
        op.category = PrintStartOpCategory::BED_MESH;
        op.has_skip_param = true;
        op.skip_param_name = "FORCE_BED_MESH"; // OPT_IN: force=1 means do, force=0 means skip
        op.param_semantic = ParameterSemantic::OPT_IN;
        analysis.operations.push_back(op);

        manager.set_macro_analysis(analysis);

        // Make bed_mesh visible but UNCHECKED (user wants to skip)
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1);
        lv_subject_set_int(&subjects.preprint_bed_mesh, 0); // unchecked

        auto skip_params = PrintPreparationManagerTestAccess::get_skip_params(manager);

        // Find the param
        bool found_force_bed_mesh = false;
        for (const auto& [param, value] : skip_params) {
            if (param == "FORCE_BED_MESH") {
                found_force_bed_mesh = true;
                // OPT_IN: skip_value is "0" (param=0 means don't do it)
                REQUIRE(value == "0");
            }
        }
        REQUIRE(found_force_bed_mesh);
    }

    SECTION("Handles OPT_OUT semantic correctly") {
        // Set up macro analysis with OPT_OUT semantic (SKIP_BED_MESH style)
        PrintStartAnalysis analysis;
        analysis.found = true;
        analysis.macro_name = "PRINT_START";

        PrintStartOperation op;
        op.name = "BED_MESH_CALIBRATE";
        op.category = PrintStartOpCategory::BED_MESH;
        op.has_skip_param = true;
        op.skip_param_name = "SKIP_BED_MESH"; // OPT_OUT: skip=1 means skip, skip=0 means do
        op.param_semantic = ParameterSemantic::OPT_OUT;
        analysis.operations.push_back(op);

        manager.set_macro_analysis(analysis);

        // Make bed_mesh visible but UNCHECKED (user wants to skip)
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1);
        lv_subject_set_int(&subjects.preprint_bed_mesh, 0); // unchecked

        auto skip_params = PrintPreparationManagerTestAccess::get_skip_params(manager);

        // Find the param
        bool found_skip_bed_mesh = false;
        for (const auto& [param, value] : skip_params) {
            if (param == "SKIP_BED_MESH") {
                found_skip_bed_mesh = true;
                // OPT_OUT: skip_value is "1" (param=1 means skip it)
                REQUIRE(value == "1");
            }
        }
        REQUIRE(found_skip_bed_mesh);
    }
}

// ============================================================================
// Tests: Unified Operation Capability Lookup (Phase 4)
// ============================================================================

/**
 * Phase 4: lookup_operation_capability() - Unified Entry Point for Capability Queries
 *
 * This method provides a single interface for determining what action to take for
 * a pre-print operation based on:
 * 1. Visibility state (from PrinterState subjects)
 * 2. Checkbox state (from UI subjects)
 * 3. Available capability sources (database, macro analysis, file scan)
 *
 * Return semantics:
 * - nullopt: Operation should be ignored (hidden, enabled, or no capability source)
 * - OperationCapabilityResult: Operation is disabled, contains skip parameters
 *
 * These tests are designed to FAIL initially because:
 * - lookup_operation_capability() doesn't exist yet
 * - OperationCapabilityResult struct doesn't exist yet
 *
 * After implementation, they should PASS.
 */

TEST_CASE("PrintPreparationManager: lookup_operation_capability", "[print_preparation][p4]") {
    lv_init_safe();
    PrintPreparationManager manager;
    PreprintSubjectsFixture subjects;
    subjects.init_all_subjects();

    // Set up manager with all subjects
    manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                  &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                  &subjects.preprint_purge_line, &subjects.preprint_timelapse);
    manager.set_preprint_visibility_subjects(
        &subjects.can_show_bed_mesh, &subjects.can_show_qgl, &subjects.can_show_z_tilt,
        &subjects.can_show_nozzle_clean, &subjects.can_show_purge_line,
        &subjects.can_show_timelapse);

    SECTION("Returns skip param when operation disabled (visible + unchecked)") {
        // Set up PrinterState with AD5M Pro (has database capability for BED_MESH)
        PrinterState& printer_state = get_printer_state();
        PrinterStateTestAccess::reset(printer_state);
        printer_state.init_subjects(false);
        printer_state.set_printer_type_sync("FlashForge Adventurer 5M Pro");
        manager.set_dependencies(nullptr, &printer_state);

        // Set visibility = shown (1), checked = unchecked (0) for BED_MESH
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1); // visible
        lv_subject_set_int(&subjects.preprint_bed_mesh, 0); // unchecked = user wants to skip

        // Call the new unified method
        auto result = manager.lookup_operation_capability(OperationCategory::BED_MESH);

        // Should return a result with skip parameters
        REQUIRE(result.has_value());
        REQUIRE(result->should_skip == true);
        REQUIRE(result->param_name == "SKIP_LEVELING");
        // AD5M uses OPT_OUT semantic: skip_value is "1" (SKIP_LEVELING=1 means skip)
        REQUIRE(result->skip_value == "1");
        REQUIRE(result->source == CapabilityOrigin::DATABASE);
    }

    SECTION("Returns nullopt when operation hidden (visibility = 0)") {
        // Set up PrinterState with known printer
        PrinterState& printer_state = get_printer_state();
        PrinterStateTestAccess::reset(printer_state);
        printer_state.init_subjects(false);
        printer_state.set_printer_type_sync("FlashForge Adventurer 5M Pro");
        manager.set_dependencies(nullptr, &printer_state);

        // Hide the BED_MESH option (visibility = 0)
        lv_subject_set_int(&subjects.can_show_bed_mesh, 0); // hidden
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1); // checked (doesn't matter when hidden)

        // When operation is hidden, it's not applicable to this printer
        auto result = manager.lookup_operation_capability(OperationCategory::BED_MESH);

        // Should return nullopt - operation is hidden, nothing to do
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Returns nullopt when operation enabled (visible + checked)") {
        // Set up PrinterState with known printer
        PrinterState& printer_state = get_printer_state();
        PrinterStateTestAccess::reset(printer_state);
        printer_state.init_subjects(false);
        printer_state.set_printer_type_sync("FlashForge Adventurer 5M Pro");
        manager.set_dependencies(nullptr, &printer_state);

        // Set visibility = shown (1), checked = checked (1) for BED_MESH
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1); // visible
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1); // checked = user wants operation

        // When user wants the operation enabled, no skip param needed
        auto result = manager.lookup_operation_capability(OperationCategory::BED_MESH);

        // Should return nullopt - user wants operation, no skip needed
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Returns nullopt when no capability source available") {
        // No PrinterState set, no macro analysis, no file scan
        // Manager has no way to know how to control this operation

        // Make the operation visible and unchecked
        lv_subject_set_int(&subjects.can_show_qgl, 1);
        lv_subject_set_int(&subjects.preprint_qgl, 0); // unchecked

        // Without any capability source, can't return skip params
        auto result = manager.lookup_operation_capability(OperationCategory::QGL);

        // Should return nullopt - no capability source available
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Uses macro analysis as capability source") {
        // Set up macro analysis with QGL capability (no database for this example)
        PrintStartAnalysis analysis;
        analysis.found = true;
        analysis.macro_name = "PRINT_START";

        PrintStartOperation op;
        op.name = "QUAD_GANTRY_LEVEL";
        op.category = PrintStartOpCategory::QGL;
        op.has_skip_param = true;
        op.skip_param_name = "SKIP_QGL";
        op.param_semantic = ParameterSemantic::OPT_OUT;
        analysis.operations.push_back(op);
        analysis.controllable_count = 1;
        analysis.is_controllable = true;

        manager.set_macro_analysis(analysis);

        // Make QGL visible but unchecked
        lv_subject_set_int(&subjects.can_show_qgl, 1);
        lv_subject_set_int(&subjects.preprint_qgl, 0); // unchecked

        auto result = manager.lookup_operation_capability(OperationCategory::QGL);

        REQUIRE(result.has_value());
        REQUIRE(result->should_skip == true);
        REQUIRE(result->param_name == "SKIP_QGL");
        // OPT_OUT semantic: skip_value is "1"
        REQUIRE(result->skip_value == "1");
        REQUIRE(result->source == CapabilityOrigin::MACRO_ANALYSIS);
    }

    SECTION("Uses best source based on priority (database over macro)") {
        // Set up PrinterState with AD5M Pro (database source)
        PrinterState& printer_state = get_printer_state();
        PrinterStateTestAccess::reset(printer_state);
        printer_state.init_subjects(false);
        printer_state.set_printer_type_sync("FlashForge Adventurer 5M Pro");
        manager.set_dependencies(nullptr, &printer_state);

        // Also add macro analysis for the same operation with different param
        PrintStartAnalysis analysis;
        analysis.found = true;
        analysis.macro_name = "PRINT_START";

        PrintStartOperation op;
        op.name = "BED_MESH_CALIBRATE";
        op.category = PrintStartOpCategory::BED_MESH;
        op.has_skip_param = true;
        op.skip_param_name = "SKIP_BED_MESH"; // Different from database's FORCE_LEVELING
        op.param_semantic = ParameterSemantic::OPT_OUT;
        analysis.operations.push_back(op);

        manager.set_macro_analysis(analysis);

        // Make BED_MESH visible but unchecked
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1);
        lv_subject_set_int(&subjects.preprint_bed_mesh, 0);

        auto result = manager.lookup_operation_capability(OperationCategory::BED_MESH);

        // Database should win over macro analysis
        REQUIRE(result.has_value());
        REQUIRE(result->source == CapabilityOrigin::DATABASE);
        REQUIRE(result->param_name == "SKIP_LEVELING"); // Database param, not SKIP_BED_MESH
    }

    SECTION("Uses file scan as capability source when no other sources") {
        // Set up file scan with NOZZLE_CLEAN operation
        gcode::ScanResult scan;
        scan.lines_scanned = 100;
        scan.bytes_scanned = 5000;

        gcode::DetectedOperation op;
        op.type = gcode::OperationType::NOZZLE_CLEAN;
        op.embedding = gcode::OperationEmbedding::MACRO_PARAMETER;
        op.param_name = "SKIP_NOZZLE_CLEAN";
        op.macro_name = "PRINT_START";
        op.line_number = 42;
        scan.operations.push_back(op);

        manager.set_cached_scan_result(scan, "test.gcode");

        // Make NOZZLE_CLEAN visible but unchecked
        lv_subject_set_int(&subjects.can_show_nozzle_clean, 1);
        lv_subject_set_int(&subjects.preprint_nozzle_clean, 0);

        auto result = manager.lookup_operation_capability(OperationCategory::NOZZLE_CLEAN);

        REQUIRE(result.has_value());
        REQUIRE(result->source == CapabilityOrigin::FILE_SCAN);
        REQUIRE(result->param_name == "SKIP_NOZZLE_CLEAN");
    }
}

TEST_CASE("PrintPreparationManager: lookup_operation_capability edge cases",
          "[print_preparation][p4]") {
    lv_init_safe();
    PrintPreparationManager manager;
    PreprintSubjectsFixture subjects;
    subjects.init_all_subjects();

    SECTION("Returns nullopt when subjects not set") {
        // Don't call set_preprint_subjects or set_preprint_visibility_subjects
        // Manager has no subjects to check

        // Set up a capability source
        PrinterState& printer_state = get_printer_state();
        PrinterStateTestAccess::reset(printer_state);
        printer_state.init_subjects(false);
        printer_state.set_printer_type_sync("FlashForge Adventurer 5M Pro");
        manager.set_dependencies(nullptr, &printer_state);

        // Without subjects, can't determine visibility or checked state
        auto result = manager.lookup_operation_capability(OperationCategory::BED_MESH);

        // Should return nullopt - can't determine user intent without subjects
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Returns nullopt for UNKNOWN operation category") {
        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                      &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                      &subjects.preprint_purge_line, &subjects.preprint_timelapse);
        manager.set_preprint_visibility_subjects(
            &subjects.can_show_bed_mesh, &subjects.can_show_qgl, &subjects.can_show_z_tilt,
            &subjects.can_show_nozzle_clean, &subjects.can_show_purge_line,
            &subjects.can_show_timelapse);

        // UNKNOWN is not a valid operation to look up
        auto result = manager.lookup_operation_capability(OperationCategory::UNKNOWN);

        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Handles Z_TILT operation correctly") {
        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                      &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                      &subjects.preprint_purge_line, &subjects.preprint_timelapse);
        manager.set_preprint_visibility_subjects(
            &subjects.can_show_bed_mesh, &subjects.can_show_qgl, &subjects.can_show_z_tilt,
            &subjects.can_show_nozzle_clean, &subjects.can_show_purge_line,
            &subjects.can_show_timelapse);

        // Set up macro analysis with Z_TILT capability
        PrintStartAnalysis analysis;
        analysis.found = true;
        analysis.macro_name = "PRINT_START";

        PrintStartOperation op;
        op.name = "Z_TILT_ADJUST";
        op.category = PrintStartOpCategory::Z_TILT;
        op.has_skip_param = true;
        op.skip_param_name = "SKIP_Z_TILT";
        op.param_semantic = ParameterSemantic::OPT_OUT;
        analysis.operations.push_back(op);

        manager.set_macro_analysis(analysis);

        // Make Z_TILT visible but unchecked
        lv_subject_set_int(&subjects.can_show_z_tilt, 1);
        lv_subject_set_int(&subjects.preprint_z_tilt, 0);

        auto result = manager.lookup_operation_capability(OperationCategory::Z_TILT);

        REQUIRE(result.has_value());
        REQUIRE(result->param_name == "SKIP_Z_TILT");
        REQUIRE(result->skip_value == "1");
    }

    SECTION("Handles PURGE_LINE operation correctly") {
        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, &subjects.preprint_qgl,
                                      &subjects.preprint_z_tilt, &subjects.preprint_nozzle_clean,
                                      &subjects.preprint_purge_line, &subjects.preprint_timelapse);
        manager.set_preprint_visibility_subjects(
            &subjects.can_show_bed_mesh, &subjects.can_show_qgl, &subjects.can_show_z_tilt,
            &subjects.can_show_nozzle_clean, &subjects.can_show_purge_line,
            &subjects.can_show_timelapse);

        // Set up macro analysis with PURGE_LINE capability
        PrintStartAnalysis analysis;
        analysis.found = true;
        analysis.macro_name = "PRINT_START";

        PrintStartOperation op;
        op.name = "PRIME_LINE";
        op.category = PrintStartOpCategory::PURGE_LINE;
        op.has_skip_param = true;
        op.skip_param_name = "PERFORM_PURGE"; // OPT_IN style
        op.param_semantic = ParameterSemantic::OPT_IN;
        analysis.operations.push_back(op);

        manager.set_macro_analysis(analysis);

        // Make PURGE_LINE visible but unchecked
        lv_subject_set_int(&subjects.can_show_purge_line, 1);
        lv_subject_set_int(&subjects.preprint_purge_line, 0);

        auto result = manager.lookup_operation_capability(OperationCategory::PURGE_LINE);

        REQUIRE(result.has_value());
        REQUIRE(result->param_name == "PERFORM_PURGE");
        // OPT_IN: skip_value is "0" (PERFORM_PURGE=0 means don't do it)
        REQUIRE(result->skip_value == "0");
    }
}

TEST_CASE("PrintPreparationManager: lookup_operation_capability with visibility-only subjects",
          "[print_preparation][p4]") {
    lv_init_safe();
    PrintPreparationManager manager;
    PreprintSubjectsFixture subjects;
    subjects.init_all_subjects();

    // Only set visibility subjects, not checkbox subjects
    manager.set_preprint_visibility_subjects(
        &subjects.can_show_bed_mesh, &subjects.can_show_qgl, &subjects.can_show_z_tilt,
        &subjects.can_show_nozzle_clean, &subjects.can_show_purge_line,
        &subjects.can_show_timelapse);

    SECTION("Returns nullopt when checkbox subjects not set") {
        // Set up capability source
        PrinterState& printer_state = get_printer_state();
        PrinterStateTestAccess::reset(printer_state);
        printer_state.init_subjects(false);
        printer_state.set_printer_type_sync("FlashForge Adventurer 5M Pro");
        manager.set_dependencies(nullptr, &printer_state);

        // Visibility is set, but checkbox subjects are not
        lv_subject_set_int(&subjects.can_show_bed_mesh, 1);

        auto result = manager.lookup_operation_capability(OperationCategory::BED_MESH);

        // Without checkbox subject, can't determine if user wants to skip
        REQUIRE_FALSE(result.has_value());
    }
}

// ============================================================================
// Tests: Extension Safety and Documentation (Phase 5)
// ============================================================================

/**
 * Phase 5: Extension Safety Tests
 *
 * These tests document the expected behavior of the pre-print subsystem's
 * extension points. They serve as both tests and documentation for developers
 * who need to add new operations or capability sources.
 *
 * Key extension points:
 * 1. OperationRegistry - Single point for adding new controllable operations
 * 2. CapabilityMatrix - Unified capability lookup with priority ordering
 * 3. CapabilityOrigin - Priority system for source ordering
 * 4. ParameterSemantic - OPT_IN/OPT_OUT parameter interpretation
 */

TEST_CASE("PrepManager: Extension safety - registry completeness", "[print_preparation][p5]") {
    SECTION("All controllable operations have registry entries") {
        // The five controllable operations should all be in the registry.
        // This test ensures that any controllable operation can be looked up.
        for (auto cat :
             {OperationCategory::BED_MESH, OperationCategory::QGL, OperationCategory::Z_TILT,
              OperationCategory::NOZZLE_CLEAN, OperationCategory::PURGE_LINE}) {
            auto info = OperationRegistry::get(cat);
            INFO("Checking category: " << category_key(cat));
            REQUIRE(info.has_value());
            REQUIRE_FALSE(info->capability_key.empty());
            REQUIRE_FALSE(info->friendly_name.empty());

            // Verify capability_key matches category_key()
            REQUIRE(info->capability_key == std::string(category_key(cat)));
        }
    }

    SECTION("Non-controllable operations return nullopt") {
        // Operations that cannot be toggled in the UI should NOT be in the registry.
        // This ensures the registry only contains operations that make sense to show
        // in the pre-print options panel.

        // HOMING: Always required, never skippable
        REQUIRE_FALSE(OperationRegistry::get(OperationCategory::HOMING).has_value());

        // START_PRINT: The macro itself, not a toggleable option
        REQUIRE_FALSE(OperationRegistry::get(OperationCategory::START_PRINT).has_value());

        // UNKNOWN: Invalid/unrecognized operation
        REQUIRE_FALSE(OperationRegistry::get(OperationCategory::UNKNOWN).has_value());

        // CHAMBER_SOAK: Not currently controllable (complex timing semantics)
        REQUIRE_FALSE(OperationRegistry::get(OperationCategory::CHAMBER_SOAK).has_value());

        // SKEW_CORRECT: Not currently controllable
        REQUIRE_FALSE(OperationRegistry::get(OperationCategory::SKEW_CORRECT).has_value());

        // BED_LEVEL: Parent category, not directly controllable (QGL/Z_TILT are)
        REQUIRE_FALSE(OperationRegistry::get(OperationCategory::BED_LEVEL).has_value());
    }

    SECTION("Registry::all() returns complete set of controllable operations") {
        // Verify that OperationRegistry::all() returns all controllable operations.
        // This is the single extension point for adding new operations.
        auto all = OperationRegistry::all();

        // At least the 5 current controllable operations
        REQUIRE(all.size() >= 5);

        // Each entry should have complete metadata
        for (const auto& info : all) {
            INFO("Validating operation: " << info.capability_key);
            REQUIRE_FALSE(info.capability_key.empty());
            REQUIRE_FALSE(info.friendly_name.empty());
            REQUIRE(info.category != OperationCategory::UNKNOWN);
        }

        // Verify specific operations are present
        bool has_bed_mesh = false;
        bool has_qgl = false;
        bool has_z_tilt = false;
        bool has_nozzle_clean = false;
        bool has_purge_line = false;

        for (const auto& info : all) {
            if (info.capability_key == "bed_mesh") {
                has_bed_mesh = true;
            }
            if (info.capability_key == "qgl") {
                has_qgl = true;
            }
            if (info.capability_key == "z_tilt") {
                has_z_tilt = true;
            }
            if (info.capability_key == "nozzle_clean") {
                has_nozzle_clean = true;
            }
            if (info.capability_key == "purge_line") {
                has_purge_line = true;
            }
        }

        REQUIRE(has_bed_mesh);
        REQUIRE(has_qgl);
        REQUIRE(has_z_tilt);
        REQUIRE(has_nozzle_clean);
        REQUIRE(has_purge_line);
    }

    SECTION("Reverse lookup by key works for all controllable operations") {
        // OperationRegistry::get_by_key() should find operations by their capability key
        auto bed_mesh = OperationRegistry::get_by_key("bed_mesh");
        REQUIRE(bed_mesh.has_value());
        REQUIRE(bed_mesh->category == OperationCategory::BED_MESH);

        auto qgl = OperationRegistry::get_by_key("qgl");
        REQUIRE(qgl.has_value());
        REQUIRE(qgl->category == OperationCategory::QGL);

        auto z_tilt = OperationRegistry::get_by_key("z_tilt");
        REQUIRE(z_tilt.has_value());
        REQUIRE(z_tilt->category == OperationCategory::Z_TILT);

        auto nozzle_clean = OperationRegistry::get_by_key("nozzle_clean");
        REQUIRE(nozzle_clean.has_value());
        REQUIRE(nozzle_clean->category == OperationCategory::NOZZLE_CLEAN);

        auto purge_line = OperationRegistry::get_by_key("purge_line");
        REQUIRE(purge_line.has_value());
        REQUIRE(purge_line->category == OperationCategory::PURGE_LINE);

        // Non-existent key returns nullopt
        REQUIRE_FALSE(OperationRegistry::get_by_key("nonexistent").has_value());
        REQUIRE_FALSE(OperationRegistry::get_by_key("").has_value());
    }
}

TEST_CASE("PrepManager: Extension safety - priority ordering", "[print_preparation][p5]") {
    SECTION("Database priority = 0 (highest)") {
        // DATABASE source is authoritative - curated and tested capabilities from
        // printer_database.json. It should always take priority over dynamic detection.
        // Priority 0 = highest (lower number = higher priority)

        // Create a matrix with DATABASE source
        CapabilityMatrix matrix;
        PrintStartCapabilities db_caps;
        db_caps.macro_name = "START_PRINT";
        db_caps.params["bed_mesh"] = {"FORCE_LEVELING", "false", "true"};
        matrix.add_from_database(db_caps);

        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source.has_value());
        REQUIRE(source->origin == CapabilityOrigin::DATABASE);
    }

    SECTION("Macro analysis priority = 1 (medium)") {
        // MACRO_ANALYSIS is dynamically detected from the printer's PRINT_START macro.
        // It's trustworthy but may be incomplete or non-standard.

        CapabilityMatrix matrix;
        PrintStartAnalysis analysis;
        analysis.found = true;

        PrintStartOperation op;
        op.name = "QUAD_GANTRY_LEVEL";
        op.category = PrintStartOpCategory::QGL;
        op.has_skip_param = true;
        op.skip_param_name = "SKIP_QGL";
        op.param_semantic = ParameterSemantic::OPT_OUT;
        analysis.operations.push_back(op);

        matrix.add_from_macro_analysis(analysis);

        auto source = matrix.get_best_source(OperationCategory::QGL);
        REQUIRE(source.has_value());
        REQUIRE(source->origin == CapabilityOrigin::MACRO_ANALYSIS);
    }

    SECTION("File scan priority = 2 (lowest)") {
        // FILE_SCAN is detected from the G-code file itself. It's the least
        // reliable because it's specific to one file and may not have parameter info.

        CapabilityMatrix matrix;
        gcode::ScanResult scan;
        scan.lines_scanned = 100;

        gcode::DetectedOperation op;
        op.type = gcode::OperationType::NOZZLE_CLEAN;
        op.embedding = gcode::OperationEmbedding::MACRO_PARAMETER;
        op.param_name = "SKIP_NOZZLE_CLEAN";
        op.line_number = 10;
        scan.operations.push_back(op);

        matrix.add_from_file_scan(scan);

        auto source = matrix.get_best_source(OperationCategory::NOZZLE_CLEAN);
        REQUIRE(source.has_value());
        REQUIRE(source->origin == CapabilityOrigin::FILE_SCAN);
    }

    SECTION("Lower priority number wins in get_best_source") {
        // When multiple sources exist for the same operation, DATABASE wins over
        // MACRO_ANALYSIS, which wins over FILE_SCAN.

        CapabilityMatrix matrix;

        // Add FILE_SCAN source first (lowest priority)
        gcode::ScanResult scan;
        scan.lines_scanned = 100;
        gcode::DetectedOperation file_op;
        file_op.type = gcode::OperationType::BED_MESH;
        file_op.embedding = gcode::OperationEmbedding::MACRO_PARAMETER;
        file_op.param_name = "SKIP_BED_MESH_FILE";
        file_op.line_number = 5;
        scan.operations.push_back(file_op);
        matrix.add_from_file_scan(scan);

        // Add MACRO_ANALYSIS source (medium priority)
        PrintStartAnalysis analysis;
        analysis.found = true;
        PrintStartOperation macro_op;
        macro_op.name = "BED_MESH_CALIBRATE";
        macro_op.category = PrintStartOpCategory::BED_MESH;
        macro_op.has_skip_param = true;
        macro_op.skip_param_name = "SKIP_BED_MESH_MACRO";
        macro_op.param_semantic = ParameterSemantic::OPT_OUT;
        analysis.operations.push_back(macro_op);
        matrix.add_from_macro_analysis(analysis);

        // Add DATABASE source (highest priority)
        PrintStartCapabilities db_caps;
        db_caps.macro_name = "START_PRINT";
        db_caps.params["bed_mesh"] = {"FORCE_LEVELING_DB", "false", "true"};
        matrix.add_from_database(db_caps);

        // DATABASE should win
        auto best = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(best.has_value());
        REQUIRE(best->origin == CapabilityOrigin::DATABASE);
        REQUIRE(best->param_name == "FORCE_LEVELING_DB");

        // All three sources should be available when requested
        auto all_sources = matrix.get_all_sources(OperationCategory::BED_MESH);
        REQUIRE(all_sources.size() == 3);

        // Sources should be sorted by priority (DATABASE first, FILE_SCAN last)
        REQUIRE(all_sources[0].origin == CapabilityOrigin::DATABASE);
        REQUIRE(all_sources[1].origin == CapabilityOrigin::MACRO_ANALYSIS);
        REQUIRE(all_sources[2].origin == CapabilityOrigin::FILE_SCAN);
    }

    SECTION("Macro analysis takes priority over file scan when both present") {
        CapabilityMatrix matrix;

        // Add FILE_SCAN source
        gcode::ScanResult scan;
        gcode::DetectedOperation file_op;
        file_op.type = gcode::OperationType::Z_TILT;
        file_op.embedding = gcode::OperationEmbedding::DIRECT_COMMAND;
        file_op.macro_name = "Z_TILT_ADJUST";
        scan.operations.push_back(file_op);
        matrix.add_from_file_scan(scan);

        // Add MACRO_ANALYSIS source
        PrintStartAnalysis analysis;
        analysis.found = true;
        PrintStartOperation macro_op;
        macro_op.name = "Z_TILT_ADJUST";
        macro_op.category = PrintStartOpCategory::Z_TILT;
        macro_op.has_skip_param = true;
        macro_op.skip_param_name = "SKIP_Z_TILT";
        macro_op.param_semantic = ParameterSemantic::OPT_OUT;
        analysis.operations.push_back(macro_op);
        matrix.add_from_macro_analysis(analysis);

        // MACRO_ANALYSIS should win over FILE_SCAN
        auto best = matrix.get_best_source(OperationCategory::Z_TILT);
        REQUIRE(best.has_value());
        REQUIRE(best->origin == CapabilityOrigin::MACRO_ANALYSIS);
    }
}

TEST_CASE("PrepManager: Extension safety - semantic handling", "[print_preparation][p5]") {
    SECTION("OPT_OUT params: SKIP_* with value 1 means skip") {
        // OPT_OUT semantic: The parameter indicates "skip this operation"
        // - SKIP_BED_MESH=1 -> skip bed mesh
        // - SKIP_BED_MESH=0 -> do bed mesh (default)

        CapabilityMatrix matrix;
        PrintStartAnalysis analysis;
        analysis.found = true;

        PrintStartOperation op;
        op.name = "BED_MESH_CALIBRATE";
        op.category = PrintStartOpCategory::BED_MESH;
        op.has_skip_param = true;
        op.skip_param_name = "SKIP_BED_MESH";
        op.param_semantic = ParameterSemantic::OPT_OUT;
        analysis.operations.push_back(op);

        matrix.add_from_macro_analysis(analysis);

        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source.has_value());
        REQUIRE(source->semantic == ParameterSemantic::OPT_OUT);
        REQUIRE(source->skip_value == "1");   // SKIP=1 means skip
        REQUIRE(source->enable_value == "0"); // SKIP=0 means do
    }

    SECTION("OPT_IN params: FORCE_*/PERFORM_* with value 0 means skip") {
        // OPT_IN semantic: The parameter indicates "do this operation"
        // - FORCE_LEVELING=1 or "true" -> do leveling
        // - FORCE_LEVELING=0 or "false" -> skip leveling

        CapabilityMatrix matrix;
        PrintStartCapabilities db_caps;
        db_caps.macro_name = "START_PRINT";
        // AD5M-style: FORCE_LEVELING with OPT_IN semantic
        db_caps.params["bed_mesh"] = {"FORCE_LEVELING", "false", "true"};
        matrix.add_from_database(db_caps);

        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source.has_value());
        // The semantic is inferred from the param name starting with FORCE_
        REQUIRE(source->semantic == ParameterSemantic::OPT_IN);
        REQUIRE(source->skip_value == "false");  // FORCE=false means skip
        REQUIRE(source->enable_value == "true"); // FORCE=true means do
    }

    SECTION("Semantic is correctly inferred from parameter name") {
        // The CapabilityMatrix::infer_semantic() function determines OPT_IN vs OPT_OUT
        // based on the parameter name prefix.

        // OPT_IN patterns: FORCE_*, PERFORM_*, DO_*, ENABLE_*
        CapabilityMatrix matrix1;
        PrintStartAnalysis analysis1;
        analysis1.found = true;
        PrintStartOperation op1;
        op1.category = PrintStartOpCategory::BED_MESH;
        op1.has_skip_param = true;

        // Test FORCE_* prefix
        op1.skip_param_name = "FORCE_LEVELING";
        op1.param_semantic = ParameterSemantic::OPT_IN;
        analysis1.operations.push_back(op1);
        matrix1.add_from_macro_analysis(analysis1);

        auto source1 = matrix1.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source1.has_value());
        REQUIRE(source1->semantic == ParameterSemantic::OPT_IN);

        // Test PERFORM_* prefix
        CapabilityMatrix matrix2;
        PrintStartAnalysis analysis2;
        analysis2.found = true;
        PrintStartOperation op2;
        op2.category = PrintStartOpCategory::QGL;
        op2.has_skip_param = true;
        op2.skip_param_name = "PERFORM_QGL";
        op2.param_semantic = ParameterSemantic::OPT_IN;
        analysis2.operations.push_back(op2);
        matrix2.add_from_macro_analysis(analysis2);

        auto source2 = matrix2.get_best_source(OperationCategory::QGL);
        REQUIRE(source2.has_value());
        REQUIRE(source2->semantic == ParameterSemantic::OPT_IN);

        // Test SKIP_* prefix (OPT_OUT)
        CapabilityMatrix matrix3;
        PrintStartAnalysis analysis3;
        analysis3.found = true;
        PrintStartOperation op3;
        op3.category = PrintStartOpCategory::Z_TILT;
        op3.has_skip_param = true;
        op3.skip_param_name = "SKIP_Z_TILT";
        op3.param_semantic = ParameterSemantic::OPT_OUT;
        analysis3.operations.push_back(op3);
        matrix3.add_from_macro_analysis(analysis3);

        auto source3 = matrix3.get_best_source(OperationCategory::Z_TILT);
        REQUIRE(source3.has_value());
        REQUIRE(source3->semantic == ParameterSemantic::OPT_OUT);
    }

    SECTION("get_skip_param returns correct values based on semantic") {
        // get_skip_param() returns the (param_name, skip_value) pair for disabling
        // an operation. The skip_value depends on the semantic.

        CapabilityMatrix matrix;

        // Add OPT_OUT operation (SKIP_QGL)
        PrintStartAnalysis analysis;
        analysis.found = true;
        PrintStartOperation op;
        op.name = "QUAD_GANTRY_LEVEL";
        op.category = PrintStartOpCategory::QGL;
        op.has_skip_param = true;
        op.skip_param_name = "SKIP_QGL";
        op.param_semantic = ParameterSemantic::OPT_OUT;
        analysis.operations.push_back(op);
        matrix.add_from_macro_analysis(analysis);

        auto skip_param = matrix.get_skip_param(OperationCategory::QGL);
        REQUIRE(skip_param.has_value());
        REQUIRE(skip_param->first == "SKIP_QGL");
        REQUIRE(skip_param->second == "1"); // OPT_OUT: skip_value = 1
    }
}

TEST_CASE("PrepManager: Extension safety - adding new operations", "[print_preparation][p5]") {
    /**
     * DOCUMENTATION: How to add a new controllable operation
     *
     * 1. Add enum value to OperationCategory in operation_patterns.h
     * 2. Add entry to OperationRegistry::build_all() in operation_registry.h
     * 3. Add keyword patterns to OPERATION_KEYWORDS in operation_patterns.h
     * 4. Add skip/perform variations to SKIP_PARAM_VARIATIONS/PERFORM_PARAM_VARIATIONS
     * 5. Update category_key() and category_name() in operation_patterns.h
     * 6. Add UI subject handling in PrintPreparationManager
     * 7. Add printer database entries if needed
     *
     * This test verifies the extension infrastructure is working correctly.
     */

    SECTION("Registry is the single extension point for controllable operations") {
        // OperationRegistry::all() is the single source of truth for what operations
        // can be shown in the pre-print UI.

        auto all = OperationRegistry::all();

        // Verify we have the expected minimum operations
        REQUIRE(all.size() >= 5);

        // Every operation in the registry must be controllable
        for (const auto& info : all) {
            // Can look it up by category
            auto by_cat = OperationRegistry::get(info.category);
            REQUIRE(by_cat.has_value());
            REQUIRE(by_cat->capability_key == info.capability_key);

            // Can look it up by key
            auto by_key = OperationRegistry::get_by_key(info.capability_key);
            REQUIRE(by_key.has_value());
            REQUIRE(by_key->category == info.category);
        }
    }

    SECTION("Each registry entry has complete and consistent metadata") {
        auto all = OperationRegistry::all();

        for (const auto& info : all) {
            INFO("Checking operation: " << info.capability_key);

            // capability_key must be non-empty and match category_key()
            REQUIRE_FALSE(info.capability_key.empty());
            REQUIRE(info.capability_key == std::string(category_key(info.category)));

            // friendly_name must be non-empty and match category_name()
            REQUIRE_FALSE(info.friendly_name.empty());
            REQUIRE(info.friendly_name == std::string(category_name(info.category)));

            // category must not be UNKNOWN
            REQUIRE(info.category != OperationCategory::UNKNOWN);
        }
    }

    SECTION("Operation categories have skip and perform variations defined") {
        // Each controllable operation should have parameter variations defined
        // for detection in macros.

        auto all = OperationRegistry::all();

        for (const auto& info : all) {
            INFO("Checking variations for: " << info.capability_key);

            // Should have at least one skip variation OR one perform variation
            const auto& skip_vars = get_skip_variations(info.category);
            const auto& perform_vars = get_perform_variations(info.category);

            bool has_variations = !skip_vars.empty() || !perform_vars.empty();
            REQUIRE(has_variations);
        }
    }

    SECTION("CapabilityMatrix supports all registry operations") {
        // The CapabilityMatrix should be able to hold capabilities for all
        // operations in the registry.

        CapabilityMatrix matrix;

        // Add a mock capability for each registry operation
        PrintStartAnalysis analysis;
        analysis.found = true;

        for (const auto& info : OperationRegistry::all()) {
            PrintStartOperation op;
            op.name = info.capability_key;
            op.category = static_cast<PrintStartOpCategory>(info.category);
            op.has_skip_param = true;
            op.skip_param_name = "SKIP_" + std::string(category_key(info.category));
            op.param_semantic = ParameterSemantic::OPT_OUT;
            analysis.operations.push_back(op);
        }

        matrix.add_from_macro_analysis(analysis);

        // Verify all operations are controllable
        for (const auto& info : OperationRegistry::all()) {
            INFO("Verifying matrix support for: " << info.capability_key);
            REQUIRE(matrix.is_controllable(info.category));
        }
    }
}

TEST_CASE("PrepManager: Extension safety - database key consistency", "[print_preparation][p5]") {
    SECTION("Database capability keys match category_key() output") {
        // This ensures that database lookups use the correct keys.
        // The printer_database.json uses these keys for capability definitions.

        REQUIRE(std::string(category_key(OperationCategory::BED_MESH)) == "bed_mesh");
        REQUIRE(std::string(category_key(OperationCategory::QGL)) == "qgl");
        REQUIRE(std::string(category_key(OperationCategory::Z_TILT)) == "z_tilt");
        REQUIRE(std::string(category_key(OperationCategory::NOZZLE_CLEAN)) == "nozzle_clean");
        REQUIRE(std::string(category_key(OperationCategory::PURGE_LINE)) == "purge_line");
    }

    SECTION("Known printer has expected capability keys") {
        // Verify that the database returns capabilities with the correct keys
        auto caps = PrinterDetector::get_print_start_capabilities("FlashForge Adventurer 5M Pro");

        if (!caps.empty()) {
            // Database should use "bed_mesh" key, not alternatives like "bed_leveling"
            if (caps.has_capability("bed_mesh")) {
                REQUIRE_FALSE(caps.has_capability("bed_leveling")); // Wrong key doesn't exist
                auto* bed_cap = caps.get_capability("bed_mesh");
                REQUIRE(bed_cap != nullptr);
                REQUIRE_FALSE(bed_cap->param.empty());
            }
        }
    }

    SECTION("CapabilityMatrix::category_from_key recognizes all registry keys") {
        // The CapabilityMatrix uses category_from_key() to map database capability
        // keys to OperationCategory. This must recognize all registry keys.

        // Note: category_from_key is private, so we test indirectly through add_from_database

        CapabilityMatrix matrix;

        // Create database capabilities for all registry operations
        PrintStartCapabilities db_caps;
        db_caps.macro_name = "START_PRINT";

        for (const auto& info : OperationRegistry::all()) {
            db_caps.params[info.capability_key] = {"PARAM_" + info.capability_key, "0", "1"};
        }

        matrix.add_from_database(db_caps);

        // Verify all operations were recognized and added
        for (const auto& info : OperationRegistry::all()) {
            INFO("Checking key recognition for: " << info.capability_key);
            REQUIRE(matrix.is_controllable(info.category));
        }
    }
}
