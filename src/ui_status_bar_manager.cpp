// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_status_bar_manager.h"

#include "ui_nav.h"
#include "ui_panel_notification_history.h"
#include "ui_theme.h"

#include "app_globals.h"
#include "moonraker_client.h"
#include "printer_state.h"
#include "settings_manager.h"

#include <spdlog/spdlog.h>

#include <cstring>

// Forward declaration for class-based API
NotificationHistoryPanel& get_global_notification_history_panel();

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

StatusBarManager& StatusBarManager::instance() {
    static StatusBarManager instance;
    return instance;
}

// ============================================================================
// PRINTER ICON STATE CONSTANTS
// ============================================================================

enum PrinterIconState {
    PRINTER_STATE_READY = 0,       // Green - connected and klippy ready
    PRINTER_STATE_WARNING = 1,     // Orange - startup, reconnecting, was connected
    PRINTER_STATE_ERROR = 2,       // Red - klippy error/shutdown, connection failed
    PRINTER_STATE_DISCONNECTED = 3 // Gray - never connected
};

enum NetworkIconState {
    NETWORK_STATE_CONNECTED = 0,   // Green
    NETWORK_STATE_CONNECTING = 1,  // Orange
    NETWORK_STATE_DISCONNECTED = 2 // Gray
};

enum NotificationSeverityState {
    NOTIFICATION_SEVERITY_INFO = 0,    // Blue badge
    NOTIFICATION_SEVERITY_WARNING = 1, // Orange badge
    NOTIFICATION_SEVERITY_ERROR = 2    // Red badge
};

// ============================================================================
// OBSERVER CALLBACKS (static)
// ============================================================================

void StatusBarManager::network_status_observer([[maybe_unused]] lv_observer_t* observer,
                                               lv_subject_t* subject) {
    int32_t network_state = lv_subject_get_int(subject);
    spdlog::trace("[StatusBarManager] Network observer fired! State: {}", network_state);

    // Map integer to NetworkStatus enum
    NetworkStatus status = static_cast<NetworkStatus>(network_state);
    StatusBarManager::instance().update_network(status);
}

void StatusBarManager::printer_connection_observer([[maybe_unused]] lv_observer_t* observer,
                                                   lv_subject_t* subject) {
    auto& mgr = StatusBarManager::instance();
    mgr.cached_connection_state_ = lv_subject_get_int(subject);
    spdlog::trace("[StatusBarManager] Connection state changed to: {}",
                  mgr.cached_connection_state_);
    mgr.update_printer_icon_combined();
}

void StatusBarManager::klippy_state_observer([[maybe_unused]] lv_observer_t* observer,
                                             lv_subject_t* subject) {
    auto& mgr = StatusBarManager::instance();
    mgr.cached_klippy_state_ = lv_subject_get_int(subject);
    spdlog::trace("[StatusBarManager] Klippy state changed to: {}", mgr.cached_klippy_state_);
    mgr.update_printer_icon_combined();
}

void StatusBarManager::notification_history_clicked([[maybe_unused]] lv_event_t* e) {
    spdlog::info("[StatusBarManager] Notification history button CLICKED!");

    auto& mgr = StatusBarManager::instance();

    // Prevent multiple panel instances - if panel already exists and is visible, ignore click
    if (mgr.notification_panel_obj_ && lv_obj_is_valid(mgr.notification_panel_obj_) &&
        !lv_obj_has_flag(mgr.notification_panel_obj_, LV_OBJ_FLAG_HIDDEN)) {
        spdlog::debug("[StatusBarManager] Notification panel already visible, ignoring click");
        return;
    }

    lv_obj_t* parent = lv_screen_active();

    // Get panel instance and init subjects BEFORE creating XML
    auto& panel = get_global_notification_history_panel();
    if (!panel.are_subjects_initialized()) {
        panel.init_subjects();
    }

    // Clean up old panel if it exists but is hidden/invalid
    if (mgr.notification_panel_obj_) {
        if (lv_obj_is_valid(mgr.notification_panel_obj_)) {
            lv_obj_delete(mgr.notification_panel_obj_);
        }
        mgr.notification_panel_obj_ = nullptr;
    }

    // Now create XML component
    lv_obj_t* panel_obj =
        static_cast<lv_obj_t*>(lv_xml_create(parent, "notification_history_panel", nullptr));
    if (!panel_obj) {
        spdlog::error("[StatusBarManager] Failed to create notification_history_panel from XML");
        return;
    }

    // Store reference for duplicate prevention
    mgr.notification_panel_obj_ = panel_obj;

    // Setup panel (wires buttons, refreshes list)
    panel.setup(panel_obj, parent);

    ui_nav_push_overlay(panel_obj);
}

