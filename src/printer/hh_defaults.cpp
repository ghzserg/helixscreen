// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hh_defaults.h"

namespace helix::printer {

std::vector<DeviceSection> hh_default_sections() {
    return {
        {"setup", "Setup", 0, "Calibration and system configuration"},
        {"speed", "Speed", 1, "Motor speeds and acceleration"},
        {"accessories", "Accessories", 2, "eSpooler, clog detection, and sensor settings"},
        {"maintenance", "Maintenance", 3, "Testing, servo, and motor operations"},
    };
}

std::vector<DeviceAction> hh_default_actions() {
    std::vector<DeviceAction> actions;

    // Helpers to reduce boilerplate (DeviceAction::enabled defaults to true)
    auto add_button = [&](std::string id, std::string label, std::string section) {
        DeviceAction a;
        a.id = std::move(id);
        a.label = std::move(label);
        a.section = std::move(section);
        a.type = ActionType::BUTTON;
        actions.push_back(std::move(a));
    };

    auto add_dropdown = [&](std::string id, std::string label, std::string section,
                            std::vector<std::string> options, std::string default_val) {
        DeviceAction a;
        a.id = std::move(id);
        a.label = std::move(label);
        a.section = std::move(section);
        a.type = ActionType::DROPDOWN;
        a.options = std::move(options);
        a.current_value = std::move(default_val);
        actions.push_back(std::move(a));
    };

    auto add_slider = [&](std::string id, std::string label, std::string section,
                          double default_val, float min_val, float max_val, std::string unit) {
        DeviceAction a;
        a.id = std::move(id);
        a.label = std::move(label);
        a.section = std::move(section);
        a.type = ActionType::SLIDER;
        a.current_value = default_val;
        a.min_value = min_val;
        a.max_value = max_val;
        a.unit = std::move(unit);
        actions.push_back(std::move(a));
    };

    // --- Setup section ---
    add_button("calibrate_bowden", "Calibrate Bowden", "setup");
    add_button("calibrate_encoder", "Calibrate Encoder", "setup");
    add_button("calibrate_gear", "Calibrate Gear", "setup");
    add_button("calibrate_gates", "Calibrate Gates", "setup");
    add_dropdown("led_mode", "LED Mode", "setup", {"off", "gate_status", "filament_color", "on"},
                 "off");
    add_button("calibrate_servo", "Calibrate Servo", "setup");

    // --- Speed section ---
    add_slider("gear_load_speed", "Gear Load Speed", "speed", 150.0, 10.0f, 300.0f, "mm/s");
    add_slider("gear_unload_speed", "Gear Unload Speed", "speed", 80.0, 10.0f, 300.0f, "mm/s");
    add_slider("selector_speed", "Selector Speed", "speed", 200.0, 10.0f, 300.0f, "mm/s");

    // --- Accessories section (v4) ---
    add_dropdown("espooler_mode", "eSpooler Mode", "accessories", {"off", "rewind", "assist"},
                 "off");
    add_dropdown("clog_detection", "Clog Detection", "accessories", {"Off", "Manual", "Auto"},
                 "Off");

    // --- Maintenance section ---
    add_button("test_grip", "Test Grip", "maintenance");
    add_button("test_load", "Test Load", "maintenance");
    {
        DeviceAction a;
        a.id = "motors_toggle";
        a.label = "Motors";
        a.section = "maintenance";
        a.type = ActionType::TOGGLE;
        a.current_value = true;
        actions.push_back(std::move(a));
    }
    add_button("servo_buzz", "Buzz Servo", "maintenance");
    add_button("reset_servo_counter", "Reset Servo Counter", "maintenance");
    add_button("reset_blade_counter", "Reset Blade Counter", "maintenance");

    return actions;
}

} // namespace helix::printer
