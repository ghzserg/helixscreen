// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_gcode_viewer.h"

#include "ui_update_queue.h"
#include "ui_utils.h"

#include "gcode_camera.h"
#include "gcode_layer_renderer.h"
#include "gcode_parser.h"
#include "gcode_streaming_config.h"
#include "gcode_streaming_controller.h"
#include "memory_utils.h"
#include "theme_manager.h"

#include <filesystem>

#ifdef ENABLE_TINYGL_3D
#include "gcode_tinygl_renderer.h"
#else
#include "gcode_renderer.h"
#endif

// FPS tracking constants (for diagnostic logging, not mode selection)
constexpr size_t GCODE_FPS_WINDOW_SIZE = 10; // Rolling window of frame times

#include <lvgl/src/xml/lv_xml_parser.h>
#include <lvgl/src/xml/parsers/lv_xml_obj_parser.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_set>

/**
 * @brief GCode Viewer widget state with proper RAII thread management
 *
 * Manages the lifecycle of async geometry building threads safely.
 * The destructor signals cancellation and waits for threads to complete,
 * preventing use-after-free crashes during shutdown.
 */
class GCodeViewerState {
  public:
    GCodeViewerState() {
        camera_ = std::make_unique<helix::gcode::GCodeCamera>();
#ifdef ENABLE_TINYGL_3D
        renderer_ = std::make_unique<helix::gcode::GCodeTinyGLRenderer>();
        spdlog::debug("[GCode Viewer] TinyGL 3D renderer available");
#else
        renderer_ = std::make_unique<helix::gcode::GCodeRenderer>();
        spdlog::debug("[GCode Viewer] Using LVGL 2D renderer (TinyGL disabled)");
#endif

        // Check HELIX_GCODE_MODE env var for render mode override
        // Default is 2D (TinyGL is too slow for production on ALL platforms)
        const char* mode_env = std::getenv("HELIX_GCODE_MODE");
        if (mode_env) {
            if (std::strcmp(mode_env, "3D") == 0) {
#ifdef ENABLE_TINYGL_3D
                render_mode_ = GCODE_VIEWER_RENDER_3D;
                spdlog::info("[GCode Viewer] HELIX_GCODE_MODE=3D: forcing 3D TinyGL renderer");
#else
                spdlog::warn("[GCode Viewer] HELIX_GCODE_MODE=3D ignored: TinyGL not available");
                render_mode_ = GCODE_VIEWER_RENDER_2D_LAYER;
#endif
            } else if (std::strcmp(mode_env, "2D") == 0) {
                render_mode_ = GCODE_VIEWER_RENDER_2D_LAYER;
                spdlog::info("[GCode Viewer] HELIX_GCODE_MODE=2D: using 2D layer renderer");
            } else {
                spdlog::warn("[GCode Viewer] Unknown HELIX_GCODE_MODE='{}', using 2D", mode_env);
                render_mode_ = GCODE_VIEWER_RENDER_2D_LAYER;
            }
        } else {
            // Default: 2D layer renderer (TinyGL is ~3-4 FPS everywhere)
            render_mode_ = GCODE_VIEWER_RENDER_2D_LAYER;
            spdlog::debug("[GCode Viewer] Default render mode: 2D layer");
        }
    }

    ~GCodeViewerState() {
        // RAII cleanup: signal cancellation and wait for thread
        cancel_build();

        // Clean up LVGL timer if pending
        // Guard against LVGL shutdown - timer may already be destroyed
        if (long_press_timer_ && lv_is_initialized()) {
            lv_timer_delete(long_press_timer_);
            long_press_timer_ = nullptr;
        }
    }

    // Non-copyable, non-movable (prevents accidental thread ownership issues)
    GCodeViewerState(const GCodeViewerState&) = delete;
    GCodeViewerState& operator=(const GCodeViewerState&) = delete;
    GCodeViewerState(GCodeViewerState&&) = delete;
    GCodeViewerState& operator=(GCodeViewerState&&) = delete;

    // ========================================================================
    // Async Build Management
    // ========================================================================

    /**
     * @brief Check if a build operation can be cancelled
     * @return true if cancellation was requested
     */
    bool is_cancelled() const {
        return cancel_flag_.load();
    }

    /**
     * @brief Start an async geometry build operation
     *
     * Cancels any existing build, then launches a new thread.
     *
     * @param build_func Function to execute in background thread
     */
    void start_build(std::function<void()> build_func) {
        // Cancel and wait for any existing build
        cancel_build();

        // Reset state for new build
        cancel_flag_.store(false);
        building_.store(true);

        // Launch new thread
        build_thread_ = std::thread([this, func = std::move(build_func)]() {
            func();
            building_.store(false);
        });
    }

    /**
     * @brief Cancel any in-progress build and wait for completion
     *
     * Safe to call multiple times. Blocks until thread exits.
     */
    void cancel_build() {
        cancel_flag_.store(true);
        if (build_thread_.joinable()) {
            build_thread_.join();
        }
    }

    bool is_building() const {
        return building_.load();
    }

    // ========================================================================
    // Public State (accessed by static callbacks)
    // ========================================================================

    // G-code data
    std::unique_ptr<helix::gcode::ParsedGCodeFile> gcode_file;
    gcode_viewer_state_enum_t viewer_state{GCODE_VIEWER_STATE_EMPTY};

    // Rendering components (exposed for callbacks)
    std::unique_ptr<helix::gcode::GCodeCamera> camera_;
#ifdef ENABLE_TINYGL_3D
    std::unique_ptr<helix::gcode::GCodeTinyGLRenderer> renderer_;
#else
    std::unique_ptr<helix::gcode::GCodeRenderer> renderer_;
#endif

    // Gesture state
    bool is_dragging{false};
    lv_point_t drag_start{0, 0};
    lv_point_t last_drag_pos{0, 0};

    // Selection and exclusion state
    std::unordered_set<std::string> selected_objects;
    std::unordered_set<std::string> excluded_objects;

    // Callbacks
    gcode_viewer_object_tap_callback_t object_tap_callback{nullptr};
    void* object_tap_user_data{nullptr};
    gcode_viewer_object_long_press_callback_t object_long_press_callback{nullptr};
    void* object_long_press_user_data{nullptr};
    gcode_viewer_load_callback_t load_callback{nullptr};
    void* load_callback_user_data{nullptr};

    // Long-press state
    lv_timer_t* long_press_timer_{nullptr};
    bool long_press_fired{false};
    std::string long_press_object_name;

    // Rendering settings
    bool use_filament_color{true};
    bool first_render{true};
    bool rendering_paused_{
        false}; ///< When true, draw_cb skips rendering (for visibility optimization)

    // Loading UI elements (managed by async load function)
    lv_obj_t* loading_container{nullptr};
    lv_obj_t* loading_spinner{nullptr};
    lv_obj_t* loading_label{nullptr};

    // Ghost build progress label (streaming mode only)
    lv_obj_t* ghost_progress_label_{nullptr};

    // ========================================================================
    // Render Mode (Phase 5: 2D Layer View)
    // ========================================================================

    /// 2D orthographic layer renderer (default for all platforms)
    std::unique_ptr<helix::gcode::GCodeLayerRenderer> layer_renderer_2d_;

    /// Streaming controller for large files (Phase 6)
    /// When set, renderer uses this instead of gcode_file for layer data.
    /// Mutually exclusive with gcode_file - exactly one should hold data.
    std::unique_ptr<helix::gcode::GCodeStreamingController> streaming_controller_;

    /// Print progress layer (set via ui_gcode_viewer_set_print_progress)
    /// -1 means "show all layers" (preview mode), >= 0 means "show up to this layer"
    int print_progress_layer_{-1};

    /// Content offset (stored to apply when 2D renderer is lazily created)
    float content_offset_y_percent_{0.0f};

    /// Render mode setting - set by constructor based on HELIX_GCODE_MODE env var
    /// Default is 2D_LAYER (TinyGL is too slow for production use everywhere)
    gcode_viewer_render_mode_t render_mode_{GCODE_VIEWER_RENDER_2D_LAYER};

    /// Helper to check if currently using 2D layer renderer
    /// AUTO mode now defaults to 2D (no FPS-based detection)
    bool is_using_2d_mode() const {
        // Only GCODE_VIEWER_RENDER_3D uses 3D renderer
        // AUTO and 2D_LAYER both use 2D layer renderer
        return render_mode_ != GCODE_VIEWER_RENDER_3D;
    }

    // FPS tracking kept for debugging/diagnostics but not used for mode selection
    float fps_samples_[GCODE_FPS_WINDOW_SIZE]{0};
    size_t fps_sample_index_{0};
    size_t fps_sample_count_{0};

