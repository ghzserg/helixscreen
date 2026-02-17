// SPDX-License-Identifier: GPL-3.0-or-later

#include "afc_config_manager.h"
#include "klipper_config_parser.h"

#include <string>

#include "../catch_amalgamated.hpp"

// Realistic AFC.cfg content for test fixtures
static const std::string AFC_CFG_CONTENT = R"(# AFC Configuration
[AFC]
tool_start: extruder
tool_end: extruder
default_material_temps: PLA:210, ABS:250, PETG:235

[AFC_hub Turtle_1]
afc_bowden_length: 450
cut: True
cut_dist: 40
assisted_retract: False

[AFC_stepper lane1]
extruder: extruder
hub: Turtle_1

[AFC_stepper lane2]
extruder: extruder
hub: Turtle_1
)";

static const std::string AFC_MACRO_VARS_CONTENT = R"([gcode_macro AFC_MacroVars]
variable_ramming_volume: 0
variable_unloading_speed_start: 80
variable_cooling_tube_length: 15
variable_cooling_tube_retraction: 35
variable_purge_enabled: True
variable_purge_length: 30
variable_brush_enabled: False
)";

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("AfcConfigManager construction with nullptr API", "[afc_config]") {
    AfcConfigManager mgr(nullptr);

    CHECK_FALSE(mgr.is_loaded());
    CHECK_FALSE(mgr.has_unsaved_changes());
    CHECK(mgr.loaded_filename().empty());
}

// ============================================================================
// Parser access
// ============================================================================

TEST_CASE("AfcConfigManager parser access returns usable reference", "[afc_config]") {
    AfcConfigManager mgr(nullptr);

    // Parse content directly into the parser
    REQUIRE(mgr.parser().parse(AFC_CFG_CONTENT));

    // Verify we can read parsed values
    CHECK(mgr.parser().get("AFC", "tool_start") == "extruder");
    CHECK(mgr.parser().get_bool("AFC_hub Turtle_1", "cut") == true);
    CHECK(mgr.parser().get_int("AFC_hub Turtle_1", "afc_bowden_length") == 450);
}

TEST_CASE("AfcConfigManager const parser access", "[afc_config]") {
    AfcConfigManager mgr(nullptr);
    mgr.parser().parse(AFC_CFG_CONTENT);

    const auto& const_mgr = mgr;
    CHECK(const_mgr.parser().get("AFC", "tool_end") == "extruder");
}

// ============================================================================
// Dirty tracking
// ============================================================================

TEST_CASE("AfcConfigManager dirty tracking - initial state", "[afc_config]") {
    AfcConfigManager mgr(nullptr);
    CHECK_FALSE(mgr.has_unsaved_changes());
}

TEST_CASE("AfcConfigManager dirty tracking - mark_dirty", "[afc_config]") {
    AfcConfigManager mgr(nullptr);
    mgr.mark_dirty();
    CHECK(mgr.has_unsaved_changes());
}

TEST_CASE("AfcConfigManager dirty tracking - mark then clear via discard", "[afc_config]") {
    AfcConfigManager mgr(nullptr);

    // Simulate a load by setting content directly
    mgr.load_from_string(AFC_CFG_CONTENT, "AFC/AFC.cfg");

    mgr.mark_dirty();
    CHECK(mgr.has_unsaved_changes());

    mgr.discard_changes();
    CHECK_FALSE(mgr.has_unsaved_changes());
}

// ============================================================================
// Discard changes
// ============================================================================

TEST_CASE("AfcConfigManager discard reverts to original content", "[afc_config]") {
    AfcConfigManager mgr(nullptr);

    // Load content
    mgr.load_from_string(AFC_CFG_CONTENT, "AFC/AFC.cfg");

    // Verify original values
    CHECK(mgr.parser().get_int("AFC_hub Turtle_1", "afc_bowden_length") == 450);
    CHECK(mgr.parser().get_int("AFC_hub Turtle_1", "cut_dist") == 40);

    // Modify values
    mgr.parser().set("AFC_hub Turtle_1", "afc_bowden_length", "600");
    mgr.parser().set("AFC_hub Turtle_1", "cut_dist", "50");
    mgr.mark_dirty();

    // Verify modifications took effect
    CHECK(mgr.parser().get_int("AFC_hub Turtle_1", "afc_bowden_length") == 600);
    CHECK(mgr.parser().get_int("AFC_hub Turtle_1", "cut_dist") == 50);

    // Discard changes
    mgr.discard_changes();

    // Verify original values are restored
    CHECK(mgr.parser().get_int("AFC_hub Turtle_1", "afc_bowden_length") == 450);
    CHECK(mgr.parser().get_int("AFC_hub Turtle_1", "cut_dist") == 40);
    CHECK_FALSE(mgr.has_unsaved_changes());
}

TEST_CASE("AfcConfigManager discard with no prior load is safe", "[afc_config]") {
    AfcConfigManager mgr(nullptr);
    mgr.mark_dirty();

    // Discard without loading should not crash and should clear dirty flag
    mgr.discard_changes();
    CHECK_FALSE(mgr.has_unsaved_changes());
}

// ============================================================================
// State queries
// ============================================================================

