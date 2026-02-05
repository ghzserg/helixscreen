// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_emergency_stop.h"

#include "ui_notification.h"
#include "ui_toast.h"
#include "ui_update_queue.h"
#include "ui_utils.h"

#include "abort_manager.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "observer_factory.h"

#include <spdlog/spdlog.h>

using helix::ui::observe_int_sync;

EmergencyStopOverlay& EmergencyStopOverlay::instance() {
    static EmergencyStopOverlay instance;
    return instance;
}

void EmergencyStopOverlay::init(PrinterState& printer_state, MoonrakerAPI* api) {
    printer_state_ = &printer_state;
    api_ = api;
    spdlog::debug("[EmergencyStop] Initialized with dependencies");
}

void EmergencyStopOverlay::set_require_confirmation(bool require) {
    require_confirmation_ = require;
    spdlog::debug("[EmergencyStop] Confirmation requirement set to: {}", require);
}

void EmergencyStopOverlay::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    // Initialize visibility subject (default hidden)
    UI_MANAGED_SUBJECT_INT(estop_visible_, 0, "estop_visible", subjects_);

    // Register click callbacks for XML event binding
    lv_xml_register_event_cb(nullptr, "emergency_stop_clicked", emergency_stop_clicked);
    lv_xml_register_event_cb(nullptr, "estop_dialog_cancel_clicked", estop_dialog_cancel_clicked);
    lv_xml_register_event_cb(nullptr, "estop_dialog_confirm_clicked", estop_dialog_confirm_clicked);
    lv_xml_register_event_cb(nullptr, "recovery_restart_klipper_clicked",
                             recovery_restart_klipper_clicked);
    lv_xml_register_event_cb(nullptr, "recovery_firmware_restart_clicked",
                             recovery_firmware_restart_clicked);
    lv_xml_register_event_cb(nullptr, "recovery_dismiss_clicked", recovery_dismiss_clicked);

    // Advanced panel button callbacks (reuse same logic)
    lv_xml_register_event_cb(nullptr, "advanced_estop_clicked", advanced_estop_clicked);
    lv_xml_register_event_cb(nullptr, "advanced_restart_klipper_clicked",
                             advanced_restart_klipper_clicked);
    lv_xml_register_event_cb(nullptr, "advanced_firmware_restart_clicked",
                             advanced_firmware_restart_clicked);

    // Home panel firmware restart button (shown during klippy SHUTDOWN)
    lv_xml_register_event_cb(nullptr, "firmware_restart_clicked", home_firmware_restart_clicked);

    subjects_initialized_ = true;
    spdlog::debug("[EmergencyStop] Subjects initialized");
}

void EmergencyStopOverlay::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::debug("[EmergencyStop] Subjects deinitialized");
}

