// SPDX-License-Identifier: GPL-3.0-or-later

#include "led/led_controller.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("MacroBackend: execute_on with null API calls error callback", "[led][macro]") {
    helix::led::MacroBackend backend;

    helix::led::LedMacroInfo macro;
    macro.display_name = "Cabinet Light";
    macro.on_macro = "LIGHTS_ON";
    macro.off_macro = "LIGHTS_OFF";
    backend.add_macro(macro);

    bool error_called = false;
    backend.execute_on("Cabinet Light", nullptr,
                       [&](const std::string& err) { error_called = true; });

    REQUIRE(error_called);
}

TEST_CASE("MacroBackend: execute_off with null API calls error callback", "[led][macro]") {
    helix::led::MacroBackend backend;

    helix::led::LedMacroInfo macro;
    macro.display_name = "Cabinet Light";
    macro.on_macro = "LIGHTS_ON";
    macro.off_macro = "LIGHTS_OFF";
    backend.add_macro(macro);

    bool error_called = false;
    backend.execute_off("Cabinet Light", nullptr,
                        [&](const std::string& err) { error_called = true; });

    REQUIRE(error_called);
}

TEST_CASE("MacroBackend: execute_toggle with null API calls error callback", "[led][macro]") {
    helix::led::MacroBackend backend;

    helix::led::LedMacroInfo macro;
    macro.display_name = "Light Toggle";
    macro.toggle_macro = "TOGGLE_LIGHT";
    backend.add_macro(macro);

    bool error_called = false;
    backend.execute_toggle("Light Toggle", nullptr,
                           [&](const std::string& err) { error_called = true; });

    REQUIRE(error_called);
}

TEST_CASE("MacroBackend: execute_custom_action with null API calls error callback",
          "[led][macro]") {
    helix::led::MacroBackend backend;

    bool error_called = false;
    backend.execute_custom_action("LED_PARTY", nullptr,
                                  [&](const std::string& err) { error_called = true; });

    REQUIRE(error_called);
}

TEST_CASE("MacroBackend: execute_on with empty on_macro calls error", "[led][macro]") {
    helix::led::MacroBackend backend;
    backend.set_api(nullptr); // Still null but with macro registered

    helix::led::LedMacroInfo macro;
    macro.display_name = "Custom";
    // Both on_macro and toggle_macro empty
    backend.add_macro(macro);

    bool error_called = false;
    backend.execute_on("Custom", nullptr, [&](const std::string& err) { error_called = true; });

    REQUIRE(error_called);
}

TEST_CASE("MacroBackend: execute_on for unknown macro calls error", "[led][macro]") {
    helix::led::MacroBackend backend;

    bool error_called = false;
    backend.execute_on("NonExistent", nullptr,
                       [&](const std::string& err) { error_called = true; });

    REQUIRE(error_called);
}

TEST_CASE("MacroBackend: null callbacks don't crash", "[led][macro]") {
    helix::led::MacroBackend backend;

    backend.execute_on("NonExistent", nullptr, nullptr);
    backend.execute_off("NonExistent", nullptr, nullptr);
    backend.execute_toggle("NonExistent", nullptr, nullptr);
    backend.execute_custom_action("LED_PARTY", nullptr, nullptr);
}

TEST_CASE("MacroBackend: type is MACRO", "[led][macro]") {
    helix::led::MacroBackend backend;
    REQUIRE(backend.type() == helix::led::LedBackendType::MACRO);
}

TEST_CASE("MacroBackend: macro with presets", "[led][macro]") {
    helix::led::MacroBackend backend;

    helix::led::LedMacroInfo macro;
    macro.display_name = "LED Modes";
    macro.type = helix::led::MacroLedType::PRESET;
    macro.presets = {"LED_PARTY", "LED_NIGHTLIGHT"};
    backend.add_macro(macro);

    REQUIRE(backend.macros().size() == 1);
    REQUIRE(backend.macros()[0].type == helix::led::MacroLedType::PRESET);
    REQUIRE(backend.macros()[0].presets.size() == 2);
    REQUIRE(backend.macros()[0].presets[0] == "LED_PARTY");
    REQUIRE(backend.macros()[0].presets[1] == "LED_NIGHTLIGHT");
}

TEST_CASE("MacroLedType: ON_OFF type has on/off macros", "[led][macro]") {
    helix::led::LedMacroInfo info;
    info.display_name = "Case Light";
    info.type = helix::led::MacroLedType::ON_OFF;
    info.on_macro = "CASELIGHT_ON";
    info.off_macro = "CASELIGHT_OFF";

    REQUIRE(info.type == helix::led::MacroLedType::ON_OFF);
    REQUIRE(!info.on_macro.empty());
    REQUIRE(!info.off_macro.empty());
    REQUIRE(info.toggle_macro.empty());
    REQUIRE(info.presets.empty());
}

TEST_CASE("pretty_print_macro: formats macro names for display", "[led][macro]") {
    using helix::led::pretty_print_macro;

    // Strip LED_ prefix and title-case
    REQUIRE(pretty_print_macro("LED_PARTY_MODE") == "Party Mode");

    // Strip LIGHT_ prefix
    REQUIRE(pretty_print_macro("LIGHT_DIM") == "Dim");

    // Strip STATUS_LED_ prefix
    REQUIRE(pretty_print_macro("STATUS_LED_READY") == "Ready");

    // No prefix to strip
    REQUIRE(pretty_print_macro("CASELIGHT_ON") == "Caselight On");

    // Already short name
    REQUIRE(pretty_print_macro("LED_ON") == "On");

    // Single word after prefix
    REQUIRE(pretty_print_macro("LED_BREATHE") == "Breathe");

    // Empty string
    REQUIRE(pretty_print_macro("") == "");
}

TEST_CASE("MacroLedType: TOGGLE type has toggle macro", "[led][macro]") {
    helix::led::LedMacroInfo info;
    info.display_name = "Chamber LEDs";
    info.type = helix::led::MacroLedType::TOGGLE;
    info.toggle_macro = "CHAMBER_LIGHTS";

    REQUIRE(info.type == helix::led::MacroLedType::TOGGLE);
    REQUIRE(info.toggle_macro == "CHAMBER_LIGHTS");
    REQUIRE(info.on_macro.empty());
    REQUIRE(info.off_macro.empty());
}
