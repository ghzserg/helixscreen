// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_object_thumbnail_renderer.h"

#include "ui_update_queue.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace helix::gcode {

// Padding around the rendered object in pixels
static constexpr int kThumbnailPadding = 2;

// Check cancellation every N layers to avoid overhead
static constexpr int kCancelCheckInterval = 10;

// ============================================================================
// CONSTRUCTION / DESTRUCTION
// ============================================================================

GCodeObjectThumbnailRenderer::GCodeObjectThumbnailRenderer() = default;

GCodeObjectThumbnailRenderer::~GCodeObjectThumbnailRenderer() {
    cancel();
}

// ============================================================================
// PUBLIC API
// ============================================================================

void GCodeObjectThumbnailRenderer::render_async(const ParsedGCodeFile* gcode, int thumb_width,
                                                int thumb_height, uint32_t color,
                                                ThumbnailCompleteCallback callback) {
    // Cancel any in-progress render
    cancel();

    if (!gcode || gcode->objects.empty()) {
        spdlog::debug("[ObjectThumbnail] No objects to render");
        if (callback) {
            callback(std::make_unique<ObjectThumbnailSet>());
        }
        return;
    }

    cancel_.store(false, std::memory_order_relaxed);
    rendering_.store(true, std::memory_order_relaxed);

    thread_ =
        std::thread([this, gcode, thumb_width, thumb_height, color, cb = std::move(callback)]() {
            auto result = render_impl(gcode, thumb_width, thumb_height, color);

            rendering_.store(false, std::memory_order_relaxed);

            if (!cancel_.load(std::memory_order_relaxed) && cb) {
                // Marshal result to UI thread. Use shared_ptr for lambda capture so the
                // ObjectThumbnailSet is freed even if the UI queue is drained on shutdown
                // before this lambda runs (std::function requires copyable lambdas).
                auto shared = std::shared_ptr<ObjectThumbnailSet>(result.release());
                ui_queue_update([cb, shared]() {
                    cb(std::make_unique<ObjectThumbnailSet>(std::move(*shared)));
                });
            }
        });
}

std::unique_ptr<ObjectThumbnailSet>
GCodeObjectThumbnailRenderer::render_sync(const ParsedGCodeFile* gcode, int thumb_width,
                                          int thumb_height, uint32_t color) {
    cancel_.store(false, std::memory_order_relaxed);
    rendering_.store(true, std::memory_order_relaxed);

    auto result = render_impl(gcode, thumb_width, thumb_height, color);

    rendering_.store(false, std::memory_order_relaxed);
    return result;
}

void GCodeObjectThumbnailRenderer::cancel() {
    cancel_.store(true, std::memory_order_relaxed);
    if (thread_.joinable()) {
        thread_.join();
    }
    cancel_.store(false, std::memory_order_relaxed);
}

// ============================================================================
// CORE RENDERING
// ============================================================================

std::unique_ptr<ObjectThumbnailSet>
GCodeObjectThumbnailRenderer::render_impl(const ParsedGCodeFile* gcode, int thumb_width,
                                          int thumb_height, uint32_t color) {
    auto start_time = std::chrono::steady_clock::now();

    auto result = std::make_unique<ObjectThumbnailSet>();

    if (!gcode || gcode->objects.empty()) {
        return result;
    }

    // Build per-object render contexts with coordinate transforms
    auto contexts = build_contexts(gcode, thumb_width, thumb_height);

    if (contexts.empty()) {
        spdlog::debug("[ObjectThumbnail] No valid object contexts (all empty bounding boxes?)");
        return result;
    }

    // Single pass through all layers and segments
    size_t segments_rendered = 0;
    for (size_t layer_idx = 0; layer_idx < gcode->layers.size(); ++layer_idx) {
        // Periodic cancellation check
        if ((layer_idx % kCancelCheckInterval) == 0 && cancel_.load(std::memory_order_relaxed)) {
            spdlog::debug("[ObjectThumbnail] Cancelled at layer {}/{}", layer_idx,
                          gcode->layers.size());
            return result;
        }

        const auto& layer = gcode->layers[layer_idx];
        for (const auto& seg : layer.segments) {
            // Skip non-extrusion and unnamed segments
            if (!seg.is_extrusion || seg.object_name.empty()) {
                continue;
            }

            // Find the render context for this object
            auto it = contexts.find(seg.object_name);
            if (it == contexts.end()) {
                continue;
            }

            auto& ctx = it->second;

            // Convert world coordinates to pixel coordinates
            int px0, py0, px1, py1;
            world_to_pixel(ctx, seg.start.x, seg.start.y, px0, py0);
            world_to_pixel(ctx, seg.end.x, seg.end.y, px1, py1);

            // Draw the line
            draw_line(ctx, px0, py0, px1, py1, color);
            ++segments_rendered;
        }
    }

    // Convert contexts to output thumbnails
    for (auto& [name, ctx] : contexts) {
        ObjectThumbnail thumb;
        thumb.object_name = name;
        thumb.pixels = std::move(ctx.pixels);
        thumb.width = ctx.width;
        thumb.height = ctx.height;
        thumb.stride = ctx.stride;
        result->thumbnails.push_back(std::move(thumb));
    }

    auto elapsed = std::chrono::steady_clock::now() - start_time;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    spdlog::debug("[ObjectThumbnail] Rendered {} thumbnails ({} segments) in {}ms",
                  result->thumbnails.size(), segments_rendered, ms);

    return result;
}

