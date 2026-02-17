// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/theme_manager.h"
#include "../../include/ui_nav.h"
#include "../ui_test_utils.h"
#include "lvgl/lvgl.h"

#include <spdlog/spdlog.h>

#include "../catch_amalgamated.hpp"

// Test fixture for navigation tests
class NavigationTestFixture {
  public:
    NavigationTestFixture() {
        // Initialize LVGL for testing (safe version avoids "already initialized" warnings)
        lv_init_safe();

        // Create a display for testing (headless)
        // LVGL 9 requires aligned buffers - use alignas(64) for portability
        lv_display_t* disp = lv_display_create(800, 480);
        alignas(64) static lv_color_t buf1[800 * 10];
        lv_display_set_buffers(disp, buf1, NULL, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

        // Initialize navigation system
        ui_nav_init();
    }

    ~NavigationTestFixture() {
        // Cleanup is handled by LVGL
    }
};

TEST_CASE_METHOD(NavigationTestFixture, "Navigation initialization", "[core][navigation]") {
    SECTION("Default active panel is HOME") {
        REQUIRE(ui_nav_get_active() == PanelId::Home);
    }
}

TEST_CASE_METHOD(NavigationTestFixture, "Panel switching", "[core][navigation]") {
    SECTION("Switch to CONTROLS panel") {
        ui_nav_set_active(PanelId::Controls);
        REQUIRE(ui_nav_get_active() == PanelId::Controls);
    }

    SECTION("Switch to FILAMENT panel") {
        ui_nav_set_active(PanelId::Filament);
        REQUIRE(ui_nav_get_active() == PanelId::Filament);
    }

    SECTION("Switch to SETTINGS panel") {
        ui_nav_set_active(PanelId::Settings);
        REQUIRE(ui_nav_get_active() == PanelId::Settings);
    }

    SECTION("Switch to ADVANCED panel") {
        ui_nav_set_active(PanelId::Advanced);
        REQUIRE(ui_nav_get_active() == PanelId::Advanced);
    }

    SECTION("Switch back to HOME panel") {
        ui_nav_set_active(PanelId::Controls);
        ui_nav_set_active(PanelId::Home);
        REQUIRE(ui_nav_get_active() == PanelId::Home);
    }
}

TEST_CASE_METHOD(NavigationTestFixture, "Invalid panel handling", "[core][navigation]") {
    SECTION("Setting invalid panel ID does not change active panel") {
        PanelId original = ui_nav_get_active();
        ui_nav_set_active((PanelId)99); // Invalid panel ID
        REQUIRE(ui_nav_get_active() == original);
    }
}

TEST_CASE_METHOD(NavigationTestFixture, "Repeated panel selection", "[core][navigation]") {
    SECTION("Setting same panel multiple times is safe") {
        ui_nav_set_active(PanelId::Controls);
        ui_nav_set_active(PanelId::Controls);
        ui_nav_set_active(PanelId::Controls);
        REQUIRE(ui_nav_get_active() == PanelId::Controls);
    }
}

TEST_CASE_METHOD(NavigationTestFixture, "All panels are accessible", "[core][navigation]") {
    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        ui_nav_set_active((PanelId)i);
        REQUIRE(ui_nav_get_active() == (PanelId)i);
    }
}

// ============================================================================
// Navbar Icon Visibility Tests (XML Integration)
// ============================================================================
// These tests verify that navbar icons show/hide correctly based on
// connection state and klippy state. They require full XML registration.

#include "../lvgl_ui_test_fixture.h"
#include "printer_state.h"

/**
 * @brief Test fixture for navbar XML binding tests
 *
 * Tests the dual-icon pattern where:
 * - Active/Inactive icons show when connected AND klippy ready
 * - Disabled icons show when disconnected OR klippy not ready
 */
class NavbarIconTestFixture : public LVGLUITestFixture {
  public:
    NavbarIconTestFixture() {
        // Create the navigation bar component
        navbar_ = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "navigation_bar", nullptr));
        if (!navbar_) {
            spdlog::error("[NavbarIconTestFixture] Failed to create navigation_bar!");
            return;
        }

        // NOTE: Don't call process_lvgl() in constructor - mDNS timer processing
        // causes test hangs. Subject changes trigger binding updates synchronously.
    }

    ~NavbarIconTestFixture() override {
        if (navbar_) {
            lv_obj_delete(navbar_);
            navbar_ = nullptr;
        }
    }

    /**
     * @brief Check if an object would be visible (no hidden flag on self or ancestors)
     *
     * Unlike lv_obj_is_visible(), this doesn't require an active screen -
     * it just checks the hidden flag chain, which is what we need for testing
     * XML binding behavior.
     */
    bool is_visible(const char* name) {
        lv_obj_t* obj = lv_obj_find_by_name(navbar_, name);
        if (!obj) {
            spdlog::warn("[NavbarIconTestFixture] Could not find object: {}", name);
            return false;
        }

        // Check hidden flag on object itself
        if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
            return false;
        }

        // Check all ancestors for hidden flag
        lv_obj_t* parent = lv_obj_get_parent(obj);
        while (parent) {
            if (lv_obj_has_flag(parent, LV_OBJ_FLAG_HIDDEN)) {
                return false;
            }
            parent = lv_obj_get_parent(parent);
        }

        return true;
    }

    /**
     * @brief Helper to check if an icon is hidden (not visible)
     */
    bool is_hidden(const char* name) {
        return !is_visible(name);
    }

    /**
     * @brief Set nav buttons enabled state directly (combined subject)
     */
    void set_nav_buttons_enabled(bool enabled) {
        lv_subject_set_int(state().get_nav_buttons_enabled_subject(), enabled ? 1 : 0);
    }

    /**
     * @brief Set active panel
     */
    void set_active_panel(int panel_id) {
        ui_nav_set_active(static_cast<PanelId>(panel_id));
    }

    lv_obj_t* navbar_ = nullptr;
};