void EmergencyStopOverlay::create() {
    if (!printer_state_ || !api_) {
        spdlog::error("[EmergencyStop] Cannot create: dependencies not initialized");
        return;
    }

    if (!subjects_initialized_) {
        spdlog::error("[EmergencyStop] Cannot create: subjects not initialized");
        return;
    }

    // Subscribe to print state changes for automatic visibility updates
    // The estop_visible subject drives XML bindings in home_panel, controls_panel,
    // and print_status_panel (no FAB - buttons are embedded in each panel)
    print_state_observer_ = observe_int_sync<EmergencyStopOverlay>(
        printer_state_->get_print_state_enum_subject(), this,
        [](EmergencyStopOverlay* self, int /*state*/) { self->update_visibility(); });

    // Subscribe to klippy state changes for recovery dialog auto-popup
    klippy_state_observer_ = observe_int_sync<EmergencyStopOverlay>(
        printer_state_->get_klippy_state_subject(), this,
        [](EmergencyStopOverlay* self, int state) {
            auto klippy_state = static_cast<KlippyState>(state);

            if (klippy_state == KlippyState::SHUTDOWN) {
                // Don't show recovery dialog if we initiated the restart operation
                // (Klipper briefly enters SHUTDOWN during firmware/klipper restart)
                if (self->restart_in_progress_) {
                    spdlog::debug("[KlipperRecovery] Ignoring SHUTDOWN during restart operation");
                    return;
                }
                // Don't show recovery dialog if AbortManager is handling controlled shutdown
                // (M112 -> FIRMWARE_RESTART escalation path)
                if (helix::AbortManager::instance().is_handling_shutdown()) {
                    spdlog::debug(
                        "[KlipperRecovery] Ignoring SHUTDOWN - AbortManager handling recovery");
                    return;
                }
                // Auto-popup recovery dialog when Klipper enters SHUTDOWN state
                // NOTE: Must defer to main thread - observer may fire from WebSocket thread
                spdlog::info(
                    "[KlipperRecovery] Detected Klipper SHUTDOWN state, queueing recovery dialog");
                spdlog::debug("[KlipperRecovery] Queueing recovery dialog (observer path)");
                ui_async_call(
                    [](void*) {
                        spdlog::debug("[KlipperRecovery] Async callback executing (observer path)");
                        EmergencyStopOverlay::instance().show_recovery_dialog();
                    },
                    nullptr);
            } else if (klippy_state == KlippyState::READY) {
                // Reset restart flag - operation complete
                self->restart_in_progress_ = false;

                // Auto-dismiss recovery dialog when Klipper is back to READY
                // NOTE: Must defer to main thread - observer may fire from WebSocket thread
                ui_async_call(
                    [](void*) {
                        auto& inst = EmergencyStopOverlay::instance();
                        // Guard against async callback firing after display destruction
                        if (inst.recovery_dialog_ && lv_obj_is_valid(inst.recovery_dialog_)) {
                            spdlog::info(
                                "[KlipperRecovery] Klipper is READY, dismissing recovery dialog");
                            inst.dismiss_recovery_dialog();
                            ui_toast_show(ToastSeverity::SUCCESS, lv_tr("Printer ready"), 3000);
                        }
                    },
                    nullptr);
            }
        });

    // Initial visibility update
    update_visibility();

    spdlog::info("[EmergencyStop] Initialized visibility subject for contextual E-Stop buttons");
}

void EmergencyStopOverlay::update_visibility() {
    if (!printer_state_) {
        return;
    }

    // Check if print is active (PRINTING or PAUSED)
    // The estop_visible subject drives XML bindings in each panel
    PrintJobState state = printer_state_->get_print_job_state();
    bool is_printing = (state == PrintJobState::PRINTING || state == PrintJobState::PAUSED);

    int new_value = is_printing ? 1 : 0;
    int current_value = lv_subject_get_int(&estop_visible_);

    if (new_value != current_value) {
        lv_subject_set_int(&estop_visible_, new_value);
        spdlog::debug("[EmergencyStop] Visibility changed: {} (state={})", is_printing,
                      static_cast<int>(state));
    }
}

void EmergencyStopOverlay::handle_click() {
    spdlog::info("[EmergencyStop] Button clicked");

    if (require_confirmation_) {
        show_confirmation_dialog();
    } else {
        execute_emergency_stop();
    }
}

void EmergencyStopOverlay::execute_emergency_stop() {
    if (!api_) {
        spdlog::error("[EmergencyStop] Cannot execute: API not available");
        ui_toast_show(ToastSeverity::ERROR, lv_tr("Emergency stop failed: not connected"), 4000);
        return;
    }

    spdlog::warn("[EmergencyStop] Executing emergency stop (M112)!");

    api_->emergency_stop(
        []() {
            spdlog::info("[EmergencyStop] Emergency stop command sent successfully");
            ui_toast_show(ToastSeverity::WARNING, lv_tr("Emergency stop activated"), 5000);

            // Proactively show recovery dialog after E-stop
            // We know Klipper will be in SHUTDOWN state - don't wait for notification
            // which may not arrive due to WebSocket timing/disconnection
            // NOTE: Must defer to main thread - this callback runs on WebSocket thread
            spdlog::debug("[EmergencyStop] Queueing proactive recovery dialog (E-stop path)");
            ui_async_call(
                [](void*) {
                    spdlog::debug("[EmergencyStop] Async callback executing (E-stop path)");
                    EmergencyStopOverlay::instance().show_recovery_dialog();
                },
                nullptr);
        },
        [](const MoonrakerError& err) {
            spdlog::error("[EmergencyStop] Emergency stop failed: {}", err.message);
            ui_toast_show(ToastSeverity::ERROR,
                          ("Emergency stop failed: " + err.user_message()).c_str(), 5000);
        });
}

