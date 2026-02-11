// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../test_helpers/printer_state_test_access.h"
#include "../ui_test_utils.h"
#include "app_globals.h"
#include "moonraker_client.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

using Catch::Approx;

// ============================================================================
// Singleton Behavior Tests
// ============================================================================

TEST_CASE("PrinterState: Singleton returns same instance", "[core][state][singleton]") {
    lv_init_safe();

    PrinterState& instance1 = get_printer_state();
    PrinterState& instance2 = get_printer_state();

    // Should be the exact same object (same memory address)
    REQUIRE(&instance1 == &instance2);
}

TEST_CASE("PrinterState: Singleton persists modifications", "[core][state][singleton]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    state.init_subjects();

    // Modify a value through one reference
    state.set_printer_connection_state(static_cast<int>(ConnectionState::CONNECTED), "Connected");
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

    // Read it back through another reference
    PrinterState& state2 = get_printer_state();
    REQUIRE(lv_subject_get_int(state2.get_printer_connection_state_subject()) ==
            static_cast<int>(ConnectionState::CONNECTED));
}

TEST_CASE("PrinterState: Singleton subjects have consistent addresses",
          "[core][state][singleton]") {
    lv_init_safe();

    PrinterState& state1 = get_printer_state();
    state1.init_subjects();

    lv_subject_t* subject1 = state1.get_printer_connection_state_subject();

    PrinterState& state2 = get_printer_state();
    lv_subject_t* subject2 = state2.get_printer_connection_state_subject();

    // Subject pointers must be identical (not just equal values)
    REQUIRE(subject1 == subject2);
}

// ============================================================================
// Observer Pattern Tests
// ============================================================================

TEST_CASE("PrinterState: Observer fires when printer connection state changes",
          "[core][state][observer]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state); // Allow re-initialization after lv_init()
    state.init_subjects(false);           // Skip XML registration

    // Register observer
    auto observer_cb = [](lv_observer_t* observer, lv_subject_t* subject) {
        int* count_ptr = static_cast<int*>(lv_observer_get_user_data(observer));
        int* value_ptr = count_ptr + 1;

        (*count_ptr)++;
        *value_ptr = lv_subject_get_int(subject);
    };

    int user_data[2] = {0, -1}; // [callback_count, last_value]

    lv_observer_t* observer = lv_subject_add_observer(state.get_printer_connection_state_subject(),
                                                      observer_cb, user_data);

    // LVGL auto-notifies observers when first added (fires immediately with current value)
    REQUIRE(user_data[0] == 1); // Callback fired immediately with initial value (0)
    REQUIRE(user_data[1] == 0); // Initial value is DISCONNECTED (0)

    // Change state - should trigger observer again
    state.set_printer_connection_state(static_cast<int>(ConnectionState::CONNECTING),
                                       "Connecting...");
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

    REQUIRE(user_data[0] == 2); // Callback fired again with new value
    REQUIRE(user_data[1] == static_cast<int>(ConnectionState::CONNECTING));

    // Change again
    state.set_printer_connection_state(static_cast<int>(ConnectionState::CONNECTED), "Connected");
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

    REQUIRE(user_data[0] == 3); // Callback fired three times total (initial + 2 changes)
    REQUIRE(user_data[1] == static_cast<int>(ConnectionState::CONNECTED));

    lv_observer_remove(observer);
}

TEST_CASE("PrinterState: Observer fires when network status changes",
          "[state][observer][network]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state); // Allow re-initialization after lv_init()
    state.init_subjects(false);           // Skip XML registration

    int callback_count = 0;

    auto observer_cb = [](lv_observer_t* observer, lv_subject_t*) {
        int* count_ptr = static_cast<int*>(lv_observer_get_user_data(observer));
        (*count_ptr)++;
    };

    lv_observer_t* observer =
        lv_subject_add_observer(state.get_network_status_subject(), observer_cb, &callback_count);

    // LVGL auto-notifies observers when first added (fires immediately with current value)
    // Note: init_subjects() initializes network_status to CONNECTED (2) as mock mode default
    REQUIRE(callback_count == 1); // Callback fired immediately with initial value

    // Change network status to a DIFFERENT value - should trigger observer again
    state.set_network_status(static_cast<int>(NetworkStatus::DISCONNECTED));

    REQUIRE(callback_count == 2); // Callback fired again with new value
    REQUIRE(lv_subject_get_int(state.get_network_status_subject()) ==
            static_cast<int>(NetworkStatus::DISCONNECTED));

    lv_observer_remove(observer);
}

TEST_CASE("PrinterState: Multiple observers on same subject all fire", "[state][observer]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state); // Allow re-initialization after lv_init()
    state.init_subjects(false);           // Skip XML registration

    int count1 = 0, count2 = 0, count3 = 0;

    auto observer_cb = [](lv_observer_t* observer, lv_subject_t*) {
        int* count_ptr = static_cast<int*>(lv_observer_get_user_data(observer));
        (*count_ptr)++;
    };

    // Register three observers on printer connection state
    lv_observer_t* observer1 =
        lv_subject_add_observer(state.get_printer_connection_state_subject(), observer_cb, &count1);
    lv_observer_t* observer2 =
        lv_subject_add_observer(state.get_printer_connection_state_subject(), observer_cb, &count2);
    lv_observer_t* observer3 =
        lv_subject_add_observer(state.get_printer_connection_state_subject(), observer_cb, &count3);

    // LVGL auto-notifies observers when first added (each fires immediately with current value)
    REQUIRE(count1 == 1); // First observer fired immediately
    REQUIRE(count2 == 1); // Second observer fired immediately
    REQUIRE(count3 == 1); // Third observer fired immediately

    // Single state change should fire all three again
    state.set_printer_connection_state(static_cast<int>(ConnectionState::CONNECTED), "Connected");
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

    REQUIRE(count1 == 2); // All observers fired again
    REQUIRE(count2 == 2);
    REQUIRE(count3 == 2);

    lv_observer_remove(observer1);
    lv_observer_remove(observer2);
    lv_observer_remove(observer3);
}

