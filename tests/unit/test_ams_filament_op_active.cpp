// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_filament_op_active.cpp
 * @brief Tests for AmsState::is_filament_operation_active()
 *
 * Verifies that toast suppression only activates during states where filament
 * is physically moving past sensors (LOADING, UNLOADING, SELECTING).
 * Stationary states (HEATING, CUTTING, PURGING, etc.) must NOT suppress
 * because a sensor change in those states indicates a real problem.
 */

#include "../lvgl_test_fixture.h"
#include "ams_state.h"
#include "ams_types.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::printer;

TEST_CASE_METHOD(LVGLTestFixture, "AmsState::is_filament_operation_active",
                 "[ams][toast_suppression]") {
    auto& ams = AmsState::instance();
    ams.init_subjects(false);

    SECTION("IDLE is not active") {
        ams.set_action(AmsAction::IDLE);
        CHECK_FALSE(ams.is_filament_operation_active());
    }

    SECTION("LOADING is active - filament moves past sensors") {
        ams.set_action(AmsAction::LOADING);
        CHECK(ams.is_filament_operation_active());
    }

    SECTION("UNLOADING is active - filament moves past sensors") {
        ams.set_action(AmsAction::UNLOADING);
        CHECK(ams.is_filament_operation_active());
    }

    SECTION("SELECTING is active - filament may move during selection") {
        ams.set_action(AmsAction::SELECTING);
        CHECK(ams.is_filament_operation_active());
    }

    SECTION("HEATING is NOT active - filament is stationary") {
        ams.set_action(AmsAction::HEATING);
        CHECK_FALSE(ams.is_filament_operation_active());
    }

    SECTION("FORMING_TIP is NOT active - filament is stationary") {
        ams.set_action(AmsAction::FORMING_TIP);
        CHECK_FALSE(ams.is_filament_operation_active());
    }

    SECTION("CUTTING is NOT active - filament is stationary") {
        ams.set_action(AmsAction::CUTTING);
        CHECK_FALSE(ams.is_filament_operation_active());
    }

    SECTION("PURGING is NOT active - filament is stationary") {
        ams.set_action(AmsAction::PURGING);
        CHECK_FALSE(ams.is_filament_operation_active());
    }

    SECTION("ERROR is NOT active") {
        ams.set_action(AmsAction::ERROR);
        CHECK_FALSE(ams.is_filament_operation_active());
    }

    SECTION("PAUSED is NOT active") {
        ams.set_action(AmsAction::PAUSED);
        CHECK_FALSE(ams.is_filament_operation_active());
    }

    SECTION("CHECKING is NOT active") {
        ams.set_action(AmsAction::CHECKING);
        CHECK_FALSE(ams.is_filament_operation_active());
    }

    SECTION("RESETTING is NOT active - no filament movement") {
        ams.set_action(AmsAction::RESETTING);
        CHECK_FALSE(ams.is_filament_operation_active());
    }

    // Reset to idle for clean state
    ams.set_action(AmsAction::IDLE);
}
