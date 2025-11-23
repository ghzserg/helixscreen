// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

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

/**
 * @file ui_subject_registry.h
 * @brief Helper macros and functions for LVGL subject registration
 *
 * This header provides convenience macros to reduce boilerplate when declaring,
 * initializing, and registering LVGL subjects for reactive data binding.
 *
 * ## Usage Example:
 *
 * ```cpp
 * // In .cpp file (file scope):
 * static lv_subject_t temp_subject;
 * static char temp_buf[32];
 *
 * void my_panel_init_subjects() {
 *     // Initialize and register in one call
 *     UI_SUBJECT_INIT_AND_REGISTER_STRING(temp_subject, temp_buf, "25°C", "my_temp");
 *
 *     // Or for int subjects:
 *     static lv_subject_t count_subject;
 *     UI_SUBJECT_INIT_AND_REGISTER_INT(count_subject, 0, "my_count");
 * }
 * ```
 *
 * ## Before (15 lines):
 * ```cpp
 * void ui_panel_motion_init_subjects() {
 *     snprintf(pos_x_buf, sizeof(pos_x_buf), "X:    --  mm");
 *     snprintf(pos_y_buf, sizeof(pos_y_buf), "Y:    --  mm");
 *     snprintf(pos_z_buf, sizeof(pos_z_buf), "Z:    --  mm");
 *
 *     lv_subject_init_string(&pos_x_subject, pos_x_buf, nullptr, sizeof(pos_x_buf), pos_x_buf);
 *     lv_subject_init_string(&pos_y_subject, pos_y_buf, nullptr, sizeof(pos_y_buf), pos_y_buf);
 *     lv_subject_init_string(&pos_z_subject, pos_z_buf, nullptr, sizeof(pos_z_buf), pos_z_buf);
 *
 *     lv_xml_register_subject(NULL, "motion_pos_x", &pos_x_subject);
 *     lv_xml_register_subject(NULL, "motion_pos_y", &pos_y_subject);
 *     lv_xml_register_subject(NULL, "motion_pos_z", &pos_z_subject);
 *
 *     spdlog::info("[Motion] Subjects initialized: X/Y/Z position displays");
 * }
 * ```
 *
 * ## After (6 lines):
 * ```cpp
 * void ui_panel_motion_init_subjects() {
 *     UI_SUBJECT_INIT_AND_REGISTER_STRING(pos_x_subject, pos_x_buf, "X:    --  mm", "motion_pos_x");
 *     UI_SUBJECT_INIT_AND_REGISTER_STRING(pos_y_subject, pos_y_buf, "Y:    --  mm", "motion_pos_y");
 *     UI_SUBJECT_INIT_AND_REGISTER_STRING(pos_z_subject, pos_z_buf, "Z:    --  mm", "motion_pos_z");
 *     spdlog::info("[Motion] Subjects initialized: X/Y/Z position displays");
 * }
 * ```
 */

#pragma once

#include <lvgl.h>

#include <cstdio>
#include <cstring>

/**
 * @brief Initialize and register a string subject with the XML system
 *
 * This macro combines subject initialization and registration into a single call.
 * It automatically handles buffer sizing and copies the initial value.
 *
 * @param subject The lv_subject_t variable (no &)
 * @param buffer The char[] buffer for string storage (no &)
 * @param initial_value C-string with initial value (will be copied to buffer)
 * @param name XML registration name (C-string)
 *
 * Example:
 * ```cpp
 * static lv_subject_t temp_subject;
 * static char temp_buf[32];
 * UI_SUBJECT_INIT_AND_REGISTER_STRING(temp_subject, temp_buf, "25°C", "temperature");
 * ```
 */
#define UI_SUBJECT_INIT_AND_REGISTER_STRING(subject, buffer, initial_value, name)                      \
    do {                                                                                               \
        snprintf((buffer), sizeof(buffer), "%s", (initial_value));                                     \
        lv_subject_init_string(&(subject), (buffer), nullptr, sizeof(buffer), (buffer));               \
        lv_xml_register_subject(NULL, (name), &(subject));                                             \
    } while (0)

/**
 * @brief Initialize and register an integer subject with the XML system
 *
 * This macro combines subject initialization and registration into a single call.
 *
 * @param subject The lv_subject_t variable (no &)
 * @param initial_value Integer initial value
 * @param name XML registration name (C-string)
 *
 * Example:
 * ```cpp
 * static lv_subject_t count_subject;
 * UI_SUBJECT_INIT_AND_REGISTER_INT(count_subject, 0, "item_count");
 * ```
 */
#define UI_SUBJECT_INIT_AND_REGISTER_INT(subject, initial_value, name)                                 \
    do {                                                                                               \
        lv_subject_init_int(&(subject), (initial_value));                                              \
        lv_xml_register_subject(NULL, (name), &(subject));                                             \
    } while (0)

/**
 * @brief Initialize and register a pointer subject with the XML system
 *
 * This macro combines subject initialization and registration into a single call.
 *
 * @param subject The lv_subject_t variable (no &)
 * @param initial_value Pointer initial value (can be nullptr)
 * @param name XML registration name (C-string)
 *
 * Example:
 * ```cpp
 * static lv_subject_t widget_subject;
 * UI_SUBJECT_INIT_AND_REGISTER_POINTER(widget_subject, nullptr, "active_widget");
 * ```
 */
#define UI_SUBJECT_INIT_AND_REGISTER_POINTER(subject, initial_value, name)                             \
    do {                                                                                               \
        lv_subject_init_pointer(&(subject), (initial_value));                                          \
        lv_xml_register_subject(NULL, (name), &(subject));                                             \
    } while (0)

/**
 * @brief Initialize and register a color subject with the XML system
 *
 * This macro combines subject initialization and registration into a single call.
 *
 * @param subject The lv_subject_t variable (no &)
 * @param initial_value lv_color_t initial value
 * @param name XML registration name (C-string)
 *
 * Example:
 * ```cpp
 * static lv_subject_t color_subject;
 * UI_SUBJECT_INIT_AND_REGISTER_COLOR(color_subject, lv_color_hex(0xFF0000), "accent_color");
 * ```
 */
#define UI_SUBJECT_INIT_AND_REGISTER_COLOR(subject, initial_value, name)                               \
    do {                                                                                               \
        lv_subject_init_color(&(subject), (initial_value));                                            \
        lv_xml_register_subject(NULL, (name), &(subject));                                             \
    } while (0)
