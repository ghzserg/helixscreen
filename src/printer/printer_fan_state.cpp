// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file printer_fan_state.cpp
 * @brief Fan state management extracted from PrinterState
 *
 * Manages fan subjects including main part-cooling fan speed, multi-fan
 * tracking with per-fan subjects, and fan metadata for UI display.
 * Extracted from PrinterState as part of god class decomposition.
 */

#include "printer_fan_state.h"

#include "config.h"
#include "device_display_name.h"
#include "state/subject_macros.h"
#include "unit_conversions.h"

#include <spdlog/spdlog.h>

namespace helix {

FanRoleConfig FanRoleConfig::from_config(Config* config) {
    FanRoleConfig roles;
    if (!config) {
        return roles;
    }
    roles.part_fan = config->get<std::string>(config->df() + "fans/part", "fan");
    roles.hotend_fan = config->get<std::string>(config->df() + "fans/hotend", "");
    roles.chamber_fan = config->get<std::string>(config->df() + "fans/chamber", "");
    roles.exhaust_fan = config->get<std::string>(config->df() + "fans/exhaust", "");
    return roles;
}

void PrinterFanState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        spdlog::debug("[PrinterFanState] Subjects already initialized, skipping");
        return;
    }

    spdlog::trace("[PrinterFanState] Initializing subjects (register_xml={})", register_xml);

    // Fan subjects
    INIT_SUBJECT_INT(fan_speed, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(fans_version, 0, subjects_, register_xml);

    subjects_initialized_ = true;
    spdlog::trace("[PrinterFanState] Subjects initialized successfully");
}

void PrinterFanState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[PrinterFanState] Deinitializing subjects");

    // Deinit per-fan speed subjects (unique_ptr handles memory, we just need to deinit)
    for (auto& [name, subject_ptr] : fan_speed_subjects_) {
        if (subject_ptr) {
            lv_subject_deinit(subject_ptr.get());
        }
    }
    fan_speed_subjects_.clear();

    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void PrinterFanState::update_from_status(const nlohmann::json& status) {
    // Update main part-cooling fan speed
    if (status.contains("fan")) {
        const auto& fan = status["fan"];
        spdlog::trace("[PrinterFanState] Received fan status update: {}", fan.dump());

        if (fan.contains("speed") && fan["speed"].is_number()) {
            int speed_pct = units::json_to_percent(fan, "speed");
            spdlog::trace("[PrinterFanState] Fan speed update: {}%", speed_pct);
            lv_subject_set_int(&fan_speed_, speed_pct);

            // Also update multi-fan tracking
            double speed = fan["speed"].get<double>();
            update_fan_speed("fan", speed);
        }
    }

    // Check for other fan types in the status update
    // Moonraker sends fan objects as top-level keys: "heater_fan hotend_fan", "fan_generic xyz"
    for (const auto& [key, value] : status.items()) {
        // Skip non-fan objects
        if (key.rfind("heater_fan ", 0) == 0 || key.rfind("fan_generic ", 0) == 0 ||
            key.rfind("controller_fan ", 0) == 0) {
            if (value.is_object() && value.contains("speed") && value["speed"].is_number()) {
                double speed = value["speed"].get<double>();
                update_fan_speed(key, speed);

                // If this is the configured part fan, also update the main fan_speed_ subject
                // so the hero slider tracks the actual part fan speed
                if (!roles_.part_fan.empty() && key == roles_.part_fan) {
                    int speed_pct = units::json_to_percent(value, "speed");
                    lv_subject_set_int(&fan_speed_, speed_pct);
                }
            }
        }
    }
}

FanType PrinterFanState::classify_fan_type(const std::string& object_name) const {
    if (object_name == "fan") {
        return FanType::PART_COOLING;
    }
    // Check if this fan is the wizard-configured part cooling fan
    if (!roles_.part_fan.empty() && object_name == roles_.part_fan) {
        return FanType::PART_COOLING;
    }
    if (object_name.rfind("heater_fan ", 0) == 0) {
        return FanType::HEATER_FAN;
    } else if (object_name.rfind("controller_fan ", 0) == 0) {
        return FanType::CONTROLLER_FAN;
    } else {
        return FanType::GENERIC_FAN;
    }
}

std::string PrinterFanState::get_role_display_name(const std::string& object_name) const {
    auto it = role_display_names_.find(object_name);
    if (it != role_display_names_.end()) {
        return it->second;
    }
    return {};
}

