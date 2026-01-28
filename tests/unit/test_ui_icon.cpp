// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ui_icon.cpp
 * @brief Unit tests for ui_icon.cpp - Icon widget with size, variant, and custom color support
 *
 * Tests cover:
 * - Size parsing (xs/sm/md/lg/xl) with valid and invalid values
 * - Variant parsing (primary/secondary/accent/disabled/success/warning/error/none)
 * - Public API functions (set_source, set_size, set_variant, set_color)
 * - Error handling (NULL pointers, invalid strings)
 *
 * Note: The implementation uses:
 * - IconSize enum (XS, SM, MD, LG, XL) - not a struct
 * - IconVariant enum (NONE, PRIMARY, SECONDARY, ACCENT, DISABLED, SUCCESS, WARNING, ERROR)
 * - Static internal functions (parse_size, parse_variant, apply_size, apply_variant)
 * - Public API uses the internal enums internally
 */

#include "../../include/ui_icon.h"
#include "../../include/ui_icon_codepoints.h"

#include <spdlog/spdlog.h>

#include "../catch_amalgamated.hpp"

// Test fixture for icon tests - manages spdlog level
class IconTest {
  public:
    IconTest() {
        spdlog::set_level(spdlog::level::debug);
    }
    ~IconTest() {
        spdlog::set_level(spdlog::level::warn);
    }
};

// ============================================================================
// Public API Tests - NULL pointer handling
// ============================================================================

TEST_CASE("ui_icon_set_source handles NULL icon", "[ui_icon][api][error]") {
    IconTest fixture;

    // Should log error and return without crashing
    REQUIRE_NOTHROW(ui_icon_set_source(nullptr, "home"));
}

TEST_CASE("ui_icon_set_source handles NULL icon_name", "[ui_icon][api][error]") {
    IconTest fixture;

    // Should log error and return without crashing
    // Note: Using dummy pointer - function should check for NULL before dereferencing
    REQUIRE_NOTHROW(ui_icon_set_source(reinterpret_cast<lv_obj_t*>(0x1), nullptr));
}

TEST_CASE("ui_icon_set_size handles NULL icon", "[ui_icon][api][error]") {
    IconTest fixture;

    REQUIRE_NOTHROW(ui_icon_set_size(nullptr, "md"));
}

TEST_CASE("ui_icon_set_size handles NULL size_str", "[ui_icon][api][error]") {
    IconTest fixture;

    REQUIRE_NOTHROW(ui_icon_set_size(reinterpret_cast<lv_obj_t*>(0x1), nullptr));
}

TEST_CASE("ui_icon_set_variant handles NULL icon", "[ui_icon][api][error]") {
    IconTest fixture;

    REQUIRE_NOTHROW(ui_icon_set_variant(nullptr, "primary"));
}

TEST_CASE("ui_icon_set_variant handles NULL variant_str", "[ui_icon][api][error]") {
    IconTest fixture;

    REQUIRE_NOTHROW(ui_icon_set_variant(reinterpret_cast<lv_obj_t*>(0x1), nullptr));
}

TEST_CASE("ui_icon_set_color handles NULL icon", "[ui_icon][api][error]") {
    IconTest fixture;

    lv_color_t color = lv_color_hex(0xFF0000);
    REQUIRE_NOTHROW(ui_icon_set_color(nullptr, color, LV_OPA_COVER));
}

// ============================================================================
// Icon Codepoint Lookup
// ============================================================================

TEST_CASE("Icon codepoint lookup returns valid codepoints", "[ui_icon][codepoint]") {
    IconTest fixture;

    // Test common icons
    const char* home = ui_icon::lookup_codepoint("home");
    REQUIRE(home != nullptr);

    const char* wifi = ui_icon::lookup_codepoint("wifi");
    REQUIRE(wifi != nullptr);

    const char* settings = ui_icon::lookup_codepoint("cog");
    REQUIRE(settings != nullptr);
}

TEST_CASE("Icon codepoint lookup returns nullptr for unknown icons", "[ui_icon][codepoint]") {
    IconTest fixture;

    const char* unknown = ui_icon::lookup_codepoint("nonexistent_icon_xyz");
    REQUIRE(unknown == nullptr);
}

TEST_CASE("Icon codepoint lookup handles NULL", "[ui_icon][codepoint][error]") {
    IconTest fixture;

    const char* result = ui_icon::lookup_codepoint(nullptr);
    REQUIRE(result == nullptr);
}

