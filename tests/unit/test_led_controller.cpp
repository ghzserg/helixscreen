// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "led/led_controller.h"
#include "printer_discovery.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

TEST_CASE("LedController singleton access", "[led]") {
    auto& ctrl = helix::led::LedController::instance();
    auto& ctrl2 = helix::led::LedController::instance();
    REQUIRE(&ctrl == &ctrl2);
}

TEST_CASE("LedController init and deinit", "[led]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit(); // Clean state

    REQUIRE(!ctrl.is_initialized());
    ctrl.init(nullptr, nullptr); // null api/client for testing
    REQUIRE(ctrl.is_initialized());
    ctrl.deinit();
    REQUIRE(!ctrl.is_initialized());
}

TEST_CASE("LedController has_any_backend empty", "[led]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    REQUIRE(!ctrl.has_any_backend());
    REQUIRE(ctrl.available_backends().empty());

    ctrl.deinit();
}

TEST_CASE("LedController discover_from_hardware populates native backend", "[led]") {
    // Use PrinterDiscovery to populate
    helix::PrinterDiscovery discovery;
    nlohmann::json objects = nlohmann::json::array(
        {"neopixel chamber_light", "dotstar status_led", "led case_light", "extruder"});
    discovery.parse_objects(objects);

    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);
    ctrl.discover_from_hardware(discovery);

    REQUIRE(ctrl.has_any_backend());
    REQUIRE(ctrl.native().is_available());
    REQUIRE(ctrl.native().strips().size() == 3);

    // Check strip details
    auto& strips = ctrl.native().strips();
    REQUIRE(strips[0].id == "neopixel chamber_light");
    REQUIRE(strips[0].name == "Chamber Light");
    REQUIRE(strips[0].supports_color == true);
    REQUIRE(strips[0].supports_white == true);

    REQUIRE(strips[1].id == "dotstar status_led");
    REQUIRE(strips[1].name == "Status LED");
    REQUIRE(strips[1].supports_white == true);

    REQUIRE(strips[2].id == "led case_light");
    REQUIRE(strips[2].name == "Case Light");
    REQUIRE(strips[2].supports_white == false);

    // Other backends should be empty
    REQUIRE(!ctrl.effects().is_available());
    REQUIRE(!ctrl.wled().is_available());
    REQUIRE(!ctrl.macro().is_available());

    auto backends = ctrl.available_backends();
    REQUIRE(backends.size() == 1);
    REQUIRE(backends[0] == helix::led::LedBackendType::NATIVE);

    ctrl.deinit();
}

TEST_CASE("LedBackendType enum values", "[led]") {
    REQUIRE(static_cast<int>(helix::led::LedBackendType::NATIVE) == 0);
    REQUIRE(static_cast<int>(helix::led::LedBackendType::LED_EFFECT) == 1);
    REQUIRE(static_cast<int>(helix::led::LedBackendType::WLED) == 2);
    REQUIRE(static_cast<int>(helix::led::LedBackendType::MACRO) == 3);
}

TEST_CASE("LedStripInfo struct", "[led]") {
    helix::led::LedStripInfo info;
    info.name = "Chamber Light";
    info.id = "neopixel chamber_light";
    info.backend = helix::led::LedBackendType::NATIVE;
    info.supports_color = true;
    info.supports_white = true;

    REQUIRE(info.name == "Chamber Light");
    REQUIRE(info.id == "neopixel chamber_light");
    REQUIRE(info.backend == helix::led::LedBackendType::NATIVE);
    REQUIRE(info.supports_color);
    REQUIRE(info.supports_white);
}

