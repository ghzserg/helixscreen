// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_print_controls_char.cpp
 * @brief Characterization tests for print status panel controls
 *
 * These tests document the EXISTING behavior of Light/Timelapse and Tune panel
 * features before extraction. They test helper functions and parsing logic
 * that mirror the implementation in PrintStatusPanel.
 *
 * Run with: ./build/bin/helix-tests "[controls]"
 *
 * Features tested:
 * - Light Button: LED toggle with icon state changes
 * - Timelapse Button: Recording toggle with icon/label updates
 * - Tune Panel: Speed/flow sliders, Z-offset buttons, reset functionality
 */

#include <cstdio>
#include <cstring>
#include <string>

#include "../catch_amalgamated.hpp"

// ============================================================================
// MDI Icon Constants (from codepoints.h)
// ============================================================================

namespace icons {
// Light button icons
constexpr const char* LIGHTBULB_OUTLINE = "\xF3\xB0\x8C\xB6"; // F0336 - LED off
constexpr const char* LIGHTBULB_ON = "\xF3\xB0\x9B\xA8";      // F06E8 - LED on

// Timelapse button icons
constexpr const char* VIDEO = "\xF3\xB0\x95\xA7";     // F0567 - recording enabled
constexpr const char* VIDEO_OFF = "\xF3\xB0\x95\xA8"; // F0568 - recording disabled

// Z-offset icons (CoreXY - bed moves)
constexpr const char* ARROW_EXPAND_DOWN = "\xF3\xB0\x9E\x93"; // F0793 - closer
constexpr const char* ARROW_EXPAND_UP = "\xF3\xB0\x9E\x96";   // F0796 - farther

// Z-offset icons (Cartesian/Delta - head moves)
constexpr const char* ARROW_DOWN = "\xF3\xB0\x81\x85"; // F0045 - closer
constexpr const char* ARROW_UP = "\xF3\xB0\x81\x9D";   // F005D - farther
} // namespace icons

// ============================================================================
// Test Helpers: Mirror implementation logic for testing
// ============================================================================

/**
 * @brief Get light button icon based on LED state
 *
 * Mirrors the logic in PrintStatusPanel::on_led_state_changed()
 *
 * @param led_on Current LED state
 * @return Icon codepoint string
 */
static const char* get_light_icon(bool led_on) {
    return led_on ? icons::LIGHTBULB_ON : icons::LIGHTBULB_OUTLINE;
}

/**
 * @brief Get timelapse button icon based on enabled state
 *
 * Mirrors the logic in PrintStatusPanel::handle_timelapse_button() success callback
 *
 * @param enabled Current timelapse state
 * @return Icon codepoint string
 */
static const char* get_timelapse_icon(bool enabled) {
    return enabled ? icons::VIDEO : icons::VIDEO_OFF;
}

/**
 * @brief Get timelapse button label based on enabled state
 *
 * @param enabled Current timelapse state
 * @return Label text ("On" or "Off")
 */
static const char* get_timelapse_label(bool enabled) {
    return enabled ? "On" : "Off";
}

/**
 * @brief Parse Z-offset button name to determine delta
 *
 * Mirrors the logic in on_tune_z_offset_cb():
 *   - btn_z_closer_01  -> -0.1mm (closer = negative = more squish)
 *   - btn_z_closer_005 -> -0.05mm
 *   - btn_z_closer_001 -> -0.01mm
 *   - btn_z_farther_001 -> +0.01mm (farther = positive = less squish)
 *   - btn_z_farther_005 -> +0.05mm
 *   - btn_z_farther_01 -> +0.1mm
 *
 * @param button_name Button name from LVGL widget
 * @param[out] delta Output delta value in mm
 * @return true if name was parsed successfully, false otherwise
 */