    /// Record a frame time for FPS tracking (diagnostic only)
    void record_frame_time(float ms) {
        fps_samples_[fps_sample_index_] = ms;
        fps_sample_index_ = (fps_sample_index_ + 1) % GCODE_FPS_WINDOW_SIZE;
        if (fps_sample_count_ < GCODE_FPS_WINDOW_SIZE) {
            fps_sample_count_++;
        }
    }

    /// Calculate average FPS from sample buffer (diagnostic only)
    float get_average_fps() const {
        if (fps_sample_count_ == 0)
            return 0.0f;
        float total_ms = 0.0f;
        for (size_t i = 0; i < fps_sample_count_; i++) {
            total_ms += fps_samples_[i];
        }
        float avg_ms = total_ms / static_cast<float>(fps_sample_count_);
        return (avg_ms > 0.0f) ? (1000.0f / avg_ms) : 0.0f;
    }

    /// Check if we have enough FPS data (diagnostic only)
    bool has_enough_fps_data() const {
        return fps_sample_count_ >= GCODE_FPS_WINDOW_SIZE;
    }

    // Per-widget FPS logging state (avoid static variables that would be shared
    // between multiple gcode_viewer instances)
    int fps_log_frame_count_{0};
    int fps_actual_render_count_{0};
    float fps_render_time_avg_ms_{0.0f};

  private:
    std::thread build_thread_;
    std::atomic<bool> building_{false};
    std::atomic<bool> cancel_flag_{false};
};

// Type alias for compatibility with existing code
using gcode_viewer_state_t = GCodeViewerState;

// Helper: Get widget state from object
static gcode_viewer_state_t* get_state(lv_obj_t* obj) {
    return static_cast<gcode_viewer_state_t*>(lv_obj_get_user_data(obj));
}

// Helper: Check if viewer has any G-code data (full file or streaming)
static bool has_gcode_data(const gcode_viewer_state_t* st) {
    return st->gcode_file || (st->streaming_controller_ && st->streaming_controller_->is_open());
}

// ==============================================
// Event Callbacks
// ==============================================

/**
 * @brief Main draw callback - renders G-code using custom renderer
 *
 * Dispatches to either the 3D TinyGL renderer or the 2D layer renderer
 * based on current render mode and AUTO fallback state.
 */
static void gcode_viewer_draw_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    gcode_viewer_state_t* st = get_state(obj);

    if (!st || !layer) {
        return;
    }

    // Check if rendering is paused (visibility optimization)
    if (st->rendering_paused_) {
        spdlog::trace("[GCode Viewer] draw_cb skipped (rendering paused)");
        return;
    }

    // If no G-code loaded, draw placeholder message
    // In streaming mode, gcode_file is null but streaming_controller_ is set
    bool has_gcode =
        st->gcode_file || (st->streaming_controller_ && st->streaming_controller_->is_open());
    if (st->viewer_state != GCODE_VIEWER_STATE_LOADED || !has_gcode) {
        return;
    }

    // On first render after async load, skip rendering to avoid blocking
    if (st->first_render) {
        spdlog::debug(
            "[GCode Viewer] First draw after async load - skipping render, will render on timer");
        return;
    }

    // Get widget's absolute screen coordinates for drawing
    lv_area_t widget_coords;
    lv_obj_get_coords(obj, &widget_coords);

    // Measure actual render time for FPS calculation
    auto render_start = std::chrono::high_resolution_clock::now();

    // Dispatch to appropriate renderer based on mode
    if (st->is_using_2d_mode()) {
        // 2D Layer Renderer (orthographic top-down view)
        if (!st->layer_renderer_2d_) {
            // Lazy initialization of 2D renderer (non-streaming mode only)
            // In streaming mode, layer_renderer_2d_ is already initialized in open_file_async
            // callback
            if (!st->gcode_file) {
                spdlog::error(
                    "[GCode Viewer] 2D lazy init but no gcode_file - streaming init failed?");
                return;
            }
            st->layer_renderer_2d_ = std::make_unique<helix::gcode::GCodeLayerRenderer>();
            st->layer_renderer_2d_->set_gcode(st->gcode_file.get());
            int width = lv_area_get_width(&widget_coords);
            int height = lv_area_get_height(&widget_coords);
            st->layer_renderer_2d_->set_canvas_size(width, height);
            st->layer_renderer_2d_->auto_fit();

            // Apply filament color from G-code metadata if available
            if (st->use_filament_color && st->gcode_file->filament_color_hex.length() >= 2) {
                lv_color_t color = lv_color_hex(static_cast<uint32_t>(
                    std::strtol(st->gcode_file->filament_color_hex.c_str() + 1, nullptr, 16)));
                st->layer_renderer_2d_->set_extrusion_color(color);
                spdlog::info("[GCode Viewer] 2D renderer using filament color: {}",
                             st->gcode_file->filament_color_hex);
            }

            // Apply any stored content offset
            if (st->content_offset_y_percent_ != 0.0f) {
                st->layer_renderer_2d_->set_content_offset_y(st->content_offset_y_percent_);
            }

            spdlog::info("[GCode Viewer] Initialized 2D layer renderer ({}x{})", width, height);
        }

        // Use stored print progress layer (set via ui_gcode_viewer_set_print_progress)
        // Consistent with 3D renderer:
        //   - >= 0: Show layers 0 to current_layer (print progress mode)
        //   - < 0:  Show all layers (preview mode)
        int current_layer = st->print_progress_layer_;
        if (current_layer < 0) {
            // Preview mode: show all layers
            int max_layer = st->layer_renderer_2d_->get_layer_count() - 1;
            current_layer = std::max(0, max_layer);
        }
        st->layer_renderer_2d_->set_current_layer(current_layer);

        // Render 2D layer view
        st->layer_renderer_2d_->render(layer, &widget_coords);

        // Check if progressive rendering needs more frames
        // This drives ghost cache and solid cache completion
        if (st->layer_renderer_2d_->needs_more_frames()) {
            // IMPORTANT: Cannot call lv_obj_invalidate() during draw callback!
            // LVGL asserts if we invalidate while rendering_in_progress is true.
            // Use ui_async_call() to schedule invalidation after render completes.
            ui_async_call(
                [](void* user_data) {
                    lv_obj_t* widget = static_cast<lv_obj_t*>(user_data);
                    if (lv_obj_is_valid(widget)) {
                        lv_obj_invalidate(widget);
                    }
                },
                obj);
        }

        // Update ghost build progress label (streaming mode)
        // IMPORTANT: Cannot create/delete/modify objects during draw callback!
        // Use ui_queue_update() to defer all label operations to after render completes.
        if (st->layer_renderer_2d_->is_ghost_build_running()) {
            int percent =
                static_cast<int>(st->layer_renderer_2d_->get_ghost_build_progress() * 100.0f);
            // Capture needed data for deferred update
            struct GhostProgressUpdate {
                lv_obj_t* viewer;
                int percent;
            };
            auto update = std::make_unique<GhostProgressUpdate>(GhostProgressUpdate{obj, percent});
            ui_queue_update<GhostProgressUpdate>(std::move(update), [](GhostProgressUpdate* u) {
                if (!lv_obj_is_valid(u->viewer)) {
                    return;
                }
                auto* state = static_cast<GCodeViewerState*>(lv_obj_get_user_data(u->viewer));
                if (!state) {
                    return;
                }
                // Create label if needed
                if (!state->ghost_progress_label_) {
                    state->ghost_progress_label_ = lv_label_create(u->viewer);
                    lv_obj_set_style_text_color(state->ghost_progress_label_,
                                                theme_manager_get_color("text_muted"),
                                                LV_PART_MAIN);
                    lv_obj_set_style_text_font(state->ghost_progress_label_,
                                               theme_manager_get_font("font_small"), LV_PART_MAIN);
                    lv_obj_align(state->ghost_progress_label_, LV_ALIGN_BOTTOM_LEFT, 8, -8);
                }
                static char text[32];
                lv_snprintf(text, sizeof(text), "Building preview: %d%%", u->percent);
                lv_label_set_text(state->ghost_progress_label_, text);
            });
        } else if (st->ghost_progress_label_) {
            // Defer label deletion to after render
            lv_obj_t* label_to_delete = st->ghost_progress_label_;
            st->ghost_progress_label_ = nullptr; // Clear reference immediately
            ui_async_call(
                [](void* user_data) {
                    lv_obj_t* widget = static_cast<lv_obj_t*>(user_data);
                    lv_obj_safe_delete(widget);
                },
                label_to_delete);
        }
    } else {
        // 3D TinyGL Renderer (isometric ribbon view)
        st->renderer_->render(layer, *st->gcode_file, *st->camera_, &widget_coords);
    }

    auto render_end = std::chrono::high_resolution_clock::now();
    auto render_duration_us =
        std::chrono::duration_cast<std::chrono::microseconds>(render_end - render_start).count();

    // FPS tracking for AUTO mode evaluation
    static constexpr float MIN_ACTUAL_RENDER_MS = 2.0f;
    float render_time_ms = render_duration_us / 1000.0f;

    // Record frame time for AUTO mode evaluation (only count actual renders)
    if (render_time_ms > MIN_ACTUAL_RENDER_MS) {
        st->record_frame_time(render_time_ms);
    }

    // Periodic FPS logging (every 30 frames) - use per-widget state to avoid
    // corruption when multiple gcode_viewer widgets exist
    constexpr float FPS_ALPHA = 0.1f;

    if (render_time_ms > MIN_ACTUAL_RENDER_MS) {
        st->fps_render_time_avg_ms_ =
            (st->fps_render_time_avg_ms_ == 0.0f)
                ? render_time_ms
                : (FPS_ALPHA * render_time_ms + (1.0f - FPS_ALPHA) * st->fps_render_time_avg_ms_);
        st->fps_actual_render_count_++;
    }

    if (++st->fps_log_frame_count_ >= 30) {
        if (st->fps_actual_render_count_ > 0 &&
            st->fps_render_time_avg_ms_ > MIN_ACTUAL_RENDER_MS) {
            float avg_fps = 1000.0f / st->fps_render_time_avg_ms_;
            const char* mode_str = st->is_using_2d_mode() ? "2D" : "3D";
            spdlog::debug("[GCode Viewer] {} mode: {:.1f}ms ({:.1f}fps) over {} frames", mode_str,
                          st->fps_render_time_avg_ms_, avg_fps, st->fps_actual_render_count_);
        }
        st->fps_log_frame_count_ = 0;
        st->fps_actual_render_count_ = 0;
    }
}

