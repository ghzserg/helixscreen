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

#include "ui_wizard_led_select.h"

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
static lv_subject_t led_strip_selected;

// Screen instance
static lv_obj_t* led_select_screen_root = nullptr;

// Dynamic options storage (for event callback mapping)
static std::vector<std::string> led_strip_items;

// ============================================================================
// Subject Initialization
// ============================================================================

void ui_wizard_led_select_init_subjects() {
    spdlog::debug("[Wizard LED] Initializing subjects");

    // Initialize subject with default index 0
    // Actual selection will be restored from config during create() after hardware is discovered
    WizardHelpers::init_int_subject(&led_strip_selected, 0, "led_strip_selected");

    spdlog::info("[Wizard LED] Subjects initialized");
}

// ============================================================================
// Callback Registration
// ============================================================================

void ui_wizard_led_select_register_callbacks() {
    // No XML callbacks needed - dropdowns attached programmatically in create()
    spdlog::debug("[Wizard LED] Callback registration (none needed for hardware selectors)");
}

// ============================================================================
// Screen Creation
// ============================================================================

lv_obj_t* ui_wizard_led_select_create(lv_obj_t* parent) {
    spdlog::info("[Wizard LED] Creating LED select screen");

    // Safety check: cleanup should have been called by wizard navigation
    if (led_select_screen_root) {
        spdlog::warn(
            "[Wizard LED] Screen pointer not null - cleanup may not have been called properly");
        led_select_screen_root = nullptr; // Reset pointer, wizard framework handles deletion
    }

    // Create screen from XML
    led_select_screen_root = (lv_obj_t*)lv_xml_create(parent, "wizard_led_select", nullptr);
    if (!led_select_screen_root) {
        spdlog::error("[Wizard LED] Failed to create screen from XML");
        return nullptr;
    }

    // Populate LED dropdown (discover + filter + populate + restore)
    wizard_populate_hardware_dropdown(
        led_select_screen_root, "led_main_dropdown",
        &led_strip_selected, led_strip_items,
        [](MoonrakerClient* c) -> const auto& { return c->get_leds(); },
        nullptr, // No filter - include all LEDs
        true,    // Allow "None" option
        WizardConfigPaths::LED_STRIP,
        nullptr, // No guessing method for LED strips
        "[Wizard LED]"
    );

    // Attach LED dropdown callback programmatically
    lv_obj_t* led_dropdown = lv_obj_find_by_name(led_select_screen_root, "led_main_dropdown");
    if (led_dropdown) {
        lv_obj_add_event_cb(led_dropdown, wizard_hardware_dropdown_changed_cb,
                           LV_EVENT_VALUE_CHANGED, &led_strip_selected);
    }

    spdlog::info("[Wizard LED] Screen created successfully");
    return led_select_screen_root;
}

// ============================================================================
// Cleanup
// ============================================================================

void ui_wizard_led_select_cleanup() {
    spdlog::debug("[Wizard LED] Cleaning up resources");

    // Save current selection to config before cleanup (deferred save pattern)
    WizardHelpers::save_dropdown_selection(&led_strip_selected, led_strip_items,
                                           WizardConfigPaths::LED_STRIP, "[Wizard LED]");

    // Persist to disk
    Config* config = Config::get_instance();
    if (config) {
        config->save();
    }

    // Reset UI references
    // Note: Do NOT call lv_obj_del() here - the wizard framework handles
    // object deletion when clearing wizard_content container
    led_select_screen_root = nullptr;

    spdlog::info("[Wizard LED] Cleanup complete");
}

// ============================================================================
// Validation
// ============================================================================

bool ui_wizard_led_select_is_validated() {
    // Always return true for baseline implementation
    return true;
}