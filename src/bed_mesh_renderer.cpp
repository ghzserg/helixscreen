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

#include "bed_mesh_renderer.h"

#include "ui_fonts.h"

#include "bed_mesh_coordinate_transform.h"
#include "bed_mesh_gradient.h"
#include "bed_mesh_internal.h"
#include "bed_mesh_overlays.h"
#include "bed_mesh_projection.h"
#include "bed_mesh_rasterizer.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

// ============================================================================
// Constants
// ============================================================================

namespace {

// Use the default angles from the public header (bed_mesh_renderer.h)
// This ensures consistency between the renderer and any code that reads those constants

// Canvas rendering
constexpr double CANVAS_PADDING_FACTOR = 0.95; // Small margin for anti-aliasing at edges
constexpr double INITIAL_FOV_SCALE = 150.0;    // Starting point for auto-scale (gets adjusted)
const lv_color_t CANVAS_BG_COLOR = lv_color_make(40, 40, 40); // Dark gray background

// ========== Geometry & Visibility Constants (Phase 1 refactoring) ==========
// Visibility margin for partially visible geometry
constexpr int VISIBILITY_MARGIN_PX = 10;

// Wall height factor (Mainsail-style: extends to 2x the mesh Z range above z_min)
constexpr double WALL_HEIGHT_FACTOR = 2.0;

} // anonymous namespace

// ============================================================================
// Helper Function Forward Declarations
// ============================================================================
static void compute_mesh_bounds(bed_mesh_renderer_t* renderer);
static double compute_dynamic_z_scale(double z_range);
static void update_trig_cache(bed_mesh_view_state_t* view_state);
static void project_and_cache_vertices(bed_mesh_renderer_t* renderer, int canvas_width,
                                       int canvas_height);
static void project_and_cache_quads(bed_mesh_renderer_t* renderer, int canvas_width,
                                    int canvas_height);
static void compute_projected_mesh_bounds(const bed_mesh_renderer_t* renderer, int* out_min_x,
                                          int* out_max_x, int* out_min_y, int* out_max_y);
static void compute_centering_offset(int mesh_min_x, int mesh_max_x, int mesh_min_y, int mesh_max_y,
                                     int layer_offset_x, int layer_offset_y, int canvas_width,
                                     int canvas_height, int* out_offset_x, int* out_offset_y);
static void generate_mesh_quads(bed_mesh_renderer_t* renderer);
static void sort_quads_by_depth(std::vector<bed_mesh_quad_3d_t>& quads);
static void render_quad(lv_layer_t* layer, const bed_mesh_quad_3d_t& quad, bool use_gradient);

// ============================================================================
// Public API Implementation
// ============================================================================

bed_mesh_renderer_t* bed_mesh_renderer_create(void) {
    bed_mesh_renderer_t* renderer = new (std::nothrow) bed_mesh_renderer_t;
    if (!renderer) {
        spdlog::error("Failed to allocate bed mesh renderer");
        return nullptr;
    }

    // Initialize state machine
    renderer->state = RendererState::UNINITIALIZED;

    // Initialize mesh data
    renderer->rows = 0;
    renderer->cols = 0;
    renderer->mesh_min_z = 0.0;
    renderer->mesh_max_z = 0.0;
    renderer->has_mesh_data = false;

    renderer->auto_color_range = true;
    renderer->color_min_z = 0.0;
    renderer->color_max_z = 0.0;

    // Initialize bed bounds (will be set via set_bed_bounds)
    renderer->bed_min_x = 0.0;
    renderer->bed_min_y = 0.0;
    renderer->bed_max_x = 0.0;
    renderer->bed_max_y = 0.0;
    renderer->has_bed_bounds = false;

    // Initialize mesh bounds (probe area, will be set via set_bounds)
    renderer->mesh_area_min_x = 0.0;
    renderer->mesh_area_min_y = 0.0;
    renderer->mesh_area_max_x = 0.0;
    renderer->mesh_area_max_y = 0.0;
    renderer->has_mesh_bounds = false;

    // Initialize computed geometry parameters
    renderer->bed_center_x = 0.0;
    renderer->bed_center_y = 0.0;
    renderer->coord_scale = 1.0;
    renderer->geometry_computed = false;

    // Default view state (Mainsail-style: looking from front-right toward back-left)
    renderer->view_state.angle_x = BED_MESH_DEFAULT_ANGLE_X;
    renderer->view_state.angle_z = BED_MESH_DEFAULT_ANGLE_Z;
    renderer->view_state.z_scale = BED_MESH_DEFAULT_Z_SCALE;
    renderer->view_state.fov_scale = INITIAL_FOV_SCALE;
    renderer->view_state.camera_distance = 1000.0; // Default, recomputed when mesh data is set
    renderer->view_state.is_dragging = false;

    // Initialize trig cache as invalid (will be computed on first render)
    renderer->view_state.trig_cache_valid = false;
    renderer->view_state.cached_cos_x = 0.0;
    renderer->view_state.cached_sin_x = 0.0;
    renderer->view_state.cached_cos_z = 0.0;
    renderer->view_state.cached_sin_z = 0.0;

    // Initialize centering offsets to zero (will be computed after projection)
    renderer->view_state.center_offset_x = 0;
    renderer->view_state.center_offset_y = 0;

    // Initialize layer offsets to zero (updated every frame during render)
    renderer->view_state.layer_offset_x = 0;
    renderer->view_state.layer_offset_y = 0;

    spdlog::debug("Created bed mesh renderer");
    return renderer;
}

void bed_mesh_renderer_destroy(bed_mesh_renderer_t* renderer) {
    if (!renderer) {
        return;
    }

    spdlog::debug("Destroying bed mesh renderer");
    delete renderer;
}

