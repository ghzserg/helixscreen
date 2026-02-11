// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_printer_fan_char.cpp
 * @brief Characterization tests for PrinterState fan domain
 *
 * These tests capture the CURRENT behavior of fan-related subjects
 * in PrinterState before extraction to a dedicated PrinterFanState class.
 *
 * Static subjects (2 total):
 * - fan_speed_ (int, 0-100% - main part cooling fan speed)
 * - fans_version_ (int, incremented on fan list changes)
 *
 * Dynamic subjects (per-fan):
 * - fan_speed_subjects_[name] (int, 0-100% for each discovered fan)
 *
 * JSON format: {"fan": {"speed": 0.75}} or {"heater_fan hotend_fan": {"speed": 0.5}}
 * - Values are 0.0-1.0 floats, converted to 0-100% integers
 *
 * Fan types:
 * - "fan" -> PART_COOLING (controllable)
 * - "heater_fan *" -> HEATER_FAN (not controllable)
 * - "controller_fan *" -> CONTROLLER_FAN (not controllable)
 * - "fan_generic *" -> GENERIC_FAN (controllable)
 */

#include "../test_helpers/printer_state_test_access.h"
#include "../ui_test_utils.h"
#include "app_globals.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

using json = nlohmann::json;
using helix::FanType;

// ============================================================================
// Initial State Tests - Document default initialization behavior
// ============================================================================

TEST_CASE("Fan characterization: initial values after init", "[characterization][fan][init]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("no per-fan subjects initially") {
        // Before init_fans(), no per-fan subjects exist
        REQUIRE(state.get_fan_speed_subject("fan") == nullptr);
        REQUIRE(state.get_fan_speed_subject("heater_fan hotend_fan") == nullptr);
    }

    SECTION("fans vector is empty initially") {
        REQUIRE(state.get_fans().empty());
    }
}

// ============================================================================
// init_fans() Tests - Fan discovery and per-fan subject creation
// ============================================================================

TEST_CASE("Fan characterization: init_fans creates per-fan subjects",
          "[characterization][fan][init_fans]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    state.init_fans({"fan", "heater_fan hotend_fan", "fan_generic aux_fan"});

    SECTION("per-fan subjects created for each fan") {
        REQUIRE(state.get_fan_speed_subject("fan") != nullptr);
        REQUIRE(state.get_fan_speed_subject("heater_fan hotend_fan") != nullptr);
        REQUIRE(state.get_fan_speed_subject("fan_generic aux_fan") != nullptr);
    }

    SECTION("unknown fan returns nullptr") {
        REQUIRE(state.get_fan_speed_subject("nonexistent") == nullptr);
        REQUIRE(state.get_fan_speed_subject("heater_fan other_fan") == nullptr);
    }

    SECTION("fans_version increments on init_fans") {
        int initial_version = lv_subject_get_int(state.get_fans_version_subject());
        // First init_fans already bumped it, so initial_version should be 1
        REQUIRE(initial_version == 1);
    }
}

TEST_CASE("Fan characterization: init_fans populates fans vector",
          "[characterization][fan][init_fans]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    state.init_fans({"fan", "heater_fan hotend_fan", "controller_fan mcu_fan", "fan_generic aux"});

    SECTION("fans vector has correct size") {
        REQUIRE(state.get_fans().size() == 4);
    }

    SECTION("FanInfo object_name matches input") {
        const auto& fans = state.get_fans();
        REQUIRE(fans[0].object_name == "fan");
        REQUIRE(fans[1].object_name == "heater_fan hotend_fan");
        REQUIRE(fans[2].object_name == "controller_fan mcu_fan");
        REQUIRE(fans[3].object_name == "fan_generic aux");
    }

    SECTION("FanInfo speed_percent initializes to 0") {
        const auto& fans = state.get_fans();
        for (const auto& fan : fans) {
            REQUIRE(fan.speed_percent == 0);
        }
    }
}

// ============================================================================
// Fan Type Classification Tests - Verify type determination from object name
// ============================================================================

