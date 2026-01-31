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
    lv_style_t card_style;               // Shared card style (bg_color, border, radius)
    lv_style_t dialog_style;             // Shared dialog style (elevated_bg bg)
    lv_style_t obj_base_style;           // Base lv_obj style (transparent, no border)
    lv_style_t text_primary_style;       // Shared primary text style (text_color)
    lv_style_t text_muted_style;         // Shared muted text style (text_muted_color)
    lv_style_t text_subtle_style;        // Shared subtle text style (text_subtle_color)
    // Icon styles (Phase 2.1) - mirror text styles but for icon coloring
    lv_style_t icon_text_style;      // Icon using text_primary_color
    lv_style_t icon_muted_style;     // Icon using text_muted_color
    lv_style_t icon_primary_style;   // Icon using primary_color (accent)
    lv_style_t icon_secondary_style; // Icon using secondary_color
    lv_style_t icon_tertiary_style;  // Icon using text_subtle_color
    lv_style_t icon_success_style;   // Icon for success state (green)
    lv_style_t icon_warning_style;   // Icon for warning state (amber)
    lv_style_t icon_danger_style;    // Icon for danger state (red)
    lv_style_t icon_info_style;      // Icon for info state (blue)
    // Spinner style (Phase 2.3)
    lv_style_t spinner_style; // Spinner arc color (primary)
    // Severity styles (Phase 2.3) - for severity_card border colors
    lv_style_t severity_info_style;
    lv_style_t severity_success_style;
    lv_style_t severity_warning_style;
    lv_style_t severity_danger_style;
    // Button styles (Phase 2.6a) - bg_color only, text handled separately
    lv_style_t button_primary_style;   // Primary button (primary color bg)
    lv_style_t button_secondary_style; // Secondary button (surface color bg)
    lv_style_t button_danger_style;    // Danger button (danger color bg)
    lv_style_t button_ghost_style;     // Ghost button (transparent bg)
    lv_style_t button_success_style;   // Success button (success color bg)
    lv_style_t button_tertiary_style;  // Tertiary button (tertiary color bg)
    lv_style_t button_warning_style;   // Warning button (warning color bg)
    lv_color_t
        dropdown_accent_color; // Accent color for dropdown selection (stored for apply callback)
    bool is_dark_mode;         // Track theme mode for context
} helix_theme_t;

// Static theme instance (singleton pattern matching LVGL's approach)
static helix_theme_t* helix_theme_instance = NULL;

/**
 * Compute saturation of a color (0-255 scale)
 *
 * Uses the HSL saturation formula: S = (max - min) / (max + min) for L <= 0.5
 * or S = (max - min) / (2 - max - min) for L > 0.5
 *
 * @param color The color to compute saturation for
 * @return Saturation value 0-255
 */
static uint8_t compute_color_saturation(lv_color_t color) {
    uint8_t r = color.red;
    uint8_t g = color.green;
    uint8_t b = color.blue;

    uint8_t max_val = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
    uint8_t min_val = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);

    if (max_val == min_val) {
        return 0; // Achromatic (gray)
    }

    uint16_t delta = max_val - min_val;
    uint16_t sum = max_val + min_val;

    // HSL saturation: scale by lightness
    // For L <= 0.5: S = delta / sum
    // For L > 0.5: S = delta / (510 - sum)
    uint16_t divisor = (sum <= 255) ? sum : (510 - sum);
    if (divisor == 0)
        divisor = 1; // Avoid division by zero

    return (uint8_t)((delta * 255) / divisor);
}

/**
 * Pick the more saturated of two colors
 *
 * Useful for accent colors where you want the more vivid/colorful option.
 * This fixes issues like gray knobs in themes with desaturated primary colors.
 *
 * @param a First color
 * @param b Second color
 * @return Whichever color has higher saturation
 */
static lv_color_t pick_more_saturated(lv_color_t a, lv_color_t b) {
    return compute_color_saturation(a) >= compute_color_saturation(b) ? a : b;
}

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
// Input widgets (dropdowns, textareas, etc.) use elevated_bg color
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

    // Apply transparent background and no border to plain lv_obj containers
    // This makes lv_obj act as a layout container by default (no visual styling)
    // Specialized widgets like buttons, cards, inputs override this as needed
    if (lv_obj_check_type(obj, &lv_obj_class)) {
        lv_obj_add_style(obj, &helix->obj_base_style, LV_PART_MAIN);
    }

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