// ============================================================================
// Initialization Tests
// ============================================================================

TEST_CASE("PrinterState: Initialization sets default values", "[state][init]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state); // Reset singleton state from previous tests
    state.init_subjects();

    // Temperature subjects should be initialized to 0
    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 0);

    // Print progress should be 0
    REQUIRE(lv_subject_get_int(state.get_print_progress_subject()) == 0);

    // Print state should be "standby"
    const char* print_state = lv_subject_get_string(state.get_print_state_subject());
    REQUIRE(std::string(print_state) == "standby");

    // Position should be 0
    REQUIRE(lv_subject_get_int(state.get_position_x_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_position_y_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_position_z_subject()) == 0);

    // Speed/flow factors should be 100%
    REQUIRE(lv_subject_get_int(state.get_speed_factor_subject()) == 100);
    REQUIRE(lv_subject_get_int(state.get_flow_factor_subject()) == 100);

    // Fan speed should be 0
    REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 0);

    // Printer connection state should be DISCONNECTED
    REQUIRE(lv_subject_get_int(state.get_printer_connection_state_subject()) ==
            static_cast<int>(ConnectionState::DISCONNECTED));

    // Network status is initialized to CONNECTED (mock mode default)
    // In production, actual network status comes from EthernetManager/WiFiManager
    REQUIRE(lv_subject_get_int(state.get_network_status_subject()) ==
            static_cast<int>(NetworkStatus::CONNECTED));
}

// ============================================================================
// Temperature Update Tests
// ============================================================================
// Note: Subjects store temperatures in centidegrees (temp * 10) for 0.1°C resolution.
// Tests use update_from_status() directly since update_from_notification() uses
// lv_async_call() which requires pumping the LVGL timer.

TEST_CASE("PrinterState: Update extruder temperature from status", "[state][temp]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    json status = {{"extruder", {{"temperature", 205.3}, {"target", 210.0}}}};
    state.update_from_status(status);

    // Subjects store centidegrees (temp * 10)
    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 2053);
    REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 2100);
}

TEST_CASE("PrinterState: Update bed temperature from status", "[state][temp]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    json status = {{"heater_bed", {{"temperature", 60.5}, {"target", 60.0}}}};
    state.update_from_status(status);

    // Subjects store centidegrees (temp * 10)
    REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 605);
    REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 600);
}

TEST_CASE("PrinterState: Temperature centidegree storage", "[state][temp][edge]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("205.4°C stored as 2054 centidegrees") {
        json status = {{"extruder", {{"temperature", 205.4}}}};
        state.update_from_status(status);
        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 2054);
    }

    SECTION("205.6°C stored as 2056 centidegrees") {
        json status = {{"extruder", {{"temperature", 205.6}}}};
        state.update_from_status(status);
        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 2056);
    }

    SECTION("210.0°C stored as 2100 centidegrees") {
        json status = {{"extruder", {{"temperature", 210.0}}}};
        state.update_from_status(status);
        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 2100);
    }
}

// ============================================================================
// Print Progress Tests
// ============================================================================

TEST_CASE("PrinterState: Update print progress from notification", "[state][progress]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    state.init_subjects();

    json notification = {{"method", "notify_status_update"},
                         {"params", {{{"virtual_sdcard", {{"progress", 0.45}}}}, 1234567890.0}}};

    state.update_from_status(notification["params"][0]);

    REQUIRE(lv_subject_get_int(state.get_print_progress_subject()) == 45);
}

TEST_CASE("PrinterState: Update print state and filename", "[state][progress]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    state.init_subjects();

    json notification = {
        {"method", "notify_status_update"},
        {"params",
         {{{"print_stats", {{"state", "printing"}, {"filename", "benchy.gcode"}}}}, 1234567890.0}}};

    state.update_from_status(notification["params"][0]);

    const char* print_state = lv_subject_get_string(state.get_print_state_subject());
    REQUIRE(std::string(print_state) == "printing");

    const char* filename = lv_subject_get_string(state.get_print_filename_subject());
    REQUIRE(std::string(filename) == "benchy.gcode");
}

TEST_CASE("PrinterState: Progress percentage edge cases", "[state][progress][edge]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    state.init_subjects();

    SECTION("0% progress") {
        json notification = {{"method", "notify_status_update"},
                             {"params", {{{"virtual_sdcard", {{"progress", 0.0}}}}, 0.0}}};
        state.update_from_status(notification["params"][0]);
        REQUIRE(lv_subject_get_int(state.get_print_progress_subject()) == 0);
    }

    SECTION("100% progress") {
        json notification = {{"method", "notify_status_update"},
                             {"params", {{{"virtual_sdcard", {{"progress", 1.0}}}}, 0.0}}};
        state.update_from_status(notification["params"][0]);
        REQUIRE(lv_subject_get_int(state.get_print_progress_subject()) == 100);
    }

    SECTION("67.3% progress -> 67%") {
        json notification = {{"method", "notify_status_update"},
                             {"params", {{{"virtual_sdcard", {{"progress", 0.673}}}}, 0.0}}};
        state.update_from_status(notification["params"][0]);
        REQUIRE(lv_subject_get_int(state.get_print_progress_subject()) == 67);
    }
}

// ============================================================================
// Motion/Position Tests
// ============================================================================

TEST_CASE("PrinterState: Update toolhead position", "[state][motion]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    state.init_subjects();

    json notification = {
        {"method", "notify_status_update"},
        {"params",
         {{{"toolhead", {{"position", {125.5, 87.3, 45.2, 1234.5}}, {"homed_axes", "xyz"}}}},
          1234567890.0}}};

    state.update_from_status(notification["params"][0]);

    // Positions are stored as centimillimeters (×100) for 0.01mm precision
    REQUIRE(lv_subject_get_int(state.get_position_x_subject()) == 12550); // 125.5mm
    REQUIRE(lv_subject_get_int(state.get_position_y_subject()) == 8730);  // 87.3mm
    REQUIRE(lv_subject_get_int(state.get_position_z_subject()) == 4520);  // 45.2mm

    const char* homed = lv_subject_get_string(state.get_homed_axes_subject());
    REQUIRE(std::string(homed) == "xyz");
}