TEST_CASE("Fan characterization: fan type classification", "[characterization][fan][type]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    state.init_fans({"fan", "heater_fan hotend_fan", "controller_fan mcu_fan", "fan_generic aux"});

    const auto& fans = state.get_fans();

    SECTION("\"fan\" is PART_COOLING type") {
        REQUIRE(fans[0].type == FanType::PART_COOLING);
    }

    SECTION("\"heater_fan *\" is HEATER_FAN type") {
        REQUIRE(fans[1].type == FanType::HEATER_FAN);
    }

    SECTION("\"controller_fan *\" is CONTROLLER_FAN type") {
        REQUIRE(fans[2].type == FanType::CONTROLLER_FAN);
    }

    SECTION("\"fan_generic *\" is GENERIC_FAN type") {
        REQUIRE(fans[3].type == FanType::GENERIC_FAN);
    }
}

TEST_CASE("Fan characterization: fan controllability", "[characterization][fan][controllable]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    state.init_fans({"fan", "heater_fan hotend_fan", "controller_fan mcu_fan", "fan_generic aux"});

    const auto& fans = state.get_fans();

    SECTION("PART_COOLING is controllable") {
        REQUIRE(fans[0].is_controllable == true);
    }

    SECTION("HEATER_FAN is not controllable") {
        REQUIRE(fans[1].is_controllable == false);
    }

    SECTION("CONTROLLER_FAN is not controllable") {
        REQUIRE(fans[2].is_controllable == false);
    }

    SECTION("GENERIC_FAN is controllable") {
        REQUIRE(fans[3].is_controllable == true);
    }
}

// ============================================================================
// Fan Speed Update Tests - JSON parsing and subject updates
// ============================================================================

TEST_CASE("Fan characterization: main fan speed updates from JSON",
          "[characterization][fan][update]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Must init fans for multi-fan tracking to work
    state.init_fans({"fan"});

    SECTION("full speed (1.0 -> 100%)") {
        json status = {{"fan", {{"speed", 1.0}}}};
        state.update_from_status(status);

        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 100);
    }

    SECTION("half speed (0.5 -> 50%)") {
        json status = {{"fan", {{"speed", 0.5}}}};
        state.update_from_status(status);

        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 50);
    }

    SECTION("off (0.0 -> 0%)") {
        // First turn on
        json on_status = {{"fan", {{"speed", 1.0}}}};
        state.update_from_status(on_status);
        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 100);

        // Then turn off
        json off_status = {{"fan", {{"speed", 0.0}}}};
        state.update_from_status(off_status);
        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 0);
    }

    SECTION("75% speed (0.75 -> 75%)") {
        json status = {{"fan", {{"speed", 0.75}}}};
        state.update_from_status(status);

        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 75);
    }

    SECTION("25% speed (0.25 -> 25%)") {
        json status = {{"fan", {{"speed", 0.25}}}};
        state.update_from_status(status);

        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 25);
    }
}

TEST_CASE("Fan characterization: per-fan speed updates from JSON",
          "[characterization][fan][update][per-fan]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    state.init_fans({"fan", "heater_fan hotend_fan", "fan_generic aux"});

    SECTION("main fan update affects per-fan subject") {
        json status = {{"fan", {{"speed", 0.8}}}};
        state.update_from_status(status);

        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject("fan")) == 80);
    }

    SECTION("heater_fan update affects its per-fan subject") {
        json status = {{"heater_fan hotend_fan", {{"speed", 0.6}}}};
        state.update_from_status(status);

        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject("heater_fan hotend_fan")) == 60);
    }

    SECTION("fan_generic update affects its per-fan subject") {
        json status = {{"fan_generic aux", {{"speed", 0.4}}}};
        state.update_from_status(status);

        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject("fan_generic aux")) == 40);
    }

    SECTION("updates for different fans are independent") {
        json status1 = {{"fan", {{"speed", 0.9}}}};
        state.update_from_status(status1);

        json status2 = {{"heater_fan hotend_fan", {{"speed", 0.3}}}};
        state.update_from_status(status2);

        // Both should retain their values
        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject("fan")) == 90);
        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject("heater_fan hotend_fan")) == 30);
        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject("fan_generic aux")) == 0);
    }
}

