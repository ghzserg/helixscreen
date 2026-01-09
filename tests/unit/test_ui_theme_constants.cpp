// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_theme.h"

#include "../lvgl_test_fixture.h"

#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "../catch_amalgamated.hpp"

namespace fs = std::filesystem;

/**
 * @brief Test fixture for ui_theme constant registration tests
 *
 * Creates a temporary directory for XML files that gets cleaned up after each test.
 * Tests can write XML files and verify the parsing functions return expected results.
 */
class ThemeConstantsFixture : public LVGLTestFixture {
  protected:
    fs::path temp_dir;

    void setup_temp_xml_dir() {
        // Use PID in path to avoid race conditions when running parallel test shards
        temp_dir =
            fs::temp_directory_path() / ("test_ui_theme_constants_" + std::to_string(getpid()));
        fs::remove_all(temp_dir);
        fs::create_directories(temp_dir);
    }

    void cleanup_temp_dir() {
        if (!temp_dir.empty()) {
            fs::remove_all(temp_dir);
        }
    }

    void write_xml(const std::string& filename, const std::string& content) {
        std::ofstream out(temp_dir / filename);
        out << content;
    }

    // Helper to check if a key exists in a map
    template <typename MapType> bool has_key(const MapType& map, const std::string& key) {
        return map.find(key) != map.end();
    }
};

// ============================================================================
// Static Color Registration Tests
// ============================================================================

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: static color with no variants is registered",
                 "[ui_theme][constants][color]") {
    setup_temp_xml_dir();

    write_xml("test.xml", R"(
<component>
    <consts>
        <color name="test_color" value="#FF0000"/>
    </consts>
</component>
)");

    auto result = ui_theme_parse_all_xml_for_element(temp_dir.string().c_str(), "color");

    REQUIRE(has_key(result, "test_color"));
    REQUIRE(result["test_color"] == "#FF0000");

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: multiple static colors are registered",
                 "[ui_theme][constants][color]") {
    setup_temp_xml_dir();

    write_xml("globals.xml", R"(
<component>
    <consts>
        <color name="primary_color" value="#3B82F6"/>
        <color name="secondary_color" value="#10B981"/>
        <color name="warning_color" value="#F59E0B"/>
    </consts>
</component>
)");

    auto result = ui_theme_parse_all_xml_for_element(temp_dir.string().c_str(), "color");

    REQUIRE(result.size() == 3);
    REQUIRE(result["primary_color"] == "#3B82F6");
    REQUIRE(result["secondary_color"] == "#10B981");
    REQUIRE(result["warning_color"] == "#F59E0B");

    cleanup_temp_dir();
}

// ============================================================================
// Static Px Registration Tests
// ============================================================================

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: static px with no variants is registered",
                 "[ui_theme][constants][px]") {
    setup_temp_xml_dir();

    write_xml("test.xml", R"(
<component>
    <consts>
        <px name="test_size" value="42"/>
    </consts>
</component>
)");

    auto result = ui_theme_parse_all_xml_for_element(temp_dir.string().c_str(), "px");

    REQUIRE(has_key(result, "test_size"));
    REQUIRE(result["test_size"] == "42");

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: multiple static px values are registered",
                 "[ui_theme][constants][px]") {
    setup_temp_xml_dir();

    write_xml("globals.xml", R"(
<component>
    <consts>
        <px name="border_radius" value="8"/>
        <px name="icon_size" value="24"/>
        <px name="button_height" value="40"/>
    </consts>
</component>
)");

    auto result = ui_theme_parse_all_xml_for_element(temp_dir.string().c_str(), "px");

    REQUIRE(result.size() == 3);
    REQUIRE(result["border_radius"] == "8");
    REQUIRE(result["icon_size"] == "24");
    REQUIRE(result["button_height"] == "40");

    cleanup_temp_dir();
}

// ============================================================================
// Dynamic Color Suffix Tests
// ============================================================================