bool bed_mesh_renderer_set_mesh_data(bed_mesh_renderer_t* renderer, const float* const* mesh,
                                     int rows, int cols) {
    if (!renderer || !mesh || rows <= 0 || cols <= 0) {
        spdlog::error(
            "Invalid parameters for set_mesh_data: renderer={}, mesh={}, rows={}, cols={}",
            (void*)renderer, (void*)mesh, rows, cols);
        if (renderer) {
            renderer->state = RendererState::ERROR;
        }
        return false;
    }

    spdlog::debug("Setting mesh data: {}x{} points", rows, cols);

    // Allocate storage
    renderer->mesh.clear();
    renderer->mesh.resize(static_cast<size_t>(rows));
    for (int row = 0; row < rows; row++) {
        renderer->mesh[static_cast<size_t>(row)].resize(static_cast<size_t>(cols));
        for (int col = 0; col < cols; col++) {
            renderer->mesh[static_cast<size_t>(row)][static_cast<size_t>(col)] =
                static_cast<double>(mesh[row][col]);
        }
    }

    renderer->rows = rows;
    renderer->cols = cols;
    renderer->has_mesh_data = true;

    // Compute bounds
    compute_mesh_bounds(renderer);

    // If auto color range, update it
    if (renderer->auto_color_range) {
        renderer->color_min_z = renderer->mesh_min_z;
        renderer->color_max_z = renderer->mesh_max_z;
    }

    spdlog::debug("Mesh bounds: min_z={:.3f}, max_z={:.3f}, range={:.3f}", renderer->mesh_min_z,
                  renderer->mesh_max_z, renderer->mesh_max_z - renderer->mesh_min_z);

    // Compute camera distance from mesh size and perspective strength
    // Formula: camera_distance = mesh_diagonal / perspective_strength
    // Where 0 = orthographic (very far), 1 = max perspective (close)
    double mesh_width = (cols - 1) * BED_MESH_SCALE;
    double mesh_height = (rows - 1) * BED_MESH_SCALE;
    double mesh_diagonal = std::sqrt(mesh_width * mesh_width + mesh_height * mesh_height);

    if (BED_MESH_PERSPECTIVE_STRENGTH > 0.001) {
        renderer->view_state.camera_distance = mesh_diagonal / BED_MESH_PERSPECTIVE_STRENGTH;
    } else {
        // Near-orthographic: very far camera
        renderer->view_state.camera_distance = mesh_diagonal * 100.0;
    }
    spdlog::debug("Camera distance: {:.1f} (mesh_diagonal={:.1f}, perspective={:.2f})",
                  renderer->view_state.camera_distance, mesh_diagonal,
                  BED_MESH_PERSPECTIVE_STRENGTH);

    // Pre-generate geometry quads (constant for this mesh data)
    // Previously regenerated every frame (wasteful!) - now only on data change
    spdlog::debug("[MESH_DATA] Initial quad generation with z_scale={:.2f}",
                  renderer->view_state.z_scale);
    generate_mesh_quads(renderer);
    spdlog::debug("Pre-generated {} quads from mesh data", renderer->quads.size());

    // State transition: UNINITIALIZED or READY_TO_RENDER → MESH_LOADED
    renderer->state = RendererState::MESH_LOADED;

    return true;
}

void bed_mesh_renderer_set_rotation(bed_mesh_renderer_t* renderer, double angle_x, double angle_z) {
    if (!renderer) {
        return;
    }

    renderer->view_state.angle_x = angle_x;
    renderer->view_state.angle_z = angle_z;

    // Rotation changes invalidate cached projections (READY_TO_RENDER → MESH_LOADED)
    if (renderer->state == RendererState::READY_TO_RENDER) {
        renderer->state = RendererState::MESH_LOADED;
    }
}

void bed_mesh_renderer_set_bounds(bed_mesh_renderer_t* renderer, double bed_x_min, double bed_x_max,
                                  double bed_y_min, double bed_y_max, double mesh_x_min,
                                  double mesh_x_max, double mesh_y_min, double mesh_y_max) {
    if (!renderer) {
        return;
    }

    // Set bed bounds (full print bed area - used for grid/walls)
    renderer->bed_min_x = bed_x_min;
    renderer->bed_max_x = bed_x_max;
    renderer->bed_min_y = bed_y_min;
    renderer->bed_max_y = bed_y_max;
    renderer->has_bed_bounds = true;

    // Set mesh bounds (probe area - used for positioning mesh surface within bed)
    renderer->mesh_area_min_x = mesh_x_min;
    renderer->mesh_area_max_x = mesh_x_max;
    renderer->mesh_area_min_y = mesh_y_min;
    renderer->mesh_area_max_y = mesh_y_max;
    renderer->has_mesh_bounds = true;

    // Compute derived geometry parameters
    renderer->bed_center_x = (bed_x_min + bed_x_max) / 2.0;
    renderer->bed_center_y = (bed_y_min + bed_y_max) / 2.0;

    // Compute scale factor: normalize larger bed dimension to target world size
    // Target world size matches the old BED_MESH_SCALE-based sizing (~200 world units)
    constexpr double TARGET_WORLD_SIZE = 200.0;
    double bed_size_x = bed_x_max - bed_x_min;
    double bed_size_y = bed_y_max - bed_y_min;
    double larger_dimension = std::max(bed_size_x, bed_size_y);
    renderer->coord_scale =
        helix::mesh::compute_bed_scale_factor(larger_dimension, TARGET_WORLD_SIZE);
    renderer->geometry_computed = true;

    spdlog::debug("Set bounds: bed [{:.1f}, {:.1f}] x [{:.1f}, {:.1f}], mesh [{:.1f}, {:.1f}] x "
                  "[{:.1f}, {:.1f}], center=({:.1f}, {:.1f}), scale={:.4f}",
                  bed_x_min, bed_x_max, bed_y_min, bed_y_max, mesh_x_min, mesh_x_max, mesh_y_min,
                  mesh_y_max, renderer->bed_center_x, renderer->bed_center_y,
                  renderer->coord_scale);

    // Bounds changes invalidate cached projections and quads
    if (renderer->state == RendererState::READY_TO_RENDER ||
        renderer->state == RendererState::MESH_LOADED) {
        renderer->state = RendererState::MESH_LOADED;
    }
}

const bed_mesh_view_state_t* bed_mesh_renderer_get_view_state(bed_mesh_renderer_t* renderer) {
    if (!renderer) {
        return nullptr;
    }
    return &renderer->view_state;
}

void bed_mesh_renderer_set_view_state(bed_mesh_renderer_t* renderer,
                                      const bed_mesh_view_state_t* state) {
    if (!renderer || !state) {
        return;
    }
    renderer->view_state = *state;

    // View state changes invalidate cached projections (READY_TO_RENDER → MESH_LOADED)
    if (renderer->state == RendererState::READY_TO_RENDER) {
        renderer->state = RendererState::MESH_LOADED;
    }
}

void bed_mesh_renderer_set_dragging(bed_mesh_renderer_t* renderer, bool is_dragging) {
    if (!renderer) {
        return;
    }
    renderer->view_state.is_dragging = is_dragging;
}

void bed_mesh_renderer_set_z_scale(bed_mesh_renderer_t* renderer, double z_scale) {
    if (!renderer) {
        return;
    }
    // Clamp to valid range
    z_scale = std::max(BED_MESH_MIN_Z_SCALE, std::min(BED_MESH_MAX_Z_SCALE, z_scale));

    // Check if z_scale actually changed
    bool changed = (renderer->view_state.z_scale != z_scale);
    renderer->view_state.z_scale = z_scale;

    // Z-scale affects quad vertex Z coordinates - regenerate if changed
    if (changed && renderer->has_mesh_data) {
        generate_mesh_quads(renderer);
        spdlog::debug("Regenerated quads due to z_scale change to {:.2f}", z_scale);

        // State transition: READY_TO_RENDER → MESH_LOADED (quads regenerated, projections invalid)
        if (renderer->state == RendererState::READY_TO_RENDER) {
            renderer->state = RendererState::MESH_LOADED;
        }
    }
}

