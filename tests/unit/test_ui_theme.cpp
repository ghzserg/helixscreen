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
 */

#include "ui_fonts.h"
#include "ui_theme.h"

#include "../ui_test_utils.h"

#include <cstring>

#include "../catch_amalgamated.hpp"

// Helper to extract RGB from lv_color_t (masks out alpha channel)
// lv_color_to_u32() returns 0xAARRGGBB, we only care about 0x00RRGGBB
#define COLOR_RGB(color) (lv_color_to_u32(color) & 0x00FFFFFF)

// ============================================================================
// Color Parsing Tests
// ============================================================================

TEST_CASE("UI Theme: Parse valid hex color", "[ui_theme][color]") {
    lv_color_t color = ui_theme_parse_color("#FF0000");

    // Red channel should be max
    REQUIRE(COLOR_RGB(color) == 0xFF0000);
}

TEST_CASE("UI Theme: Parse various colors", "[ui_theme][color]") {
    SECTION("Black") {
        lv_color_t color = ui_theme_parse_color("#000000");
        REQUIRE(COLOR_RGB(color) == 0x000000);
    }

    SECTION("White") {
        lv_color_t color = ui_theme_parse_color("#FFFFFF");
        REQUIRE(COLOR_RGB(color) == 0xFFFFFF);
    }

    SECTION("Red") {
        lv_color_t color = ui_theme_parse_color("#FF0000");
        REQUIRE(COLOR_RGB(color) == 0xFF0000);
    }

    SECTION("Green") {
        lv_color_t color = ui_theme_parse_color("#00FF00");
        REQUIRE(COLOR_RGB(color) == 0x00FF00);
    }

    SECTION("Blue") {
        lv_color_t color = ui_theme_parse_color("#0000FF");
        REQUIRE(COLOR_RGB(color) == 0x0000FF);
    }
}

TEST_CASE("UI Theme: Parse lowercase hex", "[ui_theme][color]") {
    lv_color_t color1 = ui_theme_parse_color("#ff0000");
    lv_color_t color2 = ui_theme_parse_color("#FF0000");

    REQUIRE(COLOR_RGB(color1) == COLOR_RGB(color2));
}

TEST_CASE("UI Theme: Parse mixed case hex", "[ui_theme][color]") {
    lv_color_t color = ui_theme_parse_color("#AbCdEf");

    REQUIRE(COLOR_RGB(color) == 0xABCDEF);
}

