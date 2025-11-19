// Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// G-Code SDF Builder Implementation

#include "gcode_sdf_builder.h"
#include "marching_cubes_tables.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <spdlog/spdlog.h>

namespace gcode {

// ============================================================================
// Dense Voxel Grid Implementation
// ============================================================================

DenseVoxelGrid::DenseVoxelGrid(int resolution, const AABB& bounds)
    : resolution_(resolution), bounds_(bounds) {
    // Calculate voxel size (uniform in all dimensions, based on largest axis)
    glm::vec3 size = bounds.size();
    float max_dim = std::max({size.x, size.y, size.z});
    voxel_size_ = max_dim / static_cast<float>(resolution - 1);

    // Allocate grid (resolution^3 voxels)
    size_t total_voxels = static_cast<size_t>(resolution) * resolution * resolution;
    data_.resize(total_voxels, std::numeric_limits<float>::max()); // Initialize to "far outside"

    spdlog::info("Created {}^3 voxel grid ({:.2f} MB, voxel size: {:.3f}mm)",
                 resolution, memory_usage() / (1024.0 * 1024.0), voxel_size_);
}

float DenseVoxelGrid::get(int x, int y, int z) const {
    if (!in_bounds(x, y, z)) {
        return std::numeric_limits<float>::max(); // Far outside
    }
    return data_[index(x, y, z)];
}

void DenseVoxelGrid::set(int x, int y, int z, float value) {
    if (in_bounds(x, y, z)) {
        data_[index(x, y, z)] = value;
    }
}

float DenseVoxelGrid::sample(const glm::vec3& pos) const {
    // Convert world position to voxel coordinates (floating-point)
    glm::vec3 voxel_pos = (pos - bounds_.min) / voxel_size_;

    // Get integer voxel coordinates (floor)
    int x0 = static_cast<int>(std::floor(voxel_pos.x));
    int y0 = static_cast<int>(std::floor(voxel_pos.y));
    int z0 = static_cast<int>(std::floor(voxel_pos.z));

    // Trilinear interpolation fractions
    float fx = voxel_pos.x - x0;
    float fy = voxel_pos.y - y0;
    float fz = voxel_pos.z - z0;

    // Clamp to grid bounds
    int x1 = std::min(x0 + 1, resolution_ - 1);
    int y1 = std::min(y0 + 1, resolution_ - 1);
    int z1 = std::min(z0 + 1, resolution_ - 1);
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    z0 = std::max(0, z0);

    // Sample 8 corner voxels
    float v000 = get(x0, y0, z0);
    float v100 = get(x1, y0, z0);
    float v010 = get(x0, y1, z0);
    float v110 = get(x1, y1, z0);
    float v001 = get(x0, y0, z1);
    float v101 = get(x1, y0, z1);
    float v011 = get(x0, y1, z1);
    float v111 = get(x1, y1, z1);

    // Trilinear interpolation
    float v00 = v000 * (1.0f - fx) + v100 * fx;
    float v01 = v001 * (1.0f - fx) + v101 * fx;
    float v10 = v010 * (1.0f - fx) + v110 * fx;
    float v11 = v011 * (1.0f - fx) + v111 * fx;

    float v0 = v00 * (1.0f - fy) + v10 * fy;
    float v1 = v01 * (1.0f - fy) + v11 * fy;

    return v0 * (1.0f - fz) + v1 * fz;
}

glm::ivec3 DenseVoxelGrid::world_to_voxel(const glm::vec3& pos) const {
    glm::vec3 voxel_pos = (pos - bounds_.min) / voxel_size_;
    return glm::ivec3(static_cast<int>(std::round(voxel_pos.x)),
                      static_cast<int>(std::round(voxel_pos.y)),
                      static_cast<int>(std::round(voxel_pos.z)));
}

glm::vec3 DenseVoxelGrid::voxel_to_world(const glm::ivec3& voxel) const {
    return bounds_.min + glm::vec3(voxel) * voxel_size_;
}

void DenseVoxelGrid::fill(float value) {
    std::fill(data_.begin(), data_.end(), value);
}

// ============================================================================
// BuildStats Implementation
// ============================================================================

void SDFBuilder::BuildStats::log() const {
    spdlog::info("SDF Build Statistics:");
    spdlog::info("  Input segments:      {:>8}", input_segments);
    spdlog::info("  Output vertices:     {:>8}", output_vertices);
    spdlog::info("  Output triangles:    {:>8}", output_triangles);
    spdlog::info("  Voxel grid memory:   {:>8} KB ({:.2f} MB)", voxel_grid_bytes / 1024,
                 voxel_grid_bytes / (1024.0 * 1024.0));
    spdlog::info("  Output mesh memory:  {:>8} KB ({:.2f} MB)", output_mesh_bytes / 1024,
                 output_mesh_bytes / (1024.0 * 1024.0));
    spdlog::info("  Total build time:    {:.2f}s", build_time_seconds);
    spdlog::info("    SDF generation:    {:.2f}s", sdf_time_seconds);
    spdlog::info("    Marching cubes:    {:.2f}s", marching_cubes_seconds);
}

// ============================================================================
// SDF Builder Implementation
// ============================================================================

SDFBuilder::SDFBuilder() = default;

TriangleMesh SDFBuilder::build(const ParsedGCodeFile& gcode, const SDFOptions& options) {
    auto build_start = std::chrono::high_resolution_clock::now();

    // Validate options
    SDFOptions opts = options;
    opts.validate();

    // Flatten all segments from all layers into single vector
    std::vector<ToolpathSegment> all_segments;
    for (const auto& layer : gcode.layers) {
        all_segments.insert(all_segments.end(), layer.segments.begin(), layer.segments.end());
    }

    spdlog::info("Building SDF from G-code ({} segments, {} layers)...", all_segments.size(),
                 gcode.layers.size());
    spdlog::info("  Grid resolution: {}^3", opts.grid_resolution);
    spdlog::info("  Smoothing radius: {:.2f}mm", opts.smoothing_radius);
    spdlog::info("  Segment radius: {:.2f}mm", opts.segment_radius_mm);

    // Create voxel grid from bounding box
    DenseVoxelGrid grid(opts.grid_resolution, gcode.global_bounding_box);

    // Generate SDF from G-code segments
    auto sdf_start = std::chrono::high_resolution_clock::now();
    generate_sdf(grid, all_segments, opts.segment_radius_mm, opts.smoothing_radius);
    auto sdf_end = std::chrono::high_resolution_clock::now();

    // Extract surface using marching cubes (optionally partial)
    auto mc_start = std::chrono::high_resolution_clock::now();
    TriangleMesh mesh = extract_surface(grid, opts.iso_value, opts.max_z_height);
    auto mc_end = std::chrono::high_resolution_clock::now();

    auto build_end = std::chrono::high_resolution_clock::now();

    // Update statistics
    stats_.input_segments = all_segments.size();
    stats_.output_vertices = mesh.vertices.size();
    stats_.output_triangles = mesh.triangle_count();
    stats_.voxel_grid_bytes = grid.memory_usage();
    stats_.output_mesh_bytes = mesh.memory_usage();
    stats_.build_time_seconds =
        std::chrono::duration<float>(build_end - build_start).count();
    stats_.sdf_time_seconds = std::chrono::duration<float>(sdf_end - sdf_start).count();
    stats_.marching_cubes_seconds = std::chrono::duration<float>(mc_end - mc_start).count();

    stats_.log();

    return mesh;
}

// ============================================================================
// SDF Generation
// ============================================================================

float SDFBuilder::capsule_sdf(const glm::vec3& point, const glm::vec3& start,
                               const glm::vec3& end, float radius) const {
    // Vector from start to end
    glm::vec3 segment = end - start;
    float segment_length = glm::length(segment);

    if (segment_length < 0.0001f) {
        // Degenerate segment (point) -> treat as sphere
        return glm::distance(point, start) - radius;
    }

    // Project point onto line segment
    glm::vec3 to_point = point - start;
    float t = glm::dot(to_point, segment) / (segment_length * segment_length);
    t = std::clamp(t, 0.0f, 1.0f); // Clamp to segment endpoints

    // Closest point on segment
    glm::vec3 closest = start + t * segment;

    // Distance to capsule surface (distance to axis minus radius)
    return glm::distance(point, closest) - radius;
}

float SDFBuilder::smooth_min(float a, float b, float k) const {
    // Polynomial smooth minimum (C1 continuous)
    // Reference: https://iquilezles.org/articles/smin/
    float h = std::max(k - std::abs(a - b), 0.0f) / k;
    return std::min(a, b) - h * h * k * 0.25f;
}

void SDFBuilder::generate_sdf(DenseVoxelGrid& grid, const std::vector<ToolpathSegment>& segments,
                               float segment_radius, float smoothing_radius) {
    spdlog::info("Rasterizing {} segments into SDF grid...", segments.size());

    int resolution = grid.resolution();
    float voxel_size = grid.voxel_size();

    // Calculate influence radius (segment radius + smoothing radius + safety margin)
    float influence_radius = segment_radius + smoothing_radius + voxel_size;

    // Track feature types for debugging
    std::map<std::string, size_t> feature_type_counts;
    std::map<std::string, size_t> filtered_type_counts;

    // Process each segment
    size_t segments_processed = 0;
    size_t segments_filtered = 0;
    for (const auto& segment : segments) {
        // Skip travel moves (don't contribute to solid volume)
        if (!segment.is_extrusion) {
            continue;
        }

        // Track feature types for debugging
        if (!segment.feature_type.empty()) {
            feature_type_counts[segment.feature_type]++;
        } else {
            feature_type_counts["<empty>"]++;
        }

        // Filter by feature type - only include shell/perimeter segments for smooth solid surface
        // Exclude infill, gap fill, supports, etc. which create internal structure artifacts
        if (!segment.feature_type.empty()) {
            // Check if this is a shell/perimeter feature type
            bool is_shell_feature = false;

            // Common perimeter/shell feature types from PrusaSlicer/OrcaSlicer/Cura
            if (segment.feature_type.find("wall") != std::string::npos ||
                segment.feature_type.find("Wall") != std::string::npos ||
                segment.feature_type.find("perimeter") != std::string::npos ||
                segment.feature_type.find("Perimeter") != std::string::npos ||
                segment.feature_type.find("surface") != std::string::npos ||
                segment.feature_type.find("Surface") != std::string::npos ||
                segment.feature_type.find("skin") != std::string::npos ||
                segment.feature_type.find("Skin") != std::string::npos ||
                segment.feature_type.find("bridge") != std::string::npos ||
                segment.feature_type.find("Bridge") != std::string::npos) {
                is_shell_feature = true;
            }

            if (!is_shell_feature) {
                filtered_type_counts[segment.feature_type]++;
                segments_filtered++;
                continue; // Skip infill, gap fill, support, etc.
            }
        }

        // Compute segment bounding box in voxel space
        glm::vec3 seg_min = glm::min(segment.start, segment.end) - glm::vec3(influence_radius);
        glm::vec3 seg_max = glm::max(segment.start, segment.end) + glm::vec3(influence_radius);

        glm::ivec3 voxel_min = grid.world_to_voxel(seg_min);
        glm::ivec3 voxel_max = grid.world_to_voxel(seg_max);

        // Clamp to grid bounds
        voxel_min = glm::max(voxel_min, glm::ivec3(0));
        voxel_max = glm::min(voxel_max, glm::ivec3(resolution - 1));

        // Rasterize segment into affected voxels
        for (int z = voxel_min.z; z <= voxel_max.z; ++z) {
            for (int y = voxel_min.y; y <= voxel_max.y; ++y) {
                for (int x = voxel_min.x; x <= voxel_max.x; ++x) {
                    glm::vec3 voxel_center = grid.voxel_to_world(glm::ivec3(x, y, z));

                    // Compute distance to this segment
                    float dist = capsule_sdf(voxel_center, segment.start, segment.end,
                                             segment_radius);

                    // Apply smooth minimum with existing value
                    float current = grid.get(x, y, z);
                    float blended = smooth_min(current, dist, smoothing_radius);
                    grid.set(x, y, z, blended);
                }
            }
        }

        segments_processed++;
        if (segments_processed % 1000 == 0) {
            spdlog::debug("  Processed {}/{} segments...", segments_processed, segments.size());
        }
    }

    spdlog::info("✓ SDF generation complete ({} shell/perimeter segments processed, {} filtered out)",
                 segments_processed, segments_filtered);

    // Log feature type statistics for debugging
    spdlog::info("Feature types found in G-code:");
    for (const auto& [type, count] : feature_type_counts) {
        spdlog::info("  {:30} : {:>6} segments", type, count);
    }

    if (!filtered_type_counts.empty()) {
        spdlog::info("Feature types filtered out (not included in SDF):");
        for (const auto& [type, count] : filtered_type_counts) {
            spdlog::info("  {:30} : {:>6} segments", type, count);
        }
    }
}

// ============================================================================
// Marching Cubes Surface Extraction
// ============================================================================

TriangleMesh SDFBuilder::extract_surface(const DenseVoxelGrid& grid, float iso_value, float max_z) {
    if (max_z < std::numeric_limits<float>::max()) {
        spdlog::info("Extracting partial iso-surface (iso_value={:.3f}, max_z={:.2f}mm)...",
                     iso_value, max_z);
    } else {
        spdlog::info("Extracting iso-surface (iso_value={:.3f}) via marching cubes...", iso_value);
    }

    TriangleMesh mesh;
    int resolution = grid.resolution();

    // Edge vertex indices: for each cube, which 12 edges are intersected
    // Edge numbering (Paul Bourke convention):
    //    4--------5
    //   /|       /|
    //  / |      / |
    // 7--------6  |
    // |  0-----|--1
    // | /      | /
    // |/       |/
    // 3--------2
    //
    // Edges: 0-1, 1-2, 2-3, 3-0 (bottom face)
    //        4-5, 5-6, 6-7, 7-4 (top face)
    //        0-4, 1-5, 2-6, 3-7 (vertical)

    // Cube corner offsets (8 corners)
    const glm::ivec3 corner_offsets[8] = {
        {0, 0, 0}, // 0
        {1, 0, 0}, // 1
        {1, 1, 0}, // 2
        {0, 1, 0}, // 3
        {0, 0, 1}, // 4
        {1, 0, 1}, // 5
        {1, 1, 1}, // 6
        {0, 1, 1}  // 7
    };

    // Edge connections (which two corners each edge connects)
    const int edge_connections[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, // Bottom face
        {4, 5}, {5, 6}, {6, 7}, {7, 4}, // Top face
        {0, 4}, {1, 5}, {2, 6}, {3, 7}  // Vertical
    };

    // Cache for vertex sharing across cubes
    std::unordered_map<uint64_t, uint32_t> vertex_cache;

    // Canonical edge key based on corner positions (not cube position + local edge number)
    // This ensures adjacent cubes use the same key for shared edges
    auto make_edge_key = [](const glm::ivec3& c0, const glm::ivec3& c1) -> uint64_t {
        // Use min/max for canonical ordering (c0→c1 same as c1→c0)
        glm::ivec3 min_c = glm::min(c0, c1);
        glm::ivec3 max_c = glm::max(c0, c1);

        // Encode: min corner position + axis direction (0=X, 1=Y, 2=Z)
        uint64_t axis = (max_c.x != min_c.x) ? 0 : (max_c.y != min_c.y) ? 1 : 2;
        return (static_cast<uint64_t>(min_c.x) << 44) |
               (static_cast<uint64_t>(min_c.y) << 30) |
               (static_cast<uint64_t>(min_c.z) << 16) |
               (axis << 14);
    };

    // Process each cube in the grid
    size_t cubes_processed = 0;
    size_t triangles_generated = 0;

    for (int z = 0; z < resolution - 1; ++z) {
        for (int y = 0; y < resolution - 1; ++y) {
            for (int x = 0; x < resolution - 1; ++x) {
                // Sample SDF at 8 cube corners
                float corner_values[8];
                glm::vec3 corner_positions[8];

                for (int i = 0; i < 8; ++i) {
                    glm::ivec3 corner = glm::ivec3(x, y, z) + corner_offsets[i];
                    corner_values[i] = grid.get(corner.x, corner.y, corner.z);
                    corner_positions[i] = grid.voxel_to_world(corner);
                }

                // Skip cubes entirely above the Z-height threshold
                if (corner_positions[0].z > max_z) {
                    continue;
                }

                // Determine cube configuration (8-bit index)
                int cube_index = 0;
                for (int i = 0; i < 8; ++i) {
                    if (corner_values[i] < iso_value) {
                        cube_index |= (1 << i);
                    }
                }

                // Skip if cube is entirely inside or outside
                if (cube_index == 0 || cube_index == 255) {
                    continue;
                }

                // Find which edges are intersected
                int edge_flags = marching_cubes::edge_table[cube_index];

                // Calculate vertex positions on intersected edges (with caching)
                uint32_t edge_vertices[12];
                for (int edge = 0; edge < 12; ++edge) {
                    if (edge_flags & (1 << edge)) {
                        // Get the two corners this edge connects
                        int c0 = edge_connections[edge][0];
                        int c1 = edge_connections[edge][1];

                        // Calculate actual corner positions in grid space
                        glm::ivec3 corner0 = glm::ivec3(x, y, z) + corner_offsets[c0];
                        glm::ivec3 corner1 = glm::ivec3(x, y, z) + corner_offsets[c1];

                        // Generate canonical edge key based on corner positions
                        uint64_t edge_key = make_edge_key(corner0, corner1);
                        auto it = vertex_cache.find(edge_key);

                        if (it != vertex_cache.end()) {
                            edge_vertices[edge] = it->second;
                        } else {
                            // Calculate new vertex via linear interpolation

                            float v0 = corner_values[c0];
                            float v1 = corner_values[c1];

                            // Linear interpolation parameter
                            float t = (iso_value - v0) / (v1 - v0 + 0.0001f);
                            t = std::clamp(t, 0.0f, 1.0f);

                            // Interpolate position
                            glm::vec3 pos = glm::mix(corner_positions[c0], corner_positions[c1], t);

                            // Add vertex to mesh
                            uint32_t vertex_index = static_cast<uint32_t>(mesh.vertices.size());
                            mesh.vertices.push_back(pos);
                            mesh.normals.push_back(glm::vec3(0.0f)); // Placeholder, will calculate later

                            edge_vertices[edge] = vertex_index;
                            vertex_cache[edge_key] = vertex_index;
                        }
                    }
                }

                // Generate triangles from lookup table
                const int* triangle_list = marching_cubes::triangle_table[cube_index];
                for (int i = 0; triangle_list[i] != -1; i += 3) {
                    // Add triangle with correct winding for outward-pointing normals
                    // Since SDF normals point from negative (inside) to positive (outside),
                    // and our SDF has negative inside the solid, normals point outward.
                    // Use standard winding order to match normal direction.
                    mesh.indices.push_back(edge_vertices[triangle_list[i + 0]]);
                    mesh.indices.push_back(edge_vertices[triangle_list[i + 1]]);
                    mesh.indices.push_back(edge_vertices[triangle_list[i + 2]]);
                    triangles_generated++;
                }

                cubes_processed++;
            }
        }

        if ((z + 1) % 16 == 0) {
            spdlog::debug("  Processed {}/{} slices...", z + 1, resolution - 1);
        }
    }

    spdlog::info("✓ Marching cubes complete: {} cubes processed, {} triangles generated",
                 cubes_processed, triangles_generated);

    // Calculate vertex normals (averaging face normals)
    spdlog::info("Calculating vertex normals...");
    std::vector<glm::vec3> normal_accum(mesh.vertices.size(), glm::vec3(0.0f));
    std::vector<int> normal_count(mesh.vertices.size(), 0);

    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        uint32_t i0 = mesh.indices[i];
        uint32_t i1 = mesh.indices[i + 1];
        uint32_t i2 = mesh.indices[i + 2];

        glm::vec3 v0 = mesh.vertices[i0];
        glm::vec3 v1 = mesh.vertices[i1];
        glm::vec3 v2 = mesh.vertices[i2];

        // Face normal
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 face_normal = glm::normalize(glm::cross(edge1, edge2));

        // Accumulate for each vertex
        normal_accum[i0] += face_normal;
        normal_accum[i1] += face_normal;
        normal_accum[i2] += face_normal;
        normal_count[i0]++;
        normal_count[i1]++;
        normal_count[i2]++;
    }

    // Average and normalize
    for (size_t i = 0; i < mesh.normals.size(); ++i) {
        if (normal_count[i] > 0) {
            mesh.normals[i] = glm::normalize(normal_accum[i] / static_cast<float>(normal_count[i]));
        } else {
            mesh.normals[i] = glm::vec3(0.0f, 0.0f, 1.0f); // Fallback
        }
    }

    spdlog::info("✓ Vertex normals calculated");

    return mesh;
}

} // namespace gcode