void EmergencyStopOverlay::show_confirmation_dialog() {
    // Don't show if already visible
    if (confirmation_dialog_) {
        spdlog::debug("[EmergencyStop] Confirmation dialog already visible");
        return;
    }

    spdlog::debug("[EmergencyStop] Showing confirmation dialog");

    // Create dialog on current screen
    lv_obj_t* screen = lv_screen_active();
    confirmation_dialog_ =
        static_cast<lv_obj_t*>(lv_xml_create(screen, "estop_confirmation_dialog", nullptr));

    if (!confirmation_dialog_) {
        spdlog::error("[EmergencyStop] Failed to create confirmation dialog, executing directly");
        execute_emergency_stop();
        return;
    }

    // Ensure dialog is on top of everything including the E-Stop button
    lv_obj_move_foreground(confirmation_dialog_);

    spdlog::info("[EmergencyStop] Confirmation dialog shown");
}

void EmergencyStopOverlay::dismiss_confirmation_dialog() {
    if (confirmation_dialog_) {
        lv_obj_safe_delete(confirmation_dialog_);
        spdlog::debug("[EmergencyStop] Confirmation dialog dismissed");
    }
}

void EmergencyStopOverlay::show_recovery_dialog() {
    // Don't show if already visible
    spdlog::debug("[KlipperRecovery] show_recovery_dialog() called, recovery_dialog_={}",
                  static_cast<void*>(recovery_dialog_));
    if (recovery_dialog_) {
        spdlog::debug("[KlipperRecovery] Recovery dialog already visible, skipping");
        return;
    }

    spdlog::info("[KlipperRecovery] Creating recovery dialog (Klipper in SHUTDOWN state)");

    // Create dialog on current screen
    lv_obj_t* screen = lv_screen_active();
    recovery_dialog_ =
        static_cast<lv_obj_t*>(lv_xml_create(screen, "klipper_recovery_dialog", nullptr));
    spdlog::debug("[KlipperRecovery] Dialog created, recovery_dialog_={}",
                  static_cast<void*>(recovery_dialog_));

    if (!recovery_dialog_) {
        spdlog::error("[KlipperRecovery] Failed to create recovery dialog");
        return;
    }

    // Ensure dialog is on top of everything
    lv_obj_move_foreground(recovery_dialog_);
}

void EmergencyStopOverlay::dismiss_recovery_dialog() {
    if (recovery_dialog_) {
        lv_obj_safe_delete(recovery_dialog_);
        spdlog::debug("[KlipperRecovery] Recovery dialog dismissed");
    }
}

void EmergencyStopOverlay::restart_klipper() {
    if (!api_) {
        spdlog::error("[KlipperRecovery] Cannot restart: API not available");
        ui_toast_show(ToastSeverity::ERROR, lv_tr("Restart failed: not connected"), 4000);
        return;
    }

    // Suppress recovery dialog during restart - Klipper briefly enters SHUTDOWN
    restart_in_progress_ = true;

    spdlog::info("[KlipperRecovery] Restarting Klipper...");
    ui_toast_show(ToastSeverity::INFO, lv_tr("Restarting Klipper..."), 3000);

    api_->restart_klipper(
        []() {
            spdlog::info("[KlipperRecovery] Klipper restart command sent");
            // Toast will update when klippy_state changes to READY
        },
        [](const MoonrakerError& err) {
            spdlog::error("[KlipperRecovery] Klipper restart failed: {}", err.message);
            ui_toast_show(ToastSeverity::ERROR, ("Restart failed: " + err.user_message()).c_str(),
                          5000);
        });
}

