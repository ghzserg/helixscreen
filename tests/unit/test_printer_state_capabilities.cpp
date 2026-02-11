// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_printer_state_capabilities.cpp
 * @brief Tests for PrinterState printer type and capability storage
 *
 * These tests verify the PrinterState methods:
 * - set_printer_type_sync(const std::string& type) - synchronous version for tests
 * - get_printer_type() const
 * - get_print_start_capabilities() const
 *
 * Note: Tests use set_printer_type_sync() which directly calls the internal
 * method. The async set_printer_type() defers to the main thread via
 * helix::async::call_method_ref() for thread safety from WebSocket callbacks.
 */

#include "ui_update_queue.h"

#include "../test_helpers/printer_state_test_access.h"
#include "../ui_test_utils.h"
#include "app_globals.h"
#include "printer_detector.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

// ============================================================================
// PrinterState Printer Type Storage Tests
// ============================================================================

TEST_CASE("PrinterState: set_printer_type stores the type name", "[printer_state][capabilities]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Set a known printer type
    state.set_printer_type_sync("FlashForge Adventurer 5M Pro");

    // Verify the type is stored and retrievable
    REQUIRE(state.get_printer_type() == "FlashForge Adventurer 5M Pro");
}

TEST_CASE("PrinterState: set_printer_type with different printer names",
          "[printer_state][capabilities]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("FlashForge Adventurer 5M") {
        state.set_printer_type_sync("FlashForge Adventurer 5M");
        REQUIRE(state.get_printer_type() == "FlashForge Adventurer 5M");
    }

    SECTION("Voron 2.4") {
        state.set_printer_type_sync("Voron 2.4");
        REQUIRE(state.get_printer_type() == "Voron 2.4");
    }

    SECTION("Custom/Other") {
        state.set_printer_type_sync("Custom/Other");
        REQUIRE(state.get_printer_type() == "Custom/Other");
    }

    SECTION("Empty string") {
        state.set_printer_type_sync("");
        REQUIRE(state.get_printer_type() == "");
    }
}

// ============================================================================
// PrinterState Capability Fetching Tests
// ============================================================================

TEST_CASE("PrinterState: set_printer_type fetches capabilities from database",
          "[printer_state][capabilities]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Set printer type that has capabilities in the database
    state.set_printer_type_sync("FlashForge Adventurer 5M Pro");

    // Get the capabilities
    const PrintStartCapabilities& caps = state.get_print_start_capabilities();

    // Verify macro name is populated from database
    REQUIRE(caps.macro_name == "START_PRINT");

    // Verify bed_mesh param exists with correct values
    REQUIRE(caps.has_capability("bed_mesh"));
    const auto* bed_mesh = caps.get_capability("bed_mesh");
    REQUIRE(bed_mesh != nullptr);
    REQUIRE(bed_mesh->param == "SKIP_LEVELING");
    REQUIRE(bed_mesh->skip_value == "1");
    REQUIRE(bed_mesh->enable_value == "0");
}

TEST_CASE("PrinterState: AD5M Pro does not include purge_line parameter",
          "[printer_state][capabilities]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    state.set_printer_type_sync("FlashForge Adventurer 5M Pro");

    const PrintStartCapabilities& caps = state.get_print_start_capabilities();

    // AD5M Pro START_PRINT macro does not have purge_line or skew_correct params
    REQUIRE_FALSE(caps.has_capability("purge_line"));
}

TEST_CASE("PrinterState: AD5M Pro does not include skew_correct parameter",
          "[printer_state][capabilities]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    state.set_printer_type_sync("FlashForge Adventurer 5M Pro");

    const PrintStartCapabilities& caps = state.get_print_start_capabilities();

    // AD5M Pro START_PRINT macro does not have skew_correct param
    REQUIRE_FALSE(caps.has_capability("skew_correct"));
}

// ============================================================================
// Unknown Printer Type Tests
// ============================================================================

TEST_CASE("PrinterState: unknown printer type returns empty capabilities",
          "[printer_state][capabilities]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Set an unknown printer type (not in database)
    state.set_printer_type_sync("Some Unknown Printer Model XYZ");

    // Capabilities should be empty
    const PrintStartCapabilities& caps = state.get_print_start_capabilities();
    REQUIRE(caps.empty());
    REQUIRE(caps.macro_name.empty());
    REQUIRE(caps.params.empty());
}

TEST_CASE("PrinterState: Custom/Other printer type returns empty capabilities",
          "[printer_state][capabilities]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Custom/Other is a valid selection but has no database entry
    state.set_printer_type_sync("Custom/Other");

    const PrintStartCapabilities& caps = state.get_print_start_capabilities();
    REQUIRE(caps.empty());
}

