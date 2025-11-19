// Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// G-Code Structure Analysis
// Detailed analysis of segment distribution, directions, and spatial coverage

#include "gcode_parser.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <string>
#include <map>
#include <cmath>

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::info);

    std::string gcode_path = (argc > 1) ? argv[1] : "assets/OrcaCube AD5M.gcode";
    spdlog::info("═══════════════════════════════════════════════");
    spdlog::info("  G-Code Structure Analysis");
    spdlog::info("  File: {}", gcode_path);
    spdlog::info("═══════════════════════════════════════════════\n");

    // Parse G-code
    std::ifstream file(gcode_path);
    if (!file.is_open()) {
        spdlog::error("Failed to open: {}", gcode_path);
        return 1;
    }

    gcode::GCodeParser parser;
    std::string line;
    while (std::getline(file, line)) {
        parser.parse_line(line);
    }
    file.close();

    gcode::ParsedGCodeFile gcode_file = parser.finalize();

    // Analyze segment directions and spatial distribution
    std::map<std::string, size_t> direction_counts;
    size_t extrusion_count = 0;
    size_t travel_count = 0;

    // Spatial bins: divide XYZ space into 4x4x4 grid
    const int BINS = 4;
    int spatial_bins[BINS][BINS][BINS] = {{{0}}};

    glm::vec3 bbox_min = gcode_file.global_bounding_box.min;
    glm::vec3 bbox_max = gcode_file.global_bounding_box.max;
    glm::vec3 bbox_size = bbox_max - bbox_min;

    spdlog::info("Bounding Box:");
    spdlog::info("  Min: [{:.1f}, {:.1f}, {:.1f}]", bbox_min.x, bbox_min.y, bbox_min.z);
    spdlog::info("  Max: [{:.1f}, {:.1f}, {:.1f}]", bbox_max.x, bbox_max.y, bbox_max.z);
    spdlog::info("  Size: [{:.1f}, {:.1f}, {:.1f}]\n", bbox_size.x, bbox_size.y, bbox_size.z);

    for (const auto& layer : gcode_file.layers) {
        for (const auto& seg : layer.segments) {
            if (seg.is_extrusion) extrusion_count++;
            else travel_count++;

            // Classify by direction
            glm::vec3 dir = glm::normalize(seg.end - seg.start);
            std::string dir_class;

            if (std::abs(dir.x) > 0.7f && std::abs(dir.y) < 0.3f) {
                dir_class = (dir.x > 0) ? "+X" : "-X";
            } else if (std::abs(dir.y) > 0.7f && std::abs(dir.x) < 0.3f) {
                dir_class = (dir.y > 0) ? "+Y" : "-Y";
            } else if (std::abs(dir.z) > 0.7f) {
                dir_class = (dir.z > 0) ? "+Z" : "-Z";
            } else if (std::abs(dir.x) > 0.3f && std::abs(dir.y) > 0.3f) {
                dir_class = "XY-diagonal";
            } else {
                dir_class = "other";
            }

            direction_counts[dir_class]++;

            // Bin by position (use segment start)
            glm::vec3 pos = seg.start - bbox_min;
            int x_bin = std::min(BINS - 1, (int)(BINS * pos.x / bbox_size.x));
            int y_bin = std::min(BINS - 1, (int)(BINS * pos.y / bbox_size.y));
            int z_bin = std::min(BINS - 1, (int)(BINS * pos.z / bbox_size.z));
            spatial_bins[x_bin][y_bin][z_bin]++;
        }
    }

    spdlog::info("Segment Summary:");
    spdlog::info("  Total:     {}", extrusion_count + travel_count);
    spdlog::info("  Extrusions: {}", extrusion_count);
    spdlog::info("  Travels:    {}\n", travel_count);

    spdlog::info("Direction Distribution (extrusions + travels):");
    for (const auto& [dir, count] : direction_counts) {
        float pct = 100.0f * count / (extrusion_count + travel_count);
        spdlog::info("  {:12s}: {:>6} ({:>5.1f}%)", dir, count, pct);
    }

    spdlog::info("\nSpatial Distribution ({}x{}x{} bins):", BINS, BINS, BINS);
    spdlog::info("X bins: [{:.1f}, {:.1f}, {:.1f}, {:.1f}]",
                bbox_min.x, bbox_min.x + bbox_size.x * 0.25f,
                bbox_min.x + bbox_size.x * 0.50f, bbox_min.x + bbox_size.x * 0.75f);
    spdlog::info("Y bins: [{:.1f}, {:.1f}, {:.1f}, {:.1f}]",
                bbox_min.y, bbox_min.y + bbox_size.y * 0.25f,
                bbox_min.y + bbox_size.y * 0.50f, bbox_min.y + bbox_size.y * 0.75f);
    spdlog::info("Z bins: [{:.1f}, {:.1f}, {:.1f}, {:.1f}]\n",
                bbox_min.z, bbox_min.z + bbox_size.z * 0.25f,
                bbox_min.z + bbox_size.z * 0.50f, bbox_min.z + bbox_size.z * 0.75f);

    for (int z = BINS - 1; z >= 0; z--) {
        spdlog::info("Z-bin {} (height {:.1f}-{:.1f}mm):",
                    z, bbox_min.z + z * bbox_size.z / BINS,
                    bbox_min.z + (z + 1) * bbox_size.z / BINS);
        for (int y = 0; y < BINS; y++) {
            std::string row = "  Y" + std::to_string(y) + ": ";
            for (int x = 0; x < BINS; x++) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%5d ", spatial_bins[x][y][z]);
                row += buf;
            }
            spdlog::info(row);
        }
        spdlog::info("");
    }

    spdlog::info("═══════════════════════════════════════════════");
    spdlog::info("✓ Analysis complete!");
    spdlog::info("═══════════════════════════════════════════════");

    return 0;
}
