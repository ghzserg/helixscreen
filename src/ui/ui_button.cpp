// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_button.h"

#include "ui_fonts.h"
#include "ui_icon_codepoints.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_utils.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"
#include "theme_core.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <cstring>

namespace {

// User data stored on button to track icon/label positions
struct UiButtonData {
    lv_obj_t* icon;     // Icon widget (or nullptr if none)
    lv_obj_t* label;    // Label widget (always present)
    bool icon_on_right; // true if icon is after text
};

/**
 * @brief Get icon font for button icons (24px MDI)
 * @return Pointer to the MDI 24px icon font
 */
static const lv_font_t* get_button_icon_font() {
    return &mdi_icons_24;
}

/**
 * @brief Update button label and icon text color based on button bg luminance
 *
 * Computes luminance using standard formula:
 *   L = (299*R + 587*G + 114*B) / 1000
 *
 * If L < 128 (dark bg): use light text color
 * If L >= 128 (light bg): use dark text color
 *
 * @param btn The button widget
 */
void update_button_text_contrast(lv_obj_t* btn) {
    // Get user data to find icon and label
    UiButtonData* data = static_cast<UiButtonData*>(lv_obj_get_user_data(btn));
    if (!data || !data->label) {
        spdlog::debug("[ui_button] No button data or label found");
        return;
    }

    lv_color_t bg = lv_obj_get_style_bg_color(btn, LV_PART_MAIN);
    // LVGL 9: lv_color_t has direct .red, .green, .blue members
    uint8_t r = bg.red;
    uint8_t g = bg.green;
    uint8_t b = bg.blue;
    uint32_t lum = (299 * r + 587 * g + 114 * b) / 1000;

    lv_color_t text_color =
        (lum < 128) ? theme_core_get_text_for_dark_bg() : theme_core_get_text_for_light_bg();

    // Apply to label
    lv_obj_set_style_text_color(data->label, text_color, LV_PART_MAIN);

    // Apply to icon if present
    if (data->icon) {
        lv_obj_set_style_text_color(data->icon, text_color, LV_PART_MAIN);
    }

    spdlog::trace("[ui_button] text contrast: bg=0x{:06X} lum={} -> {} text=0x{:06X}",
                  lv_color_to_u32(bg) & 0xFFFFFF, lum, (lum < 128) ? "light" : "dark",
                  lv_color_to_u32(text_color) & 0xFFFFFF);
}

/**
 * @brief Event callback for LV_EVENT_STYLE_CHANGED
 *
 * Called when button style changes (e.g., theme update).
 * Recalculates and applies appropriate text contrast.
 *
 * @param e Event object
 */
void button_style_changed_cb(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target_obj(e);
    spdlog::trace("[ui_button] STYLE_CHANGED event fired");
    update_button_text_contrast(btn);
}

/**
 * @brief Event callback for LV_EVENT_DELETE
 *
 * Called when button is deleted. Frees the UiButtonData user data.
 *
 * @param e Event object
 */
void button_delete_cb(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target_obj(e);
    UiButtonData* data = static_cast<UiButtonData*>(lv_obj_get_user_data(btn));
    if (data) {
        delete data;
        lv_obj_set_user_data(btn, nullptr);
    }
}

/**
 * @brief Create icon widget for button
 *
 * Creates a font-based MDI icon label inside the button.
 *
 * @param btn Parent button widget
 * @param icon_name Icon name (e.g., "settings", "heat_wave")
 * @return Created icon widget or nullptr on failure
 */
lv_obj_t* create_button_icon(lv_obj_t* btn, const char* icon_name) {
    if (!icon_name || strlen(icon_name) == 0) {
        return nullptr;
    }

    // Lookup icon codepoint
    const char* codepoint = ui_icon::lookup_codepoint(icon_name);
    if (!codepoint) {
        // Try stripping legacy prefix
        const char* stripped = ui_icon::strip_legacy_prefix(icon_name);
        if (stripped != icon_name) {
            codepoint = ui_icon::lookup_codepoint(stripped);
        }
    }

    if (!codepoint) {
        spdlog::warn("[ui_button] Icon '{}' not found", icon_name);
        return nullptr;
    }

    // Create icon as lv_label with MDI font
    lv_obj_t* icon = lv_label_create(btn);
    lv_label_set_text(icon, codepoint);
    lv_obj_set_style_text_font(icon, get_button_icon_font(), LV_PART_MAIN);

    spdlog::trace("[ui_button] Created icon '{}' -> codepoint", icon_name);
    return icon;
}

/**
 * @brief XML create callback for <ui_button> widget
 *
 * Creates a semantic button with:
 * - lv_button as base widget
 * - Shared style based on variant (primary/secondary/danger/ghost)
 * - Optional icon with auto-contrast
 * - Child lv_label with text attribute
 * - LV_EVENT_STYLE_CHANGED handler for auto-contrast updates
 *
 * Attributes:
 * - variant: Button style (primary/secondary/danger/success/tertiary/warning/ghost)
 * - text: Button label text
 * - icon: Optional icon name (e.g., "settings", "heat_wave")
 * - icon_position: "left" (default) or "right"
 *
 * @param state XML parser state
 * @param attrs XML attributes
 * @return Created button object
 */
void* ui_button_create(lv_xml_parser_state_t* state, const char** attrs) {
    lv_obj_t* parent = static_cast<lv_obj_t*>(lv_xml_state_get_parent(state));

    // Create button with default height from theme system
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_height(btn, theme_manager_get_spacing("button_height"));

    // Parse variant attribute (default: primary)
    const char* variant_str = lv_xml_get_value_of(attrs, "variant");
    if (!variant_str) {
        variant_str = "primary";
    }

    // Apply shared style based on variant
    lv_style_t* style = nullptr;
    if (strcmp(variant_str, "primary") == 0) {
        style = theme_core_get_button_primary_style();
    } else if (strcmp(variant_str, "secondary") == 0) {
        style = theme_core_get_button_secondary_style();
    } else if (strcmp(variant_str, "danger") == 0) {
        style = theme_core_get_button_danger_style();
    } else if (strcmp(variant_str, "success") == 0) {
        style = theme_core_get_button_success_style();
    } else if (strcmp(variant_str, "tertiary") == 0) {
        style = theme_core_get_button_tertiary_style();
    } else if (strcmp(variant_str, "warning") == 0) {
        style = theme_core_get_button_warning_style();
    } else if (strcmp(variant_str, "ghost") == 0) {
        style = theme_core_get_button_ghost_style();
    } else {
        spdlog::warn("[ui_button] Unknown variant '{}', defaulting to primary", variant_str);
        style = theme_core_get_button_primary_style();
    }

    if (style) {
        lv_obj_add_style(btn, style, LV_PART_MAIN);
    }

    // Parse text attribute
    const char* text = lv_xml_get_value_of(attrs, "text");
    if (!text) {
        text = "";
    }

    // Parse icon attribute
    const char* icon_name = lv_xml_get_value_of(attrs, "icon");

    // Parse icon_position attribute (default: left)
    const char* icon_pos_str = lv_xml_get_value_of(attrs, "icon_position");
    bool icon_on_right = (icon_pos_str && strcmp(icon_pos_str, "right") == 0);

    // Allocate user data to track icon/label
    UiButtonData* data = new UiButtonData{nullptr, nullptr, icon_on_right};

    bool has_icon = (icon_name && strlen(icon_name) > 0);
    bool has_text = (text && strlen(text) > 0);

    if (has_icon && has_text) {
        // Icon + text: use horizontal flex layout
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(btn, theme_manager_get_spacing("space_xs"), LV_PART_MAIN);

        if (icon_on_right) {
            // Text first, then icon
            data->label = lv_label_create(btn);
            lv_label_set_text(data->label, text);

            data->icon = create_button_icon(btn, icon_name);
        } else {
            // Icon first, then text
            data->icon = create_button_icon(btn, icon_name);

            data->label = lv_label_create(btn);
            lv_label_set_text(data->label, text);
        }
    } else if (has_icon) {
        // Icon only: center the icon
        data->icon = create_button_icon(btn, icon_name);
        if (data->icon) {
            lv_obj_center(data->icon);
        }

        // Create hidden label to maintain data structure
        data->label = lv_label_create(btn);
        lv_label_set_text(data->label, "");
        lv_obj_add_flag(data->label, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Text only: center the label (original behavior)
        data->label = lv_label_create(btn);
        lv_label_set_text(data->label, text);
        lv_obj_center(data->label);
    }

    // Store user data on button
    lv_obj_set_user_data(btn, data);

    // Register event handlers
    lv_obj_add_event_cb(btn, button_style_changed_cb, LV_EVENT_STYLE_CHANGED, nullptr);
    lv_obj_add_event_cb(btn, button_delete_cb, LV_EVENT_DELETE, nullptr);

    // Apply initial text contrast
    update_button_text_contrast(btn);

    spdlog::trace("[ui_button] Created button variant='{}' text='{}' icon='{}' icon_pos='{}'",
                  variant_str, text, icon_name ? icon_name : "", icon_on_right ? "right" : "left");

    return btn;
}

/**
 * @brief XML apply callback for <ui_button> widget
 *
 * Delegates to standard object parser for base properties (align, hidden, etc.)
 * Also sets derived names for icon/label children if the button has a name.
 *
 * @param state XML parser state
 * @param attrs XML attributes
 */
void ui_button_apply(lv_xml_parser_state_t* state, const char** attrs) {
    lv_xml_obj_apply(state, attrs);

    // If button has a name, give icon a derived name so it can be found
    void* item = lv_xml_state_get_item(state);
    lv_obj_t* btn = static_cast<lv_obj_t*>(item);
    const char* btn_name = lv_obj_get_name(btn);

    if (btn_name && strlen(btn_name) > 0) {
        UiButtonData* data = static_cast<UiButtonData*>(lv_obj_get_user_data(btn));
        if (data && data->icon) {
            // Set icon name as "{button_name}_icon"
            char icon_name[128];
            snprintf(icon_name, sizeof(icon_name), "%s_icon", btn_name);
            lv_obj_set_name(data->icon, icon_name);
            spdlog::trace("[ui_button] Set icon name to '{}'", icon_name);
        }
    }
}

} // namespace

void ui_button_init() {
    lv_xml_register_widget("ui_button", ui_button_create, ui_button_apply);
    spdlog::debug("[ui_button] Registered semantic button widget");
}
