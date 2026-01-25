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
#include <optional>
#include <string>
#include <vector>

namespace helix {

/**
 * @brief Mode support for themes
 */
enum class ThemeModeSupport {
    DUAL_MODE, // Theme has both dark and light palettes
    DARK_ONLY, // Theme only has dark palette
    LIGHT_ONLY // Theme only has light palette
};

/**
 * @brief 16-color mode-specific palette with semantic names
 *
 * New format using semantic color names that map directly to UI purpose.
 */
struct ModePalette {
    std::string app_bg;      // 0: Main app background
    std::string panel_bg;    // 1: Sidebar/panel background
    std::string card_bg;     // 2: Card surfaces
    std::string card_alt;    // 3: Elevated/alternate surfaces
    std::string border;      // 4: Borders and dividers
    std::string text;        // 5: Primary text
    std::string text_muted;  // 6: Secondary text
    std::string text_subtle; // 7: Hint/tertiary text
    std::string primary;     // 8: Primary accent
    std::string secondary;   // 9: Secondary accent
    std::string tertiary;    // 10: Tertiary accent
    std::string info;        // 11: Info states
    std::string success;     // 12: Success states
    std::string warning;     // 13: Warning states
    std::string danger;      // 14: Error/danger states
    std::string focus;       // 15: Focus ring color

    /** @brief Access color by index (0-15) */
    const std::string& at(size_t index) const;
    std::string& at(size_t index);

    /** @brief Get array of all color names for iteration */
    static const std::array<const char*, 16>& color_names();

    /** @brief Check if all colors are valid (non-empty, start with #) */
    bool is_valid() const;
};

/**
 * @brief 16-color palette with semantic names (LEGACY - kept for backward compatibility)
 *
 * Indices map to palette slots defined in design doc:
 * 0-3: Dark backgrounds, 4-6: Light backgrounds,
 * 7-10: Accents, 11-15: Status colors
 */
struct ThemePalette {
    std::string bg_darkest;       // 0: Dark mode app background
    std::string bg_dark;          // 1: Dark mode cards/surfaces
    std::string surface_elevated; // 2: Elevated surfaces, selections
    std::string surface_dim;      // 3: Control surfaces (buttons, inputs)
    std::string text_light;       // 4: Primary text on dark surfaces
    std::string bg_light;         // 5: Light mode cards/surfaces
    std::string bg_lightest;      // 6: Light mode app background
    std::string accent_highlight; // 7: Subtle highlights
    std::string accent_primary;   // 8: Primary accent, links
    std::string accent_secondary; // 9: Secondary accent
    std::string accent_tertiary;  // 10: Tertiary accent
    std::string status_error;     // 11: Error, danger (red)
    std::string status_danger;    // 12: Danger, attention (orange)
    std::string status_warning;   // 13: Warning, caution (yellow)
    std::string status_success;   // 14: Success, positive (green)
    std::string status_special;   // 15: Special, unusual (purple)

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

    // NEW: Dual palette system
    ModePalette dark;  // Dark mode colors
    ModePalette light; // Light mode colors

    // LEGACY: Keep for backward compatibility during migration
    ThemePalette colors;

    ThemeProperties properties;

    /** @brief Check if theme is valid (has name and at least one valid palette) */
    bool is_valid() const;

    /** @brief Check if dark mode is supported */
    bool supports_dark() const;

    /** @brief Check if light mode is supported */
    bool supports_light() const;

    /** @brief Get mode support type */
    ThemeModeSupport get_mode_support() const;
};

/**
 * @brief Theme file info for discovery listing
 */
struct ThemeInfo {
    std::string filename;     // e.g., "nord"
    std::string display_name; // e.g., "Nord"
};

/**
 * @brief Load theme from JSON file
 * @param filepath Full path to theme JSON file
 * @return ThemeData with parsed values, or empty ThemeData on error
 */
ThemeData load_theme_from_file(const std::string& filepath);

/**
 * @brief Parse theme from JSON string
 * @param json_str JSON content
 * @param filename Source filename for error messages
 * @return ThemeData with parsed values
 */
ThemeData parse_theme_json(const std::string& json_str, const std::string& filename);

/**
 * @brief Save theme to JSON file
 * @param theme Theme data to save
 * @param filepath Full path to output file
 * @return true on success, false on error
 */
bool save_theme_to_file(const ThemeData& theme, const std::string& filepath);

/**
 * @brief Get default Nord theme (fallback)
 * @return ThemeData with Nord color values
 */
ThemeData get_default_nord_theme();

/**
 * @brief Discover all theme files in directory
 * @param themes_dir Path to themes directory
 * @return List of theme info (filename, display_name)
 */
std::vector<ThemeInfo> discover_themes(const std::string& themes_dir);

/**
 * @brief Ensure themes directory exists with default theme
 * @param themes_dir Path to themes directory
 * @return true if directory is ready, false on error
 */
bool ensure_themes_directory(const std::string& themes_dir);

/**
 * @brief Get themes directory path (user overrides)
 * @return Full path to config/themes/
 */
std::string get_themes_directory();

/**
 * @brief Get default themes directory path (shipped themes)
 * @return Full path to config/themes/defaults/
 */
std::string get_default_themes_directory();

/**
 * @brief Check if a theme exists in the defaults directory
 * @param filename Theme filename (without .json extension)
 * @return true if the theme is a shipped default theme
 */
bool has_default_theme(const std::string& filename);

/**
 * @brief Reset a theme to its default state
 *
 * For built-in themes: Deletes the user override file and returns the default theme.
 * For user-created themes: Returns nullopt (no default to reset to).
 *
 * @param filename Theme filename (without .json extension)
 * @return The default ThemeData if reset succeeded, nullopt if theme has no default
 */
std::optional<ThemeData> reset_theme_to_default(const std::string& filename);

} // namespace helix
