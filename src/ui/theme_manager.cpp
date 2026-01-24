// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "theme_manager.h"

#include "ui_error_reporting.h"
#include "ui_fonts.h"

#include "config.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/libs/expat/expat.h"
#include "lvgl/src/xml/lv_xml.h"
#include "settings_manager.h"
#include "theme_core.h"
#include "theme_loader.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

static lv_theme_t* current_theme = nullptr;
static bool use_dark_mode = true;
static lv_display_t* theme_display = nullptr;

static helix::ThemeData active_theme;

// Theme preset overrides removed - colors now come from theme JSON files via ThemeData

// Parse hex color string "#FF4444" -> lv_color_hex(0xFF4444)
lv_color_t theme_manager_parse_hex_color(const char* hex_str) {
    if (!hex_str || hex_str[0] != '#') {
        spdlog::error("[Theme] Invalid hex color string: {}", hex_str ? hex_str : "NULL");
        return lv_color_hex(0x000000);
    }
    uint32_t hex = static_cast<uint32_t>(strtoul(hex_str + 1, NULL, 16));
    return lv_color_hex(hex);
}

/**
 * @brief Darken a hex color string by a percentage
 * @param hex_str Color in "#RRGGBB" format
 * @param percent Percentage to keep (0.0-1.0), e.g., 0.7 = 70% brightness
 * @return New hex string (static buffer, not thread-safe)
 */
static const char* darken_hex_color(const char* hex_str, float percent) {
    static char result[8];
    if (!hex_str || hex_str[0] != '#' || strlen(hex_str) < 7) {
        return hex_str; // Return original on error
    }
    uint32_t hex = static_cast<uint32_t>(strtoul(hex_str + 1, NULL, 16));
    uint8_t r = static_cast<uint8_t>(((hex >> 16) & 0xFF) * percent);
    uint8_t g = static_cast<uint8_t>(((hex >> 8) & 0xFF) * percent);
    uint8_t b = static_cast<uint8_t>((hex & 0xFF) * percent);
    snprintf(result, sizeof(result), "#%02x%02x%02x", r, g, b);
    return result;
}

// No longer needed - helix_theme.c handles all color patching and input widget styling

/**
 * Auto-register theme-aware color constants from all XML files
 *
 * Parses all XML files in ui_xml/ to find color pairs (xxx_light, xxx_dark) and registers
 * the base name (xxx) as a runtime constant with the appropriate value
 * based on current theme mode.
 */
static void theme_manager_register_color_pairs(lv_xml_component_scope_t* scope, bool dark_mode) {
    // Find all color tokens with _light and _dark suffixes from all XML files
    auto light_tokens = theme_manager_parse_all_xml_for_suffix("ui_xml", "color", "_light");
    auto dark_tokens = theme_manager_parse_all_xml_for_suffix("ui_xml", "color", "_dark");

    // For each _light color, check if _dark exists and register base name
    int registered = 0;
    for (const auto& [base_name, light_val] : light_tokens) {
        auto dark_it = dark_tokens.find(base_name);
        if (dark_it != dark_tokens.end()) {
            const char* selected = dark_mode ? dark_it->second.c_str() : light_val.c_str();
            spdlog::trace("[Theme] Registering color {}: selected={}", base_name, selected);
            lv_xml_register_const(scope, base_name.c_str(), selected);
            registered++;
        }
    }

    spdlog::debug("[Theme] Auto-registered {} theme-aware color pairs (dark_mode={})", registered,
                  dark_mode);
}

/**
 * Register static constants from all XML files
 *
 * Parses all XML files for <color>, <px>, and <string> elements and registers
 * any that do NOT have dynamic suffixes (_light, _dark, _small, _medium, _large).
 * These static constants are registered first so dynamic variants can override them.
 */
static void theme_manager_register_static_constants(lv_xml_component_scope_t* scope) {
    const std::vector<std::string> skip_suffixes = {"_light", "_dark", "_small", "_medium",
                                                    "_large"};

    auto has_dynamic_suffix = [&](const std::string& name) {
        for (const auto& suffix : skip_suffixes) {
            if (name.size() > suffix.size() &&
                name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                return true;
            }
        }
        return false;
    };

    int color_count = 0, px_count = 0, string_count = 0;

    auto color_tokens = theme_manager_parse_all_xml_for_element("ui_xml", "color");

    // Merge palette colors from active theme JSON - these override any XML definitions
    auto& names = helix::ThemePalette::color_names();
    for (size_t i = 0; i < 16; ++i) {
        color_tokens[names[i]] = active_theme.colors.at(i);
    }

    for (const auto& [name, value] : color_tokens) {
        if (!has_dynamic_suffix(name)) {
            lv_xml_register_const(scope, name.c_str(), value.c_str());
            color_count++;
        }
    }

    for (const auto& [name, value] : theme_manager_parse_all_xml_for_element("ui_xml", "px")) {
        if (!has_dynamic_suffix(name)) {
            lv_xml_register_const(scope, name.c_str(), value.c_str());
            px_count++;
        }
    }

    for (const auto& [name, value] : theme_manager_parse_all_xml_for_element("ui_xml", "string")) {
        if (!has_dynamic_suffix(name)) {
            lv_xml_register_const(scope, name.c_str(), value.c_str());
            string_count++;
        }
    }

    spdlog::debug("[Theme] Registered {} static colors, {} static px, {} static strings",
                  color_count, px_count, string_count);
}

/**
 * Get the breakpoint suffix for a given resolution
 *
 * @param max_resolution The maximum of horizontal and vertical resolution
 * @return Suffix string: "_small" (≤480), "_medium" (481-800), or "_large" (>800)
 */
const char* theme_manager_get_breakpoint_suffix(int32_t max_resolution) {
    if (max_resolution <= UI_BREAKPOINT_SMALL_MAX) {
        return "_small";
    } else if (max_resolution <= UI_BREAKPOINT_MEDIUM_MAX) {
        return "_medium";
    } else {
        return "_large";
    }
}

/**
 * Register responsive spacing tokens from all XML files
 *
 * Auto-discovers all <px name="xxx_small"> elements from all XML files in ui_xml/
 * and registers base tokens by matching xxx_small/xxx_medium/xxx_large triplets.
 * This makes the system fully extensible without C++ code changes.
 *
 * CRITICAL: Base tokens must NOT be pre-defined or responsive overrides will be
 * silently ignored (LVGL ignores duplicate lv_xml_register_const).
 *
 * @param display The LVGL display to get resolution from
 */