// ============================================================================
// STATUS BAR MANAGER IMPLEMENTATION
// ============================================================================

void StatusBarManager::register_callbacks() {
    if (callbacks_registered_) {
        spdlog::warn("[StatusBarManager] Callbacks already registered");
        return;
    }

    // Register notification history callback (must be called BEFORE app_layout XML is created)
    lv_xml_register_event_cb(NULL, "status_notification_history_clicked",
                             notification_history_clicked);
    callbacks_registered_ = true;
    spdlog::debug("[StatusBarManager] Event callbacks registered");
}

void StatusBarManager::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[StatusBarManager] Subjects already initialized");
        return;
    }

    spdlog::debug("[StatusBarManager] Initializing status bar subjects...");

    // Initialize all subjects with default values
    // Printer starts disconnected (gray)
    lv_subject_init_int(&printer_icon_state_subject_, PRINTER_STATE_DISCONNECTED);

    // Network starts disconnected (gray)
    lv_subject_init_int(&network_icon_state_subject_, 2); // DISCONNECTED

    // Notification badge starts hidden (count = 0)
    lv_subject_init_int(&notification_count_subject_, 0);
    lv_subject_init_pointer(&notification_count_text_subject_, notification_count_text_buf_);
    lv_subject_init_int(&notification_severity_subject_, 0); // INFO

    // Overlay backdrop starts hidden
    lv_subject_init_int(&overlay_backdrop_visible_subject_, 0);

    // Register subjects for XML binding
    lv_xml_register_subject(NULL, "printer_icon_state", &printer_icon_state_subject_);
    lv_xml_register_subject(NULL, "network_icon_state", &network_icon_state_subject_);
    lv_xml_register_subject(NULL, "notification_count", &notification_count_subject_);
    lv_xml_register_subject(NULL, "notification_count_text", &notification_count_text_subject_);
    lv_xml_register_subject(NULL, "notification_severity", &notification_severity_subject_);
    lv_xml_register_subject(NULL, "overlay_backdrop_visible", &overlay_backdrop_visible_subject_);

    subjects_initialized_ = true;
    spdlog::debug("[StatusBarManager] Subjects initialized and registered");
}

void StatusBarManager::init() {
    if (initialized_) {
        spdlog::warn("[StatusBarManager] Already initialized");
        return;
    }

    spdlog::debug("[StatusBarManager] init() called");

    // Ensure subjects are initialized
    if (!subjects_initialized_) {
        init_subjects();
    }

    // Observe network and printer states from PrinterState
    PrinterState& printer_state = get_printer_state();

    // Network status observer
    lv_subject_t* net_subject = printer_state.get_network_status_subject();
    spdlog::trace("[StatusBarManager] Registering observer on network_status_subject at {}",
                  (void*)net_subject);
    network_observer_ = ObserverGuard(net_subject, network_status_observer, nullptr);

    // Printer connection observer
    lv_subject_t* conn_subject = printer_state.get_printer_connection_state_subject();
    spdlog::trace(
        "[StatusBarManager] Registering observer on printer_connection_state_subject at {}",
        (void*)conn_subject);
    connection_observer_ = ObserverGuard(conn_subject, printer_connection_observer, nullptr);

    // Klippy state observer
    lv_subject_t* klippy_subject = printer_state.get_klippy_state_subject();
    spdlog::trace("[StatusBarManager] Registering observer on klippy_state_subject at {}",
                  (void*)klippy_subject);
    klippy_observer_ = ObserverGuard(klippy_subject, klippy_state_observer, nullptr);

    initialized_ = true;
    spdlog::debug("[StatusBarManager] Initialization complete");
}

