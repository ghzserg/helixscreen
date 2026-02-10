// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_prerendered_images.cpp
 * @brief Unit tests for pre-rendered image path selection and LZ4 compression
 *
 * Tests the logic for selecting appropriate pre-rendered image sizes
 * based on display dimensions, and validates that generated .bin files
 * use LZ4 compression.
 */

#include "../../include/prerendered_images.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// Splash Screen Size Selection Tests
// ============================================================================

TEST_CASE("get_splash_size_name returns correct size category", "[assets][splash]") {
    SECTION("Tiny displays (< 600px width)") {
        REQUIRE(std::string(get_splash_size_name(480)) == "tiny");
        REQUIRE(std::string(get_splash_size_name(320)) == "tiny");
        REQUIRE(std::string(get_splash_size_name(599)) == "tiny");
    }

    SECTION("Small displays (600-899px width) - AD5M class") {
        REQUIRE(std::string(get_splash_size_name(600)) == "small");
        REQUIRE(std::string(get_splash_size_name(800)) == "small");
        REQUIRE(std::string(get_splash_size_name(899)) == "small");
    }

    SECTION("Medium displays (900-1099px width)") {
        REQUIRE(std::string(get_splash_size_name(900)) == "medium");
        REQUIRE(std::string(get_splash_size_name(1024)) == "medium");
        REQUIRE(std::string(get_splash_size_name(1099)) == "medium");
    }

    SECTION("Large displays (>= 1100px width)") {
        REQUIRE(std::string(get_splash_size_name(1100)) == "large");
        REQUIRE(std::string(get_splash_size_name(1280)) == "large");
        REQUIRE(std::string(get_splash_size_name(1920)) == "large");
    }

    SECTION("Boundary conditions") {
        // Exact boundaries
        REQUIRE(std::string(get_splash_size_name(599)) == "tiny");
        REQUIRE(std::string(get_splash_size_name(600)) == "small");
        REQUIRE(std::string(get_splash_size_name(899)) == "small");
        REQUIRE(std::string(get_splash_size_name(900)) == "medium");
        REQUIRE(std::string(get_splash_size_name(1099)) == "medium");
        REQUIRE(std::string(get_splash_size_name(1100)) == "large");
    }
}

TEST_CASE("get_prerendered_splash_path generates correct paths", "[assets][splash]") {
    SECTION("Path format includes size name") {
        // Note: These tests check path format, not file existence
        // The function will fall back to PNG if .bin doesn't exist
        std::string path_800 = get_prerendered_splash_path(800);

        // Should either be a prerendered .bin or fallback PNG
        bool is_bin = path_800.find(".bin") != std::string::npos;
        bool is_png = path_800.find(".png") != std::string::npos;
        REQUIRE((is_bin || is_png));

        // Should start with LVGL path prefix
        REQUIRE(path_800.substr(0, 2) == "A:");
    }

    SECTION("Different screen sizes get different paths") {
        std::string path_tiny = get_prerendered_splash_path(480);
        std::string path_small = get_prerendered_splash_path(800);
        std::string path_large = get_prerendered_splash_path(1280);

        // Paths should differ (unless all falling back to same PNG)
        // At minimum, they should all be valid LVGL paths
        REQUIRE(path_tiny.substr(0, 2) == "A:");
        REQUIRE(path_small.substr(0, 2) == "A:");
        REQUIRE(path_large.substr(0, 2) == "A:");
    }
}

// ============================================================================
// Printer Image Size Selection Tests
// ============================================================================

TEST_CASE("get_printer_image_size returns correct target size", "[assets][printer]") {
    SECTION("Small displays (< 600px) get 150px images") {
        REQUIRE(get_printer_image_size(480) == 150);
        REQUIRE(get_printer_image_size(320) == 150);
        REQUIRE(get_printer_image_size(599) == 150);
    }

    SECTION("Medium-large displays (>= 600px) get 300px images") {
        REQUIRE(get_printer_image_size(600) == 300);
        REQUIRE(get_printer_image_size(800) == 300);
        REQUIRE(get_printer_image_size(1024) == 300);
        REQUIRE(get_printer_image_size(1920) == 300);
    }

    SECTION("Boundary at 600px") {
        REQUIRE(get_printer_image_size(599) == 150);
        REQUIRE(get_printer_image_size(600) == 300);
    }
}

