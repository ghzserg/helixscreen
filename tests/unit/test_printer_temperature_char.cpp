// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_printer_temperature_char.cpp
 * @brief Characterization tests for PrinterState temperature domain
 *
 * These tests capture the CURRENT behavior of temperature-related subjects
 * in PrinterState before extraction to a dedicated PrinterTemperatureState class.
 *
 * Temperature subjects (4 total):
 * - extruder_temp_ (int, centidegrees - 205.3C stored as 2053)
 * - extruder_target_ (int, centidegrees)
 * - bed_temp_ (int, centidegrees)
 * - bed_target_ (int, centidegrees)
 *
 * Centidegrees format: value * 10 for 0.1C resolution (divide by 10 for display)
 */

#include "../test_helpers/printer_state_test_access.h"
#include "../ui_test_utils.h"
#include "app_globals.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

// ============================================================================
// Observer Notification Tests - Verify observers fire on temperature changes
// ============================================================================

TEST_CASE("Temperature characterization: observer fires when extruder_temp changes",
          "[characterization][temperature][observer]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Observer callback to track notifications
    auto observer_cb = [](lv_observer_t* observer, lv_subject_t* subject) {
        int* count_ptr = static_cast<int*>(lv_observer_get_user_data(observer));
        int* value_ptr = count_ptr + 1;

        (*count_ptr)++;
        *value_ptr = lv_subject_get_int(subject);
    };

    int user_data[2] = {0, -1}; // [callback_count, last_value]

    lv_observer_t* observer =
        lv_subject_add_observer(state.get_extruder_temp_subject(), observer_cb, user_data);

    // LVGL auto-notifies observers when first added (fires immediately with current value)
    REQUIRE(user_data[0] == 1);
    REQUIRE(user_data[1] == 0); // Initial value is 0 centidegrees

    // Update temperature via status update (205.3C = 2053 centidegrees)
    json status = {{"extruder", {{"temperature", 205.3}}}};
    state.update_from_status(status);

    REQUIRE(user_data[0] == 3);
    REQUIRE(user_data[1] == 2053);

    // Update again with different value
    status = {{"extruder", {{"temperature", 210.0}}}};
    state.update_from_status(status);

    REQUIRE(user_data[0] == 5);
    REQUIRE(user_data[1] == 2100);

    lv_observer_remove(observer);
}

TEST_CASE("Temperature characterization: observer fires when extruder_target changes",
          "[characterization][temperature][observer]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    auto observer_cb = [](lv_observer_t* observer, lv_subject_t* subject) {
        int* count_ptr = static_cast<int*>(lv_observer_get_user_data(observer));
        int* value_ptr = count_ptr + 1;

        (*count_ptr)++;
        *value_ptr = lv_subject_get_int(subject);
    };

    int user_data[2] = {0, -1};

    lv_observer_t* observer =
        lv_subject_add_observer(state.get_extruder_target_subject(), observer_cb, user_data);

    // Initial notification
    REQUIRE(user_data[0] == 1);
    REQUIRE(user_data[1] == 0);

    // Update target via status update
    json status = {{"extruder", {{"target", 210.0}}}};
    state.update_from_status(status);

    REQUIRE(user_data[0] == 2);
    REQUIRE(user_data[1] == 2100);

    lv_observer_remove(observer);
}

TEST_CASE("Temperature characterization: observer fires when bed_temp changes",
          "[characterization][temperature][observer]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    auto observer_cb = [](lv_observer_t* observer, lv_subject_t* subject) {
        int* count_ptr = static_cast<int*>(lv_observer_get_user_data(observer));
        int* value_ptr = count_ptr + 1;

        (*count_ptr)++;
        *value_ptr = lv_subject_get_int(subject);
    };

    int user_data[2] = {0, -1};

    lv_observer_t* observer =
        lv_subject_add_observer(state.get_bed_temp_subject(), observer_cb, user_data);

    // Initial notification
    REQUIRE(user_data[0] == 1);
    REQUIRE(user_data[1] == 0);

    // Update bed temp via status update (60.5C = 605 centidegrees)
    json status = {{"heater_bed", {{"temperature", 60.5}}}};
    state.update_from_status(status);

    REQUIRE(user_data[0] == 3);
    REQUIRE(user_data[1] == 605);

    lv_observer_remove(observer);
}