void theme_manager_register_responsive_spacing(lv_display_t* display) {
    int32_t hor_res = lv_display_get_horizontal_resolution(display);
    int32_t ver_res = lv_display_get_vertical_resolution(display);
    int32_t greater_res = LV_MAX(hor_res, ver_res);

    const char* size_suffix = theme_manager_get_breakpoint_suffix(greater_res);
    const char* size_label = (greater_res <= UI_BREAKPOINT_SMALL_MAX)    ? "SMALL"
                             : (greater_res <= UI_BREAKPOINT_MEDIUM_MAX) ? "MEDIUM"
                                                                         : "LARGE";

    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("globals");
    if (!scope) {
        spdlog::warn("[Theme] Failed to get globals scope for spacing constants");
        return;
    }

    // Auto-discover all px tokens from all XML files
    auto small_tokens = theme_manager_parse_all_xml_for_suffix("ui_xml", "px", "_small");
    auto medium_tokens = theme_manager_parse_all_xml_for_suffix("ui_xml", "px", "_medium");
    auto large_tokens = theme_manager_parse_all_xml_for_suffix("ui_xml", "px", "_large");

    int registered = 0;
    for (const auto& [base_name, small_val] : small_tokens) {
        // Verify all three variants exist
        auto medium_it = medium_tokens.find(base_name);
        auto large_it = large_tokens.find(base_name);

        if (medium_it != medium_tokens.end() && large_it != large_tokens.end()) {
            // Select appropriate variant based on breakpoint
            const char* value = nullptr;
            if (strcmp(size_suffix, "_small") == 0) {
                value = small_val.c_str();
            } else if (strcmp(size_suffix, "_medium") == 0) {
                value = medium_it->second.c_str();
            } else {
                value = large_it->second.c_str();
            }
            spdlog::trace("[Theme] Registering spacing {}: selected={}", base_name, value);
            lv_xml_register_const(scope, base_name.c_str(), value);
            registered++;
        }
    }

    spdlog::debug("[Theme] Responsive spacing: {} ({}px) - auto-registered {} tokens", size_label,
                  greater_res, registered);

    // ========================================================================
    // Register computed layout constants (not from globals.xml variants)
    // ========================================================================

    // Select responsive nav_width based on breakpoint
    // Nav width macros: TINY=64, SMALL=76, MEDIUM=94, LARGE=102
    // Mapping: breakpoint SMALL→64, MEDIUM→94, LARGE→102
    int32_t nav_width;
    if (greater_res <= UI_BREAKPOINT_SMALL_MAX) {
        nav_width = UI_NAV_WIDTH_TINY; // 64px for 480x320
    } else if (greater_res <= UI_BREAKPOINT_MEDIUM_MAX) {
        nav_width = UI_NAV_WIDTH_MEDIUM; // 94px for 800x480
    } else {
        nav_width = UI_NAV_WIDTH_LARGE; // 102px for 1024x600, 1280x720+
    }

    // Get space_lg value (already registered above)
    const char* space_lg_str = lv_xml_get_const(nullptr, "space_lg");
    int32_t gap = space_lg_str ? std::atoi(space_lg_str) : 16; // fallback to 16px

    // Calculate overlay widths
    int32_t overlay_width = hor_res - nav_width - gap; // Standard: screen - nav - gap
    int32_t overlay_width_full = hor_res - nav_width;  // Full: screen - nav (no gap)

    // Register as string constants for XML consumption
    char nav_width_str[16];
    char overlay_width_str[16];
    char overlay_width_full_str[16];
    snprintf(nav_width_str, sizeof(nav_width_str), "%d", nav_width);
    snprintf(overlay_width_str, sizeof(overlay_width_str), "%d", overlay_width);
    snprintf(overlay_width_full_str, sizeof(overlay_width_full_str), "%d", overlay_width_full);

    lv_xml_register_const(scope, "nav_width", nav_width_str);
    lv_xml_register_const(scope, "overlay_panel_width", overlay_width_str);
    lv_xml_register_const(scope, "overlay_panel_width_full", overlay_width_full_str);

    spdlog::debug(
        "[Theme] Layout: nav_width={}px, gap={}px, overlay_width={}px, overlay_width_full={}px",
        nav_width, gap, overlay_width, overlay_width_full);
}

/**
 * Register responsive font tokens from all XML files
 *
 * Auto-discovers all <string name="xxx_small"> elements from all XML files in ui_xml/
 * and registers base tokens by matching xxx_small/xxx_medium/xxx_large triplets.
 * This makes the system fully extensible without C++ code changes.
 *
 * @param display The LVGL display to get resolution from
 */
void theme_manager_register_responsive_fonts(lv_display_t* display) {
    int32_t hor_res = lv_display_get_horizontal_resolution(display);
    int32_t ver_res = lv_display_get_vertical_resolution(display);
    int32_t greater_res = LV_MAX(hor_res, ver_res);

    const char* size_suffix = theme_manager_get_breakpoint_suffix(greater_res);
    const char* size_label = (greater_res <= UI_BREAKPOINT_SMALL_MAX)    ? "SMALL"
                             : (greater_res <= UI_BREAKPOINT_MEDIUM_MAX) ? "MEDIUM"
                                                                         : "LARGE";

    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("globals");
    if (!scope) {
        spdlog::warn("[Theme] Failed to get globals scope for font constants");
        return;
    }

    // Auto-discover all string tokens from all XML files
    auto small_tokens = theme_manager_parse_all_xml_for_suffix("ui_xml", "string", "_small");
    auto medium_tokens = theme_manager_parse_all_xml_for_suffix("ui_xml", "string", "_medium");
    auto large_tokens = theme_manager_parse_all_xml_for_suffix("ui_xml", "string", "_large");

    int registered = 0;
    for (const auto& [base_name, small_val] : small_tokens) {
        // Verify all three variants exist
        auto medium_it = medium_tokens.find(base_name);
        auto large_it = large_tokens.find(base_name);

        if (medium_it != medium_tokens.end() && large_it != large_tokens.end()) {
            // Select appropriate variant based on breakpoint
            const char* value = nullptr;
            if (strcmp(size_suffix, "_small") == 0) {
                value = small_val.c_str();
            } else if (strcmp(size_suffix, "_medium") == 0) {
                value = medium_it->second.c_str();
            } else {
                value = large_it->second.c_str();
            }
            spdlog::trace("[Theme] Registering font {}: selected={}", base_name, value);
            lv_xml_register_const(scope, base_name.c_str(), value);
            registered++;
        }
    }

    spdlog::debug("[Theme] Responsive fonts: {} ({}px) - auto-registered {} tokens", size_label,
                  greater_res, registered);
}

/**
 * @brief Register LEGACY theme palette colors as LVGL constants
 *
 * Registers all 16 palette colors from the legacy ThemePalette.
 * These are kept for backward compatibility with existing themes.
 * Must be called BEFORE theme_manager_register_static_constants() so
 * palette colors are available for semantic mapping.
 *
 * Note: The new semantic colors are registered in theme_manager_register_semantic_colors()
 */
static void theme_manager_register_palette_colors(lv_xml_component_scope_t* scope,
                                                  const helix::ThemeData& theme) {
    auto& names = helix::ThemePalette::color_names();
    for (size_t i = 0; i < 16; ++i) {
        lv_xml_register_const(scope, names[i], theme.colors.at(i).c_str());
    }
    spdlog::debug("[Theme] Registered 16 legacy palette colors from theme '{}'", theme.name);
}

/**
 * @brief Register semantic colors from dual-palette system
 *
 * Uses the new ModePalette from theme.dark and theme.light to register
 * all 16 semantic color names with _light/_dark variants.
 *
 * For themes with only one mode (dark-only or light-only), only the available
 * variant is registered. For dual-mode themes, both variants are registered.
 *
 * Also registers legacy aliases for backward compatibility with existing XML.
 *
 * @param scope LVGL XML scope to register constants in
 * @param theme Theme data with dual palettes
 * @param dark_mode Whether to use dark mode values for base names
 */
