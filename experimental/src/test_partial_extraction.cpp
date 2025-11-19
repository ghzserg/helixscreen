// Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Partial Mesh Extraction Test - Validates Z-height slicing

#include "gcode_parser.h"
#include "gcode_sdf_builder.h"
#include <spdlog/spdlog.h>
#include <fstream>

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::info);

    spdlog::info("═══════════════════════════════════════════════");
    spdlog::info("  Partial Mesh Extraction Test");
    spdlog::info("═══════════════════════════════════════════════");
    spdlog::info("");

    // Parse G-code file
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

    // Get Z-height range
    float min_z = gcode_file.global_bounding_box.min.z;
    float max_z = gcode_file.global_bounding_box.max.z;
    float height = max_z - min_z;

    spdlog::info("  Z-height range: {:.2f}mm - {:.2f}mm (height: {:.2f}mm)",
                 min_z, max_z, height);
    spdlog::info("");

    // Build full SDF once
    spdlog::info("→ Building full SDF grid...");
    gcode::SDFBuilder sdf_builder;
    gcode::SDFOptions options;
    options.grid_resolution = 128;
    options.smoothing_radius = 0.3f;
    options.segment_radius_mm = 0.21f;

    // Build full mesh to baseline
    gcode::TriangleMesh full_mesh = sdf_builder.build(gcode_file, options);
    spdlog::info("✓ Full mesh: {} vertices, {} triangles, {:.2f} MB",
                 full_mesh.vertices.size(),
                 full_mesh.triangle_count(),
                 full_mesh.memory_usage() / (1024.0 * 1024.0));
    spdlog::info("");

    // Test partial extraction at various Z-heights
    float percentages[] = {0.25f, 0.50f, 0.75f};

    for (float pct : percentages) {
        float target_z = min_z + (height * pct);

        spdlog::info("→ Extracting at {:.0f}% (Z={:.2f}mm)...", pct * 100.0f, target_z);

        auto start = std::chrono::high_resolution_clock::now();

        // Build SDF and extract partial mesh
        gcode::SDFOptions partial_options = options;
        partial_options.max_z_height = target_z;
        gcode::TriangleMesh partial_mesh = sdf_builder.build(gcode_file, partial_options);

        auto end = std::chrono::high_resolution_clock::now();
        float extract_time = std::chrono::duration<float>(end - start).count();

        float size_ratio = (float)partial_mesh.triangle_count() / (float)full_mesh.triangle_count();

        spdlog::info("  Triangles: {:>6} ({:.1f}% of full)",
                     partial_mesh.triangle_count(),
                     size_ratio * 100.0f);
        spdlog::info("  Memory:    {:>6.2f} MB",
                     partial_mesh.memory_usage() / (1024.0 * 1024.0));
        spdlog::info("  Time:      {:>6.2f}s", extract_time);
        spdlog::info("");
    }

    spdlog::info("✓ Partial extraction test complete!");

    return 0;
}
