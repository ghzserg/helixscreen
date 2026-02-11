// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temperature_sensor_manager.h"

#include "ui_update_queue.h"

#include "device_display_name.h"
#include "spdlog/spdlog.h"

#include <algorithm>

// CRITICAL: Subject updates trigger lv_obj_invalidate() which asserts if called
// during LVGL rendering. WebSocket callbacks run on libhv's event loop thread,
// not the main LVGL thread. We must defer subject updates to the main thread
// via ui_async_call to avoid the "Invalidate area not allowed during rendering"
// assertion.

namespace {

/// @brief Async callback to update subjects on the main LVGL thread
void async_update_temp_subjects_callback(void* /*user_data*/) {
    helix::sensors::TemperatureSensorManager::instance().update_subjects_on_main_thread();
}

} // namespace

namespace helix::sensors {

// ============================================================================
// Singleton
// ============================================================================

TemperatureSensorManager& TemperatureSensorManager::instance() {
    static TemperatureSensorManager instance;
    return instance;
}

TemperatureSensorManager::TemperatureSensorManager() = default;

TemperatureSensorManager::~TemperatureSensorManager() = default;

// ============================================================================
// ISensorManager Interface
// ============================================================================

std::string TemperatureSensorManager::category_name() const {
    return "temperature";
}

void TemperatureSensorManager::discover(const std::vector<std::string>& klipper_objects) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[TemperatureSensorManager] Discovering temperature sensors from {} objects",
                  klipper_objects.size());

    // Clear existing sensors
    sensors_.clear();

    for (const auto& klipper_name : klipper_objects) {
        std::string sensor_name;
        TemperatureSensorType type = TemperatureSensorType::TEMPERATURE_SENSOR;

        if (!parse_klipper_name(klipper_name, sensor_name, type)) {
            continue;
        }

        // Exclude extruder and heater_bed (managed by PrinterState)
        if (sensor_name == "heater_bed") {
            continue;
        }
        if (sensor_name == "extruder" || sensor_name.find("extruder") == 0) {
            continue;
        }

        // Generate display name
        std::string display_name = helix::get_display_name(sensor_name, DeviceType::TEMP_SENSOR);

        TemperatureSensorConfig config(klipper_name, sensor_name, display_name, type);

        // Auto-categorize based on sensor name
        if (sensor_name.find("chamber") != std::string::npos) {
            config.role = TemperatureSensorRole::CHAMBER;
            config.priority = 0;
        } else if (sensor_name.find("mcu") != std::string::npos) {
            config.role = TemperatureSensorRole::MCU;
            config.priority = 10;
        } else if (sensor_name == "raspberry_pi" || sensor_name == "host_temp" ||
                   sensor_name == "host" || sensor_name == "rpi" ||
                   sensor_name.find("raspberry") != std::string::npos) {
            config.role = TemperatureSensorRole::HOST;
            config.priority = 20;
        } else {
            config.role = TemperatureSensorRole::AUXILIARY;
            config.priority = 100;
        }

        sensors_.push_back(config);

        // Initialize state if not already present
        if (states_.find(klipper_name) == states_.end()) {
            TemperatureSensorState state;
            state.available = true;
            states_[klipper_name] = state;
        } else {
            states_[klipper_name].available = true;
        }

        // Ensure a dynamic subject exists for this sensor
        ensure_sensor_subject(klipper_name);

        spdlog::debug("[TemperatureSensorManager] Discovered sensor: {} (type: {}, role: {}, "
                      "priority: {})",
                      sensor_name, temp_type_to_string(type), temp_role_to_string(config.role),
                      config.priority);
    }

    // Mark sensors that disappeared as unavailable
    for (auto& [name, state] : states_) {
        bool found = false;
        for (const auto& sensor : sensors_) {
            if (sensor.klipper_name == name) {
                found = true;
                break;
            }
        }
        if (!found) {
            state.available = false;
        }
    }

    // Remove stale entries to prevent unbounded memory growth
    for (auto it = states_.begin(); it != states_.end();) {
        if (!it->second.available) {
            it = states_.erase(it);
        } else {
            ++it;
        }
    }

    // Remove stale dynamic subjects
    for (auto it = temp_subjects_.begin(); it != temp_subjects_.end();) {
        bool found = false;
        for (const auto& sensor : sensors_) {
            if (sensor.klipper_name == it->first) {
                found = true;
                break;
            }
        }
        if (!found) {
            it = temp_subjects_.erase(it);
        } else {
            ++it;
        }
    }

    // Update sensor count subject
    if (subjects_initialized_) {
        lv_subject_set_int(&sensor_count_, static_cast<int>(sensors_.size()));
    }

    spdlog::debug("[TemperatureSensorManager] Discovered {} temperature sensors", sensors_.size());

    // Update subjects to reflect new state
    update_subjects();
}

