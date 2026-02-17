// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_klipper_config_parser.cpp
 * @brief Unit tests for KlipperConfigParser
 *
 * Tests parsing, roundtrip serialization, value types, multi-line values,
 * comments, modification tracking, and AFC-specific config patterns.
 */

#include "klipper_config_parser.h"

#include "../catch_amalgamated.hpp"

// ============================================================================
// Basic Parsing
// ============================================================================

TEST_CASE("KlipperConfigParser: parse simple section with key-value pairs", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"([printer]
kinematics: cartesian
max_velocity: 300
max_accel: 3000
)";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get("printer", "kinematics") == "cartesian");
    REQUIRE(parser.get("printer", "max_velocity") == "300");
    REQUIRE(parser.get("printer", "max_accel") == "3000");
}

TEST_CASE("KlipperConfigParser: parse section with colon separator", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[stepper_x]\nstep_pin: PF0\ndir_pin: PF1\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get("stepper_x", "step_pin") == "PF0");
    REQUIRE(parser.get("stepper_x", "dir_pin") == "PF1");
}

TEST_CASE("KlipperConfigParser: parse section with equals separator", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[extruder]\nnozzle_diameter = 0.4\nfilament_diameter = 1.75\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get("extruder", "nozzle_diameter") == "0.4");
    REQUIRE(parser.get("extruder", "filament_diameter") == "1.75");
}

TEST_CASE("KlipperConfigParser: parse multiple sections", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"([printer]
kinematics: cartesian

[stepper_x]
step_pin: PF0

[stepper_y]
step_pin: PF6
)";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get("printer", "kinematics") == "cartesian");
    REQUIRE(parser.get("stepper_x", "step_pin") == "PF0");
    REQUIRE(parser.get("stepper_y", "step_pin") == "PF6");
}

TEST_CASE("KlipperConfigParser: has_section returns true/false correctly", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[printer]\nkinematics: cartesian\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.has_section("printer"));
    REQUIRE_FALSE(parser.has_section("extruder"));
}

TEST_CASE("KlipperConfigParser: get_sections returns all section names", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[printer]\nkinematics: cartesian\n\n[extruder]\nnozzle: 0.4\n";
    REQUIRE(parser.parse(content));
    auto sections = parser.get_sections();
    REQUIRE(sections.size() == 2);
    REQUIRE(std::find(sections.begin(), sections.end(), "printer") != sections.end());
    REQUIRE(std::find(sections.begin(), sections.end(), "extruder") != sections.end());
}

TEST_CASE("KlipperConfigParser: get_keys returns keys for a section", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[printer]\nkinematics: cartesian\nmax_velocity: 300\n";
    REQUIRE(parser.parse(content));
    auto keys = parser.get_keys("printer");
    REQUIRE(keys.size() == 2);
    REQUIRE(std::find(keys.begin(), keys.end(), "kinematics") != keys.end());
    REQUIRE(std::find(keys.begin(), keys.end(), "max_velocity") != keys.end());
}

TEST_CASE("KlipperConfigParser: get_keys returns empty for missing section", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[printer]\nkinematics: cartesian\n";
    REQUIRE(parser.parse(content));
    auto keys = parser.get_keys("nonexistent");
    REQUIRE(keys.empty());
}

// ============================================================================
// Section Name Formats
// ============================================================================

TEST_CASE("KlipperConfigParser: simple section name", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[AFC]\nenabled: True\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.has_section("AFC"));
    REQUIRE(parser.get("AFC", "enabled") == "True");
}

TEST_CASE("KlipperConfigParser: prefixed section name - AFC_stepper", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"([AFC_stepper lane1]
extruder: extruder
id: lane1

[AFC_stepper lane2]
extruder: extruder
id: lane2
)";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.has_section("AFC_stepper lane1"));
    REQUIRE(parser.has_section("AFC_stepper lane2"));
    REQUIRE(parser.get("AFC_stepper lane1", "id") == "lane1");
    REQUIRE(parser.get("AFC_stepper lane2", "id") == "lane2");
}

TEST_CASE("KlipperConfigParser: prefixed section name - gcode_macro", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[gcode_macro MY_MACRO]\ngcode:\n    G28\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.has_section("gcode_macro MY_MACRO"));
}

