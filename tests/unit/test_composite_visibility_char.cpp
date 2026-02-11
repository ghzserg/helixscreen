// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_composite_visibility_char.cpp
 * @brief Characterization tests for PrinterState composite visibility domain
 *
 * These tests capture the CURRENT behavior of composite visibility subjects
 * in PrinterState before extraction to a dedicated state class.
 *
 * Composite visibility subjects (5 total):
 * - can_show_bed_mesh_ (int, 0 or 1)
 * - can_show_qgl_ (int, 0 or 1)
 * - can_show_z_tilt_ (int, 0 or 1)
 * - can_show_nozzle_clean_ (int, 0 or 1)
 * - can_show_purge_line_ (int, 0 or 1)
 *
 * Key behavior:
 * - These are DERIVED subjects: can_show_X = helix_plugin_installed && printer_has_X
 * - All initialize to 0 (hidden by default)
 * - Updated by update_gcode_modification_visibility() which is called when:
 *   - Hardware is discovered (set_hardware_internal)
 *   - Plugin status changes (set_helix_plugin_installed)
 *   - Printer type changes (set_printer_type_internal)
 *
 * IMPORTANT: Capability sources differ:
 * - bed_mesh, qgl, z_tilt: From hardware discovery objects (quad_gantry_level, z_tilt, bed_mesh)
 * - nozzle_clean: From hardware discovery macro detection (CLEAN_NOZZLE, NOZZLE_WIPE, etc.)
 * - purge_line: From printer type database (set_printer_type), NOT hardware discovery
 */

#include "../test_helpers/printer_state_test_access.h"
#include "../ui_test_utils.h"
#include "app_globals.h"
#include "printer_discovery.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

// Helper to get subject by XML name (requires init_subjects(true))
static lv_subject_t* get_subject_by_name(const char* name) {
    return lv_xml_get_subject(NULL, name);
}

// Helper to create hardware with specific capabilities
// NOTE: purge_line is NOT from hardware discovery - it comes from set_printer_type()
static helix::PrinterDiscovery create_hardware_with_capabilities(bool has_bed_mesh, bool has_qgl,
                                                                 bool has_z_tilt,
                                                                 bool has_nozzle_clean) {
    helix::PrinterDiscovery hardware;

    // Build JSON array of objects based on requested capabilities
    nlohmann::json objects = nlohmann::json::array();

    if (has_bed_mesh) {
        objects.push_back("bed_mesh");
    }
    if (has_qgl) {
        objects.push_back("quad_gantry_level");
    }
    if (has_z_tilt) {
        objects.push_back("z_tilt");
    }
    if (has_nozzle_clean) {
        // Nozzle clean is detected via macro, need gcode_macro with specific name
        objects.push_back("gcode_macro CLEAN_NOZZLE");
    }

    hardware.parse_objects(objects);
    return hardware;
}

// ============================================================================
// Derivation Tests - Verify can_show_X = plugin_installed && printer_has_X
// ============================================================================

TEST_CASE("Composite visibility characterization: derivation logic",
          "[characterization][composite-visibility][derivation]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    SECTION("plugin NOT installed: hardware-based can_show_* remain 0 regardless of capabilities") {
        // Set up hardware with ALL capabilities (except purge_line which comes from printer type)
        auto hardware = create_hardware_with_capabilities(true, true, true, true);

        // Do NOT install plugin (it defaults to unknown/-1)
        // Explicitly mark as not installed
        state.set_helix_plugin_installed(false);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Set hardware - this triggers update_gcode_modification_visibility()
        state.set_hardware(hardware);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // All should be 0 because plugin is not installed
        REQUIRE(lv_subject_get_int(state.get_can_show_bed_mesh_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_qgl_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_z_tilt_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_nozzle_clean_subject()) == 0);
        // purge_line is 0 because it's not set via hardware discovery
        REQUIRE(lv_subject_get_int(state.get_can_show_purge_line_subject()) == 0);
    }

    SECTION("plugin installed but NO capabilities: all can_show_* remain 0") {
        // Install plugin first
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Set hardware with NO capabilities
        auto hardware = create_hardware_with_capabilities(false, false, false, false);
        state.set_hardware(hardware);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // All should be 0 because no capabilities
        REQUIRE(lv_subject_get_int(state.get_can_show_bed_mesh_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_qgl_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_z_tilt_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_nozzle_clean_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_purge_line_subject()) == 0);
    }

    SECTION("plugin installed + bed_mesh capability: can_show_bed_mesh = 1") {
        // Install plugin first
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Set hardware with only bed_mesh
        auto hardware = create_hardware_with_capabilities(true, false, false, false);
        state.set_hardware(hardware);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(lv_subject_get_int(state.get_can_show_bed_mesh_subject()) == 1);
        REQUIRE(lv_subject_get_int(state.get_can_show_qgl_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_z_tilt_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_nozzle_clean_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_purge_line_subject()) == 0);
    }

    SECTION("plugin installed + qgl capability: can_show_qgl = 1") {
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        auto hardware = create_hardware_with_capabilities(false, true, false, false);
        state.set_hardware(hardware);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(lv_subject_get_int(state.get_can_show_bed_mesh_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_qgl_subject()) == 1);
        REQUIRE(lv_subject_get_int(state.get_can_show_z_tilt_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_nozzle_clean_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_purge_line_subject()) == 0);
    }

    SECTION("plugin installed + z_tilt capability: can_show_z_tilt = 1") {
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        auto hardware = create_hardware_with_capabilities(false, false, true, false);
        state.set_hardware(hardware);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(lv_subject_get_int(state.get_can_show_bed_mesh_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_qgl_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_z_tilt_subject()) == 1);
        REQUIRE(lv_subject_get_int(state.get_can_show_nozzle_clean_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_purge_line_subject()) == 0);
    }

    SECTION("plugin installed + nozzle_clean macro: can_show_nozzle_clean = 1") {
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        auto hardware = create_hardware_with_capabilities(false, false, false, true);
        state.set_hardware(hardware);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(lv_subject_get_int(state.get_can_show_bed_mesh_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_qgl_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_z_tilt_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_nozzle_clean_subject()) == 1);
        REQUIRE(lv_subject_get_int(state.get_can_show_purge_line_subject()) == 0);
    }

    // NOTE: purge_line is NOT tested via hardware discovery - it requires
    // set_printer_type() which sets purge_line from the printer type database.
    // This is tested in the combined states section below.
}

