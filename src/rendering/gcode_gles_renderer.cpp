// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_gles_renderer.h"

#ifdef ENABLE_GLES_3D

#include "runtime_config.h"

#include <spdlog/spdlog.h>

// GL backend selection: SDL_GL on desktop (LV_USE_SDL), EGL+GBM on embedded
#if LV_USE_SDL
#include <SDL.h>
#else
#include <EGL/egl.h>
#include <fcntl.h>
#include <gbm.h>
#include <unistd.h>
#endif

// GLES2 function declarations and common headers (both paths)
#include <GLES2/gl2.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace helix {
namespace gcode {

// ============================================================
// GL Context Save/Restore (RAII)
// ============================================================
// The LVGL display backend may have a GL context bound on this thread.
// We must save, bind ours, and restore on scope exit.

#if LV_USE_SDL

class SdlGlContextGuard {
  public:
    SdlGlContextGuard(void* our_window, void* our_context) {
        saved_context_ = SDL_GL_GetCurrentContext();
        saved_window_ = SDL_GL_GetCurrentWindow();

        int rc = SDL_GL_MakeCurrent(static_cast<SDL_Window*>(our_window),
                                    static_cast<SDL_GLContext>(our_context));
        if (rc != 0) {
            spdlog::error("[GCode GLES] SDL_GL_MakeCurrent failed: {}", SDL_GetError());
            // Restore previous context on failure
            if (saved_context_) {
                SDL_GL_MakeCurrent(saved_window_, saved_context_);
            }
        } else {
            ok_ = true;
            our_window_ = our_window;
        }
    }

    ~SdlGlContextGuard() {
        if (!ok_)
            return;
        // Restore previous context (LVGL's SDL renderer)
        if (saved_context_) {
            SDL_GL_MakeCurrent(saved_window_, saved_context_);
        } else {
            // No prior context — unbind ours
            SDL_GL_MakeCurrent(static_cast<SDL_Window*>(our_window_), nullptr);
        }
    }

    bool ok() const {
        return ok_;
    }

    SdlGlContextGuard(const SdlGlContextGuard&) = delete;
    SdlGlContextGuard& operator=(const SdlGlContextGuard&) = delete;

