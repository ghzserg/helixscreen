// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_layer_renderer.h"

#include "config.h"
#include "gcode_parser.h"
#include "memory_monitor.h"
#include "memory_utils.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace helix {
namespace gcode {

// ============================================================================
// Construction
// ============================================================================

GCodeLayerRenderer::GCodeLayerRenderer() {
    // Initialize default colors from theme
    reset_colors();

    // Load configuration values
    load_config();
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
    streaming_controller_ = nullptr; // Clear streaming mode
    bounds_valid_ = false;
    current_layer_ = 0;
    warmup_frames_remaining_ = WARMUP_FRAMES; // Allow panel to render before heavy caching
    invalidate_cache();

    if (gcode_) {
        spdlog::debug("[GCodeLayerRenderer] Set G-code: {} layers, {} total segments",
                      gcode_->layers.size(), gcode_->total_segments);
    }
}

void GCodeLayerRenderer::set_streaming_controller(GCodeStreamingController* controller) {
    streaming_controller_ = controller;
    gcode_ = nullptr; // Clear full-file mode
    bounds_valid_ = false;
    current_layer_ = 0;
    warmup_frames_remaining_ = WARMUP_FRAMES; // Allow panel to render before heavy caching
    invalidate_cache();

    if (streaming_controller_) {
        spdlog::info(
            "[GCodeLayerRenderer] Set streaming controller: {} layers, cache budget {:.1f}MB",
            streaming_controller_->get_layer_count(),
            static_cast<double>(streaming_controller_->get_cache_budget()) / (1024 * 1024));
    }
}

// ============================================================================
// Layer Selection
// ============================================================================

void GCodeLayerRenderer::set_current_layer(int layer) {
    int max_layer = get_layer_count() - 1;
    if (max_layer < 0) {
        current_layer_ = 0;
        return;
    }

    current_layer_ = std::clamp(layer, 0, max_layer);
}

int GCodeLayerRenderer::get_layer_count() const {
    if (streaming_controller_) {
        return static_cast<int>(streaming_controller_->get_layer_count());
    }
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

void GCodeLayerRenderer::set_content_offset_y(float offset_percent) {
    // Clamp to reasonable range
    content_offset_y_percent_ = std::clamp(offset_percent, -1.0f, 1.0f);
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
    color_extrusion_ = theme_manager_get_color("info");

    // Travel: subtle secondary color (grey)
    color_travel_ = theme_manager_get_color("text_muted");

    // Support: orange/warning color to distinguish from model
    color_support_ = theme_manager_get_color("warning");

    use_custom_extrusion_color_ = false;
    use_custom_travel_color_ = false;
    use_custom_support_color_ = false;
}

void GCodeLayerRenderer::set_excluded_objects(const std::unordered_set<std::string>& names) {
    excluded_objects_ = names;
    invalidate_cache();
}

void GCodeLayerRenderer::set_highlighted_objects(const std::unordered_set<std::string>& names) {
    if (names != highlighted_objects_) {
        if (names.empty()) {
            spdlog::debug("[GCodeLayerRenderer] Selection cleared");
        } else {
            for (const auto& name : names) {
                spdlog::debug("[GCodeLayerRenderer] Selection brackets active for '{}'", name);
            }
        }
    }
    highlighted_objects_ = names;
    invalidate_cache();
}

// ============================================================================
// Viewport Control
// ============================================================================

void GCodeLayerRenderer::auto_fit() {
    int layer_count = get_layer_count();
    if (layer_count == 0) {
        scale_ = 1.0f;
        offset_x_ = 0.0f;
        offset_y_ = 0.0f;
        return;
    }

    // Get bounding box from either full file or streaming index stats
    AABB bb;
    if (streaming_controller_) {
        // In streaming mode, compute actual X/Y bounds from layer data
        // The index stats only have Z bounds, so we sample layers for X/Y
        const auto& stats = streaming_controller_->get_index_stats();

        // Initialize with Z bounds from index
        bb.min = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                  stats.min_z};
        bb.max = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                  stats.max_z};

        // Sample a few layers to compute X/Y bounds (first, middle, last)
        size_t layer_count = streaming_controller_->get_layer_count();
        std::vector<size_t> sample_layers;
        if (layer_count > 0) {
            sample_layers.push_back(0); // First layer
            if (layer_count > 2) {
                sample_layers.push_back(layer_count / 2); // Middle layer
            }
            if (layer_count > 1) {
                sample_layers.push_back(layer_count - 1); // Last layer
            }
        }

        bool found_bounds = false;
        for (size_t layer_idx : sample_layers) {
            auto segments = streaming_controller_->get_layer_segments(layer_idx);
            if (segments && !segments->empty()) {
                for (const auto& seg : *segments) {
                    bb.min.x = std::min(bb.min.x, std::min(seg.start.x, seg.end.x));
                    bb.max.x = std::max(bb.max.x, std::max(seg.start.x, seg.end.x));
                    bb.min.y = std::min(bb.min.y, std::min(seg.start.y, seg.end.y));
                    bb.max.y = std::max(bb.max.y, std::max(seg.start.y, seg.end.y));
                    found_bounds = true;
                }
            }
        }

        // Fallback to 200x200 if no layer data available yet
        if (!found_bounds) {
            bb.min.x = 0.0f;
            bb.min.y = 0.0f;
            bb.max.x = 200.0f;
            bb.max.y = 200.0f;
            spdlog::debug(
                "[GCodeLayerRenderer] Streaming: no layers loaded yet, using default 200x200");
        } else {
            spdlog::info("[GCodeLayerRenderer] Streaming: computed bounds X[{:.1f},{:.1f}] "
                         "Y[{:.1f},{:.1f}] from {} layers",
                         bb.min.x, bb.max.x, bb.min.y, bb.max.y, sample_layers.size());
        }
    } else if (gcode_) {
        bb = gcode_->global_bounding_box;
    } else {
        return;
    }

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

