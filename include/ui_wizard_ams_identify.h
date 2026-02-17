// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"
#include "subject_managed_panel.h"

#include <string>

/**
 * @file ui_wizard_ams_identify.h
 * @brief Wizard AMS identification step - displays detected AMS system info
 *
 * This step is purely informational - no user configuration required.
 * Shows the detected AMS system type (AFC, Happy Hare, Tool Changer, etc.)
 * and lane count.
 *
 * ## Skip Logic
 * - Skipped if no AMS detected (AmsType::NONE)
 *
 * ## Class-Based Architecture
 *
 * Follows the wizard step pattern with:
 * - Instance members instead of static globals
 * - Global singleton getter for backwards compatibility
 */

/**
 * @class WizardAmsIdentifyStep
 * @brief AMS identification step for the first-run wizard
 */
class WizardAmsIdentifyStep {
  public:
    WizardAmsIdentifyStep();
    ~WizardAmsIdentifyStep();

    // Non-copyable, non-movable (singleton with lv_subject_t members that
    // contain internal linked lists — moving corrupts observer pointers)
    WizardAmsIdentifyStep(const WizardAmsIdentifyStep&) = delete;
    WizardAmsIdentifyStep& operator=(const WizardAmsIdentifyStep&) = delete;
    WizardAmsIdentifyStep(WizardAmsIdentifyStep&&) = delete;
    WizardAmsIdentifyStep& operator=(WizardAmsIdentifyStep&&) = delete;

    /**
     * @brief Initialize reactive subjects (no-op for this step)
     */
    void init_subjects();

    /**
     * @brief Register event callbacks (no-op for this step)
     */
    void register_callbacks();

    /**
     * @brief Create the AMS identification UI from XML
     *
     * @param parent Parent container (wizard_content)
     * @return Root object of the step, or nullptr on failure
     */
    [[nodiscard]] lv_obj_t* create(lv_obj_t* parent);

    /**
     * @brief Cleanup resources
     */
    void cleanup();

    /**
     * @brief Check if step is validated
     *
     * @return true (always validated - display only step)
     */
    [[nodiscard]] bool is_validated() const;

    /**
     * @brief Check if this step should be skipped
     *
     * Skips if no AMS system is detected (AmsType::NONE).
     *
     * @return true if step should be skipped, false otherwise
     */
    [[nodiscard]] bool should_skip() const;

    /**
     * @brief Get step name for logging
     */
    [[nodiscard]] static const char* get_name() {
        return "Wizard AMS Identify";
    }

  private:
    lv_obj_t* screen_root_{nullptr};

    // RAII subject manager for automatic cleanup
    SubjectManager subjects_;

    // Subjects for reactive XML binding
    lv_subject_t wizard_ams_type_{};
    lv_subject_t wizard_ams_details_{};
    static char ams_type_buffer_[64];
    static char ams_details_buffer_[128];
    bool subjects_initialized_{false};

    /**
     * @brief Update display labels with current AMS info
     */
    void update_display();

    /**
     * @brief Get human-readable AMS type name
     * @return String like "AFC (Armored Turtle)", "Happy Hare MMU", etc.
     */
    [[nodiscard]] std::string get_ams_type_name() const;

    /**
     * @brief Get AMS details string (lane count + unit name)
     * @return String like "4 lanes • Turtle_1"
     */
    [[nodiscard]] std::string get_ams_details() const;
};

// ============================================================================
// Global Instance Access
// ============================================================================

/**
 * @brief Get the singleton wizard AMS identify step instance
 * @return Pointer to the step instance
 */
WizardAmsIdentifyStep* get_wizard_ams_identify_step();

/**
 * @brief Destroy the wizard AMS identify step instance
 */
void destroy_wizard_ams_identify_step();
