// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for Phase 6: Streaming G-Code UI Integration

#include "gcode_layer_renderer.h"
#include "gcode_streaming_config.h"
#include "gcode_streaming_controller.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>
#include <unistd.h>

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::gcode;

namespace {

// Helper to create a temporary G-code file with test content
class TempGCodeFile {
  public:
    explicit TempGCodeFile(const std::string& content) {
        char temp_path[] = "/tmp/gcode_ui_test_XXXXXX";
        int fd = mkstemp(temp_path);
        if (fd == -1) {
            throw std::runtime_error("Failed to create temp file");
        }
        close(fd);
        path_ = temp_path;

        std::ofstream out(path_);
        out << content;
        out.close();
    }

    ~TempGCodeFile() {
        std::remove(path_.c_str());
    }

    const std::string& path() const {
        return path_;
    }

    size_t size() const {
        std::ifstream file(path_, std::ios::binary | std::ios::ate);
        return file.tellg();
    }

  private:
    std::string path_;
};

// Multi-layer G-code for testing
const std::string MULTI_LAYER_GCODE = R"(
; Test file for streaming integration
G28
G1 Z0.3 F1000
G1 X10 Y10 E1 F1500
G1 X20 Y10 E2
G1 X20 Y20 E3

G1 Z0.5 F1000
G1 X10 Y10 E4
G1 X20 Y10 E5
G1 X20 Y20 E6

G1 Z0.7 F1000
G1 X15 Y15 E7
G1 X25 Y15 E8

G1 Z0.9 F1000
G1 X10 Y10 E9
G1 X20 Y20 E10
)";

} // namespace

// =============================================================================
// Streaming Config Tests
// =============================================================================

TEST_CASE("GCodeStreamingConfig decision logic", "[gcode][streaming][config]") {
    SECTION("small files should not trigger streaming in AUTO mode") {
        // A 100 byte file should never trigger streaming
        // (threshold calculation: even on 47MB RAM, threshold would be ~0.9MB)
        bool should_stream = helix::should_use_gcode_streaming(100);

        // 100 bytes is tiny - should never stream regardless of RAM
        REQUIRE_FALSE(should_stream);

        // Note: Large file threshold depends on system RAM and cannot be reliably
        // tested without mocking memory info. Threshold scaling is tested separately
        // in "threshold scales with available memory" section below.
    }

    SECTION("streaming config description is valid") {
        std::string desc = helix::get_streaming_config_description();
        REQUIRE(!desc.empty());
        REQUIRE(desc.find("streaming=") != std::string::npos);
        INFO("Config description: " << desc);
    }

    SECTION("streaming threshold calculation") {
        // Test with known values: 47MB RAM, 40% threshold
        // max_memory_bytes = 47*1024*40/100 * 1024 = ~19.7MB
        // With 10x expansion factor: ~1.97MB threshold
        size_t threshold = helix::calculate_streaming_threshold(47 * 1024, 40);

        // Threshold should be in reasonable range (0.5MB to 5MB for 47MB RAM)
        REQUIRE(threshold >= 500 * 1024);      // At least 500KB
        REQUIRE(threshold <= 5 * 1024 * 1024); // At most 5MB

        INFO("Calculated threshold for 47MB RAM @ 40%: " << threshold / 1024 << "KB");
    }

    SECTION("threshold scales with available memory") {
        // More RAM = higher threshold
        size_t threshold_low = helix::calculate_streaming_threshold(32 * 1024, 40);
        size_t threshold_high = helix::calculate_streaming_threshold(128 * 1024, 40);

        REQUIRE(threshold_high > threshold_low);
    }

    SECTION("threshold scales with percentage") {
        // Higher percentage = higher threshold
        size_t threshold_low_pct = helix::calculate_streaming_threshold(64 * 1024, 20);
        size_t threshold_high_pct = helix::calculate_streaming_threshold(64 * 1024, 60);

        REQUIRE(threshold_high_pct > threshold_low_pct);
    }
}

// =============================================================================
// Layer Renderer Streaming Integration Tests
// =============================================================================

