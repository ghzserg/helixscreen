// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "probe_sensor_manager.h"

#include "ui_update_queue.h"

#include "spdlog/spdlog.h"

#include <algorithm>

// CRITICAL: Subject updates trigger lv_obj_invalidate() which asserts if called
// during LVGL rendering. WebSocket callbacks run on libhv's event loop thread,
// not the main LVGL thread. We must defer subject updates to the main thread
// via ui_async_call to avoid the "Invalidate area not allowed during rendering"
// assertion.

namespace {

/// @brief Async callback to update subjects on the main LVGL thread
void async_update_probe_subjects_callback(void* /*user_data*/) {
    helix::sensors::ProbeSensorManager::instance().update_subjects_on_main_thread();
}

} // namespace

namespace helix::sensors {

// ============================================================================
// Singleton
// ============================================================================

ProbeSensorManager& ProbeSensorManager::instance() {
    static ProbeSensorManager instance;
    return instance;
}

ProbeSensorManager::ProbeSensorManager() = default;

ProbeSensorManager::~ProbeSensorManager() = default;

// ============================================================================
// ISensorManager Interface
// ============================================================================

std::string ProbeSensorManager::category_name() const {
    return "probe";
}

void ProbeSensorManager::discover(const std::vector<std::string>& klipper_objects) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[ProbeSensorManager] Discovering probe sensors from {} objects",
                  klipper_objects.size());

    // Clear existing sensors
    sensors_.clear();

    for (const auto& klipper_name : klipper_objects) {
        std::string sensor_name;
        ProbeSensorType type = ProbeSensorType::STANDARD;

        if (!parse_klipper_name(klipper_name, sensor_name, type)) {
            continue;
        }

        ProbeSensorConfig config(klipper_name, sensor_name, type);
        sensors_.push_back(config);

        // Initialize state if not already present
        if (states_.find(klipper_name) == states_.end()) {
            ProbeSensorState state;
            state.available = true;
            states_[klipper_name] = state;
        } else {
            states_[klipper_name].available = true;
        }

        spdlog::debug("[ProbeSensorManager] Discovered sensor: {} (type: {})", sensor_name,
                      probe_type_to_string(type));
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

    // Update sensor count subject
    if (subjects_initialized_) {
        lv_subject_set_int(&sensor_count_, static_cast<int>(sensors_.size()));
    }

    spdlog::info("[ProbeSensorManager] Discovered {} probe sensors", sensors_.size());

    // Update subjects to reflect new state
    update_subjects();
}

void ProbeSensorManager::update_from_status(const nlohmann::json& status) {
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
            ProbeSensorState old_state = state;

            // Update last_z_result
            if (sensor_data.contains("last_z_result")) {
                state.last_z_result = sensor_data["last_z_result"].get<float>();
            }

            // Update z_offset
            if (sensor_data.contains("z_offset")) {
                state.z_offset = sensor_data["z_offset"].get<float>();
            }

            // Check for state change
            if (state.last_z_result != old_state.last_z_result ||
                state.z_offset != old_state.z_offset) {
                any_changed = true;
                spdlog::debug("[ProbeSensorManager] Sensor {} updated: last_z_result={:.3f}mm, "
                              "z_offset={:.3f}mm",
                              sensor.sensor_name, state.last_z_result, state.z_offset);
            }
        }

        if (any_changed) {
            if (sync_mode_) {
                spdlog::debug("[ProbeSensorManager] sync_mode: updating subjects synchronously");
                update_subjects();
            } else {
                spdlog::debug("[ProbeSensorManager] async_mode: deferring via ui_async_call");
                ui_async_call(async_update_probe_subjects_callback, nullptr);
            }
        }
    }
}

void ProbeSensorManager::inject_mock_sensors(std::vector<std::string>& objects,
                                             nlohmann::json& /*config_keys*/,
                                             nlohmann::json& /*moonraker_info*/) {
    // Probe sensors are discovered from Klipper objects
    objects.emplace_back("probe");
    spdlog::debug("[ProbeSensorManager] Injected mock sensors: probe");
}

void ProbeSensorManager::inject_mock_status(nlohmann::json& status) {
    // Probe reports last_z_result (and optionally z_offset)
    status["probe"] = {{"last_z_result", 0.0f}};
}

void ProbeSensorManager::load_config(const nlohmann::json& config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[ProbeSensorManager] Loading config");

    if (!config.contains("sensors") || !config["sensors"].is_array()) {
        spdlog::debug("[ProbeSensorManager] No sensors config found");
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
                sensor->role = probe_role_from_string(sensor_json["role"].get<std::string>());
            }
            if (sensor_json.contains("enabled")) {
                sensor->enabled = sensor_json["enabled"].get<bool>();
            }
            spdlog::debug("[ProbeSensorManager] Loaded config for {}: role={}, enabled={}",
                          klipper_name, probe_role_to_string(sensor->role), sensor->enabled);
        }
    }

    update_subjects();
    spdlog::info("[ProbeSensorManager] Config loaded");
}