TEST_CASE("KlipperConfigParser: get_sections_matching returns matching prefixes",
          "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"([AFC]
enabled: True

[AFC_stepper lane1]
id: lane1

[AFC_stepper lane2]
id: lane2

[AFC_hub hub1]
id: hub1

[printer]
kinematics: cartesian
)";
    REQUIRE(parser.parse(content));

    auto steppers = parser.get_sections_matching("AFC_stepper");
    REQUIRE(steppers.size() == 2);
    REQUIRE(std::find(steppers.begin(), steppers.end(), "AFC_stepper lane1") != steppers.end());
    REQUIRE(std::find(steppers.begin(), steppers.end(), "AFC_stepper lane2") != steppers.end());

    auto hubs = parser.get_sections_matching("AFC_hub");
    REQUIRE(hubs.size() == 1);
    REQUIRE(hubs[0] == "AFC_hub hub1");

    auto none = parser.get_sections_matching("nonexistent");
    REQUIRE(none.empty());
}

// ============================================================================
// Value Types
// ============================================================================

TEST_CASE("KlipperConfigParser: get returns string value", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[extruder]\nnozzle_diameter: 0.4\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get("extruder", "nozzle_diameter") == "0.4");
}

TEST_CASE("KlipperConfigParser: get returns default for missing key", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[extruder]\nnozzle_diameter: 0.4\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get("extruder", "missing_key", "default_val") == "default_val");
}

TEST_CASE("KlipperConfigParser: get returns default for missing section", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[extruder]\nnozzle_diameter: 0.4\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get("nonexistent", "key", "fallback") == "fallback");
}

TEST_CASE("KlipperConfigParser: get_bool handles all boolean representations", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"([bools]
a: True
b: False
c: true
d: false
e: yes
f: no
g: 1
h: 0
)";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get_bool("bools", "a") == true);
    REQUIRE(parser.get_bool("bools", "b") == false);
    REQUIRE(parser.get_bool("bools", "c") == true);
    REQUIRE(parser.get_bool("bools", "d") == false);
    REQUIRE(parser.get_bool("bools", "e") == true);
    REQUIRE(parser.get_bool("bools", "f") == false);
    REQUIRE(parser.get_bool("bools", "g") == true);
    REQUIRE(parser.get_bool("bools", "h") == false);
}

TEST_CASE("KlipperConfigParser: get_bool returns default for missing key", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[section]\nkey: value\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get_bool("section", "missing", true) == true);
    REQUIRE(parser.get_bool("section", "missing", false) == false);
}

TEST_CASE("KlipperConfigParser: get_float parses float values", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"([extruder]
nozzle_diameter: 0.4
filament_diameter: 1.75
rotation_distance: 33.5
pressure_advance: 0.05
negative: -1.5
)";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get_float("extruder", "nozzle_diameter") == Catch::Approx(0.4f));
    REQUIRE(parser.get_float("extruder", "filament_diameter") == Catch::Approx(1.75f));
    REQUIRE(parser.get_float("extruder", "rotation_distance") == Catch::Approx(33.5f));
    REQUIRE(parser.get_float("extruder", "pressure_advance") == Catch::Approx(0.05f));
    REQUIRE(parser.get_float("extruder", "negative") == Catch::Approx(-1.5f));
    REQUIRE(parser.get_float("extruder", "missing", 99.9f) == Catch::Approx(99.9f));
}

TEST_CASE("KlipperConfigParser: get_int parses integer values", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"([printer]
max_velocity: 300
max_accel: 3000
negative: -10
)";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get_int("printer", "max_velocity") == 300);
    REQUIRE(parser.get_int("printer", "max_accel") == 3000);
    REQUIRE(parser.get_int("printer", "negative") == -10);
    REQUIRE(parser.get_int("printer", "missing", 42) == 42);
}

// ============================================================================
// Multi-line Values
// ============================================================================

TEST_CASE("KlipperConfigParser: parse gcode block with indented continuation", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"([gcode_macro START]
gcode:
    G28
    G1 Z5
    M104 S200
)";
    REQUIRE(parser.parse(content));
    std::string gcode = parser.get("gcode_macro START", "gcode");
    REQUIRE(gcode.find("G28") != std::string::npos);
    REQUIRE(gcode.find("G1 Z5") != std::string::npos);
    REQUIRE(gcode.find("M104 S200") != std::string::npos);
}