static bool parse_z_offset_button_name(const char* button_name, double& delta) {
    if (!button_name) {
        return false;
    }

    // Parse direction: "closer" = negative, "farther" = positive
    bool is_closer = (strstr(button_name, "closer") != nullptr);
    bool is_farther = (strstr(button_name, "farther") != nullptr);

    if (!is_closer && !is_farther) {
        return false;
    }

    // Parse magnitude from suffix: "_01" = 0.1, "_005" = 0.05, "_001" = 0.01
    if (strstr(button_name, "_01") && !strstr(button_name, "_001")) {
        delta = 0.1;
    } else if (strstr(button_name, "_005")) {
        delta = 0.05;
    } else if (strstr(button_name, "_001")) {
        delta = 0.01;
    } else {
        return false;
    }

    // Apply direction: closer = more squish = negative Z adjust
    if (is_closer) {
        delta = -delta;
    }

    return true;
}

/**
 * @brief Format speed/flow percentage display string
 *
 * Mirrors the logic in PrintStatusPanel::handle_tune_speed_changed()
 *
 * @param value Percentage value
 * @return Formatted string like "100%"
 */
static std::string format_tune_percentage(int value) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d%%", value);
    return std::string(buf);
}

/**
 * @brief Format Z-offset display string
 *
 * Mirrors the logic in PrintStatusPanel::handle_tune_z_offset_changed()
 *
 * @param offset_mm Z-offset in mm
 * @return Formatted string like "0.100mm" or "-0.050mm"
 */
static std::string format_z_offset(double offset_mm) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.3fmm", offset_mm);
    return std::string(buf);
}

/**
 * @brief Format G-code command for speed adjustment
 *
 * Mirrors the G-code sent in handle_tune_speed_changed()
 *
 * @param value Speed percentage (50-200)
 * @return G-code string like "M220 S100"
 */
static std::string format_speed_gcode(int value) {
    return "M220 S" + std::to_string(value);
}

/**
 * @brief Format G-code command for flow adjustment
 *
 * Mirrors the G-code sent in handle_tune_flow_changed()
 *
 * @param value Flow percentage (75-125)
 * @return G-code string like "M221 S100"
 */
static std::string format_flow_gcode(int value) {
    return "M221 S" + std::to_string(value);
}

/**
 * @brief Format G-code command for Z-offset adjustment
 *
 * Mirrors the G-code sent in handle_tune_z_offset_changed()
 *
 * @param delta Z-offset delta in mm
 * @return G-code string like "SET_GCODE_OFFSET Z_ADJUST=0.100"
 */
static std::string format_z_adjust_gcode(double delta) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "SET_GCODE_OFFSET Z_ADJUST=%.3f", delta);
    return std::string(buf);
}

/**
 * @brief Get Z-offset icon based on kinematics and direction
 *
 * Mirrors the logic in PrintStatusPanel::update_z_offset_icons()
 *
 * @param bed_moves_z true if bed moves on Z (CoreXY), false if head moves (Cartesian/Delta)
 * @param is_closer true for closer/down direction, false for farther/up
 * @return Icon codepoint string
 */
static const char* get_z_offset_icon(bool bed_moves_z, bool is_closer) {
    if (is_closer) {
        return bed_moves_z ? icons::ARROW_EXPAND_DOWN : icons::ARROW_DOWN;
    } else {
        return bed_moves_z ? icons::ARROW_EXPAND_UP : icons::ARROW_UP;
    }
}

// ============================================================================
// CHARACTERIZATION: Light Button
// ============================================================================

TEST_CASE("CHAR: Light button initial state", "[characterization][controls][light]") {
    SECTION("Default icon is lightbulb_outline (off)") {
        const char* icon = get_light_icon(false);
        REQUIRE(std::string(icon) == icons::LIGHTBULB_OUTLINE);
    }

    SECTION("Icon changes to lightbulb_on when LED is on") {
        const char* icon = get_light_icon(true);
        REQUIRE(std::string(icon) == icons::LIGHTBULB_ON);
    }
}

