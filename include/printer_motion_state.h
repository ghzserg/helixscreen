// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "subject_managed_panel.h"

#include <lvgl.h>

#include "hv/json.hpp"

namespace helix {

/**
 * @brief Manages motion-related subjects for printer state
 *
 * Extracted from PrinterState as part of god class decomposition.
 *
 * Position storage (all in centimillimeters, use from_centimm() for mm):
 * - position_x/y/z: toolhead.position - actual physical position (includes mesh compensation)
 * - gcode_position_x/y/z: gcode_move.position - commanded position (what user requested)
 *
 * Z-offset stored as microns.
 */
class PrinterMotionState {
  public:
    PrinterMotionState() = default;
    ~PrinterMotionState() = default;

    // Non-copyable
    PrinterMotionState(const PrinterMotionState&) = delete;
    PrinterMotionState& operator=(const PrinterMotionState&) = delete;

    /**
     * @brief Initialize motion subjects
     * @param register_xml If true, register subjects with LVGL XML system
     */
    void init_subjects(bool register_xml = true);

    /**
     * @brief Deinitialize subjects (called by SubjectManager automatically)
     */
    void deinit_subjects();

    /**
     * @brief Update motion state from Moonraker status JSON
     * @param status JSON object containing "toolhead" and/or "gcode_move" keys
     */
    void update_from_status(const nlohmann::json& status);

    // Toolhead position accessors - actual physical position (centimillimeters)
    lv_subject_t* get_position_x_subject() {
        return &position_x_;
    }
    lv_subject_t* get_position_y_subject() {
        return &position_y_;
    }
    lv_subject_t* get_position_z_subject() {
        return &position_z_;
    }

    // Gcode position accessors - commanded position (centimillimeters)
    lv_subject_t* get_gcode_position_x_subject() {
        return &gcode_position_x_;
    }
    lv_subject_t* get_gcode_position_y_subject() {
        return &gcode_position_y_;
    }
    lv_subject_t* get_gcode_position_z_subject() {
        return &gcode_position_z_;
    }

    lv_subject_t* get_homed_axes_subject() {
        return &homed_axes_;
    }

    // Speed/flow factor accessors (percentage, 100 = 100%)
    lv_subject_t* get_speed_factor_subject() {
        return &speed_factor_;
    }
    lv_subject_t* get_flow_factor_subject() {
        return &flow_factor_;
    }

    // Z-offset accessors (microns)
    lv_subject_t* get_gcode_z_offset_subject() {
        return &gcode_z_offset_;
    }
    lv_subject_t* get_pending_z_offset_delta_subject() {
        return &pending_z_offset_delta_;
    }

    // Pending Z-offset methods
    void add_pending_z_offset_delta(int delta_microns);
    int get_pending_z_offset_delta() const;
    bool has_pending_z_offset_adjustment() const;
    void clear_pending_z_offset_delta();

  private:
    friend class PrinterMotionStateTestAccess;

    SubjectManager subjects_;
    bool subjects_initialized_ = false;

    // Toolhead position subjects (actual physical position)
    lv_subject_t position_x_{};
    lv_subject_t position_y_{};
    lv_subject_t position_z_{};

    // Gcode position subjects (commanded position)
    lv_subject_t gcode_position_x_{};
    lv_subject_t gcode_position_y_{};
    lv_subject_t gcode_position_z_{};

    lv_subject_t homed_axes_{};
    char homed_axes_buf_[8]{};

    // Speed/flow subjects
    lv_subject_t speed_factor_{};
    lv_subject_t flow_factor_{};

    // Z-offset subjects
    lv_subject_t gcode_z_offset_{};
    lv_subject_t pending_z_offset_delta_{};
};

} // namespace helix