TEST_CASE("Temperature characterization: observer fires when bed_target changes",
          "[characterization][temperature][observer]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    auto observer_cb = [](lv_observer_t* observer, lv_subject_t* subject) {
        int* count_ptr = static_cast<int*>(lv_observer_get_user_data(observer));
        int* value_ptr = count_ptr + 1;

        (*count_ptr)++;
        *value_ptr = lv_subject_get_int(subject);
    };

    int user_data[2] = {0, -1};

    lv_observer_t* observer =
        lv_subject_add_observer(state.get_bed_target_subject(), observer_cb, user_data);

    // Initial notification
    REQUIRE(user_data[0] == 1);
    REQUIRE(user_data[1] == 0);

    // Update bed target via status update
    json status = {{"heater_bed", {{"target", 60.0}}}};
    state.update_from_status(status);

    REQUIRE(user_data[0] == 2);
    REQUIRE(user_data[1] == 600);

    lv_observer_remove(observer);
}

// ============================================================================
// Reset Cycle Tests - Verify subjects survive reset_for_testing cycle
// ============================================================================

TEST_CASE("Temperature characterization: subjects survive reset_for_testing cycle",
          "[characterization][temperature][reset]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Set some temperature values
    json status = {{"extruder", {{"temperature", 200.0}, {"target", 210.0}}},
                   {"heater_bed", {{"temperature", 55.0}, {"target", 60.0}}}};
    state.update_from_status(status);

    // Verify values were set
    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 2000);
    REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 2100);
    REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 550);
    REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 600);

    // Reset and reinitialize
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // After reset, values should be back to defaults (0)
    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 0);

    // Subjects should still be functional after reset
    json new_status = {{"extruder", {{"temperature", 150.0}}}};
    state.update_from_status(new_status);

    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 1500);
}

// ============================================================================
// Independence Tests - Verify temperature subjects are independent
// ============================================================================

TEST_CASE("Temperature characterization: all 4 temp subjects are independent",
          "[characterization][temperature][independence]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // All subjects should start at 0
    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 0);

    SECTION("changing extruder_temp does not affect others") {
        json status = {{"extruder", {{"temperature", 100.0}}}};
        state.update_from_status(status);

        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 1000);
        REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 0);
    }

    SECTION("changing extruder_target does not affect others") {
        json status = {{"extruder", {{"target", 200.0}}}};
        state.update_from_status(status);

        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 2000);
        REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 0);
    }

    SECTION("changing bed_temp does not affect others") {
        json status = {{"heater_bed", {{"temperature", 50.0}}}};
        state.update_from_status(status);

        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 500);
        REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 0);
    }

    SECTION("changing bed_target does not affect others") {
        json status = {{"heater_bed", {{"target", 75.0}}}};
        state.update_from_status(status);

        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 0);
        REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 750);
    }
}

TEST_CASE("Temperature characterization: simultaneous updates work correctly",
          "[characterization][temperature][independence]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Update all four temperatures in a single status message
    json status = {{"extruder", {{"temperature", 205.5}, {"target", 210.0}}},
                   {"heater_bed", {{"temperature", 60.5}, {"target", 65.0}}}};
    state.update_from_status(status);

    // All values should be updated independently
    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 2055);
    REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 2100);
    REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 605);
    REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 650);
}

// ============================================================================
// Centidegree Storage Tests - Verify precision handling
// ============================================================================

TEST_CASE("Temperature characterization: centidegree storage precision",
          "[characterization][temperature][precision]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("0.1C precision is preserved") {
        json status = {{"extruder", {{"temperature", 205.1}}}};
        state.update_from_status(status);
        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 2051);
    }

    SECTION("whole degrees store correctly") {
        json status = {{"extruder", {{"temperature", 200.0}}}};
        state.update_from_status(status);
        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 2000);
    }

    SECTION("zero temperature stores correctly") {
        json status = {{"extruder", {{"temperature", 0.0}}}};
        state.update_from_status(status);
        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 0);
    }

    SECTION("high temperature stores correctly") {
        json status = {{"extruder", {{"temperature", 300.0}}}};
        state.update_from_status(status);
        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 3000);
    }

    SECTION("bed temperature precision") {
        json status = {{"heater_bed", {{"temperature", 60.7}}}};
        state.update_from_status(status);
        REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 607);
    }
}

