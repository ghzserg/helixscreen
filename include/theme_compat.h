// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file theme_compat.h
 * @brief Compatibility shims for legacy theme_core API
 *
 * These functions wrap the new ThemeManager API to provide backward
 * compatibility. They will be removed once all callers are migrated.
 */

#include "theme_core.h" // For theme_palette_t

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Theme lifecycle functions
lv_theme_t* theme_core_init(lv_display_t* display, const theme_palette_t* palette, bool is_dark,
                            const lv_font_t* base_font, int32_t border_radius, int32_t border_width,
                            int32_t border_opacity);
void theme_core_update_colors(bool is_dark, const theme_palette_t* palette, int32_t border_opacity);
void theme_core_preview_colors(bool is_dark, const theme_palette_t* palette, int32_t border_radius,
                               int32_t border_opacity);

// Style getters - map to ThemeManager::get_style(StyleRole::X)
lv_style_t* theme_core_get_card_style(void);
lv_style_t* theme_core_get_dialog_style(void);
lv_style_t* theme_core_get_obj_base_style(void);
lv_style_t* theme_core_get_input_bg_style(void);
lv_style_t* theme_core_get_disabled_style(void);
lv_style_t* theme_core_get_pressed_style(void);
lv_style_t* theme_core_get_focus_ring_style(void);

// Text styles
lv_style_t* theme_core_get_text_style(void);
lv_style_t* theme_core_get_text_muted_style(void);
lv_style_t* theme_core_get_text_subtle_style(void);

// Icon styles
lv_style_t* theme_core_get_icon_text_style(void);
lv_style_t* theme_core_get_icon_muted_style(void);
lv_style_t* theme_core_get_icon_primary_style(void);
lv_style_t* theme_core_get_icon_secondary_style(void);
lv_style_t* theme_core_get_icon_tertiary_style(void);
lv_style_t* theme_core_get_icon_info_style(void);
lv_style_t* theme_core_get_icon_success_style(void);
lv_style_t* theme_core_get_icon_warning_style(void);
lv_style_t* theme_core_get_icon_danger_style(void);

// Button styles
lv_style_t* theme_core_get_button_style(void);
lv_style_t* theme_core_get_button_primary_style(void);
lv_style_t* theme_core_get_button_secondary_style(void);
lv_style_t* theme_core_get_button_tertiary_style(void);
lv_style_t* theme_core_get_button_danger_style(void);
lv_style_t* theme_core_get_button_ghost_style(void);
lv_style_t* theme_core_get_button_success_style(void);
lv_style_t* theme_core_get_button_warning_style(void);

// Severity styles
lv_style_t* theme_core_get_severity_info_style(void);
lv_style_t* theme_core_get_severity_success_style(void);
lv_style_t* theme_core_get_severity_warning_style(void);
lv_style_t* theme_core_get_severity_danger_style(void);

// Widget styles
lv_style_t* theme_core_get_dropdown_style(void);
lv_style_t* theme_core_get_checkbox_style(void);
lv_style_t* theme_core_get_switch_style(void);
lv_style_t* theme_core_get_slider_style(void);
lv_style_t* theme_core_get_spinner_style(void);
lv_style_t* theme_core_get_arc_style(void);

// Color helpers for contrast text
lv_color_t theme_core_get_text_for_dark_bg(void);
lv_color_t theme_core_get_text_for_light_bg(void);
lv_color_t theme_core_get_contrast_text_color(lv_color_t bg_color);

#ifdef __cplusplus
}
#endif
