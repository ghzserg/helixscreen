// Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sparse Grid Test - Validates NanoVDB integration and compression

#include "gcode_parser.h"
#include "gcode_sdf_builder.h"
#include <spdlog/spdlog.h>
#include <fstream>

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::info);

    spdlog::info("═══════════════════════════════════════════════");
    spdlog::info("  Sparse Grid Compression Test");
    spdlog::info("═══════════════════════════════════════════════");
    spdlog::info("");

    // Parse small G-code file
    std::string gcode_path = (argc > 1) ? argv[1] : "assets/OrcaCube AD5M.gcode";
    spdlog::info("→ Parsing G-code: {}", gcode_path);

    std::ifstream file(gcode_path);
    if (!file.is_open()) {
        spdlog::error("Failed to open G-code file: {}", gcode_path);
        return 1;
    }

    gcode::GCodeParser parser;
    std::string line;
    while (std::getline(file, line)) {
        parser.parse_line(line);
    }
    file.close();

    gcode::ParsedGCodeFile gcode_file = parser.finalize();
    spdlog::info("✓ Parsed {} layers, {} segments total",
                 gcode_file.layers.size(), gcode_file.total_segments);
    spdlog::info("");

    // Build dense SDF grid
    spdlog::info("→ Building dense SDF grid...");
    gcode::SDFBuilder sdf_builder;
    gcode::SDFOptions options;
    options.grid_resolution = 128;  // Smaller resolution for quick test
    options.smoothing_radius = 0.3f;
    options.segment_radius_mm = 0.21f;

    // Build SDF into dense grid (we'll access it before conversion)
    gcode::TriangleMesh mesh = sdf_builder.build(gcode_file, options);

    // To test sparse grid, we need to manually create a dense grid and convert it
    spdlog::info("");
    spdlog::info("→ Creating dense grid for sparse conversion test...");

    // Create a simple dense grid with known pattern
    gcode::DenseVoxelGrid test_grid(64, gcode_file.global_bounding_box);
    test_grid.fill(10.0f);  // Background value

    // Set some values to create a pattern (sphere at center)
    glm::vec3 center = gcode_file.global_bounding_box.center();
    float radius = 5.0f;

    for (int z = 0; z < 64; ++z) {
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                glm::vec3 voxel_center = test_grid.voxel_to_world(glm::ivec3(x, y, z));
                float dist = glm::distance(voxel_center, center) - radius;
                if (dist < 5.0f) {  // Only store values near surface
                    test_grid.set(x, y, z, dist);
                }
            }
        }
    }

    spdlog::info("  Dense grid: {}³ = {:.2f} MB",
                 test_grid.resolution(),
                 test_grid.memory_usage() / (1024.0 * 1024.0));

    // Convert to sparse grid
    spdlog::info("");
    gcode::SparseVoxelGrid sparse_grid(test_grid);

    // Validate sparse grid
    spdlog::info("");
    spdlog::info("→ Validating sparse grid sampling...");

    // Test a few sample points
    int sample_count = 10;
    int matches = 0;
    float max_error = 0.0f;

    for (int i = 0; i < sample_count; ++i) {
        glm::vec3 test_pos = center + glm::vec3(
            (i - sample_count/2) * 0.5f,
            (i - sample_count/2) * 0.3f,
            (i - sample_count/2) * 0.2f
        );

        float dense_value = test_grid.sample(test_pos);
        float sparse_value = sparse_grid.sample(test_pos);
        float error = std::abs(dense_value - sparse_value);

        if (error < 0.01f) {
            matches++;
        }
        max_error = std::max(max_error, error);

        spdlog::debug("  Sample {}: dense={:.3f}, sparse={:.3f}, error={:.4f}",
                     i, dense_value, sparse_value, error);
    }

    spdlog::info("  Matches: {}/{} samples (max error: {:.4f})",
                 matches, sample_count, max_error);

    if (matches == sample_count && max_error < 0.1f) {
        spdlog::info("✓ Sparse grid validation PASSED");
    } else {
        spdlog::error("✗ Sparse grid validation FAILED");
        return 1;
    }

    spdlog::info("");
    spdlog::info("✓ Test complete!");
    spdlog::info("  Dense:        {:.2f} MB", test_grid.memory_usage() / (1024.0 * 1024.0));
    spdlog::info("  Sparse:       {:.2f} MB", sparse_grid.memory_usage() / (1024.0 * 1024.0));
    spdlog::info("  Compression:  {:.1f}×", sparse_grid.compression_ratio());

    return 0;
}