TEST_CASE("Fan characterization: FanInfo speed_percent updates",
          "[characterization][fan][update][faninfo]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    state.init_fans({"fan", "heater_fan hotend_fan"});

    SECTION("FanInfo speed_percent updates with JSON") {
        json status = {{"fan", {{"speed", 0.65}}}};
        state.update_from_status(status);

        const auto& fans = state.get_fans();
        REQUIRE(fans[0].speed_percent == 65);
    }

    SECTION("FanInfo speed_percent updates for heater_fan") {
        json status = {{"heater_fan hotend_fan", {{"speed", 0.45}}}};
        state.update_from_status(status);

        const auto& fans = state.get_fans();
        REQUIRE(fans[1].speed_percent == 45);
    }
}

// ============================================================================
// Observer Notification Tests - Verify observers fire on fan changes
// ============================================================================

TEST_CASE("Fan characterization: observer fires when fan_speed changes",
          "[characterization][fan][observer]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);
    state.init_fans({"fan"});

    auto observer_cb = [](lv_observer_t* observer, lv_subject_t* subject) {
        int* count_ptr = static_cast<int*>(lv_observer_get_user_data(observer));
        int* value_ptr = count_ptr + 1;

        (*count_ptr)++;
        *value_ptr = lv_subject_get_int(subject);
    };

    int user_data[2] = {0, -1}; // [callback_count, last_value]

    lv_observer_t* observer =
        lv_subject_add_observer(state.get_fan_speed_subject(), observer_cb, user_data);

    // LVGL auto-notifies observers when first added
    REQUIRE(user_data[0] == 1);
    REQUIRE(user_data[1] == 0); // Initial value is 0

    // Update fan speed
    json status = {{"fan", {{"speed", 0.75}}}};
    state.update_from_status(status);

    REQUIRE(user_data[0] >= 2); // At least one more notification
    REQUIRE(user_data[1] == 75);

    lv_observer_remove(observer);
}

TEST_CASE("Fan characterization: observer fires on per-fan subject change",
          "[characterization][fan][observer][per-fan]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);
    state.init_fans({"heater_fan hotend_fan"});

    auto observer_cb = [](lv_observer_t* observer, lv_subject_t* subject) {
        int* count_ptr = static_cast<int*>(lv_observer_get_user_data(observer));
        int* value_ptr = count_ptr + 1;

        (*count_ptr)++;
        *value_ptr = lv_subject_get_int(subject);
    };

    int user_data[2] = {0, -1};

    lv_subject_t* per_fan_subject = state.get_fan_speed_subject("heater_fan hotend_fan");
    REQUIRE(per_fan_subject != nullptr);

    lv_observer_t* observer = lv_subject_add_observer(per_fan_subject, observer_cb, user_data);

    // Initial notification on add
    REQUIRE(user_data[0] == 1);
    REQUIRE(user_data[1] == 0);

    // Update fan speed
    json status = {{"heater_fan hotend_fan", {{"speed", 0.5}}}};
    state.update_from_status(status);

    REQUIRE(user_data[0] >= 2);
    REQUIRE(user_data[1] == 50);

    lv_observer_remove(observer);
}

TEST_CASE("Fan characterization: fans_version observer fires on init_fans",
          "[characterization][fan][observer][version]") {
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
        lv_subject_add_observer(state.get_fans_version_subject(), observer_cb, user_data);

    // Initial notification
    REQUIRE(user_data[0] == 1);
    REQUIRE(user_data[1] == 0);

    // init_fans should bump version
    state.init_fans({"fan"});

    REQUIRE(user_data[0] >= 2);
    REQUIRE(user_data[1] == 1);

    // Calling init_fans again should bump version again
    state.init_fans({"fan", "heater_fan hotend"});

    REQUIRE(user_data[0] >= 3);
    REQUIRE(user_data[1] == 2);

    lv_observer_remove(observer);
}

