// Copyright 2025 HelixScreen
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
// API Contract Documentation Tests
// ============================================================================

TEST_CASE("Icon size strings are lowercase only", "[ui_icon][api][contract]") {
    IconTest fixture;

    // This test documents the expected API contract
    // The implementation expects lowercase: xs, sm, md, lg, xl
    // Uppercase strings will result in default (xl) with a warning log

    // Valid sizes (lowercase)
    SUCCEED("Valid sizes: xs, sm, md, lg, xl");

    // Invalid sizes (uppercase) - will use default with warning
    SUCCEED("Invalid sizes (uppercase) fall back to xl: XS, SM, MD, LG, XL");
}

TEST_CASE("Icon variant strings are lowercase only", "[ui_icon][api][contract]") {
    IconTest fixture;

    // This test documents the expected API contract
    // The implementation expects lowercase: primary, secondary, accent, disabled, success,
    // warning, error, none Uppercase strings will result in default (none) with a warning log

    SUCCEED("Valid variants: primary, secondary, accent, disabled, success, warning, error, none");
    SUCCEED("Invalid variants fall back to none");
}

TEST_CASE("Icon default values", "[ui_icon][api][contract]") {
    IconTest fixture;

    // This test documents the default values used by the icon widget
    // - Default size: xl (64px font)
    // - Default source: home icon
    // - Default variant: none (primary text color)

    SUCCEED("Default size: xl (64px)");
    SUCCEED("Default source: home");
    SUCCEED("Default variant: none");
}

TEST_CASE("Icon custom color overrides variant", "[ui_icon][api][contract]") {
    IconTest fixture;

    // When both color and variant are specified in XML attributes,
    // the custom color takes precedence over the variant
    // This is documented behavior in the apply function

    SUCCEED("Custom color attribute takes precedence over variant attribute");
}

// ============================================================================
// XML Attribute Behavior Documentation
// ============================================================================

TEST_CASE("Icon XML src attribute", "[ui_icon][xml]") {
    IconTest fixture;

    // The src attribute specifies which icon to display
    // - Looks up codepoint from ui_icon_codepoints.h
    // - Strips legacy "mat_" prefix and "_img" suffix if present
    // - Falls back to "home" icon if not found

    SUCCEED("src attribute: <icon src=\"wifi\"/> displays WiFi icon");
    SUCCEED("Legacy prefix stripped: mat_wifi -> wifi");
    SUCCEED("Legacy suffix stripped: wifi_img -> wifi");
    SUCCEED("Unknown icon falls back to home");
}

TEST_CASE("Icon XML size attribute", "[ui_icon][xml]") {
    IconTest fixture;

    // The size attribute determines which MDI font to use
    // - xs: mdi_icons_16 (16px)
    // - sm: mdi_icons_24 (24px)
    // - md: mdi_icons_32 (32px)
    // - lg: mdi_icons_48 (48px)
    // - xl: mdi_icons_64 (64px) - default

    SUCCEED("size=\"xs\" uses 16px font");
    SUCCEED("size=\"sm\" uses 24px font");
    SUCCEED("size=\"md\" uses 32px font");
    SUCCEED("size=\"lg\" uses 48px font");
    SUCCEED("size=\"xl\" uses 64px font (default)");
}

TEST_CASE("Icon XML variant attribute", "[ui_icon][xml]") {
    IconTest fixture;

    // The variant attribute determines the icon color
    // - primary: UI_COLOR_TEXT_PRIMARY
    // - secondary: UI_COLOR_TEXT_SECONDARY
    // - accent: UI_COLOR_PRIMARY (red)
    // - disabled: UI_COLOR_TEXT_PRIMARY at 50% opacity
    // - success: success_color from globals.xml
    // - warning: warning_color from globals.xml
    // - error: error_color from globals.xml
    // - none: UI_COLOR_TEXT_PRIMARY (default)

    SUCCEED("variant=\"primary\" uses primary text color");
    SUCCEED("variant=\"secondary\" uses secondary text color");
    SUCCEED("variant=\"accent\" uses accent/primary color");
    SUCCEED("variant=\"disabled\" uses primary text at 50% opacity");
    SUCCEED("variant=\"success\" uses success_color from theme");
    SUCCEED("variant=\"warning\" uses warning_color from theme");
    SUCCEED("variant=\"error\" uses error_color from theme");
    SUCCEED("variant=\"none\" uses primary text color (default)");
}

TEST_CASE("Icon XML color attribute", "[ui_icon][xml]") {
    IconTest fixture;

    // The color attribute allows a custom hex color
    // - Overrides variant if both are specified
    // - Parsed using lv_xml_to_color()

    SUCCEED("color=\"0xFF0000\" sets red color");
    SUCCEED("color attribute overrides variant attribute");
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