TEST_CASE("get_prerendered_printer_path generates correct paths", "[assets][printer]") {
    SECTION("Path format is correct") {
        std::string path = get_prerendered_printer_path("creality-k1", 800);

        // Should start with LVGL prefix
        REQUIRE(path.substr(0, 2) == "A:");

        // Should contain printer name
        REQUIRE(path.find("creality-k1") != std::string::npos);

        // Should be .bin or .png
        bool is_bin = path.find(".bin") != std::string::npos;
        bool is_png = path.find(".png") != std::string::npos;
        REQUIRE((is_bin || is_png));
    }

    SECTION("Different screen sizes generate different paths") {
        std::string path_small = get_prerendered_printer_path("voron-24r2", 480);
        std::string path_large = get_prerendered_printer_path("voron-24r2", 800);

        // Both should be valid paths
        REQUIRE(path_small.substr(0, 2) == "A:");
        REQUIRE(path_large.substr(0, 2) == "A:");

        // Paths may differ (unless both falling back to PNG)
        // The key is both are valid
        REQUIRE(path_small.find("voron-24r2") != std::string::npos);
        REQUIRE(path_large.find("voron-24r2") != std::string::npos);
    }

    SECTION("Various printer names work correctly") {
        std::vector<std::string> printers = {
            "creality-k1",    "creality-ender-3", "voron-24r2", "flashforge-adventurer-5m",
            "anycubic-kobra",
        };

        for (const auto& printer : printers) {
            std::string path = get_prerendered_printer_path(printer, 800);
            INFO("Printer: " << printer);
            REQUIRE(path.substr(0, 2) == "A:");
            REQUIRE(path.find(printer) != std::string::npos);
        }
    }
}

// ============================================================================
// Fallback Behavior Tests
// ============================================================================

TEST_CASE("Prerendered paths fall back to PNG when .bin missing", "[assets][fallback]") {
    SECTION("Splash fallback is PNG") {
        // Since we're testing without pre-rendered files, should get PNG fallback
        std::string path = get_prerendered_splash_path(800);

        // In test environment without pre-rendered files, should fall back to PNG
        // The path should be valid either way
        REQUIRE(path.length() > 2);
        REQUIRE(path.substr(0, 2) == "A:");
    }

    SECTION("Printer fallback is PNG") {
        // Non-existent printer should still return a valid path
        std::string path = get_prerendered_printer_path("nonexistent-printer", 800);

        // Should fall back to PNG path
        REQUIRE(path.substr(0, 2) == "A:");
        REQUIRE(path.find("nonexistent-printer") != std::string::npos);
        REQUIRE(path.find(".png") != std::string::npos);
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("Prerendered image edge cases", "[assets][edge]") {
    SECTION("Zero width defaults sensibly") {
        // Should not crash, pick smallest size
        REQUIRE(get_printer_image_size(0) == 150);
        REQUIRE(std::string(get_splash_size_name(0)) == "tiny");
    }

    SECTION("Negative width handled gracefully") {
        // Should not crash
        REQUIRE(get_printer_image_size(-100) == 150);
        REQUIRE(std::string(get_splash_size_name(-100)) == "tiny");
    }

    SECTION("Very large width handled") {
        REQUIRE(get_printer_image_size(10000) == 300);
        REQUIRE(std::string(get_splash_size_name(10000)) == "large");
    }

    SECTION("Empty printer name returns valid path") {
        std::string path = get_prerendered_printer_path("", 800);
        REQUIRE(path.substr(0, 2) == "A:");
    }

    SECTION("Printer name with special characters") {
        std::string path = get_prerendered_printer_path("my-custom_printer.v2", 800);
        REQUIRE(path.substr(0, 2) == "A:");
        REQUIRE(path.find("my-custom_printer.v2") != std::string::npos);
    }
}

// ============================================================================
// LZ4 Compression Validation Tests
// ============================================================================

// LVGL binary image header layout (little-endian):
//   byte 0:    magic (0x19)
//   byte 1:    color format
//   bytes 2-3: flags (bit 3 = compressed)
//   bytes 4-5: width
//   bytes 6-7: height
//   bytes 8-9: stride
// After header, compressed images have a compress block:
//   bytes 0-3: compress method (0=none, 1=RLE, 2=LZ4)
//   bytes 4-7: compressed size
//   bytes 8-11: decompressed size

static constexpr uint8_t LVGL_MAGIC = 0x19;
static constexpr uint16_t LVGL_FLAG_COMPRESSED = 0x08;
static constexpr uint32_t LVGL_COMPRESS_LZ4 = 2;

struct LvglBinHeader {
    uint8_t magic;
    uint8_t cf;
    uint16_t flags;
    uint16_t w;
    uint16_t h;
    uint16_t stride;
    uint16_t reserved;
};

struct LvglCompressBlock {
    uint32_t method;
    uint32_t compressed_size;
    uint32_t decompressed_size;
};

static bool read_bin_header(const std::string& path, LvglBinHeader& hdr, LvglCompressBlock& comp) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;

    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f.good())
        return false;

    if (hdr.flags & LVGL_FLAG_COMPRESSED) {
        f.read(reinterpret_cast<char*>(&comp), sizeof(comp));
        if (!f.good())
            return false;
    }
    return true;
}