// Long-press threshold in milliseconds
constexpr uint32_t LONG_PRESS_THRESHOLD_MS = 500;

/**
 * @brief Timer callback for long-press detection
 *
 * Fires after LONG_PRESS_THRESHOLD_MS if user hasn't moved the finger.
 * Picks the object under the initial press position and invokes the long-press callback.
 */
static void long_press_timer_cb(lv_timer_t* timer) {
    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_timer_get_user_data(timer));
    gcode_viewer_state_t* st = get_state(obj);

    if (!st || !has_gcode_data(st))
        return;

    // Timer fired - this is a long-press
    st->long_press_fired = true;

    // Delete the timer (one-shot)
    lv_timer_delete(timer);
    st->long_press_timer_ = nullptr;

    // Pick object at the original press position
    const char* picked = ui_gcode_viewer_pick_object(obj, st->drag_start.x, st->drag_start.y);

    if (picked && picked[0] != '\0') {
        st->long_press_object_name = picked;

        // Highlight the object to provide visual feedback
        st->selected_objects.clear();
        st->selected_objects.insert(picked);
        ui_gcode_viewer_set_highlighted_objects(obj, st->selected_objects);

        spdlog::info("[GCode Viewer] Long-press on object '{}'", picked);

        // Invoke long-press callback
        if (st->object_long_press_callback) {
            st->object_long_press_callback(obj, picked, st->object_long_press_user_data);
        }
    } else {
        st->long_press_object_name.clear();
        spdlog::debug("[GCode Viewer] Long-press at ({}, {}) - no object found", st->drag_start.x,
                      st->drag_start.y);

        // Invoke callback with empty string to indicate long-press on empty space
        if (st->object_long_press_callback) {
            st->object_long_press_callback(obj, "", st->object_long_press_user_data);
        }
    }
}

/**
 * @brief Touch press callback - start drag gesture and long-press timer
 */
static void gcode_viewer_press_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    gcode_viewer_state_t* st = get_state(obj);

    if (!st)
        return;

    lv_indev_t* indev = lv_indev_active();
    if (!indev)
        return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    st->is_dragging = true;
    st->drag_start = point;
    st->last_drag_pos = point;
    st->long_press_fired = false;
    st->long_press_object_name.clear();

    spdlog::trace("[GCode Viewer] PRESSED at ({}, {}), is_dragging={}", point.x, point.y,
                  st->is_dragging);

    // Enter interaction mode for reduced resolution during drag
    if (st->renderer_) {
        st->renderer_->set_interaction_mode(true);
    }

    // Start long-press timer if callback is registered
    if (st->object_long_press_callback && has_gcode_data(st)) {
        // Cancel any existing timer
        if (st->long_press_timer_) {
            lv_timer_delete(st->long_press_timer_);
        }
        // Start new timer for long-press detection
        st->long_press_timer_ = lv_timer_create(long_press_timer_cb, LONG_PRESS_THRESHOLD_MS, obj);
        lv_timer_set_repeat_count(st->long_press_timer_, 1); // One-shot timer
    }

    spdlog::trace("[GCode Viewer] Press at ({}, {})", point.x, point.y);
}

// Movement threshold to cancel long-press (same as click threshold)
constexpr int LONG_PRESS_MOVE_THRESHOLD = 10;

/**
 * @brief Touch pressing callback - handle drag for camera rotation
 *
 * Also cancels long-press timer if user moves beyond threshold.
 */
static void gcode_viewer_pressing_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    gcode_viewer_state_t* st = get_state(obj);

    if (!st || !st->is_dragging)
        return;

    // In 2D mode, no camera rotation - skip drag handling entirely.
    // This also prevents mouse micro-jitter from cancelling the long-press timer.
    if (st->is_using_2d_mode())
        return;

    lv_indev_t* indev = lv_indev_active();
    if (!indev)
        return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    // Check if movement exceeds threshold - cancel long-press timer
    int total_dx = abs(point.x - st->drag_start.x);
    int total_dy = abs(point.y - st->drag_start.y);

    if ((total_dx >= LONG_PRESS_MOVE_THRESHOLD || total_dy >= LONG_PRESS_MOVE_THRESHOLD) &&
        st->long_press_timer_) {
        // User started dragging - cancel long-press
        lv_timer_delete(st->long_press_timer_);
        st->long_press_timer_ = nullptr;
        spdlog::trace("[GCode Viewer] Long-press cancelled due to movement");
    }

    // Calculate delta from last position
    int dx = point.x - st->last_drag_pos.x;
    int dy = point.y - st->last_drag_pos.y;

    if (dx != 0 || dy != 0) {
        // Convert pixel movement to rotation angles
        // Scale factor: ~0.5 degrees per pixel
        float delta_azimuth = dx * 0.5f;
        float delta_elevation = -dy * 0.5f; // Flip Y for intuitive control

        st->camera_->rotate(delta_azimuth, delta_elevation);

        // Throttled invalidation - limit to ~30fps during drag to reduce CPU load
        // Final frame is always rendered on RELEASED event
        static uint32_t last_invalidate_ms = 0;
        uint32_t now_ms = lv_tick_get();
        constexpr uint32_t MIN_FRAME_MS = 33; // ~30fps

        if (now_ms - last_invalidate_ms >= MIN_FRAME_MS) {
            lv_obj_invalidate(obj);
            last_invalidate_ms = now_ms;
        }

        st->last_drag_pos = point;

        spdlog::trace("[GCode Viewer] Drag ({}, {}) -> rotate({:.1f}, {:.1f})", dx, dy,
                      delta_azimuth, delta_elevation);
    }
}

/**
 * @brief Touch release callback - handle click vs drag gesture
 *
 * Skips tap handling if long-press already fired (user held for 500ms+).
 */
