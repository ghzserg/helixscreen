// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"

#include <memory>
#include <string>
#include <vector>

/**
 * @file ui_wizard_fan_select.h
 * @brief Wizard fan selection step - configures hotend and part cooling fans
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
 * - hotend_fan_selected (int) - Selected hotend fan index
 * - part_fan_selected (int) - Selected part cooling fan index
 */

/**
 * @class WizardFanSelectStep
 * @brief Fan configuration step for the first-run wizard
 */
class WizardFanSelectStep {
  public:
    WizardFanSelectStep();
    ~WizardFanSelectStep();

    // Non-copyable
    WizardFanSelectStep(const WizardFanSelectStep&) = delete;
    WizardFanSelectStep& operator=(const WizardFanSelectStep&) = delete;

    // Movable
    WizardFanSelectStep(WizardFanSelectStep&& other) noexcept;
    WizardFanSelectStep& operator=(WizardFanSelectStep&& other) noexcept;

    /**
     * @brief Initialize reactive subjects
     */
    void init_subjects();

    /**
     * @brief Register event callbacks
     */
    void register_callbacks();

    /**
     * @brief Create the fan selection UI from XML
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
    const char* get_name() const { return "Wizard Fan"; }

    // Public access to subjects for helper functions
    lv_subject_t* get_hotend_fan_subject() { return &hotend_fan_selected_; }
    lv_subject_t* get_part_fan_subject() { return &part_fan_selected_; }

    std::vector<std::string>& get_hotend_fan_items() { return hotend_fan_items_; }
    std::vector<std::string>& get_part_fan_items() { return part_fan_items_; }

  private:
    // Screen instance
    lv_obj_t* screen_root_ = nullptr;

    // Subjects
    lv_subject_t hotend_fan_selected_;
    lv_subject_t part_fan_selected_;

    // Dynamic options storage
    std::vector<std::string> hotend_fan_items_;
    std::vector<std::string> part_fan_items_;

    // Track initialization
    bool subjects_initialized_ = false;
};

// ============================================================================
// Global Instance Access
// ============================================================================

WizardFanSelectStep* get_wizard_fan_select_step();
void destroy_wizard_fan_select_step();
