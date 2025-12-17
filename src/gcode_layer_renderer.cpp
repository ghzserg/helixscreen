// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2025 HelixScreen Contributors

#include "gcode_layer_renderer.h"

#include "ui_theme.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace helix {
namespace gcode {

// ============================================================================
// Construction
// ============================================================================

GCodeLayerRenderer::GCodeLayerRenderer() {
    // Initialize default colors from theme
    reset_colors();
}

GCodeLayerRenderer::~GCodeLayerRenderer() {
    // Cancel background thread first (must complete before destroying buffers)
    cancel_background_ghost_render();

    destroy_cache();
    destroy_ghost_cache();
}

// ============================================================================
// Data Source
// ============================================================================

void GCodeLayerRenderer::set_gcode(const ParsedGCodeFile* gcode) {
    gcode_ = gcode;
    bounds_valid_ = false;
    current_layer_ = 0;
    invalidate_cache();

    if (gcode_) {
        spdlog::debug("[GCodeLayerRenderer] Set G-code: {} layers, {} total segments",
                      gcode_->layers.size(), gcode_->total_segments);
    }
}

// ============================================================================
// Layer Selection
// ============================================================================

void GCodeLayerRenderer::set_current_layer(int layer) {
    if (!gcode_) {
        current_layer_ = 0;
        return;
    }

    // Clamp to valid range
    int max_layer = static_cast<int>(gcode_->layers.size()) - 1;
    current_layer_ = std::clamp(layer, 0, std::max(0, max_layer));
}

int GCodeLayerRenderer::get_layer_count() const {
    return gcode_ ? static_cast<int>(gcode_->layers.size()) : 0;
}

// ============================================================================
// Canvas Setup
// ============================================================================

void GCodeLayerRenderer::set_canvas_size(int width, int height) {
    // Ensure minimum dimensions to prevent division by zero in auto_fit()
    canvas_width_ = std::max(1, width);
    canvas_height_ = std::max(1, height);
    bounds_valid_ = false; // Recalculate fit on next render
}

// ============================================================================
// Colors
// ============================================================================

void GCodeLayerRenderer::set_extrusion_color(lv_color_t color) {
    color_extrusion_ = color;
    use_custom_extrusion_color_ = true;
}

void GCodeLayerRenderer::set_travel_color(lv_color_t color) {
    color_travel_ = color;
    use_custom_travel_color_ = true;
}

void GCodeLayerRenderer::set_support_color(lv_color_t color) {
    color_support_ = color;
    use_custom_support_color_ = true;
}

void GCodeLayerRenderer::reset_colors() {
    // Use theme colors for default appearance
    // Extrusion: info blue for visibility against dark background
    color_extrusion_ = ui_theme_get_color("info_color");

    // Travel: subtle secondary color (grey)
    color_travel_ = ui_theme_get_color("text_secondary");

    // Support: orange/warning color to distinguish from model
    color_support_ = ui_theme_get_color("warning_color");

    use_custom_extrusion_color_ = false;
    use_custom_travel_color_ = false;
    use_custom_support_color_ = false;
}

// ============================================================================
// Viewport Control
// ============================================================================

void GCodeLayerRenderer::auto_fit() {
    if (!gcode_ || gcode_->layers.empty()) {
        scale_ = 1.0f;
        offset_x_ = 0.0f;
        offset_y_ = 0.0f;
        return;
    }

    // Use global bounding box for consistent framing
    const auto& bb = gcode_->global_bounding_box;

    float range_x, range_y;
    float center_x, center_y;

    switch (view_mode_) {
    case ViewMode::FRONT: {
        // Isometric-style: -45° horizontal + 30° elevation
        float xy_range_x = bb.max.x - bb.min.x;
        float xy_range_y = bb.max.y - bb.min.y;
        float z_range = bb.max.z - bb.min.z;

        // Horizontal extent after 45° rotation
        constexpr float COS_45 = 0.7071f;
        range_x = (xy_range_x + xy_range_y) * COS_45;

        // Vertical extent: Z * cos(30°) + Y_depth * sin(30°)
        constexpr float COS_30 = 0.866f;
        constexpr float SIN_30 = 0.5f;
        float y_depth = (xy_range_x + xy_range_y) * COS_45; // rotated Y range
        range_y = z_range * COS_30 + y_depth * SIN_30;

        center_x = (bb.min.x + bb.max.x) / 2.0f;
        center_y = (bb.min.y + bb.max.y) / 2.0f;
        offset_z_ = (bb.min.z + bb.max.z) / 2.0f;
        break;
    }

    case ViewMode::ISOMETRIC: {
        // Isometric: rotated X/Y view
        float xy_range_x = bb.max.x - bb.min.x;
        float xy_range_y = bb.max.y - bb.min.y;
        constexpr float ISO_ANGLE = 0.7071f;
        constexpr float ISO_Y_SCALE = 0.5f;
        range_x = (xy_range_x + xy_range_y) * ISO_ANGLE;
        range_y = (xy_range_x + xy_range_y) * ISO_ANGLE * ISO_Y_SCALE;
        center_x = (bb.min.x + bb.max.x) / 2.0f;
        center_y = (bb.min.y + bb.max.y) / 2.0f;
        break;
    }

    case ViewMode::TOP_DOWN:
    default:
        // Top-down: X/Y plane from above
        range_x = bb.max.x - bb.min.x;
        range_y = bb.max.y - bb.min.y;
        center_x = (bb.min.x + bb.max.x) / 2.0f;
        center_y = (bb.min.y + bb.max.y) / 2.0f;
        break;
    }

    // Handle degenerate cases
    if (range_x < 0.001f)
        range_x = 1.0f;
    if (range_y < 0.001f)
        range_y = 1.0f;

    // Add padding for visual breathing room
    constexpr float padding = 0.05f;
    range_x *= (1.0f + 2 * padding);
    range_y *= (1.0f + 2 * padding);

    // Scale to fit canvas (maintain aspect ratio)
    float scale_x = static_cast<float>(canvas_width_) / range_x;
    float scale_y = static_cast<float>(canvas_height_) / range_y;
    scale_ = std::min(scale_x, scale_y);

    // Store center for world_to_screen
    offset_x_ = center_x;
    offset_y_ = center_y;

    // Store bounds for reference (including Z for depth shading)
    bounds_min_x_ = bb.min.x;
    bounds_max_x_ = bb.max.x;
    bounds_min_y_ = bb.min.y;
    bounds_max_y_ = bb.max.y;
    bounds_min_z_ = bb.min.z;
    bounds_max_z_ = bb.max.z;

    bounds_valid_ = true;

    spdlog::debug("[GCodeLayerRenderer] auto_fit: mode={}, range=({:.1f},{:.1f}), scale={:.2f}",
                  static_cast<int>(view_mode_), range_x, range_y, scale_);
}

void GCodeLayerRenderer::fit_layer() {
    if (!gcode_ || gcode_->layers.empty()) {
        scale_ = 1.0f;
        offset_x_ = 0.0f;
        offset_y_ = 0.0f;
        return;
    }

    if (current_layer_ < 0 || current_layer_ >= static_cast<int>(gcode_->layers.size())) {
        return;
    }

    // Use current layer's bounding box
    const auto& bb = gcode_->layers[current_layer_].bounding_box;

    bounds_min_x_ = bb.min.x;
    bounds_max_x_ = bb.max.x;
    bounds_min_y_ = bb.min.y;
    bounds_max_y_ = bb.max.y;

    float range_x = bounds_max_x_ - bounds_min_x_;
    float range_y = bounds_max_y_ - bounds_min_y_;

    // Handle degenerate cases
    if (range_x < 0.001f)
        range_x = 1.0f;
    if (range_y < 0.001f)
        range_y = 1.0f;

    // Add padding
    constexpr float padding = 0.05f;
    range_x *= (1.0f + 2 * padding);
    range_y *= (1.0f + 2 * padding);

    // Scale to fit
    float scale_x = static_cast<float>(canvas_width_) / range_x;
    float scale_y = static_cast<float>(canvas_height_) / range_y;
    scale_ = std::min(scale_x, scale_y);

    // Center on layer
    offset_x_ = (bounds_min_x_ + bounds_max_x_) / 2.0f;
    offset_y_ = (bounds_min_y_ + bounds_max_y_) / 2.0f;

    bounds_valid_ = true;
}

void GCodeLayerRenderer::set_scale(float scale) {
    scale_ = std::max(0.001f, scale);
}

void GCodeLayerRenderer::set_offset(float x, float y) {
    offset_x_ = x;
    offset_y_ = y;
}

// ============================================================================
// Layer Information
// ============================================================================

GCodeLayerRenderer::LayerInfo GCodeLayerRenderer::get_layer_info() const {
    LayerInfo info{};
    info.layer_number = current_layer_;

    if (!gcode_ || gcode_->layers.empty()) {
        return info;
    }

    if (current_layer_ < 0 || current_layer_ >= static_cast<int>(gcode_->layers.size())) {
        return info;
    }

    const Layer& layer = gcode_->layers[current_layer_];
    info.z_height = layer.z_height;
    info.segment_count = layer.segments.size();
    info.extrusion_count = layer.segment_count_extrusion;
    info.travel_count = layer.segment_count_travel;

    // Check for support segments in this layer
    info.has_supports = false;
    for (const auto& seg : layer.segments) {
        if (is_support_segment(seg)) {
            info.has_supports = true;
            break;
        }
    }

    return info;
}

bool GCodeLayerRenderer::has_support_detection() const {
    // Support detection relies on object names from EXCLUDE_OBJECT
    // If there are named objects, we can potentially detect supports
    return gcode_ && !gcode_->objects.empty();
}

// ============================================================================
// Rendering
// ============================================================================

void GCodeLayerRenderer::destroy_cache() {
    if (cache_canvas_) {
        // Check if LVGL is still initialized and the object is valid
        // During shutdown, the widget tree may already be destroyed
        if (lv_is_initialized() && lv_obj_is_valid(cache_canvas_)) {
            lv_obj_delete(cache_canvas_);
        }
        cache_canvas_ = nullptr;
        // Note: canvas owns the draw_buf when attached, so don't double-free
        cache_buf_ = nullptr;
    } else if (cache_buf_) {
        // Buffer exists without canvas (shouldn't happen, but be safe)
        if (lv_is_initialized()) {
            lv_draw_buf_destroy(cache_buf_);
        }
        cache_buf_ = nullptr;
    }
    cached_up_to_layer_ = -1;
    cached_width_ = 0;
    cached_height_ = 0;
}

void GCodeLayerRenderer::invalidate_cache() {
    // Clear the cache buffer content but keep the canvas/buffer allocated
    if (cache_buf_) {
        lv_draw_buf_clear(cache_buf_, nullptr);
    }
    cached_up_to_layer_ = -1;

    // Cancel any in-progress background ghost rendering
    cancel_background_ghost_render();

    // Also invalidate ghost cache (new gcode = need new ghost)
    if (ghost_buf_) {
        lv_draw_buf_clear(ghost_buf_, nullptr);
    }
    ghost_cache_valid_ = false;
    ghost_rendered_up_to_ = -1;
}

void GCodeLayerRenderer::ensure_cache(int width, int height) {
    // Recreate cache if dimensions changed
    if (cache_buf_ && (cached_width_ != width || cached_height_ != height)) {
        destroy_cache();
    }

    if (!cache_buf_) {
        // Create the draw buffer
        cache_buf_ = lv_draw_buf_create(width, height, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
        if (!cache_buf_) {
            spdlog::error("[GCodeLayerRenderer] Failed to create cache buffer {}x{}", width,
                          height);
            return;
        }

        // Clear to transparent
        lv_draw_buf_clear(cache_buf_, nullptr);

        // Create a hidden canvas widget for offscreen rendering
        // We need a parent - use the top layer which always exists
        lv_obj_t* parent = lv_layer_top();
        if (!parent) {
            parent = lv_screen_active();
        }

        cache_canvas_ = lv_canvas_create(parent);
        if (!cache_canvas_) {
            spdlog::error("[GCodeLayerRenderer] Failed to create cache canvas");
            lv_draw_buf_destroy(cache_buf_);
            cache_buf_ = nullptr;
            return;
        }

        lv_canvas_set_draw_buf(cache_canvas_, cache_buf_);
        lv_obj_add_flag(cache_canvas_, LV_OBJ_FLAG_HIDDEN); // Keep it invisible

        cached_width_ = width;
        cached_height_ = height;
        cached_up_to_layer_ = -1;

        spdlog::debug("[GCodeLayerRenderer] Created cache canvas: {}x{}", width, height);
    }
}

void GCodeLayerRenderer::render_layers_to_cache(int from_layer, int to_layer) {
    if (!cache_canvas_ || !cache_buf_ || !gcode_)
        return;

    // Initialize layer for drawing to the canvas (LVGL 9.4 canvas API)
    lv_layer_t cache_layer;
    lv_canvas_init_layer(cache_canvas_, &cache_layer);

    // Temporarily set widget offset to 0 since we're rendering to cache origin
    int saved_offset_x = widget_offset_x_;
    int saved_offset_y = widget_offset_y_;
    widget_offset_x_ = 0;
    widget_offset_y_ = 0;

    size_t segments_rendered = 0;
    for (int layer_idx = from_layer; layer_idx <= to_layer; ++layer_idx) {
        if (layer_idx < 0 || layer_idx >= static_cast<int>(gcode_->layers.size()))
            continue;

        const Layer& layer_data = gcode_->layers[layer_idx];
        for (const auto& seg : layer_data.segments) {
            if (should_render_segment(seg)) {
                render_segment(&cache_layer, seg);
                ++segments_rendered;
            }
        }
    }

    // Finish the layer - flushes all pending draw tasks to the buffer
    lv_canvas_finish_layer(cache_canvas_, &cache_layer);

    widget_offset_x_ = saved_offset_x;
    widget_offset_y_ = saved_offset_y;

    spdlog::debug("[GCodeLayerRenderer] Rendered layers {}-{}: {} segments to cache", from_layer,
                  to_layer, segments_rendered);
}

void GCodeLayerRenderer::blit_cache(lv_layer_t* target) {
    if (!cache_buf_)
        return;

    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.src = cache_buf_;

    lv_area_t coords = {widget_offset_x_, widget_offset_y_, widget_offset_x_ + cached_width_ - 1,
                        widget_offset_y_ + cached_height_ - 1};

    lv_draw_image(target, &dsc, &coords);
}

// ============================================================================
// Ghost Cache (faded preview of all layers)
// ============================================================================

void GCodeLayerRenderer::destroy_ghost_cache() {
    if (ghost_canvas_) {
        if (lv_is_initialized() && lv_obj_is_valid(ghost_canvas_)) {
            lv_obj_delete(ghost_canvas_);
        }
        ghost_canvas_ = nullptr;
        ghost_buf_ = nullptr;
    } else if (ghost_buf_) {
        if (lv_is_initialized()) {
            lv_draw_buf_destroy(ghost_buf_);
        }
        ghost_buf_ = nullptr;
    }
    ghost_cache_valid_ = false;
    ghost_rendered_up_to_ = -1;
}

void GCodeLayerRenderer::ensure_ghost_cache(int width, int height) {
    // Recreate if dimensions changed
    if (ghost_buf_ && (cached_width_ != width || cached_height_ != height)) {
        destroy_ghost_cache();
    }

    if (!ghost_buf_) {
        ghost_buf_ = lv_draw_buf_create(width, height, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
        if (!ghost_buf_) {
            spdlog::error("[GCodeLayerRenderer] Failed to create ghost buffer {}x{}", width,
                          height);
            return;
        }

        lv_draw_buf_clear(ghost_buf_, nullptr);

        lv_obj_t* parent = lv_layer_top();
        if (!parent)
            parent = lv_screen_active();

        ghost_canvas_ = lv_canvas_create(parent);
        if (!ghost_canvas_) {
            lv_draw_buf_destroy(ghost_buf_);
            ghost_buf_ = nullptr;
            return;
        }

        lv_canvas_set_draw_buf(ghost_canvas_, ghost_buf_);
        lv_obj_add_flag(ghost_canvas_, LV_OBJ_FLAG_HIDDEN);

        ghost_cache_valid_ = false;
        spdlog::debug("[GCodeLayerRenderer] Created ghost cache canvas: {}x{}", width, height);
    }
}

void GCodeLayerRenderer::render_ghost_layers(int from_layer, int to_layer) {
    if (!ghost_canvas_ || !ghost_buf_ || !gcode_)
        return;

    lv_layer_t ghost_layer;
    lv_canvas_init_layer(ghost_canvas_, &ghost_layer);

    int saved_offset_x = widget_offset_x_;
    int saved_offset_y = widget_offset_y_;
    widget_offset_x_ = 0;
    widget_offset_y_ = 0;

    size_t segments_rendered = 0;
    for (int layer_idx = from_layer; layer_idx <= to_layer; ++layer_idx) {
        if (layer_idx < 0 || layer_idx >= static_cast<int>(gcode_->layers.size()))
            continue;

        const Layer& layer_data = gcode_->layers[layer_idx];
        for (const auto& seg : layer_data.segments) {
            if (should_render_segment(seg)) {
                // Render with reduced opacity for ghost effect
                render_segment(&ghost_layer, seg, true); // ghost=true
                ++segments_rendered;
            }
        }
    }

    lv_canvas_finish_layer(ghost_canvas_, &ghost_layer);

    widget_offset_x_ = saved_offset_x;
    widget_offset_y_ = saved_offset_y;

    spdlog::debug("[GCodeLayerRenderer] Rendered ghost layers {}-{}: {} segments", from_layer,
                  to_layer, segments_rendered);
}

void GCodeLayerRenderer::blit_ghost_cache(lv_layer_t* target) {
    if (!ghost_buf_)
        return;

    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.src = ghost_buf_;
    dsc.opa = LV_OPA_40; // 40% opacity for ghost

    lv_area_t coords = {widget_offset_x_, widget_offset_y_, widget_offset_x_ + cached_width_ - 1,
                        widget_offset_y_ + cached_height_ - 1};

    lv_draw_image(target, &dsc, &coords);
}

void GCodeLayerRenderer::render(lv_layer_t* layer, const lv_area_t* widget_area) {
    if (!gcode_ || gcode_->layers.empty()) {
        spdlog::debug("[GCodeLayerRenderer] render(): no gcode data");
        return;
    }

    if (current_layer_ < 0 || current_layer_ >= static_cast<int>(gcode_->layers.size())) {
        spdlog::debug("[GCodeLayerRenderer] render(): layer out of range ({} / {})", current_layer_,
                      gcode_->layers.size());
        return;
    }

    uint32_t start_time = lv_tick_get();

    // Store widget screen offset for world_to_screen()
    if (widget_area) {
        widget_offset_x_ = widget_area->x1;
        widget_offset_y_ = widget_area->y1;
    }

    // Auto-fit if bounds not yet computed
    if (!bounds_valid_) {
        auto_fit();
    }

    size_t segments_rendered = 0;

    // For FRONT view, use incremental cache with progressive rendering
    if (view_mode_ == ViewMode::FRONT) {
        int target_layer = std::min(current_layer_, static_cast<int>(gcode_->layers.size()) - 1);

        // Ensure cache buffers exist and are correct size
        ensure_cache(canvas_width_, canvas_height_);
        if (ghost_mode_enabled_) {
            ensure_ghost_cache(canvas_width_, canvas_height_);
        }

        // =====================================================================
        // GHOST CACHE: Background thread rendering (non-blocking)
        // Renders ALL layers in a background thread using software Bresenham,
        // then copies to LVGL buffer when complete.
        // =====================================================================
        if (ghost_mode_enabled_ && ghost_buf_ && !ghost_cache_valid_) {
            // Check if background thread has completed
            if (ghost_thread_ready_.load()) {
                copy_raw_to_ghost_buf();
            }
            // Start background render if not already running
            else if (!ghost_thread_running_.load()) {
                start_background_ghost_render();
            }
            // else: background thread is running, wait for it
        }

        // =====================================================================
        // SOLID CACHE: Progressive rendering up to current print layer
        // =====================================================================
        if (cache_buf_ && cache_canvas_) {
            // Check if we need to render new layers
            if (target_layer > cached_up_to_layer_) {
                // Progressive rendering: only render up to LAYERS_PER_FRAME at a time
                // This prevents UI freezing during initial load or big jumps
                int from_layer = cached_up_to_layer_ + 1;
                int to_layer = std::min(from_layer + LAYERS_PER_FRAME - 1, target_layer);

                render_layers_to_cache(from_layer, to_layer);
                cached_up_to_layer_ = to_layer;

                // If we haven't caught up yet, caller should check needs_more_frames()
                // and invalidate the widget to trigger another frame
                if (cached_up_to_layer_ < target_layer) {
                    spdlog::debug(
                        "[GCodeLayerRenderer] Progressive: rendered to layer {}/{}, more needed",
                        cached_up_to_layer_, target_layer);
                }
            } else if (target_layer < cached_up_to_layer_) {
                // Going backwards - need to re-render from scratch (progressively)
                lv_draw_buf_clear(cache_buf_, nullptr);
                cached_up_to_layer_ = -1;

                int to_layer = std::min(LAYERS_PER_FRAME - 1, target_layer);
                render_layers_to_cache(0, to_layer);
                cached_up_to_layer_ = to_layer;
                // Caller checks needs_more_frames() for continuation
            }
            // else: same layer, just blit cached image

            // =====================================================================
            // BLIT: Ghost first (underneath), then solid on top
            // =====================================================================
            if (ghost_mode_enabled_ && ghost_buf_) {
                blit_ghost_cache(layer);
            }
            blit_cache(layer);
            segments_rendered = last_segment_count_;
        }
    } else {
        // TOP_DOWN or ISOMETRIC: render single layer directly (no caching needed)
        const auto& layer_bb = gcode_->layers[current_layer_].bounding_box;
        offset_x_ = (layer_bb.min.x + layer_bb.max.x) / 2.0f;
        offset_y_ = (layer_bb.min.y + layer_bb.max.y) / 2.0f;

        const Layer& current = gcode_->layers[current_layer_];
        for (const auto& seg : current.segments) {
            if (!should_render_segment(seg))
                continue;
            render_segment(layer, seg);
            ++segments_rendered;
        }
    }

    // Track render time for diagnostics
    last_render_time_ms_ = lv_tick_get() - start_time;
    last_segment_count_ = segments_rendered;

    // Log performance if layer changed or slow render
    if (current_layer_ != last_rendered_layer_ || last_render_time_ms_ > 50) {
        spdlog::debug("[GCodeLayerRenderer] Layer {}: {}ms (cached_up_to={})", current_layer_,
                      last_render_time_ms_, cached_up_to_layer_);
        last_rendered_layer_ = current_layer_;
    }
}

bool GCodeLayerRenderer::needs_more_frames() const {
    if (!gcode_ || gcode_->layers.empty()) {
        return false;
    }

    // Only relevant for FRONT view mode (uses caching)
    if (view_mode_ != ViewMode::FRONT) {
        return false;
    }

    int target_layer = std::min(current_layer_, static_cast<int>(gcode_->layers.size()) - 1);

    // Solid cache incomplete?
    if (cached_up_to_layer_ < target_layer) {
        return true;
    }

    // Ghost rendering in background thread?
    // Keep triggering frames until background thread completes so we can
    // copy the result to the LVGL buffer
    if (ghost_mode_enabled_ && !ghost_cache_valid_) {
        if (ghost_thread_running_.load() || ghost_thread_ready_.load()) {
            return true;
        }
    }

    return false;
}

bool GCodeLayerRenderer::should_render_segment(const ToolpathSegment& seg) const {
    if (seg.is_extrusion) {
        if (is_support_segment(seg)) {
            return show_supports_;
        }
        return show_extrusions_;
    }
    return show_travels_;
}

void GCodeLayerRenderer::render_segment(lv_layer_t* layer, const ToolpathSegment& seg, bool ghost) {
    // Convert world coordinates to screen (uses Z for FRONT view)
    glm::ivec2 p1 = world_to_screen(seg.start.x, seg.start.y, seg.start.z);
    glm::ivec2 p2 = world_to_screen(seg.end.x, seg.end.y, seg.end.z);

    // Skip zero-length segments
    if (p1.x == p2.x && p1.y == p2.y) {
        return;
    }

    // Initialize line drawing descriptor
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);

    lv_color_t base_color;
    if (ghost) {
        // Ghost mode: use darkened version of the model's extrusion color
        // This provides visual continuity between ghost and solid layers
        lv_color_t model_color = color_extrusion_;
        // Darken to 40% brightness for ghost effect
        base_color = lv_color_make(model_color.red * 40 / 100, model_color.green * 40 / 100,
                                   model_color.blue * 40 / 100);
    } else {
        base_color = get_segment_color(seg);
    }

    // Apply depth shading for 3D-like appearance
    if (depth_shading_ && view_mode_ == ViewMode::FRONT) {
        // Calculate brightness factor based on Z position
        // Bottom of model = darker (40%), top = brighter (100%)
        float z_range = bounds_max_z_ - bounds_min_z_;
        float avg_z = (seg.start.z + seg.end.z) / 2.0f;

        float brightness = 0.4f; // Minimum brightness
        if (z_range > 0.001f) {
            float normalized_z = (avg_z - bounds_min_z_) / z_range;
            brightness = 0.4f + 0.6f * normalized_z; // 40% to 100%
        }

        // Also add subtle Y-depth fade (back of model slightly darker)
        // In 45° view, higher Y values are further back
        float y_range = bounds_max_y_ - bounds_min_y_;
        float avg_y = (seg.start.y + seg.end.y) / 2.0f;
        if (y_range > 0.001f) {
            float normalized_y = (avg_y - bounds_min_y_) / y_range;
            // Front (low Y) = 100%, back (high Y) = 85%
            float depth_fade = 0.85f + 0.15f * (1.0f - normalized_y);
            brightness *= depth_fade;
        }

        // Apply brightness to color (scale RGB channels)
        // LVGL 9: lv_color_t has direct .red, .green, .blue members
        uint8_t r = static_cast<uint8_t>(base_color.red * brightness);
        uint8_t g = static_cast<uint8_t>(base_color.green * brightness);
        uint8_t b = static_cast<uint8_t>(base_color.blue * brightness);
        dsc.color = lv_color_make(r, g, b);
    } else {
        dsc.color = base_color;
    }

    // Extrusion moves: thicker, fully opaque
    // Travel moves: thinner, semi-transparent
    if (seg.is_extrusion) {
        dsc.width = 2;
        dsc.opa = LV_OPA_COVER;
    } else {
        dsc.width = 1;
        dsc.opa = LV_OPA_50;
    }

    // LVGL 9: points are stored in the descriptor struct
    dsc.p1.x = static_cast<lv_value_precise_t>(p1.x);
    dsc.p1.y = static_cast<lv_value_precise_t>(p1.y);
    dsc.p2.x = static_cast<lv_value_precise_t>(p2.x);
    dsc.p2.y = static_cast<lv_value_precise_t>(p2.y);

    lv_draw_line(layer, &dsc);
}

glm::ivec2 GCodeLayerRenderer::world_to_screen(float x, float y, float z) const {
    float sx, sy;

    switch (view_mode_) {
    case ViewMode::FRONT: {
        // Isometric-style view: 45° horizontal rotation + 30° elevation
        // This creates a "corner view looking down" perspective
        //
        // First apply 90° CCW rotation around Z to match thumbnail orientation
        // (thumbnails show models from a different default angle)
        float raw_dx = x - offset_x_;
        float raw_dy = y - offset_y_;
        float dx = -raw_dy; // 90° CCW: new_x = -old_y
        float dy = raw_dx;  // 90° CCW: new_y = old_x
        float dz = z - offset_z_;

        // Horizontal rotation: -45° (negative = view from front-right corner)
        // sin(-45°) = -0.7071, cos(-45°) = 0.7071
        constexpr float COS_H = 0.7071f;  // cos(45°)
        constexpr float SIN_H = -0.7071f; // sin(-45°) - negative for other corner

        // Elevation angle: 30° looking down
        // sin(30°) = 0.5, cos(30°) = 0.866
        constexpr float COS_E = 0.866f; // cos(30°)
        constexpr float SIN_E = 0.5f;   // sin(30°)

        // Apply horizontal rotation first (around Z axis)
        float rx = dx * COS_H - dy * SIN_H;
        float ry = dx * SIN_H + dy * COS_H;

        // Then apply elevation (tilt camera down)
        // Screen X = rotated X
        // Screen Y = -Z * cos(elev) + rotated_Y * sin(elev)
        sx = rx * scale_ + static_cast<float>(canvas_width_) / 2.0f;
        sy = static_cast<float>(canvas_height_) / 2.0f - (dz * COS_E + ry * SIN_E) * scale_;
        break;
    }

    case ViewMode::ISOMETRIC: {
        // Isometric projection (45° rotation with Y compression)
        float dx = x - offset_x_;
        float dy = y - offset_y_;
        constexpr float ISO_ANGLE = 0.7071f;
        constexpr float ISO_Y_SCALE = 0.5f;

        float iso_x = (dx - dy) * ISO_ANGLE;
        float iso_y = (dx + dy) * ISO_ANGLE * ISO_Y_SCALE;

        sx = iso_x * scale_ + static_cast<float>(canvas_width_) / 2.0f;
        sy = static_cast<float>(canvas_height_) / 2.0f - iso_y * scale_;
        break;
    }

    case ViewMode::TOP_DOWN:
    default: {
        // Top-down: X → screen X, Y → screen Y (flipped)
        float dx = x - offset_x_;
        float dy = y - offset_y_;
        sx = dx * scale_ + static_cast<float>(canvas_width_) / 2.0f;
        sy = static_cast<float>(canvas_height_) / 2.0f - dy * scale_;
        break;
    }
    }

    // Add widget's screen offset (drawing is in screen coordinates)
    return {static_cast<int>(sx) + widget_offset_x_, static_cast<int>(sy) + widget_offset_y_};
}

bool GCodeLayerRenderer::is_support_segment(const ToolpathSegment& seg) const {
    // Support detection via object name (from EXCLUDE_OBJECT metadata)
    if (seg.object_name.empty()) {
        return false;
    }

    // Common patterns used by slicers for support structures:
    // - OrcaSlicer/PrusaSlicer: "support_*", "*_support", "SUPPORT_*"
    // - Cura: "support", "Support"
    const std::string& name = seg.object_name;

    // Case-insensitive check for "support" anywhere in the name
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return lower_name.find("support") != std::string::npos;
}

lv_color_t GCodeLayerRenderer::get_segment_color(const ToolpathSegment& seg) const {
    if (!seg.is_extrusion) {
        // Travel move
        return color_travel_;
    }

    // Check if this is a support segment
    if (is_support_segment(seg)) {
        return color_support_;
    }

    // Regular extrusion
    return color_extrusion_;
}

// ============================================================================
// Background Thread Ghost Rendering
// ============================================================================
// LVGL drawing APIs are not thread-safe. To avoid blocking the UI during
// ghost cache generation, we render to a raw pixel buffer in a background
// thread using software Bresenham line drawing, then copy to the LVGL
// draw buffer on the main thread when complete.

void GCodeLayerRenderer::start_background_ghost_render() {
    // Cancel any existing render first
    cancel_background_ghost_render();

    if (!gcode_ || gcode_->layers.empty()) {
        return;
    }

    // Allocate raw buffer if dimensions changed or not allocated
    int width = canvas_width_;
    int height = canvas_height_;
    size_t stride = width * 4; // ARGB8888 = 4 bytes per pixel
    size_t buffer_size = stride * height;

    if (ghost_raw_width_ != width || ghost_raw_height_ != height || !ghost_raw_buffer_) {
        ghost_raw_buffer_ = std::make_unique<uint8_t[]>(buffer_size);
        ghost_raw_width_ = width;
        ghost_raw_height_ = height;
        ghost_raw_stride_ = static_cast<int>(stride);
    }

    // Clear buffer to transparent black (ARGB = 0x00000000)
    std::memset(ghost_raw_buffer_.get(), 0, buffer_size);

    // Reset flags
    ghost_thread_cancel_.store(false);
    ghost_thread_ready_.store(false);
    ghost_thread_running_.store(true);

    // Launch background thread
    ghost_thread_ = std::thread(&GCodeLayerRenderer::background_ghost_render_thread, this);

    spdlog::info("[GCodeLayerRenderer] Started background ghost render thread ({}x{})", width,
                 height);
}

void GCodeLayerRenderer::cancel_background_ghost_render() {
    // Always signal cancellation and join if the thread is joinable.
    // This handles the case where the thread completed naturally but wasn't
    // joined yet - we MUST join before assigning a new thread or std::terminate() is called.
    ghost_thread_cancel_.store(true);
    if (ghost_thread_.joinable()) {
        ghost_thread_.join();
    }
    ghost_thread_running_.store(false);
    ghost_thread_cancel_.store(false); // Reset for next run
}

void GCodeLayerRenderer::background_ghost_render_thread() {
    if (!gcode_ || !ghost_raw_buffer_) {
        ghost_thread_running_.store(false);
        return;
    }

    // Use std::chrono for timing - lv_tick_get() is not thread-safe
    auto start_time = std::chrono::steady_clock::now();
    size_t segments_rendered = 0;
    int total_layers = static_cast<int>(gcode_->layers.size());

    // =========================================================================
    // THREAD SAFETY: Capture ALL shared state at thread start
    // These values may be modified by the main thread during rendering, so we
    // take a snapshot to ensure consistent rendering throughout.
    // =========================================================================

    // Transformation parameters
    const float local_scale = scale_;
    const float local_offset_x = offset_x_;
    const float local_offset_y = offset_y_;
    const float local_offset_z = offset_z_;
    const int local_width = ghost_raw_width_;
    const int local_height = ghost_raw_height_;
    const ViewMode local_view_mode = view_mode_;

    // Visibility flags (can be changed via set_show_*() on main thread)
    const bool local_show_travels = show_travels_;
    const bool local_show_extrusions = show_extrusions_;
    const bool local_show_supports = show_supports_;

    // Color (can be changed via set_extrusion_color() on main thread)
    const lv_color_t local_color_extrusion = color_extrusion_;

    // Local version of should_render_segment using captured flags
    auto local_should_render = [&](const ToolpathSegment& seg) -> bool {
        if (seg.is_extrusion) {
            if (is_support_segment(seg)) // Only reads seg fields, safe
                return local_show_supports;
            return local_show_extrusions;
        }
        return local_show_travels;
    };

    // Lambda for world_to_screen without widget offset (rendering to origin 0,0)
    auto local_world_to_screen = [&](float x, float y, float z) -> glm::ivec2 {
        float sx, sy;

        switch (local_view_mode) {
        case ViewMode::FRONT: {
            // Isometric-style view (same math as world_to_screen)
            float raw_dx = x - local_offset_x;
            float raw_dy = y - local_offset_y;
            float dx = -raw_dy;
            float dy = raw_dx;
            float dz = z - local_offset_z;

            constexpr float COS_H = 0.7071f;
            constexpr float SIN_H = -0.7071f;
            constexpr float COS_E = 0.866f;
            constexpr float SIN_E = 0.5f;

            float rx = dx * COS_H - dy * SIN_H;
            float ry = dx * SIN_H + dy * COS_H;

            sx = rx * local_scale + static_cast<float>(local_width) / 2.0f;
            sy = static_cast<float>(local_height) / 2.0f - (dz * COS_E + ry * SIN_E) * local_scale;
            break;
        }
        case ViewMode::ISOMETRIC: {
            float dx = x - local_offset_x;
            float dy = y - local_offset_y;
            constexpr float ISO_ANGLE = 0.7071f;
            constexpr float ISO_Y_SCALE = 0.5f;
            float iso_x = (dx - dy) * ISO_ANGLE;
            float iso_y = (dx + dy) * ISO_ANGLE * ISO_Y_SCALE;
            sx = iso_x * local_scale + static_cast<float>(local_width) / 2.0f;
            sy = static_cast<float>(local_height) / 2.0f - iso_y * local_scale;
            break;
        }
        case ViewMode::TOP_DOWN:
        default: {
            float dx = x - local_offset_x;
            float dy = y - local_offset_y;
            sx = dx * local_scale + static_cast<float>(local_width) / 2.0f;
            sy = static_cast<float>(local_height) / 2.0f - dy * local_scale;
            break;
        }
        }
        return {static_cast<int>(sx), static_cast<int>(sy)};
    };

    // Compute ghost color once (darkened extrusion color from captured value)
    // ARGB8888: A in high byte, R, G, B in lower bytes
    uint8_t ghost_r = local_color_extrusion.red * 40 / 100;
    uint8_t ghost_g = local_color_extrusion.green * 40 / 100;
    uint8_t ghost_b = local_color_extrusion.blue * 40 / 100;
    uint8_t ghost_a = 255; // Full alpha, we'll apply 40% when blitting
    uint32_t ghost_color = (ghost_a << 24) | (ghost_r << 16) | (ghost_g << 8) | ghost_b;

    // Render all layers to raw buffer
    for (int layer_idx = 0; layer_idx < total_layers; ++layer_idx) {
        // Check for cancellation periodically
        if (ghost_thread_cancel_.load()) {
            spdlog::debug("[GCodeLayerRenderer] Ghost render cancelled at layer {}/{}", layer_idx,
                          total_layers);
            ghost_thread_running_.store(false);
            return;
        }

        const Layer& layer_data = gcode_->layers[layer_idx];
        for (const auto& seg : layer_data.segments) {
            if (!local_should_render(seg))
                continue;

            // Convert world coordinates to screen (no widget offset - rendering to origin)
            glm::ivec2 p1 = local_world_to_screen(seg.start.x, seg.start.y, seg.start.z);
            glm::ivec2 p2 = local_world_to_screen(seg.end.x, seg.end.y, seg.end.z);

            // Skip zero-length segments
            if (p1.x == p2.x && p1.y == p2.y)
                continue;

            // Draw line using Bresenham algorithm
            draw_line_bresenham(p1.x, p1.y, p2.x, p2.y, ghost_color);
            ++segments_rendered;
        }
    }

    // Mark as ready for main thread to copy
    ghost_thread_ready_.store(true);
    ghost_thread_running_.store(false);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start_time)
                       .count();
    spdlog::info(
        "[GCodeLayerRenderer] Background ghost render complete: {} layers, {} segments in {}ms",
        total_layers, segments_rendered, elapsed);
}

void GCodeLayerRenderer::copy_raw_to_ghost_buf() {
    if (!ghost_thread_ready_.load() || !ghost_raw_buffer_ || !ghost_buf_) {
        return;
    }

    // Validate dimensions match - if canvas was resized during background render,
    // the raw buffer dimensions won't match the LVGL buffer
    // LVGL 9: dimensions are in the header struct
    uint32_t lvgl_width = ghost_buf_->header.w;
    uint32_t lvgl_height = ghost_buf_->header.h;
    if (ghost_raw_width_ != static_cast<int>(lvgl_width) ||
        ghost_raw_height_ != static_cast<int>(lvgl_height)) {
        spdlog::warn("[GCodeLayerRenderer] Ghost buffer dimension mismatch (raw {}x{} vs LVGL "
                     "{}x{}), discarding",
                     ghost_raw_width_, ghost_raw_height_, lvgl_width, lvgl_height);
        ghost_thread_ready_.store(false);
        return;
    }

    // Get LVGL buffer stride (may differ from our raw stride due to alignment)
    // LVGL 9: stride is in the header struct
    uint32_t lvgl_stride = ghost_buf_->header.stride;

    if (static_cast<int>(lvgl_stride) == ghost_raw_stride_) {
        // Fast path: strides match, single memcpy
        size_t buffer_size = ghost_raw_stride_ * ghost_raw_height_;
        std::memcpy(ghost_buf_->data, ghost_raw_buffer_.get(), buffer_size);
    } else {
        // Slow path: strides differ, copy row by row
        spdlog::debug("[GCodeLayerRenderer] Stride mismatch (raw {} vs LVGL {}), row-by-row copy",
                      ghost_raw_stride_, lvgl_stride);
        for (int y = 0; y < ghost_raw_height_; ++y) {
            std::memcpy(static_cast<uint8_t*>(ghost_buf_->data) + y * lvgl_stride,
                        ghost_raw_buffer_.get() + y * ghost_raw_stride_,
                        ghost_raw_width_ * 4); // Copy only actual pixel data (4 bytes per pixel)
        }
    }

    ghost_cache_valid_ = true;
    ghost_thread_ready_.store(false); // Consumed

    spdlog::debug("[GCodeLayerRenderer] Copied raw ghost buffer to LVGL ({}x{})", ghost_raw_width_,
                  ghost_raw_height_);
}

void GCodeLayerRenderer::blend_pixel(int x, int y, uint32_t color) {
    // Bounds check
    if (x < 0 || x >= ghost_raw_width_ || y < 0 || y >= ghost_raw_height_) {
        return;
    }

    // Calculate pixel offset (ARGB8888 = 4 bytes per pixel)
    uint8_t* pixel = ghost_raw_buffer_.get() + y * ghost_raw_stride_ + x * 4;

    // Simple overwrite for now (could add alpha blending later)
    // LVGL uses ARGB8888: byte order is B, G, R, A on little-endian
    pixel[0] = color & 0xFF;         // B
    pixel[1] = (color >> 8) & 0xFF;  // G
    pixel[2] = (color >> 16) & 0xFF; // R
    pixel[3] = (color >> 24) & 0xFF; // A
}

void GCodeLayerRenderer::draw_line_bresenham(int x0, int y0, int x1, int y1, uint32_t color) {
    // Bresenham's line algorithm for software line drawing
    // This runs in the background thread where LVGL APIs are not available

    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        blend_pixel(x0, y0, color);

        if (x0 == x1 && y0 == y1)
            break;

        int e2 = 2 * err;
        if (e2 >= dy) {
            if (x0 == x1)
                break;
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            if (y0 == y1)
                break;
            err += dx;
            y0 += sy;
        }
    }
}

} // namespace gcode
} // namespace helix
