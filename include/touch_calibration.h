// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 356C LLC

#pragma once

#include <string>

namespace helix {

struct Point {
    int x = 0;
    int y = 0;
};

struct TouchCalibration {
    bool valid = false;
    float a = 1.0f, b = 0.0f, c = 0.0f; // screen_x = a*x + b*y + c
    float d = 0.0f, e = 1.0f, f = 0.0f; // screen_y = d*x + e*y + f
};

/**
 * @brief Compute affine calibration coefficients from 3 point pairs
 *
 * Uses the Maxim Integrated AN5296 algorithm (determinant-based).
 * Screen points are where targets appear on display.
 * Touch points are raw coordinates from touch controller.
 *
 * @param screen_points 3 screen coordinate targets
 * @param touch_points 3 corresponding raw touch coordinates
 * @param out Output calibration coefficients
 * @return true if successful, false if points are degenerate (collinear)
 */
bool compute_calibration(const Point screen_points[3], const Point touch_points[3],
                         TouchCalibration& out);

/**
 * @brief Transform raw touch point to screen coordinates
 *
 * @param cal Calibration coefficients (must be valid)
 * @param raw Raw touch point from controller
 * @param max_x Optional maximum X value for clamping (0 = no clamp)
 * @param max_y Optional maximum Y value for clamping (0 = no clamp)
 * @return Transformed screen coordinates (or raw if cal.valid is false)
 */
Point transform_point(const TouchCalibration& cal, Point raw, int max_x = 0, int max_y = 0);

/**
 * @brief Validate calibration coefficients are finite and within reasonable bounds
 *
 * @param cal Calibration to validate
 * @return true if all coefficients are finite and within bounds
 */
bool is_calibration_valid(const TouchCalibration& cal);

/// Maximum reasonable coefficient value for validation
constexpr float MAX_CALIBRATION_COEFFICIENT = 1000.0f;

/**
 * @brief Check if a sysfs phys path indicates a USB-connected input device
 *
 * USB HID touchscreens (HDMI displays like BTT 5") report mapped coordinates
 * natively and do not need affine calibration. Only resistive/platform
 * touchscreens (sun4i_ts on AD5M, etc.) need the calibration wizard.
 *
 * USB devices have physical paths like "usb-0000:01:00.0-1.3/input0".
 * Platform devices have empty phys or paths like "sun4i_ts" without "usb".
 *
 * @param phys The sysfs phys string from /sys/class/input/eventN/device/phys
 * @return true if the device is USB-connected
 */
inline bool is_usb_input_phys(const std::string& phys) {
    return phys.find("usb") != std::string::npos;
}

/**
 * @brief Check if a device name matches known touchscreen patterns
 *
 * Used during touch device auto-detection to prefer known touchscreen
 * controllers. Performs case-insensitive substring matching against a list
 * of known touchscreen name patterns.
 *
 * Non-touch devices like HDMI CEC ("vc4-hdmi"), keyboard, or mouse
 * devices will not match and should return false.
 *
 * @param name The device name from /sys/class/input/eventN/device/name
 * @return true if the name matches a known touchscreen pattern
 */
inline bool is_known_touchscreen_name(const std::string& name) {
    static const char* patterns[] = {"rtp",    // Resistive touch panel (sun4i_ts on AD5M)
                                     "touch",  // Generic touchscreen
                                     "sun4i",  // Allwinner touch controller
                                     "ft5x",   // FocalTech touch controllers
                                     "goodix", // Goodix touch controllers
                                     "gt9",    // Goodix GT9xx series
                                     "ili2",   // ILI touch controllers
                                     "atmel",  // Atmel touch controllers
                                     "edt-ft", // EDT FocalTech displays
                                     "tsc",    // Touch screen controller
                                     nullptr};

    std::string lower_name = name;
    for (auto& c : lower_name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    for (int i = 0; patterns[i] != nullptr; ++i) {
        if (lower_name.find(patterns[i]) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Check if a device name matches a known resistive touchscreen controller
 *
 * Only resistive touchscreens need affine calibration. Capacitive controllers
 * (Goodix, FocalTech, ILI, Atmel, EDT-FT) are factory-calibrated and report
 * mapped screen coordinates via their kernel driver.
 *
 * @param name The device name from /sys/class/input/eventN/device/name
 * @return true if the name matches a known resistive touchscreen controller
 */
inline bool is_resistive_touchscreen_name(const std::string& name) {
    static const char* patterns[] = {"rtp",   // Resistive touch panel
                                     "sun4i", // Allwinner resistive controller (AD5M)
                                     "tsc",   // Generic resistive touch screen controller
                                     nullptr};

    std::string lower_name = name;
    for (auto& c : lower_name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    for (int i = 0; patterns[i] != nullptr; ++i) {
        if (lower_name.find(patterns[i]) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Determine if a touch input device needs affine calibration
 *
 * Single source of truth for calibration decisions. Returns true ONLY for
 * resistive touchscreens that need the calibration wizard.
 *
 * Devices that do NOT need calibration:
 * - USB HID touchscreens (report mapped coordinates natively)
 * - I2C capacitive touchscreens (Goodix, FocalTech, etc. — factory-calibrated)
 * - Virtual/uinput devices (VNC virtual touchscreen, testing)
 * - Non-touch devices used as pointer fallback (CEC remotes, etc.)
 * - Unknown devices (safer to skip than show broken calibration)
 *
 * @param name Device name from /sys/class/input/eventN/device/name
 * @param phys Device phys from /sys/class/input/eventN/device/phys
 * @param has_abs_xy Whether device has ABS_X/ABS_Y capabilities
 * @return true if calibration wizard should be shown for this device
 */
inline bool device_needs_calibration(const std::string& name, const std::string& phys,
                                     bool has_abs_xy) {
    // No ABS_X/ABS_Y = not a touchscreen, nothing to calibrate
    if (!has_abs_xy) {
        return false;
    }

    // USB HID touchscreens report mapped coordinates natively
    if (is_usb_input_phys(phys)) {
        return false;
    }

    // Virtual/uinput devices (VNC injection, testing) don't need calibration
    // These have empty phys and names like "virtual-touchscreen"
    if (name.find("virtual") != std::string::npos) {
        return false;
    }

    // Only known resistive touchscreen controllers need affine calibration.
    // Capacitive controllers (Goodix, FocalTech, ILI, Atmel) are factory-calibrated
    // and report mapped screen coordinates — even when connected via I2C, not USB.
    return is_resistive_touchscreen_name(name);
}

} // namespace helix
