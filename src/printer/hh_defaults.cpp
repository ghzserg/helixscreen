// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hh_defaults.h"

namespace helix::printer {

std::vector<DeviceSection> hh_default_sections() {
    return {
        {"setup", "Setup", 0, "Calibration and system configuration"},
        {"speed", "Speed", 1, "Motor speeds and acceleration"},
        {"maintenance", "Maintenance", 2, "Testing, servo, and motor operations"},
    };
}

std::vector<DeviceAction> hh_default_actions() {
    std::vector<DeviceAction> actions;

    // --- Setup section ---
    {
        DeviceAction a;
        a.id = "calibrate_bowden";
        a.label = "Calibrate Bowden";
        a.section = "setup";
        a.type = ActionType::BUTTON;
        a.enabled = true;
        actions.push_back(std::move(a));
    }
    {
        DeviceAction a;
        a.id = "calibrate_encoder";
        a.label = "Calibrate Encoder";
        a.section = "setup";
        a.type = ActionType::BUTTON;
        a.enabled = true;
        actions.push_back(std::move(a));
    }
    {
        DeviceAction a;
        a.id = "calibrate_gear";
        a.label = "Calibrate Gear";
        a.section = "setup";
        a.type = ActionType::BUTTON;
        a.enabled = true;
        actions.push_back(std::move(a));
    }
    {
        DeviceAction a;
        a.id = "calibrate_gates";
        a.label = "Calibrate Gates";
        a.section = "setup";
        a.type = ActionType::BUTTON;
        a.enabled = true;
        actions.push_back(std::move(a));
    }
    {
        DeviceAction a;
        a.id = "led_mode";
        a.label = "LED Mode";
        a.section = "setup";
        a.type = ActionType::DROPDOWN;
        a.options = {"off", "gate_status", "filament_color", "on"};
        a.current_value = std::string("off");
        a.enabled = true;
        actions.push_back(std::move(a));
    }
    {
        DeviceAction a;
        a.id = "calibrate_servo";
        a.label = "Calibrate Servo";
        a.section = "setup";
        a.type = ActionType::BUTTON;
        a.enabled = true;
        actions.push_back(std::move(a));
    }

    // --- Speed section ---
    {
        DeviceAction a;
        a.id = "gear_load_speed";
        a.label = "Gear Load Speed";
        a.section = "speed";
        a.type = ActionType::SLIDER;
        a.current_value = 150.0;
        a.min_value = 10.0;
        a.max_value = 300.0;
        a.unit = "mm/s";
        a.enabled = true;
        actions.push_back(std::move(a));
    }
    {
        DeviceAction a;
        a.id = "gear_unload_speed";
        a.label = "Gear Unload Speed";
        a.section = "speed";
        a.type = ActionType::SLIDER;
        a.current_value = 80.0;
        a.min_value = 10.0;
        a.max_value = 300.0;
        a.unit = "mm/s";
        a.enabled = true;
        actions.push_back(std::move(a));
    }
    {
        DeviceAction a;
        a.id = "selector_speed";
        a.label = "Selector Speed";
        a.section = "speed";
        a.type = ActionType::SLIDER;
        a.current_value = 200.0;
        a.min_value = 10.0;
        a.max_value = 300.0;
        a.unit = "mm/s";
        a.enabled = true;
        actions.push_back(std::move(a));
    }

    // --- Maintenance section ---
    {
        DeviceAction a;
        a.id = "test_grip";
        a.label = "Test Grip";
        a.section = "maintenance";
        a.type = ActionType::BUTTON;
        a.enabled = true;
        actions.push_back(std::move(a));
    }
    {
        DeviceAction a;
        a.id = "test_load";
        a.label = "Test Load";
        a.section = "maintenance";
        a.type = ActionType::BUTTON;
        a.enabled = true;
        actions.push_back(std::move(a));
    }
    {
        DeviceAction a;
        a.id = "motors_toggle";
        a.label = "Motors";
        a.section = "maintenance";
        a.type = ActionType::TOGGLE;
        a.current_value = true;
        a.enabled = true;
        actions.push_back(std::move(a));
    }
    {
        DeviceAction a;
        a.id = "servo_buzz";
        a.label = "Buzz Servo";
        a.section = "maintenance";
        a.type = ActionType::BUTTON;
        a.enabled = true;
        actions.push_back(std::move(a));
    }
    {
        DeviceAction a;
        a.id = "reset_servo_counter";
        a.label = "Reset Servo Counter";
        a.section = "maintenance";
        a.type = ActionType::BUTTON;
        a.enabled = true;
        actions.push_back(std::move(a));
    }
    {
        DeviceAction a;
        a.id = "reset_blade_counter";
        a.label = "Reset Blade Counter";
        a.section = "maintenance";
        a.type = ActionType::BUTTON;
        a.enabled = true;
        actions.push_back(std::move(a));
    }

    return actions;
}

} // namespace helix::printer