TEST_CASE("LedEffectBackend icon hint mapping", "[led]") {
    using helix::led::LedEffectBackend;

    REQUIRE(LedEffectBackend::icon_hint_for_effect("breathing") == "air");
    REQUIRE(LedEffectBackend::icon_hint_for_effect("pulse_slow") == "air");
    REQUIRE(LedEffectBackend::icon_hint_for_effect("fire_effect") == "local_fire_department");
    REQUIRE(LedEffectBackend::icon_hint_for_effect("flame") == "local_fire_department");
    REQUIRE(LedEffectBackend::icon_hint_for_effect("rainbow_chase") == "palette");
    REQUIRE(LedEffectBackend::icon_hint_for_effect("comet_tail") == "fast_forward");
    REQUIRE(LedEffectBackend::icon_hint_for_effect("chase_effect") == "fast_forward");
    REQUIRE(LedEffectBackend::icon_hint_for_effect("static_white") == "lightbulb");
    REQUIRE(LedEffectBackend::icon_hint_for_effect("my_custom_effect") == "auto_awesome");
}

TEST_CASE("LedEffectBackend display name conversion", "[led]") {
    using helix::led::LedEffectBackend;

    REQUIRE(LedEffectBackend::display_name_for_effect("led_effect breathing") == "Breathing");
    REQUIRE(LedEffectBackend::display_name_for_effect("led_effect fire_effect") == "Fire Effect");
    REQUIRE(LedEffectBackend::display_name_for_effect("rainbow_chase") == "Rainbow Chase");
    REQUIRE(LedEffectBackend::display_name_for_effect("") == "");
}

TEST_CASE("NativeBackend strip management", "[led]") {
    helix::led::NativeBackend backend;

    REQUIRE(!backend.is_available());
    REQUIRE(backend.strips().empty());

    helix::led::LedStripInfo strip;
    strip.name = "Test Strip";
    strip.id = "neopixel test";
    strip.backend = helix::led::LedBackendType::NATIVE;
    strip.supports_color = true;
    strip.supports_white = false;

    backend.add_strip(strip);
    REQUIRE(backend.is_available());
    REQUIRE(backend.strips().size() == 1);

    backend.clear();
    REQUIRE(!backend.is_available());
}

TEST_CASE("MacroBackend macro management", "[led]") {
    helix::led::MacroBackend backend;

    REQUIRE(!backend.is_available());

    helix::led::LedMacroInfo macro;
    macro.display_name = "Cabinet Light";
    macro.on_macro = "LIGHTS_ON";
    macro.off_macro = "LIGHTS_OFF";
    macro.presets = {{"Party Mode", "LED_PARTY"}};

    backend.add_macro(macro);
    REQUIRE(backend.is_available());
    REQUIRE(backend.macros().size() == 1);
    REQUIRE(backend.macros()[0].display_name == "Cabinet Light");
    REQUIRE(backend.macros()[0].presets.size() == 1);

    backend.clear();
    REQUIRE(!backend.is_available());
}

TEST_CASE("LedController deinit clears all backends", "[led]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    // Add some data
    helix::led::LedStripInfo strip;
    strip.name = "Test";
    strip.id = "neopixel test";
    strip.backend = helix::led::LedBackendType::NATIVE;
    strip.supports_color = true;
    strip.supports_white = false;
    ctrl.native().add_strip(strip);

    helix::led::LedEffectInfo effect;
    effect.name = "led_effect test";
    effect.display_name = "Test";
    effect.icon_hint = "auto_awesome";
    ctrl.effects().add_effect(effect);

    REQUIRE(ctrl.has_any_backend());

    ctrl.deinit();

    REQUIRE(!ctrl.has_any_backend());
    REQUIRE(ctrl.native().strips().empty());
    REQUIRE(ctrl.effects().effects().empty());
}

TEST_CASE("LedController: selected_strips can hold WLED strip IDs", "[led][controller]") {
    auto& controller = helix::led::LedController::instance();
    controller.deinit();

    // Set selected strips to a WLED-style ID
    controller.set_selected_strips({"wled_printer_led"});
    REQUIRE(controller.selected_strips().size() == 1);
    REQUIRE(controller.selected_strips()[0] == "wled_printer_led");

    // Can switch back to native
    controller.set_selected_strips({"neopixel chamber_light"});
    REQUIRE(controller.selected_strips()[0] == "neopixel chamber_light");
}