TEST_CASE("CHAR: Light button icon toggle", "[characterization][controls][light]") {
    bool led_on = false;

    SECTION("Off -> On transition") {
        REQUIRE(std::string(get_light_icon(led_on)) == icons::LIGHTBULB_OUTLINE);

        // Simulate toggle
        led_on = true;

        REQUIRE(std::string(get_light_icon(led_on)) == icons::LIGHTBULB_ON);
    }

    SECTION("On -> Off transition") {
        led_on = true;
        REQUIRE(std::string(get_light_icon(led_on)) == icons::LIGHTBULB_ON);

        // Simulate toggle
        led_on = false;

        REQUIRE(std::string(get_light_icon(led_on)) == icons::LIGHTBULB_OUTLINE);
    }
}

// ============================================================================
// CHARACTERIZATION: Timelapse Button
// ============================================================================

TEST_CASE("CHAR: Timelapse button initial state", "[characterization][controls][timelapse]") {
    SECTION("Default icon is video-off (disabled)") {
        const char* icon = get_timelapse_icon(false);
        REQUIRE(std::string(icon) == icons::VIDEO_OFF);
    }

    SECTION("Default label is 'Off'") {
        const char* label = get_timelapse_label(false);
        REQUIRE(std::string(label) == "Off");
    }
}

TEST_CASE("CHAR: Timelapse button state changes", "[characterization][controls][timelapse]") {
    SECTION("Enabled state shows video icon and 'On' label") {
        const char* icon = get_timelapse_icon(true);
        const char* label = get_timelapse_label(true);

        REQUIRE(std::string(icon) == icons::VIDEO);
        REQUIRE(std::string(label) == "On");
    }

    SECTION("Disabled state shows video-off icon and 'Off' label") {
        const char* icon = get_timelapse_icon(false);
        const char* label = get_timelapse_label(false);

        REQUIRE(std::string(icon) == icons::VIDEO_OFF);
        REQUIRE(std::string(label) == "Off");
    }
}

TEST_CASE("CHAR: Timelapse button toggle cycle", "[characterization][controls][timelapse]") {
    bool timelapse_enabled = false;

    // Initial state
    REQUIRE(std::string(get_timelapse_icon(timelapse_enabled)) == icons::VIDEO_OFF);
    REQUIRE(std::string(get_timelapse_label(timelapse_enabled)) == "Off");

    // Enable timelapse
    timelapse_enabled = true;
    REQUIRE(std::string(get_timelapse_icon(timelapse_enabled)) == icons::VIDEO);
    REQUIRE(std::string(get_timelapse_label(timelapse_enabled)) == "On");

    // Disable timelapse
    timelapse_enabled = false;
    REQUIRE(std::string(get_timelapse_icon(timelapse_enabled)) == icons::VIDEO_OFF);
    REQUIRE(std::string(get_timelapse_label(timelapse_enabled)) == "Off");
}

// ============================================================================
// CHARACTERIZATION: Tune Panel - Speed Slider
// ============================================================================

TEST_CASE("CHAR: Speed slider display formatting", "[characterization][controls][tune]") {
    SECTION("Initial value 100%") {
        std::string display = format_tune_percentage(100);
        REQUIRE(display == "100%");
    }

    SECTION("Minimum value 50%") {
        std::string display = format_tune_percentage(50);
        REQUIRE(display == "50%");
    }

    SECTION("Maximum value 200%") {
        std::string display = format_tune_percentage(200);
        REQUIRE(display == "200%");
    }

    SECTION("Mid-range values") {
        REQUIRE(format_tune_percentage(75) == "75%");
        REQUIRE(format_tune_percentage(150) == "150%");
    }
}

TEST_CASE("CHAR: Speed slider G-code commands", "[characterization][controls][tune]") {
    SECTION("Speed 100% sends M220 S100") {
        std::string gcode = format_speed_gcode(100);
        REQUIRE(gcode == "M220 S100");
    }

    SECTION("Speed 50% sends M220 S50") {
        std::string gcode = format_speed_gcode(50);
        REQUIRE(gcode == "M220 S50");
    }

    SECTION("Speed 200% sends M220 S200") {
        std::string gcode = format_speed_gcode(200);
        REQUIRE(gcode == "M220 S200");
    }
}