void TemperatureSensorManager::update_from_status(const nlohmann::json& status) {
    bool any_changed = false;

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        for (const auto& sensor : sensors_) {
            const std::string& key = sensor.klipper_name;

            if (!status.contains(key)) {
                continue;
            }

            const auto& sensor_data = status[key];
            auto& state = states_[sensor.klipper_name];
            TemperatureSensorState old_state = state;

            // Update temperature
            if (sensor_data.contains("temperature")) {
                state.temperature = sensor_data["temperature"].get<float>();
            }

            // Update target (temperature_fan only)
            if (sensor_data.contains("target")) {
                state.target = sensor_data["target"].get<float>();
            }

            // Update speed (temperature_fan only)
            if (sensor_data.contains("speed")) {
                state.speed = sensor_data["speed"].get<float>();
            }

            // Check for state change
            if (state.temperature != old_state.temperature || state.target != old_state.target ||
                state.speed != old_state.speed) {
                any_changed = true;
                spdlog::trace("[TemperatureSensorManager] Sensor {} updated: temp={:.1f}C, "
                              "target={:.1f}C, speed={:.2f}",
                              sensor.sensor_name, state.temperature, state.target, state.speed);
            }
        }

        if (any_changed) {
            if (sync_mode_) {
                spdlog::trace(
                    "[TemperatureSensorManager] sync_mode: updating subjects synchronously");
                update_subjects();
            } else {
                spdlog::trace("[TemperatureSensorManager] async_mode: deferring via ui_async_call");
                ui_async_call(async_update_temp_subjects_callback, nullptr);
            }
        }
    }
}

void TemperatureSensorManager::inject_mock_sensors(std::vector<std::string>& objects,
                                                   nlohmann::json& /*config_keys*/,
                                                   nlohmann::json& /*moonraker_info*/) {
    objects.emplace_back("temperature_sensor mcu_temp");
    objects.emplace_back("temperature_sensor raspberry_pi");
    objects.emplace_back("temperature_sensor chamber_temp");
    objects.emplace_back("temperature_fan exhaust_fan");
    spdlog::debug("[TemperatureSensorManager] Injected mock sensors: mcu_temp, raspberry_pi, "
                  "chamber_temp, exhaust_fan");
}

void TemperatureSensorManager::inject_mock_status(nlohmann::json& status) {
    status["temperature_sensor mcu_temp"] = {{"temperature", 45.2f}};
    status["temperature_sensor raspberry_pi"] = {{"temperature", 55.8f}};
    status["temperature_sensor chamber_temp"] = {{"temperature", 35.0f}};
    status["temperature_fan exhaust_fan"] = {
        {"temperature", 38.5f}, {"target", 40.0f}, {"speed", 0.65f}};
}

void TemperatureSensorManager::load_config(const nlohmann::json& config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[TemperatureSensorManager] Loading config");

    if (!config.contains("sensors") || !config["sensors"].is_array()) {
        spdlog::debug("[TemperatureSensorManager] No sensors config found");
        return;
    }

    for (const auto& sensor_json : config["sensors"]) {
        if (!sensor_json.contains("klipper_name")) {
            continue;
        }

        std::string klipper_name = sensor_json["klipper_name"].get<std::string>();
        auto* sensor = find_config(klipper_name);

        if (sensor) {
            if (sensor_json.contains("role")) {
                sensor->role = temp_role_from_string(sensor_json["role"].get<std::string>());
            }
            if (sensor_json.contains("enabled")) {
                sensor->enabled = sensor_json["enabled"].get<bool>();
            }
            spdlog::debug("[TemperatureSensorManager] Loaded config for {}: role={}, enabled={}",
                          klipper_name, temp_role_to_string(sensor->role), sensor->enabled);
        }
    }

    update_subjects();
    spdlog::info("[TemperatureSensorManager] Config loaded");
}

nlohmann::json TemperatureSensorManager::save_config() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[TemperatureSensorManager] Saving config");

    nlohmann::json config;
    nlohmann::json sensors_array = nlohmann::json::array();

    for (const auto& sensor : sensors_) {
        nlohmann::json sensor_json;
        sensor_json["klipper_name"] = sensor.klipper_name;
        sensor_json["role"] = temp_role_to_string(sensor.role);
        sensor_json["enabled"] = sensor.enabled;
        sensor_json["type"] = temp_type_to_string(sensor.type);
        sensors_array.push_back(sensor_json);
    }

    config["sensors"] = sensors_array;

    spdlog::info("[TemperatureSensorManager] Config saved");
    return config;
}

// ============================================================================
// Initialization
// ============================================================================

void TemperatureSensorManager::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    spdlog::trace("[TemperatureSensorManager] Initializing subjects");

    UI_MANAGED_SUBJECT_INT(sensor_count_, 0, "temp_sensor_count", subjects_);

    subjects_initialized_ = true;
    spdlog::trace("[TemperatureSensorManager] Subjects initialized");
}

void TemperatureSensorManager::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::trace("[TemperatureSensorManager] Deinitializing subjects");

    // Clear dynamic subjects (their destructors handle lv_subject_deinit)
    temp_subjects_.clear();

    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::trace("[TemperatureSensorManager] Subjects deinitialized");
}

// ============================================================================
// Sensor Queries
// ============================================================================