TEST_CASE("KlipperConfigParser: multi-line value preserves internal newlines", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"([gcode_macro TEST]
gcode:
    LINE1
    LINE2
    LINE3
)";
    REQUIRE(parser.parse(content));
    std::string gcode = parser.get("gcode_macro TEST", "gcode");
    // Should contain newlines between continuation lines
    auto count = std::count(gcode.begin(), gcode.end(), '\n');
    REQUIRE(count >= 2);
}

TEST_CASE("KlipperConfigParser: multi-line stops at next non-indented line", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"([gcode_macro TEST]
gcode:
    G28
    G1 Z5
other_key: value
)";
    REQUIRE(parser.parse(content));
    std::string gcode = parser.get("gcode_macro TEST", "gcode");
    REQUIRE(gcode.find("G28") != std::string::npos);
    REQUIRE(gcode.find("G1 Z5") != std::string::npos);
    REQUIRE(gcode.find("other_key") == std::string::npos);
    REQUIRE(parser.get("gcode_macro TEST", "other_key") == "value");
}

TEST_CASE("KlipperConfigParser: multi-line stops at next section", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"([gcode_macro TEST]
gcode:
    G28
    G1 Z5

[printer]
kinematics: cartesian
)";
    REQUIRE(parser.parse(content));
    std::string gcode = parser.get("gcode_macro TEST", "gcode");
    REQUIRE(gcode.find("G28") != std::string::npos);
    REQUIRE(gcode.find("cartesian") == std::string::npos);
    REQUIRE(parser.get("printer", "kinematics") == "cartesian");
}

TEST_CASE("KlipperConfigParser: empty multi-line value (key with colon but no inline value)",
          "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"([gcode_macro EMPTY]
gcode:
next_key: value
)";
    REQUIRE(parser.parse(content));
    // gcode has no continuation lines, so value is empty
    std::string gcode = parser.get("gcode_macro EMPTY", "gcode");
    REQUIRE(gcode.empty());
    REQUIRE(parser.get("gcode_macro EMPTY", "next_key") == "value");
}

// ============================================================================
// Comments and Blank Lines
// ============================================================================

TEST_CASE("KlipperConfigParser: comment lines preserved in serialize output", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"(# This is a comment
[printer]
# Another comment
kinematics: cartesian
)";
    REQUIRE(parser.parse(content));
    std::string output = parser.serialize();
    REQUIRE(output.find("# This is a comment") != std::string::npos);
    REQUIRE(output.find("# Another comment") != std::string::npos);
}

TEST_CASE("KlipperConfigParser: blank lines preserved in serialize output", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[printer]\nkinematics: cartesian\n\n[extruder]\nnozzle: 0.4\n";
    REQUIRE(parser.parse(content));
    std::string output = parser.serialize();
    // Should have a blank line between sections
    REQUIRE(output.find("cartesian\n\n[extruder]") != std::string::npos);
}

TEST_CASE("KlipperConfigParser: content with only comments", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "# Comment 1\n# Comment 2\n# Comment 3\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get_sections().empty());
    std::string output = parser.serialize();
    REQUIRE(output.find("# Comment 1") != std::string::npos);
    REQUIRE(output.find("# Comment 2") != std::string::npos);
}

// ============================================================================
// Roundtrip (CRITICAL)
// ============================================================================

TEST_CASE("KlipperConfigParser: parse then serialize produces identical output",
          "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"(# Top comment
[printer]
kinematics: cartesian
max_velocity: 300

# Section comment
[extruder]
nozzle_diameter: 0.4
filament_diameter = 1.75

[gcode_macro START]
gcode:
    G28
    G1 Z5
    M104 S200
)";
    REQUIRE(parser.parse(content));
    std::string output = parser.serialize();
    REQUIRE(output == content);
}

TEST_CASE("KlipperConfigParser: roundtrip preserves colon separator style", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[section]\nkey: value\n";
    REQUIRE(parser.parse(content));
    std::string output = parser.serialize();
    REQUIRE(output == content);
}

TEST_CASE("KlipperConfigParser: roundtrip preserves equals separator style", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[section]\nkey = value\n";
    REQUIRE(parser.parse(content));
    std::string output = parser.serialize();
    REQUIRE(output == content);
}

