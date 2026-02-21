// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_print_start_controller.cpp
 * @brief Controller for print initiation workflow
 *
 * Extracted from ui_panel_print_select.cpp to separate print start concerns.
 */

#include "ui_print_start_controller.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_modal.h"
#include "ui_nav_manager.h"
#include "ui_panel_print_status.h"
#include "ui_print_select_detail_view.h"
#include "ui_update_queue.h"
#include "ui_utils.h"

#include "active_print_media_manager.h"
#include "ams_state.h"
#include "filament_sensor_manager.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "moonraker_api.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

// ============================================================================
// Constructor / Destructor
// ============================================================================

PrintStartController::PrintStartController(PrinterState& printer_state, MoonrakerAPI* api)
    : printer_state_(printer_state), api_(api) {
    spdlog::debug("[PrintStartController] Created");
}

PrintStartController::~PrintStartController() {
    // Clean up any open modals - only if LVGL is still initialized
    // (destructor may be called after lv_deinit() during shutdown)
    if (lv_is_initialized()) {
        if (filament_warning_modal_) {
            helix::ui::modal_hide(filament_warning_modal_);
            filament_warning_modal_ = nullptr;
        }
        if (color_mismatch_modal_) {
            helix::ui::modal_hide(color_mismatch_modal_);
            color_mismatch_modal_ = nullptr;
        }
    }
    spdlog::trace("[PrintStartController] Destroyed");
}

// ============================================================================
// Setup
// ============================================================================

void PrintStartController::set_api(MoonrakerAPI* api) {
    api_ = api;
}

void PrintStartController::set_detail_view(PrintSelectDetailView* detail_view) {
    detail_view_ = detail_view;
}

void PrintStartController::set_file(const std::string& filename, const std::string& path,
                                    const std::vector<std::string>& filament_colors,
                                    const std::string& thumbnail_path) {
    filename_ = filename;
    path_ = path;
    filament_colors_ = filament_colors;
    thumbnail_path_ = thumbnail_path;
}

bool PrintStartController::is_ready() const {
    return !filename_.empty() && detail_view_ != nullptr;
}

// ============================================================================
// Print Initiation
// ============================================================================

void PrintStartController::initiate() {
    // OPTIMISTIC UI: Disable button IMMEDIATELY to prevent double-clicks.
    // This must happen BEFORE any async work or checks that could allow
    // the user to click again while we're processing.
    if (can_print_subject_) {
        lv_subject_set_int(can_print_subject_, 0);
    }

    // Check if a print is already active before allowing a new one to start
    if (!printer_state_.can_start_new_print()) {
        PrintJobState current_state = printer_state_.get_print_job_state();
        const char* state_str = print_job_state_to_string(current_state);
        NOTIFY_ERROR("Cannot start print: printer is {}", state_str);
        spdlog::warn("[PrintStartController] Attempted to start print while printer is in {} state",
                     state_str);
        if (update_print_button_) {
            update_print_button_(); // Re-enable button on early failure
        }
        return;
    }

    // Check if runout sensor shows no filament (pre-print warning)
    auto& sensor_mgr = helix::FilamentSensorManager::instance();
    if (sensor_mgr.is_master_enabled() &&
        sensor_mgr.is_sensor_available(helix::FilamentSensorRole::RUNOUT) &&
        !sensor_mgr.is_filament_detected(helix::FilamentSensorRole::RUNOUT)) {
        // No filament detected - show warning dialog
        // Button stays disabled - dialog will handle continuation or re-enable on cancel
        spdlog::info("[PrintStartController] Runout sensor shows no filament - showing pre-print "
                     "warning");
        show_filament_warning();
        return;
    }

    // Check if G-code requires colors not loaded in AMS
    auto missing_tools = check_ams_color_match();
    if (!missing_tools.empty()) {
        // Button stays disabled - dialog will handle continuation or re-enable on cancel
        spdlog::info("[PrintStartController] G-code requires {} tool colors not found in AMS slots",
                     missing_tools.size());
        show_color_mismatch_warning(missing_tools);
        return;
    }

    // All checks passed - proceed directly
    execute_print_start();
}