TEST_CASE_METHOD(ThemeConstantsFixture,
                 "ui_theme: dynamic color suffixes are included in all-element parse",
                 "[ui_theme][constants][color][dynamic]") {
    setup_temp_xml_dir();

    // Write XML with _light and _dark suffixes
    write_xml("theme.xml", R"(
<component>
    <consts>
        <color name="test_light" value="#FFF"/>
        <color name="test_dark" value="#000"/>
    </consts>
</component>
)");

    // ui_theme_parse_all_xml_for_element returns ALL elements
    // The static registration logic filters them out separately
    auto result = ui_theme_parse_all_xml_for_element(temp_dir.string().c_str(), "color");

    // Both should be present in the raw parse (filtering happens in registration)
    REQUIRE(has_key(result, "test_light"));
    REQUIRE(has_key(result, "test_dark"));
    REQUIRE(result["test_light"] == "#FFF");
    REQUIRE(result["test_dark"] == "#000");

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture,
                 "ui_theme: suffix parsing extracts base name for _light/_dark",
                 "[ui_theme][constants][color][suffix]") {
    setup_temp_xml_dir();

    write_xml("theme.xml", R"(
<component>
    <consts>
        <color name="app_bg_color_light" value="#FFFFFF"/>
        <color name="app_bg_color_dark" value="#1A1A1A"/>
        <color name="text_primary_light" value="#111111"/>
        <color name="text_primary_dark" value="#EEEEEE"/>
    </consts>
</component>
)");

    // Test suffix parsing - should extract base name
    auto light_result =
        ui_theme_parse_all_xml_for_suffix(temp_dir.string().c_str(), "color", "_light");
    auto dark_result =
        ui_theme_parse_all_xml_for_suffix(temp_dir.string().c_str(), "color", "_dark");

    // Base names should be extracted (suffix stripped)
    REQUIRE(has_key(light_result, "app_bg_color"));
    REQUIRE(has_key(light_result, "text_primary"));
    REQUIRE(light_result["app_bg_color"] == "#FFFFFF");
    REQUIRE(light_result["text_primary"] == "#111111");

    REQUIRE(has_key(dark_result, "app_bg_color"));
    REQUIRE(has_key(dark_result, "text_primary"));
    REQUIRE(dark_result["app_bg_color"] == "#1A1A1A");
    REQUIRE(dark_result["text_primary"] == "#EEEEEE");

    cleanup_temp_dir();
}

// ============================================================================
// Dynamic Px Suffix Tests
// ============================================================================

TEST_CASE_METHOD(ThemeConstantsFixture,
                 "ui_theme: dynamic px suffixes are included in all-element parse",
                 "[ui_theme][constants][px][dynamic]") {
    setup_temp_xml_dir();

    write_xml("responsive.xml", R"(
<component>
    <consts>
        <px name="size_small" value="10"/>
        <px name="size_medium" value="20"/>
        <px name="size_large" value="30"/>
    </consts>
</component>
)");

    auto result = ui_theme_parse_all_xml_for_element(temp_dir.string().c_str(), "px");

    // All should be present in raw parse
    REQUIRE(has_key(result, "size_small"));
    REQUIRE(has_key(result, "size_medium"));
    REQUIRE(has_key(result, "size_large"));

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture,
                 "ui_theme: suffix parsing extracts base name for _small/_medium/_large",
                 "[ui_theme][constants][px][suffix]") {
    setup_temp_xml_dir();

    write_xml("responsive.xml", R"(
<component>
    <consts>
        <px name="space_lg_small" value="12"/>
        <px name="space_lg_medium" value="16"/>
        <px name="space_lg_large" value="20"/>
    </consts>
</component>
)");

    auto small_result =
        ui_theme_parse_all_xml_for_suffix(temp_dir.string().c_str(), "px", "_small");
    auto medium_result =
        ui_theme_parse_all_xml_for_suffix(temp_dir.string().c_str(), "px", "_medium");
    auto large_result =
        ui_theme_parse_all_xml_for_suffix(temp_dir.string().c_str(), "px", "_large");

    REQUIRE(has_key(small_result, "space_lg"));
    REQUIRE(small_result["space_lg"] == "12");

    REQUIRE(has_key(medium_result, "space_lg"));
    REQUIRE(medium_result["space_lg"] == "16");

    REQUIRE(has_key(large_result, "space_lg"));
    REQUIRE(large_result["space_lg"] == "20");

    cleanup_temp_dir();
}