void StatusBarManager::set_backdrop_visible(bool visible) {
    if (!subjects_initialized_) {
        spdlog::warn("[StatusBarManager] Subjects not initialized, cannot set backdrop visibility");
        return;
    }

    lv_subject_set_int(&overlay_backdrop_visible_subject_, visible ? 1 : 0);
    spdlog::debug("[StatusBarManager] Overlay backdrop visibility set to: {}", visible);
}

void StatusBarManager::update_network(NetworkStatus status) {
    if (!subjects_initialized_) {
        spdlog::warn("[StatusBarManager] Subjects not initialized, cannot update network icon");
        return;
    }

    int32_t new_state;

    switch (status) {
    case NetworkStatus::CONNECTED:
        new_state = NETWORK_STATE_CONNECTED;
        spdlog::debug("[StatusBarManager] Network status CONNECTED -> state 0");
        break;
    case NetworkStatus::CONNECTING:
        new_state = NETWORK_STATE_CONNECTING;
        spdlog::debug("[StatusBarManager] Network status CONNECTING -> state 1");
        break;
    case NetworkStatus::DISCONNECTED:
    default:
        new_state = NETWORK_STATE_DISCONNECTED;
        spdlog::debug("[StatusBarManager] Network status DISCONNECTED -> state 2");
        break;
    }

    lv_subject_set_int(&network_icon_state_subject_, new_state);
}

void StatusBarManager::update_printer(PrinterStatus status) {
    spdlog::debug("[StatusBarManager] update_printer() called with status={}",
                  static_cast<int>(status));
    // Delegate to the combined logic which uses observers
    update_printer_icon_combined();
}

void StatusBarManager::update_notification(NotificationStatus status) {
    if (!subjects_initialized_) {
        spdlog::warn("[StatusBarManager] Subjects not initialized, cannot update notification");
        return;
    }

    int32_t severity;

    switch (status) {
    case NotificationStatus::ERROR:
        severity = NOTIFICATION_SEVERITY_ERROR;
        spdlog::debug("[StatusBarManager] Notification severity ERROR -> state 2");
        break;
    case NotificationStatus::WARNING:
        severity = NOTIFICATION_SEVERITY_WARNING;
        spdlog::debug("[StatusBarManager] Notification severity WARNING -> state 1");
        break;
    case NotificationStatus::INFO:
    case NotificationStatus::NONE:
    default:
        severity = NOTIFICATION_SEVERITY_INFO;
        spdlog::debug("[StatusBarManager] Notification severity INFO -> state 0");
        break;
    }

    lv_subject_set_int(&notification_severity_subject_, severity);
}

void StatusBarManager::update_notification_count(size_t count) {
    if (!subjects_initialized_) {
        spdlog::trace(
            "[StatusBarManager] Subjects not initialized, cannot update notification count");
        return;
    }

    // Trigger pulse animation if count increased (new notification arrived)
    bool should_pulse = (count > previous_notification_count_) && (count > 0);
    previous_notification_count_ = count;

    lv_subject_set_int(&notification_count_subject_, static_cast<int32_t>(count));

    snprintf(notification_count_text_buf_, sizeof(notification_count_text_buf_), "%zu", count);
    lv_subject_set_pointer(&notification_count_text_subject_, notification_count_text_buf_);

    // Pulse the badge to draw attention
    if (should_pulse) {
        animate_notification_badge();
    }

    spdlog::trace("[StatusBarManager] Notification count updated: {}", count);
}