TEST_CASE("PrinterState: Homed axes variations", "[state][motion]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    state.init_subjects();

    SECTION("Only X and Y homed") {
        json notification = {{"method", "notify_status_update"},
                             {"params", {{{"toolhead", {{"homed_axes", "xy"}}}}, 0.0}}};
        state.update_from_status(notification["params"][0]);
        const char* homed = lv_subject_get_string(state.get_homed_axes_subject());
        REQUIRE(std::string(homed) == "xy");
    }

    SECTION("No axes homed") {
        json notification = {{"method", "notify_status_update"},
                             {"params", {{{"toolhead", {{"homed_axes", ""}}}}, 0.0}}};
        state.update_from_status(notification["params"][0]);
        const char* homed = lv_subject_get_string(state.get_homed_axes_subject());
        REQUIRE(std::string(homed) == "");
    }

    SECTION("Only Z homed") {
        json notification = {{"method", "notify_status_update"},
                             {"params", {{{"toolhead", {{"homed_axes", "z"}}}}, 0.0}}};
        state.update_from_status(notification["params"][0]);
        const char* homed = lv_subject_get_string(state.get_homed_axes_subject());
        REQUIRE(std::string(homed) == "z");
    }

    SECTION("XYZ homed") {
        json notification = {{"method", "notify_status_update"},
                             {"params", {{{"toolhead", {{"homed_axes", "xyz"}}}}, 0.0}}};
        state.update_from_status(notification["params"][0]);
        const char* homed = lv_subject_get_string(state.get_homed_axes_subject());
        REQUIRE(std::string(homed) == "xyz");
    }
}

// Helper function for testing homed axes derivation logic
// This mirrors the logic in ControlsPanel::on_homed_axes_changed()
static void derive_homed_states(const char* axes, int& xy_homed, int& z_homed, int& all_homed) {
    if (!axes)
        axes = "";
    bool has_x = strchr(axes, 'x') != nullptr;
    bool has_y = strchr(axes, 'y') != nullptr;
    bool has_z = strchr(axes, 'z') != nullptr;

    xy_homed = (has_x && has_y) ? 1 : 0;
    z_homed = has_z ? 1 : 0;
    all_homed = (has_x && has_y && has_z) ? 1 : 0;
}

TEST_CASE("PrinterState: Homed axes derivation logic", "[state][motion][homing]") {
    // Test the derivation logic used by ControlsPanel to create boolean subjects
    // from the homed_axes string subject. This logic is critical for bind_style
    // to work correctly on home buttons.

    SECTION("Empty string - nothing homed") {
        int xy = 0, z = 0, all = 0;
        derive_homed_states("", xy, z, all);
        REQUIRE(xy == 0);
        REQUIRE(z == 0);
        REQUIRE(all == 0);
    }

    SECTION("Only X homed - XY not complete") {
        int xy = 0, z = 0, all = 0;
        derive_homed_states("x", xy, z, all);
        REQUIRE(xy == 0); // Need both X and Y
        REQUIRE(z == 0);
        REQUIRE(all == 0);
    }

    SECTION("Only Y homed - XY not complete") {
        int xy = 0, z = 0, all = 0;
        derive_homed_states("y", xy, z, all);
        REQUIRE(xy == 0); // Need both X and Y
        REQUIRE(z == 0);
        REQUIRE(all == 0);
    }

    SECTION("Only Z homed") {
        int xy = 0, z = 0, all = 0;
        derive_homed_states("z", xy, z, all);
        REQUIRE(xy == 0);
        REQUIRE(z == 1);
        REQUIRE(all == 0);
    }

    SECTION("XY homed (typical after G28 X Y)") {
        int xy = 0, z = 0, all = 0;
        derive_homed_states("xy", xy, z, all);
        REQUIRE(xy == 1);
        REQUIRE(z == 0);
        REQUIRE(all == 0);
    }

    SECTION("XZ homed") {
        int xy = 0, z = 0, all = 0;
        derive_homed_states("xz", xy, z, all);
        REQUIRE(xy == 0); // Missing Y
        REQUIRE(z == 1);
        REQUIRE(all == 0);
    }

    SECTION("YZ homed") {
        int xy = 0, z = 0, all = 0;
        derive_homed_states("yz", xy, z, all);
        REQUIRE(xy == 0); // Missing X
        REQUIRE(z == 1);
        REQUIRE(all == 0);
    }

    SECTION("All axes homed (typical after G28)") {
        int xy = 0, z = 0, all = 0;
        derive_homed_states("xyz", xy, z, all);
        REQUIRE(xy == 1);
        REQUIRE(z == 1);
        REQUIRE(all == 1);
    }

    SECTION("Null input treated as empty") {
        int xy = 0, z = 0, all = 0;
        derive_homed_states(nullptr, xy, z, all);
        REQUIRE(xy == 0);
        REQUIRE(z == 0);
        REQUIRE(all == 0);
    }
}

