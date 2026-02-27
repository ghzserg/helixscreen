// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef ENABLE_GLES_3D

#include "gcode_camera.h"
#include "gcode_color_palette.h"
#include "gcode_geometry_builder.h"
#include "gcode_parser.h"

#include <lvgl/lvgl.h>

#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace helix {
namespace gcode {

enum class GhostRenderMode : uint8_t { Dimmed = 0, Stipple = 1 };
constexpr GhostRenderMode kDefaultGhostRenderMode = GhostRenderMode::Stipple;

// ====== Named Constants (rendering parameters) ======

// Specular material defaults (plastic-like sheen)
constexpr float kDefaultSpecularIntensity = 0.25f;
constexpr float kDefaultSpecularShininess = 48.0f;

// Specular clamp ranges
constexpr float kMinSpecularIntensity = 0.0f;
constexpr float kMaxSpecularIntensity = 1.0f;
constexpr float kMinSpecularShininess = 1.0f;
constexpr float kMaxSpecularShininess = 128.0f;

// Lighting intensities
constexpr float kCameraLightIntensity = 0.6f;
constexpr float kFillLightIntensity = 0.2f;
constexpr float kAmbientIntensity = 0.25f;

// Background color (neutral gray for contrast with light and dark filaments)
constexpr float kBackgroundGray = 0.45f;
constexpr float kBackgroundGrayBlue = 0.47f;

// Default filament color (#26A69A teal)
constexpr glm::vec4 kDefaultFilamentColor{0.15f, 0.65f, 0.60f, 1.0f};

// Ghost layer default opacity (out of 255)
constexpr uint8_t kDefaultGhostOpacity = 5; // ~2% opacity — ghost layers should barely be visible

// Object picking screen-space threshold (pixels)
constexpr float kPickThresholdPx = 15.0f;

// Near-zero threshold for clipping space W division
constexpr float kClipSpaceWEpsilon = 0.0001f;

// Frame-skip epsilon for float comparisons
constexpr float kAngleEpsilon = 1e-5f;
constexpr float kZoomEpsilon = 1e-3f;

/// Return type for get_options()
struct RenderingOptions {
    bool show_extrusions = true;
    bool show_travels = false;
    int layer_start = -1;
    int layer_end = -1;
    std::string highlighted_object;
};

// ====== RAII Wrappers for GL Resource Handles ======
// Prevent resource leaks by tying GL object lifetime to C++ scope.
// These are lightweight (just a GLuint), movable, non-copyable.

struct GLBufferHandle {
    unsigned int id = 0;
    GLBufferHandle() = default;
    explicit GLBufferHandle(unsigned int existing_id) : id(existing_id) {}
    ~GLBufferHandle();
    GLBufferHandle(const GLBufferHandle&) = delete;
    GLBufferHandle& operator=(const GLBufferHandle&) = delete;
    GLBufferHandle(GLBufferHandle&& o) noexcept : id(o.id) {
        o.id = 0;
    }
    GLBufferHandle& operator=(GLBufferHandle&& o) noexcept {
        std::swap(id, o.id);
        return *this;
    }
    operator unsigned int() const {
        return id;
    }
};

struct GLFramebufferHandle {
    unsigned int id = 0;
    GLFramebufferHandle() = default;
    ~GLFramebufferHandle();
    GLFramebufferHandle(const GLFramebufferHandle&) = delete;
    GLFramebufferHandle& operator=(const GLFramebufferHandle&) = delete;
    GLFramebufferHandle(GLFramebufferHandle&& o) noexcept : id(o.id) {
        o.id = 0;
    }
    GLFramebufferHandle& operator=(GLFramebufferHandle&& o) noexcept {
        std::swap(id, o.id);
        return *this;
    }
    operator unsigned int() const {
        return id;
    }
};

struct GLRenderbufferHandle {
    unsigned int id = 0;
    GLRenderbufferHandle() = default;
    ~GLRenderbufferHandle();
    GLRenderbufferHandle(const GLRenderbufferHandle&) = delete;
    GLRenderbufferHandle& operator=(const GLRenderbufferHandle&) = delete;
    GLRenderbufferHandle(GLRenderbufferHandle&& o) noexcept : id(o.id) {
        o.id = 0;
    }
    GLRenderbufferHandle& operator=(GLRenderbufferHandle&& o) noexcept {
        std::swap(id, o.id);
        return *this;
    }
    operator unsigned int() const {
        return id;
    }
};

/// GPU-accelerated G-code 3D renderer using OpenGL ES 2.0
///
/// Renders to FBO, reads pixels back into lv_draw_buf_t for LVGL compositing.
/// Requires DRM+EGL display backend.
class GCodeGLESRenderer {
  public:
    GCodeGLESRenderer();
    ~GCodeGLESRenderer();