// ============================================================================
// Static and Dynamic Coexistence Tests
// ============================================================================

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: static and dynamic constants coexist correctly",
                 "[ui_theme][constants][coexist]") {
    setup_temp_xml_dir();

    // Mix of static (no suffix) and dynamic (with suffix) constants
    write_xml("mixed.xml", R"(
<component>
    <consts>
        <px name="radius" value="8"/>
        <px name="radius_small" value="4"/>
        <px name="radius_medium" value="6"/>
        <px name="radius_large" value="8"/>
    </consts>
</component>
)");

    auto all_result = ui_theme_parse_all_xml_for_element(temp_dir.string().c_str(), "px");

    // All four should be present in raw parse
    REQUIRE(all_result.size() == 4);
    REQUIRE(has_key(all_result, "radius"));
    REQUIRE(has_key(all_result, "radius_small"));
    REQUIRE(has_key(all_result, "radius_medium"));
    REQUIRE(has_key(all_result, "radius_large"));

    // Static value
    REQUIRE(all_result["radius"] == "8");

    // Dynamic values
    REQUIRE(all_result["radius_small"] == "4");
    REQUIRE(all_result["radius_medium"] == "6");
    REQUIRE(all_result["radius_large"] == "8");

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: suffix parsing does not match static constants",
                 "[ui_theme][constants][coexist][suffix]") {
    setup_temp_xml_dir();

    write_xml("mixed.xml", R"(
<component>
    <consts>
        <px name="radius" value="8"/>
        <px name="radius_small" value="4"/>
    </consts>
</component>
)");

    // Suffix parsing should only find radius_small, not radius
    auto small_result =
        ui_theme_parse_all_xml_for_suffix(temp_dir.string().c_str(), "px", "_small");

    // Should extract "radius" as base name from "radius_small"
    REQUIRE(small_result.size() == 1);
    REQUIRE(has_key(small_result, "radius"));
    REQUIRE(small_result["radius"] == "4");

    cleanup_temp_dir();
}

// ============================================================================
// Empty Directory Handling Tests
// ============================================================================

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: empty XML directory - graceful handling",
                 "[ui_theme][constants][edge]") {
    setup_temp_xml_dir();

    // Don't write any XML files - directory is empty

    auto color_result = ui_theme_parse_all_xml_for_element(temp_dir.string().c_str(), "color");
    auto px_result = ui_theme_parse_all_xml_for_element(temp_dir.string().c_str(), "px");

    // Should return empty maps, no crash
    REQUIRE(color_result.empty());
    REQUIRE(px_result.empty());

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: non-existent directory - graceful handling",
                 "[ui_theme][constants][edge]") {
    // Use a path that definitely doesn't exist
    std::string nonexistent = "/tmp/definitely_does_not_exist_12345";

    auto result = ui_theme_parse_all_xml_for_element(nonexistent.c_str(), "color");

    // Should return empty map, no crash
    REQUIRE(result.empty());
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: NULL directory - graceful handling",
                 "[ui_theme][constants][edge]") {
    auto result = ui_theme_parse_all_xml_for_element(nullptr, "color");

    // Should return empty map, no crash
    REQUIRE(result.empty());
}

// ============================================================================
// Malformed XML Handling Tests
// ============================================================================

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: malformed XML - graceful degradation",
                 "[ui_theme][constants][edge]") {
    setup_temp_xml_dir();

    // Write invalid XML
    write_xml("invalid.xml", R"(
<component>
    <consts>
        <color name="before_error" value="#111"/>
        <!-- Missing closing tag below -->
        <color name="broken" value="#222"
    </consts>
</component>
)");

    // Should not crash, may return partial results or empty
    REQUIRE_NOTHROW(ui_theme_parse_all_xml_for_element(temp_dir.string().c_str(), "color"));

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: malformed XML does not affect valid files",
                 "[ui_theme][constants][edge]") {
    setup_temp_xml_dir();

    // Write valid XML first (alphabetically first)
    write_xml("aaa_valid.xml", R"(
<component>
    <consts>
        <color name="valid_color" value="#123456"/>
    </consts>
</component>
)");

    // Write invalid XML second (alphabetically second)
    write_xml("zzz_invalid.xml", R"(
<component>
    <consts>
        <color name="broken" value
)");

    auto result = ui_theme_parse_all_xml_for_element(temp_dir.string().c_str(), "color");

    // Valid file should still be processed
    REQUIRE(has_key(result, "valid_color"));
    REQUIRE(result["valid_color"] == "#123456");

    cleanup_temp_dir();
}

