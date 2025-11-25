// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_notification_history.h"
#include "ui_notification_history.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_severity_card.h"
#include "ui_status_bar.h"
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

// Current filter (-1 = all, or ToastSeverity value)
static int current_filter = -1;
static lv_obj_t* panel_obj = nullptr;

// Helper function to convert ToastSeverity enum to string for XML
static const char* severity_to_string(ToastSeverity severity) {
    switch (severity) {
        case ToastSeverity::ERROR:   return "error";
        case ToastSeverity::WARNING: return "warning";
        case ToastSeverity::SUCCESS: return "success";
        case ToastSeverity::INFO:
        default:                      return "info";
    }
}

// Helper function to format timestamp
static std::string format_timestamp(uint64_t timestamp_ms) {
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

// Event callbacks
static void history_clear_clicked(lv_event_t* e) {
    NotificationHistory::instance().clear();
    ui_panel_notification_history_refresh();
    spdlog::info("Notification history cleared by user");
}

static void filter_all_clicked(lv_event_t* e) {
    current_filter = -1;
    ui_panel_notification_history_refresh();
}

static void filter_errors_clicked(lv_event_t* e) {
    current_filter = static_cast<int>(ToastSeverity::ERROR);
    ui_panel_notification_history_refresh();
}

static void filter_warnings_clicked(lv_event_t* e) {
    current_filter = static_cast<int>(ToastSeverity::WARNING);
    ui_panel_notification_history_refresh();
}

static void filter_info_clicked(lv_event_t* e) {
    current_filter = static_cast<int>(ToastSeverity::INFO);
    ui_panel_notification_history_refresh();
}

lv_obj_t* ui_panel_notification_history_create(lv_obj_t* parent) {
    // Create panel from XML
    panel_obj = (lv_obj_t*)lv_xml_create(parent, "notification_history_panel", nullptr);
    if (!panel_obj) {
        spdlog::error("Failed to create notification_history_panel from XML");
        return nullptr;
    }

    // Use standard overlay panel setup (wires back button automatically)
    ui_overlay_panel_setup_standard(panel_obj, parent, "overlay_header", "overlay_content");

    // Wire right button ("Clear All") to clear callback
    ui_overlay_panel_wire_right_button(panel_obj, history_clear_clicked, "overlay_header");

    // NOTE: Filter buttons removed from UI for cleaner look.
    // Filter logic below retained for future use if needed.
    // To re-enable: add filter_*_btn widgets to notification_history_panel.xml
#if 0
    lv_obj_t* filter_all = lv_obj_find_by_name(panel_obj, "filter_all_btn");
    if (filter_all) {
        lv_obj_add_event_cb(filter_all, filter_all_clicked, LV_EVENT_CLICKED, nullptr);
    }

    lv_obj_t* filter_errors = lv_obj_find_by_name(panel_obj, "filter_errors_btn");
    if (filter_errors) {
        lv_obj_add_event_cb(filter_errors, filter_errors_clicked, LV_EVENT_CLICKED, nullptr);
    }

    lv_obj_t* filter_warnings = lv_obj_find_by_name(panel_obj, "filter_warnings_btn");
    if (filter_warnings) {
        lv_obj_add_event_cb(filter_warnings, filter_warnings_clicked, LV_EVENT_CLICKED, nullptr);
    }

    lv_obj_t* filter_info = lv_obj_find_by_name(panel_obj, "filter_info_btn");
    if (filter_info) {
        lv_obj_add_event_cb(filter_info, filter_info_clicked, LV_EVENT_CLICKED, nullptr);
    }
#endif

    // Reset filter
    current_filter = -1;

    // Populate list
    ui_panel_notification_history_refresh();

    spdlog::debug("Notification history panel created");
    return panel_obj;
}

void ui_panel_notification_history_refresh() {
    if (!panel_obj) {
        spdlog::warn("Cannot refresh notification history - panel not created");
        return;
    }

    // Get entries (filtered or all)
    auto entries = (current_filter < 0)
        ? NotificationHistory::instance().get_all()
        : NotificationHistory::instance().get_filtered(current_filter);

    // Find content container (items added directly here now)
    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_obj, "overlay_content");
    if (!overlay_content) {
        spdlog::error("Could not find overlay_content");
        return;
    }

    // Find empty state
    lv_obj_t* empty_state = lv_obj_find_by_name(panel_obj, "empty_state");

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
            spdlog::error("Failed to create notification_history_item from XML");
            continue;
        }

        // Finalize severity styling for children (icon text and color)
        ui_severity_card_finalize(item);
    }

    // Mark all as read
    NotificationHistory::instance().mark_all_read();

    // Update status bar - badge count is 0 and bell goes gray (no unread)
    ui_status_bar_update_notification_count(0);
    ui_status_bar_update_notification(NotificationStatus::NONE);

    spdlog::debug("Notification history refreshed: {} entries displayed", entries.size());
}
