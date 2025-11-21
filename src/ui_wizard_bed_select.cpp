// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#include "ui_wizard_bed_select.h"

#include "ui_wizard.h"
#include "ui_wizard_hardware_selector.h"
#include "ui_wizard_helpers.h"

#include "app_globals.h"
#include "config.h"
#include "lvgl/lvgl.h"
#include "moonraker_client.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <string>
#include <vector>

// ============================================================================
// Static Data & Subjects
// ============================================================================

// Subject declarations (module scope)
static lv_subject_t bed_heater_selected;
static lv_subject_t bed_sensor_selected;

// Screen instance
static lv_obj_t* bed_select_screen_root = nullptr;

// Dynamic options storage (for event callback mapping)
static std::vector<std::string> bed_heater_items;
static std::vector<std::string> bed_sensor_items;

// ============================================================================
// Subject Initialization
// ============================================================================

void ui_wizard_bed_select_init_subjects() {
    spdlog::debug("[Wizard Bed] Initializing subjects");

    // Initialize subjects with default index 0
    // Actual selection will be restored from config during create() after hardware is discovered
    WizardHelpers::init_int_subject(&bed_heater_selected, 0, "bed_heater_selected");
    WizardHelpers::init_int_subject(&bed_sensor_selected, 0, "bed_sensor_selected");

    spdlog::info("[Wizard Bed] Subjects initialized");
}

// ============================================================================
// Callback Registration
// ============================================================================

void ui_wizard_bed_select_register_callbacks() {
    // No XML callbacks needed - dropdowns attached programmatically in create()
    spdlog::debug("[Wizard Bed] Callback registration (none needed for hardware selectors)");
}

// ============================================================================
// Screen Creation
// ============================================================================

lv_obj_t* ui_wizard_bed_select_create(lv_obj_t* parent) {
    spdlog::info("[Wizard Bed] Creating bed select screen");

    // Safety check: cleanup should have been called by wizard navigation
    if (bed_select_screen_root) {
        spdlog::warn(
            "[Wizard Bed] Screen pointer not null - cleanup may not have been called properly");
        bed_select_screen_root = nullptr; // Reset pointer, wizard framework handles deletion
    }

    // Create screen from XML
    bed_select_screen_root = (lv_obj_t*)lv_xml_create(parent, "wizard_bed_select", nullptr);
    if (!bed_select_screen_root) {
        spdlog::error("[Wizard Bed] Failed to create screen from XML");
        return nullptr;
    }

    // Populate heater dropdown (discover + filter + populate + restore)
    wizard_populate_hardware_dropdown(
        bed_select_screen_root, "bed_heater_dropdown",
        &bed_heater_selected, bed_heater_items,
        [](MoonrakerClient* c) -> const auto& { return c->get_heaters(); },
        "bed",  // Filter for bed-related heaters
        true,   // Allow "None" option
        WizardConfigPaths::BED_HEATER,
        [](MoonrakerClient* c) { return c->guess_bed_heater(); },
        "[Wizard Bed]"
    );

    // Attach heater dropdown callback programmatically
    lv_obj_t* heater_dropdown = lv_obj_find_by_name(bed_select_screen_root,
                                                      "bed_heater_dropdown");
    if (heater_dropdown) {
        lv_obj_add_event_cb(heater_dropdown, wizard_hardware_dropdown_changed_cb,
                           LV_EVENT_VALUE_CHANGED, &bed_heater_selected);
    }

    // Populate sensor dropdown (discover + filter + populate + restore)
    wizard_populate_hardware_dropdown(
        bed_select_screen_root, "bed_sensor_dropdown",
        &bed_sensor_selected, bed_sensor_items,
        [](MoonrakerClient* c) -> const auto& { return c->get_sensors(); },
        nullptr, // No filter - include all sensors for bed
        true,    // Allow "None" option
        WizardConfigPaths::BED_SENSOR,
        [](MoonrakerClient* c) { return c->guess_bed_sensor(); },
        "[Wizard Bed]"
    );

    // Attach sensor dropdown callback programmatically
    lv_obj_t* sensor_dropdown = lv_obj_find_by_name(bed_select_screen_root,
                                                      "bed_sensor_dropdown");
    if (sensor_dropdown) {
        lv_obj_add_event_cb(sensor_dropdown, wizard_hardware_dropdown_changed_cb,
                           LV_EVENT_VALUE_CHANGED, &bed_sensor_selected);
    }

    spdlog::info("[Wizard Bed] Screen created successfully");
    return bed_select_screen_root;
}

// ============================================================================
// Cleanup
// ============================================================================

void ui_wizard_bed_select_cleanup() {
    spdlog::debug("[Wizard Bed] Cleaning up resources");

    // Save current selections to config before cleanup (deferred save pattern)
    WizardHelpers::save_dropdown_selection(&bed_heater_selected, bed_heater_items,
                                           WizardConfigPaths::BED_HEATER, "[Wizard Bed]");

    WizardHelpers::save_dropdown_selection(&bed_sensor_selected, bed_sensor_items,
                                           WizardConfigPaths::BED_SENSOR, "[Wizard Bed]");

    // Persist to disk
    Config* config = Config::get_instance();
    if (config) {
        config->save();
    }

    // Reset UI references
    // Note: Do NOT call lv_obj_del() here - the wizard framework handles
    // object deletion when clearing wizard_content container
    bed_select_screen_root = nullptr;

    spdlog::info("[Wizard Bed] Cleanup complete");
}

// ============================================================================
// Validation
// ============================================================================

bool ui_wizard_bed_select_is_validated() {
    // Always return true for baseline implementation
    return true;
}