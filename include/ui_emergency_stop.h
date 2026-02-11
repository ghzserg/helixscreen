// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "lvgl.h"
#include "moonraker_api.h"
#include "printer_state.h"
#include "subject_managed_panel.h"

#include <chrono>

/**
 * @brief Reason the recovery dialog is being shown
 *
 * Tracks which error condition(s) triggered the dialog so the message
 * and available actions can adapt. Multiple reasons can be active
 * simultaneously (e.g., SHUTDOWN then DISCONNECTED in sequence).
 */
enum class RecoveryReason {
    NONE,         ///< No active recovery
    SHUTDOWN,     ///< Klipper entered SHUTDOWN state (e-stop, thermal runaway, config error)
    DISCONNECTED, ///< Klipper firmware disconnected from Moonraker
};

/**
 * @brief Emergency stop visibility coordinator
 *
 * Manages the estop_visible subject that drives contextual E-Stop buttons
 * embedded in home_panel, controls_panel, and print_status_panel.
 * Buttons are automatically shown during active prints (PRINTING or PAUSED)
 * via XML subject binding. The button triggers an M112 emergency stop
 * command via Moonraker.
 *
 * Features:
 * - Single-tap activation (default) or confirmation dialog (optional setting)
 * - Automatic visibility based on print state (via estop_visible subject)
 * - Klipper recovery dialog auto-popup on SHUTDOWN state
 * - Visual feedback via toast notifications
 *
 * Usage:
 *   // In main.cpp after LVGL and subjects initialized:
 *   EmergencyStopOverlay::instance().init(printer_state, api);
 *   EmergencyStopOverlay::instance().create();
 *
 * @see klipper_recovery_dialog.xml for post-shutdown recovery flow
 */
class EmergencyStopOverlay {
  public:
    /**
     * @brief Get singleton instance
     * @return Reference to the global EmergencyStopOverlay instance
     */
    static EmergencyStopOverlay& instance();

    /**
     * @brief Initialize with dependencies
     *
     * Must be called before create(). Sets up references to printer state
     * and API for operation.
     *
     * @param printer_state Reference to PrinterState for print job state
     * @param api Pointer to MoonrakerAPI for emergency_stop() calls
     */
    void init(PrinterState& printer_state, MoonrakerAPI* api);

    /**
     * @brief Initialize subjects for XML binding
     *
     * Registers the estop_visible subject used by XML binding.
     * Must be called during subject initialization phase (before XML creation).
     */
    void init_subjects();

    /**
     * @brief Deinitialize subjects for clean shutdown
     *
     * Must be called before lv_deinit() to prevent observer corruption.
     */
    void deinit_subjects();

    /**
     * @brief Initialize visibility coordination
     *
     * Sets up observers to update the estop_visible subject based on print
     * state. E-Stop buttons embedded in panels (home, controls, print_status)
     * bind to this subject for reactive visibility.
     *
     * Must be called after:
     * - init() with valid dependencies
     * - init_subjects() for XML binding
     */
    void create();

    /**
     * @brief Force visibility update
     *
     * Recalculates and applies estop_visible subject based on current
     * print state. Called automatically by state observers, but can be
     * called manually if needed.
     */
    void update_visibility();

    /**
     * @brief Set whether confirmation dialog is required
     *
     * When enabled, clicking E-Stop shows a confirmation dialog before
     * executing. When disabled (default), E-Stop executes immediately.
     *
     * @param require true to require confirmation, false for immediate action
     */
    void set_require_confirmation(bool require);

    /**
     * @brief Show recovery dialog for a specific reason
     *
     * Called for both SHUTDOWN state and KLIPPY_DISCONNECTED events.
     * If the dialog is already showing, updates the content to reflect
     * the combined error state (e.g., SHUTDOWN + DISCONNECTED).
     *
     * @param reason Why the recovery dialog is being shown
     */
    void show_recovery_for(RecoveryReason reason);

    /**
     * @brief Suppress recovery dialog for a duration
     *
     * Unified suppression for both SHUTDOWN and DISCONNECTED modals.
     * Used before expected restarts (SAVE_CONFIG, PID calibration).
     *
     * @param duration_ms How long to suppress (default 15 seconds)
     */
    void suppress_recovery_dialog(uint32_t duration_ms = 15000);

    /**
     * @brief Check if recovery dialog suppression is active
     * @return true if suppression window is still active
     */
    bool is_recovery_suppressed() const;

  private:
    EmergencyStopOverlay() = default;
    ~EmergencyStopOverlay() = default;

    // Non-copyable
    EmergencyStopOverlay(const EmergencyStopOverlay&) = delete;
    EmergencyStopOverlay& operator=(const EmergencyStopOverlay&) = delete;

    // Dependencies (set via init())
    PrinterState* printer_state_ = nullptr;
    MoonrakerAPI* api_ = nullptr;

    // Confirmation requirement (set via set_require_confirmation())
    bool require_confirmation_ = false;

    // Dialog widget references (created on-demand)
    lv_obj_t* confirmation_dialog_ = nullptr;
    lv_obj_t* recovery_dialog_ = nullptr;

    // Restart operation tracking - prevents recovery dialog during expected SHUTDOWN
    bool restart_in_progress_ = false;

    // Recovery dialog state
    RecoveryReason recovery_reason_ = RecoveryReason::NONE;

    // Time-based suppression for expected restarts (SAVE_CONFIG, PID calibration)
    uint32_t suppress_recovery_until_ = 0;

    // Visibility subject (1=visible, 0=hidden) - drives XML bindings
    lv_subject_t estop_visible_;

    // Recovery dialog subjects (drive XML bindings in klipper_recovery_dialog.xml)
    lv_subject_t recovery_title_subject_;
    char recovery_title_buf_[64]{};
    lv_subject_t recovery_message_subject_;
    char recovery_message_buf_[256]{};
    lv_subject_t recovery_can_restart_; // 1=show restart buttons, 0=hide (disconnected)

    bool subjects_initialized_ = false;

    // RAII subject manager for automatic cleanup
    SubjectManager subjects_;

    // State observers
    ObserverGuard print_state_observer_;
    ObserverGuard klippy_state_observer_;

    // Event handlers
    void handle_click();
    void execute_emergency_stop();
    void show_confirmation_dialog();
    void dismiss_confirmation_dialog();
    void show_recovery_dialog();
    void dismiss_recovery_dialog();
    void update_recovery_dialog_content();
    void restart_klipper();
    void firmware_restart();

    // Static callbacks
    static void emergency_stop_clicked(lv_event_t* e);
    static void estop_dialog_cancel_clicked(lv_event_t* e);
    static void estop_dialog_confirm_clicked(lv_event_t* e);
    static void recovery_restart_klipper_clicked(lv_event_t* e);
    static void recovery_firmware_restart_clicked(lv_event_t* e);
    static void recovery_dismiss_clicked(lv_event_t* e);
    static void advanced_estop_clicked(lv_event_t* e);
    static void advanced_restart_klipper_clicked(lv_event_t* e);
    static void advanced_firmware_restart_clicked(lv_event_t* e);
    static void home_firmware_restart_clicked(lv_event_t* e);
};
