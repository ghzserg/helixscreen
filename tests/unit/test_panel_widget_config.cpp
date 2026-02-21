// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "panel_widget_config.h"
#include "panel_widget_registry.h"

#include <set>

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// Test fixture — access Config internals via friend declaration
// ============================================================================

namespace helix {
class PanelWidgetConfigFixture {
  protected:
    Config config;

    void setup_empty_config() {
        config.data = json::object();
    }

    /// Set up per-panel config under panel_widgets.<panel_id>
    void setup_with_widgets(const json& widgets_json, const std::string& panel_id = "home") {
        config.data = json::object();
        config.data["panel_widgets"] = json::object();
        config.data["panel_widgets"][panel_id] = widgets_json;
    }

    /// Set up legacy flat home_widgets key (for migration testing)
    void setup_with_legacy_widgets(const json& widgets_json) {
        config.data = json::object();
        config.data["home_widgets"] = widgets_json;
    }

    json& get_data() {
        return config.data;
    }
};
} // namespace helix

// ============================================================================
// Registry tests
// ============================================================================

TEST_CASE("PanelWidgetRegistry: returns all widget definitions", "[panel_widget][widget_config]") {
    const auto& defs = get_all_widget_defs();
    REQUIRE(defs.size() == 13);
}

TEST_CASE("PanelWidgetRegistry: all widget IDs are unique", "[panel_widget][widget_config]") {
    const auto& defs = get_all_widget_defs();
    std::set<std::string> ids;
    for (const auto& def : defs) {
        REQUIRE(ids.insert(def.id).second); // insert returns false if duplicate
    }
}

TEST_CASE("PanelWidgetRegistry: can look up widget by ID", "[panel_widget][widget_config]") {
    const auto* def = find_widget_def("temperature");
    REQUIRE(def != nullptr);
    REQUIRE(std::string(def->display_name) == "Nozzle Temperature");
}

TEST_CASE("PanelWidgetRegistry: unknown ID returns nullptr", "[panel_widget][widget_config]") {
    const auto* def = find_widget_def("nonexistent_widget");
    REQUIRE(def == nullptr);
}

TEST_CASE("PanelWidgetRegistry: widget_def_count matches vector size",
          "[panel_widget][widget_config]") {
    REQUIRE(widget_def_count() == get_all_widget_defs().size());
}

// ============================================================================
// Config tests — default behavior
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: default config produces all widgets enabled in default order",
                 "[panel_widget][widget_config]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    const auto& entries = wc.entries();
    const auto& defs = get_all_widget_defs();
    REQUIRE(entries.size() == defs.size());

    for (size_t i = 0; i < entries.size(); ++i) {
        REQUIRE(entries[i].id == defs[i].id);
        REQUIRE(entries[i].enabled == defs[i].default_enabled);
    }
}

// ============================================================================
// Config tests — load from explicit JSON
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: load from explicit JSON preserves order and enabled state",
                 "[panel_widget][widget_config]") {
    json widgets = json::array({
        {{"id", "temperature"}, {"enabled", true}},
        {{"id", "led"}, {"enabled", false}},
        {{"id", "network"}, {"enabled", true}},
    });
    setup_with_widgets(widgets);

    PanelWidgetConfig wc("home", config);
    wc.load();

    const auto& entries = wc.entries();
    // 3 explicit + remaining from registry appended
    REQUIRE(entries.size() == widget_def_count());

    // First 3 should match our explicit order
    REQUIRE(entries[0].id == "temperature");
    REQUIRE(entries[0].enabled == true);
    REQUIRE(entries[1].id == "led");
    REQUIRE(entries[1].enabled == false);
    REQUIRE(entries[2].id == "network");
    REQUIRE(entries[2].enabled == true);

    // Remaining should be appended with their default_enabled value
    for (size_t i = 3; i < entries.size(); ++i) {
        const auto* def = find_widget_def(entries[i].id);
        REQUIRE(def);
        REQUIRE(entries[i].enabled == def->default_enabled);
    }
}