TEST_CASE("LedController: toggle_all turns on all selected native strips", "[led][controller]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    // Add native strips
    helix::led::LedStripInfo strip1;
    strip1.name = "Chamber Light";
    strip1.id = "neopixel chamber_light";
    strip1.backend = helix::led::LedBackendType::NATIVE;
    strip1.supports_color = true;
    strip1.supports_white = true;
    ctrl.native().add_strip(strip1);

    // Select the strip
    ctrl.set_selected_strips({"neopixel chamber_light"});

    // toggle_all should exist and not crash with nullptr api
    // (actual gcode won't be sent without real api, but the method should work)
    ctrl.toggle_all(true);
    ctrl.toggle_all(false);

    ctrl.deinit();
}

TEST_CASE("LedController: toggle_all with empty selected_strips is a no-op", "[led][controller]") {
    // Clear any auto-selected strips persisted by prior tests
    auto* cfg = Config::get_instance();
    if (cfg) {
        cfg->set("/printer/leds/selected_strips", nlohmann::json::array());
        cfg->save();
    }

    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    // No strips selected
    REQUIRE(ctrl.selected_strips().empty());

    // Should not crash
    ctrl.toggle_all(true);
    ctrl.toggle_all(false);

    ctrl.deinit();
}

TEST_CASE("LedController: toggle_all with mixed backend types", "[led][controller]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    // Add native strip
    helix::led::LedStripInfo native_strip;
    native_strip.name = "Chamber Light";
    native_strip.id = "neopixel chamber_light";
    native_strip.backend = helix::led::LedBackendType::NATIVE;
    native_strip.supports_color = true;
    native_strip.supports_white = true;
    ctrl.native().add_strip(native_strip);

    // Add WLED strip
    helix::led::LedStripInfo wled_strip;
    wled_strip.name = "Printer LED";
    wled_strip.id = "wled_printer_led";
    wled_strip.backend = helix::led::LedBackendType::WLED;
    wled_strip.supports_color = true;
    wled_strip.supports_white = false;
    ctrl.wled().add_strip(wled_strip);

    // Select both
    ctrl.set_selected_strips({"neopixel chamber_light", "wled_printer_led"});

    // Should dispatch to correct backends without crash
    ctrl.toggle_all(true);
    ctrl.toggle_all(false);

    ctrl.deinit();
}

TEST_CASE("LedController: backend_for_strip returns correct type", "[led][controller]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    // Add native strip
    helix::led::LedStripInfo native_strip;
    native_strip.name = "Chamber Light";
    native_strip.id = "neopixel chamber_light";
    native_strip.backend = helix::led::LedBackendType::NATIVE;
    native_strip.supports_color = true;
    native_strip.supports_white = true;
    ctrl.native().add_strip(native_strip);

    // Add WLED strip
    helix::led::LedStripInfo wled_strip;
    wled_strip.name = "Printer LED";
    wled_strip.id = "wled_printer_led";
    wled_strip.backend = helix::led::LedBackendType::WLED;
    wled_strip.supports_color = true;
    wled_strip.supports_white = false;
    ctrl.wled().add_strip(wled_strip);

    // Check backend_for_strip
    REQUIRE(ctrl.backend_for_strip("neopixel chamber_light") == helix::led::LedBackendType::NATIVE);
    REQUIRE(ctrl.backend_for_strip("wled_printer_led") == helix::led::LedBackendType::WLED);

    // Unknown strip should return NATIVE as default
    REQUIRE(ctrl.backend_for_strip("unknown_strip") == helix::led::LedBackendType::NATIVE);

    ctrl.deinit();
}

TEST_CASE("LedController: backend_for_strip identifies macro backend", "[led][controller]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    // Add a macro device
    helix::led::LedMacroInfo macro;
    macro.display_name = "Cabinet Light";
    macro.type = helix::led::MacroLedType::ON_OFF;
    macro.on_macro = "LIGHTS_ON";
    macro.off_macro = "LIGHTS_OFF";
    ctrl.macro().add_macro(macro);
    ctrl.set_configured_macros({macro});

    // Macro devices are identified by display name
    REQUIRE(ctrl.backend_for_strip("Cabinet Light") == helix::led::LedBackendType::MACRO);

    ctrl.deinit();
}