void bed_mesh_renderer_set_fov_scale(bed_mesh_renderer_t* renderer, double fov_scale) {
    if (!renderer) {
        return;
    }
    renderer->view_state.fov_scale = fov_scale;

    // FOV changes invalidate cached projections (READY_TO_RENDER → MESH_LOADED)
    if (renderer->state == RendererState::READY_TO_RENDER) {
        renderer->state = RendererState::MESH_LOADED;
    }
}

void bed_mesh_renderer_set_color_range(bed_mesh_renderer_t* renderer, double min_z, double max_z) {
    if (!renderer) {
        return;
    }

    // Check if color range actually changed
    bool changed = (renderer->color_min_z != min_z || renderer->color_max_z != max_z);

    renderer->auto_color_range = false;
    renderer->color_min_z = min_z;
    renderer->color_max_z = max_z;

    spdlog::debug("Manual color range set: min={:.3f}, max={:.3f}", min_z, max_z);

    // Color range affects quad vertex colors - regenerate if changed
    if (changed && renderer->has_mesh_data) {
        generate_mesh_quads(renderer);
        spdlog::debug("Regenerated quads due to color range change");

        // State transition: READY_TO_RENDER → MESH_LOADED (quads regenerated, projections invalid)
        if (renderer->state == RendererState::READY_TO_RENDER) {
            renderer->state = RendererState::MESH_LOADED;
        }
    }
}

void bed_mesh_renderer_auto_color_range(bed_mesh_renderer_t* renderer) {
    if (!renderer) {
        return;
    }

    // Check if color range will change
    bool changed = false;
    if (renderer->has_mesh_data) {
        changed = (renderer->color_min_z != renderer->mesh_min_z ||
                   renderer->color_max_z != renderer->mesh_max_z);
    }

    renderer->auto_color_range = true;
    if (renderer->has_mesh_data) {
        renderer->color_min_z = renderer->mesh_min_z;
        renderer->color_max_z = renderer->mesh_max_z;

        // Regenerate quads if color range changed
        if (changed) {
            generate_mesh_quads(renderer);
            spdlog::debug("Regenerated quads due to auto color range change");

            // State transition: READY_TO_RENDER → MESH_LOADED (quads regenerated, projections
            // invalid)
            if (renderer->state == RendererState::READY_TO_RENDER) {
                renderer->state = RendererState::MESH_LOADED;
            }
        }
    }

    spdlog::debug("Auto color range enabled");
}

