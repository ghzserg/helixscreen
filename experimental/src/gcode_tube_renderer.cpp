// Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// G-Code Tube Renderer Implementation

#include "gcode_tube_renderer.h"
#include "ui_theme.h"
#include "ui_utils.h"

#include <GL/gl.h>
#include <glm/gtc/type_ptr.hpp>
#include <chrono>
#include <cmath>
#include <spdlog/spdlog.h>

extern "C" {
#include <zbuffer.h>
}

namespace gcode {

// ============================================================================
// TubeMesh Implementation
// ============================================================================

void TubeMesh::generate(float radius, float length, int radial_segments, int length_segments) {
    vertices.clear();
    normals.clear();
    indices.clear();

    // Generate cylinder vertices
    for (int i = 0; i <= length_segments; ++i) {
        float z = (static_cast<float>(i) / length_segments) * length - length * 0.5f;

        for (int j = 0; j < radial_segments; ++j) {
            float angle = (static_cast<float>(j) / radial_segments) * 2.0f * M_PI;
            float x = radius * cosf(angle);
            float y = radius * sinf(angle);

            vertices.push_back(glm::vec3(x, y, z));
            normals.push_back(glm::normalize(glm::vec3(x, y, 0.0f)));  // Radial normal
        }
    }

    // Generate triangle indices (two triangles per quad)
    for (int i = 0; i < length_segments; ++i) {
        for (int j = 0; j < radial_segments; ++j) {
            int current = i * radial_segments + j;
            int next = i * radial_segments + (j + 1) % radial_segments;
            int current_next_row = (i + 1) * radial_segments + j;
            int next_next_row = (i + 1) * radial_segments + (j + 1) % radial_segments;

            // First triangle
            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current_next_row);

            // Second triangle
            indices.push_back(next);
            indices.push_back(next_next_row);
            indices.push_back(current_next_row);
        }
    }

    spdlog::info("Generated tube template: {} vertices, {} triangles ({} bytes)",
                 vertices.size(), indices.size() / 3, memory_usage());
}

// ============================================================================
// TubeInstance Implementation
// ============================================================================

glm::mat4 TubeInstance::get_transform() const {
    glm::vec3 direction = end - start;
    float len = glm::length(direction);

    if (len < 0.0001f) {
        // Degenerate segment - return identity scaled by radius
        return glm::scale(glm::mat4(1.0f), glm::vec3(radius, radius, 0.0f));
    }

    direction /= len;  // Normalize

    // Calculate rotation to align Z-axis (0,0,1) with direction
    glm::vec3 z_axis(0.0f, 0.0f, 1.0f);
    glm::vec3 rotation_axis = glm::cross(z_axis, direction);
    float rotation_angle = acosf(glm::clamp(glm::dot(z_axis, direction), -1.0f, 1.0f));

    glm::mat4 transform(1.0f);

    // Translation to start point
    transform = glm::translate(transform, start);

    // Rotation to align with segment direction
    if (glm::length(rotation_axis) > 0.0001f) {
        transform = glm::rotate(transform, rotation_angle, glm::normalize(rotation_axis));
    } else if (glm::dot(z_axis, direction) < 0.0f) {
        // 180-degree rotation needed
        transform = glm::rotate(transform, static_cast<float>(M_PI), glm::vec3(1.0f, 0.0f, 0.0f));
    }

    // Scale: radius in XY, length in Z
    transform = glm::scale(transform, glm::vec3(radius, radius, len));

    return transform;
}

// ============================================================================
// GCodeTubeRenderer Implementation
// ============================================================================

GCodeTubeRenderer::GCodeTubeRenderer() {
    // Generate tube template mesh (12 sides, 2 length segments = 60 vertices)
    tube_template_.generate(1.0f, 1.0f, 12, 2);
}

GCodeTubeRenderer::~GCodeTubeRenderer() {
    shutdown_tinygl();

    if (draw_buf_) {
        lv_draw_buf_destroy(draw_buf_);
        draw_buf_ = nullptr;
    }
}

void GCodeTubeRenderer::set_viewport_size(int width, int height) {
    if (viewport_width_ == width && viewport_height_ == height) {
        return;
    }

    viewport_width_ = width;
    viewport_height_ = height;

    // Reinitialize TinyGL with new size
    shutdown_tinygl();
    init_tinygl();
}

