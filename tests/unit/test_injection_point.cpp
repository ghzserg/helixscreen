// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 HelixScreen Contributors
 *
 * Unit tests for the UI injection point framework.
 * Tests registration, injection, and cleanup lifecycle.
 */

#include "../lvgl_test_fixture.h"
#include "injection_point_manager.h"

#include "../catch_amalgamated.hpp"

using namespace helix::plugin;

// Test fixture that provides LVGL initialization
class InjectionPointTestFixture : public LVGLTestFixture {
  public:
    InjectionPointTestFixture() : LVGLTestFixture() {
        // Clear any previously registered points between tests
        // Since InjectionPointManager is a singleton, we need to clean up
        for (const auto& point_id : get_manager().get_registered_points()) {
            get_manager().unregister_point(point_id);
        }
    }

    ~InjectionPointTestFixture() override {
        // Clean up all injection points
        for (const auto& point_id : get_manager().get_registered_points()) {
            get_manager().unregister_point(point_id);
        }
    }

    InjectionPointManager& get_manager() {
        return InjectionPointManager::instance();
    }

    // Helper to create a test container
    lv_obj_t* create_test_container() {
        lv_obj_t* container = lv_obj_create(test_screen());
        lv_obj_set_size(container, 200, 100);
        lv_obj_set_layout(container, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
        return container;
    }
};

// ============================================================================
// Injection Point Registration Tests
// ============================================================================

TEST_CASE_METHOD(InjectionPointTestFixture, "InjectionPointManager registration",
                 "[injection_point][registration]") {
    SECTION("Register and unregister injection point") {
        auto* container = create_test_container();

        REQUIRE_FALSE(get_manager().has_point("test_point"));

        get_manager().register_point("test_point", container);

        REQUIRE(get_manager().has_point("test_point"));

        get_manager().unregister_point("test_point");

        REQUIRE_FALSE(get_manager().has_point("test_point"));

        lv_obj_delete(container);
    }

    SECTION("Cannot register with null container") {
        // Should log error but not crash
        get_manager().register_point("null_point", nullptr);

        REQUIRE_FALSE(get_manager().has_point("null_point"));
    }

    SECTION("Duplicate registration with same container is allowed") {
        auto* container = create_test_container();

        get_manager().register_point("dup_point", container);
        get_manager().register_point("dup_point", container); // Should be silent

        REQUIRE(get_manager().has_point("dup_point"));

        get_manager().unregister_point("dup_point");
        lv_obj_delete(container);
    }

    SECTION("Re-registration with different container updates it") {
        auto* container1 = create_test_container();
        auto* container2 = create_test_container();

        get_manager().register_point("reregister_point", container1);
        REQUIRE(get_manager().has_point("reregister_point"));

        // Re-register with different container (should warn but succeed)
        get_manager().register_point("reregister_point", container2);
        REQUIRE(get_manager().has_point("reregister_point"));

        get_manager().unregister_point("reregister_point");
        lv_obj_delete(container1);
        lv_obj_delete(container2);
    }

    SECTION("Get registered points returns all point IDs") {
        auto* container1 = create_test_container();
        auto* container2 = create_test_container();

        get_manager().register_point("point_a", container1);
        get_manager().register_point("point_b", container2);

        auto points = get_manager().get_registered_points();

        REQUIRE(points.size() == 2);
        REQUIRE(std::find(points.begin(), points.end(), "point_a") != points.end());
        REQUIRE(std::find(points.begin(), points.end(), "point_b") != points.end());

        get_manager().unregister_point("point_a");
        get_manager().unregister_point("point_b");
        lv_obj_delete(container1);
        lv_obj_delete(container2);
    }
}

// ============================================================================
// Widget Injection Tests
// ============================================================================

TEST_CASE_METHOD(InjectionPointTestFixture, "InjectionPointManager widget injection",
                 "[injection_point][injection]") {
    SECTION("Inject fails for unregistered point") {
        bool result = get_manager().inject_widget("plugin_a", "nonexistent_point", "test_component",
                                                  WidgetCallbacks{});

        REQUIRE_FALSE(result);
    }

    SECTION("Inject fails for null container (edge case)") {
        // This shouldn't happen in practice, but test robustness
        // Can't directly test this without modifying internal state
        // but we verify the API handles missing points gracefully
        REQUIRE_FALSE(get_manager().has_point("no_such_point"));
        REQUIRE_FALSE(get_manager().inject_widget("plugin_x", "no_such_point", "component", {}));
    }

    SECTION("Widget count returns correct count") {
        auto* container = create_test_container();
        get_manager().register_point("count_test", container);

        REQUIRE(get_manager().get_widget_count("count_test") == 0);

        // Note: inject_widget would require a registered XML component to succeed
        // so we can't fully test injection without XML infrastructure
        // This test verifies the counting API works

        get_manager().unregister_point("count_test");
        lv_obj_delete(container);
    }
}

// ============================================================================
// Plugin Widget Removal Tests
// ============================================================================

TEST_CASE_METHOD(InjectionPointTestFixture, "InjectionPointManager plugin cleanup",
                 "[injection_point][cleanup]") {
    SECTION("Remove plugin widgets for nonexistent plugin is safe") {
        // Should not crash and should complete without error
        get_manager().remove_plugin_widgets("nonexistent_plugin");
        // Success if no crash
        SUCCEED();
    }

    SECTION("Get plugin widgets returns empty for unknown plugin") {
        auto widgets = get_manager().get_plugin_widgets("unknown_plugin");
        REQUIRE(widgets.empty());
    }
}

// ============================================================================
// Callback Invocation Tests
// ============================================================================

TEST_CASE_METHOD(InjectionPointTestFixture, "InjectionPointManager callbacks",
                 "[injection_point][callbacks]") {
    SECTION("Callback structure can be created with lambdas") {
        bool create_called = false;
        bool destroy_called = false;

        WidgetCallbacks callbacks;
        callbacks.on_create = [&create_called](lv_obj_t* widget) {
            (void)widget;
            create_called = true;
        };
        callbacks.on_destroy = [&destroy_called](lv_obj_t* widget) {
            (void)widget;
            destroy_called = true;
        };

        // Verify callbacks can be invoked
        callbacks.on_create(nullptr);
        callbacks.on_destroy(nullptr);

        REQUIRE(create_called);
        REQUIRE(destroy_called);
    }

    SECTION("Empty callbacks are safe to check") {
        WidgetCallbacks callbacks;

        // Empty callbacks should be falsy
        REQUIRE_FALSE(callbacks.on_create);
        REQUIRE_FALSE(callbacks.on_destroy);

        // Can safely check before calling
        if (callbacks.on_create) {
            callbacks.on_create(nullptr);
        }
        // Success if no crash
        SUCCEED();
    }
}

// ============================================================================
// Thread Safety Tests (basic verification)
// ============================================================================

TEST_CASE_METHOD(InjectionPointTestFixture, "InjectionPointManager thread safety",
                 "[injection_point][threading]") {
    SECTION("Singleton returns same instance") {
        auto& instance1 = InjectionPointManager::instance();
        auto& instance2 = InjectionPointManager::instance();

        REQUIRE(&instance1 == &instance2);
    }

    SECTION("Query methods are const-safe") {
        const InjectionPointManager& const_manager = get_manager();

        // These should compile and work on const reference
        REQUIRE_FALSE(const_manager.has_point("test"));
        auto points = const_manager.get_registered_points();
        auto widgets = const_manager.get_plugin_widgets("test");
        auto count = const_manager.get_widget_count("test");

        REQUIRE(points.empty());
        REQUIRE(widgets.empty());
        REQUIRE(count == 0);
    }
}

// ============================================================================
// InjectedWidget Structure Tests
// ============================================================================

TEST_CASE("InjectedWidget structure", "[injection_point][structure]") {
    SECTION("Default initialization") {
        InjectedWidget widget;

        REQUIRE(widget.plugin_id.empty());
        REQUIRE(widget.injection_point.empty());
        REQUIRE(widget.component_name.empty());
        REQUIRE(widget.widget == nullptr);
        REQUIRE_FALSE(widget.callbacks.on_create);
        REQUIRE_FALSE(widget.callbacks.on_destroy);
    }

    SECTION("Can be copied") {
        InjectedWidget original;
        original.plugin_id = "test_plugin";
        original.injection_point = "test_point";
        original.component_name = "test_component";

        InjectedWidget copy = original;

        REQUIRE(copy.plugin_id == "test_plugin");
        REQUIRE(copy.injection_point == "test_point");
        REQUIRE(copy.component_name == "test_component");
    }
}
