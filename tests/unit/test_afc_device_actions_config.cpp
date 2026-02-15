// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_afc_device_actions_config.cpp
 * @brief Unit tests for AFC config-backed device actions (Phase 3)
 *
 * Tests for AFC configuration file integration with device actions:
 * - Hub & Cutter settings (from AFC.cfg)
 * - Tip Forming settings (from AFC_Macro_Vars.cfg)
 * - Purge & Wipe settings (from AFC_Macro_Vars.cfg)
 * - Save & Restart action
 */

#include "afc_config_manager.h"
#include "ams_backend_afc.h"
#include "ams_types.h"

#include <algorithm>
#include <any>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::printer;

// Sample AFC.cfg content for tests
static const char* SAMPLE_AFC_CFG = R"(
[AFC]
tool_start: direct

[AFC_hub Turtle_1]
cut: True
cut_dist: 42.5
afc_bowden_length: 450
assisted_retract: False
)";

// Sample AFC_Macro_Vars.cfg content for tests
static const char* SAMPLE_MACRO_VARS_CFG = R"(
[gcode_macro AFC_MacroVars]
variable_ramming_volume: 20
variable_unloading_speed_start: 80
variable_cooling_tube_length: 15
variable_cooling_tube_retraction: 35
variable_purge_enabled: True
variable_purge_length: 50
variable_brush_enabled: False
)";

/**
 * @brief Test helper to access AmsBackendAfc private config members
 */
class AmsBackendAfcConfigHelper {
  public:
    static void set_configs_loaded(AmsBackendAfc& backend, bool loaded) {
        backend.configs_loaded_ = loaded;
    }

    static AfcConfigManager* get_afc_config(AmsBackendAfc& backend) {
        return backend.afc_config_.get();
    }

    static AfcConfigManager* get_macro_vars_config(AmsBackendAfc& backend) {
        return backend.macro_vars_config_.get();
    }

    static void create_configs(AmsBackendAfc& backend) {
        backend.afc_config_ = std::make_unique<AfcConfigManager>(nullptr);
        backend.macro_vars_config_ = std::make_unique<AfcConfigManager>(nullptr);
    }

    static void load_test_configs(AmsBackendAfc& backend) {
        create_configs(backend);
        backend.afc_config_->load_from_string(SAMPLE_AFC_CFG, "AFC/AFC.cfg");
        backend.macro_vars_config_->load_from_string(SAMPLE_MACRO_VARS_CFG,
                                                     "AFC/AFC_Macro_Vars.cfg");
        backend.configs_loaded_ = true;
    }
};

// Helper to find action by ID
static const DeviceAction* find_action(const std::vector<DeviceAction>& actions,
                                       const std::string& id) {
    auto it = std::find_if(actions.begin(), actions.end(),
                           [&id](const DeviceAction& a) { return a.id == id; });
    return it != actions.end() ? &(*it) : nullptr;
}

// Helper to find section by ID
static const DeviceSection* find_section(const std::vector<DeviceSection>& sections,
                                         const std::string& id) {
    auto it = std::find_if(sections.begin(), sections.end(),
                           [&id](const DeviceSection& s) { return s.id == id; });
    return it != sections.end() ? &(*it) : nullptr;
}

// =============================================================================
// Device Sections Tests
// =============================================================================

TEST_CASE("New device sections include hub, tip_forming, purge",
          "[ams][afc][device_actions][config]") {
    AmsBackendAfc backend(nullptr, nullptr);

    auto sections = backend.get_device_sections();

    SECTION("hub section present") {
        auto* hub = find_section(sections, "hub");
        REQUIRE(hub != nullptr);
        CHECK(hub->label == "Hub & Cutter");
        CHECK(hub->icon == "content-cut");
        CHECK(hub->display_order == 4);
    }

    SECTION("tip_forming section present") {
        auto* tip = find_section(sections, "tip_forming");
        REQUIRE(tip != nullptr);
        CHECK(tip->label == "Tip Forming");
        CHECK(tip->icon == "thermometer");
        CHECK(tip->display_order == 5);
    }

    SECTION("purge section present") {
        auto* purge = find_section(sections, "purge");
        REQUIRE(purge != nullptr);
        CHECK(purge->label == "Purge & Wipe");
        CHECK(purge->icon == "spray");
        CHECK(purge->display_order == 6);
    }

    SECTION("config section present") {
        auto* config = find_section(sections, "config");
        REQUIRE(config != nullptr);
        CHECK(config->label == "Configuration");
        CHECK(config->display_order == 7);
    }

    SECTION("original sections still present") {
        CHECK(find_section(sections, "calibration") != nullptr);
        CHECK(find_section(sections, "speed") != nullptr);
        CHECK(find_section(sections, "maintenance") != nullptr);
        CHECK(find_section(sections, "led") != nullptr);
    }
}