bool bed_mesh_renderer_render(bed_mesh_renderer_t* renderer, lv_layer_t* layer, int canvas_width,
                              int canvas_height) {
    if (!renderer || !layer) {
        spdlog::error("Invalid parameters for render: renderer={}, layer={}", (void*)renderer,
                      (void*)layer);
        return false;
    }

    // State validation: Cannot render in UNINITIALIZED or ERROR state
    if (renderer->state == RendererState::UNINITIALIZED) {
        spdlog::warn("Cannot render: no mesh data loaded (state: UNINITIALIZED)");
        return false;
    }

    if (renderer->state == RendererState::ERROR) {
        spdlog::error("Cannot render: renderer in ERROR state");
        return false;
    }

    // Redundant check for backwards compatibility
    if (!renderer->has_mesh_data) {
        spdlog::warn("No mesh data loaded, cannot render");
        return false;
    }

    // Skip rendering if dimensions are invalid
    if (canvas_width <= 0 || canvas_height <= 0) {
        spdlog::debug("Skipping render: invalid dimensions {}x{}", canvas_width, canvas_height);
        return false;
    }

    spdlog::debug("Rendering mesh to {}x{} layer (dragging={})", canvas_width, canvas_height,
                  renderer->view_state.is_dragging);

    // DEBUG: Log mesh Z bounds and coordinate parameters (using cached z_center)
    double debug_grid_z =
        helix::mesh::compute_grid_z(renderer->cached_z_center, renderer->view_state.z_scale);
    spdlog::debug("[COORDS] mesh_min_z={:.4f}, mesh_max_z={:.4f}, z_center={:.4f}, z_scale={:.2f}, "
                  "grid_z={:.2f}",
                  renderer->mesh_min_z, renderer->mesh_max_z, renderer->cached_z_center,
                  renderer->view_state.z_scale, debug_grid_z);
    spdlog::debug(
        "[COORDS] angle_x={:.1f}, angle_z={:.1f}, fov_scale={:.2f}, center_offset=({},{})",
        renderer->view_state.angle_x, renderer->view_state.angle_z, renderer->view_state.fov_scale,
        renderer->view_state.center_offset_x, renderer->view_state.center_offset_y);

    // Clear background using layer draw API
    // Get layer's clip area (used for clipping output, NOT for canvas dimensions)
    // IMPORTANT: During partial redraws, clip_area may be smaller than the widget.
    // We must use the passed-in canvas_width/height for projection calculations,
    // otherwise the 3D projection math will be corrupted on partial redraws.
    const lv_area_t* clip_area = &layer->_clip_area;
    int clip_width = lv_area_get_width(clip_area);
    int clip_height = lv_area_get_height(clip_area);
    int layer_offset_x = clip_area->x1; // Layer's screen X position
    int layer_offset_y = clip_area->y1; // Layer's screen Y position

    spdlog::debug("[LAYER] Widget: {}x{}, Clip area: {}x{} at offset ({},{})", canvas_width,
                  canvas_height, clip_width, clip_height, layer_offset_x, layer_offset_y);

    // Draw background to fill the clip area (not the full canvas)
    // LVGL will clip this to the dirty region during partial redraws
    lv_draw_rect_dsc_t bg_dsc;
    lv_draw_rect_dsc_init(&bg_dsc);
    bg_dsc.bg_color = CANVAS_BG_COLOR;
    bg_dsc.bg_opa = LV_OPA_COVER;
    lv_draw_rect(layer, &bg_dsc, clip_area);

    // Compute dynamic Z scale if needed
    double z_range = renderer->mesh_max_z - renderer->mesh_min_z;
    double new_z_scale;
    if (z_range < 1e-6) {
        // Flat mesh, use default scale
        new_z_scale = BED_MESH_DEFAULT_Z_SCALE;
    } else {
        // Compute dynamic scale to fit mesh in reasonable height
        new_z_scale = compute_dynamic_z_scale(z_range);
    }

    // Only regenerate quads if z_scale changed
    if (renderer->view_state.z_scale != new_z_scale) {
        spdlog::debug("[Z_SCALE] Changing z_scale from {:.2f} to {:.2f} (z_range={:.4f})",
                      renderer->view_state.z_scale, new_z_scale, z_range);
        renderer->view_state.z_scale = new_z_scale;
        generate_mesh_quads(renderer);
        spdlog::debug("Regenerated quads due to dynamic z_scale change to {:.2f}", new_z_scale);
    } else {
        spdlog::debug("[Z_SCALE] Keeping z_scale at {:.2f} (z_range={:.4f})",
                      renderer->view_state.z_scale, z_range);
    }

    // Update cached trigonometric values (avoids recomputing sin/cos for every vertex)
    update_trig_cache(&renderer->view_state);

    // Compute FOV scale ONCE on first render (when fov_scale is still at default)
    // This prevents grow/shrink effect when rotating - scale stays constant
    if (renderer->view_state.fov_scale == INITIAL_FOV_SCALE) {
        // Project all mesh vertices with initial scale to get actual bounds
        project_and_cache_vertices(renderer, canvas_width, canvas_height);

        // Compute actual projected bounds using helper function
        int min_x, max_x, min_y, max_y;
        compute_projected_mesh_bounds(renderer, &min_x, &max_x, &min_y, &max_y);

        // ALSO include wall corners in bounds calculation (walls extend WALL_HEIGHT_FACTOR * mesh
        // height) This prevents walls from being clipped when they extend above the mesh
        double mesh_half_width = (renderer->cols - 1) / 2.0 * BED_MESH_SCALE;
        double mesh_half_height = (renderer->rows - 1) / 2.0 * BED_MESH_SCALE;
        double z_min_world = helix::mesh::mesh_z_to_world_z(
            renderer->mesh_min_z, renderer->cached_z_center, renderer->view_state.z_scale);
        double z_max_world = helix::mesh::mesh_z_to_world_z(
            renderer->mesh_max_z, renderer->cached_z_center, renderer->view_state.z_scale);
        double wall_z_max = z_min_world + WALL_HEIGHT_FACTOR * (z_max_world - z_min_world);

        // Project wall top corners and expand bounds
        double x_min = -mesh_half_width, x_max = mesh_half_width;
        double y_min = -mesh_half_height, y_max = mesh_half_height;
        bed_mesh_point_3d_t wall_corners[4] = {
            bed_mesh_projection_project_3d_to_2d(x_min, y_min, wall_z_max, canvas_width,
                                                 canvas_height, &renderer->view_state),
            bed_mesh_projection_project_3d_to_2d(x_max, y_min, wall_z_max, canvas_width,
                                                 canvas_height, &renderer->view_state),
            bed_mesh_projection_project_3d_to_2d(x_min, y_max, wall_z_max, canvas_width,
                                                 canvas_height, &renderer->view_state),
            bed_mesh_projection_project_3d_to_2d(x_max, y_max, wall_z_max, canvas_width,
                                                 canvas_height, &renderer->view_state),
        };
        for (const auto& corner : wall_corners) {
            min_x = std::min(min_x, corner.screen_x);
            max_x = std::max(max_x, corner.screen_x);
            min_y = std::min(min_y, corner.screen_y);
            max_y = std::max(max_y, corner.screen_y);
        }

        // Calculate scale needed to fit projected bounds into canvas
        int projected_width = max_x - min_x;
        int projected_height = max_y - min_y;
        double scale_x = (canvas_width * CANVAS_PADDING_FACTOR) / projected_width;
        double scale_y = (canvas_height * CANVAS_PADDING_FACTOR) / projected_height;
        double scale_factor = std::min(scale_x, scale_y);

        spdlog::info("[FOV] Canvas: {}x{}, Projected (incl walls): {}x{}, Padding: {:.2f}, "
                     "Scale: {:.2f}",
                     canvas_width, canvas_height, projected_width, projected_height,
                     CANVAS_PADDING_FACTOR, scale_factor);

        // Apply scale (only once, not every frame)
        renderer->view_state.fov_scale *= scale_factor;
        spdlog::info("[FOV] Final fov_scale: {:.2f} (initial {} * scale {:.2f})",
                     renderer->view_state.fov_scale, INITIAL_FOV_SCALE, scale_factor);
    }

    // Project vertices with current (stable) fov_scale
    project_and_cache_vertices(renderer, canvas_width, canvas_height);

    // Center mesh once on first render (offsets start at 0 from initialization)
    // After initial centering, offset remains stable across rotations
    if (renderer->view_state.center_offset_x == 0 && renderer->view_state.center_offset_y == 0) {
        // Compute bounds with current projection
        int inner_min_x, inner_max_x, inner_min_y, inner_max_y;
        compute_projected_mesh_bounds(renderer, &inner_min_x, &inner_max_x, &inner_min_y,
                                      &inner_max_y);

        // Calculate centering offset using helper function
        compute_centering_offset(inner_min_x, inner_max_x, inner_min_y, inner_max_y, layer_offset_x,
                                 layer_offset_y, canvas_width, canvas_height,
                                 &renderer->view_state.center_offset_x,
                                 &renderer->view_state.center_offset_y);

        spdlog::debug("[CENTER] Computed centering offset: ({}, {})",
                      renderer->view_state.center_offset_x, renderer->view_state.center_offset_y);
    }

    // Quads are now pre-generated in set_mesh_data() - no need to regenerate every frame!
    // Just project vertices and update cached screen coordinates

    // Apply layer offset for final rendering (updated every frame for animation support)
    // IMPORTANT: Must set BEFORE projecting vertices/quads so both use the same offsets!
    renderer->view_state.layer_offset_x = layer_offset_x;
    renderer->view_state.layer_offset_y = layer_offset_y;

    // Re-project grid vertices with final view state (fov_scale, centering, AND layer offset)
    // This ensures grid lines and quads are projected with identical view parameters
    project_and_cache_vertices(renderer, canvas_width, canvas_height);

    // PERF: Track rendering pipeline timings
    auto t_start = std::chrono::high_resolution_clock::now();

    // Project all quad vertices once and cache screen coordinates + depths
    // This replaces 3 separate projection passes (depth calc, bounds tracking, rendering)
    project_and_cache_quads(renderer, canvas_width, canvas_height);
    auto t_project = std::chrono::high_resolution_clock::now();

    // Sort quads by depth using cached avg_depth (painter's algorithm - furthest first)
    sort_quads_by_depth(renderer->quads);
    auto t_sort = std::chrono::high_resolution_clock::now();

    spdlog::trace("Rendering {} quads with {} mode", renderer->quads.size(),
                  renderer->view_state.is_dragging ? "solid" : "gradient");

    // DEBUG: Track overall gradient quad bounds using cached coordinates
    int quad_min_x = INT_MAX, quad_max_x = INT_MIN;
    int quad_min_y = INT_MAX, quad_max_y = INT_MIN;
    for (const auto& quad : renderer->quads) {
        for (int i = 0; i < 4; i++) {
            quad_min_x = std::min(quad_min_x, quad.screen_x[i]);
            quad_max_x = std::max(quad_max_x, quad.screen_x[i]);
            quad_min_y = std::min(quad_min_y, quad.screen_y[i]);
            quad_max_y = std::max(quad_max_y, quad.screen_y[i]);
        }
    }
    spdlog::trace("[GRADIENT_OVERALL] All quads bounds: x=[{},{}] y=[{},{}] quads={} canvas={}x{}",
                  quad_min_x, quad_max_x, quad_min_y, quad_max_y, renderer->quads.size(),
                  canvas_width, canvas_height);

    // DEBUG: Log first quad vertex positions using cached coordinates
    if (!renderer->quads.empty()) {
        const auto& first_quad = renderer->quads[0];
        spdlog::trace("[FIRST_QUAD] Vertices (world -> cached screen):");
        for (int i = 0; i < 4; i++) {
            spdlog::trace("  v{}: world=({:.2f},{:.2f},{:.2f}) -> screen=({},{})", i,
                          first_quad.vertices[i].x, first_quad.vertices[i].y,
                          first_quad.vertices[i].z, first_quad.screen_x[i], first_quad.screen_y[i]);
        }
    }

    // Render reference grids FIRST (bottom, back, side walls) so mesh occludes them properly
    // Since LVGL canvas has no depth buffer, draw order determines visibility
    helix::mesh::render_reference_grids(layer, renderer, canvas_width, canvas_height);

    // Render quads using cached screen coordinates (drawn AFTER grids so mesh is in front)
    bool use_gradient = !renderer->view_state.is_dragging;
    for (const auto& quad : renderer->quads) {
        render_quad(layer, quad, use_gradient);
    }
    auto t_rasterize = std::chrono::high_resolution_clock::now();

    // Render wireframe grid on top of mesh surface
    helix::mesh::render_grid_lines(layer, renderer, canvas_width, canvas_height);

    // Render axis labels
    helix::mesh::render_axis_labels(layer, renderer, canvas_width, canvas_height);

    // Render numeric tick labels on axes
    helix::mesh::render_numeric_axis_ticks(layer, renderer, canvas_width, canvas_height);
    auto t_overlays = std::chrono::high_resolution_clock::now();

    // PERF: Log performance breakdown (use -vvv to see)
    auto ms_project = std::chrono::duration<double, std::milli>(t_project - t_start).count();
    auto ms_sort = std::chrono::duration<double, std::milli>(t_sort - t_project).count();
    auto ms_rasterize = std::chrono::duration<double, std::milli>(t_rasterize - t_sort).count();
    auto ms_overlays = std::chrono::duration<double, std::milli>(t_overlays - t_rasterize).count();
    auto ms_total = std::chrono::duration<double, std::milli>(t_overlays - t_start).count();

    spdlog::trace(
        "[PERF] Render: {:.2f}ms total | Proj: {:.2f}ms ({:.0f}%) | Sort: {:.2f}ms ({:.0f}%) | "
        "Raster: {:.2f}ms ({:.0f}%) | Overlays: {:.2f}ms ({:.0f}%) | Mode: {}",
        ms_total, ms_project, 100.0 * ms_project / ms_total, ms_sort, 100.0 * ms_sort / ms_total,
        ms_rasterize, 100.0 * ms_rasterize / ms_total, ms_overlays, 100.0 * ms_overlays / ms_total,
        renderer->view_state.is_dragging ? "solid" : "gradient");

    // Output canvas dimensions and view coordinates
    spdlog::trace(
        "[CANVAS_SIZE] Widget dimensions: {}x{} | Alt: {:.1f}° | Az: {:.1f}° | Zoom: {:.2f}x",
        canvas_width, canvas_height, renderer->view_state.angle_x, renderer->view_state.angle_z,
        renderer->view_state.fov_scale / INITIAL_FOV_SCALE);

    // State transition: MESH_LOADED → READY_TO_RENDER (successful render with cached projections)
    if (renderer->state == RendererState::MESH_LOADED) {
        renderer->state = RendererState::READY_TO_RENDER;
    }

    spdlog::trace("Mesh rendering complete");
    return true;
}