void PrintStartController::execute_print_start() {
    // OPTIMISTIC UI: Disable button immediately to prevent double-clicks
    if (can_print_subject_) {
        lv_subject_set_int(can_print_subject_, 0);
    }

    auto* prep_manager = detail_view_ ? detail_view_->get_prep_manager() : nullptr;
    if (!prep_manager) {
        spdlog::error("[PrintStartController] Cannot start print - prep manager not initialized");
        NOTIFY_ERROR("Cannot start print: internal error");
        if (update_print_button_) {
            update_print_button_(); // Re-enable button on early failure
        }
        return;
    }

    std::string filename_to_print = filename_;

    // Read options to check for timelapse (handled separately from prep_manager)
    auto options = prep_manager->read_options_from_subjects();

    spdlog::info(
        "[PrintStartController] Starting print: {} (pre-print: mesh={}, qgl={}, z_tilt={}, "
        "clean={}, timelapse={})",
        filename_to_print, options.bed_mesh, options.qgl, options.z_tilt, options.nozzle_clean,
        options.timelapse);

    // Enable timelapse recording if requested (Moonraker-Timelapse plugin)
    if (options.timelapse && api_) {
        api_->timelapse().set_timelapse_enabled(
            true, []() { spdlog::info("[PrintStartController] Timelapse enabled for this print"); },
            [](const MoonrakerError& err) {
                spdlog::error("[PrintStartController] Failed to enable timelapse: {}", err.message);
            });
    }

    // Navigate to print status panel IMMEDIATELY (optimistic navigation)
    // The busy overlay will show on top during download/upload operations.
    // On failure, we'll navigate back to the detail overlay.
    if (navigate_to_print_status_) {
        spdlog::info("[PrintStartController] Navigating to print status panel (preparing...)");
        if (hide_detail_view_) {
            hide_detail_view_();
        }
        navigate_to_print_status_();
    }

    // Capture callbacks for use in lambdas
    auto on_started = on_print_started_;
    auto on_cancelled = on_print_cancelled_;
    auto update_button = update_print_button_;
    auto show_detail = show_detail_view_;

    // Capture thumbnail path for lambda
    std::string thumbnail_path = thumbnail_path_;

    // Delegate to PrintPreparationManager
    prep_manager->start_print(
        filename_to_print, path_,
        // Navigation callback - called when Moonraker confirms print start
        // Sets thumbnail source so PrintStatusPanel loads the correct thumbnail
        // NOTE: Called from background HTTP thread - must defer LVGL calls to main thread
        [filename_to_print, path = path_, thumbnail_path, on_started]() {
            // Construct full path for metadata lookup (e.g., usb/flowrate_0.gcode)
            std::string full_path =
                path.empty() ? filename_to_print : path + "/" + filename_to_print;
            helix::ui::queue_update([full_path, thumbnail_path, on_started]() {
                auto& status_panel = get_global_print_status_panel();
                status_panel.set_thumbnail_source(full_path);

                // If we have a pre-extracted thumbnail (USB/embedded), set it directly
                // This bypasses Moonraker metadata lookup which doesn't have USB file info
                if (!thumbnail_path.empty()) {
                    helix::get_active_print_media_manager().set_thumbnail_path(thumbnail_path);
                    spdlog::debug("[PrintStartController] Set extracted thumbnail path: {}",
                                  thumbnail_path);
                }

                spdlog::debug(
                    "[PrintStartController] Print start confirmed, thumbnail source set: {}",
                    full_path);
                if (on_started) {
                    on_started();
                }
            });
        },
        // Completion callback
        // NOTE: Called from background HTTP thread - must defer LVGL calls to main thread
        [update_button, show_detail](bool success, const std::string& error) {
            helix::ui::queue_update([success, error, update_button, show_detail]() {
                auto& status_panel = get_global_print_status_panel();

                if (success) {
                    spdlog::debug("[PrintStartController] Print started successfully");
                    status_panel.end_preparing(true);
                } else if (!error.empty()) {
                    NOTIFY_ERROR("Print preparation failed: {}", error);
                    LOG_ERROR_INTERNAL("[PrintStartController] Print preparation failed: {}",
                                       error);
                    status_panel.end_preparing(false);

                    // Navigate back to print detail overlay on failure
                    spdlog::info(
                        "[PrintStartController] Navigating back to print select after failure");
                    NavigationManager::instance().go_back(); // Pop print status overlay

                    // Re-show the detail view so user can retry
                    if (show_detail) {
                        show_detail();
                    }

                    // Re-enable button on failure
                    if (update_button) {
                        update_button();
                    }
                }
            });
        });
}