// =============================================================================
// Hub Actions Tests
// =============================================================================

TEST_CASE("Hub actions present when config loaded", "[ams][afc][device_actions][config]") {
    AmsBackendAfc backend(nullptr, nullptr);
    AmsBackendAfcConfigHelper::load_test_configs(backend);

    auto actions = backend.get_device_actions();

    SECTION("hub_cut_enabled is a toggle with correct value") {
        auto* action = find_action(actions, "hub_cut_enabled");
        REQUIRE(action != nullptr);
        CHECK(action->type == ActionType::TOGGLE);
        CHECK(action->section == "hub");
        CHECK(action->enabled == true);
        REQUIRE(action->current_value.has_value());
        CHECK(std::any_cast<bool>(action->current_value) == true);
    }

    SECTION("hub_cut_dist is a slider with correct value") {
        auto* action = find_action(actions, "hub_cut_dist");
        REQUIRE(action != nullptr);
        CHECK(action->type == ActionType::SLIDER);
        CHECK(action->section == "hub");
        CHECK(action->enabled == true);
        CHECK(action->unit == "mm");
        REQUIRE(action->current_value.has_value());
        CHECK(std::any_cast<float>(action->current_value) == Catch::Approx(42.5f));
    }

    SECTION("hub_bowden_length is a slider with correct value") {
        auto* action = find_action(actions, "hub_bowden_length");
        REQUIRE(action != nullptr);
        CHECK(action->type == ActionType::SLIDER);
        CHECK(action->section == "hub");
        CHECK(action->enabled == true);
        CHECK(action->unit == "mm");
        CHECK(action->min_value == Catch::Approx(100.0f));
        CHECK(action->max_value == Catch::Approx(2000.0f));
        REQUIRE(action->current_value.has_value());
        CHECK(std::any_cast<float>(action->current_value) == Catch::Approx(450.0f));
    }

    SECTION("assisted_retract is a toggle with correct value") {
        auto* action = find_action(actions, "assisted_retract");
        REQUIRE(action != nullptr);
        CHECK(action->type == ActionType::TOGGLE);
        CHECK(action->section == "hub");
        CHECK(action->enabled == true);
        REQUIRE(action->current_value.has_value());
        CHECK(std::any_cast<bool>(action->current_value) == false);
    }
}

TEST_CASE("Hub actions disabled when config not loaded", "[ams][afc][device_actions][config]") {
    AmsBackendAfc backend(nullptr, nullptr);
    // Do NOT load configs — configs_loaded_ remains false

    auto actions = backend.get_device_actions();

    SECTION("hub_cut_enabled is disabled") {
        auto* action = find_action(actions, "hub_cut_enabled");
        REQUIRE(action != nullptr);
        CHECK(action->enabled == false);
        CHECK(action->disable_reason == "Loading configuration...");
    }

    SECTION("hub_cut_dist is disabled") {
        auto* action = find_action(actions, "hub_cut_dist");
        REQUIRE(action != nullptr);
        CHECK(action->enabled == false);
        CHECK(action->disable_reason == "Loading configuration...");
    }

    SECTION("hub_bowden_length is disabled") {
        auto* action = find_action(actions, "hub_bowden_length");
        REQUIRE(action != nullptr);
        CHECK(action->enabled == false);
    }

    SECTION("assisted_retract is disabled") {
        auto* action = find_action(actions, "assisted_retract");
        REQUIRE(action != nullptr);
        CHECK(action->enabled == false);
    }
}

// =============================================================================
// Tip Forming Actions Tests
// =============================================================================