  private:
    SDL_GLContext saved_context_ = nullptr;
    SDL_Window* saved_window_ = nullptr;
    void* our_window_ = nullptr;
    bool ok_ = false;
};

#else // !LV_USE_SDL — EGL backend

class EglContextGuard {
  public:
    EglContextGuard(void* our_display, void* our_surface, void* our_context) {
        saved_display_ = eglGetCurrentDisplay();
        saved_context_ = eglGetCurrentContext();
        saved_draw_ = eglGetCurrentSurface(EGL_DRAW);
        saved_read_ = eglGetCurrentSurface(EGL_READ);

        // Release current context so we can bind ours
        if (saved_context_ != EGL_NO_CONTEXT) {
            eglMakeCurrent(saved_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }

        auto surface = our_surface ? static_cast<EGLSurface>(our_surface) : EGL_NO_SURFACE;
        ok_ = eglMakeCurrent(static_cast<EGLDisplay>(our_display), surface, surface,
                             static_cast<EGLContext>(our_context));
        if (!ok_) {
            spdlog::error("[GCode GLES] eglMakeCurrent failed: 0x{:X}", eglGetError());
            // Restore previous context on failure
            if (saved_context_ != EGL_NO_CONTEXT) {
                eglMakeCurrent(saved_display_, saved_draw_, saved_read_, saved_context_);
            }
        }
    }

    ~EglContextGuard() {
        if (!ok_)
            return;
        // Release our context
        auto display = eglGetCurrentDisplay();
        if (display != EGL_NO_DISPLAY) {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        // Restore previous context (SDL's)
        if (saved_context_ != EGL_NO_CONTEXT) {
            eglMakeCurrent(saved_display_, saved_draw_, saved_read_, saved_context_);
        }
    }

    bool ok() const {
        return ok_;
    }

    EglContextGuard(const EglContextGuard&) = delete;
    EglContextGuard& operator=(const EglContextGuard&) = delete;

  private:
    EGLDisplay saved_display_ = EGL_NO_DISPLAY;
    EGLContext saved_context_ = EGL_NO_CONTEXT;
    EGLSurface saved_draw_ = EGL_NO_SURFACE;
    EGLSurface saved_read_ = EGL_NO_SURFACE;
    bool ok_ = false;
};

#endif // LV_USE_SDL

// ============================================================
// GLSL Shaders
// ============================================================

static const char* kVertexShaderSource = R"(
    // Gouraud lighting with per-vertex color support
    uniform mat4 u_mvp;
    uniform mat3 u_normal_matrix;
    uniform vec3 u_light_dir[2];
    uniform vec3 u_light_color[2];
    uniform vec3 u_ambient;
    uniform vec4 u_base_color;
    uniform float u_specular_intensity;
    uniform float u_specular_shininess;
    uniform float u_use_vertex_color;
    uniform float u_color_scale;

    attribute vec3 a_position;
    attribute vec3 a_normal;
    attribute vec3 a_color;

    varying vec4 v_color;

    void main() {
        gl_Position = u_mvp * vec4(a_position, 1.0);
        vec3 n = normalize(u_normal_matrix * a_normal);

        // Select base color: per-vertex or uniform override
        vec3 base = mix(u_base_color.rgb, a_color, u_use_vertex_color);
        base *= u_color_scale;

        // Diffuse lighting from two directional lights
        vec3 diffuse = u_ambient;
        for (int i = 0; i < 2; i++) {
            float NdotL = max(dot(n, u_light_dir[i]), 0.0);
            diffuse += u_light_color[i] * NdotL;
        }

        // Specular (Blinn-Phong, top light only — matches OrcaSlicer)
        vec3 view_dir = vec3(0.0, 0.0, 1.0);
        vec3 half_dir = normalize(u_light_dir[0] + view_dir);
        float spec = pow(max(dot(n, half_dir), 0.0), u_specular_shininess);

        v_color = vec4(base * diffuse + vec3(spec * u_specular_intensity),
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

static constexpr float INTENSITY_CORRECTION = 0.6f; // Match OrcaSlicer
static constexpr float INTENSITY_AMBIENT = 0.3f;    // Match OrcaSlicer

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

#if !LV_USE_SDL
// Try to set up EGL with a given display, returning true on success.
// On success, egl_display_, egl_context_, and optionally egl_surface_ are set.
bool GCodeGLESRenderer::try_egl_display(void* native_display, const char* label) {
    auto display = eglGetDisplay(static_cast<EGLNativeDisplayType>(native_display));
    if (!display || display == EGL_NO_DISPLAY) {
        spdlog::debug("[GCode GLES] {} — no display", label);
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) {
        spdlog::debug("[GCode GLES] {} — eglInitialize failed: 0x{:X}", label, eglGetError());
        return false;
    }
    spdlog::info("[GCode GLES] EGL {}.{} via {}", major, minor, label);

    eglBindAPI(EGL_OPENGL_ES_API);

    // Check surfaceless support
    const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
    bool has_surfaceless =
        extensions && strstr(extensions, "EGL_KHR_surfaceless_context") != nullptr;

    // Choose config (try surfaceless first, then PBuffer)
    EGLConfig egl_config = nullptr;
    EGLint num_configs = 0;

    if (has_surfaceless) {
        EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, 0, EGL_NONE};
        eglChooseConfig(display, attribs, &egl_config, 1, &num_configs);
    }
    if (num_configs == 0) {
        EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE,
                            EGL_PBUFFER_BIT, EGL_NONE};
        eglChooseConfig(display, attribs, &egl_config, 1, &num_configs);
        has_surfaceless = false;
    }
    if (num_configs == 0) {
        spdlog::debug("[GCode GLES] {} — no suitable config", label);
        eglTerminate(display);
        return false;
    }

    // Create context
    EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    auto context = eglCreateContext(display, egl_config, EGL_NO_CONTEXT, ctx_attribs);
    if (context == EGL_NO_CONTEXT) {
        spdlog::debug("[GCode GLES] {} — context creation failed: 0x{:X}", label, eglGetError());
        eglTerminate(display);
        return false;
    }

    // Create PBuffer if needed
    EGLSurface surface = EGL_NO_SURFACE;
    if (!has_surfaceless) {
        EGLint pbuf_attribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        surface = eglCreatePbufferSurface(display, egl_config, pbuf_attribs);
        if (surface == EGL_NO_SURFACE) {
            spdlog::debug("[GCode GLES] {} — PBuffer creation failed: 0x{:X}", label,
                          eglGetError());
            eglDestroyContext(display, context);
            eglTerminate(display);
            return false;
        }
    }

    // Save the current EGL state (SDL may have a context bound on this thread)
    EGLDisplay saved_display = eglGetCurrentDisplay();
    EGLContext saved_context = eglGetCurrentContext();
    EGLSurface saved_draw = eglGetCurrentSurface(EGL_DRAW);
    EGLSurface saved_read = eglGetCurrentSurface(EGL_READ);
    bool had_previous_context = (saved_context != EGL_NO_CONTEXT);
    spdlog::debug("[GCode GLES] {} — prior EGL context: {} (display={})", label,
                  had_previous_context ? "yes" : "no", saved_display ? "valid" : "none");

    // Release the current context so we can bind ours
    if (had_previous_context) {
        eglMakeCurrent(saved_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    // Verify eglMakeCurrent actually works with our new context
    EGLSurface test_surface = (surface != EGL_NO_SURFACE) ? surface : EGL_NO_SURFACE;
    if (!eglMakeCurrent(display, test_surface, test_surface, context)) {
        spdlog::debug("[GCode GLES] {} — eglMakeCurrent failed: 0x{:X}", label, eglGetError());
        // Restore previous context
        if (had_previous_context)
            eglMakeCurrent(saved_display, saved_draw, saved_read, saved_context);
        if (surface != EGL_NO_SURFACE)
            eglDestroySurface(display, surface);
        eglDestroyContext(display, context);
        eglTerminate(display);
        return false;
    }

    // Release our context (compile_shaders will re-acquire it)
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    // Restore SDL's context
    if (had_previous_context) {
        eglMakeCurrent(saved_display, saved_draw, saved_read, saved_context);
    }

    // Success — store state
    egl_display_ = display;
    egl_context_ = context;
    egl_surface_ = (surface != EGL_NO_SURFACE) ? static_cast<void*>(surface) : nullptr;
    spdlog::info("[GCode GLES] Context ready via {} ({})", label,
                 has_surfaceless ? "surfaceless" : "PBuffer");
    return true;
}
#endif // !LV_USE_SDL

bool GCodeGLESRenderer::init_gl() {
    if (gl_initialized_)
        return true;
    if (gl_init_failed_)
        return false;

#if LV_USE_SDL
    // Desktop path: use SDL_GL_CreateContext with a hidden window.
    // This avoids SDL_Init(SDL_INIT_VIDEO) on Wayland+AMD poisoning EGL operations.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    auto* window =
        SDL_CreateWindow("helix-gles-offscreen", 0, 0, 1, 1, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        spdlog::warn("[GCode GLES] SDL_CreateWindow failed: {}", SDL_GetError());
        gl_init_failed_ = true;
        return false;
    }

    auto gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        spdlog::warn("[GCode GLES] SDL_GL_CreateContext failed: {}", SDL_GetError());
        SDL_DestroyWindow(window);
        gl_init_failed_ = true;
        return false;
    }

    sdl_gl_window_ = window;
    sdl_gl_context_ = gl_ctx;

    spdlog::info("[GCode GLES] SDL GL context ready — GL_VERSION: {}, GL_RENDERER: {}",
                 reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                 reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    // Unbind our context (compile_shaders will re-acquire via guard)
    SDL_GL_MakeCurrent(window, nullptr);

#else  // !LV_USE_SDL — EGL backend
    // EGL initialization with fallback chain:
    // 1. GBM/DRM (Pi, embedded — surfaceless FBO rendering)
    // 2. Default EGL display (desktop Linux with X11/Wayland — PBuffer)
    bool egl_ok = false;

    // Path 1: Try GBM/DRM render nodes first (don't need DRM master, works alongside compositor)
    // Then try card nodes (needed on Pi where render nodes may not exist)
    static const char* kDrmDevices[] = {"/dev/dri/renderD128", "/dev/dri/renderD129",
                                        "/dev/dri/card1", "/dev/dri/card0", nullptr};
    for (int i = 0; kDrmDevices[i] && !egl_ok; ++i) {
        int fd = open(kDrmDevices[i], O_RDWR | O_CLOEXEC);
        if (fd < 0)
            continue;

        auto* gbm = gbm_create_device(fd);
        if (!gbm) {
            close(fd);
            continue;
        }

        if (try_egl_display(gbm, kDrmDevices[i])) {
            drm_fd_ = fd;
            gbm_device_ = gbm;
            egl_ok = true;
        } else {
            gbm_device_destroy(gbm);
            close(fd);
        }
    }

    // Path 2: Default EGL display (Mesa on X11/Wayland)
    if (!egl_ok) {
        if (try_egl_display(EGL_DEFAULT_DISPLAY, "EGL_DEFAULT_DISPLAY")) {
            egl_ok = true;
        }
    }

    if (!egl_ok) {
        spdlog::warn("[GCode GLES] All EGL paths failed — GPU rendering unavailable");
        gl_init_failed_ = true;
        return false;
    }
#endif // LV_USE_SDL

    // Compile shaders (will acquire GL context internally via guard)
    if (!compile_shaders()) {
        gl_init_failed_ = true;
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
#if LV_USE_SDL
    SdlGlContextGuard guard(sdl_gl_window_, sdl_gl_context_);
#else
    EglContextGuard guard(egl_display_, egl_surface_, egl_context_);
#endif
    if (!guard.ok())
        return false;

    GLuint vs = compile_shader(GL_VERTEX_SHADER, kVertexShaderSource);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFragmentShaderSource);
    if (!vs || !fs) {
        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
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

    if (!program_)
        return false;

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
    a_color_ = glGetAttribLocation(program_, "a_color");
    u_use_vertex_color_ = glGetUniformLocation(program_, "u_use_vertex_color");
    u_color_scale_ = glGetUniformLocation(program_, "u_color_scale");

    if (a_position_ < 0 || a_normal_ < 0) {
        spdlog::error("[GCode GLES] Required attribute not found: a_position={}, a_normal={}",
                      a_position_, a_normal_);
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }

    spdlog::debug("[GCode GLES] Shaders compiled and linked (program={})", program_);
    return true;
}

bool GCodeGLESRenderer::create_fbo(int width, int height) {
    if (fbo_ && fbo_width_ == width && fbo_height_ == height) {
        return true; // Already correct size
    }

    destroy_fbo();

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        spdlog::error("[GCode GLES] glGenFramebuffers/glBindFramebuffer failed: 0x{:X}", err);
        destroy_fbo();
        return false;
    }

    // Color renderbuffer (RGBA4)
    glGenRenderbuffers(1, &color_rbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, color_rbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, width, height);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        spdlog::error("[GCode GLES] Color renderbuffer creation failed: 0x{:X}", err);
        destroy_fbo();
        return false;
    }
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_rbo_);

    // Depth renderbuffer (16-bit)
    glGenRenderbuffers(1, &depth_rbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        spdlog::error("[GCode GLES] Depth renderbuffer creation failed: 0x{:X}", err);
        destroy_fbo();
        return false;
    }
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

#if LV_USE_SDL
    // Make our context current for GL resource cleanup
    if (sdl_gl_window_ && sdl_gl_context_) {
        SDL_GLContext saved_ctx = SDL_GL_GetCurrentContext();
        SDL_Window* saved_win = SDL_GL_GetCurrentWindow();

        SDL_GL_MakeCurrent(static_cast<SDL_Window*>(sdl_gl_window_),
                           static_cast<SDL_GLContext>(sdl_gl_context_));

        free_vbos(layer_vbos_);
        destroy_fbo();

        if (program_) {
            glDeleteProgram(program_);
            program_ = 0;
        }

        // Unbind before destroying
        SDL_GL_MakeCurrent(static_cast<SDL_Window*>(sdl_gl_window_), nullptr);

        SDL_GL_DeleteContext(static_cast<SDL_GLContext>(sdl_gl_context_));
        sdl_gl_context_ = nullptr;

        SDL_DestroyWindow(static_cast<SDL_Window*>(sdl_gl_window_));
        sdl_gl_window_ = nullptr;

        // Restore previous context
        if (saved_ctx) {
            SDL_GL_MakeCurrent(saved_win, saved_ctx);
        }
    }

#else  // !LV_USE_SDL — EGL backend
    // Save SDL's EGL state
    EGLDisplay saved_display = eglGetCurrentDisplay();
    EGLContext saved_context = eglGetCurrentContext();
    EGLSurface saved_draw = eglGetCurrentSurface(EGL_DRAW);
    EGLSurface saved_read = eglGetCurrentSurface(EGL_READ);

    // Make our context current for GL cleanup
    if (egl_display_ && egl_context_) {
        if (saved_context != EGL_NO_CONTEXT)
            eglMakeCurrent(saved_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglMakeCurrent(static_cast<EGLDisplay>(egl_display_), static_cast<EGLSurface>(egl_surface_),
                       static_cast<EGLSurface>(egl_surface_),
                       static_cast<EGLContext>(egl_context_));
    }

    free_vbos(layer_vbos_);
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

    if (egl_display_ && egl_surface_) {
        eglDestroySurface(static_cast<EGLDisplay>(egl_display_),
                          static_cast<EGLSurface>(egl_surface_));
        egl_surface_ = nullptr;
    }

    if (egl_display_) {
        eglTerminate(static_cast<EGLDisplay>(egl_display_));
        egl_display_ = nullptr;
    }

    // Restore SDL's EGL state
    if (saved_context != EGL_NO_CONTEXT) {
        eglMakeCurrent(saved_display, saved_draw, saved_read, saved_context);
    }

    if (gbm_device_) {
        gbm_device_destroy(static_cast<struct gbm_device*>(gbm_device_));
        gbm_device_ = nullptr;
    }

    if (drm_fd_ >= 0) {
        close(drm_fd_);
        drm_fd_ = -1;
    }
#endif // LV_USE_SDL

    gl_initialized_ = false;
    geometry_uploaded_ = false;
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

    // Interleaved vertex format: position(3f) + normal(3f) + color(3f) = 36 bytes per vertex
    constexpr size_t kVertexStride = 9 * sizeof(float);

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
        std::vector<float> buf(total_verts * 9); // 9 floats per vertex

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

                // Look up per-vertex color from geometry palette
                uint32_t rgb = 0x26A69A; // Default teal
                if (vert.color_index < geom.color_palette.size()) {
                    rgb = geom.color_palette[vert.color_index];
                }
                buf[out_idx++] = ((rgb >> 16) & 0xFF) / 255.0f; // R
                buf[out_idx++] = ((rgb >> 8) & 0xFF) / 255.0f;  // G
                buf[out_idx++] = (rgb & 0xFF) / 255.0f;         // B
            }
        }

        GLuint vbo = 0;
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(total_verts * kVertexStride),
                     buf.data(), GL_STATIC_DRAW);
        GLenum err = glGetError();
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        if (err != GL_NO_ERROR) {
            spdlog::error("[GCode GLES] VBO creation failed for layer {}: 0x{:X}", layer, err);
            if (vbo) {
                glDeleteBuffers(1, &vbo);
            }
            vbos[layer].vbo = 0;
            vbos[layer].vertex_count = 0;
            continue;
        }

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