bool PrinterFanState::is_fan_controllable(FanType type) {
    return type == FanType::PART_COOLING || type == FanType::GENERIC_FAN;
}

void PrinterFanState::init_fans(const std::vector<std::string>& fan_objects,
                                const FanRoleConfig& roles) {
    // Deinit existing per-fan subjects before clearing (unique_ptr handles memory)
    for (auto& [name, subject_ptr] : fan_speed_subjects_) {
        if (subject_ptr) {
            lv_subject_deinit(subject_ptr.get());
        }
    }
    fan_speed_subjects_.clear();

    // Store configured fan roles for classification and naming
    roles_ = roles;
    role_display_names_.clear();

    // Build role-based display name overrides for configured fans.
    // Configured fans use their role name; unconfigured fans use auto-generated names.
    if (!roles_.part_fan.empty() && roles_.part_fan != "fan") {
        role_display_names_[roles_.part_fan] = "Part Fan";
    }
    if (!roles_.hotend_fan.empty()) {
        role_display_names_[roles_.hotend_fan] = "Hotend Fan";
    }
    if (!roles_.chamber_fan.empty()) {
        role_display_names_[roles_.chamber_fan] = "Chamber Fan";
    }
    if (!roles_.exhaust_fan.empty()) {
        role_display_names_[roles_.exhaust_fan] = "Exhaust Fan";
    }

    spdlog::trace("[PrinterFanState] Fan role config: part='{}' hotend='{}' chamber='{}' "
                  "exhaust='{}' ({} display overrides)",
                  roles_.part_fan, roles_.hotend_fan, roles_.chamber_fan, roles_.exhaust_fan,
                  role_display_names_.size());

    fans_.clear();
    fans_.reserve(fan_objects.size());

    // Reserve map capacity to prevent rehashing during insertion
    // (unique_ptr makes this less critical, but still good practice)
    fan_speed_subjects_.reserve(fan_objects.size());

    for (const auto& obj_name : fan_objects) {
        FanInfo info;
        info.object_name = obj_name;

        // Use role-based display name if configured, otherwise auto-generate
        std::string role_name = get_role_display_name(obj_name);
        info.display_name =
            role_name.empty() ? get_display_name(obj_name, DeviceType::FAN) : role_name;

        info.type = classify_fan_type(obj_name);
        info.is_controllable = is_fan_controllable(info.type);
        info.speed_percent = 0;

        spdlog::trace("[PrinterFanState] Registered fan: {} -> \"{}\" (type={}, controllable={})",
                      obj_name, info.display_name, static_cast<int>(info.type),
                      info.is_controllable);
        fans_.push_back(std::move(info));

        // Create per-fan speed subject for reactive UI updates (heap-allocated to survive rehash)
        auto subject_ptr = std::make_unique<lv_subject_t>();
        lv_subject_init_int(subject_ptr.get(), 0);
        fan_speed_subjects_.emplace(obj_name, std::move(subject_ptr));
        spdlog::trace("[PrinterFanState] Created speed subject for fan: {}", obj_name);
    }

    // Initialize and bump version to notify UI
    lv_subject_set_int(&fans_version_, lv_subject_get_int(&fans_version_) + 1);
    spdlog::debug("[PrinterFanState] Initialized {} fans with {} speed subjects (version {})",
                  fans_.size(), fan_speed_subjects_.size(), lv_subject_get_int(&fans_version_));
}

void PrinterFanState::update_fan_speed(const std::string& object_name, double speed) {
    int speed_pct = units::to_percent(speed);

    for (auto& fan : fans_) {
        if (fan.object_name == object_name) {
            if (fan.speed_percent != speed_pct) {
                fan.speed_percent = speed_pct;

                // Fire per-fan subject for reactive UI updates
                auto it = fan_speed_subjects_.find(object_name);
                if (it != fan_speed_subjects_.end() && it->second) {
                    lv_subject_set_int(it->second.get(), speed_pct);
                    spdlog::trace("[PrinterFanState] Fan {} speed updated to {}%", object_name,
                                  speed_pct);
                }
            }
            return;
        }
    }
    // Fan not in list - this is normal during initial status before discovery
}

lv_subject_t* PrinterFanState::get_fan_speed_subject(const std::string& object_name) {
    auto it = fan_speed_subjects_.find(object_name);
    if (it != fan_speed_subjects_.end() && it->second) {
        return it->second.get();
    }
    return nullptr;
}

} // namespace helix
