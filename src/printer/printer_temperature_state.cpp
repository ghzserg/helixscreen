// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file printer_temperature_state.cpp
 * @brief Temperature state management extracted from PrinterState
 *
 * Manages extruder and bed temperature subjects with centidegree precision.
 * Extracted from PrinterState as part of god class decomposition.
 */

#include "printer_temperature_state.h"

#include "state/subject_macros.h"
#include "unit_conversions.h"

#include <spdlog/spdlog.h>

namespace helix {

void PrinterTemperatureState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        spdlog::debug("[PrinterTemperatureState] Subjects already initialized, skipping");
        return;
    }

    spdlog::trace("[PrinterTemperatureState] Initializing subjects (register_xml={})",
                  register_xml);

    // Temperature subjects (integer, centidegrees for 0.1C resolution)
    INIT_SUBJECT_INT(extruder_temp, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(extruder_target, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(bed_temp, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(bed_target, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(chamber_temp, 0, subjects_, register_xml);

    subjects_initialized_ = true;
    spdlog::trace("[PrinterTemperatureState] Subjects initialized successfully");
}

void PrinterTemperatureState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[PrinterTemperatureState] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void PrinterTemperatureState::register_xml_subjects() {
    if (!subjects_initialized_) {
        spdlog::warn("[PrinterTemperatureState] Cannot register XML subjects - not initialized");
        return;
    }

    spdlog::debug("[PrinterTemperatureState] Re-registering subjects with XML system");
    lv_xml_register_subject(nullptr, "extruder_temp", &extruder_temp_);
    lv_xml_register_subject(nullptr, "extruder_target", &extruder_target_);
    lv_xml_register_subject(nullptr, "bed_temp", &bed_temp_);
    lv_xml_register_subject(nullptr, "bed_target", &bed_target_);
    lv_xml_register_subject(nullptr, "chamber_temp", &chamber_temp_);
}

void PrinterTemperatureState::update_from_status(const nlohmann::json& status) {
    // Update extruder temperature (stored as centidegrees for 0.1C resolution)
    if (status.contains("extruder")) {
        const auto& extruder = status["extruder"];

        if (extruder.contains("temperature") && extruder["temperature"].is_number()) {
            int temp_centi = helix::units::json_to_centidegrees(extruder, "temperature");
            lv_subject_set_int(&extruder_temp_, temp_centi);
            lv_subject_notify(&extruder_temp_); // Force notify for graph updates even if unchanged
        }

        if (extruder.contains("target") && extruder["target"].is_number()) {
            int target_centi = helix::units::json_to_centidegrees(extruder, "target");
            lv_subject_set_int(&extruder_target_, target_centi);
        }
    }

    // Update bed temperature (stored as centidegrees for 0.1C resolution)
    if (status.contains("heater_bed")) {
        const auto& bed = status["heater_bed"];

        if (bed.contains("temperature") && bed["temperature"].is_number()) {
            int temp_centi = helix::units::json_to_centidegrees(bed, "temperature");
            lv_subject_set_int(&bed_temp_, temp_centi);
            lv_subject_notify(&bed_temp_); // Force notify for graph updates even if unchanged
            spdlog::trace("[PrinterTemperatureState] Bed temp: {}.{}C", temp_centi / 10,
                          temp_centi % 10);
        }

        if (bed.contains("target") && bed["target"].is_number()) {
            int target_centi = helix::units::json_to_centidegrees(bed, "target");
            lv_subject_set_int(&bed_target_, target_centi);
            spdlog::trace("[PrinterTemperatureState] Bed target: {}.{}C", target_centi / 10,
                          target_centi % 10);
        }
    }

    // Update chamber temperature (if configured)
    if (!chamber_sensor_name_.empty() && status.contains(chamber_sensor_name_)) {
        const auto& chamber = status[chamber_sensor_name_];

        if (chamber.contains("temperature") && chamber["temperature"].is_number()) {
            int temp_centi = helix::units::json_to_centidegrees(chamber, "temperature");
            lv_subject_set_int(&chamber_temp_, temp_centi);
            spdlog::trace("[PrinterTemperatureState] Chamber temp: {}.{}C", temp_centi / 10,
                          temp_centi % 10);
        }
    }
}

} // namespace helix