// ============================================================================
// Config tests — save produces expected JSON
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: save produces expected JSON structure",
                 "[panel_widget][widget_config]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    // Disable one widget for variety
    wc.set_enabled(2, false);
    wc.save();

    // Check the JSON was written to config under per-panel path
    auto& saved = get_data()["panel_widgets"]["home"];
    REQUIRE(saved.is_array());
    REQUIRE(saved.size() == widget_def_count());

    // Each entry should have id and enabled
    for (const auto& item : saved) {
        REQUIRE(item.contains("id"));
        REQUIRE(item.contains("enabled"));
        REQUIRE(item["id"].is_string());
        REQUIRE(item["enabled"].is_boolean());
    }

    // The third entry should be disabled
    REQUIRE(saved[2]["enabled"].get<bool>() == false);
}

// ============================================================================
// Config tests — round-trip
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: round-trip load-save-reload preserves state",
                 "[panel_widget][widget_config]") {
    setup_empty_config();

    // First load + customize
    PanelWidgetConfig wc1("home", config);
    wc1.load();
    wc1.set_enabled(1, false);
    wc1.reorder(0, 3);
    wc1.save();

    // Second load from same config
    PanelWidgetConfig wc2("home", config);
    wc2.load();

    const auto& e1 = wc1.entries();
    const auto& e2 = wc2.entries();
    REQUIRE(e1.size() == e2.size());

    for (size_t i = 0; i < e1.size(); ++i) {
        REQUIRE(e1[i].id == e2[i].id);
        REQUIRE(e1[i].enabled == e2[i].enabled);
    }
}

// ============================================================================
// Config tests — reorder
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: reorder moves item from index 2 to index 0",
                 "[panel_widget][widget_config]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    std::string moved_id = wc.entries()[2].id;
    std::string was_first = wc.entries()[0].id;
    wc.reorder(2, 0);

    REQUIRE(wc.entries()[0].id == moved_id);
    REQUIRE(wc.entries()[1].id == was_first);
}

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: reorder moves item from index 0 to index 3",
                 "[panel_widget][widget_config]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    std::string moved_id = wc.entries()[0].id;
    std::string was_at_1 = wc.entries()[1].id;
    wc.reorder(0, 3);

    // After removing from 0 and inserting at 3, old index 1 becomes 0
    REQUIRE(wc.entries()[0].id == was_at_1);
    REQUIRE(wc.entries()[3].id == moved_id);
}

TEST_CASE_METHOD(PanelWidgetConfigFixture, "PanelWidgetConfig: reorder same index is no-op",
                 "[panel_widget][widget_config]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    auto before = wc.entries();
    wc.reorder(2, 2);
    auto after = wc.entries();

    REQUIRE(before.size() == after.size());
    for (size_t i = 0; i < before.size(); ++i) {
        REQUIRE(before[i].id == after[i].id);
    }
}

TEST_CASE_METHOD(PanelWidgetConfigFixture, "PanelWidgetConfig: reorder out of bounds is no-op",
                 "[panel_widget][widget_config]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    auto before = wc.entries();
    wc.reorder(100, 0);
    auto after = wc.entries();

    REQUIRE(before.size() == after.size());
    for (size_t i = 0; i < before.size(); ++i) {
        REQUIRE(before[i].id == after[i].id);
    }
}

// ============================================================================
// Config tests — toggle enabled
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture, "PanelWidgetConfig: toggle disable a widget",
                 "[panel_widget][widget_config]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    REQUIRE(wc.entries()[0].enabled == true);
    wc.set_enabled(0, false);
    REQUIRE(wc.entries()[0].enabled == false);
    REQUIRE(wc.is_enabled(wc.entries()[0].id) == false);
}

TEST_CASE_METHOD(PanelWidgetConfigFixture, "PanelWidgetConfig: toggle re-enable a widget",
                 "[panel_widget][widget_config]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    wc.set_enabled(0, false);
    REQUIRE(wc.entries()[0].enabled == false);

    wc.set_enabled(0, true);
    REQUIRE(wc.entries()[0].enabled == true);
    REQUIRE(wc.is_enabled(wc.entries()[0].id) == true);
}