lv_theme_t* theme_core_init(lv_display_t* display, const theme_palette_t* palette, bool is_dark,
                            const lv_font_t* base_font, int32_t border_radius, int32_t border_width,
                            int32_t border_opacity) {
    // Extract colors from palette for readability
    lv_color_t screen_bg = palette->screen_bg;
    lv_color_t card_bg = palette->card_bg;
    lv_color_t elevated_bg = palette->elevated_bg;
    lv_color_t border_color = palette->border;
    lv_color_t text_primary_color = palette->text;
    lv_color_t text_muted_color = palette->text_muted;
    lv_color_t text_subtle_color = palette->text_subtle;
    lv_color_t primary_color = palette->primary;
    lv_color_t secondary_color = palette->secondary;
    lv_color_t tertiary_color = palette->tertiary;
    lv_color_t info_color = palette->info;
    lv_color_t success_color = palette->success;
    lv_color_t warning_color = palette->warning;
    lv_color_t danger_color = palette->danger;
    lv_color_t focus_color = palette->focus;

    // Derive accent colors from palette (fixes gray knobs in themes like ChatGPT)
    // knob_color: more saturated of primary vs tertiary (for switch/slider knobs)
    // accent_color: more saturated of primary vs secondary (for checkboxes, dropdowns)
    lv_color_t knob_color = pick_more_saturated(primary_color, tertiary_color);
    lv_color_t accent_color = pick_more_saturated(primary_color, secondary_color);

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
        lv_style_reset(&helix_theme_instance->card_style);
        lv_style_reset(&helix_theme_instance->dialog_style);
        lv_style_reset(&helix_theme_instance->obj_base_style);
        lv_style_reset(&helix_theme_instance->text_primary_style);
        lv_style_reset(&helix_theme_instance->text_muted_style);
        lv_style_reset(&helix_theme_instance->text_subtle_style);
        // Reset icon styles (Phase 2.1)
        lv_style_reset(&helix_theme_instance->icon_text_style);
        lv_style_reset(&helix_theme_instance->icon_muted_style);
        lv_style_reset(&helix_theme_instance->icon_primary_style);
        lv_style_reset(&helix_theme_instance->icon_secondary_style);
        lv_style_reset(&helix_theme_instance->icon_tertiary_style);
        lv_style_reset(&helix_theme_instance->icon_success_style);
        lv_style_reset(&helix_theme_instance->icon_warning_style);
        lv_style_reset(&helix_theme_instance->icon_danger_style);
        lv_style_reset(&helix_theme_instance->icon_info_style);
        // Reset spinner and severity styles (Phase 2.3)
        lv_style_reset(&helix_theme_instance->spinner_style);
        lv_style_reset(&helix_theme_instance->severity_info_style);
        lv_style_reset(&helix_theme_instance->severity_success_style);
        lv_style_reset(&helix_theme_instance->severity_warning_style);
        lv_style_reset(&helix_theme_instance->severity_danger_style);
        // Reset button styles (Phase 2.6a)
        lv_style_reset(&helix_theme_instance->button_primary_style);
        lv_style_reset(&helix_theme_instance->button_secondary_style);
        lv_style_reset(&helix_theme_instance->button_danger_style);
        lv_style_reset(&helix_theme_instance->button_ghost_style);
        lv_style_reset(&helix_theme_instance->button_success_style);
        lv_style_reset(&helix_theme_instance->button_tertiary_style);
        lv_style_reset(&helix_theme_instance->button_warning_style);
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

    // Input widgets use elevated_bg color (elevated_bg) for background
    // This provides consistent contrast in both light and dark modes

    // Initialize custom input background style
    lv_style_init(&helix_theme_instance->input_bg_style);
    lv_style_set_bg_color(&helix_theme_instance->input_bg_style, elevated_bg);
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
    lv_style_set_bg_color(&helix_theme_instance->button_style, elevated_bg);
    lv_style_set_bg_opa(&helix_theme_instance->button_style, LV_OPA_COVER);
    lv_style_set_radius(&helix_theme_instance->button_style, border_radius);
    lv_style_set_border_width(&helix_theme_instance->button_style, border_width);
    lv_style_set_border_color(&helix_theme_instance->button_style, border_color);
    lv_style_set_border_opa(&helix_theme_instance->button_style, border_opacity);
    lv_style_set_shadow_width(&helix_theme_instance->button_style, 0);
    lv_style_set_text_color(&helix_theme_instance->button_style, text_primary_color);
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

    // Initialize shared card style - for ui_card and similar widgets
    lv_style_init(&helix_theme_instance->card_style);
    lv_style_set_bg_color(&helix_theme_instance->card_style, card_bg);
    lv_style_set_bg_opa(&helix_theme_instance->card_style, LV_OPA_COVER);
    lv_style_set_border_color(&helix_theme_instance->card_style, border_color);
    lv_style_set_border_width(&helix_theme_instance->card_style, border_width);
    lv_style_set_border_opa(&helix_theme_instance->card_style, (lv_opa_t)border_opacity);
    lv_style_set_radius(&helix_theme_instance->card_style, border_radius);

    // Initialize shared dialog style - for ui_dialog and modal backgrounds
    lv_style_init(&helix_theme_instance->dialog_style);
    lv_style_set_bg_color(&helix_theme_instance->dialog_style, elevated_bg);
    lv_style_set_bg_opa(&helix_theme_instance->dialog_style, LV_OPA_COVER);
    lv_style_set_radius(&helix_theme_instance->dialog_style, border_radius);

    // Initialize base lv_obj style - transparent with no border by default
    // This eliminates the need for style_bg_opa="0" style_border_width="0" on every container
    lv_style_init(&helix_theme_instance->obj_base_style);
    lv_style_set_bg_opa(&helix_theme_instance->obj_base_style, LV_OPA_0);
    lv_style_set_border_width(&helix_theme_instance->obj_base_style, 0);

    // Initialize shared primary text style - for text labels
    lv_style_init(&helix_theme_instance->text_primary_style);
    lv_style_set_text_color(&helix_theme_instance->text_primary_style, text_primary_color);

    // Initialize shared muted text style - for secondary/muted text
    lv_style_init(&helix_theme_instance->text_muted_style);
    lv_style_set_text_color(&helix_theme_instance->text_muted_style, text_muted_color);

    // Initialize shared subtle text style - even more muted than text_muted
    lv_style_init(&helix_theme_instance->text_subtle_style);
    lv_style_set_text_color(&helix_theme_instance->text_subtle_style, text_subtle_color);

    // Initialize icon styles (Phase 2.1)
    // Icons are font-based labels, so they use text_color for their color

    // icon_text_style uses text_primary_color (same as text)
    lv_style_init(&helix_theme_instance->icon_text_style);
    lv_style_set_text_color(&helix_theme_instance->icon_text_style, text_primary_color);

    // icon_muted_style uses text_muted_color
    lv_style_init(&helix_theme_instance->icon_muted_style);
    lv_style_set_text_color(&helix_theme_instance->icon_muted_style, text_muted_color);

    // icon_primary_style uses primary_color (accent/brand color)
    lv_style_init(&helix_theme_instance->icon_primary_style);
    lv_style_set_text_color(&helix_theme_instance->icon_primary_style, primary_color);

    // icon_secondary_style uses secondary_color
    lv_style_init(&helix_theme_instance->icon_secondary_style);
    lv_style_set_text_color(&helix_theme_instance->icon_secondary_style, secondary_color);

    // icon_tertiary_style uses text_subtle_color
    lv_style_init(&helix_theme_instance->icon_tertiary_style);
    lv_style_set_text_color(&helix_theme_instance->icon_tertiary_style, text_subtle_color);

    // Semantic icon styles - using palette colors

    // icon_success_style - uses success color from palette
    lv_style_init(&helix_theme_instance->icon_success_style);
    lv_style_set_text_color(&helix_theme_instance->icon_success_style, success_color);

    // icon_warning_style - uses warning color from palette
    lv_style_init(&helix_theme_instance->icon_warning_style);
    lv_style_set_text_color(&helix_theme_instance->icon_warning_style, warning_color);

    // icon_danger_style - uses danger color from palette
    lv_style_init(&helix_theme_instance->icon_danger_style);
    lv_style_set_text_color(&helix_theme_instance->icon_danger_style, danger_color);

    // icon_info_style - uses info color from palette
    lv_style_init(&helix_theme_instance->icon_info_style);
    lv_style_set_text_color(&helix_theme_instance->icon_info_style, info_color);

    // Initialize spinner style (Phase 2.3)
    // Spinner uses arc_color for the indicator arc (uses primary_color)
    lv_style_init(&helix_theme_instance->spinner_style);
    lv_style_set_arc_color(&helix_theme_instance->spinner_style, primary_color);

    // Initialize severity styles (Phase 2.3)
    // Severity styles use border_color for severity_card borders
    // Using palette colors for consistency with icon semantic colors
    lv_style_init(&helix_theme_instance->severity_info_style);
    lv_style_set_border_color(&helix_theme_instance->severity_info_style, info_color);

    lv_style_init(&helix_theme_instance->severity_success_style);
    lv_style_set_border_color(&helix_theme_instance->severity_success_style, success_color);

    lv_style_init(&helix_theme_instance->severity_warning_style);
    lv_style_set_border_color(&helix_theme_instance->severity_warning_style, warning_color);

    lv_style_init(&helix_theme_instance->severity_danger_style);
    lv_style_set_border_color(&helix_theme_instance->severity_danger_style, danger_color);

    // Initialize button styles (Phase 2.6a)
    // Button styles set bg_color only - text color handled by button widget

    // button_primary_style - uses primary_color as background
    lv_style_init(&helix_theme_instance->button_primary_style);
    lv_style_set_bg_color(&helix_theme_instance->button_primary_style, primary_color);
    lv_style_set_bg_opa(&helix_theme_instance->button_primary_style, LV_OPA_COVER);

    // button_secondary_style - uses elevated_bg as background
    lv_style_init(&helix_theme_instance->button_secondary_style);
    lv_style_set_bg_color(&helix_theme_instance->button_secondary_style, elevated_bg);
    lv_style_set_bg_opa(&helix_theme_instance->button_secondary_style, LV_OPA_COVER);

    // button_danger_style - uses danger color from palette
    lv_style_init(&helix_theme_instance->button_danger_style);
    lv_style_set_bg_color(&helix_theme_instance->button_danger_style, danger_color);
    lv_style_set_bg_opa(&helix_theme_instance->button_danger_style, LV_OPA_COVER);

    // button_ghost_style - transparent background
    lv_style_init(&helix_theme_instance->button_ghost_style);
    lv_style_set_bg_opa(&helix_theme_instance->button_ghost_style, LV_OPA_0);

    // button_success_style - uses success color from palette
    lv_style_init(&helix_theme_instance->button_success_style);
    lv_style_set_bg_color(&helix_theme_instance->button_success_style, success_color);
    lv_style_set_bg_opa(&helix_theme_instance->button_success_style, LV_OPA_COVER);

    // button_tertiary_style - uses tertiary color from palette
    lv_style_init(&helix_theme_instance->button_tertiary_style);
    lv_style_set_bg_color(&helix_theme_instance->button_tertiary_style, tertiary_color);
    lv_style_set_bg_opa(&helix_theme_instance->button_tertiary_style, LV_OPA_COVER);

    // button_warning_style - uses warning color from palette
    lv_style_init(&helix_theme_instance->button_warning_style);
    lv_style_set_bg_color(&helix_theme_instance->button_warning_style, warning_color);
    lv_style_set_bg_opa(&helix_theme_instance->button_warning_style, LV_OPA_COVER);

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
    def_theme->color_grey = elevated_bg;

    // Update pre-computed style colors
    // Screen background
    lv_style_set_bg_color(&def_theme->styles.scr, screen_bg);

    // Card backgrounds (multiple styles use this)
    lv_style_set_bg_color(&def_theme->styles.card, card_bg);

    // Button background and radius
    lv_style_set_bg_color(&def_theme->styles.btn, elevated_bg);
    lv_style_set_radius(&def_theme->styles.btn, border_radius);

    return (lv_theme_t*)helix_theme_instance;
}

