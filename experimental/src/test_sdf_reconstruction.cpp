// Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// G-Code SDF Reconstruction Test
// Tests the complete SDF pipeline: Parse → Build SDF → Marching Cubes → Render

#include "gcode_parser.h"
#include "gcode_sdf_builder.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <string>

#ifdef ENABLE_TINYGL_3D

extern "C" {
#include <GL/gl.h>
#include <zbuffer.h>
}

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Helper function to write PPM file
static void write_ppm(const char* filename, int width, int height, unsigned char* rgb_data) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        spdlog::error("Failed to open {} for writing", filename);
        return;
    }

    fprintf(fp, "P6\n%d %d\n255\n", width, height);
    fwrite(rgb_data, 1, width * height * 3, fp);
    fclose(fp);

    spdlog::info("✓ Wrote {} ({}x{})", filename, width, height);
}

int main(int argc, char** argv) {
    // Configure logging
    spdlog::set_level(spdlog::level::info);

    spdlog::info("═══════════════════════════════════════════════");
    spdlog::info("  G-Code SDF Reconstruction Test");
    spdlog::info("═══════════════════════════════════════════════");
    spdlog::info("");

    // Determine G-code file path
    std::string gcode_path = (argc > 1) ? argv[1] : "assets/OrcaCube AD5M.gcode";
    spdlog::info("→ Parsing G-code: {}", gcode_path);

    // Parse G-code file
    std::ifstream file(gcode_path);
    if (!file.is_open()) {
        spdlog::error("Failed to open G-code file: {}", gcode_path);
        return 1;
    }

    gcode::GCodeParser parser;
    std::string line;
    size_t line_count = 0;

    while (std::getline(file, line)) {
        parser.parse_line(line);
        line_count++;

        // Progress indicator every 10k lines
        if (line_count % 10000 == 0) {
            spdlog::info("  Parsed {} lines, Z={:.2f}mm, layer={}",
                        line_count, parser.current_z(), parser.current_layer());
        }
    }
    file.close();

    gcode::ParsedGCodeFile gcode_file = parser.finalize();

    spdlog::info("✓ Parsing complete:");
    spdlog::info("  Total lines:    {:>8}", line_count);
    spdlog::info("  Total layers:   {:>8}", gcode_file.layers.size());
    spdlog::info("  Total segments: {:>8}", gcode_file.total_segments);
    spdlog::info("");

    // Build SDF and extract surface
    spdlog::info("→ Building SDF and extracting surface...");

    gcode::SDFBuilder sdf_builder;
    gcode::SDFOptions options;
    options.grid_resolution = 128;  // Start with lower resolution for speed
    options.smoothing_radius = 0.3f;
    options.segment_radius_mm = 0.21f;

    gcode::TriangleMesh mesh = sdf_builder.build(gcode_file, options);
    spdlog::info("");

    if (mesh.vertices.empty()) {
        spdlog::error("Failed to generate mesh (no vertices)");
        return 1;
    }

    // Initialize TinyGL
    const int width = 800;
    const int height = 600;

    ZBuffer* framebuffer = ZB_open(width, height, ZB_MODE_RGBA, 0);
    if (!framebuffer) {
        spdlog::error("Failed to create ZBuffer");
        return 1;
    }

    glInit(framebuffer);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE); // Normalize normals after transformations

    // Set up lighting
    GLfloat light_position[] = {10.0f, 10.0f, 10.0f, 1.0f};
    GLfloat light_ambient[] = {0.3f, 0.3f, 0.3f, 1.0f};
    GLfloat light_diffuse[] = {0.8f, 0.8f, 0.8f, 1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);
    glLightfv(GL_LIGHT0, GL_AMBIENT, light_ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);

    // Material (teal color like OrcaSlicer)
    GLfloat material_diffuse[] = {0.15f, 0.65f, 0.60f, 1.0f};  // #26A69A
    GLfloat material_ambient[] = {0.1f, 0.45f, 0.40f, 1.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, material_diffuse);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, material_ambient);

    // Clear screen
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Set up camera (isometric view)
    glm::vec3 model_center = gcode_file.global_bounding_box.center();
    glm::vec3 model_size = gcode_file.global_bounding_box.size();
    float max_dim = std::max({model_size.x, model_size.y, model_size.z});

    float azimuth = 45.0f;
    float elevation = 30.0f;
    float distance = max_dim * 2.5f;

    float azimuth_rad = glm::radians(azimuth);
    float elevation_rad = glm::radians(elevation);
    glm::vec3 camera_pos = model_center + glm::vec3(
        distance * std::cos(elevation_rad) * std::sin(azimuth_rad),
        distance * std::cos(elevation_rad) * std::cos(azimuth_rad),
        distance * std::sin(elevation_rad)
    );

    // Set up matrices
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = static_cast<float>(width) / height;
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
    glLoadMatrixf(glm::value_ptr(projection));

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glm::mat4 view = glm::lookAt(camera_pos, model_center, glm::vec3(0.0f, 0.0f, 1.0f));
    glLoadMatrixf(glm::value_ptr(view));

    spdlog::info("→ Rendering {} triangles ({} vertices)...",
                mesh.triangle_count(), mesh.vertices.size());

    // Render triangle mesh
    glBegin(GL_TRIANGLES);
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        for (int j = 0; j < 3; ++j) {
            uint32_t idx = mesh.indices[i + j];
            const glm::vec3& normal = mesh.normals[idx];
            const glm::vec3& vertex = mesh.vertices[idx];

            glNormal3f(normal.x, normal.y, normal.z);
            glVertex3f(vertex.x, vertex.y, vertex.z);
        }
    }
    glEnd();

    glFlush();

    spdlog::info("✓ Rendering complete");
    spdlog::info("");

    // Save framebuffer as PPM
    spdlog::info("→ Saving framebuffer...");

    // Extract RGB data (flip Y for correct orientation)
    std::vector<unsigned char> rgb_data(width * height * 3);
    PIXEL* pb = framebuffer->pbuf;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int flipped_y = (height - 1 - y);
            PIXEL p = pb[flipped_y * width + x];

            int idx = (y * width + x) * 3;
            rgb_data[idx + 0] = (p >> 16) & 0xFF; // R
            rgb_data[idx + 1] = (p >> 8) & 0xFF;  // G
            rgb_data[idx + 2] = p & 0xFF;         // B
        }
    }

    write_ppm("sdf_render.ppm", width, height, rgb_data.data());

    // Cleanup
    ZB_close(framebuffer);
    glClose();

    spdlog::info("");
    spdlog::info("✓ Test complete!");

    return 0;
}

#else

int main() {
    spdlog::error("TinyGL support not enabled (ENABLE_TINYGL_3D not defined)");
    return 1;
}

#endif
