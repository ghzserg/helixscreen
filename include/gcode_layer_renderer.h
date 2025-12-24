// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2025 HelixScreen Contributors

#pragma once

#include "gcode_parser.h"
#include "gcode_streaming_controller.h"

#include <lvgl/lvgl.h>

#include <atomic>
#include <glm/glm.hpp>
#include <memory>
#include <thread>

namespace helix {
namespace gcode {

/**
 * @brief 2D orthographic layer renderer for G-code visualization
 *
 * Renders a single layer from a top-down view using direct X/Y → pixel
 * mapping. Optimized for low-power hardware (AD5M) without 3D matrix transforms.
 *
 * Features:
 * - Single layer rendering (fast, no depth sorting)
 * - Auto-fit to canvas bounds
 * - Toggle visibility of travels/supports
 * - Print progress integration (auto-follow current layer)
 *
 * Usage:
 * @code
 *   GCodeLayerRenderer renderer;
 *   renderer.set_gcode(&parsed_file);
 *   renderer.set_canvas_size(400, 400);
 *   renderer.auto_fit();
 *   renderer.set_current_layer(42);
 *   renderer.render(layer, clip_area);
 * @endcode
 */
class GCodeLayerRenderer {
  public:
    GCodeLayerRenderer();
    ~GCodeLayerRenderer();

    // =========================================================================
    // Data Source
    // =========================================================================

    /**
     * @brief Set G-code data source (full file mode)
     * @param gcode Pointer to parsed G-code file (not owned)
     *
     * Use this for files small enough to fit in memory. Clears any
     * streaming controller set via set_streaming_controller().
     */
    void set_gcode(const ParsedGCodeFile* gcode);

    /**
     * @brief Set streaming controller as data source (streaming mode)
     * @param controller Pointer to streaming controller (not owned)
     *
     * Use this for large files that should be streamed layer-by-layer.
     * Clears any parsed file set via set_gcode().
     *
     * In streaming mode:
     * - Layers are loaded on-demand via the controller
     * - Prefetching happens automatically for nearby layers
     * - Memory usage is bounded by the controller's cache budget
     */
    void set_streaming_controller(GCodeStreamingController* controller);

    /**
     * @brief Check if using streaming mode
     * @return true if streaming controller is set, false if using parsed file
     */
    bool is_streaming() const {
        return streaming_controller_ != nullptr;
    }

    /**
     * @brief Get current G-code data source (full file mode only)
     * @return Pointer to parsed G-code file, or nullptr if not set or in streaming mode
     */
    const ParsedGCodeFile* get_gcode() const {
        return gcode_;
    }

    /**
     * @brief Get streaming controller (streaming mode only)
     * @return Pointer to streaming controller, or nullptr if not in streaming mode
     */
    GCodeStreamingController* get_streaming_controller() const {
        return streaming_controller_;
    }

    // =========================================================================
    // Layer Selection
    // =========================================================================

    /**
     * @brief Set current layer to render
     * @param layer Layer index (0-based)
     */
    void set_current_layer(int layer);

    /**
     * @brief Get current layer index
     * @return Current layer (0-based)
     */
    int get_current_layer() const {
        return current_layer_;
    }

    /**
     * @brief Get total number of layers
     * @return Layer count, or 0 if no G-code loaded
     */
    int get_layer_count() const;

    // =========================================================================
    // Rendering
    // =========================================================================

    /**
     * @brief Render current layer to LVGL draw layer
     * @param layer LVGL draw layer (from draw event callback)
     * @param clip_area Clipping area for the render
     */
    void render(lv_layer_t* layer, const lv_area_t* clip_area);

    /**
     * @brief Check if renderer needs more frames to complete caching
     *
     * Progressive rendering renders N layers per frame to avoid UI blocking.
     * After calling render(), check this method - if true, the caller should
     * invalidate the widget to trigger another frame.
     *
     * @return true if more frames are needed to complete solid or ghost cache
     */
    bool needs_more_frames() const;

    /**
     * @brief Set canvas dimensions
     * @param width Canvas width in pixels
     * @param height Canvas height in pixels
     */
    void set_canvas_size(int width, int height);

    /**
     * @brief Set vertical content offset (shifts render center up/down)
     * @param offset_percent Offset as percentage of canvas height (-1.0 to 1.0)
     *                       Negative = shift content up, Positive = shift down
     *
     * Use this to account for overlapping UI elements (e.g., metadata overlay at bottom).
     * A value of -0.1 shifts the render center up by 10% of canvas height.
     */
    void set_content_offset_y(float offset_percent);

    // =========================================================================
    // Display Options
    // =========================================================================

    /**
     * @brief Show/hide travel moves (default: OFF)
     * @param show true to show travel moves
     */
    void set_show_travels(bool show) {
        show_travels_ = show;
    }

