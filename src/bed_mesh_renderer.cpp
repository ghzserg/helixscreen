// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bed_mesh_renderer.h"

#include "ui_fonts.h"

#include "bed_mesh_coordinate_transform.h"
#include "bed_mesh_geometry.h"
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
static void render_quad(lv_layer_t* layer, const bed_mesh_quad_3d_t& quad, bool use_gradient);
static void prepare_render_frame(bed_mesh_renderer_t* renderer, int canvas_width, int canvas_height,
                                 int layer_offset_x, int layer_offset_y);
static void render_mesh_surface(lv_layer_t* layer, bed_mesh_renderer_t* renderer);
static void render_decorations(lv_layer_t* layer, bed_mesh_renderer_t* renderer, int canvas_width,
                               int canvas_height);

// Phase 4: Adaptive render mode helpers (forward declarations)
static void record_frame_time(bed_mesh_renderer_t* renderer, float frame_ms);
static float calculate_average_fps(const bed_mesh_renderer_t* renderer);
static bool is_fps_below_threshold(const bed_mesh_renderer_t* renderer, float min_fps);
static lv_color_t z_to_heatmap_color(float z, float z_min, float z_max);
static void render_2d_heatmap(lv_layer_t* layer, bed_mesh_renderer_t* renderer, int canvas_width,
                              int canvas_height, int offset_x, int offset_y);

// ============================================================================
// Public API Implementation
// ============================================================================

