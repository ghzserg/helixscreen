// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_panel_base.h"

/**
 * @file ui_panel_motion.h
 * @brief Motion panel - XYZ movement and homing control
 *
 * Provides manual jog controls with:
 * - 3×3 directional jog pad for XY movement
 * - Distance selector (0.1, 1, 10, 100mm)
 * - Z-axis up/down controls
 * - Home buttons (All, X, Y, Z)
 * - Real-time position display via reactive subjects
 *
 * ## Reactive Subjects:
 * - `motion_pos_x` - X position string (e.g., "X:  125.0 mm")
 * - `motion_pos_y` - Y position string
 * - `motion_pos_z` - Z position string
 *
 * ## Key Features:
 * - Creates custom jog_pad widget dynamically (replaces XML placeholder)
 * - Distance button selection with visual feedback
 * - Mock position updates (ready for Moonraker API integration)
 *
 * ## Migration Notes:
 * Final Phase 3 panel - demonstrates subject ownership and complex state.
 * Unlike launcher panels, this has actual reactive data binding.
 *
 * @see PanelBase for base class documentation
 * @see ui_jog_pad for the jog pad widget
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
    /**
     * @brief Construct MotionPanel with injected dependencies
     *
     * @param printer_state Reference to PrinterState
     * @param api Pointer to MoonrakerAPI (for future jog/home commands)
     */
    MotionPanel(PrinterState& printer_state, MoonrakerAPI* api);

    ~MotionPanel() override = default;

    //
    // === PanelBase Implementation ===
    //

    /**
     * @brief Initialize position subjects for XML binding
     *
     * Registers: motion_pos_x, motion_pos_y, motion_pos_z
     */
    void init_subjects() override;

    /**
     * @brief Setup jog pad widget, wire button handlers
     *
     * - Replaces XML placeholder with jog_pad widget
     * - Wires distance selector buttons
     * - Wires Z-axis and home buttons
     *
     * @param panel Root panel object from lv_xml_create()
     * @param parent_screen Parent screen for navigation
     */
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;

    const char* get_name() const override { return "Motion Panel"; }
    const char* get_xml_component_name() const override { return "motion_panel"; }

    //
    // === Public API ===
    //

    /**
     * @brief Update XYZ position display
     *
     * Updates subjects which automatically refresh bound UI labels.
     *
     * @param x X position in mm
     * @param y Y position in mm
     * @param z Z position in mm
     */
    void set_position(float x, float y, float z);

    /**
     * @brief Get currently selected jog distance
     * @return Current jog distance setting
     */
    jog_distance_t get_distance() const { return current_distance_; }

    /**
     * @brief Set jog distance selection
     * @param dist Distance to select
     */
    void set_distance(jog_distance_t dist);

    /**
     * @brief Execute jog command
     *
     * Currently mock implementation - updates position locally.
     * TODO: Send G-code via Moonraker API
     *
     * @param direction Direction to jog
     * @param distance_mm Distance in mm
     */
    void jog(jog_direction_t direction, float distance_mm);

    /**
     * @brief Execute home command
     *
     * Currently mock implementation - resets position to 0.
     * TODO: Send G28 via Moonraker API
     *
     * @param axis 'X', 'Y', 'Z', or 'A' for all axes
     */
    void home(char axis);

  private:
    //
    // === Subjects (owned by this panel) ===
    //

    lv_subject_t pos_x_subject_;
    lv_subject_t pos_y_subject_;
    lv_subject_t pos_z_subject_;

    // Subject storage buffers
    char pos_x_buf_[32];
    char pos_y_buf_[32];
    char pos_z_buf_[32];

    //
    // === Instance State ===
    //

    jog_distance_t current_distance_ = JOG_DIST_1MM;
    float current_x_ = 0.0f;
    float current_y_ = 0.0f;
    float current_z_ = 0.0f;

    // Child widgets
    lv_obj_t* jog_pad_ = nullptr;
    lv_obj_t* dist_buttons_[4] = {nullptr};

    //
    // === Private Helpers ===
    //

    void setup_distance_buttons();
    void setup_jog_pad();
    void setup_z_buttons();
    void setup_home_buttons();
    void update_distance_buttons();

    //
    // === Instance Handlers ===
    //

    void handle_distance_button(lv_obj_t* btn);
    void handle_z_button(const char* name);
    void handle_home_button(const char* name);

    // Jog pad callbacks (bridge to instance)
    static void jog_pad_jog_cb(jog_direction_t direction, float distance_mm, void* user_data);
    static void jog_pad_home_cb(void* user_data);

    //
    // === Static Trampolines ===
    //

    static void on_distance_button_clicked(lv_event_t* e);
    static void on_z_button_clicked(lv_event_t* e);
    static void on_home_button_clicked(lv_event_t* e);
};

// Global instance accessor (needed by main.cpp)
MotionPanel& get_global_motion_panel();