void theme_core_update_colors(bool is_dark, const theme_palette_t* palette,
                              int32_t border_opacity) {
    if (!helix_theme_instance || !palette) {
        return;
    }

    // Extract colors from palette for readability
    lv_color_t screen_bg = palette->screen_bg;
    lv_color_t card_bg = palette->card_bg;
    lv_color_t elevated_bg = palette->elevated_bg;
    lv_color_t border_color = palette->border;
    lv_color_t text_primary_color = palette->text;
    lv_color_t text_muted_color = palette->text_muted;
    lv_color_t text_subtle_color = palette->text_subtle;
    lv_color_t primary_color = palette->primary;
    lv_color_t secondary_color = palette->secondary;
    lv_color_t tertiary_color = palette->tertiary;
    lv_color_t info_color = palette->info;
    lv_color_t success_color = palette->success;
    lv_color_t warning_color = palette->warning;
    lv_color_t danger_color = palette->danger;
    lv_color_t focus_color = palette->focus;

    // Derive accent colors from palette (same logic as theme_core_init)
    lv_color_t knob_color = pick_more_saturated(primary_color, tertiary_color);
    lv_color_t accent_color = pick_more_saturated(primary_color, secondary_color);

    // Update our custom styles in-place
    helix_theme_instance->is_dark_mode = is_dark;

    // Input widgets use elevated_bg color (elevated_bg) and text_primary for text
    lv_style_set_bg_color(&helix_theme_instance->input_bg_style, elevated_bg);
    lv_style_set_text_color(&helix_theme_instance->input_bg_style, text_primary_color);
    lv_style_set_border_color(&helix_theme_instance->input_bg_style, border_color);
    lv_style_set_border_opa(&helix_theme_instance->input_bg_style, border_opacity);

    // Update button style colors (text_color handled by text_button auto-contrast)
    lv_style_set_bg_color(&helix_theme_instance->button_style, elevated_bg);
    lv_style_set_border_color(&helix_theme_instance->button_style, border_color);
    lv_style_set_border_opa(&helix_theme_instance->button_style, border_opacity);

    // Update switch track, indicator, and knob colors
    lv_style_set_bg_color(&helix_theme_instance->switch_track_style, border_color);
    lv_style_set_bg_color(&helix_theme_instance->switch_indicator_style, secondary_color);
    lv_style_set_bg_color(&helix_theme_instance->switch_knob_style, knob_color);

    // Update checkbox styles
    lv_style_set_border_color(&helix_theme_instance->checkbox_box_style, border_color);
    lv_style_set_bg_color(&helix_theme_instance->checkbox_box_style, text_primary_color);
    lv_style_set_text_color(&helix_theme_instance->checkbox_indicator_style, accent_color);
    lv_style_set_text_color(&helix_theme_instance->checkbox_text_style, text_primary_color);

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

    // Update shared card style
    lv_style_set_bg_color(&helix_theme_instance->card_style, card_bg);
    lv_style_set_border_color(&helix_theme_instance->card_style, border_color);
    lv_style_set_border_opa(&helix_theme_instance->card_style, (lv_opa_t)border_opacity);

    // Update shared dialog style
    lv_style_set_bg_color(&helix_theme_instance->dialog_style, elevated_bg);

    // Update shared text styles
    lv_style_set_text_color(&helix_theme_instance->text_primary_style, text_primary_color);
    lv_style_set_text_color(&helix_theme_instance->text_muted_style, text_muted_color);
    lv_style_set_text_color(&helix_theme_instance->text_subtle_style, text_subtle_color);

    // Update icon styles (Phase 2.1)
    lv_style_set_text_color(&helix_theme_instance->icon_text_style, text_primary_color);
    lv_style_set_text_color(&helix_theme_instance->icon_muted_style, text_muted_color);
    lv_style_set_text_color(&helix_theme_instance->icon_primary_style, primary_color);
    lv_style_set_text_color(&helix_theme_instance->icon_secondary_style, secondary_color);
    lv_style_set_text_color(&helix_theme_instance->icon_tertiary_style, text_subtle_color);

    // Update semantic icon colors
    lv_style_set_text_color(&helix_theme_instance->icon_info_style, info_color);
    lv_style_set_text_color(&helix_theme_instance->icon_success_style, success_color);
    lv_style_set_text_color(&helix_theme_instance->icon_warning_style, warning_color);
    lv_style_set_text_color(&helix_theme_instance->icon_danger_style, danger_color);

    // Update spinner style (Phase 2.3) - uses primary_color
    lv_style_set_arc_color(&helix_theme_instance->spinner_style, primary_color);

    // Update severity styles
    lv_style_set_border_color(&helix_theme_instance->severity_info_style, info_color);
    lv_style_set_border_color(&helix_theme_instance->severity_success_style, success_color);
    lv_style_set_border_color(&helix_theme_instance->severity_warning_style, warning_color);
    lv_style_set_border_color(&helix_theme_instance->severity_danger_style, danger_color);

    // Update button styles (Phase 2.6a)
    lv_style_set_bg_color(&helix_theme_instance->button_primary_style, primary_color);
    lv_style_set_bg_color(&helix_theme_instance->button_secondary_style, elevated_bg);
    lv_style_set_bg_color(&helix_theme_instance->button_tertiary_style, tertiary_color);
    lv_style_set_bg_color(&helix_theme_instance->button_success_style, success_color);
    lv_style_set_bg_color(&helix_theme_instance->button_warning_style, warning_color);
    lv_style_set_bg_color(&helix_theme_instance->button_danger_style, danger_color);
    // Note: button_ghost_style has transparent bg, no update needed

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
    def_theme->color_grey = elevated_bg;
    def_theme->color_text = text_primary_color;

    // Update pre-computed style colors
    lv_style_set_bg_color(&def_theme->styles.scr, screen_bg);
    lv_style_set_text_color(&def_theme->styles.scr, text_primary_color);
    lv_style_set_bg_color(&def_theme->styles.card, card_bg);
    lv_style_set_bg_color(&def_theme->styles.btn, elevated_bg);

    // Notify LVGL that all styles have changed - triggers refresh cascade
    // NULL means "all styles changed", which forces a complete style recalculation
    lv_obj_report_style_change(NULL);
}