bed_mesh_renderer_t* bed_mesh_renderer_create(void) {
    bed_mesh_renderer_t* renderer = new (std::nothrow) bed_mesh_renderer_t;
    if (!renderer) {
        spdlog::error("[Bed Mesh Renderer] Failed to allocate bed mesh renderer");
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

    spdlog::debug("[Bed Mesh Renderer] Created bed mesh renderer");
    return renderer;
}

void bed_mesh_renderer_destroy(bed_mesh_renderer_t* renderer) {
    if (!renderer) {
        return;
    }

    spdlog::debug("[Bed Mesh Renderer] Destroying bed mesh renderer");
    delete renderer;
}

bool bed_mesh_renderer_set_mesh_data(bed_mesh_renderer_t* renderer, const float* const* mesh,
                                     int rows, int cols) {
    if (!renderer || !mesh || rows <= 0 || cols <= 0) {
        spdlog::error("[Bed Mesh Renderer] Invalid parameters for set_mesh_data: renderer={}, "
                      "mesh={}, rows={}, cols={}",
                      (void*)renderer, (void*)mesh, rows, cols);
        if (renderer) {
            renderer->state = RendererState::ERROR;
        }
        return false;
    }

    spdlog::debug("[Bed Mesh Renderer] Setting mesh data: {}x{} points", rows, cols);

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

    spdlog::debug("[Bed Mesh Renderer] Mesh bounds: min_z={:.3f}, max_z={:.3f}, range={:.3f}",
                  renderer->mesh_min_z, renderer->mesh_max_z,
                  renderer->mesh_max_z - renderer->mesh_min_z);

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
    spdlog::debug(
        "[Bed Mesh Renderer] Camera distance: {:.1f} (mesh_diagonal={:.1f}, perspective={:.2f})",
        renderer->view_state.camera_distance, mesh_diagonal, BED_MESH_PERSPECTIVE_STRENGTH);

    // Pre-generate geometry quads (constant for this mesh data)
    // Previously regenerated every frame (wasteful!) - now only on data change
    spdlog::debug("[MESH_DATA] Initial quad generation with z_scale={:.2f}",
                  renderer->view_state.z_scale);
    helix::mesh::generate_mesh_quads(renderer);
    spdlog::debug("[Bed Mesh Renderer] Pre-generated {} quads from mesh data",
                  renderer->quads.size());

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

    spdlog::debug("[Bed Mesh Renderer] Set bounds: bed [{:.1f}, {:.1f}] x [{:.1f}, {:.1f}], mesh "
                  "[{:.1f}, {:.1f}] x "
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
        helix::mesh::generate_mesh_quads(renderer);
        spdlog::debug("[Bed Mesh Renderer] Regenerated quads due to z_scale change to {:.2f}",
                      z_scale);

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

    spdlog::debug("[Bed Mesh Renderer] Manual color range set: min={:.3f}, max={:.3f}", min_z,
                  max_z);

    // Color range affects quad vertex colors - regenerate if changed
    if (changed && renderer->has_mesh_data) {
        helix::mesh::generate_mesh_quads(renderer);
        spdlog::debug("[Bed Mesh Renderer] Regenerated quads due to color range change");

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
            helix::mesh::generate_mesh_quads(renderer);
            spdlog::debug("[Bed Mesh Renderer] Regenerated quads due to auto color range change");

            // State transition: READY_TO_RENDER → MESH_LOADED (quads regenerated, projections
            // invalid)
            if (renderer->state == RendererState::READY_TO_RENDER) {
                renderer->state = RendererState::MESH_LOADED;
            }
        }
    }

    spdlog::debug("[Bed Mesh Renderer] Auto color range enabled");
}

bool bed_mesh_renderer_render(bed_mesh_renderer_t* renderer, lv_layer_t* layer, int canvas_width,
                              int canvas_height) {
    if (!renderer || !layer) {
        spdlog::error("[Bed Mesh Renderer] Invalid parameters for render: renderer={}, layer={}",
                      (void*)renderer, (void*)layer);
        return false;
    }

    // State validation: Cannot render in UNINITIALIZED or ERROR state
    if (renderer->state == RendererState::UNINITIALIZED) {
        spdlog::warn(
            "[Bed Mesh Renderer] Cannot render: no mesh data loaded (state: UNINITIALIZED)");
        return false;
    }

    if (renderer->state == RendererState::ERROR) {
        spdlog::error("[Bed Mesh Renderer] Cannot render: renderer in ERROR state");
        return false;
    }

    // Redundant check for backwards compatibility
    if (!renderer->has_mesh_data) {
        spdlog::warn("[Bed Mesh Renderer] No mesh data loaded, cannot render");
        return false;
    }

    // Skip rendering if dimensions are invalid
    if (canvas_width <= 0 || canvas_height <= 0) {
        spdlog::debug("[Bed Mesh Renderer] Skipping render: invalid dimensions {}x{}", canvas_width,
                      canvas_height);
        return false;
    }

    spdlog::debug("[Bed Mesh Renderer] Rendering mesh to {}x{} layer (dragging={})", canvas_width,
                  canvas_height, renderer->view_state.is_dragging);

    // DEBUG: Log mesh Z bounds and coordinate parameters (using cached z_center)
    double debug_grid_z =
        helix::mesh::compute_grid_z(renderer->cached_z_center, renderer->view_state.z_scale);
    spdlog::debug("[Bed Mesh Renderer] [COORDS] mesh_min_z={:.4f}, mesh_max_z={:.4f}, "
                  "z_center={:.4f}, z_scale={:.2f}, "
                  "grid_z={:.2f}",
                  renderer->mesh_min_z, renderer->mesh_max_z, renderer->cached_z_center,
                  renderer->view_state.z_scale, debug_grid_z);
    spdlog::debug("[Bed Mesh Renderer] [COORDS] angle_x={:.1f}, angle_z={:.1f}, fov_scale={:.2f}, "
                  "center_offset=({},{})",
                  renderer->view_state.angle_x, renderer->view_state.angle_z,
                  renderer->view_state.fov_scale, renderer->view_state.center_offset_x,
                  renderer->view_state.center_offset_y);

    // Get layer's clip area (used for clipping output, NOT for canvas dimensions)
    // IMPORTANT: During partial redraws, clip_area may be smaller than the widget.
    // We must use the passed-in canvas_width/height for projection calculations,
    // otherwise the 3D projection math will be corrupted on partial redraws.
    const lv_area_t* clip_area = &layer->_clip_area;
    int clip_width = lv_area_get_width(clip_area);
    int clip_height = lv_area_get_height(clip_area);
    int layer_offset_x = clip_area->x1; // Layer's screen X position
    int layer_offset_y = clip_area->y1; // Layer's screen Y position

    spdlog::debug("[Bed Mesh Renderer] [LAYER] Widget: {}x{}, Clip area: {}x{} at offset ({},{})",
                  canvas_width, canvas_height, clip_width, clip_height, layer_offset_x,
                  layer_offset_y);

    // Draw background to fill the clip area (not the full canvas)
    // LVGL will clip this to the dirty region during partial redraws
    lv_draw_rect_dsc_t bg_dsc;
    lv_draw_rect_dsc_init(&bg_dsc);
    bg_dsc.bg_color = CANVAS_BG_COLOR;
    bg_dsc.bg_opa = LV_OPA_COVER;
    lv_draw_rect(layer, &bg_dsc, clip_area);

    // Performance tracking for complete render pipeline
    auto t_frame_start = std::chrono::high_resolution_clock::now();

    // Check render mode and dispatch to 3D or 2D rendering
    bool use_2d = bed_mesh_renderer_is_using_2d(renderer);

    if (use_2d) {
        // Fast 2D heatmap rendering (for slow hardware)
        render_2d_heatmap(layer, renderer, canvas_width, canvas_height, layer_offset_x,
                          layer_offset_y);

        auto t_frame_end = std::chrono::high_resolution_clock::now();
        auto ms_total =
            std::chrono::duration<double, std::milli>(t_frame_end - t_frame_start).count();

        // Record frame time for FPS tracking
        record_frame_time(renderer, static_cast<float>(ms_total));

        spdlog::trace("[Bed Mesh Renderer] [2D] Heatmap render: {:.2f}ms (FPS: {:.1f})", ms_total,
                      calculate_average_fps(renderer));
    } else {
        // Full 3D perspective rendering

        // Phase 1: Prepare rendering frame (projection parameters, view state)
        prepare_render_frame(renderer, canvas_width, canvas_height, layer_offset_x, layer_offset_y);
        auto t_prepare = std::chrono::high_resolution_clock::now();

        // Phase 2: Render mesh surface (quads with gradient/solid colors)
        render_mesh_surface(layer, renderer);
        auto t_surface = std::chrono::high_resolution_clock::now();

        // Phase 3: Render decorations (grids, labels, ticks)
        render_decorations(layer, renderer, canvas_width, canvas_height);
        auto t_decorations = std::chrono::high_resolution_clock::now();

        // PERF: Log overall render performance breakdown
        auto ms_prepare =
            std::chrono::duration<double, std::milli>(t_prepare - t_frame_start).count();
        auto ms_surface = std::chrono::duration<double, std::milli>(t_surface - t_prepare).count();
        auto ms_decorations =
            std::chrono::duration<double, std::milli>(t_decorations - t_surface).count();
        auto ms_total =
            std::chrono::duration<double, std::milli>(t_decorations - t_frame_start).count();

        // Record frame time for FPS tracking
        record_frame_time(renderer, static_cast<float>(ms_total));

        spdlog::trace("[Bed Mesh Renderer] [PERF] Total: {:.2f}ms | Prepare: {:.2f}ms ({:.0f}%) | "
                      "Surface: {:.2f}ms ({:.0f}%) | "
                      "Decorations: {:.2f}ms ({:.0f}%) | FPS: {:.1f}",
                      ms_total, ms_prepare, 100.0 * ms_prepare / ms_total, ms_surface,
                      100.0 * ms_surface / ms_total, ms_decorations,
                      100.0 * ms_decorations / ms_total, calculate_average_fps(renderer));

        // Output canvas dimensions and view coordinates
        spdlog::trace(
            "[Bed Mesh Renderer] [CANVAS_SIZE] Widget dimensions: {}x{} | Alt: {:.1f}° | Az: "
            "{:.1f}° | Zoom: {:.2f}x",
            canvas_width, canvas_height, renderer->view_state.angle_x, renderer->view_state.angle_z,
            renderer->view_state.fov_scale / INITIAL_FOV_SCALE);
    }

    // State transition: MESH_LOADED → READY_TO_RENDER (successful render with cached projections)
    if (renderer->state == RendererState::MESH_LOADED) {
        renderer->state = RendererState::READY_TO_RENDER;
    }

    spdlog::trace("[Bed Mesh Renderer] Mesh rendering complete");
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
                spdlog::debug("[Bed Mesh Renderer] [GRID_VERTEX] mesh[{},{}] -> "
                              "world({:.2f},{:.2f},{:.2f}) -> screen({},{})",
                              row, col, world_x, world_y, world_z, projected.screen_x,
                              projected.screen_y);
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
            spdlog::debug("[Bed Mesh Renderer] [QUAD_VERTEX] quad[{}] TL -> "
                          "world({:.2f},{:.2f},{:.2f}) -> screen({},{})",
                          center_quad_idx, q.vertices[2].x, q.vertices[2].y, q.vertices[2].z,
                          q.screen_x[2], q.screen_y[2]);
        }
    }

    spdlog::trace("[Bed Mesh Renderer] [CACHE] Projected {} quads to screen space",
                  renderer->quads.size());
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

    spdlog::debug("[Bed Mesh Renderer] [CENTERING] Mesh center: ({},{}) -> Canvas center: ({},{}) "
                  "= offset ({},{})",
                  mesh_center_x, mesh_center_y, canvas_center_x, canvas_center_y, *out_offset_x,
                  *out_offset_y);
}

