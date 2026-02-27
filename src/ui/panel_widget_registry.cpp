// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "panel_widget_registry.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <string_view>

namespace helix {

// Forward declarations — widget registration functions (defined in each widget .cpp)
void register_fan_stack_widget();
void register_temperature_widget();
void register_temp_stack_widget();
void register_power_widget();
void register_network_widget();
void register_led_widget();
void register_thermistor_widget();
void register_favorite_macro_widgets();
void register_shutdown_widget();

// Vector order defines the default display order on the home panel.
// NOTE: Factories are registered at runtime via init_widget_registrations(),
// NOT during static initialization. Do not add file-scope self-registration.
// clang-format off
static std::vector<PanelWidgetDef> s_widget_defs = {
    {"shutdown",         "Shutdown/Reboot",   "power",            "Shutdown or reboot the printer host",          "Shutdown/Reboot",  nullptr,             false},
    {"power",            "Power",            "power_cycle",      "Moonraker power device controls",              "Power",            "power_device_count"},
    {"network",          "Network",          "wifi_strength_4",  "Wi-Fi and ethernet connection status",         "Network",          nullptr,             false},
    {"firmware_restart", "Firmware Restart",  "refresh",          "Restart Klipper firmware",                     "Firmware Restart", nullptr,             false},
    {"ams",              "AMS Status",        "filament",         "Multi-material spool status and control",      "AMS Status",       "ams_slot_count"},
    {"temperature",      "Nozzle Temperature","thermometer",      "Monitor and set nozzle temperature",           "Nozzle Temperature", nullptr},
    {"temp_stack",       "Temperatures",      "thermometer",      "Nozzle, bed, and chamber temps stacked",       "Temperatures",     nullptr,             false},
    {"led",              "LED Light",         "lightbulb_outline","Quick toggle, long press for full control",    "LED Light",        "printer_has_led"},
    {"humidity",         "Humidity",          "water",            "Enclosure humidity sensor readings",           "Humidity",         "humidity_sensor_count"},
    {"width_sensor",     "Width Sensor",      "ruler",            "Filament width sensor readings",               "Width Sensor",     "width_sensor_count"},
    {"probe",            "Probe",             "target",           "Z probe status and offset",                    "Probe",            "probe_count"},
    {"filament",         "Filament Sensor",   "filament_alert",   "Filament runout detection status",             "Filament Sensor",  "filament_sensor_count"},
    {"fan_stack",        "Fan Speeds",        "fan",              "Part, hotend, and auxiliary fan speeds",        "Fan Speeds",       nullptr},
    {"thermistor",       "Thermistor",        "thermometer",      "Monitor a custom temperature sensor",          "Thermistor",       "temp_sensor_count", false},
    {"favorite_macro_1", "Macro Button 1",    "play",             "Run a configured macro with one tap",          "Macro Button 1",   nullptr,             false},
    {"favorite_macro_2", "Macro Button 2",    "play",             "Run a configured macro with one tap",          "Macro Button 2",   nullptr,             false},
    {"notifications",    "Notifications",     "notifications",    "Pending alerts and system messages",           "Notifications",    nullptr},
};
// clang-format on

const std::vector<PanelWidgetDef>& get_all_widget_defs() {
    return s_widget_defs;
}

const PanelWidgetDef* find_widget_def(std::string_view id) {
    auto it = std::find_if(s_widget_defs.begin(), s_widget_defs.end(),
                           [&id](const PanelWidgetDef& def) { return id == def.id; });
    return it != s_widget_defs.end() ? &*it : nullptr;
}

size_t widget_def_count() {
    return s_widget_defs.size();
}

void register_widget_factory(std::string_view id, WidgetFactory factory) {
    for (auto& def : s_widget_defs) {
        if (id == def.id) {
            def.factory = std::move(factory);
            return;
        }
    }
    spdlog::warn("[PanelWidgetRegistry] Factory registration failed: '{}' not found", id);
}

void register_widget_subjects(std::string_view id, SubjectInitFn init_fn) {
    for (auto& def : s_widget_defs) {
        if (id == def.id) {
            def.init_subjects = std::move(init_fn);
            return;
        }
    }
    spdlog::warn("[PanelWidgetRegistry] Subject init registration failed: '{}' not found", id);
}

void init_widget_registrations() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    register_power_widget();
    register_network_widget();
    register_temperature_widget();
    register_temp_stack_widget();
    register_led_widget();
    register_fan_stack_widget();
    register_thermistor_widget();
    register_favorite_macro_widgets();
    register_shutdown_widget();

    spdlog::debug("[PanelWidgetRegistry] All widget factories registered");
}

} // namespace helix
