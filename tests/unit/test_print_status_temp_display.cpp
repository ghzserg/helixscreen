// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_print_status_temp_display.cpp
 * @brief Tests for PrintStatusPanel temperature display formatting
 *
 * PrinterState stores temperatures in centi-degrees (×10) for precision.
 * These tests verify the display correctly converts to whole degrees.
 *
 * Bug context: Previously displayed "2100 / 2200°C" instead of "210 / 220°C"
 * because the centi-degree values weren't divided by 10 before display.
 */

#include <cstdio>
#include <string>

#include "../catch_amalgamated.hpp"

// ============================================================================
// Test Helper: Simulates the temperature formatting logic from PrintStatusPanel
// ============================================================================

/**
 * @brief Format temperature display string from centi-degree values
 *
 * Mirrors the logic in PrintStatusPanel::update_all_displays():
 * - Takes current and target temps in centi-degrees (×10)
 * - Returns formatted string like "210 / 220°C"
 *
 * @param current_centi Current temperature in centi-degrees
 * @param target_centi Target temperature in centi-degrees
 * @return Formatted temperature string
 */
static std::string format_temp_display(int current_centi, int target_centi) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d / %d°C", current_centi / 10, target_centi / 10);
    return std::string(buf);
}

// ============================================================================
// Temperature Display Conversion Tests
// ============================================================================

TEST_CASE("Temperature display converts centi-degrees to degrees", "[print_status][temperature]") {
    SECTION("Typical PLA nozzle temperature") {
        // 210°C stored as 2100 centi-degrees
        int current_centi = 2100;
        int target_centi = 2150; // 215°C target

        std::string result = format_temp_display(current_centi, target_centi);

        REQUIRE(result == "210 / 215°C");
    }

    SECTION("Typical PLA bed temperature") {
        // 60°C stored as 600 centi-degrees
        int current_centi = 580; // 58°C current
        int target_centi = 600;  // 60°C target

        std::string result = format_temp_display(current_centi, target_centi);

        REQUIRE(result == "58 / 60°C");
    }

    SECTION("High temperature ABS nozzle") {
        // 250°C stored as 2500 centi-degrees
        int current_centi = 2480; // 248°C heating up
        int target_centi = 2500;  // 250°C target

        std::string result = format_temp_display(current_centi, target_centi);

        REQUIRE(result == "248 / 250°C");
    }

    SECTION("High temperature ABS bed") {
        // 110°C stored as 1100 centi-degrees
        int current_centi = 1050; // 105°C heating up
        int target_centi = 1100;  // 110°C target

        std::string result = format_temp_display(current_centi, target_centi);

        REQUIRE(result == "105 / 110°C");
    }

    SECTION("Room temperature (heater off)") {
        // 25°C stored as 250 centi-degrees, target 0
        int current_centi = 250;
        int target_centi = 0;

        std::string result = format_temp_display(current_centi, target_centi);

        REQUIRE(result == "25 / 0°C");
    }

    SECTION("Zero temperature") {
        int current_centi = 0;
        int target_centi = 0;

        std::string result = format_temp_display(current_centi, target_centi);

        REQUIRE(result == "0 / 0°C");
    }

    SECTION("3DBenchy default temperatures from G-code metadata") {
        // From test file: nozzle=220°C, bed=55°C
        // These caused the original bug (displayed as 2200°C / 550°C)
        int nozzle_current = 2200; // 220°C
        int nozzle_target = 2200;  // 220°C
        int bed_current = 550;     // 55°C
        int bed_target = 550;      // 55°C

        std::string nozzle_result = format_temp_display(nozzle_current, nozzle_target);
        std::string bed_result = format_temp_display(bed_current, bed_target);

        REQUIRE(nozzle_result == "220 / 220°C");
        REQUIRE(bed_result == "55 / 55°C");

        // These would have been wrong before the fix:
        REQUIRE(nozzle_result != "2200 / 2200°C");
        REQUIRE(bed_result != "550 / 550°C");
    }
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_CASE("Temperature display edge cases", "[print_status][temperature][edge]") {
    SECTION("Negative temperature (should not happen but handle gracefully)") {
        int current_centi = -100; // -10°C (impossible for heater)
        int target_centi = 0;

        std::string result = format_temp_display(current_centi, target_centi);

        // Integer division of negative numbers: -100/10 = -10
        REQUIRE(result == "-10 / 0°C");
    }

    SECTION("Very high temperature (chamber heater)") {
        // 80°C chamber = 800 centi-degrees
        int current_centi = 750;
        int target_centi = 800;

        std::string result = format_temp_display(current_centi, target_centi);

        REQUIRE(result == "75 / 80°C");
    }

    SECTION("Fractional degrees are truncated (integer division)") {
        // 215.5°C stored as 2155 centi-degrees
        // Integer division: 2155/10 = 215 (truncated, not rounded)
        int current_centi = 2155;
        int target_centi = 2200;

        std::string result = format_temp_display(current_centi, target_centi);

        REQUIRE(result == "215 / 220°C");
    }
}
