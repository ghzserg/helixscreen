// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../ui_test_utils.h"
#include "printer_temperature_state.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// Test helper for accessing private members
class PrinterTemperatureStateTestAccess {
  public:
    static void reset(PrinterTemperatureState& state) {
        state.deinit_subjects();
    }
};

TEST_CASE("PrinterTemperatureState: active extruder defaults to 'extruder'",
          "[core][temperature][active-extruder]") {
    lv_init_safe();
    PrinterTemperatureState state;
    state.init_subjects(false);

    REQUIRE(state.active_extruder_name() == "extruder");

    PrinterTemperatureStateTestAccess::reset(state);
}

TEST_CASE("PrinterTemperatureState: set_active_extruder changes active name",
          "[core][temperature][active-extruder]") {
    lv_init_safe();
    PrinterTemperatureState state;
    state.init_subjects(false);
    state.init_extruders({"extruder", "extruder1"});

    state.set_active_extruder("extruder1");
    REQUIRE(state.active_extruder_name() == "extruder1");

    PrinterTemperatureStateTestAccess::reset(state);
}

TEST_CASE("PrinterTemperatureState: set_active_extruder syncs current values",
          "[core][temperature][active-extruder]") {
    lv_init_safe();
    PrinterTemperatureState state;
    state.init_subjects(false);
    state.init_extruders({"extruder", "extruder1"});

    // Set extruder1's temperature via per-extruder subjects
    nlohmann::json status = {{"extruder1", {{"temperature", 220.5}, {"target", 230.0}}}};
    state.update_from_status(status);

    // Active subjects should still show "extruder" values (0) since that's the default active
    REQUIRE(lv_subject_get_int(state.get_active_extruder_temp_subject()) == 0);

    // Now switch active to extruder1
    state.set_active_extruder("extruder1");

    // Active subjects should now mirror extruder1's values
    REQUIRE(lv_subject_get_int(state.get_active_extruder_temp_subject()) == 2205);
    REQUIRE(lv_subject_get_int(state.get_active_extruder_target_subject()) == 2300);

    PrinterTemperatureStateTestAccess::reset(state);
}

TEST_CASE("PrinterTemperatureState: update_from_status updates active subjects",
          "[core][temperature][active-extruder]") {
    lv_init_safe();
    PrinterTemperatureState state;
    state.init_subjects(false);
    state.init_extruders({"extruder", "extruder1"});
    state.set_active_extruder("extruder1");

    // Update with extruder1 data - should reflect in active subjects
    nlohmann::json status = {{"extruder1", {{"temperature", 195.3}, {"target", 200.0}}}};
    state.update_from_status(status);

    REQUIRE(lv_subject_get_int(state.get_active_extruder_temp_subject()) == 1953);
    REQUIRE(lv_subject_get_int(state.get_active_extruder_target_subject()) == 2000);

    // extruder data should NOT update active (since active is extruder1)
    nlohmann::json status2 = {{"extruder", {{"temperature", 100.0}, {"target", 110.0}}}};
    state.update_from_status(status2);

    // Active subjects should still show extruder1's values
    REQUIRE(lv_subject_get_int(state.get_active_extruder_temp_subject()) == 1953);
    REQUIRE(lv_subject_get_int(state.get_active_extruder_target_subject()) == 2000);

    PrinterTemperatureStateTestAccess::reset(state);
}

TEST_CASE("PrinterTemperatureState: unknown extruder name stays on previous",
          "[core][temperature][active-extruder]") {
    lv_init_safe();
    PrinterTemperatureState state;
    state.init_subjects(false);
    state.init_extruders({"extruder", "extruder1"});

    state.set_active_extruder("extruder1");
    REQUIRE(state.active_extruder_name() == "extruder1");

    // Unknown name should not change active
    state.set_active_extruder("extruder99");
    REQUIRE(state.active_extruder_name() == "extruder1");

    PrinterTemperatureStateTestAccess::reset(state);
}

TEST_CASE("PrinterTemperatureState: default active works with single extruder",
          "[core][temperature][active-extruder]") {
    lv_init_safe();
    PrinterTemperatureState state;
    state.init_subjects(false);
    state.init_extruders({"extruder"});

    // Should default to "extruder" and update active subjects
    nlohmann::json status = {{"extruder", {{"temperature", 205.0}, {"target", 210.0}}}};
    state.update_from_status(status);

    REQUIRE(lv_subject_get_int(state.get_active_extruder_temp_subject()) == 2050);
    REQUIRE(lv_subject_get_int(state.get_active_extruder_target_subject()) == 2100);

    PrinterTemperatureStateTestAccess::reset(state);
}