static void theme_manager_register_semantic_colors(lv_xml_component_scope_t* scope,
                                                   const helix::ThemeData& theme, bool dark_mode) {
    // Check which palettes are available
    bool has_dark = theme.supports_dark();
    bool has_light = theme.supports_light();

    // Determine which palette to use for base name registration
    // For dark-only themes in light mode, still use dark palette
    // For light-only themes in dark mode, still use light palette
    const helix::ModePalette* current_palette = nullptr;
    if (dark_mode && has_dark) {
        current_palette = &theme.dark;
    } else if (!dark_mode && has_light) {
        current_palette = &theme.light;
    } else if (has_dark) {
        current_palette = &theme.dark;
    } else if (has_light) {
        current_palette = &theme.light;
    }

    if (!current_palette) {
        spdlog::error("[Theme] No valid palette available in theme");
        return;
    }

    // Register helper - registers base, _dark, and _light variants (if available)
    auto register_color = [&](const char* name, size_t index) {
        const std::string& current_val = current_palette->at(index);

        char dark_name[128], light_name[128];
        snprintf(dark_name, sizeof(dark_name), "%s_dark", name);
        snprintf(light_name, sizeof(light_name), "%s_light", name);

        // Register base name with current mode's value
        if (!current_val.empty()) {
            lv_xml_register_const(scope, name, current_val.c_str());
        }

        // Register _dark variant if dark palette is available
        if (has_dark) {
            const std::string& dark_val = theme.dark.at(index);
            if (!dark_val.empty()) {
                lv_xml_register_const(scope, dark_name, dark_val.c_str());
            }
        }

        // Register _light variant if light palette is available
        if (has_light) {
            const std::string& light_val = theme.light.at(index);
            if (!light_val.empty()) {
                lv_xml_register_const(scope, light_name, light_val.c_str());
            }
        }
    };

    // Register all 16 semantic colors from ModePalette
    auto& names = helix::ModePalette::color_names();
    for (size_t i = 0; i < 16; ++i) {
        register_color(names[i], i);
    }

    // Register legacy aliases for backward compatibility with existing XML
    // Use silent lookup to avoid LVGL warnings for missing light/dark variants
    auto register_alias = [&](const char* legacy_name, const char* new_name) {
        const char* value = lv_xml_get_const(scope, new_name);
        if (value) {
            lv_xml_register_const(scope, legacy_name, value);
            // Also register _dark/_light variants for the legacy name (if available)
            char dark_name[128], light_name[128];
            char new_dark[128], new_light[128];
            snprintf(dark_name, sizeof(dark_name), "%s_dark", legacy_name);
            snprintf(light_name, sizeof(light_name), "%s_light", legacy_name);
            snprintf(new_dark, sizeof(new_dark), "%s_dark", new_name);
            snprintf(new_light, sizeof(new_light), "%s_light", new_name);

            // Only look up variants that exist based on theme mode support
            if (has_dark) {
                const char* dark_val = lv_xml_get_const_silent(scope, new_dark);
                if (dark_val)
                    lv_xml_register_const(scope, dark_name, dark_val);
            }
            if (has_light) {
                const char* light_val = lv_xml_get_const_silent(scope, new_light);
                if (light_val)
                    lv_xml_register_const(scope, light_name, light_val);
            }
        }
    };

    // Legacy aliases - map old names to new semantic names
    register_alias("app_bg_color", "app_bg");
    register_alias("text_primary", "text");
    register_alias("text_secondary", "text_muted");
    register_alias("header_text", "text_muted");
    register_alias("primary_color", "primary");
    register_alias("secondary_color", "secondary");
    register_alias("tertiary_color", "tertiary");
    register_alias("error_color", "danger");
    register_alias("danger_color", "danger");
    register_alias("attention_color", "warning");
    register_alias("warning_color", "warning");
    register_alias("success_color", "success");
    register_alias("info_color", "info");
    register_alias("highlight_color", "primary");
    register_alias("special_color", "info");
    register_alias("surface_control", "card_alt");
    register_alias("selection_highlight", "card_alt");
    register_alias("keyboard_key", "card_bg");
    register_alias("keyboard_key_special", "panel_bg");

    // Swatch descriptions for theme editor - new semantic names
    lv_xml_register_const(scope, "swatch_0_desc", "App background");
    lv_xml_register_const(scope, "swatch_1_desc", "Panel/sidebar background");
    lv_xml_register_const(scope, "swatch_2_desc", "Card surfaces");
    lv_xml_register_const(scope, "swatch_3_desc", "Elevated surfaces");
    lv_xml_register_const(scope, "swatch_4_desc", "Borders and dividers");
    lv_xml_register_const(scope, "swatch_5_desc", "Primary text");
    lv_xml_register_const(scope, "swatch_6_desc", "Secondary text");
    lv_xml_register_const(scope, "swatch_7_desc", "Subtle/hint text");
    lv_xml_register_const(scope, "swatch_8_desc", "Primary accent");
    lv_xml_register_const(scope, "swatch_9_desc", "Secondary accent");
    lv_xml_register_const(scope, "swatch_10_desc", "Tertiary accent");
    lv_xml_register_const(scope, "swatch_11_desc", "Info states");
    lv_xml_register_const(scope, "swatch_12_desc", "Success states");
    lv_xml_register_const(scope, "swatch_13_desc", "Warning states");
    lv_xml_register_const(scope, "swatch_14_desc", "Danger/error states");
    lv_xml_register_const(scope, "swatch_15_desc", "Focus ring");

    spdlog::debug("[Theme] Registered 16 semantic colors + legacy aliases (dark={}, light={})",
                  has_dark, has_light);
}

/**
 * @brief Load active theme from config
 *
 * Reads /display/theme from config, loads corresponding JSON file.
 * Falls back to Nord if not found.
 *
 * HELIX_THEME env var overrides config (useful for testing/screenshots).
 */
static helix::ThemeData theme_manager_load_active_theme() {
    std::string themes_dir = helix::get_themes_directory();

    // Ensure themes directory exists with default theme
    helix::ensure_themes_directory(themes_dir);

    // Check for HELIX_THEME env var override (useful for testing/screenshots)
    std::string theme_name;
    const char* env_theme = std::getenv("HELIX_THEME");
    if (env_theme && env_theme[0] != '\0') {
        theme_name = env_theme;
        spdlog::info("[Theme] Using HELIX_THEME override: {}", theme_name);
    } else {
        // Read theme name from config
        Config* config = Config::get_instance();
        theme_name = config ? config->get<std::string>("/display/theme", "nord") : "nord";
    }

    // Load theme file
    std::string theme_path = themes_dir + "/" + theme_name + ".json";
    auto theme = helix::load_theme_from_file(theme_path);

    if (!theme.is_valid()) {
        spdlog::warn("[Theme] Theme '{}' not found or invalid, using Nord", theme_name);
        theme = helix::get_default_nord_theme();
    }

    spdlog::info("[Theme] Loaded theme: {} ({})", theme.name, theme.filename);
    return theme;
}