TEST_CASE("PrinterState: Homed axes observer pattern for derived subjects",
          "[state][motion][homing][observer]") {
    // This tests the observer pattern that panels use to derive boolean subjects
    // from homed_axes. ControlsPanel uses this to update xy_homed_, z_homed_,
    // all_homed_ subjects for bind_style on home buttons.

    lv_init_safe();
    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Track derived values via observer
    struct HomingState {
        int xy_homed = 0;
        int z_homed = 0;
        int all_homed = 0;
        int callback_count = 0;
    } homing;

    auto observer_cb = [](lv_observer_t* observer, lv_subject_t* subject) {
        auto* state = static_cast<HomingState*>(lv_observer_get_user_data(observer));
        const char* axes = lv_subject_get_string(subject);
        derive_homed_states(axes, state->xy_homed, state->z_homed, state->all_homed);
        state->callback_count++;
    };

    lv_observer_t* observer =
        lv_subject_add_observer(state.get_homed_axes_subject(), observer_cb, &homing);

    // Initial callback fires immediately (LVGL behavior)
    REQUIRE(homing.callback_count == 1);
    REQUIRE(homing.xy_homed == 0);
    REQUIRE(homing.z_homed == 0);
    REQUIRE(homing.all_homed == 0);

    // Simulate G28 X Y
    json notification = {{"method", "notify_status_update"},
                         {"params", {{{"toolhead", {{"homed_axes", "xy"}}}}, 0.0}}};
    state.update_from_status(notification["params"][0]);

    REQUIRE(homing.callback_count == 2);
    REQUIRE(homing.xy_homed == 1);
    REQUIRE(homing.z_homed == 0);
    REQUIRE(homing.all_homed == 0);

    // Simulate G28 Z (now all homed)
    notification = {{"method", "notify_status_update"},
                    {"params", {{{"toolhead", {{"homed_axes", "xyz"}}}}, 0.0}}};
    state.update_from_status(notification["params"][0]);

    REQUIRE(homing.callback_count == 3);
    REQUIRE(homing.xy_homed == 1);
    REQUIRE(homing.z_homed == 1);
    REQUIRE(homing.all_homed == 1);

    // Simulate unhoming (e.g., SET_KINEMATIC_POSITION or restart)
    notification = {{"method", "notify_status_update"},
                    {"params", {{{"toolhead", {{"homed_axes", ""}}}}, 0.0}}};
    state.update_from_status(notification["params"][0]);

    REQUIRE(homing.callback_count == 4);
    REQUIRE(homing.xy_homed == 0);
    REQUIRE(homing.z_homed == 0);
    REQUIRE(homing.all_homed == 0);

    lv_observer_remove(observer);
}

// ============================================================================
// Speed/Flow Factor Tests
// ============================================================================

TEST_CASE("PrinterState: Update speed and flow factors", "[state][speed]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    state.init_subjects();

    json notification = {
        {"method", "notify_status_update"},
        {"params",
         {{{"gcode_move", {{"speed_factor", 1.25}, {"extrude_factor", 0.95}}}}, 1234567890.0}}};

    state.update_from_status(notification["params"][0]);

    REQUIRE(lv_subject_get_int(state.get_speed_factor_subject()) == 125);
    REQUIRE(lv_subject_get_int(state.get_flow_factor_subject()) == 95);
}

TEST_CASE("PrinterState: Update fan speed", "[state][fan]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    state.init_subjects();

    json notification = {{"method", "notify_status_update"},
                         {"params", {{{"fan", {{"speed", 0.75}}}}, 1234567890.0}}};

    state.update_from_status(notification["params"][0]);

    REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 75);
}

// ============================================================================
// Connection State Tests
// ============================================================================

TEST_CASE("PrinterState: Set printer connection state", "[state][connection]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    state.init_subjects();

    state.set_printer_connection_state(static_cast<int>(ConnectionState::CONNECTED), "Connected");
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

    REQUIRE(lv_subject_get_int(state.get_printer_connection_state_subject()) ==
            static_cast<int>(ConnectionState::CONNECTED));

    const char* message = lv_subject_get_string(state.get_printer_connection_message_subject());
    REQUIRE(std::string(message) == "Connected");
}

TEST_CASE("PrinterState: Connection state transitions", "[state][connection]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    state.init_subjects();

    SECTION("Disconnected -> Connecting") {
        state.set_printer_connection_state(static_cast<int>(ConnectionState::DISCONNECTED),
                                           "Disconnected");
        state.set_printer_connection_state(static_cast<int>(ConnectionState::CONNECTING),
                                           "Connecting...");
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        REQUIRE(lv_subject_get_int(state.get_printer_connection_state_subject()) ==
                static_cast<int>(ConnectionState::CONNECTING));
    }

    SECTION("Connecting -> Connected") {
        state.set_printer_connection_state(static_cast<int>(ConnectionState::CONNECTING),
                                           "Connecting...");
        state.set_printer_connection_state(static_cast<int>(ConnectionState::CONNECTED), "Ready");
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        REQUIRE(lv_subject_get_int(state.get_printer_connection_state_subject()) ==
                static_cast<int>(ConnectionState::CONNECTED));
    }

    SECTION("Connected -> Reconnecting") {
        state.set_printer_connection_state(static_cast<int>(ConnectionState::CONNECTED), "Ready");
        state.set_printer_connection_state(static_cast<int>(ConnectionState::RECONNECTING),
                                           "Reconnecting...");
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        REQUIRE(lv_subject_get_int(state.get_printer_connection_state_subject()) ==
                static_cast<int>(ConnectionState::RECONNECTING));
    }

    SECTION("Failed connection") {
        state.set_printer_connection_state(static_cast<int>(ConnectionState::FAILED),
                                           "Connection failed");
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        REQUIRE(lv_subject_get_int(state.get_printer_connection_state_subject()) ==
                static_cast<int>(ConnectionState::FAILED));
    }
}

// ============================================================================
// Network Status Tests
// ============================================================================

TEST_CASE("PrinterState: Network status initialization", "[state][network]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state); // Reset singleton state from previous tests
    state.init_subjects();

    // Network status is initialized to CONNECTED (mock mode default)
    // In production, actual network status comes from EthernetManager/WiFiManager
    REQUIRE(lv_subject_get_int(state.get_network_status_subject()) ==
            static_cast<int>(NetworkStatus::CONNECTED));
}

TEST_CASE("PrinterState: Set network status updates subject", "[state][network]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    state.init_subjects();

    state.set_network_status(static_cast<int>(NetworkStatus::CONNECTED));

    REQUIRE(lv_subject_get_int(state.get_network_status_subject()) ==
            static_cast<int>(NetworkStatus::CONNECTED));
}

TEST_CASE("PrinterState: Network status enum values", "[state][network]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    state.init_subjects();

    SECTION("DISCONNECTED") {
        state.set_network_status(static_cast<int>(NetworkStatus::DISCONNECTED));
        REQUIRE(lv_subject_get_int(state.get_network_status_subject()) ==
                static_cast<int>(NetworkStatus::DISCONNECTED));
    }

    SECTION("CONNECTING") {
        state.set_network_status(static_cast<int>(NetworkStatus::CONNECTING));
        REQUIRE(lv_subject_get_int(state.get_network_status_subject()) ==
                static_cast<int>(NetworkStatus::CONNECTING));
    }

    SECTION("CONNECTED") {
        state.set_network_status(static_cast<int>(NetworkStatus::CONNECTED));
        REQUIRE(lv_subject_get_int(state.get_network_status_subject()) ==
                static_cast<int>(NetworkStatus::CONNECTED));
    }
}

