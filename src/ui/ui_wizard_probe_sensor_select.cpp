// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_wizard_probe_sensor_select.h"

#include "ui_error_reporting.h"
#include "ui_notification.h"
#include "ui_wizard.h"
#include "ui_wizard_helpers.h"

#include "app_globals.h"
#include "filament_sensor_manager.h"
#include "lvgl/lvgl.h"
#include "moonraker_client.h"
#include "printer_hardware.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<WizardProbeSensorSelectStep> g_wizard_probe_sensor_select_step;

WizardProbeSensorSelectStep* get_wizard_probe_sensor_select_step() {
    if (!g_wizard_probe_sensor_select_step) {
        g_wizard_probe_sensor_select_step = std::make_unique<WizardProbeSensorSelectStep>();
        StaticPanelRegistry::instance().register_destroy(
            "WizardProbeSensorSelectStep", []() { g_wizard_probe_sensor_select_step.reset(); });
    }
    return g_wizard_probe_sensor_select_step.get();
}

void destroy_wizard_probe_sensor_select_step() {
    g_wizard_probe_sensor_select_step.reset();
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

WizardProbeSensorSelectStep::WizardProbeSensorSelectStep() {
    spdlog::debug("[{}] Instance created", get_name());
}

WizardProbeSensorSelectStep::~WizardProbeSensorSelectStep() {
    // Do NOT call LVGL functions or log here - they may be destroyed first
    screen_root_ = nullptr;
}

// ============================================================================
// Move Semantics
// ============================================================================

WizardProbeSensorSelectStep::WizardProbeSensorSelectStep(
    WizardProbeSensorSelectStep&& other) noexcept
    : screen_root_(other.screen_root_), probe_sensor_selected_(other.probe_sensor_selected_),
      sensor_items_(std::move(other.sensor_items_)),
      available_sensors_(std::move(other.available_sensors_)),
      subjects_initialized_(other.subjects_initialized_) {
    other.screen_root_ = nullptr;
    other.subjects_initialized_ = false;
}

WizardProbeSensorSelectStep&
WizardProbeSensorSelectStep::operator=(WizardProbeSensorSelectStep&& other) noexcept {
    if (this != &other) {
        screen_root_ = other.screen_root_;
        probe_sensor_selected_ = other.probe_sensor_selected_;
        sensor_items_ = std::move(other.sensor_items_);
        available_sensors_ = std::move(other.available_sensors_);
        subjects_initialized_ = other.subjects_initialized_;

        other.screen_root_ = nullptr;
        other.subjects_initialized_ = false;
    }
    return *this;
}

// ============================================================================
// Sensor Filtering
// ============================================================================

void WizardProbeSensorSelectStep::filter_available_sensors() {
    available_sensors_.clear();

    auto& sensor_mgr = helix::FilamentSensorManager::instance();
    auto all_sensors = sensor_mgr.get_sensors();

    for (const auto& sensor : all_sensors) {
        // Only include switch sensors that are not assigned to any role
        if (sensor.type == helix::FilamentSensorType::SWITCH &&
            sensor.role == helix::FilamentSensorRole::NONE) {
            available_sensors_.push_back(sensor);
            spdlog::debug("[{}] Found available sensor: {}", get_name(), sensor.sensor_name);
        } else {
            spdlog::debug("[{}] Filtered out sensor: {} (type={}, role={})", get_name(),
                          sensor.sensor_name,
                          sensor.type == helix::FilamentSensorType::SWITCH ? "switch" : "motion",
                          helix::role_to_config_string(sensor.role));
        }
    }

    spdlog::info("[{}] Found {} available sensors (filtered from {} total)", get_name(),
                 available_sensors_.size(), all_sensors.size());
}

// ============================================================================
// Subject Initialization
// ============================================================================

void WizardProbeSensorSelectStep::init_subjects() {
    spdlog::debug("[{}] Initializing subjects", get_name());

    // Initialize subject with default index 0 (None)
    helix::ui::wizard::init_int_subject(&probe_sensor_selected_, 0, "probe_sensor_selected");

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

// ============================================================================
// Static Callbacks (XML event_cb pattern)
// ============================================================================

static void on_probe_sensor_dropdown_changed(lv_event_t* e) {
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int index = static_cast<int>(lv_dropdown_get_selected(dropdown));
    auto* step = get_wizard_probe_sensor_select_step();
    if (step) {
        lv_subject_set_int(step->get_probe_sensor_subject(), index);
        spdlog::debug("[WizardProbeSensorSelectStep] Probe sensor selection changed to index {}",
                      index);
    }
}

// ============================================================================
// Callback Registration
// ============================================================================

void WizardProbeSensorSelectStep::register_callbacks() {
    lv_xml_register_event_cb(nullptr, "on_probe_sensor_dropdown_changed",
                             on_probe_sensor_dropdown_changed);
    spdlog::debug("[{}] Registered dropdown callback", get_name());
}

// ============================================================================
// Dropdown Population
// ============================================================================

void WizardProbeSensorSelectStep::populate_dropdown() {
    if (!screen_root_)
        return;

    // Build sensor items list: "None" + sensor names
    sensor_items_.clear();
    sensor_items_.push_back("None");
    for (const auto& sensor : available_sensors_) {
        sensor_items_.push_back(sensor.klipper_name);
    }

    // Build options string for dropdown (newline-separated)
    std::string options;
    for (size_t i = 0; i < sensor_items_.size(); i++) {
        if (i > 0)
            options += "\n";
        // Use display name (sensor_name) for dropdown, but store klipper_name
        if (i == 0) {
            options += "None";
        } else {
            options += available_sensors_[i - 1].sensor_name;
        }
    }

    // Find and populate the probe dropdown
    lv_obj_t* probe_dropdown = lv_obj_find_by_name(screen_root_, "probe_sensor_dropdown");

    if (probe_dropdown) {
        lv_dropdown_set_options(probe_dropdown, options.c_str());
        lv_dropdown_set_selected(
            probe_dropdown, static_cast<uint32_t>(lv_subject_get_int(&probe_sensor_selected_)));
    }

    spdlog::debug("[{}] Populated dropdown with {} options", get_name(), sensor_items_.size());
}

std::string WizardProbeSensorSelectStep::get_klipper_name_for_index(int dropdown_index) const {
    if (dropdown_index <= 0 || static_cast<size_t>(dropdown_index) >= sensor_items_.size()) {
        return ""; // "None" selected or invalid
    }
    return sensor_items_[static_cast<size_t>(dropdown_index)];
}

// ============================================================================
// Screen Creation
// ============================================================================

lv_obj_t* WizardProbeSensorSelectStep::create(lv_obj_t* parent) {
    spdlog::debug("[{}] Creating probe sensor select screen", get_name());

    // Safety check
    if (screen_root_) {
        spdlog::warn("[{}] Screen pointer not null - cleanup may not have been called properly",
                     get_name());
        screen_root_ = nullptr;
    }

    // Filter sensors to get available (unassigned switch) sensors
    filter_available_sensors();

    // Create screen from XML
    screen_root_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent, "wizard_probe_sensor_select", nullptr));
    if (!screen_root_) {
        spdlog::error("[{}] Failed to create screen from XML", get_name());
        ui_notification_error(
            "Wizard Error",
            "Failed to load probe sensor configuration screen. Please restart the application.");
        return nullptr;
    }

    // Restore selection from existing FilamentSensorManager config
    for (size_t i = 0; i < available_sensors_.size(); i++) {
        const auto& sensor = available_sensors_[i];
        int dropdown_index = static_cast<int>(i + 1); // +1 because index 0 is "None"

        if (sensor.role == helix::FilamentSensorRole::Z_PROBE) {
            lv_subject_set_int(&probe_sensor_selected_, dropdown_index);
            spdlog::debug("[{}] Restored Z_PROBE sensor from config: {}", get_name(),
                          sensor.sensor_name);
            break;
        }
    }

    // Populate dropdown
    populate_dropdown();

    spdlog::debug("[{}] Screen created successfully", get_name());
    return screen_root_;
}

