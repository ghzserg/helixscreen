// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file printer_led_state.cpp
 * @brief LED state management extracted from PrinterState
 *
 * Manages LED subjects including RGBW channels, brightness, and on/off state.
 * Extracted from PrinterState as part of god class decomposition.
 */

#include "printer_led_state.h"

#include "state/subject_macros.h"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace helix {

void PrinterLedState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        spdlog::debug("[PrinterLedState] Subjects already initialized, skipping");
        return;
    }

    spdlog::trace("[PrinterLedState] Initializing subjects (register_xml={})", register_xml);

    // LED state subject (0=off, 1=on, derived from LED color data)
    INIT_SUBJECT_INT(led_state, 0, subjects_, register_xml);

    // LED RGBW channel subjects (0-255 range)
    INIT_SUBJECT_INT(led_r, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(led_g, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(led_b, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(led_w, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(led_brightness, 0, subjects_, register_xml);

    subjects_initialized_ = true;
    spdlog::trace("[PrinterLedState] Subjects initialized successfully");
}

void PrinterLedState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[PrinterLedState] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void PrinterLedState::update_from_status(const nlohmann::json& status) {
    // Update LED state if we're tracking an LED
    // LED object names in Moonraker are like "neopixel chamber_light" or "led status_led"
    if (tracked_led_name_.empty() || !status.contains(tracked_led_name_)) {
        return;
    }

    const auto& led = status[tracked_led_name_];

    if (!led.contains("color_data") || !led["color_data"].is_array() || led["color_data"].empty()) {
        return;
    }

    // color_data is array of [R, G, B, W] arrays (one per LED in strip)
    // For on/off, we check if any color component of the first LED is > 0
    const auto& first_led = led["color_data"][0];
    if (!first_led.is_array() || first_led.size() < 3 || !first_led[0].is_number() ||
        !first_led[1].is_number() || !first_led[2].is_number()) {
        return;
    }

    double r = first_led[0].get<double>();
    double g = first_led[1].get<double>();
    double b = first_led[2].get<double>();
    double w =
        (first_led.size() >= 4 && first_led[3].is_number()) ? first_led[3].get<double>() : 0.0;

    // Convert 0.0-1.0 range to 0-255 integer range (clamp for safety)
    int r_int = std::clamp(static_cast<int>(r * 255.0 + 0.5), 0, 255);
    int g_int = std::clamp(static_cast<int>(g * 255.0 + 0.5), 0, 255);
    int b_int = std::clamp(static_cast<int>(b * 255.0 + 0.5), 0, 255);
    int w_int = std::clamp(static_cast<int>(w * 255.0 + 0.5), 0, 255);

    // Compute brightness as max of RGBW channels (0-100%)
    int max_channel = std::max({r_int, g_int, b_int, w_int});
    int brightness = (max_channel * 100) / 255;

    // Update RGBW subjects
    lv_subject_set_int(&led_r_, r_int);
    lv_subject_set_int(&led_g_, g_int);
    lv_subject_set_int(&led_b_, b_int);
    lv_subject_set_int(&led_w_, w_int);
    lv_subject_set_int(&led_brightness_, brightness);

    // LED is "on" if any channel is non-zero
    bool is_on = (max_channel > 0);
    int new_state = is_on ? 1 : 0;

    int old_state = lv_subject_get_int(&led_state_);
    if (new_state != old_state) {
        lv_subject_set_int(&led_state_, new_state);
        spdlog::debug("[PrinterLedState] LED {} state: {} (R={} G={} B={} W={} brightness={}%)",
                      tracked_led_name_, is_on ? "ON" : "OFF", r_int, g_int, b_int, w_int,
                      brightness);
    }
}

void PrinterLedState::set_tracked_led(const std::string& led_name) {
    tracked_led_name_ = led_name;
    if (!led_name.empty()) {
        spdlog::debug("[PrinterLedState] Tracking LED: {}", led_name);
    } else {
        spdlog::debug("[PrinterLedState] LED tracking disabled");
    }
}

} // namespace helix
