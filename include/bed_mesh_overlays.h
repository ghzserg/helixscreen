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

#pragma once

#include "bed_mesh_renderer.h" // For bed_mesh_renderer_t, bed_mesh_view_state_t

#include <lvgl/lvgl.h>

/**
 * @file bed_mesh_overlays.h
 * @brief Grid lines, axes, and labels for bed mesh visualization
 *
 * Provides overlay rendering functions for the bed mesh 3D view:
 * - Grid lines on mesh surface
 * - Reference grids (Mainsail-style wall grids)
 * - Axis labels (X, Y, Z indicators)
 * - Numeric tick labels showing coordinate values
 *
 * All functions operate on an existing bed_mesh_renderer_t instance and
 * render to an LVGL layer in the helix::mesh namespace.
 */

namespace helix {
namespace mesh {

/**
 * @brief Render grid lines on mesh surface
 *
 * Draws a wireframe grid connecting all mesh probe points using cached
 * screen coordinates. Grid lines help visualize mesh topology and spacing.
 *
 * @param layer LVGL draw layer (from DRAW_POST event callback)
 * @param renderer Renderer instance with valid mesh data and projection cache
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 */
void render_grid_lines(lv_layer_t* layer, const bed_mesh_renderer_t* renderer, int canvas_width,
                       int canvas_height);

/**
 * @brief Render reference grids (Mainsail-style wall grids)
 *
 * Draws three orthogonal grid planes that create a "room" around the mesh:
 * 1. BOTTOM GRID (XY plane at Z=z_min): Gridlines every 50mm in both X and Y directions
 * 2. BACK WALL GRID (XZ plane at Y=y_max): Vertical lines for X positions, horizontal for Z heights
 * 3. SIDE WALL GRID (YZ plane at X=x_min): Vertical lines for Y positions, horizontal for Z heights
 *
 * The mesh data floats inside this reference frame, providing spatial context
 * similar to Mainsail's bed mesh visualization.
 *
 * @param layer LVGL draw layer (from DRAW_POST event callback)
 * @param renderer Renderer instance with valid mesh data
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 */
void render_reference_grids(lv_layer_t* layer, const bed_mesh_renderer_t* renderer,
                            int canvas_width, int canvas_height);

/**
 * @brief Render axis labels (X, Y, Z indicators)
 *
 * Positions labels at the MIDPOINT of each axis extent, just outside the grid edge:
 * - X label: Middle of X axis extent, below/outside the front edge
 * - Y label: Middle of Y axis extent, to the right/outside the right edge
 * - Z label: At the top of the Z axis, at the back-right corner
 *
 * This matches Mainsail's visualization style where axis labels indicate
 * the direction/dimension rather than the axis endpoint.
 *
 * @param layer LVGL draw layer (from DRAW_POST event callback)
 * @param renderer Renderer instance with valid mesh data
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 */
void render_axis_labels(lv_layer_t* layer, const bed_mesh_renderer_t* renderer, int canvas_width,
                        int canvas_height);

/**
 * @brief Render numeric tick labels on X, Y, and Z axes
 *
 * Adds millimeter labels (e.g., "-100", "0", "100") at regular intervals along
 * the X and Y axes to show bed dimensions, and height labels on the Z-axis.
 * Uses actual printer coordinates (works with any origin convention).
 *
 * @param layer LVGL draw layer (from DRAW_POST event callback)
 * @param renderer Renderer instance with valid mesh data
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 */
void render_numeric_axis_ticks(lv_layer_t* layer, const bed_mesh_renderer_t* renderer,
                               int canvas_width, int canvas_height);

/**
 * @brief Draw a single axis tick label at the given screen position
 *
 * Helper function to reduce code duplication in render_numeric_axis_ticks.
 * Handles bounds checking, text formatting, and deferred text copy for LVGL.
 *
 * @param layer LVGL draw layer (from DRAW_POST event callback)
 * @param label_dsc LVGL label drawing descriptor (pre-configured with font, color, opacity)
 * @param screen_x Screen X coordinate for label origin
 * @param screen_y Screen Y coordinate for label origin
 * @param offset_x X offset from screen position (for label alignment)
 * @param offset_y Y offset from screen position (for label alignment)
 * @param value Numeric value to format and display
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 * @param use_decimals If true, formats with 2 decimal places (for Z-axis mm values)
 *                     If false, formats as whole number (for X/Y axis values)
 */
void draw_axis_tick_label(lv_layer_t* layer, lv_draw_label_dsc_t* label_dsc, int screen_x,
                          int screen_y, int offset_x, int offset_y, double value, int canvas_width,
                          int canvas_height, bool use_decimals = false);

} // namespace mesh
} // namespace helix