void theme_core_preview_colors(bool is_dark, const theme_palette_t* palette, int32_t border_radius,
                               int32_t border_opacity) {
    if (!helix_theme_instance || !palette) {
        return;
    }

    // Extract colors from palette for readability
    lv_color_t screen_bg = palette->screen_bg;
    lv_color_t card_bg = palette->card_bg;
    lv_color_t elevated_bg = palette->elevated_bg;
    lv_color_t border_color = palette->border;
    lv_color_t text_primary = palette->text;
    lv_color_t text_muted = palette->text_muted;
    lv_color_t text_subtle = palette->text_subtle;
    lv_color_t primary_color = palette->primary;
    lv_color_t secondary_color = palette->secondary;
    lv_color_t tertiary_color = palette->tertiary;
    lv_color_t info_color = palette->info;
    lv_color_t success_color = palette->success;
    lv_color_t warning_color = palette->warning;
    lv_color_t danger_color = palette->danger;
    lv_color_t focus_color = palette->focus;

    // Update the helix_theme instance
    helix_theme_instance->is_dark_mode = is_dark;

    // Input widgets use elevated_bg color and text_primary for text
    lv_style_set_bg_color(&helix_theme_instance->input_bg_style, elevated_bg);
    lv_style_set_text_color(&helix_theme_instance->input_bg_style, text_primary);
    lv_style_set_radius(&helix_theme_instance->input_bg_style, border_radius);
    lv_style_set_border_opa(&helix_theme_instance->input_bg_style, border_opacity);

    // Update button style
    lv_style_set_bg_color(&helix_theme_instance->button_style, elevated_bg);
    lv_style_set_text_color(&helix_theme_instance->button_style, text_primary);
    lv_style_set_radius(&helix_theme_instance->button_style, border_radius);
    lv_style_set_radius(&helix_theme_instance->pressed_style, border_radius);
    lv_style_set_border_opa(&helix_theme_instance->button_style, border_opacity);

    // Update switch indicator and knob colors
    lv_style_set_bg_color(&helix_theme_instance->switch_indicator_style, primary_color);
    lv_style_set_bg_color(&helix_theme_instance->switch_knob_style, text_primary);

    // Update focus ring color
    lv_style_set_outline_color(&helix_theme_instance->focus_ring_style, focus_color);

    // Update slider styles
    lv_style_set_bg_color(&helix_theme_instance->slider_track_style, border_color);
    lv_style_set_bg_color(&helix_theme_instance->slider_indicator_style, primary_color);
    lv_style_set_bg_color(&helix_theme_instance->slider_knob_style, card_bg);
    lv_style_set_shadow_color(&helix_theme_instance->slider_knob_style, screen_bg);

    // Update dropdown selected item style
    lv_style_set_bg_color(&helix_theme_instance->dropdown_selected_style, elevated_bg);

    // Update shared card style
    lv_style_set_bg_color(&helix_theme_instance->card_style, card_bg);
    lv_style_set_border_color(&helix_theme_instance->card_style, border_color);
    lv_style_set_border_opa(&helix_theme_instance->card_style, (lv_opa_t)border_opacity);
    lv_style_set_radius(&helix_theme_instance->card_style, border_radius);

    // Update shared dialog style
    lv_style_set_bg_color(&helix_theme_instance->dialog_style, elevated_bg);
    lv_style_set_radius(&helix_theme_instance->dialog_style, border_radius);

    // Update shared text styles
    lv_style_set_text_color(&helix_theme_instance->text_primary_style, text_primary);
    lv_style_set_text_color(&helix_theme_instance->text_muted_style, text_muted);
    lv_style_set_text_color(&helix_theme_instance->text_subtle_style, text_subtle);

    // Update icon styles (Phase 2.1)
    lv_style_set_text_color(&helix_theme_instance->icon_text_style, text_primary);
    lv_style_set_text_color(&helix_theme_instance->icon_muted_style, text_muted);
    lv_style_set_text_color(&helix_theme_instance->icon_primary_style, primary_color);
    lv_style_set_text_color(&helix_theme_instance->icon_secondary_style, secondary_color);
    lv_style_set_text_color(&helix_theme_instance->icon_tertiary_style, text_subtle);

    // Update semantic icon colors
    lv_style_set_text_color(&helix_theme_instance->icon_info_style, info_color);
    lv_style_set_text_color(&helix_theme_instance->icon_success_style, success_color);
    lv_style_set_text_color(&helix_theme_instance->icon_warning_style, warning_color);
    lv_style_set_text_color(&helix_theme_instance->icon_danger_style, danger_color);

    // Update spinner style (Phase 2.3) - uses primary color
    lv_style_set_arc_color(&helix_theme_instance->spinner_style, primary_color);

    // Update severity styles (Phase 2.3) - use palette semantic colors
    lv_style_set_border_color(&helix_theme_instance->severity_info_style, info_color);
    lv_style_set_border_color(&helix_theme_instance->severity_success_style, success_color);
    lv_style_set_border_color(&helix_theme_instance->severity_warning_style, warning_color);
    lv_style_set_border_color(&helix_theme_instance->severity_danger_style, danger_color);

    // Update button styles (Phase 2.6a) - use palette colors
    lv_style_set_bg_color(&helix_theme_instance->button_primary_style, primary_color);
    lv_style_set_bg_color(&helix_theme_instance->button_secondary_style, elevated_bg);
    lv_style_set_bg_color(&helix_theme_instance->button_tertiary_style, tertiary_color);
    lv_style_set_bg_color(&helix_theme_instance->button_success_style, success_color);
    lv_style_set_bg_color(&helix_theme_instance->button_warning_style, warning_color);
    lv_style_set_bg_color(&helix_theme_instance->button_danger_style, danger_color);
    // Note: button_ghost_style has transparent bg, no update needed

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
    def_theme->color_grey = elevated_bg;
    def_theme->color_text = text_primary;

    lv_style_set_bg_color(&def_theme->styles.scr, screen_bg);
    lv_style_set_text_color(&def_theme->styles.scr, text_primary);
    lv_style_set_bg_color(&def_theme->styles.card, card_bg);
    lv_style_set_bg_color(&def_theme->styles.btn, elevated_bg);
    lv_style_set_radius(&def_theme->styles.btn, border_radius);

    // Trigger style refresh
    lv_obj_report_style_change(NULL);
}

