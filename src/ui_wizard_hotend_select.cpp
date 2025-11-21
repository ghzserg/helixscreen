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

#include "ui_wizard_hotend_select.h"

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
static lv_subject_t hotend_heater_selected;
static lv_subject_t hotend_sensor_selected;

// Screen instance
static lv_obj_t* hotend_select_screen_root = nullptr;

// Dynamic options storage (for event callback mapping)
static std::vector<std::string> hotend_heater_items;
static std::vector<std::string> hotend_sensor_items;

// ============================================================================
// Subject Initialization
// ============================================================================

void ui_wizard_hotend_select_init_subjects() {
    spdlog::debug("[Wizard Hotend] Initializing subjects");

    // Initialize subjects with default index 0
    // Actual selection will be restored from config during create() after hardware is discovered
    WizardHelpers::init_int_subject(&hotend_heater_selected, 0, "hotend_heater_selected");
    WizardHelpers::init_int_subject(&hotend_sensor_selected, 0, "hotend_sensor_selected");

    spdlog::info("[Wizard Hotend] Subjects initialized");
}

// ============================================================================
// Callback Registration
// ============================================================================

void ui_wizard_hotend_select_register_callbacks() {
    // No XML callbacks needed - dropdowns attached programmatically in create()
    spdlog::debug("[Wizard Hotend] Callback registration (none needed for hardware selectors)");
}

// ============================================================================
// Screen Creation
// ============================================================================

lv_obj_t* ui_wizard_hotend_select_create(lv_obj_t* parent) {
    spdlog::info("[Wizard Hotend] Creating hotend select screen");

    // Safety check: cleanup should have been called by wizard navigation
    if (hotend_select_screen_root) {
        spdlog::warn(
            "[Wizard Hotend] Screen pointer not null - cleanup may not have been called properly");
        hotend_select_screen_root = nullptr; // Reset pointer, wizard framework handles deletion
    }

    // Create screen from XML
    hotend_select_screen_root = (lv_obj_t*)lv_xml_create(parent, "wizard_hotend_select", nullptr);
    if (!hotend_select_screen_root) {
        spdlog::error("[Wizard Hotend] Failed to create screen from XML");
        return nullptr;
    }

    // Populate heater dropdown (discover + filter + populate + restore)
    wizard_populate_hardware_dropdown(
        hotend_select_screen_root, "hotend_heater_dropdown",
        &hotend_heater_selected, hotend_heater_items,
        [](MoonrakerClient* c) -> const auto& { return c->get_heaters(); },
        "extruder", // Filter for extruder-related heaters
        true,       // Allow "None" option
        WizardConfigPaths::HOTEND_HEATER,
        [](MoonrakerClient* c) { return c->guess_hotend_heater(); },
        "[Wizard Hotend]"
    );

    // Attach heater dropdown callback programmatically
    lv_obj_t* heater_dropdown = lv_obj_find_by_name(hotend_select_screen_root,
                                                      "hotend_heater_dropdown");
    if (heater_dropdown) {
        lv_obj_add_event_cb(heater_dropdown, wizard_hardware_dropdown_changed_cb,
                           LV_EVENT_VALUE_CHANGED, &hotend_heater_selected);
    }

    // Populate sensor dropdown (discover + filter + populate + restore)
    wizard_populate_hardware_dropdown(
        hotend_select_screen_root, "hotend_sensor_dropdown",
        &hotend_sensor_selected, hotend_sensor_items,
        [](MoonrakerClient* c) -> const auto& { return c->get_sensors(); },
        "extruder", // Filter for extruder/hotend-related sensors
        true,       // Allow "None" option
        WizardConfigPaths::HOTEND_SENSOR,
        [](MoonrakerClient* c) { return c->guess_hotend_sensor(); },
        "[Wizard Hotend]"
    );

    // Attach sensor dropdown callback programmatically
    lv_obj_t* sensor_dropdown = lv_obj_find_by_name(hotend_select_screen_root,
                                                      "hotend_sensor_dropdown");
    if (sensor_dropdown) {
        lv_obj_add_event_cb(sensor_dropdown, wizard_hardware_dropdown_changed_cb,
                           LV_EVENT_VALUE_CHANGED, &hotend_sensor_selected);
    }

    spdlog::info("[Wizard Hotend] Screen created successfully");
    return hotend_select_screen_root;
}

// ============================================================================
// Cleanup
// ============================================================================

void ui_wizard_hotend_select_cleanup() {
    spdlog::debug("[Wizard Hotend] Cleaning up resources");

    // Save current selections to config before cleanup (deferred save pattern)
    WizardHelpers::save_dropdown_selection(&hotend_heater_selected, hotend_heater_items,
                                           WizardConfigPaths::HOTEND_HEATER, "[Wizard Hotend]");

    WizardHelpers::save_dropdown_selection(&hotend_sensor_selected, hotend_sensor_items,
                                           WizardConfigPaths::HOTEND_SENSOR, "[Wizard Hotend]");

    // Persist to disk
    Config* config = Config::get_instance();
    if (config) {
        config->save();
    }

    // Reset UI references
    // Note: Do NOT call lv_obj_del() here - the wizard framework handles
    // object deletion when clearing wizard_content container
    hotend_select_screen_root = nullptr;

    spdlog::info("[Wizard Hotend] Cleanup complete");
}

// ============================================================================
// Validation
// ============================================================================

bool ui_wizard_hotend_select_is_validated() {
    // Always return true for baseline implementation
    return true;
}