// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "theme_manager.h"

#include "ui_error_reporting.h"
#include "ui_fonts.h"

#include "config.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/libs/expat/expat.h"
#include "lvgl/src/themes/lv_theme_private.h"
#include "lvgl/src/xml/lv_xml.h"
#include "settings_manager.h"
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

// ============================================================================
// LVGL Theme Infrastructure (formerly in theme_compat.cpp)
// ============================================================================

// Static theme instance - persists for lifetime of app
static lv_theme_t helix_theme;
static lv_theme_t* default_theme_backup = nullptr;

// Additional styles not in StyleRole enum (widget-specific parts)
static lv_style_t dropdown_indicator_style;
static lv_style_t checkbox_text_style;
static lv_style_t checkbox_box_style;
static lv_style_t checkbox_indicator_style;
static lv_style_t switch_track_style;
static lv_style_t switch_indicator_style;
static lv_style_t switch_knob_style;
static lv_style_t slider_track_style;
static lv_style_t slider_indicator_style;
static lv_style_t slider_knob_style;
static lv_style_t slider_disabled_style;
static lv_color_t dropdown_accent_color;
static bool extra_styles_initialized = false;

/**
 * @brief 16-color semantic palette for theme initialization (internal use)
 */
struct theme_palette_t {
    lv_color_t screen_bg;   // 0: Main app background
    lv_color_t overlay_bg;  // 1: Sidebar/panel background
    lv_color_t card_bg;     // 2: Card surfaces
    lv_color_t elevated_bg; // 3: Elevated/control surfaces (buttons, inputs)
    lv_color_t border;      // 4: Borders and dividers
    lv_color_t text;        // 5: Primary text
    lv_color_t text_muted;  // 6: Secondary text
    lv_color_t text_subtle; // 7: Hint/tertiary text
    lv_color_t primary;     // 8: Primary accent
    lv_color_t secondary;   // 9: Secondary accent
    lv_color_t tertiary;    // 10: Tertiary accent
    lv_color_t info;        // 11: Info states
    lv_color_t success;     // 12: Success states
    lv_color_t warning;     // 13: Warning states
    lv_color_t danger;      // 14: Error/danger states
    lv_color_t focus;       // 15: Focus ring color
};

// Forward declarations for theme infrastructure
static void init_extra_styles(const theme_palette_t* palette, int border_radius);
static void helix_theme_apply(lv_theme_t* theme, lv_obj_t* obj);

/**
 * @brief Build theme_palette_t from ModePalette
 *
 * Converts the C++ ModePalette struct (hex strings) to C theme_palette_t (lv_color_t).
 * Used to pass colors to theme_core functions.
 *
 * @param mode_palette ModePalette with hex color strings
 * @return theme_palette_t with parsed lv_color_t values
 */
static theme_palette_t build_palette_from_mode(const helix::ModePalette& mode_palette) {
    theme_palette_t palette = {};
    palette.screen_bg = theme_manager_parse_hex_color(mode_palette.screen_bg.c_str());
    palette.overlay_bg = theme_manager_parse_hex_color(mode_palette.overlay_bg.c_str());
    palette.card_bg = theme_manager_parse_hex_color(mode_palette.card_bg.c_str());
    palette.elevated_bg = theme_manager_parse_hex_color(mode_palette.elevated_bg.c_str());
    palette.border = theme_manager_parse_hex_color(mode_palette.border.c_str());
    palette.text = theme_manager_parse_hex_color(mode_palette.text.c_str());
    palette.text_muted = theme_manager_parse_hex_color(mode_palette.text_muted.c_str());
    palette.text_subtle = theme_manager_parse_hex_color(mode_palette.text_subtle.c_str());
    palette.primary = theme_manager_parse_hex_color(mode_palette.primary.c_str());
    palette.secondary = theme_manager_parse_hex_color(mode_palette.secondary.c_str());
    palette.tertiary = theme_manager_parse_hex_color(mode_palette.tertiary.c_str());
    palette.info = theme_manager_parse_hex_color(mode_palette.info.c_str());
    palette.success = theme_manager_parse_hex_color(mode_palette.success.c_str());
    palette.warning = theme_manager_parse_hex_color(mode_palette.warning.c_str());
    palette.danger = theme_manager_parse_hex_color(mode_palette.danger.c_str());
    palette.focus = theme_manager_parse_hex_color(mode_palette.focus.c_str());
    return palette;
}

/**
 * @brief Get the current mode palette based on dark/light mode
 *
 * Returns reference to appropriate ModePalette from active_theme.
 * Falls back to the available palette if the requested mode is not supported.
 */
static const helix::ModePalette& get_current_mode_palette() {
    if (use_dark_mode && active_theme.supports_dark()) {
        return active_theme.dark;
    } else if (!use_dark_mode && active_theme.supports_light()) {
        return active_theme.light;
    } else if (active_theme.supports_dark()) {
        return active_theme.dark;
    } else {
        return active_theme.light;
    }
}

// Theme preset overrides removed - colors now come from theme JSON files via ThemeData

// Parse hex color string "#FF4444" -> lv_color_hex(0xFF4444)
lv_color_t theme_manager_parse_hex_color(const char* hex_str) {
    if (!hex_str || hex_str[0] != '#') {
        spdlog::error("[Theme] Invalid hex color string: {}", hex_str ? hex_str : "NULL");
        return lv_color_hex(0x000000);
    }
    uint32_t hex = static_cast<uint32_t>(strtoul(hex_str + 1, nullptr, 16));
    return lv_color_hex(hex);
}

/**
 * @brief Calculate perceived brightness of an lv_color_t
 * Uses standard luminance formula: 0.299*R + 0.587*G + 0.114*B
 * @return Brightness value 0-255
 */
int theme_compute_brightness(lv_color_t color) {
    uint32_t c = lv_color_to_u32(color);
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = c & 0xFF;
    return (299 * r + 587 * g + 114 * b) / 1000;
}

/**
 * @brief Return the brighter of two colors
 */
lv_color_t theme_compute_brighter_color(lv_color_t a, lv_color_t b) {
    return theme_compute_brightness(a) >= theme_compute_brightness(b) ? a : b;
}

/**
 * @brief Compute saturation of a color (0-255)
 *
 * Uses HSV saturation: (max - min) / max * 255
 * Returns 0 for grayscale colors, higher for more vivid colors.
 */
int theme_compute_saturation(lv_color_t c) {
    int max_val =
        c.red > c.green ? (c.red > c.blue ? c.red : c.blue) : (c.green > c.blue ? c.green : c.blue);
    int min_val =
        c.red < c.green ? (c.red < c.blue ? c.red : c.blue) : (c.green < c.blue ? c.green : c.blue);
    if (max_val == 0)
        return 0;
    return (max_val - min_val) * 255 / max_val;
}

/**
 * @brief Return the more saturated of two colors
 *
 * Useful for accent colors where you want the more vivid/colorful option
 * rather than the literally brighter one.
 */
lv_color_t theme_compute_more_saturated(lv_color_t a, lv_color_t b) {
    return theme_compute_saturation(a) >= theme_compute_saturation(b) ? a : b;
}

lv_color_t theme_get_knob_color() {
    // Knob color: more saturated of primary vs tertiary (for switch/slider handles)
    const char* primary_str = lv_xml_get_const(nullptr, "primary");
    const char* tertiary_str = lv_xml_get_const(nullptr, "tertiary");

    if (!primary_str) {
        spdlog::warn("[Theme] theme_get_knob_color: missing 'primary' constant");
        return lv_color_hex(0x5e81ac); // Fallback to Nord blue
    }

    lv_color_t primary = theme_manager_parse_hex_color(primary_str);
    lv_color_t tertiary = tertiary_str ? theme_manager_parse_hex_color(tertiary_str) : primary;

    return theme_compute_more_saturated(primary, tertiary);
}

lv_color_t theme_get_accent_color() {
    // Accent color: more saturated of primary vs secondary (for icon accents)
    const char* primary_str = lv_xml_get_const(nullptr, "primary");
    const char* secondary_str = lv_xml_get_const(nullptr, "secondary");

    if (!primary_str) {
        spdlog::warn("[Theme] theme_get_accent_color: missing 'primary' constant");
        return lv_color_hex(0x5e81ac); // Fallback to Nord blue
    }

    lv_color_t primary = theme_manager_parse_hex_color(primary_str);
    lv_color_t secondary = secondary_str ? theme_manager_parse_hex_color(secondary_str) : primary;

    return theme_compute_more_saturated(primary, secondary);
}

lv_color_t theme_manager_get_contrast_text(lv_color_t bg_color) {
    int brightness = theme_compute_brightness(bg_color);
    auto& tm = ThemeManager::instance();
    // Dark background needs light text (dark palette has light-colored text for readability)
    // Light background needs dark text (light palette has dark-colored text for readability)
    return (brightness < 128) ? tm.dark_palette().text : tm.light_palette().text;
}

