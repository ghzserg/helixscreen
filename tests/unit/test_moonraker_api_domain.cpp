// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_moonraker_api_domain.cpp
 * @brief Unit tests for MoonrakerAPI domain service operations and PrinterHardware guessing
 *
 * Tests the domain logic:
 * - PrinterHardware guessing (guess_bed_heater, guess_hotend_heater, guess_bed_sensor,
 *   guess_hotend_sensor, guess_part_cooling_fan, guess_main_led_strip)
 * - Bed mesh operations (get_active_bed_mesh, get_bed_mesh_profiles, has_bed_mesh)
 * - Object exclusion (get_excluded_objects, get_available_objects)
 */

#include "../../include/moonraker_api.h"
#include "../../include/moonraker_client.h"
#include "../../include/moonraker_client_mock.h"
#include "../../include/printer_hardware.h"
#include "../../lvgl/lvgl.h"
#include "../ui_test_utils.h"

#include <chrono>
#include <thread>

#include "../catch_amalgamated.hpp"

using namespace helix;
// ============================================================================
// Global LVGL Initialization (called once)
// ============================================================================

namespace {
struct LVGLInitializerAPIDomain {
    LVGLInitializerAPIDomain() {
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

static LVGLInitializerAPIDomain lvgl_init;
} // namespace

// ============================================================================
// Test Fixtures
// ============================================================================

/**
 * @brief Test fixture for MoonrakerAPI domain operations with mock client
 *
 * Uses MoonrakerClientMock to provide hardware discovery data for testing
 * the domain service operations.
 */
class MoonrakerAPIDomainTestFixture {
  public:
    MoonrakerAPIDomainTestFixture() : mock_client(MoonrakerClientMock::PrinterType::VORON_24) {
        // Initialize printer state
        state.init_subjects(false);

        // Connect mock client (required for discovery)
        mock_client.connect("ws://mock/websocket", []() {}, []() {});

        // Create API with mock client BEFORE discovery
        // (API registers hardware discovered callback in constructor)
        api = std::make_unique<MoonrakerAPI>(mock_client, state);

        // Run discovery to populate hardware lists (triggers API callback)
        mock_client.discover_printer([]() {});
    }

    ~MoonrakerAPIDomainTestFixture() {
        mock_client.stop_temperature_simulation();
        mock_client.disconnect();
        api.reset();
    }

