// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "subject_managed_panel.h"

#include <lvgl.h>

#include "hv/json.hpp"

namespace helix {

/**
 * @brief Manages calibration and configuration subjects for printer state
 *
 * Tracks firmware retraction settings, manual probe state, and motor enabled state.
 * These subjects are used by calibration panels and Z-offset workflows.
 *
 * Extracted from PrinterState as part of god class decomposition.
 *
 * ## Firmware Retraction (4 subjects)
 * - retract_length_: Length in centimillimeters (0.8mm stored as 80)
 * - retract_speed_: Speed in mm/s (integer)
 * - unretract_extra_length_: Extra length in centimillimeters
 * - unretract_speed_: Speed in mm/s (integer)
 *
 * ## Manual Probe (2 subjects)
 * - manual_probe_active_: 0=inactive, 1=active (during PROBE_CALIBRATE)
 * - manual_probe_z_position_: Z position in microns (0.125mm stored as 125)
 *
 * ## Motor State (1 subject)
 * - motors_enabled_: 0=disabled (Idle), 1=enabled (Ready/Printing)
 *
 * @note Thread safety: update_from_status() should be called from the main thread
 *       (typically via helix::async::invoke in PrinterState)
 */
class PrinterCalibrationState {
  public:
    PrinterCalibrationState() = default;
    ~PrinterCalibrationState() = default;

    // Non-copyable
    PrinterCalibrationState(const PrinterCalibrationState&) = delete;
    PrinterCalibrationState& operator=(const PrinterCalibrationState&) = delete;

    /**
     * @brief Initialize calibration subjects
     * @param register_xml If true, register subjects with LVGL XML system
     */
    void init_subjects(bool register_xml = true);

    /**
     * @brief Deinitialize subjects (called by SubjectManager automatically)
     */
    void deinit_subjects();

    /**
     * @brief Update calibration state from Moonraker status JSON
     *
     * Parses firmware_retraction, manual_probe, and idle_timeout sections.
     *
     * @param status JSON status object from Moonraker
     */
    void update_from_status(const nlohmann::json& status);

    // ========================================================================
    // Subject Accessors
    // ========================================================================

    /**
     * @brief Get manual probe active subject
     * @return Integer subject: 0=inactive, 1=active
     */
    lv_subject_t* get_manual_probe_active_subject() {
        return &manual_probe_active_;
    }

    /**
     * @brief Get manual probe Z position subject
     * @return Integer subject: Z position in microns
     */
    lv_subject_t* get_manual_probe_z_position_subject() {
        return &manual_probe_z_position_;
    }

    /**
     * @brief Get motors enabled subject
     * @return Integer subject: 0=disabled, 1=enabled
     */
    lv_subject_t* get_motors_enabled_subject() {
        return &motors_enabled_;
    }

    /**
     * @brief Get retract length subject
     * @return Integer subject: length in centimillimeters
     */
    lv_subject_t* get_retract_length_subject() {
        return &retract_length_;
    }

    /**
     * @brief Get retract speed subject
     * @return Integer subject: speed in mm/s
     */
    lv_subject_t* get_retract_speed_subject() {
        return &retract_speed_;
    }

    /**
     * @brief Get unretract extra length subject
     * @return Integer subject: length in centimillimeters
     */
    lv_subject_t* get_unretract_extra_length_subject() {
        return &unretract_extra_length_;
    }

    /**
     * @brief Get unretract speed subject
     * @return Integer subject: speed in mm/s
     */
    lv_subject_t* get_unretract_speed_subject() {
        return &unretract_speed_;
    }

  private:
    friend class PrinterCalibrationStateTestAccess;

    SubjectManager subjects_;
    bool subjects_initialized_ = false;

    // Firmware retraction settings (from firmware_retraction Klipper module)
    // Lengths stored as centimillimeters (x100) to preserve 0.01mm precision with integers
    lv_subject_t retract_length_{};         // centimm (e.g., 80 = 0.8mm)
    lv_subject_t retract_speed_{};          // mm/s (integer, e.g., 35)
    lv_subject_t unretract_extra_length_{}; // centimm (e.g., 0 = 0.0mm)
    lv_subject_t unretract_speed_{};        // mm/s (integer, e.g., 35)

    // Manual probe subjects (for Z-offset calibration)
    lv_subject_t manual_probe_active_{};     // 0=inactive, 1=active (PROBE_CALIBRATE running)
    lv_subject_t manual_probe_z_position_{}; // Z position * 1000 (for 0.001mm resolution)

    // Motor enabled state (from idle_timeout.state)
    lv_subject_t motors_enabled_{}; // 0=disabled (Idle), 1=enabled (Ready/Printing)
};

} // namespace helix
