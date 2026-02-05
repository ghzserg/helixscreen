// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bed_mesh_overlays.h"

#include "ui_fonts.h"

#include "bed_mesh_coordinate_transform.h"
#include "bed_mesh_internal.h"
#include "bed_mesh_projection.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstring>

// ============================================================================
// Constants
// ============================================================================

namespace {

// Rendering opacity values
constexpr lv_opa_t GRID_LINE_OPACITY = LV_OPA_70; // 70% opacity for grid overlay

// Visibility margin for partially visible geometry
constexpr int VISIBILITY_MARGIN_PX = 10;

// Label positioning
constexpr double Z_LABEL_ABOVE_CEILING = 32.0; // Z label offset above ceiling

// Grid spacing in millimeters for reference grids
constexpr double GRID_SPACING_MM = 50.0;

// Number of segments for Z-axis grid divisions
constexpr int Z_AXIS_SEGMENT_COUNT = 5;

// Axis label offset from edge (world units)
constexpr double AXIS_LABEL_OFFSET = 50.0; // Distance from grid edge to axis letter labels

// Tick label dimensions (pixels)
constexpr int TICK_LABEL_WIDTH_DECIMAL = 40; // Wider for decimal values (e.g., "-0.25")
constexpr int TICK_LABEL_WIDTH_INTEGER = 30; // Narrower for integers (e.g., "100")
constexpr int TICK_LABEL_HEIGHT = 12;

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

/**
 * Draw a single axis line from 3D start to 3D end point
 * Projects coordinates to 2D screen space and renders the line.
 * LVGL's layer system handles clipping automatically - no manual clipping needed.
 */
static void draw_axis_line(lv_layer_t* layer, lv_draw_line_dsc_t* line_dsc, double start_x,
                           double start_y, double start_z, double end_x, double end_y, double end_z,
                           int canvas_width, int canvas_height,
                           const bed_mesh_view_state_t* view_state) {
    bed_mesh_point_3d_t start = bed_mesh_projection_project_3d_to_2d(
        start_x, start_y, start_z, canvas_width, canvas_height, view_state);
    bed_mesh_point_3d_t end = bed_mesh_projection_project_3d_to_2d(
        end_x, end_y, end_z, canvas_width, canvas_height, view_state);

    // Let LVGL handle clipping via the layer's clip area (same as mesh wireframe)
    // The projected coordinates include layer_offset_x/y for screen positioning
    line_dsc->p1.x = static_cast<lv_value_precise_t>(start.screen_x);
    line_dsc->p1.y = static_cast<lv_value_precise_t>(start.screen_y);
    line_dsc->p2.x = static_cast<lv_value_precise_t>(end.screen_x);
    line_dsc->p2.y = static_cast<lv_value_precise_t>(end.screen_y);
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
    line_dsc.color = theme_manager_get_color("elevated_bg");
    line_dsc.width = 1;
    line_dsc.opa = GRID_LINE_OPACITY;

    // Use cached projected screen coordinates (SOA arrays - already computed in render function)
    // This eliminates ~400 redundant projections for 20×20 mesh
    const auto& screen_x = renderer->projected_screen_x;
    const auto& screen_y = renderer->projected_screen_y;

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
                line_dsc.p1.x = static_cast<lv_value_precise_t>(p1_x);
                line_dsc.p1.y = static_cast<lv_value_precise_t>(p1_y);
                line_dsc.p2.x = static_cast<lv_value_precise_t>(p2_x);
                line_dsc.p2.y = static_cast<lv_value_precise_t>(p2_y);
                lv_draw_line(layer, &line_dsc);
            }
        }
    }
}

void render_reference_floor(lv_layer_t* layer, const bed_mesh_renderer_t* renderer,
                            int canvas_width, int canvas_height) {
    // Render all reference grids (floor + walls) BEFORE mesh surface
    // The mesh surface is rendered on top, naturally occluding the floor grid
    render_reference_grids(layer, renderer, canvas_width, canvas_height);
}