static void gcode_viewer_release_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    gcode_viewer_state_t* st = get_state(obj);

    if (!st)
        return;

    // Cancel long-press timer if still pending
    if (st->long_press_timer_) {
        lv_timer_delete(st->long_press_timer_);
        st->long_press_timer_ = nullptr;
    }

    // Get release position
    lv_indev_t* indev = lv_indev_active();
    if (!indev) {
        st->is_dragging = false;
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    // Calculate total drag distance from initial press
    int dx = abs(point.x - st->drag_start.x);
    int dy = abs(point.y - st->drag_start.y);

    const int CLICK_THRESHOLD = 10; // pixels - distinguish click from drag

    // Skip tap handling if long-press already fired
    if (st->long_press_fired) {
        spdlog::trace("[GCode Viewer] Release after long-press - skipping tap handling");
        st->is_dragging = false;
        st->long_press_fired = false;
        return;
    }

    // If movement was minimal, treat as click and try to pick object
    if (dx < CLICK_THRESHOLD && dy < CLICK_THRESHOLD && has_gcode_data(st)) {
        spdlog::debug("[GCode Viewer] Click detected at ({}, {})", point.x, point.y);
        const char* picked = ui_gcode_viewer_pick_object(obj, point.x, point.y);

        if (picked && picked[0] != '\0') {
            // Object clicked - toggle selection (single-select)
            std::string picked_name(picked);

            if (st->selected_objects.count(picked_name) > 0) {
                // Already selected - deselect
                st->selected_objects.clear();
                spdlog::info("[GCode Viewer] Deselected object '{}'", picked_name);
            } else {
                // Select this object (replacing any previous selection)
                st->selected_objects.clear();
                st->selected_objects.insert(picked_name);
                spdlog::info("[GCode Viewer] Selected object '{}'", picked_name);
            }

            // Update highlighting to show all selected objects
            ui_gcode_viewer_set_highlighted_objects(obj, st->selected_objects);

            // Invoke tap callback if registered (for exclude object UI)
            if (st->object_tap_callback) {
                st->object_tap_callback(obj, picked, st->object_tap_user_data);
            }
        } else {
            spdlog::debug("[GCode Viewer] Click at ({}, {}) - no object found (G-code may lack "
                          "EXCLUDE_OBJECT metadata)",
                          point.x, point.y);
            // Still invoke callback with empty string to indicate click on empty space
            if (st->object_tap_callback) {
                st->object_tap_callback(obj, "", st->object_tap_user_data);
            }
        }
        // Note: If no object picked, keep current selection (per user requirements)
    }

    st->is_dragging = false;

    // Exit interaction mode to restore full resolution for final frame
    if (st->renderer_) {
        st->renderer_->set_interaction_mode(false);
    }

    // Always render final frame on release to ensure camera settles at correct position
    // (throttling during drag may have skipped the last frame)
    lv_obj_invalidate(obj);

    spdlog::trace("[GCode Viewer] Release at ({}, {}), drag=({}, {})", point.x, point.y, dx, dy);
}

/**
 * @brief Size changed callback - update camera aspect ratio on resize
 */
static void gcode_viewer_size_changed_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    gcode_viewer_state_t* st = get_state(obj);

    if (!st)
        return;

    // Get new widget dimensions
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    int width = lv_area_get_width(&coords);
    int height = lv_area_get_height(&coords);

    // Update camera and renderer viewport to match new size
    st->camera_->set_viewport_size(width, height);
    st->renderer_->set_viewport_size(width, height);

    // Also update 2D renderer if initialized
    if (st->layer_renderer_2d_) {
        st->layer_renderer_2d_->set_canvas_size(width, height);
        st->layer_renderer_2d_->auto_fit();
    }

    // Trigger redraw with new aspect ratio
    lv_obj_invalidate(obj);

    spdlog::trace("[GCode Viewer] SIZE_CHANGED: {}x{}, aspect={:.3f}", width, height,
                  (float)width / (float)height);
}

/**
 * @brief Cleanup callback - free resources on widget deletion
 */
static void gcode_viewer_delete_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    std::unique_ptr<gcode_viewer_state_t> state(
        static_cast<gcode_viewer_state_t*>(lv_obj_get_user_data(obj)));
    lv_obj_set_user_data(obj, nullptr);

    if (state) {
        spdlog::trace("[GCode Viewer] Widget destroyed");
        // state automatically freed when unique_ptr goes out of scope
        // RAII destructor handles thread cleanup, timers, etc.
    }
}

// ==============================================
// Public API Implementation
// ==============================================

