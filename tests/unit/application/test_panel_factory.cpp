// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_panel_factory.cpp
 * @brief Unit tests for PanelFactory class
 *
 * Tests panel discovery, setup, and overlay creation.
 * Full tests require LVGL and XML components - marked as .integration.
 */

#include "ui_nav.h" // For UI_PANEL_COUNT

#include "panel_factory.h"

#include <cstring>

#include "../../catch_amalgamated.hpp"

// ============================================================================
// PanelFactory Constants Tests
// ============================================================================

TEST_CASE("PanelFactory has correct panel count", "[application][panels]") {
    REQUIRE(UI_PANEL_COUNT == 6);
}

TEST_CASE("Panel enum values are sequential", "[application][panels]") {
    REQUIRE(static_cast<int>(PanelId::Home) == 0);
    REQUIRE(static_cast<int>(PanelId::PrintSelect) == 1);
    REQUIRE(static_cast<int>(PanelId::Controls) == 2);
    REQUIRE(static_cast<int>(PanelId::Filament) == 3);
    REQUIRE(static_cast<int>(PanelId::Settings) == 4);
    REQUIRE(static_cast<int>(PanelId::Advanced) == 5);
}

TEST_CASE("PanelFactory PANEL_NAMES has correct entries", "[application][panels]") {
    // Verify the panel names array matches expected values
    REQUIRE(std::strcmp(PanelFactory::PANEL_NAMES[static_cast<int>(PanelId::Home)], "home_panel") ==
            0);
    REQUIRE(std::strcmp(PanelFactory::PANEL_NAMES[static_cast<int>(PanelId::PrintSelect)],
                        "print_select_panel") == 0);
    REQUIRE(std::strcmp(PanelFactory::PANEL_NAMES[static_cast<int>(PanelId::Controls)],
                        "controls_panel") == 0);
    REQUIRE(std::strcmp(PanelFactory::PANEL_NAMES[static_cast<int>(PanelId::Filament)],
                        "filament_panel") == 0);
    REQUIRE(std::strcmp(PanelFactory::PANEL_NAMES[static_cast<int>(PanelId::Settings)],
                        "settings_panel") == 0);
    REQUIRE(std::strcmp(PanelFactory::PANEL_NAMES[static_cast<int>(PanelId::Advanced)],
                        "advanced_panel") == 0);
}

TEST_CASE("PanelFactory PANEL_NAMES count matches UI_PANEL_COUNT", "[application][panels]") {
    // Verify the array has the right number of entries
    size_t count = sizeof(PanelFactory::PANEL_NAMES) / sizeof(PanelFactory::PANEL_NAMES[0]);
    REQUIRE(count == UI_PANEL_COUNT);
}

TEST_CASE("PanelFactory starts with null panels", "[application][panels]") {
    PanelFactory factory;

    // All panel pointers should be null initially
    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        REQUIRE(factory.panels()[i] == nullptr);
    }

    // Print status overlay should be null
    REQUIRE(factory.print_status_panel() == nullptr);
}