// ============================================================================
// Update Ignored Tests - Updates without init_fans or for unknown fans
// ============================================================================

TEST_CASE("Fan characterization: updates before init_fans", "[characterization][fan][no_init]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Do NOT call init_fans

    SECTION("main fan subject still updates (static subject)") {
        json status = {{"fan", {{"speed", 0.5}}}};
        state.update_from_status(status);

        // The static fan_speed_ subject should still update
        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 50);
    }

    SECTION("per-fan subject returns nullptr without init_fans") {
        // Without init_fans, no per-fan subjects exist
        REQUIRE(state.get_fan_speed_subject("fan") == nullptr);
    }
}

TEST_CASE("Fan characterization: update for undiscovered fan is ignored",
          "[characterization][fan][unknown]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Only init some fans
    state.init_fans({"fan"});

    SECTION("update for unknown heater_fan does not create subject") {
        json status = {{"heater_fan hotend_fan", {{"speed", 0.5}}}};
        state.update_from_status(status);

        // Should not create a subject for unknown fan
        REQUIRE(state.get_fan_speed_subject("heater_fan hotend_fan") == nullptr);
    }

    SECTION("known fan still updates correctly") {
        json status = {{"fan", {{"speed", 0.75}}}};
        state.update_from_status(status);

        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject("fan")) == 75);
    }
}

// ============================================================================
// Reset Cycle Tests - Verify behavior across reset_for_testing cycles
// ============================================================================

TEST_CASE("Fan characterization: per-fan subjects cleared on reset",
          "[characterization][fan][reset]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    state.init_fans({"fan", "heater_fan hotend_fan"});

    // Verify subjects exist
    REQUIRE(state.get_fan_speed_subject("fan") != nullptr);
    REQUIRE(state.get_fan_speed_subject("heater_fan hotend_fan") != nullptr);

    // Update values
    json status = {{"fan", {{"speed", 0.8}}}};
    state.update_from_status(status);
    REQUIRE(lv_subject_get_int(state.get_fan_speed_subject("fan")) == 80);

    // Reset
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Per-fan subjects should be cleared
    REQUIRE(state.get_fan_speed_subject("fan") == nullptr);
    REQUIRE(state.get_fan_speed_subject("heater_fan hotend_fan") == nullptr);

    // NOTE: Current behavior - fans_ vector is NOT cleared by reset_for_testing()
    // Only fan_speed_subjects_ map is cleared. This documents the current behavior.
    // If fans_ should be cleared, that would be a refactor change, not captured here.
    REQUIRE(state.get_fans().size() == 2); // Fans vector persists
}

TEST_CASE("Fan characterization: static subjects reset to defaults",
          "[characterization][fan][reset]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    state.init_fans({"fan"});

    // Set values
    json status = {{"fan", {{"speed", 0.75}}}};
    state.update_from_status(status);
    REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 75);

    int version_before = lv_subject_get_int(state.get_fans_version_subject());
    REQUIRE(version_before == 1);

    // Reset
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Static subjects should be back to defaults
    REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_fans_version_subject()) == 0);
}

TEST_CASE("Fan characterization: reinitializing fans replaces previous subjects",
          "[characterization][fan][reinit]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // First init
    state.init_fans({"fan"});
    lv_subject_t* fan_subject_v1 = state.get_fan_speed_subject("fan");
    REQUIRE(fan_subject_v1 != nullptr);

    json status = {{"fan", {{"speed", 0.5}}}};
    state.update_from_status(status);
    REQUIRE(lv_subject_get_int(fan_subject_v1) == 50);

    // Reinit with different fans
    state.init_fans({"heater_fan hotend_fan"});

    // Old fan subject should be gone
    REQUIRE(state.get_fan_speed_subject("fan") == nullptr);

    // New fan subject should exist
    REQUIRE(state.get_fan_speed_subject("heater_fan hotend_fan") != nullptr);

    // fans_version should have incremented
    REQUIRE(lv_subject_get_int(state.get_fans_version_subject()) == 2);
}