TEST_CASE("PrinterState: Printer and network status are independent", "[state][integration]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    state.init_subjects();

    // Set printer connected but network disconnected
    state.set_printer_connection_state(static_cast<int>(ConnectionState::CONNECTED), "Connected");
    state.set_network_status(static_cast<int>(NetworkStatus::DISCONNECTED));
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

    REQUIRE(lv_subject_get_int(state.get_printer_connection_state_subject()) ==
            static_cast<int>(ConnectionState::CONNECTED));
    REQUIRE(lv_subject_get_int(state.get_network_status_subject()) ==
            static_cast<int>(NetworkStatus::DISCONNECTED));

    // Set network connected but printer disconnected
    state.set_printer_connection_state(static_cast<int>(ConnectionState::DISCONNECTED),
                                       "Disconnected");
    state.set_network_status(static_cast<int>(NetworkStatus::CONNECTED));
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

    REQUIRE(lv_subject_get_int(state.get_printer_connection_state_subject()) ==
            static_cast<int>(ConnectionState::DISCONNECTED));
    REQUIRE(lv_subject_get_int(state.get_network_status_subject()) ==
            static_cast<int>(NetworkStatus::CONNECTED));
}

// ============================================================================
// Invalid/Malformed Data Tests
// ============================================================================
// These tests verify update_from_status handles edge cases gracefully.
// Note: update_from_notification validation (method/params checks) is tested
// implicitly through integration tests with MoonrakerClientMock.

TEST_CASE("PrinterState: Empty status object is handled", "[state][error]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Empty JSON should not crash
    json empty_status = json::object();
    state.update_from_status(empty_status);

    // Values should remain at defaults
    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 0);
}

TEST_CASE("PrinterState: Partial status updates work", "[state][error]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("Only extruder temp, no target") {
        json status = {{"extruder", {{"temperature", 205.0}}}};
        state.update_from_status(status);
        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 2050);
        REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 0); // unchanged
    }

    SECTION("Only bed target, no temp") {
        json status = {{"heater_bed", {{"target", 60.0}}}};
        state.update_from_status(status);
        REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 0); // unchanged
        REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 600);
    }

    SECTION("Unknown fields are ignored") {
        json status = {{"unknown_sensor", {{"value", 123.0}}},
                       {"extruder", {{"temperature", 100.0}}}};
        state.update_from_status(status);
        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 1000);
    }
}

// ============================================================================
// Comprehensive State Update Tests
// ============================================================================

TEST_CASE("PrinterState: Complete printing state update", "[state][integration]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    state.init_subjects();

    json notification = {
        {"method", "notify_status_update"},
        {"params",
         {{{"extruder", {{"temperature", 210.5}, {"target", 210.0}}},
           {"heater_bed", {{"temperature", 60.2}, {"target", 60.0}}},
           {"virtual_sdcard", {{"progress", 0.67}}},
           {"print_stats", {{"state", "printing"}, {"filename", "model.gcode"}}},
           {"toolhead", {{"position", {125.0, 87.0, 45.0, 1234.0}}, {"homed_axes", "xyz"}}},
           {"gcode_move", {{"speed_factor", 1.0}, {"extrude_factor", 1.0}}},
           {"fan", {{"speed", 0.5}}}},
          1234567890.0}}};

    state.update_from_status(notification["params"][0]);

    // Verify all values updated correctly (temps stored as centidegrees)
    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 2105);
    REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 2100);
    REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 602);
    REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 600);
    REQUIRE(lv_subject_get_int(state.get_print_progress_subject()) == 67);
    REQUIRE(std::string(lv_subject_get_string(state.get_print_state_subject())) == "printing");
    REQUIRE(std::string(lv_subject_get_string(state.get_print_filename_subject())) ==
            "model.gcode");
    // Positions are stored as centimillimeters (×100) for 0.01mm precision
    REQUIRE(lv_subject_get_int(state.get_position_x_subject()) == 12500); // 125.0mm
    REQUIRE(lv_subject_get_int(state.get_position_y_subject()) == 8700);  // 87.0mm
    REQUIRE(lv_subject_get_int(state.get_position_z_subject()) == 4500);  // 45.0mm
    REQUIRE(std::string(lv_subject_get_string(state.get_homed_axes_subject())) == "xyz");
    REQUIRE(lv_subject_get_int(state.get_speed_factor_subject()) == 100);
    REQUIRE(lv_subject_get_int(state.get_flow_factor_subject()) == 100);
    REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 50);
}

// ============================================================================
// PrintJobState Enum Tests
// ============================================================================

TEST_CASE("PrintJobState: parse_print_job_state parses Moonraker strings", "[state][enum]") {
    SECTION("Parses all standard Moonraker states") {
        REQUIRE(parse_print_job_state("standby") == PrintJobState::STANDBY);
        REQUIRE(parse_print_job_state("printing") == PrintJobState::PRINTING);
        REQUIRE(parse_print_job_state("paused") == PrintJobState::PAUSED);
        REQUIRE(parse_print_job_state("complete") == PrintJobState::COMPLETE);
        REQUIRE(parse_print_job_state("cancelled") == PrintJobState::CANCELLED);
        REQUIRE(parse_print_job_state("error") == PrintJobState::ERROR);
    }

    SECTION("Unknown strings default to STANDBY") {
        REQUIRE(parse_print_job_state("unknown") == PrintJobState::STANDBY);
        REQUIRE(parse_print_job_state("") == PrintJobState::STANDBY);
        REQUIRE(parse_print_job_state("PRINTING") == PrintJobState::STANDBY); // Case sensitive
    }

    SECTION("Handles null input") {
        REQUIRE(parse_print_job_state(nullptr) == PrintJobState::STANDBY);
    }
}

