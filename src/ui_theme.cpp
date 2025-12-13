// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_theme.h"

#include "ui_error_reporting.h"
#include "ui_fonts.h"

#include "helix_theme.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/libs/expat/expat.h"
#include "lvgl/src/xml/lv_xml.h"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static lv_theme_t* current_theme = nullptr;
static bool use_dark_mode = true;
static lv_display_t* theme_display = nullptr;

// Parse hex color string "#FF4444" -> lv_color_hex(0xFF4444)
lv_color_t ui_theme_parse_color(const char* hex_str) {
    if (!hex_str || hex_str[0] != '#') {
        spdlog::error("[Theme] Invalid hex color string: {}", hex_str ? hex_str : "NULL");
        return lv_color_hex(0x000000);
    }
    uint32_t hex = static_cast<uint32_t>(strtoul(hex_str + 1, NULL, 16));
    return lv_color_hex(hex);
}

// No longer needed - helix_theme.c handles all color patching and input widget styling

// ============================================================================
// Responsive Token Pattern Matching
// ============================================================================
// Unified system for discovering responsive tokens from globals.xml by suffix:
// - Colors: xxx_light / xxx_dark -> xxx (theme mode selects variant)
// - Spacing: xxx_small / xxx_medium / xxx_large -> xxx (breakpoint selects variant)
// - Fonts: xxx_small / xxx_medium / xxx_large -> xxx (breakpoint selects variant)

// Expat callback data for collecting base names with a specific suffix
struct SuffixParserData {
    const char* element_type;            // "color", "px", or "string"
    const char* suffix;                  // "_light", "_small", etc.
    std::vector<std::string> base_names; // Collected base names (without suffix)
};

// Generic expat element start handler - finds <element_type name="xxx{suffix}"> elements
static void XMLCALL suffix_element_start(void* user_data, const XML_Char* name,
                                         const XML_Char** attrs) {
    SuffixParserData* data = static_cast<SuffixParserData*>(user_data);

    if (strcmp(name, data->element_type) != 0)
        return;

    // Find the "name" attribute
    for (int i = 0; attrs[i]; i += 2) {
        if (strcmp(attrs[i], "name") == 0) {
            const char* attr_name = attrs[i + 1];
            size_t len = strlen(attr_name);
            size_t suffix_len = strlen(data->suffix);

            // Check if name ends with the target suffix
            if (len > suffix_len && strcmp(attr_name + len - suffix_len, data->suffix) == 0) {
                // Extract base name (without suffix)
                std::string base_name(attr_name, len - suffix_len);
                data->base_names.push_back(base_name);
            }
            break;
        }
    }
}

/**
 * Parse globals.xml and collect base names for elements with a given suffix
 *
 * @param element_type XML element type ("color", "px", "string")
 * @param suffix Suffix to match ("_light", "_small", etc.)
 * @return Vector of base names (without suffix)
 */
