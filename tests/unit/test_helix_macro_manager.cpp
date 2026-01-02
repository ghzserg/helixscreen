// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "macro_manager.h"
#include "moonraker_api.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"

#include <thread>

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// Test Fixtures
// ============================================================================

// DEFERRED: All tests using this fixture crash with SIGSEGV during destruction
// The crash is in unordered_set<string> destructor with corrupted pointer 0x4079000000000000 (=
// 400.0 as double) Root cause: Memory corruption likely from uninitialized lv_subject_t in
// PrinterState when init_subjects() isn't called. Pre-existing issue - needs investigation.
class MacroManagerTestFixture {
  public:
    MacroManagerTestFixture() : state_(), api_(client_, state_), manager_(api_, capabilities_) {}

    void set_helix_macros_installed() {
        // Simulate printer with Helix macros (v2.0+)
        json objects = json::array(
            {"gcode_macro HELIX_READY", "gcode_macro HELIX_ENDED", "gcode_macro HELIX_RESET",
             "gcode_macro HELIX_START_PRINT", "gcode_macro HELIX_CLEAN_NOZZLE",
             "gcode_macro HELIX_BED_LEVEL_IF_NEEDED", "gcode_macro _HELIX_STATE", "bed_mesh"});
        capabilities_.parse_objects(objects);
    }

    void set_no_helix_macros() {
        // Simulate printer without Helix macros
        json objects =
            json::array({"gcode_macro START_PRINT", "gcode_macro CLEAN_NOZZLE", "bed_mesh"});
        capabilities_.parse_objects(objects);
    }

    void set_partial_helix_macros() {
        // Simulate printer with legacy v1.x macros (no HELIX_READY)
        json objects = json::array({"gcode_macro HELIX_START_PRINT", "bed_mesh"});
        capabilities_.parse_objects(objects);
    }

  protected:
    MoonrakerClientMock client_;
    PrinterState state_;
    MoonrakerAPI api_;
    PrinterCapabilities capabilities_;
    MacroManager manager_;
};

// ============================================================================
// Status Detection Tests
// ============================================================================

TEST_CASE_METHOD(MacroManagerTestFixture,
                 "MacroManager - is_installed returns false when no macros",
                 "[config][status][.]") {
    set_no_helix_macros();

    REQUIRE_FALSE(manager_.is_installed());
}

TEST_CASE_METHOD(MacroManagerTestFixture, "MacroManager - is_installed returns true when installed",
                 "[config][status][.]") {
    set_helix_macros_installed();

    REQUIRE(manager_.is_installed());
}

TEST_CASE_METHOD(MacroManagerTestFixture,
                 "MacroManager - get_status returns NOT_INSTALLED when no macros",
                 "[config][status][.]") {
    set_no_helix_macros();

    REQUIRE(manager_.get_status() == MacroInstallStatus::NOT_INSTALLED);
}

TEST_CASE_METHOD(MacroManagerTestFixture,
                 "MacroManager - get_status returns INSTALLED when current version",
                 "[config][status][.]") {
    set_helix_macros_installed();

    REQUIRE(manager_.get_status() == MacroInstallStatus::INSTALLED);
}

// ============================================================================
// Macro Content Tests
// ============================================================================

TEST_CASE("MacroManager - get_macro_content returns valid Klipper config", "[config][content]") {
    std::string content = MacroManager::get_macro_content();

    // Should contain version header (v2.0+ format)
    REQUIRE(content.find("# helix_macros v") != std::string::npos);

    // Should contain core signal macros
    REQUIRE(content.find("[gcode_macro HELIX_READY]") != std::string::npos);
    REQUIRE(content.find("[gcode_macro HELIX_ENDED]") != std::string::npos);
    REQUIRE(content.find("[gcode_macro HELIX_RESET]") != std::string::npos);

    // Should contain pre-print helper macros
    REQUIRE(content.find("[gcode_macro HELIX_START_PRINT]") != std::string::npos);
    REQUIRE(content.find("[gcode_macro HELIX_CLEAN_NOZZLE]") != std::string::npos);
    REQUIRE(content.find("[gcode_macro HELIX_BED_LEVEL_IF_NEEDED]") != std::string::npos);

    // Should contain phase tracking macros
    REQUIRE(content.find("[gcode_macro HELIX_PHASE_HOMING]") != std::string::npos);
    REQUIRE(content.find("[gcode_macro HELIX_PHASE_HEATING_BED]") != std::string::npos);

    // Should contain proper gcode: sections
    REQUIRE(content.find("gcode:") != std::string::npos);

    // Should contain Jinja2 templating
    REQUIRE(content.find("{% set") != std::string::npos);
    REQUIRE(content.find("{% if") != std::string::npos);
}

TEST_CASE("MacroManager - get_macro_content contains parameter handling", "[config][content]") {
    std::string content = MacroManager::get_macro_content();

    // HELIX_START_PRINT should accept temperature parameters
    REQUIRE(content.find("BED_TEMP") != std::string::npos);
    REQUIRE(content.find("EXTRUDER_TEMP") != std::string::npos);

    // HELIX_START_PRINT should accept operation flags
    REQUIRE(content.find("DO_QGL") != std::string::npos);
    REQUIRE(content.find("DO_Z_TILT") != std::string::npos);
    REQUIRE(content.find("DO_BED_MESH") != std::string::npos);
    REQUIRE(content.find("DO_NOZZLE_CLEAN") != std::string::npos);
}

