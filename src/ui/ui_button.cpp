// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_button.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_utils.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"
#include "theme_core.h"

#include <spdlog/spdlog.h>

#include <cstring>

namespace {

/**
 * @brief Update button label text color based on button bg luminance
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
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    if (!label) {
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

    lv_obj_set_style_text_color(label, text_color, LV_PART_MAIN);

    spdlog::trace("[ui_button] Updated text contrast: R={} G={} B={} lum={} -> {}", r, g, b, lum,
                  (lum < 128) ? "light" : "dark");
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
    update_button_text_contrast(btn);
}

/**
 * @brief XML create callback for <ui_button> widget
 *
 * Creates a semantic button with:
 * - lv_button as base widget
 * - Shared style based on variant (primary/secondary/danger/ghost)
 * - Child lv_label with text attribute
 * - LV_EVENT_STYLE_CHANGED handler for auto-contrast updates
 *
 * @param state XML parser state
 * @param attrs XML attributes
 * @return Created button object
 */
void* ui_button_create(lv_xml_parser_state_t* state, const char** attrs) {
    lv_obj_t* parent = static_cast<lv_obj_t*>(lv_xml_state_get_parent(state));

    // Create button
    lv_obj_t* btn = lv_button_create(parent);

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
    } else if (strcmp(variant_str, "ghost") == 0) {
        style = theme_core_get_button_ghost_style();
    } else {
        spdlog::warn("[ui_button] Unknown variant '{}', defaulting to primary", variant_str);
        style = theme_core_get_button_primary_style();
    }

    if (style) {
        lv_obj_add_style(btn, style, LV_PART_MAIN);
    }

    // Parse text attribute and create label
    const char* text = lv_xml_get_value_of(attrs, "text");
    if (!text) {
        text = "";
    }

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);

    // Register style changed event handler for auto-contrast updates
    lv_obj_add_event_cb(btn, button_style_changed_cb, LV_EVENT_STYLE_CHANGED, nullptr);

    // Apply initial text contrast
    update_button_text_contrast(btn);

    spdlog::trace("[ui_button] Created button variant='{}' text='{}'", variant_str, text);

    return btn;
}

/**
 * @brief XML apply callback for <ui_button> widget
 *
 * Delegates to standard object parser for base properties (align, hidden, etc.)
 *
 * @param state XML parser state
 * @param attrs XML attributes
 */
void ui_button_apply(lv_xml_parser_state_t* state, const char** attrs) {
    lv_xml_obj_apply(state, attrs);
}

} // namespace

void ui_button_init() {
    lv_xml_register_widget("ui_button", ui_button_create, ui_button_apply);
    spdlog::debug("[ui_button] Registered semantic button widget");
}
