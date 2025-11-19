// Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// G-Code Tube Renderer - Instanced cylinder rendering for memory-efficient visualization
// Renders each G-code segment as a tube/cylinder using geometry instancing

#pragma once

#include "gcode_camera.h"
#include "gcode_parser.h"

#include <glm/glm.hpp>
#include <lvgl/lvgl.h>
#include <vector>

namespace gcode {

/**
 * @brief Tube template mesh for instanced rendering
 *
 * Single low-poly cylinder mesh that gets instanced and transformed
 * for each G-code segment. Optimized for embedded hardware with
 * minimal vertex count while maintaining smooth appearance.
 */
struct TubeMesh {
    std::vector<glm::vec3> vertices;  ///< Template vertex positions (unit cylinder)
    std::vector<glm::vec3> normals;   ///< Pre-computed normals
    std::vector<uint16_t> indices;    ///< Triangle indices

    /**
     * @brief Generate tube template mesh
     * @param radius Base radius (1.0 for unit cylinder)
     * @param length Length (1.0 for unit cylinder along Z-axis)
     * @param radial_segments Number of sides (8-16 recommended)
     * @param length_segments Number of segments along length (2 minimum)
     */
    void generate(float radius = 1.0f, float length = 1.0f, int radial_segments = 12,
                  int length_segments = 2);

    /**
     * @brief Get memory usage in bytes
     */
    size_t memory_usage() const {
        return vertices.size() * sizeof(glm::vec3) + normals.size() * sizeof(glm::vec3) +
               indices.size() * sizeof(uint16_t);
    }
};

/**
 * @brief Instance data for a single tube segment
 *
 * Compact representation of a G-code segment as a tube.
 * GPU transforms the template mesh to match this segment.
 */
struct TubeInstance {
    glm::vec3 start;   ///< Segment start position
    glm::vec3 end;     ///< Segment end position
    float radius;      ///< Tube radius
    glm::vec3 color;   ///< RGB color (0-1 range)

    /**
     * @brief Calculate transformation matrix for this instance
     * @return 4x4 transformation matrix (rotation + translation + scale)
     */
    glm::mat4 get_transform() const;

    /**
     * @brief Get segment length
     */
    float length() const {
        return glm::length(end - start);
    }
};

/**
 * @brief Tube-based G-code renderer for TinyGL
 *
 * Memory-efficient alternative to SDF/marching cubes approach.
 * Renders each G-code segment as an instanced tube/cylinder.
 *
 * Memory usage: ~28 bytes/segment (vs ~700 bytes/segment for SDF mesh)
 * - 50,000 segments: ~1.4MB (vs ~35MB for SDF)
 * - Template mesh: ~2KB (60 vertices)
 * - Total: ~1.5MB (vs ~35MB)
 */
class GCodeTubeRenderer {
  public:
    GCodeTubeRenderer();
    ~GCodeTubeRenderer();

    /**
     * @brief Render G-code segments as tubes
     * @param layer LVGL draw layer
     * @param gcode Parsed G-code file
     * @param camera Camera with view/projection matrices
     */
    void render(lv_layer_t* layer, const ParsedGCodeFile& gcode, const GCodeCamera& camera);

    /**
     * @brief Set viewport size
     */
    void set_viewport_size(int width, int height);

    /**
     * @brief Set tube radius (extrusion width)
     * @param radius_mm Tube radius in mm (default: 0.21mm)
     */
    void set_tube_radius(float radius_mm);

    /**
     * @brief Set filament color
     */
    void set_filament_color(const std::string& hex_color);

    /**
     * @brief Get statistics
     */
    struct Statistics {
        size_t segment_count;      ///< Number of tube instances
        size_t vertex_count;       ///< Total vertices rendered (instances × template)
        size_t triangle_count;     ///< Total triangles rendered
        size_t memory_bytes;       ///< Total memory usage
        float build_time_seconds;  ///< Time to build instance data
        float render_time_seconds; ///< Last frame render time
    };

    const Statistics& get_statistics() const {
        return stats_;
    }

  private:
    /**
     * @brief Build tube instances from G-code
     */
    void build_instances(const ParsedGCodeFile& gcode);

    /**
     * @brief Render all tube instances with TinyGL
     */
    void render_tubes(const GCodeCamera& camera);

    /**
     * @brief Initialize TinyGL context
     */
    void init_tinygl();

    /**
     * @brief Shutdown TinyGL context
     */
    void shutdown_tinygl();

    /**
     * @brief Setup lighting (two-point studio setup)
     */
    void setup_lighting();

    /**
     * @brief Convert TinyGL framebuffer to LVGL and draw
     */
    void draw_to_lvgl(lv_layer_t* layer);

    // Configuration
    int viewport_width_{800};
    int viewport_height_{600};
    float tube_radius_{0.21f};  // Half of typical 0.42mm extrusion width
    glm::vec3 filament_color_{0.15f, 0.65f, 0.60f};  // Default teal

    // Tube template
    TubeMesh tube_template_;

    // Instance data
    std::vector<TubeInstance> instances_;
    std::string current_gcode_filename_;  // Cache invalidation

    // TinyGL context
    void* zbuffer_{nullptr};
    unsigned int* framebuffer_{nullptr};

    // LVGL image buffer
    lv_draw_buf_t* draw_buf_{nullptr};

    // Statistics
    Statistics stats_;
};

} // namespace gcode
