// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file theme_loader.h
 * @brief Data structures for JSON-based dynamic theming system
 *
 * @pattern POD structs with accessor methods; used by ThemeLoader
 * @threading Main thread only
 */

#pragma once

#include <array>
#include <string>
#include <vector>

namespace helix {

/**
 * @brief 16-color palette with semantic names
 *
 * Indices map to palette slots defined in design doc:
 * 0-3: Dark backgrounds, 4-6: Light backgrounds,
 * 7-10: Accents, 11-15: Status colors
 */
struct ThemePalette {
    std::string bg_darkest;        // 0: Dark mode app background
    std::string bg_dark;           // 1: Dark mode cards/surfaces
    std::string bg_dark_highlight; // 2: Selection highlight on dark
    std::string border_muted;      // 3: Borders, muted text
    std::string text_light;        // 4: Primary text on dark surfaces
    std::string bg_light;          // 5: Light mode cards/surfaces
    std::string bg_lightest;       // 6: Light mode app background
    std::string accent_highlight;  // 7: Subtle highlights
    std::string accent_primary;    // 8: Primary accent, links
    std::string accent_secondary;  // 9: Secondary accent
    std::string accent_tertiary;   // 10: Tertiary accent
    std::string status_error;      // 11: Error, danger (red)
    std::string status_danger;     // 12: Danger, attention (orange)
    std::string status_warning;    // 13: Warning, caution (yellow)
    std::string status_success;    // 14: Success, positive (green)
    std::string status_special;    // 15: Special, unusual (purple)

    /** @brief Access color by index (0-15) */
    const std::string& at(size_t index) const;
    std::string& at(size_t index);

    /** @brief Get array of all color names for iteration */
    static const std::array<const char*, 16>& color_names();
};

/**
 * @brief Non-color theme properties
 */
struct ThemeProperties {
    int border_radius = 12;   // Corner roundness (0 = sharp, 12 = soft)
    int border_width = 1;     // Default border width
    int border_opacity = 40;  // Border opacity (0-255)
    int shadow_intensity = 0; // Shadow strength (0 = disabled)
};

/**
 * @brief Complete theme definition
 */
struct ThemeData {
    std::string name;     // Display name (shown in UI)
    std::string filename; // Source filename (without .json)
    ThemePalette colors;
    ThemeProperties properties;

    /** @brief Check if theme has all required colors */
    bool is_valid() const;
};

/**
 * @brief Theme file info for discovery listing
 */
struct ThemeInfo {
    std::string filename;     // e.g., "nord"
    std::string display_name; // e.g., "Nord"
};

} // namespace helix
