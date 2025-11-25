// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#include "ui_status_bar.h"
#include "ui_theme.h"
#include "ui_nav.h"
#include "ui_panel_notification_history.h"
#include "app_globals.h"
#include <spdlog/spdlog.h>

// Cached widget references
static lv_obj_t* network_icon = nullptr;
static lv_obj_t* printer_icon = nullptr;
static lv_obj_t* notification_icon = nullptr;
static lv_obj_t* notification_badge = nullptr;
static lv_obj_t* notification_badge_count = nullptr;

// Observer callback for network state changes
static void network_status_observer(lv_observer_t* observer, lv_subject_t* subject) {
    int32_t network_state = lv_subject_get_int(subject);
    spdlog::debug("[StatusBar] Network observer fired! State: {}", network_state);

    // Map integer to NetworkStatus enum
    NetworkStatus status = static_cast<NetworkStatus>(network_state);
    ui_status_bar_update_network(status);
}

// Observer callback for printer connection state changes
static void printer_connection_observer(lv_observer_t* observer, lv_subject_t* subject) {
    int32_t connection_state = lv_subject_get_int(subject);

    spdlog::debug("[StatusBar] Observer fired! Connection state changed to: {}", connection_state);

    // Map MoonrakerClient::ConnectionState to PrinterStatus
    // ConnectionState: 0=DISCONNECTED, 1=CONNECTING, 2=CONNECTED, 3=RECONNECTING, 4=FAILED
    PrinterStatus status;
    switch (connection_state) {
        case 2: // CONNECTED
            status = PrinterStatus::READY;
            spdlog::debug("[StatusBar] Mapped state 2 (CONNECTED) -> PrinterStatus::READY");
            break;
        case 4: // FAILED
            status = PrinterStatus::ERROR;
            spdlog::debug("[StatusBar] Mapped state 4 (FAILED) -> PrinterStatus::ERROR");
            break;
        case 0: // DISCONNECTED
        case 1: // CONNECTING
        case 3: // RECONNECTING
        default:
            status = PrinterStatus::DISCONNECTED;
            spdlog::debug("[StatusBar] Mapped state {} -> PrinterStatus::DISCONNECTED", connection_state);
            break;
    }

    spdlog::debug("[StatusBar] Calling ui_status_bar_update_printer() with status={}", static_cast<int>(status));
    ui_status_bar_update_printer(status);
}

// Event callback for notification history button
static void status_notification_history_clicked(lv_event_t* e) {
    lv_obj_t* parent = lv_screen_active();
    lv_obj_t* panel = ui_panel_notification_history_create(parent);
    if (panel) {
        ui_nav_push_overlay(panel);
    }
}

void ui_status_bar_register_callbacks() {
    // Register notification history callback (must be called BEFORE app_layout XML is created)
    lv_xml_register_event_cb(NULL, "status_notification_history_clicked", status_notification_history_clicked);
    spdlog::debug("[StatusBar] Event callbacks registered");
}

void ui_status_bar_init() {
    spdlog::debug("[StatusBar] ui_status_bar_init() called");

    // First, find the status_bar container itself
    lv_obj_t* status_bar = lv_obj_find_by_name(lv_screen_active(), "status_bar");
    if (!status_bar) {
        spdlog::error("[StatusBar] Failed to find status_bar container - cannot initialize");
        return;
    }
    spdlog::debug("[StatusBar] Found status_bar container at {}", (void*)status_bar);

    // Find status bar icons by name (search within status_bar container)
    network_icon = lv_obj_find_by_name(status_bar, "status_network_icon");
    printer_icon = lv_obj_find_by_name(status_bar, "status_printer_icon");

    // Bell icon and badge are nested in status_notification_history_container
    lv_obj_t* notif_container = lv_obj_find_by_name(status_bar, "status_notification_history_container");
    if (notif_container) {
        notification_icon = lv_obj_find_by_name(notif_container, "status_notification_icon");
        notification_badge = lv_obj_find_by_name(notif_container, "notification_badge");
        if (notification_badge) {
            notification_badge_count = lv_obj_find_by_name(notification_badge, "notification_badge_count");
        }
    }

    spdlog::debug("[StatusBar] Widget lookup: network_icon={}, printer_icon={}, notification_icon={}",
                  (void*)network_icon, (void*)printer_icon, (void*)notification_icon);

    if (!network_icon || !printer_icon || !notification_icon) {
        spdlog::error("[StatusBar] Failed to find status bar icon widgets");
        return;
    }

    if (!notification_badge || !notification_badge_count) {
        spdlog::warn("[StatusBar] Failed to find notification badge widgets");
    }

    // Observe network and printer states for reactive icon updates
    PrinterState& printer_state = get_printer_state();

    // Network status observer
    lv_subject_t* net_subject = printer_state.get_network_status_subject();
    spdlog::debug("[StatusBar] Registering observer on network_status_subject at {}", (void*)net_subject);
    lv_subject_add_observer(net_subject, network_status_observer, nullptr);

    // Trigger initial network update
    int32_t initial_net_state = lv_subject_get_int(net_subject);
    spdlog::debug("[StatusBar] Initial network state: {}", initial_net_state);
    ui_status_bar_update_network(static_cast<NetworkStatus>(initial_net_state));

    // Printer connection observer
    lv_subject_t* conn_subject = printer_state.get_printer_connection_state_subject();
    spdlog::debug("[StatusBar] Registering observer on printer_connection_state_subject at {}", (void*)conn_subject);
    lv_subject_add_observer(conn_subject, printer_connection_observer, nullptr);

    // Trigger initial printer update
    int32_t initial_state = lv_subject_get_int(conn_subject);
    spdlog::debug("[StatusBar] Initial printer connection state from subject: {}", initial_state);
    spdlog::debug("[StatusBar] Triggering initial update with PrinterStatus={}", static_cast<int>(static_cast<PrinterStatus>(initial_state)));
    ui_status_bar_update_printer(static_cast<PrinterStatus>(initial_state));

    spdlog::debug("[StatusBar] Initialization complete");
}

