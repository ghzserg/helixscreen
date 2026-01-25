// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "theme_loader.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>

#include "hv/json.hpp"

namespace helix {

// ============================================================================
// ModePalette implementation (new dual-palette system)
// ============================================================================

const std::array<const char*, 16>& ModePalette::color_names() {
    static const std::array<const char*, 16> names = {
        "app_bg",     "panel_bg",    "card_bg", "card_alt",  "border",   "text",
        "text_muted", "text_subtle", "primary", "secondary", "tertiary", "info",
        "success",    "warning",     "danger",  "focus"};
    return names;
}

const std::string& ModePalette::at(size_t index) const {
    switch (index) {
    case 0:
        return app_bg;
    case 1:
        return panel_bg;
    case 2:
        return card_bg;
    case 3:
        return card_alt;
    case 4:
        return border;
    case 5:
        return text;
    case 6:
        return text_muted;
    case 7:
        return text_subtle;
    case 8:
        return primary;
    case 9:
        return secondary;
    case 10:
        return tertiary;
    case 11:
        return info;
    case 12:
        return success;
    case 13:
        return warning;
    case 14:
        return danger;
    case 15:
        return focus;
    default:
        throw std::out_of_range("ModePalette index out of range");
    }
}

std::string& ModePalette::at(size_t index) {
    return const_cast<std::string&>(static_cast<const ModePalette*>(this)->at(index));
}

bool ModePalette::is_valid() const {
    for (size_t i = 0; i < 16; ++i) {
        const auto& color = at(i);
        if (color.empty() || color[0] != '#' || color.size() != 7) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// ThemePalette implementation (legacy - kept for backward compatibility)
// ============================================================================

const std::array<const char*, 16>& ThemePalette::color_names() {
    static const std::array<const char*, 16> names = {
        "bg_darkest",     "bg_dark",          "surface_elevated", "surface_dim",
        "text_light",     "bg_light",         "bg_lightest",      "accent_highlight",
        "accent_primary", "accent_secondary", "accent_tertiary",  "status_error",
        "status_danger",  "status_warning",   "status_success",   "status_special"};
    return names;
}

const std::string& ThemePalette::at(size_t index) const {
    switch (index) {
    case 0:
        return bg_darkest;
    case 1:
        return bg_dark;
    case 2:
        return surface_elevated;
    case 3:
        return surface_dim;
    case 4:
        return text_light;
    case 5:
        return bg_light;
    case 6:
        return bg_lightest;
    case 7:
        return accent_highlight;
    case 8:
        return accent_primary;
    case 9:
        return accent_secondary;
    case 10:
        return accent_tertiary;
    case 11:
        return status_error;
    case 12:
        return status_danger;
    case 13:
        return status_warning;
    case 14:
        return status_success;
    case 15:
        return status_special;
    default:
        throw std::out_of_range("ThemePalette index out of range");
    }
}

std::string& ThemePalette::at(size_t index) {
    return const_cast<std::string&>(static_cast<const ThemePalette*>(this)->at(index));
}

bool ThemeData::is_valid() const {
    if (name.empty()) {
        return false;
    }

    // New format: at least one of dark/light must be valid
    if (dark.is_valid() || light.is_valid()) {
        return true;
    }

    // Legacy format: check all colors in ThemePalette
    for (size_t i = 0; i < 16; ++i) {
        const auto& color = colors.at(i);
        if (color.empty() || color[0] != '#' || color.size() != 7) {
            return false;
        }
    }
    return true;
}

bool ThemeData::supports_dark() const {
    return dark.is_valid();
}

bool ThemeData::supports_light() const {
    return light.is_valid();
}

ThemeModeSupport ThemeData::get_mode_support() const {
    bool has_dark = dark.is_valid();
    bool has_light = light.is_valid();

    if (has_dark && has_light) {
        return ThemeModeSupport::DUAL_MODE;
    } else if (has_dark) {
        return ThemeModeSupport::DARK_ONLY;
    } else {
        return ThemeModeSupport::LIGHT_ONLY;
    }
}

ThemeData get_default_nord_theme() {
    ThemeData theme;
    theme.name = "Nord";
    theme.filename = "nord";

    // NEW: Populate dual palette system (dark mode)
    theme.dark.app_bg = "#2e3440";
    theme.dark.panel_bg = "#3b4252";
    theme.dark.card_bg = "#434c5e";
    theme.dark.card_alt = "#4c566a";
    theme.dark.border = "#616e88";
    theme.dark.text = "#eceff4";
    theme.dark.text_muted = "#d8dee9";
    theme.dark.text_subtle = "#b8c2d1";
    theme.dark.primary = "#88c0d0";   // nord8 - frost cyan
    theme.dark.secondary = "#81a1c1"; // nord9 - frost blue
    theme.dark.tertiary = "#5e81ac";  // nord10 - frost dark blue
    theme.dark.info = "#b48ead";      // nord15 - aurora purple
    theme.dark.success = "#a3be8c";   // nord14 - aurora green
    theme.dark.warning = "#ebcb8b";   // nord13 - aurora yellow
    theme.dark.danger = "#bf616a";    // nord11 - aurora red
    theme.dark.focus = "#8fbcbb";     // nord7 - frost teal

    // NEW: Populate dual palette system (light mode)
    theme.light.app_bg = "#eceff4";
    theme.light.panel_bg = "#e5e9f0";
    theme.light.card_bg = "#ffffff";
    theme.light.card_alt = "#edeff6";
    theme.light.border = "#cbd5e1";
    theme.light.text = "#2e3440";
    theme.light.text_muted = "#3b4252";
    theme.light.text_subtle = "#64748b";
    theme.light.primary = "#5e81ac";   // nord10 - darker frost for light bg
    theme.light.secondary = "#81a1c1"; // nord9 - frost blue
    theme.light.tertiary = "#4c566a";  // nord3 - polar night for contrast
    theme.light.info = "#b48ead";      // nord15 - aurora purple
    theme.light.success = "#3fa47d";   // adjusted green for light bg
    theme.light.warning = "#b08900";   // adjusted yellow for light bg
    theme.light.danger = "#b23a48";    // adjusted red for light bg
    theme.light.focus = "#8fbcbb";     // nord7 - frost teal

    // LEGACY: Keep for backward compatibility
    theme.colors.bg_darkest = "#2e3440";
    theme.colors.bg_dark = "#3b4252";
    theme.colors.surface_elevated = "#434c5e";
    theme.colors.surface_dim = "#4c566a";
    theme.colors.text_light = "#d8dee9";
    theme.colors.bg_light = "#e5e9f0";
    theme.colors.bg_lightest = "#eceff4";
    theme.colors.accent_highlight = "#8fbcbb";
    theme.colors.accent_primary = "#88c0d0";
    theme.colors.accent_secondary = "#81a1c1";
    theme.colors.accent_tertiary = "#5e81ac";
    theme.colors.status_error = "#bf616a";
    theme.colors.status_danger = "#d08770";
    theme.colors.status_warning = "#ebcb8b";
    theme.colors.status_success = "#a3be8c";
    theme.colors.status_special = "#b48ead";

    theme.properties.border_radius = 12;
    theme.properties.border_width = 1;
    theme.properties.border_opacity = 40;
    theme.properties.shadow_intensity = 0;

    return theme;
}

/**
 * @brief Helper to parse a ModePalette from a JSON object
 */
static void parse_mode_palette(const nlohmann::json& palette_json, ModePalette& palette,
                               const std::string& filename, const std::string& mode_name) {
    auto& names = ModePalette::color_names();

    for (size_t i = 0; i < 16; ++i) {
        const char* name = names[i];
        if (palette_json.contains(name)) {
            palette.at(i) = palette_json[name].get<std::string>();
        } else {
            spdlog::warn("[ThemeLoader] Missing '{}' in {}.{}, using empty", name, filename,
                         mode_name);
        }
    }
}

/**
 * @brief Convert legacy ThemePalette format to dark-only ModePalette
 *
 * Legacy mapping:
 *   bg_darkest -> app_bg
 *   bg_dark -> panel_bg
 *   surface_elevated -> card_bg
 *   surface_dim -> card_alt
 *   accent_secondary -> border (approximation)
 *   bg_lightest -> text (inverted for dark theme)
 *   text_light -> text_muted (approximate)
 *   accent_highlight -> text_subtle (approximate)
 *   accent_primary -> primary
 *   accent_secondary -> secondary
 *   accent_tertiary -> tertiary
 *   status_special -> info
 *   status_success -> success
 *   status_warning -> warning
 *   status_error -> danger
 *   accent_primary -> focus
 */
static void convert_legacy_to_dark(const ThemePalette& legacy, ModePalette& dark) {
    dark.app_bg = legacy.bg_darkest;
    dark.panel_bg = legacy.bg_dark;
    dark.card_bg = legacy.surface_elevated;
    dark.card_alt = legacy.surface_dim;
    dark.border = legacy.accent_secondary;
    dark.text = legacy.bg_lightest;
    dark.text_muted = legacy.text_light;
    dark.text_subtle = legacy.accent_highlight;
    dark.primary = legacy.accent_primary;
    dark.secondary = legacy.accent_secondary;
    dark.tertiary = legacy.accent_tertiary;
    dark.info = legacy.status_special;
    dark.success = legacy.status_success;
    dark.warning = legacy.status_warning;
    dark.danger = legacy.status_error;
    dark.focus = legacy.accent_primary;
}

/**
 * @brief Convert dark ModePalette to legacy ThemePalette (for backward compatibility)
 *
 * Reverse mapping from convert_legacy_to_dark
 */
static void convert_dark_to_legacy(const ModePalette& dark, ThemePalette& legacy) {
    legacy.bg_darkest = dark.app_bg;
    legacy.bg_dark = dark.panel_bg;
    legacy.surface_elevated = dark.card_bg;
    legacy.surface_dim = dark.card_alt;
    legacy.text_light = dark.text_muted;
    legacy.bg_light = dark.text_subtle; // approximate
    legacy.bg_lightest = dark.text;
    legacy.accent_highlight = dark.text_subtle;
    legacy.accent_primary = dark.primary;
    legacy.accent_secondary = dark.secondary;
    legacy.accent_tertiary = dark.tertiary;
    legacy.status_error = dark.danger;
    legacy.status_danger = dark.tertiary; // approximate
    legacy.status_warning = dark.warning;
    legacy.status_success = dark.success;
    legacy.status_special = dark.info;
}

ThemeData parse_theme_json(const std::string& json_str, const std::string& filename) {
    ThemeData theme;
    theme.filename = filename;

    // Remove .json extension if present
    if (theme.filename.size() > 5 && theme.filename.substr(theme.filename.size() - 5) == ".json") {
        theme.filename = theme.filename.substr(0, theme.filename.size() - 5);
    }

    try {
        auto json = nlohmann::json::parse(json_str);

        theme.name = json.value("name", "Unnamed Theme");

        // Detect format: new format has "dark" and/or "light" keys
        bool has_dark = json.contains("dark");
        bool has_light = json.contains("light");
        bool has_colors = json.contains("colors");

        if (has_dark || has_light) {
            // NEW FORMAT: Parse dark and/or light palettes
            spdlog::debug("[ThemeLoader] Parsing {} in new dual-palette format", filename);

            if (has_dark) {
                parse_mode_palette(json["dark"], theme.dark, filename, "dark");
                // Also populate legacy colors for backward compatibility
                convert_dark_to_legacy(theme.dark, theme.colors);
            }
            if (has_light) {
                parse_mode_palette(json["light"], theme.light, filename, "light");
            }
        } else if (has_colors) {
            // LEGACY FORMAT: Parse old colors object and convert to dark-only
            spdlog::trace("[ThemeLoader] Parsing {} in legacy format, converting to dark-only",
                          filename);

            auto& colors = json["colors"];
            auto& names = ThemePalette::color_names();
            auto defaults = get_default_nord_theme();

            for (size_t i = 0; i < 16; ++i) {
                const char* name = names[i];
                if (colors.contains(name)) {
                    theme.colors.at(i) = colors[name].get<std::string>();
                } else {
                    // Fall back to Nord default
                    theme.colors.at(i) = defaults.colors.at(i);
                    spdlog::warn("[ThemeLoader] Missing color '{}' in {}, using Nord default", name,
                                 filename);
                }
            }

            // Convert legacy to new dark palette
            convert_legacy_to_dark(theme.colors, theme.dark);
        } else {
            spdlog::error("[ThemeLoader] No 'dark', 'light', or 'colors' object in {}", filename);
            return get_default_nord_theme();
        }

        // Parse properties with defaults
        theme.properties.border_radius = json.value("border_radius", 12);
        theme.properties.border_width = json.value("border_width", 1);
        theme.properties.border_opacity = json.value("border_opacity", 40);
        theme.properties.shadow_intensity = json.value("shadow_intensity", 0);

    } catch (const nlohmann::json::exception& e) {
        spdlog::error("[ThemeLoader] Failed to parse {}: {}", filename, e.what());
        return get_default_nord_theme();
    }

    return theme;
}

ThemeData load_theme_from_file(const std::string& filepath_or_name) {
    std::string filepath = filepath_or_name;
    std::string filename;

    // Check if this is a full path or just a theme name
    if (filepath_or_name.find('/') == std::string::npos) {
        // Just a theme name - look up in directories
        std::string themes_dir = get_themes_directory();
        std::string defaults_dir = get_default_themes_directory();

        // Add .json extension if not present
        std::string name_with_ext = filepath_or_name;
        if (name_with_ext.size() < 5 || name_with_ext.substr(name_with_ext.size() - 5) != ".json") {
            name_with_ext += ".json";
        }

        // Check user themes directory first
        std::string user_path = themes_dir + "/" + name_with_ext;
        struct stat st;
        if (stat(user_path.c_str(), &st) == 0) {
            filepath = user_path;
            spdlog::debug("[ThemeLoader] Loading user theme from {}", filepath);
        } else {
            // Fall back to defaults directory
            std::string defaults_path = defaults_dir + "/" + name_with_ext;
            if (stat(defaults_path.c_str(), &st) == 0) {
                filepath = defaults_path;
                spdlog::debug("[ThemeLoader] Loading default theme from {}", filepath);
            } else {
                spdlog::error("[ThemeLoader] Theme '{}' not found in themes or defaults",
                              filepath_or_name);
                return {};
            }
        }
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        spdlog::error("[ThemeLoader] Failed to open {}", filepath);
        return {};
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    // Extract filename from path
    filename = filepath;
    size_t slash = filepath.rfind('/');
    if (slash != std::string::npos) {
        filename = filepath.substr(slash + 1);
    }

    return parse_theme_json(buffer.str(), filename);
}

/**
 * @brief Helper to serialize a ModePalette to JSON
 */
static nlohmann::json serialize_mode_palette(const ModePalette& palette) {
    nlohmann::json result;
    auto& names = ModePalette::color_names();
    for (size_t i = 0; i < 16; ++i) {
        result[names[i]] = palette.at(i);
    }
    return result;
}

bool save_theme_to_file(const ThemeData& theme, const std::string& filepath) {
    nlohmann::json json;

    json["name"] = theme.name;

    // NEW FORMAT: Save dark and/or light palettes
    if (theme.dark.is_valid()) {
        json["dark"] = serialize_mode_palette(theme.dark);
    }
    if (theme.light.is_valid()) {
        json["light"] = serialize_mode_palette(theme.light);
    }

    // LEGACY FALLBACK: If no new palettes but has legacy colors, save those
    if (!theme.dark.is_valid() && !theme.light.is_valid()) {
        nlohmann::json colors;
        auto& names = ThemePalette::color_names();
        for (size_t i = 0; i < 16; ++i) {
            colors[names[i]] = theme.colors.at(i);
        }
        json["colors"] = colors;
    }

    // Properties
    json["border_radius"] = theme.properties.border_radius;
    json["border_width"] = theme.properties.border_width;
    json["border_opacity"] = theme.properties.border_opacity;
    json["shadow_intensity"] = theme.properties.shadow_intensity;

    // Write with pretty formatting
    std::ofstream file(filepath);
    if (!file.is_open()) {
        spdlog::error("[ThemeLoader] Failed to write {}", filepath);
        return false;
    }

    file << json.dump(2);
    return true;
}

std::string get_themes_directory() {
    return "config/themes";
}

std::string get_default_themes_directory() {
    return "config/themes/defaults";
}

bool has_default_theme(const std::string& filename) {
    std::string defaults_dir = get_default_themes_directory();

    // Add .json extension if not present
    std::string name_with_ext = filename;
    if (name_with_ext.size() < 5 || name_with_ext.substr(name_with_ext.size() - 5) != ".json") {
        name_with_ext += ".json";
    }

    std::string defaults_path = defaults_dir + "/" + name_with_ext;
    struct stat st;
    return stat(defaults_path.c_str(), &st) == 0;
}

std::optional<ThemeData> reset_theme_to_default(const std::string& filename) {
    // Check if a default exists
    if (!has_default_theme(filename)) {
        spdlog::debug("[ThemeLoader] No default theme for '{}', cannot reset", filename);
        return std::nullopt;
    }

    std::string themes_dir = get_themes_directory();
    std::string defaults_dir = get_default_themes_directory();

    // Add .json extension if not present
    std::string name_with_ext = filename;
    if (name_with_ext.size() < 5 || name_with_ext.substr(name_with_ext.size() - 5) != ".json") {
        name_with_ext += ".json";
    }

    // Delete user override if it exists
    std::string user_path = themes_dir + "/" + name_with_ext;
    struct stat st;
    if (stat(user_path.c_str(), &st) == 0) {
        if (std::remove(user_path.c_str()) != 0) {
            spdlog::error("[ThemeLoader] Failed to delete user theme override: {}", user_path);
            return std::nullopt;
        }
        spdlog::info("[ThemeLoader] Deleted user theme override: {}", user_path);
    }

    // Load and return the default theme
    std::string defaults_path = defaults_dir + "/" + name_with_ext;
    return load_theme_from_file(defaults_path);
}

bool ensure_themes_directory(const std::string& themes_dir) {
    struct stat st;

    // First ensure parent config directory exists
    std::string config_dir = "config";
    if (stat(config_dir.c_str(), &st) != 0) {
        if (mkdir(config_dir.c_str(), 0755) != 0) {
            spdlog::error("[ThemeLoader] Failed to create config directory {}: {}", config_dir,
                          strerror(errno));
            return false;
        }
        spdlog::info("[ThemeLoader] Created config directory: {}", config_dir);
    }

    // Then create themes directory if it doesn't exist
    if (stat(themes_dir.c_str(), &st) != 0) {
        if (mkdir(themes_dir.c_str(), 0755) != 0) {
            spdlog::error("[ThemeLoader] Failed to create themes directory {}: {}", themes_dir,
                          strerror(errno));
            return false;
        }
        spdlog::info("[ThemeLoader] Created themes directory: {}", themes_dir);
    }

    // Check if nord.json exists, create if missing
    std::string nord_path = themes_dir + "/nord.json";
    if (stat(nord_path.c_str(), &st) != 0) {
        auto nord = get_default_nord_theme();
        if (!save_theme_to_file(nord, nord_path)) {
            spdlog::error("[ThemeLoader] Failed to create default nord.json");
            return false;
        }
        spdlog::info("[ThemeLoader] Created default theme: {}", nord_path);
    }

    return true;
}

std::vector<ThemeInfo> discover_themes(const std::string& themes_dir) {
    std::vector<ThemeInfo> themes;
    std::set<std::string> seen_filenames;

    // Helper lambda to scan a directory
    auto scan_directory = [&](const std::string& dir_path, bool is_defaults) {
        DIR* dir = opendir(dir_path.c_str());
        if (!dir) {
            if (!is_defaults) {
                // User themes dir not existing is fine
                spdlog::debug("[ThemeLoader] Themes directory doesn't exist yet: {}", dir_path);
            }
            return;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;

            // Skip non-json files
            if (filename.size() <= 5 || filename.substr(filename.size() - 5) != ".json") {
                continue;
            }

            // Skip hidden files
            if (filename[0] == '.') {
                continue;
            }

            std::string base_name = filename.substr(0, filename.size() - 5); // Remove .json

            // Skip if we've already seen this theme (user overrides take precedence)
            if (seen_filenames.count(base_name) > 0) {
                continue;
            }

            std::string filepath = dir_path + "/" + filename;
            auto theme = load_theme_from_file(filepath);

            if (theme.is_valid()) {
                ThemeInfo info;
                info.filename = base_name;
                info.display_name = theme.name;
                themes.push_back(info);
                seen_filenames.insert(base_name);
            }
        }

        closedir(dir);
    };

    // First scan user themes directory (takes precedence)
    scan_directory(themes_dir, false);

    // Then scan defaults directory
    std::string defaults_dir = get_default_themes_directory();
    scan_directory(defaults_dir, true);

    // Sort alphabetically by display name
    std::sort(themes.begin(), themes.end(), [](const ThemeInfo& a, const ThemeInfo& b) {
        return a.display_name < b.display_name;
    });

    spdlog::debug("[ThemeLoader] Discovered {} themes (user + defaults)", themes.size());
    return themes;
}

} // namespace helix
