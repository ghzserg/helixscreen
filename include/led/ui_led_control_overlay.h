// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_observer_guard.h"

#include "led/led_backend.h"
#include "overlay_base.h"
#include "subject_managed_panel.h"

namespace helix {
class PrinterState;
}
using helix::PrinterState;
class MoonrakerAPI;

namespace helix::led {

/**
 * @file ui_led_control_overlay.h
 * @brief Full-screen overlay for controlling LED strips, effects, WLED, and macros
 *
 * Displays sections for each available LED backend:
 * - Native color: Color presets, brightness slider, custom color picker
 * - Effects: led_effect buttons with stop-all
 * - WLED: Preset buttons and brightness
 * - Macros: On/off and custom action buttons
 *
 * Sections auto-hide based on discovered backends.
 * Opened via long-press on home panel lightbulb.
 *
 * @see LedController for backend discovery and control
 * @see led_control_overlay.xml for layout definition
 */
class LedControlOverlay : public OverlayBase {
  public:
    explicit LedControlOverlay(PrinterState& printer_state);
    ~LedControlOverlay() override;

    void init_subjects() override;
    lv_obj_t* create(lv_obj_t* parent) override;
    void register_callbacks() override;

    [[nodiscard]] const char* get_name() const override {
        return "LED Control";
    }

    void on_activate() override;
    void on_deactivate() override;
    void cleanup() override;

    void set_api(MoonrakerAPI* api) {
        api_ = api;
    }

  private:
    // Section population
    void populate_sections();
    void populate_strip_selector();
    void populate_color_presets();
    void populate_effects();
    void populate_wled();
    void populate_macros();
    void update_section_visibility();

    // Action handlers
    void handle_color_preset(uint32_t color);
    void handle_brightness_change(int brightness);
    void handle_custom_color();
    void handle_effect_activate(const std::string& effect_name);
    void handle_native_turn_off();
    void handle_wled_toggle();
    void handle_wled_preset(int preset_id);
    void handle_wled_brightness(int brightness);
    void handle_macro_on(const std::string& macro_name);
    void handle_macro_off(const std::string& macro_name);
    void handle_macro_toggle(const std::string& macro_name);
    void handle_macro_custom(const std::string& gcode);
    void handle_strip_selected(const std::string& strip_id);

    // WLED status refresh
    void refresh_wled_status();

    // Helpers
    void apply_current_color();
    void send_color_to_strips(double r, double g, double b, double w);
    void update_brightness_text(int brightness);
    void update_wled_brightness_text(int brightness);
    void update_current_color_swatch();
    void highlight_active_effect(const std::string& active_name);

    // Helper for creating macro chips with click handlers
    using MacroClickHandler = void (LedControlOverlay::*)(const std::string&);
    void add_macro_chip(const std::string& label, const std::string& data,
                        MacroClickHandler handler);
    void populate_macro_controls(const LedMacroInfo& macro);

    // Update WLED toggle button appearance based on strip state
    void update_wled_toggle_button();

    // Static callbacks for XML event_cb (delegate to singleton)
    static void on_custom_color_cb(lv_event_t* e);
    static void on_native_turn_off_cb(lv_event_t* e);
    static void on_wled_toggle_cb(lv_event_t* e);
    static void on_color_preset_cb(lv_event_t* e);
    static void on_brightness_changed_cb(lv_event_t* e);

    // Dependencies
    PrinterState& printer_state_;
    MoonrakerAPI* api_ = nullptr;

    // Widget references (owned by LVGL, not us)
    // Section visibility handled declaratively via bind_flag_if_eq subjects
    lv_obj_t* strip_selector_section_ = nullptr;
    lv_obj_t* color_presets_container_ = nullptr;
    lv_obj_t* effects_container_ = nullptr;
    lv_obj_t* wled_presets_container_ = nullptr;
    lv_obj_t* macro_buttons_container_ = nullptr;
    lv_obj_t* current_color_swatch_ = nullptr;
    // brightness_slider and wled_brightness_slider use bind_value — no C++ reference needed
    // wled_toggle_btn_ removed — styling driven by led_wled_is_on subject + bind_style

    // Subjects for XML bindings
    SubjectManager subjects_;
    lv_subject_t brightness_subject_{};
    lv_subject_t brightness_text_subject_{};
    char brightness_text_buf_[16] = {0};
    lv_subject_t strip_name_subject_{};
    char strip_name_buf_[64] = {0};
    lv_subject_t wled_brightness_subject_{};
    lv_subject_t wled_brightness_text_subject_{};
    char wled_brightness_text_buf_[16] = {0};
    lv_subject_t wled_is_on_{};

    // Section visibility subjects (0=hidden, 1=visible)
    lv_subject_t native_visible_{};
    lv_subject_t effects_visible_{};
    lv_subject_t wled_visible_{};
    lv_subject_t macro_visible_{};
    lv_subject_t strip_selector_visible_{};

    // Observers
    ObserverGuard wled_brightness_observer_;

    // State
    int current_brightness_ = 100;
    uint32_t current_color_ = 0xFFFFFF;
    LedBackendType selected_backend_type_ = LedBackendType::NATIVE;
};

} // namespace helix::led

/**
 * @brief Get global LedControlOverlay instance
 * @return Reference to singleton instance
 * @throws std::runtime_error if not initialized
 */
helix::led::LedControlOverlay& get_led_control_overlay();

/**
 * @brief Initialize global LedControlOverlay instance
 * @param printer_state Reference to global PrinterState
 */
void init_led_control_overlay(PrinterState& printer_state);