    /**
     * @brief Show/hide extrusion moves (default: ON)
     * @param show true to show extrusion moves
     */
    void set_show_extrusions(bool show) {
        show_extrusions_ = show;
    }

    /**
     * @brief Show/hide support structures (default: ON, if detectable)
     * @param show true to show support structures
     */
    void set_show_supports(bool show) {
        show_supports_ = show;
    }

    /** @brief Check if travel moves are shown */
    bool get_show_travels() const {
        return show_travels_;
    }

    /** @brief Check if support structures are shown */
    bool get_show_supports() const {
        return show_supports_;
    }

    /**
     * @brief Enable/disable depth shading for 3D-like appearance (default: ON)
     *
     * When enabled in FRONT view:
     * - Lines are brighter at top, darker at bottom (simulates top-down lighting)
     * - Older layers slightly fade (focus on current print progress)
     *
     * @param enable true to enable depth shading
     */
    void set_depth_shading(bool enable) {
        depth_shading_ = enable;
    }

    /** @brief Check if depth shading is enabled */
    bool get_depth_shading() const {
        return depth_shading_;
    }

    /**
     * @brief Enable/disable ghost mode (shows faded preview of remaining layers)
     * @param enable true to enable ghost mode (default: ON)
     */
    void set_ghost_mode(bool enable) {
        ghost_mode_enabled_ = enable;
    }

    /** @brief Check if ghost mode is enabled */
    bool get_ghost_mode() const {
        return ghost_mode_enabled_;
    }

    /**
     * @brief Get progress of streaming ghost build
     *
     * In streaming mode, ghost is built progressively in background.
     * Returns 0.0 to 1.0 indicating build progress.
     *
     * @return Progress fraction, or 1.0 if complete/not applicable
     */
    float get_ghost_build_progress() const;

    /**
     * @brief Check if streaming ghost build is complete
     * @return true if ghost is fully built (all layers rendered)
     */
    bool is_ghost_build_complete() const;

    /**
     * @brief Check if streaming ghost build is running
     * @return true if background ghost build is in progress
     */
    bool is_ghost_build_running() const;

    /**
     * @brief View mode for 2D layer renderer
     */
    enum class ViewMode {
        TOP_DOWN, ///< X/Y plane from above (default)
        FRONT,    ///< X/Z plane - side profile showing all layers
        ISOMETRIC ///< X/Y plane with isometric projection
    };

    /**
     * @brief Set view mode
     * @param mode View mode (TOP_DOWN, FRONT, or ISOMETRIC)
     */
    void set_view_mode(ViewMode mode) {
        view_mode_ = mode;
        bounds_valid_ = false; // Recompute scale for new projection
    }

    /** @brief Get current view mode */
    ViewMode get_view_mode() const {
        return view_mode_;
    }

    // =========================================================================
    // Colors
    // =========================================================================

    /**
     * @brief Set extrusion color (overrides theme)
     * @param color Color for extrusion moves
     */
    void set_extrusion_color(lv_color_t color);

    /**
     * @brief Set travel color (overrides theme)
     * @param color Color for travel moves
     */
    void set_travel_color(lv_color_t color);

    /**
     * @brief Set support color (overrides theme)
     * @param color Color for support structures
     */
    void set_support_color(lv_color_t color);

    /**
     * @brief Reset all colors to theme defaults
     */
    void reset_colors();

    // =========================================================================
    // Viewport Control
    // =========================================================================

    /**
     * @brief Auto-fit all layers to canvas
     *
     * Computes scale and offset to fit the entire model's bounding box
     * within the canvas with 5% padding.
     */
    void auto_fit();

    /**
     * @brief Fit current layer to canvas
     *
     * Computes scale and offset to fit only the current layer's bounding box.
     */
    void fit_layer();

    /**
     * @brief Set zoom scale manually
     * @param scale Pixels per mm
     */
    void set_scale(float scale);

    /**
     * @brief Set viewport offset manually
     * @param x Center X in world coordinates
     * @param y Center Y in world coordinates
     */
    void set_offset(float x, float y);

    // =========================================================================
    // Layer Information
    // =========================================================================

    /**
     * @brief Information about the current layer
     */
    struct LayerInfo {
        int layer_number;       ///< Layer index (0-based)
        float z_height;         ///< Z-height in mm
        size_t segment_count;   ///< Total segments in layer
        size_t extrusion_count; ///< Number of extrusion segments
        size_t travel_count;    ///< Number of travel segments
        bool has_supports;      ///< True if layer contains support structures
    };

    /**
     * @brief Get information about current layer
     * @return LayerInfo struct with layer details
     */
    LayerInfo get_layer_info() const;