void GCodeTubeRenderer::set_tube_radius(float radius_mm) {
    tube_radius_ = radius_mm;
}

void GCodeTubeRenderer::set_filament_color(const std::string& hex_color) {
    lv_color_t lv_col = ui_theme_parse_color(hex_color.c_str());
    filament_color_ = glm::vec3(lv_col.red / 255.0f, lv_col.green / 255.0f, lv_col.blue / 255.0f);
}

void GCodeTubeRenderer::render(lv_layer_t* layer, const ParsedGCodeFile& gcode,
                                const GCodeCamera& camera) {
    // Build instances if G-code changed
    if (current_gcode_filename_ != gcode.filename || instances_.empty()) {
        build_instances(gcode);
        current_gcode_filename_ = gcode.filename;
    }

    // Initialize TinyGL if needed
    if (!zbuffer_) {
        init_tinygl();
    }

    // Render tubes
    auto render_start = std::chrono::high_resolution_clock::now();
    render_tubes(camera);
    auto render_end = std::chrono::high_resolution_clock::now();
    stats_.render_time_seconds = std::chrono::duration<float>(render_end - render_start).count();

    // Draw to LVGL
    draw_to_lvgl(layer);
}

void GCodeTubeRenderer::build_instances(const ParsedGCodeFile& gcode) {
    auto build_start = std::chrono::high_resolution_clock::now();

    instances_.clear();

    // Extract tube instances from G-code segments (perimeter/shell only)
    for (const auto& layer : gcode.layers) {
        for (const auto& segment : layer.segments) {
            // Skip travel moves
            if (!segment.is_extrusion) {
                continue;
            }

            // Filter by feature type (same logic as SDF builder)
            if (!segment.feature_type.empty()) {
                bool is_shell_feature =
                    segment.feature_type.find("wall") != std::string::npos ||
                    segment.feature_type.find("Wall") != std::string::npos ||
                    segment.feature_type.find("perimeter") != std::string::npos ||
                    segment.feature_type.find("Perimeter") != std::string::npos ||
                    segment.feature_type.find("surface") != std::string::npos ||
                    segment.feature_type.find("Surface") != std::string::npos ||
                    segment.feature_type.find("skin") != std::string::npos ||
                    segment.feature_type.find("Skin") != std::string::npos ||
                    segment.feature_type.find("bridge") != std::string::npos ||
                    segment.feature_type.find("Bridge") != std::string::npos;

                if (!is_shell_feature) {
                    continue;  // Skip infill/support
                }
            }

            // Create tube instance
            TubeInstance instance;
            instance.start = segment.start;
            instance.end = segment.end;
            instance.radius = tube_radius_;
            instance.color = filament_color_;

            instances_.push_back(instance);
        }
    }

    auto build_end = std::chrono::high_resolution_clock::now();

    // Update statistics
    stats_.segment_count = instances_.size();
    stats_.vertex_count = instances_.size() * tube_template_.vertices.size();
    stats_.triangle_count = instances_.size() * (tube_template_.indices.size() / 3);
    stats_.memory_bytes =
        instances_.size() * sizeof(TubeInstance) + tube_template_.memory_usage();
    stats_.build_time_seconds = std::chrono::duration<float>(build_end - build_start).count();

    spdlog::info("Built {} tube instances ({:.2f} MB, {:.2f}s)", stats_.segment_count,
                 stats_.memory_bytes / (1024.0 * 1024.0), stats_.build_time_seconds);
    spdlog::info("  Vertices: {}, Triangles: {}", stats_.vertex_count, stats_.triangle_count);
}

