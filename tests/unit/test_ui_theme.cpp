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

#include "../catch_amalgamated.hpp"
#include "ui_theme.h"
#include "../ui_test_utils.h"

// ============================================================================
// Color Parsing Tests
// ============================================================================

TEST_CASE("UI Theme: Parse valid hex color", "[ui_theme][color]") {
    lv_color_t color = ui_theme_parse_color("#FF0000");

    // Red channel should be max
    REQUIRE(lv_color_to_u32(color) == 0xFF0000);
}

TEST_CASE("UI Theme: Parse various colors", "[ui_theme][color]") {
    SECTION("Black") {
        lv_color_t color = ui_theme_parse_color("#000000");
        REQUIRE(lv_color_to_u32(color) == 0x000000);
    }

    SECTION("White") {
        lv_color_t color = ui_theme_parse_color("#FFFFFF");
        REQUIRE(lv_color_to_u32(color) == 0xFFFFFF);
    }

    SECTION("Red") {
        lv_color_t color = ui_theme_parse_color("#FF0000");
        REQUIRE(lv_color_to_u32(color) == 0xFF0000);
    }

    SECTION("Green") {
        lv_color_t color = ui_theme_parse_color("#00FF00");
        REQUIRE(lv_color_to_u32(color) == 0x00FF00);
    }

    SECTION("Blue") {
        lv_color_t color = ui_theme_parse_color("#0000FF");
        REQUIRE(lv_color_to_u32(color) == 0x0000FF);
    }
}

TEST_CASE("UI Theme: Parse lowercase hex", "[ui_theme][color]") {
    lv_color_t color1 = ui_theme_parse_color("#ff0000");
    lv_color_t color2 = ui_theme_parse_color("#FF0000");

    REQUIRE(lv_color_to_u32(color1) == lv_color_to_u32(color2));
}

TEST_CASE("UI Theme: Parse mixed case hex", "[ui_theme][color]") {
    lv_color_t color = ui_theme_parse_color("#AbCdEf");

    REQUIRE(lv_color_to_u32(color) == 0xABCDEF);
}

TEST_CASE("UI Theme: Parse typical UI colors", "[ui_theme][color]") {
    SECTION("Primary color (example)") {
        lv_color_t color = ui_theme_parse_color("#2196F3");
        REQUIRE(lv_color_to_u32(color) == 0x2196F3);
    }

    SECTION("Success green") {
        lv_color_t color = ui_theme_parse_color("#4CAF50");
        REQUIRE(lv_color_to_u32(color) == 0x4CAF50);
    }

    SECTION("Warning orange") {
        lv_color_t color = ui_theme_parse_color("#FF9800");
        REQUIRE(lv_color_to_u32(color) == 0xFF9800);
    }

    SECTION("Error red") {
        lv_color_t color = ui_theme_parse_color("#F44336");
        REQUIRE(lv_color_to_u32(color) == 0xF44336);
    }

    SECTION("Gray") {
        lv_color_t color = ui_theme_parse_color("#9E9E9E");
        REQUIRE(lv_color_to_u32(color) == 0x9E9E9E);
    }
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_CASE("UI Theme: Handle invalid color strings", "[ui_theme][color][error]") {
    SECTION("NULL pointer") {
        lv_color_t color = ui_theme_parse_color(nullptr);
        // Should return black (0x000000) as fallback
        REQUIRE(lv_color_to_u32(color) == 0x000000);
    }

    SECTION("Missing # prefix") {
        lv_color_t color = ui_theme_parse_color("FF0000");
        // Should return black as fallback
        REQUIRE(lv_color_to_u32(color) == 0x000000);
    }

    SECTION("Empty string") {
        lv_color_t color = ui_theme_parse_color("");
        // Should return black as fallback
        REQUIRE(lv_color_to_u32(color) == 0x000000);
    }

    SECTION("Just # symbol") {
        lv_color_t color = ui_theme_parse_color("#");
        // Should parse as 0 (black)
        REQUIRE(lv_color_to_u32(color) == 0x000000);
    }
}

TEST_CASE("UI Theme: Handle malformed hex strings", "[ui_theme][color][error]") {
    SECTION("Too short") {
        lv_color_t color = ui_theme_parse_color("#FF");
        // Should parse as 0xFF (255)
        REQUIRE(lv_color_to_u32(color) == 0x0000FF);
    }

    SECTION("Invalid hex characters") {
        lv_color_t color = ui_theme_parse_color("#GGGGGG");
        // Invalid hex, should parse as 0
        REQUIRE(lv_color_to_u32(color) == 0x000000);
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("UI Theme: Color parsing edge cases", "[ui_theme][color][edge]") {
    SECTION("All zeros") {
        lv_color_t color = ui_theme_parse_color("#000000");
        REQUIRE(lv_color_to_u32(color) == 0x000000);
    }

    SECTION("All ones") {
        lv_color_t color = ui_theme_parse_color("#111111");
        REQUIRE(lv_color_to_u32(color) == 0x111111);
    }

    SECTION("All Fs") {
        lv_color_t color = ui_theme_parse_color("#FFFFFF");
        REQUIRE(lv_color_to_u32(color) == 0xFFFFFF);
    }

    SECTION("Leading zeros") {
        lv_color_t color = ui_theme_parse_color("#000001");
        REQUIRE(lv_color_to_u32(color) == 0x000001);
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

    REQUIRE(lv_color_to_u32(color1) == lv_color_to_u32(color2));
    REQUIRE(lv_color_to_u32(color2) == lv_color_to_u32(color3));
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

    REQUIRE(lv_color_to_u32(color1) == lv_color_to_u32(color2));
    REQUIRE(lv_color_to_u32(color1) != lv_color_to_u32(color3));
}

// ============================================================================
// Real-world Color Examples
// ============================================================================

TEST_CASE("UI Theme: Parse colors from globals.xml", "[ui_theme][color][integration]") {
    // These are typical colors that might appear in globals.xml

    SECTION("Primary colors") {
        lv_color_t primary_light = ui_theme_parse_color("#2196F3");
        lv_color_t primary_dark = ui_theme_parse_color("#1976D2");

        REQUIRE(lv_color_to_u32(primary_light) == 0x2196F3);
        REQUIRE(lv_color_to_u32(primary_dark) == 0x1976D2);
    }

    SECTION("Background colors") {
        lv_color_t bg_light = ui_theme_parse_color("#FFFFFF");
        lv_color_t bg_dark = ui_theme_parse_color("#121212");

        REQUIRE(lv_color_to_u32(bg_light) == 0xFFFFFF);
        REQUIRE(lv_color_to_u32(bg_dark) == 0x121212);
    }

    SECTION("Text colors") {
        lv_color_t text_light = ui_theme_parse_color("#000000");
        lv_color_t text_dark = ui_theme_parse_color("#FFFFFF");

        REQUIRE(lv_color_to_u32(text_light) == 0x000000);
        REQUIRE(lv_color_to_u32(text_dark) == 0xFFFFFF);
    }

    SECTION("State colors") {
        lv_color_t success = ui_theme_parse_color("#4CAF50");
        lv_color_t warning = ui_theme_parse_color("#FF9800");
        lv_color_t error = ui_theme_parse_color("#F44336");

        REQUIRE(lv_color_to_u32(success) == 0x4CAF50);
        REQUIRE(lv_color_to_u32(warning) == 0xFF9800);
        REQUIRE(lv_color_to_u32(error) == 0xF44336);
    }
}
