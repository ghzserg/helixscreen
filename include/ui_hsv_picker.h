// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"

#include <functional>

/**
 * @file ui_hsv_picker.h
 * @brief HSV color picker widget with Saturation-Value square and Hue bar
 *
 * Android-style color picker that allows selecting any RGB color through HSV.
 * Consists of:
 * - SV Square: 2D gradient showing saturation (X) and value (Y) for current hue
 * - Hue Bar: Vertical rainbow gradient for selecting hue
 * - Crosshair/indicator showing current selection
 *
 * Usage in XML:
 * ```xml
 * <ui_hsv_picker name="hsv_picker" width="200" height="150"
 *                sv_size="120" hue_width="24" gap="8"/>
 * ```
 *
 * @see ui_hsv_picker_register() to register the widget
 */

/**
 * @brief Register the ui_hsv_picker widget for XML creation
 *
 * Call once during initialization before loading XML that uses this widget.
 */
void ui_hsv_picker_register();

/**
 * @brief Set the currently selected color (RGB)
 * @param obj The hsv_picker widget
 * @param rgb RGB color value (0x00RRGGBB format)
 *
 * Updates all internal state and redraws both SV square and hue bar.
 */
void ui_hsv_picker_set_color_rgb(lv_obj_t* obj, uint32_t rgb);

/**
 * @brief Get the currently selected color (RGB)
 * @param obj The hsv_picker widget
 * @return RGB color value (0x00RRGGBB format)
 */
uint32_t ui_hsv_picker_get_color_rgb(lv_obj_t* obj);

/**
 * @brief Set callback for when color changes
 * @param obj The hsv_picker widget
 * @param callback Function called with (rgb_color, user_data) when color changes
 * @param user_data Pointer passed to callback
 */
namespace helix {
using HsvPickerCallback = std::function<void(uint32_t rgb, void* user_data)>;
} // namespace helix
void ui_hsv_picker_set_callback(lv_obj_t* obj, helix::HsvPickerCallback callback, void* user_data);

/**
 * @brief Set HSV values directly
 * @param obj The hsv_picker widget
 * @param hue Hue value 0-360
 * @param sat Saturation value 0-100
 * @param val Value (brightness) 0-100
 */
void ui_hsv_picker_set_hsv(lv_obj_t* obj, float hue, float sat, float val);

/**
 * @brief Get HSV values
 * @param obj The hsv_picker widget
 * @param hue Output: Hue value 0-360
 * @param sat Output: Saturation value 0-100
 * @param val Output: Value (brightness) 0-100
 */
void ui_hsv_picker_get_hsv(lv_obj_t* obj, float* hue, float* sat, float* val);