lv_obj_t* ui_gcode_viewer_create(lv_obj_t* parent) {
    // Create base object
    lv_obj_t* obj = lv_obj_create(parent);
    if (!obj) {
        return nullptr;
    }

    // Set default size (will be overridden by XML attrs or manual sizing)
    // This prevents 0x0 at init time since lv_obj now defaults to content sizing
    lv_obj_set_size(obj, 200, 200);

    // Allocate state (C++ object) using RAII
    auto state_ptr = std::make_unique<gcode_viewer_state_t>();
    if (!state_ptr) {
        lv_obj_safe_delete(obj);
        return nullptr;
    }

    // Get raw pointer for subsequent initialization before transferring ownership
    gcode_viewer_state_t* st = state_ptr.get();
    lv_obj_set_user_data(obj, state_ptr.release());

    // Configure object appearance
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);

    // Register event handlers
    lv_obj_add_event_cb(obj, gcode_viewer_draw_cb, LV_EVENT_DRAW_POST, nullptr);
    lv_obj_add_event_cb(obj, gcode_viewer_size_changed_cb, LV_EVENT_SIZE_CHANGED, nullptr);
    lv_obj_add_event_cb(obj, gcode_viewer_press_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(obj, gcode_viewer_pressing_cb, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(obj, gcode_viewer_release_cb, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(obj, gcode_viewer_delete_cb, LV_EVENT_DELETE, nullptr);

    // Initialize viewport size based on current widget dimensions
    // This ensures correct aspect ratio from the start
    lv_obj_update_layout(obj); // Force layout calculation
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    int width = lv_area_get_width(&coords);
    int height = lv_area_get_height(&coords);

    if (width > 0 && height > 0) {
        st->camera_->set_viewport_size(width, height);
        st->renderer_->set_viewport_size(width, height);
        spdlog::debug("[GCode Viewer] INIT: viewport={}x{}, aspect={:.3f}", width, height,
                      (float)width / (float)height);
    } else {
        spdlog::error("[GCode Viewer] INIT: Invalid size {}x{}, using defaults", width, height);
    }

    spdlog::debug("[GCode Viewer] Widget created");
    return obj;
}

// Result structure for async geometry building
struct AsyncBuildResult {
    std::unique_ptr<helix::gcode::ParsedGCodeFile> gcode_file;
#ifdef ENABLE_TINYGL_3D
    std::unique_ptr<helix::gcode::RibbonGeometry> geometry;        ///< Full detail geometry
    std::unique_ptr<helix::gcode::RibbonGeometry> coarse_geometry; ///< Coarse LOD for interaction
#endif
    std::string error_msg;
    bool success{true};
};

/**
 * @brief Asynchronously load and build G-code geometry in background thread
 *
 * Shows loading spinner while parsing and building geometry. Uses background
 * thread to avoid blocking the UI thread. Geometry building is thread-safe
 * (no OpenGL calls, pure CPU work).
 */
static void ui_gcode_viewer_load_file_async(lv_obj_t* obj, const char* file_path) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st || !file_path) {
        return;
    }

    spdlog::info("[GCode Viewer] Loading file async: {}", file_path);
    st->viewer_state = GCODE_VIEWER_STATE_LOADING;
    st->first_render = true; // Reset for new file

    // Clear any existing data sources (mutually exclusive: streaming XOR full-file)
    st->streaming_controller_.reset();
    st->gcode_file.reset();
    st->layer_renderer_2d_.reset(); // Will be recreated on first render

    // =========================================================================
    // PHASE 0: Streaming Mode Detection (Phase 6)
    // Determine whether to use streaming (layer-by-layer) or full-load mode
    // based on file size and available memory.
    // =========================================================================
    std::error_code ec;
    auto file_size = std::filesystem::file_size(file_path, ec);
    if (ec) {
        spdlog::warn("[GCode Viewer] Cannot get file size for {}: {}", file_path, ec.message());
        file_size = 0; // Fall through to full-load mode
    }

    bool use_streaming = helix::should_use_gcode_streaming(file_size);
    spdlog::info("[GCode Viewer] File size: {}KB, streaming mode: {}", file_size / 1024,
                 use_streaming ? "ON" : "OFF");

    // Clean up previous loading UI if it exists
    if (st->loading_container) {
        lv_obj_safe_delete(st->loading_container);
        st->loading_container = nullptr;
    }

    // =========================================================================
    // STREAMING MODE PATH
    // Uses GCodeStreamingController for on-demand layer loading.
    // Ideal for large files on memory-constrained devices.
    // =========================================================================
    if (use_streaming) {
        // Create loading UI
        st->loading_container = lv_obj_create(obj);
        lv_obj_set_size(st->loading_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_center(st->loading_container);
        lv_obj_set_flex_flow(st->loading_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(st->loading_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(st->loading_container, theme_manager_get_color("card_bg"),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(st->loading_container, 220, LV_PART_MAIN);
        lv_obj_set_style_border_width(st->loading_container, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(st->loading_container, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(st->loading_container, 24, LV_PART_MAIN);
        lv_obj_set_style_pad_gap(st->loading_container, 12, LV_PART_MAIN);

        st->loading_spinner = lv_spinner_create(st->loading_container);
        lv_obj_set_size(st->loading_spinner, 48, 48);
        lv_color_t primary = theme_manager_get_color("primary");
        lv_obj_set_style_arc_color(st->loading_spinner, primary, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(st->loading_spinner, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(st->loading_spinner, LV_OPA_0, LV_PART_MAIN);

        st->loading_label = lv_label_create(st->loading_container);
        lv_label_set_text(st->loading_label, "Indexing G-code...");
        lv_obj_set_style_text_color(st->loading_label, theme_manager_get_color("text"),
                                    LV_PART_MAIN);

        // Create streaming controller
        st->streaming_controller_ = std::make_unique<helix::gcode::GCodeStreamingController>();

        // Launch async index building with completion callback
        // The callback runs on the background thread, so we use lv_async_call to marshal to UI
        std::string path_copy = file_path;
        st->streaming_controller_->open_file_async(path_copy, [obj, path_copy](bool success) {
            // Marshal completion to UI thread
            struct StreamingResult {
                bool success;
                std::string path;
            };
            auto result = std::make_unique<StreamingResult>();
            result->success = success;
            result->path = path_copy;

            ui_queue_update<StreamingResult>(std::move(result), [obj](StreamingResult* r) {
                gcode_viewer_state_t* st = get_state(obj);
                if (!st) {
                    return; // Widget destroyed
                }

                // Clean up loading UI
                if (st->loading_container) {
                    lv_obj_safe_delete(st->loading_container);
                    st->loading_spinner = nullptr;
                    st->loading_label = nullptr;
                }

                if (r->success && st->streaming_controller_ &&
                    st->streaming_controller_->is_open()) {
                    spdlog::info("[GCode Viewer] Streaming mode: indexed {} layers",
                                 st->streaming_controller_->get_layer_count());

                    // Initialize 2D renderer with streaming controller
                    st->layer_renderer_2d_ = std::make_unique<helix::gcode::GCodeLayerRenderer>();
                    st->layer_renderer_2d_->set_streaming_controller(
                        st->streaming_controller_.get());

                    // Apply filament color from G-code metadata if available
                    const auto& stats = st->streaming_controller_->get_index_stats();
                    if (!stats.filament_color.empty()) {
                        lv_color_t color = lv_color_hex(
                            std::strtol(stats.filament_color.c_str() + 1, nullptr, 16));
                        st->layer_renderer_2d_->set_extrusion_color(color);
                        spdlog::info("[GCode Viewer] Using filament color from metadata: {}",
                                     stats.filament_color);
                    }

                    // Get canvas size from widget
                    lv_area_t coords;
                    lv_obj_get_coords(obj, &coords);
                    int width = lv_area_get_width(&coords);
                    int height = lv_area_get_height(&coords);
                    st->layer_renderer_2d_->set_canvas_size(width, height);
                    st->layer_renderer_2d_->auto_fit();

                    // Apply any stored content offset (may have been set before streaming started)
                    if (st->content_offset_y_percent_ != 0.0f) {
                        st->layer_renderer_2d_->set_content_offset_y(st->content_offset_y_percent_);
                        spdlog::debug("[GCode Viewer] Applied stored content offset: {}%",
                                      st->content_offset_y_percent_ * 100);
                    }

                    // Note: Ghost mode is disabled in streaming mode (requires all layers)
                    // The renderer handles this automatically in set_streaming_controller()

                    st->viewer_state = GCODE_VIEWER_STATE_LOADED;
                    st->first_render = false;

                    // Trigger initial render
                    lv_obj_invalidate(obj);

                    // Invoke load callback
                    if (st->load_callback) {
                        st->load_callback(obj, st->load_callback_user_data, true);
                    }
                } else {
                    spdlog::error("[GCode Viewer] Streaming mode: failed to index {}", r->path);
                    st->viewer_state = GCODE_VIEWER_STATE_ERROR;
                    st->streaming_controller_.reset();

                    if (st->load_callback) {
                        st->load_callback(obj, st->load_callback_user_data, false);
                    }
                }
            });
        });

        return; // Streaming path handles everything asynchronously
    }

    // =========================================================================
    // FULL-LOAD MODE PATH (existing implementation)
    // Parses entire file into memory. Used for smaller files.
    // =========================================================================

    // Create loading UI with dark theme styling (matching preparing_overlay pattern)
    st->loading_container = lv_obj_create(obj);
    lv_obj_set_size(st->loading_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(st->loading_container);
    lv_obj_set_flex_flow(st->loading_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(st->loading_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    // Style container: semi-transparent dark background, no border, padding for content
    // Use theme_manager_get_color() for token lookup (not theme_manager_parse_hex_color which
    // expects hex)
    lv_obj_set_style_bg_color(st->loading_container, theme_manager_get_color("card_bg"),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(st->loading_container, 220, LV_PART_MAIN);
    lv_obj_set_style_border_width(st->loading_container, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(st->loading_container, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(st->loading_container, 24, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(st->loading_container, 12, LV_PART_MAIN);

    st->loading_spinner = lv_spinner_create(st->loading_container);
    lv_obj_set_size(st->loading_spinner, 48, 48); // ~lg size for small screens

    // Apply consistent spinner styling (matching ui_spinner component)
    lv_color_t primary = theme_manager_get_color("primary");
    lv_obj_set_style_arc_color(st->loading_spinner, primary, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(st->loading_spinner, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(st->loading_spinner, LV_OPA_0, LV_PART_MAIN);

    st->loading_label = lv_label_create(st->loading_container);
    lv_label_set_text(st->loading_label, "Loading G-code...");
    // Set text color for visibility on dark background
    lv_obj_set_style_text_color(st->loading_label, theme_manager_get_color("text"), LV_PART_MAIN);

    // Launch worker thread via RAII-managed start_build()
    // Automatically cancels any existing build and joins the thread
    st->start_build([st, obj, path = std::string(file_path)]() {
        auto result = std::make_unique<AsyncBuildResult>();

        try {
            // PHASE 1: Parse G-code file (fast, ~100ms)
            std::ifstream file(path);
            if (!file.is_open()) {
                result->success = false;
                result->error_msg = "Failed to open file: " + path;
            } else {
                helix::gcode::GCodeParser parser;
                std::string line;

                while (std::getline(file, line)) {
                    parser.parse_line(line);
                }

                file.close();

                result->gcode_file =
                    std::make_unique<helix::gcode::ParsedGCodeFile>(parser.finalize());
                result->gcode_file->filename = path;

                spdlog::info("[GCode Viewer] Parsed {} layers, {} segments",
                             result->gcode_file->layers.size(), result->gcode_file->total_segments);

#ifdef ENABLE_TINYGL_3D
                // PHASE 2: Build 3D geometry (slow, 1-5s for large files)
                // This is thread-safe - no OpenGL calls, just CPU work
                // SKIP entirely for 2D mode - 2D renderer uses ParsedGCodeFile directly
                if (st->render_mode_ == GCODE_VIEWER_RENDER_3D) {
                    // Check if available memory is low (< 64MB available right now)
                    // When memory is low, ONLY build coarse geometry to save ~50MB
                    auto mem_info = helix::get_system_memory_info();
                    bool memory_constrained = mem_info.is_low_memory();
                    if (memory_constrained) {
                        spdlog::info("[GCode Viewer] Memory constrained ({}MB available) - "
                                     "building coarse geometry only",
                                     mem_info.available_mb());
                    }

                    // Helper lambda to configure a builder with common settings
                    auto configure_builder = [&](helix::gcode::GeometryBuilder& builder) {
                        if (!result->gcode_file->tool_color_palette.empty()) {
                            builder.set_tool_color_palette(result->gcode_file->tool_color_palette);
                        }
                        if (result->gcode_file->perimeter_extrusion_width_mm > 0.0f) {
                            builder.set_extrusion_width(
                                result->gcode_file->perimeter_extrusion_width_mm);
                        } else if (result->gcode_file->extrusion_width_mm > 0.0f) {
                            builder.set_extrusion_width(result->gcode_file->extrusion_width_mm);
                        }
                        builder.set_layer_height(result->gcode_file->layer_height_mm);
                    };

                    // Build full geometry only on non-constrained systems
                    if (!memory_constrained) {
                        helix::gcode::GeometryBuilder builder;
                        configure_builder(builder);

                        // Aggressive simplification: 0.5mm merges more collinear segments
                        // (still well within 3D printer precision of ~50 microns)
                        helix::gcode::SimplificationOptions opts{.tolerance_mm = 0.5f,
                                                                 .min_segment_length_mm = 0.05f};

                        result->geometry = std::make_unique<helix::gcode::RibbonGeometry>(
                            builder.build(*result->gcode_file, opts));

                        spdlog::info(
                            "[GCode Viewer] Built full geometry: {} vertices, {} triangles",
                            result->geometry->vertices.size(),
                            result->geometry->extrusion_triangle_count +
                                result->geometry->travel_triangle_count);
                    }

                    // Build coarse LOD geometry for interaction
                    // More aggressive simplification for better frame rate during drag
                    // 2.0mm tolerance gives ~55% fewer triangles - good balance of quality/speed
                    {
                        helix::gcode::GeometryBuilder coarse_builder;
                        configure_builder(coarse_builder);

                        helix::gcode::SimplificationOptions coarse_opts{
                            .tolerance_mm = 2.0f, .min_segment_length_mm = 0.5f};

                        result->coarse_geometry = std::make_unique<helix::gcode::RibbonGeometry>(
                            coarse_builder.build(*result->gcode_file, coarse_opts));

                        size_t coarse_tris = result->coarse_geometry->extrusion_triangle_count +
                                             result->coarse_geometry->travel_triangle_count;

                        if (memory_constrained) {
                            spdlog::info("[GCode Viewer] Built coarse-only geometry: {} triangles",
                                         coarse_tris);
                        } else {
                            size_t full_tris = result->geometry->extrusion_triangle_count +
                                               result->geometry->travel_triangle_count;
                            float reduction =
                                full_tris > 0
                                    ? 100.0f * (1.0f - float(coarse_tris) / float(full_tris))
                                    : 0.0f;
                            spdlog::info("[GCode Viewer] Built coarse LOD: {} triangles ({:.0f}% "
                                         "reduction from full)",
                                         coarse_tris, reduction);
                        }
                    }

                    // Free parsed segment data - 3D mode doesn't need raw segments
                    // This releases 40-160MB on large files while preserving metadata
                    size_t freed = result->gcode_file->clear_segments();
                    spdlog::info("[GCode Viewer] Freed {} MB of parsed segment data",
                                 freed / (1024 * 1024));
                } else {
                    // 2D mode: Skip geometry building entirely
                    // Preserve segments for 2D renderer (uses ParsedGCodeFile directly)
                    spdlog::debug("[GCode Viewer] 2D mode - skipping 3D geometry build");
                }
#else
                // 2D renderer: No geometry building needed
                // The renderer uses ParsedGCodeFile directly for 2D line drawing
                spdlog::debug("[GCode Viewer] 2D renderer - skipping geometry build");
#endif
            }
        } catch (const std::exception& ex) {
            result->success = false;
            result->error_msg = std::string("Exception: ") + ex.what();
        }

        // Check cancellation before dispatching to UI - if cancelled, widget may be destroyed
        if (st->is_cancelled()) {
            spdlog::debug("[GCode Viewer] Build cancelled, discarding result");
            return;
        }

        // PHASE 3: Marshal result back to UI thread (SAFE)
        ui_queue_update<AsyncBuildResult>(std::move(result), [obj](AsyncBuildResult* r) {
            gcode_viewer_state_t* st = get_state(obj);
            if (!st) {
                return; // Widget was destroyed
            }

            // Clean up loading UI
            if (st->loading_container) {
                lv_obj_safe_delete(st->loading_container);
                st->loading_spinner = nullptr;
                st->loading_label = nullptr;
            }

            if (r->success) {
                spdlog::debug("[GCode Viewer] Async callback - setting up geometry");

                // Store G-code data
                st->gcode_file = std::move(r->gcode_file);

                // Update 2D renderer if it exists (prevents dangling pointer)
                if (st->layer_renderer_2d_) {
                    st->layer_renderer_2d_->set_gcode(st->gcode_file.get());
                    st->layer_renderer_2d_->auto_fit();
                }

                // Set pre-built geometry on renderer
                // On memory-constrained systems, we only have coarse geometry
#ifdef ENABLE_TINYGL_3D
                if (r->geometry) {
                    // Normal case: full + coarse geometry
                    spdlog::debug("[GCode Viewer] Setting full + coarse geometry");
                    st->renderer_->set_prebuilt_geometry(std::move(r->geometry),
                                                         st->gcode_file->filename);
                    if (r->coarse_geometry) {
                        st->renderer_->set_prebuilt_coarse_geometry(std::move(r->coarse_geometry));
                    }
                } else if (r->coarse_geometry) {
                    // Memory-constrained: use coarse as primary (no separate LOD)
                    spdlog::info("[GCode Viewer] Memory-constrained mode: using coarse geometry as "
                                 "primary (no LOD switching)");
                    st->renderer_->set_prebuilt_geometry(std::move(r->coarse_geometry),
                                                         st->gcode_file->filename);
                }
#endif

                // Fit camera to model bounds
                st->camera_->fit_to_bounds(st->gcode_file->global_bounding_box);

                st->viewer_state = GCODE_VIEWER_STATE_LOADED;
                spdlog::debug("[GCode Viewer] State set to LOADED");

                // Auto-apply filament color if enabled, but ONLY for single-color prints
                // Multicolor prints have multiple colors in the geometry's color palette
#ifdef ENABLE_TINYGL_3D
                size_t color_count = st->renderer_->get_geometry_color_count();
                bool is_multicolor = (color_count > 1); // >1 means multiple tool colors
#else
                size_t color_count = 1; // 2D renderer doesn't track color palette
                bool is_multicolor = false; // 2D renderer doesn't have color palette
#endif

                if (st->use_filament_color && !is_multicolor &&
                    st->gcode_file->filament_color_hex.length() >= 2) {
                    lv_color_t color = lv_color_hex(static_cast<uint32_t>(
                        std::strtol(st->gcode_file->filament_color_hex.c_str() + 1, nullptr, 16)));
                    st->renderer_->set_extrusion_color(color);
                    spdlog::debug("[GCode Viewer] Auto-applied single-color filament: {}",
                                  st->gcode_file->filament_color_hex);
                } else if (is_multicolor) {
                    spdlog::info(
                        "[GCode Viewer] Multicolor print detected ({} colors) - preserving "
                        "per-segment colors",
                        color_count);
                }

                // Clear first_render flag to allow actual rendering on next draw
                st->first_render = false;

                // Trigger redraw (will render geometry now that first_render is false)
                lv_obj_invalidate(obj);

                spdlog::info("[GCode Viewer] Async load completed successfully");

                // Invoke load callback if registered
                if (st->load_callback) {
                    spdlog::debug("[GCode Viewer] Invoking load callback");
                    st->load_callback(obj, st->load_callback_user_data, true);
                }
            } else {
                spdlog::error("[GCode Viewer] Async load failed: {}", r->error_msg);
                st->viewer_state = GCODE_VIEWER_STATE_ERROR;
                st->gcode_file.reset();

                // Invoke load callback with error status if registered
                if (st->load_callback) {
                    spdlog::debug("[GCode Viewer] Invoking load callback (error)");
                    st->load_callback(obj, st->load_callback_user_data, false);
                }
            }
        });
    });
}

void ui_gcode_viewer_load_file(lv_obj_t* obj, const char* file_path) {
    // Use async version by default
    ui_gcode_viewer_load_file_async(obj, file_path);
}

void ui_gcode_viewer_set_load_callback(lv_obj_t* obj, gcode_viewer_load_callback_t callback,
                                       void* user_data) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st) {
        return;
    }

    st->load_callback = callback;
    st->load_callback_user_data = user_data;
    spdlog::debug("[GCode Viewer] Load callback registered");
}

void ui_gcode_viewer_set_gcode_data(lv_obj_t* obj, void* gcode_data) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st || !gcode_data)
        return;

    // Clear streaming controller (mutually exclusive with gcode_file)
    st->streaming_controller_.reset();

    // Take ownership of the data (caller must use new to allocate)
    st->gcode_file.reset(static_cast<helix::gcode::ParsedGCodeFile*>(gcode_data));

    // Fit camera to model (uses current camera orientation from reset())
    st->camera_->fit_to_bounds(st->gcode_file->global_bounding_box);

    st->viewer_state = GCODE_VIEWER_STATE_LOADED;

    spdlog::info("[GCode Viewer] Set G-code data: {} layers, {} segments",
                 st->gcode_file->layers.size(), st->gcode_file->total_segments);

    // Auto-apply filament color if enabled and available
    if (st->use_filament_color && st->gcode_file->filament_color_hex.length() >= 2) {
        lv_color_t color = lv_color_hex(static_cast<uint32_t>(
            std::strtol(st->gcode_file->filament_color_hex.c_str() + 1, nullptr, 16)));
        st->renderer_->set_extrusion_color(color);
        spdlog::info("[GCode Viewer] Auto-applied filament color: {}",
                     st->gcode_file->filament_color_hex);
    }

    // Trigger redraw
    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_clear(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->gcode_file.reset();
    st->streaming_controller_.reset(); // Clear streaming controller (Phase 6)
    st->layer_renderer_2d_.reset();    // Clear 2D renderer to avoid dangling pointer
    st->viewer_state = GCODE_VIEWER_STATE_EMPTY;

    lv_obj_invalidate(obj);
    spdlog::debug("[GCode Viewer] Cleared");
}

gcode_viewer_state_enum_t ui_gcode_viewer_get_state(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    return st ? st->viewer_state : GCODE_VIEWER_STATE_EMPTY;
}

// ==============================================
// Rendering Pause Control
// ==============================================

void ui_gcode_viewer_set_paused(lv_obj_t* obj, bool paused) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    if (st->rendering_paused_ != paused) {
        st->rendering_paused_ = paused;
        spdlog::debug("[GCode Viewer] Rendering {} (visibility optimization)",
                      paused ? "PAUSED" : "RESUMED");

        // If resuming, trigger a redraw to show current state
        if (!paused) {
            lv_obj_invalidate(obj);
        }
    }
}

bool ui_gcode_viewer_is_paused(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    return st ? st->rendering_paused_ : true;
}

// ==============================================
// Render Mode Control
// ==============================================

void ui_gcode_viewer_set_render_mode(lv_obj_t* obj, gcode_viewer_render_mode_t mode) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->render_mode_ = mode;

    // Reset FPS samples when switching modes (diagnostic tracking)
    st->fps_sample_count_ = 0;
    st->fps_sample_index_ = 0;

    const char* mode_names[] = {"AUTO (2D)", "3D", "2D_LAYER"};
    spdlog::debug("[GCode Viewer] Render mode set to {}", mode_names[static_cast<int>(mode)]);

    // If using 2D mode (AUTO or 2D_LAYER), ensure the 2D renderer is initialized
    if (st->is_using_2d_mode() && st->gcode_file && !st->layer_renderer_2d_) {
        st->layer_renderer_2d_ = std::make_unique<helix::gcode::GCodeLayerRenderer>();
        st->layer_renderer_2d_->set_gcode(st->gcode_file.get());

        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);
        int width = lv_area_get_width(&coords);
        int height = lv_area_get_height(&coords);
        st->layer_renderer_2d_->set_canvas_size(width, height);
        st->layer_renderer_2d_->auto_fit();
    }

    lv_obj_invalidate(obj);
}

gcode_viewer_render_mode_t ui_gcode_viewer_get_render_mode(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    return st ? st->render_mode_ : GCODE_VIEWER_RENDER_AUTO;
}

void ui_gcode_viewer_evaluate_render_mode(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    // No-op: AUTO mode now defaults to 2D without FPS-based detection
    // This function is kept for API compatibility but does nothing
    // Render mode is determined at widget creation based on HELIX_GCODE_MODE env var

    if (st->has_enough_fps_data()) {
        float avg_fps = st->get_average_fps();
        spdlog::debug("[GCode Viewer] FPS diagnostic: avg {:.1f} (mode: {})", avg_fps,
                      st->is_using_2d_mode() ? "2D" : "3D");
    }
}

bool ui_gcode_viewer_is_using_2d_mode(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    return st ? st->is_using_2d_mode() : false;
}

void ui_gcode_viewer_set_show_supports(lv_obj_t* obj, bool show) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    // Only affects 2D renderer
    if (st->layer_renderer_2d_) {
        st->layer_renderer_2d_->set_show_supports(show);
        lv_obj_invalidate(obj);
    }
}

// ==============================================
// Camera Controls
// ==============================================

void ui_gcode_viewer_rotate(lv_obj_t* obj, float delta_azimuth, float delta_elevation) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->camera_->rotate(delta_azimuth, delta_elevation);
    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_pan(lv_obj_t* obj, float delta_x, float delta_y) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->camera_->pan(delta_x, delta_y);
    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_zoom(lv_obj_t* obj, float factor) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->camera_->zoom(factor);
    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_reset_camera(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->camera_->reset();

    // Re-fit to model if loaded
    if (st->gcode_file) {
        st->camera_->fit_to_bounds(st->gcode_file->global_bounding_box);
    }

    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_set_view(lv_obj_t* obj, gcode_viewer_preset_view_t preset) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    switch (preset) {
    case GCODE_VIEWER_VIEW_ISOMETRIC:
        st->camera_->set_isometric_view();
        break;
    case GCODE_VIEWER_VIEW_TOP:
        st->camera_->set_top_view();
        break;
    case GCODE_VIEWER_VIEW_FRONT:
        st->camera_->set_front_view();
        break;
    case GCODE_VIEWER_VIEW_SIDE:
        st->camera_->set_side_view();
        break;
    }

    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_set_camera_azimuth(lv_obj_t* obj, float azimuth) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->camera_->set_azimuth(azimuth);
    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_set_camera_elevation(lv_obj_t* obj, float elevation) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->camera_->set_elevation(elevation);
    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_set_camera_zoom(lv_obj_t* obj, float zoom) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->camera_->set_zoom_level(zoom);
    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_set_debug_colors(lv_obj_t* obj, bool enable) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

#ifdef ENABLE_TINYGL_3D
    st->renderer_->set_debug_face_colors(enable);
    lv_obj_invalidate(obj);
#else
    (void)enable;
#endif
}

// ==============================================
// Rendering Options
// ==============================================

void ui_gcode_viewer_set_show_travels(lv_obj_t* obj, bool show) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->renderer_->set_show_travels(show);

    // Also update 2D renderer if initialized
    if (st->layer_renderer_2d_) {
        st->layer_renderer_2d_->set_show_travels(show);
    }

    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_set_show_extrusions(lv_obj_t* obj, bool show) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->renderer_->set_show_extrusions(show);

    // Also update 2D renderer if initialized
    if (st->layer_renderer_2d_) {
        st->layer_renderer_2d_->set_show_extrusions(show);
    }

    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_set_layer_range(lv_obj_t* obj, int start_layer, int end_layer) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->renderer_->set_layer_range(start_layer, end_layer);
    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_set_highlighted_object(lv_obj_t* obj, const char* object_name) {
    // Legacy single-object API - convert to set and call multi-object version
    std::unordered_set<std::string> objects;
    if (object_name && object_name[0] != '\0') {
        objects.insert(object_name);
    }
    ui_gcode_viewer_set_highlighted_objects(obj, objects);
}

void ui_gcode_viewer_set_highlighted_objects(lv_obj_t* obj,
                                             const std::unordered_set<std::string>& object_names) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->renderer_->set_highlighted_objects(object_names);
    if (st->layer_renderer_2d_) {
        st->layer_renderer_2d_->set_highlighted_objects(object_names);
    }
    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_set_excluded_objects(lv_obj_t* obj,
                                          const std::unordered_set<std::string>& object_names) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->excluded_objects = object_names;
    st->renderer_->set_excluded_objects(object_names);
    if (st->layer_renderer_2d_) {
        st->layer_renderer_2d_->set_excluded_objects(object_names);
    }
    lv_obj_invalidate(obj);

    spdlog::debug("[GCode Viewer] Excluded objects updated ({} objects)", object_names.size());
}

void ui_gcode_viewer_set_object_tap_callback(lv_obj_t* obj,
                                             gcode_viewer_object_tap_callback_t callback,
                                             void* user_data) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->object_tap_callback = callback;
    st->object_tap_user_data = user_data;
}

void ui_gcode_viewer_set_object_long_press_callback(
    lv_obj_t* obj, gcode_viewer_object_long_press_callback_t callback, void* user_data) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->object_long_press_callback = callback;
    st->object_long_press_user_data = user_data;

    spdlog::debug("[GCode Viewer] Long-press callback {}", callback ? "registered" : "cleared");
}

// ==============================================
// Color & Rendering Control
// ==============================================

void ui_gcode_viewer_set_extrusion_color(lv_obj_t* obj, lv_color_t color) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->renderer_->set_extrusion_color(color);
    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_set_travel_color(lv_obj_t* obj, lv_color_t color) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->renderer_->set_travel_color(color);
    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_use_filament_color(lv_obj_t* obj, bool enable) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->use_filament_color = enable;

    // If enabling and we have a loaded file with filament color, apply it now
    if (enable && st->gcode_file && st->gcode_file->filament_color_hex.length() >= 2) {
        lv_color_t color = lv_color_hex(static_cast<uint32_t>(
            std::strtol(st->gcode_file->filament_color_hex.c_str() + 1, nullptr, 16)));
        st->renderer_->set_extrusion_color(color);
        lv_obj_invalidate(obj);
        spdlog::debug("[GCode Viewer] Applied filament color: {}",
                      st->gcode_file->filament_color_hex);
    } else if (!enable) {
        // Reset to theme default
        st->renderer_->reset_colors();
        lv_obj_invalidate(obj);
    }
}

void ui_gcode_viewer_set_opacity(lv_obj_t* obj, lv_opa_t opacity) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->renderer_->set_global_opacity(opacity);
    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_set_brightness(lv_obj_t* obj, float factor) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->renderer_->set_brightness_factor(factor);
    lv_obj_invalidate(obj);
}

// ==============================================
// Layer Control Extensions
// ==============================================

void ui_gcode_viewer_set_single_layer(lv_obj_t* obj, int layer) {
    ui_gcode_viewer_set_layer_range(obj, layer, layer);
}

int ui_gcode_viewer_get_current_layer_start(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return 0;

    return st->renderer_->get_options().layer_start;
}

int ui_gcode_viewer_get_current_layer_end(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return -1;

    return st->renderer_->get_options().layer_end;
}

// ==============================================
// Print Progress / Ghost Layer Visualization
// ==============================================

void ui_gcode_viewer_set_print_progress(lv_obj_t* obj, int current_layer) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    // Store the print progress layer for use by render callback
    st->print_progress_layer_ = current_layer;

    // Update 3D renderer
    st->renderer_->set_print_progress_layer(current_layer);

    // Note: 2D renderer's current_layer is set in the render callback
    // using print_progress_layer_, so we just need to invalidate

    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_set_ghost_opacity(lv_obj_t* obj, lv_opa_t opacity) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    st->renderer_->set_ghost_opacity(opacity);
    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_set_ghost_mode(lv_obj_t* obj, int mode) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    // Map int to enum (0=Dimmed, 1=Stipple)
    helix::gcode::GhostRenderMode render_mode = (mode == 1) ? helix::gcode::GhostRenderMode::Stipple
                                                            : helix::gcode::GhostRenderMode::Dimmed;

    st->renderer_->set_ghost_render_mode(render_mode);
    lv_obj_invalidate(obj);
}

void ui_gcode_viewer_set_content_offset_y(lv_obj_t* obj, float offset_percent) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

    // Store offset for later application (2D renderer may not exist yet)
    st->content_offset_y_percent_ = offset_percent;

    // Apply to 2D renderer if it exists
    if (st->layer_renderer_2d_) {
        st->layer_renderer_2d_->set_content_offset_y(offset_percent);
        lv_obj_invalidate(obj);
        spdlog::debug("[GCode Viewer] Applied content offset: {}%", offset_percent * 100);
    } else {
        spdlog::debug("[GCode Viewer] Stored content offset: {}% (renderer not ready)",
                      offset_percent * 100);
    }
}

int ui_gcode_viewer_get_max_layer(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return -1;

    // In streaming mode, get layer count from streaming controller
    if (st->streaming_controller_ && st->streaming_controller_->is_open()) {
        return static_cast<int>(st->streaming_controller_->get_layer_count()) - 1;
    }

    // In 2D mode with parsed gcode, get from 2D renderer
    if (st->layer_renderer_2d_) {
        return st->layer_renderer_2d_->get_layer_count() - 1;
    }

    // Fallback to 3D renderer
    return st->renderer_->get_max_layer_index();
}

// ==============================================
// Metadata Access
// ==============================================

const char* ui_gcode_viewer_get_filament_color(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st || !st->gcode_file || st->gcode_file->filament_color_hex.empty())
        return nullptr;

    return st->gcode_file->filament_color_hex.c_str();
}

const char* ui_gcode_viewer_get_filament_type(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st || !st->gcode_file || st->gcode_file->filament_type.empty())
        return nullptr;

    return st->gcode_file->filament_type.c_str();
}

const char* ui_gcode_viewer_get_printer_model(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st || !st->gcode_file || st->gcode_file->printer_model.empty())
        return nullptr;

    return st->gcode_file->printer_model.c_str();
}