// ============================================================================
// File Discovery Tests
// ============================================================================

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: find_xml_files returns sorted list",
                 "[ui_theme][constants][files]") {
    setup_temp_xml_dir();

    // Create files in non-alphabetical order
    write_xml("zebra.xml", "<component/>");
    write_xml("apple.xml", "<component/>");
    write_xml("mango.xml", "<component/>");

    auto files = ui_theme_find_xml_files(temp_dir.string().c_str());

    REQUIRE(files.size() == 3);

    // Should be sorted alphabetically
    REQUIRE(files[0].find("apple.xml") != std::string::npos);
    REQUIRE(files[1].find("mango.xml") != std::string::npos);
    REQUIRE(files[2].find("zebra.xml") != std::string::npos);

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: find_xml_files ignores non-xml files",
                 "[ui_theme][constants][files]") {
    setup_temp_xml_dir();

    write_xml("test.xml", "<component/>");

    // Create non-XML files
    std::ofstream(temp_dir / "readme.txt") << "test";
    std::ofstream(temp_dir / "data.json") << "{}";

    auto files = ui_theme_find_xml_files(temp_dir.string().c_str());

    REQUIRE(files.size() == 1);
    REQUIRE(files[0].find("test.xml") != std::string::npos);

    cleanup_temp_dir();
}

// ============================================================================
// Multi-File Override Tests
// ============================================================================

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: later files override earlier files (last-wins)",
                 "[ui_theme][constants][override]") {
    setup_temp_xml_dir();

    // globals.xml comes first alphabetically
    write_xml("globals.xml", R"(
<component>
    <consts>
        <px name="button_height" value="40"/>
    </consts>
</component>
)");

    // widget.xml comes later alphabetically
    write_xml("widget.xml", R"(
<component>
    <consts>
        <px name="button_height" value="48"/>
    </consts>
</component>
)");

    auto result = ui_theme_parse_all_xml_for_element(temp_dir.string().c_str(), "px");

    // widget.xml value should win (last alphabetically)
    REQUIRE(result["button_height"] == "48");

    cleanup_temp_dir();
}

// ============================================================================
// String Element Tests
// ============================================================================

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: string elements are parsed correctly",
                 "[ui_theme][constants][string]") {
    setup_temp_xml_dir();

    write_xml("fonts.xml", R"(
<component>
    <consts>
        <string name="font_body" value="noto_sans_18"/>
        <string name="font_heading" value="noto_sans_bold_24"/>
    </consts>
</component>
)");

    auto result = ui_theme_parse_all_xml_for_element(temp_dir.string().c_str(), "string");

    REQUIRE(result.size() == 2);
    REQUIRE(result["font_body"] == "noto_sans_18");
    REQUIRE(result["font_heading"] == "noto_sans_bold_24");

    cleanup_temp_dir();
}