TEST_CASE("PrinterState: empty printer type returns empty capabilities",
          "[printer_state][capabilities]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    state.set_printer_type_sync("");

    const PrintStartCapabilities& caps = state.get_print_start_capabilities();
    REQUIRE(caps.empty());
}

// ============================================================================
// Changing Printer Type Tests
// ============================================================================

TEST_CASE("PrinterState: changing printer type updates capabilities",
          "[printer_state][capabilities]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // First set to AD5M Pro (has capabilities)
    state.set_printer_type_sync("FlashForge Adventurer 5M Pro");

    // Verify it has capabilities
    const PrintStartCapabilities& caps1 = state.get_print_start_capabilities();
    REQUIRE_FALSE(caps1.empty());
    REQUIRE(caps1.macro_name == "START_PRINT");
    REQUIRE(caps1.has_capability("bed_mesh"));

    // Change to unknown printer
    state.set_printer_type_sync("Some Unknown Printer");

    // Capabilities should now be empty
    const PrintStartCapabilities& caps2 = state.get_print_start_capabilities();
    REQUIRE(caps2.empty());
}

TEST_CASE("PrinterState: changing from unknown to known updates capabilities",
          "[printer_state][capabilities]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Start with unknown
    state.set_printer_type_sync("Unknown Printer");
    REQUIRE(state.get_print_start_capabilities().empty());

    // Change to known printer with capabilities
    state.set_printer_type_sync("FlashForge Adventurer 5M Pro");

    const PrintStartCapabilities& caps = state.get_print_start_capabilities();
    REQUIRE_FALSE(caps.empty());
    REQUIRE(caps.macro_name == "START_PRINT");
}

TEST_CASE("PrinterState: changing between printers with different capabilities",
          "[printer_state][capabilities]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Set to AD5M Pro
    state.set_printer_type_sync("FlashForge Adventurer 5M Pro");
    const PrintStartCapabilities& caps1 = state.get_print_start_capabilities();
    REQUIRE(caps1.macro_name == "START_PRINT");

    // Change to regular AD5M (also has START_PRINT but same capabilities in DB)
    state.set_printer_type_sync("FlashForge Adventurer 5M");
    const PrintStartCapabilities& caps2 = state.get_print_start_capabilities();
    // AD5M should also have capabilities from database
    REQUIRE(caps2.macro_name == "START_PRINT");
}

// ============================================================================
// Default/Initial State Tests
// ============================================================================

TEST_CASE("PrinterState: initial printer type is empty", "[printer_state][capabilities]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Before setting any type, should be empty
    REQUIRE(state.get_printer_type().empty());
}

TEST_CASE("PrinterState: initial capabilities are empty", "[printer_state][capabilities]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Before setting any type, capabilities should be empty
    const PrintStartCapabilities& caps = state.get_print_start_capabilities();
    REQUIRE(caps.empty());
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_CASE("PrinterState: printer type lookup is case-insensitive",
          "[printer_state][capabilities][edge]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Correct case should work
    state.set_printer_type_sync("FlashForge Adventurer 5M Pro");
    REQUIRE_FALSE(state.get_print_start_capabilities().empty());

    // Different case should also work (database lookup is case-insensitive)
    state.set_printer_type_sync("flashforge adventurer 5m pro");
    REQUIRE_FALSE(state.get_print_start_capabilities().empty());
}

TEST_CASE("PrinterState: setting same type twice is idempotent",
          "[printer_state][capabilities][edge]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    state.set_printer_type_sync("FlashForge Adventurer 5M Pro");
    const PrintStartCapabilities& caps1 = state.get_print_start_capabilities();

    // Set same type again
    state.set_printer_type_sync("FlashForge Adventurer 5M Pro");
    const PrintStartCapabilities& caps2 = state.get_print_start_capabilities();

    // Should still have same capabilities
    REQUIRE(caps2.macro_name == caps1.macro_name);
    REQUIRE(caps2.params.size() == caps1.params.size());
}

TEST_CASE("PrinterState: get_printer_type returns const reference",
          "[printer_state][capabilities][edge]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    state.set_printer_type_sync("FlashForge Adventurer 5M Pro");

    // Get reference and verify it's stable
    const std::string& type1 = state.get_printer_type();
    const std::string& type2 = state.get_printer_type();

    // Should return the same reference (not a copy)
    REQUIRE(&type1 == &type2);
    REQUIRE(type1 == "FlashForge Adventurer 5M Pro");
}

TEST_CASE("PrinterState: get_print_start_capabilities returns const reference",
          "[printer_state][capabilities][edge]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    state.set_printer_type_sync("FlashForge Adventurer 5M Pro");

    // Get reference and verify it's stable
    const PrintStartCapabilities& caps1 = state.get_print_start_capabilities();
    const PrintStartCapabilities& caps2 = state.get_print_start_capabilities();

    // Should return the same reference (not a copy)
    REQUIRE(&caps1 == &caps2);
}