TEST_CASE("KlipperConfigParser: set value only changes that value on roundtrip",
          "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"(# Top comment
[printer]
kinematics: cartesian
max_velocity: 300

[extruder]
nozzle_diameter: 0.4
)";
    REQUIRE(parser.parse(content));
    parser.set("printer", "max_velocity", "500");
    std::string output = parser.serialize();

    // Changed value should be updated
    REQUIRE(output.find("max_velocity: 500") != std::string::npos);
    // Everything else preserved
    REQUIRE(output.find("# Top comment") != std::string::npos);
    REQUIRE(output.find("kinematics: cartesian") != std::string::npos);
    REQUIRE(output.find("nozzle_diameter: 0.4") != std::string::npos);
    // Old value should be gone
    REQUIRE(output.find("max_velocity: 300") == std::string::npos);
}

TEST_CASE("KlipperConfigParser: roundtrip with complex AFC config", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"(# AFC Configuration
[AFC]
enabled: True

[AFC_stepper lane1]
extruder: extruder
id: lane1
led_index: AFC_Indicator:1

[AFC_stepper lane2]
extruder: extruder
id: lane2
led_index: AFC_Indicator:2
)";
    REQUIRE(parser.parse(content));
    std::string output = parser.serialize();
    REQUIRE(output == content);
}

// ============================================================================
// Modification
// ============================================================================

TEST_CASE("KlipperConfigParser: set changes existing value", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[printer]\nmax_velocity: 300\n";
    REQUIRE(parser.parse(content));
    parser.set("printer", "max_velocity", "500");
    REQUIRE(parser.get("printer", "max_velocity") == "500");
}

TEST_CASE("KlipperConfigParser: set new key in existing section appends it", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[printer]\nkinematics: cartesian\n";
    REQUIRE(parser.parse(content));
    parser.set("printer", "max_velocity", "300");
    REQUIRE(parser.get("printer", "max_velocity") == "300");

    std::string output = parser.serialize();
    REQUIRE(output.find("max_velocity: 300") != std::string::npos);
}

TEST_CASE("KlipperConfigParser: is_modified returns false after parse, true after set",
          "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[printer]\nmax_velocity: 300\n";
    REQUIRE(parser.parse(content));
    REQUIRE_FALSE(parser.is_modified());

    parser.set("printer", "max_velocity", "500");
    REQUIRE(parser.is_modified());
}

TEST_CASE("KlipperConfigParser: multiple set calls work correctly", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[printer]\nkinematics: cartesian\nmax_velocity: 300\nmax_accel: 3000\n";
    REQUIRE(parser.parse(content));

    parser.set("printer", "max_velocity", "500");
    parser.set("printer", "max_accel", "5000");

    REQUIRE(parser.get("printer", "max_velocity") == "500");
    REQUIRE(parser.get("printer", "max_accel") == "5000");
    REQUIRE(parser.get("printer", "kinematics") == "cartesian");
}

TEST_CASE("KlipperConfigParser: set preserves original separator style", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[section]\ncolon_key: old_val\nequals_key = old_val\n";
    REQUIRE(parser.parse(content));

    parser.set("section", "colon_key", "new_val");
    parser.set("section", "equals_key", "new_val");

    std::string output = parser.serialize();
    REQUIRE(output.find("colon_key: new_val") != std::string::npos);
    REQUIRE(output.find("equals_key = new_val") != std::string::npos);
}

// ============================================================================
// AFC-specific
// ============================================================================

TEST_CASE("KlipperConfigParser: parse real AFC.cfg snippet", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"([AFC]
Type: Box_Turtle
tool_stn: 72
tool_stn_unload: 100

[AFC_hub hub1]
id: hub1
switch_pin: mcu:PA0

[AFC_stepper lane1]
extruder: extruder
id: lane1
hub: hub1
led_index: AFC_Indicator:1
prep: mcu:PA1
load: mcu:PA2

[AFC_stepper lane2]
extruder: extruder
id: lane2
hub: hub1
led_index: AFC_Indicator:2
prep: mcu:PA3
load: mcu:PA4
)";
    REQUIRE(parser.parse(content));

    REQUIRE(parser.has_section("AFC"));
    REQUIRE(parser.has_section("AFC_hub hub1"));
    REQUIRE(parser.has_section("AFC_stepper lane1"));
    REQUIRE(parser.has_section("AFC_stepper lane2"));

    REQUIRE(parser.get("AFC", "Type") == "Box_Turtle");
    REQUIRE(parser.get_int("AFC", "tool_stn") == 72);
    REQUIRE(parser.get("AFC_hub hub1", "switch_pin") == "mcu:PA0");
    REQUIRE(parser.get("AFC_stepper lane1", "hub") == "hub1");
}