// ============================================================================
// Config tests — new widget appended
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: new registry widget gets appended with default_enabled",
                 "[panel_widget][widget_config]") {
    // Save config with only a subset of widgets
    json widgets = json::array({
        {{"id", "power"}, {"enabled", true}},
        {{"id", "network"}, {"enabled", false}},
    });
    setup_with_widgets(widgets);

    PanelWidgetConfig wc("home", config);
    wc.load();

    // Should have all registry widgets
    REQUIRE(wc.entries().size() == widget_def_count());

    // First two should match saved order/state
    REQUIRE(wc.entries()[0].id == "power");
    REQUIRE(wc.entries()[0].enabled == true);
    REQUIRE(wc.entries()[1].id == "network");
    REQUIRE(wc.entries()[1].enabled == false);

    // Rest should be appended with their default_enabled value
    for (size_t i = 2; i < wc.entries().size(); ++i) {
        const auto* def = find_widget_def(wc.entries()[i].id);
        REQUIRE(def != nullptr);
        REQUIRE(wc.entries()[i].enabled == def->default_enabled);
    }
}

// ============================================================================
// Config tests — unknown widget IDs dropped
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: unknown widget ID in saved JSON gets dropped",
                 "[panel_widget][widget_config]") {
    json widgets = json::array({
        {{"id", "power"}, {"enabled", true}},
        {{"id", "bogus_widget"}, {"enabled", true}},
        {{"id", "network"}, {"enabled", false}},
    });
    setup_with_widgets(widgets);

    PanelWidgetConfig wc("home", config);
    wc.load();

    // bogus_widget should be dropped, so total is still widget_def_count
    REQUIRE(wc.entries().size() == widget_def_count());

    // First should be power, second should be network (bogus skipped)
    REQUIRE(wc.entries()[0].id == "power");
    REQUIRE(wc.entries()[1].id == "network");
}

// ============================================================================
// Config tests — reset to defaults
// ============================================================================

TEST_CASE_METHOD(
    PanelWidgetConfigFixture,
    "PanelWidgetConfig: reset to defaults restores all widgets enabled in default order",
    "[panel_widget][widget_config]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    // Customize
    wc.set_enabled(0, false);
    wc.reorder(0, 5);

    // Reset
    wc.reset_to_defaults();

    const auto& entries = wc.entries();
    const auto& defs = get_all_widget_defs();
    REQUIRE(entries.size() == defs.size());

    for (size_t i = 0; i < entries.size(); ++i) {
        REQUIRE(entries[i].id == defs[i].id);
        REQUIRE(entries[i].enabled == defs[i].default_enabled);
    }
}

// ============================================================================
// Config tests — duplicate IDs in saved JSON
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: duplicate IDs in saved JSON keeps only first occurrence",
                 "[panel_widget][widget_config]") {
    json widgets = json::array({
        {{"id", "power"}, {"enabled", true}},
        {{"id", "network"}, {"enabled", true}},
        {{"id", "power"}, {"enabled", false}}, // duplicate
        {{"id", "temperature"}, {"enabled", true}},
    });
    setup_with_widgets(widgets);

    PanelWidgetConfig wc("home", config);
    wc.load();

    REQUIRE(wc.entries().size() == widget_def_count());

    // power should appear once, with enabled=true (first occurrence)
    REQUIRE(wc.entries()[0].id == "power");
    REQUIRE(wc.entries()[0].enabled == true);

    // Verify no duplicate power entries
    int power_count = 0;
    for (const auto& e : wc.entries()) {
        if (e.id == "power") {
            ++power_count;
        }
    }
    REQUIRE(power_count == 1);
}

// ============================================================================
// Config tests — is_enabled convenience
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: is_enabled returns false for unknown ID",
                 "[panel_widget][widget_config]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    REQUIRE(wc.is_enabled("nonexistent") == false);
}

// ============================================================================
// Config tests — malformed field types
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: malformed field types skip entry but keep others",
                 "[panel_widget][widget_config]") {
    json widgets = json::array({
        {{"id", "power"}, {"enabled", true}},
        {{"id", 42}, {"enabled", true}},         // id is not string
        {{"id", "network"}, {"enabled", "yes"}}, // enabled is not bool
        {{"id", "temperature"}, {"enabled", false}},
    });
    setup_with_widgets(widgets);

    PanelWidgetConfig wc("home", config);
    wc.load();

    // Bad entries skipped, good entries kept, rest appended
    REQUIRE(wc.entries().size() == widget_def_count());
    REQUIRE(wc.entries()[0].id == "power");
    REQUIRE(wc.entries()[0].enabled == true);
    REQUIRE(wc.entries()[1].id == "temperature");
    REQUIRE(wc.entries()[1].enabled == false);
}

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: panel_widgets key is not an array falls back to defaults",
                 "[panel_widget][widget_config]") {
    get_data()["panel_widgets"]["home"] = "corrupted";

    PanelWidgetConfig wc("home", config);
    wc.load();

    const auto& defs = get_all_widget_defs();
    REQUIRE(wc.entries().size() == defs.size());
    for (size_t i = 0; i < defs.size(); ++i) {
        REQUIRE(wc.entries()[i].id == defs[i].id);
        REQUIRE(wc.entries()[i].enabled == defs[i].default_enabled);
    }
}

