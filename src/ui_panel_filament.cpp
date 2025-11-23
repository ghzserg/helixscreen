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

#include "ui_panel_filament.h"

#include "ui_component_keypad.h"
#include "ui_event_safety.h"
#include "ui_nav.h"
#include "ui_subject_registry.h"
#include "ui_temperature_utils.h"
#include "ui_theme.h"
#include "ui_utils.h"

#include "app_constants.h"

#include <spdlog/spdlog.h>

#include <string.h>

// Temperature subjects (reactive data binding)
static lv_subject_t filament_temp_display_subject;
static lv_subject_t filament_status_subject;
static lv_subject_t filament_material_selected_subject;
static lv_subject_t filament_extrusion_allowed_subject;
static lv_subject_t filament_safety_warning_visible_subject;
static lv_subject_t filament_warning_temps_subject;

// Subject storage buffers
static char temp_display_buf[32];
static char status_buf[64];
static char warning_temps_buf[64];

// Current state
static int nozzle_current = 25;
static int nozzle_target = 0;
static int selected_material = -1; // -1 = none, 0=PLA, 1=PETG, 2=ABS, 3=Custom

// Material temperature presets
static const int MATERIAL_TEMPS[] = {
    AppConstants::MaterialPresets::PLA, AppConstants::MaterialPresets::PETG,
    AppConstants::MaterialPresets::ABS, AppConstants::MaterialPresets::CUSTOM_DEFAULT};

// Temperature limits (can be updated from Moonraker heater config)
static int nozzle_min_temp = AppConstants::Temperature::DEFAULT_MIN_TEMP;
static int nozzle_max_temp = AppConstants::Temperature::DEFAULT_NOZZLE_MAX;

// Panel widgets
static lv_obj_t* filament_panel = nullptr;
static lv_obj_t* parent_obj = nullptr;
static lv_obj_t* btn_load = nullptr;
static lv_obj_t* btn_unload = nullptr;
static lv_obj_t* btn_purge = nullptr;
static lv_obj_t* safety_warning = nullptr;
static lv_obj_t* spool_image = nullptr;

// Preset button widgets (for visual feedback)
static lv_obj_t* preset_buttons[4] = {nullptr, nullptr, nullptr, nullptr};

// Subjects initialized flag
static bool subjects_initialized = false;

// Forward declarations
static void update_temp_display();
static void update_status();
static void update_safety_state();
static void update_preset_buttons_visual();

void ui_panel_filament_init_subjects() {
    if (subjects_initialized) {
        spdlog::warn("[Filament] Subjects already initialized");
        return;
    }

    // Initialize subjects with default values
    char temp_display_str[32], status_str[64], warning_temps_str[64];

    snprintf(temp_display_str, sizeof(temp_display_str), "%d / %d°C", nozzle_current,
             nozzle_target);
    snprintf(status_str, sizeof(status_str), "Select material to begin");
    snprintf(warning_temps_str, sizeof(warning_temps_str), "Current: %d°C | Target: %d°C",
             nozzle_current, nozzle_target);

    UI_SUBJECT_INIT_AND_REGISTER_STRING(filament_temp_display_subject, temp_display_buf,
                                        temp_display_str, "filament_temp_display");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(filament_status_subject, status_buf, status_str,
                                        "filament_status");
    UI_SUBJECT_INIT_AND_REGISTER_INT(filament_material_selected_subject, -1,
                                     "filament_material_selected");
    UI_SUBJECT_INIT_AND_REGISTER_INT(filament_extrusion_allowed_subject, 0,
                                     "filament_extrusion_allowed"); // false (cold at start)
    UI_SUBJECT_INIT_AND_REGISTER_INT(filament_safety_warning_visible_subject, 1,
                                     "filament_safety_warning_visible"); // true (cold at start)
    UI_SUBJECT_INIT_AND_REGISTER_STRING(filament_warning_temps_subject, warning_temps_buf,
                                        warning_temps_str, "filament_warning_temps");

    subjects_initialized = true;

    spdlog::debug("[Filament] Subjects initialized: temp={}/{}°C, material={}", nozzle_current,
                  nozzle_target, selected_material);
}