void render_reference_walls(lv_layer_t* layer, const bed_mesh_renderer_t* renderer,
                            int canvas_width, int canvas_height) {
    // Stub - merged into render_reference_grids
    (void)layer;
    (void)renderer;
    (void)canvas_width;
    (void)canvas_height;
}

void render_reference_grids(lv_layer_t* layer, const bed_mesh_renderer_t* renderer,
                            int canvas_width, int canvas_height) {
    if (!renderer || !renderer->has_mesh_data) {
        return;
    }

    // Use PRINTER BED bounds for grid extent (not mesh bounds)
    // This makes the floor/walls larger than the mesh, so the mesh "floats" inside
    double bed_half_width, bed_half_height;
    if (renderer->has_bed_bounds) {
        // Use actual bed dimensions, centered
        bed_half_width = (renderer->bed_max_x - renderer->bed_min_x) / 2.0 * renderer->coord_scale;
        bed_half_height = (renderer->bed_max_y - renderer->bed_min_y) / 2.0 * renderer->coord_scale;
    } else {
        // Fallback to mesh dimensions if no bed bounds
        bed_half_width = (renderer->cols - 1) / 2.0 * BED_MESH_SCALE;
        bed_half_height = (renderer->rows - 1) / 2.0 * BED_MESH_SCALE;
    }

    // Get printer-mm coordinate ranges to align walls with grid lines
    double x_min_mm, x_max_mm, y_min_mm, y_max_mm;
    double bed_center_x, bed_center_y, coord_scale;
    if (renderer->has_bed_bounds && renderer->geometry_computed) {
        x_min_mm = renderer->bed_min_x;
        x_max_mm = renderer->bed_max_x;
        y_min_mm = renderer->bed_min_y;
        y_max_mm = renderer->bed_max_y;
        bed_center_x = renderer->bed_center_x;
        bed_center_y = renderer->bed_center_y;
        coord_scale = renderer->coord_scale;
    } else {
        x_min_mm = 0.0;
        x_max_mm = (renderer->cols - 1) * BED_MESH_SCALE;
        y_min_mm = 0.0;
        y_max_mm = (renderer->rows - 1) * BED_MESH_SCALE;
        bed_center_x = x_max_mm / 2.0;
        bed_center_y = y_max_mm / 2.0;
        coord_scale = 1.0;
    }

    // Round to first/last grid line positions (aligned to GRID_SPACING_MM)
    double x_grid_start = std::ceil(x_min_mm / GRID_SPACING_MM) * GRID_SPACING_MM;
    double x_grid_end = std::floor(x_max_mm / GRID_SPACING_MM) * GRID_SPACING_MM;
    double y_grid_start = std::ceil(y_min_mm / GRID_SPACING_MM) * GRID_SPACING_MM;
    double y_grid_end = std::floor(y_max_mm / GRID_SPACING_MM) * GRID_SPACING_MM;

    // Convert grid bounds to world coordinates for wall positioning
    double x_min = helix::mesh::printer_x_to_world_x(x_grid_start, bed_center_x, coord_scale);
    double x_max = helix::mesh::printer_x_to_world_x(x_grid_end, bed_center_x, coord_scale);
    double y_min =
        helix::mesh::printer_y_to_world_y(y_grid_end, bed_center_y, coord_scale); // Y inverted
    double y_max =
        helix::mesh::printer_y_to_world_y(y_grid_start, bed_center_y, coord_scale); // Y inverted

    // Calculate Z range and wall bounds using centralized function
    double z_min_world = helix::mesh::mesh_z_to_world_z(
        renderer->mesh_min_z, renderer->cached_z_center, renderer->view_state.z_scale);
    double z_max_world = helix::mesh::mesh_z_to_world_z(
        renderer->mesh_max_z, renderer->cached_z_center, renderer->view_state.z_scale);

    auto bounds =
        helix::mesh::compute_wall_bounds(z_min_world, z_max_world, bed_half_width, bed_half_height);
    double z_floor = bounds.floor_z;
    double z_ceiling = bounds.ceiling_z;

    // Configure grid line drawing style
    lv_draw_line_dsc_t grid_line_dsc;
    lv_draw_line_dsc_init(&grid_line_dsc);
    grid_line_dsc.color = theme_manager_get_color("elevated_bg");
    grid_line_dsc.width = 1;
    grid_line_dsc.opa = LV_OPA_60;

    // ========== 1. BOTTOM GRID (XY plane at Z=z_floor) ==========
    // Draw Y-parallel lines at printer-mm X positions (converted to world coords)
    for (double x_mm = x_grid_start; x_mm <= x_max_mm + 0.001; x_mm += GRID_SPACING_MM) {
        double x_world = helix::mesh::printer_x_to_world_x(x_mm, bed_center_x, coord_scale);
        draw_axis_line(layer, &grid_line_dsc, x_world, y_min, z_floor, x_world, y_max, z_floor,
                       canvas_width, canvas_height, &renderer->view_state);
    }
    // Draw X-parallel lines at printer-mm Y positions (converted to world coords)
    for (double y_mm = y_grid_start; y_mm <= y_max_mm + 0.001; y_mm += GRID_SPACING_MM) {
        double y_world = helix::mesh::printer_y_to_world_y(y_mm, bed_center_y, coord_scale);
        draw_axis_line(layer, &grid_line_dsc, x_min, y_world, z_floor, x_max, y_world, z_floor,
                       canvas_width, canvas_height, &renderer->view_state);
    }

    // ========== 2. BACK WALL GRID (XZ plane at Y=y_min) ==========
    // Vertical lines at printer-mm X positions
    for (double x_mm = x_grid_start; x_mm <= x_max_mm + 0.001; x_mm += GRID_SPACING_MM) {
        double x_world = helix::mesh::printer_x_to_world_x(x_mm, bed_center_x, coord_scale);
        draw_axis_line(layer, &grid_line_dsc, x_world, y_min, z_floor, x_world, y_min, z_ceiling,
                       canvas_width, canvas_height, &renderer->view_state);
    }
    // Horizontal lines (constant Z, varying X) - keep as world coords (Z isn't in printer-mm)
    double wall_z_range = z_ceiling - z_floor;
    double wall_z_spacing = wall_z_range / Z_AXIS_SEGMENT_COUNT;
    if (wall_z_spacing < 0.5)
        wall_z_spacing = wall_z_range / 3.0;
    for (double z = z_floor; z <= z_ceiling + 0.01; z += wall_z_spacing) {
        draw_axis_line(layer, &grid_line_dsc, x_min, y_min, z, x_max, y_min, z, canvas_width,
                       canvas_height, &renderer->view_state);
    }

    // ========== 3. LEFT WALL GRID (YZ plane at X=x_min) ==========
    // Vertical lines at printer-mm Y positions
    for (double y_mm = y_grid_start; y_mm <= y_max_mm + 0.001; y_mm += GRID_SPACING_MM) {
        double y_world = helix::mesh::printer_y_to_world_y(y_mm, bed_center_y, coord_scale);
        draw_axis_line(layer, &grid_line_dsc, x_min, y_world, z_floor, x_min, y_world, z_ceiling,
                       canvas_width, canvas_height, &renderer->view_state);
    }
    // Horizontal lines (constant Z, varying Y)
    for (double z = z_floor; z <= z_ceiling + 0.01; z += wall_z_spacing) {
        draw_axis_line(layer, &grid_line_dsc, x_min, y_min, z, x_min, y_max, z, canvas_width,
                       canvas_height, &renderer->view_state);
    }
}

