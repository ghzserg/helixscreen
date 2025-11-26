// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_wizard_heater_select.h"

#include "ui_wizard.h"
#include "ui_wizard_hardware_selector.h"
#include "ui_wizard_helpers.h"

#include "app_globals.h"
#include "config.h"
#include "lvgl/lvgl.h"
#include "moonraker_client.h"
#include "ui_error_reporting.h"
#include "ui_notification.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<WizardHeaterSelectStep> g_wizard_heater_select_step;

WizardHeaterSelectStep* get_wizard_heater_select_step() {
    if (!g_wizard_heater_select_step) {
        g_wizard_heater_select_step = std::make_unique<WizardHeaterSelectStep>();
    }
    return g_wizard_heater_select_step.get();
}

void destroy_wizard_heater_select_step() {
    g_wizard_heater_select_step.reset();
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

WizardHeaterSelectStep::WizardHeaterSelectStep() {
    spdlog::debug("[{}] Instance created", get_name());
}

WizardHeaterSelectStep::~WizardHeaterSelectStep() {
    // NOTE: Do NOT call LVGL functions here - LVGL may be destroyed first
    // NOTE: Do NOT log here - spdlog may be destroyed first
    screen_root_ = nullptr;
}

// ============================================================================
// Move Semantics
// ============================================================================

WizardHeaterSelectStep::WizardHeaterSelectStep(WizardHeaterSelectStep&& other) noexcept
    : screen_root_(other.screen_root_),
      bed_heater_selected_(other.bed_heater_selected_),
      hotend_heater_selected_(other.hotend_heater_selected_),
      bed_heater_items_(std::move(other.bed_heater_items_)),
      hotend_heater_items_(std::move(other.hotend_heater_items_)),
      subjects_initialized_(other.subjects_initialized_) {
    other.screen_root_ = nullptr;
    other.subjects_initialized_ = false;
}

WizardHeaterSelectStep& WizardHeaterSelectStep::operator=(WizardHeaterSelectStep&& other) noexcept {
    if (this != &other) {
        screen_root_ = other.screen_root_;
        bed_heater_selected_ = other.bed_heater_selected_;
        hotend_heater_selected_ = other.hotend_heater_selected_;
        bed_heater_items_ = std::move(other.bed_heater_items_);
        hotend_heater_items_ = std::move(other.hotend_heater_items_);
        subjects_initialized_ = other.subjects_initialized_;

        other.screen_root_ = nullptr;
        other.subjects_initialized_ = false;
    }
    return *this;
}

// ============================================================================
// Subject Initialization
// ============================================================================

void WizardHeaterSelectStep::init_subjects() {
    spdlog::debug("[{}] Initializing subjects", get_name());

    // Initialize subjects with default index 0
    // Actual selection will be restored from config during create() after hardware is discovered
    WizardHelpers::init_int_subject(&bed_heater_selected_, 0, "bed_heater_selected");
    WizardHelpers::init_int_subject(&hotend_heater_selected_, 0, "hotend_heater_selected");

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

// ============================================================================
// Callback Registration
// ============================================================================

void WizardHeaterSelectStep::register_callbacks() {
    // No XML callbacks needed - dropdowns attached programmatically in create()
    spdlog::debug("[{}] Callback registration (none needed for hardware selectors)", get_name());
}

// ============================================================================
// Screen Creation
// ============================================================================

lv_obj_t* WizardHeaterSelectStep::create(lv_obj_t* parent) {
    spdlog::debug("[{}] Creating heater select screen", get_name());

    // Safety check: cleanup should have been called by wizard navigation
    if (screen_root_) {
        spdlog::warn(
            "[{}] Screen pointer not null - cleanup may not have been called properly", get_name());
        screen_root_ = nullptr; // Reset pointer, wizard framework handles deletion
    }

    // Create screen from XML
    screen_root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "wizard_heater_select", nullptr));
    if (!screen_root_) {
        spdlog::error("[{}] Failed to create screen from XML", get_name());
        ui_notification_error("Wizard Error",
                            "Failed to load heater configuration screen. Please restart the application.");
        return nullptr;
    }

    // Populate bed heater dropdown (discover + filter + populate + restore)
    wizard_populate_hardware_dropdown(
        screen_root_, "bed_heater_dropdown", &bed_heater_selected_, bed_heater_items_,
        [](MoonrakerClient* c) -> const auto& { return c->get_heaters(); },
        "bed", // Filter for bed-related heaters
        true,  // Allow "None" option
        WizardConfigPaths::BED_HEATER, [](MoonrakerClient* c) { return c->guess_bed_heater(); },
        "[Wizard Heater]");

    // Attach bed heater dropdown callback programmatically
    lv_obj_t* bed_heater_dropdown =
        lv_obj_find_by_name(screen_root_, "bed_heater_dropdown");
    if (bed_heater_dropdown) {
        lv_obj_add_event_cb(bed_heater_dropdown, wizard_hardware_dropdown_changed_cb,
                            LV_EVENT_VALUE_CHANGED, &bed_heater_selected_);
    }

    // Populate hotend heater dropdown (discover + filter + populate + restore)
    wizard_populate_hardware_dropdown(
        screen_root_, "hotend_heater_dropdown", &hotend_heater_selected_,
        hotend_heater_items_, [](MoonrakerClient* c) -> const auto& { return c->get_heaters(); },
        "extruder", // Filter for extruder-related heaters
        true,       // Allow "None" option
        WizardConfigPaths::HOTEND_HEATER,
        [](MoonrakerClient* c) { return c->guess_hotend_heater(); }, "[Wizard Heater]");

    // Attach hotend heater dropdown callback programmatically
    lv_obj_t* hotend_heater_dropdown =
        lv_obj_find_by_name(screen_root_, "hotend_heater_dropdown");
    if (hotend_heater_dropdown) {
        lv_obj_add_event_cb(hotend_heater_dropdown, wizard_hardware_dropdown_changed_cb,
                            LV_EVENT_VALUE_CHANGED, &hotend_heater_selected_);
    }

    spdlog::debug("[{}] Screen created successfully", get_name());
    return screen_root_;
}

