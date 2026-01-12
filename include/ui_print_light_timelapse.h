// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "subject_managed_panel.h"

#include <lvgl/lvgl.h>

#include <string>

// Forward declarations
class MoonrakerAPI;
class PrinterState;

/**
 * @file ui_print_light_timelapse.h
 * @brief Light and timelapse button controls extracted from PrintStatusPanel
 *
 * Manages the light and timelapse toggle buttons on the print status panel:
 * - Light button: Toggles configured LED on/off via Moonraker
 * - Timelapse button: Enables/disables timelapse recording via Moonraker plugin
 *
 * This class is a helper owned by PrintStatusPanel, not a standalone component.
 * It manages the subjects and callbacks for the buttons' reactive UI.
 *
 * @pattern Helper class with subject management
 * @threading Main thread only (LVGL)
 */
class PrintLightTimelapseControls {
  public:
    PrintLightTimelapseControls();
    ~PrintLightTimelapseControls();

    // Non-copyable
    PrintLightTimelapseControls(const PrintLightTimelapseControls&) = delete;
    PrintLightTimelapseControls& operator=(const PrintLightTimelapseControls&) = delete;

    /**
     * @brief Initialize subjects for XML binding
     *
     * Registers light_button_icon, timelapse_button_icon, timelapse_button_label
     * subjects and XML event callbacks.
     */
    void init_subjects();

    /**
     * @brief Deinitialize subjects
     *
     * Called during cleanup. Safe to call multiple times.
     */
    void deinit_subjects();

    /**
     * @brief Set the Moonraker API for sending commands
     * @param api Pointer to MoonrakerAPI (can be nullptr for mock mode)
     */
    void set_api(MoonrakerAPI* api) {
        api_ = api;
    }

    /**
     * @brief Set the configured LED name
     * @param led LED name from wizard config (e.g., "chamber_light")
     */
    void set_configured_led(const std::string& led) {
        configured_led_ = led;
    }

    /**
     * @brief Get the configured LED name
     * @return LED name, or empty string if not configured
     */
    const std::string& get_configured_led() const {
        return configured_led_;
    }

    /**
     * @brief Handle light button click
     *
     * Toggles the LED state via Moonraker API.
     * No-op if no LED is configured.
     */
    void handle_light_button();

    /**
     * @brief Handle timelapse button click
     *
     * Toggles timelapse recording via Moonraker timelapse plugin.
     */
    void handle_timelapse_button();

    /**
     * @brief Update LED state from PrinterState observer
     *
     * Called when LED state changes (from PrinterState subject).
     * Updates the light button icon accordingly.
     *
     * @param on true if LED is on, false if off
     */
    void update_led_state(bool on);

    /**
     * @brief Check if subjects have been initialized
     * @return true if init_subjects() was called
     */
    bool are_subjects_initialized() const {
        return subjects_initialized_;
    }

  private:
    //
    // === Dependencies ===
    //

    MoonrakerAPI* api_ = nullptr;

    //
    // === Subject Management ===
    //

    SubjectManager subjects_;
    bool subjects_initialized_ = false;

    //
    // === Light State ===
    //

    std::string configured_led_;
    bool led_on_ = false;
    lv_subject_t light_button_subject_;
    char light_button_buf_[8] = "\xF3\xB0\x8C\xB6"; // MDI lightbulb_outline (off state)

    //
    // === Timelapse State ===
    //

    bool timelapse_enabled_ = false;
    lv_subject_t timelapse_button_subject_;
    lv_subject_t timelapse_label_subject_;
    char timelapse_button_buf_[8] = ""; // MDI video/video-off icon
    char timelapse_label_buf_[16] = "Off";
};

/**
 * @brief Get global PrintLightTimelapseControls instance
 *
 * Used by XML event callbacks to route events to the controls instance.
 * The instance is managed by PrintStatusPanel.
 *
 * @return Reference to PrintLightTimelapseControls
 */
PrintLightTimelapseControls& get_global_light_timelapse_controls();

/**
 * @brief Set global PrintLightTimelapseControls instance
 *
 * Called by PrintStatusPanel during initialization.
 *
 * @param instance Pointer to PrintLightTimelapseControls (or nullptr to clear)
 */
void set_global_light_timelapse_controls(PrintLightTimelapseControls* instance);