    /**
     * @brief Check if G-code has detectable support structures
     * @return true if supports can be identified
     */
    bool has_support_detection() const;

  private:
    // =========================================================================
    // Internal Rendering
    // =========================================================================

    /**
     * @brief Render a single segment
     * @param layer LVGL draw layer
     * @param seg Toolpath segment to render
     * @param ghost If true, render in ghost style (grey, for preview)
     */
    void render_segment(lv_layer_t* layer, const ToolpathSegment& seg, bool ghost = false);

    /**
     * @brief Convert world coordinates to screen coordinates
     * @param x World X coordinate (mm)
     * @param y World Y coordinate (mm)
     * @param z World Z coordinate (mm) - used for FRONT view
     * @return Screen coordinates (pixels)
     */
    glm::ivec2 world_to_screen(float x, float y, float z = 0.0f) const;

    // =========================================================================
    // Transformation Parameters (for thread-safe coordinate conversion)
    // =========================================================================

    /**
     * @brief Captured transformation state for thread-safe rendering
     *
     * This struct captures all parameters needed for world_to_screen conversion.
     * Background threads capture this at start to ensure consistent rendering
     * even if main thread modifies renderer state during the operation.
     */
    struct TransformParams {
        ViewMode view_mode;
        float scale;
        float offset_x;
        float offset_y;
        float offset_z;
        int canvas_width;
        int canvas_height;
        float content_offset_y_percent; // CRITICAL: must be included for alignment!
    };

    /**
     * @brief Capture current transformation parameters (thread-safe snapshot)
     * @return TransformParams struct with current values
     */
    TransformParams capture_transform_params() const;

    /**
     * @brief Convert world coordinates to screen using captured parameters
     *
     * This is the single source of truth for coordinate conversion. Both
     * world_to_screen() and background ghost rendering use this method
     * to ensure perfect alignment between solid and ghost layers.
     *
     * @param params Captured transformation parameters
     * @param x World X coordinate (mm)
     * @param y World Y coordinate (mm)
     * @param z World Z coordinate (mm) - used for FRONT view
     * @return Screen coordinates (pixels, no widget offset applied)
     */
    static glm::ivec2 world_to_screen_raw(const TransformParams& params, float x, float y,
                                          float z = 0.0f);

    /**
     * @brief Check if a segment is a support structure
     * @param seg Segment to check
     * @return true if segment is part of support
     */
    bool is_support_segment(const ToolpathSegment& seg) const;

    /**
     * @brief Check if a segment should be rendered based on visibility settings
     * @param seg Segment to check
     * @return true if segment should be rendered
     */
    bool should_render_segment(const ToolpathSegment& seg) const;

    /**
     * @brief Get line color for a segment
     * @param seg Segment to get color for
     * @return LVGL color
     */
    lv_color_t get_segment_color(const ToolpathSegment& seg) const;

    // Data source (exactly one should be non-null)
    const ParsedGCodeFile* gcode_ = nullptr;
    GCodeStreamingController* streaming_controller_ = nullptr;
    int current_layer_ = 0;

    // Canvas dimensions
    int canvas_width_ = 400;
    int canvas_height_ = 400;
    float content_offset_y_percent_ = 0.0f; // Vertical content offset (-1.0 to 1.0)

    // Viewport transform (world → screen)
    float scale_ = 1.0f;
    float offset_x_ = 0.0f; // World-space center X
    float offset_y_ = 0.0f; // World-space center Y
    float offset_z_ = 0.0f; // World-space center Z (for FRONT view)

    // Display options
    bool show_travels_ = false;
    bool show_extrusions_ = true;
    bool show_supports_ = true;
    bool depth_shading_ = true;            // Enabled by default for 3D-like appearance
    ViewMode view_mode_ = ViewMode::FRONT; // Default to front view

    // Colors
    lv_color_t color_extrusion_;
    lv_color_t color_travel_;
    lv_color_t color_support_;
    bool use_custom_extrusion_color_ = false;
    bool use_custom_travel_color_ = false;
    bool use_custom_support_color_ = false;

    // Cached bounds
    float bounds_min_x_ = 0.0f;
    float bounds_max_x_ = 0.0f;
    float bounds_min_y_ = 0.0f;
    float bounds_max_y_ = 0.0f;
    float bounds_min_z_ = 0.0f;
    float bounds_max_z_ = 0.0f;
    bool bounds_valid_ = false;

    // Widget screen offset (set during render())
    int widget_offset_x_ = 0;
    int widget_offset_y_ = 0;

    // Render statistics (for debugging)
    int last_rendered_layer_ = -1;
    uint32_t last_render_time_ms_ = 0;
    size_t last_segment_count_ = 0;