// ============================================================================
// Config tests — set_enabled out of bounds
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: set_enabled out of bounds is a no-op",
                 "[panel_widget][widget_config]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    auto before = wc.entries();
    wc.set_enabled(999, false);
    REQUIRE(wc.entries() == before);
}

// ============================================================================
// Registry tests — field completeness
// ============================================================================

TEST_CASE("PanelWidgetRegistry: all defs have non-null required fields",
          "[panel_widget][widget_config]") {
    const auto& defs = get_all_widget_defs();
    for (const auto& def : defs) {
        CAPTURE(def.id);
        REQUIRE(def.id != nullptr);
        REQUIRE(def.display_name != nullptr);
        REQUIRE(def.icon != nullptr);
        REQUIRE(def.description != nullptr);
        REQUIRE(def.translation_tag != nullptr);
        // hardware_gate_subject CAN be nullptr (always-available widgets)
    }
}

TEST_CASE("PanelWidgetRegistry: all IDs are non-empty strings", "[panel_widget][widget_config]") {
    const auto& defs = get_all_widget_defs();
    for (const auto& def : defs) {
        REQUIRE(std::string_view(def.id).size() > 0);
        REQUIRE(std::string_view(def.display_name).size() > 0);
        REQUIRE(std::string_view(def.icon).size() > 0);
        REQUIRE(std::string_view(def.description).size() > 0);
    }
}

TEST_CASE("PanelWidgetRegistry: can find every registered widget by ID",
          "[panel_widget][widget_config]") {
    const auto& defs = get_all_widget_defs();
    for (const auto& def : defs) {
        const auto* found = find_widget_def(def.id);
        REQUIRE(found != nullptr);
        REQUIRE(found->id == std::string_view(def.id));
    }
}

TEST_CASE("PanelWidgetRegistry: known hardware-gated widgets have gate subjects",
          "[panel_widget][widget_config]") {
    // These widgets require specific hardware
    const char* gated[] = {"power",        "ams",   "led",      "humidity",
                           "width_sensor", "probe", "filament", "thermistor"};
    for (const auto* id : gated) {
        CAPTURE(id);
        const auto* def = find_widget_def(id);
        REQUIRE(def != nullptr);
        REQUIRE(def->hardware_gate_subject != nullptr);
    }
}

TEST_CASE("PanelWidgetRegistry: always-available widgets have no gate subject",
          "[panel_widget][widget_config]") {
    const char* always[] = {"network", "firmware_restart", "temperature", "notifications"};
    for (const auto* id : always) {
        CAPTURE(id);
        const auto* def = find_widget_def(id);
        REQUIRE(def != nullptr);
        REQUIRE(def->hardware_gate_subject == nullptr);
    }
}

// ============================================================================
// Config tests — reorder edge cases
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture, "PanelWidgetConfig: reorder to last position works",
                 "[panel_widget][widget_config]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    size_t last = wc.entries().size() - 1;
    std::string moved_id = wc.entries()[0].id;
    wc.reorder(0, last);

    REQUIRE(wc.entries()[last].id == moved_id);
}

