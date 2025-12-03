/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#include "../../include/ui_nav.h"
#include "../../include/ui_theme.h"
#include "lvgl/lvgl.h"

#include "../catch_amalgamated.hpp"

// Test fixture for navigation tests
class NavigationTestFixture {
  public:
    NavigationTestFixture() {
        // Initialize LVGL for testing
        lv_init();

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

TEST_CASE_METHOD(NavigationTestFixture, "Navigation initialization", "[navigation]") {
    SECTION("Default active panel is HOME") {
        REQUIRE(ui_nav_get_active() == UI_PANEL_HOME);
    }
}

TEST_CASE_METHOD(NavigationTestFixture, "Panel switching", "[navigation]") {
    SECTION("Switch to CONTROLS panel") {
        ui_nav_set_active(UI_PANEL_CONTROLS);
        REQUIRE(ui_nav_get_active() == UI_PANEL_CONTROLS);
    }

    SECTION("Switch to FILAMENT panel") {
        ui_nav_set_active(UI_PANEL_FILAMENT);
        REQUIRE(ui_nav_get_active() == UI_PANEL_FILAMENT);
    }

    SECTION("Switch to SETTINGS panel") {
        ui_nav_set_active(UI_PANEL_SETTINGS);
        REQUIRE(ui_nav_get_active() == UI_PANEL_SETTINGS);
    }

    SECTION("Switch to ADVANCED panel") {
        ui_nav_set_active(UI_PANEL_ADVANCED);
        REQUIRE(ui_nav_get_active() == UI_PANEL_ADVANCED);
    }

    SECTION("Switch back to HOME panel") {
        ui_nav_set_active(UI_PANEL_CONTROLS);
        ui_nav_set_active(UI_PANEL_HOME);
        REQUIRE(ui_nav_get_active() == UI_PANEL_HOME);
    }
}

TEST_CASE_METHOD(NavigationTestFixture, "Invalid panel handling", "[navigation]") {
    SECTION("Setting invalid panel ID does not change active panel") {
        ui_panel_id_t original = ui_nav_get_active();
        ui_nav_set_active((ui_panel_id_t)99); // Invalid panel ID
        REQUIRE(ui_nav_get_active() == original);
    }
}

TEST_CASE_METHOD(NavigationTestFixture, "Repeated panel selection", "[navigation]") {
    SECTION("Setting same panel multiple times is safe") {
        ui_nav_set_active(UI_PANEL_CONTROLS);
        ui_nav_set_active(UI_PANEL_CONTROLS);
        ui_nav_set_active(UI_PANEL_CONTROLS);
        REQUIRE(ui_nav_get_active() == UI_PANEL_CONTROLS);
    }
}

TEST_CASE_METHOD(NavigationTestFixture, "All panels are accessible", "[navigation]") {
    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        ui_nav_set_active((ui_panel_id_t)i);
        REQUIRE(ui_nav_get_active() == (ui_panel_id_t)i);
    }
}
