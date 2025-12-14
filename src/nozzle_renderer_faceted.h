// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_faceted.h
/// @brief Faceted red toolhead renderer (Voron-style angular design)

#pragma once

#include "lvgl/lvgl.h"

/// @brief Draw faceted red print head with angular surfaces
///
/// Creates a stylized 3D view of a print head with:
/// - Faceted red body with angular panels (lit from top-left)
/// - Dark frame outline
/// - Top circular recess (extruder motor area)
/// - Cooling fan with visible hub
/// - Tapered nozzle with brass tip
/// - 4 screws and logo detail
///
/// @param layer LVGL draw layer
/// @param cx Center X position
/// @param cy Center Y position (center of entire print head)
/// @param filament_color Color of loaded filament (tints nozzle tip)
/// @param scale_unit Base scaling unit (typically from theme space_md)
void draw_nozzle_faceted(lv_layer_t* layer, int32_t cx, int32_t cy, lv_color_t filament_color,
                         int32_t scale_unit);