TEST_CASE("Icon codepoint lookup handles empty string", "[ui_icon][codepoint][error]") {
    IconTest fixture;

    const char* result = ui_icon::lookup_codepoint("");
    REQUIRE(result == nullptr);
}

// ============================================================================
// Legacy Prefix Stripping
// ============================================================================

TEST_CASE("strip_legacy_prefix removes mat_ prefix", "[ui_icon][legacy]") {
    IconTest fixture;

    const char* result = ui_icon::strip_legacy_prefix("mat_home");
    REQUIRE(strcmp(result, "home") == 0);
}

TEST_CASE("strip_legacy_prefix does NOT strip _img suffix without mat_ prefix",
          "[ui_icon][legacy]") {
    IconTest fixture;

    // The implementation ONLY handles names starting with "mat_"
    // A plain "_img" suffix without "mat_" prefix is NOT stripped
    const char* result = ui_icon::strip_legacy_prefix("home_img");
    REQUIRE(strcmp(result, "home_img") == 0); // Returns original, unchanged
}

TEST_CASE("strip_legacy_prefix removes both prefix and suffix", "[ui_icon][legacy]") {
    IconTest fixture;

    const char* result = ui_icon::strip_legacy_prefix("mat_wifi_img");
    REQUIRE(strcmp(result, "wifi") == 0);
}

TEST_CASE("strip_legacy_prefix returns original if no prefix/suffix", "[ui_icon][legacy]") {
    IconTest fixture;

    const char* result = ui_icon::strip_legacy_prefix("wifi");
    REQUIRE(strcmp(result, "wifi") == 0);
}

TEST_CASE("strip_legacy_prefix handles NULL", "[ui_icon][legacy][error]") {
    IconTest fixture;

    const char* result = ui_icon::strip_legacy_prefix(nullptr);
    REQUIRE(result == nullptr);
}

TEST_CASE("strip_legacy_prefix handles empty string", "[ui_icon][legacy][error]") {
    IconTest fixture;

    const char* result = ui_icon::strip_legacy_prefix("");
    REQUIRE(result != nullptr);
    REQUIRE(strlen(result) == 0);
}

// ============================================================================
// Reactive Icon Tests - Phase 2.2
// ============================================================================
// These tests verify that icon widgets update their color when the theme
// changes. The current implementation uses inline styles (lv_obj_set_style_text_color)
// which don't respond to theme changes.
//
// The fix (Phase 2.2 IMPL) will make ui_icon use lv_obj_add_style() with the
// shared icon styles from theme_core, which update in-place when
// theme_core_update_colors() is called.
// ============================================================================

#include "../lvgl_ui_test_fixture.h"
#include "theme_core.h"

// ============================================================================
// New Variant Name Tests
// ============================================================================
// Test that the new semantic variant names work correctly

TEST_CASE_METHOD(LVGLUITestFixture, "ui_icon: accepts 'text' variant name (new name for 'primary')",
                 "[reactive-icon][variant-names]") {
    // Create icon widget via XML with new variant name
    const char* attrs[] = {"src", "home", "variant", "text", nullptr};
    lv_obj_t* icon = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));
    REQUIRE(icon != nullptr);

    // Get the icon's text color - should match icon_text_style
    lv_color_t icon_color = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    // Get expected color from the shared style
    lv_style_t* text_style = theme_core_get_icon_text_style();
    REQUIRE(text_style != nullptr);
    lv_style_value_t value;
    lv_style_res_t res = lv_style_get_prop(text_style, LV_STYLE_TEXT_COLOR, &value);
    REQUIRE(res == LV_STYLE_RES_FOUND);

    // Icon should have the text style's color
    REQUIRE(lv_color_eq(icon_color, value.color));

    lv_obj_delete(icon);
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "ui_icon: accepts 'muted' variant name (new name for 'secondary')",
                 "[reactive-icon][variant-names]") {
    const char* attrs[] = {"src", "home", "variant", "muted", nullptr};
    lv_obj_t* icon = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));
    REQUIRE(icon != nullptr);

    lv_color_t icon_color = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    lv_style_t* muted_style = theme_core_get_icon_muted_style();
    REQUIRE(muted_style != nullptr);
    lv_style_value_t value;
    lv_style_res_t res = lv_style_get_prop(muted_style, LV_STYLE_TEXT_COLOR, &value);
    REQUIRE(res == LV_STYLE_RES_FOUND);

    REQUIRE(lv_color_eq(icon_color, value.color));

    lv_obj_delete(icon);
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "ui_icon: accepts 'primary' variant name (new name for 'accent')",
                 "[reactive-icon][variant-names]") {
    // NOTE: "primary" now means "accent/brand color" (was "text color")
    const char* attrs[] = {"src", "home", "variant", "primary", nullptr};
    lv_obj_t* icon = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));
    REQUIRE(icon != nullptr);

    lv_color_t icon_color = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    lv_style_t* primary_style = theme_core_get_icon_primary_style();
    REQUIRE(primary_style != nullptr);
    lv_style_value_t value;
    lv_style_res_t res = lv_style_get_prop(primary_style, LV_STYLE_TEXT_COLOR, &value);
    REQUIRE(res == LV_STYLE_RES_FOUND);

    REQUIRE(lv_color_eq(icon_color, value.color));

    lv_obj_delete(icon);
}