TEST_CASE_METHOD(PanelWidgetConfigFixture, "PanelWidgetConfig: reorder from last to first works",
                 "[panel_widget][widget_config]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    size_t last = wc.entries().size() - 1;
    std::string moved_id = wc.entries()[last].id;
    wc.reorder(last, 0);

    REQUIRE(wc.entries()[0].id == moved_id);
}

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: reorder preserves enabled state of moved item",
                 "[panel_widget][widget_config]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    wc.set_enabled(3, false);
    std::string moved_id = wc.entries()[3].id;
    wc.reorder(3, 0);

    REQUIRE(wc.entries()[0].id == moved_id);
    REQUIRE(wc.entries()[0].enabled == false);
}

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: multiple reorders produce correct final order",
                 "[panel_widget][widget_config]") {
    setup_empty_config();
    PanelWidgetConfig wc("home", config);
    wc.load();

    // Capture IDs for first 4
    std::string id0 = wc.entries()[0].id;
    std::string id1 = wc.entries()[1].id;
    std::string id2 = wc.entries()[2].id;
    std::string id3 = wc.entries()[3].id;

    // Move 0→2, then 3→1
    wc.reorder(0, 2); // [id1, id2, id0, id3, ...]
    wc.reorder(3, 1); // [id1, id3, id2, id0, ...]

    REQUIRE(wc.entries()[0].id == id1);
    REQUIRE(wc.entries()[1].id == id3);
    REQUIRE(wc.entries()[2].id == id2);
    REQUIRE(wc.entries()[3].id == id0);
}

// ============================================================================
// Config tests — save-load round trip with reorder
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: reorder + toggle + save + reload preserves everything",
                 "[panel_widget][widget_config]") {
    setup_empty_config();

    PanelWidgetConfig wc1("home", config);
    wc1.load();

    // Do several operations
    wc1.set_enabled(0, false);
    wc1.set_enabled(4, false);
    wc1.reorder(2, 8);
    wc1.reorder(0, 5);
    wc1.save();

    // Reload
    PanelWidgetConfig wc2("home", config);
    wc2.load();

    REQUIRE(wc1.entries().size() == wc2.entries().size());
    for (size_t i = 0; i < wc1.entries().size(); ++i) {
        CAPTURE(i);
        REQUIRE(wc1.entries()[i].id == wc2.entries()[i].id);
        REQUIRE(wc1.entries()[i].enabled == wc2.entries()[i].enabled);
    }
}

// ============================================================================
// Config tests — empty array in JSON
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: empty array in JSON falls back to defaults",
                 "[panel_widget][widget_config]") {
    setup_with_widgets(json::array());

    PanelWidgetConfig wc("home", config);
    wc.load();

    const auto& defs = get_all_widget_defs();
    REQUIRE(wc.entries().size() == defs.size());
    for (size_t i = 0; i < defs.size(); ++i) {
        REQUIRE(wc.entries()[i].id == defs[i].id);
        REQUIRE(wc.entries()[i].enabled == defs[i].default_enabled);
    }
}

// ============================================================================
// Per-panel config tests
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: per-panel load/save uses panel_widgets path",
                 "[panel_widget][widget_config]") {
    json widgets = json::array({
        {{"id", "power"}, {"enabled", true}},
        {{"id", "network"}, {"enabled", false}},
    });
    setup_with_widgets(widgets, "home");

    PanelWidgetConfig wc("home", config);
    wc.load();

    REQUIRE(wc.entries()[0].id == "power");
    REQUIRE(wc.entries()[0].enabled == true);
    REQUIRE(wc.entries()[1].id == "network");
    REQUIRE(wc.entries()[1].enabled == false);

    // Save and verify it writes to panel_widgets.home
    wc.save();
    REQUIRE(get_data().contains("panel_widgets"));
    REQUIRE(get_data()["panel_widgets"].contains("home"));
    REQUIRE(get_data()["panel_widgets"]["home"].is_array());
}

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: non-home panel starts with defaults when no config exists",
                 "[panel_widget][widget_config]") {
    setup_empty_config();

    PanelWidgetConfig wc("controls", config);
    wc.load();

    // Should get defaults from registry
    const auto& defs = get_all_widget_defs();
    REQUIRE(wc.entries().size() == defs.size());
    for (size_t i = 0; i < defs.size(); ++i) {
        REQUIRE(wc.entries()[i].id == defs[i].id);
        REQUIRE(wc.entries()[i].enabled == defs[i].default_enabled);
    }
}

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: different panels have independent configs",
                 "[panel_widget][widget_config]") {
    setup_empty_config();

    // Set up home config
    PanelWidgetConfig home_wc("home", config);
    home_wc.load();
    home_wc.set_enabled(0, false);
    home_wc.save();

    // Set up controls config (should be independent)
    PanelWidgetConfig ctrl_wc("controls", config);
    ctrl_wc.load();

    // Controls should still have defaults (not affected by home's changes)
    const auto& defs = get_all_widget_defs();
    REQUIRE(ctrl_wc.entries()[0].enabled == defs[0].default_enabled);

    // Home should have its customization
    PanelWidgetConfig home_wc2("home", config);
    home_wc2.load();
    REQUIRE(home_wc2.entries()[0].enabled == false);
}