    spdlog::debug("[GCodeLayerRenderer] auto_fit: canvas={}x{}, mode={}, range=({:.1f},{:.1f}), "
                  "scale={:.2f}, center=({:.1f},{:.1f},{:.1f})",
                  canvas_width_, canvas_height_, static_cast<int>(view_mode_), range_x, range_y,
                  scale_, offset_x_, offset_y_, offset_z_);
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

    int layer_count = get_layer_count();
    if (layer_count == 0) {
        return info;
    }

    if (current_layer_ < 0 || current_layer_ >= layer_count) {
        return info;
    }

    if (streaming_controller_) {
        // Streaming mode: get Z height from controller, segments on demand
        info.z_height = streaming_controller_->get_layer_z(static_cast<size_t>(current_layer_));

        // Get segments to compute counts (this will cache the layer)
        // Use shared_ptr to keep data alive during iteration
        auto segments =
            streaming_controller_->get_layer_segments(static_cast<size_t>(current_layer_));
        if (segments) {
            info.segment_count = segments->size();
            info.extrusion_count = 0;
            info.travel_count = 0;
            info.has_supports = false;

            for (const auto& seg : *segments) {
                if (seg.is_extrusion) {
                    ++info.extrusion_count;
                    if (!info.has_supports && is_support_segment(seg)) {
                        info.has_supports = true;
                    }
                } else {
                    ++info.travel_count;
                }
            }
        }
    } else if (gcode_) {
        // Full file mode
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
    }

    return info;
}

bool GCodeLayerRenderer::has_support_detection() const {
    // Support detection relies on object names from EXCLUDE_OBJECT
    // If there are named objects, we can potentially detect supports
    // Note: Streaming mode doesn't have full object metadata, so return false
    if (streaming_controller_) {
        return false; // Object metadata not available in streaming mode
    }
    return gcode_ && !gcode_->objects.empty();
}

// ============================================================================
// Rendering
// ============================================================================