TEST_CASE_METHOD(LVGLUITestFixture, "ui_icon: accepts 'danger' variant name (new name for 'error')",
                 "[reactive-icon][variant-names]") {
    const char* attrs[] = {"src", "home", "variant", "danger", nullptr};
    lv_obj_t* icon = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));
    REQUIRE(icon != nullptr);

    lv_color_t icon_color = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    lv_style_t* danger_style = theme_core_get_icon_danger_style();
    REQUIRE(danger_style != nullptr);
    lv_style_value_t value;
    lv_style_res_t res = lv_style_get_prop(danger_style, LV_STYLE_TEXT_COLOR, &value);
    REQUIRE(res == LV_STYLE_RES_FOUND);

    REQUIRE(lv_color_eq(icon_color, value.color));

    lv_obj_delete(icon);
}

// ============================================================================
// Secondary Variant Test
// ============================================================================
// Test that secondary variant uses secondary accent color style

TEST_CASE_METHOD(LVGLUITestFixture, "ui_icon: accepts 'secondary' variant for secondary accent",
                 "[reactive-icon][variant-names]") {
    const char* attrs[] = {"src", "home", "variant", "secondary", nullptr};
    lv_obj_t* icon = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));
    REQUIRE(icon != nullptr);

    lv_color_t icon_color = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    lv_style_t* secondary_style = theme_core_get_icon_secondary_style();
    REQUIRE(secondary_style != nullptr);
    lv_style_value_t value;
    lv_style_res_t res = lv_style_get_prop(secondary_style, LV_STYLE_TEXT_COLOR, &value);
    REQUIRE(res == LV_STYLE_RES_FOUND);

    REQUIRE(lv_color_eq(icon_color, value.color));

    lv_obj_delete(icon);
}

// ============================================================================
// Reactive Theme Change Tests - CRITICAL
// ============================================================================
// These tests verify icons update when the theme changes

TEST_CASE_METHOD(LVGLUITestFixture, "ui_icon: text variant color updates on theme change",
                 "[reactive-icon][reactive]") {
    // Create icon with text variant
    const char* attrs[] = {"src", "home", "variant", "text", nullptr};
    lv_obj_t* icon = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));
    REQUIRE(icon != nullptr);

    // Get initial color
    lv_color_t before = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    uint32_t before_rgb = lv_color_to_u32(before) & 0x00FFFFFF;
    INFO("Initial icon text color: 0x" << std::hex << before_rgb);

    // Update theme colors to dark mode (significantly different colors)
    lv_color_t dark_screen_bg = lv_color_hex(0x121212);
    lv_color_t dark_card_bg = lv_color_hex(0x1E1E1E);
    lv_color_t dark_surface = lv_color_hex(0x2D2D2D);
    lv_color_t dark_text = lv_color_hex(0xE0E0E0); // Light text for dark mode
    lv_color_t dark_text_muted = lv_color_hex(0xA0A0A0);
    lv_color_t dark_text_subtle = lv_color_hex(0x808080);
    lv_color_t dark_focus = lv_color_hex(0x4FC3F7);
    lv_color_t dark_primary = lv_color_hex(0x2196F3);
    lv_color_t dark_border = lv_color_hex(0x424242);

    theme_core_update_colors(true, // is_dark
                             dark_screen_bg, dark_card_bg, dark_surface, dark_text, dark_text_muted,
                             dark_text_subtle, dark_focus, dark_primary, dark_border);

    // Force LVGL style refresh cascade
    lv_obj_report_style_change(nullptr);

    // Get updated color
    lv_color_t after = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    uint32_t after_rgb = lv_color_to_u32(after) & 0x00FFFFFF;
    INFO("After theme change icon text color: 0x" << std::hex << after_rgb);

    // Icon color should change (light mode dark text -> dark mode light text)
    // This will FAIL with current inline style implementation
    REQUIRE_FALSE(lv_color_eq(before, after));

    lv_obj_delete(icon);
}