// ============================================================================
// Constant Set Validation Tests
// ============================================================================

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: complete responsive px set passes validation",
                 "[ui_theme][validation][responsive]") {
    setup_temp_xml_dir();

    write_xml("responsive.xml", R"(
<component>
    <consts>
        <px name="button_height_small" value="32"/>
        <px name="button_height_medium" value="40"/>
        <px name="button_height_large" value="48"/>
    </consts>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.empty());

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture,
                 "ui_theme: incomplete responsive px set (missing _large) triggers warning",
                 "[ui_theme][validation][responsive]") {
    setup_temp_xml_dir();

    write_xml("responsive.xml", R"(
<component>
    <consts>
        <px name="button_height_small" value="32"/>
        <px name="button_height_medium" value="40"/>
    </consts>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find("button_height") != std::string::npos);
    REQUIRE(warnings[0].find("missing") != std::string::npos);
    REQUIRE(warnings[0].find("_large") != std::string::npos);

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture,
                 "ui_theme: incomplete responsive px set (missing _small) triggers warning",
                 "[ui_theme][validation][responsive]") {
    setup_temp_xml_dir();

    write_xml("responsive.xml", R"(
<component>
    <consts>
        <px name="button_height_medium" value="40"/>
        <px name="button_height_large" value="48"/>
    </consts>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find("button_height") != std::string::npos);
    REQUIRE(warnings[0].find("missing") != std::string::npos);
    REQUIRE(warnings[0].find("_small") != std::string::npos);

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture,
                 "ui_theme: static px (no responsive suffix) passes validation",
                 "[ui_theme][validation][responsive]") {
    setup_temp_xml_dir();

    // button_height_sm has no responsive suffix (_small/_medium/_large)
    // It's just a size variant name, not a breakpoint variant
    write_xml("static.xml", R"(
<component>
    <consts>
        <px name="button_height_sm" value="32"/>
        <px name="border_radius" value="8"/>
    </consts>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.empty());

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: complete theme color pair passes validation",
                 "[ui_theme][validation][theme]") {
    setup_temp_xml_dir();

    write_xml("theme.xml", R"(
<component>
    <consts>
        <color name="card_bg_light" value="#FFFFFF"/>
        <color name="card_bg_dark" value="#1A1A1A"/>
    </consts>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.empty());

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture,
                 "ui_theme: incomplete theme color pair (only _light) triggers warning",
                 "[ui_theme][validation][theme]") {
    setup_temp_xml_dir();

    write_xml("theme.xml", R"(
<component>
    <consts>
        <color name="card_bg_light" value="#FFFFFF"/>
    </consts>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find("card_bg") != std::string::npos);
    REQUIRE(warnings[0].find("missing") != std::string::npos);
    REQUIRE(warnings[0].find("_dark") != std::string::npos);

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture,
                 "ui_theme: incomplete theme color pair (only _dark) triggers warning",
                 "[ui_theme][validation][theme]") {
    setup_temp_xml_dir();

    write_xml("theme.xml", R"(
<component>
    <consts>
        <color name="card_bg_dark" value="#1A1A1A"/>
    </consts>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find("card_bg") != std::string::npos);
    REQUIRE(warnings[0].find("missing") != std::string::npos);
    REQUIRE(warnings[0].find("_light") != std::string::npos);

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: un-themed color (no suffix) passes validation",
                 "[ui_theme][validation][theme]") {
    setup_temp_xml_dir();

    // success_color has no _light/_dark suffix - it's the same in both themes
    write_xml("static.xml", R"(
<component>
    <consts>
        <color name="success_color" value="#10B981"/>
        <color name="warning_color" value="#F59E0B"/>
    </consts>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.empty());

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture,
                 "ui_theme: multiple incomplete sets produce multiple warnings",
                 "[ui_theme][validation]") {
    setup_temp_xml_dir();

    write_xml("mixed.xml", R"(
<component>
    <consts>
        <!-- Incomplete responsive px set (missing _large) -->
        <px name="button_height_small" value="32"/>
        <px name="button_height_medium" value="40"/>

        <!-- Incomplete color theme pair (missing _dark) -->
        <color name="card_bg_light" value="#FFFFFF"/>
    </consts>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.size() == 2);

    // Check that both issues are reported (order may vary)
    bool found_px_warning = false;
    bool found_color_warning = false;
    for (const auto& w : warnings) {
        if (w.find("button_height") != std::string::npos) {
            found_px_warning = true;
        }
        if (w.find("card_bg") != std::string::npos) {
            found_color_warning = true;
        }
    }
    REQUIRE(found_px_warning);
    REQUIRE(found_color_warning);

    cleanup_temp_dir();
}