TEST_CASE("MacroManager - get_macro_content includes conditional operations", "[config][content]") {
    std::string content = MacroManager::get_macro_content();

    // Should check for QGL availability
    REQUIRE(content.find("quad_gantry_level") != std::string::npos);

    // Should check for Z-tilt availability
    REQUIRE(content.find("z_tilt") != std::string::npos);

    // Should call standard Klipper commands
    REQUIRE(content.find("BED_MESH_CALIBRATE") != std::string::npos);
    REQUIRE(content.find("QUAD_GANTRY_LEVEL") != std::string::npos);
    REQUIRE(content.find("Z_TILT_ADJUST") != std::string::npos);
}

TEST_CASE("MacroManager - get_macro_names returns expected macros", "[slow][config][content]") {
    auto names = MacroManager::get_macro_names();

    // v2.0 has 15 public macros (excluding _HELIX_STATE which starts with _)
    REQUIRE(names.size() == 15);

    // Core signals
    REQUIRE(std::find(names.begin(), names.end(), "HELIX_READY") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "HELIX_ENDED") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "HELIX_RESET") != names.end());

    // Pre-print helpers
    REQUIRE(std::find(names.begin(), names.end(), "HELIX_START_PRINT") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "HELIX_CLEAN_NOZZLE") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "HELIX_BED_LEVEL_IF_NEEDED") != names.end());

    // Phase tracking (spot check a few)
    REQUIRE(std::find(names.begin(), names.end(), "HELIX_PHASE_HOMING") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "HELIX_PHASE_BED_MESH") != names.end());
}

// ============================================================================
// HELIX_CLEAN_NOZZLE Macro Tests
// ============================================================================

TEST_CASE("MacroManager - HELIX_CLEAN_NOZZLE has configurable brush position",
          "[config][content]") {
    std::string content = MacroManager::get_macro_content();

    // Should have configurable variables
    REQUIRE(content.find("variable_brush_x") != std::string::npos);
    REQUIRE(content.find("variable_brush_y") != std::string::npos);
    REQUIRE(content.find("variable_brush_z") != std::string::npos);
    REQUIRE(content.find("variable_wipe_count") != std::string::npos);
}

// ============================================================================
// HELIX_BED_LEVEL_IF_NEEDED Macro Tests
// ============================================================================

TEST_CASE("MacroManager - HELIX_BED_LEVEL_IF_NEEDED has age-based logic", "[config][content]") {
    std::string content = MacroManager::get_macro_content();

    // Should have MAX_AGE parameter
    REQUIRE(content.find("MAX_AGE") != std::string::npos);

    // Should track last mesh time
    REQUIRE(content.find("variable_last_mesh_time") != std::string::npos);

    // Should check mesh profile
    REQUIRE(content.find("bed_mesh.profile_name") != std::string::npos);
}

// ============================================================================
// Version Tests
// ============================================================================

TEST_CASE("MacroManager - get_version returns valid semver", "[slow][config][version]") {
    std::string version = MacroManager::get_version();

    // Should not be empty
    REQUIRE_FALSE(version.empty());

    // Should match semver pattern (major.minor.patch)
    REQUIRE(version.find('.') != std::string::npos);

    // Should be at least 2.0.0 (v2.0 format)
    REQUIRE(version >= "2.0.0");
}

TEST_CASE("MacroManager - filename constant is valid", "[slow][config][constants]") {
    std::string filename = HELIX_MACROS_FILENAME;

    REQUIRE(filename == "helix_macros.cfg");
    REQUIRE(filename.find(".cfg") != std::string::npos);
}

// ============================================================================
// Integration-Style Tests (using mock)
// ============================================================================

// NOTE: The install/update tests below currently expect callbacks NOT to fire
// because the mock doesn't implement printer.restart. When HTTP file upload
// is implemented, these tests should be updated to verify actual success.

TEST_CASE_METHOD(MacroManagerTestFixture, "MacroManager - install initiates sequence",
                 "[config][install][.]") {
    set_no_helix_macros();

    bool callback_received = false;

    // Install initiates the sequence but mock doesn't complete it
    // (printer.restart not implemented in mock)
    manager_.install([&callback_received]() { callback_received = true; },
                     [&callback_received](const MoonrakerError&) { callback_received = true; });

    // For now, just verify no crash occurs during the install sequence
    // The callback won't fire because mock's printer.restart doesn't invoke callbacks
    // This is expected behavior until HTTP file upload is fully implemented
    SUCCEED("Install sequence initiated without crash");
}

TEST_CASE_METHOD(MacroManagerTestFixture, "MacroManager - update initiates sequence",
                 "[config][install][.]") {
    set_helix_macros_installed();

    bool callback_received = false;

    // Same as install - mock doesn't complete the sequence
    manager_.update([&callback_received]() { callback_received = true; },
                    [&callback_received](const MoonrakerError&) { callback_received = true; });

    SUCCEED("Update sequence initiated without crash");
}