float ui_gcode_viewer_get_estimated_time_minutes(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st || !st->gcode_file)
        return 0.0f;

    return st->gcode_file->estimated_print_time_minutes;
}

float ui_gcode_viewer_get_filament_weight_g(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st || !st->gcode_file)
        return 0.0f;

    return st->gcode_file->filament_weight_g;
}

float ui_gcode_viewer_get_filament_length_mm(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st || !st->gcode_file)
        return 0.0f;

    return st->gcode_file->total_filament_mm;
}

float ui_gcode_viewer_get_filament_cost(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st || !st->gcode_file)
        return 0.0f;

    return st->gcode_file->filament_cost;
}

float ui_gcode_viewer_get_nozzle_diameter_mm(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st || !st->gcode_file)
        return 0.0f;

    return st->gcode_file->nozzle_diameter_mm;
}

// ==============================================
// Object Picking
// ==============================================

const char* ui_gcode_viewer_pick_object(lv_obj_t* obj, int x, int y) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st || !has_gcode_data(st))
        return nullptr;

    // Convert screen coordinates to widget-local coordinates
    lv_area_t widget_coords;
    lv_obj_get_coords(obj, &widget_coords);
    int local_x = x - widget_coords.x1;
    int local_y = y - widget_coords.y1;

    spdlog::debug("[GCode Viewer] pick_object screen=({}, {}), widget_pos=({}, {}), local=({}, {})",
                  x, y, widget_coords.x1, widget_coords.y1, local_x, local_y);

    std::optional<std::string> result;

    // Use 2D renderer's pick_object_at in 2D mode
    if (st->is_using_2d_mode() && st->layer_renderer_2d_) {
        result = st->layer_renderer_2d_->pick_object_at(local_x, local_y);
    } else if (st->renderer_ && st->gcode_file) {
        // 3D renderer path (requires full gcode file)
        result =
            st->renderer_->pick_object(glm::vec2(local_x, local_y), *st->gcode_file, *st->camera_);
    }

    if (result) {
        // Store in static buffer (safe for single-threaded LVGL)
        static std::string picked_name;
        picked_name = *result;
        return picked_name.c_str();
    }

    return nullptr;
}

