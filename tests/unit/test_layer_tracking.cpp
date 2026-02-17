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

using namespace helix;
using namespace helix::ui;
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

    SECTION("setter marks has_real_layer_data true") {
        REQUIRE_FALSE(state.has_real_layer_data());
        state.set_print_layer_current(5);
        // Flag is set inside the async lambda, so drain the queue first
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        REQUIRE(state.has_real_layer_data());
    }
}

// ============================================================================
// Progress-based layer estimation fallback
// ============================================================================

TEST_CASE("Layer tracking: progress-based estimation fallback", "[layer_tracking][estimation]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Start printing
    json printing = {{"print_stats", {{"state", "printing"}}}};
    state.update_from_status(printing);

    // Set total layers from metadata (this is how it works in practice)
    state.set_print_layer_total(320);

    SECTION("estimates layer from progress when no real layer data") {
        REQUIRE_FALSE(state.has_real_layer_data());

        // 50% progress → ~160/320
        json progress = {{"virtual_sdcard", {{"progress", 0.50}}}};
        state.update_from_status(progress);

        REQUIRE(lv_subject_get_int(state.get_print_layer_current_subject()) == 160);
    }

    SECTION("estimates at low progress") {
        json progress = {{"virtual_sdcard", {{"progress", 0.01}}}};
        state.update_from_status(progress);

        // 1% of 320 = 3.2, rounded = 3. But clamped to min 1.
        REQUIRE(lv_subject_get_int(state.get_print_layer_current_subject()) >= 1);
    }

    SECTION("estimates at high progress") {
        json progress = {{"virtual_sdcard", {{"progress", 0.99}}}};
        state.update_from_status(progress);

        // 99% of 320 = 316.8 → 317
        int estimated = lv_subject_get_int(state.get_print_layer_current_subject());
        REQUIRE(estimated >= 315);
        REQUIRE(estimated <= 320);
    }

    SECTION("does not estimate when total_layers is 0") {
        state.set_print_layer_total(0);

        json progress = {{"virtual_sdcard", {{"progress", 0.50}}}};
        state.update_from_status(progress);

        // Should stay at 0 — no total to estimate from
        REQUIRE(lv_subject_get_int(state.get_print_layer_current_subject()) == 0);
    }

    SECTION("stops estimating once real data arrives from print_stats.info") {
        // First: estimation active
        json progress = {{"virtual_sdcard", {{"progress", 0.50}}}};
        state.update_from_status(progress);
        REQUIRE(lv_subject_get_int(state.get_print_layer_current_subject()) == 160);
        REQUIRE_FALSE(state.has_real_layer_data());

        // Real data arrives
        json real_layer = {{"print_stats", {{"info", {{"current_layer", 142}}}}}};
        state.update_from_status(real_layer);
        REQUIRE(lv_subject_get_int(state.get_print_layer_current_subject()) == 142);
        REQUIRE(state.has_real_layer_data());

        // Further progress updates should NOT overwrite real data
        json progress2 = {{"virtual_sdcard", {{"progress", 0.55}}}};
        state.update_from_status(progress2);
        REQUIRE(lv_subject_get_int(state.get_print_layer_current_subject()) == 142);
    }

    SECTION("stops estimating once real data arrives from gcode fallback") {
        json progress = {{"virtual_sdcard", {{"progress", 0.50}}}};
        state.update_from_status(progress);
        REQUIRE_FALSE(state.has_real_layer_data());

        // Gcode fallback sets real data
        state.set_print_layer_current(150);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        REQUIRE(state.has_real_layer_data());

        // Progress update should NOT overwrite
        json progress2 = {{"virtual_sdcard", {{"progress", 0.55}}}};
        state.update_from_status(progress2);
        REQUIRE(lv_subject_get_int(state.get_print_layer_current_subject()) == 150);
    }

    SECTION("does not estimate in terminal state even without real data") {
        // Set total layers and make some progress
        json progress = {{"virtual_sdcard", {{"progress", 0.50}}}};
        state.update_from_status(progress);
        REQUIRE(lv_subject_get_int(state.get_print_layer_current_subject()) == 160);

        // Print completes
        json complete = {{"print_stats", {{"state", "complete"}}}};
        state.update_from_status(complete);

        // Progress update arrives after completion — should NOT change layer
        json progress2 = {{"virtual_sdcard", {{"progress", 0.99}}}};
        state.update_from_status(progress2);
        REQUIRE(lv_subject_get_int(state.get_print_layer_current_subject()) == 160);
    }

    SECTION("has_real_layer_data resets on new print") {
        // Get real data
        json real_layer = {{"print_stats", {{"info", {{"current_layer", 42}}}}}};
        state.update_from_status(real_layer);
        REQUIRE(state.has_real_layer_data());

        // Simulate new print starting (state goes to standby then printing)
        json standby = {{"print_stats", {{"state", "standby"}}}};
        state.update_from_status(standby);

        // Reset via the same mechanism as real code
        PrinterStateTestAccess::reset(state);
        state.init_subjects(false);

        json printing2 = {{"print_stats", {{"state", "printing"}}}};
        state.update_from_status(printing2);

        REQUIRE_FALSE(state.has_real_layer_data());
    }
}