// ============================================================================
// LVGL Theme Infrastructure - Apply Callbacks & Style Initialization
// ============================================================================

/**
 * @brief Initialize the extra widget-specific styles
 *
 * These are styles for widget parts not covered by the StyleRole enum.
 */
static void init_extra_styles(const theme_palette_t* palette, int border_radius) {
    if (extra_styles_initialized)
        return;

    dropdown_accent_color = palette->secondary;

    // Dropdown indicator - MDI font for chevron
    lv_style_init(&dropdown_indicator_style);
    lv_style_set_text_font(&dropdown_indicator_style, &mdi_icons_24);

    // Checkbox styles
    lv_style_init(&checkbox_text_style);
    lv_style_set_text_color(&checkbox_text_style, palette->text);

    lv_style_init(&checkbox_box_style);
    lv_style_set_bg_color(&checkbox_box_style, palette->elevated_bg);
    lv_style_set_bg_opa(&checkbox_box_style, LV_OPA_COVER);
    lv_style_set_border_color(&checkbox_box_style, palette->border);
    lv_style_set_border_width(&checkbox_box_style, 2);
    lv_style_set_radius(&checkbox_box_style, 4);

    lv_style_init(&checkbox_indicator_style);
    lv_style_set_bg_color(&checkbox_indicator_style, palette->primary);
    lv_style_set_bg_opa(&checkbox_indicator_style, LV_OPA_COVER);
    lv_style_set_border_color(&checkbox_indicator_style, palette->primary);
    // Checkmark: set bg_image_src to bold check symbol, rendered via text_font
    lv_style_set_bg_image_src(&checkbox_indicator_style, LV_SYMBOL_OK);
    lv_style_set_text_font(&checkbox_indicator_style, &mdi_icons_16);
    // Contrast text color based on primary luminance (same pattern as ui_button)
    uint8_t cb_lum = lv_color_luminance(palette->primary);
    lv_style_set_text_color(&checkbox_indicator_style,
                            (cb_lum > 140) ? lv_color_black() : lv_color_white());

    // Switch styles
    lv_style_init(&switch_track_style);
    lv_style_set_bg_color(&switch_track_style, palette->border);
    lv_style_set_bg_opa(&switch_track_style, LV_OPA_COVER);

    lv_style_init(&switch_indicator_style);
    lv_style_set_bg_color(&switch_indicator_style, palette->secondary);
    lv_style_set_bg_opa(&switch_indicator_style, LV_OPA_COVER);

    lv_style_init(&switch_knob_style);
    lv_style_set_bg_color(&switch_knob_style, palette->primary);
    lv_style_set_bg_opa(&switch_knob_style, LV_OPA_COVER);

    // Slider styles
    lv_style_init(&slider_track_style);
    lv_style_set_bg_color(&slider_track_style, palette->border);
    lv_style_set_bg_opa(&slider_track_style, LV_OPA_COVER);
    lv_style_set_radius(&slider_track_style, border_radius);

    lv_style_init(&slider_indicator_style);
    lv_style_set_bg_color(&slider_indicator_style, palette->primary);
    lv_style_set_bg_opa(&slider_indicator_style, LV_OPA_COVER);

    lv_style_init(&slider_knob_style);
    lv_style_set_bg_color(&slider_knob_style, palette->primary);
    lv_style_set_bg_opa(&slider_knob_style, LV_OPA_COVER);
    lv_style_set_border_color(&slider_knob_style, palette->border);
    lv_style_set_border_width(&slider_knob_style, 1);
    lv_style_set_shadow_width(&slider_knob_style, 4);
    lv_style_set_shadow_color(&slider_knob_style, lv_color_black());
    lv_style_set_shadow_opa(&slider_knob_style, LV_OPA_30);

    lv_style_init(&slider_disabled_style);
    lv_style_set_opa(&slider_disabled_style, LV_OPA_50);

    extra_styles_initialized = true;
}

/**
 * @brief HelixScreen theme apply callback - applies styles based on widget type
 *
 * This is called by LVGL for every widget created. It first applies the default
 * theme, then layers our custom styles on top.
 */
