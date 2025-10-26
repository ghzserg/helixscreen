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

#pragma once

#include "lvgl/lvgl.h"

/**
 * Filament Panel - Filament loading/unloading operations with safety checks
 *
 * Features:
 * - Material presets (PLA, PETG, ABS, Custom)
 * - Load/Unload/Purge operations
 * - Temperature safety checks (170°C minimum)
 * - Visual status indicators
 * - Filament spool visualization
 */

// Initialize subjects (MUST be called before creating XML)
void ui_panel_filament_init_subjects();

// Create filament panel (returns panel widget)
lv_obj_t* ui_panel_filament_create(lv_obj_t* parent);

// Setup event handlers and reactive bindings
void ui_panel_filament_setup(lv_obj_t* panel, lv_obj_t* parent_screen);

// Temperature control
void ui_panel_filament_set_temp(int current, int target);
void ui_panel_filament_get_temp(int* current, int* target);

// Material selection
void ui_panel_filament_set_material(int material_id);  // 0=PLA, 1=PETG, 2=ABS, 3=Custom
int ui_panel_filament_get_material();

// Safety state
bool ui_panel_filament_is_extrusion_allowed();

// Temperature limits (configurable from Moonraker heater config)
void ui_panel_filament_set_limits(int min_temp, int max_temp);