lv_style_t* theme_core_get_card_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->card_style;
}

lv_style_t* theme_core_get_dialog_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->dialog_style;
}

lv_style_t* theme_core_get_text_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->text_primary_style;
}

lv_style_t* theme_core_get_text_muted_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->text_muted_style;
}

lv_style_t* theme_core_get_text_subtle_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->text_subtle_style;
}

// ============================================================================
// Icon Style Getters (Phase 2.1)
// ============================================================================

lv_style_t* theme_core_get_icon_text_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->icon_text_style;
}

lv_style_t* theme_core_get_icon_muted_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->icon_muted_style;
}

lv_style_t* theme_core_get_icon_primary_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->icon_primary_style;
}

lv_style_t* theme_core_get_icon_secondary_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->icon_secondary_style;
}

lv_style_t* theme_core_get_icon_tertiary_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->icon_tertiary_style;
}

lv_style_t* theme_core_get_icon_success_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->icon_success_style;
}

lv_style_t* theme_core_get_icon_warning_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->icon_warning_style;
}

lv_style_t* theme_core_get_icon_danger_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->icon_danger_style;
}

lv_style_t* theme_core_get_icon_info_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->icon_info_style;
}

// ============================================================================
// Spinner Style Getter (Phase 2.3)
// ============================================================================

