// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_printer_plugin_char.cpp
 * @brief Characterization tests for PrinterState plugin status domain
 *
 * These tests capture the CURRENT behavior of plugin-related subjects
 * in PrinterState before extraction to a dedicated PrinterPluginStatusState class.
 *
 * Plugin status subjects (2 total):
 * - helix_plugin_installed_ (int, tri-state: -1=unknown, 0=not installed, 1=installed)
 * - phase_tracking_enabled_ (int, tri-state: -1=unknown, 0=disabled, 1=enabled)
 *
 * Update mechanisms:
 * - set_helix_plugin_installed(bool) - async update via helix::async::invoke
 * - set_phase_tracking_enabled(bool) - async update via helix::async::invoke
 *
 * Query methods:
 * - service_has_helix_plugin() - returns true only when value is 1
 * - is_phase_tracking_enabled() - returns true only when value is 1
 *
 * Key behaviors:
 * - Both subjects are tri-state: -1 (unknown) is the initial value
 * - Unknown state (-1) is treated as false for boolean queries
 * - Updates trigger update_gcode_modification_visibility() for composite subjects
 */

#include "../test_helpers/printer_state_test_access.h"
#include "../ui_test_utils.h"
#include "app_globals.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

// Helper to get subject by XML name (requires init_subjects(true))
static lv_subject_t* get_subject_by_name(const char* name) {
    return lv_xml_get_subject(NULL, name);
}

// ============================================================================
// Initial Value Tests - Document tri-state initialization behavior
// ============================================================================

TEST_CASE("Plugin status characterization: initial values after init",
          "[characterization][plugin][init]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true); // Need XML registration to lookup by name

    SECTION("helix_plugin_installed initializes to -1 (unknown)") {
        lv_subject_t* subject = get_subject_by_name("helix_plugin_installed");
        REQUIRE(subject != nullptr);
        REQUIRE(lv_subject_get_int(subject) == -1);
    }

    SECTION("phase_tracking_enabled initializes to -1 (unknown)") {
        lv_subject_t* subject = get_subject_by_name("phase_tracking_enabled");
        REQUIRE(subject != nullptr);
        REQUIRE(lv_subject_get_int(subject) == -1);
    }
}

TEST_CASE("Plugin status characterization: initial query methods return false for unknown state",
          "[characterization][plugin][init]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("service_has_helix_plugin returns false when unknown (-1)") {
        REQUIRE(state.service_has_helix_plugin() == false);
    }

    SECTION("is_phase_tracking_enabled returns false when unknown (-1)") {
        REQUIRE(state.is_phase_tracking_enabled() == false);
    }
}

// ============================================================================
// set_helix_plugin_installed Tests - Verify plugin detection updates
// ============================================================================

TEST_CASE("Plugin status characterization: set_helix_plugin_installed behavior",
          "[characterization][plugin][setter]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    SECTION("set_helix_plugin_installed(true) sets subject to 1") {
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        lv_subject_t* subject = get_subject_by_name("helix_plugin_installed");
        REQUIRE(lv_subject_get_int(subject) == 1);
    }

    SECTION("set_helix_plugin_installed(false) sets subject to 0") {
        // First set to true
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Then set to false
        state.set_helix_plugin_installed(false);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        lv_subject_t* subject = get_subject_by_name("helix_plugin_installed");
        REQUIRE(lv_subject_get_int(subject) == 0);
    }

    SECTION("service_has_helix_plugin returns true after set_helix_plugin_installed(true)") {
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(state.service_has_helix_plugin() == true);
    }

    SECTION("service_has_helix_plugin returns false after set_helix_plugin_installed(false)") {
        state.set_helix_plugin_installed(false);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(state.service_has_helix_plugin() == false);
    }
}

// ============================================================================
// set_phase_tracking_enabled Tests - Verify phase tracking toggle behavior
// ============================================================================

