// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_dialog.h"

#include "ui_theme.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_style.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"

#include <spdlog/spdlog.h>

/**
 * XML create handler for ui_dialog
 * Creates an lv_obj widget when <ui_dialog> is encountered in XML
 */
static void* ui_dialog_xml_create(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);

    void* parent = lv_xml_state_get_parent(state);
    lv_obj_t* obj = lv_obj_create((lv_obj_t*)parent);

    if (!obj) {
        spdlog::error("[Dialog] Failed to create lv_obj");
        return NULL;
    }

    spdlog::trace("[Dialog] Created base lv_obj");
    return (void*)obj;
}

/**
 * XML apply handler for ui_dialog
 * Applies theme defaults + XML attributes to the dialog widget
 */
static void ui_dialog_xml_apply(lv_xml_parser_state_t* state, const char** attrs) {
    void* item = lv_xml_state_get_item(state);
    lv_obj_t* obj = (lv_obj_t*)item;

    if (!obj) {
        spdlog::error("[Dialog] NULL object in xml_apply");
        return;
    }

    // Apply theme grey as background (matches lv_button styling from helix_theme)
    lv_color_t bg_color = ui_theme_get_color("theme_grey");
    lv_obj_set_style_bg_color(obj, bg_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);

    // Border radius from theme (read from globals.xml)
    const char* border_radius_str = lv_xml_get_const(NULL, "border_radius");
    if (border_radius_str) {
        int32_t border_radius = atoi(border_radius_str);
        lv_obj_set_style_radius(obj, border_radius, LV_PART_MAIN);
    }

    // No padding by default (dividers/buttons go edge-to-edge)
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);

    // No border by default
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);

    // No shadow by default
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);

    // Clip children to rounded corners (for full-bleed buttons at bottom)
    lv_obj_set_style_clip_corner(obj, true, LV_PART_MAIN);

    spdlog::trace("[Dialog] Applied LVGL button grey background (0x{:06X})",
                  lv_color_to_u32(bg_color) & 0xFFFFFF);

    // Now apply standard lv_obj properties from XML (highest priority)
    lv_xml_obj_apply(state, attrs);
}

void ui_dialog_register(void) {
    lv_xml_register_widget("ui_dialog", ui_dialog_xml_create, ui_dialog_xml_apply);
    spdlog::trace("[Dialog] Registered <ui_dialog> widget with LVGL XML system");
}
