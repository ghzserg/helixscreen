// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_panel_base.h"

/**
 * @brief Extrusion control panel - filament extrude/retract with safety checks
 *
 * Provides amount selector (5/10/25/50mm), extrude/retract buttons,
 * cold extrusion prevention (requires nozzle >= 170°C).
 */
class ExtrusionPanel : public PanelBase {
  public:
    ExtrusionPanel(PrinterState& printer_state, MoonrakerAPI* api);
    ~ExtrusionPanel() override = default;

    void init_subjects() override;
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;
    const char* get_name() const override { return "Extrusion Panel"; }
    const char* get_xml_component_name() const override { return "extrusion_panel"; }

    void set_temp(int current, int target);
    int get_amount() const { return selected_amount_; }
    bool is_extrusion_allowed() const;
    void set_limits(int min_temp, int max_temp);

  private:
    lv_subject_t temp_status_subject_;
    lv_subject_t warning_temps_subject_;
    lv_subject_t safety_warning_visible_subject_;

    char temp_status_buf_[64];
    char warning_temps_buf_[64];

    int nozzle_current_ = 25;
    int nozzle_target_ = 0;
    int selected_amount_ = 10;
    int nozzle_min_temp_ = 0;
    int nozzle_max_temp_ = 500;

    lv_obj_t* btn_extrude_ = nullptr;
    lv_obj_t* btn_retract_ = nullptr;
    lv_obj_t* safety_warning_ = nullptr;
    lv_obj_t* amount_buttons_[4] = {nullptr};

    static constexpr int AMOUNT_VALUES[4] = {5, 10, 25, 50};

    void setup_amount_buttons();
    void setup_action_buttons();
    void setup_temperature_observer();
    void update_temp_status();
    void update_warning_text();
    void update_safety_state();
    void update_amount_buttons_visual();

    void handle_amount_button(lv_obj_t* btn);
    void handle_extrude();
    void handle_retract();

    static void on_amount_button_clicked(lv_event_t* e);
    static void on_extrude_clicked(lv_event_t* e);
    static void on_retract_clicked(lv_event_t* e);
    static void on_nozzle_temp_changed(lv_observer_t* observer, lv_subject_t* subject);
};

// Global instance accessor (needed by main.cpp)
ExtrusionPanel& get_global_controls_extrusion_panel();