// ============================================================================
// Migration tests — legacy home_widgets → panel_widgets.home
// ============================================================================

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: migrates legacy home_widgets to panel_widgets.home",
                 "[panel_widget][widget_config][migration]") {
    // Set up old-style flat config
    json legacy = json::array({
        {{"id", "power"}, {"enabled", true}},
        {{"id", "network"}, {"enabled", false}},
        {{"id", "temperature"}, {"enabled", true}},
    });
    setup_with_legacy_widgets(legacy);

    // Verify legacy key exists before migration
    REQUIRE(get_data().contains("home_widgets"));

    PanelWidgetConfig wc("home", config);
    wc.load();

    // Entries should be loaded correctly from migrated data
    REQUIRE(wc.entries()[0].id == "power");
    REQUIRE(wc.entries()[0].enabled == true);
    REQUIRE(wc.entries()[1].id == "network");
    REQUIRE(wc.entries()[1].enabled == false);
    REQUIRE(wc.entries()[2].id == "temperature");
    REQUIRE(wc.entries()[2].enabled == true);

    // Migration should have moved data to new location and removed old key
    REQUIRE(get_data().contains("panel_widgets"));
    REQUIRE(get_data()["panel_widgets"].contains("home"));
    REQUIRE(get_data()["panel_widgets"]["home"].is_array());
    REQUIRE_FALSE(get_data().contains("home_widgets"));
}

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: migration does not trigger for non-home panels",
                 "[panel_widget][widget_config][migration]") {
    // Set up legacy home_widgets
    json legacy = json::array({
        {{"id", "power"}, {"enabled", true}},
    });
    setup_with_legacy_widgets(legacy);

    // Loading "controls" should NOT migrate home_widgets
    PanelWidgetConfig wc("controls", config);
    wc.load();

    // Legacy key should still exist (untouched)
    REQUIRE(get_data().contains("home_widgets"));

    // Controls should get defaults
    const auto& defs = get_all_widget_defs();
    REQUIRE(wc.entries().size() == defs.size());
}

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: migration skipped if panel_widgets.home already exists",
                 "[panel_widget][widget_config][migration]") {
    // Set up both legacy and new-style config
    json legacy = json::array({
        {{"id", "power"}, {"enabled", false}},
    });
    json new_style = json::array({
        {{"id", "network"}, {"enabled", true}},
        {{"id", "temperature"}, {"enabled", true}},
    });

    get_data() = json::object();
    get_data()["home_widgets"] = legacy;
    get_data()["panel_widgets"] = json::object();
    get_data()["panel_widgets"]["home"] = new_style;

    PanelWidgetConfig wc("home", config);
    wc.load();

    // Should use the new-style config, not the legacy one
    REQUIRE(wc.entries()[0].id == "network");
    REQUIRE(wc.entries()[1].id == "temperature");

    // Legacy key should still exist (not removed since no migration happened)
    REQUIRE(get_data().contains("home_widgets"));
}

TEST_CASE_METHOD(PanelWidgetConfigFixture,
                 "PanelWidgetConfig: migration preserves per-widget config",
                 "[panel_widget][widget_config][migration]") {
    json legacy = json::array({
        {{"id", "temperature"}, {"enabled", true}, {"config", {{"sensor", "extruder"}}}},
        {{"id", "power"}, {"enabled", true}},
    });
    setup_with_legacy_widgets(legacy);

    PanelWidgetConfig wc("home", config);
    wc.load();

    // Per-widget config should survive migration
    REQUIRE(wc.entries()[0].id == "temperature");
    auto widget_cfg = wc.get_widget_config("temperature");
    REQUIRE(widget_cfg.contains("sensor"));
    REQUIRE(widget_cfg["sensor"] == "extruder");
}
