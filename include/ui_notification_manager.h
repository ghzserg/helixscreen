// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl.h"
#include "subject_managed_panel.h"

/**
 * @brief Active notification status for badge coloring
 */
enum class NotificationStatus {
    NONE,    ///< No active notifications
    INFO,    ///< Info notification active
    WARNING, ///< Warning notification active
    ERROR    ///< Error notification active
};

/**
 * @brief Singleton manager for notification badge and history panel
 *
 * Manages the notification badge in the navbar showing:
 * - Unread notification count
 * - Notification severity color
 * - Badge pulse animation on new notifications
 *
 * Uses LVGL subjects for reactive XML bindings.
 *
 * Usage:
 *   NotificationManager::instance().register_callbacks();  // Before XML creation
 *   NotificationManager::instance().init_subjects();       // Before XML creation
 *   // Create XML...
 *   NotificationManager::instance().init();                // After XML creation
 */
class NotificationManager {
  public:
    /**
     * @brief Get singleton instance
     * @return Reference to the NotificationManager singleton
     */
    static NotificationManager& instance();

    // Non-copyable, non-movable (singleton)
    NotificationManager(const NotificationManager&) = delete;
    NotificationManager& operator=(const NotificationManager&) = delete;
    NotificationManager(NotificationManager&&) = delete;
    NotificationManager& operator=(NotificationManager&&) = delete;

    /**
     * @brief Register notification event callbacks
     *
     * Must be called BEFORE app_layout XML is created so LVGL can find the callbacks.
     */
    void register_callbacks();

    /**
     * @brief Initialize notification subjects for XML reactive bindings
     *
     * Must be called BEFORE app_layout XML is created so XML bindings can find subjects.
     * Registers the following subjects:
     * - notification_count (int: badge count, 0=hidden)
     * - notification_count_text (string: formatted count)
     * - notification_severity (int: 0=info, 1=warning, 2=error)
     */
    void init_subjects();

    /**
     * @brief Initialize the notification system
     *
     * Should be called after XML is created.
     */
    void init();

    /**
     * @brief Update notification severity (badge color)
     * @param status New notification status (NONE defaults to INFO color)
     */
    void update_notification(NotificationStatus status);

    /**
     * @brief Update notification unread count badge
     * @param count Number of unread notifications (0 hides badge)
     */
    void update_notification_count(size_t count);

    /**
     * @brief Deinitialize subjects for clean shutdown
     *
     * Must be called before lv_deinit() to prevent observer corruption.
     */
    void deinit_subjects();

  private:
    /**
     * @brief Animate notification badge with attention pulse
     *
     * Finds the notification_badge widget on active screen and
     * triggers scale pulse animation to draw attention.
     */
    void animate_notification_badge();

    // Private constructor for singleton
    NotificationManager() = default;
    ~NotificationManager() = default;

    // Event callback for notification history button (static to work with LVGL XML API)
    static void notification_history_clicked(lv_event_t* e);

    // ============================================================================
    // Notification State Subjects (drive XML reactive bindings)
    // ============================================================================

    // RAII subject manager for automatic cleanup
    SubjectManager subjects_;

    // Notification badge: count (0 = hidden), text for display, severity for badge color
    lv_subject_t notification_count_subject_{};
    lv_subject_t notification_count_text_subject_{};
    lv_subject_t notification_severity_subject_{}; // 0=info, 1=warning, 2=error

    // Notification count text buffer (for string subject)
    char notification_count_text_buf_[8] = "0";

    // Track notification panel to prevent multiple instances
    lv_obj_t* notification_panel_obj_ = nullptr;

    // Track previous notification count for pulse animation (only pulse on increase)
    size_t previous_notification_count_ = 0;

    bool subjects_initialized_ = false;
    bool callbacks_registered_ = false;
    bool initialized_ = false;
};

// ============================================================================
// FREE FUNCTIONS (in helix::ui namespace)
// ============================================================================

namespace helix::ui {

/**
 * @brief Register notification event callbacks
 */
void notification_register_callbacks();

/**
 * @brief Initialize notification subjects for XML reactive bindings
 */
void notification_init_subjects();

/**
 * @brief Deinitialize notification subjects for clean shutdown
 */
void notification_deinit_subjects();

/**
 * @brief Initialize the notification system
 */
void notification_manager_init();

/**
 * @brief Update notification severity
 */
void notification_update(NotificationStatus status);

/**
 * @brief Update notification unread count badge
 */
void notification_update_count(size_t count);

} // namespace helix::ui
