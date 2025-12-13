// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"
#include "ui_panel_base.h"

/**
 * @file ui_panel_motion.h
 * @brief Motion panel - XYZ movement and homing control
 */

// Jog distance options
typedef enum {
    JOG_DIST_0_1MM = 0,
    JOG_DIST_1MM = 1,
    JOG_DIST_10MM = 2,
    JOG_DIST_100MM = 3
} jog_distance_t;

// Jog direction
typedef enum {
    JOG_DIR_N,  // +Y
    JOG_DIR_S,  // -Y
    JOG_DIR_E,  // +X
    JOG_DIR_W,  // -X
    JOG_DIR_NE, // +X+Y
    JOG_DIR_NW, // -X+Y
    JOG_DIR_SE, // +X-Y
    JOG_DIR_SW  // -X-Y
} jog_direction_t;

class MotionPanel : public PanelBase {
  public:
    MotionPanel(PrinterState& printer_state, MoonrakerAPI* api);
    ~MotionPanel() override = default;

    void init_subjects() override;
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;
    const char* get_name() const override {
        return "Motion Panel";
    }
    const char* get_xml_component_name() const override {
        return "motion_panel";
    }

    lv_obj_t* get_panel() const {
        return panel_;
    }
    void set_position(float x, float y, float z);
    jog_distance_t get_distance() const {
        return current_distance_;
    }
    void set_distance(jog_distance_t dist);
    void jog(jog_direction_t direction, float distance_mm);
    void home(char axis);
    void handle_z_button(const char* name);

  private:
    lv_subject_t pos_x_subject_;
    lv_subject_t pos_y_subject_;
    lv_subject_t pos_z_subject_;
    lv_subject_t z_axis_label_subject_; // "Bed" or "Print Head"
    char pos_x_buf_[32];
    char pos_y_buf_[32];
    char pos_z_buf_[32];
    char z_axis_label_buf_[16];
    bool bed_moves_ = false; // If true, invert Z direction (arrows match bed movement)

    jog_distance_t current_distance_ = JOG_DIST_1MM;
    float current_x_ = 0.0f;
    float current_y_ = 0.0f;
    float current_z_ = 0.0f;

    lv_obj_t* jog_pad_ = nullptr;
    lv_obj_t* dist_buttons_[4] = {nullptr};

    ObserverGuard position_x_observer_;
    ObserverGuard position_y_observer_;
    ObserverGuard position_z_observer_;
    ObserverGuard bed_moves_observer_;

    void setup_distance_buttons();
    void setup_jog_pad();
    void setup_z_buttons();
    void setup_home_buttons();
    void register_position_observers();
    void update_distance_buttons();

    void handle_distance_button(lv_obj_t* btn);
    void handle_home_button(const char* name);

    static void jog_pad_jog_cb(jog_direction_t direction, float distance_mm, void* user_data);
    static void jog_pad_home_cb(void* user_data);
    static void on_distance_button_clicked(lv_event_t* e);
    static void on_home_button_clicked(lv_event_t* e);
    static void on_position_x_changed(lv_observer_t* observer, lv_subject_t* subject);
    static void on_position_y_changed(lv_observer_t* observer, lv_subject_t* subject);
    static void on_position_z_changed(lv_observer_t* observer, lv_subject_t* subject);
    static void on_bed_moves_changed(lv_observer_t* observer, lv_subject_t* subject);

    void update_z_axis_label(bool bed_moves);
};

MotionPanel& get_global_motion_panel();