static void helix_theme_apply(lv_theme_t* theme, lv_obj_t* obj) {
    (void)theme;

    // First apply LVGL default theme
    if (default_theme_backup && default_theme_backup->apply_cb) {
        default_theme_backup->apply_cb(default_theme_backup, obj);
    }

    auto& tm = ThemeManager::instance();

    // Global disabled state
    lv_obj_add_style(obj, tm.get_style(StyleRole::Disabled), LV_PART_MAIN | LV_STATE_DISABLED);

    // Plain lv_obj containers get transparent background (layout containers)
    if (lv_obj_check_type(obj, &lv_obj_class)) {
        lv_obj_add_style(obj, tm.get_style(StyleRole::ObjBase), LV_PART_MAIN);
    }

#if LV_USE_BUTTON
    if (lv_obj_check_type(obj, &lv_button_class)) {
        lv_obj_add_style(obj, tm.get_style(StyleRole::Button), LV_PART_MAIN);
        lv_obj_add_style(obj, tm.get_style(StyleRole::Pressed), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_add_style(obj, tm.get_style(StyleRole::Focused), LV_STATE_FOCUSED);
    }
#endif

#if LV_USE_TEXTAREA
    if (lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_obj_add_style(obj, tm.get_style(StyleRole::InputBg), LV_PART_MAIN);
        lv_obj_add_style(obj, tm.get_style(StyleRole::Focused), LV_STATE_FOCUSED);
    }
#endif

#if LV_USE_DROPDOWN
    if (lv_obj_check_type(obj, &lv_dropdown_class)) {
        lv_obj_add_style(obj, tm.get_style(StyleRole::InputBg), LV_PART_MAIN);
        lv_obj_add_style(obj, &dropdown_indicator_style, LV_PART_INDICATOR);
        lv_obj_add_style(obj, tm.get_style(StyleRole::Focused), LV_STATE_FOCUSED);
    }
    if (lv_obj_check_type(obj, &lv_dropdownlist_class)) {
        lv_obj_add_style(obj, tm.get_style(StyleRole::InputBg), LV_PART_MAIN);

        // Compute contrast text for dropdown accent
        uint8_t lum = lv_color_luminance(dropdown_accent_color);
        lv_color_t selected_text = (lum > 140) ? lv_color_black() : lv_color_white();

        lv_obj_set_style_bg_color(obj, dropdown_accent_color, LV_PART_SELECTED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_SELECTED);
        lv_obj_set_style_text_color(obj, selected_text, LV_PART_SELECTED);
        lv_obj_set_style_bg_color(obj, dropdown_accent_color, LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(obj, selected_text, LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(obj, dropdown_accent_color, LV_PART_SELECTED | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_SELECTED | LV_STATE_PRESSED);
        lv_obj_set_style_text_color(obj, selected_text, LV_PART_SELECTED | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(obj, dropdown_accent_color,
                                  LV_PART_SELECTED | LV_STATE_CHECKED | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER,
                                LV_PART_SELECTED | LV_STATE_CHECKED | LV_STATE_PRESSED);
        lv_obj_set_style_text_color(obj, selected_text,
                                    LV_PART_SELECTED | LV_STATE_CHECKED | LV_STATE_PRESSED);
    }
#endif

#if LV_USE_ROLLER
    if (lv_obj_check_type(obj, &lv_roller_class)) {
        lv_obj_add_style(obj, tm.get_style(StyleRole::InputBg), LV_PART_MAIN);
    }
#endif

#if LV_USE_SPINBOX
    if (lv_obj_check_type(obj, &lv_spinbox_class)) {
        lv_obj_add_style(obj, tm.get_style(StyleRole::InputBg), LV_PART_MAIN);
    }
#endif

#if LV_USE_CHECKBOX
    if (lv_obj_check_type(obj, &lv_checkbox_class)) {
        lv_obj_add_style(obj, &checkbox_text_style, LV_PART_MAIN);
        lv_obj_add_style(obj, &checkbox_box_style, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &checkbox_indicator_style, LV_PART_INDICATOR | LV_STATE_CHECKED);
    }
#endif

#if LV_USE_SWITCH
    if (lv_obj_check_type(obj, &lv_switch_class)) {
        lv_obj_add_style(obj, &switch_track_style, LV_PART_MAIN);
        lv_obj_add_style(obj, &switch_indicator_style, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_style(obj, &switch_knob_style, LV_PART_KNOB);
        lv_obj_add_style(obj, tm.get_style(StyleRole::Focused), LV_STATE_FOCUSED);
    }
#endif

#if LV_USE_SLIDER
    if (lv_obj_check_type(obj, &lv_slider_class)) {
        lv_obj_add_style(obj, &slider_track_style, LV_PART_MAIN);
        lv_obj_add_style(obj, &slider_indicator_style, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &slider_knob_style, LV_PART_KNOB);
        lv_obj_add_style(obj, &slider_disabled_style, LV_PART_MAIN | LV_STATE_DISABLED);
        lv_obj_add_style(obj, &slider_disabled_style, LV_PART_INDICATOR | LV_STATE_DISABLED);
        lv_obj_add_style(obj, &slider_disabled_style, LV_PART_KNOB | LV_STATE_DISABLED);
    }
#endif
}

/**
 * @brief Convert theme_palette_t to ThemePalette for ThemeManager
 */
static ThemePalette convert_to_theme_palette(const theme_palette_t* p, int border_radius,
                                             int border_width, int border_opacity) {
    ThemePalette palette;
    palette.screen_bg = p->screen_bg;
    palette.overlay_bg = p->overlay_bg;
    palette.card_bg = p->card_bg;
    palette.elevated_bg = p->elevated_bg;
    palette.border = p->border;
    palette.text = p->text;
    palette.text_muted = p->text_muted;
    palette.text_subtle = p->text_subtle;
    palette.primary = p->primary;
    palette.secondary = p->secondary;
    palette.tertiary = p->tertiary;
    palette.info = p->info;
    palette.success = p->success;
    palette.warning = p->warning;
    palette.danger = p->danger;
    palette.focus = p->focus;
    palette.border_radius = border_radius;
    palette.border_width = border_width;
    palette.border_opacity = border_opacity;
    return palette;
}

/**
 * @brief Initialize the HelixScreen LVGL theme
 *
 * Sets up ThemeManager, initializes extra widget styles, and registers
 * the helix_theme with LVGL.
 */
static lv_theme_t* theme_init_lvgl(lv_display_t* display, const theme_palette_t* palette,
                                   bool is_dark, const lv_font_t* base_font, int32_t border_radius,
                                   int32_t border_width, int32_t border_opacity) {
    // Build BOTH dark and light palettes from active_theme for contrast calculations
    // The contrast system needs both palettes to pick appropriate text colors
    theme_palette_t dark_theme_pal = build_palette_from_mode(active_theme.dark);
    theme_palette_t light_theme_pal = build_palette_from_mode(active_theme.light);

    ThemePalette dark_pal =
        convert_to_theme_palette(&dark_theme_pal, border_radius, border_width, border_opacity);
    ThemePalette light_pal =
        convert_to_theme_palette(&light_theme_pal, border_radius, border_width, border_opacity);

    auto& tm = ThemeManager::instance();
    tm.set_palettes(light_pal, dark_pal);
    tm.init();
    tm.set_dark_mode(is_dark);

    // Initialize widget-specific styles not in StyleRole enum
    init_extra_styles(palette, border_radius);

    // Create LVGL default theme as base (we'll layer on top)
    default_theme_backup =
        lv_theme_default_init(display, palette->primary, palette->secondary, is_dark, base_font);

    // Initialize our custom theme
    lv_theme_set_apply_cb(&helix_theme, helix_theme_apply);
    helix_theme.font_small = base_font;
    helix_theme.font_normal = base_font;
    helix_theme.font_large = base_font;
    helix_theme.color_primary = palette->primary;
    helix_theme.color_secondary = palette->secondary;

    spdlog::debug("[Theme] Initialized HelixScreen theme via ThemeManager");
    return &helix_theme;
}

/**
 * @brief Update theme colors without full re-initialization
 */
static void theme_update_colors(bool is_dark, const theme_palette_t* palette,
                                int32_t border_opacity) {
    auto& tm = ThemeManager::instance();
    const auto& current = tm.current_palette();

    // Convert and update both palettes
    ThemePalette new_pal = convert_to_theme_palette(palette, current.border_radius,
                                                    current.border_width, border_opacity);

    if (is_dark) {
        // Update dark palette
        ThemePalette light_pal = new_pal; // Keep symmetric for now
        tm.set_palettes(light_pal, new_pal);
    } else {
        // Update light palette
        ThemePalette dark_pal = new_pal;
        tm.set_palettes(new_pal, dark_pal);
    }

    tm.set_dark_mode(is_dark);
    spdlog::debug("[Theme] Updated colors, dark_mode={}", is_dark);
}

/**
 * @brief Preview theme colors (for theme editor)
 */
static void theme_preview_colors(bool is_dark, const theme_palette_t* palette,
                                 int32_t border_radius, int32_t border_opacity) {
    auto& tm = ThemeManager::instance();
    const auto& current = tm.current_palette();

    ThemePalette preview_pal =
        convert_to_theme_palette(palette, border_radius, current.border_width, border_opacity);
    (void)is_dark; // Preview uses the palette directly

    tm.preview_palette(preview_pal);
    spdlog::debug("[Theme] Previewing colors");
}

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
 * @brief Register theme properties (border_radius, border_width, etc.) as XML constants
 *
 * These override the default values from globals.xml, allowing themes to customize
 * geometry like corner radius and border width - similar to how colors work.
 *
 * IMPORTANT: Must be called BEFORE theme_manager_register_static_constants() since
 * LVGL ignores duplicate lv_xml_register_const calls (first registration wins).
 *
 * @param scope LVGL XML scope to register constants in
 * @param theme Theme data with properties
 */
static void theme_manager_register_theme_properties(lv_xml_component_scope_t* scope,
                                                    const helix::ThemeData& theme) {
    char buf[32];

    // Register button_radius and card_radius - theme-specific corner radii
    // Separate from border_radius which controls general UI elements like dropdowns/inputs
    // Both currently use the same theme value, but may be split later
    snprintf(buf, sizeof(buf), "%d", theme.properties.border_radius);
    lv_xml_register_const(scope, "button_radius", buf);
    lv_xml_register_const(scope, "card_radius", buf);

    // Register border_width
    snprintf(buf, sizeof(buf), "%d", theme.properties.border_width);
    lv_xml_register_const(scope, "border_width", buf);

    // Register border_opacity (0-255)
    snprintf(buf, sizeof(buf), "%d", theme.properties.border_opacity);
    lv_xml_register_const(scope, "border_opacity", buf);

    // Register shadow_intensity
    snprintf(buf, sizeof(buf), "%d", theme.properties.shadow_intensity);
    lv_xml_register_const(scope, "shadow_intensity", buf);

    spdlog::debug("[Theme] Registered properties: border_radius={}, border_width={}, "
                  "border_opacity={}, shadow_intensity={}",
                  theme.properties.border_radius, theme.properties.border_width,
                  theme.properties.border_opacity, theme.properties.shadow_intensity);
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

    // Load theme file (supports fallback from user themes to defaults)
    auto theme = helix::load_theme_from_file(theme_name);

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

    // Register semantic colors from dual-palette system (includes _light/_dark variants and base
    // names) NOTE: Legacy palette registration removed - was causing token collisions (text_light
    // conflict)
    theme_manager_register_semantic_colors(scope, active_theme, use_dark_mode);

    // Register theme properties (border_radius, etc.) - must be before static constants
    // so theme values override globals.xml defaults (first registration wins in LVGL)
    theme_manager_register_theme_properties(scope, active_theme);

    // Register static constants (colors, px, strings without dynamic suffixes)
    theme_manager_register_static_constants(scope);

    // Auto-register all color pairs from globals.xml (xxx_light/xxx_dark -> xxx)
    // This handles screen_bg, text, header_text, elevated_bg, card_bg, etc.
    theme_manager_register_color_pairs(scope, use_dark_mode);

    // Register responsive constants (must be before theme init so fonts are available)
    theme_manager_register_responsive_spacing(display);
    theme_manager_register_responsive_fonts(display);

    // Validate critical color pairs were registered (fail-fast if missing)
    static const char* required_colors[] = {"screen_bg", "text", "text_muted", nullptr};
    for (const char** name = required_colors; *name != nullptr; ++name) {
        if (!lv_xml_get_const(nullptr, *name)) {
            spdlog::critical(
                "[Theme] FATAL: Missing required color pair {}_light/{}_dark in globals.xml", *name,
                *name);
            std::exit(EXIT_FAILURE);
        }
    }

    spdlog::debug("[Theme] Runtime constants set for {} mode", use_dark_mode ? "dark" : "light");

    // Read responsive font based on current breakpoint
    // NOTE: We read the variant directly because base constants are removed to enable
    // responsive overrides (LVGL ignores lv_xml_register_const for existing constants)
    int32_t hor_res = lv_display_get_horizontal_resolution(display);
    int32_t ver_res = lv_display_get_vertical_resolution(display);
    int32_t greater_res = LV_MAX(hor_res, ver_res);
    const char* size_suffix = theme_manager_get_breakpoint_suffix(greater_res);

    char font_variant_name[64];
    snprintf(font_variant_name, sizeof(font_variant_name), "font_body%s", size_suffix);
    const char* font_body_name = lv_xml_get_const(nullptr, font_variant_name);
    const lv_font_t* base_font =
        font_body_name ? lv_xml_get_font(nullptr, font_body_name) : nullptr;
    if (!base_font) {
        spdlog::warn("[Theme] Failed to get font '{}', using noto_sans_16", font_variant_name);
        base_font = &noto_sans_16;
    }

    // Build palette from current mode
    const helix::ModePalette& mode_palette = get_current_mode_palette();
    theme_palette_t palette = build_palette_from_mode(mode_palette);

    // Get theme properties
    int32_t border_radius = active_theme.properties.border_radius;
    int32_t border_width = active_theme.properties.border_width;
    int32_t border_opacity = active_theme.properties.border_opacity;

    // Initialize custom HelixScreen theme (wraps LVGL default theme)
    current_theme = theme_init_lvgl(display, &palette, use_dark_mode, base_font, border_radius,
                                    border_width, border_opacity);

    if (current_theme) {
        lv_display_set_theme(display, current_theme);
        spdlog::info("[Theme] Initialized HelixScreen theme: {} mode",
                     use_dark_mode ? "dark" : "light");
        spdlog::debug("[Theme] Colors: primary={}, screen={}, card={}", mode_palette.primary,
                      mode_palette.screen_bg, mode_palette.card_bg);
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

    // Build palette from the new mode
    const helix::ModePalette& mode_palette = get_current_mode_palette();
    theme_palette_t palette = build_palette_from_mode(mode_palette);

    spdlog::debug("[Theme] New colors: screen={}, card={}, text={}", mode_palette.screen_bg,
                  mode_palette.card_bg, mode_palette.text);

    // Update helix theme styles in-place (triggers lv_obj_report_style_change)
    theme_update_colors(new_use_dark_mode, &palette, active_theme.properties.border_opacity);

    // Re-register XML color constants with new dark mode value
    // This updates #screen_bg, #card_bg, etc. before the widget tree refresh
    theme_manager_register_color_pairs(nullptr, new_use_dark_mode);

    // Explicitly update screen background - XML inline styles are baked at parse time
    // so we need to directly set the bg color on the active screen
    lv_color_t screen_bg = theme_manager_parse_hex_color(mode_palette.screen_bg.c_str());
    lv_obj_set_style_bg_color(lv_screen_active(), screen_bg, LV_PART_MAIN);

    // Force style refresh on entire widget tree for local/inline styles
    theme_manager_refresh_widget_tree(lv_screen_active());

    // Apply palette colors to all widgets (labels, buttons, sliders, etc.)
    // This updates C++-styled widgets that don't use shared LVGL styles
    theme_apply_current_palette_to_tree(lv_screen_active());

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
    // Use global dark mode setting
    theme_manager_preview(theme, use_dark_mode);
}

void theme_manager_preview(const helix::ThemeData& theme, bool is_dark) {
    // Select the appropriate mode palette for preview
    const helix::ModePalette* mode_palette = nullptr;
    if (is_dark && theme.supports_dark()) {
        mode_palette = &theme.dark;
    } else if (!is_dark && theme.supports_light()) {
        mode_palette = &theme.light;
    } else if (theme.supports_dark()) {
        mode_palette = &theme.dark;
    } else {
        mode_palette = &theme.light;
    }

    theme_palette_t palette = build_palette_from_mode(*mode_palette);
    theme_preview_colors(is_dark, &palette, theme.properties.border_radius,
                         theme.properties.border_opacity);
    theme_manager_refresh_widget_tree(lv_screen_active());

    spdlog::debug("[Theme] Previewing theme: {} ({})", theme.name, is_dark ? "dark" : "light");
}

void theme_manager_revert_preview() {
    theme_manager_preview(active_theme, use_dark_mode);
    spdlog::debug("[Theme] Reverted to active theme: {}", active_theme.name);
}

void theme_manager_refresh_preview_elements(lv_obj_t* root, const helix::ThemeData& theme) {
    if (!root) {
        return;
    }

    // Get mode-appropriate palette (all colors should come from the same palette)
    const helix::ModePalette* palette = use_dark_mode
                                            ? (theme.supports_dark() ? &theme.dark : nullptr)
                                            : (theme.supports_light() ? &theme.light : nullptr);
    if (!palette) {
        palette = theme.supports_dark() ? &theme.dark : &theme.light;
    }
    if (!palette) {
        spdlog::warn("[Theme] No palette available for preview refresh");
        return;
    }

    // Background/surface colors
    lv_color_t screen_bg = theme_manager_parse_hex_color(palette->screen_bg.c_str());
    lv_color_t card_bg = theme_manager_parse_hex_color(palette->card_bg.c_str());
    lv_color_t elevated_bg = theme_manager_parse_hex_color(palette->elevated_bg.c_str());
    lv_color_t border = theme_manager_parse_hex_color(palette->border.c_str());

    // Text colors
    lv_color_t text_color = theme_manager_parse_hex_color(palette->text.c_str());
    lv_color_t text_muted = theme_manager_parse_hex_color(palette->text_muted.c_str());

    // Semantic/accent colors
    lv_color_t primary = theme_manager_parse_hex_color(palette->primary.c_str());
    lv_color_t secondary = theme_manager_parse_hex_color(palette->secondary.c_str());
    lv_color_t tertiary = theme_manager_parse_hex_color(palette->tertiary.c_str());
    lv_color_t success = theme_manager_parse_hex_color(palette->success.c_str());
    lv_color_t warning = theme_manager_parse_hex_color(palette->warning.c_str());
    lv_color_t danger = theme_manager_parse_hex_color(palette->danger.c_str());
    lv_color_t info = theme_manager_parse_hex_color(palette->info.c_str());

    // Knob color: brighter of primary vs tertiary (for switch/slider handles)
    lv_color_t knob_color = theme_compute_more_saturated(primary, tertiary);

    // Theme geometry properties
    int32_t border_radius = theme.properties.border_radius;
    int32_t border_width = theme.properties.border_width;
    int32_t border_opacity = theme.properties.border_opacity;

    // ========================================================================
    // OVERLAY BACKGROUNDS - Update BOTH theme_preview_overlay AND theme_settings_overlay
    // ========================================================================
    // Both overlays extend overlay_panel which has bg_color on the root view.
    // Strategy: Find unique child elements and walk up to find overlay roots.

    lv_obj_t* preview_overlay = nullptr;
    lv_obj_t* editor_overlay = nullptr;

    // Try finding by name first
    preview_overlay = lv_obj_find_by_name(root, "theme_preview_overlay");
    editor_overlay = lv_obj_find_by_name(root, "theme_settings_overlay");

    // Fallback: Find unique named elements and walk up to overlay roots
    // Structure: overlay_root -> overlay_content -> ... -> named_element
    if (!preview_overlay) {
        // Find theme_preset_dropdown which is in overlay_content
        lv_obj_t* dropdown = lv_obj_find_by_name(root, "theme_preset_dropdown");
        if (dropdown) {
            // Walk up: dropdown -> container -> theme_controls_row -> overlay_content ->
            // overlay_root
            lv_obj_t* p = dropdown;
            for (int i = 0; i < 4 && p; i++) {
                p = lv_obj_get_parent(p);
            }
            preview_overlay = p;
        }
    }
    if (!editor_overlay) {
        lv_obj_t* swatch_list = lv_obj_find_by_name(root, "theme_swatch_list");
        if (swatch_list) {
            // Walk up: swatch_list -> overlay_content -> overlay_root
            lv_obj_t* parent1 = lv_obj_get_parent(swatch_list);
            if (parent1) {
                editor_overlay = lv_obj_get_parent(parent1);
            }
        }
    }

    // Update overlay backgrounds
    // NOTE: When extending a component, the name goes on a wrapper object.
    // The actual styled content (with style_bg_color) is the first child.
    // So we update BOTH the found object and its first child to be safe.
    auto update_overlay_bg = [&](lv_obj_t* overlay, const char* name) {
        if (!overlay)
            return;
        // Update the wrapper
        lv_obj_set_style_bg_color(overlay, screen_bg, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
        // Update the first child (actual content from component template)
        lv_obj_t* first_child = lv_obj_get_child(overlay, 0);
        if (first_child) {
            lv_obj_set_style_bg_color(first_child, screen_bg, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(first_child, LV_OPA_COVER, LV_PART_MAIN);
        }
        lv_obj_invalidate(overlay);
        spdlog::debug("[Theme] Updated {} bg to #{:06X} (first_child={})", name,
                      lv_color_to_u32(screen_bg) & 0xFFFFFF, first_child ? "yes" : "no");
    };

    if (preview_overlay) {
        update_overlay_bg(preview_overlay, "preview_overlay");
    } else {
        spdlog::warn("[Theme] Could not find preview overlay!");
    }

    if (editor_overlay) {
        update_overlay_bg(editor_overlay, "editor_overlay");
    }

    // Update header bars - header_bar component has bg_opa="0" by default
    // Also need to update first child since name is on component wrapper
    auto update_header = [&](lv_obj_t* overlay) {
        if (!overlay)
            return;
        lv_obj_t* header = lv_obj_find_by_name(overlay, "overlay_header");
        if (header) {
            // Header should match overlay background (screen_bg), not card_bg
            lv_obj_set_style_bg_color(header, screen_bg, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_PART_MAIN);
            // Also update first child (actual header_bar view)
            lv_obj_t* inner = lv_obj_get_child(header, 0);
            if (inner) {
                lv_obj_set_style_bg_color(inner, screen_bg, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, LV_PART_MAIN);
            }
            // Back button should have no background (transparent icon)
            lv_obj_t* back_btn = lv_obj_find_by_name(header, "back_button");
            if (back_btn) {
                lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, LV_PART_MAIN);
            }
        }
    };
    update_header(preview_overlay);
    update_header(editor_overlay);

    // ========================================================================
    // PREVIEW CARDS
    // ========================================================================
    lv_obj_t* card = lv_obj_find_by_name(root, "preview_typography_card");
    if (card) {
        lv_obj_set_style_bg_color(card, card_bg, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, border, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, border_width, LV_PART_MAIN);
        lv_obj_set_style_border_opa(card, border_opacity, LV_PART_MAIN);
        lv_obj_set_style_radius(card, border_radius, LV_PART_MAIN);
    }
    card = lv_obj_find_by_name(root, "preview_actions_card");
    if (card) {
        lv_obj_set_style_bg_color(card, card_bg, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, border, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, border_width, LV_PART_MAIN);
        lv_obj_set_style_border_opa(card, border_opacity, LV_PART_MAIN);
        lv_obj_set_style_radius(card, border_radius, LV_PART_MAIN);
    }
    card = lv_obj_find_by_name(root, "preview_background");
    if (card) {
        lv_obj_set_style_bg_color(card, screen_bg, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, border, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, border_width, LV_PART_MAIN);
        lv_obj_set_style_border_opa(card, border_opacity, LV_PART_MAIN);
        lv_obj_set_style_radius(card, border_radius, LV_PART_MAIN);
    }

    // ========================================================================
    // TYPOGRAPHY - Update text colors for labeled elements
    // ========================================================================
    // Elements using primary text color (palette.text)
    static const char* text_primary_elements[] = {
        "preview_sample_body", // Sample body text demonstration
        "preview_bg_title",    // "App Background" title
        nullptr};

    // Elements using muted text color (palette.text_muted)
    static const char* text_muted_elements[] = {
        // Card section headers
        "preview_heading_typography", "preview_heading_actions",
        // Sample typography demonstrations
        "preview_sample_heading", "preview_sample_small",
        // Form/control labels
        "preview_label_preset", "preview_label_dark_mode", "preview_label_mode",
        "preview_label_auto", "preview_label_input", "preview_label_intensity",
        "preview_label_status_icons", "preview_label_status",
        // Descriptive text
        "preview_description", "preview_bg_description", nullptr};

    // Apply primary text color
    for (const char** name = text_primary_elements; *name != nullptr; ++name) {
        lv_obj_t* elem = lv_obj_find_by_name(root, *name);
        if (elem) {
            lv_obj_set_style_text_color(elem, text_color, LV_PART_MAIN);
        }
    }

    // Apply muted text color
    for (const char** name = text_muted_elements; *name != nullptr; ++name) {
        lv_obj_t* elem = lv_obj_find_by_name(root, *name);
        if (elem) {
            lv_obj_set_style_text_color(elem, text_muted, LV_PART_MAIN);
        }
    }

    // ========================================================================
    // ACTION BUTTONS
    // ========================================================================
    lv_obj_t* btn = lv_obj_find_by_name(root, "example_btn_primary");
    if (btn) {
        lv_obj_set_style_bg_color(btn, primary, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, border_radius, LV_PART_MAIN);
    }
    btn = lv_obj_find_by_name(root, "example_btn_secondary");
    if (btn) {
        lv_obj_set_style_bg_color(btn, secondary, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, border_radius, LV_PART_MAIN);
    }
    btn = lv_obj_find_by_name(root, "example_btn_tertiary");
    if (btn) {
        lv_obj_set_style_bg_color(btn, tertiary, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, border_radius, LV_PART_MAIN);
    }
    btn = lv_obj_find_by_name(root, "example_btn_warning");
    if (btn) {
        lv_obj_set_style_bg_color(btn, warning, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, border_radius, LV_PART_MAIN);
    }
    btn = lv_obj_find_by_name(root, "example_btn_danger");
    if (btn) {
        lv_obj_set_style_bg_color(btn, danger, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, border_radius, LV_PART_MAIN);
    }
    // Header action buttons - button_2 (Edit) uses secondary, button (Apply) uses primary
    btn = lv_obj_find_by_name(root, "action_button_2");
    if (btn) {
        lv_obj_set_style_bg_color(btn, secondary, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, border_radius, LV_PART_MAIN);
    }
    btn = lv_obj_find_by_name(root, "action_button");
    if (btn) {
        lv_obj_set_style_bg_color(btn, primary, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, border_radius, LV_PART_MAIN);
    }
    // Preview "Open" button uses tertiary
    btn = lv_obj_find_by_name(root, "preview_open_button");
    if (btn) {
        lv_obj_set_style_bg_color(btn, tertiary, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, border_radius, LV_PART_MAIN);
    }

    // ========================================================================
    // STATUS DOTS (danger, warning, success, info)
    // ========================================================================
    lv_obj_t* aurora = lv_obj_find_by_name(root, "aurora_0");
    if (aurora) {
        lv_obj_set_style_bg_color(aurora, danger, LV_PART_MAIN);
        lv_obj_set_style_border_color(aurora, border, LV_PART_MAIN);
        lv_obj_set_style_radius(aurora, border_radius, LV_PART_MAIN);
    }
    aurora = lv_obj_find_by_name(root, "aurora_1");
    if (aurora) {
        lv_obj_set_style_bg_color(aurora, warning, LV_PART_MAIN);
        lv_obj_set_style_border_color(aurora, border, LV_PART_MAIN);
        lv_obj_set_style_radius(aurora, border_radius, LV_PART_MAIN);
    }
    aurora = lv_obj_find_by_name(root, "aurora_2");
    if (aurora) {
        lv_obj_set_style_bg_color(aurora, success, LV_PART_MAIN);
        lv_obj_set_style_border_color(aurora, border, LV_PART_MAIN);
        lv_obj_set_style_radius(aurora, border_radius, LV_PART_MAIN);
    }
    aurora = lv_obj_find_by_name(root, "aurora_3");
    if (aurora) {
        lv_obj_set_style_bg_color(aurora, info, LV_PART_MAIN);
        lv_obj_set_style_border_color(aurora, border, LV_PART_MAIN);
        lv_obj_set_style_radius(aurora, border_radius, LV_PART_MAIN);
    }

    // ========================================================================
    // INPUT WIDGETS (dropdowns, textarea) - use elevated_bg for input backgrounds
    // ========================================================================
    lv_obj_t* dropdown = lv_obj_find_by_name(root, "theme_preset_dropdown");
    if (dropdown) {
        lv_obj_set_style_bg_color(dropdown, elevated_bg, LV_PART_MAIN);
        lv_obj_set_style_border_color(dropdown, border, LV_PART_MAIN);
        lv_obj_set_style_text_color(dropdown, text_color, LV_PART_MAIN);
        lv_obj_set_style_radius(dropdown, border_radius, LV_PART_MAIN);
    }
    dropdown = lv_obj_find_by_name(root, "preview_dropdown");
    if (dropdown) {
        lv_obj_set_style_bg_color(dropdown, elevated_bg, LV_PART_MAIN);
        lv_obj_set_style_text_color(dropdown, text_color, LV_PART_MAIN);
        lv_obj_set_style_radius(dropdown, border_radius, LV_PART_MAIN);
    }

    lv_obj_t* textarea = lv_obj_find_by_name(root, "preview_text_input");
    if (textarea) {
        lv_obj_set_style_bg_color(textarea, elevated_bg, LV_PART_MAIN);
        lv_obj_set_style_text_color(textarea, text_color, LV_PART_MAIN);
        lv_obj_set_style_radius(textarea, border_radius, LV_PART_MAIN);
    }

    // ========================================================================
    // ICONS - accent variant uses brighter of primary vs secondary
    // ========================================================================
    lv_color_t accent_color = theme_compute_more_saturated(primary, secondary);
    lv_obj_t* icon = lv_obj_find_by_name(root, "preview_icon_typography");
    if (icon) {
        lv_obj_set_style_text_color(icon, accent_color, LV_PART_MAIN);
    }
    icon = lv_obj_find_by_name(root, "preview_icon_actions");
    if (icon) {
        lv_obj_set_style_text_color(icon, accent_color, LV_PART_MAIN);
    }

    // ========================================================================
    // SLIDER - track (border), indicator (secondary), knob (brighter of primary/tertiary)
    // ========================================================================
    lv_obj_t* slider = lv_obj_find_by_name(root, "preview_intensity_slider");
    if (slider) {
        lv_obj_set_style_bg_color(slider, border, LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, secondary, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, knob_color, LV_PART_KNOB);
        lv_obj_set_style_shadow_color(slider, screen_bg, LV_PART_KNOB);
    }

    // ========================================================================
    // SWITCH - track=border, indicator=secondary, knob=brighter of primary/tertiary
    // ========================================================================
    lv_obj_t* sw = lv_obj_find_by_name(root, "preview_switch");
    if (sw) {
        lv_obj_set_style_bg_color(sw, border, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw, secondary, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, knob_color, LV_PART_KNOB);
        lv_obj_set_style_bg_color(sw, knob_color, LV_PART_KNOB | LV_STATE_CHECKED);
    }
    sw = lv_obj_find_by_name(root, "preview_dark_mode_toggle");
    if (sw) {
        // ui_switch creates lv_switch directly (no wrapper), so sw IS the switch
        lv_obj_set_style_bg_color(sw, border, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw, secondary, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, knob_color, LV_PART_KNOB);
        lv_obj_set_style_bg_color(sw, knob_color, LV_PART_KNOB | LV_STATE_CHECKED);
    }

    // ========================================================================
    // THEME EDITOR (Edit Colors) PANEL - update swatch cards, buttons, sliders
    // ========================================================================
    if (editor_overlay) {
        // Update swatch card backgrounds (they all use #card_bg)
        // The swatch list container
        lv_obj_t* swatch_list = lv_obj_find_by_name(editor_overlay, "theme_swatch_list");
        if (swatch_list) {
            // Walk through all children and update card backgrounds
            uint32_t child_count = lv_obj_get_child_count(swatch_list);
            for (uint32_t i = 0; i < child_count; i++) {
                lv_obj_t* row = lv_obj_get_child(swatch_list, i);
                if (!row)
                    continue;
                // Each row has 2 swatch containers
                uint32_t container_count = lv_obj_get_child_count(row);
                for (uint32_t j = 0; j < container_count; j++) {
                    lv_obj_t* container = lv_obj_get_child(row, j);
                    if (container) {
                        // Container background uses card_bg, border uses border color
                        lv_obj_set_style_bg_color(container, card_bg, LV_PART_MAIN);
                        lv_obj_set_style_border_color(container, border, LV_PART_MAIN);
                    }
                }
            }
        }

        // Update action buttons in editor - colors AND border properties
        lv_obj_t* btn_reset = lv_obj_find_by_name(editor_overlay, "btn_reset");
        if (btn_reset) {
            lv_obj_set_style_bg_color(btn_reset, card_bg, LV_PART_MAIN);
            lv_obj_set_style_border_color(btn_reset, border, LV_PART_MAIN);
            lv_obj_set_style_radius(btn_reset, border_radius, LV_PART_MAIN);
            lv_obj_set_style_border_width(btn_reset, border_width, LV_PART_MAIN);
            lv_obj_set_style_border_opa(btn_reset, border_opacity, LV_PART_MAIN);
        }
        lv_obj_t* btn_save_as = lv_obj_find_by_name(editor_overlay, "btn_save_as");
        if (btn_save_as) {
            lv_obj_set_style_bg_color(btn_save_as, card_bg, LV_PART_MAIN);
            lv_obj_set_style_border_color(btn_save_as, border, LV_PART_MAIN);
            lv_obj_set_style_radius(btn_save_as, border_radius, LV_PART_MAIN);
            lv_obj_set_style_border_width(btn_save_as, border_width, LV_PART_MAIN);
            lv_obj_set_style_border_opa(btn_save_as, border_opacity, LV_PART_MAIN);
        }
        lv_obj_t* btn_save = lv_obj_find_by_name(editor_overlay, "btn_save");
        if (btn_save) {
            lv_obj_set_style_bg_color(btn_save, primary, LV_PART_MAIN);
            lv_obj_set_style_border_color(btn_save, border, LV_PART_MAIN);
            lv_obj_set_style_radius(btn_save, border_radius, LV_PART_MAIN);
            lv_obj_set_style_border_width(btn_save, border_width, LV_PART_MAIN);
            lv_obj_set_style_border_opa(btn_save, border_opacity, LV_PART_MAIN);
        }

        // Update sliders in editor (border_radius, border_width, etc.)
        // Use knob_color (more saturated of primary/tertiary) for consistency with other sliders
        const char* slider_names[] = {"row_border_radius", "row_border_width", "row_border_opacity",
                                      "row_shadow_intensity"};
        for (const char* row_name : slider_names) {
            lv_obj_t* row = lv_obj_find_by_name(editor_overlay, row_name);
            if (row) {
                lv_obj_t* slider = lv_obj_find_by_name(row, "slider");
                if (slider) {
                    lv_obj_set_style_bg_color(slider, border, LV_PART_MAIN);
                    lv_obj_set_style_bg_color(slider, secondary, LV_PART_INDICATOR);
                    lv_obj_set_style_bg_color(slider, knob_color, LV_PART_KNOB);
                }
            }
        }
    }

    // ========================================================================
    // SAMPLE MODAL DIALOG - Update if visible (from preview "Open" button)
    // ========================================================================
    // The sample modal is created via modal_dialog component which uses ui_dialog.
    // It gets border_radius from XML constants at creation, so we update it here.
    lv_obj_t* modal_dialog = lv_obj_find_by_name(root, "modal_dialog");
    if (modal_dialog) {
        lv_obj_set_style_bg_color(modal_dialog, elevated_bg, LV_PART_MAIN);
        lv_obj_set_style_radius(modal_dialog, border_radius, LV_PART_MAIN);

        // Update icon colors in the sample modal
        lv_obj_t* icon_info = lv_obj_find_by_name(modal_dialog, "icon_info");
        if (icon_info) {
            lv_obj_set_style_text_color(icon_info, accent_color, LV_PART_MAIN);
        }
        lv_obj_t* icon_warning = lv_obj_find_by_name(modal_dialog, "icon_warning");
        if (icon_warning) {
            lv_obj_set_style_text_color(icon_warning, warning, LV_PART_MAIN);
        }
        lv_obj_t* icon_error = lv_obj_find_by_name(modal_dialog, "icon_error");
        if (icon_error) {
            lv_obj_set_style_text_color(icon_error, danger, LV_PART_MAIN);
        }
    }

    spdlog::trace("[Theme] Refreshed preview elements");
}

// ============================================================================
// Palette Application Functions (for DRY preview styling)
// ============================================================================

/**
 * Check if a font is one of the MDI icon fonts (forward declaration)
 */
static bool is_icon_font(const lv_font_t* font);

/**
 * Helper to update button label text with contrast-aware color
 * text_light = dark text for light backgrounds
 * text_dark = light text for dark backgrounds
 */
static void apply_button_text_contrast(lv_obj_t* btn, lv_color_t text_light, lv_color_t text_dark) {
    if (!btn)
        return;

    // Get button's background color
    lv_color_t bg_color = lv_obj_get_style_bg_color(btn, LV_PART_MAIN);
    uint8_t lum = lv_color_luminance(bg_color);

    // Pick text color based on luminance (same threshold as text_button component)
    lv_color_t text_color = (lum > 140) ? text_light : text_dark;

    // Check for disabled state - use muted color
    bool btn_disabled = lv_obj_has_state(btn, LV_STATE_DISABLED);
    if (btn_disabled) {
        // Blend toward gray for disabled state
        text_color = lv_color_mix(text_color, lv_color_hex(0x888888), 128);
    }

    // Get current text colors to detect text/muted-variant icons
    lv_color_t current_text = theme_manager_get_color("text");
    lv_color_t current_muted = theme_manager_get_color("text_muted");

    // Helper lambda to check if icon color is a "text-like" color that should get contrast
    auto is_text_variant_color = [&](lv_color_t c) {
        return lv_color_eq(c, current_text) || lv_color_eq(c, current_muted) ||
               lv_color_eq(c, text_light) || lv_color_eq(c, text_dark);
    };

    // Update all label children in the button
    // For icons: only apply contrast if they're using text/muted variant
    // Skip icons with semantic colors (primary, warning, etc.)
    uint32_t count = lv_obj_get_child_count(btn);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t* child = lv_obj_get_child(btn, i);
        if (lv_obj_check_type(child, &lv_label_class)) {
            const lv_font_t* font = lv_obj_get_style_text_font(child, LV_PART_MAIN);
            if (is_icon_font(font)) {
                // Icon: only apply contrast if it's using text/muted variant
                lv_color_t icon_color = lv_obj_get_style_text_color(child, LV_PART_MAIN);
                if (is_text_variant_color(icon_color)) {
                    lv_obj_set_style_text_color(child, text_color, LV_PART_MAIN);
                }
            } else {
                // Regular label: always apply contrast
                lv_obj_set_style_text_color(child, text_color, LV_PART_MAIN);
            }
        }
        // Also check nested containers (some buttons have container > label structure)
        uint32_t nested_count = lv_obj_get_child_count(child);
        for (uint32_t j = 0; j < nested_count; j++) {
            lv_obj_t* nested = lv_obj_get_child(child, j);
            if (lv_obj_check_type(nested, &lv_label_class)) {
                const lv_font_t* nested_font = lv_obj_get_style_text_font(nested, LV_PART_MAIN);
                if (is_icon_font(nested_font)) {
                    lv_color_t icon_color = lv_obj_get_style_text_color(nested, LV_PART_MAIN);
                    if (is_text_variant_color(icon_color)) {
                        lv_obj_set_style_text_color(nested, text_color, LV_PART_MAIN);
                    }
                } else {
                    lv_obj_set_style_text_color(nested, text_color, LV_PART_MAIN);
                }
            }
        }
    }
}

/**
 * Check if a font is one of the MDI icon fonts
 */
static bool is_icon_font(const lv_font_t* font) {
    if (!font)
        return false;
    return font == &mdi_icons_16 || font == &mdi_icons_24 || font == &mdi_icons_32 ||
           font == &mdi_icons_48 || font == &mdi_icons_64;
}

/**
 * Check if a font is a "small" semantic font (text_small, text_xs, text_heading use muted color)
 * Returns true for fonts that should use text_muted color
 */
static bool is_muted_text_font(const lv_font_t* font) {
    if (!font)
        return false;

    // Get semantic font pointers for comparison
    static const lv_font_t* font_small = nullptr;
    static const lv_font_t* font_xs = nullptr;
    static const lv_font_t* font_heading = nullptr;
    static bool fonts_initialized = false;

    if (!fonts_initialized) {
        const char* small_name = lv_xml_get_const(nullptr, "font_small");
        const char* xs_name = lv_xml_get_const(nullptr, "font_xs");
        const char* heading_name = lv_xml_get_const(nullptr, "font_heading");
        if (small_name)
            font_small = lv_xml_get_font(nullptr, small_name);
        if (xs_name)
            font_xs = lv_xml_get_font(nullptr, xs_name);
        if (heading_name)
            font_heading = lv_xml_get_font(nullptr, heading_name);
        fonts_initialized = true;
    }

    // text_small, text_xs, and text_heading all use muted color (per ui_text.cpp)
    return font == font_small || font == font_xs || font == font_heading;
}

/**
 * @brief Check if an object is inside a dialog container
 *
 * Dialogs are marked with LV_OBJ_FLAG_USER_1 in ui_dialog_xml_create().
 * Inputs inside dialogs need overlay_bg for contrast against elevated_bg dialog background.
 */
static bool is_inside_dialog(lv_obj_t* obj) {
    lv_obj_t* parent = lv_obj_get_parent(obj);
    while (parent) {
        if (lv_obj_has_flag(parent, LV_OBJ_FLAG_USER_1))
            return true;
        parent = lv_obj_get_parent(parent);
    }
    return false;
}

void theme_apply_palette_to_widget(lv_obj_t* obj, const helix::ModePalette& palette,
                                   lv_color_t text_light, lv_color_t text_dark) {
    if (!obj)
        return;

    // Parse palette colors
    lv_color_t screen_bg = theme_manager_parse_hex_color(palette.screen_bg.c_str());
    lv_color_t overlay_bg = theme_manager_parse_hex_color(palette.overlay_bg.c_str());
    lv_color_t card_bg = theme_manager_parse_hex_color(palette.card_bg.c_str());
    lv_color_t elevated_bg = theme_manager_parse_hex_color(palette.elevated_bg.c_str());
    lv_color_t border = theme_manager_parse_hex_color(palette.border.c_str());
    lv_color_t text_primary = theme_manager_parse_hex_color(palette.text.c_str());
    lv_color_t text_muted = theme_manager_parse_hex_color(palette.text_muted.c_str());
    lv_color_t primary = theme_manager_parse_hex_color(palette.primary.c_str());
    lv_color_t secondary = theme_manager_parse_hex_color(palette.secondary.c_str());
    lv_color_t tertiary = theme_manager_parse_hex_color(palette.tertiary.c_str());

    // Compute knob color: brighter of primary vs tertiary
    lv_color_t knob_color = theme_compute_more_saturated(primary, tertiary);

    // Get object name for container classification (cards, backgrounds, dividers)
    const char* obj_name = lv_obj_get_name(obj);

    // ==========================================================================
    // LABELS - Use font-based detection instead of name matching
    // ==========================================================================
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const lv_font_t* font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);

        // Skip icons (MDI font) - they use the icon variant system with shared
        // ThemeManager styles that auto-update on theme change. Setting inline
        // colors here would override variant styles (muted, secondary, etc.)
        // and HeatingIconAnimator's temperature-based colors.
        if (is_icon_font(font)) {
            return;
        }

        // Labels inside buttons get auto-contrast based on button background
        lv_obj_t* parent = lv_obj_get_parent(obj);
        if (parent && lv_obj_check_type(parent, &lv_button_class)) {
            // Button text - contrast is handled by apply_button_text_contrast on parent
            // Just skip, the button handler will update child labels
            return;
        }

        // Small/heading fonts get muted color, body fonts get primary
        if (is_muted_text_font(font)) {
            lv_obj_set_style_text_color(obj, text_muted, LV_PART_MAIN);
        } else {
            lv_obj_set_style_text_color(obj, text_primary, LV_PART_MAIN);
        }
        return;
    }

    // ==========================================================================
    // BUTTONS - background, border, and text contrast
    // ==========================================================================
    if (lv_obj_check_type(obj, &lv_button_class)) {
        // Get current button background to check if it's a "neutral" button
        lv_color_t current_bg = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
        uint8_t r = current_bg.red;
        uint8_t g = current_bg.green;
        uint8_t b = current_bg.blue;

        // Check if button is "neutral" (grayscale or very desaturated)
        // Accent buttons (primary, secondary, etc.) have colorful backgrounds
        int max_rgb = std::max({(int)r, (int)g, (int)b});
        int min_rgb = std::min({(int)r, (int)g, (int)b});
        int saturation = (max_rgb > 0) ? ((max_rgb - min_rgb) * 255 / max_rgb) : 0;

        // If saturation is low (<30), this is a neutral/gray button - apply elevated_bg
        if (saturation < 30) {
            lv_obj_set_style_bg_color(obj, elevated_bg, LV_PART_MAIN);
        }

        lv_obj_set_style_border_color(obj, border, LV_PART_MAIN);
        apply_button_text_contrast(obj, text_light, text_dark);
        return;
    }

    // ==========================================================================
    // INTERACTIVE WIDGETS - specific styling per widget type
    // ==========================================================================

    // Checkboxes - box border, inverted bg, accent checkmark
    if (lv_obj_check_type(obj, &lv_checkbox_class)) {
        lv_obj_set_style_text_color(obj, text_primary, LV_PART_MAIN);
        lv_obj_set_style_border_color(obj, border, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(obj, text_primary, LV_PART_INDICATOR);
        lv_color_t accent_color = theme_compute_more_saturated(primary, secondary);
        lv_obj_set_style_text_color(obj, accent_color, LV_PART_INDICATOR | LV_STATE_CHECKED);
        return;
    }

    // Switches - track, indicator, knob
    if (lv_obj_check_type(obj, &lv_switch_class)) {
        lv_obj_set_style_bg_color(obj, border, LV_PART_MAIN);
        lv_obj_set_style_bg_color(obj, secondary, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(obj, knob_color, LV_PART_KNOB);
        lv_obj_set_style_bg_color(obj, knob_color, LV_PART_KNOB | LV_STATE_CHECKED);
        return;
    }

    // Sliders - track, indicator, knob
    if (lv_obj_check_type(obj, &lv_slider_class)) {
        lv_obj_set_style_bg_color(obj, border, LV_PART_MAIN);
        lv_obj_set_style_bg_color(obj, secondary, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(obj, knob_color, LV_PART_KNOB);
        lv_obj_set_style_shadow_color(obj, screen_bg, LV_PART_KNOB);
        return;
    }

    // Dropdowns - background, border, text
    // Inside dialogs (elevated_bg background), use overlay_bg for contrast
    if (lv_obj_check_type(obj, &lv_dropdown_class)) {
        lv_color_t bg = is_inside_dialog(obj) ? overlay_bg : elevated_bg;
        lv_obj_set_style_bg_color(obj, bg, LV_PART_MAIN);
        lv_obj_set_style_border_color(obj, border, LV_PART_MAIN);
        lv_obj_set_style_text_color(obj, text_primary, LV_PART_MAIN);
        return;
    }

    // Textareas - background, text
    // Inside dialogs (elevated_bg background), use overlay_bg for contrast
    if (lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_color_t bg = is_inside_dialog(obj) ? overlay_bg : elevated_bg;
        lv_obj_set_style_bg_color(obj, bg, LV_PART_MAIN);
        lv_obj_set_style_text_color(obj, text_primary, LV_PART_MAIN);
        return;
    }

    // Dropdown lists (popup menus)
    if (lv_obj_check_type(obj, &lv_dropdownlist_class)) {
        lv_color_t dropdown_accent = theme_compute_more_saturated(primary, secondary);
        lv_obj_set_style_bg_color(obj, elevated_bg, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(obj, text_primary, LV_PART_MAIN);
        lv_obj_set_style_bg_color(obj, dropdown_accent, LV_PART_SELECTED);
        return;
    }

    // ==========================================================================
    // DIVIDERS - detect by structure: thin lv_obj (1-2px) with visible bg, no children
    // ==========================================================================
    if (lv_obj_check_type(obj, &lv_obj_class)) {
        int32_t w = lv_obj_get_width(obj);
        int32_t h = lv_obj_get_height(obj);
        lv_opa_t bg_opa = lv_obj_get_style_bg_opa(obj, LV_PART_MAIN);
        uint32_t child_count = lv_obj_get_child_count(obj);

        // Divider: thin (<=2px in one dimension), visible bg, no children
        bool is_thin_horizontal = (h <= 2 && w > h * 10);
        bool is_thin_vertical = (w <= 2 && h > w * 10);
        bool is_divider =
            (is_thin_horizontal || is_thin_vertical) && bg_opa > 0 && child_count == 0;

        if (is_divider) {
            lv_obj_set_style_bg_color(obj, border, LV_PART_MAIN);
            return;
        }
    }

    // ==========================================================================
    // CONTAINERS - use name hints for cards/backgrounds/dialogs
    // ==========================================================================
    if (obj_name) {
        if (strstr(obj_name, "divider") != nullptr) {
            // Named dividers (fallback for any that don't match structural detection)
            lv_obj_set_style_bg_color(obj, border, LV_PART_MAIN);
        } else if (strstr(obj_name, "card") != nullptr) {
            lv_obj_set_style_bg_color(obj, card_bg, LV_PART_MAIN);
            lv_obj_set_style_border_color(obj, border, LV_PART_MAIN);
        } else if (strstr(obj_name, "dialog") != nullptr) {
            // Modal dialogs use elevated_bg (elevated surface)
            spdlog::debug("[Theme] Applying elevated_bg to dialog: {} (color: 0x{:06X})", obj_name,
                          lv_color_to_u32(elevated_bg) & 0xFFFFFF);
            lv_obj_set_style_bg_color(obj, elevated_bg, LV_PART_MAIN);
        } else if (strstr(obj_name, "background") != nullptr ||
                   strstr(obj_name, "toast") != nullptr || strstr(obj_name, "header") != nullptr) {
            // Containers that use screen_bg background
            lv_obj_set_style_bg_color(obj, screen_bg, LV_PART_MAIN);
        }
    }
}

void theme_apply_palette_to_tree(lv_obj_t* root, const helix::ModePalette& palette,
                                 lv_color_t text_light, lv_color_t text_dark) {
    if (!root)
        return;

    // Apply to this widget
    theme_apply_palette_to_widget(root, palette, text_light, text_dark);

    // Recurse into children
    uint32_t child_count = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(root, i);
        theme_apply_palette_to_tree(child, palette, text_light, text_dark);
    }
}

void theme_apply_current_palette_to_tree(lv_obj_t* root) {
    if (!root)
        return;

    // Get the active palette based on current mode
    const helix::ModePalette& palette = use_dark_mode ? active_theme.dark : active_theme.light;

    // Get text contrast colors from globals
    const char* text_light_str = lv_xml_get_const(nullptr, "text_light");
    const char* text_dark_str = lv_xml_get_const(nullptr, "text_dark");
    lv_color_t text_light = text_light_str ? theme_manager_parse_hex_color(text_light_str)
                                           : theme_manager_parse_hex_color(palette.text.c_str());
    lv_color_t text_dark = text_dark_str ? theme_manager_parse_hex_color(text_dark_str)
                                         : theme_manager_parse_hex_color(palette.text.c_str());

    const char* root_name = lv_obj_get_name(root);
    spdlog::debug("[Theme] Applying current palette to tree root={}",
                  root_name ? root_name : "(screen)");
    theme_apply_palette_to_tree(root, palette, text_light, text_dark);
}

void theme_apply_palette_to_screen_dropdowns(const helix::ModePalette& palette) {
    // Style any screen-level popups (dropdown lists, modals, etc.)
    // These are direct children of the screen, not part of the overlay tree
    lv_color_t elevated_bg = theme_manager_parse_hex_color(palette.elevated_bg.c_str());
    lv_color_t text_color = theme_manager_parse_hex_color(palette.text.c_str());
    lv_color_t border = theme_manager_parse_hex_color(palette.border.c_str());
    lv_color_t primary = theme_manager_parse_hex_color(palette.primary.c_str());
    lv_color_t secondary = theme_manager_parse_hex_color(palette.secondary.c_str());

    // Use more saturated of primary/secondary for highlight (avoids white/gray primaries)
    lv_color_t dropdown_accent = theme_compute_more_saturated(primary, secondary);

    // Text color for selected based on accent luminance
    uint8_t lum = lv_color_luminance(dropdown_accent);
    lv_color_t selected_text = (lum > 140) ? lv_color_black() : lv_color_white();

    // Get text_light/text_dark for button contrast
    const char* text_light_str = lv_xml_get_const(nullptr, "text_light");
    const char* text_dark_str = lv_xml_get_const(nullptr, "text_dark");
    lv_color_t text_light = text_light_str ? theme_manager_parse_hex_color(text_light_str)
                                           : theme_manager_parse_hex_color(palette.text.c_str());
    lv_color_t text_dark = text_dark_str ? theme_manager_parse_hex_color(text_dark_str)
                                         : theme_manager_parse_hex_color(palette.text.c_str());

    lv_obj_t* screen = lv_screen_active();
    uint32_t child_count = lv_obj_get_child_count(screen);
    spdlog::debug("[Theme] Screen has {} children", child_count);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(screen, i);

        // Dropdown lists get special treatment for selection highlighting
        if (lv_obj_check_type(child, &lv_dropdownlist_class)) {
            lv_obj_set_style_bg_color(child, elevated_bg, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(child, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(child, text_color, LV_PART_MAIN);
            lv_obj_set_style_border_color(child, border, LV_PART_MAIN);
            lv_obj_set_style_bg_color(child, dropdown_accent, LV_PART_SELECTED);
            lv_obj_set_style_bg_opa(child, LV_OPA_COVER, LV_PART_SELECTED);
            lv_obj_set_style_text_color(child, selected_text, LV_PART_SELECTED);
            continue;
        }

        // Other screen-level children (modals, etc.) - apply palette to entire tree
        // Skip the main app layout (it's handled separately by the overlay system)
        const char* name = lv_obj_get_name(child);
        if (name && strcmp(name, "app_layout") == 0) {
            continue;
        }

        // Apply palette to this popup and all its children
        spdlog::debug("[Theme] Applying palette to screen popup: {}", name ? name : "(unnamed)");
        theme_apply_palette_to_tree(child, palette, text_light, text_dark);
    }
}

/**
 * Get theme-appropriate color variant with fallback for static colors
 *
 * First attempts to look up {base_name}_light and {base_name}_dark from globals.xml,
 * selecting the appropriate one based on current theme mode. If the theme variants
 * don't exist, falls back to {base_name} directly (for static colors like
 * warning, danger that are the same in both themes).
 *
 * @param base_name Color constant base name (e.g., "screen_bg", "warning")
 * @return Parsed color, or black (0x000000) if not found
 *
 * Example:
 *   lv_color_t bg = theme_manager_get_color("screen_bg");
 *   // Returns screen_bg_light in light mode, screen_bg_dark in dark mode
 *
 *   lv_color_t warn = theme_manager_get_color("warning");
 *   // Returns warning directly (static, no theme variants)
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
 * @param base_name Color constant base name (e.g., "screen_bg", "card_bg")
 * @param part Style part to apply to (default: LV_PART_MAIN)
 *
 * Example:
 *   theme_manager_apply_bg_color(screen, "screen_bg", LV_PART_MAIN);
 *   // Applies screen_bg_light/dark depending on theme mode
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

    const char* value = lv_xml_get_const(nullptr, token);
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
