// SPDX-License-Identifier: GPL-3.0-or-later

#include "print_completion.h"

#include "ui_modal.h"
#include "ui_nav_manager.h"
#include "ui_panel_print_status.h"
#include "ui_theme.h"
#include "ui_toast.h"
#include "ui_utils.h"

#include "app_globals.h"
#include "moonraker_api.h"
#include "moonraker_manager.h"
#include "printer_state.h"
#include "settings_manager.h"

#include <spdlog/spdlog.h>

#include <lvgl.h>

namespace helix {

// Track previous state to detect transitions to terminal states
static PrintJobState prev_print_state = PrintJobState::STANDBY;

// Helper to cleanup .helix_temp modified G-code files after print ends
static void cleanup_helix_temp_file(const std::string& filename) {
    // Check if this is a .helix_temp modified file
    if (filename.find(".helix_temp/modified_") == std::string::npos) {
        return; // Not a temp file
    }

    auto* mgr = get_moonraker_manager();
    if (!mgr) {
        spdlog::warn("[PrintComplete] Cannot cleanup temp file - MoonrakerManager not available");
        return;
    }

    auto* api = mgr->api();
    if (!api) {
        spdlog::warn("[PrintComplete] Cannot cleanup temp file - API not available");
        return;
    }

    // Moonraker's delete_file requires full path including root
    std::string full_path = "gcodes/" + filename;
    spdlog::info("[PrintComplete] Cleaning up temp file: {}", full_path);

    api->delete_file(
        full_path,
        [filename]() { spdlog::info("[PrintComplete] Deleted temp file: {}", filename); },
        [filename](const MoonrakerError& err) {
            // Log but don't show error to user - cleanup is best-effort
            spdlog::warn("[PrintComplete] Failed to delete temp file {}: {}", filename,
                         err.message);
        });
}

// Helper to format duration as "Xh YYm" or "Ym"
static void format_duration(int seconds, char* buf, size_t buf_size) {
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    if (hours > 0) {
        snprintf(buf, buf_size, "%dh %02dm", hours, minutes);
    } else {
        snprintf(buf, buf_size, "%dm", minutes);
    }
}

// Helper to show the rich print completion modal
static void show_rich_completion_modal(PrintJobState state, const char* filename) {
    auto& printer_state = get_printer_state();

    // Get print stats
    int duration_secs = lv_subject_get_int(printer_state.get_print_duration_subject());
    int total_layers = lv_subject_get_int(printer_state.get_print_layer_total_subject());

    // Determine icon colors and title based on state
    const char* icon_color_token = "success_color";
    const char* title = "Print Complete";

    switch (state) {
    case PrintJobState::COMPLETE:
        icon_color_token = "success_color";
        title = "Print Complete";
        break;
    case PrintJobState::CANCELLED:
        icon_color_token = "warning_color";
        title = "Print Cancelled";
        break;
    case PrintJobState::ERROR:
        icon_color_token = "error_color";
        title = "Print Failed";
        break;
    default:
        break;
    }

    // Show modal using unified Modal system
    // Backdrop click-to-close and ESC handling come for free
    lv_obj_t* dialog = ui_modal_show("print_completion_modal");

    if (!dialog) {
        spdlog::error("[PrintComplete] Failed to create print_completion_modal");
        return;
    }

    // Find and update the icon
    lv_obj_t* icon_widget = lv_obj_find_by_name(dialog, "status_icon");
    if (icon_widget) {
        // Update icon source
        lv_obj_t* icon_label = lv_obj_get_child(icon_widget, 0);
        if (icon_label) {
            // Icon component stores the icon name, we need to update via the icon API
            // For now, just update the color - the icon is set in XML
            lv_color_t color = ui_theme_get_color(icon_color_token);
            lv_obj_set_style_text_color(icon_label, color, LV_PART_MAIN);
        }
    }

    // Update title
    lv_obj_t* title_label = lv_obj_find_by_name(dialog, "title_label");
    if (title_label) {
        lv_label_set_text(title_label, title);
    }

    // Update filename
    lv_obj_t* filename_label = lv_obj_find_by_name(dialog, "filename_label");
    if (filename_label) {
        lv_label_set_text(filename_label, filename);
    }

    // Update duration
    lv_obj_t* duration_label = lv_obj_find_by_name(dialog, "duration_label");
    if (duration_label) {
        char duration_buf[32];
        format_duration(duration_secs, duration_buf, sizeof(duration_buf));
        lv_label_set_text(duration_label, duration_buf);
    }

    // Update layers
    lv_obj_t* layers_label = lv_obj_find_by_name(dialog, "layers_label");
    if (layers_label) {
        char layers_buf[32];
        snprintf(layers_buf, sizeof(layers_buf), "%d layers", total_layers);
        lv_label_set_text(layers_label, layers_buf);
    }

    // Hide filament stat for now (would need metadata fetch)
    lv_obj_t* filament_stat = lv_obj_find_by_name(dialog, "filament_stat");
    if (filament_stat) {
        lv_obj_add_flag(filament_stat, LV_OBJ_FLAG_HIDDEN);
    }

    // Note: OK button dismissal is wired via XML event_cb="on_modal_ok_clicked"
    // Backdrop click-to-close and ESC handling are automatic via Modal system

    spdlog::info("[PrintComplete] Showing rich completion modal: {} ({})", title, filename);
}

// Callback for print state changes - triggers completion notifications
static void on_print_state_changed_for_notification(lv_observer_t* observer,
                                                    lv_subject_t* subject) {
    (void)observer;
    auto current = static_cast<PrintJobState>(lv_subject_get_int(subject));

    spdlog::debug("[PrintComplete] State change: {} -> {}", static_cast<int>(prev_print_state),
                  static_cast<int>(current));

    // Check for transitions to terminal states (from active print states)
    bool was_active =
        (prev_print_state == PrintJobState::PRINTING || prev_print_state == PrintJobState::PAUSED);
    bool is_terminal = (current == PrintJobState::COMPLETE || current == PrintJobState::CANCELLED ||
                        current == PrintJobState::ERROR);

    spdlog::debug("[PrintComplete] was_active={}, is_terminal={}", was_active, is_terminal);

    if (was_active && is_terminal) {
        // Get filename from PrinterState and format for display
        const char* raw_filename =
            lv_subject_get_string(get_printer_state().get_print_filename_subject());
        std::string resolved_filename =
            (raw_filename && raw_filename[0]) ? resolve_gcode_filename(raw_filename) : "";
        std::string display_name =
            !resolved_filename.empty() ? get_display_filename(resolved_filename) : "Unknown";

        // Cleanup temp files - delete .helix_temp/modified_* files after print ends
        // Do this before anything else so it happens regardless of notification settings
        if (raw_filename && raw_filename[0]) {
            cleanup_helix_temp_file(raw_filename);
        }

        // Check if user is on print status panel
        lv_obj_t* print_status_panel = get_global_print_status_panel().get_panel();
        bool on_print_status = NavigationManager::instance().is_panel_in_stack(print_status_panel);

        auto mode = SettingsManager::instance().get_completion_alert_mode();

        spdlog::info("[PrintComplete] Print {} - on_print_status={}, mode={}",
                     (current == PrintJobState::COMPLETE)    ? "complete"
                     : (current == PrintJobState::CANCELLED) ? "cancelled"
                                                             : "failed",
                     on_print_status, static_cast<int>(mode));

        // 1. Errors ALWAYS get a modal (high visibility needed)
        if (current == PrintJobState::ERROR) {
            SettingsManager::instance().wake_display();
            show_rich_completion_modal(current, display_name.c_str());
            prev_print_state = current;
            return;
        }

        // 2. On print status panel - no notification needed (panel shows state)
        if (on_print_status) {
            spdlog::debug("[PrintComplete] On print status panel - skipping notification");
            prev_print_state = current;
            return;
        }

        // 3. On other panels - respect the completion alert mode setting
        switch (mode) {
        case CompletionAlertMode::OFF:
            spdlog::debug("[PrintComplete] Notification disabled by setting");
            break;

        case CompletionAlertMode::NOTIFICATION: {
            SettingsManager::instance().wake_display();
            char message[128];
            ToastSeverity severity = (current == PrintJobState::COMPLETE) ? ToastSeverity::SUCCESS
                                                                          : ToastSeverity::WARNING;
            snprintf(message, sizeof(message), "Print %s: %s",
                     (current == PrintJobState::COMPLETE) ? "complete" : "cancelled",
                     display_name.c_str());
            ui_toast_show(severity, message, 5000);
            break;
        }

        case CompletionAlertMode::ALERT:
            SettingsManager::instance().wake_display();
            show_rich_completion_modal(current, display_name.c_str());
            break;
        }
    }

    prev_print_state = current;
}

ObserverGuard init_print_completion_observer() {
    // Initialize prev_print_state to current state to prevent false trigger on startup
    prev_print_state = static_cast<PrintJobState>(
        lv_subject_get_int(get_printer_state().get_print_state_enum_subject()));
    spdlog::debug("[PrintComplete] Observer registered (initial state={})",
                  static_cast<int>(prev_print_state));
    return ObserverGuard(get_printer_state().get_print_state_enum_subject(),
                         on_print_state_changed_for_notification, nullptr);
}

void cleanup_stale_helix_temp_files(MoonrakerAPI* api) {
    if (!api) {
        spdlog::warn("[PrintComplete] Cannot cleanup stale temp files - API not available");
        return;
    }

    // List files in .helix_temp directory
    api->list_files(
        "gcodes", ".helix_temp", false,
        [api](const std::vector<FileInfo>& files) {
            if (files.empty()) {
                spdlog::debug("[PrintComplete] No stale temp files to clean up");
                return;
            }

            spdlog::info("[PrintComplete] Cleaning up {} stale temp files from .helix_temp",
                         files.size());

            // Delete each file
            for (const auto& file : files) {
                if (file.is_dir) {
                    continue; // Skip directories
                }

                // Moonraker's delete_file requires full path including root
                std::string filepath = "gcodes/.helix_temp/" + file.filename;
                api->delete_file(
                    filepath,
                    [filepath]() {
                        spdlog::debug("[PrintComplete] Deleted stale temp file: {}", filepath);
                    },
                    [filepath](const MoonrakerError& err) {
                        spdlog::warn("[PrintComplete] Failed to delete stale temp file {}: {}",
                                     filepath, err.message);
                    });
            }
        },
        [](const MoonrakerError& err) {
            // Directory doesn't exist or can't be listed - that's fine
            spdlog::debug("[PrintComplete] No .helix_temp directory to clean: {}", err.message);
        });
}

} // namespace helix