// Helper function implementations

static void compute_mesh_bounds(bed_mesh_renderer_t* renderer) {
    if (!renderer || !renderer->has_mesh_data) {
        return;
    }

    double min_z = renderer->mesh[0][0];
    double max_z = renderer->mesh[0][0];

    for (int row = 0; row < renderer->rows; row++) {
        for (int col = 0; col < renderer->cols; col++) {
            double z = renderer->mesh[static_cast<size_t>(row)][static_cast<size_t>(col)];
            if (z < min_z)
                min_z = z;
            if (z > max_z)
                max_z = z;
        }
    }

    renderer->mesh_min_z = min_z;
    renderer->mesh_max_z = max_z;
    // Cache z_center to avoid repeated computation (computed once per mesh data change)
    renderer->cached_z_center = helix::mesh::compute_mesh_z_center(min_z, max_z);
}

static double compute_dynamic_z_scale(double z_range) {
    // Compute scale to amplify Z range to target height
    double z_scale = BED_MESH_DEFAULT_Z_TARGET_HEIGHT / z_range;

    // Clamp to valid range
    z_scale = std::max(BED_MESH_MIN_Z_SCALE, std::min(BED_MESH_MAX_Z_SCALE, z_scale));

    return z_scale;
}

/**
 * Update cached trigonometric values when angles change
 * Call this once per frame before projection loop to eliminate redundant trig computations
 * @param view_state Mutable view state to update (const-cast required)
 */
static inline void update_trig_cache(bed_mesh_view_state_t* view_state) {
    // Angle conversion for looking DOWN at the bed from above:
    // - angle_x uses +90° offset so user's -90° = top-down, -45° = tilted view
    // - angle_z is used directly (negative = clockwise from above)
    //
    // Convention:
    //   angle_x = -90° → top-down view (internal 0°)
    //   angle_x = -45° → 45° tilt from top-down (internal 45°)
    //   angle_x = 0°   → edge-on view (internal 90°)
    //   angle_z = 0°   → front view
    //   angle_z = -45° → rotated 45° clockwise (from above)
    double x_angle_rad = (view_state->angle_x + 90.0) * M_PI / 180.0;
    double z_angle_rad = view_state->angle_z * M_PI / 180.0;

    view_state->cached_cos_x = std::cos(x_angle_rad);
    view_state->cached_sin_x = std::sin(x_angle_rad);
    view_state->cached_cos_z = std::cos(z_angle_rad);
    view_state->cached_sin_z = std::sin(z_angle_rad);
    view_state->trig_cache_valid = true;
}