void StatusBarManager::animate_notification_badge() {
    // Skip animation if disabled
    if (!SettingsManager::instance().get_animations_enabled()) {
        spdlog::debug("[StatusBarManager] Animations disabled - skipping badge pulse");
        return;
    }

    // Find the notification badge on the active screen
    lv_obj_t* screen = lv_screen_active();
    if (!screen)
        return;

    lv_obj_t* badge = lv_obj_find_by_name(screen, "notification_badge");
    if (!badge)
        return;

    // Animation constants for attention pulse
    // Stage 1: Scale up to 130% (300ms with overshoot)
    // Stage 2: Scale back to 100% (implicit - overshoot settles naturally)
    constexpr int32_t PULSE_DURATION_MS = 300;
    constexpr int32_t SCALE_NORMAL = 256; // 100%
    constexpr int32_t SCALE_PULSE = 333;  // ~130%

    // Scale up animation with overshoot easing (automatically bounces back)
    lv_anim_t scale_anim;
    lv_anim_init(&scale_anim);
    lv_anim_set_var(&scale_anim, badge);
    lv_anim_set_values(&scale_anim, SCALE_NORMAL, SCALE_PULSE);
    lv_anim_set_duration(&scale_anim, PULSE_DURATION_MS);
    lv_anim_set_path_cb(&scale_anim, lv_anim_path_overshoot);
    lv_anim_set_playback_duration(&scale_anim, PULSE_DURATION_MS / 2);
    lv_anim_set_exec_cb(&scale_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
    });
    lv_anim_start(&scale_anim);

    spdlog::debug("[StatusBarManager] Notification badge pulse animation started");
}

void StatusBarManager::update_printer_icon_combined() {
    int32_t new_state;

    if (cached_connection_state_ == static_cast<int>(ConnectionState::CONNECTED)) {
        switch (cached_klippy_state_) {
        case static_cast<int>(KlippyState::STARTUP):
            new_state = PRINTER_STATE_WARNING;
            spdlog::debug("[StatusBarManager] Klippy STARTUP -> printer state WARNING");
            break;
        case static_cast<int>(KlippyState::SHUTDOWN):
        case static_cast<int>(KlippyState::ERROR):
            new_state = PRINTER_STATE_ERROR;
            spdlog::debug("[StatusBarManager] Klippy SHUTDOWN/ERROR -> printer state ERROR");
            break;
        case static_cast<int>(KlippyState::READY):
        default:
            new_state = PRINTER_STATE_READY;
            spdlog::debug("[StatusBarManager] Klippy READY -> printer state READY");
            break;
        }
    } else if (cached_connection_state_ == static_cast<int>(ConnectionState::FAILED)) {
        new_state = PRINTER_STATE_ERROR;
        spdlog::debug("[StatusBarManager] Connection FAILED -> printer state ERROR");
    } else { // DISCONNECTED, CONNECTING, RECONNECTING
        if (get_printer_state().was_ever_connected()) {
            new_state = PRINTER_STATE_WARNING;
            spdlog::trace(
                "[StatusBarManager] Disconnected (was connected) -> printer state WARNING");
        } else {
            new_state = PRINTER_STATE_DISCONNECTED;
            spdlog::trace("[StatusBarManager] Never connected -> printer state DISCONNECTED");
        }
    }

    if (subjects_initialized_) {
        lv_subject_set_int(&printer_icon_state_subject_, new_state);
    }
}

// ============================================================================
// LEGACY API (forwards to StatusBarManager)
// ============================================================================

void ui_status_bar_register_callbacks() {
    StatusBarManager::instance().register_callbacks();
}

void ui_status_bar_init_subjects() {
    StatusBarManager::instance().init_subjects();
}

void ui_status_bar_init() {
    StatusBarManager::instance().init();
}

void ui_status_bar_set_backdrop_visible(bool visible) {
    StatusBarManager::instance().set_backdrop_visible(visible);
}

void ui_status_bar_update_network(NetworkStatus status) {
    StatusBarManager::instance().update_network(status);
}

void ui_status_bar_update_printer(PrinterStatus status) {
    StatusBarManager::instance().update_printer(status);
}

void ui_status_bar_update_notification(NotificationStatus status) {
    StatusBarManager::instance().update_notification(status);
}

void ui_status_bar_update_notification_count(size_t count) {
    StatusBarManager::instance().update_notification_count(count);
}
