// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_bambu.h
/// @brief Bambu-style metallic gray toolhead renderer

#pragma once

#include "lvgl/lvgl.h"

/// @brief Draw Bambu-style metallic gray print head
///
/// Creates a 3D isometric view of a print head with:
/// - Heater block (main body with gradient shading)
/// - Large circular fan duct
/// - Tapered nozzle tip
///
/// @param layer LVGL draw layer
/// @param cx Center X position
/// @param cy Center Y position (center of entire print head)
/// @param filament_color Color of loaded filament (tints nozzle tip)
/// @param scale_unit Base scaling unit (typically from theme space_md)
void draw_nozzle_bambu(lv_layer_t* layer, int32_t cx, int32_t cy, lv_color_t filament_color,
                       int32_t scale_unit);
