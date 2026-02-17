// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"

#include <memory>
#include <string>
#include <vector>

/**
 * @file ui_wizard_heater_select.h
 * @brief Wizard heater selection step - configures bed and hotend heaters
 *
 * Uses hardware discovery from MoonrakerClient to populate dropdowns.
 *
 * ## Class-Based Architecture (Phase 6)
 *
 * Migrated from function-based to class-based design with:
 * - Instance members instead of static globals
 * - Static trampolines for LVGL callbacks
 * - Global singleton getter for backwards compatibility
 *
 * ## Subject Bindings (2 total):
 *
 * - bed_heater_selected (int) - Selected bed heater index
 * - hotend_heater_selected (int) - Selected hotend heater index
 *
 * Note: This screen does not use separate sensor dropdowns. The selected
 * heater names are automatically used as sensor names since Klipper heater
 * objects inherently provide temperature readings.
 */

/**
 * @class WizardHeaterSelectStep
 * @brief Heater configuration step for the first-run wizard
 */
class WizardHeaterSelectStep {
  public:
    WizardHeaterSelectStep();
    ~WizardHeaterSelectStep();

    // Non-copyable, non-movable (singleton with lv_subject_t members that
    // contain internal linked lists — moving corrupts observer pointers)
    WizardHeaterSelectStep(const WizardHeaterSelectStep&) = delete;
    WizardHeaterSelectStep& operator=(const WizardHeaterSelectStep&) = delete;
    WizardHeaterSelectStep(WizardHeaterSelectStep&&) = delete;
    WizardHeaterSelectStep& operator=(WizardHeaterSelectStep&&) = delete;

    /**
     * @brief Initialize reactive subjects
     */
    void init_subjects();

    /**
     * @brief Register event callbacks
     */
    void register_callbacks();

    /**
     * @brief Create the heater selection UI from XML
     *
     * @param parent Parent container (wizard_content)
     * @return Root object of the step, or nullptr on failure
     */
    lv_obj_t* create(lv_obj_t* parent);

    /**
     * @brief Cleanup resources and save selections to config
     */
    void cleanup();

    /**
     * @brief Check if step is validated
     *
     * @return true (always validated for baseline)
     */
    bool is_validated() const;

    /**
     * @brief Get step name for logging
     */
    const char* get_name() const {
        return "Wizard Heater";
    }

    // Public access to subjects for helper functions
    lv_subject_t* get_bed_heater_subject() {
        return &bed_heater_selected_;
    }
    lv_subject_t* get_hotend_heater_subject() {
        return &hotend_heater_selected_;
    }

    std::vector<std::string>& get_bed_heater_items() {
        return bed_heater_items_;
    }
    std::vector<std::string>& get_hotend_heater_items() {
        return hotend_heater_items_;
    }

  private:
    // Screen instance
    lv_obj_t* screen_root_ = nullptr;

    // Subjects
    lv_subject_t bed_heater_selected_;
    lv_subject_t hotend_heater_selected_;

    // Dynamic options storage (for event callback mapping)
    std::vector<std::string> bed_heater_items_;
    std::vector<std::string> hotend_heater_items_;

    // Track initialization
    bool subjects_initialized_ = false;
};

// ============================================================================
// Global Instance Access
// ============================================================================

WizardHeaterSelectStep* get_wizard_heater_select_step();
void destroy_wizard_heater_select_step();