// Update temperature display text
static void update_temp_display() {
    snprintf(temp_display_buf, sizeof(temp_display_buf), "%d / %d°C", nozzle_current,
             nozzle_target);
    lv_subject_copy_string(&filament_temp_display_subject, temp_display_buf);
}

// Update status message
static void update_status() {
    const char* status_msg;

    if (UITemperatureUtils::is_extrusion_safe(nozzle_current,
                                              AppConstants::Temperature::MIN_EXTRUSION_TEMP)) {
        // Hot enough
        if (nozzle_target > 0 && nozzle_current >= nozzle_target - 5 &&
            nozzle_current <= nozzle_target + 5) {
            status_msg = "✓ Ready to load";
        } else {
            status_msg = "✓ Ready to load";
        }
    } else if (nozzle_target >= AppConstants::Temperature::MIN_EXTRUSION_TEMP) {
        // Heating
        char heating_buf[64];
        snprintf(heating_buf, sizeof(heating_buf), "⚡ Heating to %d°C...", nozzle_target);
        status_msg = heating_buf;
    } else {
        // Cold
        status_msg = "❄ Select material to begin";
    }

    lv_subject_copy_string(&filament_status_subject, status_msg);
}

// Update warning card text
static void update_warning_text() {
    snprintf(warning_temps_buf, sizeof(warning_temps_buf), "Current: %d°C | Target: %d°C",
             nozzle_current, nozzle_target);
    lv_subject_copy_string(&filament_warning_temps_subject, warning_temps_buf);
}