void theme_manager_init(lv_display_t* display, bool use_dark_mode_param) {
    theme_display = display;
    use_dark_mode = use_dark_mode_param;

    // Override runtime theme constants based on light/dark mode preference
    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("globals");
    if (!scope) {
        spdlog::critical(
            "[Theme] FATAL: Failed to get globals scope for runtime constant registration");
        std::exit(EXIT_FAILURE);
    }

    // Load active theme from config/themes directory
    active_theme = theme_manager_load_active_theme();

    // Register palette colors FIRST (before static constants)
    theme_manager_register_palette_colors(scope, active_theme);

    // Register semantic colors derived from palette (includes _light/_dark variants and base names)
    theme_manager_register_semantic_colors(scope, active_theme, use_dark_mode);

    // Register static constants first (colors, px, strings without dynamic suffixes)
    theme_manager_register_static_constants(scope);

    // Auto-register all color pairs from globals.xml (xxx_light/xxx_dark -> xxx)
    // This handles app_bg_color, text_primary, header_text, surface_control, card_bg, etc.
    theme_manager_register_color_pairs(scope, use_dark_mode);

    // Register responsive constants (must be before theme_core_init so fonts are available)
    theme_manager_register_responsive_spacing(display);
    theme_manager_register_responsive_fonts(display);

    // Validate critical color pairs were registered (fail-fast if missing)
    static const char* required_colors[] = {"app_bg_color", "text_primary", "header_text", nullptr};
    for (const char** name = required_colors; *name != nullptr; ++name) {
        if (!lv_xml_get_const(nullptr, *name)) {
            spdlog::critical(
                "[Theme] FATAL: Missing required color pair {}_light/{}_dark in globals.xml", *name,
                *name);
            std::exit(EXIT_FAILURE);
        }
    }

    spdlog::debug("[Theme] Runtime constants set for {} mode", use_dark_mode ? "dark" : "light");

    // Read colors from globals.xml
    const char* primary_str = lv_xml_get_const(NULL, "primary_color");
    const char* secondary_str = lv_xml_get_const(NULL, "secondary_color");

    if (!primary_str || !secondary_str) {
        spdlog::error("[Theme] Failed to read color constants from globals.xml");
        return;
    }

    lv_color_t primary_color = theme_manager_parse_hex_color(primary_str);
    lv_color_t secondary_color = theme_manager_parse_hex_color(secondary_str);

    // Read responsive font based on current breakpoint
    // NOTE: We read the variant directly because base constants are removed to enable
    // responsive overrides (LVGL ignores lv_xml_register_const for existing constants)
    int32_t hor_res = lv_display_get_horizontal_resolution(display);
    int32_t ver_res = lv_display_get_vertical_resolution(display);
    int32_t greater_res = LV_MAX(hor_res, ver_res);
    const char* size_suffix = theme_manager_get_breakpoint_suffix(greater_res);

    char font_variant_name[64];
    snprintf(font_variant_name, sizeof(font_variant_name), "font_body%s", size_suffix);
    const char* font_body_name = lv_xml_get_const(NULL, font_variant_name);
    const lv_font_t* base_font = font_body_name ? lv_xml_get_font(NULL, font_body_name) : nullptr;
    if (!base_font) {
        spdlog::warn("[Theme] Failed to get font '{}', using noto_sans_16", font_variant_name);
        base_font = &noto_sans_16;
    }

    // Read color values from auto-registered constants
    const char* screen_bg_str = lv_xml_get_const(nullptr, "app_bg_color");
    const char* card_bg_str = lv_xml_get_const(nullptr, "card_bg");
    const char* surface_control_str = lv_xml_get_const(nullptr, "surface_control");
    const char* text_primary_str = lv_xml_get_const(nullptr, "text_primary");
    const char* focus_str = lv_xml_get_const(nullptr, "focus");
    const char* border_str = lv_xml_get_const(nullptr, "border");

    if (!screen_bg_str || !card_bg_str || !surface_control_str || !text_primary_str) {
        spdlog::error("[Theme] Failed to read auto-registered color constants");
        return;
    }

    lv_color_t screen_bg = theme_manager_parse_hex_color(screen_bg_str);
    lv_color_t card_bg = theme_manager_parse_hex_color(card_bg_str);
    lv_color_t surface_control = theme_manager_parse_hex_color(surface_control_str);
    lv_color_t text_primary_color = theme_manager_parse_hex_color(text_primary_str);
    // Default to primary color if focus token not available
    lv_color_t focus_color = focus_str ? theme_manager_parse_hex_color(focus_str) : primary_color;
    // Default to surface_control if border token not available
    lv_color_t border_color = border_str ? theme_manager_parse_hex_color(border_str) : surface_control;

    // Read border radius from globals.xml
    const char* border_radius_str = lv_xml_get_const(nullptr, "border_radius");
    if (!border_radius_str) {
        spdlog::error("[Theme] Failed to read border_radius from globals.xml");
        return;
    }
    int32_t border_radius = atoi(border_radius_str);

    // Initialize custom HelixScreen theme (wraps LVGL default theme)
    current_theme =
        theme_core_init(display, primary_color, secondary_color, text_primary_color, use_dark_mode,
                        base_font, screen_bg, card_bg, surface_control, focus_color, border_color,
                        border_radius);

    if (current_theme) {
        lv_display_set_theme(display, current_theme);
        spdlog::info("[Theme] Initialized HelixScreen theme: {} mode",
                     use_dark_mode ? "dark" : "light");
        spdlog::debug("[Theme] Colors: primary={}, secondary={}, screen={}, card={}, grey={}",
                      primary_str, secondary_str, screen_bg_str, card_bg_str, surface_control_str);
    } else {
        spdlog::error("[Theme] Failed to initialize HelixScreen theme");
    }
}

/**
 * Walk widget tree and force style refresh on each widget
 *
 * This is needed for widgets that have local/inline styles from XML.
 * Theme styles are automatically refreshed by lv_obj_report_style_change(),
 * but local styles need explicit refresh.
 */
static lv_obj_tree_walk_res_t refresh_style_cb(lv_obj_t* obj, void* user_data) {
    (void)user_data;
    // Force LVGL to recalculate all style properties for this widget
    lv_obj_refresh_style(obj, LV_PART_ANY, LV_STYLE_PROP_ANY);
    return LV_OBJ_TREE_WALK_NEXT;
}

void theme_manager_refresh_widget_tree(lv_obj_t* root) {
    if (!root)
        return;

    // Walk entire tree and refresh each widget's styles
    lv_obj_tree_walk(root, refresh_style_cb, nullptr);
}