// ============================================================================
// Independence Tests - Verify fan updates don't affect other subjects
// ============================================================================

TEST_CASE("Fan characterization: fan update does not affect non-fan subjects",
          "[characterization][fan][independence]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);
    state.init_fans({"fan"});

    // Set some non-fan values first
    json initial = {{"toolhead", {{"position", {100.0, 200.0, 30.0}}}}};
    state.update_from_status(initial);

    REQUIRE(lv_subject_get_int(state.get_position_x_subject()) == 10000); // centimm

    // Now update fan
    json fan_update = {{"fan", {{"speed", 0.75}}}};
    state.update_from_status(fan_update);

    // Fan value should be updated
    REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 75);

    // Position should be unchanged (in centimm)
    REQUIRE(lv_subject_get_int(state.get_position_x_subject()) == 10000);
}

TEST_CASE("Fan characterization: non-fan update does not affect fan subjects",
          "[characterization][fan][independence]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);
    state.init_fans({"fan"});

    // Set fan value first
    json fan_status = {{"fan", {{"speed", 0.8}}}};
    state.update_from_status(fan_status);

    REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 80);
    REQUIRE(lv_subject_get_int(state.get_fan_speed_subject("fan")) == 80);

    // Now update position (non-fan)
    json position_update = {{"toolhead", {{"position", {50.0, 75.0, 10.0}}}}};
    state.update_from_status(position_update);

    // Fan values should be unchanged
    REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 80);
    REQUIRE(lv_subject_get_int(state.get_fan_speed_subject("fan")) == 80);
}

// ============================================================================
// Multiple Observer Tests - Verify observer isolation and independence
// ============================================================================

TEST_CASE("Fan characterization: observers on different fan subjects are independent",
          "[characterization][fan][observer][independence]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);
    state.init_fans({"fan", "heater_fan hotend_fan"});

    int main_count = 0;
    int per_fan_count = 0;

    auto observer_cb = [](lv_observer_t* observer, lv_subject_t*) {
        int* count = static_cast<int*>(lv_observer_get_user_data(observer));
        (*count)++;
    };

    lv_observer_t* main_observer =
        lv_subject_add_observer(state.get_fan_speed_subject(), observer_cb, &main_count);
    lv_observer_t* per_fan_observer =
        lv_subject_add_observer(state.get_fan_speed_subject("fan"), observer_cb, &per_fan_count);

    // Both observers fire on initial add
    REQUIRE(main_count == 1);
    REQUIRE(per_fan_count == 1);

    // Update main fan
    json status = {{"fan", {{"speed", 0.5}}}};
    state.update_from_status(status);

    // Both should have received notifications (main fan update affects both subjects)
    REQUIRE(main_count >= 2);
    REQUIRE(per_fan_count >= 2);

    lv_observer_remove(main_observer);
    lv_observer_remove(per_fan_observer);
}

TEST_CASE("Fan characterization: multiple observers on same fan subject all fire",
          "[characterization][fan][observer]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);
    state.init_fans({"fan"});

    int count1 = 0, count2 = 0, count3 = 0;

    auto observer_cb = [](lv_observer_t* observer, lv_subject_t*) {
        int* count = static_cast<int*>(lv_observer_get_user_data(observer));
        (*count)++;
    };

    lv_observer_t* observer1 =
        lv_subject_add_observer(state.get_fan_speed_subject(), observer_cb, &count1);
    lv_observer_t* observer2 =
        lv_subject_add_observer(state.get_fan_speed_subject(), observer_cb, &count2);
    lv_observer_t* observer3 =
        lv_subject_add_observer(state.get_fan_speed_subject(), observer_cb, &count3);

    // All observers fire on initial add
    REQUIRE(count1 == 1);
    REQUIRE(count2 == 1);
    REQUIRE(count3 == 1);

    // Single update should fire all three
    json status = {{"fan", {{"speed", 0.5}}}};
    state.update_from_status(status);

    REQUIRE(count1 >= 2);
    REQUIRE(count2 >= 2);
    REQUIRE(count3 >= 2);

    lv_observer_remove(observer1);
    lv_observer_remove(observer2);
    lv_observer_remove(observer3);
}

