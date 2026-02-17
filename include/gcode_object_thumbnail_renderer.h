// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gcode_parser.h"
#include "gcode_projection.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace helix::gcode {

/**
 * @brief Per-object rendered toolpath thumbnail
 *
 * Contains an ARGB8888 raw pixel buffer of a single object's toolpath,
 * rendered with isometric FRONT projection and scaled to fit within the thumbnail.
 */
struct ObjectThumbnail {
    std::string object_name;
    std::unique_ptr<uint8_t[]> pixels; ///< ARGB8888 raw pixels (little-endian: BGRA byte order)
    int width{0};
    int height{0};
    int stride{0}; ///< Bytes per row (width * 4, no padding)

    /// Check if thumbnail has valid pixel data
    bool is_valid() const {
        return pixels && width > 0 && height > 0;
    }

    /// Total size in bytes
    size_t byte_size() const {
        return static_cast<size_t>(height) * stride;
    }
};

/**
 * @brief Set of thumbnails for all objects in a print
 */
struct ObjectThumbnailSet {
    std::vector<ObjectThumbnail> thumbnails;

    /// Find thumbnail by object name (linear search, small N)
    const ObjectThumbnail* find(const std::string& name) const {
        for (const auto& t : thumbnails) {
            if (t.object_name == name)
                return &t;
        }
        return nullptr;
    }
};

/**
 * @brief Callback type for async thumbnail completion
 *
 * Called on the UI thread with the rendered thumbnail set.
 * Ownership of the set is transferred to the callback.
 */
using ThumbnailCompleteCallback = std::function<void(std::unique_ptr<ObjectThumbnailSet>)>;

/**
 * @brief Renders per-object toolpath thumbnails from parsed G-code
 *
 * Single-pass algorithm: iterates all segments once, dispatching each to the
 * correct object's pixel buffer based on segment.object_name. Runs in a
 * background thread with cancellation support.
 *
 * Usage:
 * @code
 *   auto renderer = std::make_unique<GCodeObjectThumbnailRenderer>();
 *   renderer->render_async(parsed_file, 40, 40, 0xFF26A69A,
 *       [](std::unique_ptr<ObjectThumbnailSet> set) {
 *           // Use thumbnails on UI thread
 *       });
 *   // Cancel: renderer.reset() or renderer->cancel()
 * @endcode
 *
 * Thread safety: Background thread only reads ParsedGCodeFile (immutable during print).
 * Raw pixel buffers use std::make_unique - no LVGL calls from background thread.
 * Results are marshaled to UI thread via helix::ui::queue_update().
 */
class GCodeObjectThumbnailRenderer {
  public:
    GCodeObjectThumbnailRenderer();
    ~GCodeObjectThumbnailRenderer();

    // Non-copyable
    GCodeObjectThumbnailRenderer(const GCodeObjectThumbnailRenderer&) = delete;
    GCodeObjectThumbnailRenderer& operator=(const GCodeObjectThumbnailRenderer&) = delete;

    /**
     * @brief Render thumbnails asynchronously in a background thread
     *
     * @param gcode Parsed G-code file (must remain valid until completion/cancel)
     * @param thumb_width Thumbnail width in pixels
     * @param thumb_height Thumbnail height in pixels
     * @param color ARGB8888 color for toolpath lines
     * @param callback Called on UI thread when rendering completes
     */
    void render_async(const ParsedGCodeFile* gcode, int thumb_width, int thumb_height,
                      uint32_t color, ThumbnailCompleteCallback callback);

    /**
     * @brief Render thumbnails synchronously (for testing)
     *
     * Same as render_async but blocks and returns the result directly.
     */
    std::unique_ptr<ObjectThumbnailSet> render_sync(const ParsedGCodeFile* gcode, int thumb_width,
                                                    int thumb_height, uint32_t color);

    /**
     * @brief Cancel in-progress rendering
     *
     * Sets cancellation flag and waits for background thread to finish.
     * Safe to call multiple times or when no render is in progress.
     */
    void cancel();

    /**
     * @brief Check if rendering is currently in progress
     */
    bool is_rendering() const {
        return rendering_.load(std::memory_order_relaxed);
    }

  private:
    /**
     * @brief Per-object rendering context used during the single-pass algorithm
     */
    struct ObjectRenderContext {
        std::string name;
        std::unique_ptr<uint8_t[]> pixels;
        int width{0};
        int height{0};
        int stride{0};

        // Shared projection params (FRONT view by default)
        ProjectionParams projection;

        // Bounding box Z/Y ranges for depth shading
        float z_min{0.0f};
        float z_max{1.0f};
        float y_min{0.0f};
        float y_max{1.0f};
    };

    /**
     * @brief Core render function (runs in background thread or synchronously)
     *
     * Single pass through all layers and segments. Each segment is dispatched
     * to its object's pixel buffer based on object_name.
     */
    std::unique_ptr<ObjectThumbnailSet> render_impl(const ParsedGCodeFile* gcode, int thumb_width,
                                                    int thumb_height, uint32_t color);

    /**
     * @brief Build render contexts from object AABBs
     */
    std::unordered_map<std::string, ObjectRenderContext>
    build_contexts(const ParsedGCodeFile* gcode, int thumb_width, int thumb_height);

    /**
     * @brief Draw line using Bresenham's algorithm to a raw pixel buffer
     */
    static void draw_line(ObjectRenderContext& ctx, int x0, int y0, int x1, int y1, uint32_t color);

    /**
     * @brief Write a single pixel to a raw ARGB8888 buffer
     */
    static void put_pixel(ObjectRenderContext& ctx, int x, int y, uint32_t color);

    /**
     * @brief Convert world coordinates to pixel coordinates for an object
     *
     * Uses shared projection from gcode_projection.h (FRONT view).
     */
    static void world_to_pixel(const ObjectRenderContext& ctx, float wx, float wy, float wz,
                               int& px, int& py);

    std::thread thread_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> rendering_{false};
};

} // namespace helix::gcode