lv_style_t* theme_core_get_spinner_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->spinner_style;
}

// ============================================================================
// Severity Style Getters (Phase 2.3)
// ============================================================================

lv_style_t* theme_core_get_severity_info_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->severity_info_style;
}

lv_style_t* theme_core_get_severity_success_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->severity_success_style;
}

lv_style_t* theme_core_get_severity_warning_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->severity_warning_style;
}

lv_style_t* theme_core_get_severity_danger_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->severity_danger_style;
}

// ============================================================================
// Button Style Getters (Phase 2.6a)
// ============================================================================

lv_style_t* theme_core_get_button_primary_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->button_primary_style;
}

lv_style_t* theme_core_get_button_secondary_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->button_secondary_style;
}

lv_style_t* theme_core_get_button_danger_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->button_danger_style;
}

lv_style_t* theme_core_get_button_ghost_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->button_ghost_style;
}

lv_style_t* theme_core_get_button_success_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->button_success_style;
}

lv_style_t* theme_core_get_button_tertiary_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->button_tertiary_style;
}

lv_style_t* theme_core_get_button_warning_style(void) {
    if (!helix_theme_instance) {
        return NULL;
    }
    return &helix_theme_instance->button_warning_style;
}

// ============================================================================
// Contrast Text Color Getters (Phase 2.6a)
// ============================================================================