TEST_CASE("UI Theme: Parse typical UI colors", "[ui_theme][color]") {
    SECTION("Primary color (example)") {
        lv_color_t color = ui_theme_parse_color("#2196F3");
        REQUIRE(COLOR_RGB(color) == 0x2196F3);
    }

    SECTION("Success green") {
        lv_color_t color = ui_theme_parse_color("#4CAF50");
        REQUIRE(COLOR_RGB(color) == 0x4CAF50);
    }

    SECTION("Warning orange") {
        lv_color_t color = ui_theme_parse_color("#FF9800");
        REQUIRE(COLOR_RGB(color) == 0xFF9800);
    }

    SECTION("Error red") {
        lv_color_t color = ui_theme_parse_color("#F44336");
        REQUIRE(COLOR_RGB(color) == 0xF44336);
    }

    SECTION("Gray") {
        lv_color_t color = ui_theme_parse_color("#9E9E9E");
        REQUIRE(COLOR_RGB(color) == 0x9E9E9E);
    }
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_CASE("UI Theme: Handle invalid color strings", "[ui_theme][color][error]") {
    SECTION("NULL pointer") {
        lv_color_t color = ui_theme_parse_color(nullptr);
        // Should return black (0x000000) as fallback
        REQUIRE(COLOR_RGB(color) == 0x000000);
    }

    SECTION("Missing # prefix") {
        lv_color_t color = ui_theme_parse_color("FF0000");
        // Should return black as fallback
        REQUIRE(COLOR_RGB(color) == 0x000000);
    }

    SECTION("Empty string") {
        lv_color_t color = ui_theme_parse_color("");
        // Should return black as fallback
        REQUIRE(COLOR_RGB(color) == 0x000000);
    }

    SECTION("Just # symbol") {
        lv_color_t color = ui_theme_parse_color("#");
        // Should parse as 0 (black)
        REQUIRE(COLOR_RGB(color) == 0x000000);
    }
}

TEST_CASE("UI Theme: Handle malformed hex strings", "[ui_theme][color][error]") {
    SECTION("Too short") {
        lv_color_t color = ui_theme_parse_color("#FF");
        // Should parse as 0xFF (255)
        REQUIRE(COLOR_RGB(color) == 0x0000FF);
    }

    SECTION("Invalid hex characters") {
        lv_color_t color = ui_theme_parse_color("#GGGGGG");
        // Invalid hex, should parse as 0
        REQUIRE(COLOR_RGB(color) == 0x000000);
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("UI Theme: Color parsing edge cases", "[ui_theme][color][edge]") {
    SECTION("All zeros") {
        lv_color_t color = ui_theme_parse_color("#000000");
        REQUIRE(COLOR_RGB(color) == 0x000000);
    }

    SECTION("All ones") {
        lv_color_t color = ui_theme_parse_color("#111111");
        REQUIRE(COLOR_RGB(color) == 0x111111);
    }

    SECTION("All Fs") {
        lv_color_t color = ui_theme_parse_color("#FFFFFF");
        REQUIRE(COLOR_RGB(color) == 0xFFFFFF);
    }

    SECTION("Leading zeros") {
        lv_color_t color = ui_theme_parse_color("#000001");
        REQUIRE(COLOR_RGB(color) == 0x000001);
    }
}

// ============================================================================
// Consistency Tests
// ============================================================================

TEST_CASE("UI Theme: Multiple parses of same color", "[ui_theme][color]") {
    const char* color_str = "#2196F3";

    lv_color_t color1 = ui_theme_parse_color(color_str);
    lv_color_t color2 = ui_theme_parse_color(color_str);
    lv_color_t color3 = ui_theme_parse_color(color_str);

    REQUIRE(COLOR_RGB(color1) == COLOR_RGB(color2));
    REQUIRE(COLOR_RGB(color2) == COLOR_RGB(color3));
}

// ============================================================================
// Integration Tests with LVGL
// ============================================================================

TEST_CASE("UI Theme: Parsed colors work with LVGL", "[ui_theme][integration]") {
    lv_init();

    lv_color_t red = ui_theme_parse_color("#FF0000");
    lv_color_t green = ui_theme_parse_color("#00FF00");
    lv_color_t blue = ui_theme_parse_color("#0000FF");

    // Create a simple object and set its background color
    lv_obj_t* obj = lv_obj_create(lv_screen_active());
    REQUIRE(obj != nullptr);

    lv_obj_set_style_bg_color(obj, red, 0);
    lv_obj_set_style_bg_color(obj, green, 0);
    lv_obj_set_style_bg_color(obj, blue, 0);

    // Cleanup
    lv_obj_delete(obj);
}

// ============================================================================
// Color Comparison Tests
// ============================================================================

TEST_CASE("UI Theme: Color equality", "[ui_theme][color]") {
    lv_color_t color1 = ui_theme_parse_color("#FF0000");
    lv_color_t color2 = ui_theme_parse_color("#FF0000");
    lv_color_t color3 = ui_theme_parse_color("#00FF00");

    REQUIRE(COLOR_RGB(color1) == COLOR_RGB(color2));
    REQUIRE(COLOR_RGB(color1) != COLOR_RGB(color3));
}

// ============================================================================
// Real-world Color Examples
// ============================================================================

TEST_CASE("UI Theme: Parse colors from globals.xml", "[ui_theme][color][integration]") {
    // These are typical colors that might appear in globals.xml

    SECTION("Primary colors") {
        lv_color_t primary_light = ui_theme_parse_color("#2196F3");
        lv_color_t primary_dark = ui_theme_parse_color("#1976D2");

        REQUIRE(COLOR_RGB(primary_light) == 0x2196F3);
        REQUIRE(COLOR_RGB(primary_dark) == 0x1976D2);
    }

    SECTION("Background colors") {
        lv_color_t bg_light = ui_theme_parse_color("#FFFFFF");
        lv_color_t bg_dark = ui_theme_parse_color("#121212");

        REQUIRE(COLOR_RGB(bg_light) == 0xFFFFFF);
        REQUIRE(COLOR_RGB(bg_dark) == 0x121212);
    }

    SECTION("Text colors") {
        lv_color_t text_light = ui_theme_parse_color("#000000");
        lv_color_t text_dark = ui_theme_parse_color("#FFFFFF");

        REQUIRE(COLOR_RGB(text_light) == 0x000000);
        REQUIRE(COLOR_RGB(text_dark) == 0xFFFFFF);
    }

    SECTION("State colors") {
        lv_color_t success = ui_theme_parse_color("#4CAF50");
        lv_color_t warning = ui_theme_parse_color("#FF9800");
        lv_color_t error = ui_theme_parse_color("#F44336");

        REQUIRE(COLOR_RGB(success) == 0x4CAF50);
        REQUIRE(COLOR_RGB(warning) == 0xFF9800);
        REQUIRE(COLOR_RGB(error) == 0xF44336);
    }
}

// ============================================================================
// Responsive Breakpoint Tests
// ============================================================================

TEST_CASE("UI Theme: Breakpoint suffix detection", "[ui_theme][responsive]") {
    SECTION("Small breakpoint (≤480px)") {
        // Resolutions at or below 480 should select _small variants
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(320), "_small") == 0);
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(480), "_small") == 0);
    }

    SECTION("Medium breakpoint (481-800px)") {
        // Resolutions between 481 and 800 should select _medium variants
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(481), "_medium") == 0);
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(600), "_medium") == 0);
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(800), "_medium") == 0);
    }

    SECTION("Large breakpoint (>800px)") {
        // Resolutions above 800 should select _large variants
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(801), "_large") == 0);
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(1024), "_large") == 0);
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(1280), "_large") == 0);
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(1920), "_large") == 0);
    }
}

