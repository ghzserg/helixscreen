// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_wizard_fan_select.h"

#include "ui_wizard.h"
#include "ui_wizard_hardware_selector.h"
#include "ui_wizard_helpers.h"

#include "app_globals.h"
#include "config.h"
#include "lvgl/lvgl.h"
#include "moonraker_client.h"
#include "ui_error_reporting.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<WizardFanSelectStep> g_wizard_fan_select_step;

WizardFanSelectStep* get_wizard_fan_select_step() {
    if (!g_wizard_fan_select_step) {
        g_wizard_fan_select_step = std::make_unique<WizardFanSelectStep>();
    }
    return g_wizard_fan_select_step.get();
}

void destroy_wizard_fan_select_step() {
    g_wizard_fan_select_step.reset();
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

WizardFanSelectStep::WizardFanSelectStep() {
    spdlog::debug("[{}] Instance created", get_name());
}

WizardFanSelectStep::~WizardFanSelectStep() {
    // NOTE: Do NOT call LVGL functions here - LVGL may be destroyed first
    // NOTE: Do NOT log here - spdlog may be destroyed first
    screen_root_ = nullptr;
}

// ============================================================================
// Move Semantics
// ============================================================================

WizardFanSelectStep::WizardFanSelectStep(WizardFanSelectStep&& other) noexcept
    : screen_root_(other.screen_root_),
      hotend_fan_selected_(other.hotend_fan_selected_),
      part_fan_selected_(other.part_fan_selected_),
      hotend_fan_items_(std::move(other.hotend_fan_items_)),
      part_fan_items_(std::move(other.part_fan_items_)),
      subjects_initialized_(other.subjects_initialized_) {
    other.screen_root_ = nullptr;
    other.subjects_initialized_ = false;
}

WizardFanSelectStep& WizardFanSelectStep::operator=(WizardFanSelectStep&& other) noexcept {
    if (this != &other) {
        screen_root_ = other.screen_root_;
        hotend_fan_selected_ = other.hotend_fan_selected_;
        part_fan_selected_ = other.part_fan_selected_;
        hotend_fan_items_ = std::move(other.hotend_fan_items_);
        part_fan_items_ = std::move(other.part_fan_items_);
        subjects_initialized_ = other.subjects_initialized_;

        other.screen_root_ = nullptr;
        other.subjects_initialized_ = false;
    }
    return *this;
}

// ============================================================================
// Subject Initialization
// ============================================================================

void WizardFanSelectStep::init_subjects() {
    spdlog::debug("[{}] Initializing subjects", get_name());

    // Initialize subjects with default index 0
    // Actual selection will be restored from config during create() after hardware is discovered
    WizardHelpers::init_int_subject(&hotend_fan_selected_, 0, "hotend_fan_selected");
    WizardHelpers::init_int_subject(&part_fan_selected_, 0, "part_fan_selected");

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

// ============================================================================
// Callback Registration
// ============================================================================

void WizardFanSelectStep::register_callbacks() {
    // No XML callbacks needed - dropdowns attached programmatically in create()
    spdlog::debug("[{}] Callback registration (none needed for hardware selectors)", get_name());
}

// ============================================================================
// Screen Creation
// ============================================================================

lv_obj_t* WizardFanSelectStep::create(lv_obj_t* parent) {
    spdlog::debug("[{}] Creating fan select screen", get_name());

    // Safety check: cleanup should have been called by wizard navigation
    if (screen_root_) {
        spdlog::warn(
            "[{}] Screen pointer not null - cleanup may not have been called properly", get_name());
        screen_root_ = nullptr; // Reset pointer, wizard framework handles deletion
    }

    // Create screen from XML
    screen_root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "wizard_fan_select", nullptr));
    if (!screen_root_) {
        LOG_ERROR_INTERNAL("[{}] Failed to create screen from XML", get_name());
        NOTIFY_ERROR("Failed to load fan configuration screen");
        return nullptr;
    }

    // Get Moonraker client for hardware discovery
    MoonrakerClient* client = get_moonraker_client();

    // Build hotend fan options with custom filter (heater_fan OR hotend_fan)
    hotend_fan_items_.clear();
    if (client) {
        const auto& fans = client->get_fans();
        for (const auto& fan : fans) {
            if (fan.find("heater_fan") != std::string::npos ||
                fan.find("hotend_fan") != std::string::npos) {
                hotend_fan_items_.push_back(fan);
            }
        }
    }

    // Build dropdown options string with "None" option
    std::string hotend_options_str =
        WizardHelpers::build_dropdown_options(hotend_fan_items_, nullptr, true);

    // Add "None" to items vector to match dropdown
    hotend_fan_items_.push_back("None");

    // Build part cooling fan options with custom filter (has "fan" but NOT heater/hotend)
    part_fan_items_.clear();
    if (client) {
        const auto& fans = client->get_fans();
        for (const auto& fan : fans) {
            if (fan.find("fan") != std::string::npos &&
                fan.find("heater_fan") == std::string::npos &&
                fan.find("hotend_fan") == std::string::npos) {
                part_fan_items_.push_back(fan);
            }
        }
    }

    // Build dropdown options string with "None" option
    std::string part_options_str =
        WizardHelpers::build_dropdown_options(part_fan_items_, nullptr, true);

    // Add "None" to items vector to match dropdown
    part_fan_items_.push_back("None");

    // Find and configure hotend fan dropdown
    lv_obj_t* hotend_dropdown = lv_obj_find_by_name(screen_root_, "hotend_fan_dropdown");
    if (hotend_dropdown) {
        lv_dropdown_set_options(hotend_dropdown, hotend_options_str.c_str());

        // Restore saved selection (no guessing method for fans)
        WizardHelpers::restore_dropdown_selection(hotend_dropdown, &hotend_fan_selected_,
                                                  hotend_fan_items_, WizardConfigPaths::HOTEND_FAN,
                                                  client,
                                                  nullptr, // No guessing method for hotend fans
                                                  "[Wizard Fan]");
    }

    // Attach hotend fan dropdown callback programmatically
    if (hotend_dropdown) {
        lv_obj_add_event_cb(hotend_dropdown, wizard_hardware_dropdown_changed_cb,
                            LV_EVENT_VALUE_CHANGED, &hotend_fan_selected_);
    }

    // Find and configure part fan dropdown
    lv_obj_t* part_dropdown =
        lv_obj_find_by_name(screen_root_, "part_cooling_fan_dropdown");
    if (part_dropdown) {
        lv_dropdown_set_options(part_dropdown, part_options_str.c_str());

        // Restore saved selection (no guessing method for fans)
        WizardHelpers::restore_dropdown_selection(part_dropdown, &part_fan_selected_, part_fan_items_,
                                                  WizardConfigPaths::PART_FAN, client,
                                                  nullptr, // No guessing method for part fans
                                                  "[Wizard Fan]");
    }

    // Attach part fan dropdown callback programmatically
    if (part_dropdown) {
        lv_obj_add_event_cb(part_dropdown, wizard_hardware_dropdown_changed_cb,
                            LV_EVENT_VALUE_CHANGED, &part_fan_selected_);
    }

    spdlog::debug("[{}] Screen created successfully", get_name());
    return screen_root_;
}

// ============================================================================
// Cleanup
// ============================================================================

void WizardFanSelectStep::cleanup() {
    spdlog::debug("[{}] Cleaning up resources", get_name());

    // Save current selections to config before cleanup (deferred save pattern)
    WizardHelpers::save_dropdown_selection(&hotend_fan_selected_, hotend_fan_items_,
                                           WizardConfigPaths::HOTEND_FAN, "[Wizard Fan]");

    WizardHelpers::save_dropdown_selection(&part_fan_selected_, part_fan_items_,
                                           WizardConfigPaths::PART_FAN, "[Wizard Fan]");

    // Persist to disk
    Config* config = Config::get_instance();
    if (config) {
        if (!config->save()) {
            NOTIFY_ERROR("Failed to save fan configuration");
        }
    }

    // Reset UI references
    // Note: Do NOT call lv_obj_del() here - the wizard framework handles
    // object deletion when clearing wizard_content container
    screen_root_ = nullptr;

    spdlog::debug("[{}] Cleanup complete", get_name());
}

// ============================================================================
// Validation
// ============================================================================

bool WizardFanSelectStep::is_validated() const {
    // Always return true for baseline implementation
    return true;
}
