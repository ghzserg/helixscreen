// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme_compat.h"

#include "theme_manager.h"

#include <spdlog/spdlog.h>

// Convert theme_palette_t to ThemePalette
static ThemePalette convert_palette(const theme_palette_t* p, int border_radius, int border_width,
                                    int border_opacity) {
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

// Theme lifecycle functions
lv_theme_t* theme_core_init(lv_display_t* display, const theme_palette_t* palette, bool is_dark,
                            const lv_font_t* base_font, int32_t border_radius, int32_t border_width,
                            int32_t border_opacity) {
    (void)base_font; // Font handled separately

    // Convert palette and initialize ThemeManager
    ThemePalette dark_pal = convert_palette(palette, border_radius, border_width, border_opacity);
    ThemePalette light_pal = dark_pal; // Same palette for both modes initially

    auto& tm = ThemeManager::instance();
    tm.set_palettes(light_pal, dark_pal);
    tm.init();
    tm.set_dark_mode(is_dark);

    // Create LVGL default theme and return it
    // ThemeManager handles our custom styles, LVGL handles base widget styling
    lv_theme_t* theme =
        lv_theme_default_init(display, palette->primary, palette->secondary, is_dark, base_font);
    spdlog::debug("[ThemeCompat] Initialized theme via ThemeManager");
    return theme;
}

void theme_core_update_colors(bool is_dark, const theme_palette_t* palette,
                              int32_t border_opacity) {
    auto& tm = ThemeManager::instance();
    const auto& current = tm.current_palette();

    // Convert and update both palettes
    ThemePalette new_pal =
        convert_palette(palette, current.border_radius, current.border_width, border_opacity);

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
    spdlog::debug("[ThemeCompat] Updated colors, dark_mode={}", is_dark);
}

void theme_core_preview_colors(bool is_dark, const theme_palette_t* palette, int32_t border_radius,
                               int32_t border_opacity) {
    auto& tm = ThemeManager::instance();
    const auto& current = tm.current_palette();

    ThemePalette preview_pal =
        convert_palette(palette, border_radius, current.border_width, border_opacity);
    (void)is_dark; // Preview uses the palette directly

    tm.preview_palette(preview_pal);
    spdlog::debug("[ThemeCompat] Previewing colors");
}

// Helper macro to reduce boilerplate
#define STYLE_GETTER(name, role)                                                                   \
    lv_style_t* theme_core_get_##name##_style(void) {                                              \
        return ThemeManager::instance().get_style(StyleRole::role);                                \
    }

// Base styles
STYLE_GETTER(card, Card)
STYLE_GETTER(dialog, Dialog)
STYLE_GETTER(obj_base, ObjBase)
STYLE_GETTER(input_bg, InputBg)
STYLE_GETTER(disabled, Disabled)
STYLE_GETTER(pressed, Pressed)
STYLE_GETTER(focus_ring, Focused)

// Text styles
STYLE_GETTER(text, TextPrimary)
STYLE_GETTER(text_muted, TextMuted)
STYLE_GETTER(text_subtle, TextSubtle)

// Icon styles
STYLE_GETTER(icon_text, IconText)
STYLE_GETTER(icon_muted, TextMuted) // Maps to TextMuted, same color
STYLE_GETTER(icon_primary, IconPrimary)
STYLE_GETTER(icon_secondary, IconSecondary)
STYLE_GETTER(icon_tertiary, IconTertiary)
STYLE_GETTER(icon_info, IconInfo)
STYLE_GETTER(icon_success, IconSuccess)
STYLE_GETTER(icon_warning, IconWarning)
STYLE_GETTER(icon_danger, IconDanger)

// Button styles
STYLE_GETTER(button, Button)
STYLE_GETTER(button_primary, ButtonPrimary)
STYLE_GETTER(button_secondary, ButtonSecondary)
STYLE_GETTER(button_tertiary, ButtonTertiary)
STYLE_GETTER(button_danger, ButtonDanger)
STYLE_GETTER(button_ghost, ButtonGhost)
STYLE_GETTER(button_success, ButtonSuccess)
STYLE_GETTER(button_warning, ButtonWarning)

// Severity styles
STYLE_GETTER(severity_info, SeverityInfo)
STYLE_GETTER(severity_success, SeveritySuccess)
STYLE_GETTER(severity_warning, SeverityWarning)
STYLE_GETTER(severity_danger, SeverityDanger)

// Widget styles
STYLE_GETTER(dropdown, Dropdown)
STYLE_GETTER(checkbox, Checkbox)
STYLE_GETTER(switch, Switch)
STYLE_GETTER(slider, Slider)
STYLE_GETTER(spinner, Spinner)
STYLE_GETTER(arc, Arc)

#undef STYLE_GETTER

// Color helpers
lv_color_t theme_core_get_text_for_dark_bg(void) {
    // Light text for dark backgrounds
    return lv_color_hex(0xECEFF4);
}

lv_color_t theme_core_get_text_for_light_bg(void) {
    // Dark text for light backgrounds
    return lv_color_hex(0x2E3440);
}

lv_color_t theme_core_get_contrast_text_color(lv_color_t bg_color) {
    // Compute luminance using standard formula: L = (299*R + 587*G + 114*B) / 1000
    int luminance = (299 * bg_color.red + 587 * bg_color.green + 114 * bg_color.blue) / 1000;

    // Dark bg (L < 128): use light text; Light bg (L >= 128): use dark text
    if (luminance < 128) {
        return theme_core_get_text_for_dark_bg();
    } else {
        return theme_core_get_text_for_light_bg();
    }
}
