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

#include "bed_mesh_overlays.h"

#include "ui_fonts.h"

#include "bed_mesh_coordinate_transform.h"
#include "bed_mesh_internal.h"
#include "bed_mesh_projection.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

// ============================================================================
// Constants
// ============================================================================

namespace {

// Grid and axis colors
const lv_color_t GRID_LINE_COLOR = lv_color_make(140, 140, 140); // Medium gray

// Rendering opacity values
constexpr lv_opa_t GRID_LINE_OPACITY = LV_OPA_70; // 70% opacity for grid overlay

// Visibility margin for partially visible geometry
constexpr int VISIBILITY_MARGIN_PX = 10;

// Grid margin (world units, extends past mesh edges for AA/rounding)
constexpr double GRID_MARGIN_WORLD = 5.0;

// Grid spacing in millimeters for reference grids
constexpr double GRID_SPACING_MM = 50.0;

// Wall height factor (Mainsail-style: extends to 2x the mesh Z range above z_min)
constexpr double WALL_HEIGHT_FACTOR = 2.0;

// Number of segments for Z-axis grid divisions
constexpr int Z_AXIS_SEGMENT_COUNT = 5;

// Axis label offset from edge (world units)
constexpr double AXIS_LABEL_OFFSET = 40.0;

// Z axis height factor (percentage above mesh max)
constexpr double Z_AXIS_HEIGHT_FACTOR = 1.1; // 10% above mesh max

// Tick label dimensions (pixels)
constexpr int TICK_LABEL_WIDTH_DECIMAL = 40; // Wider for decimal values (e.g., "-0.25")
constexpr int TICK_LABEL_WIDTH_INTEGER = 30; // Narrower for integers (e.g., "100")
constexpr int TICK_LABEL_HEIGHT = 12;
constexpr int TICK_LABEL_CLIP_MARGIN = 20; // Margin for partial clipping at edges

// Axis label dimensions
constexpr int AXIS_LABEL_HALF_SIZE = 7; // 7px half-size = 14px label area

/**
 * Check if point is visible on canvas (with margin for partially visible geometry)
 * @param x Screen X coordinate
 * @param y Screen Y coordinate
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 * @param margin Pixel margin for partially visible objects
 * @return true if point is visible or partially visible
 */
static inline bool is_point_visible(int x, int y, int canvas_width, int canvas_height,
                                    int margin = VISIBILITY_MARGIN_PX) {
    return x >= -margin && x < canvas_width + margin && y >= -margin && y < canvas_height + margin;
}

/**
 * Check if line segment is potentially visible on canvas
 * @return true if either endpoint is visible (line may be partially visible)
 */
static inline bool is_line_visible(int x1, int y1, int x2, int y2, int canvas_width,
                                   int canvas_height, int margin = VISIBILITY_MARGIN_PX) {
    return is_point_visible(x1, y1, canvas_width, canvas_height, margin) ||
           is_point_visible(x2, y2, canvas_width, canvas_height, margin);
}

// Cohen-Sutherland line clipping outcode bits
constexpr int CS_INSIDE = 0; // 0000
constexpr int CS_LEFT = 1;   // 0001
constexpr int CS_RIGHT = 2;  // 0010
constexpr int CS_BOTTOM = 4; // 0100
constexpr int CS_TOP = 8;    // 1000

static int compute_outcode(double x, double y, double x_min, double y_min, double x_max,
                           double y_max) {
    int code = CS_INSIDE;
    if (x < x_min)
        code |= CS_LEFT;
    else if (x > x_max)
        code |= CS_RIGHT;
    if (y < y_min)
        code |= CS_TOP; // Note: y increases downward in screen coords
    else if (y > y_max)
        code |= CS_BOTTOM;
    return code;
}

// Cohen-Sutherland line clipping: clips line to rectangle, returns false if fully outside
static bool clip_line_to_rect(double& x0, double& y0, double& x1, double& y1, double x_min,
                              double y_min, double x_max, double y_max) {
    int outcode0 = compute_outcode(x0, y0, x_min, y_min, x_max, y_max);
    int outcode1 = compute_outcode(x1, y1, x_min, y_min, x_max, y_max);
    bool accept = false;

    while (true) {
        if (!(outcode0 | outcode1)) {
            // Both endpoints inside - accept
            accept = true;
            break;
        } else if (outcode0 & outcode1) {
            // Both endpoints share an outside zone - reject
            break;
        } else {
            // Line crosses boundary - clip
            double x = 0, y = 0;
            int outcodeOut = outcode0 ? outcode0 : outcode1;

            // Find intersection with clipping boundary
            if (outcodeOut & CS_BOTTOM) {
                x = x0 + (x1 - x0) * (y_max - y0) / (y1 - y0);
                y = y_max;
            } else if (outcodeOut & CS_TOP) {
                x = x0 + (x1 - x0) * (y_min - y0) / (y1 - y0);
                y = y_min;
            } else if (outcodeOut & CS_RIGHT) {
                y = y0 + (y1 - y0) * (x_max - x0) / (x1 - x0);
                x = x_max;
            } else if (outcodeOut & CS_LEFT) {
                y = y0 + (y1 - y0) * (x_min - x0) / (x1 - x0);
                x = x_min;
            }

            // Update endpoint and recompute outcode
            if (outcodeOut == outcode0) {
                x0 = x;
                y0 = y;
                outcode0 = compute_outcode(x0, y0, x_min, y_min, x_max, y_max);
            } else {
                x1 = x;
                y1 = y;
                outcode1 = compute_outcode(x1, y1, x_min, y_min, x_max, y_max);
            }
        }
    }
    return accept;
}

/**
 * Draw a single axis line from 3D start to 3D end point
 * Projects coordinates to 2D screen space and renders the line
 */
static void draw_axis_line(lv_layer_t* layer, lv_draw_line_dsc_t* line_dsc, double start_x,
                           double start_y, double start_z, double end_x, double end_y, double end_z,
                           int canvas_width, int canvas_height,
                           const bed_mesh_view_state_t* view_state) {
    bed_mesh_point_3d_t start = bed_mesh_projection_project_3d_to_2d(
        start_x, start_y, start_z, canvas_width, canvas_height, view_state);
    bed_mesh_point_3d_t end = bed_mesh_projection_project_3d_to_2d(
        end_x, end_y, end_z, canvas_width, canvas_height, view_state);

    // Use proper line clipping (Cohen-Sutherland) instead of naive endpoint clamping
    // This preserves line slope when clipping to canvas bounds
    double x1 = static_cast<double>(start.screen_x);
    double y1 = static_cast<double>(start.screen_y);
    double x2 = static_cast<double>(end.screen_x);
    double y2 = static_cast<double>(end.screen_y);

    // Clip line to canvas bounds - skip if fully outside
    if (!clip_line_to_rect(x1, y1, x2, y2, 0.0, 0.0, static_cast<double>(canvas_width - 1),
                           static_cast<double>(canvas_height - 1))) {
        return; // Line fully outside canvas
    }

    line_dsc->p1.x = static_cast<lv_value_precise_t>(x1);
    line_dsc->p1.y = static_cast<lv_value_precise_t>(y1);
    line_dsc->p2.x = static_cast<lv_value_precise_t>(x2);
    line_dsc->p2.y = static_cast<lv_value_precise_t>(y2);
    lv_draw_line(layer, line_dsc);
}

} // anonymous namespace

