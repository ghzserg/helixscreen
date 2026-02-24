// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_gles_renderer.h"

#ifdef ENABLE_GLES_3D

#include "runtime_config.h"

#include <spdlog/spdlog.h>

// EGL/GLES2 system headers (DRM+EGL builds exclude GLAD include path in Makefile)
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <gbm.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <unistd.h>

namespace helix {
namespace gcode {

// ============================================================
// GLSL Shaders
// ============================================================

static const char* kVertexShaderSource = R"(
    // Gouraud lighting matching OrcaSlicer's gouraud_light.vs
    uniform mat4 u_mvp;
    uniform mat3 u_normal_matrix;
    uniform vec3 u_light_dir[2];
    uniform vec3 u_light_color[2];
    uniform vec3 u_ambient;
    uniform vec4 u_base_color;
    uniform float u_specular_intensity;
    uniform float u_specular_shininess;

    attribute vec3 a_position;
    attribute vec3 a_normal;

    varying vec4 v_color;

    void main() {
        gl_Position = u_mvp * vec4(a_position, 1.0);
        vec3 n = normalize(u_normal_matrix * a_normal);

        // Diffuse lighting from two directional lights
        vec3 diffuse = u_ambient;
        for (int i = 0; i < 2; i++) {
            float NdotL = max(dot(n, u_light_dir[i]), 0.0);
            diffuse += u_light_color[i] * NdotL;
        }

        // Specular (Blinn-Phong, view-space)
        vec3 view_dir = vec3(0.0, 0.0, 1.0);
        float spec = 0.0;
        for (int i = 0; i < 2; i++) {
            vec3 half_dir = normalize(u_light_dir[i] + view_dir);
            float s = max(dot(n, half_dir), 0.0);
            spec += pow(s, u_specular_shininess);
        }

        v_color = vec4(u_base_color.rgb * diffuse + vec3(spec * u_specular_intensity),
                       u_base_color.a);
    }
)";

static const char* kFragmentShaderSource = R"(
    precision mediump float;
    varying vec4 v_color;
    uniform float u_ghost_alpha;

    void main() {
        // Stipple emulation for ghost mode (screen-door transparency)
        if (u_ghost_alpha < 1.0) {
            // 50% checkerboard discard pattern
            vec2 fc = floor(gl_FragCoord.xy);
            if (mod(fc.x + fc.y, 2.0) < 0.5) discard;
        }
        gl_FragColor = v_color;
    }
)";

// ============================================================
// Lighting Constants (match TinyGL setup_lighting)
// ============================================================

static constexpr float INTENSITY_CORRECTION = 0.8f;
static constexpr float INTENSITY_AMBIENT = 0.2f;

// Top light: primary from above-right (OrcaSlicer LIGHT_TOP_DIR)
static constexpr glm::vec3 kLightTopDir{-0.4574957f, 0.4574957f, 0.7624929f};
static constexpr glm::vec3 kLightTopColor{0.8f * INTENSITY_CORRECTION, 0.8f * INTENSITY_CORRECTION,
                                          0.8f * INTENSITY_CORRECTION};

// Front light: fill from front-right (OrcaSlicer LIGHT_FRONT_DIR)
static constexpr glm::vec3 kLightFrontDir{0.6985074f, 0.1397015f, 0.6985074f};
static constexpr glm::vec3 kLightFrontColor{
    0.3f * INTENSITY_CORRECTION, 0.3f * INTENSITY_CORRECTION, 0.3f * INTENSITY_CORRECTION};

static constexpr glm::vec3 kAmbientColor{INTENSITY_AMBIENT, INTENSITY_AMBIENT, INTENSITY_AMBIENT};

// ============================================================
// Construction / Destruction
// ============================================================

GCodeGLESRenderer::GCodeGLESRenderer() {
    spdlog::debug("[GCode GLES] GCodeGLESRenderer created");
}

GCodeGLESRenderer::~GCodeGLESRenderer() {
    destroy_gl();

    if (draw_buf_) {
        lv_draw_buf_destroy(draw_buf_);
        draw_buf_ = nullptr;
    }

    spdlog::trace("[GCode GLES] GCodeGLESRenderer destroyed");
}

// ============================================================
// GL Initialization
// ============================================================