lv_color_t theme_core_get_text_for_dark_bg(void) {
    // Look up text_dark token directly from XML constant system
    // This is the dark mode's text color (light) - used on dark backgrounds
    const char* text_dark_str = lv_xml_get_const(NULL, "text_dark");
    if (text_dark_str && text_dark_str[0] == '#') {
        return lv_color_hex(strtoul(text_dark_str + 1, NULL, 16));
    }
    // Fallback for themes without dark mode defined
    return lv_color_hex(0xE0E0E0);
}

lv_color_t theme_core_get_text_for_light_bg(void) {
    // Look up text_light token directly from XML constant system
    // This is the light mode's text color (dark) - used on light backgrounds
    const char* text_light_str = lv_xml_get_const(NULL, "text_light");
    if (text_light_str && text_light_str[0] == '#') {
        return lv_color_hex(strtoul(text_light_str + 1, NULL, 16));
    }
    // Fallback for themes without light mode defined
    return lv_color_hex(0x212121);
}

lv_color_t theme_core_get_contrast_text_color(lv_color_t bg_color) {
    // Standard luminance formula: L = (299*R + 587*G + 114*B) / 1000
    uint8_t r = bg_color.red;
    uint8_t g = bg_color.green;
    uint8_t b = bg_color.blue;
    uint32_t lum = (299 * r + 587 * g + 114 * b) / 1000;

    // Dark backgrounds (lum < 128) need light text, and vice versa
    return (lum < 128) ? theme_core_get_text_for_dark_bg() : theme_core_get_text_for_light_bg();
}