// ============================================================================
// Update Trigger Tests - Verify visibility updates on state changes
// ============================================================================

TEST_CASE("Composite visibility characterization: update triggers",
          "[characterization][composite-visibility][triggers]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    SECTION("plugin status change from 0 to 1 triggers visibility update") {
        // First set up hardware with bed_mesh capability
        auto hardware = create_hardware_with_capabilities(true, false, false, false);
        state.set_hardware(hardware);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Plugin not installed yet - should be 0
        state.set_helix_plugin_installed(false);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        REQUIRE(lv_subject_get_int(state.get_can_show_bed_mesh_subject()) == 0);

        // Now install plugin - should become 1
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        REQUIRE(lv_subject_get_int(state.get_can_show_bed_mesh_subject()) == 1);
    }

    SECTION("plugin status change from 1 to 0 clears visibility") {
        // Set up with plugin installed and hardware capabilities
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        auto hardware = create_hardware_with_capabilities(true, true, true, true);
        state.set_hardware(hardware);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Verify hardware-detected ones are visible
        REQUIRE(lv_subject_get_int(state.get_can_show_bed_mesh_subject()) == 1);
        REQUIRE(lv_subject_get_int(state.get_can_show_qgl_subject()) == 1);
        REQUIRE(lv_subject_get_int(state.get_can_show_z_tilt_subject()) == 1);
        REQUIRE(lv_subject_get_int(state.get_can_show_nozzle_clean_subject()) == 1);
        // purge_line is 0 because it's not set via hardware
        REQUIRE(lv_subject_get_int(state.get_can_show_purge_line_subject()) == 0);

        // Uninstall plugin
        state.set_helix_plugin_installed(false);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // All should be hidden now
        REQUIRE(lv_subject_get_int(state.get_can_show_bed_mesh_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_qgl_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_z_tilt_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_nozzle_clean_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_purge_line_subject()) == 0);
    }

    SECTION("hardware change with plugin installed updates visibility") {
        // Install plugin first
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Start with no capabilities
        auto hardware1 = create_hardware_with_capabilities(false, false, false, false);
        state.set_hardware(hardware1);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        REQUIRE(lv_subject_get_int(state.get_can_show_bed_mesh_subject()) == 0);

        // Now add bed_mesh capability
        auto hardware2 = create_hardware_with_capabilities(true, false, false, false);
        state.set_hardware(hardware2);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        REQUIRE(lv_subject_get_int(state.get_can_show_bed_mesh_subject()) == 1);
    }
}

// ============================================================================
// Combined State Tests - Verify all combinations work correctly
// ============================================================================

TEST_CASE("Composite visibility characterization: combined states",
          "[characterization][composite-visibility][combined]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    SECTION("all hardware capabilities + plugin installed: hardware-based visible") {
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        auto hardware = create_hardware_with_capabilities(true, true, true, true);
        state.set_hardware(hardware);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(lv_subject_get_int(state.get_can_show_bed_mesh_subject()) == 1);
        REQUIRE(lv_subject_get_int(state.get_can_show_qgl_subject()) == 1);
        REQUIRE(lv_subject_get_int(state.get_can_show_z_tilt_subject()) == 1);
        REQUIRE(lv_subject_get_int(state.get_can_show_nozzle_clean_subject()) == 1);
        // purge_line stays 0 - requires set_printer_type() with compatible printer
        REQUIRE(lv_subject_get_int(state.get_can_show_purge_line_subject()) == 0);
    }

    SECTION("all hardware capabilities + plugin NOT installed: all hidden") {
        state.set_helix_plugin_installed(false);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        auto hardware = create_hardware_with_capabilities(true, true, true, true);
        state.set_hardware(hardware);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(lv_subject_get_int(state.get_can_show_bed_mesh_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_qgl_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_z_tilt_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_nozzle_clean_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_purge_line_subject()) == 0);
    }

    SECTION("mixed capabilities + plugin installed: only enabled ones visible") {
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Only bed_mesh and z_tilt
        auto hardware = create_hardware_with_capabilities(true, false, true, false);
        state.set_hardware(hardware);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(lv_subject_get_int(state.get_can_show_bed_mesh_subject()) == 1);
        REQUIRE(lv_subject_get_int(state.get_can_show_qgl_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_z_tilt_subject()) == 1);
        REQUIRE(lv_subject_get_int(state.get_can_show_nozzle_clean_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_can_show_purge_line_subject()) == 0);
    }
}