// ============================================================================
// Undefined Constant Reference Tests
// ============================================================================

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: defined constant reference passes validation",
                 "[ui_theme][validation][undefined]") {
    setup_temp_xml_dir();

    // Define space_lg and use #space_lg in an attribute
    write_xml("test.xml", R"(
<component>
    <consts>
        <px name="space_lg" value="16"/>
    </consts>
    <view>
        <lv_obj style_pad_all="#space_lg"/>
    </view>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.empty());

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: undefined constant reference triggers warning",
                 "[ui_theme][validation][undefined]") {
    setup_temp_xml_dir();

    // Use #space_xxl but space_xxl is not defined
    write_xml("test.xml", R"(
<component>
    <consts>
        <px name="space_lg" value="16"/>
    </consts>
    <view>
        <lv_obj style_pad_all="#space_xxl"/>
    </view>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find("#space_xxl") != std::string::npos);
    REQUIRE(warnings[0].find("test.xml") != std::string::npos);
    REQUIRE(warnings[0].find("style_pad_all") != std::string::npos);

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture,
                 "ui_theme: responsive constant base name is valid reference",
                 "[ui_theme][validation][undefined]") {
    setup_temp_xml_dir();

    // Define space_lg_small, space_lg_medium, space_lg_large
    // Use #space_lg (the base name) - should be valid because responsive system registers it
    write_xml("test.xml", R"(
<component>
    <consts>
        <px name="space_lg_small" value="12"/>
        <px name="space_lg_medium" value="16"/>
        <px name="space_lg_large" value="20"/>
    </consts>
    <view>
        <lv_obj style_pad_all="#space_lg"/>
    </view>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.empty());

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: themed color base name is valid reference",
                 "[ui_theme][validation][undefined]") {
    setup_temp_xml_dir();

    // Define card_bg_light and card_bg_dark
    // Use #card_bg (the base name) - should be valid because theme system registers it
    write_xml("test.xml", R"(
<component>
    <consts>
        <color name="card_bg_light" value="#FFFFFF"/>
        <color name="card_bg_dark" value="#1A1A1A"/>
    </consts>
    <view>
        <lv_obj style_bg_color="#card_bg"/>
    </view>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.empty());

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: hex color values are not flagged as undefined",
                 "[ui_theme][validation][undefined]") {
    setup_temp_xml_dir();

    // Use style_bg_color="#FF0000" - this is a hex color, not a constant reference
    write_xml("test.xml", R"(
<component>
    <view>
        <lv_obj style_bg_color="#FF0000"/>
        <lv_obj style_bg_color="#ABC"/>
        <lv_obj style_bg_color="#12345678"/>
    </view>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.empty());

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: local constant in same file is valid",
                 "[ui_theme][validation][undefined]") {
    setup_temp_xml_dir();

    // Define local constant and use it in same file
    write_xml("component.xml", R"(
<component>
    <consts>
        <px name="my_local_padding" value="8"/>
        <color name="my_local_color" value="#123456"/>
    </consts>
    <view>
        <lv_obj style_pad_all="#my_local_padding" style_bg_color="#my_local_color"/>
    </view>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.empty());

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: constant defined in different file is valid",
                 "[ui_theme][validation][undefined]") {
    setup_temp_xml_dir();

    // Define constants in globals.xml
    write_xml("globals.xml", R"(
<component>
    <consts>
        <px name="shared_padding" value="16"/>
    </consts>
</component>
)");

    // Use in component.xml
    write_xml("component.xml", R"(
<component>
    <view>
        <lv_obj style_pad_all="#shared_padding"/>
    </view>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.empty());

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: string constant is valid reference",
                 "[ui_theme][validation][undefined]") {
    setup_temp_xml_dir();

    write_xml("test.xml", R"(
<component>
    <consts>
        <string name="font_body" value="noto_sans_18"/>
    </consts>
    <view>
        <lv_label style_text_font="#font_body"/>
    </view>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.empty());

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: str (icon glyph) constant is valid reference",
                 "[ui_theme][validation][undefined]") {
    setup_temp_xml_dir();

    // Icon glyphs are defined as <str> elements
    write_xml("test.xml", R"(
<component>
    <consts>
        <str name="icon_home" value="&#xF0001;"/>
    </consts>
    <view>
        <lv_label text="#icon_home"/>
    </view>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.empty());

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture, "ui_theme: percentage constant is valid reference",
                 "[ui_theme][validation][undefined]") {
    setup_temp_xml_dir();

    write_xml("test.xml", R"(
<component>
    <consts>
        <percentage name="card_width" value="45%"/>
    </consts>
    <view>
        <lv_obj width="#card_width"/>
    </view>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.empty());

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture,
                 "ui_theme: multiple undefined constants produce multiple warnings",
                 "[ui_theme][validation][undefined]") {
    setup_temp_xml_dir();

    write_xml("test.xml", R"(
<component>
    <view>
        <lv_obj style_pad_all="#undefined_padding" style_bg_color="#undefined_color"/>
    </view>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    // Find warnings for undefined constants
    bool found_padding = false;
    bool found_color = false;
    for (const auto& w : warnings) {
        if (w.find("#undefined_padding") != std::string::npos) {
            found_padding = true;
        }
        if (w.find("#undefined_color") != std::string::npos) {
            found_color = true;
        }
    }
    REQUIRE(found_padding);
    REQUIRE(found_color);

    cleanup_temp_dir();
}

TEST_CASE_METHOD(ThemeConstantsFixture,
                 "ui_theme: responsive string constant base name is valid reference",
                 "[ui_theme][validation][undefined]") {
    setup_temp_xml_dir();

    // Define font_body_small, font_body_medium, font_body_large
    // Use #font_body (the base name) - should be valid
    write_xml("test.xml", R"(
<component>
    <consts>
        <string name="font_body_small" value="noto_sans_14"/>
        <string name="font_body_medium" value="noto_sans_18"/>
        <string name="font_body_large" value="noto_sans_20"/>
    </consts>
    <view>
        <lv_label style_text_font="#font_body"/>
    </view>
</component>
)");

    auto warnings = ui_theme_validate_constant_sets(temp_dir.string().c_str());

    REQUIRE(warnings.empty());

    cleanup_temp_dir();
}