        // Acquire our GL context (saves and restores LVGL's)
#if LV_USE_SDL
    SdlGlContextGuard guard(sdl_gl_window_, sdl_gl_context_);
#else
    EglContextGuard guard(egl_display_, egl_surface_, egl_context_);
#endif
    if (!guard.ok())
        return;

    // Upload geometry to VBOs if needed
    if (!geometry_uploaded_ && geometry_) {
        upload_geometry(*geometry_, layer_vbos_);
        geometry_uploaded_ = true;
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

    // guard destructor restores LVGL's GL context

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

    // Neutral gray background — light and dark filaments both contrast well
    glClearColor(0.45f, 0.45f, 0.47f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    // Select active geometry
    auto* active_vbos = &layer_vbos_;
    active_geometry_ = geometry_.get();

    if (!active_geometry_ || active_vbos->empty()) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    // Use shader program
    glUseProgram(program_);

    // Model transform: rotate -90° (CW) around Z to match slicer thumbnail orientation
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0, 0, 1));
    glm::mat4 view = camera.get_view_matrix();
    glm::mat4 proj = camera.get_projection_matrix();
    glm::mat4 mvp = proj * view * model;

    // Normal matrix (inverse transpose of upper-left 3x3 of model-view)
    glm::mat3 normal_mat = glm::transpose(glm::inverse(glm::mat3(view * model)));