// ==============================================
// Statistics
// ==============================================

const char* ui_gcode_viewer_get_filename(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return nullptr;

    // Streaming mode: get filename from controller
    if (st->streaming_controller_ && st->streaming_controller_->is_open()) {
        static std::string streaming_name; // Thread-safe for single-threaded LVGL
        streaming_name = st->streaming_controller_->get_source_name();
        return streaming_name.empty() ? nullptr : streaming_name.c_str();
    }

    // Full-file mode
    if (st->gcode_file && !st->gcode_file->filename.empty()) {
        return st->gcode_file->filename.c_str();
    }

    return nullptr;
}

int ui_gcode_viewer_get_layer_count(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return 0;

    // Streaming mode: get layer count from controller
    if (st->streaming_controller_ && st->streaming_controller_->is_open()) {
        return static_cast<int>(st->streaming_controller_->get_layer_count());
    }

    // Full-file mode: get layer count from parsed file
    if (st->gcode_file) {
        return static_cast<int>(st->gcode_file->layers.size());
    }

    return 0;
}

int ui_gcode_viewer_get_segments_rendered(lv_obj_t* obj) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return 0;

    return static_cast<int>(st->renderer_->get_segments_rendered());
}

// ==============================================
// Material & Lighting Control
// ==============================================

void ui_gcode_viewer_set_specular(lv_obj_t* obj, float intensity, float shininess) {
    gcode_viewer_state_t* st = get_state(obj);
    if (!st)
        return;

#ifdef ENABLE_TINYGL_3D
    st->renderer_->set_specular(intensity, shininess);
    lv_obj_invalidate(obj); // Request redraw
#else
    (void)intensity;
    (void)shininess;
    spdlog::warn("[GCode Viewer] set_specular() ignored - not using TinyGL 3D renderer");
#endif
}

