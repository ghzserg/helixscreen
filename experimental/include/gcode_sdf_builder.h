// Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// G-Code SDF (Signed Distance Field) Builder
// Converts parsed G-code toolpath into volumetric signed distance field,
// then extracts smooth triangle mesh via marching cubes for finished-product visualization.

#pragma once

#include "gcode_parser.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace gcode {

// ============================================================================
// Dense Voxel Grid
// ============================================================================

/**
 * @brief Dense 3D voxel grid for SDF storage
 *
 * Stores signed distance values in a 3D array. Negative values = inside solid,
 * positive values = outside solid, zero = on surface.
 */
class DenseVoxelGrid {
  public:
    /**
     * @brief Construct voxel grid with specified resolution
     * @param resolution Grid size (resolution^3 voxels)
     * @param bounds World-space bounding box
     */
    DenseVoxelGrid(int resolution, const AABB& bounds);

    /**
     * @brief Get SDF value at voxel coordinates
     * @param x X voxel index [0, resolution)
     * @param y Y voxel index [0, resolution)
     * @param z Z voxel index [0, resolution)
     * @return Signed distance value (negative = inside)
     */
    float get(int x, int y, int z) const;

    /**
     * @brief Set SDF value at voxel coordinates
     */
    void set(int x, int y, int z, float value);

    /**
     * @brief Sample SDF at world-space position (trilinear interpolation)
     * @param pos World-space 3D position
     * @return Interpolated signed distance value
     */
    float sample(const glm::vec3& pos) const;

    /**
     * @brief Convert world-space position to voxel coordinates
     * @param pos World-space position
     * @return Voxel coordinates (may be out of bounds)
     */
    glm::ivec3 world_to_voxel(const glm::vec3& pos) const;

    /**
     * @brief Convert voxel coordinates to world-space position (voxel center)
     * @param voxel Voxel coordinates
     * @return World-space position
     */
    glm::vec3 voxel_to_world(const glm::ivec3& voxel) const;

    /**
     * @brief Get voxel size in world units
     */
    float voxel_size() const {
        return voxel_size_;
    }

    /**
     * @brief Get grid resolution
     */
    int resolution() const {
        return resolution_;
    }

    /**
     * @brief Get world-space bounding box
     */
    const AABB& bounds() const {
        return bounds_;
    }

    /**
     * @brief Get total memory usage in bytes
     */
    size_t memory_usage() const {
        return data_.size() * sizeof(float);
    }

    /**
     * @brief Fill entire grid with value
     */
    void fill(float value);

  private:
    int resolution_;             ///< Grid resolution (resolution^3 voxels)
    AABB bounds_;                ///< World-space bounding box
    float voxel_size_;           ///< Voxel size in world units
    std::vector<float> data_;    ///< Flattened 3D array [z][y][x]

    // Convert 3D coordinates to flat array index
    inline size_t index(int x, int y, int z) const {
        return z * resolution_ * resolution_ + y * resolution_ + x;
    }

    // Bounds checking
    inline bool in_bounds(int x, int y, int z) const {
        return x >= 0 && x < resolution_ && y >= 0 && y < resolution_ && z >= 0 && z < resolution_;
    }
};

// ============================================================================
// Sparse Voxel Grid (NanoVDB-backed)
// ============================================================================

/**
 * @brief Sparse 3D voxel grid using NanoVDB for memory-efficient SDF storage
 *
 * Provides 10-20× memory savings vs dense grid by only storing occupied voxels.
 * Read-only after construction - must be built from DenseVoxelGrid.
 * Ideal for large prints where dense grid memory becomes prohibitive.
 */
class SparseVoxelGrid {
  public:
    /**
     * @brief Construct sparse grid from dense grid
     * @param dense_grid Dense grid to convert
     *
     * Converts dense grid to sparse NanoVDB representation. This involves:
     * 1. Building NanoVDB grid structure from dense data
     * 2. Compressing via hierarchical tree (only stores occupied voxels)
     * 3. Original dense grid can be discarded after conversion
     */
    explicit SparseVoxelGrid(const DenseVoxelGrid& dense_grid);