    GCodeGLESRenderer(const GCodeGLESRenderer&) = delete;
    GCodeGLESRenderer& operator=(const GCodeGLESRenderer&) = delete;

    // ====== Main Rendering Interface ======

    void render(lv_layer_t* layer, const ParsedGCodeFile& gcode, const GCodeCamera& camera,
                const lv_area_t* widget_coords);

    void set_viewport_size(int width, int height);
    void set_interaction_mode(bool interacting);
    bool is_interaction_mode() const {
        return interaction_mode_;
    }

    /// True while VBO upload is still in progress (caller should invalidate widget)
    bool is_uploading() const {
        return geometry_ && !geometry_uploaded_;
    }

    // ====== Color / Material ======

    void set_filament_color(const std::string& hex_color);
    void set_smooth_shading(bool enable);
    void set_extrusion_width(float width_mm);
    void set_simplification_tolerance(float tolerance_mm);
    void set_specular(float intensity, float shininess);
    void set_debug_face_colors(bool enable);

    // Color setters (lv_color_t interface used by gcode viewer widget)
    void set_extrusion_color(lv_color_t color);
    void set_tool_color_overrides(const std::vector<uint32_t>& ams_colors);
    void set_travel_color(lv_color_t) {}
    void set_brightness_factor(float) {}

    // ====== Rendering Options ======

    void set_show_travels(bool show);
    void set_show_extrusions(bool show);
    void set_layer_range(int start, int end);
    void set_highlighted_object(const std::string& name);
    void set_highlighted_objects(const std::unordered_set<std::string>& names);
    void set_excluded_objects(const std::unordered_set<std::string>& names);
    void set_global_opacity(lv_opa_t opacity);
    void reset_colors();
    void clear_cached_frame();
    RenderingOptions get_options() const;

    // ====== Object Picking ======

    std::optional<std::string> pick_object(const glm::vec2& screen_pos,
                                           const ParsedGCodeFile& gcode,
                                           const GCodeCamera& camera) const;

    // ====== Ghost Layer / Print Progress ======

    void set_print_progress_layer(int current_layer);
    void set_ghost_opacity(lv_opa_t opacity);
    void set_ghost_render_mode(GhostRenderMode mode);
    void set_content_offset_y(float offset_percent);
    GhostRenderMode get_ghost_render_mode() const {
        return ghost_render_mode_;
    }
    bool is_ghost_mode_enabled() const {
        return progress_layer_ >= 0;
    }
    int get_max_layer_index() const;

    // ====== Async Geometry Loading ======

    void set_prebuilt_geometry(std::unique_ptr<RibbonGeometry> geometry,
                               const std::string& filename);
    void set_prebuilt_coarse_geometry(std::unique_ptr<RibbonGeometry> geometry);

    // ====== Statistics ======

    size_t get_segments_rendered() const {
        return triangles_rendered_ / 2;
    }
    size_t get_geometry_color_count() const;
    size_t get_memory_usage() const;
    size_t get_triangle_count() const;

  private:
    // ====== GL Resource Management ======

    bool init_gl();
#if !LV_USE_SDL
    bool try_egl_display(void* native_display, const char* label);
#endif
    bool compile_shaders();
    bool create_fbo(int width, int height);
    void destroy_fbo();
    void destroy_gl();

    // ====== Geometry Upload ======

    struct LayerVBO {
        GLBufferHandle vbo;
        size_t vertex_count = 0;
    };

    void upload_geometry(const RibbonGeometry& geom, std::vector<LayerVBO>& vbos);

    /// Upload a time-budgeted batch of layers. Returns true when all layers are done.
    bool upload_geometry_chunk(const RibbonGeometry& geom, std::vector<LayerVBO>& vbos,
                               size_t& next_layer, size_t total_layers);

    void free_vbos(std::vector<LayerVBO>& vbos);

    // ====== Internal Rendering ======

    void render_to_fbo(const ParsedGCodeFile& gcode, const GCodeCamera& camera);
    void draw_layers(const std::vector<LayerVBO>& vbos, int layer_start, int layer_end,
                     float color_scale, float alpha);
    void blit_to_lvgl(lv_layer_t* layer, const lv_area_t* widget_coords);
    void draw_cached_to_lvgl(lv_layer_t* layer, const lv_area_t* widget_coords);

    // ====== Frame Skip ======

