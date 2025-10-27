/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#include "ui_switch.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/others/xml/lv_xml.h"
#include "lvgl/src/others/xml/lv_xml_widget.h"
#include "lvgl/src/others/xml/lv_xml_parser.h"
#include "lvgl/src/others/xml/lv_xml_style.h"
#include "lvgl/src/others/xml/parsers/lv_xml_obj_parser.h"
#include <spdlog/spdlog.h>
#include <cstring>

/**
 * XML create handler for ui_switch
 * Creates an lv_switch widget when <ui_switch> is encountered in XML
 */
static void *ui_switch_xml_create(lv_xml_parser_state_t *state, const char **attrs)
{
    LV_UNUSED(attrs);

    void *parent = lv_xml_state_get_parent(state);
    lv_obj_t *obj = lv_switch_create((lv_obj_t *)parent);

    if (!obj) {
        spdlog::error("[Switch] Failed to create lv_switch");
        return NULL;
    }

    return (void *)obj;
}

/**
 * XML apply handler for ui_switch
 * Applies attributes from XML to the switch widget
 */
static void ui_switch_xml_apply(lv_xml_parser_state_t *state, const char **attrs)
{
    void *item = lv_xml_state_get_item(state);
    lv_obj_t *obj = (lv_obj_t *)item;

    if (!obj) {
        spdlog::error("[Switch] NULL object in xml_apply");
        return;
    }

    // First apply standard lv_obj properties (width, height, style_*, etc.)
    lv_xml_obj_apply(state, attrs);

    // Then process switch-specific properties
    for (int i = 0; attrs[i]; i += 2) {
        const char* name = attrs[i];
        const char* value = attrs[i + 1];

        if (strcmp(name, "checked") == 0) {
            // Handle checked state
            if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                lv_obj_add_state(obj, LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(obj, LV_STATE_CHECKED);
            }
        }
        else if (strcmp(name, "orientation") == 0) {
            // Handle orientation
            if (strcmp(value, "horizontal") == 0) {
                lv_switch_set_orientation(obj, LV_SWITCH_ORIENTATION_HORIZONTAL);
            }
            else if (strcmp(value, "vertical") == 0) {
                lv_switch_set_orientation(obj, LV_SWITCH_ORIENTATION_VERTICAL);
            }
            else if (strcmp(value, "auto") == 0) {
                lv_switch_set_orientation(obj, LV_SWITCH_ORIENTATION_AUTO);
            }
        }
    }
}

/**
 * Register the ui_switch widget with LVGL's XML system
 */
void ui_switch_register()
{
    lv_xml_widget_register("ui_switch", ui_switch_xml_create, ui_switch_xml_apply);
    spdlog::info("[Switch] Registered ui_switch widget with XML system");
}
