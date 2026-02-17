// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/moonraker_api.h"
#include "../../include/moonraker_client.h"
#include "../../include/printer_state.h"
#include "../../lvgl/lvgl.h"
#include "../ui_test_utils.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "../catch_amalgamated.hpp"

using namespace helix;
// ============================================================================
// Global LVGL Initialization
// ============================================================================

struct LVGLInitializerHelixPrint {
    LVGLInitializerHelixPrint() {
        static bool initialized = false;
        if (!initialized) {
            lv_init_safe();
            lv_display_t* disp = lv_display_create(800, 480);
            alignas(64) static lv_color_t buf[800 * 10];
            lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
            initialized = true;
        }
    }
};

static LVGLInitializerHelixPrint lvgl_init_helix;

// ============================================================================
// Test Fixtures
// ============================================================================

class HelixPrintAPITestFixture {
  public:
    HelixPrintAPITestFixture() {
        state.init_subjects(false);
        client = std::make_unique<MoonrakerClient>();
        api = std::make_unique<MoonrakerAPI>(*client, state);
        reset_callbacks();
    }

    void reset_callbacks() {
        success_called = false;
        error_called = false;
        bool_result = false;
        error_message.clear();
        modified_print_result = {};
    }

    PrinterState state;
    std::unique_ptr<MoonrakerClient> client;
    std::unique_ptr<MoonrakerAPI> api;

    // Callback tracking
    std::atomic<bool> success_called{false};
    std::atomic<bool> error_called{false};
    std::atomic<bool> bool_result{false};
    std::string error_message;
    ModifiedPrintResult modified_print_result;
};

// ============================================================================
// Plugin Detection Tests
// ============================================================================

TEST_CASE_METHOD(HelixPrintAPITestFixture, "HelixPrint API - has_helix_plugin initial state",
                 "[print][api]") {
    // Initially, plugin should not be detected (no check performed yet)
    REQUIRE(state.service_has_helix_plugin() == false);
}

TEST_CASE_METHOD(HelixPrintAPITestFixture,
                 "HelixPrint API - check_helix_plugin with disconnected client", "[print][api]") {
    // With disconnected client, check should report plugin unavailable
    // (error callback path returns false, not error)

    api->check_helix_plugin(
        [this](bool available) {
            bool_result = available;
            success_called = true;
        },
        [this](const MoonrakerError& err) {
            error_message = err.message;
            error_called = true;
        });

    // Give async operation time to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Should complete (either success with false, or trigger error->false path)
    // The implementation treats errors as "plugin not available"
    // So either way, service_has_helix_plugin should be false
    REQUIRE(state.service_has_helix_plugin() == false);
}

// ============================================================================
// Modified Print API Validation Tests (v2.0 - Path-Based)
// ============================================================================

