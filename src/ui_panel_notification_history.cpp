// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_notification_history.h"

#include "app_globals.h"
#include "printer_state.h"
#include "ui_event_safety.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_severity_card.h"
#include "ui_status_bar.h"

#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

#include <memory>

// ============================================================================
// CONSTRUCTOR
// ============================================================================

NotificationHistoryPanel::NotificationHistoryPanel(PrinterState& printer_state, MoonrakerAPI* api,
                                                   NotificationHistory& history)
    : PanelBase(printer_state, api), history_(history) {
    // Dependencies stored for use in refresh()
}

// ============================================================================
// PANELBASE IMPLEMENTATION
// ============================================================================

void NotificationHistoryPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // No subjects needed for this panel
    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized (none required)", get_name());
}

void NotificationHistoryPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    // Use standard overlay panel setup (wires back button automatically)
    ui_overlay_panel_setup_standard(panel_, parent_screen_, "overlay_header", "overlay_content");

    // Wire right button ("Clear All") to clear callback
    ui_overlay_panel_wire_right_button(panel_, on_clear_clicked, "overlay_header", this);

    // NOTE: Filter buttons removed from UI for cleaner look.
    // Filter logic retained for future use if needed.
#if 0
    lv_obj_t* filter_all = lv_obj_find_by_name(panel_, "filter_all_btn");
    if (filter_all) {
        lv_obj_add_event_cb(filter_all, on_filter_all_clicked, LV_EVENT_CLICKED, this);
    }

    lv_obj_t* filter_errors = lv_obj_find_by_name(panel_, "filter_errors_btn");
    if (filter_errors) {
        lv_obj_add_event_cb(filter_errors, on_filter_errors_clicked, LV_EVENT_CLICKED, this);
    }

    lv_obj_t* filter_warnings = lv_obj_find_by_name(panel_, "filter_warnings_btn");
    if (filter_warnings) {
        lv_obj_add_event_cb(filter_warnings, on_filter_warnings_clicked, LV_EVENT_CLICKED, this);
    }

    lv_obj_t* filter_info = lv_obj_find_by_name(panel_, "filter_info_btn");
    if (filter_info) {
        lv_obj_add_event_cb(filter_info, on_filter_info_clicked, LV_EVENT_CLICKED, this);
    }
#endif

    // Reset filter
    current_filter_ = -1;

    // Populate list
    refresh();

    spdlog::info("[{}] Setup complete", get_name());
}

// ============================================================================
// PUBLIC API
// ============================================================================

void NotificationHistoryPanel::refresh() {
    if (!panel_) {
        spdlog::warn("[{}] Cannot refresh - panel not created", get_name());
        return;
    }

    // Get entries (filtered or all)
    auto entries = (current_filter_ < 0)
        ? history_.get_all()
        : history_.get_filtered(current_filter_);

    // Find content container
    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_, "overlay_content");
    if (!overlay_content) {
        spdlog::error("[{}] Could not find overlay_content", get_name());
        return;
    }

    // Find empty state
    lv_obj_t* empty_state = lv_obj_find_by_name(panel_, "empty_state");

    // Clear existing items from content area
    lv_obj_clean(overlay_content);

    // Show/hide content vs empty state
    bool has_entries = !entries.empty();
    if (has_entries) {
        lv_obj_remove_flag(overlay_content, LV_OBJ_FLAG_HIDDEN);
        if (empty_state) lv_obj_add_flag(empty_state, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(overlay_content, LV_OBJ_FLAG_HIDDEN);
        if (empty_state) lv_obj_remove_flag(empty_state, LV_OBJ_FLAG_HIDDEN);
    }

    // Create list items using severity_card for automatic color styling
    for (const auto& entry : entries) {
        // Format timestamp
        std::string timestamp_str = format_timestamp(entry.timestamp_ms);

        // Use title if present, otherwise use severity-based default
        const char* title = entry.title[0] ? entry.title : "Notification";

        // Build attributes array - just pass semantic severity, widget handles colors
        const char* attrs[] = {
            "severity", severity_to_string(entry.severity),
            "title", title,
            "message", entry.message,
            "timestamp", timestamp_str.c_str(),
            nullptr
        };

        // Create item from XML (severity_card sets border color automatically)
        lv_xml_create(overlay_content, "notification_history_item", attrs);

        // Find the most recently created item (last child)
        uint32_t child_cnt = lv_obj_get_child_count(overlay_content);
        lv_obj_t* item = (child_cnt > 0) ? lv_obj_get_child(overlay_content, child_cnt - 1) : nullptr;
        if (!item) {
            spdlog::error("[{}] Failed to create notification_history_item from XML", get_name());
            continue;
        }

        // Finalize severity styling for children (icon text and color)
        ui_severity_card_finalize(item);
    }

    // Mark all as read
    history_.mark_all_read();

    // Update status bar - badge count is 0 and bell goes gray (no unread)
    ui_status_bar_update_notification_count(0);
    ui_status_bar_update_notification(NotificationStatus::NONE);

    spdlog::debug("[{}] Refreshed: {} entries displayed", get_name(), entries.size());
}