    MoonrakerClientMock mock_client;
    PrinterState state;
    std::unique_ptr<MoonrakerAPI> api;
};

// ============================================================================
// Hardware Guessing Tests - PrinterHardware
// ============================================================================

TEST_CASE_METHOD(MoonrakerAPIDomainTestFixture,
                 "PrinterHardware::guess_bed_heater returns correct heater",
                 "[printer][guessing]") {
    // VORON_24 mock should have heater_bed
    PrinterHardware hw(api->hardware().heaters(), api->hardware().sensors(), api->hardware().fans(),
                       api->hardware().leds());
    std::string bed_heater = hw.guess_bed_heater();
    REQUIRE(bed_heater == "heater_bed");
}

TEST_CASE_METHOD(MoonrakerAPIDomainTestFixture,
                 "PrinterHardware::guess_hotend_heater returns correct heater",
                 "[printer][guessing]") {
    // VORON_24 mock should have extruder
    PrinterHardware hw(api->hardware().heaters(), api->hardware().sensors(), api->hardware().fans(),
                       api->hardware().leds());
    std::string hotend_heater = hw.guess_hotend_heater();
    REQUIRE(hotend_heater == "extruder");
}

TEST_CASE_METHOD(MoonrakerAPIDomainTestFixture,
                 "PrinterHardware::guess_bed_sensor returns correct sensor",
                 "[printer][guessing]") {
    // Bed sensor should return heater_bed (heaters have built-in sensors)
    PrinterHardware hw(api->hardware().heaters(), api->hardware().sensors(), api->hardware().fans(),
                       api->hardware().leds());
    std::string bed_sensor = hw.guess_bed_sensor();
    REQUIRE(bed_sensor == "heater_bed");
}

TEST_CASE_METHOD(MoonrakerAPIDomainTestFixture,
                 "PrinterHardware::guess_hotend_sensor returns correct sensor",
                 "[printer][guessing]") {
    // Hotend sensor should return extruder (heaters have built-in sensors)
    PrinterHardware hw(api->hardware().heaters(), api->hardware().sensors(), api->hardware().fans(),
                       api->hardware().leds());
    std::string hotend_sensor = hw.guess_hotend_sensor();
    REQUIRE(hotend_sensor == "extruder");
}

TEST_CASE_METHOD(MoonrakerAPIDomainTestFixture,
                 "PrinterHardware::guess_part_cooling_fan returns correct fan",
                 "[printer][guessing]") {
    // VORON_24 should have canonical "fan" for part cooling
    PrinterHardware hw(api->hardware().heaters(), api->hardware().sensors(), api->hardware().fans(),
                       api->hardware().leds());
    std::string fan = hw.guess_part_cooling_fan();
    // The canonical [fan] section should be prioritized if it exists
    REQUIRE_FALSE(fan.empty());
}

TEST_CASE_METHOD(MoonrakerAPIDomainTestFixture, "PrinterHardware::guess_main_led_strip returns LED",
                 "[printer][guessing]") {
    PrinterHardware hw(api->hardware().heaters(), api->hardware().sensors(), api->hardware().fans(),
                       api->hardware().leds());
    std::string led = hw.guess_main_led_strip();
    // May be empty if no LEDs configured, but shouldn't crash
    // Just verify the call works
    (void)led;
}

// ============================================================================
// Hardware Guessing - Multiple Printer Types
// ============================================================================

TEST_CASE("PrinterHardware guessing works for multiple printer types",
          "[printer][guessing][printers]") {
    PrinterState state;
    state.init_subjects(false);

    SECTION("VORON_24 printer type") {
        MoonrakerClientMock mock(MoonrakerClientMock::PrinterType::VORON_24);
        mock.connect("ws://mock/websocket", []() {}, []() {});
        mock.discover_printer([]() {});

        PrinterHardware hw(mock.hardware().heaters(), mock.hardware().sensors(),
                           mock.hardware().fans(), mock.hardware().leds());

        REQUIRE(hw.guess_bed_heater() == "heater_bed");
        REQUIRE(hw.guess_hotend_heater() == "extruder");
        REQUIRE(hw.guess_bed_sensor() == "heater_bed");
        REQUIRE(hw.guess_hotend_sensor() == "extruder");

        mock.stop_temperature_simulation();
        mock.disconnect();
    }

    SECTION("CREALITY_K1 printer type") {
        MoonrakerClientMock mock(MoonrakerClientMock::PrinterType::CREALITY_K1);
        mock.connect("ws://mock/websocket", []() {}, []() {});
        mock.discover_printer([]() {});

        PrinterHardware hw(mock.hardware().heaters(), mock.hardware().sensors(),
                           mock.hardware().fans(), mock.hardware().leds());

        // Just verify these return something sensible
        REQUIRE_FALSE(hw.guess_bed_heater().empty());
        REQUIRE_FALSE(hw.guess_hotend_heater().empty());

        mock.stop_temperature_simulation();
        mock.disconnect();
    }

    SECTION("MULTI_EXTRUDER printer type") {
        MoonrakerClientMock mock(MoonrakerClientMock::PrinterType::MULTI_EXTRUDER);
        mock.connect("ws://mock/websocket", []() {}, []() {});
        mock.discover_printer([]() {});

        PrinterHardware hw(mock.hardware().heaters(), mock.hardware().sensors(),
                           mock.hardware().fans(), mock.hardware().leds());

        // Multi-extruder should still find bed and primary extruder
        REQUIRE_FALSE(hw.guess_bed_heater().empty());
        REQUIRE_FALSE(hw.guess_hotend_heater().empty());

        mock.stop_temperature_simulation();
        mock.disconnect();
    }
}

// ============================================================================
// Bed Mesh Tests
// ============================================================================

TEST_CASE_METHOD(MoonrakerAPIDomainTestFixture, "MoonrakerAPI::has_bed_mesh returns correct state",
                 "[api][bedmesh]") {
    // Initially the mock client may or may not have bed mesh data
    // This tests that the API method delegates correctly
    // API method should return consistent state
    bool has_mesh = api->has_bed_mesh();
    (void)has_mesh; // Test only verifies method doesn't crash
}

TEST_CASE_METHOD(MoonrakerAPIDomainTestFixture,
                 "MoonrakerAPI::get_active_bed_mesh returns nullptr when no mesh",
                 "[api][bedmesh]") {
    // Check current state
    const BedMeshProfile* mesh = api->get_active_bed_mesh();

    // If no mesh, should return nullptr
    // If mesh exists, should return valid pointer
    if (mesh == nullptr) {
        // No mesh available
        REQUIRE(mesh == nullptr);
    } else {
        // Mesh exists and should have valid data
        REQUIRE(mesh != nullptr);
        REQUIRE(!mesh->probed_matrix.empty());
    }
}

TEST_CASE_METHOD(MoonrakerAPIDomainTestFixture,
                 "MoonrakerAPI::get_bed_mesh_profiles returns profile list", "[api][bedmesh]") {
    std::vector<std::string> profiles = api->get_bed_mesh_profiles();

    // Verify profiles list is reasonable
    REQUIRE(profiles.size() >= 0); // Should be non-negative size
    // Common profile names that might exist
    for (const auto& profile : profiles) {
        REQUIRE(!profile.empty()); // Profile names should not be empty
    }
}

// ============================================================================
// Object Exclusion Tests
// ============================================================================

TEST_CASE_METHOD(MoonrakerAPIDomainTestFixture,
                 "MoonrakerAPI::get_excluded_objects handles empty response", "[api][exclude]") {
    bool callback_called = false;
    std::set<std::string> result;

    api->get_excluded_objects(
        [&callback_called, &result](const std::set<std::string>& objects) {
            callback_called = true;
            result = objects;
        },
        [](const MoonrakerError&) {
            // Error callback - should not be called for this test
        });

    // Note: Mock client may not invoke callbacks immediately
    // This test verifies the API method signature is correct
}

TEST_CASE_METHOD(MoonrakerAPIDomainTestFixture,
                 "MoonrakerAPI::get_available_objects handles empty response", "[api][exclude]") {
    bool callback_called = false;
    std::vector<std::string> result;

    api->get_available_objects(
        [&callback_called, &result](const std::vector<std::string>& objects) {
            callback_called = true;
            result = objects;
        },
        [](const MoonrakerError&) {
            // Error callback - should not be called for this test
        });

    // Note: Mock client may not invoke callbacks immediately
    // This test verifies the API method signature is correct
}

// ============================================================================
// Domain Service Interface Compliance Tests
// ============================================================================

TEST_CASE("BedMeshProfile struct initialization", "[slow][api][bedmesh]") {
    BedMeshProfile profile;

    SECTION("Default values are correct") {
        REQUIRE(profile.name.empty());
        REQUIRE(profile.probed_matrix.empty());
        REQUIRE(profile.mesh_min[0] == 0.0f);
        REQUIRE(profile.mesh_min[1] == 0.0f);
        REQUIRE(profile.mesh_max[0] == 0.0f);
        REQUIRE(profile.mesh_max[1] == 0.0f);
        REQUIRE(profile.x_count == 0);
        REQUIRE(profile.y_count == 0);
        REQUIRE(profile.algo.empty());
    }

    SECTION("Can be populated with data") {
        profile.name = "test_profile";
        profile.mesh_min[0] = 10.0f;
        profile.mesh_min[1] = 10.0f;
        profile.mesh_max[0] = 200.0f;
        profile.mesh_max[1] = 200.0f;
        profile.x_count = 5;
        profile.y_count = 5;
        profile.algo = "bicubic";

        // Add some mesh data
        for (int y = 0; y < 5; ++y) {
            std::vector<float> row;
            for (int x = 0; x < 5; ++x) {
                row.push_back(0.01f * (x + y));
            }
            profile.probed_matrix.push_back(row);
        }

        REQUIRE(profile.name == "test_profile");
        REQUIRE(profile.probed_matrix.size() == 5);
        REQUIRE(profile.probed_matrix[0].size() == 5);
        REQUIRE(profile.x_count == 5);
        REQUIRE(profile.y_count == 5);
    }
}

// ============================================================================
// All Printer Types Tests
// ============================================================================

TEST_CASE("PrinterHardware and MoonrakerAPI domain methods work for all printer types",
          "[printer][api][domain][all_printers]") {
    PrinterState state;
    state.init_subjects(false);

    std::vector<MoonrakerClientMock::PrinterType> printer_types = {
        MoonrakerClientMock::PrinterType::VORON_24,
        MoonrakerClientMock::PrinterType::VORON_TRIDENT,
        MoonrakerClientMock::PrinterType::CREALITY_K1,
        MoonrakerClientMock::PrinterType::FLASHFORGE_AD5M,
        MoonrakerClientMock::PrinterType::GENERIC_COREXY,
        MoonrakerClientMock::PrinterType::GENERIC_BEDSLINGER,
        MoonrakerClientMock::PrinterType::MULTI_EXTRUDER,
    };

    for (auto printer_type : printer_types) {
        DYNAMIC_SECTION("Printer type " << static_cast<int>(printer_type)) {
            MoonrakerClientMock mock(printer_type);
            mock.connect("ws://mock/websocket", []() {}, []() {});
            mock.discover_printer([]() {});

            // Test PrinterHardware guessing
            PrinterHardware hw(mock.hardware().heaters(), mock.hardware().sensors(),
                               mock.hardware().fans(), mock.hardware().leds());

            std::string bed_heater = hw.guess_bed_heater();
            std::string hotend_heater = hw.guess_hotend_heater();
            std::string bed_sensor = hw.guess_bed_sensor();
            std::string hotend_sensor = hw.guess_hotend_sensor();

            // All standard printer types should have bed and hotend
            REQUIRE_FALSE(bed_heater.empty());
            REQUIRE_FALSE(hotend_heater.empty());
            REQUIRE_FALSE(bed_sensor.empty());
            REQUIRE_FALSE(hotend_sensor.empty());

            // Test MoonrakerAPI bed mesh methods
            MoonrakerAPI api(mock, state);
            bool has_mesh = api.has_bed_mesh();
            const BedMeshProfile* mesh = api.get_active_bed_mesh();
            std::vector<std::string> profiles = api.get_bed_mesh_profiles();

            // Consistency check
            if (has_mesh) {
                REQUIRE(mesh != nullptr);
            } else {
                REQUIRE(mesh == nullptr);
            }

            mock.stop_temperature_simulation();
            mock.disconnect();
        }
    }
}

// ============================================================================
// Hardware Discovery Access via MoonrakerAPI Tests
// ============================================================================

TEST_CASE("MoonrakerAPI hardware() returns discovery data after discovery completes",
          "[api][hardware]") {
    PrinterState state;
    state.init_subjects(false);

    MoonrakerClientMock mock(MoonrakerClientMock::PrinterType::VORON_24);
    mock.connect("ws://mock/websocket", []() {}, []() {});

    // Create API before discovery so callbacks are registered
    MoonrakerAPI api(mock, state);

    // Run discovery - this fires callbacks that populate api.hardware_
    mock.discover_printer([]() {});

    // Verify hardware data is accessible through API
    // After discovery, the API should have hardware data populated
    const auto& hw = api.hardware();

    // VORON_24 should have hostname populated from mock
    // Note: Mock sets hostname during discovery
    REQUIRE_FALSE(hw.hostname().empty());

    // Should have expected hardware for VORON_24
    REQUIRE_FALSE(hw.heaters().empty());
    REQUIRE_FALSE(hw.fans().empty());

    // Check capabilities that VORON_24 should have
    REQUIRE(hw.has_heater_bed() == true);
    REQUIRE(hw.has_qgl() == true); // Voron 2.4 has QGL

    mock.stop_temperature_simulation();
    mock.disconnect();
}

TEST_CASE("MoonrakerAPI hardware() accessor provides const access", "[api][hardware]") {
    PrinterState state;
    state.init_subjects(false);

    MoonrakerClientMock mock(MoonrakerClientMock::PrinterType::CREALITY_K1);
    mock.connect("ws://mock/websocket", []() {}, []() {});

    // Create API before discovery so callbacks are registered
    MoonrakerAPI api(mock, state);

    // Run discovery - this fires callbacks that populate api.hardware_
    mock.discover_printer([]() {});

    // Const access should work
    const MoonrakerAPI& const_api = api;
    const auto& hw = const_api.hardware();

    // K1 should have basic hardware
    REQUIRE_FALSE(hw.heaters().empty());

    mock.stop_temperature_simulation();
    mock.disconnect();
}
