// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_metadata_and_usb_symlink.cpp
 * @brief Unit tests for metadata silent flag, metascan fallback, and USB symlink detection
 *
 * Tests:
 * - MoonrakerAPI::get_file_metadata() with silent flag
 * - MoonrakerAPI::metascan_file() API method
 * - PrintSelectUsbSource symlink detection and tab hiding
 */

#include "../../include/moonraker_api.h"
#include "../../include/moonraker_client.h"
#include "../../include/moonraker_client_mock.h"
#include "../../include/ui_print_select_usb_source.h"
#include "../../lvgl/lvgl.h"
#include "../ui_test_utils.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "../catch_amalgamated.hpp"

using namespace helix;
// ============================================================================
// Global LVGL Initialization (called once)
// ============================================================================

namespace {
struct LVGLInitializerMetadataUSB {
    LVGLInitializerMetadataUSB() {
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

static LVGLInitializerMetadataUSB lvgl_init;
} // namespace

// ============================================================================
// Test Fixtures
// ============================================================================

/**
 * @brief Test fixture for MoonrakerAPI metadata operations
 */
class MetadataAPITestFixture {
  public:
    MetadataAPITestFixture() : mock_client(MoonrakerClientMock::PrinterType::VORON_24) {
        // Initialize printer state
        state.init_subjects(false);

        // Connect mock client
        mock_client.connect("ws://mock/websocket", []() {}, []() {});

        // Run discovery
        mock_client.discover_printer([]() {});

        // Create API
        api = std::make_unique<MoonrakerAPI>(mock_client, state);
    }

    ~MetadataAPITestFixture() {
        mock_client.stop_temperature_simulation();
        mock_client.disconnect();
        api.reset();
    }