    // Set uniforms
    glUniformMatrix4fv(u_mvp_, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix3fv(u_normal_matrix_, 1, GL_FALSE, glm::value_ptr(normal_mat));

    // Lighting — transform light directions from world space to view-model space
    // (normals are in view-model space via u_normal_matrix, so lights must match)
    glm::mat3 view_model_rot = glm::mat3(view * model);
    glm::vec3 light_dirs[2] = {glm::normalize(view_model_rot * kLightTopDir),
                               glm::normalize(view_model_rot * kLightFrontDir)};
    glm::vec3 light_colors[2] = {kLightTopColor, kLightFrontColor};
    glUniform3fv(u_light_dir_, 2, glm::value_ptr(light_dirs[0]));
    glUniform3fv(u_light_color_, 2, glm::value_ptr(light_colors[0]));
    glUniform3fv(u_ambient_, 1, glm::value_ptr(kAmbientColor));

    // Material
    glUniform1f(u_specular_intensity_, specular_intensity_);
    glUniform1f(u_specular_shininess_, specular_shininess_);

    // Per-vertex color mode: use vertex colors when geometry has a color palette.
    // With per-tool AMS overrides, the palette is updated in-place so vertex colors
    // always reflect the correct AMS slot colors. Only fall back to uniform color
    // when palette has a single-tool override (legacy path).
    bool has_palette = active_geometry_ && !active_geometry_->color_palette.empty();
    bool has_vertex_colors = has_palette && !palette_.has_override;
    glUniform1f(u_use_vertex_color_, has_vertex_colors ? 1.0f : 0.0f);

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
            draw_layers(*active_vbos, draw_start, solid_end, 1.0f, 1.0f);
        }

