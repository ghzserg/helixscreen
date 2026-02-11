// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "subject_managed_panel.h"

#include <lvgl.h>
#include <string>

#include "hv/json.hpp"

namespace helix {

/**
 * @brief Manages LED-related subjects for printer state
 *
 * Tracks a single LED object (neopixel, dotstar, etc.) and provides
 * RGBW color values plus derived brightness and on/off state.
 * Extracted from PrinterState as part of god class decomposition.
 */
class PrinterLedState {
  public:
    PrinterLedState() = default;
    ~PrinterLedState() = default;

    // Non-copyable
    PrinterLedState(const PrinterLedState&) = delete;
    PrinterLedState& operator=(const PrinterLedState&) = delete;

    /**
     * @brief Initialize LED subjects
     * @param register_xml If true, register subjects with LVGL XML system
     */
    void init_subjects(bool register_xml = true);

    /**
     * @brief Deinitialize subjects (called by SubjectManager automatically)
     */
    void deinit_subjects();

    /**
     * @brief Update LED state from Moonraker status JSON
     * @param status JSON object containing LED data (keyed by tracked LED name)
     */
    void update_from_status(const nlohmann::json& status);

    /**
     * @brief Set which LED to track for state updates
     *
     * Call this after loading config to tell PrinterLedState which LED object
     * to monitor from Moonraker notifications. The LED name should match
     * the Klipper config (e.g., "neopixel chamber_light", "led status_led").
     *
     * @param led_name Full LED name including type prefix, or empty to disable
     */
    void set_tracked_led(const std::string& led_name);

    /**
     * @brief Get the currently tracked LED name
     * @return LED name being tracked, or empty string if none
     */
    std::string get_tracked_led() const {
        return tracked_led_name_;
    }

    /**
     * @brief Check if an LED is configured for tracking
     * @return true if a LED name has been set
     */
    bool has_tracked_led() const {
        return !tracked_led_name_.empty();
    }

    // Subject accessors
    lv_subject_t* get_led_state_subject() {
        return &led_state_;
    }
    lv_subject_t* get_led_r_subject() {
        return &led_r_;
    }
    lv_subject_t* get_led_g_subject() {
        return &led_g_;
    }
    lv_subject_t* get_led_b_subject() {
        return &led_b_;
    }
    lv_subject_t* get_led_w_subject() {
        return &led_w_;
    }
    lv_subject_t* get_led_brightness_subject() {
        return &led_brightness_;
    }

  private:
    friend class PrinterLedStateTestAccess;

    SubjectManager subjects_;
    bool subjects_initialized_ = false;
    std::string tracked_led_name_;

    // LED subjects
    lv_subject_t led_state_{};      // 0=off, 1=on
    lv_subject_t led_r_{};          // 0-255
    lv_subject_t led_g_{};          // 0-255
    lv_subject_t led_b_{};          // 0-255
    lv_subject_t led_w_{};          // 0-255
    lv_subject_t led_brightness_{}; // 0-100 (derived from max channel)
};

} // namespace helix
