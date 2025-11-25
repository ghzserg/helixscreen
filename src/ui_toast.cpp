// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_toast.h"
#include "ui_severity_card.h"
#include "ui_status_bar.h"
#include "ui_notification_history.h"
#include <spdlog/spdlog.h>

// Active toast state
static lv_obj_t* active_toast = nullptr;
static lv_timer_t* dismiss_timer = nullptr;

// Forward declarations
static void toast_dismiss_timer_cb(lv_timer_t* timer);
static void toast_close_btn_clicked(lv_event_t* e);

void ui_toast_init() {
    spdlog::debug("Toast notification system initialized");
}

// Convert ToastSeverity enum to string for XML
static const char* severity_to_string(ToastSeverity severity) {
    switch (severity) {
        case ToastSeverity::ERROR:   return "error";
        case ToastSeverity::WARNING: return "warning";
        case ToastSeverity::SUCCESS: return "success";
        case ToastSeverity::INFO:
        default:                      return "info";
    }
}

static NotificationStatus severity_to_notification_status(ToastSeverity severity) {
    switch (severity) {
        case ToastSeverity::INFO:
            return NotificationStatus::INFO;
        case ToastSeverity::SUCCESS:
            return NotificationStatus::INFO;  // Treat success as info in status bar
        case ToastSeverity::WARNING:
            return NotificationStatus::WARNING;
        case ToastSeverity::ERROR:
            return NotificationStatus::ERROR;
        default:
            return NotificationStatus::NONE;
    }
}

void ui_toast_show(ToastSeverity severity, const char* message, uint32_t duration_ms) {
    if (!message) {
        spdlog::warn("Attempted to show toast with null message");
        return;
    }

    // Hide existing toast if any
    if (active_toast) {
        ui_toast_hide();
    }

    // Create toast via XML component - just pass semantic severity
    const char* attrs[] = {
        "severity", severity_to_string(severity),
        "message", message,
        nullptr
    };

    lv_obj_t* screen = lv_screen_active();
    lv_xml_create(screen, "toast_notification", attrs);

    // Find the created toast (should be last child of screen)
    uint32_t child_cnt = lv_obj_get_child_count(screen);
    active_toast = (child_cnt > 0) ? lv_obj_get_child(screen, child_cnt - 1) : nullptr;

    if (!active_toast) {
        spdlog::error("Failed to create toast notification widget");
        return;
    }

    // Finalize severity styling for children (icon text and color)
    ui_severity_card_finalize(active_toast);

    // Position at top-center below navigation
    const int32_t top_margin = 80;  // Below nav/header area
    lv_obj_align(active_toast, LV_ALIGN_TOP_MID, 0, top_margin);

    // Wire up close button callback
    lv_obj_t* close_btn = lv_obj_find_by_name(active_toast, "toast_close_btn");
    if (close_btn) {
        lv_obj_add_event_cb(close_btn, toast_close_btn_clicked, LV_EVENT_CLICKED, nullptr);
    }

    // Create auto-dismiss timer
    dismiss_timer = lv_timer_create(toast_dismiss_timer_cb, duration_ms, nullptr);
    lv_timer_set_repeat_count(dismiss_timer, 1);  // Run once then stop

    // Update status bar notification icon
    ui_status_bar_update_notification(severity_to_notification_status(severity));

    spdlog::debug("Toast shown: {} ({}ms)", message, duration_ms);
}

void ui_toast_hide() {
    if (!active_toast) {
        return;
    }

    // Cancel dismiss timer if active
    if (dismiss_timer) {
        lv_timer_delete(dismiss_timer);
        dismiss_timer = nullptr;
    }

    // Delete toast widget
    lv_obj_delete(active_toast);
    active_toast = nullptr;

    // Update bell color based on highest unread severity in history
    ToastSeverity highest = NotificationHistory::instance().get_highest_unread_severity();
    size_t unread = NotificationHistory::instance().get_unread_count();

    if (unread == 0) {
        ui_status_bar_update_notification(NotificationStatus::NONE);
    } else {
        ui_status_bar_update_notification(severity_to_notification_status(highest));
    }

    spdlog::debug("Toast hidden");
}

bool ui_toast_is_visible() {
    return active_toast != nullptr;
}

// Timer callback for auto-dismiss
static void toast_dismiss_timer_cb(lv_timer_t* timer) {
    (void)timer;
    ui_toast_hide();
}

// Close button callback
static void toast_close_btn_clicked(lv_event_t* e) {
    (void)e;
    ui_toast_hide();
}
