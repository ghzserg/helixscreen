// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "subject_managed_panel.h"

#include <lvgl.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "hv/json.hpp"

namespace helix {

/**
 * @brief Per-extruder temperature data with reactive subjects
 *
 * Each extruder discovered via init_extruders() gets its own ExtruderInfo
 * with heap-allocated subjects (unique_ptr for pointer stability across rehash).
 */
struct ExtruderInfo {
    std::string name;         ///< Klipper name: "extruder", "extruder1", etc.
    std::string display_name; ///< Human-readable: "Nozzle", "Nozzle 1"
    float temperature = 0.0f; ///< Raw float for internal tracking
    float target = 0.0f;
    std::unique_ptr<lv_subject_t> temp_subject;   ///< Centidegrees (value * 10)
    std::unique_ptr<lv_subject_t> target_subject; ///< Centidegrees
};

/**
 * @brief Manages temperature-related subjects for printer state
 *
 * Extracted from PrinterState as part of god class decomposition.
 * All temperatures stored in centidegrees (value * 10 for 0.1C precision).
 *
 * Supports multiple extruders via a dynamic ExtruderInfo map. The "active extruder"
 * subjects track whichever extruder is currently selected (via set_active_extruder),
 * defaulting to "extruder". XML bindings use names "extruder_temp"/"extruder_target"
 * for the active extruder subjects.
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

    /**
     * @brief Initialize extruder tracking from discovered heater objects
     *
     * Filters the heater list for extruder* names, creates ExtruderInfo entries
     * with heap-allocated subjects, and bumps the version subject to trigger
     * UI rebuilds.
     *
     * Safe to call multiple times (cleans up previous entries first).
     *
     * @param heaters List of Moonraker heater object names
     */
    void init_extruders(const std::vector<std::string>& heaters);

    // Active extruder subjects (centidegrees: value * 10)
    // These track whichever extruder is currently active (set via set_active_extruder)
    lv_subject_t* get_active_extruder_temp_subject() {
        return &active_extruder_temp_;
    }
    lv_subject_t* get_active_extruder_target_subject() {
        return &active_extruder_target_;
    }

    // Per-extruder subject accessors (returns nullptr if not found)
    lv_subject_t* get_extruder_temp_subject(const std::string& name);
    lv_subject_t* get_extruder_target_subject(const std::string& name);

    lv_subject_t* get_bed_temp_subject() {
        return &bed_temp_;
    }
    lv_subject_t* get_bed_target_subject() {
        return &bed_target_;
    }
    lv_subject_t* get_chamber_temp_subject() {
        return &chamber_temp_;
    }

    /// Number of tracked extruders
    int extruder_count() const {
        return static_cast<int>(extruders_.size());
    }

    /// Access to extruder map (for UI enumeration)
    const std::unordered_map<std::string, ExtruderInfo>& extruders() const {
        return extruders_;
    }

    /// Version subject, bumped when extruder list changes (for UI rebuild triggers)
    lv_subject_t* get_extruder_version_subject() {
        return &extruder_version_;
    }

    /**
     * @brief Set the sensor name used to read chamber temperature
     * @param name Klipper sensor name (e.g., "temperature_sensor chamber")
     */
    void set_chamber_sensor_name(const std::string& name) {
        chamber_sensor_name_ = name;
    }

    /**
     * @brief Set which extruder is active (mirrors its data to active subjects)
     *
     * Validates that the name exists in the extruder map. If unknown, logs a
     * warning and keeps the previous active extruder.
     *
     * @param name Klipper extruder name (e.g., "extruder", "extruder1")
     */
    void set_active_extruder(const std::string& name);

    /**
     * @brief Get the name of the currently active extruder
     * @return Klipper name of active extruder (defaults to "extruder")
     */
    const std::string& active_extruder_name() const;

  private:
    friend class PrinterTemperatureStateTestAccess;

    SubjectManager subjects_;
    bool subjects_initialized_ = false;

    // Active extruder subjects (centidegrees: 205.3C stored as 2053)
    // These track whichever extruder is currently active, defaulting to "extruder".
    // Registered in XML as "extruder_temp"/"extruder_target" for binding compatibility.
    lv_subject_t active_extruder_temp_{};
    lv_subject_t active_extruder_target_{};
    lv_subject_t bed_temp_{};
    lv_subject_t bed_target_{};
    lv_subject_t chamber_temp_{};

    // Dynamic per-extruder tracking
    std::unordered_map<std::string, ExtruderInfo> extruders_;
    lv_subject_t extruder_version_{}; ///< Bumped when extruder list changes

    // Active extruder name (defaults to "extruder")
    std::string active_extruder_name_ = "extruder";

    // Chamber sensor configuration
    std::string chamber_sensor_name_;
};

} // namespace helix
