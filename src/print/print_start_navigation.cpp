// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "print_start_navigation.h"

#include "ui_nav_manager.h"
#include "ui_panel_print_status.h"

#include "app_globals.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <lvgl.h>

namespace helix {

// Track previous state to detect transitions TO printing
static PrintJobState prev_print_state = PrintJobState::STANDBY;

// Callback for print state changes - auto-navigates to print status on print start
static void on_print_state_changed_for_navigation(lv_observer_t* observer, lv_subject_t* subject) {
    (void)observer;
    auto current = static_cast<PrintJobState>(lv_subject_get_int(subject));

    spdlog::trace("[PrintStartNav] State change: {} -> {}", static_cast<int>(prev_print_state),
                  static_cast<int>(current));

    // Check for transition TO printing from a non-printing state
    bool was_not_printing =
        (prev_print_state != PrintJobState::PRINTING && prev_print_state != PrintJobState::PAUSED);
    bool is_now_printing = (current == PrintJobState::PRINTING);

    if (was_not_printing && is_now_printing) {
        // Don't auto-navigate while the setup wizard is running
        if (is_wizard_active()) {
            spdlog::debug(
                "[PrintStartNav] Wizard active, suppressing auto-navigation to print status");
            prev_print_state = current;
            return;
        }

        // A print just started - auto-navigate to print status from any panel
        auto& nav = NavigationManager::instance();
        lv_obj_t* print_status_widget = get_global_print_status_panel().get_panel();

        if (print_status_widget) {
            // Only navigate if print status isn't already showing
            if (!nav.is_panel_in_stack(print_status_widget)) {
                spdlog::info("[PrintStartNav] Auto-navigating to print status (print started)");
                nav.push_overlay(print_status_widget);
            } else {
                spdlog::debug("[PrintStartNav] Print status already showing, skipping navigation");
            }
        } else {
            spdlog::warn("[PrintStartNav] Print status panel widget not available");
        }
    }

    prev_print_state = current;
}

ObserverGuard init_print_start_navigation_observer() {
    // Initialize prev_print_state to current state to prevent false trigger on startup
    prev_print_state = static_cast<PrintJobState>(
        lv_subject_get_int(get_printer_state().get_print_state_enum_subject()));
    spdlog::debug("[PrintStartNav] Observer registered (initial state={})",
                  static_cast<int>(prev_print_state));
    return ObserverGuard(get_printer_state().get_print_state_enum_subject(),
                         on_print_state_changed_for_navigation, nullptr);
}

} // namespace helix
