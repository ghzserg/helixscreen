// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "theme_loader.h"

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
