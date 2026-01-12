// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file printer_temperature_state.cpp
 * @brief Temperature state management extracted from PrinterState
 *
 * Manages extruder and bed temperature subjects with centidegree precision.
 * Extracted from PrinterState as part of god class decomposition.
 */

#include "printer_temperature_state.h"

#include "unit_conversions.h"

#include <spdlog/spdlog.h>

namespace helix {

void PrinterTemperatureState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        spdlog::debug("[PrinterTemperatureState] Subjects already initialized, skipping");
        return;
    }

    spdlog::debug("[PrinterTemperatureState] Initializing subjects (register_xml={})",
                  register_xml);

    // Temperature subjects (integer, centidegrees for 0.1C resolution)
    lv_subject_init_int(&extruder_temp_, 0);
    lv_subject_init_int(&extruder_target_, 0);
    lv_subject_init_int(&bed_temp_, 0);
    lv_subject_init_int(&bed_target_, 0);

    // Register with SubjectManager for automatic cleanup
    subjects_.register_subject(&extruder_temp_);
    subjects_.register_subject(&extruder_target_);
    subjects_.register_subject(&bed_temp_);
    subjects_.register_subject(&bed_target_);

    // Register with LVGL XML system for XML bindings
    if (register_xml) {
        spdlog::debug("[PrinterTemperatureState] Registering subjects with XML system");
        lv_xml_register_subject(NULL, "extruder_temp", &extruder_temp_);
        lv_xml_register_subject(NULL, "extruder_target", &extruder_target_);
        lv_xml_register_subject(NULL, "bed_temp", &bed_temp_);
        lv_xml_register_subject(NULL, "bed_target", &bed_target_);
    } else {
        spdlog::debug("[PrinterTemperatureState] Skipping XML registration (tests mode)");
    }

    subjects_initialized_ = true;
    spdlog::debug("[PrinterTemperatureState] Subjects initialized successfully");
}

void PrinterTemperatureState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[PrinterTemperatureState] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
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
}

void PrinterTemperatureState::reset_for_testing() {
    if (!subjects_initialized_) {
        spdlog::debug("[PrinterTemperatureState] reset_for_testing: subjects not initialized, "
                      "nothing to reset");
        return;
    }

    spdlog::info(
        "[PrinterTemperatureState] reset_for_testing: Deinitializing subjects to clear observers");

    // Use SubjectManager for automatic subject cleanup
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

} // namespace helix
