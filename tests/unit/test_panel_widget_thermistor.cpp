// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "panel_widget_config.h"
#include "panel_widget_registry.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using json = nlohmann::json;

// ============================================================================
// Test fixture — reuse Config internals access pattern
// ============================================================================

// Fixture with friend access to Config::data
namespace helix {
class ThermistorConfigFixture {
  protected:
    Config config;

    void setup_empty_config() {
        config.data = json::object();
    }

    void setup_with_widgets(const json& widgets_json) {
        config.data = json::object();
        config.data["panel_widgets"] = json::object();
        config.data["panel_widgets"]["home"] = widgets_json;
    }

    json& get_data() {
        return config.data;
    }
};
} // namespace helix

// ============================================================================
// Registry: thermistor widget definition
// ============================================================================

TEST_CASE("ThermistorWidget: registered in widget registry", "[thermistor][panel_widget]") {
    const auto* def = find_widget_def("thermistor");
    REQUIRE(def != nullptr);
    REQUIRE(std::string(def->display_name) == "Thermistor");
    REQUIRE(std::string(def->icon) == "thermometer");
    REQUIRE(def->hardware_gate_subject != nullptr);
    REQUIRE(std::string(def->hardware_gate_subject) == "temp_sensor_count");
    REQUIRE(def->default_enabled == false); // opt-in widget
}

// ============================================================================
// Config field serialization
// ============================================================================

TEST_CASE_METHOD(helix::ThermistorConfigFixture,
                 "ThermistorWidget: config field round-trips through save/load",
                 "[thermistor][panel_widget]") {
    json widgets = json::array({
        {{"id", "thermistor"},
         {"enabled", true},
         {"config", {{"sensor", "temperature_sensor mcu_temp"}}}},
        {{"id", "power"}, {"enabled", true}},
    });
    setup_with_widgets(widgets);

    PanelWidgetConfig wc("home", config);
    wc.load();

    // Verify config was loaded
    auto cfg = wc.get_widget_config("thermistor");
    REQUIRE(cfg.contains("sensor"));
    REQUIRE(cfg["sensor"].get<std::string>() == "temperature_sensor mcu_temp");

    // Save and reload
    wc.save();

    PanelWidgetConfig wc2("home", config);
    wc2.load();

    auto cfg2 = wc2.get_widget_config("thermistor");
    REQUIRE(cfg2.contains("sensor"));
    REQUIRE(cfg2["sensor"].get<std::string>() == "temperature_sensor mcu_temp");
}

TEST_CASE_METHOD(
    helix::ThermistorConfigFixture,
    "ThermistorWidget: get_widget_config returns empty object for widget without config",
    "[thermistor][panel_widget]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    auto cfg = wc.get_widget_config("power");
    REQUIRE(cfg.is_object());
    REQUIRE(cfg.empty());
}

TEST_CASE_METHOD(helix::ThermistorConfigFixture,
                 "ThermistorWidget: get_widget_config returns empty object for unknown widget",
                 "[thermistor][panel_widget]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    auto cfg = wc.get_widget_config("nonexistent_widget_xyz");
    REQUIRE(cfg.is_object());
    REQUIRE(cfg.empty());
}

TEST_CASE_METHOD(helix::ThermistorConfigFixture,
                 "ThermistorWidget: set_widget_config saves and persists",
                 "[thermistor][panel_widget]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    json sensor_config = {{"sensor", "temperature_sensor chamber"}};
    wc.set_widget_config("thermistor", sensor_config);

    // Verify immediate read
    auto cfg = wc.get_widget_config("thermistor");
    REQUIRE(cfg["sensor"].get<std::string>() == "temperature_sensor chamber");

    // Verify persisted in underlying JSON
    auto& saved = get_data()["panel_widgets"]["home"];
    bool found = false;
    for (const auto& item : saved) {
        if (item["id"] == "thermistor" && item.contains("config")) {
            found = true;
            REQUIRE(item["config"]["sensor"] == "temperature_sensor chamber");
        }
    }
    REQUIRE(found);
}

TEST_CASE_METHOD(helix::ThermistorConfigFixture,
                 "ThermistorWidget: config field omitted from JSON when empty",
                 "[thermistor][panel_widget]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();
    wc.save();

    // No widget should have a "config" key since none was set
    auto& saved = get_data()["panel_widgets"]["home"];
    for (const auto& item : saved) {
        CAPTURE(item["id"].get<std::string>());
        REQUIRE_FALSE(item.contains("config"));
    }
}

TEST_CASE_METHOD(helix::ThermistorConfigFixture,
                 "ThermistorWidget: config preserves unknown fields (forward compatibility)",
                 "[thermistor][panel_widget]") {
    json widgets = json::array({
        {{"id", "thermistor"},
         {"enabled", true},
         {"config",
          {{"sensor", "temperature_sensor mcu_temp"}, {"color", "#FF0000"}, {"threshold", 80}}}},
    });
    setup_with_widgets(widgets);

    PanelWidgetConfig wc("home", config);
    wc.load();

    auto cfg = wc.get_widget_config("thermistor");
    REQUIRE(cfg["sensor"] == "temperature_sensor mcu_temp");
    REQUIRE(cfg["color"] == "#FF0000");
    REQUIRE(cfg["threshold"] == 80);

    // Round-trip preserves unknown fields
    wc.save();
    PanelWidgetConfig wc2("home", config);
    wc2.load();
    auto cfg2 = wc2.get_widget_config("thermistor");
    REQUIRE(cfg2["color"] == "#FF0000");
    REQUIRE(cfg2["threshold"] == 80);
}

TEST_CASE_METHOD(helix::ThermistorConfigFixture,
                 "ThermistorWidget: set_widget_config on unknown widget is no-op",
                 "[thermistor][panel_widget]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    auto entries_before = wc.entries();

    // This should be a no-op (widget not in entries since it's unknown to registry)
    wc.set_widget_config("nonexistent_widget_xyz", {{"key", "value"}});

    // Entries unchanged
    REQUIRE(wc.entries().size() == entries_before.size());
}

TEST_CASE_METHOD(helix::ThermistorConfigFixture,
                 "ThermistorWidget: config field with non-object value in JSON is ignored",
                 "[thermistor][panel_widget]") {
    json widgets = json::array({
        {{"id", "thermistor"}, {"enabled", true}, {"config", "not_an_object"}},
    });
    setup_with_widgets(widgets);

    PanelWidgetConfig wc("home", config);
    wc.load();

    // Non-object config should be ignored (returns empty)
    auto cfg = wc.get_widget_config("thermistor");
    REQUIRE(cfg.is_object());
    REQUIRE(cfg.empty());
}