void GCodeTubeRenderer::init_tinygl() {
    // Create TinyGL zbuffer and framebuffer
    ZBuffer* zb = ZB_open(viewport_width_, viewport_height_, ZB_MODE_RGBA, 0);
    if (!zb) {
        spdlog::error("Failed to create TinyGL zbuffer");
        return;
    }

    zbuffer_ = zb;  // Store as void*
    framebuffer_ = static_cast<unsigned int*>(zb->pbuf);  // Get framebuffer pointer from ZBuffer
    glInit(zb);

    // OpenGL state setup
    glViewport(0, 0, viewport_width_, viewport_height_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    // Enable smooth shading and normal normalization
    glShadeModel(GL_SMOOTH);
    glEnable(GL_NORMALIZE);

    setup_lighting();

    spdlog::info("TinyGL tube renderer initialized ({}x{})", viewport_width_, viewport_height_);
}

void GCodeTubeRenderer::shutdown_tinygl() {
    if (zbuffer_) {
        glClose();
        ZB_close(static_cast<ZBuffer*>(zbuffer_));
        zbuffer_ = nullptr;
        framebuffer_ = nullptr;
    }
}

void GCodeTubeRenderer::setup_lighting() {
    // Two-point studio lighting (same as SDF renderer)
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);

    // Key light (bright, from upper-right-front)
    GLfloat light0_pos[] = {1.0f, 1.0f, 2.0f, 0.0f};  // Directional
    GLfloat light0_diffuse[] = {1.0f, 1.0f, 1.0f, 1.0f};
    GLfloat light0_specular[] = {0.3f, 0.3f, 0.3f, 1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, light0_pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light0_diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, light0_specular);

    // Fill light (dimmer, from left)
    GLfloat light1_pos[] = {-1.0f, 0.5f, 1.0f, 0.0f};  // Directional
    GLfloat light1_diffuse[] = {0.4f, 0.4f, 0.4f, 1.0f};
    glLightfv(GL_LIGHT1, GL_POSITION, light1_pos);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, light1_diffuse);

    // Global ambient
    GLfloat ambient[] = {0.2f, 0.2f, 0.2f, 1.0f};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
}

void GCodeTubeRenderer::render_tubes(const GCodeCamera& camera) {
    // Clear buffers
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Set up camera matrices
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(glm::value_ptr(camera.get_projection_matrix()));

    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(glm::value_ptr(camera.get_view_matrix()));

    // Set material color
    glColor3f(filament_color_.r, filament_color_.g, filament_color_.b);

    // Render each tube instance
    for (const auto& instance : instances_) {
        glPushMatrix();

        // Apply instance transformation
        glm::mat4 transform = instance.get_transform();
        glMultMatrixf(glm::value_ptr(transform));

        // Draw tube template mesh
        glBegin(GL_TRIANGLES);
        for (size_t i = 0; i < tube_template_.indices.size(); i += 3) {
            for (int j = 0; j < 3; ++j) {
                uint16_t idx = tube_template_.indices[i + j];
                glNormal3fv(glm::value_ptr(tube_template_.normals[idx]));
                glVertex3fv(glm::value_ptr(tube_template_.vertices[idx]));
            }
        }
        glEnd();

        glPopMatrix();
    }
}

void GCodeTubeRenderer::draw_to_lvgl(lv_layer_t* layer) {
    if (!framebuffer_) {
        return;
    }

    // Create LVGL draw buffer if needed
    if (!draw_buf_) {
        draw_buf_ = lv_draw_buf_create(viewport_width_, viewport_height_, LV_COLOR_FORMAT_RGB888, 0);
        if (!draw_buf_) {
            spdlog::error("Failed to create LVGL draw buffer");
            return;
        }
    }

    // Copy TinyGL framebuffer to LVGL draw buffer
    // TinyGL ZB_MODE_RGBA is actually ABGR format (Alpha-Blue-Green-Red)
    // LVGL uses RGB888, so we need to swap R and B
    uint8_t* lvgl_buf = static_cast<uint8_t*>(draw_buf_->data);
    for (int y = 0; y < viewport_height_; ++y) {
        for (int x = 0; x < viewport_width_; ++x) {
            unsigned int pixel = framebuffer_[y * viewport_width_ + x];
            int lvgl_idx = (y * viewport_width_ + x) * 3;

            // Extract RGB and swap R/B channels
            lvgl_buf[lvgl_idx + 0] = (pixel >> 0) & 0xFF;   // R (from TinyGL B channel)
            lvgl_buf[lvgl_idx + 1] = (pixel >> 8) & 0xFF;   // G (stays the same)
            lvgl_buf[lvgl_idx + 2] = (pixel >> 16) & 0xFF;  // B (from TinyGL R channel)
        }
    }

    // Draw to LVGL layer
    lv_draw_image_dsc_t img_dsc;
    lv_draw_image_dsc_init(&img_dsc);
    img_dsc.src = draw_buf_;

    lv_area_t area = {0, 0, viewport_width_ - 1, viewport_height_ - 1};
    lv_draw_image(layer, &img_dsc, &area);
}

} // namespace gcode
