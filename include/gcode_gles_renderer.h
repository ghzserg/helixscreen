// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef ENABLE_GLES_3D

#include "gcode_camera.h"
#include "gcode_geometry_builder.h"
#include "gcode_parser.h"

#include <lvgl/lvgl.h>

#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace helix {
namespace gcode {

// Re-use GhostRenderMode from TinyGL header for API compat
// (already defined in gcode_tinygl_renderer.h when both exist)
#ifndef HELIX_GHOST_RENDER_MODE_DEFINED
#define HELIX_GHOST_RENDER_MODE_DEFINED
enum class GhostRenderMode : uint8_t { Dimmed = 0, Stipple = 1 };
constexpr GhostRenderMode kDefaultGhostRenderMode = GhostRenderMode::Stipple;
#endif

/// Return type for get_options() — matches TinyGL API
struct RenderingOptions {
    bool show_extrusions = true;
    bool show_travels = false;
    int layer_start = -1;
    int layer_end = -1;
    std::string highlighted_object;
};

/// GPU-accelerated G-code 3D renderer using OpenGL ES 2.0
///
/// Drop-in replacement for GCodeTinyGLRenderer. Renders to FBO,
/// reads pixels back into lv_draw_buf_t for LVGL compositing.
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

    // ====== Color / Material ======

    void set_filament_color(const std::string& hex_color);
    void set_smooth_shading(bool enable);
    void set_extrusion_width(float width_mm);
    void set_simplification_tolerance(float tolerance_mm);
    void set_specular(float intensity, float shininess);
    void set_debug_face_colors(bool enable);

    // Compatibility stubs (no-op)
    void set_extrusion_color(lv_color_t) {}
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
    RenderingOptions get_options() const;

    // ====== Object Picking ======

    std::optional<std::string> pick_object(const glm::vec2& screen_pos,
                                           const ParsedGCodeFile& gcode,
                                           const GCodeCamera& camera) const;

    // ====== Ghost Layer / Print Progress ======

    void set_print_progress_layer(int current_layer);
    void set_ghost_opacity(lv_opa_t opacity);
    void set_ghost_render_mode(GhostRenderMode mode);
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
    bool compile_shaders();
    bool create_fbo(int width, int height);
    void destroy_fbo();
    void destroy_gl();

    // ====== Geometry Upload ======

    struct LayerVBO {
        unsigned int vbo = 0; // GLuint
        size_t vertex_count = 0;
    };

    void upload_geometry(const RibbonGeometry& geom, std::vector<LayerVBO>& vbos);
    void free_vbos(std::vector<LayerVBO>& vbos);

    // ====== Internal Rendering ======

    void render_to_fbo(const ParsedGCodeFile& gcode, const GCodeCamera& camera);
    void draw_layers(const std::vector<LayerVBO>& vbos, int layer_start, int layer_end,
                     const glm::vec4& color, float ghost_alpha);
    void blit_to_lvgl(lv_layer_t* layer, const lv_area_t* widget_coords);

    // ====== Frame Skip ======

    struct CachedRenderState {
        float azimuth = -999.0f;
        float elevation = -999.0f;
        float distance = -999.0f;
        glm::vec3 target{-999.0f};
        int progress_layer = -2;
        int layer_start = -2;
        int layer_end = -2;
        size_t highlight_count = 0;
        size_t exclude_count = 0;
        bool operator==(const CachedRenderState& o) const;
        bool operator!=(const CachedRenderState& o) const {
            return !(*this == o);
        }
    };

    // ====== EGL State ======

    void* egl_display_ = nullptr; // EGLDisplay
    void* egl_context_ = nullptr; // EGLContext
    void* gbm_device_ = nullptr;  // struct gbm_device*
    int drm_fd_ = -1;             // DRM file descriptor (owned by us)
    bool gl_initialized_ = false;

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
    int u_ghost_alpha_ = -1;
    // Attribute locations
    int a_position_ = -1;
    int a_normal_ = -1;

    // ====== FBO State ======

    unsigned int fbo_ = 0;
    unsigned int color_rbo_ = 0;
    unsigned int depth_rbo_ = 0;
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
    std::unique_ptr<RibbonGeometry> coarse_geometry_;
    RibbonGeometry* active_geometry_ = nullptr;
    std::string current_filename_;

    std::vector<LayerVBO> layer_vbos_;
    std::vector<LayerVBO> coarse_layer_vbos_;
    bool geometry_uploaded_ = false;
    bool coarse_geometry_uploaded_ = false;

    // ====== Configuration ======

    glm::vec4 filament_color_{0.15f, 0.65f, 0.60f, 1.0f}; // #26A69A
    float specular_intensity_ = 0.10f;
    float specular_shininess_ = 20.0f;
    float extrusion_width_ = 0.5f;
    float simplification_tolerance_ = 0.15f;
    bool smooth_shading_ = false;
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
    lv_opa_t ghost_opacity_ = 77;
    GhostRenderMode ghost_render_mode_ = kDefaultGhostRenderMode;

    // ====== Frame Skip ======

    CachedRenderState cached_state_;
    bool frame_dirty_ = true;
    size_t triangles_rendered_ = 0;
};

} // namespace gcode
} // namespace helix

#endif // ENABLE_GLES_3D