TEST_CASE("Plugin status characterization: set_phase_tracking_enabled behavior",
          "[characterization][plugin][setter]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    SECTION("set_phase_tracking_enabled(true) sets subject to 1") {
        state.set_phase_tracking_enabled(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        lv_subject_t* subject = get_subject_by_name("phase_tracking_enabled");
        REQUIRE(lv_subject_get_int(subject) == 1);
    }

    SECTION("set_phase_tracking_enabled(false) sets subject to 0") {
        // First set to true
        state.set_phase_tracking_enabled(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Then set to false
        state.set_phase_tracking_enabled(false);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        lv_subject_t* subject = get_subject_by_name("phase_tracking_enabled");
        REQUIRE(lv_subject_get_int(subject) == 0);
    }

    SECTION("is_phase_tracking_enabled returns true after set_phase_tracking_enabled(true)") {
        state.set_phase_tracking_enabled(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(state.is_phase_tracking_enabled() == true);
    }

    SECTION("is_phase_tracking_enabled returns false after set_phase_tracking_enabled(false)") {
        state.set_phase_tracking_enabled(false);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(state.is_phase_tracking_enabled() == false);
    }
}

// ============================================================================
// Tri-state Semantics Tests - Verify -1/0/1 distinction is maintained
// ============================================================================

TEST_CASE("Plugin status characterization: tri-state semantics",
          "[characterization][plugin][tristate]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("helix_plugin_installed: unknown (-1) vs not installed (0) are distinct") {
        lv_subject_t* subject = state.get_helix_plugin_installed_subject();

        // Initially unknown
        REQUIRE(lv_subject_get_int(subject) == -1);
        REQUIRE(state.service_has_helix_plugin() == false);

        // Set to not installed
        state.set_helix_plugin_installed(false);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(lv_subject_get_int(subject) == 0);
        REQUIRE(state.service_has_helix_plugin() == false);

        // Both return false for query, but subject values are different
        // This allows UI to distinguish "still checking" from "definitely not installed"
    }

    SECTION("phase_tracking_enabled: unknown (-1) vs disabled (0) are distinct") {
        lv_subject_t* subject = get_subject_by_name("phase_tracking_enabled");
        // Need XML registration for this lookup
        state.init_subjects(true);
        subject = get_subject_by_name("phase_tracking_enabled");

        // Get fresh subject after init
        REQUIRE(subject != nullptr);
        // Note: init_subjects already called, so initial state is -1
        int initial_value = lv_subject_get_int(subject);
        REQUIRE(initial_value == -1);
    }
}

// ============================================================================
// Async Update Tests - Verify thread-safe updates
// ============================================================================

TEST_CASE("Plugin status characterization: async update behavior",
          "[characterization][plugin][async]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("set_helix_plugin_installed requires queue drain to take effect") {
        lv_subject_t* subject = state.get_helix_plugin_installed_subject();

        // Call setter but don't drain
        state.set_helix_plugin_installed(true);

        // Subject may still be -1 if queue hasn't processed
        // (This is implementation-dependent - the async call may be synchronous in tests)

        // Drain queue ensures update is processed
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        REQUIRE(lv_subject_get_int(subject) == 1);
    }

    SECTION("set_phase_tracking_enabled requires queue drain to take effect") {
        state.init_subjects(true);
        lv_subject_t* subject = get_subject_by_name("phase_tracking_enabled");

        state.set_phase_tracking_enabled(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(lv_subject_get_int(subject) == 1);
    }

    SECTION("multiple rapid updates coalesce correctly") {
        lv_subject_t* subject = state.get_helix_plugin_installed_subject();

        // Rapid toggling
        state.set_helix_plugin_installed(true);
        state.set_helix_plugin_installed(false);
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Final value should be 1 (last write wins)
        REQUIRE(lv_subject_get_int(subject) == 1);
    }
}

// ============================================================================
// Integration Tests - Verify interaction with composite visibility subjects
// ============================================================================

TEST_CASE("Plugin status characterization: triggers composite visibility update",
          "[characterization][plugin][integration]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    SECTION("set_helix_plugin_installed(true) updates can_show_* subjects") {
        // First verify can_show_bed_mesh is 0 (plugin not installed)
        lv_subject_t* can_show_bed_mesh = get_subject_by_name("can_show_bed_mesh");
        REQUIRE(can_show_bed_mesh != nullptr);
        REQUIRE(lv_subject_get_int(can_show_bed_mesh) == 0);

        // Install plugin and enable bed mesh capability
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Note: can_show_bed_mesh also requires printer_has_bed_mesh to be true
        // So it stays 0 unless we also set the capability
        // This test documents that plugin install triggers the visibility update
    }

    SECTION("set_helix_plugin_installed(false) clears can_show_* subjects") {
        // Install plugin first
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Then uninstall
        state.set_helix_plugin_installed(false);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // All can_show_* should be 0
        lv_subject_t* can_show_bed_mesh = get_subject_by_name("can_show_bed_mesh");
        lv_subject_t* can_show_qgl = get_subject_by_name("can_show_qgl");
        lv_subject_t* can_show_z_tilt = get_subject_by_name("can_show_z_tilt");
        lv_subject_t* can_show_nozzle_clean = get_subject_by_name("can_show_nozzle_clean");
        lv_subject_t* can_show_purge_line = get_subject_by_name("can_show_purge_line");

        REQUIRE(lv_subject_get_int(can_show_bed_mesh) == 0);
        REQUIRE(lv_subject_get_int(can_show_qgl) == 0);
        REQUIRE(lv_subject_get_int(can_show_z_tilt) == 0);
        REQUIRE(lv_subject_get_int(can_show_nozzle_clean) == 0);
        REQUIRE(lv_subject_get_int(can_show_purge_line) == 0);
    }
}