TEST_CASE("PrintJobState: print_job_state_to_string converts to display strings", "[state][enum]") {
    REQUIRE(std::string(print_job_state_to_string(PrintJobState::STANDBY)) == "Standby");
    REQUIRE(std::string(print_job_state_to_string(PrintJobState::PRINTING)) == "Printing");
    REQUIRE(std::string(print_job_state_to_string(PrintJobState::PAUSED)) == "Paused");
    REQUIRE(std::string(print_job_state_to_string(PrintJobState::COMPLETE)) == "Complete");
    REQUIRE(std::string(print_job_state_to_string(PrintJobState::CANCELLED)) == "Cancelled");
    REQUIRE(std::string(print_job_state_to_string(PrintJobState::ERROR)) == "Error");
}

TEST_CASE("PrintJobState: Enum values match expected integers", "[state][enum]") {
    // These values are documented and must not change for backward compatibility
    REQUIRE(static_cast<int>(PrintJobState::STANDBY) == 0);
    REQUIRE(static_cast<int>(PrintJobState::PRINTING) == 1);
    REQUIRE(static_cast<int>(PrintJobState::PAUSED) == 2);
    REQUIRE(static_cast<int>(PrintJobState::COMPLETE) == 3);
    REQUIRE(static_cast<int>(PrintJobState::CANCELLED) == 4);
    REQUIRE(static_cast<int>(PrintJobState::ERROR) == 5);
}

TEST_CASE("PrinterState: Print state enum subject updates from notification", "[state][enum]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    state.init_subjects();

    // Reset to known state first
    json standby_notification = {{"method", "notify_status_update"},
                                 {"params", {{{"print_stats", {{"state", "standby"}}}}, 0.0}}};
    state.update_from_notification(standby_notification);

    SECTION("Updates to PRINTING from notification") {
        json notification = {{"method", "notify_status_update"},
                             {"params", {{{"print_stats", {{"state", "printing"}}}}, 0.0}}};
        state.update_from_status(notification["params"][0]);

        REQUIRE(state.get_print_job_state() == PrintJobState::PRINTING);
        REQUIRE(lv_subject_get_int(state.get_print_state_enum_subject()) ==
                static_cast<int>(PrintJobState::PRINTING));
    }

    SECTION("Updates to PAUSED from notification") {
        json notification = {{"method", "notify_status_update"},
                             {"params", {{{"print_stats", {{"state", "paused"}}}}, 0.0}}};
        state.update_from_status(notification["params"][0]);

        REQUIRE(state.get_print_job_state() == PrintJobState::PAUSED);
    }

    SECTION("Both string and enum subjects update together") {
        json notification = {{"method", "notify_status_update"},
                             {"params", {{{"print_stats", {{"state", "complete"}}}}, 0.0}}};
        state.update_from_status(notification["params"][0]);

        // String subject should have the raw string
        REQUIRE(std::string(lv_subject_get_string(state.get_print_state_subject())) == "complete");
        // Enum subject should have the parsed enum value
        REQUIRE(state.get_print_job_state() == PrintJobState::COMPLETE);
    }
}

TEST_CASE("PrinterState: can_start_new_print logic", "[state][enum]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    state.init_subjects();

    SECTION("Can start from STANDBY") {
        json notification = {{"method", "notify_status_update"},
                             {"params", {{{"print_stats", {{"state", "standby"}}}}, 0.0}}};
        state.update_from_status(notification["params"][0]);
        REQUIRE(state.can_start_new_print() == true);
    }

    SECTION("Can start from COMPLETE") {
        json notification = {{"method", "notify_status_update"},
                             {"params", {{{"print_stats", {{"state", "complete"}}}}, 0.0}}};
        state.update_from_status(notification["params"][0]);
        REQUIRE(state.can_start_new_print() == true);
    }

    SECTION("Can start from CANCELLED") {
        json notification = {{"method", "notify_status_update"},
                             {"params", {{{"print_stats", {{"state", "cancelled"}}}}, 0.0}}};
        state.update_from_status(notification["params"][0]);
        REQUIRE(state.can_start_new_print() == true);
    }

    SECTION("Can start from ERROR") {
        json notification = {{"method", "notify_status_update"},
                             {"params", {{{"print_stats", {{"state", "error"}}}}, 0.0}}};
        state.update_from_status(notification["params"][0]);
        REQUIRE(state.can_start_new_print() == true);
    }

    SECTION("Cannot start from PRINTING") {
        json notification = {{"method", "notify_status_update"},
                             {"params", {{{"print_stats", {{"state", "printing"}}}}, 0.0}}};
        state.update_from_status(notification["params"][0]);
        REQUIRE(state.can_start_new_print() == false);
    }

    SECTION("Cannot start from PAUSED") {
        json notification = {{"method", "notify_status_update"},
                             {"params", {{{"print_stats", {{"state", "paused"}}}}, 0.0}}};
        state.update_from_status(notification["params"][0]);
        REQUIRE(state.can_start_new_print() == false);
    }
}