TEST_CASE_METHOD(NavbarIconTestFixture, "Navbar: Only one icon visible per button",
                 "[navbar][ui_integration]") {
    REQUIRE(navbar_ != nullptr);

    SECTION("Enabled + On Home: shows inactive icons") {
        set_nav_buttons_enabled(true);
        set_active_panel(PanelId::Home); // Not on controls or filament

        // Controls button: inactive should be visible, others hidden
        REQUIRE(is_visible("nav_icon_controls_inactive"));
        REQUIRE(is_hidden("nav_icon_controls_active"));
        REQUIRE(is_hidden("nav_icon_controls_disabled"));

        // Filament button: same pattern
        REQUIRE(is_visible("nav_icon_filament_inactive"));
        REQUIRE(is_hidden("nav_icon_filament_active"));
        REQUIRE(is_hidden("nav_icon_filament_disabled"));
    }

    SECTION("Enabled + On Controls: shows active icon") {
        set_nav_buttons_enabled(true);
        set_active_panel(PanelId::Controls);

        // Controls button: active should be visible
        REQUIRE(is_visible("nav_icon_controls_active"));
        REQUIRE(is_hidden("nav_icon_controls_inactive"));
        REQUIRE(is_hidden("nav_icon_controls_disabled"));
    }

    SECTION("Disabled: shows only disabled icon") {
        set_nav_buttons_enabled(false);
        set_active_panel(PanelId::Home);

        // Controls button: only disabled should be visible
        REQUIRE(is_visible("nav_icon_controls_disabled"));
        REQUIRE(is_hidden("nav_icon_controls_inactive"));
        REQUIRE(is_hidden("nav_icon_controls_active"));

        // Filament button: same pattern
        REQUIRE(is_visible("nav_icon_filament_disabled"));
        REQUIRE(is_hidden("nav_icon_filament_inactive"));
        REQUIRE(is_hidden("nav_icon_filament_active"));
    }
}

TEST_CASE_METHOD(NavbarIconTestFixture, "Navbar: State transitions work correctly",
                 "[navbar][ui_integration]") {
    REQUIRE(navbar_ != nullptr);

    SECTION("Transition: Enabled -> Disabled -> Enabled") {
        // Start enabled
        set_nav_buttons_enabled(true);
        set_active_panel(PanelId::Home);

        REQUIRE(is_visible("nav_icon_controls_inactive"));
        REQUIRE(is_hidden("nav_icon_controls_disabled"));

        // Disable (simulate disconnect or klippy shutdown)
        set_nav_buttons_enabled(false);
        REQUIRE(is_hidden("nav_icon_controls_inactive"));
        REQUIRE(is_visible("nav_icon_controls_disabled"));

        // Re-enable
        set_nav_buttons_enabled(true);
        REQUIRE(is_visible("nav_icon_controls_inactive"));
        REQUIRE(is_hidden("nav_icon_controls_disabled"));
    }

    SECTION("Transition: Panel switch while enabled") {
        set_nav_buttons_enabled(true);
        set_active_panel(PanelId::Home);

        REQUIRE(is_visible("nav_icon_controls_inactive"));
        REQUIRE(is_hidden("nav_icon_controls_active"));

        // Switch to controls panel
        set_active_panel(PanelId::Controls);
        REQUIRE(is_hidden("nav_icon_controls_inactive"));
        REQUIRE(is_visible("nav_icon_controls_active"));

        // Switch back to home
        set_active_panel(PanelId::Home);
        REQUIRE(is_visible("nav_icon_controls_inactive"));
        REQUIRE(is_hidden("nav_icon_controls_active"));
    }
}

// ============================================================================
// Overlay Instance Registration Tests
// ============================================================================

#include "ui_nav_manager.h"

#include "panel_lifecycle.h"

/**
 * @brief Mock implementation of IPanelLifecycle for testing overlay registration
 *
 * Tests that NavigationManager::register_overlay_instance accepts any
 * IPanelLifecycle implementation, not just OverlayBase.
 */
class MockPanelLifecycle : public IPanelLifecycle {
  public:
    void on_activate() override {
        activate_count_++;
    }
    void on_deactivate() override {
        deactivate_count_++;
    }
    const char* get_name() const override {
        return "MockPanel";
    }

    int activate_count_ = 0;
    int deactivate_count_ = 0;
};

TEST_CASE_METHOD(NavbarIconTestFixture, "Overlay registration accepts IPanelLifecycle",
                 "[navigation][overlay]") {
    MockPanelLifecycle mock_panel;

    // Create a test widget to serve as overlay root
    lv_obj_t* test_overlay = lv_obj_create(test_screen());
    REQUIRE(test_overlay != nullptr);

    SECTION("Can register IPanelLifecycle implementation") {
        // Should not throw - IPanelLifecycle is accepted, not just OverlayBase
        NavigationManager::instance().register_overlay_instance(test_overlay, &mock_panel);

        // Verify it was registered by checking we can unregister without error
        NavigationManager::instance().unregister_overlay_instance(test_overlay);
    }

    // Cleanup
    lv_obj_delete(test_overlay);
}
