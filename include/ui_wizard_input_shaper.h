// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "input_shaper_calibrator.h"
#include "lvgl/lvgl.h"

#include <atomic>
#include <memory>
#include <string>

/**
 * @file ui_wizard_input_shaper.h
 * @brief Wizard input shaper calibration step - optional accelerometer calibration
 *
 * Provides input shaper calibration during first-run wizard when an accelerometer
 * is detected. Uses InputShaperCalibrator for the actual calibration workflow.
 *
 * ## Skip Logic:
 *
 * - No accelerometer detected: Skip entirely (input shaper can be configured later
 *   in Settings → Advanced → Input Shaper)
 * - Accelerometer detected: Show wizard step for calibration
 * - Footer shows "Skip" button (via wizard_show_skip subject) to allow skipping
 * - After successful calibration, footer changes to "Next"
 *
 * ## Subject Bindings:
 *
 * - wizard_input_shaper_status (string) - Current calibration status message
 * - wizard_input_shaper_progress (int) - Calibration progress 0-100
 *
 * ## Validation:
 *
 * Step is validated when calibration completed successfully.
 * User can also skip via the footer "Skip" button without completing calibration.
 */

namespace helix {
namespace calibration {
class InputShaperCalibrator;
} // namespace calibration
} // namespace helix

/**
 * @class WizardInputShaperStep
 * @brief Input shaper calibration step for the first-run wizard
 */
class WizardInputShaperStep {
  public:
    WizardInputShaperStep();
    ~WizardInputShaperStep();

    // Non-copyable
    WizardInputShaperStep(const WizardInputShaperStep&) = delete;
    WizardInputShaperStep& operator=(const WizardInputShaperStep&) = delete;

    // Movable
    WizardInputShaperStep(WizardInputShaperStep&& other) noexcept;
    WizardInputShaperStep& operator=(WizardInputShaperStep&& other) noexcept;

    /**
     * @brief Initialize reactive subjects
     */
    void init_subjects();

    /**
     * @brief Register event callbacks
     */
    void register_callbacks();

    /**
     * @brief Create the input shaper calibration UI from XML
     *
     * @param parent Parent container (wizard_content)
     * @return Root object of the step, or nullptr on failure
     */
    lv_obj_t* create(lv_obj_t* parent);

    /**
     * @brief Cleanup resources
     */
    void cleanup();

    /**
     * @brief Check if step is validated
     *
     * @return true if calibration complete or user explicitly skipped
     */
    bool is_validated() const;

    /**
     * @brief Check if this step should be skipped
     *
     * Skips if no accelerometer is detected from the printer.
     *
     * @return true if step should be skipped, false otherwise
     */
    bool should_skip() const;

    /**
     * @brief Get step name for logging
     */
    const char* get_name() const {
        return "Wizard Input Shaper";
    }

    // ========================================================================
    // State accessors for testing and wizard flow
    // ========================================================================

    /**
     * @brief Check if accelerometer is available
     *
     * Queries printer_has_accelerometer subject.
     *
     * @return true if accelerometer detected
     */
    bool has_accelerometer() const;

    /**
     * @brief Get the calibrator instance
     *
     * @return Pointer to the InputShaperCalibrator (never null)
     */
    helix::calibration::InputShaperCalibrator* get_calibrator();

    /**
     * @brief Check if calibration was completed
     */
    bool is_calibration_complete() const {
        return calibration_complete_;
    }

    /**
     * @brief Set calibration complete flag
     */
    void set_calibration_complete(bool complete) {
        calibration_complete_ = complete;
    }

    /**
     * @brief Check if user explicitly skipped calibration
     */
    bool is_user_skipped() const {
        return user_skipped_;
    }

    /**
     * @brief Set user skipped flag
     */
    void set_user_skipped(bool skipped) {
        user_skipped_ = skipped;
    }

    // ========================================================================
    // Subject access for testing
    // ========================================================================

    lv_subject_t* get_status_subject() {
        return &calibration_status_;
    }

    lv_subject_t* get_progress_subject() {
        return &calibration_progress_;
    }

    lv_subject_t* get_started_subject() {
        return &calibration_started_;
    }

    /**
     * @brief Get alive flag for async callback safety
     *
     * Used by callbacks to check if step is still valid before updating subjects.
     * Returns weak_ptr to allow safe checking without preventing cleanup.
     */
    std::weak_ptr<std::atomic<bool>> get_alive_flag() {
        return alive_;
    }

    /**
     * @brief Get the screen root object
     *
     * @return Pointer to the screen root object, or nullptr if not created
     */
    lv_obj_t* get_screen_root() const {
        return screen_root_;
    }

  private:
    // Screen instance
    lv_obj_t* screen_root_ = nullptr;

    // Subjects
    lv_subject_t calibration_status_;
    lv_subject_t calibration_progress_;
    lv_subject_t calibration_started_; ///< 0=not started, 1=started (hides Start button)

    // String buffers for subjects
    char status_buffer_[128] = "Ready to calibrate";

    // Calibrator instance (owns the calibrator)
    std::unique_ptr<helix::calibration::InputShaperCalibrator> calibrator_;

    // State tracking
    bool subjects_initialized_ = false;
    bool calibration_complete_ = false;
    bool user_skipped_ = false;

    // Lifetime guard for async callback safety
    // Shared pointer ensures the flag outlives the callbacks that capture it
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};

// ============================================================================
// Global Instance Access
// ============================================================================

WizardInputShaperStep* get_wizard_input_shaper_step();
void destroy_wizard_input_shaper_step();
