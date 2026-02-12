// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_emergency_stop.h"

#include "../lvgl_test_fixture.h"
#include "../lvgl_ui_test_fixture.h"

#include <spdlog/spdlog.h>

#include "../catch_amalgamated.hpp"

// ============================================================================
// Suppression logic tests (lightweight, just need LVGL tick)
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Recovery suppression - basic timing", "[recovery][suppress]") {
    auto& estop = EmergencyStopOverlay::instance();

    SECTION("not suppressed by default") {
        REQUIRE_FALSE(estop.is_recovery_suppressed());
    }

    SECTION("suppressed after calling suppress_recovery_dialog") {
        estop.suppress_recovery_dialog(5000);
        REQUIRE(estop.is_recovery_suppressed());
    }

    SECTION("suppression expires after duration") {
        estop.suppress_recovery_dialog(10); // 10ms
        REQUIRE(estop.is_recovery_suppressed());

        // Advance LVGL tick past suppression window
        // Only need tick advancement (not timer processing) for time-based check
        lv_tick_inc(50);
        REQUIRE_FALSE(estop.is_recovery_suppressed());
    }
}

// ============================================================================
// Recovery reason enum coverage
// ============================================================================

TEST_CASE("RecoveryReason enum values", "[recovery]") {
    REQUIRE(static_cast<int>(RecoveryReason::NONE) == 0);
    REQUIRE(RecoveryReason::SHUTDOWN != RecoveryReason::DISCONNECTED);
    REQUIRE(RecoveryReason::SHUTDOWN != RecoveryReason::NONE);
    REQUIRE(RecoveryReason::DISCONNECTED != RecoveryReason::NONE);
}

// ============================================================================
// Full integration tests (need XML components, subjects, PrinterState)
// ============================================================================

TEST_CASE_METHOD(LVGLUITestFixture, "Unified recovery dialog - SHUTDOWN shows dialog",
                 "[recovery][integration]") {
    auto& estop = EmergencyStopOverlay::instance();

    // Trigger SHUTDOWN via show_recovery_for (bypasses observer, tests the method directly)
    estop.show_recovery_for(RecoveryReason::SHUTDOWN);
    process_lvgl(50); // Allow async callback to execute

    // Dialog should be visible - find it by the backdrop name
    lv_obj_t* dialog = lv_obj_find_by_name(lv_screen_active(), "klipper_recovery_card");
    REQUIRE(dialog != nullptr);

    // Title should say "Printer Shutdown"
    lv_obj_t* title = lv_obj_find_by_name(dialog, "recovery_title");
    REQUIRE(title != nullptr);
    REQUIRE(std::string(lv_label_get_text(title)).find("Shutdown") != std::string::npos);
}

TEST_CASE_METHOD(LVGLUITestFixture, "Unified recovery dialog - DISCONNECTED shows dialog",
                 "[recovery][integration]") {
    auto& estop = EmergencyStopOverlay::instance();

    estop.show_recovery_for(RecoveryReason::DISCONNECTED);
    process_lvgl(50);

    lv_obj_t* dialog = lv_obj_find_by_name(lv_screen_active(), "klipper_recovery_card");
    REQUIRE(dialog != nullptr);

    // Title should say "Disconnected"
    lv_obj_t* title = lv_obj_find_by_name(dialog, "recovery_title");
    REQUIRE(title != nullptr);
    REQUIRE(std::string(lv_label_get_text(title)).find("Disconnected") != std::string::npos);
}

TEST_CASE_METHOD(LVGLUITestFixture, "Unified recovery dialog - deduplication",
                 "[recovery][integration]") {
    auto& estop = EmergencyStopOverlay::instance();

    // Show SHUTDOWN first
    estop.show_recovery_for(RecoveryReason::SHUTDOWN);
    process_lvgl(50);

    lv_obj_t* first_dialog = lv_obj_find_by_name(lv_screen_active(), "klipper_recovery_card");
    REQUIRE(first_dialog != nullptr);

    // Try DISCONNECTED - should NOT create a second dialog
    estop.show_recovery_for(RecoveryReason::DISCONNECTED);
    process_lvgl(50);

    // Count recovery dialogs - should only be one (search recursively)
    int count = 0;
    uint32_t child_cnt = lv_obj_get_child_count(lv_screen_active());
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* backdrop = lv_obj_get_child(lv_screen_active(), i);
        if (!backdrop)
            continue;
        // Modal backdrops are direct children of screen; check if they contain our dialog
        lv_obj_t* card = lv_obj_find_by_name(backdrop, "klipper_recovery_card");
        if (card) {
            count++;
        }
    }
    REQUIRE(count == 1);
}

TEST_CASE_METHOD(LVGLUITestFixture, "Unified recovery dialog - suppression prevents showing",
                 "[recovery][integration]") {
    auto& estop = EmergencyStopOverlay::instance();

    // Suppress for 5 seconds
    estop.suppress_recovery_dialog(5000);

    // Try both reasons - neither should show
    estop.show_recovery_for(RecoveryReason::SHUTDOWN);
    process_lvgl(50);

    lv_obj_t* dialog = lv_obj_find_by_name(lv_screen_active(), "klipper_recovery_card");
    REQUIRE(dialog == nullptr);

    estop.show_recovery_for(RecoveryReason::DISCONNECTED);
    process_lvgl(50);

    dialog = lv_obj_find_by_name(lv_screen_active(), "klipper_recovery_card");
    REQUIRE(dialog == nullptr);
}