// ============================================================================
// Refresh
// ============================================================================

void WizardProbeSensorSelectStep::refresh() {
    if (!screen_root_) {
        return; // No screen to refresh
    }

    // Re-filter sensors (may have been discovered since create())
    size_t old_count = available_sensors_.size();
    filter_available_sensors();

    if (old_count != available_sensors_.size()) {
        spdlog::info("[{}] Sensor count changed ({} -> {}), refreshing dropdown", get_name(),
                     old_count, available_sensors_.size());
    }

    // Re-populate dropdown
    populate_dropdown();
    spdlog::debug("[{}] Refreshed with {} available sensors", get_name(),
                  available_sensors_.size());
}

// ============================================================================
// Skip Logic
// ============================================================================

size_t WizardProbeSensorSelectStep::get_available_sensor_count() const {
    // Query FilamentSensorManager directly as the single source of truth
    // This works even when the step is skipped and create() was never called
    auto& sensor_mgr = helix::FilamentSensorManager::instance();
    auto all_sensors = sensor_mgr.get_sensors();

    size_t count = 0;
    for (const auto& sensor : all_sensors) {
        // Only count switch sensors that are unassigned
        if (sensor.type == helix::FilamentSensorType::SWITCH &&
            sensor.role == helix::FilamentSensorRole::NONE) {
            count++;
        }
    }
    return count;
}

bool WizardProbeSensorSelectStep::should_skip() const {
    size_t available_count = get_available_sensor_count();
    spdlog::debug("[{}] should_skip: {} available sensors", get_name(), available_count);
    return available_count == 0;
}

// ============================================================================
// Cleanup
// ============================================================================

void WizardProbeSensorSelectStep::cleanup() {
    spdlog::debug("[{}] Cleaning up resources", get_name());

    auto& sensor_mgr = helix::FilamentSensorManager::instance();

    // Clear existing Z_PROBE role assignments first
    // NOTE: Query ALL sensors from the manager, not available_sensors_ (which only has role=NONE)
    for (const auto& sensor : sensor_mgr.get_sensors()) {
        if (sensor.role == helix::FilamentSensorRole::Z_PROBE) {
            sensor_mgr.set_sensor_role(sensor.klipper_name, helix::FilamentSensorRole::NONE);
        }
    }

    // Apply new role assignment based on dropdown selection
    std::string probe_name =
        get_klipper_name_for_index(lv_subject_get_int(&probe_sensor_selected_));

    if (!probe_name.empty()) {
        sensor_mgr.set_sensor_role(probe_name, helix::FilamentSensorRole::Z_PROBE);
        spdlog::info("[{}] Assigned Z_PROBE role to: {}", get_name(), probe_name);
    }

    // Persist to disk
    sensor_mgr.save_config();

    // Reset UI references
    screen_root_ = nullptr;

    spdlog::debug("[{}] Cleanup complete", get_name());
}

// ============================================================================
// Validation
// ============================================================================

bool WizardProbeSensorSelectStep::is_validated() const {
    // Always return true for baseline implementation
    return true;
}