TEST_CASE("LedController: get/set_led_on_at_start", "[led][controller]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    // Default should be false
    REQUIRE(ctrl.get_led_on_at_start() == false);

    ctrl.set_led_on_at_start(true);
    REQUIRE(ctrl.get_led_on_at_start() == true);

    ctrl.set_led_on_at_start(false);
    REQUIRE(ctrl.get_led_on_at_start() == false);

    ctrl.deinit();
}

TEST_CASE("LedController: apply_startup_preference does nothing when disabled",
          "[led][controller]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    ctrl.set_led_on_at_start(false);

    // Should not crash - just a no-op
    ctrl.apply_startup_preference();

    ctrl.deinit();
}

TEST_CASE("LedController: apply_startup_preference with no strips is a no-op",
          "[led][controller]") {
    // Clear any auto-selected strips persisted by prior tests
    auto* cfg = Config::get_instance();
    if (cfg) {
        cfg->set("/printer/leds/selected_strips", nlohmann::json::array());
        cfg->save();
    }

    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    ctrl.set_led_on_at_start(true);
    REQUIRE(ctrl.selected_strips().empty());

    // Should not crash even though enabled
    ctrl.apply_startup_preference();

    ctrl.deinit();
}

// ============================================================================
// Phase 1: macro: prefix handling
// ============================================================================

TEST_CASE("LedController: backend_for_strip with macro: prefix", "[led][controller]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    helix::led::LedMacroInfo macro;
    macro.display_name = "Cabinet Light";
    macro.type = helix::led::MacroLedType::ON_OFF;
    macro.on_macro = "LIGHTS_ON";
    macro.off_macro = "LIGHTS_OFF";
    ctrl.macro().add_macro(macro);
    ctrl.set_configured_macros({macro});

    // Both prefixed and unprefixed should resolve to MACRO
    REQUIRE(ctrl.backend_for_strip("macro:Cabinet Light") == helix::led::LedBackendType::MACRO);
    REQUIRE(ctrl.backend_for_strip("Cabinet Light") == helix::led::LedBackendType::MACRO);

    ctrl.deinit();
}

TEST_CASE("LedController: toggle_all dispatches macro: prefixed strips", "[led][controller]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    helix::led::LedMacroInfo macro;
    macro.display_name = "Cabinet Light";
    macro.type = helix::led::MacroLedType::ON_OFF;
    macro.on_macro = "LIGHTS_ON";
    macro.off_macro = "LIGHTS_OFF";
    ctrl.macro().add_macro(macro);
    ctrl.set_configured_macros({macro});

    // Use prefixed strip ID (as the control overlay would)
    ctrl.set_selected_strips({"macro:Cabinet Light"});

    // Should not crash (will warn about no API, which is expected)
    ctrl.toggle_all(true);
    ctrl.toggle_all(false);

    ctrl.deinit();
}

// ============================================================================
// Phase 2: all_selectable_strips
// ============================================================================