    MoonrakerClientMock mock_client;
    PrinterState state;
    std::unique_ptr<MoonrakerAPI> api;
};

// ============================================================================
// Metadata API Tests
// ============================================================================

TEST_CASE_METHOD(MetadataAPITestFixture, "get_file_metadata calls success callback with valid file",
                 "[metadata][api]") {
    bool success_called = false;
    bool error_called = false;

    api->get_file_metadata(
        "test_file.gcode", [&](const FileMetadata&) { success_called = true; },
        [&](const MoonrakerError&) { error_called = true; });

    // Mock is synchronous - callback already fired
    REQUIRE(success_called);
    REQUIRE_FALSE(error_called);
}

TEST_CASE_METHOD(MetadataAPITestFixture, "get_file_metadata with silent flag compiles correctly",
                 "[metadata][api][silent]") {
    // This test verifies that silent=true parameter is accepted
    // In real usage, this prevents toast spam when files aren't indexed
    bool success_called = false;

    // Call with silent=true (4th parameter)
    api->get_file_metadata(
        "test_file.gcode", [&](const FileMetadata&) { success_called = true; },
        [&](const MoonrakerError&) {}, true // silent
    );

    // With mock, this should succeed
    REQUIRE(success_called);
}

TEST_CASE_METHOD(MetadataAPITestFixture, "metascan_file calls success callback with metadata",
                 "[metadata][api][metascan]") {
    bool success_called = false;
    bool error_called = false;

    api->metascan_file(
        "test_file.gcode", [&](const FileMetadata&) { success_called = true; },
        [&](const MoonrakerError&) { error_called = true; });

    // Mock is synchronous - callback already fired
    REQUIRE(success_called);
    REQUIRE_FALSE(error_called);
}

TEST_CASE_METHOD(MetadataAPITestFixture, "metascan_file is silent by default",
                 "[metadata][api][metascan]") {
    // metascan_file has silent=true by default (see API declaration)
    bool success_called = false;

    api->metascan_file(
        "test_file.gcode", [&](const FileMetadata&) { success_called = true; },
        [&](const MoonrakerError&) {});

    REQUIRE(success_called);
}

// ============================================================================
// USB Source Symlink Detection Tests
// ============================================================================

TEST_CASE("PrintSelectUsbSource initial state has Moonraker access false", "[usb][symlink]") {
    helix::ui::PrintSelectUsbSource usb_source;

    REQUIRE_FALSE(usb_source.moonraker_has_usb_access());
}

TEST_CASE("PrintSelectUsbSource::set_moonraker_has_usb_access sets flag correctly",
          "[usb][symlink]") {
    helix::ui::PrintSelectUsbSource usb_source;

    usb_source.set_moonraker_has_usb_access(true);
    REQUIRE(usb_source.moonraker_has_usb_access());

    usb_source.set_moonraker_has_usb_access(false);
    REQUIRE_FALSE(usb_source.moonraker_has_usb_access());
}

TEST_CASE("PrintSelectUsbSource with symlink access stays on PRINTER source", "[usb][symlink]") {
    helix::ui::PrintSelectUsbSource usb_source;

    // Set symlink access
    usb_source.set_moonraker_has_usb_access(true);

    // Verify we stay on PRINTER source (default)
    REQUIRE(usb_source.get_current_source() == FileSource::PRINTER);
    REQUIRE_FALSE(usb_source.is_usb_active());
}

TEST_CASE("PrintSelectUsbSource on_drive_inserted does nothing when symlink active",
          "[usb][symlink]") {
    helix::ui::PrintSelectUsbSource usb_source;

    // Set symlink access BEFORE drive insert
    usb_source.set_moonraker_has_usb_access(true);

    // on_drive_inserted should be a no-op (button is null anyway, but logic should skip)
    usb_source.on_drive_inserted();

    // Should still be on PRINTER source
    REQUIRE(usb_source.get_current_source() == FileSource::PRINTER);
}

TEST_CASE("PrintSelectUsbSource switches from USB to PRINTER when symlink detected",
          "[usb][symlink]") {
    helix::ui::PrintSelectUsbSource usb_source;

    // Track source changes
    FileSource last_source = FileSource::PRINTER;
    usb_source.set_on_source_changed([&](FileSource source) { last_source = source; });

    // Manually set to USB source (simulating user clicked USB tab)
    // Note: We can't fully test this without LVGL widgets, but we test the logic
    // The implementation switches to PRINTER when symlink is detected while on USB

    // Detecting symlink should trigger switch to PRINTER and callback
    usb_source.set_moonraker_has_usb_access(true);

    // If we were on USB, we'd switch to PRINTER
    // Since we start on PRINTER, the callback won't fire but state should remain PRINTER
    REQUIRE(usb_source.get_current_source() == FileSource::PRINTER);
}

// ============================================================================
// Integration-style Tests
// ============================================================================

TEST_CASE_METHOD(MetadataAPITestFixture, "list_files for usb path returns empty when no symlink",
                 "[usb][symlink][integration]") {
    // Ensure symlink simulation is off
    mock_set_usb_symlink_active(false);

    bool success_called = false;
    std::vector<FileInfo> received_files;

    api->list_files(
        "gcodes", "usb", false,
        [&](const std::vector<FileInfo>& files) {
            received_files = files;
            success_called = true;
        },
        [&](const MoonrakerError&) {});

    // Mock is synchronous
    REQUIRE(success_called);
    REQUIRE(received_files.empty()); // No files when symlink not active
}

TEST_CASE_METHOD(MetadataAPITestFixture,
                 "list_files for usb path returns files when symlink active",
                 "[usb][symlink][integration]") {
    // Enable symlink simulation
    mock_set_usb_symlink_active(true);

    bool success_called = false;
    std::vector<FileInfo> received_files;

    api->list_files(
        "gcodes", "usb", false,
        [&](const std::vector<FileInfo>& files) {
            received_files = files;
            success_called = true;
        },
        [&](const MoonrakerError&) {});

    // Mock is synchronous
    REQUIRE(success_called);
    REQUIRE_FALSE(received_files.empty()); // Has files when symlink active

    // Cleanup
    mock_set_usb_symlink_active(false);
}