/**
 * @brief Prepare rendering frame - compute projection parameters and update view state
 *
 * Performs one-time and per-frame preparation:
 * - Dynamic Z scale calculation (if mesh is too flat/tall)
 * - Trig cache update (avoids recomputing sin/cos for every vertex)
 * - FOV scaling on first render (prevents grow/shrink during rotation)
 * - Centering offset on first render (keeps mesh centered during rotation)
 *
 * @param renderer Renderer instance
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 * @param layer_offset_x Layer's screen position X (from clip area)
 * @param layer_offset_y Layer's screen position Y (from clip area)
 */
static void prepare_render_frame(bed_mesh_renderer_t* renderer, int canvas_width, int canvas_height,
                                 int layer_offset_x, int layer_offset_y) {
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
        spdlog::debug(
            "[Bed Mesh Renderer] [Z_SCALE] Changing z_scale from {:.2f} to {:.2f} (z_range={:.4f})",
            renderer->view_state.z_scale, new_z_scale, z_range);
        renderer->view_state.z_scale = new_z_scale;
        helix::mesh::generate_mesh_quads(renderer);
        spdlog::debug(
            "[Bed Mesh Renderer] Regenerated quads due to dynamic z_scale change to {:.2f}",
            new_z_scale);
    } else {
        spdlog::debug("[Bed Mesh Renderer] [Z_SCALE] Keeping z_scale at {:.2f} (z_range={:.4f})",
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

        spdlog::info("[Bed Mesh Renderer] [FOV] Canvas: {}x{}, Projected (incl walls): {}x{}, "
                     "Padding: {:.2f}, "
                     "Scale: {:.2f}",
                     canvas_width, canvas_height, projected_width, projected_height,
                     CANVAS_PADDING_FACTOR, scale_factor);

        // Apply scale (only once, not every frame)
        renderer->view_state.fov_scale *= scale_factor;
        spdlog::info(
            "[Bed Mesh Renderer] [FOV] Final fov_scale: {:.2f} (initial {} * scale {:.2f})",
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

        spdlog::debug("[Bed Mesh Renderer] [CENTER] Computed centering offset: ({}, {})",
                      renderer->view_state.center_offset_x, renderer->view_state.center_offset_y);
    }

    // Apply layer offset for final rendering (updated every frame for animation support)
    // IMPORTANT: Must set BEFORE projecting vertices/quads so both use the same offsets!
    renderer->view_state.layer_offset_x = layer_offset_x;
    renderer->view_state.layer_offset_y = layer_offset_y;

    // Re-project grid vertices with final view state (fov_scale, centering, AND layer offset)
    // This ensures grid lines and quads are projected with identical view parameters
    project_and_cache_vertices(renderer, canvas_width, canvas_height);
}

/**
 * @brief Render mesh surface as colored quads
 *
 * Projects all quad vertices, sorts by depth (painter's algorithm), and renders
 * each quad as two triangles. Uses gradient interpolation when static, solid
 * colors when dragging for performance.
 *
 * @param layer LVGL draw layer
 * @param renderer Renderer with prepared view state
 */
static void render_mesh_surface(lv_layer_t* layer, bed_mesh_renderer_t* renderer) {
    // PERF: Track rendering pipeline timings
    auto t_start = std::chrono::high_resolution_clock::now();

    // Get canvas dimensions from layer (already validated in main render function)
    const lv_area_t* clip_area = &layer->_clip_area;
    int canvas_width = lv_area_get_width(clip_area);
    int canvas_height = lv_area_get_height(clip_area);

    // Project all quad vertices once and cache screen coordinates + depths
    // This replaces 3 separate projection passes (depth calc, bounds tracking, rendering)
    project_and_cache_quads(renderer, canvas_width, canvas_height);
    auto t_project = std::chrono::high_resolution_clock::now();

    // Sort quads by depth using cached avg_depth (painter's algorithm - furthest first)
    helix::mesh::sort_quads_by_depth(renderer->quads);
    auto t_sort = std::chrono::high_resolution_clock::now();

    spdlog::trace("[Bed Mesh Renderer] Rendering {} quads with {} mode", renderer->quads.size(),
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
    spdlog::trace("[Bed Mesh Renderer] [GRADIENT_OVERALL] All quads bounds: x=[{},{}] y=[{},{}] "
                  "quads={} canvas={}x{}",
                  quad_min_x, quad_max_x, quad_min_y, quad_max_y, renderer->quads.size(),
                  canvas_width, canvas_height);

    // DEBUG: Log first quad vertex positions using cached coordinates
    if (!renderer->quads.empty()) {
        const auto& first_quad = renderer->quads[0];
        spdlog::trace("[Bed Mesh Renderer] [FIRST_QUAD] Vertices (world -> cached screen):");
        for (int i = 0; i < 4; i++) {
            spdlog::trace(
                "[Bed Mesh Renderer]   v{}: world=({:.2f},{:.2f},{:.2f}) -> screen=({},{})", i,
                first_quad.vertices[i].x, first_quad.vertices[i].y, first_quad.vertices[i].z,
                first_quad.screen_x[i], first_quad.screen_y[i]);
        }
    }

    // Render quads using cached screen coordinates
    bool use_gradient = !renderer->view_state.is_dragging;
    for (const auto& quad : renderer->quads) {
        render_quad(layer, quad, use_gradient);
    }
    auto t_rasterize = std::chrono::high_resolution_clock::now();

    // PERF: Log performance breakdown (use -vvv to see)
    auto ms_project = std::chrono::duration<double, std::milli>(t_project - t_start).count();
    auto ms_sort = std::chrono::duration<double, std::milli>(t_sort - t_project).count();
    auto ms_rasterize = std::chrono::duration<double, std::milli>(t_rasterize - t_sort).count();

    spdlog::trace("[Bed Mesh Renderer] [PERF] Surface render: Proj: {:.2f}ms ({:.0f}%) | Sort: "
                  "{:.2f}ms ({:.0f}%) | "
                  "Raster: {:.2f}ms ({:.0f}%) | Mode: {}",
                  ms_project, 100.0 * ms_project / (ms_project + ms_sort + ms_rasterize), ms_sort,
                  100.0 * ms_sort / (ms_project + ms_sort + ms_rasterize), ms_rasterize,
                  100.0 * ms_rasterize / (ms_project + ms_sort + ms_rasterize),
                  renderer->view_state.is_dragging ? "solid" : "gradient");
}

/**
 * @brief Render decorations (reference grids, grid lines, axis labels, tick marks)
 *
 * Renders overlay elements on top of the mesh surface:
 * - Reference grids (bottom, back, side walls)
 * - Wireframe grid on mesh surface
 * - Axis labels (X, Y, Z)
 * - Numeric tick labels on axes
 *
 * @param layer LVGL draw layer
 * @param renderer Renderer with prepared view state
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 */
static void render_decorations(lv_layer_t* layer, bed_mesh_renderer_t* renderer, int canvas_width,
                               int canvas_height) {
    auto t_start = std::chrono::high_resolution_clock::now();

    // Render reference grids (bottom, back, side walls)
    helix::mesh::render_reference_grids(layer, renderer, canvas_width, canvas_height);

    // Render wireframe grid on top of mesh surface
    helix::mesh::render_grid_lines(layer, renderer, canvas_width, canvas_height);

    // Render axis labels
    helix::mesh::render_axis_labels(layer, renderer, canvas_width, canvas_height);

    // Render numeric tick labels on axes
    helix::mesh::render_numeric_axis_ticks(layer, renderer, canvas_width, canvas_height);

    auto t_end = std::chrono::high_resolution_clock::now();
    auto ms_overlays = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    spdlog::trace("[Bed Mesh Renderer] [PERF] Decorations render: {:.2f}ms", ms_overlays);
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

// ============================================================================
// Phase 4: Adaptive Render Mode (FPS-based 3D/2D switching)
// ============================================================================

/**
 * @brief Record frame time for FPS tracking
 */
static void record_frame_time(bed_mesh_renderer_t* renderer, float frame_ms) {
    renderer->frame_times[renderer->fps_write_idx] = frame_ms;
    renderer->fps_write_idx = (renderer->fps_write_idx + 1) % BED_MESH_FPS_WINDOW_SIZE;
    if (renderer->fps_sample_count < BED_MESH_FPS_WINDOW_SIZE) {
        renderer->fps_sample_count++;
    }
}

/**
 * @brief Calculate average FPS from recorded frame times
 */
static float calculate_average_fps(const bed_mesh_renderer_t* renderer) {
    if (renderer->fps_sample_count == 0) {
        return 60.0f; // Assume good until measured
    }

    float total_ms = 0.0f;
    for (size_t i = 0; i < renderer->fps_sample_count; i++) {
        total_ms += renderer->frame_times[i];
    }
    float avg_ms = total_ms / static_cast<float>(renderer->fps_sample_count);
    return avg_ms > 0.0f ? 1000.0f / avg_ms : 60.0f;
}

/**
 * @brief Check if FPS is below threshold (requires full sample window)
 */
static bool is_fps_below_threshold(const bed_mesh_renderer_t* renderer, float min_fps) {
    return renderer->fps_sample_count >= BED_MESH_FPS_WINDOW_SIZE &&
           calculate_average_fps(renderer) < min_fps;
}

/**
 * @brief Map Z value to heatmap color (purple → green → red)
 *
 * Uses the same color gradient as 3D mode for visual consistency.
 */
static lv_color_t z_to_heatmap_color(float z, float z_min, float z_max) {
    // Use the shared bed mesh gradient function (handles normalization internally)
    return bed_mesh_gradient_height_to_color(static_cast<double>(z), static_cast<double>(z_min),
                                             static_cast<double>(z_max));
}

/**
 * @brief Render mesh as 2D heatmap with triangle-based color blending
 *
 * Each cell is rendered as 4 triangles meeting at center, with colors
 * averaged from the corner Z values. This provides smooth color transitions
 * while maintaining honest probe resolution (N-1 cells for N probe points).
 */
static void render_2d_heatmap(lv_layer_t* layer, bed_mesh_renderer_t* renderer, int canvas_width,
                              int canvas_height, int offset_x, int offset_y) {
    if (!renderer->has_mesh_data) {
        return;
    }

    // Layout parameters
    int padding = 8;
    int grid_width = canvas_width - 2 * padding;
    int grid_height = canvas_height - 2 * padding;

    // Calculate cell dimensions at actual mesh resolution
    // Grid shows honest probe resolution (N-1 cells for N probe points)
    int num_cells_x = renderer->cols - 1;
    int num_cells_y = renderer->rows - 1;

    // Guard against 1x1 mesh (no cells to render)
    if (num_cells_x <= 0 || num_cells_y <= 0) {
        spdlog::warn("[Bed Mesh] 2D heatmap requires at least 2x2 mesh (got {}x{})", renderer->cols,
                     renderer->rows);
        return;
    }

    int cell_w = grid_width / num_cells_x;
    int cell_h = grid_height / num_cells_y;
    if (cell_w < 1)
        cell_w = 1;
    if (cell_h < 1)
        cell_h = 1;

    // Center the grid
    int grid_x = offset_x + padding + (grid_width - cell_w * num_cells_x) / 2;
    int grid_y = offset_y + padding + (grid_height - cell_h * num_cells_y) / 2;

    // Z range for coloring
    float z_min = static_cast<float>(renderer->auto_color_range ? renderer->mesh_min_z
                                                                : renderer->color_min_z);
    float z_max = static_cast<float>(renderer->auto_color_range ? renderer->mesh_max_z
                                                                : renderer->color_max_z);
    float z_range = z_max - z_min;
    if (z_range < 0.001f)
        z_range = 0.001f;

    // Contour interval (in mm) - adaptive based on range
    float contour_interval = 0.05f; // 50 microns
    if (z_range > 0.5f)
        contour_interval = 0.1f;
    if (z_range < 0.1f)
        contour_interval = 0.02f;

    // Triangle-based rendering: each cell is 4 triangles meeting at center
    // Vertex colors come from actual mesh Z values - smooth blending without fake resolution
    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);
    tri_dsc.opa = LV_OPA_COVER;

    // Helper to blend colors (average RGB)
    auto blend_colors = [](lv_color_t c1, lv_color_t c2, lv_color_t c3) -> lv_color_t {
        return lv_color_make((c1.red + c2.red + c3.red) / 3, (c1.green + c2.green + c3.green) / 3,
                             (c1.blue + c2.blue + c3.blue) / 3);
    };

    // Render each cell as 4 triangles
    for (int row = 0; row < num_cells_y; row++) {
        for (int col = 0; col < num_cells_x; col++) {
            // Get Z values at the 4 corners of this cell
            float z_tl = static_cast<float>(
                renderer->mesh[static_cast<size_t>(row)][static_cast<size_t>(col)]);
            float z_tr = static_cast<float>(
                renderer->mesh[static_cast<size_t>(row)][static_cast<size_t>(col + 1)]);
            float z_bl = static_cast<float>(
                renderer->mesh[static_cast<size_t>(row + 1)][static_cast<size_t>(col)]);
            float z_br = static_cast<float>(
                renderer->mesh[static_cast<size_t>(row + 1)][static_cast<size_t>(col + 1)]);
            float z_center = (z_tl + z_tr + z_bl + z_br) / 4.0f;

            // Convert Z values to colors
            lv_color_t c_tl = z_to_heatmap_color(z_tl, z_min, z_max);
            lv_color_t c_tr = z_to_heatmap_color(z_tr, z_min, z_max);
            lv_color_t c_bl = z_to_heatmap_color(z_bl, z_min, z_max);
            lv_color_t c_br = z_to_heatmap_color(z_br, z_min, z_max);
            lv_color_t c_center = z_to_heatmap_color(z_center, z_min, z_max);

            // Screen coordinates for corners and center
            int x_left = grid_x + col * cell_w;
            int x_right = grid_x + (col + 1) * cell_w;
            int y_top = grid_y + row * cell_h;
            int y_bottom = grid_y + (row + 1) * cell_h;
            int x_mid = (x_left + x_right) / 2;
            int y_mid = (y_top + y_bottom) / 2;

            // Top triangle (TL - TR - Center)
            tri_dsc.color = blend_colors(c_tl, c_tr, c_center);
            tri_dsc.p[0].x = x_left;
            tri_dsc.p[0].y = y_top;
            tri_dsc.p[1].x = x_right;
            tri_dsc.p[1].y = y_top;
            tri_dsc.p[2].x = x_mid;
            tri_dsc.p[2].y = y_mid;
            lv_draw_triangle(layer, &tri_dsc);

            // Right triangle (TR - BR - Center)
            tri_dsc.color = blend_colors(c_tr, c_br, c_center);
            tri_dsc.p[0].x = x_right;
            tri_dsc.p[0].y = y_top;
            tri_dsc.p[1].x = x_right;
            tri_dsc.p[1].y = y_bottom;
            tri_dsc.p[2].x = x_mid;
            tri_dsc.p[2].y = y_mid;
            lv_draw_triangle(layer, &tri_dsc);

            // Bottom triangle (BR - BL - Center)
            tri_dsc.color = blend_colors(c_br, c_bl, c_center);
            tri_dsc.p[0].x = x_right;
            tri_dsc.p[0].y = y_bottom;
            tri_dsc.p[1].x = x_left;
            tri_dsc.p[1].y = y_bottom;
            tri_dsc.p[2].x = x_mid;
            tri_dsc.p[2].y = y_mid;
            lv_draw_triangle(layer, &tri_dsc);

            // Left triangle (BL - TL - Center)
            tri_dsc.color = blend_colors(c_bl, c_tl, c_center);
            tri_dsc.p[0].x = x_left;
            tri_dsc.p[0].y = y_bottom;
            tri_dsc.p[1].x = x_left;
            tri_dsc.p[1].y = y_top;
            tri_dsc.p[2].x = x_mid;
            tri_dsc.p[2].y = y_mid;
            lv_draw_triangle(layer, &tri_dsc);
        }
    }

    // NOTE: Contour lines disabled - the smooth gradient + hillshade provides
    // sufficient depth perception without the visual noise of disconnected segments.
    // A proper marching squares algorithm would be needed for smooth contour curves.
    (void)contour_interval; // Suppress unused warning

    // Draw subtle border around the entire grid
    lv_draw_rect_dsc_t border_dsc;
    lv_draw_rect_dsc_init(&border_dsc);
    border_dsc.bg_opa = LV_OPA_TRANSP;
    border_dsc.border_color = lv_color_make(60, 60, 60);
    border_dsc.border_width = 1;
    border_dsc.border_opa = LV_OPA_60;
    border_dsc.radius = 2;

    lv_area_t border_area;
    border_area.x1 = static_cast<int16_t>(grid_x - 1);
    border_area.y1 = static_cast<int16_t>(grid_y - 1);
    border_area.x2 = static_cast<int16_t>(grid_x + num_cells_x * cell_w + 1);
    border_area.y2 = static_cast<int16_t>(grid_y + num_cells_y * cell_h + 1);
    lv_draw_rect(layer, &border_dsc, &border_area);

    // Draw tooltip for touched cell
    if (renderer->touch_valid) {
        // cell_w and cell_h already calculated above at mesh resolution
        // Highlight the touched mesh cell
        lv_draw_rect_dsc_t highlight_dsc;
        lv_draw_rect_dsc_init(&highlight_dsc);
        highlight_dsc.bg_opa = LV_OPA_20;
        highlight_dsc.bg_color = lv_color_white();
        highlight_dsc.border_color = lv_color_white();
        highlight_dsc.border_width = 2;
        highlight_dsc.border_opa = LV_OPA_COVER;
        highlight_dsc.radius = 2;

        lv_area_t highlight_area;
        highlight_area.x1 = static_cast<int16_t>(grid_x + renderer->touched_col * cell_w);
        highlight_area.y1 = static_cast<int16_t>(grid_y + renderer->touched_row * cell_h);
        highlight_area.x2 = static_cast<int16_t>(highlight_area.x1 + cell_w - 1);
        highlight_area.y2 = static_cast<int16_t>(highlight_area.y1 + cell_h - 1);
        lv_draw_rect(layer, &highlight_dsc, &highlight_area);

        // Draw Z value tooltip
        char z_text[32];
        snprintf(z_text, sizeof(z_text), "%.3f mm", static_cast<double>(renderer->touched_z));

        // Position tooltip above the cell (or below if near top)
        int tooltip_x = highlight_area.x1 + cell_w / 2 - 30;
        int tooltip_y = highlight_area.y1 - 24;
        if (tooltip_y < offset_y + 5) {
            tooltip_y = highlight_area.y2 + 5;
        }

        // Draw tooltip background with shadow effect
        lv_draw_rect_dsc_t tooltip_bg;
        lv_draw_rect_dsc_init(&tooltip_bg);
        tooltip_bg.bg_color = lv_color_make(30, 30, 30);
        tooltip_bg.bg_opa = LV_OPA_90;
        tooltip_bg.radius = 6;
        tooltip_bg.border_color = lv_color_make(80, 80, 80);
        tooltip_bg.border_width = 1;
        tooltip_bg.border_opa = LV_OPA_60;

        lv_area_t tooltip_area = {.x1 = static_cast<int16_t>(tooltip_x - 8),
                                  .y1 = static_cast<int16_t>(tooltip_y - 4),
                                  .x2 = static_cast<int16_t>(tooltip_x + 68),
                                  .y2 = static_cast<int16_t>(tooltip_y + 18)};
        lv_draw_rect(layer, &tooltip_bg, &tooltip_area);

        // Draw tooltip text
        lv_draw_label_dsc_t label_dsc;
        lv_draw_label_dsc_init(&label_dsc);
        label_dsc.color = lv_color_white();
        label_dsc.font = &noto_sans_14;
        label_dsc.text = z_text;
        label_dsc.align = LV_TEXT_ALIGN_CENTER;

        lv_area_t label_area = {.x1 = static_cast<int16_t>(tooltip_x),
                                .y1 = static_cast<int16_t>(tooltip_y),
                                .x2 = static_cast<int16_t>(tooltip_x + 60),
                                .y2 = static_cast<int16_t>(tooltip_y + 14)};
        lv_draw_label(layer, &label_dsc, &label_area);
    }
}

// ============================================================================
// Public API: Render Mode Control
// ============================================================================

void bed_mesh_renderer_set_render_mode(bed_mesh_renderer_t* renderer, bed_mesh_render_mode_t mode) {
    if (!renderer)
        return;
    renderer->render_mode = mode;

    // If forcing a mode, update the fallback flag immediately
    if (mode == BED_MESH_RENDER_MODE_FORCE_2D) {
        renderer->using_2d_fallback = true;
    } else if (mode == BED_MESH_RENDER_MODE_FORCE_3D) {
        renderer->using_2d_fallback = false;
    }
    // AUTO mode: fallback flag is controlled by evaluate_render_mode()
}

bed_mesh_render_mode_t bed_mesh_renderer_get_render_mode(bed_mesh_renderer_t* renderer) {
    if (!renderer)
        return BED_MESH_RENDER_MODE_AUTO;
    return renderer->render_mode;
}

bool bed_mesh_renderer_is_using_2d(bed_mesh_renderer_t* renderer) {
    if (!renderer)
        return false;

    switch (renderer->render_mode) {
    case BED_MESH_RENDER_MODE_FORCE_2D:
        return true;
    case BED_MESH_RENDER_MODE_FORCE_3D:
        return false;
    case BED_MESH_RENDER_MODE_AUTO:
    default:
        return renderer->using_2d_fallback;
    }
}

void bed_mesh_renderer_evaluate_render_mode(bed_mesh_renderer_t* renderer) {
    if (!renderer)
        return;
    if (renderer->render_mode != BED_MESH_RENDER_MODE_AUTO) {
        spdlog::debug("[Bed Mesh Renderer] Mode evaluation skipped (mode={}, not AUTO)",
                      static_cast<int>(renderer->render_mode));
        return;
    }

    spdlog::debug("[Bed Mesh Renderer] Evaluating render mode: {} FPS samples, avg={:.1f} FPS",
                  renderer->fps_sample_count, calculate_average_fps(renderer));

    // Check if we have enough samples and FPS is below threshold
    if (is_fps_below_threshold(renderer, BED_MESH_FPS_THRESHOLD)) {
        if (!renderer->using_2d_fallback) {
            renderer->using_2d_fallback = true;
            spdlog::info("[Bed Mesh Renderer] Switching to 2D heatmap (FPS: {:.1f} < {:.0f})",
                         calculate_average_fps(renderer), BED_MESH_FPS_THRESHOLD);
        }
    }
    // Note: We don't auto-upgrade back to 3D (user must explicitly request via settings)
}

float bed_mesh_renderer_get_average_fps(bed_mesh_renderer_t* renderer) {
    if (!renderer)
        return 60.0f;
    return calculate_average_fps(renderer);
}

// ============================================================================
// Public API: Touch Handling for 2D Mode
// ============================================================================

bool bed_mesh_renderer_handle_touch(bed_mesh_renderer_t* renderer, int touch_x, int touch_y,
                                    int canvas_width, int canvas_height) {
    if (!renderer || !renderer->has_mesh_data)
        return false;

    // Only handle touch in 2D mode
    if (!bed_mesh_renderer_is_using_2d(renderer))
        return false;

    // Calculate grid dimensions (must match render_2d_heatmap)
    // N probe points = N-1 cells
    int padding = 8;
    int grid_width = canvas_width - 2 * padding;
    int grid_height = canvas_height - 2 * padding;
    int num_cells_x = renderer->cols - 1;
    int num_cells_y = renderer->rows - 1;

    // Guard against 1x1 mesh (no cells)
    if (num_cells_x <= 0 || num_cells_y <= 0) {
        renderer->touch_valid = false;
        return false;
    }

    int cell_w = grid_width / num_cells_x;
    int cell_h = grid_height / num_cells_y;
    int grid_x = padding + (grid_width - cell_w * num_cells_x) / 2;
    int grid_y = padding + (grid_height - cell_h * num_cells_y) / 2;

    // Convert touch to cell coordinates
    int col = (touch_x - grid_x) / cell_w;
    int row = (touch_y - grid_y) / cell_h;

    // Check bounds (N-1 cells)
    if (col < 0 || col >= num_cells_x || row < 0 || row >= num_cells_y) {
        renderer->touch_valid = false;
        return false;
    }

    // Store touched cell info
    // Cell (row, col) has its top-left corner at mesh point (row, col),
    // so cell indices directly map to mesh array indices for the corner Z value
    renderer->touched_row = row;
    renderer->touched_col = col;
    renderer->touched_z =
        static_cast<float>(renderer->mesh[static_cast<size_t>(row)][static_cast<size_t>(col)]);
    renderer->touch_valid = true;

    return true;
}

bool bed_mesh_renderer_get_touched_cell(bed_mesh_renderer_t* renderer, int* out_row, int* out_col,
                                        float* out_z) {
    if (!renderer || !renderer->touch_valid)
        return false;

    if (out_row)
        *out_row = renderer->touched_row;
    if (out_col)
        *out_col = renderer->touched_col;
    if (out_z)
        *out_z = renderer->touched_z;

    return true;
}

void bed_mesh_renderer_clear_touch(bed_mesh_renderer_t* renderer) {
    if (!renderer)
        return;
    renderer->touch_valid = false;
}
