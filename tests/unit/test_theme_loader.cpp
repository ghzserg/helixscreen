// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "theme_loader.h"

#include <cstdio>

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE("ThemePalette index access", "[theme]") {
    ThemePalette palette;
    palette.bg_darkest = "#2e3440";
    palette.status_special = "#b48ead";

    REQUIRE(palette.at(0) == "#2e3440");
    REQUIRE(palette.at(15) == "#b48ead");
}

TEST_CASE("ThemePalette color_names returns all 16 names", "[theme]") {
    auto& names = ThemePalette::color_names();
    REQUIRE(names.size() == 16);
    REQUIRE(std::string(names[0]) == "bg_darkest");
    REQUIRE(std::string(names[15]) == "status_special");
}

TEST_CASE("ThemeData::is_valid checks colors and name", "[theme]") {
    ThemeData theme;
    theme.name = "Test";

    // Set all 16 colors to valid hex
    for (size_t i = 0; i < 16; ++i) {
        theme.colors.at(i) = "#aabbcc";
    }

    REQUIRE(theme.is_valid());

    // Empty name should be invalid
    theme.name = "";
    REQUIRE_FALSE(theme.is_valid());
    theme.name = "Test";

    // Invalid color format should fail
    theme.colors.bg_darkest = "invalid";
    REQUIRE_FALSE(theme.is_valid());

    // Short hex should fail
    theme.colors.bg_darkest = "#abc";
    REQUIRE_FALSE(theme.is_valid());
}

TEST_CASE("ThemePalette::at throws on invalid index", "[theme]") {
    ThemePalette palette;
    REQUIRE_THROWS_AS(palette.at(16), std::out_of_range);
    REQUIRE_THROWS_AS(palette.at(100), std::out_of_range);
}

TEST_CASE("parse_theme_json parses valid theme", "[theme]") {
    const char* json = R"({
        "name": "Test Theme",
        "colors": {
            "bg_darkest": "#2e3440",
            "bg_dark": "#3b4252",
            "bg_dark_highlight": "#434c5e",
            "border_muted": "#4c566a",
            "text_light": "#d8dee9",
            "bg_light": "#e5e9f0",
            "bg_lightest": "#eceff4",
            "accent_highlight": "#8fbcbb",
            "accent_primary": "#88c0d0",
            "accent_secondary": "#81a1c1",
            "accent_tertiary": "#5e81ac",
            "status_error": "#bf616a",
            "status_danger": "#d08770",
            "status_warning": "#ebcb8b",
            "status_success": "#a3be8c",
            "status_special": "#b48ead"
        },
        "border_radius": 8,
        "border_width": 2,
        "border_opacity": 50,
        "shadow_intensity": 10
    })";

    auto theme = helix::parse_theme_json(json, "test.json");

    REQUIRE(theme.name == "Test Theme");
    REQUIRE(theme.colors.bg_darkest == "#2e3440");
    REQUIRE(theme.colors.status_special == "#b48ead");
    REQUIRE(theme.properties.border_radius == 8);
    REQUIRE(theme.properties.shadow_intensity == 10);
    REQUIRE(theme.is_valid());
}

TEST_CASE("get_default_nord_theme returns valid theme", "[theme]") {
    auto theme = helix::get_default_nord_theme();

    REQUIRE(theme.name == "Nord");
    REQUIRE(theme.is_valid());
    REQUIRE(theme.colors.bg_darkest == "#2e3440");
}

TEST_CASE("parse_theme_json falls back to Nord for missing colors", "[theme]") {
    // JSON with only 2 colors - rest should fall back to Nord
    const char* json = R"({
        "name": "Partial Theme",
        "colors": {
            "bg_darkest": "#111111",
            "status_special": "#222222"
        }
    })";

    auto theme = helix::parse_theme_json(json, "partial.json");

    REQUIRE(theme.name == "Partial Theme");
    REQUIRE(theme.colors.bg_darkest == "#111111");     // From JSON
    REQUIRE(theme.colors.status_special == "#222222"); // From JSON
    REQUIRE(theme.colors.bg_dark == "#3b4252");        // Nord fallback
    REQUIRE(theme.colors.accent_primary == "#88c0d0"); // Nord fallback
}

TEST_CASE("parse_theme_json returns Nord on invalid JSON", "[theme]") {
    auto theme = helix::parse_theme_json("{ invalid json", "bad.json");

    REQUIRE(theme.name == "Nord");
    REQUIRE(theme.is_valid());
}

TEST_CASE("save_theme_to_file and load_theme_from_file roundtrip", "[theme]") {
    auto original = helix::get_default_nord_theme();
    original.name = "Roundtrip Test";
    original.properties.border_radius = 20;

    std::string path = "/tmp/test_theme_roundtrip.json";
    REQUIRE(helix::save_theme_to_file(original, path));

    auto loaded = helix::load_theme_from_file(path);

    REQUIRE(loaded.name == "Roundtrip Test");
    REQUIRE(loaded.properties.border_radius == 20);
    REQUIRE(loaded.colors.bg_darkest == original.colors.bg_darkest);
    REQUIRE(loaded.is_valid());

    // Cleanup
    std::remove(path.c_str());
}