    // Incremental render cache - paint new layers on top of previous (SOLID)
    // Note: We only use draw buffers (no canvas widgets) to avoid clip area
    // contamination from overlays/toasts on lv_layer_top().
    lv_draw_buf_t* cache_buf_ = nullptr;
    int cached_up_to_layer_ = -1; // Highest layer rendered in cache
    int cached_width_ = 0;        // Dimensions cache was built for
    int cached_height_ = 0;

    // Ghost cache - all layers rendered once at reduced opacity
    // Note: We only use draw buffers (no canvas widgets) to avoid clip area
    // contamination from overlays/toasts on lv_layer_top().
    lv_draw_buf_t* ghost_buf_ = nullptr;
    bool ghost_cache_valid_ = false;
    bool ghost_mode_enabled_ = true; // Enable ghost mode by default
    int ghost_rendered_up_to_ = -1;  // Progress tracker for progressive ghost rendering

    // Progressive rendering - render N layers per frame to avoid blocking UI
    // These are defaults; actual values come from config or adaptive adjustment
    static constexpr int DEFAULT_LAYERS_PER_FRAME = 15;
    static constexpr int MIN_LAYERS_PER_FRAME = 1;
    static constexpr int MAX_LAYERS_PER_FRAME = 100;
    static constexpr int DEFAULT_ADAPTIVE_TARGET_MS = 16; // ~60 FPS

    // Constrained device limits (AD5M, low-RAM embedded < 256MB)
    static constexpr int CONSTRAINED_START_LPF = 5;
    static constexpr int CONSTRAINED_MAX_LPF = 15;
    static constexpr float CONSTRAINED_GROWTH_CAP = 1.3f; // vs 2.0f normal

    int layers_per_frame_{DEFAULT_LAYERS_PER_FRAME}; ///< Current layers per frame (may be adaptive)
    int config_layers_per_frame_{0};                 ///< Config value (0 = adaptive)
    int adaptive_target_ms_{DEFAULT_ADAPTIVE_TARGET_MS}; ///< Target render time for adaptive mode
    uint32_t last_frame_render_ms_{0}; ///< Render time of last frame (for adaptive)

    // Device-aware limits
    bool is_constrained_device_{false};               ///< True if device has < 256MB RAM
    int max_layers_per_frame_{MAX_LAYERS_PER_FRAME};  ///< Device-adjusted max (15 on constrained)

    // Warm-up frames: skip heavy rendering for first N frames to let panel layout complete
    static constexpr int WARMUP_FRAMES = 2;
    int warmup_frames_remaining_{WARMUP_FRAMES};      ///< Countdown before heavy render starts

    /// Load config values from helixconfig.json
    void load_config();

    /// Adjust layers_per_frame based on last render time (when config_layers_per_frame_ == 0)
    void adapt_layers_per_frame();

    void invalidate_cache();
    void ensure_cache(int width, int height);
    void render_layers_to_cache(int from_layer, int to_layer);
    void blit_cache(lv_layer_t* target);
    void destroy_cache();

    // Ghost cache methods (LVGL-based, for main thread progressive rendering)
    void ensure_ghost_cache(int width, int height);
    void render_ghost_layers(int from_layer, int to_layer);
    void blit_ghost_cache(lv_layer_t* target);
    void destroy_ghost_cache();

    // =========================================================================
    // Background Thread Ghost Rendering
    // =========================================================================
    // LVGL is not thread-safe, so background thread renders to a raw pixel
    // buffer using software Bresenham line drawing, then copies to LVGL buffer
    // on main thread when complete.

    /// Raw pixel buffer for background thread rendering (ARGB8888)
    std::unique_ptr<uint8_t[]> ghost_raw_buffer_;
    int ghost_raw_width_ = 0;
    int ghost_raw_height_ = 0;
    int ghost_raw_stride_ = 0; // Bytes per row

    /// Background thread management (works for both streaming and non-streaming modes)
    std::thread ghost_thread_;
    std::atomic<bool> ghost_thread_cancel_{false};
    std::atomic<bool> ghost_thread_running_{false};
    std::atomic<bool> ghost_thread_ready_{false}; // True when raw buffer is complete

    /// Start background ghost rendering (called when new gcode loaded)
    void start_background_ghost_render();

    /// Cancel any in-progress background ghost render
    void cancel_background_ghost_render();

    /// Background thread entry point (renders all layers to raw buffer)
    void background_ghost_render_thread();

    /// Copy completed raw buffer to LVGL ghost_buf_ (called on main thread)
    void copy_raw_to_ghost_buf();

    /// Software Bresenham line drawing to raw ARGB8888 buffer
    void draw_line_bresenham(int x0, int y0, int x1, int y1, uint32_t color);

    /// Blend a pixel with alpha into the raw buffer
    void blend_pixel(int x, int y, uint32_t color);
};

} // namespace gcode
} // namespace helix
