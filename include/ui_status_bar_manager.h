// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "lvgl.h"
#include "printer_state.h" // For PrinterStatus and NetworkStatus enums

/**
 * @brief Active notification status
 */
enum class NotificationStatus {
    NONE,    ///< No active notifications
    INFO,    ///< Info notification active
    WARNING, ///< Warning notification active
    ERROR    ///< Error notification active
};

/**
 * @brief Singleton manager for status bar icons
 *
 * Manages the persistent status icons at the bottom of the navigation bar showing:
 * - Network connection status (WiFi/Ethernet)
 * - Printer connection status
 * - Active notification indicator with badge
 * - Overlay backdrop for modal dimming
 *
 * Uses LVGL subjects for reactive XML bindings and ObserverGuard for RAII cleanup.
 *
 * Usage:
 *   StatusBarManager::instance().register_callbacks();  // Before XML creation
 *   StatusBarManager::instance().init_subjects();       // Before XML creation
 *   // Create XML...
 *   StatusBarManager::instance().init();                // After XML creation
 */
class StatusBarManager {
  public:
    /**
     * @brief Get singleton instance
     * @return Reference to the StatusBarManager singleton
     */
    static StatusBarManager& instance();

    // Non-copyable, non-movable (singleton)
    StatusBarManager(const StatusBarManager&) = delete;
    StatusBarManager& operator=(const StatusBarManager&) = delete;
    StatusBarManager(StatusBarManager&&) = delete;
    StatusBarManager& operator=(StatusBarManager&&) = delete;

    /**
     * @brief Register status bar event callbacks
     *
     * Must be called BEFORE app_layout XML is created so LVGL can find the callbacks.
     */
    void register_callbacks();

    /**
     * @brief Initialize status bar subjects for XML reactive bindings
     *
     * Must be called BEFORE app_layout XML is created so XML bindings can find subjects.
     * Registers the following subjects:
     * - printer_icon_state (int: 0=ready, 1=warning, 2=error, 3=disconnected)
     * - network_icon_state (int: 0=connected, 1=connecting, 2=disconnected)
     * - notification_count (int: badge count, 0=hidden)
     * - notification_count_text (string: formatted count)
     * - notification_severity (int: 0=info, 1=warning, 2=error)
     * - overlay_backdrop_visible (int: 0=hidden, 1=visible)
     */
    void init_subjects();

    /**
     * @brief Initialize the status bar system
     *
     * Sets up observers on PrinterState subjects to update status bar subjects.
     * Should be called after XML is created.
     */
    void init();

    /**
     * @brief Set overlay backdrop visibility
     *
     * Updates the overlay_backdrop_visible subject which controls the
     * modal dimming backdrop visibility via XML binding.
     *
     * @param visible true to show backdrop, false to hide
     */
    void set_backdrop_visible(bool visible);

    /**
     * @brief Update network status icon
     * @param status New network status
     */
    void update_network(NetworkStatus status);

    /**
     * @brief Update printer status icon
     * @param status New printer status
     */
    void update_printer(PrinterStatus status);

    /**
     * @brief Update notification indicator icon
     * @param status New notification status (NONE hides the icon)
     */
    void update_notification(NotificationStatus status);

    /**
     * @brief Update notification unread count badge
     * @param count Number of unread notifications (0 hides badge)
     */
    void update_notification_count(size_t count);

  private:
    /**
     * @brief Animate notification badge with attention pulse
     *
     * Finds the notification_badge widget on active screen and
     * triggers scale pulse animation to draw attention.
     */
    void animate_notification_badge();
    // Private constructor for singleton
    StatusBarManager() = default;
    ~StatusBarManager() = default;

    // Observer callbacks (static to work with LVGL API)
    static void network_status_observer(lv_observer_t* observer, lv_subject_t* subject);
    static void printer_connection_observer(lv_observer_t* observer, lv_subject_t* subject);
    static void klippy_state_observer(lv_observer_t* observer, lv_subject_t* subject);
    static void notification_history_clicked(lv_event_t* e);

    // Combined logic to update printer icon
    void update_printer_icon_combined();

    // ============================================================================
    // Status Icon State Subjects (drive XML reactive bindings)
    // ============================================================================

    // Printer icon state: 0=ready(green), 1=warning(orange), 2=error(red), 3=disconnected(gray)
    lv_subject_t printer_icon_state_subject_{};

    // Network icon state: 0=connected(green), 1=connecting(orange), 2=disconnected(gray)
    lv_subject_t network_icon_state_subject_{};

    // Notification badge: count (0 = hidden), text for display, severity for badge color
    lv_subject_t notification_count_subject_{};
    lv_subject_t notification_count_text_subject_{};
    lv_subject_t notification_severity_subject_{}; // 0=info, 1=warning, 2=error

    // Overlay backdrop visibility (for modal dimming)
    lv_subject_t overlay_backdrop_visible_subject_{};

    // Notification count text buffer (for string subject)
    char notification_count_text_buf_[8] = "0";

    // RAII observer guards for automatic cleanup
    ObserverGuard network_observer_;
    ObserverGuard connection_observer_;
    ObserverGuard klippy_observer_;

    // Cached state for combined printer icon logic
    int32_t cached_connection_state_ = 0;
    int32_t cached_klippy_state_ = 0; // 0=READY, 1=STARTUP, 2=SHUTDOWN, 3=ERROR

    // Track notification panel to prevent multiple instances
    lv_obj_t* notification_panel_obj_ = nullptr;

    // Track previous notification count for pulse animation (only pulse on increase)
    size_t previous_notification_count_ = 0;

    bool subjects_initialized_ = false;
    bool callbacks_registered_ = false;
    bool initialized_ = false;
};

// ============================================================================
// LEGACY API (forwards to StatusBarManager for backward compatibility)
// ============================================================================

/**
 * @brief Register status bar event callbacks
 * @deprecated Use StatusBarManager::instance().register_callbacks() instead
 */
void ui_status_bar_register_callbacks();

/**
 * @brief Initialize status bar subjects for XML reactive bindings
 * @deprecated Use StatusBarManager::instance().init_subjects() instead
 */
void ui_status_bar_init_subjects();

/**
 * @brief Initialize the status bar system
 * @deprecated Use StatusBarManager::instance().init() instead
 */
void ui_status_bar_init();

/**
 * @brief Set overlay backdrop visibility
 * @deprecated Use StatusBarManager::instance().set_backdrop_visible() instead
 */
void ui_status_bar_set_backdrop_visible(bool visible);

/**
 * @brief Update network status icon
 * @deprecated Use StatusBarManager::instance().update_network() instead
 */
void ui_status_bar_update_network(NetworkStatus status);

/**
 * @brief Update printer status icon
 * @deprecated Use StatusBarManager::instance().update_printer() instead
 */
void ui_status_bar_update_printer(PrinterStatus status);

/**
 * @brief Update notification indicator icon
 * @deprecated Use StatusBarManager::instance().update_notification() instead
 */
void ui_status_bar_update_notification(NotificationStatus status);

/**
 * @brief Update notification unread count badge
 * @deprecated Use StatusBarManager::instance().update_notification_count() instead
 */
void ui_status_bar_update_notification_count(size_t count);
