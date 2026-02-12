// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_printer_manager_overlay.cpp
 * @brief Unit tests for PrinterManagerOverlay
 *
 * Tests subject initialization, lifecycle guards, and global accessor pattern.
 * Uses LVGLTestFixture for LVGL-dependent subject operations.
 *
 * @see ui_printer_manager_overlay.h
 */

#include "ui_printer_manager_overlay.h"

#include "../lvgl_test_fixture.h"

#include "../catch_amalgamated.hpp"

// =============================================================================
// Basic Properties
// =============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "PrinterManagerOverlay: get_name returns expected value",
                 "[printer_manager]") {
    PrinterManagerOverlay overlay;
    REQUIRE(std::string(overlay.get_name()) == "Printer Manager");
}

// =============================================================================
// Subject Initialization
// =============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "PrinterManagerOverlay: init_subjects sets initialized flag",
                 "[printer_manager]") {
    PrinterManagerOverlay overlay;

    REQUIRE_FALSE(overlay.are_subjects_initialized());

    overlay.init_subjects();

    REQUIRE(overlay.are_subjects_initialized());
}

TEST_CASE_METHOD(LVGLTestFixture, "PrinterManagerOverlay: double init_subjects does not crash",
                 "[printer_manager]") {
    PrinterManagerOverlay overlay;

    overlay.init_subjects();
    REQUIRE(overlay.are_subjects_initialized());

    // Second call should be a no-op (guarded)
    overlay.init_subjects();
    REQUIRE(overlay.are_subjects_initialized());
}

// =============================================================================
// Global Accessor Pattern
// =============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "PrinterManagerOverlay: global accessor returns valid reference",
                 "[printer_manager]") {
    PrinterManagerOverlay& overlay = get_printer_manager_overlay();
    REQUIRE(std::string(overlay.get_name()) == "Printer Manager");

    // Cleanup for other tests
    destroy_printer_manager_overlay();
}

TEST_CASE_METHOD(LVGLTestFixture, "PrinterManagerOverlay: global accessor returns same instance",
                 "[printer_manager]") {
    PrinterManagerOverlay& first = get_printer_manager_overlay();
    PrinterManagerOverlay& second = get_printer_manager_overlay();

    REQUIRE(&first == &second);

    destroy_printer_manager_overlay();
}

// =============================================================================
// Destructor / Cleanup
// =============================================================================

TEST_CASE_METHOD(LVGLTestFixture,
                 "PrinterManagerOverlay: destructor cleans up initialized subjects",
                 "[printer_manager]") {
    {
        PrinterManagerOverlay overlay;
        overlay.init_subjects();
        REQUIRE(overlay.are_subjects_initialized());
        // Destructor runs here - should not crash
    }
    SUCCEED("Destructor completed without crash");
}

TEST_CASE_METHOD(LVGLTestFixture, "PrinterManagerOverlay: destructor safe without init_subjects",
                 "[printer_manager]") {
    {
        PrinterManagerOverlay overlay;
        REQUIRE_FALSE(overlay.are_subjects_initialized());
        // Destructor runs here - should be safe even without init
    }
    SUCCEED("Destructor completed without crash");
}

// =============================================================================
// Visibility / Lifecycle State
// =============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "PrinterManagerOverlay: initially not visible",
                 "[printer_manager]") {
    PrinterManagerOverlay overlay;
    REQUIRE_FALSE(overlay.is_visible());
}

TEST_CASE_METHOD(LVGLTestFixture, "PrinterManagerOverlay: cleanup sets flag", "[printer_manager]") {
    PrinterManagerOverlay overlay;
    REQUIRE_FALSE(overlay.cleanup_called());

    overlay.cleanup();

    REQUIRE(overlay.cleanup_called());
}

// =============================================================================
// Overlay Root State
// =============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "PrinterManagerOverlay: root is null before create",
                 "[printer_manager]") {
    PrinterManagerOverlay overlay;
    REQUIRE(overlay.get_root() == nullptr);
}