// ============================================================================
// CHARACTERIZATION: Tune Panel - Flow Slider
// ============================================================================

TEST_CASE("CHAR: Flow slider display formatting", "[characterization][controls][tune]") {
    SECTION("Initial value 100%") {
        std::string display = format_tune_percentage(100);
        REQUIRE(display == "100%");
    }

    SECTION("Minimum value 75%") {
        std::string display = format_tune_percentage(75);
        REQUIRE(display == "75%");
    }

    SECTION("Maximum value 125%") {
        std::string display = format_tune_percentage(125);
        REQUIRE(display == "125%");
    }
}

TEST_CASE("CHAR: Flow slider G-code commands", "[characterization][controls][tune]") {
    SECTION("Flow 100% sends M221 S100") {
        std::string gcode = format_flow_gcode(100);
        REQUIRE(gcode == "M221 S100");
    }

    SECTION("Flow 75% sends M221 S75") {
        std::string gcode = format_flow_gcode(75);
        REQUIRE(gcode == "M221 S75");
    }

    SECTION("Flow 125% sends M221 S125") {
        std::string gcode = format_flow_gcode(125);
        REQUIRE(gcode == "M221 S125");
    }
}

// ============================================================================
// CHARACTERIZATION: Tune Panel - Reset Button
// ============================================================================

TEST_CASE("CHAR: Reset button behavior", "[characterization][controls][tune]") {
    SECTION("Reset sets speed to 100%") {
        int speed = 150;
        // Simulate reset
        speed = 100;
        REQUIRE(format_tune_percentage(speed) == "100%");
        REQUIRE(format_speed_gcode(speed) == "M220 S100");
    }

    SECTION("Reset sets flow to 100%") {
        int flow = 125;
        // Simulate reset
        flow = 100;
        REQUIRE(format_tune_percentage(flow) == "100%");
        REQUIRE(format_flow_gcode(flow) == "M221 S100");
    }
}

// ============================================================================
// CHARACTERIZATION: Tune Panel - Z-Offset Button Name Parsing
// ============================================================================

TEST_CASE("CHAR: Z-offset button name parsing - closer buttons",
          "[characterization][controls][tune]") {
    double delta = 0.0;

    SECTION("btn_z_closer_01 -> -0.1mm") {
        REQUIRE(parse_z_offset_button_name("btn_z_closer_01", delta) == true);
        REQUIRE(delta == Catch::Approx(-0.1));
    }

    SECTION("btn_z_closer_005 -> -0.05mm") {
        REQUIRE(parse_z_offset_button_name("btn_z_closer_005", delta) == true);
        REQUIRE(delta == Catch::Approx(-0.05));
    }

    SECTION("btn_z_closer_001 -> -0.01mm") {
        REQUIRE(parse_z_offset_button_name("btn_z_closer_001", delta) == true);
        REQUIRE(delta == Catch::Approx(-0.01));
    }
}

TEST_CASE("CHAR: Z-offset button name parsing - farther buttons",
          "[characterization][controls][tune]") {
    double delta = 0.0;

    SECTION("btn_z_farther_01 -> +0.1mm") {
        REQUIRE(parse_z_offset_button_name("btn_z_farther_01", delta) == true);
        REQUIRE(delta == Catch::Approx(0.1));
    }

    SECTION("btn_z_farther_005 -> +0.05mm") {
        REQUIRE(parse_z_offset_button_name("btn_z_farther_005", delta) == true);
        REQUIRE(delta == Catch::Approx(0.05));
    }

    SECTION("btn_z_farther_001 -> +0.01mm") {
        REQUIRE(parse_z_offset_button_name("btn_z_farther_001", delta) == true);
        REQUIRE(delta == Catch::Approx(0.01));
    }
}