// Update safety state (button enable/disable, warning visibility)
static void update_safety_state() {
    bool allowed = UITemperatureUtils::is_extrusion_safe(
        nozzle_current, AppConstants::Temperature::MIN_EXTRUSION_TEMP);

    lv_subject_set_int(&filament_extrusion_allowed_subject, allowed ? 1 : 0);
    lv_subject_set_int(&filament_safety_warning_visible_subject, allowed ? 0 : 1);

    // Update button states (theme handles colors)
    if (btn_load) {
        if (allowed) {
            lv_obj_remove_state(btn_load, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btn_load, LV_STATE_DISABLED);
        }
    }

    if (btn_unload) {
        if (allowed) {
            lv_obj_remove_state(btn_unload, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btn_unload, LV_STATE_DISABLED);
        }
    }

    if (btn_purge) {
        if (allowed) {
            lv_obj_remove_state(btn_purge, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btn_purge, LV_STATE_DISABLED);
        }
    }

    // Show/hide safety warning
    if (safety_warning) {
        if (allowed) {
            lv_obj_add_flag(safety_warning, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(safety_warning, LV_OBJ_FLAG_HIDDEN);
        }
    }

    spdlog::debug("[Filament] Safety state updated: allowed={} (temp={}°C)", allowed,
                  nozzle_current);
}

// Update visual feedback for preset buttons
static void update_preset_buttons_visual() {
    for (int i = 0; i < 4; i++) {
        if (preset_buttons[i]) {
            if (i == selected_material) {
                // Selected state - theme handles colors
                lv_obj_add_state(preset_buttons[i], LV_STATE_CHECKED);
            } else {
                // Unselected state - theme handles colors
                lv_obj_remove_state(preset_buttons[i], LV_STATE_CHECKED);
            }
        }
    }
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

// Event handler: Material preset buttons
LVGL_SAFE_EVENT_CB_WITH_EVENT(preset_button_cb, event, {
    (void)lv_event_get_target(event); // Unused - we only need user_data
    int material_id = (int)(uintptr_t)lv_event_get_user_data(event);

    selected_material = material_id;
    nozzle_target = MATERIAL_TEMPS[material_id];

    lv_subject_set_int(&filament_material_selected_subject, selected_material);
    update_preset_buttons_visual();
    update_temp_display();
    update_status();

    spdlog::info("[Filament] Material selected: {} (target={}°C)", material_id, nozzle_target);

    // TODO: Send command to printer to set temperature
})

// Custom temperature keypad callback
static void custom_temp_confirmed_cb(float value, void* user_data) {
    (void)user_data;

    spdlog::info("[Filament] Custom temperature confirmed: {}°C", static_cast<int>(value));

    selected_material = 3; // Custom
    nozzle_target = (int)value;

    lv_subject_set_int(&filament_material_selected_subject, selected_material);
    update_preset_buttons_visual();
    update_temp_display();
    update_status();

    // TODO: Send command to printer to set temperature
}

// Event handler: Custom preset button (opens keypad)
static void preset_custom_button_cb(lv_event_t* e) {
    ui_event_safe_call("preset_custom_button_cb", [&]() {
        (void)e;

        spdlog::debug("[Filament] Opening custom temperature keypad");

        ui_keypad_config_t config = {.initial_value =
                                         (float)(nozzle_target > 0 ? nozzle_target : 200),
                                     .min_value = 0.0f,
                                     .max_value = (float)nozzle_max_temp,
                                     .title_label = "Custom Temperature",
                                     .unit_label = "°C",
                                     .allow_decimal = false,
                                     .allow_negative = false,
                                     .callback = custom_temp_confirmed_cb,
                                     .user_data = nullptr};

        ui_keypad_show(&config);
    });
}

// Event handler: Load filament button
LVGL_SAFE_EVENT_CB(load_button_cb, {
    if (!UITemperatureUtils::is_extrusion_safe(nozzle_current,
                                               AppConstants::Temperature::MIN_EXTRUSION_TEMP)) {
        spdlog::warn("[Filament] Load blocked: nozzle too cold ({}°C < {}°C)", nozzle_current,
                     AppConstants::Temperature::MIN_EXTRUSION_TEMP);
        return;
    }

    spdlog::info("[Filament] Loading filament");
    // TODO: Send LOAD_FILAMENT macro to printer
})

// Event handler: Unload filament button
LVGL_SAFE_EVENT_CB(unload_button_cb, {
    if (!UITemperatureUtils::is_extrusion_safe(nozzle_current,
                                               AppConstants::Temperature::MIN_EXTRUSION_TEMP)) {
        spdlog::warn("[Filament] Unload blocked: nozzle too cold ({}°C < {}°C)", nozzle_current,
                     AppConstants::Temperature::MIN_EXTRUSION_TEMP);
        return;
    }

    spdlog::info("[Filament] Unloading filament");
    // TODO: Send UNLOAD_FILAMENT macro to printer
})

// Event handler: Purge button
LVGL_SAFE_EVENT_CB(purge_button_cb, {
    if (!UITemperatureUtils::is_extrusion_safe(nozzle_current,
                                               AppConstants::Temperature::MIN_EXTRUSION_TEMP)) {
        spdlog::warn("[Filament] Purge blocked: nozzle too cold ({}°C < {}°C)", nozzle_current,
                     AppConstants::Temperature::MIN_EXTRUSION_TEMP);
        return;
    }

    spdlog::info("[Filament] Purging 10mm");
    // TODO: Send extrude command to printer (M83 \n G1 E10 F300)
})

// ============================================================================
// PUBLIC API
// ============================================================================

lv_obj_t* ui_panel_filament_create(lv_obj_t* parent) {
    if (!subjects_initialized) {
        spdlog::error("[Filament] Call ui_panel_filament_init_subjects() first!");
        return nullptr;
    }

    filament_panel = (lv_obj_t*)lv_xml_create(parent, "filament_panel", nullptr);
    if (!filament_panel) {
        spdlog::error("[Filament] Failed to create filament_panel from XML");
        return nullptr;
    }

    spdlog::debug("[Filament] Panel created from XML");
    return filament_panel;
}

void ui_panel_filament_setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    filament_panel = panel;
    parent_obj = parent_screen;

    spdlog::debug("[Filament] Setting up panel event handlers");

    // Find and setup preset buttons
    const char* preset_names[] = {"preset_pla", "preset_petg", "preset_abs", "preset_custom"};
    for (int i = 0; i < 4; i++) {
        preset_buttons[i] = lv_obj_find_by_name(panel, preset_names[i]);
        if (preset_buttons[i]) {
            if (i < 3) {
                // Standard presets (PLA, PETG, ABS)
                lv_obj_add_event_cb(preset_buttons[i], preset_button_cb, LV_EVENT_CLICKED,
                                    (void*)(uintptr_t)i);
            } else {
                // Custom preset (opens keypad)
                lv_obj_add_event_cb(preset_buttons[i], preset_custom_button_cb, LV_EVENT_CLICKED,
                                    nullptr);
            }
        }
    }
    spdlog::debug("[Filament] Preset buttons configured (4)");

    // Find and setup action buttons
    btn_load = lv_obj_find_by_name(panel, "btn_load");
    if (btn_load) {
        lv_obj_add_event_cb(btn_load, load_button_cb, LV_EVENT_CLICKED, nullptr);
        spdlog::debug("[Filament] Load button configured");
    }

    btn_unload = lv_obj_find_by_name(panel, "btn_unload");
    if (btn_unload) {
        lv_obj_add_event_cb(btn_unload, unload_button_cb, LV_EVENT_CLICKED, nullptr);
        spdlog::debug("[Filament] Unload button configured");
    }

    btn_purge = lv_obj_find_by_name(panel, "btn_purge");
    if (btn_purge) {
        lv_obj_add_event_cb(btn_purge, purge_button_cb, LV_EVENT_CLICKED, nullptr);
        spdlog::debug("[Filament] Purge button configured");
    }

    // Find safety warning card
    safety_warning = lv_obj_find_by_name(panel, "safety_warning");

    // Find spool image widget
    spool_image = lv_obj_find_by_name(panel, "spool_image");

    // Initialize visual state
    update_preset_buttons_visual();
    update_temp_display();
    update_status();
    update_warning_text();
    update_safety_state();

    spdlog::info("[Filament] Panel setup complete");
}

void ui_panel_filament_set_temp(int current, int target) {
    // Validate temperature ranges
    UITemperatureUtils::validate_and_clamp_pair(current, target, nozzle_min_temp, nozzle_max_temp,
                                                "Filament");

    nozzle_current = current;
    nozzle_target = target;

    update_temp_display();
    update_status();
    update_warning_text();
    update_safety_state();
}

void ui_panel_filament_get_temp(int* current, int* target) {
    if (current)
        *current = nozzle_current;
    if (target)
        *target = nozzle_target;
}

void ui_panel_filament_set_material(int material_id) {
    if (material_id < 0 || material_id > 3) {
        spdlog::error("[Filament] Invalid material ID {} (valid: 0-3)", material_id);
        return;
    }

    selected_material = material_id;
    nozzle_target = MATERIAL_TEMPS[material_id];

    lv_subject_set_int(&filament_material_selected_subject, selected_material);
    update_preset_buttons_visual();
    update_temp_display();
    update_status();

    spdlog::info("[Filament] Material set: {} (target={}°C)", material_id, nozzle_target);
}

int ui_panel_filament_get_material() {
    return selected_material;
}

bool ui_panel_filament_is_extrusion_allowed() {
    return UITemperatureUtils::is_extrusion_safe(nozzle_current,
                                                 AppConstants::Temperature::MIN_EXTRUSION_TEMP);
}

void ui_panel_filament_set_limits(int min_temp, int max_temp) {
    nozzle_min_temp = min_temp;
    nozzle_max_temp = max_temp;
    spdlog::info("[Filament] Nozzle temperature limits updated: {}-{}°C", min_temp, max_temp);
}