        // Pass 2: Ghost layers (progress_layer_+1 to end)
        int ghost_start = std::max(progress_layer_ + 1, draw_start);
        if (ghost_start <= draw_end) {
            draw_layers(*active_vbos, ghost_start, draw_end, ghost_opacity_ / 255.0f,
                        (ghost_render_mode_ == GhostRenderMode::Stipple) ? 0.5f : 1.0f);
        }
    } else {
        // Normal: all layers solid
        draw_layers(*active_vbos, draw_start, draw_end, 1.0f, 1.0f);
    }

    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GCodeGLESRenderer::draw_layers(const std::vector<LayerVBO>& vbos, int layer_start,
                                    int layer_end, float color_scale, float ghost_alpha) {
    // Set uniforms for this draw batch
    glUniform4fv(u_base_color_, 1, glm::value_ptr(filament_color_));
    glUniform1f(u_color_scale_, color_scale);
    glUniform1f(u_ghost_alpha_, ghost_alpha);

    constexpr size_t kStride = 9 * sizeof(float);

    // Enable vertex attributes once before the loop (a_position_ and a_normal_
    // are validated >= 0 during compile_shaders)
    glEnableVertexAttribArray(static_cast<GLuint>(a_position_));
    glEnableVertexAttribArray(static_cast<GLuint>(a_normal_));
    if (a_color_ >= 0) {
        glEnableVertexAttribArray(static_cast<GLuint>(a_color_));
    }

    for (int layer = layer_start; layer <= layer_end; ++layer) {
        if (layer < 0 || layer >= static_cast<int>(vbos.size()))
            continue;
        const auto& lv = vbos[static_cast<size_t>(layer)];
        if (!lv.vbo || lv.vertex_count == 0)
            continue;

        glBindBuffer(GL_ARRAY_BUFFER, lv.vbo);

        glVertexAttribPointer(static_cast<GLuint>(a_position_), 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(kStride), reinterpret_cast<void*>(0));

        glVertexAttribPointer(static_cast<GLuint>(a_normal_), 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(kStride),
                              reinterpret_cast<void*>(3 * sizeof(float)));

        if (a_color_ >= 0) {
            glVertexAttribPointer(static_cast<GLuint>(a_color_), 3, GL_FLOAT, GL_FALSE,
                                  static_cast<GLsizei>(kStride),
                                  reinterpret_cast<void*>(6 * sizeof(float)));
        }

        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(lv.vertex_count));
        triangles_rendered_ += lv.vertex_count / 3;
    }

