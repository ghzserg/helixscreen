// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../ui_test_utils.h"
#include "panel_widget.h"
#include "panel_widget_manager.h"
#include "panel_widget_registry.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE("PanelWidgetManager singleton access", "[panel_widget][manager]") {
    auto& mgr = PanelWidgetManager::instance();
    auto& mgr2 = PanelWidgetManager::instance();
    REQUIRE(&mgr == &mgr2);
}

TEST_CASE("PanelWidgetManager shared resources", "[panel_widget][manager]") {
    auto& mgr = PanelWidgetManager::instance();
    mgr.clear_shared_resources();

    SECTION("returns nullptr for unregistered type") {
        REQUIRE(mgr.shared_resource<int>() == nullptr);
    }

    SECTION("register and retrieve") {
        auto val = std::make_shared<int>(42);
        mgr.register_shared_resource<int>(val);
        REQUIRE(mgr.shared_resource<int>() != nullptr);
        REQUIRE(*mgr.shared_resource<int>() == 42);
    }

    SECTION("clear removes all resources") {
        auto val = std::make_shared<int>(99);
        mgr.register_shared_resource<int>(val);
        mgr.clear_shared_resources();
        REQUIRE(mgr.shared_resource<int>() == nullptr);
    }

    SECTION("multiple types coexist") {
        auto i = std::make_shared<int>(10);
        auto s = std::make_shared<std::string>("hello");
        mgr.register_shared_resource<int>(i);
        mgr.register_shared_resource<std::string>(s);
        REQUIRE(*mgr.shared_resource<int>() == 10);
        REQUIRE(*mgr.shared_resource<std::string>() == "hello");
        mgr.clear_shared_resources();
    }
}

TEST_CASE("PanelWidgetManager config change callbacks", "[panel_widget][manager]") {
    auto& mgr = PanelWidgetManager::instance();

    SECTION("callback is invoked on notify") {
        bool called = false;
        mgr.register_rebuild_callback("test_panel", [&called]() { called = true; });
        mgr.notify_config_changed("test_panel");
        REQUIRE(called);
        mgr.unregister_rebuild_callback("test_panel");
    }

    SECTION("notify for nonexistent panel does not crash") {
        mgr.notify_config_changed("nonexistent");
    }

    SECTION("unregister removes callback") {
        int count = 0;
        mgr.register_rebuild_callback("counting", [&count]() { count++; });
        mgr.notify_config_changed("counting");
        REQUIRE(count == 1);
        mgr.unregister_rebuild_callback("counting");
        mgr.notify_config_changed("counting");
        REQUIRE(count == 1);
    }
}

TEST_CASE("PanelWidgetManager populate with null container", "[panel_widget][manager]") {
    auto& mgr = PanelWidgetManager::instance();
    auto widgets = mgr.populate_widgets("home", nullptr);
    REQUIRE(widgets.empty());
}

TEST_CASE("Widget factories are self-registered", "[panel_widget][self_registration]") {
    lv_init_safe(); // Widget registration requires LVGL for XML event callbacks
    helix::init_widget_registrations();

    const char* expected[] = {"temperature", "temp_stack", "led",      "power",
                              "network",     "thermistor", "fan_stack"};
    for (const auto* id : expected) {
        INFO("Checking widget factory: " << id);
        const auto* def = helix::find_widget_def(id);
        REQUIRE(def != nullptr);
        REQUIRE(def->factory != nullptr);
    }
}

TEST_CASE("PanelWidgetManager raw pointer shared resources", "[panel_widget][manager]") {
    auto& mgr = PanelWidgetManager::instance();
    mgr.clear_shared_resources();

    int stack_val = 77;
    mgr.register_shared_resource<int>(&stack_val);
    REQUIRE(mgr.shared_resource<int>() != nullptr);
    REQUIRE(*mgr.shared_resource<int>() == 77);
    mgr.clear_shared_resources();
}
