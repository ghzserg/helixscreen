// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "overlay_base.h"
#include "subject_managed_panel.h"

/**
 * @file ui_panel_motion.h
 * @brief Motion panel - XYZ movement and homing control
 *
 * Overlay panel for jogging the printer head in X/Y/Z directions and homing axes.
 * Uses OverlayBase pattern with lifecycle hooks.
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

class MotionPanel : public OverlayBase {
  public:
    MotionPanel();
    ~MotionPanel() override;

    // === OverlayBase interface ===
    void init_subjects() override;
    void deinit_subjects();
    void register_callbacks() override;
    lv_obj_t* create(lv_obj_t* parent) override;
    const char* get_name() const override {
        return "Motion Panel";
    }

    // === Lifecycle hooks ===
    void on_activate() override;
    void on_deactivate() override;

    // === Public API ===
    lv_obj_t* get_panel() const {
        return overlay_root_;
    }
    void set_position(float x, float y, float z);
    jog_distance_t get_distance() const {
        return current_distance_;
    }
    void jog(jog_direction_t direction, float distance_mm);
    void home(char axis);
    void handle_z_button(const char* name);

  private:
    // RAII subject manager - auto-deinits all registered subjects on destruction
    SubjectManager subjects_;

    lv_subject_t pos_x_subject_;
    lv_subject_t pos_y_subject_;
    lv_subject_t pos_z_subject_;
    lv_subject_t z_axis_label_subject_; // "Bed" or "Print Head"
    lv_subject_t z_up_icon_subject_;    // "arrow_expand_up" or "arrow_up"
    lv_subject_t z_down_icon_subject_;  // "arrow_expand_down" or "arrow_down"
    char pos_x_buf_[32];
    char pos_y_buf_[32];
    char pos_z_buf_[32];
    char z_axis_label_buf_[16];
    char z_up_icon_buf_[24];
    char z_down_icon_buf_[24];
    bool bed_moves_ = false; // If true, invert Z direction (arrows match bed movement)

    jog_distance_t current_distance_ = JOG_DIST_1MM;
    float current_x_ = 0.0f;
    float current_y_ = 0.0f;
    float current_z_ = 0.0f; // Gcode (commanded) Z position

    // For Z display: track both commanded and actual positions
    int gcode_z_centimm_ = 0;
    int actual_z_centimm_ = 0;

    lv_obj_t* jog_pad_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;
    bool callbacks_registered_ = false;

    // Homing state subjects (0=unhomed, 1=homed) for declarative XML bind_style
    lv_subject_t motion_x_homed_;
    lv_subject_t motion_y_homed_;
    lv_subject_t motion_z_homed_;

    ObserverGuard position_x_observer_;
    ObserverGuard position_y_observer_;
    ObserverGuard gcode_z_observer_;
    ObserverGuard actual_z_observer_;
    ObserverGuard bed_moves_observer_;
    ObserverGuard homed_axes_observer_;

    void setup_jog_pad();
    void register_position_observers();

    static void jog_pad_jog_cb(jog_direction_t direction, float distance_mm, void* user_data);
    static void jog_pad_home_cb(void* user_data);
    // Position observers use lambda-based observer factory (no static callbacks needed)

    void update_z_axis_label(bool bed_moves);
    void update_z_display(); // Updates Z label with actual in brackets when different
};

MotionPanel& get_global_motion_panel();