TEST_CASE("PrinterState: Enum subject value reflects all state transitions", "[state][enum]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    state.init_subjects();

    // Test all state transitions are reflected in the enum subject

    // STANDBY
    json notification = {{"method", "notify_status_update"},
                         {"params", {{{"print_stats", {{"state", "standby"}}}}, 0.0}}};
    state.update_from_status(notification["params"][0]);
    REQUIRE(lv_subject_get_int(state.get_print_state_enum_subject()) ==
            static_cast<int>(PrintJobState::STANDBY));

    // PRINTING
    notification = {{"method", "notify_status_update"},
                    {"params", {{{"print_stats", {{"state", "printing"}}}}, 0.0}}};
    state.update_from_status(notification["params"][0]);
    REQUIRE(lv_subject_get_int(state.get_print_state_enum_subject()) ==
            static_cast<int>(PrintJobState::PRINTING));

    // PAUSED
    notification = {{"method", "notify_status_update"},
                    {"params", {{{"print_stats", {{"state", "paused"}}}}, 0.0}}};
    state.update_from_status(notification["params"][0]);
    REQUIRE(lv_subject_get_int(state.get_print_state_enum_subject()) ==
            static_cast<int>(PrintJobState::PAUSED));

    // COMPLETE
    notification = {{"method", "notify_status_update"},
                    {"params", {{{"print_stats", {{"state", "complete"}}}}, 0.0}}};
    state.update_from_status(notification["params"][0]);
    REQUIRE(lv_subject_get_int(state.get_print_state_enum_subject()) ==
            static_cast<int>(PrintJobState::COMPLETE));

    // CANCELLED
    notification = {{"method", "notify_status_update"},
                    {"params", {{{"print_stats", {{"state", "cancelled"}}}}, 0.0}}};
    state.update_from_status(notification["params"][0]);
    REQUIRE(lv_subject_get_int(state.get_print_state_enum_subject()) ==
            static_cast<int>(PrintJobState::CANCELLED));

    // ERROR
    notification = {{"method", "notify_status_update"},
                    {"params", {{{"print_stats", {{"state", "error"}}}}, 0.0}}};
    state.update_from_status(notification["params"][0]);
    REQUIRE(lv_subject_get_int(state.get_print_state_enum_subject()) ==
            static_cast<int>(PrintJobState::ERROR));
}

// ============================================================================
// KlippyState Tests
// ============================================================================

TEST_CASE("PrinterState: Klippy state initialization defaults to READY", "[state][klippy]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Default should be READY (0)
    REQUIRE(lv_subject_get_int(state.get_klippy_state_subject()) ==
            static_cast<int>(KlippyState::READY));
}

TEST_CASE("PrinterState: set_klippy_state_sync changes subject value", "[state][klippy]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Default should be READY
    REQUIRE(lv_subject_get_int(state.get_klippy_state_subject()) ==
            static_cast<int>(KlippyState::READY));

    // Call set_klippy_state_sync (direct call, no async)
    state.set_klippy_state_sync(KlippyState::SHUTDOWN);

    // Subject should now be SHUTDOWN
    REQUIRE(lv_subject_get_int(state.get_klippy_state_subject()) ==
            static_cast<int>(KlippyState::SHUTDOWN));

    // Test other states
    state.set_klippy_state_sync(KlippyState::STARTUP);
    REQUIRE(lv_subject_get_int(state.get_klippy_state_subject()) ==
            static_cast<int>(KlippyState::STARTUP));

    state.set_klippy_state_sync(KlippyState::ERROR);
    REQUIRE(lv_subject_get_int(state.get_klippy_state_subject()) ==
            static_cast<int>(KlippyState::ERROR));

    state.set_klippy_state_sync(KlippyState::READY);
    REQUIRE(lv_subject_get_int(state.get_klippy_state_subject()) ==
            static_cast<int>(KlippyState::READY));
}

TEST_CASE("PrinterState: Observer fires when klippy state changes", "[state][klippy][observer]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Register observer
    auto observer_cb = [](lv_observer_t* observer, lv_subject_t* subject) {
        int* count_ptr = static_cast<int*>(lv_observer_get_user_data(observer));
        int* value_ptr = count_ptr + 1;

        (*count_ptr)++;
        *value_ptr = lv_subject_get_int(subject);
    };

    int user_data[2] = {0, -1}; // [callback_count, last_value]

    lv_observer_t* observer =
        lv_subject_add_observer(state.get_klippy_state_subject(), observer_cb, user_data);

    // LVGL auto-notifies observers when first added (fires immediately with current value)
    REQUIRE(user_data[0] == 1);
    REQUIRE(user_data[1] == static_cast<int>(KlippyState::READY));

    // Change state via sync call (direct, no async)
    state.set_klippy_state_sync(KlippyState::SHUTDOWN);

    // Observer should have fired with new value
    REQUIRE(user_data[0] == 2);
    REQUIRE(user_data[1] == static_cast<int>(KlippyState::SHUTDOWN));

    // Change again
    state.set_klippy_state_sync(KlippyState::READY);

    REQUIRE(user_data[0] == 3);
    REQUIRE(user_data[1] == static_cast<int>(KlippyState::READY));

    lv_observer_remove(observer);
}

TEST_CASE("PrinterState: Update klippy state from webhooks notification",
          "[state][klippy][webhooks]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Test "startup" state (RESTART in progress)
    nlohmann::json notification = {
        {"method", "notify_status_update"},
        {"params",
         {{{"webhooks", {{"state", "startup"}, {"state_message", "Klipper restart"}}}}, 0.0}}};
    state.update_from_status(notification["params"][0]);
    REQUIRE(lv_subject_get_int(state.get_klippy_state_subject()) ==
            static_cast<int>(KlippyState::STARTUP));

    // Test "ready" state (restart complete)
    notification = {
        {"method", "notify_status_update"},
        {"params",
         {{{"webhooks", {{"state", "ready"}, {"state_message", "Printer is ready"}}}}, 0.0}}};
    state.update_from_status(notification["params"][0]);
    REQUIRE(lv_subject_get_int(state.get_klippy_state_subject()) ==
            static_cast<int>(KlippyState::READY));

    // Test "shutdown" state (M112 emergency stop)
    notification = {
        {"method", "notify_status_update"},
        {"params",
         {{{"webhooks", {{"state", "shutdown"}, {"state_message", "Emergency stop"}}}}, 0.0}}};
    state.update_from_status(notification["params"][0]);
    REQUIRE(lv_subject_get_int(state.get_klippy_state_subject()) ==
            static_cast<int>(KlippyState::SHUTDOWN));

    // Test "error" state (Klipper error)
    notification = {
        {"method", "notify_status_update"},
        {"params",
         {{{"webhooks", {{"state", "error"}, {"state_message", "Check klippy.log"}}}}, 0.0}}};
    state.update_from_status(notification["params"][0]);
    REQUIRE(lv_subject_get_int(state.get_klippy_state_subject()) ==
            static_cast<int>(KlippyState::ERROR));
}

