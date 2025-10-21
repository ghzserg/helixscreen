/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of GuppyScreen.
 *
 * GuppyScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * GuppyScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GuppyScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#include "material_icons.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/others/xml/lv_xml.h"

// External icon declarations
extern const lv_image_dsc_t list;

void material_icons_register() {
    LV_LOG_USER("Registering Material Design icons (57 total)...");

    // Navigation & Movement
    lv_xml_register_image(NULL, "mat_home", &home);
    lv_xml_register_image(NULL, "mat_arrow_up", &arrow_up);
    lv_xml_register_image(NULL, "mat_arrow_down", &arrow_down);
    lv_xml_register_image(NULL, "mat_arrow_left", &arrow_left);
    lv_xml_register_image(NULL, "mat_arrow_right", &arrow_right);
    lv_xml_register_image(NULL, "mat_back", &back);
    lv_xml_register_image(NULL, "mat_move", &move);
    lv_xml_register_image(NULL, "mat_z_closer", &z_closer);
    lv_xml_register_image(NULL, "mat_z_farther", &z_farther);
    lv_xml_register_image(NULL, "mat_home_z", &home_z);

    // Print & Files
    lv_xml_register_image(NULL, "mat_print", &print);
    lv_xml_register_image(NULL, "mat_list", &list);
    lv_xml_register_image(NULL, "mat_pause", &pause_img);
    lv_xml_register_image(NULL, "mat_resume", &resume);
    lv_xml_register_image(NULL, "mat_cancel", &cancel);
    lv_xml_register_image(NULL, "mat_sd", &sd_img);
    lv_xml_register_image(NULL, "mat_refresh", &refresh_img);

    // Temperature & Heating
    lv_xml_register_image(NULL, "mat_bed", &bed);
    lv_xml_register_image(NULL, "mat_heater", &heater);
    lv_xml_register_image(NULL, "mat_cooldown", &cooldown_img);

    // Extrusion & Filament
    lv_xml_register_image(NULL, "mat_extruder", &extruder);
    lv_xml_register_image(NULL, "mat_extrude", &extrude);
    lv_xml_register_image(NULL, "mat_extrude_img", &extrude_img);
    lv_xml_register_image(NULL, "mat_filament", &filament_img);
    lv_xml_register_image(NULL, "mat_load_filament", &load_filament_img);
    lv_xml_register_image(NULL, "mat_unload_filament", &unload_filament_img);
    lv_xml_register_image(NULL, "mat_retract", &retract_img);

    // Fan & Cooling
    lv_xml_register_image(NULL, "mat_fan", &fan);
    lv_xml_register_image(NULL, "mat_fan_on", &fan_on);
    lv_xml_register_image(NULL, "mat_fan_off", &fan_off_img);

    // Lighting
    lv_xml_register_image(NULL, "mat_light", &light_img);
    lv_xml_register_image(NULL, "mat_light_off", &light_off);

    // Network & Communication
    lv_xml_register_image(NULL, "mat_network", &network_img);

    // Tuning & Adjustments
    lv_xml_register_image(NULL, "mat_fine_tune", &fine_tune_img);
    lv_xml_register_image(NULL, "mat_flow_down", &flow_down_img);
    lv_xml_register_image(NULL, "mat_flow_up", &flow_up_img);
    lv_xml_register_image(NULL, "mat_speed_down", &speed_down_img);
    lv_xml_register_image(NULL, "mat_speed_up", &speed_up_img);
    lv_xml_register_image(NULL, "mat_pa_minus", &pa_minus_img);
    lv_xml_register_image(NULL, "mat_pa_plus", &pa_plus_img);

    // Calibration & Advanced
    lv_xml_register_image(NULL, "mat_bedmesh", &bedmesh_img);
    lv_xml_register_image(NULL, "mat_belts_calibration", &belts_calibration_img);
    lv_xml_register_image(NULL, "mat_inputshaper", &inputshaper_img);
    lv_xml_register_image(NULL, "mat_limit", &limit_img);

    // System & Info
    lv_xml_register_image(NULL, "mat_info", &info_img);
    lv_xml_register_image(NULL, "mat_sysinfo", &sysinfo_img);
    lv_xml_register_image(NULL, "mat_power_devices", &power_devices_img);
    lv_xml_register_image(NULL, "mat_motor", &motor_img);
    lv_xml_register_image(NULL, "mat_motor_off", &motor_off_img);
    lv_xml_register_image(NULL, "mat_update", &update_img);
    lv_xml_register_image(NULL, "mat_emergency", &emergency);
    lv_xml_register_image(NULL, "mat_delete", &delete_img);

    // Monitoring & Display
    lv_xml_register_image(NULL, "mat_chart", &chart_img);
    lv_xml_register_image(NULL, "mat_layers", &layers_img);
    lv_xml_register_image(NULL, "mat_clock", &clock_img);
    lv_xml_register_image(NULL, "mat_hourglass", &hourglass);

    // Misc
    lv_xml_register_image(NULL, "mat_spoolman", &spoolman_img);

    LV_LOG_USER("Material Design icons registered successfully");
}
