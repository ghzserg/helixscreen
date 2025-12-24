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
 * Map severity string to icon glyph
 * Uses LVGL built-in symbols
 */
static const char* severity_to_icon(const char* severity) {
    if (!severity || strcmp(severity, "info") == 0) {
        return "\xEF\x81\x9A"; // F05A - circle-info (i in circle)
    } else if (strcmp(severity, "error") == 0) {
        return LV_SYMBOL_WARNING; // F071 - exclamation-triangle
    } else if (strcmp(severity, "warning") == 0) {
        return LV_SYMBOL_WARNING; // F071 - exclamation-triangle
    } else if (strcmp(severity, "success") == 0) {
        return LV_SYMBOL_OK; // F00C - check
    }
    return "\xEF\x81\x9A"; // F05A - circle-info (i in circle)
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
    const char* severity = "info"; // default
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
    spdlog::trace("[SeverityCard] Registered <severity_card> widget with LVGL XML system");
}

void ui_severity_card_finalize(lv_obj_t* obj) {
    if (!obj) {
        spdlog::warn("[SeverityCard] finalize called with NULL obj");
        return;
    }

    // Get stored severity from user data
    const char* severity = (const char*)lv_obj_get_user_data(obj);
    if (!severity) {
        spdlog::debug("[SeverityCard] No severity in user_data, defaulting to 'info'");
        severity = "info";
    }

    // New pattern: XML defines 4 icons (icon_info, icon_success, icon_warning, icon_error)
    // all hidden by default. We just unhide the correct one.
    // This keeps all styling (text, color) in XML.

    // Map severity to icon name
    const char* icon_name = nullptr;
    if (strcmp(severity, "info") == 0) {
        icon_name = "icon_info";
    } else if (strcmp(severity, "success") == 0) {
        icon_name = "icon_success";
    } else if (strcmp(severity, "warning") == 0) {
        icon_name = "icon_warning";
    } else if (strcmp(severity, "error") == 0) {
        icon_name = "icon_error";
    } else {
        icon_name = "icon_info"; // Default to info
    }

    // Find and unhide the correct icon
    lv_obj_t* icon = lv_obj_find_by_name(obj, icon_name);
    if (icon) {
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);
        spdlog::debug("[SeverityCard] Finalized: showing '{}' for severity='{}'", icon_name,
                      severity);
    } else {
        // Fallback: try legacy severity_icon pattern for backward compatibility
        lv_obj_t* legacy_icon = lv_obj_find_by_name(obj, "severity_icon");
        if (legacy_icon) {
            const char* icon_text = severity_to_icon(severity);
            lv_label_set_text(legacy_icon, icon_text);
            lv_obj_set_style_text_color(legacy_icon, ui_severity_get_color(severity), LV_PART_MAIN);
            spdlog::debug("[SeverityCard] Finalized via legacy pattern for severity='{}'",
                          severity);
        } else {
            spdlog::warn("[SeverityCard] Could not find icon for severity='{}'", severity);
        }
    }
}

lv_color_t ui_severity_get_color(const char* severity) {
    const char* color_const = severity_to_color_const(severity);
    return ui_theme_parse_hex_color(lv_xml_get_const(NULL, color_const));
}