TEST_CASE("GCodeLayerRenderer streaming integration", "[gcode][streaming][renderer]") {
    TempGCodeFile temp_file(MULTI_LAYER_GCODE);

    SECTION("renderer starts with no data source") {
        GCodeLayerRenderer renderer;

        REQUIRE_FALSE(renderer.is_streaming());
        REQUIRE(renderer.get_gcode() == nullptr);
        REQUIRE(renderer.get_streaming_controller() == nullptr);
        REQUIRE(renderer.get_layer_count() == 0);
    }

    SECTION("set_streaming_controller switches to streaming mode") {
        GCodeLayerRenderer renderer;
        GCodeStreamingController controller;

        REQUIRE(controller.open_file(temp_file.path()));

        renderer.set_streaming_controller(&controller);

        REQUIRE(renderer.is_streaming());
        REQUIRE(renderer.get_streaming_controller() == &controller);
        REQUIRE(renderer.get_gcode() == nullptr); // Cleared when streaming is set
        REQUIRE(renderer.get_layer_count() == static_cast<int>(controller.get_layer_count()));
    }

    SECTION("set_gcode clears streaming mode") {
        GCodeLayerRenderer renderer;
        GCodeStreamingController controller;

        REQUIRE(controller.open_file(temp_file.path()));
        renderer.set_streaming_controller(&controller);
        REQUIRE(renderer.is_streaming());

        // Parse the file to get a ParsedGCodeFile
        std::ifstream file(temp_file.path());
        GCodeParser parser;
        std::string line;
        while (std::getline(file, line)) {
            parser.parse_line(line);
        }
        auto gcode = parser.finalize();

        renderer.set_gcode(&gcode);

        REQUIRE_FALSE(renderer.is_streaming());
        REQUIRE(renderer.get_gcode() == &gcode);
        REQUIRE(renderer.get_streaming_controller() == nullptr);
    }

    SECTION("layer count is correct in streaming mode") {
        GCodeLayerRenderer renderer;
        GCodeStreamingController controller;

        REQUIRE(controller.open_file(temp_file.path()));
        renderer.set_streaming_controller(&controller);

        // Our test file has 4 layers (Z=0.3, 0.5, 0.7, 0.9)
        REQUIRE(renderer.get_layer_count() == 4);
    }

    SECTION("set_current_layer is clamped correctly") {
        GCodeLayerRenderer renderer;
        GCodeStreamingController controller;

        REQUIRE(controller.open_file(temp_file.path()));
        renderer.set_streaming_controller(&controller);

        int max_layer = renderer.get_layer_count() - 1;

        // Normal case
        renderer.set_current_layer(2);
        REQUIRE(renderer.get_current_layer() == 2);

        // Clamp to max
        renderer.set_current_layer(100);
        REQUIRE(renderer.get_current_layer() == max_layer);

        // Clamp to 0
        renderer.set_current_layer(-5);
        REQUIRE(renderer.get_current_layer() == 0);
    }

    SECTION("get_layer_info works in streaming mode") {
        GCodeLayerRenderer renderer;
        GCodeStreamingController controller;

        REQUIRE(controller.open_file(temp_file.path()));
        renderer.set_streaming_controller(&controller);

        renderer.set_current_layer(1);
        auto info = renderer.get_layer_info();

        REQUIRE(info.layer_number == 1);
        // Z height should be approximately 0.5 for layer 1 based on our test file
        REQUIRE(info.z_height == Catch::Approx(0.5f).epsilon(0.1));
        // Should have segments (from the streaming controller)
        // Note: This triggers actual layer loading from the controller
        REQUIRE(info.segment_count > 0);
    }
}

// =============================================================================
// Controller and Renderer Integration Tests
// =============================================================================

TEST_CASE("Streaming controller prefetch integration", "[gcode][streaming]") {
    // Create a larger file with more layers
    std::string large_gcode = "; Test file\nG28\n";
    for (int layer = 0; layer < 20; ++layer) {
        float z = 0.2f + layer * 0.2f;
        large_gcode += "G1 Z" + std::to_string(z) + " F1000\n";
        large_gcode +=
            "G1 X" + std::to_string(10 + layer) + " Y10 E" + std::to_string(layer + 1) + " F1500\n";
    }

    TempGCodeFile temp_file(large_gcode);
    GCodeStreamingController controller;
    REQUIRE(controller.open_file(temp_file.path()));

    GCodeLayerRenderer renderer;
    renderer.set_streaming_controller(&controller);

    SECTION("accessing a layer triggers prefetch") {
        controller.clear_cache();

        // Access layer 10 - should prefetch nearby layers
        auto info = renderer.get_layer_info();
        renderer.set_current_layer(10);
        info = renderer.get_layer_info();

        // Nearby layers should be cached
        REQUIRE(controller.is_layer_cached(10));
        REQUIRE(controller.is_layer_cached(9));
        REQUIRE(controller.is_layer_cached(11));
    }
}

// =============================================================================
// Async Loading Tests
// =============================================================================

TEST_CASE("Streaming controller async open integration", "[gcode][streaming][async]") {
    TempGCodeFile temp_file(MULTI_LAYER_GCODE);

    SECTION("async open completes and renderer can be attached") {
        GCodeStreamingController controller;
        std::atomic<bool> completed{false};
        std::atomic<bool> success{false};

        controller.open_file_async(temp_file.path(), [&](bool result) {
            success.store(result);
            completed.store(true);
        });

        // Wait for completion (with timeout)
        auto start = std::chrono::steady_clock::now();
        while (!completed.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > std::chrono::seconds(5)) {
                FAIL("Async open timed out");
                break;
            }
        }

        REQUIRE(success.load());
        REQUIRE(controller.is_open());

        // Now attach to renderer - this is what the UI does after async completion
        GCodeLayerRenderer renderer;
        renderer.set_streaming_controller(&controller);

        REQUIRE(renderer.is_streaming());
        REQUIRE(renderer.get_layer_count() == 4);
    }
}

// =============================================================================
// Ghost Mode Disabled in Streaming Tests
// =============================================================================

TEST_CASE("Ghost mode behavior in streaming mode", "[gcode][streaming][renderer]") {
    TempGCodeFile temp_file(MULTI_LAYER_GCODE);
    GCodeStreamingController controller;
    REQUIRE(controller.open_file(temp_file.path()));

    GCodeLayerRenderer renderer;

    SECTION("ghost mode setting is preserved but effectively disabled") {
        // Enable ghost mode before setting streaming controller
        renderer.set_ghost_mode(true);
        REQUIRE(renderer.get_ghost_mode() == true);

        // Attach streaming controller
        renderer.set_streaming_controller(&controller);

        // Ghost mode setting is preserved (API doesn't change)
        // but the renderer internally skips ghost rendering in streaming mode
        REQUIRE(renderer.get_ghost_mode() == true);
        REQUIRE(renderer.is_streaming());
    }
}
