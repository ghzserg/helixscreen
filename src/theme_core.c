// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "theme_core.h"

#include "ui_fonts.h"

#include "lvgl/src/themes/lv_theme_private.h"

#include <stdlib.h>
#include <string.h>

// HelixScreen custom theme structure
typedef struct {
    lv_theme_t base;                     // Base LVGL theme structure (MUST be first)
    lv_theme_t* default_theme;           // LVGL default theme to delegate to
    lv_style_t input_bg_style;           // Custom style for input widget backgrounds
    lv_style_t disabled_style;           // Global disabled state style (50% opacity)
    lv_style_t pressed_style;            // Global pressed state style (preserve radius)
    lv_style_t button_style;             // Default button style (grey background)
    lv_style_t dropdown_indicator_style; // Dropdown indicator font (MDI icons)
    lv_style_t checkbox_box_style;       // Checkbox indicator box (border, bg)
    lv_style_t checkbox_indicator_style; // Checkbox checkmark font + color (MDI icons)
    lv_style_t checkbox_text_style;      // Checkbox label text color
    lv_style_t switch_track_style;       // Switch track (OFF state background)
    lv_style_t switch_indicator_style;   // Switch checked state (accent color)
    lv_style_t switch_knob_style;        // Switch knob (handle) color
    lv_style_t focus_ring_style;         // Focus ring for accessibility (outline)
    lv_style_t slider_track_style;       // Slider track (unfilled portion) - border color
    lv_style_t slider_indicator_style;   // Slider indicator (filled portion) - primary color
    lv_style_t slider_knob_style;        // Slider knob with shadow
    lv_style_t slider_disabled_style;    // Slider disabled state (50% opacity)
    lv_style_t dropdown_selected_style;  // Dropdown selected item highlight
    lv_color_t
        dropdown_accent_color; // Accent color for dropdown selection (stored for apply callback)
    bool is_dark_mode;         // Track theme mode for context
} helix_theme_t;

// Static theme instance (singleton pattern matching LVGL's approach)
static helix_theme_t* helix_theme_instance = NULL;

// Button press transition descriptor (scale transform)
// Properties to animate when transitioning to/from pressed state
static const lv_style_prop_t button_press_props[] = {LV_STYLE_TRANSFORM_SCALE_X,
                                                     LV_STYLE_TRANSFORM_SCALE_Y, 0};
static lv_style_transition_dsc_t button_press_transition;

/**
 * Compute input background color from card background
 *
 * Dark mode: Lighten card bg by +22/+23/+27 RGB offset
 * Light mode: Darken card bg by -22/-23/-27 RGB offset
 *
 * @param card_bg Card background color
 * @param is_dark Dark mode flag
 * @return Computed input background color
 */
// Input widgets (dropdowns, textareas, etc.) use card_alt color
// This is passed directly rather than computed from card_bg

/**
 * Custom theme apply callback
 *
 * Delegates to LVGL default theme, then overrides input widget backgrounds
 * to use our custom computed color.
 *
 * Pattern: Apply default theme first, then selectively override specific widgets.
 */
