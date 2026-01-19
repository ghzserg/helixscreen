// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file printer_motion_state.cpp
 * @brief Motion state management extracted from PrinterState
 *
 * Manages position, speed/flow factors, and Z-offset subjects.
 * Extracted from PrinterState as part of god class decomposition.
 */

#include "printer_motion_state.h"

#include "unit_conversions.h"

#include <spdlog/spdlog.h>

namespace helix {

void PrinterMotionState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        spdlog::debug("[PrinterMotionState] Subjects already initialized, skipping");
        return;
    }

    spdlog::debug("[PrinterMotionState] Initializing subjects (register_xml={})", register_xml);

    // Toolhead position subjects (actual physical position)
    lv_subject_init_int(&position_x_, 0);
    lv_subject_init_int(&position_y_, 0);
    lv_subject_init_int(&position_z_, 0);

    // Gcode position subjects (commanded position)
    lv_subject_init_int(&gcode_position_x_, 0);
    lv_subject_init_int(&gcode_position_y_, 0);
    lv_subject_init_int(&gcode_position_z_, 0);

    lv_subject_init_string(&homed_axes_, homed_axes_buf_, nullptr, sizeof(homed_axes_buf_), "");

    // Speed/Flow subjects (percentages)
    lv_subject_init_int(&speed_factor_, 100);
    lv_subject_init_int(&flow_factor_, 100);
    lv_subject_init_int(&gcode_z_offset_, 0);         // Z-offset in microns from homing_origin[2]
    lv_subject_init_int(&pending_z_offset_delta_, 0); // Accumulated adjustment during print

    // Register with SubjectManager for automatic cleanup
    subjects_.register_subject(&position_x_);
    subjects_.register_subject(&position_y_);
    subjects_.register_subject(&position_z_);
    subjects_.register_subject(&gcode_position_x_);
    subjects_.register_subject(&gcode_position_y_);
    subjects_.register_subject(&gcode_position_z_);
    subjects_.register_subject(&homed_axes_);
    subjects_.register_subject(&speed_factor_);
    subjects_.register_subject(&flow_factor_);
    subjects_.register_subject(&gcode_z_offset_);
    subjects_.register_subject(&pending_z_offset_delta_);

    // Register with LVGL XML system for XML bindings
    if (register_xml) {
        spdlog::debug("[PrinterMotionState] Registering subjects with XML system");
        lv_xml_register_subject(NULL, "position_x", &position_x_);
        lv_xml_register_subject(NULL, "position_y", &position_y_);
        lv_xml_register_subject(NULL, "position_z", &position_z_);
        lv_xml_register_subject(NULL, "gcode_position_x", &gcode_position_x_);
        lv_xml_register_subject(NULL, "gcode_position_y", &gcode_position_y_);
        lv_xml_register_subject(NULL, "gcode_position_z", &gcode_position_z_);
        lv_xml_register_subject(NULL, "homed_axes", &homed_axes_);
        lv_xml_register_subject(NULL, "speed_factor", &speed_factor_);
        lv_xml_register_subject(NULL, "flow_factor", &flow_factor_);
        lv_xml_register_subject(NULL, "gcode_z_offset", &gcode_z_offset_);
        lv_xml_register_subject(NULL, "pending_z_offset_delta", &pending_z_offset_delta_);
    } else {
        spdlog::debug("[PrinterMotionState] Skipping XML registration (tests mode)");
    }

    subjects_initialized_ = true;
    spdlog::debug("[PrinterMotionState] Subjects initialized successfully");
}