void theme_manager_toggle_dark_mode() {
    if (!theme_display) {
        spdlog::error("[Theme] Cannot toggle: theme not initialized");
        return;
    }

    bool new_use_dark_mode = !use_dark_mode;
    use_dark_mode = new_use_dark_mode;
    spdlog::info("[Theme] Switching to {} mode", new_use_dark_mode ? "dark" : "light");

    // Read color values directly from _light/_dark variants
    // Note: We can't update lv_xml_register_const() values at runtime (LVGL limitation),
    // so we read the appropriate variant directly based on the new theme mode.
    const char* suffix = new_use_dark_mode ? "_dark" : "_light";

    auto get_themed_color = [suffix](const char* base_name,
                                     const char* fallback_name) -> const char* {
        char full_name[128];
        snprintf(full_name, sizeof(full_name), "%s%s", base_name, suffix);
        const char* val = lv_xml_get_const(nullptr, full_name);
        if (!val && fallback_name) {
            // Try legacy name as fallback
            snprintf(full_name, sizeof(full_name), "%s%s", fallback_name, suffix);
            val = lv_xml_get_const(nullptr, full_name);
        }
        return val;
    };

    // Use new semantic names with legacy fallbacks
    const char* screen_bg_str = get_themed_color("app_bg", "app_bg_color");
    const char* card_bg_str = get_themed_color("card_bg", nullptr);
    const char* surface_control_str = get_themed_color("card_alt", "surface_control");
    const char* text_primary_str = get_themed_color("text", "text_primary");
    const char* focus_str = get_themed_color("focus", nullptr);
    const char* primary_str = get_themed_color("primary", nullptr);
    const char* border_str = get_themed_color("border", nullptr);

    if (!screen_bg_str || !card_bg_str || !surface_control_str || !text_primary_str) {
        spdlog::error("[Theme] Failed to read color constants for {} mode",
                      new_use_dark_mode ? "dark" : "light");
        return;
    }

    lv_color_t screen_bg = theme_manager_parse_hex_color(screen_bg_str);
    lv_color_t card_bg = theme_manager_parse_hex_color(card_bg_str);
    lv_color_t surface_control = theme_manager_parse_hex_color(surface_control_str);
    lv_color_t text_primary_color = theme_manager_parse_hex_color(text_primary_str);
    // Default to primary accent color (#5e81ac) if focus token not available
    lv_color_t focus_color =
        focus_str ? theme_manager_parse_hex_color(focus_str) : lv_color_hex(0x5e81ac);
    // Default to primary accent color if primary token not available
    lv_color_t primary_color =
        primary_str ? theme_manager_parse_hex_color(primary_str) : lv_color_hex(0x5e81ac);
    // Default to surface_control if border token not available
    lv_color_t border_color =
        border_str ? theme_manager_parse_hex_color(border_str) : surface_control;

    spdlog::debug("[Theme] New colors: screen={}, card={}, surface={}, text={}", screen_bg_str,
                  card_bg_str, surface_control_str, text_primary_str);

    // Update helix theme styles in-place (triggers lv_obj_report_style_change)
    theme_core_update_colors(new_use_dark_mode, screen_bg, card_bg, surface_control,
                             text_primary_color, focus_color, primary_color, border_color);

    // Force style refresh on entire widget tree for local/inline styles
    theme_manager_refresh_widget_tree(lv_screen_active());

    // Invalidate screen to trigger redraw
    lv_obj_invalidate(lv_screen_active());

    spdlog::info("[Theme] Theme toggle complete");
}

bool theme_manager_is_dark_mode() {
    return use_dark_mode;
}

const helix::ThemeData& theme_manager_get_active_theme() {
    return active_theme;
}

helix::ThemeModeSupport theme_manager_get_mode_support() {
    return active_theme.get_mode_support();
}

bool theme_manager_supports_dark_mode() {
    return active_theme.supports_dark();
}

bool theme_manager_supports_light_mode() {
    return active_theme.supports_light();
}

void theme_manager_preview(const helix::ThemeData& theme) {
    const char* colors[16];
    for (size_t i = 0; i < 16; ++i) {
        colors[i] = theme.colors.at(i).c_str();
    }

    theme_core_preview_colors(use_dark_mode, colors, theme.properties.border_radius);
    theme_manager_refresh_widget_tree(lv_screen_active());

    spdlog::debug("[Theme] Previewing theme: {}", theme.name);
}

void theme_manager_revert_preview() {
    theme_manager_preview(active_theme);
    spdlog::debug("[Theme] Reverted to active theme: {}", active_theme.name);
}

/**
 * Get theme-appropriate color variant with fallback for static colors
 *
 * First attempts to look up {base_name}_light and {base_name}_dark from globals.xml,
 * selecting the appropriate one based on current theme mode. If the theme variants
 * don't exist, falls back to {base_name} directly (for static colors like
 * warning_color, error_color that are the same in both themes).
 *
 * @param base_name Color constant base name (e.g., "app_bg_color", "warning_color")
 * @return Parsed color, or black (0x000000) if not found
 *
 * Example:
 *   lv_color_t bg = theme_manager_get_color("app_bg_color");
 *   // Returns app_bg_color_light in light mode, app_bg_color_dark in dark mode
 *
 *   lv_color_t warn = theme_manager_get_color("warning_color");
 *   // Returns warning_color directly (static, no theme variants)
 */
lv_color_t theme_manager_get_color(const char* base_name) {
    if (!base_name) {
        spdlog::error("[Theme] theme_manager_get_color: NULL base_name");
        return lv_color_hex(0x000000);
    }

    // Construct variant names: {base_name}_light and {base_name}_dark
    char light_name[128];
    char dark_name[128];
    snprintf(light_name, sizeof(light_name), "%s_light", base_name);
    snprintf(dark_name, sizeof(dark_name), "%s_dark", base_name);

    // Use silent lookups to avoid LVGL warnings when probing for variants
    // Pattern 1: Theme-aware color with _light/_dark variants
    const char* light_str = lv_xml_get_const_silent(nullptr, light_name);
    const char* dark_str = lv_xml_get_const_silent(nullptr, dark_name);

    if (light_str && dark_str) {
        // Both variants exist - use theme-appropriate one
        return theme_manager_parse_hex_color(use_dark_mode ? dark_str : light_str);
    }

    // Pattern 2: Static color with just base name (no variants)
    const char* base_str = lv_xml_get_const_silent(nullptr, base_name);
    if (base_str) {
        return theme_manager_parse_hex_color(base_str);
    }

    // Pattern 3: Partial variants (error case)
    if (light_str || dark_str) {
        spdlog::error("[Theme] Color {} has only one variant (_light or _dark), need both",
                      base_name);
        return lv_color_hex(0x000000);
    }

    // Nothing found
    spdlog::error("[Theme] Color not found: {} (no base, no _light/_dark variants)", base_name);
    return lv_color_hex(0x000000);
}

/**
 * Apply theme-appropriate background color to object
 *
 * Convenience wrapper that gets the color variant and applies it to the object.
 *
 * @param obj LVGL object to apply color to
 * @param base_name Color constant base name (e.g., "app_bg_color", "card_bg")
 * @param part Style part to apply to (default: LV_PART_MAIN)
 *
 * Example:
 *   theme_manager_apply_bg_color(screen, "app_bg_color", LV_PART_MAIN);
 *   // Applies app_bg_color_light/dark depending on theme mode
 */
void theme_manager_apply_bg_color(lv_obj_t* obj, const char* base_name, lv_part_t part) {
    if (!obj) {
        spdlog::error("[Theme] theme_manager_apply_bg_color: NULL object");
        return;
    }

    lv_color_t color = theme_manager_get_color(base_name);
    lv_obj_set_style_bg_color(obj, color, part);
}

