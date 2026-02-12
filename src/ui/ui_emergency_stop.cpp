// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_emergency_stop.h"

#include "ui_modal.h"
#include "ui_notification.h"
#include "ui_toast.h"
#include "ui_update_queue.h"
#include "ui_utils.h"

#include "abort_manager.h"
#include "app_globals.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "observer_factory.h"

#include <spdlog/spdlog.h>

namespace {
// Recovery dialog content per reason
struct RecoveryContent {
    const char* title;
    const char* message;
};

RecoveryContent get_recovery_content(RecoveryReason reason) {
    switch (reason) {
    case RecoveryReason::SHUTDOWN:
        return {"Printer Shutdown",
                "Klipper has entered shutdown state. This may be due to an emergency stop, "
                "thermal runaway, or configuration error."};
    case RecoveryReason::DISCONNECTED:
        return {"Printer Firmware Disconnected",
                "Klipper firmware has disconnected from the host. "
                "Try restarting Klipper or performing a firmware restart."};
    default:
        return {"Printer Error", "An unexpected printer error occurred."};
    }
}
} // namespace

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

    // Recovery dialog subjects (bound in klipper_recovery_dialog.xml)
    UI_MANAGED_SUBJECT_STRING(recovery_title_subject_, recovery_title_buf_, "Printer Shutdown",
                              "recovery_title", subjects_);
    UI_MANAGED_SUBJECT_STRING(recovery_message_subject_, recovery_message_buf_, "",
                              "recovery_message", subjects_);
    UI_MANAGED_SUBJECT_INT(recovery_can_restart_, 1, "recovery_can_restart", subjects_);

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
                // Unified recovery path - all suppression checks are in show_recovery_for()
                self->show_recovery_for(RecoveryReason::SHUTDOWN);
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

    spdlog::debug("[EmergencyStop] Initialized visibility subject for contextual E-Stop buttons");
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
            EmergencyStopOverlay::instance().show_recovery_for(RecoveryReason::SHUTDOWN);
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

    // Create dialog via Modal system (handles backdrop, z-order, animations)
    confirmation_dialog_ = ui_modal_show("estop_confirmation_dialog");

    if (!confirmation_dialog_) {
        spdlog::error("[EmergencyStop] Failed to create confirmation dialog, executing directly");
        execute_emergency_stop();
        return;
    }

    spdlog::info("[EmergencyStop] Confirmation dialog shown");
}

void EmergencyStopOverlay::dismiss_confirmation_dialog() {
    if (confirmation_dialog_) {
        ui_modal_hide(confirmation_dialog_);
        confirmation_dialog_ = nullptr;
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

    // Use Modal system — backdrop is created programmatically
    recovery_dialog_ = ui_modal_show("klipper_recovery_dialog");
    spdlog::debug("[KlipperRecovery] Dialog created, recovery_dialog_={}",
                  static_cast<void*>(recovery_dialog_));

    if (!recovery_dialog_) {
        spdlog::error("[KlipperRecovery] Failed to create recovery dialog");
        return;
    }

    // XML <view name="..."> is not applied by lv_xml_create — set explicitly for lookups
    lv_obj_set_name(recovery_dialog_, "klipper_recovery_card");
}

void EmergencyStopOverlay::dismiss_recovery_dialog() {
    if (recovery_dialog_) {
        ui_modal_hide(recovery_dialog_);
        recovery_dialog_ = nullptr;
        recovery_reason_ = RecoveryReason::NONE;
        spdlog::debug("[KlipperRecovery] Recovery dialog dismissed");
    }
}

void EmergencyStopOverlay::show_recovery_for(RecoveryReason reason) {
    // Check suppression
    if (is_recovery_suppressed()) {
        spdlog::info("[KlipperRecovery] Suppressing recovery dialog (suppression active)");
        return;
    }

    // Don't show during wizard
    if (is_wizard_active()) {
        spdlog::debug("[KlipperRecovery] Ignoring {} during setup wizard",
                      reason == RecoveryReason::SHUTDOWN ? "SHUTDOWN" : "DISCONNECTED");
        return;
    }

    // Don't show if restart is in progress (expected shutdown cycle)
    if (restart_in_progress_) {
        spdlog::debug("[KlipperRecovery] Ignoring {} during restart operation",
                      reason == RecoveryReason::SHUTDOWN ? "SHUTDOWN" : "DISCONNECTED");
        return;
    }

    // Don't show if AbortManager is handling controlled shutdown
    if (helix::AbortManager::instance().is_handling_shutdown()) {
        spdlog::debug("[KlipperRecovery] Ignoring {} - AbortManager handling recovery",
                      reason == RecoveryReason::SHUTDOWN ? "SHUTDOWN" : "DISCONNECTED");
        return;
    }

    // If dialog is already showing, update reason if it's worse (SHUTDOWN -> DISCONNECTED means
    // can't restart)
    if (recovery_dialog_) {
        if (reason == RecoveryReason::DISCONNECTED &&
            recovery_reason_ == RecoveryReason::SHUTDOWN) {
            spdlog::info("[KlipperRecovery] Connection dropped while SHUTDOWN dialog showing, "
                         "updating buttons");
            recovery_reason_ = RecoveryReason::DISCONNECTED;
            ui_async_call(
                [](void*) { EmergencyStopOverlay::instance().update_recovery_dialog_content(); },
                nullptr);
        } else {
            spdlog::debug("[KlipperRecovery] Recovery dialog already visible, ignoring {}",
                          reason == RecoveryReason::SHUTDOWN ? "SHUTDOWN" : "DISCONNECTED");
        }
        return;
    }

    recovery_reason_ = reason;

    // Defer to main thread - may be called from WebSocket thread
    ui_async_call(
        [](void*) {
            auto& inst = EmergencyStopOverlay::instance();
            // Guard: dialog may have been shown by another async call in the meantime
            if (inst.recovery_dialog_) {
                return;
            }
            spdlog::info("[KlipperRecovery] Showing recovery dialog (reason: {})",
                         inst.recovery_reason_ == RecoveryReason::SHUTDOWN ? "SHUTDOWN"
                                                                           : "DISCONNECTED");
            inst.show_recovery_dialog();
            inst.update_recovery_dialog_content();
        },
        nullptr);
}

void EmergencyStopOverlay::suppress_recovery_dialog(uint32_t duration_ms) {
    suppress_recovery_until_ = lv_tick_get() + duration_ms;
    spdlog::info("[KlipperRecovery] Suppressing recovery dialog for {}ms", duration_ms);
}

bool EmergencyStopOverlay::is_recovery_suppressed() const {
    if (suppress_recovery_until_ == 0) {
        return false;
    }
    return lv_tick_elaps(suppress_recovery_until_) > (UINT32_MAX / 2);
}

void EmergencyStopOverlay::update_recovery_dialog_content() {
    auto content = get_recovery_content(recovery_reason_);

    // Update subjects — XML bindings in klipper_recovery_dialog.xml react automatically
    lv_subject_copy_string(&recovery_title_subject_, lv_tr(content.title));
    lv_subject_copy_string(&recovery_message_subject_, lv_tr(content.message));
    lv_subject_set_int(&recovery_can_restart_,
                       recovery_reason_ != RecoveryReason::DISCONNECTED ? 1 : 0);

    spdlog::debug("[KlipperRecovery] Updated dialog content: reason={}, can_restart={}",
                  recovery_reason_ == RecoveryReason::SHUTDOWN ? "SHUTDOWN" : "DISCONNECTED",
                  recovery_reason_ != RecoveryReason::DISCONNECTED);
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
