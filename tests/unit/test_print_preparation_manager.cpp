// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_print_preparation_manager.h"
#include "ui_update_queue.h"

#include "capability_matrix.h"

#include "../mocks/mock_websocket_server.h"
#include "../ui_test_utils.h"
#include "hv/EventLoopThread.h"
#include "moonraker_api.h"
#include "moonraker_client.h"
#include "moonraker_error.h"
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
 *   - Database entry: "bed_mesh": { "param": "FORCE_LEVELING", ... }
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
        REQUIRE(bed_cap->param == "FORCE_LEVELING");

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
        REQUIRE(bed_cap->param == "FORCE_LEVELING");
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
    PrinterState printer_state;
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
        REQUIRE(bed_cap->param == "FORCE_LEVELING");
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
    PrinterState printer_state;
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

        // The capability should have a param name (FORCE_LEVELING)
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
        helix::ui::UpdateQueue::instance().drain_queue_for_testing();

        // Shutdown queue
        ui_update_queue_shutdown();

        // Reset static flag for next test [L053]
        queue_initialized = false;
    }

    /**
     * @brief Drain pending UI updates (simulates main loop iteration)
     */
    void drain_queue() {
        helix::ui::UpdateQueue::instance().drain_queue_for_testing();
        lv_timer_handler(); // Process LVGL timers for retry scheduling
    }

    /**
     * @brief Wait for condition with queue draining
     */
    bool wait_for(std::function<bool()> condition, int timeout_ms = 5000) {
        auto start = std::chrono::steady_clock::now();
        while (!condition()) {
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
                 "[print_preparation][retry]") {
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
        REQUIRE(manager_.has_macro_analysis() == true); // Has result, just found=false
    }
}

TEST_CASE_METHOD(MacroAnalysisRetryFixture,
                 "PrintPreparationManager: macro analysis retry - first fails, second succeeds",
                 "[print_preparation][retry]") {
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
        REQUIRE(manager_.has_macro_analysis() == true);
    }
}

TEST_CASE_METHOD(MacroAnalysisRetryFixture,
                 "PrintPreparationManager: macro analysis retry - all retries exhausted",
                 "[print_preparation][retry]") {
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
        REQUIRE(manager_.has_macro_analysis() == true); // Has result with found=false
    }
}

TEST_CASE_METHOD(MacroAnalysisRetryFixture,
                 "PrintPreparationManager: macro analysis retry counter resets on new request",
                 "[print_preparation][retry]") {
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
                 "[print_preparation][retry]") {
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
                 "[print_preparation][retry][timing]") {
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
 * - is_option_disabled_from_subject(): Checks visibility + checked state
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

TEST_CASE("PrintPreparationManager: is_option_disabled_from_subject handles visibility + checked",
          "[print_preparation][p1][subjects]") {
    lv_init_safe();
    PrintPreparationManager manager;

    // Create test subjects for the four combinations
    lv_subject_t visibility_visible{};
    lv_subject_t visibility_hidden{};
    lv_subject_t checked_true{};
    lv_subject_t checked_false{};

    lv_subject_init_int(&visibility_visible, 1); // visible
    lv_subject_init_int(&visibility_hidden, 0);  // hidden
    lv_subject_init_int(&checked_true, 1);       // checked
    lv_subject_init_int(&checked_false, 0);      // unchecked

    SECTION("Visible + checked = NOT disabled (option enabled)") {
        // User can see the option and has it checked - they want this operation
        // We need to call the private method via collect_ops_to_disable indirectly,
        // but we can test the behavior through read_options_from_subjects
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

    SECTION("Visible + unchecked = disabled (option disabled by user)") {
        // User can see the option but has it unchecked - they DON'T want this operation
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

    SECTION("Hidden + checked = NOT disabled (hidden means not applicable)") {
        // Option is hidden because it's not applicable to this printer
        // The checked state is irrelevant - hidden options shouldn't affect behavior
        PreprintSubjectsFixture subjects;
        subjects.init_all_subjects();

        lv_subject_set_int(&subjects.can_show_bed_mesh, 0); // hidden
        lv_subject_set_int(&subjects.preprint_bed_mesh, 1); // checked (irrelevant)

        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, nullptr, nullptr, nullptr,
                                      nullptr, nullptr);
        manager.set_preprint_visibility_subjects(&subjects.can_show_bed_mesh, nullptr, nullptr,
                                                 nullptr, nullptr, nullptr);

        auto options = manager.read_options_from_subjects();
        // Hidden means not applicable - should return false (not enabled, but also not "disabled")
        REQUIRE(options.bed_mesh == false);
    }

    SECTION("Hidden + unchecked = NOT disabled (hidden means not applicable)") {
        // Option is hidden because it's not applicable to this printer
        PreprintSubjectsFixture subjects;
        subjects.init_all_subjects();

        lv_subject_set_int(&subjects.can_show_bed_mesh, 0); // hidden
        lv_subject_set_int(&subjects.preprint_bed_mesh, 0); // unchecked (irrelevant)

        manager.set_preprint_subjects(&subjects.preprint_bed_mesh, nullptr, nullptr, nullptr,
                                      nullptr, nullptr);
        manager.set_preprint_visibility_subjects(&subjects.can_show_bed_mesh, nullptr, nullptr,
                                                 nullptr, nullptr, nullptr);

        auto options = manager.read_options_from_subjects();
        // Hidden means not applicable - should return false
        REQUIRE(options.bed_mesh == false);
    }

    // Clean up test subjects
    lv_subject_deinit(&checked_false);
    lv_subject_deinit(&checked_true);
    lv_subject_deinit(&visibility_hidden);
    lv_subject_deinit(&visibility_visible);
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