TEST_CASE_METHOD(LVGLUITestFixture, "ui_icon: muted variant color updates on theme change",
                 "[reactive-icon][reactive]") {
    const char* attrs[] = {"src", "home", "variant", "muted", nullptr};
    lv_obj_t* icon = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));
    REQUIRE(icon != nullptr);

    lv_color_t before = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    // Update to dark mode
    lv_color_t dark_screen_bg = lv_color_hex(0x121212);
    lv_color_t dark_card_bg = lv_color_hex(0x1E1E1E);
    lv_color_t dark_surface = lv_color_hex(0x2D2D2D);
    lv_color_t dark_text = lv_color_hex(0xE0E0E0);
    lv_color_t dark_text_muted = lv_color_hex(0xA0A0A0);
    lv_color_t dark_text_subtle = lv_color_hex(0x808080);
    lv_color_t dark_focus = lv_color_hex(0x4FC3F7);
    lv_color_t dark_primary = lv_color_hex(0x2196F3);
    lv_color_t dark_border = lv_color_hex(0x424242);

    theme_core_update_colors(true, dark_screen_bg, dark_card_bg, dark_surface, dark_text,
                             dark_text_muted, dark_text_subtle, dark_focus, dark_primary,
                             dark_border);

    lv_obj_report_style_change(nullptr);

    lv_color_t after = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    REQUIRE_FALSE(lv_color_eq(before, after));

    lv_obj_delete(icon);
}

TEST_CASE_METHOD(LVGLUITestFixture, "ui_icon: primary variant color updates on theme change",
                 "[reactive-icon][reactive]") {
    const char* attrs[] = {"src", "home", "variant", "primary", nullptr};
    lv_obj_t* icon = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));
    REQUIRE(icon != nullptr);

    lv_color_t before = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    // Update to dark mode with DIFFERENT primary color
    lv_color_t dark_screen_bg = lv_color_hex(0x121212);
    lv_color_t dark_card_bg = lv_color_hex(0x1E1E1E);
    lv_color_t dark_surface = lv_color_hex(0x2D2D2D);
    lv_color_t dark_text = lv_color_hex(0xE0E0E0);
    lv_color_t dark_text_muted = lv_color_hex(0xA0A0A0);
    lv_color_t dark_text_subtle = lv_color_hex(0x808080);
    lv_color_t dark_focus = lv_color_hex(0x4FC3F7);
    lv_color_t dark_primary = lv_color_hex(0xFF5722); // Different primary color
    lv_color_t dark_border = lv_color_hex(0x424242);

    theme_core_update_colors(true, dark_screen_bg, dark_card_bg, dark_surface, dark_text,
                             dark_text_muted, dark_text_subtle, dark_focus, dark_primary,
                             dark_border);

    lv_obj_report_style_change(nullptr);

    lv_color_t after = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    // Primary color changed, so icon should too
    REQUIRE_FALSE(lv_color_eq(before, after));

    lv_obj_delete(icon);
}

TEST_CASE_METHOD(LVGLUITestFixture, "ui_icon: success variant color matches shared style",
                 "[reactive-icon][semantic]") {
    const char* attrs[] = {"src", "home", "variant", "success", nullptr};
    lv_obj_t* icon = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));
    REQUIRE(icon != nullptr);

    lv_color_t icon_color = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    lv_style_t* success_style = theme_core_get_icon_success_style();
    REQUIRE(success_style != nullptr);
    lv_style_value_t value;
    lv_style_res_t res = lv_style_get_prop(success_style, LV_STYLE_TEXT_COLOR, &value);
    REQUIRE(res == LV_STYLE_RES_FOUND);

    REQUIRE(lv_color_eq(icon_color, value.color));

    lv_obj_delete(icon);
}

