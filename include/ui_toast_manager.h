// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl.h"

#include <cstdint>

/**
 * @brief Toast notification severity levels
 */
enum class ToastSeverity {
    INFO,    ///< Informational message (blue)
    SUCCESS, ///< Success message (green)
    WARNING, ///< Warning message (orange)
    ERROR    ///< Error message (red)
};

/**
 * @brief Callback type for toast action button
 */
typedef void (*toast_action_callback_t)(void* user_data);

/**
 * @brief Singleton manager for toast notifications
 *
 * Manages temporary non-blocking toast notifications that appear at the
 * top-center of the screen and auto-dismiss after a configurable duration.
 *
 * Features:
 * - Single active toast (new notifications replace old ones)
 * - Auto-dismiss with configurable timer
 * - Manual dismiss via close button
 * - Severity-based color coding (info, success, warning, error)
 * - Encapsulated state with proper RAII lifecycle
 *
 * Usage:
 *   ToastManager::instance().init();  // Call once at startup
 *   ToastManager::instance().show(ToastSeverity::INFO, "Message");
 */
class ToastManager {
  public:
    /**
     * @brief Get singleton instance
     * @return Reference to the ToastManager singleton
     */
    static ToastManager& instance();

    // Non-copyable, non-movable (singleton)
    ToastManager(const ToastManager&) = delete;
    ToastManager& operator=(const ToastManager&) = delete;
    ToastManager(ToastManager&&) = delete;
    ToastManager& operator=(ToastManager&&) = delete;

    /**
     * @brief Initialize the toast notification system
     *
     * Registers LVGL subjects for XML binding and event callbacks.
     * Should be called during app initialization.
     */
    void init();

    /**
     * @brief Show a toast notification
     *
     * Displays a toast notification with the specified severity and message.
     * If a toast is already visible, it will be replaced with the new one.
     *
     * @param severity Toast severity level (determines color)
     * @param message Message text to display
     * @param duration_ms Duration in milliseconds before auto-dismiss (default: 4000ms)
     */
    void show(ToastSeverity severity, const char* message, uint32_t duration_ms = 4000);

    /**
     * @brief Show a toast notification with an action button
     *
     * Displays a toast with an action button (e.g., "Undo"). The action callback
     * is invoked when the button is clicked. The toast auto-dismisses after
     * duration_ms, or when the close button is clicked.
     *
     * @param severity Toast severity level (determines color)
     * @param message Message text to display
     * @param action_text Text for the action button (e.g., "Undo")
     * @param action_callback Callback invoked when action button is clicked
     * @param user_data User data passed to the callback
     * @param duration_ms Duration in milliseconds before auto-dismiss (default: 5000ms)
     */
    void show_with_action(ToastSeverity severity, const char* message, const char* action_text,
                          toast_action_callback_t action_callback, void* user_data,
                          uint32_t duration_ms = 5000);

    /**
     * @brief Hide the currently visible toast
     *
     * Can be called to manually dismiss a toast before its timer expires.
     */
    void hide();

    /**
     * @brief Check if a toast is currently visible
     * @return true if a toast is visible, false otherwise
     */
    bool is_visible() const;

  private:
    // Private constructor for singleton
    ToastManager() = default;
    ~ToastManager() = default;

    // Internal helper
    void create_toast_internal(ToastSeverity severity, const char* message, uint32_t duration_ms,
                               bool with_action);

    // Animation helpers
    void animate_entrance(lv_obj_t* toast);
    void animate_exit(lv_obj_t* toast);
    static void exit_animation_complete_cb(lv_anim_t* anim);

    // Timer callback
    static void dismiss_timer_cb(lv_timer_t* timer);

    // Event callbacks
    static void close_btn_clicked(lv_event_t* e);
    static void action_btn_clicked(lv_event_t* e);

    // Active toast state
    lv_obj_t* active_toast_ = nullptr;
    lv_timer_t* dismiss_timer_ = nullptr;

    // Action button state
    toast_action_callback_t action_callback_ = nullptr;
    void* action_user_data_ = nullptr;

    // Subjects for XML binding
    lv_subject_t action_visible_subject_{};
    lv_subject_t action_text_subject_{};
    lv_subject_t severity_subject_{};

    // Text buffer for action button (for string subject)
    char action_text_buf_[64] = "";

    bool initialized_ = false;
    bool animating_exit_ = false; // Prevents double-hide during exit animation
};

// ============================================================================
// LEGACY API (forwards to ToastManager for backward compatibility)
// ============================================================================

/**
 * @brief Initialize the toast notification system
 * @deprecated Use ToastManager::instance().init() instead
 */
void ui_toast_init();

/**
 * @brief Show a toast notification
 * @deprecated Use ToastManager::instance().show() instead
 */
void ui_toast_show(ToastSeverity severity, const char* message, uint32_t duration_ms = 4000);

/**
 * @brief Show a toast notification with an action button
 * @deprecated Use ToastManager::instance().show_with_action() instead
 */
void ui_toast_show_with_action(ToastSeverity severity, const char* message, const char* action_text,
                               toast_action_callback_t action_callback, void* user_data,
                               uint32_t duration_ms = 5000);

/**
 * @brief Hide the currently visible toast
 * @deprecated Use ToastManager::instance().hide() instead
 */
void ui_toast_hide();

/**
 * @brief Check if a toast is currently visible
 * @deprecated Use ToastManager::instance().is_visible() instead
 */
bool ui_toast_is_visible();
