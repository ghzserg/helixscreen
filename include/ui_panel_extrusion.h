// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"
#include "ui_panel_base.h"

/**
 * @brief Extrusion control panel - filament extrude/retract with safety checks
 *
 * Provides amount selector (5/10/25/50mm), speed control slider (60-600 mm/min),
 * extrude/retract/purge buttons, animated filament flow visualization,
 * and cold extrusion prevention (requires nozzle >= 170°C).
 */
class ExtrusionPanel : public PanelBase {
  public:
    ExtrusionPanel(PrinterState& printer_state, MoonrakerAPI* api);
    ~ExtrusionPanel() override = default;

    void init_subjects() override;
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;
    const char* get_name() const override {
        return "Extrusion Panel";
    }
    const char* get_xml_component_name() const override {
        return "extrusion_panel";
    }

    void set_temp(int current, int target);
    int get_amount() const {
        return selected_amount_;
    }
    int get_speed() const {
        return extrusion_speed_mmpm_;
    }
    void set_speed(int speed_mmpm);
    bool is_extrusion_allowed() const;
    void set_limits(int min_temp, int max_temp);

  private:
    // Reactive subjects for UI bindings
    lv_subject_t temp_status_subject_;
    lv_subject_t warning_temps_subject_;
    lv_subject_t safety_warning_visible_subject_;
    lv_subject_t speed_display_subject_;

    // String buffers for subject text
    char temp_status_buf_[64];
    char warning_temps_buf_[64];
    char speed_display_buf_[32];

    // State variables
    int nozzle_current_ = 25;
    int nozzle_target_ = 0;
    int selected_amount_ = 10;
    int extrusion_speed_mmpm_ = 300; // Default 300 mm/min (5 mm/s)
    int nozzle_min_temp_ = 0;
    int nozzle_max_temp_ = 500;
    bool animation_active_ = false;

    // Widget pointers
    lv_obj_t* btn_extrude_ = nullptr;
    lv_obj_t* btn_retract_ = nullptr;
    lv_obj_t* btn_purge_ = nullptr;
    lv_obj_t* safety_warning_ = nullptr;
    lv_obj_t* amount_buttons_[4] = {nullptr};
    lv_obj_t* speed_slider_ = nullptr;
    lv_obj_t* filament_anim_obj_ = nullptr;

    // Animation constants
    static constexpr int PURGE_AMOUNT_MM = 50;

    static constexpr int AMOUNT_VALUES[4] = {5, 10, 25, 50};

    // Setup methods
    void setup_amount_buttons();
    void setup_action_buttons();
    void setup_speed_slider();
    void setup_animation_widget();
    void setup_temperature_observer();

    // Update methods
    void update_temp_status();
    void update_warning_text();
    void update_safety_state();
    void update_amount_buttons_visual();
    void update_speed_display();

    // Action handlers
    void handle_amount_button(lv_obj_t* btn);
    void handle_extrude();
    void handle_retract();
    void handle_purge();

    // Animation control
    void start_extrusion_animation(bool is_extruding);
    void stop_extrusion_animation();

    // Static callbacks (amount buttons use imperative wiring due to loop/index logic)
    static void on_amount_button_clicked(lv_event_t* e);
    static void on_nozzle_temp_changed(lv_observer_t* observer, lv_subject_t* subject);

    // Observer for nozzle temperature (ObserverGuard handles cleanup)
    ObserverGuard nozzle_temp_observer_;
};

// Global instance accessors
ExtrusionPanel& get_global_extrusion_panel();
ExtrusionPanel& get_global_controls_extrusion_panel();
