// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_card.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

/**
 * XML create handler for ui_card
 * Creates an lv_obj widget when <ui_card> is encountered in XML
 * and applies theme-aware defaults. Defaults are set here (not in apply)
 * because create is called exactly once, while apply may be called multiple times.
 */
static void* ui_card_xml_create(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);

    void* parent = lv_xml_state_get_parent(state);
    lv_obj_t* obj = lv_obj_create((lv_obj_t*)parent);

    if (!obj) {
        spdlog::error("[Card] Failed to create lv_obj");
        return NULL;
    }

    // Apply theme-aware defaults (can be overridden by XML attrs in apply handler)
    // 1. Background color (theme-aware: light/dark via theme_manager_get_color)
    lv_color_t bg_color = theme_manager_get_color("card_bg");
    lv_obj_set_style_bg_color(obj, bg_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);

    // 2. Disabled state: 50% opacity for visual feedback
    lv_obj_set_style_opa(obj, LV_OPA_50, LV_PART_MAIN | LV_STATE_DISABLED);

    // 3. Border: theme-aware width, color, and opacity
    lv_obj_set_style_border_width(obj, theme_manager_get_spacing("border_width"), LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, theme_manager_get_color("border"), LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, theme_manager_get_spacing("border_opacity"), LV_PART_MAIN);

    // 4. Disable scrolling (cards are fixed containers, not scroll areas)
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    // 5. Shadow: 0 (no shadow)
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);

    // 6. Padding: theme-aware via space_md
    lv_obj_set_style_pad_all(obj, theme_manager_get_spacing("space_md"), LV_PART_MAIN);
    lv_obj_set_style_pad_gap(obj, theme_manager_get_spacing("space_md"), LV_PART_MAIN);

    // 7. Border radius: theme-aware via card_radius token
    int32_t radius = theme_manager_get_spacing("card_radius");
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);

    spdlog::trace("[Card] Created ui_card with theme-aware defaults");
    return (void*)obj;
}

void ui_card_register(void) {
    // Use standard lv_xml_obj_apply for XML attr processing - defaults are in create handler
    lv_xml_register_widget("ui_card", ui_card_xml_create, lv_xml_obj_apply);
    spdlog::trace("[Card] Registered <ui_card> widget with LVGL XML system");
}