TEST_CASE("AfcConfigManager is_loaded initially false", "[afc_config]") {
    AfcConfigManager mgr(nullptr);
    CHECK_FALSE(mgr.is_loaded());
}

TEST_CASE("AfcConfigManager is_loaded true after load_from_string", "[afc_config]") {
    AfcConfigManager mgr(nullptr);
    mgr.load_from_string(AFC_CFG_CONTENT, "AFC/AFC.cfg");
    CHECK(mgr.is_loaded());
}

TEST_CASE("AfcConfigManager loaded_filename tracks filename", "[afc_config]") {
    AfcConfigManager mgr(nullptr);
    CHECK(mgr.loaded_filename().empty());

    mgr.load_from_string(AFC_CFG_CONTENT, "AFC/AFC.cfg");
    CHECK(mgr.loaded_filename() == "AFC/AFC.cfg");

    mgr.load_from_string(AFC_MACRO_VARS_CONTENT, "AFC/AFC_Macro_Vars.cfg");
    CHECK(mgr.loaded_filename() == "AFC/AFC_Macro_Vars.cfg");
}

// ============================================================================
// Integration with KlipperConfigParser
// ============================================================================

TEST_CASE("AfcConfigManager full workflow: parse, modify, discard, verify", "[afc_config]") {
    AfcConfigManager mgr(nullptr);
    mgr.load_from_string(AFC_CFG_CONTENT, "AFC/AFC.cfg");

    // Read sections
    auto sections = mgr.parser().get_sections_matching("AFC_stepper");
    REQUIRE(sections.size() == 2);
    CHECK(sections[0] == "AFC_stepper lane1");
    CHECK(sections[1] == "AFC_stepper lane2");

    // Read values
    CHECK(mgr.parser().get("AFC_stepper lane1", "hub") == "Turtle_1");
    CHECK(mgr.parser().get("AFC", "default_material_temps") == "PLA:210, ABS:250, PETG:235");

    // Modify a value and mark dirty
    mgr.parser().set("AFC", "default_material_temps", "PLA:215, ABS:260, PETG:240");
    mgr.mark_dirty();
    CHECK(mgr.has_unsaved_changes());

    // Verify modification is visible
    CHECK(mgr.parser().get("AFC", "default_material_temps") == "PLA:215, ABS:260, PETG:240");

    // Discard and verify original restored
    mgr.discard_changes();
    CHECK(mgr.parser().get("AFC", "default_material_temps") == "PLA:210, ABS:250, PETG:235");
}

TEST_CASE("AfcConfigManager with AFC_Macro_Vars content", "[afc_config]") {
    AfcConfigManager mgr(nullptr);
    mgr.load_from_string(AFC_MACRO_VARS_CONTENT, "AFC/AFC_Macro_Vars.cfg");

    CHECK(mgr.is_loaded());
    CHECK(mgr.parser().get_int("gcode_macro AFC_MacroVars", "variable_ramming_volume") == 0);
    CHECK(mgr.parser().get_int("gcode_macro AFC_MacroVars", "variable_unloading_speed_start") ==
          80);
    CHECK(mgr.parser().get_bool("gcode_macro AFC_MacroVars", "variable_purge_enabled") == true);
    CHECK(mgr.parser().get_bool("gcode_macro AFC_MacroVars", "variable_brush_enabled") == false);
    CHECK(mgr.parser().get_int("gcode_macro AFC_MacroVars", "variable_purge_length") == 30);
}

TEST_CASE("AfcConfigManager serialize produces saveable content", "[afc_config]") {
    AfcConfigManager mgr(nullptr);
    mgr.load_from_string(AFC_CFG_CONTENT, "AFC/AFC.cfg");

    // Modify a value
    mgr.parser().set("AFC_hub Turtle_1", "afc_bowden_length", "500");
    mgr.mark_dirty();

    // Serialize should reflect the modification
    std::string serialized = mgr.parser().serialize();
    REQUIRE_FALSE(serialized.empty());

    // Re-parse the serialized content and verify
    KlipperConfigParser verify;
    REQUIRE(verify.parse(serialized));
    CHECK(verify.get_int("AFC_hub Turtle_1", "afc_bowden_length") == 500);
    // Other values should be preserved
    CHECK(verify.get("AFC", "tool_start") == "extruder");
    CHECK(verify.get_bool("AFC_hub Turtle_1", "cut") == true);
}

TEST_CASE("AfcConfigManager load_from_string resets previous state", "[afc_config]") {
    AfcConfigManager mgr(nullptr);

    // First load
    mgr.load_from_string(AFC_CFG_CONTENT, "AFC/AFC.cfg");
    mgr.parser().set("AFC", "tool_start", "extruder1");
    mgr.mark_dirty();
    CHECK(mgr.has_unsaved_changes());

    // Second load should reset everything
    mgr.load_from_string(AFC_MACRO_VARS_CONTENT, "AFC/AFC_Macro_Vars.cfg");
    CHECK_FALSE(mgr.has_unsaved_changes());
    CHECK(mgr.loaded_filename() == "AFC/AFC_Macro_Vars.cfg");
    CHECK_FALSE(mgr.parser().has_section("AFC"));
    CHECK(mgr.parser().has_section("gcode_macro AFC_MacroVars"));
}