static std::vector<std::string> parse_globals_for_suffix(const char* element_type,
                                                         const char* suffix) {
    std::vector<std::string> result;

    std::ifstream file("ui_xml/globals.xml");
    if (!file.is_open()) {
        NOTIFY_ERROR("Could not open ui_xml/globals.xml for {} pattern matching", element_type);
        return result;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string xml_content = buffer.str();
    file.close();

    SuffixParserData parser_data = {element_type, suffix, {}};
    XML_Parser parser = XML_ParserCreate(nullptr);
    XML_SetUserData(parser, &parser_data);
    XML_SetElementHandler(parser, suffix_element_start, nullptr);

    if (XML_Parse(parser, xml_content.c_str(), static_cast<int>(xml_content.size()), XML_TRUE) ==
        XML_STATUS_ERROR) {
        NOTIFY_ERROR("XML parse error in globals.xml line {}: {}", XML_GetCurrentLineNumber(parser),
                     XML_ErrorString(XML_GetErrorCode(parser)));
        XML_ParserFree(parser);
        return result;
    }
    XML_ParserFree(parser);

    return parser_data.base_names;
}

/**
 * Auto-register theme-aware color constants from globals.xml
 *
 * Parses globals.xml to find color pairs (xxx_light, xxx_dark) and registers
 * the base name (xxx) as a runtime constant with the appropriate value
 * based on current theme mode.
 */
static void ui_theme_register_color_pairs(lv_xml_component_scope_t* scope, bool dark_mode) {
    // Find all color base names that have _light suffix
    std::vector<std::string> base_names = parse_globals_for_suffix("color", "_light");

    // For each _light color, check if _dark exists and register base name
    int registered = 0;
    for (const auto& base_name : base_names) {
        std::string light_name = base_name + "_light";
        std::string dark_name = base_name + "_dark";

        const char* light_val = lv_xml_get_const(nullptr, light_name.c_str());
        const char* dark_val = lv_xml_get_const(nullptr, dark_name.c_str());

        if (light_val && dark_val) {
            const char* selected = dark_mode ? dark_val : light_val;
            spdlog::trace("[Theme] Registering color {}: selected={}", base_name, selected);
            lv_xml_register_const(scope, base_name.c_str(), selected);
            registered++;
        }
    }

    spdlog::debug("[Theme] Auto-registered {} theme-aware color pairs (dark_mode={})", registered,
                  dark_mode);
}

/**
 * Get the breakpoint suffix for a given resolution
 *
 * @param max_resolution The maximum of horizontal and vertical resolution
 * @return Suffix string: "_small" (≤480), "_medium" (481-800), or "_large" (>800)
 */
const char* ui_theme_get_breakpoint_suffix(int32_t max_resolution) {
    if (max_resolution <= UI_BREAKPOINT_SMALL_MAX) {
        return "_small";
    } else if (max_resolution <= UI_BREAKPOINT_MEDIUM_MAX) {
        return "_medium";
    } else {
        return "_large";
    }
}

/**
 * Register responsive spacing tokens from globals.xml
 *
 * Auto-discovers all <px name="xxx_small"> elements and registers base tokens
 * by matching xxx_small/xxx_medium/xxx_large triplets. This makes the system
 * fully extensible from globals.xml without C++ code changes.
 *
 * CRITICAL: Base tokens must NOT be defined in globals.xml or responsive
 * overrides will be silently ignored (LVGL ignores duplicate lv_xml_register_const).
 *
 * @param display The LVGL display to get resolution from
 */
void ui_theme_register_responsive_spacing(lv_display_t* display) {
    int32_t hor_res = lv_display_get_horizontal_resolution(display);
    int32_t ver_res = lv_display_get_vertical_resolution(display);
    int32_t greater_res = LV_MAX(hor_res, ver_res);

    const char* size_suffix = ui_theme_get_breakpoint_suffix(greater_res);
    const char* size_label = (greater_res <= UI_BREAKPOINT_SMALL_MAX)    ? "SMALL"
                             : (greater_res <= UI_BREAKPOINT_MEDIUM_MAX) ? "MEDIUM"
                                                                         : "LARGE";

    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("globals");
    if (!scope) {
        spdlog::warn("[Theme] Failed to get globals scope for spacing constants");
        return;
    }

    // Auto-discover all px tokens with _small suffix
    std::vector<std::string> base_names = parse_globals_for_suffix("px", "_small");

    int registered = 0;
    for (const auto& base_name : base_names) {
        // Verify all three variants exist
        std::string small_name = base_name + "_small";
        std::string medium_name = base_name + "_medium";
        std::string large_name = base_name + "_large";

        const char* small_val = lv_xml_get_const(nullptr, small_name.c_str());
        const char* medium_val = lv_xml_get_const(nullptr, medium_name.c_str());
        const char* large_val = lv_xml_get_const(nullptr, large_name.c_str());

        if (small_val && medium_val && large_val) {
            // Select appropriate variant based on breakpoint
            std::string variant_name = base_name + size_suffix;
            const char* value = lv_xml_get_const(nullptr, variant_name.c_str());
            if (value) {
                spdlog::trace("[Theme] Registering spacing {}: selected={}", base_name, value);
                lv_xml_register_const(scope, base_name.c_str(), value);
                registered++;
            }
        }
    }

    spdlog::debug("[Theme] Responsive spacing: {} ({}px) - auto-registered {} tokens", size_label,
                  greater_res, registered);
}

/**
 * Register responsive font tokens from globals.xml
 *
 * Auto-discovers all <string name="xxx_small"> elements and registers base tokens
 * by matching xxx_small/xxx_medium/xxx_large triplets. This makes the system
 * fully extensible from globals.xml without C++ code changes.
 *
 * @param display The LVGL display to get resolution from
 */
void ui_theme_register_responsive_fonts(lv_display_t* display) {
    int32_t hor_res = lv_display_get_horizontal_resolution(display);
    int32_t ver_res = lv_display_get_vertical_resolution(display);
    int32_t greater_res = LV_MAX(hor_res, ver_res);

    const char* size_suffix = ui_theme_get_breakpoint_suffix(greater_res);
    const char* size_label = (greater_res <= UI_BREAKPOINT_SMALL_MAX)    ? "SMALL"
                             : (greater_res <= UI_BREAKPOINT_MEDIUM_MAX) ? "MEDIUM"
                                                                         : "LARGE";

    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("globals");
    if (!scope) {
        spdlog::warn("[Theme] Failed to get globals scope for font constants");
        return;
    }

    // Auto-discover all string tokens with _small suffix
    std::vector<std::string> base_names = parse_globals_for_suffix("string", "_small");

    int registered = 0;
    for (const auto& base_name : base_names) {
        // Verify all three variants exist
        std::string small_name = base_name + "_small";
        std::string medium_name = base_name + "_medium";
        std::string large_name = base_name + "_large";

        const char* small_val = lv_xml_get_const(nullptr, small_name.c_str());
        const char* medium_val = lv_xml_get_const(nullptr, medium_name.c_str());
        const char* large_val = lv_xml_get_const(nullptr, large_name.c_str());

        if (small_val && medium_val && large_val) {
            // Select appropriate variant based on breakpoint
            std::string variant_name = base_name + size_suffix;
            const char* value = lv_xml_get_const(nullptr, variant_name.c_str());
            if (value) {
                spdlog::trace("[Theme] Registering font {}: selected={}", base_name, value);
                lv_xml_register_const(scope, base_name.c_str(), value);
                registered++;
            }
        }
    }

    spdlog::debug("[Theme] Responsive fonts: {} ({}px) - auto-registered {} tokens", size_label,
                  greater_res, registered);
}

void ui_theme_init(lv_display_t* display, bool use_dark_mode_param) {
    theme_display = display;
    use_dark_mode = use_dark_mode_param;

    // Override runtime theme constants based on light/dark mode preference
    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("globals");
    if (!scope) {
        spdlog::critical(
            "[Theme] FATAL: Failed to get globals scope for runtime constant registration");
        std::exit(EXIT_FAILURE);
    }

    // Auto-register all color pairs from globals.xml (xxx_light/xxx_dark -> xxx)
    // This handles app_bg_color, text_primary, header_text, theme_grey, card_bg, etc.
    ui_theme_register_color_pairs(scope, use_dark_mode);

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

    lv_color_t primary_color = ui_theme_parse_color(primary_str);
    lv_color_t secondary_color = ui_theme_parse_color(secondary_str);

    // Read responsive font based on current breakpoint
    // NOTE: We read the variant directly because base constants are removed to enable
    // responsive overrides (LVGL ignores lv_xml_register_const for existing constants)
    int32_t hor_res = lv_display_get_horizontal_resolution(display);
    int32_t ver_res = lv_display_get_vertical_resolution(display);
    int32_t greater_res = LV_MAX(hor_res, ver_res);
    const char* size_suffix = ui_theme_get_breakpoint_suffix(greater_res);

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
    const char* theme_grey_str = lv_xml_get_const(nullptr, "theme_grey");
    const char* text_primary_str = lv_xml_get_const(nullptr, "text_primary");

    if (!screen_bg_str || !card_bg_str || !theme_grey_str || !text_primary_str) {
        spdlog::error("[Theme] Failed to read auto-registered color constants");
        return;
    }

    lv_color_t screen_bg = ui_theme_parse_color(screen_bg_str);
    lv_color_t card_bg = ui_theme_parse_color(card_bg_str);
    lv_color_t theme_grey = ui_theme_parse_color(theme_grey_str);
    lv_color_t text_primary_color = ui_theme_parse_color(text_primary_str);

    // Read border radius from globals.xml
    const char* border_radius_str = lv_xml_get_const(nullptr, "border_radius");
    if (!border_radius_str) {
        spdlog::error("[Theme] Failed to read border_radius from globals.xml");
        return;
    }
    int32_t border_radius = atoi(border_radius_str);

    // Initialize custom HelixScreen theme (wraps LVGL default theme)
    current_theme =
        helix_theme_init(display, primary_color, secondary_color, text_primary_color, use_dark_mode,
                         base_font, screen_bg, card_bg, theme_grey, border_radius);

    if (current_theme) {
        lv_display_set_theme(display, current_theme);
        spdlog::info("[Theme] Initialized HelixScreen theme: {} mode",
                     use_dark_mode ? "dark" : "light");
        spdlog::debug("[Theme] Colors: primary={}, secondary={}, screen={}, card={}, grey={}",
                      primary_str, secondary_str, screen_bg_str, card_bg_str, theme_grey_str);

        // Register responsive constants AFTER theme init
        ui_theme_register_responsive_spacing(display);
        ui_theme_register_responsive_fonts(display);
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

void ui_theme_refresh_widget_tree(lv_obj_t* root) {
    if (!root)
        return;

    // Walk entire tree and refresh each widget's styles
    lv_obj_tree_walk(root, refresh_style_cb, nullptr);
}

void ui_theme_toggle_dark_mode() {
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

    auto get_themed_color = [suffix](const char* base_name) -> const char* {
        char full_name[128];
        snprintf(full_name, sizeof(full_name), "%s%s", base_name, suffix);
        return lv_xml_get_const(nullptr, full_name);
    };

    const char* screen_bg_str = get_themed_color("app_bg_color");
    const char* card_bg_str = get_themed_color("card_bg");
    const char* theme_grey_str = get_themed_color("theme_grey");
    const char* text_primary_str = get_themed_color("text_primary");

    if (!screen_bg_str || !card_bg_str || !theme_grey_str || !text_primary_str) {
        spdlog::error("[Theme] Failed to read color constants for {} mode",
                      new_use_dark_mode ? "dark" : "light");
        return;
    }

    lv_color_t screen_bg = ui_theme_parse_color(screen_bg_str);
    lv_color_t card_bg = ui_theme_parse_color(card_bg_str);
    lv_color_t theme_grey = ui_theme_parse_color(theme_grey_str);
    lv_color_t text_primary_color = ui_theme_parse_color(text_primary_str);

    spdlog::debug("[Theme] New colors: screen={}, card={}, grey={}, text={}", screen_bg_str,
                  card_bg_str, theme_grey_str, text_primary_str);

    // Update helix theme styles in-place (triggers lv_obj_report_style_change)
    helix_theme_update_colors(new_use_dark_mode, screen_bg, card_bg, theme_grey,
                              text_primary_color);

    // Force style refresh on entire widget tree for local/inline styles
    ui_theme_refresh_widget_tree(lv_screen_active());

    // Invalidate screen to trigger redraw
    lv_obj_invalidate(lv_screen_active());

    spdlog::info("[Theme] Theme toggle complete");
}

bool ui_theme_is_dark_mode() {
    return use_dark_mode;
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
 *   lv_color_t bg = ui_theme_get_color("app_bg_color");
 *   // Returns app_bg_color_light in light mode, app_bg_color_dark in dark mode
 *
 *   lv_color_t warn = ui_theme_get_color("warning_color");
 *   // Returns warning_color directly (static, no theme variants)
 */
lv_color_t ui_theme_get_color(const char* base_name) {
    if (!base_name) {
        spdlog::error("[Theme] ui_theme_get_color: NULL base_name");
        return lv_color_hex(0x000000);
    }

    // Construct variant names: {base_name}_light and {base_name}_dark
    char light_name[128];
    char dark_name[128];
    snprintf(light_name, sizeof(light_name), "%s_light", base_name);
    snprintf(dark_name, sizeof(dark_name), "%s_dark", base_name);

    // Look up color strings from globals.xml
    const char* light_str = lv_xml_get_const(nullptr, light_name);
    const char* dark_str = lv_xml_get_const(nullptr, dark_name);

    if (!light_str || !dark_str) {
        // Fallback: try the base name directly (for static colors like warning_color, error_color)
        const char* base_str = lv_xml_get_const(nullptr, base_name);
        if (base_str) {
            return ui_theme_parse_color(base_str);
        }

        spdlog::error("[Theme] Color not found: {} (no _light/_dark variants, no static fallback)",
                      base_name);
        return lv_color_hex(0x000000);
    }

    // Select appropriate variant based on theme mode
    const char* selected_str = use_dark_mode ? dark_str : light_str;
    return ui_theme_parse_color(selected_str);
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
 *   ui_theme_apply_bg_color(screen, "app_bg_color", LV_PART_MAIN);
 *   // Applies app_bg_color_light/dark depending on theme mode
 */
void ui_theme_apply_bg_color(lv_obj_t* obj, const char* base_name, lv_part_t part) {
    if (!obj) {
        spdlog::error("[Theme] ui_theme_apply_bg_color: NULL object");
        return;
    }

    lv_color_t color = ui_theme_get_color(base_name);
    lv_obj_set_style_bg_color(obj, color, part);
}

/**
 * Get font line height in pixels
 *
 * Returns the total vertical space a line of text will occupy for the given font.
 * This includes ascender, descender, and line gap. Useful for calculating layout
 * heights before widgets are created.
 *
 * @param font Font to query (e.g., UI_FONT_HEADING, &noto_sans_16)
 * @return Line height in pixels, or 0 if font is NULL
 *
 * Examples:
 *   int32_t heading_h = ui_theme_get_font_height(UI_FONT_HEADING);  // ~24px
 *   int32_t body_h = ui_theme_get_font_height(UI_FONT_BODY);        // ~20px
 *   int32_t small_h = ui_theme_get_font_height(UI_FONT_SMALL);      // ~15px
 *
 *   // Calculate total height for multi-line layout
 *   int32_t total = ui_theme_get_font_height(UI_FONT_HEADING) +
 *                   (ui_theme_get_font_height(UI_FONT_BODY) * 3) +
 *                   (4 * 8);  // 4 gaps of 8px padding
 */
int32_t ui_theme_get_font_height(const lv_font_t* font) {
    if (!font) {
        spdlog::warn("[Theme] ui_theme_get_font_height: NULL font pointer");
        return 0;
    }

    return lv_font_get_line_height(font);
}

void ui_set_overlay_width(lv_obj_t* obj, lv_obj_t* screen) {
    if (!obj || !screen) {
        spdlog::warn("[Theme] ui_set_overlay_width: NULL pointer");
        return;
    }

    lv_coord_t screen_width = lv_obj_get_width(screen);
    lv_coord_t nav_width = UI_NAV_WIDTH(screen_width);
    lv_obj_set_width(obj, screen_width - nav_width);
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
 *   lv_obj_set_style_pad_all(obj, ui_theme_get_spacing("space_lg"), 0);
 */
int32_t ui_theme_get_spacing(const char* token) {
    if (!token) {
        spdlog::warn("[Theme] ui_theme_get_spacing: NULL token");
        return 0;
    }

    const char* value = lv_xml_get_const(NULL, token);
    if (!value) {
        spdlog::warn("[Theme] Spacing token '{}' not found - is theme initialized?", token);
        return 0;
    }

    return std::atoi(value);
}