void ui_status_bar_update_network(NetworkStatus status) {
    if (!network_icon) {
        spdlog::warn("Status bar not initialized, cannot update network icon");
        return;
    }

    // Network icon is a Font Awesome label showing link symbol (generic network indicator)
    lv_color_t color;

    switch (status) {
        case NetworkStatus::CONNECTED:
            color = ui_theme_parse_color(lv_xml_get_const(NULL, "success_color"));
            break;
        case NetworkStatus::CONNECTING:
            color = ui_theme_parse_color(lv_xml_get_const(NULL, "warning_color"));
            break;
        case NetworkStatus::DISCONNECTED:
        default:
            color = ui_theme_parse_color(lv_xml_get_const(NULL, "text_secondary"));
            break;
    }

    // Update text color for the Font Awesome label
    lv_obj_set_style_text_color(network_icon, color, 0);
}

void ui_status_bar_update_printer(PrinterStatus status) {
    spdlog::debug("[StatusBar] ui_status_bar_update_printer() called with status={}", static_cast<int>(status));

    if (!printer_icon) {
        spdlog::warn("[StatusBar] printer_icon is NULL, cannot update");
        return;
    }

    const char* icon = nullptr;
    lv_color_t color;

    switch (status) {
        case PrinterStatus::READY:
            icon = LV_SYMBOL_OK;
            color = ui_theme_parse_color(lv_xml_get_const(NULL, "success_color"));
            spdlog::debug("[StatusBar] Setting icon to LV_SYMBOL_OK (green checkmark)");
            break;
        case PrinterStatus::PRINTING:
            icon = LV_SYMBOL_PLAY;
            color = ui_theme_parse_color(lv_xml_get_const(NULL, "info_color"));
            spdlog::debug("[StatusBar] Setting icon to LV_SYMBOL_PLAY (blue play)");
            break;
        case PrinterStatus::ERROR:
            icon = LV_SYMBOL_WARNING;
            color = ui_theme_parse_color(lv_xml_get_const(NULL, "error_color"));
            spdlog::debug("[StatusBar] Setting icon to LV_SYMBOL_WARNING (red)");
            break;
        case PrinterStatus::DISCONNECTED:
        default:
            icon = LV_SYMBOL_WARNING;
            color = ui_theme_parse_color(lv_xml_get_const(NULL, "warning_color"));
            spdlog::debug("[StatusBar] Setting icon to LV_SYMBOL_WARNING (yellow)");
            break;
    }

    spdlog::debug("[StatusBar] Setting text on printer_icon widget at {}", (void*)printer_icon);
    lv_label_set_text(printer_icon, icon);
    lv_obj_set_style_text_color(printer_icon, color, 0);
    spdlog::debug("[StatusBar] Printer icon updated successfully");
}

void ui_status_bar_update_notification(NotificationStatus status) {
    if (!notification_icon) {
        spdlog::warn("Status bar not initialized, cannot update notification icon");
        return;
    }

    // Bell is always visible - just change color based on highest severity
    // Red = error, Yellow = warning, Gray = info/none
    lv_color_t color;

    switch (status) {
        case NotificationStatus::ERROR:
            color = ui_theme_parse_color(lv_xml_get_const(NULL, "error_color"));
            break;
        case NotificationStatus::WARNING:
            color = ui_theme_parse_color(lv_xml_get_const(NULL, "warning_color"));
            break;
        case NotificationStatus::INFO:
        case NotificationStatus::NONE:
        default:
            color = ui_theme_parse_color(lv_xml_get_const(NULL, "text_secondary"));
            break;
    }

    lv_obj_set_style_text_color(notification_icon, color, 0);
}

void ui_status_bar_update_notification_count(size_t count) {
    if (!notification_badge || !notification_badge_count) {
        spdlog::trace("Notification badge widgets not available");
        return;
    }

    if (count == 0) {
        // Hide badge when no unread notifications
        lv_obj_add_flag(notification_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Show badge and update count
        lv_obj_remove_flag(notification_badge, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(notification_badge_count, "%zu", count);
    }

    spdlog::trace("Notification count updated: {}", count);
}