bool TemperatureSensorManager::has_sensors() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return !sensors_.empty();
}

std::vector<TemperatureSensorConfig> TemperatureSensorManager::get_sensors() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_; // Return thread-safe copy
}

std::vector<TemperatureSensorConfig> TemperatureSensorManager::get_sensors_sorted() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto sorted = sensors_;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        if (a.priority != b.priority) {
            return a.priority < b.priority;
        }
        return a.display_name < b.display_name;
    });

    return sorted;
}

size_t TemperatureSensorManager::sensor_count() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_.size();
}

// ============================================================================
// Configuration
// ============================================================================

void TemperatureSensorManager::set_sensor_role(const std::string& klipper_name,
                                               TemperatureSensorRole role) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto* sensor = find_config(klipper_name);
    if (sensor) {
        sensor->role = role;
        spdlog::info("[TemperatureSensorManager] Set role for {} to {}", sensor->sensor_name,
                     temp_role_to_string(role));
        update_subjects();
    }
}

void TemperatureSensorManager::set_sensor_enabled(const std::string& klipper_name, bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto* sensor = find_config(klipper_name);
    if (sensor) {
        sensor->enabled = enabled;
        spdlog::info("[TemperatureSensorManager] Set enabled for {} to {}", sensor->sensor_name,
                     enabled);
        update_subjects();
    }
}

// ============================================================================
// State Queries
// ============================================================================

std::optional<TemperatureSensorState>
TemperatureSensorManager::get_sensor_state(const std::string& klipper_name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto it = states_.find(klipper_name);
    if (it == states_.end()) {
        return std::nullopt;
    }

    return it->second; // Return thread-safe copy
}

// ============================================================================
// LVGL Subjects
// ============================================================================

lv_subject_t* TemperatureSensorManager::get_temp_subject(const std::string& klipper_name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto it = temp_subjects_.find(klipper_name);
    if (it == temp_subjects_.end()) {
        return nullptr;
    }

    return &it->second->subject;
}

lv_subject_t* TemperatureSensorManager::get_sensor_count_subject() {
    return &sensor_count_;
}

// ============================================================================
// Testing Support
// ============================================================================

void TemperatureSensorManager::set_sync_mode(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sync_mode_ = enabled;
}

void TemperatureSensorManager::update_subjects_on_main_thread() {
    update_subjects();
}

// ============================================================================
// Private Helpers
// ============================================================================

bool TemperatureSensorManager::parse_klipper_name(const std::string& klipper_name,
                                                  std::string& sensor_name,
                                                  TemperatureSensorType& type) const {
    const std::string temp_sensor_prefix = "temperature_sensor ";
    const std::string temp_fan_prefix = "temperature_fan ";

    if (klipper_name.rfind(temp_sensor_prefix, 0) == 0) {
        sensor_name = klipper_name.substr(temp_sensor_prefix.length());
        type = TemperatureSensorType::TEMPERATURE_SENSOR;
        return true;
    }

    if (klipper_name.rfind(temp_fan_prefix, 0) == 0) {
        sensor_name = klipper_name.substr(temp_fan_prefix.length());
        type = TemperatureSensorType::TEMPERATURE_FAN;
        return true;
    }

    return false;
}

TemperatureSensorConfig* TemperatureSensorManager::find_config(const std::string& klipper_name) {
    for (auto& sensor : sensors_) {
        if (sensor.klipper_name == klipper_name) {
            return &sensor;
        }
    }
    return nullptr;
}

const TemperatureSensorConfig*
TemperatureSensorManager::find_config(const std::string& klipper_name) const {
    for (const auto& sensor : sensors_) {
        if (sensor.klipper_name == klipper_name) {
            return &sensor;
        }
    }
    return nullptr;
}

void TemperatureSensorManager::ensure_sensor_subject(const std::string& klipper_name) {
    if (temp_subjects_.find(klipper_name) != temp_subjects_.end()) {
        return; // Already exists
    }

    auto subj = std::make_unique<DynamicIntSubject>();
    lv_subject_init_int(&subj->subject, 0);
    subj->initialized = true;

    spdlog::debug("[TemperatureSensorManager] Created dynamic subject for {}", klipper_name);
    temp_subjects_[klipper_name] = std::move(subj);
}

void TemperatureSensorManager::update_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    // Update per-sensor dynamic subjects with centidegrees values
    for (const auto& sensor : sensors_) {
        auto subj_it = temp_subjects_.find(sensor.klipper_name);
        if (subj_it == temp_subjects_.end() || !subj_it->second->initialized) {
            continue;
        }

        auto state_it = states_.find(sensor.klipper_name);
        if (state_it == states_.end()) {
            continue;
        }

        // Convert temperature to centidegrees (temp * 100)
        int centidegrees = static_cast<int>(state_it->second.temperature * 100.0f);
        lv_subject_set_int(&subj_it->second->subject, centidegrees);
    }

    spdlog::trace("[TemperatureSensorManager] Subjects updated: {} sensors", sensors_.size());
}

} // namespace helix::sensors
