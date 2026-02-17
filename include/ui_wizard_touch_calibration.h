// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 356C LLC

#pragma once

#include "touch_calibration_panel.h"

#include <lvgl.h>
#include <memory>

/**
 * @file ui_wizard_touch_calibration.h
 * @brief Wizard touch calibration step - touchscreen calibration for fbdev displays
 *
 * This is a thin wrapper around TouchCalibrationPanel that integrates it into
 * the wizard framework. The panel handles all calibration logic; this step
 * just manages UI integration, button visibility, and config persistence.
 *
 * ## Class-Based Architecture
 *
 * - Instance members instead of static globals
 * - Global singleton getter for wizard framework compatibility
 * - Static trampolines for LVGL event callbacks
 *
 * ## Subject Bindings (3 total):
 *
 * - instruction_text (string) - Current instruction for the user
 * - current_step (int) - 0-3 (0-2 = calibration points, 3 = verify)
 * - calibration_valid (int) - 0=not valid, 1=valid
 *
 * Initialization Order (CRITICAL):
 *   1. Register XML components (wizard_touch_calibration.xml)
 *   2. init_subjects()
 *   3. register_callbacks()
 *   4. create(parent)
 */

/**
 * @class WizardTouchCalibrationStep
 * @brief Touch calibration step for the first-run wizard
 *
 * Wraps TouchCalibrationPanel for wizard integration. Only shown on fbdev
 * displays that need touchscreen calibration.
 */
class WizardTouchCalibrationStep {
  public:
    WizardTouchCalibrationStep();
    ~WizardTouchCalibrationStep();

    // Non-copyable, non-movable (singleton with lv_subject_t members that
    // contain internal linked lists — moving corrupts observer pointers)
    WizardTouchCalibrationStep(const WizardTouchCalibrationStep&) = delete;
    WizardTouchCalibrationStep& operator=(const WizardTouchCalibrationStep&) = delete;
    WizardTouchCalibrationStep(WizardTouchCalibrationStep&&) = delete;
    WizardTouchCalibrationStep& operator=(WizardTouchCalibrationStep&&) = delete;

    /**
     * @brief Initialize reactive subjects
     *
     * Creates and registers 3 subjects with defaults.
     */
    void init_subjects();

    /**
     * @brief Register event callbacks with lv_xml system
     *
     * Registers callbacks:
     * - on_touch_cal_accept_clicked
     * - on_touch_cal_retry_clicked
     * - on_touch_cal_screen_touched
     */
    void register_callbacks();

    /**
     * @brief Create the touch calibration UI from XML
     *
     * @param parent Parent container (wizard_content)
     * @return Root object of the step, or nullptr on failure
     */
    lv_obj_t* create(lv_obj_t* parent);

    /**
     * @brief Cleanup resources
     *
     * Resets UI references. Does NOT call lv_obj_del() - wizard
     * framework handles widget deletion.
     */
    void cleanup();

    /**
     * @brief Commit pending calibration to config
     *
     * Saves the calibration data to config file. Called by wizard
     * when user clicks 'Next' to confirm calibration.
     *
     * @return true if calibration was saved, false if no pending calibration
     */
    bool commit_calibration();

    /**
     * @brief Check if step should be skipped
     *
     * Returns true if:
     * - Already calibrated (touch_calibrated config flag is true)
     * - Not on framebuffer display (HELIX_DISPLAY_FBDEV not defined)
     *
     * @return true if step should be skipped
     */
    bool should_skip() const;

    /**
     * @brief Get step name for logging
     */
    const char* get_name() const {
        return "Touch Calibration";
    }

  private:
    lv_obj_t* screen_root_ = nullptr;
    lv_obj_t* crosshair_ = nullptr;           // Reparented to screen for absolute positioning
    lv_obj_t* test_area_container_ = nullptr; // Container for test area (shown in COMPLETE state)
    lv_obj_t* test_touch_area_ = nullptr;     // Touch area for testing calibration
    std::unique_ptr<helix::TouchCalibrationPanel> panel_;

    // Subjects for UI state (instruction text uses wizard_subtitle instead)
    lv_subject_t current_step_; // 0, 1, 2, 3 (3 = verify)
    lv_subject_t calibration_valid_;

    bool subjects_initialized_ = false;
    bool calibration_failed_ = false; // True after failed attempt, cleared on first point capture

    // Pending calibration data (saved only when user clicks 'Next')
    bool has_pending_calibration_ = false;
    helix::TouchCalibration pending_calibration_;

    // Backup calibration for revert on timeout
    helix::TouchCalibration backup_calibration_;
    bool has_backup_ = false;

    // Event handlers (static trampolines)
    static void on_accept_clicked_static(lv_event_t* e);
    static void on_retry_clicked_static(lv_event_t* e);
    static void on_screen_touched_static(lv_event_t* e);
    static void on_test_area_touched_static(lv_event_t* e);

    // Instance method handlers
    void handle_accept_clicked();
    void handle_retry_clicked();
    void handle_screen_touched(lv_event_t* e);
    void handle_test_area_touched(lv_event_t* e);

    // Ripple animation for touch feedback
    void create_ripple_at(lv_coord_t x, lv_coord_t y);

    // Panel callback
    void on_calibration_complete(const helix::TouchCalibration* cal);

    // UI update helper
    void update_instruction_text();

    // Update crosshair position based on current calibration step
    void update_crosshair_position();

    // Update button visibility based on panel state
    void update_button_visibility();
};

// ============================================================================
// Global Instance Access
// ============================================================================

/**
 * @brief Get the global WizardTouchCalibrationStep instance
 *
 * Creates the instance on first call. Used by wizard framework.
 *
 * @return Pointer to the singleton instance
 */
WizardTouchCalibrationStep* get_wizard_touch_calibration_step();

/**
 * @brief Destroy the global WizardTouchCalibrationStep instance
 *
 * Call during application shutdown to ensure proper cleanup.
 */
void destroy_wizard_touch_calibration_step();

/**
 * @brief Force touch calibration step to show (for visual testing)
 *
 * When set to true, should_skip() returns false even on non-fbdev displays.
 * Use with --wizard-step 0 to test the touch calibration UI on SDL.
 *
 * @param force true to force-show the step, false for normal behavior
 */
void force_touch_calibration_step(bool force);