TEST_CASE_METHOD(LVGLUITestFixture, "ui_icon: warning variant color matches shared style",
                 "[reactive-icon][semantic]") {
    const char* attrs[] = {"src", "home", "variant", "warning", nullptr};
    lv_obj_t* icon = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));
    REQUIRE(icon != nullptr);

    lv_color_t icon_color = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    lv_style_t* warning_style = theme_core_get_icon_warning_style();
    REQUIRE(warning_style != nullptr);
    lv_style_value_t value;
    lv_style_res_t res = lv_style_get_prop(warning_style, LV_STYLE_TEXT_COLOR, &value);
    REQUIRE(res == LV_STYLE_RES_FOUND);

    REQUIRE(lv_color_eq(icon_color, value.color));

    lv_obj_delete(icon);
}

TEST_CASE_METHOD(LVGLUITestFixture, "ui_icon: danger variant color matches shared style",
                 "[reactive-icon][semantic]") {
    const char* attrs[] = {"src", "home", "variant", "danger", nullptr};
    lv_obj_t* icon = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));
    REQUIRE(icon != nullptr);

    lv_color_t icon_color = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    lv_style_t* danger_style = theme_core_get_icon_danger_style();
    REQUIRE(danger_style != nullptr);
    lv_style_value_t value;
    lv_style_res_t res = lv_style_get_prop(danger_style, LV_STYLE_TEXT_COLOR, &value);
    REQUIRE(res == LV_STYLE_RES_FOUND);

    REQUIRE(lv_color_eq(icon_color, value.color));

    lv_obj_delete(icon);
}

TEST_CASE_METHOD(LVGLUITestFixture, "ui_icon: none variant uses text style",
                 "[reactive-icon][semantic]") {
    const char* attrs[] = {"src", "home", "variant", "none", nullptr};
    lv_obj_t* icon = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));
    REQUIRE(icon != nullptr);

    lv_color_t icon_color = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    // none variant should use text style (same as default)
    lv_style_t* text_style = theme_core_get_icon_text_style();
    REQUIRE(text_style != nullptr);
    lv_style_value_t value;
    lv_style_res_t res = lv_style_get_prop(text_style, LV_STYLE_TEXT_COLOR, &value);
    REQUIRE(res == LV_STYLE_RES_FOUND);

    REQUIRE(lv_color_eq(icon_color, value.color));

    lv_obj_delete(icon);
}

// ============================================================================
// Multiple Icons Update Together Tests
// ============================================================================

TEST_CASE_METHOD(LVGLUITestFixture, "ui_icon: multiple icons update together on theme change",
                 "[reactive-icon][reactive]") {
    // Create multiple icons with same variant
    const char* attrs[] = {"src", "home", "variant", "text", nullptr};
    lv_obj_t* icon1 = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));
    lv_obj_t* icon2 = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));
    lv_obj_t* icon3 = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));

    REQUIRE(icon1 != nullptr);
    REQUIRE(icon2 != nullptr);
    REQUIRE(icon3 != nullptr);

    // Get initial colors
    lv_color_t before1 = lv_obj_get_style_text_color(icon1, LV_PART_MAIN);
    lv_color_t before2 = lv_obj_get_style_text_color(icon2, LV_PART_MAIN);
    lv_color_t before3 = lv_obj_get_style_text_color(icon3, LV_PART_MAIN);

    // All icons should have the same initial color
    REQUIRE(lv_color_eq(before1, before2));
    REQUIRE(lv_color_eq(before2, before3));

    // Update to dark mode
    lv_color_t dark_screen_bg = lv_color_hex(0x121212);
    lv_color_t dark_card_bg = lv_color_hex(0x1E1E1E);
    lv_color_t dark_surface = lv_color_hex(0x2D2D2D);
    lv_color_t dark_text = lv_color_hex(0xE0E0E0);
    lv_color_t dark_text_muted = lv_color_hex(0xA0A0A0);
    lv_color_t dark_text_subtle = lv_color_hex(0x808080);
    lv_color_t dark_focus = lv_color_hex(0x4FC3F7);
    lv_color_t dark_primary = lv_color_hex(0x2196F3);
    lv_color_t dark_border = lv_color_hex(0x424242);

    theme_core_update_colors(true, dark_screen_bg, dark_card_bg, dark_surface, dark_text,
                             dark_text_muted, dark_text_subtle, dark_focus, dark_primary,
                             dark_border);

    lv_obj_report_style_change(nullptr);

    // Get colors after theme change
    lv_color_t after1 = lv_obj_get_style_text_color(icon1, LV_PART_MAIN);
    lv_color_t after2 = lv_obj_get_style_text_color(icon2, LV_PART_MAIN);
    lv_color_t after3 = lv_obj_get_style_text_color(icon3, LV_PART_MAIN);

    // All icons should still have the same color (consistency)
    REQUIRE(lv_color_eq(after1, after2));
    REQUIRE(lv_color_eq(after2, after3));

    // And the color should have changed from before (reactivity)
    // This will FAIL with current inline style implementation
    REQUIRE_FALSE(lv_color_eq(before1, after1));

    lv_obj_delete(icon1);
    lv_obj_delete(icon2);
    lv_obj_delete(icon3);
}