TEST_CASE("LedController: all_selectable_strips includes native + WLED + macros",
          "[led][controller]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    // Add a native strip
    helix::led::LedStripInfo native_strip;
    native_strip.name = "Chamber Light";
    native_strip.id = "neopixel chamber_light";
    native_strip.backend = helix::led::LedBackendType::NATIVE;
    native_strip.supports_color = true;
    native_strip.supports_white = true;
    ctrl.native().add_strip(native_strip);

    // Add a WLED strip
    helix::led::LedStripInfo wled_strip;
    wled_strip.name = "Printer LED";
    wled_strip.id = "wled_printer_led";
    wled_strip.backend = helix::led::LedBackendType::WLED;
    wled_strip.supports_color = true;
    wled_strip.supports_white = false;
    ctrl.wled().add_strip(wled_strip);

    // Add ON_OFF macro (should appear)
    helix::led::LedMacroInfo on_off_macro;
    on_off_macro.display_name = "Cabinet Light";
    on_off_macro.type = helix::led::MacroLedType::ON_OFF;
    on_off_macro.on_macro = "LIGHTS_ON";
    on_off_macro.off_macro = "LIGHTS_OFF";

    // Add TOGGLE macro (should appear)
    helix::led::LedMacroInfo toggle_macro;
    toggle_macro.display_name = "Desk Lamp";
    toggle_macro.type = helix::led::MacroLedType::TOGGLE;
    toggle_macro.toggle_macro = "TOGGLE_DESK";

    // Add PRESET macro (should NOT appear)
    helix::led::LedMacroInfo preset_macro;
    preset_macro.display_name = "Party Mode";
    preset_macro.type = helix::led::MacroLedType::PRESET;

    ctrl.set_configured_macros({on_off_macro, toggle_macro, preset_macro});

    auto strips = ctrl.all_selectable_strips();

    // Should have native + WLED + 2 macros (not PRESET) = 4
    REQUIRE(strips.size() == 4);
    REQUIRE(strips[0].id == "neopixel chamber_light");
    REQUIRE(strips[1].id == "wled_printer_led");
    REQUIRE(strips[2].id == "macro:Cabinet Light");
    REQUIRE(strips[2].backend == helix::led::LedBackendType::MACRO);
    REQUIRE(strips[3].id == "macro:Desk Lamp");

    ctrl.deinit();
}

TEST_CASE("LedController: all_selectable_strips empty when no backends", "[led][controller]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    auto strips = ctrl.all_selectable_strips();
    REQUIRE(strips.empty());

    ctrl.deinit();
}

// ============================================================================
// Phase 3: first_available_strip
// ============================================================================

TEST_CASE("LedController: first_available_strip priority order", "[led][controller]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    // With nothing: empty
    REQUIRE(ctrl.first_available_strip().empty());

    // Add macro only
    helix::led::LedMacroInfo macro;
    macro.display_name = "Cabinet Light";
    macro.type = helix::led::MacroLedType::ON_OFF;
    macro.on_macro = "LIGHTS_ON";
    macro.off_macro = "LIGHTS_OFF";
    ctrl.set_configured_macros({macro});

    REQUIRE(ctrl.first_available_strip() == "macro:Cabinet Light");

    // Add WLED -- should now prefer WLED over macro
    helix::led::LedStripInfo wled_strip;
    wled_strip.name = "WLED Strip";
    wled_strip.id = "wled_test";
    wled_strip.backend = helix::led::LedBackendType::WLED;
    wled_strip.supports_color = true;
    wled_strip.supports_white = false;
    ctrl.wled().add_strip(wled_strip);

    REQUIRE(ctrl.first_available_strip() == "wled_test");

    // Add native -- should now prefer native
    helix::led::LedStripInfo native_strip;
    native_strip.name = "Chamber Light";
    native_strip.id = "neopixel chamber_light";
    native_strip.backend = helix::led::LedBackendType::NATIVE;
    native_strip.supports_color = true;
    native_strip.supports_white = true;
    ctrl.native().add_strip(native_strip);

    REQUIRE(ctrl.first_available_strip() == "neopixel chamber_light");

    // Set selected -- should prefer that
    ctrl.set_selected_strips({"wled_test"});
    REQUIRE(ctrl.first_available_strip() == "wled_test");

    ctrl.deinit();
}

TEST_CASE("LedController: first_available_strip skips PRESET macros", "[led][controller]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    helix::led::LedMacroInfo preset_macro;
    preset_macro.display_name = "Party Mode";
    preset_macro.type = helix::led::MacroLedType::PRESET;

    helix::led::LedMacroInfo toggle_macro;
    toggle_macro.display_name = "Desk Lamp";
    toggle_macro.type = helix::led::MacroLedType::TOGGLE;
    toggle_macro.toggle_macro = "TOGGLE_DESK";

    ctrl.set_configured_macros({preset_macro, toggle_macro});

    // Should skip PRESET and return TOGGLE
    REQUIRE(ctrl.first_available_strip() == "macro:Desk Lamp");

    ctrl.deinit();
}

// ============================================================================
// Phase 4: MacroBackend state tracking + abstract API
// ============================================================================

