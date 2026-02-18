// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_display_message.cpp
 * @brief Tests for Klipper display message (M117 / display_status.message) (Issue #124)
 *
 * Klipper's display_status.message carries the LCD message set by M117 gcode
 * commands and macros. HelixScreen parses this into a string subject for UI display.
 */

#include "../test_helpers/printer_state_test_access.h"
#include "../ui_test_utils.h"
#include "app_globals.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;
using json = nlohmann::json;

// ============================================================================
// Basic Message Parsing
// ============================================================================

TEST_CASE("Display message: parses string message from display_status",
          "[print][display_message]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("sets message from display_status.message string") {
        json status = {{"display_status", {{"progress", 0.0}, {"message", "Heating bed..."}}}};
        state.update_from_status(status);

        const char* msg = lv_subject_get_string(state.get_display_message_subject());
        REQUIRE(std::string(msg) == "Heating bed...");
    }

    SECTION("clears message when null") {
        // First set a message
        json set = {{"display_status", {{"message", "Purging nozzle"}}}};
        state.update_from_status(set);
        REQUIRE(std::string(lv_subject_get_string(state.get_display_message_subject())) ==
                "Purging nozzle");

        // Then clear it (null)
        json clear = {{"display_status", {{"message", nullptr}}}};
        state.update_from_status(clear);
        REQUIRE(std::string(lv_subject_get_string(state.get_display_message_subject())) == "");
    }

    SECTION("clears message with empty string") {
        json set = {{"display_status", {{"message", "Layer 5/100"}}}};
        state.update_from_status(set);

        json clear = {{"display_status", {{"message", ""}}}};
        state.update_from_status(clear);
        REQUIRE(std::string(lv_subject_get_string(state.get_display_message_subject())) == "");
    }

    SECTION("message updates independently of progress") {
        json status = {{"display_status", {{"message", "QGL in progress..."}}}};
        state.update_from_status(status);

        const char* msg = lv_subject_get_string(state.get_display_message_subject());
        REQUIRE(std::string(msg) == "QGL in progress...");
    }
}

// ============================================================================
// Reset Behavior
// ============================================================================

TEST_CASE("Display message: resets on new print", "[print][display_message]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Set a message during first print
    json printing = {{"print_stats", {{"state", "printing"}}}};
    state.update_from_status(printing);

    json msg = {{"display_status", {{"message", "Old print message"}}}};
    state.update_from_status(msg);
    REQUIRE(std::string(lv_subject_get_string(state.get_display_message_subject())) ==
            "Old print message");

    // Complete and start new print
    json complete = {{"print_stats", {{"state", "complete"}}}};
    state.update_from_status(complete);

    json standby = {{"print_stats", {{"state", "standby"}}}};
    state.update_from_status(standby);

    json new_print = {{"print_stats", {{"state", "printing"}}}};
    state.update_from_status(new_print);

    // Message should be cleared for new print
    REQUIRE(std::string(lv_subject_get_string(state.get_display_message_subject())) == "");
}

// ============================================================================
// Initial State
// ============================================================================

TEST_CASE("Display message: initializes empty", "[print][display_message]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    const char* msg = lv_subject_get_string(state.get_display_message_subject());
    REQUIRE(std::string(msg) == "");
}

// ============================================================================
// Visibility Subject
// ============================================================================

TEST_CASE("Display message: visibility subject tracks non-empty state",
          "[print][display_message]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("visible=0 initially") {
        REQUIRE(lv_subject_get_int(state.get_display_message_visible_subject()) == 0);
    }

    SECTION("visible=1 when message set") {
        json status = {{"display_status", {{"message", "Heating..."}}}};
        state.update_from_status(status);
        REQUIRE(lv_subject_get_int(state.get_display_message_visible_subject()) == 1);
    }

    SECTION("visible=0 when message cleared with null") {
        json set = {{"display_status", {{"message", "Hello"}}}};
        state.update_from_status(set);
        REQUIRE(lv_subject_get_int(state.get_display_message_visible_subject()) == 1);

        json clear = {{"display_status", {{"message", nullptr}}}};
        state.update_from_status(clear);
        REQUIRE(lv_subject_get_int(state.get_display_message_visible_subject()) == 0);
    }

    SECTION("visible=0 when message cleared with empty string") {
        json set = {{"display_status", {{"message", "Hello"}}}};
        state.update_from_status(set);

        json clear = {{"display_status", {{"message", ""}}}};
        state.update_from_status(clear);
        REQUIRE(lv_subject_get_int(state.get_display_message_visible_subject()) == 0);
    }
}

// ============================================================================
// Long Message Handling
// ============================================================================

TEST_CASE("Display message: truncates long messages safely", "[print][display_message]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Send a message longer than the 128-byte buffer
    std::string long_msg(200, 'A');
    json status = {{"display_status", {{"message", long_msg}}}};
    state.update_from_status(status);

    const char* msg = lv_subject_get_string(state.get_display_message_subject());
    // Should not crash, and should contain some content
    REQUIRE(std::strlen(msg) > 0);
    REQUIRE(std::strlen(msg) < 200); // Truncated
}