nlohmann::json ProbeSensorManager::save_config() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[ProbeSensorManager] Saving config");

    nlohmann::json config;
    nlohmann::json sensors_array = nlohmann::json::array();

    for (const auto& sensor : sensors_) {
        nlohmann::json sensor_json;
        sensor_json["klipper_name"] = sensor.klipper_name;
        sensor_json["role"] = probe_role_to_string(sensor.role);
        sensor_json["enabled"] = sensor.enabled;
        sensor_json["type"] = probe_type_to_string(sensor.type);
        sensors_array.push_back(sensor_json);
    }

    config["sensors"] = sensors_array;

    spdlog::info("[ProbeSensorManager] Config saved");
    return config;
}

// ============================================================================
// Initialization
// ============================================================================

void ProbeSensorManager::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    spdlog::trace("[ProbeSensorManager] Initializing subjects");

    // Initialize subjects with SubjectManager for automatic cleanup
    // -1 = no sensor assigned
    UI_MANAGED_SUBJECT_INT(probe_triggered_, -1, "probe_triggered", subjects_);
    UI_MANAGED_SUBJECT_INT(probe_last_z_, -1, "probe_last_z", subjects_);
    UI_MANAGED_SUBJECT_INT(probe_z_offset_, -1, "probe_z_offset", subjects_);
    UI_MANAGED_SUBJECT_INT(sensor_count_, 0, "probe_count", subjects_);

    subjects_initialized_ = true;
    spdlog::trace("[ProbeSensorManager] Subjects initialized");
}

void ProbeSensorManager::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::trace("[ProbeSensorManager] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::trace("[ProbeSensorManager] Subjects deinitialized");
}

// ============================================================================
// Sensor Queries
// ============================================================================

bool ProbeSensorManager::has_sensors() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return !sensors_.empty();
}

std::vector<ProbeSensorConfig> ProbeSensorManager::get_sensors() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_; // Return thread-safe copy
}

size_t ProbeSensorManager::sensor_count() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_.size();
}

// ============================================================================
// Configuration
// ============================================================================

void ProbeSensorManager::set_sensor_role(const std::string& klipper_name, ProbeSensorRole role) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // If assigning a role, clear it from any other sensor first
    if (role != ProbeSensorRole::NONE) {
        for (auto& sensor : sensors_) {
            if (sensor.role == role && sensor.klipper_name != klipper_name) {
                spdlog::debug("[ProbeSensorManager] Clearing role {} from {}",
                              probe_role_to_string(role), sensor.sensor_name);
                sensor.role = ProbeSensorRole::NONE;
            }
        }
    }

    auto* sensor = find_config(klipper_name);
    if (sensor) {
        sensor->role = role;
        spdlog::info("[ProbeSensorManager] Set role for {} to {}", sensor->sensor_name,
                     probe_role_to_string(role));
        update_subjects();
    }
}

void ProbeSensorManager::set_sensor_enabled(const std::string& klipper_name, bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto* sensor = find_config(klipper_name);
    if (sensor) {
        sensor->enabled = enabled;
        spdlog::info("[ProbeSensorManager] Set enabled for {} to {}", sensor->sensor_name, enabled);
        update_subjects();
    }
}

// ============================================================================
// State Queries
// ============================================================================

std::optional<ProbeSensorState> ProbeSensorManager::get_sensor_state(ProbeSensorRole role) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (role == ProbeSensorRole::NONE) {
        return std::nullopt;
    }

    const auto* config = find_config_by_role(role);
    if (!config) {
        return std::nullopt;
    }

    auto it = states_.find(config->klipper_name);
    if (it == states_.end()) {
        return std::nullopt;
    }

    return it->second; // Return thread-safe copy
}

bool ProbeSensorManager::is_sensor_available(ProbeSensorRole role) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (role == ProbeSensorRole::NONE) {
        return false;
    }

    const auto* config = find_config_by_role(role);
    if (!config || !config->enabled) {
        return false;
    }

    auto it = states_.find(config->klipper_name);
    return it != states_.end() && it->second.available;
}

float ProbeSensorManager::get_last_z_result() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    const auto* config = find_config_by_role(ProbeSensorRole::Z_PROBE);
    if (!config || !config->enabled) {
        return 0.0f;
    }

    auto it = states_.find(config->klipper_name);
    if (it == states_.end() || !it->second.available) {
        return 0.0f;
    }

    return it->second.last_z_result;
}

float ProbeSensorManager::get_z_offset() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    const auto* config = find_config_by_role(ProbeSensorRole::Z_PROBE);
    if (!config || !config->enabled) {
        return 0.0f;
    }

    auto it = states_.find(config->klipper_name);
    if (it == states_.end() || !it->second.available) {
        return 0.0f;
    }

    return it->second.z_offset;
}

// ============================================================================
// LVGL Subjects
// ============================================================================