    ~SparseVoxelGrid();

    // Non-copyable (NanoVDB grids are expensive to copy)
    SparseVoxelGrid(const SparseVoxelGrid&) = delete;
    SparseVoxelGrid& operator=(const SparseVoxelGrid&) = delete;

    // Movable
    SparseVoxelGrid(SparseVoxelGrid&& other) noexcept;
    SparseVoxelGrid& operator=(SparseVoxelGrid&& other) noexcept;

    /**
     * @brief Sample SDF at world-space position (trilinear interpolation)
     * @param pos World-space 3D position
     * @return Interpolated signed distance value
     *
     * Note: Read-only operation. NanoVDB grids cannot be modified after creation.
     */
    float sample(const glm::vec3& pos) const;

    /**
     * @brief Get SDF value at voxel coordinates (no interpolation)
     * @param x X voxel index
     * @param y Y voxel index
     * @param z Z voxel index
     * @return Signed distance value (or background value if unset)
     */
    float get(int x, int y, int z) const;

    /**
     * @brief Convert world-space position to voxel coordinates
     */
    glm::ivec3 world_to_voxel(const glm::vec3& pos) const;

    /**
     * @brief Convert voxel coordinates to world-space position (voxel center)
     */
    glm::vec3 voxel_to_world(const glm::ivec3& voxel) const;

    /**
     * @brief Get voxel size in world units
     */
    float voxel_size() const {
        return voxel_size_;
    }

    /**
     * @brief Get grid resolution
     */
    int resolution() const {
        return resolution_;
    }

    /**
     * @brief Get world-space bounding box
     */
    const AABB& bounds() const {
        return bounds_;
    }

    /**
     * @brief Get total memory usage in bytes
     * @return Compressed memory usage (typically 10-20× smaller than dense)
     */
    size_t memory_usage() const;

    /**
     * @brief Get compression ratio vs equivalent dense grid
     * @return Ratio (e.g., 15.0 means 15× smaller than dense)
     */
    float compression_ratio() const;

  private:
    int resolution_;           ///< Grid resolution (matches original dense grid)
    AABB bounds_;              ///< World-space bounding box
    float voxel_size_;         ///< Voxel size in world units
    void* nanovdb_grid_;       ///< Opaque pointer to NanoVDB grid (avoid header pollution)
    size_t dense_equivalent_; ///< Equivalent dense grid size (for compression ratio)
};

// ============================================================================
// Triangle Mesh Output
// ============================================================================

/**
 * @brief Simple triangle mesh (output from marching cubes)
 */
struct TriangleMesh {
    std::vector<glm::vec3> vertices;  ///< Vertex positions
    std::vector<glm::vec3> normals;   ///< Vertex normals (per-vertex)
    std::vector<uint32_t> indices;    ///< Triangle indices (3 per triangle)

    /**
     * @brief Calculate total memory usage in bytes
     */
    size_t memory_usage() const {
        return vertices.size() * sizeof(glm::vec3) + normals.size() * sizeof(glm::vec3) +
               indices.size() * sizeof(uint32_t);
    }

    /**
     * @brief Get triangle count
     */
    size_t triangle_count() const {
        return indices.size() / 3;
    }

    /**
     * @brief Clear all data
     */
    void clear() {
        vertices.clear();
        normals.clear();
        indices.clear();
    }
};

// ============================================================================
// SDF Builder Configuration
// ============================================================================

/**
 * @brief Grid storage backend selection
 */
enum class GridType {
    Dense,  ///< Dense 3D array - fast, simple, high memory (resolution^3 * 4 bytes)
    Sparse  ///< NanoVDB sparse tree - slower build, 10-20× memory savings
};

/**
 * @brief SDF generation and surface extraction options
 */