void PrinterMotionState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[PrinterMotionState] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void PrinterMotionState::update_from_status(const nlohmann::json& status) {
    // Update toolhead position
    if (status.contains("toolhead")) {
        const auto& toolhead = status["toolhead"];

        if (toolhead.contains("position") && toolhead["position"].is_array()) {
            const auto& pos = toolhead["position"];
            // Note: Klipper can send null position values before homing or during errors
            // Store positions as centimillimeters (×100) for 0.01mm precision
            if (pos.size() >= 3 && pos[0].is_number() && pos[1].is_number() && pos[2].is_number()) {
                lv_subject_set_int(&position_x_, helix::units::to_centimm(pos[0].get<double>()));
                lv_subject_set_int(&position_y_, helix::units::to_centimm(pos[1].get<double>()));
                lv_subject_set_int(&position_z_, helix::units::to_centimm(pos[2].get<double>()));
            }
        }

        if (toolhead.contains("homed_axes") && toolhead["homed_axes"].is_string()) {
            std::string axes = toolhead["homed_axes"].get<std::string>();
            lv_subject_copy_string(&homed_axes_, axes.c_str());
            // Note: Derived homing subjects (xy_homed, z_homed, all_homed) are now
            // panel-local in ControlsPanel, which observes this homed_axes string.
        }
    }

    // Update gcode_move data (commanded position, speed/flow factors, z-offset)
    if (status.contains("gcode_move")) {
        const auto& gcode_move = status["gcode_move"];

        // Parse commanded position from gcode_move.position
        if (gcode_move.contains("position") && gcode_move["position"].is_array()) {
            const auto& pos = gcode_move["position"];
            if (pos.size() >= 3 && pos[0].is_number() && pos[1].is_number() && pos[2].is_number()) {
                lv_subject_set_int(&gcode_position_x_,
                                   helix::units::to_centimm(pos[0].get<double>()));
                lv_subject_set_int(&gcode_position_y_,
                                   helix::units::to_centimm(pos[1].get<double>()));
                lv_subject_set_int(&gcode_position_z_,
                                   helix::units::to_centimm(pos[2].get<double>()));
            }
        }

        if (gcode_move.contains("speed_factor") && gcode_move["speed_factor"].is_number()) {
            int factor_pct = helix::units::json_to_percent(gcode_move, "speed_factor");
            lv_subject_set_int(&speed_factor_, factor_pct);
        }

        if (gcode_move.contains("extrude_factor") && gcode_move["extrude_factor"].is_number()) {
            int factor_pct = helix::units::json_to_percent(gcode_move, "extrude_factor");
            lv_subject_set_int(&flow_factor_, factor_pct);
        }

        // Parse Z-offset from homing_origin[2] (baby stepping / SET_GCODE_OFFSET Z=)
        if (gcode_move.contains("homing_origin") && gcode_move["homing_origin"].is_array()) {
            const auto& origin = gcode_move["homing_origin"];
            if (origin.size() >= 3 && origin[2].is_number()) {
                int z_microns = static_cast<int>(origin[2].get<double>() * 1000.0);
                lv_subject_set_int(&gcode_z_offset_, z_microns);
                spdlog::trace("[PrinterMotionState] G-code Z-offset: {}um", z_microns);
            }
        }
    }
}

void PrinterMotionState::reset_for_testing() {
    if (!subjects_initialized_) {
        spdlog::debug(
            "[PrinterMotionState] reset_for_testing: subjects not initialized, nothing to reset");
        return;
    }

    spdlog::info(
        "[PrinterMotionState] reset_for_testing: Deinitializing subjects to clear observers");

    // Use SubjectManager for automatic subject cleanup
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

// ============================================================================
// PENDING Z-OFFSET DELTA TRACKING
// ============================================================================

void PrinterMotionState::add_pending_z_offset_delta(int delta_microns) {
    int current = lv_subject_get_int(&pending_z_offset_delta_);
    int new_value = current + delta_microns;
    lv_subject_set_int(&pending_z_offset_delta_, new_value);
    spdlog::debug("[PrinterMotionState] Pending Z-offset delta: {:+}um (total: {:+}um)",
                  delta_microns, new_value);
}

int PrinterMotionState::get_pending_z_offset_delta() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&pending_z_offset_delta_));
}

bool PrinterMotionState::has_pending_z_offset_adjustment() const {
    return get_pending_z_offset_delta() != 0;
}

void PrinterMotionState::clear_pending_z_offset_delta() {
    if (has_pending_z_offset_adjustment()) {
        spdlog::info("[PrinterMotionState] Clearing pending Z-offset delta");
        lv_subject_set_int(&pending_z_offset_delta_, 0);
    }
}

} // namespace helix