// ==============================================
// LVGL XML Component Registration
// ==============================================

/**
 * @brief XML create handler for gcode_viewer widget
 */
static void* gcode_viewer_xml_create(lv_xml_parser_state_t* state, const char** attrs) {
    (void)attrs; // Required by callback signature, but widget has no XML attributes
    void* parent = lv_xml_state_get_parent(state);
    if (!parent) {
        spdlog::error("[GCode Viewer] XML create: no parent object");
        return nullptr;
    }

    lv_obj_t* obj = ui_gcode_viewer_create((lv_obj_t*)parent);
    if (!obj) {
        spdlog::error("[GCode Viewer] XML create: failed to create widget");
        return nullptr;
    }

    spdlog::trace("[GCode Viewer] XML created widget");
    return (void*)obj;
}

/**
 * @brief XML apply handler for gcode_viewer widget
 * Applies XML attributes to the widget
 */
static void gcode_viewer_xml_apply(lv_xml_parser_state_t* state, const char** attrs) {
    void* item = lv_xml_state_get_item(state);
    lv_obj_t* obj = (lv_obj_t*)item;

    if (!obj) {
        spdlog::error("[GCode Viewer] NULL object in xml_apply");
        return;
    }

    // Apply standard lv_obj properties from XML (size, style, align, name, etc.)
    lv_xml_obj_apply(state, attrs);

    spdlog::trace("[GCode Viewer] Applied XML attributes");
}

/**
 * @brief Register gcode_viewer widget with LVGL XML system
 *
 * Call this during application initialization before loading any XML.
 * Typically called from main() or ui_init().
 */
extern "C" void ui_gcode_viewer_register(void) {
    lv_xml_register_widget("gcode_viewer", gcode_viewer_xml_create, gcode_viewer_xml_apply);
    spdlog::trace("[GCode Viewer] Registered <gcode_viewer> widget with LVGL XML system");
}