/**
 * Get font line height in pixels
 *
 * Returns the total vertical space a line of text will occupy for the given font.
 * This includes ascender, descender, and line gap. Useful for calculating layout
 * heights before widgets are created.
 *
 * @param font Font to query (e.g., theme_manager_get_font("font_heading"), &noto_sans_16)
 * @return Line height in pixels, or 0 if font is NULL
 *
 * Examples:
 *   int32_t heading_h = theme_manager_get_font_height(theme_manager_get_font("font_heading"));
 *   int32_t body_h = theme_manager_get_font_height(theme_manager_get_font("font_body"));
 *   int32_t small_h = theme_manager_get_font_height(theme_manager_get_font("font_small"));
 *
 *   // Calculate total height for multi-line layout
 *   int32_t total = theme_manager_get_font_height(theme_manager_get_font("font_heading")) +
 *                   (theme_manager_get_font_height(theme_manager_get_font("font_body")) * 3) +
 *                   (4 * 8);  // 4 gaps of 8px padding
 */
int32_t theme_manager_get_font_height(const lv_font_t* font) {
    if (!font) {
        spdlog::warn("[Theme] theme_manager_get_font_height: NULL font pointer");
        return 0;
    }

    return lv_font_get_line_height(font);
}

void ui_set_overlay_width(lv_obj_t* obj, lv_obj_t* screen) {
    if (!obj || !screen) {
        spdlog::warn("[Theme] ui_set_overlay_width: NULL pointer");
        return;
    }

    // Use registered overlay_panel_width constant (consistent with XML overlays)
    const char* width_str = lv_xml_get_const(nullptr, "overlay_panel_width");
    if (width_str) {
        lv_obj_set_width(obj, std::atoi(width_str));
    } else {
        // Fallback if theme not initialized: calculate from screen size
        lv_coord_t screen_width = lv_obj_get_width(screen);
        lv_coord_t nav_width = UI_NAV_WIDTH(screen_width);
        lv_obj_set_width(obj, screen_width - nav_width - 16); // 16px gap fallback
        spdlog::warn("[Theme] overlay_panel_width not registered, using fallback");
    }
}

/**
 * Get spacing value from unified space_* system
 *
 * Reads the registered space_* constant value from LVGL's XML constant registry.
 * The value returned is responsive - it depends on what breakpoint was used
 * during theme initialization (small/medium/large).
 *
 * This function is the C++ interface to the unified spacing system, replacing
 * the old hardcoded UI_PADDING_* constants. All spacing in C++ code should now
 * use this function to stay consistent with XML layouts.
 *
 * Available tokens and their responsive values:
 *   space_xxs: 2/3/4px  (small/medium/large)
 *   space_xs:  4/5/6px
 *   space_sm:  6/7/8px
 *   space_md:  8/10/12px
 *   space_lg:  12/16/20px
 *   space_xl:  16/20/24px
 *   space_2xl: 24/32/40px
 *
 * @param token Spacing token name (e.g., "space_lg", "space_md", "space_xs")
 * @return Spacing value in pixels, or 0 if token not found
 *
 * Example:
 *   lv_obj_set_style_pad_all(obj, theme_manager_get_spacing("space_lg"), 0);
 */
int32_t theme_manager_get_spacing(const char* token) {
    if (!token) {
        spdlog::warn("[Theme] theme_manager_get_spacing: NULL token");
        return 0;
    }

    const char* value = lv_xml_get_const(NULL, token);
    if (!value) {
        spdlog::warn("[Theme] Spacing token '{}' not found - is theme initialized?", token);
        return 0;
    }

    return std::atoi(value);
}

/**
 * Get responsive font by token name
 *
 * Looks up the font token (e.g., "font_small") which was registered during
 * theme init with the appropriate breakpoint variant value (e.g., "noto_sans_16"),
 * then retrieves the actual font pointer.
 *
 * @param token Font token name (e.g., "font_small", "font_body", "font_heading")
 * @return Font pointer, or nullptr if not found
 */
const lv_font_t* theme_manager_get_font(const char* token) {
    if (!token) {
        spdlog::warn("[Theme] theme_manager_get_font: NULL token");
        return nullptr;
    }

    // Get the font name from the registered constant (e.g., "font_small" -> "noto_sans_16")
    const char* font_name = lv_xml_get_const(nullptr, token);
    if (!font_name) {
        spdlog::warn("[Theme] Font token '{}' not found - is theme initialized?", token);
        return nullptr;
    }

    // Get the actual font pointer
    const lv_font_t* font = lv_xml_get_font(nullptr, font_name);
    if (!font) {
        spdlog::warn("[Theme] Font '{}' (from token '{}') not registered", font_name, token);
        return nullptr;
    }

    return font;
}

const char* theme_manager_size_to_font_token(const char* size, const char* default_size) {
    const char* effective_size = size ? size : default_size;
    if (!effective_size) {
        effective_size = "sm"; // Fallback if both are null
    }

    if (strcmp(effective_size, "xs") == 0) {
        return "font_xs";
    } else if (strcmp(effective_size, "sm") == 0) {
        return "font_small";
    } else if (strcmp(effective_size, "md") == 0) {
        return "font_body";
    } else if (strcmp(effective_size, "lg") == 0) {
        return "font_heading";
    }

    // Unknown size - warn and return default
    spdlog::warn("[Theme] Unknown size '{}', using default '{}'", effective_size, default_size);
    return theme_manager_size_to_font_token(default_size, "sm");
}

// ============================================================================
// Multi-File Responsive Constants
// ============================================================================
// Extension of responsive constants (_small/_medium/_large) to work with ALL
// XML files, not just globals.xml. This allows component-specific responsive
// tokens to be defined in their respective XML files.

// Expat callback data for extracting name→value pairs with a specific suffix
struct SuffixValueParserData {
    const char* element_type;                              // "color", "px", or "string"
    const char* suffix;                                    // "_light", "_small", etc.
    std::unordered_map<std::string, std::string>* results; // Output: base_name → value
};

// Helper: check if string ends with suffix
static bool ends_with_suffix(const char* str, const char* suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (str_len < suffix_len)
        return false;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

// Parser callback for ALL elements of a given type (no suffix matching)
struct AllElementParserData {
    const char* element_type;
    std::unordered_map<std::string, std::string>* token_values;
};

static void XMLCALL all_element_start(void* userData, const XML_Char* name, const XML_Char** atts) {
    auto* data = static_cast<AllElementParserData*>(userData);
    if (strcmp(name, data->element_type) != 0)
        return;

    const char* elem_name = nullptr;
    const char* elem_value = nullptr;
    for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "name") == 0)
            elem_name = atts[i + 1];
        else if (strcmp(atts[i], "value") == 0)
            elem_value = atts[i + 1];
    }
    if (elem_name && elem_value) {
        (*data->token_values)[elem_name] = elem_value;
    }
}