TEST_CASE("KlipperConfigParser: parse AFC_Macro_Vars with variable_* keys", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"([gcode_macro AFC_Macro_Vars]
variable_travel_speed: 100
variable_z_travel_speed: 50
variable_tip_distance: 0
variable_toolhead_sensor_pin: mcu:PG12
variable_ramming_volume: 0
variable_unloading_speed_start: 80
variable_unloading_speed: 18
)";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get_int("gcode_macro AFC_Macro_Vars", "variable_travel_speed") == 100);
    REQUIRE(parser.get_int("gcode_macro AFC_Macro_Vars", "variable_ramming_volume") == 0);
    REQUIRE(parser.get("gcode_macro AFC_Macro_Vars", "variable_toolhead_sensor_pin") == "mcu:PG12");
}

TEST_CASE("KlipperConfigParser: get_sections_matching for AFC_hub sections", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"([AFC]
enabled: True

[AFC_hub hub1]
id: hub1

[AFC_hub hub2]
id: hub2

[AFC_stepper lane1]
id: lane1
)";
    REQUIRE(parser.parse(content));

    auto hubs = parser.get_sections_matching("AFC_hub");
    REQUIRE(hubs.size() == 2);
}

TEST_CASE("KlipperConfigParser: modify variable_ramming_volume and roundtrip", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = R"([gcode_macro AFC_Macro_Vars]
variable_travel_speed: 100
variable_ramming_volume: 0
variable_unloading_speed: 18
)";
    REQUIRE(parser.parse(content));
    parser.set("gcode_macro AFC_Macro_Vars", "variable_ramming_volume", "20");

    std::string output = parser.serialize();
    REQUIRE(output.find("variable_ramming_volume: 20") != std::string::npos);
    REQUIRE(output.find("variable_travel_speed: 100") != std::string::npos);
    REQUIRE(output.find("variable_unloading_speed: 18") != std::string::npos);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("KlipperConfigParser: empty content", "[klipper_config]") {
    KlipperConfigParser parser;
    REQUIRE(parser.parse(""));
    REQUIRE(parser.get_sections().empty());
    REQUIRE(parser.serialize().empty());
}

TEST_CASE("KlipperConfigParser: section with no keys", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[empty_section]\n\n[other]\nkey: val\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.has_section("empty_section"));
    REQUIRE(parser.get_keys("empty_section").empty());
    REQUIRE(parser.has_section("other"));
}

TEST_CASE("KlipperConfigParser: value with special characters - paths", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content =
        "[section]\nserial: /dev/serial/by-id/usb-Klipper_stm32f446xx_12345-if00\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get("section", "serial") ==
            "/dev/serial/by-id/usb-Klipper_stm32f446xx_12345-if00");
}

TEST_CASE("KlipperConfigParser: value with special characters - URLs", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[section]\nurl: http://localhost:7125/api/version\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get("section", "url") == "http://localhost:7125/api/version");
}

TEST_CASE("KlipperConfigParser: value with colons (like pin references)", "[klipper_config]") {
    KlipperConfigParser parser;
    // The first colon (with space after) is the separator; the rest is part of the value
    std::string content = "[stepper]\nstep_pin: mcu:PF0\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get("stepper", "step_pin") == "mcu:PF0");
}

TEST_CASE("KlipperConfigParser: very long value", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string long_val(1000, 'x');
    std::string content = "[section]\nlong_key: " + long_val + "\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get("section", "long_key") == long_val);
}

TEST_CASE("KlipperConfigParser: whitespace trimming on key and value", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[section]\n  key_with_spaces  :  value_with_spaces  \n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get("section", "key_with_spaces") == "value_with_spaces");
}

TEST_CASE("KlipperConfigParser: trailing comments are NOT treated as inline comments",
          "[klipper_config]") {
    KlipperConfigParser parser;
    // Klipper does NOT support inline comments - the # is part of the value
    std::string content = "[section]\npin: PA0 # this is part of the value\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get("section", "pin") == "PA0 # this is part of the value");
}

TEST_CASE("KlipperConfigParser: key with empty value after separator", "[klipper_config]") {
    KlipperConfigParser parser;
    std::string content = "[section]\nempty_colon:\nempty_equals =\n";
    REQUIRE(parser.parse(content));
    REQUIRE(parser.get("section", "empty_colon").empty());
    REQUIRE(parser.get("section", "empty_equals").empty());
}