/**
 * Project all mesh vertices to screen space and cache for reuse
 * Avoids redundant projections in grid/axis rendering (15-20% speedup)
 * @param renderer Renderer with mesh data
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 */
static void project_and_cache_vertices(bed_mesh_renderer_t* renderer, int canvas_width,
                                       int canvas_height) {
    if (!renderer || !renderer->has_mesh_data) {
        return;
    }

    // Resize SOA caches if needed (avoid reallocation on every frame)
    if (renderer->projected_screen_x.size() != static_cast<size_t>(renderer->rows)) {
        renderer->projected_screen_x.resize(static_cast<size_t>(renderer->rows));
        renderer->projected_screen_y.resize(static_cast<size_t>(renderer->rows));
    }

    // Use cached z_center (computed once in compute_mesh_bounds)

    // Project all vertices once (projection handles centering internally)
    for (int row = 0; row < renderer->rows; row++) {
        if (renderer->projected_screen_x[static_cast<size_t>(row)].size() !=
            static_cast<size_t>(renderer->cols)) {
            renderer->projected_screen_x[static_cast<size_t>(row)].resize(
                static_cast<size_t>(renderer->cols));
            renderer->projected_screen_y[static_cast<size_t>(row)].resize(
                static_cast<size_t>(renderer->cols));
        }

        for (int col = 0; col < renderer->cols; col++) {
            // Convert mesh coordinates to world space
            double world_x, world_y;

            if (renderer->geometry_computed) {
                // Mainsail-style: Position mesh within bed using mesh_area bounds
                double cols_minus_1 = static_cast<double>(renderer->cols - 1);
                double rows_minus_1 = static_cast<double>(renderer->rows - 1);

                double printer_x =
                    renderer->mesh_area_min_x +
                    col / cols_minus_1 * (renderer->mesh_area_max_x - renderer->mesh_area_min_x);
                double printer_y =
                    renderer->mesh_area_min_y +
                    row / rows_minus_1 * (renderer->mesh_area_max_y - renderer->mesh_area_min_y);

                world_x = helix::mesh::printer_x_to_world_x(printer_x, renderer->bed_center_x,
                                                            renderer->coord_scale);
                world_y = helix::mesh::printer_y_to_world_y(printer_y, renderer->bed_center_y,
                                                            renderer->coord_scale);
            } else {
                // Legacy: Index-based coordinates
                world_x = helix::mesh::mesh_col_to_world_x(col, renderer->cols, BED_MESH_SCALE);
                world_y = helix::mesh::mesh_row_to_world_y(row, renderer->rows, BED_MESH_SCALE);
            }

            double world_z = helix::mesh::mesh_z_to_world_z(
                renderer->mesh[static_cast<size_t>(row)][static_cast<size_t>(col)],
                renderer->cached_z_center, renderer->view_state.z_scale);

            // Project to screen space and cache only screen coordinates (SOA)
            bed_mesh_point_3d_t projected = bed_mesh_projection_project_3d_to_2d(
                world_x, world_y, world_z, canvas_width, canvas_height, &renderer->view_state);

            renderer->projected_screen_x[static_cast<size_t>(row)][static_cast<size_t>(col)] =
                projected.screen_x;
            renderer->projected_screen_y[static_cast<size_t>(row)][static_cast<size_t>(col)] =
                projected.screen_y;

            // DEBUG: Log sample point (center of mesh)
            if (row == renderer->rows / 2 && col == renderer->cols / 2) {
                spdlog::debug(
                    "[GRID_VERTEX] mesh[{},{}] -> world({:.2f},{:.2f},{:.2f}) -> screen({},{})",
                    row, col, world_x, world_y, world_z, projected.screen_x, projected.screen_y);
            }
        }
    }
}

/**
 * @brief Project all quad vertices to screen space and cache results
 *
 * Computes screen coordinates and depths for all vertices of all quads in a single pass.
 * This eliminates redundant projections - previously each quad was projected 3 times:
 * once for depth sorting, once for bounds tracking, and once during rendering.
 *
 * Must be called whenever view state changes (rotation, FOV, centering offset).
 *
 * @param renderer Renderer with quads already generated
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 *
 * Side effects:
 * - Updates quad.screen_x[], quad.screen_y[], quad.depths[] for all quads
 * - Updates quad.avg_depth for depth sorting
 */
static void project_and_cache_quads(bed_mesh_renderer_t* renderer, int canvas_width,
                                    int canvas_height) {
    if (!renderer || renderer->quads.empty()) {
        return;
    }

    for (auto& quad : renderer->quads) {
        double total_depth = 0.0;

        for (int i = 0; i < 4; i++) {
            bed_mesh_point_3d_t projected = bed_mesh_projection_project_3d_to_2d(
                quad.vertices[i].x, quad.vertices[i].y, quad.vertices[i].z, canvas_width,
                canvas_height, &renderer->view_state);

            quad.screen_x[i] = projected.screen_x;
            quad.screen_y[i] = projected.screen_y;
            quad.depths[i] = projected.depth;
            total_depth += projected.depth;
        }

        quad.avg_depth = total_depth / 4.0;
    }

    // DEBUG: Log a sample quad vertex (TL of center quad corresponds to mesh center)
    // For an NxN grid, center quad is at index ((N-1)/2 * (N-1) + (N-1)/2)
    if (!renderer->quads.empty()) {
        int center_row = (renderer->rows - 1) / 2;
        int center_col = (renderer->cols - 1) / 2;
        size_t center_quad_idx =
            static_cast<size_t>(center_row * (renderer->cols - 1) + center_col);
        if (center_quad_idx < renderer->quads.size()) {
            const auto& q = renderer->quads[center_quad_idx];
            // TL vertex (index 2) corresponds to mesh[row][col]
            spdlog::debug(
                "[QUAD_VERTEX] quad[{}] TL -> world({:.2f},{:.2f},{:.2f}) -> screen({},{})",
                center_quad_idx, q.vertices[2].x, q.vertices[2].y, q.vertices[2].z, q.screen_x[2],
                q.screen_y[2]);
        }
    }

    spdlog::trace("[CACHE] Projected {} quads to screen space", renderer->quads.size());
}

/**
 * @brief Compute 2D bounding box of projected mesh points
 *
 * Scans all cached projected_points to find min/max X and Y coordinates in screen space.
 * Used for FOV scaling and centering calculations.
 *
 * @param renderer Renderer with projected_points cache populated
 * @param[out] out_min_x Minimum screen X coordinate
 * @param[out] out_max_x Maximum screen X coordinate
 * @param[out] out_min_y Minimum screen Y coordinate
 * @param[out] out_max_y Maximum screen Y coordinate
 */
static void compute_projected_mesh_bounds(const bed_mesh_renderer_t* renderer, int* out_min_x,
                                          int* out_max_x, int* out_min_y, int* out_max_y) {
    if (!renderer || !renderer->has_mesh_data) {
        *out_min_x = *out_max_x = *out_min_y = *out_max_y = 0;
        return;
    }

    int min_x = INT_MAX, max_x = INT_MIN;
    int min_y = INT_MAX, max_y = INT_MIN;

    for (int row = 0; row < renderer->rows; row++) {
        for (int col = 0; col < renderer->cols; col++) {
            int screen_x =
                renderer->projected_screen_x[static_cast<size_t>(row)][static_cast<size_t>(col)];
            int screen_y =
                renderer->projected_screen_y[static_cast<size_t>(row)][static_cast<size_t>(col)];
            min_x = std::min(min_x, screen_x);
            max_x = std::max(max_x, screen_x);
            min_y = std::min(min_y, screen_y);
            max_y = std::max(max_y, screen_y);
        }
    }

    *out_min_x = min_x;
    *out_max_x = max_x;
    *out_min_y = min_y;
    *out_max_y = max_y;
}