    struct CachedRenderState {
        float azimuth = -999.0f;
        float elevation = -999.0f;
        float distance = -999.0f;
        float zoom_level = -999.0f;
        glm::vec3 target{-999.0f};
        int progress_layer = -2;
        int layer_start = -2;
        int layer_end = -2;
        size_t highlight_count = 0;
        size_t exclude_count = 0;
        glm::vec4 filament_color{-1.0f};
        uint8_t ghost_opacity = 0;
        bool operator==(const CachedRenderState& o) const;
        bool operator!=(const CachedRenderState& o) const {
            return !(*this == o);
        }
    };

    // ====== GL Backend State ======

#if LV_USE_SDL
    // SDL GL backend (desktop)
    void* sdl_gl_window_ = nullptr;  // SDL_Window*
    void* sdl_gl_context_ = nullptr; // SDL_GLContext
#else
    // EGL backend (Pi/embedded)
    void* egl_display_ = nullptr; // EGLDisplay
    void* egl_context_ = nullptr; // EGLContext
    void* egl_surface_ = nullptr; // EGLSurface (PBuffer for non-surfaceless drivers)
    void* gbm_device_ = nullptr;  // struct gbm_device*
    int drm_fd_ = -1;             // DRM file descriptor (owned by us)
#endif
    bool gl_initialized_ = false;
    bool gl_init_failed_ = false; // Prevents repeated init attempts

    // ====== Shader State ======

    unsigned int program_ = 0; // GLuint
    // Uniform locations
    int u_mvp_ = -1;
    int u_normal_matrix_ = -1;
    int u_light_dir_ = -1;
    int u_light_color_ = -1;
    int u_ambient_ = -1;
    int u_base_color_ = -1;
    int u_specular_intensity_ = -1;
    int u_specular_shininess_ = -1;
    int u_model_view_ = -1;
    int u_base_alpha_ = -1;
    // Attribute locations
    int a_position_ = -1;
    int a_normal_ = -1;
    int a_color_ = -1;
    int u_use_vertex_color_ = -1;
    int u_color_scale_ = -1;

    // ====== FBO State ======

    GLFramebufferHandle fbo_;
    GLRenderbufferHandle color_rbo_;
    GLRenderbufferHandle depth_rbo_;
    int fbo_width_ = 0;
    int fbo_height_ = 0;

    // ====== Output Buffer ======

    lv_draw_buf_t* draw_buf_ = nullptr;
    int draw_buf_width_ = 0;
    int draw_buf_height_ = 0;

    // ====== Viewport ======

    int viewport_width_ = 800;
    int viewport_height_ = 480;
    bool interaction_mode_ = false;

    // ====== Geometry ======

    std::unique_ptr<RibbonGeometry> geometry_;
    RibbonGeometry* active_geometry_ = nullptr;
    std::string current_filename_;

    std::vector<LayerVBO> layer_vbos_;
    bool geometry_uploaded_ = false;
    size_t upload_next_layer_ = 0;   ///< Next layer to upload (incremental)
    size_t upload_total_layers_ = 0; ///< Total layers needing upload

    // ====== Configuration ======

    GCodeColorPalette palette_; ///< Tool color palette for per-vertex coloring
    std::mutex palette_mutex_;  ///< Guards geometry color palette reads/writes
    glm::vec4 filament_color_{kDefaultFilamentColor};
    float specular_intensity_ = kDefaultSpecularIntensity;
    float specular_shininess_ = kDefaultSpecularShininess;
    float extrusion_width_ = 0.5f;
    bool debug_face_colors_ = false;
    bool show_travels_ = false;
    bool show_extrusions_ = true;
    int layer_start_ = -1;
    int layer_end_ = -1;
    std::string highlighted_object_;
    std::unordered_set<std::string> highlighted_objects_;
    std::unordered_set<std::string> excluded_objects_;
    lv_opa_t global_opacity_ = LV_OPA_COVER;

    // ====== Ghost / Progress ======

    int progress_layer_ = -1;
    lv_opa_t ghost_opacity_ = kDefaultGhostOpacity;
    GhostRenderMode ghost_render_mode_ = kDefaultGhostRenderMode;
    float content_offset_y_percent_ = 0.0f;

    // ====== Frame Skip ======

    CachedRenderState cached_state_;
    bool frame_dirty_ = true;
    size_t triangles_rendered_ = 0;

    // ====== Readback Buffer (persistent to avoid per-frame allocation) ======

    std::vector<uint8_t> readback_buf_;

    // ====== Render Deferral (avoid blocking panel animations) ======

    int render_defer_frames_ = 0; ///< Skip N draw callbacks before first GPU render
};

} // namespace gcode
} // namespace helix

#endif // ENABLE_GLES_3D