void NotificationHistoryPanel::set_filter(int filter) {
    if (current_filter_ != filter) {
        current_filter_ = filter;
        refresh();
    }
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

const char* NotificationHistoryPanel::severity_to_string(ToastSeverity severity) {
    switch (severity) {
        case ToastSeverity::ERROR:   return "error";
        case ToastSeverity::WARNING: return "warning";
        case ToastSeverity::SUCCESS: return "success";
        case ToastSeverity::INFO:
        default:                      return "info";
    }
}

std::string NotificationHistoryPanel::format_timestamp(uint64_t timestamp_ms) {
    uint64_t now = lv_tick_get();

    // Handle edge case where timestamp is in future (shouldn't happen)
    if (timestamp_ms > now) {
        return "Just now";
    }

    uint64_t diff_ms = now - timestamp_ms;

    if (diff_ms < 60000) {  // < 1 min
        return "Just now";
    } else if (diff_ms < 3600000) {  // < 1 hour
        return fmt::format("{} min ago", diff_ms / 60000);
    } else if (diff_ms < 86400000) {  // < 1 day
        uint64_t hours = diff_ms / 3600000;
        return fmt::format("{} hour{} ago", hours, hours > 1 ? "s" : "");
    } else {
        uint64_t days = diff_ms / 86400000;
        return fmt::format("{} day{} ago", days, days > 1 ? "s" : "");
    }
}

// ============================================================================
// BUTTON HANDLERS
// ============================================================================

void NotificationHistoryPanel::handle_clear_clicked() {
    history_.clear();
    refresh();
    spdlog::info("[{}] History cleared by user", get_name());
}

void NotificationHistoryPanel::handle_filter_all() {
    set_filter(-1);
}

void NotificationHistoryPanel::handle_filter_errors() {
    set_filter(static_cast<int>(ToastSeverity::ERROR));
}

void NotificationHistoryPanel::handle_filter_warnings() {
    set_filter(static_cast<int>(ToastSeverity::WARNING));
}

void NotificationHistoryPanel::handle_filter_info() {
    set_filter(static_cast<int>(ToastSeverity::INFO));
}

// ============================================================================
// STATIC TRAMPOLINES
// ============================================================================

void NotificationHistoryPanel::on_clear_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[NotificationHistoryPanel] on_clear_clicked");
    auto* self = static_cast<NotificationHistoryPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_clear_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void NotificationHistoryPanel::on_filter_all_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[NotificationHistoryPanel] on_filter_all_clicked");
    auto* self = static_cast<NotificationHistoryPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_filter_all();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void NotificationHistoryPanel::on_filter_errors_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[NotificationHistoryPanel] on_filter_errors_clicked");
    auto* self = static_cast<NotificationHistoryPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_filter_errors();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void NotificationHistoryPanel::on_filter_warnings_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[NotificationHistoryPanel] on_filter_warnings_clicked");
    auto* self = static_cast<NotificationHistoryPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_filter_warnings();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void NotificationHistoryPanel::on_filter_info_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[NotificationHistoryPanel] on_filter_info_clicked");
    auto* self = static_cast<NotificationHistoryPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_filter_info();
    }
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// GLOBAL INSTANCE (needed by main.cpp)
// ============================================================================

static std::unique_ptr<NotificationHistoryPanel> g_notification_history_panel;

NotificationHistoryPanel& get_global_notification_history_panel() {
    if (!g_notification_history_panel) {
        g_notification_history_panel = std::make_unique<NotificationHistoryPanel>(
            get_printer_state(), nullptr);
    }
    return *g_notification_history_panel;
}