/**
 * @brief Compute centering offset to center mesh in layer
 *
 * Compares mesh bounding box center (in screen space) to layer center
 * (in screen space) and returns offset needed to align them.
 *
 * COORDINATE SPACE: All inputs and outputs are in SCREEN SPACE (absolute pixels).
 *
 * @param mesh_min_x Minimum projected mesh X (screen space)
 * @param mesh_max_x Maximum projected mesh X (screen space)
 * @param mesh_min_y Minimum projected mesh Y (screen space)
 * @param mesh_max_y Maximum projected mesh Y (screen space)
 * @param layer_offset_x Layer's screen position X (from clip_area->x1)
 * @param layer_offset_y Layer's screen position Y (from clip_area->y1)
 * @param canvas_width Layer width in pixels
 * @param canvas_height Layer height in pixels
 * @param[out] out_offset_x Horizontal centering offset
 * @param[out] out_offset_y Vertical centering offset
 */
static void compute_centering_offset(int mesh_min_x, int mesh_max_x, int mesh_min_y, int mesh_max_y,
                                     int /*layer_offset_x*/, int /*layer_offset_y*/,
                                     int canvas_width, int canvas_height, int* out_offset_x,
                                     int* out_offset_y) {
    // Calculate centers in CANVAS space (not screen space)
    // The mesh bounds are relative to canvas origin, so we center within the canvas
    // Layer offset is handled separately in projection to support animations
    int mesh_center_x = (mesh_min_x + mesh_max_x) / 2;
    int mesh_center_y = (mesh_min_y + mesh_max_y) / 2;
    int canvas_center_x = canvas_width / 2;
    int canvas_center_y = canvas_height / 2;

    // Offset needed to move mesh center to canvas center (canvas-relative coords)
    *out_offset_x = canvas_center_x - mesh_center_x;
    *out_offset_y = canvas_center_y - mesh_center_y;

    spdlog::debug("[CENTERING] Mesh center: ({},{}) -> Canvas center: ({},{}) = offset ({},{})",
                  mesh_center_x, mesh_center_y, canvas_center_x, canvas_center_y, *out_offset_x,
                  *out_offset_y);
}

static void generate_mesh_quads(bed_mesh_renderer_t* renderer) {
    if (!renderer || !renderer->has_mesh_data) {
        return;
    }

    renderer->quads.clear();

    // Pre-allocate capacity to avoid reallocations during generation
    // Number of quads = (rows-1) × (cols-1)
    int expected_quads = (renderer->rows - 1) * (renderer->cols - 1);
    renderer->quads.reserve(static_cast<size_t>(expected_quads));

    // Use cached z_center (computed once in compute_mesh_bounds)

    // Generate quads for each mesh cell
    for (int row = 0; row < renderer->rows - 1; row++) {
        for (int col = 0; col < renderer->cols - 1; col++) {
            bed_mesh_quad_3d_t quad;

            // Compute base X,Y positions
            double base_x_0, base_x_1, base_y_0, base_y_1;

            if (renderer->geometry_computed) {
                // Mainsail-style: Position mesh within bed using mesh_area bounds
                // Interpolate printer coordinates from mesh indices
                double cols_minus_1 = static_cast<double>(renderer->cols - 1);
                double rows_minus_1 = static_cast<double>(renderer->rows - 1);

                double printer_x0 =
                    renderer->mesh_area_min_x +
                    col / cols_minus_1 * (renderer->mesh_area_max_x - renderer->mesh_area_min_x);
                double printer_x1 = renderer->mesh_area_min_x +
                                    (col + 1) / cols_minus_1 *
                                        (renderer->mesh_area_max_x - renderer->mesh_area_min_x);
                double printer_y0 =
                    renderer->mesh_area_min_y +
                    row / rows_minus_1 * (renderer->mesh_area_max_y - renderer->mesh_area_min_y);
                double printer_y1 = renderer->mesh_area_min_y +
                                    (row + 1) / rows_minus_1 *
                                        (renderer->mesh_area_max_y - renderer->mesh_area_min_y);

                // Convert printer coordinates to world space
                base_x_0 = helix::mesh::printer_x_to_world_x(printer_x0, renderer->bed_center_x,
                                                             renderer->coord_scale);
                base_x_1 = helix::mesh::printer_x_to_world_x(printer_x1, renderer->bed_center_x,
                                                             renderer->coord_scale);
                base_y_0 = helix::mesh::printer_y_to_world_y(printer_y0, renderer->bed_center_y,
                                                             renderer->coord_scale);
                base_y_1 = helix::mesh::printer_y_to_world_y(printer_y1, renderer->bed_center_y,
                                                             renderer->coord_scale);
            } else {
                // Legacy: Index-based coordinates (centered around origin)
                // Note: Y is inverted because mesh[0] = front edge
                base_x_0 = helix::mesh::mesh_col_to_world_x(col, renderer->cols, BED_MESH_SCALE);
                base_x_1 =
                    helix::mesh::mesh_col_to_world_x(col + 1, renderer->cols, BED_MESH_SCALE);
                base_y_0 = helix::mesh::mesh_row_to_world_y(row, renderer->rows, BED_MESH_SCALE);
                base_y_1 =
                    helix::mesh::mesh_row_to_world_y(row + 1, renderer->rows, BED_MESH_SCALE);
            }

            /**
             * Quad vertex layout (view from above, looking down -Z axis):
             *
             *   mesh[row][col]         mesh[row][col+1]
             *        [2]TL ──────────────── [3]TR
             *         │                      │
             *         │                      │
             *         │       QUAD           │     ← One mesh cell
             *         │     (row,col)        │
             *         │                      │
             *        [0]BL ──────────────── [1]BR
             *   mesh[row+1][col]       mesh[row+1][col+1]
             *
             * Vertex indices: [0]=BL, [1]=BR, [2]=TL, [3]=TR
             * Mesh mapping:   [0]=mesh[row+1][col], [1]=mesh[row+1][col+1],
             *                 [2]=mesh[row][col],    [3]=mesh[row][col+1]
             *
             * Split into triangles for rasterization:
             *   Triangle 1: [0]→[1]→[2] (BL→BR→TL, lower-right triangle)
             *   Triangle 2: [1]→[3]→[2] (BR→TR→TL, upper-left triangle)
             *
             * Winding order: Counter-clockwise (CCW) for front-facing
             */
            quad.vertices[0].x = base_x_0;
            quad.vertices[0].y = base_y_1;
            quad.vertices[0].z = helix::mesh::mesh_z_to_world_z(
                renderer->mesh[static_cast<size_t>(row + 1)][static_cast<size_t>(col)],
                renderer->cached_z_center, renderer->view_state.z_scale);
            quad.vertices[0].color = bed_mesh_gradient_height_to_color(
                renderer->mesh[static_cast<size_t>(row + 1)][static_cast<size_t>(col)],
                renderer->color_min_z, renderer->color_max_z);

            quad.vertices[1].x = base_x_1;
            quad.vertices[1].y = base_y_1;
            quad.vertices[1].z = helix::mesh::mesh_z_to_world_z(
                renderer->mesh[static_cast<size_t>(row + 1)][static_cast<size_t>(col + 1)],
                renderer->cached_z_center, renderer->view_state.z_scale);
            quad.vertices[1].color = bed_mesh_gradient_height_to_color(
                renderer->mesh[static_cast<size_t>(row + 1)][static_cast<size_t>(col + 1)],
                renderer->color_min_z, renderer->color_max_z);

            quad.vertices[2].x = base_x_0;
            quad.vertices[2].y = base_y_0;
            quad.vertices[2].z = helix::mesh::mesh_z_to_world_z(
                renderer->mesh[static_cast<size_t>(row)][static_cast<size_t>(col)],
                renderer->cached_z_center, renderer->view_state.z_scale);
            quad.vertices[2].color = bed_mesh_gradient_height_to_color(
                renderer->mesh[static_cast<size_t>(row)][static_cast<size_t>(col)],
                renderer->color_min_z, renderer->color_max_z);

            quad.vertices[3].x = base_x_1;
            quad.vertices[3].y = base_y_0;
            quad.vertices[3].z = helix::mesh::mesh_z_to_world_z(
                renderer->mesh[static_cast<size_t>(row)][static_cast<size_t>(col + 1)],
                renderer->cached_z_center, renderer->view_state.z_scale);
            quad.vertices[3].color = bed_mesh_gradient_height_to_color(
                renderer->mesh[static_cast<size_t>(row)][static_cast<size_t>(col + 1)],
                renderer->color_min_z, renderer->color_max_z);

            // Compute center color for fast rendering
            bed_mesh_rgb_t avg_color = {
                static_cast<uint8_t>((quad.vertices[0].color.red + quad.vertices[1].color.red +
                                      quad.vertices[2].color.red + quad.vertices[3].color.red) /
                                     4),
                static_cast<uint8_t>((quad.vertices[0].color.green + quad.vertices[1].color.green +
                                      quad.vertices[2].color.green + quad.vertices[3].color.green) /
                                     4),
                static_cast<uint8_t>((quad.vertices[0].color.blue + quad.vertices[1].color.blue +
                                      quad.vertices[2].color.blue + quad.vertices[3].color.blue) /
                                     4)};
            quad.center_color = lv_color_make(avg_color.r, avg_color.g, avg_color.b);

            quad.avg_depth = 0.0; // Will be computed during projection

            renderer->quads.push_back(quad);
        }
    }

    // DEBUG: Log quad generation with z_scale used
    spdlog::debug("[QUAD_GEN] Generated {} quads, z_scale={:.2f}, z_center={:.4f}",
                  renderer->quads.size(), renderer->view_state.z_scale, renderer->cached_z_center);
    // Log a sample quad to verify Z values
    if (!renderer->quads.empty()) {
        int center_row = (renderer->rows - 1) / 2;
        int center_col = (renderer->cols - 1) / 2;
        size_t center_quad_idx =
            static_cast<size_t>(center_row * (renderer->cols - 1) + center_col);
        if (center_quad_idx < renderer->quads.size()) {
            const auto& q = renderer->quads[center_quad_idx];
            spdlog::debug(
                "[QUAD_GEN] Center quad[{}] TL world_z={:.2f}, from mesh_z={:.4f}", center_quad_idx,
                q.vertices[2].z,
                renderer->mesh[static_cast<size_t>(center_row)][static_cast<size_t>(center_col)]);
        }
    }
    spdlog::trace("Generated {} quads from {}x{} mesh", renderer->quads.size(), renderer->rows,
                  renderer->cols);
}