TEST_CASE("Tip forming actions read macro vars", "[ams][afc][device_actions][config]") {
    AmsBackendAfc backend(nullptr, nullptr);
    AmsBackendAfcConfigHelper::load_test_configs(backend);

    auto actions = backend.get_device_actions();

    SECTION("ramming_volume reads correct value") {
        auto* action = find_action(actions, "ramming_volume");
        REQUIRE(action != nullptr);
        CHECK(action->type == ActionType::SLIDER);
        CHECK(action->section == "tip_forming");
        CHECK(action->enabled == true);
        REQUIRE(action->current_value.has_value());
        CHECK(std::any_cast<float>(action->current_value) == Catch::Approx(20.0f));
    }

    SECTION("unloading_speed_start reads correct value") {
        auto* action = find_action(actions, "unloading_speed_start");
        REQUIRE(action != nullptr);
        CHECK(action->type == ActionType::SLIDER);
        CHECK(action->unit == "mm/s");
        CHECK(action->max_value == Catch::Approx(200.0f));
        REQUIRE(action->current_value.has_value());
        CHECK(std::any_cast<float>(action->current_value) == Catch::Approx(80.0f));
    }

    SECTION("cooling_tube_length reads correct value") {
        auto* action = find_action(actions, "cooling_tube_length");
        REQUIRE(action != nullptr);
        CHECK(action->type == ActionType::SLIDER);
        CHECK(action->unit == "mm");
        REQUIRE(action->current_value.has_value());
        CHECK(std::any_cast<float>(action->current_value) == Catch::Approx(15.0f));
    }

    SECTION("cooling_tube_retraction reads correct value") {
        auto* action = find_action(actions, "cooling_tube_retraction");
        REQUIRE(action != nullptr);
        CHECK(action->type == ActionType::SLIDER);
        CHECK(action->unit == "mm");
        REQUIRE(action->current_value.has_value());
        CHECK(std::any_cast<float>(action->current_value) == Catch::Approx(35.0f));
    }
}

// =============================================================================
// Purge Actions Tests
// =============================================================================

TEST_CASE("Purge actions read macro vars", "[ams][afc][device_actions][config]") {
    AmsBackendAfc backend(nullptr, nullptr);
    AmsBackendAfcConfigHelper::load_test_configs(backend);

    auto actions = backend.get_device_actions();

    SECTION("purge_enabled reads correct value") {
        auto* action = find_action(actions, "purge_enabled");
        REQUIRE(action != nullptr);
        CHECK(action->type == ActionType::TOGGLE);
        CHECK(action->section == "purge");
        CHECK(action->enabled == true);
        REQUIRE(action->current_value.has_value());
        CHECK(std::any_cast<bool>(action->current_value) == true);
    }

    SECTION("purge_length reads correct value") {
        auto* action = find_action(actions, "purge_length");
        REQUIRE(action != nullptr);
        CHECK(action->type == ActionType::SLIDER);
        CHECK(action->unit == "mm");
        CHECK(action->max_value == Catch::Approx(200.0f));
        REQUIRE(action->current_value.has_value());
        CHECK(std::any_cast<float>(action->current_value) == Catch::Approx(50.0f));
    }

    SECTION("brush_enabled reads correct value") {
        auto* action = find_action(actions, "brush_enabled");
        REQUIRE(action != nullptr);
        CHECK(action->type == ActionType::TOGGLE);
        CHECK(action->section == "purge");
        REQUIRE(action->current_value.has_value());
        CHECK(std::any_cast<bool>(action->current_value) == false);
    }
}

// =============================================================================
// Execute Hub Toggle Tests
// =============================================================================

TEST_CASE("Execute hub toggle modifies config", "[ams][afc][device_actions][config]") {
    AmsBackendAfc backend(nullptr, nullptr);
    AmsBackendAfcConfigHelper::load_test_configs(backend);

    SECTION("toggle hub_cut_enabled to false") {
        auto result = backend.execute_device_action("hub_cut_enabled", std::any(false));
        CHECK(result.success());

        // Verify config was modified
        auto* cfg = AmsBackendAfcConfigHelper::get_afc_config(backend);
        REQUIRE(cfg != nullptr);
        CHECK(cfg->parser().get_bool("AFC_hub Turtle_1", "cut", true) == false);
        CHECK(cfg->has_unsaved_changes());
    }

    SECTION("toggle assisted_retract to true") {
        auto result = backend.execute_device_action("assisted_retract", std::any(true));
        CHECK(result.success());

        auto* cfg = AmsBackendAfcConfigHelper::get_afc_config(backend);
        REQUIRE(cfg != nullptr);
        CHECK(cfg->parser().get_bool("AFC_hub Turtle_1", "assisted_retract", false) == true);
        CHECK(cfg->has_unsaved_changes());
    }
}