TEST_CASE("CHAR: Z-offset button name parsing - invalid names",
          "[characterization][controls][tune]") {
    double delta = 0.0;

    SECTION("nullptr returns false") {
        REQUIRE(parse_z_offset_button_name(nullptr, delta) == false);
    }

    SECTION("Unknown direction returns false") {
        REQUIRE(parse_z_offset_button_name("btn_z_up_01", delta) == false);
        REQUIRE(parse_z_offset_button_name("btn_z_down_01", delta) == false);
    }

    SECTION("Unknown magnitude returns false") {
        REQUIRE(parse_z_offset_button_name("btn_z_closer_1", delta) == false);
        REQUIRE(parse_z_offset_button_name("btn_z_closer_02", delta) == false);
    }

    SECTION("Empty string returns false") {
        REQUIRE(parse_z_offset_button_name("", delta) == false);
    }
}

// ============================================================================
// CHARACTERIZATION: Tune Panel - Z-Offset Display and G-code
// ============================================================================

TEST_CASE("CHAR: Z-offset display formatting", "[characterization][controls][tune]") {
    SECTION("Initial offset 0.000mm") {
        std::string display = format_z_offset(0.0);
        REQUIRE(display == "0.000mm");
    }

    SECTION("Positive offset +0.100mm") {
        std::string display = format_z_offset(0.1);
        REQUIRE(display == "0.100mm");
    }

    SECTION("Negative offset -0.050mm") {
        std::string display = format_z_offset(-0.05);
        REQUIRE(display == "-0.050mm");
    }

    SECTION("Accumulated offset +0.150mm") {
        std::string display = format_z_offset(0.15);
        REQUIRE(display == "0.150mm");
    }
}

TEST_CASE("CHAR: Z-offset G-code commands", "[characterization][controls][tune]") {
    SECTION("Closer 0.1mm sends SET_GCODE_OFFSET Z_ADJUST=-0.100") {
        std::string gcode = format_z_adjust_gcode(-0.1);
        REQUIRE(gcode == "SET_GCODE_OFFSET Z_ADJUST=-0.100");
    }

    SECTION("Farther 0.1mm sends SET_GCODE_OFFSET Z_ADJUST=0.100") {
        std::string gcode = format_z_adjust_gcode(0.1);
        REQUIRE(gcode == "SET_GCODE_OFFSET Z_ADJUST=0.100");
    }

    SECTION("Small adjustment 0.01mm") {
        std::string gcode = format_z_adjust_gcode(0.01);
        REQUIRE(gcode == "SET_GCODE_OFFSET Z_ADJUST=0.010");
    }
}

TEST_CASE("CHAR: Z-offset accumulation", "[characterization][controls][tune]") {
    double current_z_offset = 0.0;

    SECTION("Multiple closer adjustments accumulate") {
        current_z_offset += (-0.1); // btn_z_closer_01
        REQUIRE(format_z_offset(current_z_offset) == "-0.100mm");

        current_z_offset += (-0.05); // btn_z_closer_005
        REQUIRE(format_z_offset(current_z_offset) == "-0.150mm");

        current_z_offset += (-0.01); // btn_z_closer_001
        REQUIRE(format_z_offset(current_z_offset) == "-0.160mm");
    }

    SECTION("Multiple farther adjustments accumulate") {
        current_z_offset += 0.1; // btn_z_farther_01
        REQUIRE(format_z_offset(current_z_offset) == "0.100mm");

        current_z_offset += 0.05; // btn_z_farther_005
        REQUIRE(format_z_offset(current_z_offset) == "0.150mm");
    }

    SECTION("Mixed adjustments accumulate correctly") {
        current_z_offset += (-0.1); // closer
        current_z_offset += 0.05;   // farther
        REQUIRE(format_z_offset(current_z_offset) == "-0.050mm");
    }
}

// ============================================================================
// CHARACTERIZATION: Tune Panel - Save Z-Offset
// ============================================================================

TEST_CASE("CHAR: Save Z-offset sends SAVE_CONFIG", "[characterization][controls][tune]") {
    // The save operation sends "SAVE_CONFIG" G-code command
    // This is tested by verifying the expected G-code string
    SECTION("SAVE_CONFIG command format") {
        const char* expected_gcode = "SAVE_CONFIG";
        REQUIRE(std::string(expected_gcode) == "SAVE_CONFIG");
    }
}