static void helix_theme_apply(lv_theme_t* theme, lv_obj_t* obj) {
    helix_theme_t* helix = (helix_theme_t*)theme;

    // First, apply default LVGL theme to get all standard styling
    if (helix->default_theme && helix->default_theme->apply_cb) {
        helix->default_theme->apply_cb(helix->default_theme, obj);
    }

    // Apply global disabled state styling (50% opacity for all widgets)
    lv_obj_add_style(obj, &helix->disabled_style, LV_PART_MAIN | LV_STATE_DISABLED);

    // Apply button styling (grey background + radius preservation + focus ring)
#if LV_USE_BUTTON
    if (lv_obj_check_type(obj, &lv_button_class)) {
        // Default button style: grey background
        lv_obj_add_style(obj, &helix->button_style, LV_PART_MAIN);
        // Preserve radius on press
        lv_obj_add_style(obj, &helix->pressed_style, LV_PART_MAIN | LV_STATE_PRESSED);
        // Focus ring for accessibility
        lv_obj_add_style(obj, &helix->focus_ring_style, LV_STATE_FOCUSED);
    }
#endif

    // Now override input widgets to use our custom background color
#if LV_USE_TEXTAREA
    if (lv_obj_check_type(obj, &lv_textarea_class)) {
        // Remove default card background, add our custom input background
        lv_obj_add_style(obj, &helix->input_bg_style, LV_PART_MAIN);
        // Focus ring for accessibility
        lv_obj_add_style(obj, &helix->focus_ring_style, LV_STATE_FOCUSED);
    }
#endif

#if LV_USE_DROPDOWN
    if (lv_obj_check_type(obj, &lv_dropdown_class)) {
        // Dropdown button background
        lv_obj_add_style(obj, &helix->input_bg_style, LV_PART_MAIN);
        // Set MDI font for the dropdown indicator (chevron symbol)
        lv_obj_add_style(obj, &helix->dropdown_indicator_style, LV_PART_INDICATOR);
        // Focus ring for accessibility
        lv_obj_add_style(obj, &helix->focus_ring_style, LV_STATE_FOCUSED);
    }
    // Dropdown list uses input bg style to match dropdown button appearance
    if (lv_obj_check_type(obj, &lv_dropdownlist_class)) {
        lv_obj_add_style(obj, &helix->input_bg_style, LV_PART_MAIN);

        // Text color based on accent luminance (same logic as text_button)
        uint8_t lum = lv_color_luminance(helix->dropdown_accent_color);
        lv_color_t selected_text = (lum > 140) ? lv_color_black() : lv_color_white();

        // LVGL dropdown uses multiple state combinations for selection highlight:
        // - LV_STATE_CHECKED: the currently selected item
        // - LV_STATE_PRESSED: item being hovered/pressed
        // - LV_STATE_CHECKED | LV_STATE_PRESSED: selected item being pressed
        // Must set styles for ALL combinations with LV_PART_SELECTED

        // Base SELECTED part
        lv_obj_set_style_bg_color(obj, helix->dropdown_accent_color, LV_PART_SELECTED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_SELECTED);
        lv_obj_set_style_text_color(obj, selected_text, LV_PART_SELECTED);

        // CHECKED state (selected item)
        lv_obj_set_style_bg_color(obj, helix->dropdown_accent_color,
                                  LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(obj, selected_text, LV_PART_SELECTED | LV_STATE_CHECKED);

        // PRESSED state (hovered item)
        lv_obj_set_style_bg_color(obj, helix->dropdown_accent_color,
                                  LV_PART_SELECTED | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_SELECTED | LV_STATE_PRESSED);
        lv_obj_set_style_text_color(obj, selected_text, LV_PART_SELECTED | LV_STATE_PRESSED);

        // CHECKED + PRESSED state (selected item being pressed)
        lv_obj_set_style_bg_color(obj, helix->dropdown_accent_color,
                                  LV_PART_SELECTED | LV_STATE_CHECKED | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER,
                                LV_PART_SELECTED | LV_STATE_CHECKED | LV_STATE_PRESSED);
        lv_obj_set_style_text_color(obj, selected_text,
                                    LV_PART_SELECTED | LV_STATE_CHECKED | LV_STATE_PRESSED);
    }
#endif

#if LV_USE_ROLLER
    if (lv_obj_check_type(obj, &lv_roller_class)) {
        lv_obj_add_style(obj, &helix->input_bg_style, LV_PART_MAIN);
    }
#endif

#if LV_USE_SPINBOX
    if (lv_obj_check_type(obj, &lv_spinbox_class)) {
        lv_obj_add_style(obj, &helix->input_bg_style, LV_PART_MAIN);
    }
#endif

#if LV_USE_CHECKBOX
    // Checkbox theming: box style, checkmark, and label text color
    if (lv_obj_check_type(obj, &lv_checkbox_class)) {
        // Text label color - uses theme text color
        lv_obj_add_style(obj, &helix->checkbox_text_style, LV_PART_MAIN);
        // Indicator box (unchecked and checked) - border + inverted bg
        lv_obj_add_style(obj, &helix->checkbox_box_style, LV_PART_INDICATOR);
        // Checkmark (checked state only) - MDI font + accent color
        lv_obj_add_style(obj, &helix->checkbox_indicator_style,
                         LV_PART_INDICATOR | LV_STATE_CHECKED);
    }
#endif

#if LV_USE_SWITCH
    // Switch theming: track (OFF state), indicator (ON state), and knob
    if (lv_obj_check_type(obj, &lv_switch_class)) {
        // Track background (OFF state) - uses border color for visibility
        lv_obj_add_style(obj, &helix->switch_track_style, LV_PART_MAIN);
        // Indicator (ON state) - uses secondary accent color
        lv_obj_add_style(obj, &helix->switch_indicator_style, LV_PART_INDICATOR | LV_STATE_CHECKED);
        // Knob - uses primary accent color
        lv_obj_add_style(obj, &helix->switch_knob_style, LV_PART_KNOB);
        // Focus ring for accessibility
        lv_obj_add_style(obj, &helix->focus_ring_style, LV_STATE_FOCUSED);
    }
#endif

#if LV_USE_SLIDER
    // Slider theming: track, indicator, knob, and disabled states
    if (lv_obj_check_type(obj, &lv_slider_class)) {
        // Track background (unfilled portion)
        lv_obj_add_style(obj, &helix->slider_track_style, LV_PART_MAIN);
        // Indicator (filled portion)
        lv_obj_add_style(obj, &helix->slider_indicator_style, LV_PART_INDICATOR);
        // Knob with shadow
        lv_obj_add_style(obj, &helix->slider_knob_style, LV_PART_KNOB);
        // Disabled state for all parts
        lv_obj_add_style(obj, &helix->slider_disabled_style, LV_PART_MAIN | LV_STATE_DISABLED);
        lv_obj_add_style(obj, &helix->slider_disabled_style, LV_PART_INDICATOR | LV_STATE_DISABLED);
        lv_obj_add_style(obj, &helix->slider_disabled_style, LV_PART_KNOB | LV_STATE_DISABLED);
        // Focus ring for accessibility
        lv_obj_add_style(obj, &helix->focus_ring_style, LV_STATE_FOCUSED);
    }
#endif
}

lv_theme_t* theme_core_init(lv_display_t* display, lv_color_t primary_color,
                            lv_color_t secondary_color, lv_color_t text_primary_color, bool is_dark,
                            const lv_font_t* base_font, lv_color_t screen_bg, lv_color_t card_bg,
                            lv_color_t surface_control, lv_color_t focus_color,
                            lv_color_t border_color, int32_t border_radius, int32_t border_width,
                            int32_t border_opacity, lv_color_t knob_color,
                            lv_color_t accent_color) {
    // Clean up previous theme instance if exists
    if (helix_theme_instance) {
        lv_style_reset(&helix_theme_instance->input_bg_style);
        lv_style_reset(&helix_theme_instance->disabled_style);
        lv_style_reset(&helix_theme_instance->pressed_style);
        lv_style_reset(&helix_theme_instance->button_style);
        lv_style_reset(&helix_theme_instance->dropdown_indicator_style);
        lv_style_reset(&helix_theme_instance->checkbox_box_style);
        lv_style_reset(&helix_theme_instance->checkbox_indicator_style);
        lv_style_reset(&helix_theme_instance->checkbox_text_style);
        lv_style_reset(&helix_theme_instance->switch_track_style);
        lv_style_reset(&helix_theme_instance->switch_indicator_style);
        lv_style_reset(&helix_theme_instance->switch_knob_style);
        lv_style_reset(&helix_theme_instance->focus_ring_style);
        lv_style_reset(&helix_theme_instance->slider_track_style);
        lv_style_reset(&helix_theme_instance->slider_indicator_style);
        lv_style_reset(&helix_theme_instance->slider_knob_style);
        lv_style_reset(&helix_theme_instance->slider_disabled_style);
        lv_style_reset(&helix_theme_instance->dropdown_selected_style);
        free(helix_theme_instance);
        helix_theme_instance = NULL;
    }

    // Allocate new theme instance
    helix_theme_instance = (helix_theme_t*)malloc(sizeof(helix_theme_t));
    if (!helix_theme_instance) {
        return NULL;
    }
    memset(helix_theme_instance, 0, sizeof(helix_theme_t));

    // Initialize LVGL default theme (this does most of the heavy lifting)
    helix_theme_instance->default_theme =
        lv_theme_default_init(display, primary_color, secondary_color, is_dark, base_font);

    if (!helix_theme_instance->default_theme) {
        free(helix_theme_instance);
        helix_theme_instance = NULL;
        return NULL;
    }

    // Configure base theme structure
    helix_theme_instance->base.apply_cb = helix_theme_apply;
    helix_theme_instance->base.parent = NULL; // No parent theme
    helix_theme_instance->base.user_data = NULL;
    helix_theme_instance->base.disp = display;
    helix_theme_instance->base.color_primary = primary_color;
    helix_theme_instance->base.color_secondary = secondary_color;
    // Copy font settings from default theme
    helix_theme_instance->base.font_small = helix_theme_instance->default_theme->font_small;
    helix_theme_instance->base.font_normal = helix_theme_instance->default_theme->font_normal;
    helix_theme_instance->base.font_large = helix_theme_instance->default_theme->font_large;
    helix_theme_instance->base.flags = 0;
    helix_theme_instance->is_dark_mode = is_dark;

    // Input widgets use card_alt color (surface_control) for background
    // This provides consistent contrast in both light and dark modes

    // Initialize custom input background style
    lv_style_init(&helix_theme_instance->input_bg_style);
    lv_style_set_bg_color(&helix_theme_instance->input_bg_style, surface_control);
    lv_style_set_bg_opa(&helix_theme_instance->input_bg_style, LV_OPA_COVER);
    lv_style_set_text_color(&helix_theme_instance->input_bg_style, text_primary_color);
    lv_style_set_radius(&helix_theme_instance->input_bg_style, border_radius);
    lv_style_set_border_width(&helix_theme_instance->input_bg_style, border_width);
    lv_style_set_border_color(&helix_theme_instance->input_bg_style, border_color);
    lv_style_set_border_opa(&helix_theme_instance->input_bg_style, border_opacity);

    // Initialize global disabled state style (50% opacity)
    lv_style_init(&helix_theme_instance->disabled_style);
    lv_style_set_opa(&helix_theme_instance->disabled_style, LV_OPA_50);

    // Initialize global pressed state style (scale down + preserve radius for buttons)
    // 250 = ~98% of 256 (full scale), creates subtle "press in" feedback
    // Pivot point set to center (50%) so button shrinks uniformly, not toward top-left
    lv_style_init(&helix_theme_instance->pressed_style);
    lv_style_set_radius(&helix_theme_instance->pressed_style, border_radius);
    lv_style_set_transform_scale(&helix_theme_instance->pressed_style, 250);
    lv_style_set_transform_pivot_x(&helix_theme_instance->pressed_style, LV_PCT(50));
    lv_style_set_transform_pivot_y(&helix_theme_instance->pressed_style, LV_PCT(50));

    // Initialize button press transition (80ms press, 120ms release with slight overshoot)
    lv_style_transition_dsc_init(&button_press_transition, button_press_props,
                                 lv_anim_path_ease_out, 80, 0, NULL);

    // Initialize default button style (grey background with border radius, theme border,
    // theme-aware text color) Pivot point set to center (50%) so release animation stays centered
    // (matches pressed_style)
    lv_style_init(&helix_theme_instance->button_style);
    lv_style_set_bg_color(&helix_theme_instance->button_style, surface_control);
    lv_style_set_bg_opa(&helix_theme_instance->button_style, LV_OPA_COVER);
    lv_style_set_radius(&helix_theme_instance->button_style, border_radius);
    lv_style_set_border_width(&helix_theme_instance->button_style, border_width);
    lv_style_set_border_color(&helix_theme_instance->button_style, border_color);
    lv_style_set_border_opa(&helix_theme_instance->button_style, border_opacity);
    lv_style_set_shadow_width(&helix_theme_instance->button_style, 0);
    // NOTE: text_color intentionally NOT set on button_style
    // Button text color is handled by text_button component with auto-contrast logic
    lv_style_set_transform_pivot_x(&helix_theme_instance->button_style, LV_PCT(50));
    lv_style_set_transform_pivot_y(&helix_theme_instance->button_style, LV_PCT(50));
    lv_style_set_transition(&helix_theme_instance->button_style, &button_press_transition);

    // Initialize dropdown indicator style with MDI icon font
    // This ensures LV_SYMBOL_DOWN (overridden to MDI chevron-down in lv_conf.h) renders correctly
    lv_style_init(&helix_theme_instance->dropdown_indicator_style);
    lv_style_set_text_font(&helix_theme_instance->dropdown_indicator_style, &mdi_icons_24);

    // Initialize checkbox box style (the indicator square)
    // Box has border color and text_primary background (inverted for contrast)
    lv_style_init(&helix_theme_instance->checkbox_box_style);
    lv_style_set_border_color(&helix_theme_instance->checkbox_box_style, border_color);
    lv_style_set_border_width(&helix_theme_instance->checkbox_box_style, 1);
    lv_style_set_bg_color(&helix_theme_instance->checkbox_box_style, text_primary_color);
    lv_style_set_bg_opa(&helix_theme_instance->checkbox_box_style, LV_OPA_COVER);

    // Initialize checkbox indicator style (checked state with checkmark)
    // MDI font for checkmark symbol + accent color for the check
    lv_style_init(&helix_theme_instance->checkbox_indicator_style);
    lv_style_set_text_font(&helix_theme_instance->checkbox_indicator_style, &mdi_icons_16);
    lv_style_set_text_color(&helix_theme_instance->checkbox_indicator_style, accent_color);

    // Initialize checkbox text style - ensure label uses theme text color
    lv_style_init(&helix_theme_instance->checkbox_text_style);
    lv_style_set_text_color(&helix_theme_instance->checkbox_text_style, text_primary_color);

    // Initialize switch track style (OFF state background) - uses border color for visibility
    lv_style_init(&helix_theme_instance->switch_track_style);
    lv_style_set_bg_color(&helix_theme_instance->switch_track_style, border_color);
    lv_style_set_bg_opa(&helix_theme_instance->switch_track_style, LV_OPA_COVER);

    // Initialize switch indicator style (ON state) - uses secondary accent color
    lv_style_init(&helix_theme_instance->switch_indicator_style);
    lv_style_set_bg_color(&helix_theme_instance->switch_indicator_style, secondary_color);

    // Initialize switch knob style - uses computed knob color (brighter of primary/tertiary)
    lv_style_init(&helix_theme_instance->switch_knob_style);
    lv_style_set_bg_color(&helix_theme_instance->switch_knob_style, knob_color);

    // Initialize focus ring style for accessibility
    // Uses outline (not border) to avoid layout shift
    lv_style_init(&helix_theme_instance->focus_ring_style);
    lv_style_set_outline_color(&helix_theme_instance->focus_ring_style, focus_color);
    lv_style_set_outline_width(&helix_theme_instance->focus_ring_style, 2);
    lv_style_set_outline_opa(&helix_theme_instance->focus_ring_style, LV_OPA_COVER);
    lv_style_set_outline_pad(&helix_theme_instance->focus_ring_style, 2);

    // Initialize slider track style (unfilled portion) - uses border color
    lv_style_init(&helix_theme_instance->slider_track_style);
    lv_style_set_bg_color(&helix_theme_instance->slider_track_style, border_color);
    lv_style_set_bg_opa(&helix_theme_instance->slider_track_style, LV_OPA_COVER);

    // Initialize slider indicator style (filled portion) - uses secondary accent color
    lv_style_init(&helix_theme_instance->slider_indicator_style);
    lv_style_set_bg_color(&helix_theme_instance->slider_indicator_style, secondary_color);
    lv_style_set_bg_opa(&helix_theme_instance->slider_indicator_style, LV_OPA_COVER);

    // Initialize slider knob style - uses computed knob color with shadow using screen_bg
    lv_style_init(&helix_theme_instance->slider_knob_style);
    lv_style_set_bg_color(&helix_theme_instance->slider_knob_style, knob_color);
    lv_style_set_bg_opa(&helix_theme_instance->slider_knob_style, LV_OPA_COVER);
    lv_style_set_shadow_color(&helix_theme_instance->slider_knob_style, screen_bg);
    lv_style_set_shadow_width(&helix_theme_instance->slider_knob_style, 4);
    lv_style_set_shadow_opa(&helix_theme_instance->slider_knob_style, LV_OPA_50);

    // Initialize slider disabled style - 50% opacity
    lv_style_init(&helix_theme_instance->slider_disabled_style);
    lv_style_set_opa(&helix_theme_instance->slider_disabled_style, LV_OPA_50);

    // Initialize dropdown selected item style - uses accent_color (more saturated of
    // primary/secondary)
    lv_style_init(&helix_theme_instance->dropdown_selected_style);
    lv_style_set_bg_color(&helix_theme_instance->dropdown_selected_style, accent_color);
    lv_style_set_bg_opa(&helix_theme_instance->dropdown_selected_style, LV_OPA_COVER);
    // Store accent color for use in apply callback (local style override)
    helix_theme_instance->dropdown_accent_color = accent_color;

    // CRITICAL: Now we need to patch the default theme's color fields
    // This is necessary because LVGL's default theme bakes colors into pre-computed
    // styles during init. We must update both the theme color fields AND the styles.
    // This mirrors the approach in theme_manager_patch_colors() but is cleaner since
    // we control the theme lifecycle.

    // Access internal default theme structure to patch colors
    // NOTE: This uses LVGL private API - may need updates when upgrading LVGL
    typedef enum {
        DISP_SMALL = 0,
        DISP_MEDIUM = 1,
        DISP_LARGE = 2,
    } disp_size_t;

    typedef struct {
        lv_style_t scr;
        lv_style_t scrollbar;
        lv_style_t scrollbar_scrolled;
        lv_style_t card;
        lv_style_t btn;
        // We only need to access card style, but include others for alignment
        // Full structure defined in lv_theme_default.c
    } theme_styles_partial_t;

    typedef struct {
        lv_theme_t base;
        disp_size_t disp_size;
        int32_t disp_dpi;
        lv_color_t color_scr;
        lv_color_t color_text;
        lv_color_t color_card;
        lv_color_t color_grey;
        bool inited;
        theme_styles_partial_t styles; // Partial - only what we need to access
    } default_theme_t;

    default_theme_t* def_theme = (default_theme_t*)helix_theme_instance->default_theme;

    // Update theme color fields
    def_theme->color_scr = screen_bg;
    def_theme->color_card = card_bg;
    def_theme->color_grey = surface_control;

    // Update pre-computed style colors
    // Screen background
    lv_style_set_bg_color(&def_theme->styles.scr, screen_bg);

    // Card backgrounds (multiple styles use this)
    lv_style_set_bg_color(&def_theme->styles.card, card_bg);

    // Button background and radius
    lv_style_set_bg_color(&def_theme->styles.btn, surface_control);
    lv_style_set_radius(&def_theme->styles.btn, border_radius);

    return (lv_theme_t*)helix_theme_instance;
}

void theme_core_update_colors(bool is_dark, lv_color_t screen_bg, lv_color_t card_bg,
                              lv_color_t surface_control, lv_color_t text_primary_color,
                              lv_color_t focus_color, lv_color_t primary_color,
                              lv_color_t secondary_color, lv_color_t border_color,
                              int32_t border_opacity, lv_color_t knob_color,
                              lv_color_t accent_color) {
    if (!helix_theme_instance) {
        return;
    }

    // Update our custom styles in-place
    helix_theme_instance->is_dark_mode = is_dark;

    // Input widgets use card_alt color (surface_control) and text_primary for text
    lv_style_set_bg_color(&helix_theme_instance->input_bg_style, surface_control);
    lv_style_set_text_color(&helix_theme_instance->input_bg_style, text_primary_color);
    lv_style_set_border_color(&helix_theme_instance->input_bg_style, border_color);
    lv_style_set_border_opa(&helix_theme_instance->input_bg_style, border_opacity);

    // Update button style colors (text_color handled by text_button auto-contrast)
    lv_style_set_bg_color(&helix_theme_instance->button_style, surface_control);
    lv_style_set_border_color(&helix_theme_instance->button_style, border_color);
    lv_style_set_border_opa(&helix_theme_instance->button_style, border_opacity);

    // Update checkbox styles
    lv_style_set_text_color(&helix_theme_instance->checkbox_text_style, text_primary_color);
    lv_style_set_border_color(&helix_theme_instance->checkbox_box_style, border_color);
    lv_style_set_bg_color(&helix_theme_instance->checkbox_box_style, text_primary_color);
    lv_style_set_text_color(&helix_theme_instance->checkbox_indicator_style, accent_color);

    // Update switch colors (track=border, indicator=secondary, knob=computed brighter color)
    lv_style_set_bg_color(&helix_theme_instance->switch_track_style, border_color);
    lv_style_set_bg_color(&helix_theme_instance->switch_indicator_style, secondary_color);
    lv_style_set_bg_color(&helix_theme_instance->switch_knob_style, knob_color);

    // Update focus ring color
    lv_style_set_outline_color(&helix_theme_instance->focus_ring_style, focus_color);

    // Update slider styles (indicator=secondary, knob=computed brighter color)
    lv_style_set_bg_color(&helix_theme_instance->slider_track_style, border_color);
    lv_style_set_bg_color(&helix_theme_instance->slider_indicator_style, secondary_color);
    lv_style_set_bg_color(&helix_theme_instance->slider_knob_style, knob_color);
    lv_style_set_shadow_color(&helix_theme_instance->slider_knob_style, screen_bg);

    // Update dropdown selected item style - uses accent_color (more saturated of primary/secondary)
    lv_style_set_bg_color(&helix_theme_instance->dropdown_selected_style, accent_color);
    helix_theme_instance->dropdown_accent_color = accent_color;

    // Update LVGL default theme's internal styles
    // This is the same private API access pattern used in theme_core_init
    typedef enum {
        DISP_SMALL = 0,
        DISP_MEDIUM = 1,
        DISP_LARGE = 2,
    } disp_size_t;

    typedef struct {
        lv_style_t scr;
        lv_style_t scrollbar;
        lv_style_t scrollbar_scrolled;
        lv_style_t card;
        lv_style_t btn;
    } theme_styles_partial_t;

    typedef struct {
        lv_theme_t base;
        disp_size_t disp_size;
        int32_t disp_dpi;
        lv_color_t color_scr;
        lv_color_t color_text;
        lv_color_t color_card;
        lv_color_t color_grey;
        bool inited;
        theme_styles_partial_t styles;
    } default_theme_t;

    default_theme_t* def_theme = (default_theme_t*)helix_theme_instance->default_theme;

    // Update theme color fields
    def_theme->color_scr = screen_bg;
    def_theme->color_card = card_bg;
    def_theme->color_grey = surface_control;
    def_theme->color_text = text_primary_color;

    // Update pre-computed style colors
    lv_style_set_bg_color(&def_theme->styles.scr, screen_bg);
    lv_style_set_text_color(&def_theme->styles.scr, text_primary_color);
    lv_style_set_bg_color(&def_theme->styles.card, card_bg);
    lv_style_set_bg_color(&def_theme->styles.btn, surface_control);

    // Notify LVGL that all styles have changed - triggers refresh cascade
    // NULL means "all styles changed", which forces a complete style recalculation
    lv_obj_report_style_change(NULL);
}

void theme_core_preview_colors(bool is_dark, const char* colors[16], int32_t border_radius,
                               int32_t border_opacity) {
    if (!helix_theme_instance) {
        return;
    }

    // Parse palette colors
    // 0: bg_darkest, 1: bg_dark, 2: bg_dark_highlight, 3: card_alt
    // 4: text_subtle, 5: bg_light, 6: bg_lightest, 7: accent_highlight
    // 8-10: accents, 11-15: status

    lv_color_t screen_bg = is_dark ? lv_color_hex(strtoul(colors[0] + 1, NULL, 16)) : // bg_darkest
                               lv_color_hex(strtoul(colors[6] + 1, NULL, 16));        // bg_lightest

    lv_color_t card_bg = is_dark ? lv_color_hex(strtoul(colors[1] + 1, NULL, 16)) : // bg_dark
                             lv_color_hex(strtoul(colors[5] + 1, NULL, 16));        // bg_light

    // card_alt used for both light and dark (consistent input field backgrounds)
    lv_color_t card_alt = lv_color_hex(strtoul(colors[3] + 1, NULL, 16));

    lv_color_t text_primary = is_dark ? lv_color_hex(strtoul(colors[6] + 1, NULL, 16))
                                      :                                           // bg_lightest
                                  lv_color_hex(strtoul(colors[0] + 1, NULL, 16)); // bg_darkest

    // Update the helix_theme instance
    helix_theme_instance->is_dark_mode = is_dark;

    // Input widgets use card_alt color and text_primary for text
    lv_style_set_bg_color(&helix_theme_instance->input_bg_style, card_alt);
    lv_style_set_text_color(&helix_theme_instance->input_bg_style, text_primary);
    lv_style_set_radius(&helix_theme_instance->input_bg_style, border_radius);
    lv_style_set_border_opa(&helix_theme_instance->input_bg_style, border_opacity);

    // Update button style (text_color handled by text_button auto-contrast)
    lv_style_set_bg_color(&helix_theme_instance->button_style, card_alt);
    lv_style_set_radius(&helix_theme_instance->button_style, border_radius);
    lv_style_set_radius(&helix_theme_instance->pressed_style, border_radius);
    lv_style_set_border_opa(&helix_theme_instance->button_style, border_opacity);

    // Parse accent colors: colors[8]=primary, colors[9]=secondary, colors[10]=tertiary
    lv_color_t primary_accent = lv_color_hex(strtoul(colors[8] + 1, NULL, 16));
    lv_color_t secondary_accent = lv_color_hex(strtoul(colors[9] + 1, NULL, 16));
    lv_color_t tertiary_accent = lv_color_hex(strtoul(colors[10] + 1, NULL, 16));

    // Compute knob color: brighter of secondary vs tertiary
    // Brightness = 0.299*R + 0.587*G + 0.114*B
    uint32_t sec_rgb = strtoul(colors[9] + 1, NULL, 16);
    uint32_t ter_rgb = strtoul(colors[10] + 1, NULL, 16);
    int sec_bright =
        (299 * ((sec_rgb >> 16) & 0xFF) + 587 * ((sec_rgb >> 8) & 0xFF) + 114 * (sec_rgb & 0xFF)) /
        1000;
    int ter_bright =
        (299 * ((ter_rgb >> 16) & 0xFF) + 587 * ((ter_rgb >> 8) & 0xFF) + 114 * (ter_rgb & 0xFF)) /
        1000;
    lv_color_t knob_accent = (ter_bright > sec_bright) ? tertiary_accent : secondary_accent;

    // For switch/slider track: use text_subtle (4) as approximate border color
    lv_color_t border_approx = lv_color_hex(strtoul(colors[4] + 1, NULL, 16));

    // Update switch colors (track=border, indicator=secondary, knob=brighter accent)
    lv_style_set_bg_color(&helix_theme_instance->switch_track_style, border_approx);
    lv_style_set_bg_color(&helix_theme_instance->switch_indicator_style, secondary_accent);
    lv_style_set_bg_color(&helix_theme_instance->switch_knob_style, knob_accent);

    // Update focus ring color (colors[15] is focus)
    lv_color_t focus_color = lv_color_hex(strtoul(colors[15] + 1, NULL, 16));
    lv_style_set_outline_color(&helix_theme_instance->focus_ring_style, focus_color);

    // Update slider styles (track=border, indicator=secondary, knob=brighter accent)
    lv_style_set_bg_color(&helix_theme_instance->slider_track_style, border_approx);
    lv_style_set_bg_color(&helix_theme_instance->slider_indicator_style, secondary_accent);
    lv_style_set_bg_color(&helix_theme_instance->slider_knob_style, knob_accent);
    lv_style_set_shadow_color(&helix_theme_instance->slider_knob_style, screen_bg);

    // Update dropdown selected item style - uses more saturated of primary/secondary for highlight
    // Compute saturation for both colors to pick the more vivid one
    int pri_sat =
        (primary_accent.red > primary_accent.green)
            ? (primary_accent.red > primary_accent.blue ? primary_accent.red : primary_accent.blue)
            : (primary_accent.green > primary_accent.blue ? primary_accent.green
                                                          : primary_accent.blue);
    int pri_min =
        (primary_accent.red < primary_accent.green)
            ? (primary_accent.red < primary_accent.blue ? primary_accent.red : primary_accent.blue)
            : (primary_accent.green < primary_accent.blue ? primary_accent.green
                                                          : primary_accent.blue);
    int sec_sat = (secondary_accent.red > secondary_accent.green)
                      ? (secondary_accent.red > secondary_accent.blue ? secondary_accent.red
                                                                      : secondary_accent.blue)
                      : (secondary_accent.green > secondary_accent.blue ? secondary_accent.green
                                                                        : secondary_accent.blue);
    int sec_min = (secondary_accent.red < secondary_accent.green)
                      ? (secondary_accent.red < secondary_accent.blue ? secondary_accent.red
                                                                      : secondary_accent.blue)
                      : (secondary_accent.green < secondary_accent.blue ? secondary_accent.green
                                                                        : secondary_accent.blue);
    int pri_range = pri_sat - pri_min;
    int sec_range = sec_sat - sec_min;
    lv_color_t dropdown_accent = (pri_range >= sec_range) ? primary_accent : secondary_accent;
    lv_style_set_bg_color(&helix_theme_instance->dropdown_selected_style, dropdown_accent);
    helix_theme_instance->dropdown_accent_color = dropdown_accent;

    // Update default theme internal styles (private API access)
    typedef struct {
        lv_style_t scr;
        lv_style_t scrollbar;
        lv_style_t scrollbar_scrolled;
        lv_style_t card;
        lv_style_t btn;
    } theme_styles_partial_t;

    typedef struct {
        lv_theme_t base;
        int disp_size;
        int32_t disp_dpi;
        lv_color_t color_scr;
        lv_color_t color_text;
        lv_color_t color_card;
        lv_color_t color_grey;
        bool inited;
        theme_styles_partial_t styles;
    } default_theme_t;

    default_theme_t* def_theme = (default_theme_t*)helix_theme_instance->default_theme;

    def_theme->color_scr = screen_bg;
    def_theme->color_card = card_bg;
    def_theme->color_grey = card_alt;
    def_theme->color_text = text_primary;

    lv_style_set_bg_color(&def_theme->styles.scr, screen_bg);
    lv_style_set_text_color(&def_theme->styles.scr, text_primary);
    lv_style_set_bg_color(&def_theme->styles.card, card_bg);
    lv_style_set_bg_color(&def_theme->styles.btn, card_alt);
    lv_style_set_radius(&def_theme->styles.btn, border_radius);

    // Trigger style refresh
    lv_obj_report_style_change(NULL);
}