void render_axis_labels(lv_layer_t* layer, const bed_mesh_renderer_t* renderer, int canvas_width,
                        int canvas_height) {
    if (!renderer || !renderer->has_mesh_data) {
        return;
    }

    // Get printer-mm coordinate ranges to compute grid-aligned positions
    double x_min_mm, x_max_mm, y_min_mm, y_max_mm;
    double bed_center_x, bed_center_y, coord_scale;
    if (renderer->geometry_computed) {
        x_min_mm = renderer->bed_min_x;
        x_max_mm = renderer->bed_max_x;
        y_min_mm = renderer->bed_min_y;
        y_max_mm = renderer->bed_max_y;
        bed_center_x = renderer->bed_center_x;
        bed_center_y = renderer->bed_center_y;
        coord_scale = renderer->coord_scale;
    } else {
        x_min_mm = 0.0;
        x_max_mm = (renderer->cols - 1) * BED_MESH_SCALE;
        y_min_mm = 0.0;
        y_max_mm = (renderer->rows - 1) * BED_MESH_SCALE;
        bed_center_x = x_max_mm / 2.0;
        bed_center_y = y_max_mm / 2.0;
        coord_scale = 1.0;
    }

    // Round to first/last grid line positions (must match wall positions)
    double x_grid_start = std::ceil(x_min_mm / GRID_SPACING_MM) * GRID_SPACING_MM;
    double x_grid_end = std::floor(x_max_mm / GRID_SPACING_MM) * GRID_SPACING_MM;
    double y_grid_start = std::ceil(y_min_mm / GRID_SPACING_MM) * GRID_SPACING_MM;
    double y_grid_end = std::floor(y_max_mm / GRID_SPACING_MM) * GRID_SPACING_MM;

    // Convert grid bounds to world coordinates (must match wall positioning)
    double x_min_world = helix::mesh::printer_x_to_world_x(x_grid_start, bed_center_x, coord_scale);
    double x_max_world = helix::mesh::printer_x_to_world_x(x_grid_end, bed_center_x, coord_scale);
    double y_min_world =
        helix::mesh::printer_y_to_world_y(y_grid_end, bed_center_y, coord_scale); // Y inverted
    double y_max_world =
        helix::mesh::printer_y_to_world_y(y_grid_start, bed_center_y, coord_scale); // Y inverted

    // Calculate bed half dimensions for wall bounds calculation
    double bed_half_width = (renderer->bed_max_x - renderer->bed_min_x) / 2.0 * coord_scale;
    double bed_half_height = (renderer->bed_max_y - renderer->bed_min_y) / 2.0 * coord_scale;
    if (!renderer->geometry_computed) {
        bed_half_width = (renderer->cols - 1) / 2.0 * BED_MESH_SCALE;
        bed_half_height = (renderer->rows - 1) / 2.0 * BED_MESH_SCALE;
    }

    // Use cached z_center for world-space Z coordinates
    double z_min_world = helix::mesh::mesh_z_to_world_z(
        renderer->mesh_min_z, renderer->cached_z_center, renderer->view_state.z_scale);
    double z_max_world = helix::mesh::mesh_z_to_world_z(
        renderer->mesh_max_z, renderer->cached_z_center, renderer->view_state.z_scale);

    // Calculate wall bounds using centralized function
    auto bounds =
        helix::mesh::compute_wall_bounds(z_min_world, z_max_world, bed_half_width, bed_half_height);
    double floor_z = bounds.floor_z;

    // Configure label drawing style
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_white();
    label_dsc.font = &noto_sans_14;
    label_dsc.opa = LV_OPA_90;
    label_dsc.align = LV_TEXT_ALIGN_CENTER;

    // X label: At the CENTER of the front edge, pushed OUTWARD (away from grid)
    // Positioned beyond the tick labels in the Y direction
    double x_label_x = 0.0;                             // Center of X axis
    double x_label_y = y_max_world + AXIS_LABEL_OFFSET; // Pushed outward from front edge
    double x_label_z = floor_z;                         // At floor grid level (base of walls)
    bed_mesh_point_3d_t x_pos = bed_mesh_projection_project_3d_to_2d(
        x_label_x, x_label_y, x_label_z, canvas_width, canvas_height, &renderer->view_state);

    // X label - let LVGL handle clipping
    {
        label_dsc.text = "X";
        lv_area_t x_area;
        x_area.x1 = x_pos.screen_x - AXIS_LABEL_HALF_SIZE;
        x_area.y1 = x_pos.screen_y - AXIS_LABEL_HALF_SIZE;
        x_area.x2 = x_area.x1 + 2 * AXIS_LABEL_HALF_SIZE;
        x_area.y2 = x_area.y1 + 2 * AXIS_LABEL_HALF_SIZE;
        lv_draw_label(layer, &label_dsc, &x_area);
    }

    // Y label: At the CENTER of the right edge, pushed OUTWARD (away from grid)
    // Positioned beyond the tick labels in the X direction
    double y_label_x = x_max_world + AXIS_LABEL_OFFSET; // Pushed outward from right edge
    double y_label_y = 0.0;                             // Center of Y axis
    double y_label_z = floor_z;                         // At floor grid level (base of walls)
    bed_mesh_point_3d_t y_pos = bed_mesh_projection_project_3d_to_2d(
        y_label_x, y_label_y, y_label_z, canvas_width, canvas_height, &renderer->view_state);

    // Y label - let LVGL handle clipping
    {
        label_dsc.text = "Y";
        lv_area_t y_area;
        y_area.x1 = y_pos.screen_x - AXIS_LABEL_HALF_SIZE;
        y_area.y1 = y_pos.screen_y - AXIS_LABEL_HALF_SIZE;
        y_area.x2 = y_area.x1 + 2 * AXIS_LABEL_HALF_SIZE;
        y_area.y2 = y_area.y1 + 2 * AXIS_LABEL_HALF_SIZE;
        lv_draw_label(layer, &label_dsc, &y_area);
    }

    // Z label: At the top of Z axis, ABOVE the wall ceiling where tick labels end
    // Position at front-left corner (grid-aligned)
    double z_axis_top = bounds.ceiling_z + Z_LABEL_ABOVE_CEILING; // Position above the highest tick
    bed_mesh_point_3d_t z_pos = bed_mesh_projection_project_3d_to_2d(
        x_min_world, y_max_world, z_axis_top, canvas_width, canvas_height, &renderer->view_state);

    // Z label - let LVGL handle clipping
    {
        label_dsc.text = "Z";
        lv_area_t z_area;
        z_area.x1 = z_pos.screen_x - AXIS_LABEL_HALF_SIZE - 5; // Offset left of the axis
        z_area.y1 = z_pos.screen_y - AXIS_LABEL_HALF_SIZE;
        z_area.x2 = z_area.x1 + 2 * AXIS_LABEL_HALF_SIZE;
        z_area.y2 = z_area.y1 + 2 * AXIS_LABEL_HALF_SIZE;
        lv_draw_label(layer, &label_dsc, &z_area);
    }
}

