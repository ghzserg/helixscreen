// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_notification_history.h"
#include "ui_notification_history.h"
#include "ui_nav.h"
#include "ui_theme.h"
#include "ui_status_bar.h"
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

// Current filter (-1 = all, or ToastSeverity value)
static int current_filter = -1;
static lv_obj_t* panel_obj = nullptr;

// Helper function to convert severity to color
static const char* severity_to_color(ToastSeverity severity) {
    switch (severity) {
        case ToastSeverity::ERROR:   return "error_color";
        case ToastSeverity::WARNING: return "warning_color";
        case ToastSeverity::SUCCESS: return "success_color";
        case ToastSeverity::INFO:
        default:                      return "info_color";
    }
}

// Helper function to convert severity to icon
static const char* severity_to_icon(ToastSeverity severity) {
    switch (severity) {
        case ToastSeverity::ERROR:   return "\xEF\x81\x81";  // F071 - exclamation-triangle
        case ToastSeverity::WARNING: return "\xEF\x81\xB1";  // F071 - exclamation-triangle
        case ToastSeverity::SUCCESS: return "\xEF\x80\x8C";  // F00C - check
        case ToastSeverity::INFO:
        default:                      return "\xEF\x81\x9A";  // F05A - info-circle
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
static void history_back_clicked(lv_event_t* e) {
    ui_nav_go_back();
}

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
    // Register event callbacks
    lv_xml_register_event_cb(NULL, "history_back_clicked", history_back_clicked);
    lv_xml_register_event_cb(NULL, "history_clear_clicked", history_clear_clicked);
    lv_xml_register_event_cb(NULL, "filter_all_clicked", filter_all_clicked);
    lv_xml_register_event_cb(NULL, "filter_errors_clicked", filter_errors_clicked);
    lv_xml_register_event_cb(NULL, "filter_warnings_clicked", filter_warnings_clicked);
    lv_xml_register_event_cb(NULL, "filter_info_clicked", filter_info_clicked);

    // Create panel from XML
    lv_xml_create(parent, "notification_history_panel", nullptr);
    panel_obj = lv_obj_find_by_name(parent, "notification_history_panel");
    if (!panel_obj) {
        spdlog::error("Failed to create notification_history_panel from XML");
        return nullptr;
    }

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

    // Find list container
    lv_obj_t* list_container = lv_obj_find_by_name(panel_obj, "notification_list_container");
    if (!list_container) {
        spdlog::error("Could not find notification_list_container");
        return;
    }

    // Find empty state
    lv_obj_t* empty_state = lv_obj_find_by_name(panel_obj, "empty_state");

    // Clear existing items
    lv_obj_clean(list_container);

    // Show/hide empty state
    bool has_entries = !entries.empty();
    if (empty_state) {
        if (has_entries) {
            lv_obj_add_flag(empty_state, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(empty_state, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Create list items
    for (const auto& entry : entries) {
        // Get color constant name
        const char* color_const = severity_to_color(entry.severity);

        // Parse to actual color value
        lv_color_t border_color = ui_theme_parse_color(lv_xml_get_const(NULL, color_const));

        // Format color as hex string for XML
        char color_hex[16];
        snprintf(color_hex, sizeof(color_hex), "#%06X", lv_color_to_u32(border_color) & 0xFFFFFF);

        // Format timestamp
        std::string timestamp_str = format_timestamp(entry.timestamp_ms);

        // Use title if present, otherwise use severity-based default
        const char* title = entry.title[0] ? entry.title : "Notification";

        // Build attributes array
        const char* attrs[] = {
            "severity", (entry.severity == ToastSeverity::ERROR) ? "error" :
                       (entry.severity == ToastSeverity::WARNING) ? "warning" :
                       (entry.severity == ToastSeverity::SUCCESS) ? "success" : "info",
            "title", title,
            "message", entry.message,
            "timestamp", timestamp_str.c_str(),
            "border_color", color_hex,
            nullptr
        };

        // Create item from XML
        lv_xml_create(list_container, "notification_history_item", attrs);

        // Find the most recently created item (last child)
        uint32_t child_cnt = lv_obj_get_child_count(list_container);
        lv_obj_t* item = (child_cnt > 0) ? lv_obj_get_child(list_container, child_cnt - 1) : nullptr;
        if (!item) {
            spdlog::error("Failed to create notification_history_item from XML");
            continue;
        }

        // Update severity icon
        lv_obj_t* icon = lv_obj_find_by_name(item, "severity_icon");
        if (icon) {
            lv_label_set_text(icon, severity_to_icon(entry.severity));
            lv_obj_set_style_text_color(icon, border_color, 0);
        }
    }

    // Mark all as read
    NotificationHistory::instance().mark_all_read();

    // Update status bar badge (should now be 0)
    ui_status_bar_update_notification_count(0);

    spdlog::debug("Notification history refreshed: {} entries displayed", entries.size());
}
