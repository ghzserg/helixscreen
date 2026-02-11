// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layer_tracking.cpp
 * @brief Tests for layer tracking: print_stats.info primary path + gcode response fallback
 *
 * Verifies that print_layer_current_ subject is updated from both:
 * 1. Moonraker print_stats.info.current_layer (primary path via update_from_status)
 * 2. Gcode response parsing (fallback for slicers that don't emit SET_PRINT_STATS_INFO)
 */

#include "../test_helpers/printer_state_test_access.h"
#include "../ui_test_utils.h"
#include "app_globals.h"
#include "printer_state.h"

#include <regex>
#include <string>

#include "../catch_amalgamated.hpp"

using json = nlohmann::json;

// ============================================================================
// Helper: parse a gcode response line for layer info (mirrors application.cpp logic)
// ============================================================================

namespace {

struct LayerParseResult {
    int layer = -1;
    int total = -1;
};

LayerParseResult parse_layer_from_gcode(const std::string& line) {
    LayerParseResult result;

    // Pattern 1: SET_PRINT_STATS_INFO CURRENT_LAYER=N [TOTAL_LAYER=N]
    if (line.find("SET_PRINT_STATS_INFO") != std::string::npos) {
        auto pos = line.find("CURRENT_LAYER=");
        if (pos != std::string::npos) {
            result.layer = std::atoi(line.c_str() + pos + 14);
        }
        pos = line.find("TOTAL_LAYER=");
        if (pos != std::string::npos) {
            result.total = std::atoi(line.c_str() + pos + 12);
        }
    }

    // Pattern 2: ;LAYER:N
    if (result.layer < 0 && line.size() >= 8 && line[0] == ';' && line[1] == 'L' &&
        line[2] == 'A' && line[3] == 'Y' && line[4] == 'E' && line[5] == 'R' && line[6] == ':') {
        result.layer = std::atoi(line.c_str() + 7);
    }

    return result;
}

} // namespace

// ============================================================================
// Primary path: print_stats.info.current_layer via update_from_status
// ============================================================================

TEST_CASE("Layer tracking: print_stats.info.current_layer updates subject",
          "[layer_tracking][print_stats]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Start printing
    json printing = {{"print_stats", {{"state", "printing"}}}};
    state.update_from_status(printing);

    SECTION("current_layer updates from info object") {
        json status = {{"print_stats", {{"info", {{"current_layer", 5}, {"total_layer", 110}}}}}};
        state.update_from_status(status);

        REQUIRE(lv_subject_get_int(state.get_print_layer_current_subject()) == 5);
        REQUIRE(lv_subject_get_int(state.get_print_layer_total_subject()) == 110);
    }

    SECTION("null info does not crash or update") {
        // Set initial value
        json status = {{"print_stats", {{"info", {{"current_layer", 3}}}}}};
        state.update_from_status(status);
        REQUIRE(lv_subject_get_int(state.get_print_layer_current_subject()) == 3);

        // Send null info - should not change the value
        json null_info = {{"print_stats", {{"info", nullptr}}}};
        state.update_from_status(null_info);
        REQUIRE(lv_subject_get_int(state.get_print_layer_current_subject()) == 3);
    }

    SECTION("missing info key does not crash") {
        json status = {{"print_stats", {{"state", "printing"}}}};
        state.update_from_status(status);
        // Should still be at default (0)
        REQUIRE(lv_subject_get_int(state.get_print_layer_current_subject()) == 0);
    }
}

// ============================================================================
// Gcode response parsing (unit tests for the parsing logic)
// ============================================================================

TEST_CASE("Layer tracking: gcode response parsing", "[layer_tracking][gcode]") {
    SECTION("SET_PRINT_STATS_INFO CURRENT_LAYER=N parses correctly") {
        auto result = parse_layer_from_gcode("SET_PRINT_STATS_INFO CURRENT_LAYER=5");
        REQUIRE(result.layer == 5);
        REQUIRE(result.total == -1); // no total in this line
    }

    SECTION("SET_PRINT_STATS_INFO with both CURRENT_LAYER and TOTAL_LAYER") {
        auto result =
            parse_layer_from_gcode("SET_PRINT_STATS_INFO CURRENT_LAYER=3 TOTAL_LAYER=110");
        REQUIRE(result.layer == 3);
        REQUIRE(result.total == 110);
    }

    SECTION(";LAYER:N comment format (OrcaSlicer/PrusaSlicer/Cura)") {
        auto result = parse_layer_from_gcode(";LAYER:42");
        REQUIRE(result.layer == 42);
    }

    SECTION(";LAYER:0 parses zero layer") {
        auto result = parse_layer_from_gcode(";LAYER:0");
        REQUIRE(result.layer == 0);
    }

    SECTION("unrelated gcode lines are ignored") {
        auto result = parse_layer_from_gcode("ok");
        REQUIRE(result.layer == -1);

        result = parse_layer_from_gcode("G1 X10 Y20 Z0.3");
        REQUIRE(result.layer == -1);

        result = parse_layer_from_gcode("M104 S200");
        REQUIRE(result.layer == -1);

        result = parse_layer_from_gcode("");
        REQUIRE(result.layer == -1);
    }

    SECTION("short lines don't cause out-of-bounds") {
        auto result = parse_layer_from_gcode(";L");
        REQUIRE(result.layer == -1);

        result = parse_layer_from_gcode(";LAYER");
        REQUIRE(result.layer == -1);
    }
}

// ============================================================================
// set_print_layer_current setter (thread-safe path)
// ============================================================================

TEST_CASE("Layer tracking: set_print_layer_current setter", "[layer_tracking][setter]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("setter updates the subject via async") {
        state.set_print_layer_current(7);
        // Process the async queue so the value actually lands
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(lv_subject_get_int(state.get_print_layer_current_subject()) == 7);
    }

    SECTION("setter and print_stats.info both update same subject") {
        // Simulate gcode fallback setting layer
        state.set_print_layer_current(10);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        REQUIRE(lv_subject_get_int(state.get_print_layer_current_subject()) == 10);

        // Then print_stats.info comes in with a different value (takes precedence naturally)
        json status = {{"print_stats", {{"info", {{"current_layer", 12}}}}}};
        state.update_from_status(status);
        REQUIRE(lv_subject_get_int(state.get_print_layer_current_subject()) == 12);
    }
}