static void sort_quads_by_depth(std::vector<bed_mesh_quad_3d_t>& quads) {
    std::sort(quads.begin(), quads.end(),
              [](const bed_mesh_quad_3d_t& a, const bed_mesh_quad_3d_t& b) {
                  // Descending order: furthest (largest depth) first
                  return a.avg_depth > b.avg_depth;
              });
}

// ============================================================================
// Quad Rendering
// ============================================================================

/**
 * @brief Render a single quad using cached screen coordinates
 *
 * IMPORTANT: Assumes quad screen coordinates are already computed via
 * project_and_cache_quads(). Does NOT perform projection - uses cached values.
 *
 * Uses helix::mesh rasterizer module for triangle fills - LVGL handles clipping
 * automatically via the layer system.
 *
 * @param layer LVGL draw layer
 * @param quad Quad with cached screen_x[], screen_y[] coordinates
 * @param use_gradient true = gradient interpolation, false = solid color
 */
static void render_quad(lv_layer_t* layer, const bed_mesh_quad_3d_t& quad, bool use_gradient) {
    /**
     * Render quad as 2 triangles (diagonal split from BL to TR):
     *
     *    [2]TL ──────── [3]TR
     *      │  ╲          │
     *      │    ╲  Tri2  │     Tri1: [0]BL → [1]BR → [2]TL (lower-right)
     *      │ Tri1 ╲      │     Tri2: [1]BR → [3]TR → [2]TL (upper-left)
     *      │        ╲    │
     *    [0]BL ──────── [1]BR
     *
     * Using indices [0,1,2] and [1,3,2] creates CCW winding for front-facing triangles
     */

    // Triangle 1: [0]BL → [1]BR → [2]TL
    // use_gradient = false during drag for performance (solid color fallback)
    // use_gradient = true when static for quality (gradient interpolation)
    if (use_gradient) {
        helix::mesh::fill_triangle_gradient(
            layer, quad.screen_x[0], quad.screen_y[0], quad.vertices[0].color, quad.screen_x[1],
            quad.screen_y[1], quad.vertices[1].color, quad.screen_x[2], quad.screen_y[2],
            quad.vertices[2].color);
    } else {
        helix::mesh::fill_triangle_solid(layer, quad.screen_x[0], quad.screen_y[0],
                                         quad.screen_x[1], quad.screen_y[1], quad.screen_x[2],
                                         quad.screen_y[2], quad.center_color);
    }

    // Triangle 2: [1]BR → [3]TR → [2]TL
    if (use_gradient) {
        helix::mesh::fill_triangle_gradient(
            layer, quad.screen_x[1], quad.screen_y[1], quad.vertices[1].color, quad.screen_x[2],
            quad.screen_y[2], quad.vertices[2].color, quad.screen_x[3], quad.screen_y[3],
            quad.vertices[3].color);
    } else {
        helix::mesh::fill_triangle_solid(layer, quad.screen_x[1], quad.screen_y[1],
                                         quad.screen_x[2], quad.screen_y[2], quad.screen_x[3],
                                         quad.screen_y[3], quad.center_color);
    }
}