// =============================================================================
// Execute Macro Var Slider Tests
// =============================================================================

TEST_CASE("Execute macro var slider modifies config", "[ams][afc][device_actions][config]") {
    AmsBackendAfc backend(nullptr, nullptr);
    AmsBackendAfcConfigHelper::load_test_configs(backend);

    SECTION("change ramming_volume") {
        auto result = backend.execute_device_action("ramming_volume", std::any(45.0f));
        CHECK(result.success());

        auto* cfg = AmsBackendAfcConfigHelper::get_macro_vars_config(backend);
        REQUIRE(cfg != nullptr);
        CHECK(cfg->parser().get_float("gcode_macro AFC_MacroVars", "variable_ramming_volume",
                                      0.0f) == Catch::Approx(45.0f));
        CHECK(cfg->has_unsaved_changes());
    }

    SECTION("change purge_length") {
        auto result = backend.execute_device_action("purge_length", std::any(100.0f));
        CHECK(result.success());

        auto* cfg = AmsBackendAfcConfigHelper::get_macro_vars_config(backend);
        REQUIRE(cfg != nullptr);
        CHECK(cfg->parser().get_float("gcode_macro AFC_MacroVars", "variable_purge_length", 0.0f) ==
              Catch::Approx(100.0f));
        CHECK(cfg->has_unsaved_changes());
    }

    SECTION("toggle purge_enabled") {
        auto result = backend.execute_device_action("purge_enabled", std::any(false));
        CHECK(result.success());

        auto* cfg = AmsBackendAfcConfigHelper::get_macro_vars_config(backend);
        REQUIRE(cfg != nullptr);
        CHECK(cfg->parser().get_bool("gcode_macro AFC_MacroVars", "variable_purge_enabled", true) ==
              false);
        CHECK(cfg->has_unsaved_changes());
    }
}

// =============================================================================
// Save Restart Action Tests
// =============================================================================

TEST_CASE("Save restart action enabled only when dirty", "[ams][afc][device_actions][config]") {
    AmsBackendAfc backend(nullptr, nullptr);
    AmsBackendAfcConfigHelper::load_test_configs(backend);

    SECTION("disabled when no changes") {
        auto actions = backend.get_device_actions();
        auto* save_action = find_action(actions, "save_restart");
        REQUIRE(save_action != nullptr);
        CHECK(save_action->enabled == false);
        CHECK(save_action->disable_reason == "No unsaved changes");
    }

    SECTION("enabled after modifying config") {
        // Make a change to mark dirty
        backend.execute_device_action("hub_cut_enabled", std::any(false));

        auto actions = backend.get_device_actions();
        auto* save_action = find_action(actions, "save_restart");
        REQUIRE(save_action != nullptr);
        CHECK(save_action->enabled == true);
        CHECK(save_action->disable_reason.empty());
    }

    SECTION("enabled after modifying macro vars") {
        backend.execute_device_action("ramming_volume", std::any(30.0f));

        auto actions = backend.get_device_actions();
        auto* save_action = find_action(actions, "save_restart");
        REQUIRE(save_action != nullptr);
        CHECK(save_action->enabled == true);
    }

    SECTION("execute fails when no changes") {
        auto result = backend.execute_device_action("save_restart");
        CHECK_FALSE(result.success());
    }
}

// =============================================================================
// Config Values Match Parser Tests
// =============================================================================

TEST_CASE("Config sections show correct values from parser", "[ams][afc][device_actions][config]") {
    AmsBackendAfc backend(nullptr, nullptr);
    AmsBackendAfcConfigHelper::create_configs(backend);

    // Load custom config with different values
    const char* custom_afc = R"(
[AFC_hub MyHub]
cut: False
cut_dist: 75.0
afc_bowden_length: 600
assisted_retract: True
)";

    const char* custom_macros = R"(