TEST_CASE("MacroBackend: optimistic state tracking", "[led][macro]") {
    helix::led::MacroBackend backend;

    helix::led::LedMacroInfo on_off;
    on_off.display_name = "Cabinet Light";
    on_off.type = helix::led::MacroLedType::ON_OFF;
    on_off.on_macro = "LIGHTS_ON";
    on_off.off_macro = "LIGHTS_OFF";
    backend.add_macro(on_off);

    // Initially off
    REQUIRE(!backend.is_on("Cabinet Light"));

    // ON_OFF has known state
    REQUIRE(backend.has_known_state("Cabinet Light"));

    // After execute_on (will warn about no API, but state should track)
    backend.execute_on("Cabinet Light");
    REQUIRE(!backend.is_on("Cabinet Light")); // No API -> state NOT tracked (early return)

    // Clear resets state
    backend.clear();
    REQUIRE(!backend.is_on("Cabinet Light"));
}

TEST_CASE("MacroBackend: TOGGLE has unknown state", "[led][macro]") {
    helix::led::MacroBackend backend;

    helix::led::LedMacroInfo toggle;
    toggle.display_name = "Desk Lamp";
    toggle.type = helix::led::MacroLedType::TOGGLE;
    toggle.toggle_macro = "TOGGLE_DESK";
    backend.add_macro(toggle);

    // TOGGLE macros don't have known state
    REQUIRE(!backend.has_known_state("Desk Lamp"));
}

TEST_CASE("LedController: light_state_trackable with various selections", "[led][controller]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    // Native only -- trackable
    helix::led::LedStripInfo native_strip;
    native_strip.name = "Chamber Light";
    native_strip.id = "neopixel chamber_light";
    native_strip.backend = helix::led::LedBackendType::NATIVE;
    native_strip.supports_color = true;
    native_strip.supports_white = true;
    ctrl.native().add_strip(native_strip);
    ctrl.set_selected_strips({"neopixel chamber_light"});
    REQUIRE(ctrl.light_state_trackable());

    // Add ON_OFF macro -- still trackable
    helix::led::LedMacroInfo on_off;
    on_off.display_name = "Cabinet Light";
    on_off.type = helix::led::MacroLedType::ON_OFF;
    on_off.on_macro = "LIGHTS_ON";
    on_off.off_macro = "LIGHTS_OFF";
    ctrl.macro().add_macro(on_off);
    ctrl.set_configured_macros({on_off});
    ctrl.set_selected_strips({"neopixel chamber_light", "macro:Cabinet Light"});
    REQUIRE(ctrl.light_state_trackable());

    // Add TOGGLE macro -- NOT trackable
    helix::led::LedMacroInfo toggle;
    toggle.display_name = "Desk Lamp";
    toggle.type = helix::led::MacroLedType::TOGGLE;
    toggle.toggle_macro = "TOGGLE_DESK";
    ctrl.macro().add_macro(toggle);
    ctrl.set_configured_macros({on_off, toggle});
    ctrl.set_selected_strips({"neopixel chamber_light", "macro:Desk Lamp"});
    REQUIRE(!ctrl.light_state_trackable());

    ctrl.deinit();
}

TEST_CASE("LedController: light_toggle and light_is_on", "[led][controller]") {
    auto& ctrl = helix::led::LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    // Add ON_OFF macro
    helix::led::LedMacroInfo macro;
    macro.display_name = "Cabinet Light";
    macro.type = helix::led::MacroLedType::ON_OFF;
    macro.on_macro = "LIGHTS_ON";
    macro.off_macro = "LIGHTS_OFF";
    ctrl.macro().add_macro(macro);
    ctrl.set_configured_macros({macro});
    ctrl.set_selected_strips({"macro:Cabinet Light"});

    // Initially off
    REQUIRE(!ctrl.light_is_on());

    // Toggle on (no API so macro state won't track, but light_toggle uses toggle_all)
    ctrl.light_toggle();

    // Toggle off
    ctrl.light_toggle();

    ctrl.deinit();
}