void EmergencyStopOverlay::firmware_restart() {
    if (!api_) {
        spdlog::error("[KlipperRecovery] Cannot firmware restart: API not available");
        ui_toast_show(ToastSeverity::ERROR, lv_tr("Restart failed: not connected"), 4000);
        return;
    }

    // Suppress recovery dialog during restart - Klipper briefly enters SHUTDOWN
    restart_in_progress_ = true;

    spdlog::info("[KlipperRecovery] Firmware restarting...");
    ui_toast_show(ToastSeverity::INFO, lv_tr("Firmware restarting..."), 3000);

    api_->restart_firmware(
        []() {
            spdlog::info("[KlipperRecovery] Firmware restart command sent");
            // Toast will update when klippy_state changes to READY
        },
        [](const MoonrakerError& err) {
            spdlog::error("[KlipperRecovery] Firmware restart failed: {}", err.message);
            ui_toast_show(ToastSeverity::ERROR,
                          ("Firmware restart failed: " + err.user_message()).c_str(), 5000);
        });
}

// Static callback trampolines
void EmergencyStopOverlay::emergency_stop_clicked(lv_event_t* e) {
    LV_UNUSED(e);
    EmergencyStopOverlay::instance().handle_click();
}

void EmergencyStopOverlay::estop_dialog_cancel_clicked(lv_event_t* e) {
    LV_UNUSED(e);
    spdlog::debug("[EmergencyStop] Cancel clicked - aborting E-Stop");
    EmergencyStopOverlay::instance().dismiss_confirmation_dialog();
}

void EmergencyStopOverlay::estop_dialog_confirm_clicked(lv_event_t* e) {
    LV_UNUSED(e);
    spdlog::debug("[EmergencyStop] Confirm clicked - executing E-Stop");
    auto& instance = EmergencyStopOverlay::instance();
    instance.dismiss_confirmation_dialog();
    instance.execute_emergency_stop();
}

void EmergencyStopOverlay::recovery_restart_klipper_clicked(lv_event_t* e) {
    LV_UNUSED(e);
    spdlog::debug("[KlipperRecovery] Restart Klipper clicked");
    auto& instance = EmergencyStopOverlay::instance();
    instance.dismiss_recovery_dialog();
    instance.restart_klipper();
}

void EmergencyStopOverlay::recovery_firmware_restart_clicked(lv_event_t* e) {
    LV_UNUSED(e);
    spdlog::debug("[KlipperRecovery] Firmware Restart clicked");
    auto& instance = EmergencyStopOverlay::instance();
    instance.dismiss_recovery_dialog();
    instance.firmware_restart();
}

void EmergencyStopOverlay::recovery_dismiss_clicked(lv_event_t* e) {
    LV_UNUSED(e);
    spdlog::debug("[KlipperRecovery] Dismiss clicked");
    EmergencyStopOverlay::instance().dismiss_recovery_dialog();
}

// Advanced panel button callbacks
void EmergencyStopOverlay::advanced_estop_clicked(lv_event_t* e) {
    LV_UNUSED(e);
    spdlog::info("[Advanced] E-Stop clicked from Advanced panel");
    EmergencyStopOverlay::instance().handle_click();
}

void EmergencyStopOverlay::advanced_restart_klipper_clicked(lv_event_t* e) {
    LV_UNUSED(e);
    spdlog::info("[Advanced] Restart Klipper clicked from Advanced panel");
    EmergencyStopOverlay::instance().restart_klipper();
}

void EmergencyStopOverlay::advanced_firmware_restart_clicked(lv_event_t* e) {
    LV_UNUSED(e);
    spdlog::info("[Advanced] Firmware Restart clicked from Advanced panel");
    EmergencyStopOverlay::instance().firmware_restart();
}

void EmergencyStopOverlay::home_firmware_restart_clicked(lv_event_t* e) {
    LV_UNUSED(e);
    spdlog::info("[Home] Firmware Restart clicked from Home panel");
    EmergencyStopOverlay::instance().firmware_restart();
}
