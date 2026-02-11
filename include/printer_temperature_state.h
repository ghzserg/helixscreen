// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "subject_managed_panel.h"

#include <lvgl.h>

#include "hv/json.hpp"

namespace helix {

/**
 * @brief Manages temperature-related subjects for printer state
 *
 * Extracted from PrinterState as part of god class decomposition.
 * All temperatures stored in centidegrees (value * 10 for 0.1C precision).
 */
class PrinterTemperatureState {
  public:
    PrinterTemperatureState() = default;
    ~PrinterTemperatureState() = default;

    // Non-copyable
    PrinterTemperatureState(const PrinterTemperatureState&) = delete;
    PrinterTemperatureState& operator=(const PrinterTemperatureState&) = delete;

    /**
     * @brief Initialize temperature subjects
     * @param register_xml If true, register subjects with LVGL XML system
     */
    void init_subjects(bool register_xml = true);

    /**
     * @brief Deinitialize subjects (called by SubjectManager automatically)
     */
    void deinit_subjects();

    /**
     * @brief Update temperatures from Moonraker status JSON
     * @param status JSON object containing "extruder" and/or "heater_bed" keys
     */
    void update_from_status(const nlohmann::json& status);

    /**
     * @brief Re-register subjects with LVGL XML system
     *
     * Call this to ensure subjects are registered in LVGL's global XML registry.
     * Use when other code may have overwritten the registry (e.g., other tests
     * calling init_subjects(true) on their own PrinterState instances).
     *
     * Does NOT reinitialize subjects - only updates LVGL XML registry mappings.
     * Safe to call multiple times.
     */
    void register_xml_subjects();

    // Subject accessors (centidegrees: value * 10)
    lv_subject_t* get_extruder_temp_subject() {
        return &extruder_temp_;
    }
    lv_subject_t* get_extruder_target_subject() {
        return &extruder_target_;
    }
    lv_subject_t* get_bed_temp_subject() {
        return &bed_temp_;
    }
    lv_subject_t* get_bed_target_subject() {
        return &bed_target_;
    }
    lv_subject_t* get_chamber_temp_subject() {
        return &chamber_temp_;
    }

    /**
     * @brief Set the sensor name used to read chamber temperature
     * @param name Klipper sensor name (e.g., "temperature_sensor chamber")
     */
    void set_chamber_sensor_name(const std::string& name) {
        chamber_sensor_name_ = name;
    }

  private:
    friend class PrinterTemperatureStateTestAccess;

    SubjectManager subjects_;
    bool subjects_initialized_ = false;

    // Temperature subjects (centidegrees: 205.3C stored as 2053)
    lv_subject_t extruder_temp_{};
    lv_subject_t extruder_target_{};
    lv_subject_t bed_temp_{};
    lv_subject_t bed_target_{};
    lv_subject_t chamber_temp_{};

    // Chamber sensor configuration
    std::string chamber_sensor_name_;
};

} // namespace helix
