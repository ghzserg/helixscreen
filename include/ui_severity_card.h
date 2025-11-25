// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UI_SEVERITY_CARD_H
#define UI_SEVERITY_CARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

/**
 * @brief Register the severity_card widget with LVGL's XML system
 *
 * The severity_card widget provides automatic color styling based on
 * a semantic severity level. In XML, simply pass:
 *
 *   <severity_card severity="error" ...>
 *
 * The widget will:
 * - Set border-left color to the appropriate severity color
 * - Store severity for later use by finalize
 *
 * Available severity values: "error", "warning", "success", "info" (default)
 *
 * Must be called before any XML files using <severity_card> are registered.
 */
void ui_severity_card_register(void);

/**
 * @brief Finalize severity styling for children
 *
 * Call this after creating a severity_card via lv_xml_create to style
 * children (which don't exist during widget apply phase).
 *
 * This will find any child named "severity_icon" and:
 * - Set its text to the appropriate icon glyph
 * - Set its text color to match the severity
 *
 * @param obj The severity_card widget
 */
void ui_severity_card_finalize(lv_obj_t* obj);

/**
 * @brief Get the severity color for a given severity string
 *
 * Useful for other code that needs to match severity colors.
 *
 * @param severity The severity string ("error", "warning", "success", "info")
 * @return The theme color for the severity
 */
lv_color_t ui_severity_get_color(const char* severity);

#ifdef __cplusplus
}
#endif

#endif // UI_SEVERITY_CARD_H
