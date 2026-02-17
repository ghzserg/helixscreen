// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_printer_status_icon.h"

#include "app_globals.h"
#include "moonraker_client.h"
#include "observer_factory.h"
#include "printer_state.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

using namespace helix;
using helix::ui::observe_int_sync;

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

PrinterStatusIcon& PrinterStatusIcon::instance() {
    static PrinterStatusIcon instance;
    return instance;
}

// ============================================================================
// PRINTER STATUS ICON IMPLEMENTATION
// ============================================================================

void PrinterStatusIcon::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[PrinterStatusIcon] Subjects already initialized");
        return;
    }

    spdlog::trace("[PrinterStatusIcon] Initializing printer icon subjects...");

    // Printer starts disconnected (gray)
    UI_MANAGED_SUBJECT_INT(printer_icon_state_subject_,
                           static_cast<int>(PrinterIconState::DISCONNECTED), "printer_icon_state",
                           subjects_);

    subjects_initialized_ = true;

    // Self-register cleanup — ensures deinit runs before lv_deinit()
    StaticPanelRegistry::instance().register_destroy(
        "PrinterStatusIconSubjects", []() { PrinterStatusIcon::instance().deinit_subjects(); });

    spdlog::trace("[PrinterStatusIcon] Subjects initialized and registered");
}

void PrinterStatusIcon::init() {
    if (initialized_) {
        spdlog::warn("[PrinterStatusIcon] Already initialized");
        return;
    }

    spdlog::debug("[PrinterStatusIcon] init() called");

    // Ensure subjects are initialized
    if (!subjects_initialized_) {
        init_subjects();
    }

    // Observe printer states from PrinterState
    PrinterState& printer_state = get_printer_state();

    // Printer connection observer
    lv_subject_t* conn_subject = printer_state.get_printer_connection_state_subject();
    spdlog::trace("[PrinterStatusIcon] Registering observer on printer_connection_state_subject at "
                  "{}",
                  (void*)conn_subject);
    connection_observer_ = observe_int_sync<PrinterStatusIcon>(
        conn_subject, this, [](PrinterStatusIcon* self, int val) {
            self->cached_connection_state_ = val;
            spdlog::trace("[PrinterStatusIcon] Connection state changed to: {}",
                          self->cached_connection_state_);
            self->update_icon_state();
        });

    // Klippy state observer
    lv_subject_t* klippy_subject = printer_state.get_klippy_state_subject();
    spdlog::trace("[PrinterStatusIcon] Registering observer on klippy_state_subject at {}",
                  (void*)klippy_subject);
    klippy_observer_ = observe_int_sync<PrinterStatusIcon>(
        klippy_subject, this, [](PrinterStatusIcon* self, int val) {
            self->cached_klippy_state_ = val;
            spdlog::trace("[PrinterStatusIcon] Klippy state changed to: {}",
                          self->cached_klippy_state_);
            self->update_icon_state();
        });

    initialized_ = true;
    spdlog::debug("[PrinterStatusIcon] Initialization complete");
}

void PrinterStatusIcon::update_icon_state() {
    PrinterIconState new_state;

    if (cached_connection_state_ == static_cast<int>(ConnectionState::CONNECTED)) {
        switch (cached_klippy_state_) {
        case static_cast<int>(KlippyState::STARTUP):
            new_state = PrinterIconState::WARNING;
            spdlog::debug("[PrinterStatusIcon] Klippy STARTUP -> printer state WARNING");
            break;
        case static_cast<int>(KlippyState::SHUTDOWN):
        case static_cast<int>(KlippyState::ERROR):
            new_state = PrinterIconState::ERROR;
            spdlog::debug("[PrinterStatusIcon] Klippy SHUTDOWN/ERROR -> printer state ERROR");
            break;
        case static_cast<int>(KlippyState::READY):
        default:
            new_state = PrinterIconState::READY;
            spdlog::debug("[PrinterStatusIcon] Klippy READY -> printer state READY");
            break;
        }
    } else if (cached_connection_state_ == static_cast<int>(ConnectionState::FAILED)) {
        new_state = PrinterIconState::ERROR;
        spdlog::debug("[PrinterStatusIcon] Connection FAILED -> printer state ERROR");
    } else { // DISCONNECTED, CONNECTING, RECONNECTING
        if (get_printer_state().was_ever_connected()) {
            new_state = PrinterIconState::WARNING;
            spdlog::trace("[PrinterStatusIcon] Disconnected (was connected) -> printer state "
                          "WARNING");
        } else {
            new_state = PrinterIconState::DISCONNECTED;
            spdlog::trace("[PrinterStatusIcon] Never connected -> printer state DISCONNECTED");
        }
    }

    if (subjects_initialized_) {
        lv_subject_set_int(&printer_icon_state_subject_, static_cast<int>(new_state));
    }
}

void PrinterStatusIcon::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }
    // Clear observers before deinit to prevent callbacks during teardown
    connection_observer_.reset();
    klippy_observer_.reset();
    subjects_.deinit_all();
    subjects_initialized_ = false;
    initialized_ = false;
    spdlog::debug("[PrinterStatusIcon] Subjects deinitialized");
}