// ============================================================================
// Filament Warning Dialog
// ============================================================================

void PrintStartController::show_filament_warning() {
    // Close any existing dialog first
    if (filament_warning_modal_) {
        helix::ui::modal_hide(filament_warning_modal_);
        filament_warning_modal_ = nullptr;
    }

    filament_warning_modal_ = helix::ui::modal_show_confirmation(
        lv_tr("No Filament Detected"),
        lv_tr("The runout sensor indicates no filament is loaded. "
              "Start print anyway?"),
        ModalSeverity::Warning, lv_tr("Start Print"), on_filament_warning_proceed_static,
        on_filament_warning_cancel_static, this);

    if (!filament_warning_modal_) {
        spdlog::error("[PrintStartController] Failed to create filament warning dialog");
        // Re-enable print button since we couldn't show the dialog
        if (update_print_button_) {
            update_print_button_();
        }
        return;
    }

    spdlog::debug("[PrintStartController] Pre-print filament warning dialog shown");
}

void PrintStartController::on_filament_warning_proceed_static(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStartController] on_filament_warning_proceed_static");
    auto* self = static_cast<PrintStartController*>(lv_event_get_user_data(e));
    if (self) {
        // Hide dialog first
        if (self->filament_warning_modal_) {
            helix::ui::modal_hide(self->filament_warning_modal_);
            self->filament_warning_modal_ = nullptr;
        }
        // Execute print
        self->execute_print_start();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStartController::on_filament_warning_cancel_static(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStartController] on_filament_warning_cancel_static");
    auto* self = static_cast<PrintStartController*>(lv_event_get_user_data(e));
    if (self) {
        if (self->filament_warning_modal_) {
            helix::ui::modal_hide(self->filament_warning_modal_);
            self->filament_warning_modal_ = nullptr;
        }
        // Re-enable print button since user cancelled
        if (self->update_print_button_) {
            self->update_print_button_();
        }
        if (self->on_print_cancelled_) {
            self->on_print_cancelled_();
        }
        spdlog::debug("[PrintStartController] Print cancelled by user (no filament warning)");
    }
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// AMS Color Mismatch Detection
// ============================================================================

std::vector<int> PrintStartController::check_ams_color_match() {
    std::vector<int> missing_tools;

    // Skip check if no multi-color G-code (single color or no colors)
    if (filament_colors_.size() <= 1) {
        return missing_tools;
    }

    // Skip check if AMS not available
    if (!AmsState::instance().is_available()) {
        spdlog::debug("[PrintStartController] AMS not available, skipping color match check");
        return missing_tools;
    }

    // Get slot count from AMS
    int slot_count = lv_subject_get_int(AmsState::instance().get_slot_count_subject());
    if (slot_count <= 0) {
        spdlog::debug("[PrintStartController] No AMS slots available");
        return missing_tools;
    }

    // Collect all slot colors
    std::vector<uint32_t> slot_colors;
    for (int i = 0; i < slot_count && i < AmsState::MAX_SLOTS; ++i) {
        lv_subject_t* color_subject = AmsState::instance().get_slot_color_subject(i);
        if (color_subject) {
            uint32_t color = static_cast<uint32_t>(lv_subject_get_int(color_subject));
            slot_colors.push_back(color);
        }
    }

    // Color match tolerance (0-255 scale)
    // Value of 40 allows ~15% variance per RGB channel, accounting for
    // differences between slicer color palettes and Spoolman/AMS colors
    constexpr int COLOR_MATCH_TOLERANCE = 40;

    // Check each required tool color
    for (size_t tool_idx = 0; tool_idx < filament_colors_.size(); ++tool_idx) {
        auto required_color = ui_parse_hex_color(filament_colors_[tool_idx]);
        if (!required_color) {
            continue; // Skip invalid/empty colors (but NOT black #000000!)
        }

        // Look for a matching slot
        bool found_match = false;
        for (uint32_t slot_color : slot_colors) {
            if (ui_color_distance(*required_color, slot_color) <= COLOR_MATCH_TOLERANCE) {
                found_match = true;
                break;
            }
        }

        if (!found_match) {
            missing_tools.push_back(static_cast<int>(tool_idx));
            spdlog::debug("[PrintStartController] Tool T{} color #{:06X} not found in AMS slots",
                          tool_idx, *required_color);
        }
    }

    return missing_tools;
}

void PrintStartController::show_color_mismatch_warning(const std::vector<int>& missing_tools) {
    // Close any existing dialog first
    if (color_mismatch_modal_) {
        helix::ui::modal_hide(color_mismatch_modal_);
        color_mismatch_modal_ = nullptr;
    }

    // Build message listing missing tools and their colors
    std::string message = "This print requires colors not loaded in the AMS:\n\n";
    for (int tool_idx : missing_tools) {
        if (tool_idx < static_cast<int>(filament_colors_.size())) {
            const std::string& color = filament_colors_[tool_idx];
            message += "  " + std::string(LV_SYMBOL_BULLET) + " T" + std::to_string(tool_idx) +
                       ": " + color + "\n";
        }
    }
    message += "\nLoad the required filaments or start anyway?";

    // Static buffer for message - must persist during modal lifetime.
    // Safe because we always close any existing dialog first above,
    // preventing concurrent access to this buffer.
    static char message_buffer[512];
    snprintf(message_buffer, sizeof(message_buffer), "%s", message.c_str());

    color_mismatch_modal_ = helix::ui::modal_show_confirmation(
        lv_tr("Color Mismatch"), message_buffer, ModalSeverity::Warning, lv_tr("Start Anyway"),
        on_color_mismatch_proceed_static, on_color_mismatch_cancel_static, this);

    if (!color_mismatch_modal_) {
        spdlog::error("[PrintStartController] Failed to create color mismatch warning dialog");
        // Re-enable print button since we couldn't show the dialog
        if (update_print_button_) {
            update_print_button_();
        }
        return;
    }

    spdlog::debug("[PrintStartController] Color mismatch warning dialog shown for {} tools",
                  missing_tools.size());
}

void PrintStartController::on_color_mismatch_proceed_static(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStartController] on_color_mismatch_proceed_static");
    auto* self = static_cast<PrintStartController*>(lv_event_get_user_data(e));
    if (self) {
        // Hide dialog first
        if (self->color_mismatch_modal_) {
            helix::ui::modal_hide(self->color_mismatch_modal_);
            self->color_mismatch_modal_ = nullptr;
        }
        // Execute print despite mismatch
        self->execute_print_start();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStartController::on_color_mismatch_cancel_static(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStartController] on_color_mismatch_cancel_static");
    auto* self = static_cast<PrintStartController*>(lv_event_get_user_data(e));
    if (self) {
        if (self->color_mismatch_modal_) {
            helix::ui::modal_hide(self->color_mismatch_modal_);
            self->color_mismatch_modal_ = nullptr;
        }
        // Re-enable print button since user cancelled
        if (self->update_print_button_) {
            self->update_print_button_();
        }
        if (self->on_print_cancelled_) {
            self->on_print_cancelled_();
        }
        spdlog::debug("[PrintStartController] Print cancelled by user (color mismatch warning)");
    }
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::ui
