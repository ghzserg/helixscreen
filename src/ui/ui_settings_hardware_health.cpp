// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_settings_hardware_health.cpp
 * @brief Implementation of HardwareHealthOverlay
 */

#include "ui_settings_hardware_health.h"

#include "ui_event_safety.h"
#include "ui_modal.h"
#include "ui_nav_manager.h"
#include "ui_severity_card.h"
#include "ui_toast.h"

#include "config.h"
#include "hardware_validator.h"
#include "printer_state.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <memory>

namespace helix::settings {

// ============================================================================
// SINGLETON ACCESSOR
// ============================================================================

static std::unique_ptr<HardwareHealthOverlay> g_hardware_health_overlay;

HardwareHealthOverlay& get_hardware_health_overlay() {
    if (!g_hardware_health_overlay) {
        g_hardware_health_overlay = std::make_unique<HardwareHealthOverlay>();
        StaticPanelRegistry::instance().register_destroy(
            "HardwareHealthOverlay", []() { g_hardware_health_overlay.reset(); });
    }
    return *g_hardware_health_overlay;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

HardwareHealthOverlay::HardwareHealthOverlay() {
    spdlog::debug("[{}] Created", get_name());
}

HardwareHealthOverlay::~HardwareHealthOverlay() {
    spdlog::debug("[{}] Destroyed", get_name());
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void HardwareHealthOverlay::register_callbacks() {
    // No XML callbacks needed - on_hardware_health_clicked is registered in SettingsPanel
    // Action button callbacks use lv_obj_add_event_cb (dynamic row creation)
    spdlog::debug("[{}] Callbacks registered", get_name());
}

// ============================================================================
// UI CREATION
// ============================================================================

lv_obj_t* HardwareHealthOverlay::create(lv_obj_t* parent) {
    if (overlay_) {
        spdlog::warn("[{}] create() called but overlay already exists", get_name());
        return overlay_;
    }

    spdlog::debug("[{}] Creating overlay...", get_name());

    // Create from XML component
    overlay_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "hardware_health_overlay", nullptr));
    if (!overlay_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    // Initially hidden until show() pushes it
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);

    spdlog::info("[{}] Overlay created", get_name());
    return overlay_;
}

void HardwareHealthOverlay::show(lv_obj_t* parent_screen) {
    spdlog::debug("[{}] show() called", get_name());

    parent_screen_ = parent_screen;

    // Lazy create overlay
    if (!overlay_ && parent_screen_) {
        create(parent_screen_);
    }

    if (!overlay_) {
        spdlog::error("[{}] Cannot show - overlay not created", get_name());
        return;
    }

    // Populate issues from validation result
    populate_hardware_issues();

    // Push onto navigation stack
    ui_nav_push_overlay(overlay_);
}

// ============================================================================
// INTERNAL METHODS
// ============================================================================

void HardwareHealthOverlay::populate_hardware_issues() {
    if (!overlay_) {
        return;
    }

    if (!printer_state_) {
        spdlog::error("[{}] Cannot populate - printer_state_ not set", get_name());
        return;
    }

    const auto& result = printer_state_->get_hardware_validation_result();

    // Helper to convert severity enum to string for XML attribute
    auto severity_to_string = [](HardwareIssueSeverity sev) -> const char* {
        switch (sev) {
        case HardwareIssueSeverity::CRITICAL:
            return "error";
        case HardwareIssueSeverity::WARNING:
            return "warning";
        case HardwareIssueSeverity::INFO:
        default:
            return "info";
        }
    };

    // Helper to populate a list with issues
    auto populate_list = [&](const char* list_name, const std::vector<HardwareIssue>& issues) {
        lv_obj_t* list = lv_obj_find_by_name(overlay_, list_name);
        if (!list) {
            spdlog::warn("[{}] Could not find list: {}", get_name(), list_name);
            return;
        }

        // Clear existing children
        lv_obj_clean(list);

        // Add issue rows
        for (const auto& issue : issues) {
            // Create row with severity attribute for colored left border
            const char* attrs[] = {"severity", severity_to_string(issue.severity), nullptr};
            lv_obj_t* row =
                static_cast<lv_obj_t*>(lv_xml_create(list, "hardware_issue_row", attrs));
            if (!row) {
                continue;
            }

            // Finalize severity_card to show correct icon
            ui_severity_card_finalize(row);

            // Set hardware name
            lv_obj_t* name_label = lv_obj_find_by_name(row, "hardware_name");
            if (name_label) {
                lv_label_set_text(name_label, issue.hardware_name.c_str());
            }

            // Set issue message
            lv_obj_t* message_label = lv_obj_find_by_name(row, "issue_message");
            if (message_label) {
                lv_label_set_text(message_label, issue.message.c_str());
            }

            // Configure action buttons for non-critical issues
            if (issue.severity != HardwareIssueSeverity::CRITICAL) {
                lv_obj_t* action_buttons = lv_obj_find_by_name(row, "action_buttons");
                lv_obj_t* ignore_btn = lv_obj_find_by_name(row, "ignore_btn");
                lv_obj_t* save_btn = lv_obj_find_by_name(row, "save_btn");

                if (action_buttons && ignore_btn) {
                    // Show button container
                    lv_obj_clear_flag(action_buttons, LV_OBJ_FLAG_HIDDEN);

                    // Show Save button only for INFO severity (newly discovered)
                    if (save_btn && issue.severity == HardwareIssueSeverity::INFO) {
                        lv_obj_clear_flag(save_btn, LV_OBJ_FLAG_HIDDEN);
                    }

                    // Store hardware name in row for callback (freed on row delete)
                    char* name_copy = strdup(issue.hardware_name.c_str());
                    if (!name_copy) {
                        spdlog::error("[{}] Failed to allocate memory for hardware name",
                                      get_name());
                        continue;
                    }
                    lv_obj_set_user_data(row, name_copy);

                    // Add delete handler to free the strdup'd name
                    // (acceptable exception to declarative UI rule for cleanup)
                    lv_obj_add_event_cb(
                        row,
                        [](lv_event_t* e) {
                            auto* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                            void* data = lv_obj_get_user_data(obj);
                            if (data) {
                                free(data);
                            }
                        },
                        LV_EVENT_DELETE, nullptr);

                    // Helper lambda for button click handlers
                    // (acceptable exception to declarative UI rule for dynamic rows)
                    auto add_button_handler = [&](lv_obj_t* btn, bool is_ignore) {
                        lv_obj_add_event_cb(
                            btn,
                            [](lv_event_t* e) {
                                LVGL_SAFE_EVENT_CB_BEGIN("[HardwareHealthOverlay] action_clicked");
                                auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                                // Navigate up: btn -> action_buttons -> row
                                lv_obj_t* action_container = lv_obj_get_parent(btn);
                                lv_obj_t* row = lv_obj_get_parent(action_container);
                                const char* hw_name =
                                    static_cast<const char*>(lv_obj_get_user_data(row));
                                bool is_ignore = static_cast<bool>(
                                    reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));

                                if (hw_name) {
                                    get_hardware_health_overlay().handle_hardware_action(hw_name,
                                                                                         is_ignore);
                                }
                                LVGL_SAFE_EVENT_CB_END();
                            },
                            LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<uintptr_t>(is_ignore)));
                    };

                    // Wire up Ignore button (always visible for non-critical)
                    add_button_handler(ignore_btn, true);

                    // Wire up Save button (only for INFO severity)
                    if (save_btn && issue.severity == HardwareIssueSeverity::INFO) {
                        add_button_handler(save_btn, false);
                    }
                }
            }
        }
    };

    // Populate each section
    populate_list("critical_issues_list", result.critical_missing);
    populate_list("warning_issues_list", result.expected_missing);
    populate_list("info_issues_list", result.newly_discovered);
    populate_list("session_issues_list", result.changed_from_last_session);

    spdlog::debug("[{}] Populated hardware issues: {} critical, {} warning, {} info, {} session",
                  get_name(), result.critical_missing.size(), result.expected_missing.size(),
                  result.newly_discovered.size(), result.changed_from_last_session.size());
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

