// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_filament_runout_handler.h"

#include "ui_error_reporting.h"
#include "ui_nav_manager.h"
#include "ui_panel_print_status.h" // For PrintState enum
#include "ui_update_queue.h"

#include "filament_sensor_manager.h"
#include "moonraker_api.h"
#include "runtime_config.h"
#include "standard_macros.h"

#include <spdlog/spdlog.h>

#include <lvgl.h>

namespace helix::ui {

// ============================================================================
// FilamentRunoutHandler Implementation
// ============================================================================

FilamentRunoutHandler::FilamentRunoutHandler(MoonrakerAPI* api) : api_(api) {
    spdlog::debug("[FilamentRunoutHandler] Constructed");
}

FilamentRunoutHandler::~FilamentRunoutHandler() {
    // Signal async callbacks to abort
    alive_->store(false);

    spdlog::trace("[FilamentRunoutHandler] Destroyed");
}

// ============================================================================
// State Transition Handler
// ============================================================================

void FilamentRunoutHandler::on_print_state_changed(::PrintState old_state, ::PrintState new_state) {
    (void)old_state;

    // Check for runout condition when entering Paused state
    if (new_state == ::PrintState::Paused) {
        check_and_show_runout_guidance();
    }

    // Reset runout modal flag and hide modal on print resume or end
    if (new_state == ::PrintState::Printing || new_state == ::PrintState::Idle ||
        new_state == ::PrintState::Complete || new_state == ::PrintState::Cancelled ||
        new_state == ::PrintState::Error) {
        runout_modal_shown_for_pause_ = false;
        hide_runout_guidance_modal();
    }
}

// ============================================================================
// Runout Detection and Modal Display
// ============================================================================

void FilamentRunoutHandler::check_and_show_runout_guidance() {
    // Only show once per pause event
    if (runout_modal_shown_for_pause_) {
        return;
    }

    // Skip if AMS/MMU present and not forced (runout during swaps is normal)
    if (!get_runtime_config()->should_show_runout_modal()) {
        return;
    }

    auto& sensor_mgr = helix::FilamentSensorManager::instance();

    // Check if any runout sensor shows no filament
    if (sensor_mgr.has_any_runout()) {
        spdlog::info(
            "[FilamentRunoutHandler] Runout detected during pause - showing guidance modal");
        show_runout_guidance_modal();
        runout_modal_shown_for_pause_ = true;
    }
}

void FilamentRunoutHandler::show_runout_guidance_modal() {
    if (runout_modal_.is_visible()) {
        // Already showing
        return;
    }

    spdlog::info("[FilamentRunoutHandler] Showing runout guidance modal");

    // Capture alive guard for async callback safety
    auto alive = alive_;

    // Configure callbacks for the six options
    runout_modal_.set_on_load_filament([alive]() {
        if (!alive->load()) {
            return;
        }
        spdlog::info("[FilamentRunoutHandler] User chose to load filament after runout");
        // Navigate to filament panel for loading
        NavigationManager::instance().set_active(PanelId::Filament);
    });

    runout_modal_.set_on_resume([this, alive]() {
        if (!alive->load()) {
            return;
        }

        // Check if filament is now present before allowing resume
        auto& sensor_mgr = helix::FilamentSensorManager::instance();
        if (sensor_mgr.has_any_runout()) {
            spdlog::warn(
                "[FilamentRunoutHandler] User attempted resume but filament still not detected");
            NOTIFY_WARNING("Insert filament before resuming");
            return; // Modal stays open - user needs to load filament first
        }

        // Check if resume slot is available
        const auto& resume_info = StandardMacros::instance().get(StandardMacroSlot::Resume);
        if (resume_info.is_empty()) {
            spdlog::warn("[FilamentRunoutHandler] Resume macro slot is empty");
            NOTIFY_WARNING("Resume macro not configured");
            return;
        }

        spdlog::info("[FilamentRunoutHandler] User chose to resume print after runout");

        // Resume the print via StandardMacros
        if (api_) {
            spdlog::info("[FilamentRunoutHandler] Using StandardMacros resume: {}",
                         resume_info.get_macro());
            StandardMacros::instance().execute(
                StandardMacroSlot::Resume, api_,
                []() { spdlog::info("[FilamentRunoutHandler] Print resumed after runout"); },
                [](const MoonrakerError& err) {
                    spdlog::error("[FilamentRunoutHandler] Failed to resume print: {}",
                                  err.message);
                    NOTIFY_ERROR("Failed to resume: {}", err.user_message());
                });
        }
    });

    runout_modal_.set_on_cancel_print([this, alive]() {
        if (!alive->load()) {
            return;
        }

        spdlog::info("[FilamentRunoutHandler] User chose to cancel print after runout");

        // Check if cancel slot is available
        const auto& cancel_info = StandardMacros::instance().get(StandardMacroSlot::Cancel);
        if (cancel_info.is_empty()) {
            spdlog::warn("[FilamentRunoutHandler] Cancel macro slot is empty");
            NOTIFY_WARNING("Cancel macro not configured");
            return;
        }

        // Cancel the print via StandardMacros
        if (api_) {
            spdlog::info("[FilamentRunoutHandler] Using StandardMacros cancel: {}",
                         cancel_info.get_macro());
            StandardMacros::instance().execute(
                StandardMacroSlot::Cancel, api_,
                []() { spdlog::info("[FilamentRunoutHandler] Print cancelled after runout"); },
                [](const MoonrakerError& err) {
                    spdlog::error("[FilamentRunoutHandler] Failed to cancel print: {}",
                                  err.message);
                    NOTIFY_ERROR("Failed to cancel: {}", err.user_message());
                });
        }
    });

    runout_modal_.set_on_unload_filament([this, alive]() {
        if (!alive->load()) {
            return;
        }

        spdlog::info("[FilamentRunoutHandler] User chose to unload filament after runout");

        const auto& unload_info = StandardMacros::instance().get(StandardMacroSlot::UnloadFilament);
        if (unload_info.is_empty()) {
            spdlog::warn("[FilamentRunoutHandler] Unload filament macro slot is empty");
            NOTIFY_WARNING("Unload macro not configured");
            return;
        }

        if (api_) {
            spdlog::info("[FilamentRunoutHandler] Using StandardMacros unload: {}",
                         unload_info.get_macro());
            StandardMacros::instance().execute(
                StandardMacroSlot::UnloadFilament, api_,
                []() { spdlog::info("[FilamentRunoutHandler] Unload filament started"); },
                [](const MoonrakerError& err) {
                    spdlog::error("[FilamentRunoutHandler] Failed to unload filament: {}",
                                  err.message);
                    NOTIFY_ERROR("Failed to unload: {}", err.user_message());
                });
        }
    });

    runout_modal_.set_on_purge([this, alive]() {
        if (!alive->load()) {
            return;
        }

        spdlog::info("[FilamentRunoutHandler] User chose to purge after runout");

        const auto& purge_info = StandardMacros::instance().get(StandardMacroSlot::Purge);
        if (purge_info.is_empty()) {
            spdlog::warn("[FilamentRunoutHandler] Purge macro slot is empty");
            NOTIFY_WARNING("Purge macro not configured");
            return;
        }

        if (api_) {
            spdlog::info("[FilamentRunoutHandler] Using StandardMacros purge: {}",
                         purge_info.get_macro());
            StandardMacros::instance().execute(
                StandardMacroSlot::Purge, api_,
                []() { spdlog::info("[FilamentRunoutHandler] Purge started"); },
                [](const MoonrakerError& err) {
                    spdlog::error("[FilamentRunoutHandler] Failed to purge: {}", err.message);
                    NOTIFY_ERROR("Failed to purge: {}", err.user_message());
                });
        }
    });

    runout_modal_.set_on_ok_dismiss([alive]() {
        if (!alive->load()) {
            return;
        }
        spdlog::info("[FilamentRunoutHandler] User dismissed runout modal (idle mode)");
        // Just hide the modal - no action needed
    });

    if (!runout_modal_.show(lv_screen_active())) {
        spdlog::error("[FilamentRunoutHandler] Failed to create runout guidance modal");
    }
}

void FilamentRunoutHandler::hide_modal() {
    hide_runout_guidance_modal();
}

void FilamentRunoutHandler::hide_runout_guidance_modal() {
    if (!runout_modal_.is_visible()) {
        return;
    }

    spdlog::debug("[FilamentRunoutHandler] Hiding runout guidance modal");
    runout_modal_.hide();
}

} // namespace helix::ui
