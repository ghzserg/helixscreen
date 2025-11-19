// Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sparse Voxel Grid Implementation (NanoVDB Backend)
// Provides 10-20× memory compression for large G-code SDF grids

#include "gcode_sdf_builder.h"

#include <nanovdb/GridHandle.h>
#include <nanovdb/HostBuffer.h>
#include <nanovdb/tools/GridBuilder.h>
#include <nanovdb/tools/CreateNanoGrid.h>
#include <spdlog/spdlog.h>

namespace gcode {

// ============================================================================
// SparseVoxelGrid Implementation
// ============================================================================

// Internal NanoVDB grid storage (type-erased to avoid header pollution)
struct NanoVDBGridStorage {
    nanovdb::GridHandle<nanovdb::HostBuffer> handle;
    const nanovdb::FloatGrid* grid = nullptr;
};

SparseVoxelGrid::SparseVoxelGrid(const DenseVoxelGrid& dense_grid)
    : resolution_(dense_grid.resolution()),
      bounds_(dense_grid.bounds()),
      voxel_size_(dense_grid.voxel_size()),
      nanovdb_grid_(nullptr),
      dense_equivalent_(dense_grid.memory_usage()) {

    spdlog::info("→ Converting dense grid ({}³ = {:.2f} MB) to sparse NanoVDB...",
                 resolution_, dense_equivalent_ / (1024.0 * 1024.0));

    // Create NanoVDB grid builder with background value = +infinity
    using BuilderT = nanovdb::tools::build::Grid<float>;
    BuilderT builder(std::numeric_limits<float>::max());

    // Copy dense grid data to NanoVDB builder
    size_t non_background_voxels = 0;
    for (int z = 0; z < resolution_; ++z) {
        for (int y = 0; y < resolution_; ++y) {
            for (int x = 0; x < resolution_; ++x) {
                float value = dense_grid.get(x, y, z);

                // Only store voxels that differ from background (sparse optimization)
                // Positive values far from surface can use background value
                if (value < 10.0f) {  // Keep values near or inside surface
                    builder.setValue(nanovdb::Coord(x, y, z), value);
                    non_background_voxels++;
                }
            }
        }
    }

    // Build NanoVDB grid (triggers compression)
    auto storage = new NanoVDBGridStorage();
    storage->handle = nanovdb::tools::createNanoGrid(builder);
    storage->grid = storage->handle.template grid<float>();

    if (!storage->grid) {
        delete storage;
        throw std::runtime_error("Failed to create NanoVDB grid");
    }

    nanovdb_grid_ = storage;

    float occupancy_percent = (non_background_voxels * 100.0f) / (resolution_ * resolution_ * resolution_);
    spdlog::info("✓ Sparse conversion complete:");
    spdlog::info("  Non-background voxels: {:>12} ({:.1f}%)", non_background_voxels, occupancy_percent);
    spdlog::info("  Compressed size:       {:>12.2f} MB", memory_usage() / (1024.0 * 1024.0));
    spdlog::info("  Compression ratio:     {:>12.1f}×", compression_ratio());
}

SparseVoxelGrid::~SparseVoxelGrid() {
    if (nanovdb_grid_) {
        delete static_cast<NanoVDBGridStorage*>(nanovdb_grid_);
    }
}

SparseVoxelGrid::SparseVoxelGrid(SparseVoxelGrid&& other) noexcept
    : resolution_(other.resolution_),
      bounds_(other.bounds_),
      voxel_size_(other.voxel_size_),
      nanovdb_grid_(other.nanovdb_grid_),
      dense_equivalent_(other.dense_equivalent_) {
    other.nanovdb_grid_ = nullptr;
}

SparseVoxelGrid& SparseVoxelGrid::operator=(SparseVoxelGrid&& other) noexcept {
    if (this != &other) {
        if (nanovdb_grid_) {
            delete static_cast<NanoVDBGridStorage*>(nanovdb_grid_);
        }
        resolution_ = other.resolution_;
        bounds_ = other.bounds_;
        voxel_size_ = other.voxel_size_;
        nanovdb_grid_ = other.nanovdb_grid_;
        dense_equivalent_ = other.dense_equivalent_;
        other.nanovdb_grid_ = nullptr;
    }
    return *this;
}

float SparseVoxelGrid::sample(const glm::vec3& pos) const {
    auto storage = static_cast<NanoVDBGridStorage*>(nanovdb_grid_);
    if (!storage || !storage->grid) {
        return std::numeric_limits<float>::max();
    }

    // Convert world position to grid coordinates
    glm::ivec3 voxel = world_to_voxel(pos);

    // Get accessor for efficient random access
    auto accessor = storage->grid->getAccessor();

    // Compute fractional voxel position for trilinear interpolation
    glm::vec3 grid_pos = (pos - bounds_.min) / voxel_size_;
    glm::vec3 frac = grid_pos - glm::vec3(voxel);

    // Sample 8 corners of voxel cube
    float v000 = accessor.getValue(nanovdb::Coord(voxel.x,     voxel.y,     voxel.z));
    float v001 = accessor.getValue(nanovdb::Coord(voxel.x,     voxel.y,     voxel.z + 1));
    float v010 = accessor.getValue(nanovdb::Coord(voxel.x,     voxel.y + 1, voxel.z));
    float v011 = accessor.getValue(nanovdb::Coord(voxel.x,     voxel.y + 1, voxel.z + 1));
    float v100 = accessor.getValue(nanovdb::Coord(voxel.x + 1, voxel.y,     voxel.z));
    float v101 = accessor.getValue(nanovdb::Coord(voxel.x + 1, voxel.y,     voxel.z + 1));
    float v110 = accessor.getValue(nanovdb::Coord(voxel.x + 1, voxel.y + 1, voxel.z));
    float v111 = accessor.getValue(nanovdb::Coord(voxel.x + 1, voxel.y + 1, voxel.z + 1));

    // Trilinear interpolation
    float v00 = v000 * (1.0f - frac.x) + v100 * frac.x;
    float v01 = v001 * (1.0f - frac.x) + v101 * frac.x;
    float v10 = v010 * (1.0f - frac.x) + v110 * frac.x;
    float v11 = v011 * (1.0f - frac.x) + v111 * frac.x;

    float v0 = v00 * (1.0f - frac.y) + v10 * frac.y;
    float v1 = v01 * (1.0f - frac.y) + v11 * frac.y;

    return v0 * (1.0f - frac.z) + v1 * frac.z;
}

float SparseVoxelGrid::get(int x, int y, int z) const {
    auto storage = static_cast<NanoVDBGridStorage*>(nanovdb_grid_);
    if (!storage || !storage->grid) {
        return std::numeric_limits<float>::max();
    }

    auto accessor = storage->grid->getAccessor();
    return accessor.getValue(nanovdb::Coord(x, y, z));
}

glm::ivec3 SparseVoxelGrid::world_to_voxel(const glm::vec3& pos) const {
    glm::vec3 normalized = (pos - bounds_.min) / voxel_size_;
    return glm::ivec3(
        std::clamp(static_cast<int>(std::floor(normalized.x)), 0, resolution_ - 1),
        std::clamp(static_cast<int>(std::floor(normalized.y)), 0, resolution_ - 1),
        std::clamp(static_cast<int>(std::floor(normalized.z)), 0, resolution_ - 1)
    );
}

glm::vec3 SparseVoxelGrid::voxel_to_world(const glm::ivec3& voxel) const {
    // Match dense grid coordinate system (no half-voxel offset)
    return bounds_.min + glm::vec3(voxel) * voxel_size_;
}

size_t SparseVoxelGrid::memory_usage() const {
    auto storage = static_cast<NanoVDBGridStorage*>(nanovdb_grid_);
    if (!storage || !storage->grid) {
        return 0;
    }
    return storage->handle.size();
}

float SparseVoxelGrid::compression_ratio() const {
    size_t sparse_size = memory_usage();
    if (sparse_size == 0) {
        return 0.0f;
    }
    return static_cast<float>(dense_equivalent_) / static_cast<float>(sparse_size);
}

} // namespace gcode