// Expat element start handler - extracts name and value for matching elements
static void XMLCALL suffix_value_element_start(void* user_data, const XML_Char* name,
                                               const XML_Char** attrs) {
    SuffixValueParserData* data = static_cast<SuffixValueParserData*>(user_data);

    if (strcmp(name, data->element_type) != 0)
        return;

    // Extract both name and value attributes
    const char* const_name = nullptr;
    const char* const_value = nullptr;
    for (int i = 0; attrs[i]; i += 2) {
        if (strcmp(attrs[i], "name") == 0)
            const_name = attrs[i + 1];
        if (strcmp(attrs[i], "value") == 0)
            const_value = attrs[i + 1];
    }

    // Skip if either attribute is missing
    if (!const_name || !const_value)
        return;

    // Check if name ends with the target suffix
    if (ends_with_suffix(const_name, data->suffix)) {
        // Extract base name (without suffix)
        size_t base_len = strlen(const_name) - strlen(data->suffix);
        std::string base_name(const_name, base_len);

        // Store in results (overwrites any existing value - last-wins)
        (*data->results)[base_name] = const_value;
    }
}

void theme_manager_parse_xml_file_for_all(
    const char* filepath, const char* element_type,
    std::unordered_map<std::string, std::string>& token_values) {
    if (!filepath)
        return;

    std::ifstream file(filepath);
    if (!file.is_open())
        return;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string xml_content = buffer.str();
    file.close();
    if (xml_content.empty())
        return;

    AllElementParserData parser_data = {element_type, &token_values};
    XML_Parser parser = XML_ParserCreate(nullptr);
    if (!parser)
        return;

    XML_SetUserData(parser, &parser_data);
    XML_SetElementHandler(parser, all_element_start, nullptr);
    XML_Parse(parser, xml_content.c_str(), static_cast<int>(xml_content.size()), XML_TRUE);
    XML_ParserFree(parser);
}

void theme_manager_parse_xml_file_for_suffix(
    const char* filepath, const char* element_type, const char* suffix,
    std::unordered_map<std::string, std::string>& token_values) {
    // Handle NULL filepath gracefully
    if (!filepath) {
        spdlog::trace("[Theme] parse_xml_file_for_suffix: NULL filepath");
        return;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        spdlog::trace("[Theme] Could not open {} for suffix parsing", filepath);
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string xml_content = buffer.str();
    file.close();

    // Handle empty file
    if (xml_content.empty()) {
        return;
    }

    SuffixValueParserData parser_data = {element_type, suffix, &token_values};
    XML_Parser parser = XML_ParserCreate(nullptr);
    if (!parser) {
        spdlog::error("[Theme] Failed to create XML parser for {}", filepath);
        return;
    }
    XML_SetUserData(parser, &parser_data);
    XML_SetElementHandler(parser, suffix_value_element_start, nullptr);

    if (XML_Parse(parser, xml_content.c_str(), static_cast<int>(xml_content.size()), XML_TRUE) ==
        XML_STATUS_ERROR) {
        spdlog::trace("[Theme] XML parse error in {} line {}: {}", filepath,
                      XML_GetCurrentLineNumber(parser), XML_ErrorString(XML_GetErrorCode(parser)));
        // Continue with partial results (don't clear token_values)
    }
    XML_ParserFree(parser);
}

std::vector<std::string> theme_manager_find_xml_files(const char* directory) {
    std::vector<std::string> result;

    // Handle NULL directory gracefully
    if (!directory) {
        spdlog::trace("[Theme] find_xml_files: NULL directory");
        return result;
    }

    DIR* dir = opendir(directory);
    if (!dir) {
        spdlog::trace("[Theme] Could not open directory: {}", directory);
        return result;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;

        // Skip directories (including . and ..)
        if (entry->d_type == DT_DIR)
            continue;

        // Skip suspicious filenames (path traversal defense)
        if (filename.find('/') != std::string::npos || filename.find("..") != std::string::npos) {
            continue;
        }

        // Check if file ends with .xml (case-sensitive, lowercase only)
        if (filename.length() > 4 && filename.substr(filename.length() - 4) == ".xml") {
            std::string full_path = std::string(directory) + "/" + filename;
            result.push_back(full_path);
        }
    }
    closedir(dir);

    // Sort alphabetically for deterministic ordering (needed for last-wins)
    std::sort(result.begin(), result.end());

    return result;
}

std::unordered_map<std::string, std::string>
theme_manager_parse_all_xml_for_element(const char* directory, const char* element_type) {
    std::unordered_map<std::string, std::string> token_values;
    std::vector<std::string> files = theme_manager_find_xml_files(directory);
    for (const auto& filepath : files) {
        theme_manager_parse_xml_file_for_all(filepath.c_str(), element_type, token_values);
    }
    return token_values;
}

std::unordered_map<std::string, std::string>
theme_manager_parse_all_xml_for_suffix(const char* directory, const char* element_type,
                                       const char* suffix) {
    std::unordered_map<std::string, std::string> token_values;

    // Get sorted list of all XML files
    std::vector<std::string> files = theme_manager_find_xml_files(directory);

    // Parse each file in alphabetical order (last-wins via map overwrite)
    for (const auto& filepath : files) {
        theme_manager_parse_xml_file_for_suffix(filepath.c_str(), element_type, suffix,
                                                token_values);
    }

    return token_values;
}

// Helper to check if a string looks like a hex color value
static bool is_hex_color_value(const std::string& value) {
    if (value.empty())
        return false;

    // Must start with a digit or be 3/6/8 hex chars
    // Hex colors: RGB (3), RRGGBB (6), or AARRGGBB (8)
    size_t len = value.length();
    if (len != 3 && len != 6 && len != 8)
        return false;

    // All characters must be hex digits
    for (char c : value) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    }

    return true;
}

// Expat callback data for extracting constant references from attribute values
struct ConstantRefParserData {
    std::string current_file;
    std::vector<std::tuple<std::string, std::string, std::string>>* refs; // constant, file, attr
};

static void XMLCALL constant_ref_element_start(void* user_data, const XML_Char* name,
                                               const XML_Char** attrs) {
    (void)name;
    auto* data = static_cast<ConstantRefParserData*>(user_data);

    // Scan all attributes for constant references (pattern: ="# ... ")
    for (int i = 0; attrs[i]; i += 2) {
        const char* attr_name = attrs[i];
        const char* attr_value = attrs[i + 1];

        if (!attr_value || attr_value[0] != '#')
            continue;

        // Extract constant name (everything after # until end of string)
        std::string const_name(attr_value + 1);

        // Skip hex color values (start with digit or are 3/6/8 hex chars)
        if (is_hex_color_value(const_name))
            continue;

        data->refs->emplace_back(const_name, data->current_file, std::string(attr_name));
    }
}

// Parse XML file for constant references in attribute values
static void theme_manager_parse_xml_file_for_refs(
    const char* filepath, std::vector<std::tuple<std::string, std::string, std::string>>& refs) {
    if (!filepath)
        return;

    std::ifstream file(filepath);
    if (!file.is_open())
        return;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string xml_content = buffer.str();
    file.close();

    if (xml_content.empty())
        return;

    // Extract just the filename for error messages
    std::string filepath_str(filepath);
    std::string filename = filepath_str;
    size_t slash_pos = filepath_str.rfind('/');
    if (slash_pos != std::string::npos) {
        filename = filepath_str.substr(slash_pos + 1);
    }

    ConstantRefParserData parser_data = {filename, &refs};
    XML_Parser parser = XML_ParserCreate(nullptr);
    if (!parser)
        return;

    XML_SetUserData(parser, &parser_data);
    XML_SetElementHandler(parser, constant_ref_element_start, nullptr);

    XML_Parse(parser, xml_content.c_str(), static_cast<int>(xml_content.size()), XML_TRUE);
    XML_ParserFree(parser);
}