// ============================================================================
// Public API Implementation
// ============================================================================

namespace helix {
namespace mesh {

void render_grid_lines(lv_layer_t* layer, const bed_mesh_renderer_t* renderer, int canvas_width,
                       int canvas_height) {
    if (!renderer || !renderer->has_mesh_data) {
        return;
    }

    // Configure line drawing style
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = GRID_LINE_COLOR;
    line_dsc.width = 1;
    line_dsc.opa = GRID_LINE_OPACITY;

    // Use cached projected screen coordinates (SOA arrays - already computed in render function)
    // This eliminates ~400 redundant projections for 20×20 mesh
    const auto& screen_x = renderer->projected_screen_x;
    const auto& screen_y = renderer->projected_screen_y;

    // DEBUG: Track grid line bounds
    int grid_min_x = INT_MAX, grid_max_x = INT_MIN;
    int grid_min_y = INT_MAX, grid_max_y = INT_MIN;
    int grid_lines_drawn = 0;

    // Draw horizontal grid lines (connect points in same row)
    for (int row = 0; row < renderer->rows; row++) {
        for (int col = 0; col < renderer->cols - 1; col++) {
            int p1_x = screen_x[static_cast<size_t>(row)][static_cast<size_t>(col)];
            int p1_y = screen_y[static_cast<size_t>(row)][static_cast<size_t>(col)];
            int p2_x = screen_x[static_cast<size_t>(row)][static_cast<size_t>(col + 1)];
            int p2_y = screen_y[static_cast<size_t>(row)][static_cast<size_t>(col + 1)];

            // Bounds check (allow some margin for partially visible lines)
            if (is_line_visible(p1_x, p1_y, p2_x, p2_y, canvas_width, canvas_height)) {
                // Set line endpoints in descriptor
                line_dsc.p1.x = static_cast<lv_value_precise_t>(p1_x);
                line_dsc.p1.y = static_cast<lv_value_precise_t>(p1_y);
                line_dsc.p2.x = static_cast<lv_value_precise_t>(p2_x);
                line_dsc.p2.y = static_cast<lv_value_precise_t>(p2_y);

                // DEBUG: Track bounds
                grid_min_x = std::min({grid_min_x, p1_x, p2_x});
                grid_max_x = std::max({grid_max_x, p1_x, p2_x});
                grid_min_y = std::min({grid_min_y, p1_y, p2_y});
                grid_max_y = std::max({grid_max_y, p1_y, p2_y});
                grid_lines_drawn++;

                lv_draw_line(layer, &line_dsc);
            }
        }
    }

    // Draw vertical grid lines (connect points in same column)
    for (int col = 0; col < renderer->cols; col++) {
        for (int row = 0; row < renderer->rows - 1; row++) {
            int p1_x = screen_x[static_cast<size_t>(row)][static_cast<size_t>(col)];
            int p1_y = screen_y[static_cast<size_t>(row)][static_cast<size_t>(col)];
            int p2_x = screen_x[static_cast<size_t>(row + 1)][static_cast<size_t>(col)];
            int p2_y = screen_y[static_cast<size_t>(row + 1)][static_cast<size_t>(col)];

            // Bounds check
            if (is_line_visible(p1_x, p1_y, p2_x, p2_y, canvas_width, canvas_height)) {
                // Set line endpoints in descriptor
                line_dsc.p1.x = static_cast<lv_value_precise_t>(p1_x);
                line_dsc.p1.y = static_cast<lv_value_precise_t>(p1_y);
                line_dsc.p2.x = static_cast<lv_value_precise_t>(p2_x);
                line_dsc.p2.y = static_cast<lv_value_precise_t>(p2_y);

                // DEBUG: Track bounds
                grid_min_x = std::min({grid_min_x, p1_x, p2_x});
                grid_max_x = std::max({grid_max_x, p1_x, p2_x});
                grid_min_y = std::min({grid_min_y, p1_y, p2_y});
                grid_max_y = std::max({grid_max_y, p1_y, p2_y});
                grid_lines_drawn++;

                lv_draw_line(layer, &line_dsc);
            }
        }
    }

    // DEBUG: Log grid line bounds summary
    if (grid_lines_drawn > 0) {
        spdlog::trace("[GRID_LINES] Total bounds: x=[{},{}] y=[{},{}] lines_drawn={} canvas={}x{}",
                      grid_min_x, grid_max_x, grid_min_y, grid_max_y, grid_lines_drawn,
                      canvas_width, canvas_height);
    }
}

void render_reference_grids(lv_layer_t* layer, const bed_mesh_renderer_t* renderer,
                            int canvas_width, int canvas_height) {
    if (!renderer || !renderer->has_mesh_data) {
        return;
    }

    // Calculate mesh dimensions
    double mesh_half_width = (renderer->cols - 1) / 2.0 * BED_MESH_SCALE;
    double mesh_half_height = (renderer->rows - 1) / 2.0 * BED_MESH_SCALE;

    // Use cached z_center for world-space Z coordinates
    double z_min_world = helix::mesh::mesh_z_to_world_z(
        renderer->mesh_min_z, renderer->cached_z_center, renderer->view_state.z_scale);
    double z_max_world = helix::mesh::mesh_z_to_world_z(
        renderer->mesh_max_z, renderer->cached_z_center, renderer->view_state.z_scale);

    // Grid boundaries (extend slightly past mesh edges to account for AA and rounding)
    double x_min = -mesh_half_width - GRID_MARGIN_WORLD;
    double x_max = mesh_half_width + GRID_MARGIN_WORLD;
    double y_min = -mesh_half_height - GRID_MARGIN_WORLD;
    double y_max = mesh_half_height + GRID_MARGIN_WORLD;
    double z_min = z_min_world;
    // Mainsail-style: wall extends to WALL_HEIGHT_FACTOR * mesh Z range above z_min
    // This gives visual headroom above the mesh surface
    double mesh_z_range = z_max_world - z_min_world;
    double z_max = z_min_world + WALL_HEIGHT_FACTOR * mesh_z_range;

    // Configure grid line drawing style
    lv_draw_line_dsc_t grid_line_dsc;
    lv_draw_line_dsc_init(&grid_line_dsc);
    grid_line_dsc.color = GRID_LINE_COLOR;
    grid_line_dsc.width = 1;
    grid_line_dsc.opa = LV_OPA_40; // Light opacity for reference grids

    // ========== 1. BOTTOM GRID (XY plane at Z=z_min) ==========
    // Horizontal lines (constant Y, varying X)
    for (double y = y_min; y <= y_max; y += GRID_SPACING_MM) {
        draw_axis_line(layer, &grid_line_dsc, x_min, y, z_min, x_max, y, z_min, canvas_width,
                       canvas_height, &renderer->view_state);
    }
    // Vertical lines (constant X, varying Y)
    for (double x = x_min; x <= x_max; x += GRID_SPACING_MM) {
        draw_axis_line(layer, &grid_line_dsc, x, y_min, z_min, x, y_max, z_min, canvas_width,
                       canvas_height, &renderer->view_state);
    }

    // ========== 2. BACK WALL GRID (XZ plane at Y=y_min) ==========
    // Note: With camera angle_z=-45°, y_min projects to the back of the view
    // Vertical lines (constant X, varying Z)
    for (double x = x_min; x <= x_max + 0.1; x += GRID_SPACING_MM) {
        draw_axis_line(layer, &grid_line_dsc, x, y_min, z_min, x, y_min, z_max, canvas_width,
                       canvas_height, &renderer->view_state);
    }
    // Horizontal lines (constant Z, varying X)
    double wall_z_range = z_max - z_min;
    double wall_z_spacing = wall_z_range / Z_AXIS_SEGMENT_COUNT;
    if (wall_z_spacing < 1.0)
        wall_z_spacing = wall_z_range / 4.0;
    for (double z = z_min; z <= z_max + 0.01; z += wall_z_spacing) {
        draw_axis_line(layer, &grid_line_dsc, x_min, y_min, z, x_max, y_min, z, canvas_width,
                       canvas_height, &renderer->view_state);
    }

    // ========== 3. LEFT WALL GRID (YZ plane at X=x_min) ==========
    // Vertical lines (constant Y, varying Z)
    double z_range = z_max - z_min;
    double z_spacing = z_range / Z_AXIS_SEGMENT_COUNT;
    if (z_spacing < 1.0)
        z_spacing = z_range / 4.0; // At least 4 divisions for small ranges
    for (double y = y_min; y <= y_max + 0.1; y += GRID_SPACING_MM) {
        draw_axis_line(layer, &grid_line_dsc, x_min, y, z_min, x_min, y, z_max, canvas_width,
                       canvas_height, &renderer->view_state);
    }
    // Horizontal lines (constant Z, varying Y)
    for (double z = z_min; z <= z_max + 0.01; z += z_spacing) {
        draw_axis_line(layer, &grid_line_dsc, x_min, y_min, z, x_min, y_max, z, canvas_width,
                       canvas_height, &renderer->view_state);
    }

    spdlog::trace("[REFERENCE_GRIDS] Rendered bottom/back/side grids: X=[{:.1f},{:.1f}] "
                  "Y=[{:.1f},{:.1f}] Z=[{:.3f},{:.3f}]",
                  x_min, x_max, y_min, y_max, z_min, z_max);
}

void render_axis_labels(lv_layer_t* layer, const bed_mesh_renderer_t* renderer, int canvas_width,
                        int canvas_height) {
    if (!renderer || !renderer->has_mesh_data) {
        return;
    }

    // Use cached z_center for world-space Z coordinates
    double z_min_world = helix::mesh::mesh_z_to_world_z(
        renderer->mesh_min_z, renderer->cached_z_center, renderer->view_state.z_scale);
    double z_max_world = helix::mesh::mesh_z_to_world_z(
        renderer->mesh_max_z, renderer->cached_z_center, renderer->view_state.z_scale);

    // Calculate mesh extent in world coordinates
    double mesh_half_width = (renderer->cols - 1) / 2.0 * BED_MESH_SCALE;
    double mesh_half_height = (renderer->rows - 1) / 2.0 * BED_MESH_SCALE;

    // Grid bounds for label positioning
    double x_max = mesh_half_width;
    double y_min = -mesh_half_height;
    double y_max = mesh_half_height;

    // Configure label drawing style
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_white();
    label_dsc.font = &noto_sans_14;
    label_dsc.opa = LV_OPA_90;
    label_dsc.align = LV_TEXT_ALIGN_CENTER;

    // X label: At the MIDDLE of the front edge (where X axis is most visible)
    // Mainsail places this at the center of the X extent, not at a corner
    double x_label_x = 0.0;                       // Center of X axis
    double x_label_y = y_max + AXIS_LABEL_OFFSET; // Just beyond front edge
    double x_label_z = z_min_world;               // At grid plane level
    bed_mesh_point_3d_t x_pos = bed_mesh_projection_project_3d_to_2d(
        x_label_x, x_label_y, x_label_z, canvas_width, canvas_height, &renderer->view_state);

    if (x_pos.screen_x >= 0 && x_pos.screen_x < canvas_width && x_pos.screen_y >= 0 &&
        x_pos.screen_y < canvas_height) {
        label_dsc.text = "X";
        lv_area_t x_area;
        x_area.x1 = x_pos.screen_x - AXIS_LABEL_HALF_SIZE;
        x_area.y1 = x_pos.screen_y - AXIS_LABEL_HALF_SIZE;
        x_area.x2 = x_area.x1 + 2 * AXIS_LABEL_HALF_SIZE;
        x_area.y2 = x_area.y1 + 2 * AXIS_LABEL_HALF_SIZE;

        if (x_area.x1 >= 0 && x_area.x2 < canvas_width && x_area.y1 >= 0 &&
            x_area.y2 < canvas_height) {
            lv_draw_label(layer, &label_dsc, &x_area);
        }
    }

    // Y label: Centered on the RIGHT edge (analogous to X on front edge)
    double y_label_x = x_max + AXIS_LABEL_OFFSET; // Just beyond right edge
    double y_label_y = 0.0;                       // Center of Y axis
    double y_label_z = z_min_world;               // At grid plane level
    bed_mesh_point_3d_t y_pos = bed_mesh_projection_project_3d_to_2d(
        y_label_x, y_label_y, y_label_z, canvas_width, canvas_height, &renderer->view_state);

    if (y_pos.screen_x >= 0 && y_pos.screen_x < canvas_width && y_pos.screen_y >= 0 &&
        y_pos.screen_y < canvas_height) {
        label_dsc.text = "Y";
        lv_area_t y_area;
        y_area.x1 = y_pos.screen_x - AXIS_LABEL_HALF_SIZE;
        y_area.y1 = y_pos.screen_y - AXIS_LABEL_HALF_SIZE;
        y_area.x2 = y_area.x1 + 2 * AXIS_LABEL_HALF_SIZE;
        y_area.y2 = y_area.y1 + 2 * AXIS_LABEL_HALF_SIZE;

        if (y_area.x1 >= 0 && y_area.x2 < canvas_width && y_area.y1 >= 0 &&
            y_area.y2 < canvas_height) {
            lv_draw_label(layer, &label_dsc, &y_area);
        }
    }

    // Z label: At the top of Z axis, at the back-right corner (x_max, y_min)
    // This is where the two back walls meet with angle_z=-40°
    double z_axis_top = z_max_world * Z_AXIS_HEIGHT_FACTOR;
    bed_mesh_point_3d_t z_pos = bed_mesh_projection_project_3d_to_2d(
        x_max, y_min, z_axis_top, canvas_width, canvas_height, &renderer->view_state);

    if (z_pos.screen_x >= 0 && z_pos.screen_x < canvas_width && z_pos.screen_y >= 0 &&
        z_pos.screen_y < canvas_height) {
        label_dsc.text = "Z";
        lv_area_t z_area;
        z_area.x1 = z_pos.screen_x + 5; // Offset right of the axis
        z_area.y1 = z_pos.screen_y - AXIS_LABEL_HALF_SIZE;
        z_area.x2 = z_area.x1 + 2 * AXIS_LABEL_HALF_SIZE;
        z_area.y2 = z_area.y1 + 2 * AXIS_LABEL_HALF_SIZE;

        if (z_area.x1 >= 0 && z_area.x2 < canvas_width && z_area.y1 >= 0 &&
            z_area.y2 < canvas_height) {
            lv_draw_label(layer, &label_dsc, &z_area);
        }
    }

    spdlog::debug("[AXIS_LABELS] X at ({:.1f},{:.1f}), Y at ({:.1f},{:.1f}), Z at ({:.1f},{:.1f})",
                  x_label_x, x_label_y, y_label_x, y_label_y, x_max, y_min);
}

void draw_axis_tick_label(lv_layer_t* layer, lv_draw_label_dsc_t* label_dsc, int screen_x,
                          int screen_y, int offset_x, int offset_y, double value, int canvas_width,
                          int canvas_height, bool use_decimals) {
    // Check screen bounds for tick position
    if (screen_x < 0 || screen_x >= canvas_width || screen_y < 0 || screen_y >= canvas_height) {
        return;
    }

    // Format label text (use decimal format for Z-axis heights)
    char label_text[12];
    if (use_decimals) {
        snprintf(label_text, sizeof(label_text), "%.2f", value);
    } else {
        snprintf(label_text, sizeof(label_text), "%.0f", value);
    }
    label_dsc->text = label_text;
    label_dsc->text_length = static_cast<uint32_t>(strlen(label_text));

    // Calculate label area with offsets (wider for decimal values)
    lv_area_t label_area;
    label_area.x1 = screen_x + offset_x;
    label_area.y1 = screen_y + offset_y;
    label_area.x2 =
        label_area.x1 + (use_decimals ? TICK_LABEL_WIDTH_DECIMAL : TICK_LABEL_WIDTH_INTEGER);
    label_area.y2 = label_area.y1 + TICK_LABEL_HEIGHT;

    // Draw if label origin is within canvas (allow partial clipping at edges)
    if (label_area.x1 >= -TICK_LABEL_CLIP_MARGIN && label_area.x1 < canvas_width &&
        label_area.y1 >= -TICK_LABEL_HEIGHT && label_area.y1 < canvas_height) {
        lv_draw_label(layer, label_dsc, &label_area);
    }
}

void render_numeric_axis_ticks(lv_layer_t* layer, const bed_mesh_renderer_t* renderer,
                               int canvas_width, int canvas_height) {
    if (!renderer || !renderer->has_mesh_data) {
        return;
    }

    // Get actual printer coordinate range (supports any origin convention)
    double x_min_mm, x_max_mm, y_min_mm, y_max_mm;
    if (renderer->geometry_computed) {
        // Use actual printer coordinates from set_bounds()
        x_min_mm = renderer->mesh_area_min_x;
        x_max_mm = renderer->mesh_area_max_x;
        y_min_mm = renderer->mesh_area_min_y;
        y_max_mm = renderer->mesh_area_max_y;
    } else {
        // Fallback: assume corner-origin 0 to mesh size
        x_min_mm = 0.0;
        x_max_mm = static_cast<double>(renderer->cols - 1) * BED_MESH_SCALE;
        y_min_mm = 0.0;
        y_max_mm = static_cast<double>(renderer->rows - 1) * BED_MESH_SCALE;
    }

    // Calculate mesh dimensions
    double mesh_half_width = (renderer->cols - 1) / 2.0 * BED_MESH_SCALE;
    double mesh_half_height = (renderer->rows - 1) / 2.0 * BED_MESH_SCALE;

    // Use cached z_center for world-space Z coordinates
    double z_min_world = helix::mesh::mesh_z_to_world_z(
        renderer->mesh_min_z, renderer->cached_z_center, renderer->view_state.z_scale);
    double z_max_world = helix::mesh::mesh_z_to_world_z(
        renderer->mesh_max_z, renderer->cached_z_center, renderer->view_state.z_scale);

    // Grid plane Z position (same as reference grids)
    double grid_z = z_min_world;

    // Configure label drawing style (smaller font than axis letters)
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_white();
    label_dsc.font = &noto_sans_10; // Smaller font for numeric labels
    label_dsc.opa = LV_OPA_80;      // Slightly more transparent than axis letters
    label_dsc.align = LV_TEXT_ALIGN_CENTER;
    label_dsc.text_local = 1; // Tell LVGL to copy text (we use stack buffers)

    // Determine appropriate tick spacing (aim for 3-5 ticks per axis)
    double x_range = x_max_mm - x_min_mm;
    double tick_spacing = GRID_SPACING_MM; // Default: 50mm intervals
    if (x_range > 250.0) {
        tick_spacing = 100.0; // For larger beds
    }

    // X-axis tick label offsets: below the front edge (outside the grid)
    constexpr int X_LABEL_OFFSET_X = -15;
    constexpr int X_LABEL_OFFSET_Y = 12; // Push down from edge
    // Y-axis tick label offsets: to the right of the right edge
    constexpr int Y_LABEL_OFFSET_X = 5;
    constexpr int Y_LABEL_OFFSET_Y = -5;
    // Z-axis tick label offsets: to the left of the axis line
    constexpr int Z_LABEL_OFFSET_X = -30;
    constexpr int Z_LABEL_OFFSET_Y = -6;

    // Round tick start to nearest multiple of tick_spacing
    // e.g., for x_min=-125 and tick_spacing=50, start at -100
    double x_tick_start = std::ceil(x_min_mm / tick_spacing) * tick_spacing;
    double y_tick_start = std::ceil(y_min_mm / tick_spacing) * tick_spacing;

    // Draw X-axis tick labels along FRONT edge
    // Y-world = +mesh_half_height (front edge in world coords)
    double x_tick_y_world = mesh_half_height;
    for (double x_mm = x_tick_start; x_mm <= x_max_mm + 0.001; x_mm += tick_spacing) {
        // Convert printer X coordinate to world X coordinate
        double x_world;
        if (renderer->geometry_computed) {
            x_world = helix::mesh::printer_x_to_world_x(x_mm, renderer->bed_center_x,
                                                        renderer->coord_scale);
        } else {
            // Fallback: linear interpolation from 0-based to centered world coords
            double t = (x_mm - x_min_mm) / (x_max_mm - x_min_mm);
            x_world = -mesh_half_width + t * (2.0 * mesh_half_width);
        }

        bed_mesh_point_3d_t tick = bed_mesh_projection_project_3d_to_2d(
            x_world, x_tick_y_world, grid_z, canvas_width, canvas_height, &renderer->view_state);
        draw_axis_tick_label(layer, &label_dsc, tick.screen_x, tick.screen_y, X_LABEL_OFFSET_X,
                             X_LABEL_OFFSET_Y, x_mm, canvas_width, canvas_height);
    }

    // Draw Y-axis tick labels along RIGHT edge
    // X-world = +mesh_half_width (right edge in world coords)
    double y_tick_x_world = mesh_half_width;
    for (double y_mm = y_tick_start; y_mm <= y_max_mm + 0.001; y_mm += tick_spacing) {
        // Convert printer Y coordinate to world Y coordinate
        double y_world;
        if (renderer->geometry_computed) {
            y_world = helix::mesh::printer_y_to_world_y(y_mm, renderer->bed_center_y,
                                                        renderer->coord_scale);
        } else {
            // Fallback: linear interpolation (Y inverted in world space)
            double t = (y_mm - y_min_mm) / (y_max_mm - y_min_mm);
            y_world = mesh_half_height - t * (2.0 * mesh_half_height);
        }

        bed_mesh_point_3d_t tick = bed_mesh_projection_project_3d_to_2d(
            y_tick_x_world, y_world, grid_z, canvas_width, canvas_height, &renderer->view_state);
        draw_axis_tick_label(layer, &label_dsc, tick.screen_x, tick.screen_y, Y_LABEL_OFFSET_X,
                             Y_LABEL_OFFSET_Y, y_mm, canvas_width, canvas_height);
    }

    // Draw Z-axis tick labels (along Z-axis at front-left corner)
    // Show mesh min/max heights in mm (actual Z values, not world-scaled)
    double axis_origin_x = -mesh_half_width;
    double axis_origin_y = mesh_half_height;

    bed_mesh_point_3d_t z_min_tick =
        bed_mesh_projection_project_3d_to_2d(axis_origin_x, axis_origin_y, z_min_world,
                                             canvas_width, canvas_height, &renderer->view_state);
    draw_axis_tick_label(layer, &label_dsc, z_min_tick.screen_x, z_min_tick.screen_y,
                         Z_LABEL_OFFSET_X, Z_LABEL_OFFSET_Y, renderer->mesh_min_z, canvas_width,
                         canvas_height, true);

    bed_mesh_point_3d_t z_max_tick =
        bed_mesh_projection_project_3d_to_2d(axis_origin_x, axis_origin_y, z_max_world,
                                             canvas_width, canvas_height, &renderer->view_state);
    draw_axis_tick_label(layer, &label_dsc, z_max_tick.screen_x, z_max_tick.screen_y,
                         Z_LABEL_OFFSET_X, Z_LABEL_OFFSET_Y, renderer->mesh_max_z, canvas_width,
                         canvas_height, true);
}

} // namespace mesh
} // namespace helix
