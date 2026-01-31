// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme_manager.h"

#include <lvgl.h>

namespace style_configs {

// Base styles
void configure_card(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_bg_color(s, p.card_bg);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
    lv_style_set_border_color(s, p.border);
    lv_style_set_border_width(s, p.border_width);
    lv_style_set_border_opa(s, static_cast<lv_opa_t>(p.border_opacity * 255 / 100));
    lv_style_set_radius(s, p.border_radius);
}

void configure_dialog(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_bg_color(s, p.overlay_bg);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
    lv_style_set_radius(s, p.border_radius);
}

void configure_obj_base(lv_style_t* s, const ThemePalette& p) {
    (void)p; // Unused - obj_base is palette-independent
    lv_style_set_bg_opa(s, LV_OPA_0);
    lv_style_set_border_width(s, 0);
    lv_style_set_pad_all(s, 0);
    lv_style_set_width(s, LV_SIZE_CONTENT);
    lv_style_set_height(s, LV_SIZE_CONTENT);
}

void configure_input_bg(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_bg_color(s, p.elevated_bg);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
    lv_style_set_radius(s, p.border_radius);
}

void configure_disabled(lv_style_t* s, const ThemePalette& p) {
    (void)p;
    lv_style_set_opa(s, LV_OPA_50);
}

void configure_pressed(lv_style_t* s, const ThemePalette& p) {
    (void)p;
    lv_style_set_transform_scale_x(s, 245); // 96% scale
    lv_style_set_transform_scale_y(s, 245);
}

void configure_focused(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_outline_color(s, p.focus);
    lv_style_set_outline_width(s, 2);
    lv_style_set_outline_pad(s, 2);
    lv_style_set_outline_opa(s, LV_OPA_COVER);
}

// Text styles
void configure_text_primary(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_text_color(s, p.text);
}

void configure_text_muted(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_text_color(s, p.text_muted);
}

void configure_text_subtle(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_text_color(s, p.text_subtle);
}

// Icon styles - use text_color since icons are font-based
void configure_icon_text(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_text_color(s, p.text);
}

void configure_icon_primary(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_text_color(s, p.primary);
}

void configure_icon_secondary(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_text_color(s, p.secondary);
}

void configure_icon_tertiary(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_text_color(s, p.tertiary);
}

void configure_icon_info(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_text_color(s, p.info);
}

void configure_icon_success(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_text_color(s, p.success);
}

void configure_icon_warning(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_text_color(s, p.warning);
}

void configure_icon_danger(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_text_color(s, p.danger);
}

// Button styles
void configure_button(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_bg_color(s, p.elevated_bg);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
    lv_style_set_radius(s, p.border_radius);
}

void configure_button_primary(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_bg_color(s, p.primary);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
}

void configure_button_secondary(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_bg_color(s, p.secondary);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
}

void configure_button_tertiary(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_bg_color(s, p.tertiary);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
}

void configure_button_danger(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_bg_color(s, p.danger);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
}

void configure_button_ghost(lv_style_t* s, const ThemePalette& p) {
    (void)p;
    lv_style_set_bg_opa(s, LV_OPA_0);
}

void configure_button_success(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_bg_color(s, p.success);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
}

void configure_button_warning(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_bg_color(s, p.warning);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
}

void configure_button_disabled(lv_style_t* s, const ThemePalette& p) {
    (void)p;
    lv_style_set_opa(s, LV_OPA_50);
}

void configure_button_pressed(lv_style_t* s, const ThemePalette& p) {
    (void)p;
    lv_style_set_transform_scale_x(s, 245);
    lv_style_set_transform_scale_y(s, 245);
}

// Severity border styles
void configure_severity_info(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_border_color(s, p.info);
    lv_style_set_border_width(s, 2);
}

void configure_severity_success(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_border_color(s, p.success);
    lv_style_set_border_width(s, 2);
}

void configure_severity_warning(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_border_color(s, p.warning);
    lv_style_set_border_width(s, 2);
}

void configure_severity_danger(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_border_color(s, p.danger);
    lv_style_set_border_width(s, 2);
}

// Widget styles
void configure_dropdown(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_bg_color(s, p.elevated_bg);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
    lv_style_set_border_color(s, p.border);
    lv_style_set_border_width(s, p.border_width);
    lv_style_set_radius(s, p.border_radius);
}

void configure_checkbox(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_bg_color(s, p.elevated_bg);
    lv_style_set_border_color(s, p.border);
    lv_style_set_border_width(s, 2);
    lv_style_set_radius(s, 4);
}

void configure_switch(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_bg_color(s, p.border);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
    lv_style_set_radius(s, LV_RADIUS_CIRCLE);
}

void configure_slider(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_bg_color(s, p.border);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
    lv_style_set_radius(s, 4);
}

void configure_spinner(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_arc_color(s, p.primary);
}

void configure_arc(lv_style_t* s, const ThemePalette& p) {
    lv_style_set_arc_color(s, p.primary);
}

} // namespace style_configs
