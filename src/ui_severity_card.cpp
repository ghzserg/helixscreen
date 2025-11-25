// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_severity_card.h"

#include "ui_theme.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"

#include <spdlog/spdlog.h>

#include <cstring>

/**
 * Map severity string to theme color constant name
 */
static const char* severity_to_color_const(const char* severity) {
    if (!severity || strcmp(severity, "info") == 0) {
        return "info_color";
    } else if (strcmp(severity, "error") == 0) {
        return "error_color";
    } else if (strcmp(severity, "warning") == 0) {
        return "warning_color";
    } else if (strcmp(severity, "success") == 0) {
        return "success_color";
    }
    return "info_color";
}

/**
 * Map severity string to FontAwesome icon glyph
 * Uses glyphs from fa_icons_24 font
 */
static const char* severity_to_icon(const char* severity) {
    if (!severity || strcmp(severity, "info") == 0) {
        return "\xEF\x81\x99";  // F059 - question-circle
    } else if (strcmp(severity, "error") == 0) {
        return LV_SYMBOL_WARNING;  // F071 - exclamation-triangle
    } else if (strcmp(severity, "warning") == 0) {
        return LV_SYMBOL_WARNING;  // F071 - exclamation-triangle
    } else if (strcmp(severity, "success") == 0) {
        return LV_SYMBOL_OK;       // F00C - check
    }
    return "\xEF\x81\x99";  // F059 - question-circle
}

/**
 * XML create handler for severity_card
 * Creates an lv_obj widget when <severity_card> is encountered in XML
 */
static void* severity_card_xml_create(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);

    void* parent = lv_xml_state_get_parent(state);
    lv_obj_t* obj = lv_obj_create((lv_obj_t*)parent);

    if (!obj) {
        spdlog::error("[SeverityCard] Failed to create lv_obj");
        return NULL;
    }

    spdlog::trace("[SeverityCard] Created base lv_obj");
    return (void*)obj;
}

/**
 * XML apply handler for severity_card
 * Applies severity-based styling + XML attributes
 */
static void severity_card_xml_apply(lv_xml_parser_state_t* state, const char** attrs) {
    void* item = lv_xml_state_get_item(state);
    lv_obj_t* obj = (lv_obj_t*)item;

    if (!obj) {
        spdlog::error("[SeverityCard] NULL object in xml_apply");
        return;
    }

    // Extract severity attribute from attrs
    const char* severity = "info";  // default
    for (int i = 0; attrs[i]; i += 2) {
        if (strcmp(attrs[i], "severity") == 0) {
            severity = attrs[i + 1];
            break;
        }
    }

    // Store severity as user data for finalize to use later
    // Note: severity string comes from XML attrs and is stable
    lv_obj_set_user_data(obj, (void*)severity);

    // Apply standard lv_obj properties from XML first
    lv_xml_obj_apply(state, attrs);

    // Apply severity-based border color immediately
    lv_color_t severity_color = ui_severity_get_color(severity);
    lv_obj_set_style_border_color(obj, severity_color, LV_PART_MAIN);

    spdlog::trace("[SeverityCard] Applied severity='{}', stored for finalize", severity);
}

void ui_severity_card_register(void) {
    lv_xml_register_widget("severity_card", severity_card_xml_create, severity_card_xml_apply);
    spdlog::debug("[SeverityCard] Registered <severity_card> widget with LVGL XML system");
}

void ui_severity_card_finalize(lv_obj_t* obj) {
    if (!obj) {
        return;
    }

    // Get stored severity from user data
    const char* severity = (const char*)lv_obj_get_user_data(obj);
    if (!severity) {
        severity = "info";
    }

    lv_color_t severity_color = ui_severity_get_color(severity);

    // Find severity_icon child and style it
    lv_obj_t* icon = lv_obj_find_by_name(obj, "severity_icon");
    if (icon) {
        lv_label_set_text(icon, severity_to_icon(severity));
        lv_obj_set_style_text_color(icon, severity_color, LV_PART_MAIN);
        spdlog::trace("[SeverityCard] Finalized severity_icon: severity={}", severity);
    }
}

lv_color_t ui_severity_get_color(const char* severity) {
    const char* color_const = severity_to_color_const(severity);
    return ui_theme_parse_color(lv_xml_get_const(NULL, color_const));
}