[gcode_macro AFC_MacroVars]
variable_ramming_volume: 55
variable_unloading_speed_start: 120
variable_cooling_tube_length: 25
variable_cooling_tube_retraction: 40
variable_purge_enabled: False
variable_purge_length: 100
variable_brush_enabled: True
)";

    AmsBackendAfcConfigHelper::get_afc_config(backend)->load_from_string(custom_afc, "AFC/AFC.cfg");
    AmsBackendAfcConfigHelper::get_macro_vars_config(backend)->load_from_string(
        custom_macros, "AFC/AFC_Macro_Vars.cfg");
    AmsBackendAfcConfigHelper::set_configs_loaded(backend, true);

    auto actions = backend.get_device_actions();

    SECTION("hub values reflect custom config") {
        auto* cut = find_action(actions, "hub_cut_enabled");
        REQUIRE(cut != nullptr);
        CHECK(std::any_cast<bool>(cut->current_value) == false);

        auto* dist = find_action(actions, "hub_cut_dist");
        REQUIRE(dist != nullptr);
        CHECK(std::any_cast<float>(dist->current_value) == Catch::Approx(75.0f));

        auto* bowden = find_action(actions, "hub_bowden_length");
        REQUIRE(bowden != nullptr);
        CHECK(std::any_cast<float>(bowden->current_value) == Catch::Approx(600.0f));

        auto* retract = find_action(actions, "assisted_retract");
        REQUIRE(retract != nullptr);
        CHECK(std::any_cast<bool>(retract->current_value) == true);
    }

    SECTION("macro var values reflect custom config") {
        auto* ramming = find_action(actions, "ramming_volume");
        REQUIRE(ramming != nullptr);
        CHECK(std::any_cast<float>(ramming->current_value) == Catch::Approx(55.0f));

        auto* speed = find_action(actions, "unloading_speed_start");
        REQUIRE(speed != nullptr);
        CHECK(std::any_cast<float>(speed->current_value) == Catch::Approx(120.0f));

        auto* tube_len = find_action(actions, "cooling_tube_length");
        REQUIRE(tube_len != nullptr);
        CHECK(std::any_cast<float>(tube_len->current_value) == Catch::Approx(25.0f));

        auto* tube_ret = find_action(actions, "cooling_tube_retraction");
        REQUIRE(tube_ret != nullptr);
        CHECK(std::any_cast<float>(tube_ret->current_value) == Catch::Approx(40.0f));
    }

    SECTION("purge values reflect custom config") {
        auto* purge_en = find_action(actions, "purge_enabled");
        REQUIRE(purge_en != nullptr);
        CHECK(std::any_cast<bool>(purge_en->current_value) == false);

        auto* purge_len = find_action(actions, "purge_length");
        REQUIRE(purge_len != nullptr);
        CHECK(std::any_cast<float>(purge_len->current_value) == Catch::Approx(100.0f));

        auto* brush = find_action(actions, "brush_enabled");
        REQUIRE(brush != nullptr);
        CHECK(std::any_cast<bool>(brush->current_value) == true);
    }
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST_CASE("Config actions fail gracefully when config not loaded",
          "[ams][afc][device_actions][config]") {
    AmsBackendAfc backend(nullptr, nullptr);
    // Configs not loaded

    SECTION("hub toggle fails") {
        auto result = backend.execute_device_action("hub_cut_enabled", std::any(true));
        CHECK_FALSE(result.success());
    }

    SECTION("macro var slider fails") {
        auto result = backend.execute_device_action("ramming_volume", std::any(30.0f));
        CHECK_FALSE(result.success());
    }

    SECTION("macro var toggle fails") {
        auto result = backend.execute_device_action("purge_enabled", std::any(true));
        CHECK_FALSE(result.success());
    }
}

TEST_CASE("Hub slider action modifies config", "[ams][afc][device_actions][config]") {
    AmsBackendAfc backend(nullptr, nullptr);
    AmsBackendAfcConfigHelper::load_test_configs(backend);

    SECTION("change hub_cut_dist") {
        auto result = backend.execute_device_action("hub_cut_dist", std::any(65.0f));
        CHECK(result.success());

        auto* cfg = AmsBackendAfcConfigHelper::get_afc_config(backend);
        REQUIRE(cfg != nullptr);
        CHECK(cfg->parser().get_float("AFC_hub Turtle_1", "cut_dist", 0.0f) ==
              Catch::Approx(65.0f));
        CHECK(cfg->has_unsaved_changes());
    }

    SECTION("change hub_bowden_length") {
        auto result = backend.execute_device_action("hub_bowden_length", std::any(800.0f));
        CHECK(result.success());

        auto* cfg = AmsBackendAfcConfigHelper::get_afc_config(backend);
        REQUIRE(cfg != nullptr);
        CHECK(cfg->parser().get_float("AFC_hub Turtle_1", "afc_bowden_length", 0.0f) ==
              Catch::Approx(800.0f));
        CHECK(cfg->has_unsaved_changes());
    }
}