TEST_CASE("UI Theme: Breakpoint boundary conditions", "[ui_theme][responsive]") {
    SECTION("Exact boundary: 480 → small") {
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(480), "_small") == 0);
    }

    SECTION("Exact boundary: 481 → medium") {
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(481), "_medium") == 0);
    }

    SECTION("Exact boundary: 800 → medium") {
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(800), "_medium") == 0);
    }

    SECTION("Exact boundary: 801 → large") {
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(801), "_large") == 0);
    }
}

TEST_CASE("UI Theme: Target hardware resolutions", "[ui_theme][responsive]") {
    // Test against the specific target hardware resolutions from ui_theme.h
    SECTION("480x320 (tiny screen) → SMALL") {
        // max(480, 320) = 480
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(480), "_small") == 0);
    }

    SECTION("800x480 (small screen) → MEDIUM") {
        // max(800, 480) = 800
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(800), "_medium") == 0);
    }

    SECTION("1024x600 (medium screen) → LARGE") {
        // max(1024, 600) = 1024
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(1024), "_large") == 0);
    }

    SECTION("1280x720 (large screen) → LARGE") {
        // max(1280, 720) = 1280
        REQUIRE(strcmp(ui_theme_get_breakpoint_suffix(1280), "_large") == 0);
    }
}

TEST_CASE("UI Theme: Font height helper", "[ui_theme][responsive]") {
    // Test that font height helper returns valid values for project fonts
    // Note: This project uses noto_sans_* fonts instead of lv_font_montserrat_*
    SECTION("Valid fonts return positive height") {
        REQUIRE(ui_theme_get_font_height(&noto_sans_12) > 0);
        REQUIRE(ui_theme_get_font_height(&noto_sans_16) > 0);
        REQUIRE(ui_theme_get_font_height(&noto_sans_20) > 0);
    }

    SECTION("NULL font returns 0") {
        REQUIRE(ui_theme_get_font_height(nullptr) == 0);
    }

    SECTION("Larger fonts have larger heights") {
        int32_t h12 = ui_theme_get_font_height(&noto_sans_12);
        int32_t h16 = ui_theme_get_font_height(&noto_sans_16);
        int32_t h20 = ui_theme_get_font_height(&noto_sans_20);

        REQUIRE(h12 < h16);
        REQUIRE(h16 < h20);
    }
}