// ============================================================================
// Edge Cases - Boundary values and unusual inputs
// ============================================================================

TEST_CASE("Fan characterization: edge cases and boundary values", "[characterization][fan][edge]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);
    state.init_fans({"fan"});

    SECTION("very small speed values") {
        json status = {{"fan", {{"speed", 0.01}}}};
        state.update_from_status(status);

        // 0.01 * 100 = 1%
        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 1);
    }

    SECTION("speed value exactly 0.5") {
        json status = {{"fan", {{"speed", 0.5}}}};
        state.update_from_status(status);

        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 50);
    }

    SECTION("speed value exactly 1.0") {
        json status = {{"fan", {{"speed", 1.0}}}};
        state.update_from_status(status);

        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 100);
    }

    SECTION("speed value slightly above 1.0 (clamping behavior)") {
        json status = {{"fan", {{"speed", 1.01}}}};
        state.update_from_status(status);

        // Depends on implementation - typically clamped to 100
        int speed = lv_subject_get_int(state.get_fan_speed_subject());
        REQUIRE(speed <= 101); // Allow for 101 if not clamped
    }

    SECTION("missing speed field is handled gracefully") {
        json status = {{"fan", {{"rpm", 5000}}}};
        state.update_from_status(status);

        // Value should remain at initial 0 (no crash)
        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 0);
    }

    SECTION("non-number speed field is handled gracefully") {
        json status = {{"fan", {{"speed", "fast"}}}};
        state.update_from_status(status);

        // Value should remain at initial 0 (no crash)
        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 0);
    }
}

TEST_CASE("Fan characterization: empty init_fans", "[characterization][fan][edge]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("init_fans with empty vector") {
        state.init_fans({});

        REQUIRE(state.get_fans().empty());
        // Version should still increment
        REQUIRE(lv_subject_get_int(state.get_fans_version_subject()) == 1);
    }
}

TEST_CASE("Fan characterization: fan with unusual name format", "[characterization][fan][edge]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("fan_generic with underscore in name") {
        state.init_fans({"fan_generic aux_cooling_fan"});

        REQUIRE(state.get_fan_speed_subject("fan_generic aux_cooling_fan") != nullptr);
        REQUIRE(state.get_fans()[0].type == FanType::GENERIC_FAN);
        REQUIRE(state.get_fans()[0].is_controllable == true);
    }

    SECTION("heater_fan with multiple words") {
        state.init_fans({"heater_fan my_custom_hotend_fan"});

        REQUIRE(state.get_fan_speed_subject("heater_fan my_custom_hotend_fan") != nullptr);
        REQUIRE(state.get_fans()[0].type == FanType::HEATER_FAN);
        REQUIRE(state.get_fans()[0].is_controllable == false);
    }
}

// ============================================================================
// FanRoleConfig Tests - Configured fan role classification and naming
// ============================================================================

TEST_CASE("Fan role config: configured part fan classified as PART_COOLING", "[fan][role_config]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    helix::FanRoleConfig roles;
    roles.part_fan = "fan_generic Fanm106";

    state.init_fans(
        {"fan", "fan_generic Fanm106", "heater_fan heat_fan", "fan_generic chamber_fan"}, roles);

    const auto& fans = state.get_fans();

    SECTION("canonical 'fan' is still PART_COOLING") {
        REQUIRE(fans[0].type == helix::FanType::PART_COOLING);
    }

    SECTION("configured part fan is classified as PART_COOLING") {
        REQUIRE(fans[1].type == helix::FanType::PART_COOLING);
        REQUIRE(fans[1].is_controllable == true);
    }

    SECTION("other fans retain normal classification") {
        REQUIRE(fans[2].type == helix::FanType::HEATER_FAN);
        REQUIRE(fans[3].type == helix::FanType::GENERIC_FAN);
    }
}