    glDisableVertexAttribArray(static_cast<GLuint>(a_position_));
    glDisableVertexAttribArray(static_cast<GLuint>(a_normal_));
    if (a_color_ >= 0) {
        glDisableVertexAttribArray(static_cast<GLuint>(a_color_));
    }
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

    // Convert GL RGBA → LVGL RGB888 (BGR byte order), flip Y, and scale if needed
    if (!draw_buf_->data) {
        spdlog::error("[GCode GLES] draw_buf_ data is null");
        return;
    }
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

            if (src_idx + 3 >= rgba.size())
                continue;

            dest[dst_idx + 0] = rgba[src_idx + 2]; // B (LVGL RGB888 = BGR byte order)
            dest[dst_idx + 1] = rgba[src_idx + 1]; // G
            dest[dst_idx + 2] = rgba[src_idx + 0]; // R
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
    constexpr float kEps = 1e-5f;
    auto near = [](float a, float b) { return std::abs(a - b) < kEps; };
    return near(azimuth, o.azimuth) && near(elevation, o.elevation) && near(distance, o.distance) &&
           near(target.x, o.target.x) && near(target.y, o.target.y) && near(target.z, o.target.z) &&
           progress_layer == o.progress_layer && layer_start == o.layer_start &&
           layer_end == o.layer_end && highlight_count == o.highlight_count &&
           exclude_count == o.exclude_count;
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

void GCodeGLESRenderer::set_extrusion_color(lv_color_t color) {
    filament_color_ =
        glm::vec4(color.red / 255.0f, color.green / 255.0f, color.blue / 255.0f, 1.0f);
    palette_.has_override = true;
    palette_.override_color = color;
    frame_dirty_ = true;
    spdlog::debug("[GCode GLES] set_extrusion_color: R={} G={} B={} → ({:.2f},{:.2f},{:.2f})",
                  color.red, color.green, color.blue, filament_color_.r, filament_color_.g,
                  filament_color_.b);
}

void GCodeGLESRenderer::set_tool_color_overrides(const std::vector<uint32_t>& ams_colors) {
    if (!geometry_ || ams_colors.empty()) {
        return;
    }

    // Replace palette entries using tool→palette mapping from geometry build
    bool changed = false;
    for (size_t tool = 0; tool < ams_colors.size(); ++tool) {
        if (tool >= geometry_->tool_palette_map.size()) {
            continue;
        }
        uint8_t palette_idx = geometry_->tool_palette_map[tool];
        if (palette_idx < geometry_->color_palette.size() &&
            geometry_->color_palette[palette_idx] != ams_colors[tool]) {
            geometry_->color_palette[palette_idx] = ams_colors[tool];
            changed = true;
        }
    }

    if (changed) {
        // Per-tool overrides replace palette entries baked into vertex data,
        // so clear any single-color override that would bypass vertex colors.
        palette_.has_override = false;
        // Force VBO re-upload to bake new colors into vertex data
        geometry_uploaded_ = false;
        frame_dirty_ = true;
        spdlog::debug("[GCode GLES] Applied {} tool color overrides, triggering VBO re-upload",
                      ams_colors.size());
    }
}

void GCodeGLESRenderer::set_smooth_shading(bool /*enable*/) {
    frame_dirty_ = true;
}

void GCodeGLESRenderer::set_extrusion_width(float width_mm) {
    extrusion_width_ = width_mm;
}

void GCodeGLESRenderer::set_simplification_tolerance(float /*tolerance_mm*/) {
    // Simplification is applied during geometry build, not at render time
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
    palette_.has_override = false;
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

void GCodeGLESRenderer::set_prebuilt_coarse_geometry(std::unique_ptr<RibbonGeometry> /*geometry*/) {
    // Coarse LOD no longer used — GPU handles full geometry at full speed
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
    // Approximate GPU VRAM usage (VBOs + FBO)
    for (const auto& lv : layer_vbos_) {
        if (lv.vbo) {
            total += lv.vertex_count * 9 * sizeof(float);
        }
    }
    if (fbo_) {
        // Color RBO (RGBA4 = 2 bytes/pixel) + Depth RBO (16-bit = 2 bytes/pixel)
        total += static_cast<size_t>(fbo_width_ * fbo_height_ * 4);
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