TEST_CASE_METHOD(LVGLUITestFixture, "ui_icon: style matches shared style after theme change",
                 "[reactive-icon][reactive]") {
    const char* attrs[] = {"src", "home", "variant", "text", nullptr};
    lv_obj_t* icon = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));
    REQUIRE(icon != nullptr);

    // Get the shared icon text style
    lv_style_t* shared_style = theme_core_get_icon_text_style();
    REQUIRE(shared_style != nullptr);

    // Update to dark mode
    lv_color_t dark_screen_bg = lv_color_hex(0x121212);
    lv_color_t dark_card_bg = lv_color_hex(0x1E1E1E);
    lv_color_t dark_surface = lv_color_hex(0x2D2D2D);
    lv_color_t dark_text = lv_color_hex(0xE0E0E0);
    lv_color_t dark_text_muted = lv_color_hex(0xA0A0A0);
    lv_color_t dark_text_subtle = lv_color_hex(0x808080);
    lv_color_t dark_focus = lv_color_hex(0x4FC3F7);
    lv_color_t dark_primary = lv_color_hex(0x2196F3);
    lv_color_t dark_border = lv_color_hex(0x424242);

    theme_core_update_colors(true, dark_screen_bg, dark_card_bg, dark_surface, dark_text,
                             dark_text_muted, dark_text_subtle, dark_focus, dark_primary,
                             dark_border);

    lv_obj_report_style_change(nullptr);

    // Get the updated color from the shared style
    lv_style_value_t style_value;
    lv_style_res_t res = lv_style_get_prop(shared_style, LV_STYLE_TEXT_COLOR, &style_value);
    REQUIRE(res == LV_STYLE_RES_FOUND);

    // Get the actual color from the icon widget
    lv_color_t icon_color = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    // Log for debugging
    uint32_t style_rgb = lv_color_to_u32(style_value.color) & 0x00FFFFFF;
    uint32_t icon_rgb = lv_color_to_u32(icon_color) & 0x00FFFFFF;
    INFO("Shared style text_color: 0x" << std::hex << style_rgb);
    INFO("Icon actual text_color: 0x" << std::hex << icon_rgb);

    // The icon should have the same color as the shared style after update
    // This will FAIL until ui_icon uses lv_obj_add_style() with the shared style
    REQUIRE(lv_color_eq(icon_color, style_value.color));

    lv_obj_delete(icon);
}

// ============================================================================
// API Tests - ui_icon_set_variant with new names
// ============================================================================

TEST_CASE_METHOD(LVGLUITestFixture, "ui_icon_set_variant: accepts new variant names",
                 "[reactive-icon][api]") {
    // Create a plain icon
    const char* attrs[] = {"src", "home", nullptr};
    lv_obj_t* icon = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "icon", attrs));
    REQUIRE(icon != nullptr);

    // Set to 'text' variant using API
    ui_icon_set_variant(icon, "text");
    lv_color_t text_color = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    // Should match shared text style
    lv_style_t* text_style = theme_core_get_icon_text_style();
    lv_style_value_t value;
    lv_style_get_prop(text_style, LV_STYLE_TEXT_COLOR, &value);
    REQUIRE(lv_color_eq(text_color, value.color));

    // Set to 'muted' variant using API
    ui_icon_set_variant(icon, "muted");
    lv_color_t muted_color = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    // Should match shared muted style
    lv_style_t* muted_style = theme_core_get_icon_muted_style();
    lv_style_get_prop(muted_style, LV_STYLE_TEXT_COLOR, &value);
    REQUIRE(lv_color_eq(muted_color, value.color));

    // Set to 'danger' variant using API
    ui_icon_set_variant(icon, "danger");
    lv_color_t danger_color = lv_obj_get_style_text_color(icon, LV_PART_MAIN);

    // Should match shared danger style
    lv_style_t* danger_style = theme_core_get_icon_danger_style();
    lv_style_get_prop(danger_style, LV_STYLE_TEXT_COLOR, &value);
    REQUIRE(lv_color_eq(danger_color, value.color));

    lv_obj_delete(icon);
}
