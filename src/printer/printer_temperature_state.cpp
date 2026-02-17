// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file printer_temperature_state.cpp
 * @brief Temperature state management extracted from PrinterState
 *
 * Manages extruder and bed temperature subjects with centidegree precision.
 * Supports multiple extruders via dynamic ExtruderInfo map. The "active extruder"
 * subjects track whichever extruder is currently selected, defaulting to "extruder".
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

    // Active extruder subjects (track whichever extruder is currently active)
    // XML names stay as "extruder_temp"/"extruder_target" for XML binding compatibility
    lv_subject_init_int(&active_extruder_temp_, 0);
    subjects_.register_subject(&active_extruder_temp_);
    if (register_xml) {
        lv_xml_register_subject(nullptr, "extruder_temp", &active_extruder_temp_);
    }

    lv_subject_init_int(&active_extruder_target_, 0);
    subjects_.register_subject(&active_extruder_target_);
    if (register_xml) {
        lv_xml_register_subject(nullptr, "extruder_target", &active_extruder_target_);
    }

    // Bed and chamber temperature subjects
    INIT_SUBJECT_INT(bed_temp, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(bed_target, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(chamber_temp, 0, subjects_, register_xml);

    // Extruder version subject (bumped when extruder list changes)
    INIT_SUBJECT_INT(extruder_version, 0, subjects_, register_xml);

    subjects_initialized_ = true;
    spdlog::trace("[PrinterTemperatureState] Subjects initialized successfully");
}

void PrinterTemperatureState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[PrinterTemperatureState] Deinitializing subjects");

    // Deinit dynamic per-extruder subjects before clearing map
    for (auto& [name, info] : extruders_) {
        if (info.temp_subject) {
            lv_subject_deinit(info.temp_subject.get());
        }
        if (info.target_subject) {
            lv_subject_deinit(info.target_subject.get());
        }
    }
    extruders_.clear();

    // Reset active extruder to default
    active_extruder_name_ = "extruder";

    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void PrinterTemperatureState::register_xml_subjects() {
    if (!subjects_initialized_) {
        spdlog::warn("[PrinterTemperatureState] Cannot register XML subjects - not initialized");
        return;
    }

    spdlog::debug("[PrinterTemperatureState] Re-registering subjects with XML system");
    lv_xml_register_subject(nullptr, "extruder_temp", &active_extruder_temp_);
    lv_xml_register_subject(nullptr, "extruder_target", &active_extruder_target_);
    lv_xml_register_subject(nullptr, "bed_temp", &bed_temp_);
    lv_xml_register_subject(nullptr, "bed_target", &bed_target_);
    lv_xml_register_subject(nullptr, "chamber_temp", &chamber_temp_);
    lv_xml_register_subject(nullptr, "extruder_version", &extruder_version_);
}

void PrinterTemperatureState::init_extruders(const std::vector<std::string>& heaters) {
    // Deinit existing per-extruder subjects before clearing
    for (auto& [name, info] : extruders_) {
        if (info.temp_subject) {
            lv_subject_deinit(info.temp_subject.get());
        }
        if (info.target_subject) {
            lv_subject_deinit(info.target_subject.get());
        }
    }
    extruders_.clear();

    // Filter for extruder* names and count them
    std::vector<std::string> extruder_names;
    for (const auto& name : heaters) {
        // Accept "extruder" and "extruderN" (digit suffix), reject "extruder_stepper" etc.
        if (name == "extruder" ||
            (name.size() > 8 && name.rfind("extruder", 0) == 0 && std::isdigit(name[8]))) {
            extruder_names.push_back(name);
        }
    }

    bool multi = extruder_names.size() > 1;

    extruders_.reserve(extruder_names.size());
    for (size_t i = 0; i < extruder_names.size(); ++i) {
        const auto& name = extruder_names[i];
        ExtruderInfo info;
        info.name = name;

        // Single extruder: "Nozzle". Multiple: "Nozzle 1", "Nozzle 2", ...
        if (multi) {
            info.display_name = "Nozzle " + std::to_string(i + 1);
        } else {
            info.display_name = "Nozzle";
        }

        // Create heap-allocated subjects (stable across rehash)
        info.temp_subject = std::make_unique<lv_subject_t>();
        lv_subject_init_int(info.temp_subject.get(), 0);

        info.target_subject = std::make_unique<lv_subject_t>();
        lv_subject_init_int(info.target_subject.get(), 0);

        spdlog::trace("[PrinterTemperatureState] Registered extruder: {} -> \"{}\"", name,
                      info.display_name);
        extruders_.emplace(name, std::move(info));
    }

    // Bump version to notify UI of extruder list change
    lv_subject_set_int(&extruder_version_, lv_subject_get_int(&extruder_version_) + 1);
    spdlog::debug("[PrinterTemperatureState] Initialized {} extruders (version {})",
                  extruders_.size(), lv_subject_get_int(&extruder_version_));
}

lv_subject_t* PrinterTemperatureState::get_extruder_temp_subject(const std::string& name) {
    auto it = extruders_.find(name);
    if (it != extruders_.end() && it->second.temp_subject) {
        return it->second.temp_subject.get();
    }
    return nullptr;
}

lv_subject_t* PrinterTemperatureState::get_extruder_target_subject(const std::string& name) {
    auto it = extruders_.find(name);
    if (it != extruders_.end() && it->second.target_subject) {
        return it->second.target_subject.get();
    }
    return nullptr;
}

void PrinterTemperatureState::set_active_extruder(const std::string& name) {
    // Verify the extruder exists in our map
    auto it = extruders_.find(name);
    if (it == extruders_.end()) {
        spdlog::warn("[PrinterTemperatureState] Unknown extruder '{}', keeping '{}'", name,
                     active_extruder_name_);
        return;
    }

    if (name == active_extruder_name_) {
        return; // No change needed
    }

    spdlog::info("[PrinterTemperatureState] Active extruder: {} -> {}", active_extruder_name_,
                 name);
    active_extruder_name_ = name;

    // Sync current values from per-extruder subjects to active subjects
    const auto& info = it->second;
    if (info.temp_subject) {
        lv_subject_set_int(&active_extruder_temp_, lv_subject_get_int(info.temp_subject.get()));
        lv_subject_notify(&active_extruder_temp_);
    }
    if (info.target_subject) {
        lv_subject_set_int(&active_extruder_target_, lv_subject_get_int(info.target_subject.get()));
    }
}

const std::string& PrinterTemperatureState::active_extruder_name() const {
    return active_extruder_name_;
}

void PrinterTemperatureState::update_from_status(const nlohmann::json& status) {
    // Update dynamic per-extruder subjects
    for (auto& [name, info] : extruders_) {
        if (!status.contains(name))
            continue;
        const auto& data = status[name];

        if (data.contains("temperature") && data["temperature"].is_number()) {
            int temp_centi = helix::units::json_to_centidegrees(data, "temperature");
            info.temperature = data["temperature"].get<float>();
            lv_subject_set_int(info.temp_subject.get(), temp_centi);
            // Force notify for graph updates even when value unchanged
            lv_subject_notify(info.temp_subject.get());
        }

        if (data.contains("target") && data["target"].is_number()) {
            int target_centi = helix::units::json_to_centidegrees(data, "target");
            info.target = data["target"].get<float>();
            lv_subject_set_int(info.target_subject.get(), target_centi);
        }
    }

    // Update active extruder subjects from the currently active extruder's data
    if (status.contains(active_extruder_name_)) {
        const auto& active = status[active_extruder_name_];

        if (active.contains("temperature") && active["temperature"].is_number()) {
            int temp_centi = helix::units::json_to_centidegrees(active, "temperature");
            lv_subject_set_int(&active_extruder_temp_, temp_centi);
            lv_subject_notify(&active_extruder_temp_);
        }

        if (active.contains("target") && active["target"].is_number()) {
            int target_centi = helix::units::json_to_centidegrees(active, "target");
            lv_subject_set_int(&active_extruder_target_, target_centi);
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
