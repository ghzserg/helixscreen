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

#include "ui_wizard_fan_select.h"

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
static lv_subject_t hotend_fan_selected;
static lv_subject_t part_fan_selected;

// Screen instance
static lv_obj_t* fan_select_screen_root = nullptr;

// Dynamic options storage (for event callback mapping)
static std::vector<std::string> hotend_fan_items;
static std::vector<std::string> part_fan_items;

// ============================================================================
// Subject Initialization
// ============================================================================

void ui_wizard_fan_select_init_subjects() {
    spdlog::debug("[Wizard Fan] Initializing subjects");

    // Initialize subjects with default index 0
    // Actual selection will be restored from config during create() after hardware is discovered
    WizardHelpers::init_int_subject(&hotend_fan_selected, 0, "hotend_fan_selected");
    WizardHelpers::init_int_subject(&part_fan_selected, 0, "part_fan_selected");

    spdlog::info("[Wizard Fan] Subjects initialized");
}

// ============================================================================
// Callback Registration
// ============================================================================

void ui_wizard_fan_select_register_callbacks() {
    // No XML callbacks needed - dropdowns attached programmatically in create()
    spdlog::debug("[Wizard Fan] Callback registration (none needed for hardware selectors)");
}

// ============================================================================
// Screen Creation
// ============================================================================

lv_obj_t* ui_wizard_fan_select_create(lv_obj_t* parent) {
    spdlog::info("[Wizard Fan] Creating fan select screen");

    // Safety check: cleanup should have been called by wizard navigation
    if (fan_select_screen_root) {
        spdlog::warn(
            "[Wizard Fan] Screen pointer not null - cleanup may not have been called properly");
        fan_select_screen_root = nullptr; // Reset pointer, wizard framework handles deletion
    }

    // Create screen from XML
    fan_select_screen_root = (lv_obj_t*)lv_xml_create(parent, "wizard_fan_select", nullptr);
    if (!fan_select_screen_root) {
        spdlog::error("[Wizard Fan] Failed to create screen from XML");
        return nullptr;
    }

    // Get Moonraker client for hardware discovery
    MoonrakerClient* client = get_moonraker_client();

    // Build hotend fan options with custom filter (heater_fan OR hotend_fan)
    hotend_fan_items.clear();
    if (client) {
        const auto& fans = client->get_fans();
        for (const auto& fan : fans) {
            if (fan.find("heater_fan") != std::string::npos ||
                fan.find("hotend_fan") != std::string::npos) {
                hotend_fan_items.push_back(fan);
            }
        }
    }

    // Build dropdown options string with "None" option
    std::string hotend_options_str = WizardHelpers::build_dropdown_options(
        hotend_fan_items, nullptr, true);

    // Add "None" to items vector to match dropdown
    hotend_fan_items.push_back("None");

    // Build part cooling fan options with custom filter (has "fan" but NOT heater/hotend)
    part_fan_items.clear();
    if (client) {
        const auto& fans = client->get_fans();
        for (const auto& fan : fans) {
            if (fan.find("fan") != std::string::npos &&
                fan.find("heater_fan") == std::string::npos &&
                fan.find("hotend_fan") == std::string::npos) {
                part_fan_items.push_back(fan);
            }
        }
    }

    // Build dropdown options string with "None" option
    std::string part_options_str = WizardHelpers::build_dropdown_options(
        part_fan_items, nullptr, true);

    // Add "None" to items vector to match dropdown
    part_fan_items.push_back("None");

    // Find and configure hotend fan dropdown
    lv_obj_t* hotend_dropdown = lv_obj_find_by_name(fan_select_screen_root, "hotend_fan_dropdown");
    if (hotend_dropdown) {
        lv_dropdown_set_options(hotend_dropdown, hotend_options_str.c_str());

        // Restore saved selection (no guessing method for fans)
        WizardHelpers::restore_dropdown_selection(
            hotend_dropdown, &hotend_fan_selected, hotend_fan_items,
            WizardConfigPaths::HOTEND_FAN, client,
            nullptr, // No guessing method for hotend fans
            "[Wizard Fan]");
    }

    // Attach hotend fan dropdown callback programmatically
    if (hotend_dropdown) {
        lv_obj_add_event_cb(hotend_dropdown, wizard_hardware_dropdown_changed_cb,
                           LV_EVENT_VALUE_CHANGED, &hotend_fan_selected);
    }

    // Find and configure part fan dropdown
    lv_obj_t* part_dropdown =
        lv_obj_find_by_name(fan_select_screen_root, "part_cooling_fan_dropdown");
    if (part_dropdown) {
        lv_dropdown_set_options(part_dropdown, part_options_str.c_str());

        // Restore saved selection (no guessing method for fans)
        WizardHelpers::restore_dropdown_selection(
            part_dropdown, &part_fan_selected, part_fan_items,
            WizardConfigPaths::PART_FAN, client,
            nullptr, // No guessing method for part fans
            "[Wizard Fan]");
    }

    // Attach part fan dropdown callback programmatically
    if (part_dropdown) {
        lv_obj_add_event_cb(part_dropdown, wizard_hardware_dropdown_changed_cb,
                           LV_EVENT_VALUE_CHANGED, &part_fan_selected);
    }

    spdlog::info("[Wizard Fan] Screen created successfully");
    return fan_select_screen_root;
}

// ============================================================================
// Cleanup
// ============================================================================

void ui_wizard_fan_select_cleanup() {
    spdlog::debug("[Wizard Fan] Cleaning up resources");

    // Save current selections to config before cleanup (deferred save pattern)
    WizardHelpers::save_dropdown_selection(&hotend_fan_selected, hotend_fan_items,
                                           WizardConfigPaths::HOTEND_FAN, "[Wizard Fan]");

    WizardHelpers::save_dropdown_selection(&part_fan_selected, part_fan_items,
                                           WizardConfigPaths::PART_FAN, "[Wizard Fan]");

    // Persist to disk
    Config* config = Config::get_instance();
    if (config) {
        config->save();
    }

    // Reset UI references
    // Note: Do NOT call lv_obj_del() here - the wizard framework handles
    // object deletion when clearing wizard_content container
    fan_select_screen_root = nullptr;

    spdlog::info("[Wizard Fan] Cleanup complete");
}

// ============================================================================
// Validation
// ============================================================================

bool ui_wizard_fan_select_is_validated() {
    // Always return true for baseline implementation
    return true;
}