TEST_CASE_METHOD(LVGLUITestFixture, "Unified recovery dialog - buttons present",
                 "[recovery][integration]") {
    auto& estop = EmergencyStopOverlay::instance();

    estop.show_recovery_for(RecoveryReason::SHUTDOWN);
    process_lvgl(50);

    lv_obj_t* dialog = lv_obj_find_by_name(lv_screen_active(), "klipper_recovery_card");
    REQUIRE(dialog != nullptr);

    // All three buttons should exist
    lv_obj_t* restart_btn = lv_obj_find_by_name(dialog, "restart_klipper_btn");
    REQUIRE(restart_btn != nullptr);

    lv_obj_t* firmware_btn = lv_obj_find_by_name(dialog, "firmware_restart_btn");
    REQUIRE(firmware_btn != nullptr);

    lv_obj_t* dismiss_btn = lv_obj_find_by_name(dialog, "recovery_dismiss_btn");
    REQUIRE(dismiss_btn != nullptr);
}

// ============================================================================
// Button state tests (restart buttons hidden when DISCONNECTED)
// ============================================================================

TEST_CASE_METHOD(LVGLUITestFixture, "Recovery dialog - SHUTDOWN shows all buttons visible",
                 "[recovery][buttons]") {
    auto& estop = EmergencyStopOverlay::instance();

    estop.show_recovery_for(RecoveryReason::SHUTDOWN);
    process_lvgl(50);

    lv_obj_t* dialog = lv_obj_find_by_name(lv_screen_active(), "klipper_recovery_card");
    REQUIRE(dialog != nullptr);

    lv_obj_t* restart_btn = lv_obj_find_by_name(dialog, "restart_klipper_btn");
    lv_obj_t* firmware_btn = lv_obj_find_by_name(dialog, "firmware_restart_btn");
    lv_obj_t* dismiss_btn = lv_obj_find_by_name(dialog, "recovery_dismiss_btn");

    REQUIRE(restart_btn != nullptr);
    REQUIRE(firmware_btn != nullptr);
    REQUIRE(dismiss_btn != nullptr);

    // All buttons visible for SHUTDOWN (can restart)
    REQUIRE_FALSE(lv_obj_has_flag(restart_btn, LV_OBJ_FLAG_HIDDEN));
    REQUIRE_FALSE(lv_obj_has_flag(firmware_btn, LV_OBJ_FLAG_HIDDEN));
    REQUIRE_FALSE(lv_obj_has_flag(dismiss_btn, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(LVGLUITestFixture, "Recovery dialog - DISCONNECTED hides restart buttons",
                 "[recovery][buttons]") {
    auto& estop = EmergencyStopOverlay::instance();

    estop.show_recovery_for(RecoveryReason::DISCONNECTED);
    process_lvgl(50);

    lv_obj_t* dialog = lv_obj_find_by_name(lv_screen_active(), "klipper_recovery_card");
    REQUIRE(dialog != nullptr);

    lv_obj_t* restart_btn = lv_obj_find_by_name(dialog, "restart_klipper_btn");
    lv_obj_t* firmware_btn = lv_obj_find_by_name(dialog, "firmware_restart_btn");
    lv_obj_t* dismiss_btn = lv_obj_find_by_name(dialog, "recovery_dismiss_btn");

    REQUIRE(restart_btn != nullptr);
    REQUIRE(firmware_btn != nullptr);
    REQUIRE(dismiss_btn != nullptr);

    // Restart buttons hidden when disconnected (can't restart)
    REQUIRE(lv_obj_has_flag(restart_btn, LV_OBJ_FLAG_HIDDEN));
    REQUIRE(lv_obj_has_flag(firmware_btn, LV_OBJ_FLAG_HIDDEN));

    // Dismiss always visible
    REQUIRE_FALSE(lv_obj_has_flag(dismiss_btn, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(LVGLUITestFixture, "Recovery dialog - SHUTDOWN then DISCONNECTED updates buttons",
                 "[recovery][buttons]") {
    auto& estop = EmergencyStopOverlay::instance();

    // Show SHUTDOWN first - all buttons visible
    estop.show_recovery_for(RecoveryReason::SHUTDOWN);
    process_lvgl(50);

    lv_obj_t* dialog = lv_obj_find_by_name(lv_screen_active(), "klipper_recovery_card");
    REQUIRE(dialog != nullptr);

    lv_obj_t* restart_btn = lv_obj_find_by_name(dialog, "restart_klipper_btn");
    REQUIRE(restart_btn != nullptr);
    REQUIRE_FALSE(lv_obj_has_flag(restart_btn, LV_OBJ_FLAG_HIDDEN));

    // Connection drops - DISCONNECTED fires, buttons should update
    estop.show_recovery_for(RecoveryReason::DISCONNECTED);
    process_lvgl(50);

    // Restart buttons should now be hidden
    REQUIRE(lv_obj_has_flag(restart_btn, LV_OBJ_FLAG_HIDDEN));

    lv_obj_t* firmware_btn = lv_obj_find_by_name(dialog, "firmware_restart_btn");
    REQUIRE(firmware_btn != nullptr);
    REQUIRE(lv_obj_has_flag(firmware_btn, LV_OBJ_FLAG_HIDDEN));
}