void draw_axis_tick_label(lv_layer_t* layer, lv_draw_label_dsc_t* label_dsc, int screen_x,
                          int screen_y, int offset_x, int offset_y, double value,
                          [[maybe_unused]] int canvas_width, [[maybe_unused]] int canvas_height,
                          bool use_decimals) {
    // Let LVGL handle clipping via the layer's clip area
    // (screen coordinates include layer_offset so manual bounds check would be wrong)

    // Format label text (use decimal format for Z-axis heights)
    // Handle -0 by treating small negative values as 0
    double display_value = (std::fabs(value) < 0.005) ? 0.0 : value;
    char label_text[12];
    if (use_decimals) {
        snprintf(label_text, sizeof(label_text), "%.2f", display_value);
    } else {
        snprintf(label_text, sizeof(label_text), "%.0f", display_value);
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

    // Let LVGL handle clipping via the layer's clip area
    lv_draw_label(layer, label_dsc, &label_area);
}

void render_numeric_axis_ticks(lv_layer_t* layer, const bed_mesh_renderer_t* renderer,
                               int canvas_width, int canvas_height) {
    if (!renderer || !renderer->has_mesh_data) {
        return;
    }

    // Get actual BED coordinate range (full bed, not just mesh probe area)
    double x_min_mm, x_max_mm, y_min_mm, y_max_mm;
    double bed_center_x, bed_center_y, coord_scale;
    if (renderer->geometry_computed) {
        x_min_mm = renderer->bed_min_x;
        x_max_mm = renderer->bed_max_x;
        y_min_mm = renderer->bed_min_y;
        y_max_mm = renderer->bed_max_y;
        bed_center_x = renderer->bed_center_x;
        bed_center_y = renderer->bed_center_y;
        coord_scale = renderer->coord_scale;
    } else {
        x_min_mm = 0.0;
        x_max_mm = static_cast<double>(renderer->cols - 1) * BED_MESH_SCALE;
        y_min_mm = 0.0;
        y_max_mm = static_cast<double>(renderer->rows - 1) * BED_MESH_SCALE;
        bed_center_x = x_max_mm / 2.0;
        bed_center_y = y_max_mm / 2.0;
        coord_scale = 1.0;
    }

    // Round to first/last grid line positions (must match wall positions from
    // render_reference_grids)
    double x_grid_start = std::ceil(x_min_mm / GRID_SPACING_MM) * GRID_SPACING_MM;
    double x_grid_end = std::floor(x_max_mm / GRID_SPACING_MM) * GRID_SPACING_MM;
    double y_grid_start = std::ceil(y_min_mm / GRID_SPACING_MM) * GRID_SPACING_MM;
    double y_grid_end = std::floor(y_max_mm / GRID_SPACING_MM) * GRID_SPACING_MM;

    // Convert grid bounds to world coordinates (must match wall positioning)
    double x_min_world = helix::mesh::printer_x_to_world_x(x_grid_start, bed_center_x, coord_scale);
    double x_max_world = helix::mesh::printer_x_to_world_x(x_grid_end, bed_center_x, coord_scale);
    double y_min_world =
        helix::mesh::printer_y_to_world_y(y_grid_end, bed_center_y, coord_scale); // Y inverted
    double y_max_world =
        helix::mesh::printer_y_to_world_y(y_grid_start, bed_center_y, coord_scale); // Y inverted

    // Calculate bed half dimensions for wall bounds calculation
    double bed_half_width = (renderer->bed_max_x - renderer->bed_min_x) / 2.0 * coord_scale;
    double bed_half_height = (renderer->bed_max_y - renderer->bed_min_y) / 2.0 * coord_scale;
    if (!renderer->geometry_computed) {
        bed_half_width = (renderer->cols - 1) / 2.0 * BED_MESH_SCALE;
        bed_half_height = (renderer->rows - 1) / 2.0 * BED_MESH_SCALE;
    }

    // Use cached z_center for world-space Z coordinates
    double z_min_world = helix::mesh::mesh_z_to_world_z(
        renderer->mesh_min_z, renderer->cached_z_center, renderer->view_state.z_scale);
    double z_max_world = helix::mesh::mesh_z_to_world_z(
        renderer->mesh_max_z, renderer->cached_z_center, renderer->view_state.z_scale);

    // Calculate wall bounds using centralized function
    auto bounds =
        helix::mesh::compute_wall_bounds(z_min_world, z_max_world, bed_half_width, bed_half_height);
    double floor_z = bounds.floor_z;

    // Configure label drawing style (smaller font than axis letters)
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_white();
    label_dsc.font = &noto_sans_10; // Smaller font for numeric labels
    label_dsc.opa = LV_OPA_80;      // Slightly more transparent than axis letters
    label_dsc.align = LV_TEXT_ALIGN_CENTER;
    label_dsc.text_local = 1; // Tell LVGL to copy text (we use stack buffers)

    // Use same spacing as grid lines (50mm) for tick labels
    double tick_spacing = GRID_SPACING_MM;

    // World-space offset to push tick labels OUTWARD from grid edges (prevents overlap)
    constexpr double TICK_LABEL_OUTWARD_OFFSET = 20.0; // World units away from grid edge

    // X-axis tick label offsets: at front edge (at floor level, centered beneath tick)
    constexpr int X_LABEL_OFFSET_X = -15; // Center label beneath tick
    constexpr int X_LABEL_OFFSET_Y = 0;   // At floor level
    // Y-axis tick label offsets: at right edge (same pattern as X)
    constexpr int Y_LABEL_OFFSET_X = -15; // Center label beneath tick
    constexpr int Y_LABEL_OFFSET_Y = 0;   // At floor level
    // Z-axis tick label offsets: to the left of the axis line (closer to wall)
    constexpr int Z_LABEL_OFFSET_X = -38;
    constexpr int Z_LABEL_OFFSET_Y = -6;

    // Draw X-axis tick labels along FRONT edge of the grid, pushed outward
    // Only label every other tick (0, 100, 200... not 50, 150, 250) to reduce crowding
    int x_tick_index = 0;
    for (double x_mm = x_grid_start; x_mm <= x_grid_end + 0.001; x_mm += tick_spacing) {
        if (x_tick_index % 2 == 0) {
            double x_world = helix::mesh::printer_x_to_world_x(x_mm, bed_center_x, coord_scale);
            // Push outward from front edge (+Y direction in world space)
            bed_mesh_point_3d_t tick = bed_mesh_projection_project_3d_to_2d(
                x_world, y_max_world + TICK_LABEL_OUTWARD_OFFSET, floor_z, canvas_width,
                canvas_height, &renderer->view_state);
            draw_axis_tick_label(layer, &label_dsc, tick.screen_x, tick.screen_y, X_LABEL_OFFSET_X,
                                 X_LABEL_OFFSET_Y, x_mm, canvas_width, canvas_height);
        }
        x_tick_index++;
    }

    // Draw Y-axis tick labels along RIGHT edge of the grid, pushed outward
    // Only label every other tick to reduce crowding
    int y_tick_index = 0;
    for (double y_mm = y_grid_start; y_mm <= y_grid_end + 0.001; y_mm += tick_spacing) {
        if (y_tick_index % 2 == 0) {
            double y_world = helix::mesh::printer_y_to_world_y(y_mm, bed_center_y, coord_scale);
            // Push outward from right edge (+X direction in world space)
            bed_mesh_point_3d_t tick = bed_mesh_projection_project_3d_to_2d(
                x_max_world + TICK_LABEL_OUTWARD_OFFSET, y_world, floor_z, canvas_width,
                canvas_height, &renderer->view_state);
            draw_axis_tick_label(layer, &label_dsc, tick.screen_x, tick.screen_y, Y_LABEL_OFFSET_X,
                                 Y_LABEL_OFFSET_Y, y_mm, canvas_width, canvas_height);
        }
        y_tick_index++;
    }

    // Draw Z-axis tick labels on the LEFT WALL at front-left corner
    // Use x_min_world and y_max_world (grid-aligned positions)

    // Generate 3 evenly-spaced Z labels along the wall (reduced from 5 to avoid crowding)
    constexpr int NUM_Z_LABELS = 3;
    for (int i = 0; i < NUM_Z_LABELS; i++) {
        double t = static_cast<double>(i) / (NUM_Z_LABELS - 1);
        double z_world = bounds.floor_z + t * (bounds.ceiling_z - bounds.floor_z);

        // Convert world Z back to mesh Z for display, adding back the normalization
        // offset so labels show original probe heights (not mean-subtracted values)
        double z_mm = helix::mesh::world_z_to_mesh_z(z_world, renderer->cached_z_center,
                                                     renderer->view_state.z_scale) +
                      renderer->z_display_offset;

        bed_mesh_point_3d_t tick = bed_mesh_projection_project_3d_to_2d(
            x_min_world, y_max_world, z_world, canvas_width, canvas_height, &renderer->view_state);
        draw_axis_tick_label(layer, &label_dsc, tick.screen_x, tick.screen_y, Z_LABEL_OFFSET_X,
                             Z_LABEL_OFFSET_Y, z_mm, canvas_width, canvas_height, true);
    }
}

} // namespace mesh
} // namespace helix