void HardwareHealthOverlay::handle_hardware_action(const char* hardware_name, bool is_ignore) {
    if (!hardware_name) {
        return;
    }

    Config* config = Config::get_instance();
    std::string hw_name(hardware_name);

    if (is_ignore) {
        // "Ignore" - Mark hardware as optional (no confirmation needed)
        HardwareValidator::set_hardware_optional(config, hw_name, true);
        ui_toast_show(ToastSeverity::SUCCESS, "Hardware marked as optional", 2000);
        spdlog::info("[{}] Marked hardware '{}' as optional", get_name(), hw_name);

        // Remove from cached validation result and refresh overlay
        if (printer_state_) {
            printer_state_->remove_hardware_issue(hw_name);
        }
        populate_hardware_issues();
    } else {
        // "Save" - Add to expected hardware (with confirmation)
        // Close any existing dialog first (must happen before writing to static buffer)
        if (hardware_save_dialog_) {
            ui_modal_hide(hardware_save_dialog_);
            hardware_save_dialog_ = nullptr;
        }

        // Store name for confirmation callback
        pending_hardware_save_ = hw_name;

        // Static message buffer (safe since we close existing dialogs first)
        static char message_buf[256];
        snprintf(message_buf, sizeof(message_buf),
                 "Add '%s' to expected hardware?\n\nYou'll be notified if it's removed later.",
                 hw_name.c_str());

        // Show confirmation dialog
        hardware_save_dialog_ =
            ui_modal_show_confirmation("Save Hardware", message_buf, ModalSeverity::Info, "Save",
                                       on_hardware_save_confirm, on_hardware_save_cancel, this);
    }
}

void HardwareHealthOverlay::handle_hardware_save_confirm() {
    // Close dialog first
    if (hardware_save_dialog_) {
        ui_modal_hide(hardware_save_dialog_);
        hardware_save_dialog_ = nullptr;
    }

    Config* cfg = Config::get_instance();

    // Add to expected hardware list
    HardwareValidator::add_expected_hardware(cfg, pending_hardware_save_);
    ui_toast_show(ToastSeverity::SUCCESS, "Hardware saved to config", 2000);
    spdlog::info("[{}] Added hardware '{}' to expected list", get_name(), pending_hardware_save_);

    // Remove from cached validation result and refresh overlay
    if (printer_state_) {
        printer_state_->remove_hardware_issue(pending_hardware_save_);
    }
    populate_hardware_issues();
    pending_hardware_save_.clear();
}

void HardwareHealthOverlay::handle_hardware_save_cancel() {
    // Close dialog
    if (hardware_save_dialog_) {
        ui_modal_hide(hardware_save_dialog_);
        hardware_save_dialog_ = nullptr;
    }

    pending_hardware_save_.clear();
}

// ============================================================================
// STATIC CALLBACKS
// ============================================================================

void HardwareHealthOverlay::on_hardware_save_confirm(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HardwareHealthOverlay] on_hardware_save_confirm");
    auto* self = static_cast<HardwareHealthOverlay*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_hardware_save_confirm();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void HardwareHealthOverlay::on_hardware_save_cancel(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HardwareHealthOverlay] on_hardware_save_cancel");
    auto* self = static_cast<HardwareHealthOverlay*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_hardware_save_cancel();
    }
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::settings