// ============================================================================
// Cleanup
// ============================================================================

void WizardHeaterSelectStep::cleanup() {
    spdlog::debug("[{}] Cleaning up resources", get_name());

    Config* config = Config::get_instance();
    if (!config) {
        spdlog::error("[{}] Config instance not available!", get_name());
        return;
    }

    // Save bed heater selection
    // Store the heater name to BOTH heater and sensor paths (Klipper heaters provide temp readings)
    WizardHelpers::save_dropdown_selection(&bed_heater_selected_, bed_heater_items_,
                                           WizardConfigPaths::BED_HEATER, "[Wizard Heater]");

    // Get the selected bed heater name and also save it as the sensor
    int32_t bed_idx = lv_subject_get_int(&bed_heater_selected_);
    if (bed_idx >= 0 && bed_idx < static_cast<int32_t>(bed_heater_items_.size())) {
        const std::string& bed_heater_name = bed_heater_items_[bed_idx];
        config->set<std::string>(WizardConfigPaths::BED_SENSOR, bed_heater_name);
        spdlog::debug("[{}] Bed sensor set to: {}", get_name(), bed_heater_name);
    }

    // Save hotend heater selection
    // Store the heater name to BOTH heater and sensor paths
    WizardHelpers::save_dropdown_selection(&hotend_heater_selected_, hotend_heater_items_,
                                           WizardConfigPaths::HOTEND_HEATER, "[Wizard Heater]");

    // Get the selected hotend heater name and also save it as the sensor
    int32_t hotend_idx = lv_subject_get_int(&hotend_heater_selected_);
    if (hotend_idx >= 0 && hotend_idx < static_cast<int32_t>(hotend_heater_items_.size())) {
        const std::string& hotend_heater_name = hotend_heater_items_[hotend_idx];
        config->set<std::string>(WizardConfigPaths::HOTEND_SENSOR, hotend_heater_name);
        spdlog::debug("[{}] Hotend sensor set to: {}", get_name(), hotend_heater_name);
    }

    // Persist to disk
    if (!config->save()) {
        NOTIFY_ERROR("Failed to save heater configuration");
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

bool WizardHeaterSelectStep::is_validated() const {
    // Always return true for baseline implementation
    return true;
}