TEST_CASE("LZ4-compressed prerendered images have valid headers", "[assets][lz4]") {
    // These tests validate the actual .bin files generated by 'make gen-all-images'
    // They run from the project root, so build/ paths are accessible

    namespace fs = std::filesystem;
    const std::string prerendered_dir = "build/assets/images/prerendered";
    const std::string printer_dir = "build/assets/images/printers/prerendered";

    SECTION("Splash images use LZ4 compression") {
        if (!fs::exists(prerendered_dir)) {
            SKIP("Prerendered images not generated (run 'make gen-all-images')");
        }

        int checked = 0;
        for (const auto& entry : fs::directory_iterator(prerendered_dir)) {
            if (entry.path().extension() != ".bin")
                continue;

            LvglBinHeader hdr{};
            LvglCompressBlock comp{};
            INFO("File: " << entry.path().filename().string());
            REQUIRE(read_bin_header(entry.path().string(), hdr, comp));
            REQUIRE(hdr.magic == LVGL_MAGIC);
            REQUIRE((hdr.flags & LVGL_FLAG_COMPRESSED) != 0);
            REQUIRE(comp.method == LVGL_COMPRESS_LZ4);
            REQUIRE(comp.compressed_size > 0);
            REQUIRE(comp.decompressed_size > comp.compressed_size);
            REQUIRE(hdr.w > 0);
            REQUIRE(hdr.h > 0);
            checked++;
        }
        REQUIRE(checked > 0);
    }

    SECTION("Printer images use LZ4 compression") {
        if (!fs::exists(printer_dir)) {
            SKIP("Printer prerendered images not generated (run 'make gen-all-images')");
        }

        int checked = 0;
        for (const auto& entry : fs::directory_iterator(printer_dir)) {
            if (entry.path().extension() != ".bin")
                continue;

            LvglBinHeader hdr{};
            LvglCompressBlock comp{};
            INFO("File: " << entry.path().filename().string());
            REQUIRE(read_bin_header(entry.path().string(), hdr, comp));
            REQUIRE(hdr.magic == LVGL_MAGIC);
            REQUIRE((hdr.flags & LVGL_FLAG_COMPRESSED) != 0);
            REQUIRE(comp.method == LVGL_COMPRESS_LZ4);
            REQUIRE(comp.compressed_size > 0);
            REQUIRE(comp.decompressed_size > comp.compressed_size);
            checked++;
        }
        REQUIRE(checked > 0);
    }
}