struct SDFOptions {
    int grid_resolution = 256;         ///< Voxel grid resolution (256^3 = 16.8M voxels)
    float smoothing_radius = 0.5f;     ///< Smooth minimum blending radius (mm)
    bool use_smooth_minimum = true;    ///< Use smooth minimum (vs Gaussian blur)
    float segment_radius_mm = 0.21f;   ///< Half of extrusion width (default: 0.42mm / 2)
    float iso_value = 0.0f;            ///< Marching cubes iso-surface value (0 = surface)
    GridType grid_type = GridType::Dense; ///< Grid storage backend (dense vs sparse)
    float max_z_height = std::numeric_limits<float>::max(); ///< Maximum Z height for partial extraction

    /**
     * @brief Validate and clamp options
     */
    void validate() {
        grid_resolution = std::max(64, std::min(1024, grid_resolution));
        smoothing_radius = std::max(0.1f, std::min(2.0f, smoothing_radius));
        segment_radius_mm = std::max(0.05f, std::min(1.0f, segment_radius_mm));
    }
};

// ============================================================================
// SDF Builder
// ============================================================================

/**
 * @brief Converts G-code toolpath into smooth triangle mesh via SDF reconstruction
 *
 * Pipeline:
 * 1. Compute bounding box from G-code and create voxel grid
 * 2. Rasterize each G-code segment as capsule SDF into grid
 * 3. Apply smooth minimum blending to merge overlapping segments
 * 4. Extract iso-surface using marching cubes algorithm
 * 5. Output triangle mesh with vertex normals for TinyGL rendering
 */
class SDFBuilder {
  public:
    SDFBuilder();

    /**
     * @brief Build triangle mesh from parsed G-code using SDF reconstruction
     *
     * @param gcode Parsed G-code file with toolpath segments
     * @param options SDF generation configuration
     * @return Smooth triangle mesh ready for TinyGL rendering
     */
    TriangleMesh build(const ParsedGCodeFile& gcode, const SDFOptions& options);

    /**
     * @brief Get statistics about last build operation
     */
    struct BuildStats {
        size_t input_segments;       ///< Original segment count
        size_t output_triangles;     ///< Triangle count in final mesh
        size_t output_vertices;      ///< Vertex count in final mesh
        size_t voxel_grid_bytes;     ///< Memory used by voxel grid
        size_t output_mesh_bytes;    ///< Memory used by output mesh
        float build_time_seconds;    ///< Total build time
        float sdf_time_seconds;      ///< SDF generation time
        float marching_cubes_seconds; ///< Marching cubes time

        void log() const; ///< Log statistics via spdlog
    };

    const BuildStats& last_stats() const {
        return stats_;
    }

  private:
    // SDF generation
    void generate_sdf(DenseVoxelGrid& grid, const std::vector<ToolpathSegment>& segments,
                      float segment_radius, float smoothing_radius);

    /**
     * @brief Compute signed distance from point to capsule (line segment with radius)
     * @param point Test point
     * @param start Capsule start point
     * @param end Capsule end point
     * @param radius Capsule radius
     * @return Signed distance (negative = inside)
     */
    float capsule_sdf(const glm::vec3& point, const glm::vec3& start, const glm::vec3& end,
                      float radius) const;

    /**
     * @brief Polynomial smooth minimum for SDF blending
     * @param a First SDF value
     * @param b Second SDF value
     * @param k Smoothing factor (larger = smoother)
     * @return Smoothly blended minimum
     */
    float smooth_min(float a, float b, float k) const;

    /**
     * @brief Extract iso-surface from SDF grid using marching cubes
     * @param grid Voxel grid containing SDF values
     * @param iso_value Iso-surface threshold (0.0 = exact surface)
     * @param max_z Optional maximum Z height (for partial extraction)
     * @return Triangle mesh representing the iso-surface
     */
    TriangleMesh extract_surface(const DenseVoxelGrid& grid, float iso_value,
                                  float max_z = std::numeric_limits<float>::max());

    // Build statistics
    BuildStats stats_;
};

} // namespace gcode