TEST_CASE_METHOD(HelixPrintAPITestFixture,
                 "HelixPrint API - start_modified_print validates original filename",
                 "[print][api][security]") {
    SECTION("Rejects path traversal in original filename") {
        api->start_modified_print(
            "../../../etc/passwd",       // Malicious original path
            ".helix_temp/mod_123.gcode", // Valid temp path
            {"test_mod"}, [this](const ModifiedPrintResult&) { success_called = true; },
            [this](const MoonrakerError& err) {
                error_message = err.message;
                error_called = true;
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        REQUIRE(error_called == true);
        REQUIRE(success_called == false);
        REQUIRE(error_message.find("directory traversal") != std::string::npos);
    }

    SECTION("Rejects filename with newlines") {
        api->start_modified_print(
            "test\nfile.gcode",          // Newline injection
            ".helix_temp/mod_123.gcode", // Valid temp path
            {"test_mod"}, [this](const ModifiedPrintResult&) { success_called = true; },
            [this](const MoonrakerError& err) {
                error_message = err.message;
                error_called = true;
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        REQUIRE(error_called == true);
        REQUIRE(success_called == false);
    }

    SECTION("Accepts valid filename") {
        // This will fail due to disconnected client, but should pass validation
        api->start_modified_print(
            "benchy.gcode", ".helix_temp/mod_benchy.gcode", {"bed_leveling_disabled"},
            [this](const ModifiedPrintResult& result) {
                modified_print_result = result;
                success_called = true;
            },
            [this](const MoonrakerError& err) {
                error_message = err.message;
                error_called = true;
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Should fail due to disconnected client, not validation
        // Error message should NOT mention "directory traversal" or "illegal characters"
        if (error_called) {
            REQUIRE(error_message.find("directory traversal") == std::string::npos);
            REQUIRE(error_message.find("illegal characters") == std::string::npos);
        }
    }

    SECTION("Accepts filename with subdirectory") {
        api->start_modified_print(
            "prints/2024/benchy.gcode", // Valid subdirectory path
            ".helix_temp/mod_benchy.gcode", {"test_mod"},
            [this](const ModifiedPrintResult&) { success_called = true; },
            [this](const MoonrakerError& err) {
                error_message = err.message;
                error_called = true;
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Should NOT fail validation (may fail due to network)
        if (error_called) {
            REQUIRE(error_message.find("directory traversal") == std::string::npos);
        }
    }
}

TEST_CASE_METHOD(HelixPrintAPITestFixture,
                 "HelixPrint API - start_modified_print validates temp file path",
                 "[print][api][security]") {
    SECTION("Rejects path traversal in temp path") {
        api->start_modified_print(
            "benchy.gcode",        // Valid original
            "../../../etc/passwd", // Malicious temp path
            {"test_mod"}, [this](const ModifiedPrintResult&) { success_called = true; },
            [this](const MoonrakerError& err) {
                error_message = err.message;
                error_called = true;
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        REQUIRE(error_called == true);
        REQUIRE(success_called == false);
        REQUIRE(error_message.find("directory traversal") != std::string::npos);
    }

    SECTION("Rejects temp path with newlines") {
        api->start_modified_print(
            "benchy.gcode",
            ".helix_temp/mod\n123.gcode", // Newline injection
            {"test_mod"}, [this](const ModifiedPrintResult&) { success_called = true; },
            [this](const MoonrakerError& err) {
                error_message = err.message;
                error_called = true;
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        REQUIRE(error_called == true);
        REQUIRE(success_called == false);
    }
}

// ============================================================================
// ModifiedPrintResult Structure Tests
// ============================================================================

TEST_CASE("HelixPrint API - ModifiedPrintResult structure", "[slow][print][api]") {
    ModifiedPrintResult result;

    SECTION("Default values are empty") {
        REQUIRE(result.original_filename.empty());
        REQUIRE(result.print_filename.empty());
        REQUIRE(result.temp_filename.empty());
        REQUIRE(result.status.empty());
    }

    SECTION("Can be populated") {
        result.original_filename = "benchy.gcode";
        result.print_filename = ".helix_print/benchy.gcode";
        result.temp_filename = ".helix_temp/mod_123_benchy.gcode";
        result.status = "printing";

        REQUIRE(result.original_filename == "benchy.gcode");
        REQUIRE(result.print_filename == ".helix_print/benchy.gcode");
        REQUIRE(result.temp_filename == ".helix_temp/mod_123_benchy.gcode");
        REQUIRE(result.status == "printing");
    }
}

// ============================================================================
// Modification List Tests
// ============================================================================

TEST_CASE_METHOD(HelixPrintAPITestFixture, "HelixPrint API - handles empty modifications list",
                 "[print][api]") {
    // Empty modifications list should be valid
    api->start_modified_print(
        "benchy.gcode", ".helix_temp/mod_benchy.gcode", {}, // Empty modifications
        [this](const ModifiedPrintResult&) { success_called = true; },
        [this](const MoonrakerError& err) {
            error_message = err.message;
            error_called = true;
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Should not fail validation due to empty modifications
    if (error_called) {
        REQUIRE(error_message.find("modifications") == std::string::npos);
    }
}

TEST_CASE_METHOD(HelixPrintAPITestFixture, "HelixPrint API - handles multiple modifications",
                 "[print][api]") {
    std::vector<std::string> mods = {"bed_leveling_disabled", "z_tilt_disabled", "qgl_disabled",
                                     "nozzle_clean_disabled"};

    api->start_modified_print(
        "benchy.gcode", ".helix_temp/mod_benchy.gcode", mods,
        [this](const ModifiedPrintResult&) { success_called = true; },
        [this](const MoonrakerError& err) {
            error_message = err.message;
            error_called = true;
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Should not fail validation
    if (error_called) {
        REQUIRE(error_message.find("directory traversal") == std::string::npos);
    }
}

// ============================================================================
// Path Format Tests (v2.0 API)
// ============================================================================

TEST_CASE_METHOD(HelixPrintAPITestFixture, "HelixPrint API - accepts various valid temp paths",
                 "[print][api]") {
    SECTION("Standard .helix_temp path") {
        api->start_modified_print(
            "print.gcode", ".helix_temp/mod_12345_print.gcode", {"test_mod"},
            [this](const ModifiedPrintResult&) { success_called = true; },
            [this](const MoonrakerError& err) {
                error_message = err.message;
                error_called = true;
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Should not fail validation (network error is expected)
        if (error_called) {
            REQUIRE(error_message.find("directory traversal") == std::string::npos);
            REQUIRE(error_message.find("temp path") == std::string::npos);
        }
    }

    SECTION("Path with special characters in filename") {
        api->start_modified_print(
            "my-print_v2.0 (final).gcode", ".helix_temp/mod_my-print_v2.0 (final).gcode",
            {"test_mod"}, [this](const ModifiedPrintResult&) { success_called = true; },
            [this](const MoonrakerError& err) {
                error_message = err.message;
                error_called = true;
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Should not fail validation
        if (error_called) {
            REQUIRE(error_message.find("directory traversal") == std::string::npos);
        }
    }
}