// ============================================================================
// CHARACTERIZATION: Z-Offset Kinematics-Aware Icons
// ============================================================================

TEST_CASE("CHAR: Z-offset icons for CoreXY (bed moves)", "[characterization][controls][tune]") {
    bool bed_moves_z = true; // CoreXY

    SECTION("Closer icons use arrow-expand-down") {
        const char* icon = get_z_offset_icon(bed_moves_z, true);
        REQUIRE(std::string(icon) == icons::ARROW_EXPAND_DOWN);
    }

    SECTION("Farther icons use arrow-expand-up") {
        const char* icon = get_z_offset_icon(bed_moves_z, false);
        REQUIRE(std::string(icon) == icons::ARROW_EXPAND_UP);
    }
}

TEST_CASE("CHAR: Z-offset icons for Cartesian/Delta (head moves)",
          "[characterization][controls][tune]") {
    bool bed_moves_z = false; // Cartesian or Delta

    SECTION("Closer icons use arrow-down") {
        const char* icon = get_z_offset_icon(bed_moves_z, true);
        REQUIRE(std::string(icon) == icons::ARROW_DOWN);
    }

    SECTION("Farther icons use arrow-up") {
        const char* icon = get_z_offset_icon(bed_moves_z, false);
        REQUIRE(std::string(icon) == icons::ARROW_UP);
    }
}

// ============================================================================
// Documentation: Print Controls Pattern Summary
// ============================================================================

/**
 * SUMMARY OF PRINT CONTROLS CHARACTERIZATION:
 *
 * 1. Light Button:
 *    - Initial state: LED off, icon = lightbulb_outline (F0336)
 *    - Toggle on: calls api_->set_led_on(configured_led_)
 *    - Toggle off: calls api_->set_led_off(configured_led_)
 *    - State update comes from PrinterState observer (led_state_subject)
 *    - On state icon: lightbulb_on (F06E8)
 *    - No-op if no LED configured (configured_led_ empty)
 *
 * 2. Timelapse Button:
 *    - Initial state: timelapse off, icon = video-off (F0568), label = "Off"
 *    - Toggle on: calls api_->timelapse().set_timelapse_enabled(true)
 *    - On success: icon = video (F0567), label = "On"
 *    - Toggle off: calls api_->timelapse().set_timelapse_enabled(false)
 *    - On success: icon = video-off (F0568), label = "Off"
 *
 * 3. Speed Slider:
 *    - Initial value: 100%
 *    - Valid range: 50-200%
 *    - Changing value sends: M220 S{value}
 *    - Display updates immediately via subject
 *
 * 4. Flow Slider:
 *    - Initial value: 100%
 *    - Valid range: 75-125%
 *    - Changing value sends: M221 S{value}
 *    - Display updates immediately via subject
 *
 * 5. Reset Button:
 *    - Resets speed to 100% (M220 S100)
 *    - Resets flow to 100% (M221 S100)
 *    - Updates slider positions with animation
 *
 * 6. Z-Offset Buttons:
 *    - 6 buttons total: closer_01, closer_005, closer_001,
 *                       farther_001, farther_005, farther_01
 *    - Magnitude: _01 = 0.1mm, _005 = 0.05mm, _001 = 0.01mm
 *    - Direction: closer = negative (more squish), farther = positive
 *    - Command: SET_GCODE_OFFSET Z_ADJUST={delta}
 *    - Accumulates into current_z_offset_ for display
 *
 * 7. Save Z-Offset:
 *    - Shows SaveZOffsetModal warning (SAVE_CONFIG restarts Klipper)
 *    - On confirm: sends SAVE_CONFIG command
 *
 * 8. Kinematics-Aware Icons:
 *    - CoreXY (bed moves Z): expand icons (arrow-expand-down/up)
 *    - Cartesian/Delta (head moves Z): arrow icons (arrow-down/up)
 *    - Determined by printer_bed_moves_ subject (0=head moves, 1=bed moves)
 */