bool GCodeGLESRenderer::init_gl() {
    if (gl_initialized_)
        return true;

    // Create an EGL context for offscreen FBO rendering.
    // Path 1: GBM device (Pi/DRM) — open /dev/dri/card* and create GBM-backed display
    // Path 2: Default EGL display (desktop Linux with Mesa) — no DRM needed
    EGLint major, minor;

    // Try GBM/DRM first (embedded Pi builds)
    static const char* kDrmDevices[] = {"/dev/dri/card1", "/dev/dri/card0", nullptr};
    for (int i = 0; kDrmDevices[i] && drm_fd_ < 0; ++i) {
        drm_fd_ = open(kDrmDevices[i], O_RDWR | O_CLOEXEC);
        if (drm_fd_ >= 0) {
            spdlog::debug("[GCode GLES] Opened DRM device: {}", kDrmDevices[i]);
        }
    }

    if (drm_fd_ >= 0) {
        gbm_device_ = gbm_create_device(drm_fd_);
        if (gbm_device_) {
            egl_display_ = eglGetDisplay(static_cast<EGLNativeDisplayType>(gbm_device_));
        }
    }

    // Fallback: default EGL display (desktop Mesa, X11/Wayland)
    if (egl_display_ == EGL_NO_DISPLAY || !egl_display_) {
        egl_display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        spdlog::debug("[GCode GLES] Using default EGL display");
    }

    if (!egl_display_ || egl_display_ == EGL_NO_DISPLAY) {
        spdlog::warn("[GCode GLES] No EGL display available — GPU rendering unavailable");
        destroy_gl();
        return false;
    }

    if (!eglInitialize(static_cast<EGLDisplay>(egl_display_), &major, &minor)) {
        spdlog::error("[GCode GLES] eglInitialize failed: 0x{:X}", eglGetError());
        destroy_gl();
        return false;
    }
    spdlog::info("[GCode GLES] EGL {}.{} initialized", major, minor);

    // Bind OpenGL ES API
    eglBindAPI(EGL_OPENGL_ES_API);

    // Choose EGL config (offscreen only — no surface needed)
    EGLint config_attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE,
                               0, // No surface, FBO only
                               EGL_NONE};
    EGLConfig egl_config;
    EGLint num_configs;
    if (!eglChooseConfig(static_cast<EGLDisplay>(egl_display_), config_attribs, &egl_config, 1,
                         &num_configs) ||
        num_configs == 0) {
        spdlog::error("[GCode GLES] No suitable EGL config found");
        destroy_gl();
        return false;
    }

    // Create context
    EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    egl_context_ = eglCreateContext(static_cast<EGLDisplay>(egl_display_), egl_config,
                                    EGL_NO_CONTEXT, ctx_attribs);
    if (egl_context_ == EGL_NO_CONTEXT) {
        spdlog::error("[GCode GLES] Failed to create EGL context: 0x{:X}", eglGetError());
        destroy_gl();
        return false;
    }

    spdlog::info("[GCode GLES] EGL context created (standalone, offscreen FBO)");

    // Compile shaders
    if (!compile_shaders()) {
        destroy_gl();
        return false;
    }

    gl_initialized_ = true;
    return true;
}

static GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        spdlog::error("[GCode GLES] Shader compile error: {}", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool GCodeGLESRenderer::compile_shaders() {
    // Must make our context current for GL calls
    if (!eglMakeCurrent(static_cast<EGLDisplay>(egl_display_), EGL_NO_SURFACE, EGL_NO_SURFACE,
                        static_cast<EGLContext>(egl_context_))) {
        spdlog::error("[GCode GLES] eglMakeCurrent failed: 0x{:X}", eglGetError());
        return false;
    }

    GLuint vs = compile_shader(GL_VERTEX_SHADER, kVertexShaderSource);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFragmentShaderSource);
    if (!vs || !fs) {
        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
        eglMakeCurrent(static_cast<EGLDisplay>(egl_display_), EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        return false;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);

    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        spdlog::error("[GCode GLES] Program link error: {}", log);
        glDeleteProgram(program_);
        program_ = 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!program_) {
        eglMakeCurrent(static_cast<EGLDisplay>(egl_display_), EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        return false;
    }

    // Cache uniform/attribute locations
    u_mvp_ = glGetUniformLocation(program_, "u_mvp");
    u_normal_matrix_ = glGetUniformLocation(program_, "u_normal_matrix");
    u_light_dir_ = glGetUniformLocation(program_, "u_light_dir");
    u_light_color_ = glGetUniformLocation(program_, "u_light_color");
    u_ambient_ = glGetUniformLocation(program_, "u_ambient");
    u_base_color_ = glGetUniformLocation(program_, "u_base_color");
    u_specular_intensity_ = glGetUniformLocation(program_, "u_specular_intensity");
    u_specular_shininess_ = glGetUniformLocation(program_, "u_specular_shininess");
    u_ghost_alpha_ = glGetUniformLocation(program_, "u_ghost_alpha");
    a_position_ = glGetAttribLocation(program_, "a_position");
    a_normal_ = glGetAttribLocation(program_, "a_normal");

    spdlog::debug("[GCode GLES] Shaders compiled and linked (program={})", program_);

    eglMakeCurrent(static_cast<EGLDisplay>(egl_display_), EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    return true;
}

bool GCodeGLESRenderer::create_fbo(int width, int height) {
    if (fbo_ && fbo_width_ == width && fbo_height_ == height) {
        return true; // Already correct size
    }

    destroy_fbo();

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // Color renderbuffer (RGBA8)
    glGenRenderbuffers(1, &color_rbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, color_rbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_rbo_);

    // Depth renderbuffer (16-bit)
    glGenRenderbuffers(1, &depth_rbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rbo_);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        spdlog::error("[GCode GLES] FBO incomplete: 0x{:X}", status);
        destroy_fbo();
        return false;
    }

    fbo_width_ = width;
    fbo_height_ = height;
    spdlog::debug("[GCode GLES] FBO created: {}x{}", width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void GCodeGLESRenderer::destroy_fbo() {
    if (depth_rbo_) {
        glDeleteRenderbuffers(1, &depth_rbo_);
        depth_rbo_ = 0;
    }
    if (color_rbo_) {
        glDeleteRenderbuffers(1, &color_rbo_);
        color_rbo_ = 0;
    }
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    fbo_width_ = 0;
    fbo_height_ = 0;
}

void GCodeGLESRenderer::destroy_gl() {
    if (!gl_initialized_)
        return;

    // Make context current for cleanup
    if (egl_display_ && egl_context_) {
        eglMakeCurrent(static_cast<EGLDisplay>(egl_display_), EGL_NO_SURFACE, EGL_NO_SURFACE,
                       static_cast<EGLContext>(egl_context_));
    }

    free_vbos(layer_vbos_);
    free_vbos(coarse_layer_vbos_);
    destroy_fbo();

    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }

    if (egl_display_ && egl_context_) {
        eglMakeCurrent(static_cast<EGLDisplay>(egl_display_), EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        eglDestroyContext(static_cast<EGLDisplay>(egl_display_),
                          static_cast<EGLContext>(egl_context_));
        egl_context_ = nullptr;
    }

    if (egl_display_) {
        eglTerminate(static_cast<EGLDisplay>(egl_display_));
        egl_display_ = nullptr;
    }

    if (gbm_device_) {
        gbm_device_destroy(static_cast<struct gbm_device*>(gbm_device_));
        gbm_device_ = nullptr;
    }

    if (drm_fd_ >= 0) {
        close(drm_fd_);
        drm_fd_ = -1;
    }

    gl_initialized_ = false;
    geometry_uploaded_ = false;
    coarse_geometry_uploaded_ = false;
    spdlog::debug("[GCode GLES] GL resources destroyed");
}

// ============================================================
// Geometry Upload
// ============================================================

void GCodeGLESRenderer::upload_geometry(const RibbonGeometry& geom, std::vector<LayerVBO>& vbos) {
    free_vbos(vbos);

    if (geom.strips.empty() || geom.vertices.empty()) {
        return;
    }

    // Determine number of layers
    size_t num_layers = geom.layer_strip_ranges.empty() ? 1 : geom.layer_strip_ranges.size();

    vbos.resize(num_layers);

    // Interleaved vertex format: position(3f) + normal(3f) = 24 bytes per vertex
    constexpr size_t kVertexStride = 6 * sizeof(float);

    for (size_t layer = 0; layer < num_layers; ++layer) {
        size_t first_strip = 0;
        size_t strip_count = geom.strips.size();

        if (!geom.layer_strip_ranges.empty()) {
            auto [fs, sc] = geom.layer_strip_ranges[layer];
            first_strip = fs;
            strip_count = sc;
        }

        if (strip_count == 0) {
            vbos[layer].vbo = 0;
            vbos[layer].vertex_count = 0;
            continue;
        }

        // Each strip = 4 vertices → 2 triangles → 6 vertices (for GL_TRIANGLES)
        size_t total_verts = strip_count * 6;
        std::vector<float> buf(total_verts * 6); // 6 floats per vertex

        size_t out_idx = 0;
        for (size_t s = 0; s < strip_count; ++s) {
            const auto& strip = geom.strips[first_strip + s];
            // Strip order: BL(0), BR(1), TL(2), TR(3)
            // Triangle 1: BL-BR-TL,  Triangle 2: BR-TR-TL
            static constexpr int kTriIndices[6] = {0, 1, 2, 1, 3, 2};

            for (int ti = 0; ti < 6; ++ti) {
                const auto& vert = geom.vertices[strip[static_cast<size_t>(kTriIndices[ti])]];
                glm::vec3 pos = geom.quantization.dequantize_vec3(vert.position);
                const glm::vec3& normal = geom.normal_palette[vert.normal_index];

                buf[out_idx++] = pos.x;
                buf[out_idx++] = pos.y;
                buf[out_idx++] = pos.z;
                buf[out_idx++] = normal.x;
                buf[out_idx++] = normal.y;
                buf[out_idx++] = normal.z;
            }
        }

        GLuint vbo = 0;
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(total_verts * kVertexStride),
                     buf.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        vbos[layer].vbo = vbo;
        vbos[layer].vertex_count = total_verts;
    }

    spdlog::debug("[GCode GLES] Uploaded {} layers, {} total strips to VBOs", num_layers,
                  geom.strips.size());
}

void GCodeGLESRenderer::free_vbos(std::vector<LayerVBO>& vbos) {
    for (auto& lv : vbos) {
        if (lv.vbo) {
            GLuint vbo = lv.vbo;
            glDeleteBuffers(1, &vbo);
            lv.vbo = 0;
            lv.vertex_count = 0;
        }
    }
    vbos.clear();
}

// ============================================================
// Main Render Entry Point
// ============================================================

void GCodeGLESRenderer::render(lv_layer_t* layer, const ParsedGCodeFile& gcode,
                               const GCodeCamera& camera, const lv_area_t* widget_coords) {
    // Initialize GL on first render
    if (!gl_initialized_) {
        if (!init_gl()) {
            return; // GPU not available
        }
    }

    // No geometry loaded
    if (!geometry_)
        return;

    // Make our EGL context current
    if (!eglMakeCurrent(static_cast<EGLDisplay>(egl_display_), EGL_NO_SURFACE, EGL_NO_SURFACE,
                        static_cast<EGLContext>(egl_context_))) {
        spdlog::error("[GCode GLES] eglMakeCurrent failed: 0x{:X}", eglGetError());
        return;
    }

    // Upload geometry to VBOs if needed
    if (!geometry_uploaded_ && geometry_) {
        upload_geometry(*geometry_, layer_vbos_);
        geometry_uploaded_ = true;
    }
    if (!coarse_geometry_uploaded_ && coarse_geometry_) {
        upload_geometry(*coarse_geometry_, coarse_layer_vbos_);
        coarse_geometry_uploaded_ = true;
    }

    // Build current render state for frame-skip check
    CachedRenderState current_state;
    current_state.azimuth = camera.get_azimuth();
    current_state.elevation = camera.get_elevation();
    current_state.distance = camera.get_distance();
    current_state.target = camera.get_target();
    current_state.progress_layer = progress_layer_;
    current_state.layer_start = layer_start_;
    current_state.layer_end = layer_end_;
    current_state.highlight_count = highlighted_objects_.size();
    current_state.exclude_count = excluded_objects_.size();

    // Skip GPU render if state unchanged and we have a valid cached framebuffer
    if (!frame_dirty_ && current_state == cached_state_ && draw_buf_) {
        blit_to_lvgl(layer, widget_coords);
        eglMakeCurrent(static_cast<EGLDisplay>(egl_display_), EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        return;
    }

    cached_state_ = current_state;
    frame_dirty_ = false;

    auto t0 = std::chrono::high_resolution_clock::now();

    // Render to FBO
    render_to_fbo(gcode, camera);

    auto t1 = std::chrono::high_resolution_clock::now();

    // Blit to LVGL
    blit_to_lvgl(layer, widget_coords);

    auto t2 = std::chrono::high_resolution_clock::now();

    // Release context
    eglMakeCurrent(static_cast<EGLDisplay>(egl_display_), EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);

    auto gpu_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    auto blit_ms = std::chrono::duration<float, std::milli>(t2 - t1).count();
    spdlog::trace("[GCode GLES] gpu={:.1f}ms, blit={:.1f}ms, triangles={}", gpu_ms, blit_ms,
                  triangles_rendered_);
}

// ============================================================
// FBO Rendering
// ============================================================

void GCodeGLESRenderer::render_to_fbo(const ParsedGCodeFile& /*gcode*/, const GCodeCamera& camera) {
    int render_w = viewport_width_;
    int render_h = viewport_height_;

    // Half resolution during interaction for faster frames
    if (interaction_mode_) {
        render_w /= 2;
        render_h /= 2;
    }
    if (render_w < 1)
        render_w = 1;
    if (render_h < 1)
        render_h = 1;

    // Create/resize FBO
    if (!create_fbo(render_w, render_h)) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, render_w, render_h);

    // Clear with dark background
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    // Select active geometry (LOD for interaction)
    auto* active_vbos = &layer_vbos_;
    active_geometry_ = geometry_.get();
    if (interaction_mode_ && coarse_geometry_ && coarse_geometry_uploaded_) {
        active_vbos = &coarse_layer_vbos_;
        active_geometry_ = coarse_geometry_.get();
    }

    if (!active_geometry_ || active_vbos->empty()) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    // Use shader program
    glUseProgram(program_);

    // Model transform: rotate 180° around Z to match slicer orientation
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0, 0, 1));
    glm::mat4 view = camera.get_view_matrix();
    glm::mat4 proj = camera.get_projection_matrix();
    glm::mat4 mvp = proj * view * model;

    // Normal matrix (inverse transpose of upper-left 3x3 of model-view)
    glm::mat3 normal_mat = glm::transpose(glm::inverse(glm::mat3(view * model)));

    // Set uniforms
    glUniformMatrix4fv(u_mvp_, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix3fv(u_normal_matrix_, 1, GL_FALSE, glm::value_ptr(normal_mat));

    // Lighting
    glm::vec3 light_dirs[2] = {kLightTopDir, kLightFrontDir};
    glm::vec3 light_colors[2] = {kLightTopColor, kLightFrontColor};
    glUniform3fv(u_light_dir_, 2, glm::value_ptr(light_dirs[0]));
    glUniform3fv(u_light_color_, 2, glm::value_ptr(light_colors[0]));
    glUniform3fv(u_ambient_, 1, glm::value_ptr(kAmbientColor));

    // Material
    glUniform1f(u_specular_intensity_, specular_intensity_);
    glUniform1f(u_specular_shininess_, specular_shininess_);

    // Determine layer range
    int max_layer = static_cast<int>(active_vbos->size()) - 1;
    int draw_start = (layer_start_ >= 0) ? layer_start_ : 0;
    int draw_end = (layer_end_ >= 0) ? std::min(layer_end_, max_layer) : max_layer;

    triangles_rendered_ = 0;

    // Ghost / print progress rendering
    if (progress_layer_ >= 0 && progress_layer_ < max_layer) {
        // Pass 1: Solid layers (0 to progress_layer_)
        int solid_end = std::min(progress_layer_, draw_end);
        if (draw_start <= solid_end) {
            draw_layers(*active_vbos, draw_start, solid_end, filament_color_, 1.0f);
        }

        // Pass 2: Ghost layers (progress_layer_+1 to end)
        int ghost_start = std::max(progress_layer_ + 1, draw_start);
        if (ghost_start <= draw_end) {
            float dim = ghost_opacity_ / 255.0f;
            glm::vec4 ghost_color = filament_color_ * dim;
            ghost_color.a = filament_color_.a;
            draw_layers(*active_vbos, ghost_start, draw_end, ghost_color,
                        (ghost_render_mode_ == GhostRenderMode::Stipple) ? 0.5f : 1.0f);
        }
    } else {
        // Normal: all layers solid
        draw_layers(*active_vbos, draw_start, draw_end, filament_color_, 1.0f);
    }

    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GCodeGLESRenderer::draw_layers(const std::vector<LayerVBO>& vbos, int layer_start,
                                    int layer_end, const glm::vec4& color, float ghost_alpha) {
    glUniform4fv(u_base_color_, 1, glm::value_ptr(color));
    glUniform1f(u_ghost_alpha_, ghost_alpha);

    constexpr size_t kStride = 6 * sizeof(float);

    for (int layer = layer_start; layer <= layer_end; ++layer) {
        if (layer < 0 || layer >= static_cast<int>(vbos.size()))
            continue;
        const auto& lv = vbos[static_cast<size_t>(layer)];
        if (!lv.vbo || lv.vertex_count == 0)
            continue;

        glBindBuffer(GL_ARRAY_BUFFER, lv.vbo);

        glEnableVertexAttribArray(static_cast<GLuint>(a_position_));
        glVertexAttribPointer(static_cast<GLuint>(a_position_), 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(kStride), reinterpret_cast<void*>(0));

        glEnableVertexAttribArray(static_cast<GLuint>(a_normal_));
        glVertexAttribPointer(static_cast<GLuint>(a_normal_), 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(kStride),
                              reinterpret_cast<void*>(3 * sizeof(float)));

        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(lv.vertex_count));
        triangles_rendered_ += lv.vertex_count / 3;
    }

    glDisableVertexAttribArray(static_cast<GLuint>(a_position_));
    glDisableVertexAttribArray(static_cast<GLuint>(a_normal_));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ============================================================
// LVGL Output
// ============================================================

void GCodeGLESRenderer::blit_to_lvgl(lv_layer_t* layer, const lv_area_t* widget_coords) {
    int widget_w = lv_area_get_width(widget_coords);
    int widget_h = lv_area_get_height(widget_coords);

    // Create or recreate draw buffer at widget size
    if (!draw_buf_ || draw_buf_width_ != widget_w || draw_buf_height_ != widget_h) {
        if (draw_buf_) {
            lv_draw_buf_destroy(draw_buf_);
        }
        draw_buf_ = lv_draw_buf_create(static_cast<uint32_t>(widget_w),
                                       static_cast<uint32_t>(widget_h), LV_COLOR_FORMAT_RGB888, 0);
        if (!draw_buf_) {
            spdlog::error("[GCode GLES] Failed to create draw buffer");
            return;
        }
        draw_buf_width_ = widget_w;
        draw_buf_height_ = widget_h;
    }

    if (!fbo_)
        return;

    // Read pixels from FBO
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // Read RGBA from GPU
    std::vector<uint8_t> rgba(static_cast<size_t>(fbo_width_ * fbo_height_ * 4));
    glReadPixels(0, 0, fbo_width_, fbo_height_, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Convert RGBA → RGB888, flip Y (OpenGL origin is bottom-left), and scale if needed
    auto* dest = static_cast<uint8_t*>(draw_buf_->data);
    bool needs_scale = (fbo_width_ != widget_w || fbo_height_ != widget_h);

    for (int dy = 0; dy < widget_h; ++dy) {
        for (int dx = 0; dx < widget_w; ++dx) {
            int sx, sy;
            if (needs_scale) {
                sx = dx * fbo_width_ / widget_w;
                sy = dy * fbo_height_ / widget_h;
            } else {
                sx = dx;
                sy = dy;
            }
            // Flip Y: OpenGL row 0 = bottom
            int gl_row = fbo_height_ - 1 - sy;
            size_t src_idx = static_cast<size_t>((gl_row * fbo_width_ + sx) * 4);
            size_t dst_idx = static_cast<size_t>((dy * widget_w + dx) * 3);

            dest[dst_idx + 0] = rgba[src_idx + 0]; // R
            dest[dst_idx + 1] = rgba[src_idx + 1]; // G
            dest[dst_idx + 2] = rgba[src_idx + 2]; // B
        }
    }

    // Draw to LVGL layer
    lv_draw_image_dsc_t img_dsc;
    lv_draw_image_dsc_init(&img_dsc);
    img_dsc.src = draw_buf_;

    lv_area_t area = *widget_coords;
    lv_draw_image(layer, &img_dsc, &area);
}

// ============================================================
// CachedRenderState
// ============================================================

bool GCodeGLESRenderer::CachedRenderState::operator==(const CachedRenderState& o) const {
    return azimuth == o.azimuth && elevation == o.elevation && distance == o.distance &&
           target == o.target && progress_layer == o.progress_layer &&
           layer_start == o.layer_start && layer_end == o.layer_end &&
           highlight_count == o.highlight_count && exclude_count == o.exclude_count;
}

// ============================================================
// Configuration Methods
// ============================================================

void GCodeGLESRenderer::set_viewport_size(int width, int height) {
    if (width == viewport_width_ && height == viewport_height_)
        return;
    viewport_width_ = width;
    viewport_height_ = height;
    frame_dirty_ = true;
}

void GCodeGLESRenderer::set_interaction_mode(bool interacting) {
    if (interaction_mode_ == interacting)
        return;
    interaction_mode_ = interacting;
    frame_dirty_ = true;
}

void GCodeGLESRenderer::set_filament_color(const std::string& hex_color) {
    if (hex_color.size() < 7 || hex_color[0] != '#')
        return;
    unsigned int r = 0, g = 0, b = 0;
    if (sscanf(hex_color.c_str(), "#%02x%02x%02x", &r, &g, &b) == 3) {
        filament_color_ = glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
        frame_dirty_ = true;
    }
}

void GCodeGLESRenderer::set_smooth_shading(bool enable) {
    smooth_shading_ = enable;
    frame_dirty_ = true;
}

void GCodeGLESRenderer::set_extrusion_width(float width_mm) {
    extrusion_width_ = width_mm;
}

void GCodeGLESRenderer::set_simplification_tolerance(float tolerance_mm) {
    simplification_tolerance_ = tolerance_mm;
}

void GCodeGLESRenderer::set_specular(float intensity, float shininess) {
    specular_intensity_ = std::clamp(intensity, 0.0f, 1.0f);
    specular_shininess_ = std::clamp(shininess, 1.0f, 128.0f);
    frame_dirty_ = true;
}

void GCodeGLESRenderer::set_debug_face_colors(bool enable) {
    debug_face_colors_ = enable;
    frame_dirty_ = true;
}

void GCodeGLESRenderer::set_show_travels(bool show) {
    show_travels_ = show;
    frame_dirty_ = true;
}

void GCodeGLESRenderer::set_show_extrusions(bool show) {
    show_extrusions_ = show;
    frame_dirty_ = true;
}

void GCodeGLESRenderer::set_layer_range(int start, int end) {
    layer_start_ = start;
    layer_end_ = end;
    frame_dirty_ = true;
}

void GCodeGLESRenderer::set_highlighted_object(const std::string& name) {
    std::unordered_set<std::string> objects;
    if (!name.empty())
        objects.insert(name);
    set_highlighted_objects(objects);
}

void GCodeGLESRenderer::set_highlighted_objects(const std::unordered_set<std::string>& names) {
    if (highlighted_objects_ != names) {
        highlighted_objects_ = names;
        frame_dirty_ = true;
    }
}

void GCodeGLESRenderer::set_excluded_objects(const std::unordered_set<std::string>& names) {
    if (excluded_objects_ != names) {
        excluded_objects_ = names;
        frame_dirty_ = true;
    }
}

void GCodeGLESRenderer::set_global_opacity(lv_opa_t opacity) {
    global_opacity_ = opacity;
    frame_dirty_ = true;
}

void GCodeGLESRenderer::reset_colors() {
    filament_color_ = glm::vec4(0.15f, 0.65f, 0.60f, 1.0f);
    frame_dirty_ = true;
}

RenderingOptions GCodeGLESRenderer::get_options() const {
    RenderingOptions opts;
    opts.show_extrusions = show_extrusions_;
    opts.show_travels = show_travels_;
    opts.layer_start = layer_start_;
    opts.layer_end = layer_end_;
    opts.highlighted_object = highlighted_object_;
    return opts;
}

// ============================================================
// Ghost / Print Progress
// ============================================================

void GCodeGLESRenderer::set_print_progress_layer(int current_layer) {
    if (progress_layer_ != current_layer) {
        progress_layer_ = current_layer;
        frame_dirty_ = true;
    }
}

void GCodeGLESRenderer::set_ghost_opacity(lv_opa_t opacity) {
    ghost_opacity_ = opacity;
    frame_dirty_ = true;
}

void GCodeGLESRenderer::set_ghost_render_mode(GhostRenderMode mode) {
    ghost_render_mode_ = mode;
    frame_dirty_ = true;
}

int GCodeGLESRenderer::get_max_layer_index() const {
    if (geometry_)
        return static_cast<int>(geometry_->max_layer_index);
    return 0;
}

// ============================================================
// Geometry Loading
// ============================================================

void GCodeGLESRenderer::set_prebuilt_geometry(std::unique_ptr<RibbonGeometry> geometry,
                                              const std::string& filename) {
    geometry_ = std::move(geometry);
    current_filename_ = filename;
    geometry_uploaded_ = false;
    frame_dirty_ = true;
    spdlog::debug("[GCode GLES] Geometry set: {} strips, {} vertices",
                  geometry_ ? geometry_->strips.size() : 0,
                  geometry_ ? geometry_->vertices.size() : 0);
}

void GCodeGLESRenderer::set_prebuilt_coarse_geometry(std::unique_ptr<RibbonGeometry> geometry) {
    coarse_geometry_ = std::move(geometry);
    coarse_geometry_uploaded_ = false;
    spdlog::debug("[GCode GLES] Coarse geometry set: {} strips",
                  coarse_geometry_ ? coarse_geometry_->strips.size() : 0);
}

// ============================================================
// Statistics
// ============================================================

size_t GCodeGLESRenderer::get_geometry_color_count() const {
    if (geometry_)
        return geometry_->color_palette.size();
    return 0;
}

size_t GCodeGLESRenderer::get_memory_usage() const {
    size_t total = sizeof(*this);
    if (geometry_) {
        total += geometry_->vertices.size() * sizeof(RibbonVertex);
        total += geometry_->strips.size() * sizeof(TriangleStrip);
        total += geometry_->normal_palette.size() * sizeof(glm::vec3);
    }
    if (draw_buf_) {
        total += static_cast<size_t>(draw_buf_width_ * draw_buf_height_ * 3);
    }
    return total;
}

size_t GCodeGLESRenderer::get_triangle_count() const {
    if (geometry_)
        return geometry_->extrusion_triangle_count;
    return 0;
}

// ============================================================
// Object Picking (CPU-side, no GL needed)
// ============================================================

std::optional<std::string> GCodeGLESRenderer::pick_object(const glm::vec2& screen_pos,
                                                          const ParsedGCodeFile& gcode,
                                                          const GCodeCamera& camera) const {
    glm::mat4 transform = camera.get_view_projection_matrix();
    float closest_distance = std::numeric_limits<float>::max();
    std::optional<std::string> picked_object;

    constexpr float PICK_THRESHOLD = 15.0f;

    int ls = layer_start_;
    int le = (layer_end_ < 0 || layer_end_ >= static_cast<int>(gcode.layers.size()))
                 ? static_cast<int>(gcode.layers.size()) - 1
                 : layer_end_;

    for (int layer_idx = ls; layer_idx <= le; ++layer_idx) {
        if (layer_idx < 0 || layer_idx >= static_cast<int>(gcode.layers.size()))
            continue;
        const auto& layer = gcode.layers[static_cast<size_t>(layer_idx)];

        for (const auto& segment : layer.segments) {
            if (!segment.is_extrusion || !show_extrusions_)
                continue;
            if (segment.object_name.empty())
                continue;

            glm::vec4 start_clip = transform * glm::vec4(segment.start, 1.0f);
            glm::vec4 end_clip = transform * glm::vec4(segment.end, 1.0f);

            if (std::abs(start_clip.w) < 0.0001f || std::abs(end_clip.w) < 0.0001f)
                continue;

            glm::vec3 start_ndc = glm::vec3(start_clip) / start_clip.w;
            glm::vec3 end_ndc = glm::vec3(end_clip) / end_clip.w;

            if (start_ndc.x < -1 || start_ndc.x > 1 || start_ndc.y < -1 || start_ndc.y > 1 ||
                end_ndc.x < -1 || end_ndc.x > 1 || end_ndc.y < -1 || end_ndc.y > 1) {
                continue;
            }

            glm::vec2 start_screen((start_ndc.x + 1) * 0.5f * viewport_width_,
                                   (1 - start_ndc.y) * 0.5f * viewport_height_);
            glm::vec2 end_screen((end_ndc.x + 1) * 0.5f * viewport_width_,
                                 (1 - end_ndc.y) * 0.5f * viewport_height_);

            glm::vec2 v = end_screen - start_screen;
            glm::vec2 w = screen_pos - start_screen;
            float len_sq = glm::dot(v, v);
            float t = (len_sq > 0.0001f) ? std::clamp(glm::dot(w, v) / len_sq, 0.0f, 1.0f) : 0.0f;
            float dist = glm::length(screen_pos - (start_screen + t * v));

            if (dist < PICK_THRESHOLD && dist < closest_distance) {
                closest_distance = dist;
                picked_object = segment.object_name;
            }
        }
    }
    return picked_object;
}

} // namespace gcode
} // namespace helix

#endif // ENABLE_GLES_3D
