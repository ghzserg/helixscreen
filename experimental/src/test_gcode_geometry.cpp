// Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// G-Code Geometry Builder Test
// Tests the complete pipeline: Parse → Build Geometry → Render with TinyGL

#include "gcode_parser.h"
#include "gcode_geometry_builder.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <string>
#include <algorithm>

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
    // Configure logging (debug to see segment range)
    spdlog::set_level(spdlog::level::debug);

    spdlog::info("═══════════════════════════════════════════════");
    spdlog::info("  G-Code Geometry Builder Test");
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
    spdlog::info("  Bounding box:   [{:.1f}, {:.1f}, {:.1f}] to [{:.1f}, {:.1f}, {:.1f}]",
                gcode_file.global_bounding_box.min.x, gcode_file.global_bounding_box.min.y,
                gcode_file.global_bounding_box.min.z, gcode_file.global_bounding_box.max.x,
                gcode_file.global_bounding_box.max.y, gcode_file.global_bounding_box.max.z);
    spdlog::info("");

    // Build ribbon geometry (extrusions only, skip travels)
    spdlog::info("→ Building ribbon geometry (extrusions only)...");

    // Count segment types for verification
    size_t total_extrusions = 0;
    size_t total_travels = 0;
    for (const auto& layer : gcode_file.layers) {
        for (const auto& seg : layer.segments) {
            if (seg.is_extrusion) total_extrusions++;
            else total_travels++;
        }
    }
    spdlog::info("  Original: {} extrusions, {} travels", total_extrusions, total_travels);

    // Create a filtered version with only extrusion moves
    gcode::ParsedGCodeFile filtered_gcode = gcode_file;
    size_t filtered_count = 0;
    for (auto& layer : filtered_gcode.layers) {
        auto it = std::remove_if(layer.segments.begin(), layer.segments.end(),
            [](const gcode::ToolpathSegment& seg) { return !seg.is_extrusion; });
        layer.segments.erase(it, layer.segments.end());
        filtered_count += layer.segments.size();
    }
    spdlog::info("  Filtered: {} extrusion segments kept", filtered_count);

    gcode::GeometryBuilder builder;

    // Use realistic nozzle width for smooth appearance
    builder.set_extrusion_width(0.26f);  // Thinner tubes for smoother look
    builder.set_travel_width(0.1f);      // Thin travel moves

    // Enable smooth (Gouraud) shading for polished appearance
    builder.set_smooth_shading(true);

    // Use filament color from G-code metadata if available
    if (!gcode_file.filament_color_hex.empty()) {
        builder.set_filament_color(gcode_file.filament_color_hex);
        spdlog::info("Using filament color {} with smooth shading", gcode_file.filament_color_hex);
    } else {
        spdlog::warn("No filament color found in G-code metadata, using rainbow gradient");
    }

    gcode::SimplificationOptions options;
    options.enable_merging = true;
    // Use default tolerance from SimplificationOptions (0.15mm for aggressive optimization)

    gcode::RibbonGeometry geometry = builder.build(filtered_gcode, options);

    // Log palette compression stats
    spdlog::info("Palette compression:");
    spdlog::info("  Normal palette: {} unique normals ({} bytes)",
                 geometry.normal_palette.size(),
                 geometry.normal_palette.size() * sizeof(glm::vec3));
    spdlog::info("  Color palette: {} unique colors ({} bytes)",
                 geometry.color_palette.size(),
                 geometry.color_palette.size() * sizeof(uint32_t));
    size_t palette_overhead = geometry.normal_palette.size() * sizeof(glm::vec3) +
                              geometry.color_palette.size() * sizeof(uint32_t);
    spdlog::info("  Total palette overhead: {} bytes ({:.2f} KB)", palette_overhead, palette_overhead / 1024.0f);

    spdlog::info("");
    spdlog::info("→ Initializing TinyGL...");

    const int WIDTH = 800;
    const int HEIGHT = 600;

    // Allocate framebuffer
    unsigned int* framebuffer = (unsigned int*)calloc(WIDTH * HEIGHT, sizeof(unsigned int));
    if (!framebuffer) {
        spdlog::error("Failed to allocate framebuffer");
        return 1;
    }

    // Initialize TinyGL
    ZBuffer* zb = nullptr;
    if (TGL_FEATURE_RENDER_BITS == 32) {
        zb = ZB_open(WIDTH, HEIGHT, ZB_MODE_RGBA, 0);
        spdlog::info("  Using 32-bit RGBA mode");
    } else {
        zb = ZB_open(WIDTH, HEIGHT, ZB_MODE_5R6G5B, 0);
        spdlog::info("  Using 16-bit RGB565 mode");
    }

    if (!zb) {
        spdlog::error("ZB_open failed");
        free(framebuffer);
        return 1;
    }

    glInit(zb);
    glViewport(0, 0, WIDTH, HEIGHT);

    // Enable depth testing for proper 3D rendering
    glEnable(GL_DEPTH_TEST);

    // Enable lighting for realistic shading
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);  // Allow vertex colors to modulate lighting
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    // Enable backface culling (geometry is correct now)
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    spdlog::info("  Rendering with depth test, lighting, and backface culling ENABLED");

    // Key light (GL_LIGHT0): Front-right, maximum visibility
    GLfloat key_pos[] = {100.0f, 100.0f, 200.0f, 1.0f};
    GLfloat key_ambient[] = {0.8f, 0.8f, 0.8f, 1.0f};     // Very high ambient - studio lighting
    GLfloat key_diffuse[] = {1.0f, 1.0f, 1.0f, 1.0f};     // Full key light
    GLfloat key_specular[] = {0.5f, 0.5f, 0.5f, 1.0f};    // Bright specular highlights

    glLightfv(GL_LIGHT0, GL_POSITION, key_pos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, key_ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, key_diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, key_specular);

    // Fill light (GL_LIGHT1): Back-left, very strong fill for even lighting
    glEnable(GL_LIGHT1);
    GLfloat fill_pos[] = {-80.0f, -80.0f, 100.0f, 1.0f};
    GLfloat fill_ambient[] = {0.0f, 0.0f, 0.0f, 1.0f};   // No ambient from fill
    GLfloat fill_diffuse[] = {0.8f, 0.8f, 0.8f, 1.0f};   // Very strong fill light
    GLfloat fill_specular[] = {0.0f, 0.0f, 0.0f, 1.0f};  // No specular from fill

    glLightfv(GL_LIGHT1, GL_POSITION, fill_pos);
    glLightfv(GL_LIGHT1, GL_AMBIENT, fill_ambient);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, fill_diffuse);
    glLightfv(GL_LIGHT1, GL_SPECULAR, fill_specular);

    spdlog::info("  Using two-point lighting (key + fill) for high contrast");

    // Clear background
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    spdlog::info("→ Geometry statistics:");
    spdlog::info("  Total triangle strips: {:>8}", geometry.strips.size());
    spdlog::info("  Total triangles:       {:>8}", geometry.strips.size() * 2);
    spdlog::info("  Extrusion triangles:   {:>8}", geometry.extrusion_triangle_count);
    spdlog::info("  Travel triangles:      {:>8}", geometry.travel_triangle_count);
    spdlog::info("");
    spdlog::info("→ Rendering {} triangle strips ({} triangles)...",
                geometry.strips.size(), geometry.extrusion_triangle_count);

    // Set up camera/projection (matching working LVGL viewer)
    glm::vec3 model_center = gcode_file.global_bounding_box.center();
    glm::vec3 model_size = gcode_file.global_bounding_box.size();
    float max_dim = std::max({model_size.x, model_size.y, model_size.z});

    spdlog::info("  Model center: [{:.1f}, {:.1f}, {:.1f}]",
                model_center.x, model_center.y, model_center.z);
    spdlog::info("  Model size:   [{:.1f}, {:.1f}, {:.1f}]",
                model_size.x, model_size.y, model_size.z);
    spdlog::info("  Max dimension: {:.1f}mm", max_dim);

    // Back to isometric view with VERY thick tubes
    float azimuth = 45.0f;     // Isometric azimuth
    float elevation = 30.0f;   // Isometric elevation
    float distance = max_dim * 2.5f;  // Move camera back to see whole model
    float zoom_level = 1.8f;  // Zoom in closer to fill frame

    // Compute camera position (matching GCodeCamera::compute_camera_position)
    float azimuth_rad = glm::radians(azimuth);
    float elevation_rad = glm::radians(elevation);
    glm::vec3 camera_pos = model_center + glm::vec3(
        distance * std::cos(elevation_rad) * std::sin(azimuth_rad),
        distance * std::cos(elevation_rad) * std::cos(azimuth_rad),
        distance * std::sin(elevation_rad)
    );

    spdlog::info("  Camera: Isometric view ({:.0f}° azimuth, {:.0f}° elevation)",
                azimuth, elevation);
    spdlog::info("  Camera pos:   [{:.1f}, {:.1f}, {:.1f}]",
                camera_pos.x, camera_pos.y, camera_pos.z);
    spdlog::info("  Distance:     {:.1f}mm", distance);

    // Set up orthographic projection
    float aspect_ratio = static_cast<float>(WIDTH) / static_cast<float>(HEIGHT);
    float ortho_size = distance / (2.0f * zoom_level);
    float left = -ortho_size * aspect_ratio;
    float right = ortho_size * aspect_ratio;
    float bottom = -ortho_size;
    float top = ortho_size;
    float near_plane = 0.1f;
    float far_plane = distance * 3.0f;  // Ensure everything fits

    // Create orthographic projection matrix with GLM (TinyGL's glOrtho is a stub)
    glm::mat4 projection = glm::ortho(left, right, bottom, top, near_plane, far_plane);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glLoadMatrixf(glm::value_ptr(projection));

    // Set up view matrix with lookAt
    glm::vec3 up(0, 0, 1);  // Z-up world
    glm::mat4 view = glm::lookAt(camera_pos, model_center, up);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glLoadMatrixf(glm::value_ptr(view));

    spdlog::info("  Projection: Orthographic [{:.1f}, {:.1f}] x [{:.1f}, {:.1f}]",
                left, right, bottom, top);
    spdlog::info("  Near/Far: {:.1f} / {:.1f}", near_plane, far_plane);

    // Render geometry using triangle strips
    spdlog::info("  Starting triangle strip rendering loop...");

    // DEBUG: Sample strips throughout geometry to check spatial distribution
    spdlog::info("  DEBUG: Sampling 10 strips across geometry:");
    for (int sample = 0; sample < 10; sample++) {
        size_t s = (geometry.strips.size() * sample) / 10;
        const auto& strip = geometry.strips[s];
        glm::vec3 pos = geometry.quantization.dequantize_vec3(geometry.vertices[strip[0]].position);
        spdlog::info("    S{}: [{:.2f}, {:.2f}, {:.2f}]", s, pos.x, pos.y, pos.z);
    }
    spdlog::info("  Bounding box: X=[{:.1f}, {:.1f}] Y=[{:.1f}, {:.1f}] Z=[{:.1f}, {:.1f}]",
                gcode_file.global_bounding_box.min.x, gcode_file.global_bounding_box.max.x,
                gcode_file.global_bounding_box.min.y, gcode_file.global_bounding_box.max.y,
                gcode_file.global_bounding_box.min.z, gcode_file.global_bounding_box.max.z);

    // Render using triangle strips (4 indices per strip = 2 triangles)
    size_t strips_rendered = 0;
    size_t triangles_rendered = 0;
    for (const auto& strip : geometry.strips) {
        glBegin(GL_TRIANGLE_STRIP);

        for (int i = 0; i < 4; i++) {
            const auto& vertex = geometry.vertices[strip[i]];

            // Dequantize position
            glm::vec3 pos = geometry.quantization.dequantize_vec3(vertex.position);

            // Lookup normal from palette
            const glm::vec3& normal = geometry.normal_palette[vertex.normal_index];
            glNormal3f(normal.x, normal.y, normal.z);

            // Lookup color from palette and unpack RGB
            uint32_t color_rgb = geometry.color_palette[vertex.color_index];
            glColor3f(((color_rgb >> 16) & 0xFF) / 255.0f,
                      ((color_rgb >> 8) & 0xFF) / 255.0f,
                      (color_rgb & 0xFF) / 255.0f);

            // Set vertex position
            glVertex3f(pos.x, pos.y, pos.z);
        }

        glEnd();
        strips_rendered++;
        triangles_rendered += 2;  // Each strip renders 2 triangles
    }

    spdlog::info("  Rendered {} triangle strips ({} triangles) to TinyGL",
                strips_rendered, triangles_rendered);

    // DEBUG: Verify rendered bounding box by sampling vertices
    spdlog::info("\n→ Verifying rendered geometry:");
    glm::vec3 rendered_min(FLT_MAX), rendered_max(-FLT_MAX);
    for (const auto& vertex : geometry.vertices) {
        glm::vec3 pos = geometry.quantization.dequantize_vec3(vertex.position);
        rendered_min = glm::min(rendered_min, pos);
        rendered_max = glm::max(rendered_max, pos);
    }
    spdlog::info("  Vertex bounds: X=[{:.1f}, {:.1f}] Y=[{:.1f}, {:.1f}] Z=[{:.1f}, {:.1f}]",
                rendered_min.x, rendered_max.x, rendered_min.y, rendered_max.y,
                rendered_min.z, rendered_max.z);
    spdlog::info("  Vertex size:   [{:.1f}, {:.1f}, {:.1f}]",
                rendered_max.x - rendered_min.x, rendered_max.y - rendered_min.y,
                rendered_max.z - rendered_min.z);
    spdlog::info("  Expected size: [{:.1f}, {:.1f}, {:.1f}]",
                gcode_file.global_bounding_box.size().x,
                gcode_file.global_bounding_box.size().y,
                gcode_file.global_bounding_box.size().z);

    // Flush TinyGL command buffer
    glFlush();
    glFinish();

    // Copy framebuffer from TinyGL
    ZB_copyFrameBuffer(zb, framebuffer, WIDTH * sizeof(unsigned int));

    // Convert to RGB24 for PPM output
    unsigned char* rgb_data = (unsigned char*)malloc(WIDTH * HEIGHT * 3);
    if (!rgb_data) {
        spdlog::error("Failed to allocate RGB buffer");
        ZB_close(zb);
        glClose();
        free(framebuffer);
        return 1;
    }

    // Extract RGB components
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        if (TGL_FEATURE_RENDER_BITS == 32) {
            rgb_data[i * 3 + 0] = (framebuffer[i] >> 16) & 0xFF;  // R
            rgb_data[i * 3 + 1] = (framebuffer[i] >> 8) & 0xFF;   // G
            rgb_data[i * 3 + 2] = (framebuffer[i] >> 0) & 0xFF;   // B
        } else {
            // RGB565 extraction
            rgb_data[i * 3 + 0] = GET_RED(framebuffer[i]);
            rgb_data[i * 3 + 1] = GET_GREEN(framebuffer[i]);
            rgb_data[i * 3 + 2] = GET_BLUE(framebuffer[i]);
        }
    }

    // Write PPM file
    const char* output_file = (argc > 2) ? argv[2] : "gcode_render.ppm";
    write_ppm(output_file, WIDTH, HEIGHT, rgb_data);

    // Cleanup
    free(rgb_data);
    free(framebuffer);
    ZB_close(zb);
    glClose();

    spdlog::info("");
    spdlog::info("═══════════════════════════════════════════════");
    spdlog::info("✓ Test complete!");
    spdlog::info("  View with: open {} (macOS)", output_file);
    spdlog::info("═══════════════════════════════════════════════");

    return 0;
}

#else

int main(int argc, char** argv) {
    fprintf(stderr, "Error: TinyGL support not compiled in\n");
    fprintf(stderr, "Rebuild with: make ENABLE_TINYGL_3D=yes\n");
    return 1;
}

#endif