lv_subject_t* ProbeSensorManager::get_probe_triggered_subject() {
    return &probe_triggered_;
}

lv_subject_t* ProbeSensorManager::get_probe_last_z_subject() {
    return &probe_last_z_;
}

lv_subject_t* ProbeSensorManager::get_probe_z_offset_subject() {
    return &probe_z_offset_;
}

lv_subject_t* ProbeSensorManager::get_sensor_count_subject() {
    return &sensor_count_;
}

// ============================================================================
// Testing Support
// ============================================================================

void ProbeSensorManager::set_sync_mode(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sync_mode_ = enabled;
}

void ProbeSensorManager::update_subjects_on_main_thread() {
    update_subjects();
}

// ============================================================================
// Private Helpers
// ============================================================================

bool ProbeSensorManager::parse_klipper_name(const std::string& klipper_name,
                                            std::string& sensor_name, ProbeSensorType& type) const {
    // Standard probe
    if (klipper_name == "probe") {
        sensor_name = "probe";
        type = ProbeSensorType::STANDARD;
        return true;
    }

    // BLTouch
    if (klipper_name == "bltouch") {
        sensor_name = "bltouch";
        type = ProbeSensorType::BLTOUCH;
        return true;
    }

    // Smart Effector
    if (klipper_name == "smart_effector") {
        sensor_name = "smart_effector";
        type = ProbeSensorType::SMART_EFFECTOR;
        return true;
    }

    // Eddy current probe: "probe_eddy_current <name>"
    const std::string eddy_prefix = "probe_eddy_current ";
    if (klipper_name.rfind(eddy_prefix, 0) == 0 && klipper_name.size() > eddy_prefix.size()) {
        sensor_name = klipper_name.substr(eddy_prefix.size());
        type = ProbeSensorType::EDDY_CURRENT;
        return true;
    }

    return false;
}

ProbeSensorConfig* ProbeSensorManager::find_config(const std::string& klipper_name) {
    for (auto& sensor : sensors_) {
        if (sensor.klipper_name == klipper_name) {
            return &sensor;
        }
    }
    return nullptr;
}

const ProbeSensorConfig* ProbeSensorManager::find_config(const std::string& klipper_name) const {
    for (const auto& sensor : sensors_) {
        if (sensor.klipper_name == klipper_name) {
            return &sensor;
        }
    }
    return nullptr;
}

const ProbeSensorConfig* ProbeSensorManager::find_config_by_role(ProbeSensorRole role) const {
    for (const auto& sensor : sensors_) {
        if (sensor.role == role) {
            return &sensor;
        }
    }
    return nullptr;
}

void ProbeSensorManager::update_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    // Helper to check if Z_PROBE role is available
    auto get_z_probe_config = [this]() -> const ProbeSensorConfig* {
        const auto* config = find_config_by_role(ProbeSensorRole::Z_PROBE);
        if (!config || !config->enabled) {
            return nullptr;
        }
        return config;
    };

    // Get probe triggered value (currently always 0 since triggered comes from query)
    auto get_triggered_value = [this, &get_z_probe_config]() -> int {
        const auto* config = get_z_probe_config();
        if (!config) {
            return -1; // No sensor assigned or disabled
        }

        auto it = states_.find(config->klipper_name);
        if (it == states_.end() || !it->second.available) {
            return -1; // Sensor unavailable
        }

        // For now, triggered state is not in regular status updates
        // Return 0 (not triggered) as default when sensor is available
        return it->second.triggered ? 1 : 0;
    };

    // Get last Z result value
    auto get_last_z_value = [this, &get_z_probe_config]() -> int {
        const auto* config = get_z_probe_config();
        if (!config) {
            return -1; // No sensor assigned or disabled
        }

        auto it = states_.find(config->klipper_name);
        if (it == states_.end() || !it->second.available) {
            return -1; // Sensor unavailable
        }

        // Convert to microns (mm * 1000)
        return static_cast<int>(it->second.last_z_result * 1000.0f);
    };

    // Get Z offset value
    auto get_z_offset_value = [this, &get_z_probe_config]() -> int {
        const auto* config = get_z_probe_config();
        if (!config) {
            return -1; // No sensor assigned or disabled
        }

        auto it = states_.find(config->klipper_name);
        if (it == states_.end() || !it->second.available) {
            return -1; // Sensor unavailable
        }

        // Convert to microns (mm * 1000)
        return static_cast<int>(it->second.z_offset * 1000.0f);
    };

    lv_subject_set_int(&probe_triggered_, get_triggered_value());
    lv_subject_set_int(&probe_last_z_, get_last_z_value());
    lv_subject_set_int(&probe_z_offset_, get_z_offset_value());

    spdlog::trace("[ProbeSensorManager] Subjects updated: triggered={}, last_z={}, z_offset={}",
                  lv_subject_get_int(&probe_triggered_), lv_subject_get_int(&probe_last_z_),
                  lv_subject_get_int(&probe_z_offset_));
}

} // namespace helix::sensors