std::unordered_map<std::string, GCodeObjectThumbnailRenderer::ObjectRenderContext>
GCodeObjectThumbnailRenderer::build_contexts(const ParsedGCodeFile* gcode, int thumb_width,
                                             int thumb_height) {
    std::unordered_map<std::string, ObjectRenderContext> contexts;

    const int effective_size_w = thumb_width - 2 * kThumbnailPadding;
    const int effective_size_h = thumb_height - 2 * kThumbnailPadding;

    if (effective_size_w <= 0 || effective_size_h <= 0) {
        return contexts;
    }

    for (const auto& [name, obj] : gcode->objects) {
        const auto& bbox = obj.bounding_box;

        // Skip objects with empty/degenerate bounding boxes
        if (bbox.is_empty()) {
            continue;
        }

        float bbox_width = bbox.max.x - bbox.min.x;
        float bbox_height = bbox.max.y - bbox.min.y;

        // Handle degenerate dimensions (line objects)
        if (bbox_width < 0.01f)
            bbox_width = 0.01f;
        if (bbox_height < 0.01f)
            bbox_height = 0.01f;

        // Compute uniform scale to fit within effective area
        float scale_x = static_cast<float>(effective_size_w) / bbox_width;
        float scale_y = static_cast<float>(effective_size_h) / bbox_height;
        float scale = std::min(scale_x, scale_y);

        // Center within the effective area
        float offset_x = kThumbnailPadding + (effective_size_w - bbox_width * scale) / 2.0f;
        float offset_y = kThumbnailPadding + (effective_size_h - bbox_height * scale) / 2.0f;

        ObjectRenderContext ctx;
        ctx.name = name;
        ctx.width = thumb_width;
        ctx.height = thumb_height;
        ctx.stride = thumb_width * 4;
        ctx.scale = scale;
        ctx.offset_x = offset_x;
        ctx.offset_y = offset_y;
        ctx.min_x = bbox.min.x;
        ctx.max_y = bbox.max.y;

        // Allocate and zero-fill pixel buffer (transparent black)
        size_t buf_size = static_cast<size_t>(ctx.height) * ctx.stride;
        ctx.pixels = std::make_unique<uint8_t[]>(buf_size);
        std::memset(ctx.pixels.get(), 0, buf_size);

        contexts.emplace(name, std::move(ctx));
    }

    return contexts;
}

// ============================================================================
// DRAWING PRIMITIVES
// ============================================================================

void GCodeObjectThumbnailRenderer::world_to_pixel(const ObjectRenderContext& ctx, float wx,
                                                  float wy, int& px, int& py) {
    // Top-down view: X maps to pixel X, Y is flipped (max_y at top)
    px = static_cast<int>((wx - ctx.min_x) * ctx.scale + ctx.offset_x);
    py = static_cast<int>((ctx.max_y - wy) * ctx.scale + ctx.offset_y);
}

void GCodeObjectThumbnailRenderer::put_pixel(ObjectRenderContext& ctx, int x, int y,
                                             uint32_t color) {
    if (x < 0 || x >= ctx.width || y < 0 || y >= ctx.height) {
        return;
    }

    uint8_t* pixel = ctx.pixels.get() + y * ctx.stride + x * 4;

    // LVGL ARGB8888: byte order is B, G, R, A on little-endian
    pixel[0] = color & 0xFF;         // B
    pixel[1] = (color >> 8) & 0xFF;  // G
    pixel[2] = (color >> 16) & 0xFF; // R
    pixel[3] = (color >> 24) & 0xFF; // A
}

void GCodeObjectThumbnailRenderer::draw_line(ObjectRenderContext& ctx, int x0, int y0, int x1,
                                             int y1, uint32_t color) {
    // Bresenham's line algorithm
    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        put_pixel(ctx, x0, y0, color);

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

} // namespace helix::gcode