TEST_CASE("Fan role config: display name overrides from configured roles",
          "[fan][role_config][display_name]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    helix::FanRoleConfig roles;
    roles.part_fan = "fan_generic Fanm106";
    roles.hotend_fan = "heater_fan heat_fan";
    roles.chamber_fan = "fan_generic chamber_fan";
    roles.exhaust_fan = "fan_generic external_fan";

    state.init_fans({"fan", "fan_generic Fanm106", "heater_fan heat_fan", "fan_generic chamber_fan",
                     "fan_generic external_fan", "controller_fan driver_fan"},
                    roles);

    const auto& fans = state.get_fans();

    SECTION("canonical 'fan' uses direct mapping, not role override") {
        // "fan" has a direct mapping to "Part Cooling Fan" in device_display_name
        REQUIRE(fans[0].display_name == "Part Cooling Fan");
    }

    SECTION("configured part fan gets 'Part Fan' display name") {
        REQUIRE(fans[1].display_name == "Part Fan");
    }

    SECTION("configured hotend fan gets 'Hotend Fan' display name") {
        REQUIRE(fans[2].display_name == "Hotend Fan");
    }

    SECTION("configured chamber fan gets 'Chamber Fan' display name") {
        REQUIRE(fans[3].display_name == "Chamber Fan");
    }

    SECTION("configured exhaust fan gets 'Exhaust Fan' display name") {
        REQUIRE(fans[4].display_name == "Exhaust Fan");
    }

    SECTION("unconfigured fan uses auto-generated display name") {
        // "controller_fan driver_fan" not in any role config -> auto-generated
        REQUIRE(fans[5].display_name == "Driver Fan");
    }
}

TEST_CASE("Fan role config: empty roles uses default behavior", "[fan][role_config]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Default-constructed FanRoleConfig has empty strings
    helix::FanRoleConfig roles;

    state.init_fans({"fan", "fan_generic Fanm106"}, roles);

    const auto& fans = state.get_fans();

    SECTION("without role config, fan_generic is GENERIC_FAN") {
        REQUIRE(fans[1].type == helix::FanType::GENERIC_FAN);
    }

    SECTION("without role config, fan_generic gets auto-generated name") {
        REQUIRE(fans[1].display_name == "Fanm106 Fan");
    }
}

TEST_CASE("Fan role config: configured part fan updates hero slider subject",
          "[fan][role_config][update]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    helix::FanRoleConfig roles;
    roles.part_fan = "fan_generic Fanm106";

    state.init_fans({"fan_generic Fanm106"}, roles);

    SECTION("configured part fan speed updates main fan_speed subject") {
        json status = {{"fan_generic Fanm106", {{"speed", 0.69}}}};
        state.update_from_status(status);

        // Main hero slider subject should reflect configured part fan speed
        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 69);
    }

    SECTION("per-fan subject also updates") {
        json status = {{"fan_generic Fanm106", {{"speed", 0.42}}}};
        state.update_from_status(status);

        REQUIRE(lv_subject_get_int(state.get_fan_speed_subject("fan_generic Fanm106")) == 42);
    }
}

TEST_CASE("Fan role config: canonical 'fan' part_fan does not create redundant override",
          "[fan][role_config]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // When the configured part fan IS the canonical "fan", don't add a role override
    // (it already has a direct mapping to "Part Cooling Fan")
    helix::FanRoleConfig roles;
    roles.part_fan = "fan";

    state.init_fans({"fan"}, roles);

    const auto& fans = state.get_fans();

    SECTION("canonical fan keeps direct mapping name") {
        REQUIRE(fans[0].display_name == "Part Cooling Fan");
    }

    SECTION("still classified as PART_COOLING") {
        REQUIRE(fans[0].type == helix::FanType::PART_COOLING);
    }
}