void GCodeLayerRenderer::destroy_cache() {
    if (cache_buf_) {
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
    // Clear the cache buffer content but keep the buffer allocated
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
        // Create the draw buffer (no canvas widget - avoids clip area contamination
        // from overlays/toasts on lv_layer_top())
        cache_buf_ = lv_draw_buf_create(width, height, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
        if (!cache_buf_) {
            spdlog::error("[GCodeLayerRenderer] Failed to create cache buffer {}x{}", width,
                          height);
            return;
        }

        // Clear to transparent
        lv_draw_buf_clear(cache_buf_, nullptr);

        cached_width_ = width;
        cached_height_ = height;
        cached_up_to_layer_ = -1;

        spdlog::debug("[GCodeLayerRenderer] Created cache buffer: {}x{}", width, height);
        helix::MemoryMonitor::log_now("gcode_cache_buffer_created");
    }
}

void GCodeLayerRenderer::render_layers_to_cache(int from_layer, int to_layer) {
    if (!cache_buf_)
        return;

    // Need either gcode file or streaming controller
    if (!gcode_ && !streaming_controller_)
        return;

    // Capture transform params for coordinate conversion
    // This ensures consistent rendering with widget offset set to 0 for cache
    TransformParams transform = capture_transform_params();
    transform.canvas_width = cached_width_;
    transform.canvas_height = cached_height_;

    int layer_count = get_layer_count();
    size_t segments_rendered = 0;

    // Compute base color once (full filament color with full alpha)
    uint8_t base_r = color_extrusion_.red;
    uint8_t base_g = color_extrusion_.green;
    uint8_t base_b = color_extrusion_.blue;

    for (int layer_idx = from_layer; layer_idx <= to_layer; ++layer_idx) {
        if (layer_idx < 0 || layer_idx >= layer_count)
            continue;

        // Get segments from appropriate source
        // For streaming mode, hold shared_ptr to keep data alive during iteration
        std::shared_ptr<const std::vector<ToolpathSegment>> segments_holder;
        const std::vector<ToolpathSegment>* segments = nullptr;

        if (streaming_controller_) {
            // Streaming mode: get segments from controller (returns shared_ptr)
            segments_holder =
                streaming_controller_->get_layer_segments(static_cast<size_t>(layer_idx));
            segments = segments_holder.get();
        } else if (gcode_) {
            // Full file mode: get segments from parsed file
            segments = &gcode_->layers[layer_idx].segments;
        }

        if (!segments)
            continue;

        for (const auto& seg : *segments) {
            if (!should_render_segment(seg))
                continue;

            // Skip non-extrusion moves for solid rendering (travels are subtle)
            if (!seg.is_extrusion)
                continue;

            // Convert world coordinates to screen using cached transform
            glm::ivec2 p1 = world_to_screen_raw(transform, seg.start.x, seg.start.y, seg.start.z);
            glm::ivec2 p2 = world_to_screen_raw(transform, seg.end.x, seg.end.y, seg.end.z);

            // Skip zero-length segments
            if (p1.x == p2.x && p1.y == p2.y)
                continue;

            // Calculate color with depth shading for 3D-like appearance
            uint8_t r = base_r, g = base_g, b = base_b;
            if (depth_shading_ && view_mode_ == ViewMode::FRONT) {
                // Calculate brightness factor based on Z position
                float z_range = bounds_max_z_ - bounds_min_z_;
                float avg_z = (seg.start.z + seg.end.z) / 2.0f;

                float brightness = 0.4f;
                if (z_range > 0.001f) {
                    float normalized_z = (avg_z - bounds_min_z_) / z_range;
                    brightness = 0.4f + 0.6f * normalized_z;
                }

                // Y-depth fade
                float y_range = bounds_max_y_ - bounds_min_y_;
                float avg_y = (seg.start.y + seg.end.y) / 2.0f;
                if (y_range > 0.001f) {
                    float normalized_y = (avg_y - bounds_min_y_) / y_range;
                    float depth_fade = 0.85f + 0.15f * (1.0f - normalized_y);
                    brightness *= depth_fade;
                }

                r = static_cast<uint8_t>(base_r * brightness);
                g = static_cast<uint8_t>(base_g * brightness);
                b = static_cast<uint8_t>(base_b * brightness);
            }

            // Override color for excluded/highlighted objects
            if (!seg.object_name.empty()) {
                if (excluded_objects_.count(seg.object_name) > 0) {
                    // Excluded: orange-red with reduced alpha
                    r = 0xFF;
                    g = 0x6B;
                    b = 0x35;
                    uint32_t color = (153u << 24) | (r << 16) | (g << 8) | b; // 60% alpha
                    draw_line_bresenham_solid(p1.x, p1.y, p2.x, p2.y, color);
                    ++segments_rendered;
                    continue;
                }
                if (highlighted_objects_.count(seg.object_name) > 0) {
                    // Highlighted: selection blue, full alpha
                    r = 0x42;
                    g = 0xA5;
                    b = 0xF5;
                }
            }

            // Build ARGB8888 color (full alpha for solid layers)
            uint32_t color = (255u << 24) | (r << 16) | (g << 8) | b;

            // Draw using software Bresenham - bypasses LVGL draw API for AD5M compatibility
            draw_line_bresenham_solid(p1.x, p1.y, p2.x, p2.y, color);
            ++segments_rendered;
        }
    }

    spdlog::debug("[GCodeLayerRenderer] Rendered layers {}-{}: {} segments to cache (direct), "
                  "color=#{:02X}{:02X}{:02X}, buf={}x{} stride={}",
                  from_layer, to_layer, segments_rendered, base_r, base_g, base_b, cached_width_,
                  cached_height_, cache_buf_ ? cache_buf_->header.stride : 0);
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
    if (ghost_buf_) {
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
        // Create the draw buffer (no canvas widget - avoids clip area contamination
        // from overlays/toasts on lv_layer_top())
        ghost_buf_ = lv_draw_buf_create(width, height, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
        if (!ghost_buf_) {
            spdlog::error("[GCodeLayerRenderer] Failed to create ghost buffer {}x{}", width,
                          height);
            return;
        }

        lv_draw_buf_clear(ghost_buf_, nullptr);

        ghost_cache_valid_ = false;
        spdlog::debug("[GCodeLayerRenderer] Created ghost cache buffer: {}x{}", width, height);
        helix::MemoryMonitor::log_now("gcode_ghost_buffer_created");
    }
}

void GCodeLayerRenderer::render_ghost_layers(int from_layer, int to_layer) {
    if (!ghost_buf_ || !gcode_)
        return;

    // Manually initialize layer for offscreen rendering (no canvas widget)
    // This avoids clip area contamination from overlays/toasts on lv_layer_top()
    lv_layer_t ghost_layer;
    lv_memzero(&ghost_layer, sizeof(ghost_layer));
    ghost_layer.draw_buf = ghost_buf_;
    ghost_layer.color_format = LV_COLOR_FORMAT_ARGB8888;
    ghost_layer.buf_area.x1 = 0;
    ghost_layer.buf_area.y1 = 0;
    ghost_layer.buf_area.x2 = cached_width_ - 1;
    ghost_layer.buf_area.y2 = cached_height_ - 1;
    ghost_layer._clip_area = ghost_layer.buf_area; // Full buffer as clip area
    ghost_layer.phy_clip_area = ghost_layer.buf_area;

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

    // Dispatch pending draw tasks (equivalent to lv_canvas_finish_layer)
    lv_draw_dispatch_wait_for_request();
    while (ghost_layer.draw_task_head) {
        lv_draw_dispatch_layer(nullptr, &ghost_layer);
        if (ghost_layer.draw_task_head) {
            lv_draw_dispatch_wait_for_request();
        }
    }

    widget_offset_x_ = saved_offset_x;
    widget_offset_y_ = saved_offset_y;

    spdlog::debug("[GCodeLayerRenderer] Rendered ghost layers {}-{}: {} segments", from_layer,
                  to_layer, segments_rendered);
    helix::MemoryMonitor::log_now("gcode_ghost_render_done");
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
    int layer_count = get_layer_count();
    if (layer_count == 0) {
        spdlog::debug("[GCodeLayerRenderer] render(): no gcode data");
        return;
    }

    if (current_layer_ < 0 || current_layer_ >= layer_count) {
        spdlog::debug("[GCodeLayerRenderer] render(): layer out of range ({} / {})", current_layer_,
                      layer_count);
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
        int target_layer = std::min(current_layer_, layer_count - 1);

        // Ensure cache buffers exist and are correct size
        ensure_cache(canvas_width_, canvas_height_);
        if (ghost_mode_enabled_) {
            ensure_ghost_cache(canvas_width_, canvas_height_);
        }

        // =====================================================================
        // GHOST CACHE: Background thread rendering (non-blocking)
        // Uses unified background thread for both streaming and non-streaming modes.
        // The background thread renders all layers to a raw buffer, then we copy
        // to LVGL buffer on main thread when ready.
        // =====================================================================
        if (ghost_mode_enabled_ && ghost_buf_ && !ghost_cache_valid_) {
            if (ghost_thread_ready_.load()) {
                // Background thread finished - copy to LVGL buffer
                copy_raw_to_ghost_buf();
            } else if (!ghost_thread_running_.load()) {
                // Start background thread if not running
                start_background_ghost_render();
            }
            // else: background thread is running, wait for it
        }

        // =====================================================================
        // WARM-UP FRAMES: Skip heavy rendering to let panel layout complete
        // =====================================================================
        if (warmup_frames_remaining_ > 0) {
            warmup_frames_remaining_--;
            // Just blit ghost cache (if available) and return - no heavy caching yet
            if (ghost_mode_enabled_ && ghost_buf_) {
                blit_ghost_cache(layer);
            }
            // Request another frame to continue after warmup
            last_frame_render_ms_ = 1; // Minimal time so adaptation doesn't spike
            return;
        }

        // =====================================================================
        // SOLID CACHE: Progressive rendering up to current print layer
        // =====================================================================
        if (cache_buf_) {
            // Check if we need to render new layers
            if (target_layer > cached_up_to_layer_) {
                // Progressive rendering: only render up to layers_per_frame_ at a time
                // This prevents UI freezing during initial load or big jumps
                int from_layer = cached_up_to_layer_ + 1;
                int to_layer = std::min(from_layer + layers_per_frame_ - 1, target_layer);

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

                int to_layer = std::min(layers_per_frame_ - 1, target_layer);
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
        // Get segments from appropriate source
        // For streaming mode, hold shared_ptr to keep data alive during iteration
        std::shared_ptr<const std::vector<ToolpathSegment>> segments_holder;
        const std::vector<ToolpathSegment>* segments = nullptr;

        if (streaming_controller_) {
            // Streaming mode: get segments from controller (returns shared_ptr)
            segments_holder =
                streaming_controller_->get_layer_segments(static_cast<size_t>(current_layer_));
            segments = segments_holder.get();
            // Use default centering for streaming mode
            // (Could be improved by computing bounds from segments if needed)
        } else if (gcode_) {
            // Full file mode: get segments and bounding box from parsed file
            const auto& layer_bb = gcode_->layers[current_layer_].bounding_box;
            offset_x_ = (layer_bb.min.x + layer_bb.max.x) / 2.0f;
            offset_y_ = (layer_bb.min.y + layer_bb.max.y) / 2.0f;
            segments = &gcode_->layers[current_layer_].segments;
        }

        if (segments) {
            for (const auto& seg : *segments) {
                if (!should_render_segment(seg))
                    continue;
                render_segment(layer, seg);
                ++segments_rendered;
            }
        }
    }

    // Draw selection brackets on top of everything
    render_selection_brackets(layer);

    // Track render time for diagnostics
    last_render_time_ms_ = lv_tick_get() - start_time;
    last_frame_render_ms_ = last_render_time_ms_;
    last_segment_count_ = segments_rendered;

    // Adapt layers_per_frame for next frame (if in adaptive mode)
    if (config_layers_per_frame_ == 0 && view_mode_ == ViewMode::FRONT) {
        adapt_layers_per_frame();
    }

    // Log performance if layer changed or slow render
    if (current_layer_ != last_rendered_layer_ || last_render_time_ms_ > 50) {
        spdlog::debug("[GCodeLayerRenderer] Layer {}: {}ms (cached_up_to={}, lpf={})",
                      current_layer_, last_render_time_ms_, cached_up_to_layer_, layers_per_frame_);
        last_rendered_layer_ = current_layer_;
    }
}

bool GCodeLayerRenderer::needs_more_frames() const {
    int layer_count = get_layer_count();
    if (layer_count == 0) {
        return false;
    }

    // Only relevant for FRONT view mode (uses caching)
    if (view_mode_ != ViewMode::FRONT) {
        return false;
    }

    int target_layer = std::min(current_layer_, layer_count - 1);

    // Solid cache incomplete?
    if (cached_up_to_layer_ < target_layer) {
        return true;
    }

    // Ghost rendering in background?
    // Keep triggering frames while ghost is building so we can show progress
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

    // Check excluded/highlighted state for width/opacity
    bool is_excluded = !seg.object_name.empty() && excluded_objects_.count(seg.object_name) > 0;
    bool is_highlighted =
        !seg.object_name.empty() && highlighted_objects_.count(seg.object_name) > 0;

    if (is_excluded) {
        dsc.width = 1;
        dsc.opa = LV_OPA_60;
    } else if (is_highlighted) {
        dsc.width = 3;
        dsc.opa = LV_OPA_COVER;
    } else if (seg.is_extrusion) {
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

// ============================================================================
// Transformation - Single Source of Truth
// ============================================================================

GCodeLayerRenderer::TransformParams GCodeLayerRenderer::capture_transform_params() const {
    return TransformParams{
        .view_mode = view_mode_,
        .scale = scale_,
        .offset_x = offset_x_,
        .offset_y = offset_y_,
        .offset_z = offset_z_,
        .canvas_width = canvas_width_,
        .canvas_height = canvas_height_,
        .content_offset_y_percent = content_offset_y_percent_,
    };
}

glm::ivec2 GCodeLayerRenderer::world_to_screen_raw(const TransformParams& params, float x, float y,
                                                   float z) {
    float sx, sy;

    switch (params.view_mode) {
    case ViewMode::FRONT: {
        // Isometric-style view: 45° horizontal rotation + 30° elevation
        // This creates a "corner view looking down" perspective
        //
        // First apply 90° CCW rotation around Z to match thumbnail orientation
        // (thumbnails show models from a different default angle)
        float raw_dx = x - params.offset_x;
        float raw_dy = y - params.offset_y;
        float dx = -raw_dy; // 90° CCW: new_x = -old_y
        float dy = raw_dx;  // 90° CCW: new_y = old_x
        float dz = z - params.offset_z;

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
        sx = rx * params.scale + static_cast<float>(params.canvas_width) / 2.0f;
        sy = static_cast<float>(params.canvas_height) / 2.0f -
             (dz * COS_E + ry * SIN_E) * params.scale;
        break;
    }

    case ViewMode::ISOMETRIC: {
        // Isometric projection (45° rotation with Y compression)
        float dx = x - params.offset_x;
        float dy = y - params.offset_y;
        constexpr float ISO_ANGLE = 0.7071f;
        constexpr float ISO_Y_SCALE = 0.5f;

        float iso_x = (dx - dy) * ISO_ANGLE;
        float iso_y = (dx + dy) * ISO_ANGLE * ISO_Y_SCALE;

        sx = iso_x * params.scale + static_cast<float>(params.canvas_width) / 2.0f;
        sy = static_cast<float>(params.canvas_height) / 2.0f - iso_y * params.scale;
        break;
    }

    case ViewMode::TOP_DOWN:
    default: {
        // Top-down: X → screen X, Y → screen Y (flipped)
        float dx = x - params.offset_x;
        float dy = y - params.offset_y;
        sx = dx * params.scale + static_cast<float>(params.canvas_width) / 2.0f;
        sy = static_cast<float>(params.canvas_height) / 2.0f - dy * params.scale;
        break;
    }
    }

    // Apply content offset (shifts render center for UI overlapping elements)
    // Negative offset_percent shifts content UP (toward top of canvas)
    // CRITICAL: This must match for both solid and ghost layers!
    sy += params.content_offset_y_percent * static_cast<float>(params.canvas_height);

    return {static_cast<int>(sx), static_cast<int>(sy)};
}

glm::ivec2 GCodeLayerRenderer::world_to_screen(float x, float y, float z) const {
    // Use the shared transformation logic
    TransformParams params = capture_transform_params();
    glm::ivec2 raw = world_to_screen_raw(params, x, y, z);

    // Add widget's screen offset (drawing is in screen coordinates)
    return {raw.x + widget_offset_x_, raw.y + widget_offset_y_};
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

std::optional<std::string> GCodeLayerRenderer::pick_object_at(int screen_x, int screen_y) const {
    // Need data source
    if (!gcode_ && !streaming_controller_)
        return std::nullopt;

    int layer_count = get_layer_count();
    if (current_layer_ < 0 || current_layer_ >= layer_count)
        return std::nullopt;

    // Capture transform params (no widget offset for cache coords)
    TransformParams transform = capture_transform_params();

    const float PICK_THRESHOLD = 15.0f; // pixels
    float closest_distance = std::numeric_limits<float>::max();
    std::optional<std::string> picked_object;

    // Get segments for current layer
    std::shared_ptr<const std::vector<ToolpathSegment>> segments_holder;
    const std::vector<ToolpathSegment>* segments = nullptr;

    if (streaming_controller_) {
        segments_holder =
            streaming_controller_->get_layer_segments(static_cast<size_t>(current_layer_));
        segments = segments_holder.get();
    } else if (gcode_) {
        segments = &gcode_->layers[static_cast<size_t>(current_layer_)].segments;
    }

    if (!segments)
        return std::nullopt;

    glm::vec2 click_pos(static_cast<float>(screen_x), static_cast<float>(screen_y));

    for (const auto& seg : *segments) {
        if (!should_render_segment(seg))
            continue;

        if (seg.object_name.empty())
            continue;

        // Project segment endpoints to screen space
        glm::ivec2 p1 = world_to_screen_raw(transform, seg.start.x, seg.start.y, seg.start.z);
        glm::ivec2 p2 = world_to_screen_raw(transform, seg.end.x, seg.end.y, seg.end.z);

        // Calculate distance from click to line segment
        glm::vec2 v(static_cast<float>(p2.x - p1.x), static_cast<float>(p2.y - p1.y));
        glm::vec2 w(click_pos.x - static_cast<float>(p1.x), click_pos.y - static_cast<float>(p1.y));

        float segment_length_sq = glm::dot(v, v);
        float t = (segment_length_sq > 0.0001f)
                      ? std::clamp(glm::dot(w, v) / segment_length_sq, 0.0f, 1.0f)
                      : 0.0f;

        glm::vec2 closest_point(static_cast<float>(p1.x) + t * v.x,
                                static_cast<float>(p1.y) + t * v.y);
        float dist = glm::length(click_pos - closest_point);

        if (dist < PICK_THRESHOLD && dist < closest_distance) {
            closest_distance = dist;
            picked_object = seg.object_name;
        }
    }

    return picked_object;
}

lv_color_t GCodeLayerRenderer::get_segment_color(const ToolpathSegment& seg) const {
    // Check excluded/highlighted state first
    if (!seg.object_name.empty()) {
        if (excluded_objects_.count(seg.object_name) > 0) {
            return lv_color_hex(0xFF6B35); // Orange-red for excluded
        }
        if (highlighted_objects_.count(seg.object_name) > 0) {
            return lv_color_hex(0x42A5F5); // Selection blue for highlighted
        }
    }

    // Existing logic below
    if (!seg.is_extrusion) {
        return color_travel_;
    }
    if (is_support_segment(seg)) {
        return color_support_;
    }
    return color_extrusion_;
}

void GCodeLayerRenderer::render_selection_brackets(lv_layer_t* layer) {
    // Only render if we have highlighted objects and full gcode data
    if (highlighted_objects_.empty() || !gcode_) {
        return;
    }

    for (const auto& object_name : highlighted_objects_) {
        auto it = gcode_->objects.find(object_name);
        if (it == gcode_->objects.end()) {
            continue;
        }

        const AABB& bbox = it->second.bounding_box;

        // Calculate corner bracket length (20% of shortest edge, capped at 5mm)
        // Same formula as TinyGL renderer
        float dx = bbox.max.x - bbox.min.x;
        float dy = bbox.max.y - bbox.min.y;
        float dz = bbox.max.z - bbox.min.z;
        float min_edge = std::min({dx, dy, dz});
        float bracket_len = std::min(min_edge * 0.2f, 5.0f);

        // If bounding box is degenerate, skip
        if (bracket_len < 0.01f) {
            continue;
        }

        // Set up line drawing style
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color = lv_color_hex(0xC0C0C0); // Light grey - visible against dark background
        dsc.width = 2;
        dsc.opa = LV_OPA_COVER;

        // Define all 8 corners of the AABB
        const glm::vec3 corners[8] = {
            {bbox.min.x, bbox.min.y, bbox.min.z}, // 0: min,min,min
            {bbox.max.x, bbox.min.y, bbox.min.z}, // 1: max,min,min
            {bbox.max.x, bbox.max.y, bbox.min.z}, // 2: max,max,min
            {bbox.min.x, bbox.max.y, bbox.min.z}, // 3: min,max,min
            {bbox.min.x, bbox.min.y, bbox.max.z}, // 4: min,min,max
            {bbox.max.x, bbox.min.y, bbox.max.z}, // 5: max,min,max
            {bbox.max.x, bbox.max.y, bbox.max.z}, // 6: max,max,max
            {bbox.min.x, bbox.max.y, bbox.max.z}, // 7: min,max,max
        };

        // For each corner, define 3 bracket directions (along X, Y, Z axes)
        // Sign: +1 if bracket goes toward max, -1 if toward min
        const float signs[8][3] = {
            {+1, +1, +1}, // corner 0: min,min,min -> bracket toward max
            {-1, +1, +1}, // corner 1: max,min,min
            {-1, -1, +1}, // corner 2: max,max,min
            {+1, -1, +1}, // corner 3: min,max,min
            {+1, +1, -1}, // corner 4: min,min,max
            {-1, +1, -1}, // corner 5: max,min,max
            {-1, -1, -1}, // corner 6: max,max,max
            {+1, -1, -1}, // corner 7: min,max,max
        };

        for (int c = 0; c < 8; ++c) {
            glm::ivec2 corner_screen = world_to_screen(corners[c].x, corners[c].y, corners[c].z);

            // Draw bracket line along X axis
            glm::ivec2 bx = world_to_screen(corners[c].x + signs[c][0] * bracket_len, corners[c].y,
                                            corners[c].z);

            dsc.p1.x = static_cast<lv_value_precise_t>(corner_screen.x);
            dsc.p1.y = static_cast<lv_value_precise_t>(corner_screen.y);
            dsc.p2.x = static_cast<lv_value_precise_t>(bx.x);
            dsc.p2.y = static_cast<lv_value_precise_t>(bx.y);
            lv_draw_line(layer, &dsc);

            // Draw bracket line along Y axis
            glm::ivec2 by = world_to_screen(corners[c].x, corners[c].y + signs[c][1] * bracket_len,
                                            corners[c].z);

            dsc.p2.x = static_cast<lv_value_precise_t>(by.x);
            dsc.p2.y = static_cast<lv_value_precise_t>(by.y);
            lv_draw_line(layer, &dsc);

            // Draw bracket line along Z axis
            glm::ivec2 bz = world_to_screen(corners[c].x, corners[c].y,
                                            corners[c].z + signs[c][2] * bracket_len);

            dsc.p2.x = static_cast<lv_value_precise_t>(bz.x);
            dsc.p2.y = static_cast<lv_value_precise_t>(bz.y);
            lv_draw_line(layer, &dsc);
        }
    }
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

    // Need either gcode or streaming controller
    int layer_count = get_layer_count();
    if (layer_count == 0) {
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
    // Signal cancellation and join if the thread is joinable.
    // This handles the case where the thread completed naturally but wasn't
    // joined yet - we MUST join before assigning a new thread or std::terminate() is called.
    ghost_thread_cancel_.store(true);
    if (ghost_thread_.joinable()) {
        ghost_thread_.join();
    }
    ghost_thread_running_.store(false);
    ghost_thread_cancel_.store(false); // Reset for next run
}

// =============================================================================
// Ghost Build Progress
// =============================================================================

float GCodeLayerRenderer::get_ghost_build_progress() const {
    // Background thread running: return 0.5 (in progress)
    // Background thread ready: return 1.0 (complete)
    // Otherwise: return 1.0 (nothing to do or already done)
    return ghost_thread_ready_.load() ? 1.0f : (ghost_thread_running_.load() ? 0.5f : 1.0f);
}

bool GCodeLayerRenderer::is_ghost_build_complete() const {
    return ghost_thread_ready_.load() || ghost_cache_valid_;
}

bool GCodeLayerRenderer::is_ghost_build_running() const {
    return ghost_thread_running_.load();
}

void GCodeLayerRenderer::background_ghost_render_thread() {
    // Works with both full-file mode (gcode_) and streaming mode (streaming_controller_)
    if (!ghost_raw_buffer_ || (!gcode_ && !streaming_controller_)) {
        ghost_thread_running_.store(false);
        return;
    }

    // Use std::chrono for timing - lv_tick_get() is not thread-safe
    auto start_time = std::chrono::steady_clock::now();
    size_t segments_rendered = 0;
    int total_layers = get_layer_count();

    // =========================================================================
    // THREAD SAFETY: Capture ALL shared state at thread start
    // These values may be modified by the main thread during rendering, so we
    // take a snapshot to ensure consistent rendering throughout.
    // =========================================================================

    // Use TransformParams for unified coordinate conversion - includes content offset!
    // This is the SINGLE SOURCE OF TRUTH for coordinate transforms.
    TransformParams transform = capture_transform_params();
    // Override canvas size with ghost buffer dimensions (may differ from display)
    transform.canvas_width = ghost_raw_width_;
    transform.canvas_height = ghost_raw_height_;

    // Visibility flags (can be changed via set_show_*() on main thread)
    const bool local_show_travels = show_travels_;
    const bool local_show_extrusions = show_extrusions_;
    const bool local_show_supports = show_supports_;

    // Color (can be changed via set_extrusion_color() on main thread)
    const lv_color_t local_color_extrusion = color_extrusion_;

    // Capture excluded objects for ghost rendering (thread-safe copy)
    const auto local_excluded = excluded_objects_;

    // Local version of should_render_segment using captured flags
    auto local_should_render = [&](const ToolpathSegment& seg) -> bool {
        if (seg.is_extrusion) {
            if (is_support_segment(seg)) // Only reads seg fields, safe
                return local_show_supports;
            return local_show_extrusions;
        }
        return local_show_travels;
    };

    // Compute ghost color once (darkened extrusion color from captured value)
    // ARGB8888: A in high byte, R, G, B in lower bytes
    uint8_t ghost_r = local_color_extrusion.red * 40 / 100;
    uint8_t ghost_g = local_color_extrusion.green * 40 / 100;
    uint8_t ghost_b = local_color_extrusion.blue * 40 / 100;
    uint8_t ghost_a = 255; // Full alpha, we'll apply 40% when blitting
    uint32_t ghost_color = (ghost_a << 24) | (ghost_r << 16) | (ghost_g << 8) | ghost_b;

    // Render all layers to raw buffer
    // Works with both full-file mode (gcode_) and streaming mode (streaming_controller_)
    for (int layer_idx = 0; layer_idx < total_layers; ++layer_idx) {
        // Check for cancellation periodically
        if (ghost_thread_cancel_.load()) {
            spdlog::debug("[GCodeLayerRenderer] Ghost render cancelled at layer {}/{}", layer_idx,
                          total_layers);
            ghost_thread_running_.store(false);
            return;
        }

        // Get segments from appropriate source
        // CRITICAL: For streaming mode, hold shared_ptr to keep data alive during iteration.
        // This prevents use-after-free if cache evicts the layer while we're iterating.
        std::shared_ptr<const std::vector<ToolpathSegment>> segments_holder;
        const std::vector<ToolpathSegment>* segments = nullptr;

        if (streaming_controller_) {
            // Streaming mode: get segments from controller (returns shared_ptr)
            segments_holder =
                streaming_controller_->get_layer_segments(static_cast<size_t>(layer_idx));
            segments = segments_holder.get();
        } else if (gcode_) {
            segments = &gcode_->layers[layer_idx].segments;
        }

        if (!segments)
            continue;

        for (const auto& seg : *segments) {
            if (!local_should_render(seg))
                continue;

            // Use unified world_to_screen_raw - includes content offset!
            glm::ivec2 p1 = world_to_screen_raw(transform, seg.start.x, seg.start.y, seg.start.z);
            glm::ivec2 p2 = world_to_screen_raw(transform, seg.end.x, seg.end.y, seg.end.z);

            // Skip zero-length segments
            if (p1.x == p2.x && p1.y == p2.y)
                continue;

            // Use excluded color for excluded objects even in ghost
            uint32_t seg_color = ghost_color;
            if (!seg.object_name.empty() && local_excluded.count(seg.object_name) > 0) {
                // Excluded: dim orange-red
                uint8_t ex_r = 0xFF * 40 / 100;
                uint8_t ex_g = 0x6B * 40 / 100;
                uint8_t ex_b = 0x35 * 40 / 100;
                seg_color = (255u << 24) | (ex_r << 16) | (ex_g << 8) | ex_b;
            }

            // Draw line using Bresenham algorithm
            draw_line_bresenham(p1.x, p1.y, p2.x, p2.y, seg_color);
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

void GCodeLayerRenderer::blend_pixel_solid(int x, int y, uint32_t color) {
    // Bounds check using cached dimensions
    if (x < 0 || x >= cached_width_ || y < 0 || y >= cached_height_ || !cache_buf_) {
        return;
    }

    // Get stride from LVGL buffer (may differ from width * 4 due to alignment)
    uint32_t stride = cache_buf_->header.stride;

    // Calculate pixel offset (ARGB8888 = 4 bytes per pixel)
    uint8_t* pixel = static_cast<uint8_t*>(cache_buf_->data) + y * stride + x * 4;

    // LVGL uses ARGB8888: byte order is B, G, R, A on little-endian
    pixel[0] = color & 0xFF;         // B
    pixel[1] = (color >> 8) & 0xFF;  // G
    pixel[2] = (color >> 16) & 0xFF; // R
    pixel[3] = (color >> 24) & 0xFF; // A
}

void GCodeLayerRenderer::draw_line_bresenham_solid(int x0, int y0, int x1, int y1, uint32_t color) {
    // Bresenham's line algorithm for software line drawing to solid cache

    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        blend_pixel_solid(x0, y0, color);

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

// ============================================================================
// Configuration
// ============================================================================

void GCodeLayerRenderer::load_config() {
    auto* config = Config::get_instance();
    if (!config) {
        spdlog::debug("[GCodeLayerRenderer] No config instance, using defaults");
        return;
    }

    // Load layers_per_frame: 0 = adaptive, 1-100 = fixed
    config_layers_per_frame_ = config->get<int>("/gcode_viewer/layers_per_frame", 0);
    config_layers_per_frame_ = std::clamp(config_layers_per_frame_, 0, MAX_LAYERS_PER_FRAME);

    if (config_layers_per_frame_ > 0) {
        // Fixed value from config
        layers_per_frame_ =
            std::clamp(config_layers_per_frame_, MIN_LAYERS_PER_FRAME, MAX_LAYERS_PER_FRAME);
        spdlog::info("[GCodeLayerRenderer] Using fixed layers_per_frame: {}", layers_per_frame_);
    } else {
        // Adaptive mode - start with default, adjust based on render time
        layers_per_frame_ = DEFAULT_LAYERS_PER_FRAME;
        spdlog::info("[GCodeLayerRenderer] Using adaptive layers_per_frame (starting at {})",
                     layers_per_frame_);
    }

    // Load adaptive target (only used when config_layers_per_frame_ == 0)
    adaptive_target_ms_ =
        config->get<int>("/gcode_viewer/adaptive_layer_target_ms", DEFAULT_ADAPTIVE_TARGET_MS);
    adaptive_target_ms_ = std::clamp(adaptive_target_ms_, 1, 100); // Sensible bounds

    spdlog::debug("[GCodeLayerRenderer] Adaptive target: {}ms", adaptive_target_ms_);

    // Detect device tier and apply appropriate limits for constrained devices
    auto mem_info = ::helix::get_system_memory_info();
    is_constrained_device_ = mem_info.is_constrained_device();

    if (is_constrained_device_) {
        max_layers_per_frame_ = CONSTRAINED_MAX_LPF;
        if (config_layers_per_frame_ == 0) { // Adaptive mode
            layers_per_frame_ = CONSTRAINED_START_LPF;
        }
        spdlog::info(
            "[GCodeLayerRenderer] Constrained device detected: lpf capped at {}, starting at {}",
            max_layers_per_frame_, layers_per_frame_);
    }
}

void GCodeLayerRenderer::adapt_layers_per_frame() {
    // Only adapt when in adaptive mode (config value == 0)
    if (config_layers_per_frame_ != 0) {
        return;
    }

    // Skip adaptation if no meaningful render time yet
    if (last_frame_render_ms_ == 0) {
        return;
    }

    // Adaptive algorithm:
    // - If render time < target: increase layers_per_frame to cache faster
    // - If render time > target: decrease layers_per_frame to avoid UI stutter
    // - Use exponential moving average to smooth adjustments

    int old_lpf = layers_per_frame_;

    if (last_frame_render_ms_ < static_cast<uint32_t>(adaptive_target_ms_)) {
        // Under budget - can render more layers
        // Scale up proportionally but cap growth (conservative on constrained devices)
        float ratio = static_cast<float>(adaptive_target_ms_) / std::max(1u, last_frame_render_ms_);
        float max_growth = is_constrained_device_ ? CONSTRAINED_GROWTH_CAP : 2.0f;
        ratio = std::min(ratio, max_growth);
        int new_lpf = static_cast<int>(layers_per_frame_ * ratio);
        // Smooth increase (take average of current and target)
        layers_per_frame_ = (layers_per_frame_ + new_lpf) / 2;
    } else if (last_frame_render_ms_ > static_cast<uint32_t>(adaptive_target_ms_ * 2)) {
        // Significantly over budget - reduce aggressively
        float ratio = static_cast<float>(adaptive_target_ms_) / std::max(1u, last_frame_render_ms_);
        layers_per_frame_ = static_cast<int>(layers_per_frame_ * ratio);
    } else if (last_frame_render_ms_ > static_cast<uint32_t>(adaptive_target_ms_)) {
        // Slightly over budget - reduce gradually
        layers_per_frame_ = layers_per_frame_ * 3 / 4; // 75% of current
    }

    // Clamp to valid range (using device-aware max)
    layers_per_frame_ = std::clamp(layers_per_frame_, MIN_LAYERS_PER_FRAME, max_layers_per_frame_);

    // Log significant changes
    if (layers_per_frame_ != old_lpf) {
        spdlog::trace("[GCodeLayerRenderer] Adaptive lpf: {} -> {} (render={}ms, target={}ms)",
                      old_lpf, layers_per_frame_, last_frame_render_ms_, adaptive_target_ms_);
    }
}

} // namespace gcode
} // namespace helix