// ============================================================================
// Observer Independence Tests - Verify observer isolation
// ============================================================================

TEST_CASE("Temperature characterization: observers on different subjects are independent",
          "[characterization][temperature][observer][independence]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    int extruder_count = 0;
    int bed_count = 0;

    auto extruder_cb = [](lv_observer_t* observer, lv_subject_t*) {
        int* count = static_cast<int*>(lv_observer_get_user_data(observer));
        (*count)++;
    };

    auto bed_cb = [](lv_observer_t* observer, lv_subject_t*) {
        int* count = static_cast<int*>(lv_observer_get_user_data(observer));
        (*count)++;
    };

    lv_observer_t* extruder_observer =
        lv_subject_add_observer(state.get_extruder_temp_subject(), extruder_cb, &extruder_count);
    lv_observer_t* bed_observer =
        lv_subject_add_observer(state.get_bed_temp_subject(), bed_cb, &bed_count);

    // Both observers fire on initial add
    REQUIRE(extruder_count == 1);
    REQUIRE(bed_count == 1);

    // Update only extruder temp
    json status = {{"extruder", {{"temperature", 100.0}}}};
    state.update_from_status(status);

    // Only extruder observer should fire
    REQUIRE(extruder_count == 3);
    REQUIRE(bed_count == 1);

    // Update only bed temp
    status = {{"heater_bed", {{"temperature", 50.0}}}};
    state.update_from_status(status);

    // Only bed observer should fire
    REQUIRE(extruder_count == 3);
    REQUIRE(bed_count == 3);

    lv_observer_remove(extruder_observer);
    lv_observer_remove(bed_observer);
}

TEST_CASE("Temperature characterization: multiple observers on same subject all fire",
          "[characterization][temperature][observer]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    int count1 = 0, count2 = 0, count3 = 0;

    auto observer_cb = [](lv_observer_t* observer, lv_subject_t*) {
        int* count = static_cast<int*>(lv_observer_get_user_data(observer));
        (*count)++;
    };

    lv_observer_t* observer1 =
        lv_subject_add_observer(state.get_extruder_temp_subject(), observer_cb, &count1);
    lv_observer_t* observer2 =
        lv_subject_add_observer(state.get_extruder_temp_subject(), observer_cb, &count2);
    lv_observer_t* observer3 =
        lv_subject_add_observer(state.get_extruder_temp_subject(), observer_cb, &count3);

    // All observers fire on initial add
    REQUIRE(count1 == 1);
    REQUIRE(count2 == 1);
    REQUIRE(count3 == 1);

    // Single update should fire all three
    json status = {{"extruder", {{"temperature", 150.0}}}};
    state.update_from_status(status);

    REQUIRE(count1 == 3);
    REQUIRE(count2 == 3);
    REQUIRE(count3 == 3);

    lv_observer_remove(observer1);
    lv_observer_remove(observer2);
    lv_observer_remove(observer3);
}

// ============================================================================
// Partial Update Tests - Document behavior with incomplete data
// ============================================================================

TEST_CASE("Temperature characterization: partial status updates preserve other values",
          "[characterization][temperature][partial]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Set initial values
    json initial = {{"extruder", {{"temperature", 200.0}, {"target", 210.0}}},
                    {"heater_bed", {{"temperature", 60.0}, {"target", 65.0}}}};
    state.update_from_status(initial);

    // Verify initial values
    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 2000);
    REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 2100);
    REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 600);
    REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 650);

    // Update only extruder temp - other values should not change
    json partial = {{"extruder", {{"temperature", 205.0}}}};
    state.update_from_status(partial);

    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 2050);
    // These should be unchanged:
    REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 2100);
    REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 600);
    REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 650);
}

TEST_CASE("Temperature characterization: empty status does not affect values",
          "[characterization][temperature][partial]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Set initial values
    json initial = {{"extruder", {{"temperature", 200.0}}}};
    state.update_from_status(initial);

    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 2000);

    // Empty status should not change anything
    json empty = json::object();
    state.update_from_status(empty);

    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 2000);
}
