// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_faceted.h
/// @brief Voron Stealthburner toolhead renderer
///
/// Vector-drawn Stealthburner print head using LVGL polygon primitives.
/// Automatically uses Voron red when no filament color is loaded.

#pragma once

#include "lvgl/lvgl.h"

/// @brief Draw Voron Stealthburner print head
///
/// Creates a detailed vector rendering of the Stealthburner toolhead with:
/// - Dark housing outline
/// - Faceted body panels with 3D shading effect
/// - Top motor recess circle
/// - Bottom fan circle
/// - Voron logo stripes
///
/// The toolhead is colored with the loaded filament color when printing,
/// or defaults to Voron red (#D11D1D) when no filament is loaded.
///
/// @param layer LVGL draw layer
/// @param cx Center X position
/// @param cy Center Y position (center of entire print head)
/// @param filament_color Color of loaded filament (or gray/black for default)
/// @param scale_unit Base scaling unit (typically from theme space_md)
void draw_nozzle_faceted(lv_layer_t* layer, int32_t cx, int32_t cy, lv_color_t filament_color,
                         int32_t scale_unit);