std::vector<std::string> theme_manager_validate_constant_sets(const char* directory) {
    std::vector<std::string> warnings;

    if (!directory) {
        return warnings;
    }

    // Validate responsive px sets (_small/_medium/_large)
    {
        auto small_tokens = theme_manager_parse_all_xml_for_suffix(directory, "px", "_small");
        auto medium_tokens = theme_manager_parse_all_xml_for_suffix(directory, "px", "_medium");
        auto large_tokens = theme_manager_parse_all_xml_for_suffix(directory, "px", "_large");

        // Collect all base names that have at least one responsive suffix
        std::unordered_map<std::string, int> base_names;
        for (const auto& [name, _] : small_tokens) {
            base_names[name] |= 1; // bit 0 = _small
        }
        for (const auto& [name, _] : medium_tokens) {
            base_names[name] |= 2; // bit 1 = _medium
        }
        for (const auto& [name, _] : large_tokens) {
            base_names[name] |= 4; // bit 2 = _large
        }

        // Check for incomplete sets
        for (const auto& [base_name, flags] : base_names) {
            if (flags != 7) { // Not all three present (111 in binary)
                std::vector<std::string> found;
                std::vector<std::string> missing;

                if (flags & 1)
                    found.push_back("_small");
                else
                    missing.push_back("_small");

                if (flags & 2)
                    found.push_back("_medium");
                else
                    missing.push_back("_medium");

                if (flags & 4)
                    found.push_back("_large");
                else
                    missing.push_back("_large");

                std::string found_str;
                for (size_t i = 0; i < found.size(); ++i) {
                    if (i > 0)
                        found_str += ", ";
                    found_str += found[i];
                }

                std::string missing_str;
                for (size_t i = 0; i < missing.size(); ++i) {
                    if (i > 0)
                        missing_str += ", ";
                    missing_str += missing[i];
                }

                warnings.push_back("Incomplete responsive set for '" + base_name + "': found " +
                                   found_str + " but missing " + missing_str);
            }
        }
    }

    // Validate themed color pairs (_light/_dark)
    {
        auto light_tokens = theme_manager_parse_all_xml_for_suffix(directory, "color", "_light");
        auto dark_tokens = theme_manager_parse_all_xml_for_suffix(directory, "color", "_dark");

        // Collect all base names that have at least one theme suffix
        std::unordered_map<std::string, int> base_names;
        for (const auto& [name, _] : light_tokens) {
            base_names[name] |= 1; // bit 0 = _light
        }
        for (const auto& [name, _] : dark_tokens) {
            base_names[name] |= 2; // bit 1 = _dark
        }

        // Check for incomplete pairs
        for (const auto& [base_name, flags] : base_names) {
            if (flags != 3) { // Not both present (11 in binary)
                if (flags == 1) {
                    warnings.push_back("Incomplete theme pair for '" + base_name +
                                       "': found _light but missing _dark");
                } else if (flags == 2) {
                    warnings.push_back("Incomplete theme pair for '" + base_name +
                                       "': found _dark but missing _light");
                }
            }
        }
    }

    // ========================================================================
    // Validate undefined constant references
    // ========================================================================
    {
        // Whitelist of constants registered in C++ (not XML) or work-in-progress
        static const std::unordered_set<std::string> cpp_registered_constants = {
            // Registered dynamically in theme_manager_register_responsive_spacing()
            "nav_width",
            "overlay_panel_width",
            "overlay_panel_width_full",
            // WIP wizard constants (user actively working on these)
            "wizard_footer_height",
            "wizard_button_width",
        };

        // Step 1: Collect all defined constants from all element types
        std::unordered_set<std::string> defined_constants;

        // Direct definitions (px, color, string, str, percentage)
        for (const auto& [name, _] : theme_manager_parse_all_xml_for_element(directory, "px")) {
            defined_constants.insert(name);
        }
        for (const auto& [name, _] : theme_manager_parse_all_xml_for_element(directory, "color")) {
            defined_constants.insert(name);
        }
        for (const auto& [name, _] : theme_manager_parse_all_xml_for_element(directory, "string")) {
            defined_constants.insert(name);
        }
        for (const auto& [name, _] : theme_manager_parse_all_xml_for_element(directory, "str")) {
            defined_constants.insert(name);
        }
        for (const auto& [name, _] :
             theme_manager_parse_all_xml_for_element(directory, "percentage")) {
            defined_constants.insert(name);
        }
        for (const auto& [name, _] : theme_manager_parse_all_xml_for_element(directory, "int")) {
            defined_constants.insert(name);
        }

        // Step 2: Add base names for responsive constants (_small/_medium/_large -> base)
        // These get registered at runtime as the base name
        auto small_px = theme_manager_parse_all_xml_for_suffix(directory, "px", "_small");
        auto medium_px = theme_manager_parse_all_xml_for_suffix(directory, "px", "_medium");
        auto large_px = theme_manager_parse_all_xml_for_suffix(directory, "px", "_large");
        for (const auto& [base_name, _] : small_px) {
            if (medium_px.count(base_name) && large_px.count(base_name)) {
                defined_constants.insert(base_name);
            }
        }

        auto small_str = theme_manager_parse_all_xml_for_suffix(directory, "string", "_small");
        auto medium_str = theme_manager_parse_all_xml_for_suffix(directory, "string", "_medium");
        auto large_str = theme_manager_parse_all_xml_for_suffix(directory, "string", "_large");
        for (const auto& [base_name, _] : small_str) {
            if (medium_str.count(base_name) && large_str.count(base_name)) {
                defined_constants.insert(base_name);
            }
        }

        // Step 3: Add base names for themed colors (_light/_dark -> base)
        auto light_colors = theme_manager_parse_all_xml_for_suffix(directory, "color", "_light");
        auto dark_colors = theme_manager_parse_all_xml_for_suffix(directory, "color", "_dark");
        for (const auto& [base_name, _] : light_colors) {
            if (dark_colors.count(base_name)) {
                defined_constants.insert(base_name);
            }
        }

        // Step 4: Scan all XML files for constant references
        std::vector<std::tuple<std::string, std::string, std::string>> refs;
        auto files = theme_manager_find_xml_files(directory);
        for (const auto& filepath : files) {
            theme_manager_parse_xml_file_for_refs(filepath.c_str(), refs);
        }

        // Step 5: Check each reference against defined constants
        for (const auto& [const_name, filename, attr_name] : refs) {
            // Skip whitelisted constants (registered in C++ or WIP)
            if (cpp_registered_constants.count(const_name)) {
                continue;
            }
            if (defined_constants.find(const_name) == defined_constants.end()) {
                warnings.push_back("Undefined constant '#" + const_name + "' in " + filename +
                                   " (attribute: " + attr_name + ")");
            }
        }
    }

    return warnings;
}