TEST_CASE("PrinterState: Unknown webhooks state defaults to READY", "[state][klippy][webhooks]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Can't pre-set klippy state (async), so just verify unknown -> READY
    // The subject starts at READY (0), so we verify the parse logic works
    nlohmann::json notification = {{"method", "notify_status_update"},
                                   {"params", {{{"webhooks", {{"state", "unknown_state"}}}}, 0.0}}};
    state.update_from_status(notification["params"][0]);
    // Unknown state should remain READY (no change from default)
    REQUIRE(lv_subject_get_int(state.get_klippy_state_subject()) ==
            static_cast<int>(KlippyState::READY));
}

// ============================================================================
// Kinematics / Bed Moves Tests
// ============================================================================

TEST_CASE("PrinterState: set_kinematics detects corexy as bed-moves", "[state][kinematics]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Default should be 0 (gantry moves)
    REQUIRE(lv_subject_get_int(state.get_printer_bed_moves_subject()) == 0);

    // CoreXY printers (without QGL) have moving beds on Z
    state.set_kinematics("corexy");
    REQUIRE(lv_subject_get_int(state.get_printer_bed_moves_subject()) == 1);
}

TEST_CASE("PrinterState: set_kinematics detects cartesian as gantry-moves", "[state][kinematics]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // First set to corexy (bed moves)
    state.set_kinematics("corexy");
    REQUIRE(lv_subject_get_int(state.get_printer_bed_moves_subject()) == 1);

    // Cartesian printers have moving gantry on Z
    state.set_kinematics("cartesian");
    REQUIRE(lv_subject_get_int(state.get_printer_bed_moves_subject()) == 0);
}

TEST_CASE("PrinterState: set_kinematics detects delta as gantry-moves", "[state][kinematics]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Delta printers have moving effector, not bed
    state.set_kinematics("delta");
    REQUIRE(lv_subject_get_int(state.get_printer_bed_moves_subject()) == 0);
}

TEST_CASE("PrinterState: set_kinematics handles kinematics variations", "[state][kinematics]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("corexz - gantry moves on Z (Voron Switchwire)") {
        // CoreXZ has gantry-Z, not bed-Z
        state.set_kinematics("corexz");
        REQUIRE(lv_subject_get_int(state.get_printer_bed_moves_subject()) == 0);
    }

    SECTION("hybrid_corexy - bed moves (contains corexy)") {
        // hybrid_corexy contains "corexy", so bed moves
        state.set_kinematics("hybrid_corexy");
        REQUIRE(lv_subject_get_int(state.get_printer_bed_moves_subject()) == 1);
    }

    SECTION("limited_cartesian - gantry moves (no corexy/corexz)") {
        // limited_cartesian does NOT contain "corexy" or "corexz"
        state.set_kinematics("limited_cartesian");
        REQUIRE(lv_subject_get_int(state.get_printer_bed_moves_subject()) == 0);
    }
}

TEST_CASE("PrinterState: Update kinematics from toolhead notification", "[state][kinematics][ui]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Send notification with toolhead kinematics
    nlohmann::json notification = {
        {"method", "notify_status_update"},
        {"params", {{{"toolhead", {{"kinematics", "cartesian"}, {"homed_axes", "xyz"}}}}, 0.0}}};
    state.update_from_status(notification["params"][0]);

    // Cartesian = gantry moves on Z
    REQUIRE(lv_subject_get_int(state.get_printer_bed_moves_subject()) == 0);
}

TEST_CASE("PrinterState: Kinematics update from cartesian notification",
          "[state][kinematics][ui]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // First set to corexy (bed moves)
    state.set_kinematics("corexy");
    REQUIRE(lv_subject_get_int(state.get_printer_bed_moves_subject()) == 1);

    // Update to cartesian via notification
    nlohmann::json notification = {
        {"method", "notify_status_update"},
        {"params",
         {{{"toolhead", {{"kinematics", "cartesian"}, {"position", {0.0, 0.0, 0.0, 0.0}}}}}, 0.0}}};
    state.update_from_status(notification["params"][0]);

    // Should now be gantry-moves
    REQUIRE(lv_subject_get_int(state.get_printer_bed_moves_subject()) == 0);
}

TEST_CASE("PrinterState: Observer fires when bed_moves changes", "[state][kinematics][observer]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Register observer
    auto observer_cb = [](lv_observer_t* observer, lv_subject_t* subject) {
        int* count_ptr = static_cast<int*>(lv_observer_get_user_data(observer));
        int* value_ptr = count_ptr + 1;

        (*count_ptr)++;
        *value_ptr = lv_subject_get_int(subject);
    };

    int user_data[2] = {0, -1}; // [callback_count, last_value]

    lv_observer_t* observer =
        lv_subject_add_observer(state.get_printer_bed_moves_subject(), observer_cb, user_data);

    // LVGL auto-notifies observers when first added
    REQUIRE(user_data[0] == 1);
    REQUIRE(user_data[1] == 0); // Default: gantry moves

    // Change to corexy (bed moves)
    state.set_kinematics("corexy");
    REQUIRE(user_data[0] == 2);
    REQUIRE(user_data[1] == 1); // Now bed moves

    // Change to cartesian (gantry moves)
    state.set_kinematics("cartesian");
    REQUIRE(user_data[0] == 3);
    REQUIRE(user_data[1] == 0); // Back to gantry moves

    lv_observer_remove(observer);
}

// ============================================================================
// PrintOutcome Tests (set_print_outcome method - FAILING until implemented)
// ============================================================================

TEST_CASE("PrinterState: set_print_outcome updates subject", "[state][print_outcome]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Initial state should be NONE
    auto initial = static_cast<PrintOutcome>(lv_subject_get_int(state.get_print_outcome_subject()));
    REQUIRE(initial == PrintOutcome::NONE);

    // Set to CANCELLED - THIS SHOULD FAIL TO COMPILE (method doesn't exist yet)
    state.set_print_outcome(PrintOutcome::CANCELLED);
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

    auto after = static_cast<PrintOutcome>(lv_subject_get_int(state.get_print_outcome_subject()));
    REQUIRE(after == PrintOutcome::CANCELLED);
}
