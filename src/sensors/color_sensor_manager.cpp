// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "color_sensor_manager.h"

#include "ui_update_queue.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <cstring>
#include <regex>

// CRITICAL: Subject updates trigger lv_obj_invalidate() which asserts if called
// during LVGL rendering. WebSocket callbacks run on libhv's event loop thread,
// not the main LVGL thread. We must defer subject updates to the main thread
// via ui_async_call to avoid the "Invalidate area not allowed during rendering"
// assertion.

namespace {

/// @brief Async callback to update subjects on the main LVGL thread
void async_update_color_subjects_callback(void* /*user_data*/) {
    helix::sensors::ColorSensorManager::instance().update_subjects_on_main_thread();
}

} // namespace

namespace helix::sensors {

// ============================================================================
// Singleton
// ============================================================================

ColorSensorManager& ColorSensorManager::instance() {
    static ColorSensorManager instance;
    return instance;
}

ColorSensorManager::ColorSensorManager() {
    color_hex_buf_.fill('\0');
}

ColorSensorManager::~ColorSensorManager() = default;

// ============================================================================
// ISensorManager Interface
// ============================================================================

std::string ColorSensorManager::category_name() const {
    return "color";
}

void ColorSensorManager::discover_from_moonraker(const nlohmann::json& moonraker_info) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Extract device IDs from Moonraker TD-1 API response
    // Expected format from /machine/td1/data:
    // {
    //   "result": {
    //     "status": "ok",
    //     "devices": {
    //       "E6625877D318C430": { "td": null, "color": null, "scan_time": null }
    //     }
    //   }
    // }
    // OR direct devices object if already unwrapped:
    // { "E6625877D318C430": { "td": null, "color": null, "scan_time": null } }
    std::vector<std::string> device_ids;

    // Handle nested result.devices format
    const nlohmann::json* devices_obj = nullptr;
    if (moonraker_info.contains("result") && moonraker_info["result"].contains("devices") &&
        moonraker_info["result"]["devices"].is_object()) {
        devices_obj = &moonraker_info["result"]["devices"];
    } else if (moonraker_info.contains("devices") && moonraker_info["devices"].is_object()) {
        // Already at result level
        devices_obj = &moonraker_info["devices"];
    } else if (moonraker_info.is_object() && !moonraker_info.empty()) {
        // Check if this IS the devices object (keys are device IDs)
        bool looks_like_devices = true;
        for (auto it = moonraker_info.begin(); it != moonraker_info.end(); ++it) {
            // Device entries should have td/color/scan_time fields
            if (!it.value().is_object() ||
                (!it.value().contains("td") && !it.value().contains("color"))) {
                looks_like_devices = false;
                break;
            }
        }
        if (looks_like_devices) {
            devices_obj = &moonraker_info;
        }
    }

    if (devices_obj) {
        for (auto it = devices_obj->begin(); it != devices_obj->end(); ++it) {
            device_ids.push_back(it.key()); // Device ID is the key (e.g., "E6625877D318C430")
        }
    }

    spdlog::debug("[ColorSensorManager] Discovering color sensors from {} device IDs",
                  device_ids.size());

    // Clear existing sensors
    sensors_.clear();

    for (const auto& device_id : device_ids) {
        std::string sensor_name = generate_display_name(device_id);

        ColorSensorConfig config(device_id, sensor_name);
        sensors_.push_back(config);

        // Initialize state if not already present
        if (states_.find(device_id) == states_.end()) {
            ColorSensorState state;
            state.available = true;
            states_[device_id] = state;
        } else {
            states_[device_id].available = true;
        }

        spdlog::debug("[ColorSensorManager] Discovered sensor: {} ({})", device_id, sensor_name);
    }

    // Mark sensors that disappeared as unavailable
    for (auto& [id, state] : states_) {
        bool found = false;
        for (const auto& sensor : sensors_) {
            if (sensor.device_id == id) {
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

    spdlog::info("[ColorSensorManager] Discovered {} color sensors", sensors_.size());

    // Update subjects to reflect new state
    update_subjects();
}

void ColorSensorManager::update_from_status(const nlohmann::json& status) {
    bool any_changed = false;

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        for (const auto& sensor : sensors_) {
            const std::string& key = sensor.device_id;

            if (!status.contains(key)) {
                continue;
            }

            const auto& sensor_data = status[key];
            auto& state = states_[sensor.device_id];
            ColorSensorState old_state = state;

            // Update color hex
            if (sensor_data.contains("color")) {
                state.color_hex = sensor_data["color"].get<std::string>();
            }

            // Update transmission distance
            if (sensor_data.contains("td")) {
                state.transmission_distance = sensor_data["td"].get<float>();
            }

            // Check for state change
            if (state.color_hex != old_state.color_hex ||
                state.transmission_distance != old_state.transmission_distance) {
                any_changed = true;
                spdlog::debug("[ColorSensorManager] Sensor {} updated: color={}, td={:.2f}",
                              sensor.device_id, state.color_hex, state.transmission_distance);
            }
        }

        if (any_changed) {
            if (sync_mode_) {
                spdlog::debug("[ColorSensorManager] sync_mode: updating subjects synchronously");
                update_subjects();
            } else {
                spdlog::debug("[ColorSensorManager] async_mode: deferring via ui_async_call");
                ui_async_call(async_update_color_subjects_callback, nullptr);
            }
        }
    }
}

void ColorSensorManager::inject_mock_sensors(std::vector<std::string>& /*objects*/,
                                             nlohmann::json& /*config_keys*/,
                                             nlohmann::json& moonraker_info) {
    // Color sensors (TD-1) are discovered from Moonraker info
    moonraker_info["components"]["td1_sensor"] = nlohmann::json::array({"default"});
    spdlog::debug("[ColorSensorManager] Injected mock sensors: td1_sensor default");
}

void ColorSensorManager::inject_mock_status(nlohmann::json& status) {
    // TD-1 sensor reports color and transmission distance
    status["td1_sensor default"] = {{"color", {255, 200, 150}}, {"detected", true}};
}

void ColorSensorManager::load_config(const nlohmann::json& config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[ColorSensorManager] Loading config");

    if (!config.contains("sensors") || !config["sensors"].is_array()) {
        spdlog::debug("[ColorSensorManager] No sensors config found");
        return;
    }

    for (const auto& sensor_json : config["sensors"]) {
        if (!sensor_json.contains("device_id")) {
            continue;
        }

        std::string device_id = sensor_json["device_id"].get<std::string>();
        auto* sensor = find_config(device_id);

        if (sensor) {
            if (sensor_json.contains("role")) {
                sensor->role = color_role_from_string(sensor_json["role"].get<std::string>());
            }
            if (sensor_json.contains("enabled")) {
                sensor->enabled = sensor_json["enabled"].get<bool>();
            }
            spdlog::debug("[ColorSensorManager] Loaded config for {}: role={}, enabled={}",
                          device_id, color_role_to_string(sensor->role), sensor->enabled);
        }
    }

    update_subjects();
    spdlog::info("[ColorSensorManager] Config loaded");
}

nlohmann::json ColorSensorManager::save_config() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[ColorSensorManager] Saving config");

    nlohmann::json config;
    nlohmann::json sensors_array = nlohmann::json::array();

    for (const auto& sensor : sensors_) {
        nlohmann::json sensor_json;
        sensor_json["device_id"] = sensor.device_id;
        sensor_json["role"] = color_role_to_string(sensor.role);
        sensor_json["enabled"] = sensor.enabled;
        sensors_array.push_back(sensor_json);
    }

    config["sensors"] = sensors_array;

    spdlog::info("[ColorSensorManager] Config saved");
    return config;
}

// ============================================================================
// Initialization
// ============================================================================

void ColorSensorManager::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    spdlog::trace("[ColorSensorManager] Initializing subjects");

    // Initialize subjects with SubjectManager for automatic cleanup
    // Empty string = no sensor assigned
    UI_MANAGED_SUBJECT_STRING(color_hex_, color_hex_buf_.data(), "", "filament_color_hex",
                              subjects_);
    // -1 = no sensor assigned, 0+ = TD value * 100
    UI_MANAGED_SUBJECT_INT(td_value_, -1, "filament_td_value", subjects_);
    UI_MANAGED_SUBJECT_INT(sensor_count_, 0, "color_sensor_count", subjects_);

    subjects_initialized_ = true;
    spdlog::trace("[ColorSensorManager] Subjects initialized");
}

void ColorSensorManager::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::trace("[ColorSensorManager] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::trace("[ColorSensorManager] Subjects deinitialized");
}

// ============================================================================
// Sensor Queries
// ============================================================================

bool ColorSensorManager::has_sensors() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return !sensors_.empty();
}

std::vector<ColorSensorConfig> ColorSensorManager::get_sensors() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_; // Return thread-safe copy
}

size_t ColorSensorManager::sensor_count() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_.size();
}

// ============================================================================
// Configuration
// ============================================================================

void ColorSensorManager::set_sensor_role(const std::string& device_id, ColorSensorRole role) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // If assigning a role, clear it from any other sensor first
    if (role != ColorSensorRole::NONE) {
        for (auto& sensor : sensors_) {
            if (sensor.role == role && sensor.device_id != device_id) {
                spdlog::debug("[ColorSensorManager] Clearing role {} from {}",
                              color_role_to_string(role), sensor.sensor_name);
                sensor.role = ColorSensorRole::NONE;
            }
        }
    }

    auto* sensor = find_config(device_id);
    if (sensor) {
        sensor->role = role;
        spdlog::info("[ColorSensorManager] Set role for {} to {}", sensor->sensor_name,
                     color_role_to_string(role));
        update_subjects();
    }
}

void ColorSensorManager::set_sensor_enabled(const std::string& device_id, bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto* sensor = find_config(device_id);
    if (sensor) {
        sensor->enabled = enabled;
        spdlog::info("[ColorSensorManager] Set enabled for {} to {}", sensor->sensor_name, enabled);
        update_subjects();
    }
}

// ============================================================================
// State Queries
// ============================================================================

std::optional<ColorSensorState> ColorSensorManager::get_sensor_state(ColorSensorRole role) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (role == ColorSensorRole::NONE) {
        return std::nullopt;
    }

    const auto* config = find_config_by_role(role);
    if (!config) {
        return std::nullopt;
    }

    auto it = states_.find(config->device_id);
    if (it == states_.end()) {
        return std::nullopt;
    }

    return it->second; // Return thread-safe copy
}

bool ColorSensorManager::is_sensor_available(ColorSensorRole role) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (role == ColorSensorRole::NONE) {
        return false;
    }

    const auto* config = find_config_by_role(role);
    if (!config || !config->enabled) {
        return false;
    }

    auto it = states_.find(config->device_id);
    return it != states_.end() && it->second.available;
}

std::string ColorSensorManager::get_filament_color_hex() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    const auto* config = find_config_by_role(ColorSensorRole::FILAMENT_COLOR);
    if (!config || !config->enabled) {
        return "";
    }

    auto it = states_.find(config->device_id);
    if (it == states_.end() || !it->second.available) {
        return "";
    }

    return it->second.color_hex;
}

// ============================================================================
// LVGL Subjects
// ============================================================================

lv_subject_t* ColorSensorManager::get_color_hex_subject() {
    return &color_hex_;
}

lv_subject_t* ColorSensorManager::get_td_value_subject() {
    return &td_value_;
}

lv_subject_t* ColorSensorManager::get_sensor_count_subject() {
    return &sensor_count_;
}

// ============================================================================
// Testing Support
// ============================================================================

void ColorSensorManager::set_sync_mode(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sync_mode_ = enabled;
}

void ColorSensorManager::update_subjects_on_main_thread() {
    update_subjects();
}

// ============================================================================
// Private Helpers
// ============================================================================

std::string ColorSensorManager::generate_display_name(const std::string& device_id) const {
    // Convert "td1_lane0" -> "TD-1 Lane 0"
    std::regex pattern(R"(td1_lane(\d+))");
    std::smatch match;

    if (std::regex_match(device_id, match, pattern)) {
        return "TD-1 Lane " + match[1].str();
    }

    // Fallback: just return the device_id
    return device_id;
}

ColorSensorConfig* ColorSensorManager::find_config(const std::string& device_id) {
    for (auto& sensor : sensors_) {
        if (sensor.device_id == device_id) {
            return &sensor;
        }
    }
    return nullptr;
}

const ColorSensorConfig* ColorSensorManager::find_config(const std::string& device_id) const {
    for (const auto& sensor : sensors_) {
        if (sensor.device_id == device_id) {
            return &sensor;
        }
    }
    return nullptr;
}

const ColorSensorConfig* ColorSensorManager::find_config_by_role(ColorSensorRole role) const {
    for (const auto& sensor : sensors_) {
        if (sensor.role == role) {
            return &sensor;
        }
    }
    return nullptr;
}

void ColorSensorManager::update_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    // Get color hex value for filament color role
    auto get_color_hex_value = [this]() -> std::string {
        const auto* config = find_config_by_role(ColorSensorRole::FILAMENT_COLOR);
        if (!config || !config->enabled) {
            return ""; // No sensor assigned or disabled
        }

        auto it = states_.find(config->device_id);
        if (it == states_.end() || !it->second.available) {
            return ""; // Sensor unavailable
        }

        return it->second.color_hex;
    };

    // Get TD value for filament color role
    auto get_td_value = [this]() -> int {
        const auto* config = find_config_by_role(ColorSensorRole::FILAMENT_COLOR);
        if (!config || !config->enabled) {
            return -1; // No sensor assigned or disabled
        }

        auto it = states_.find(config->device_id);
        if (it == states_.end() || !it->second.available) {
            return -1; // Sensor unavailable
        }

        // Convert TD to int (TD * 100)
        return static_cast<int>(it->second.transmission_distance * 100.0f);
    };

    // Update color hex subject
    std::string color_hex = get_color_hex_value();
    std::strncpy(color_hex_buf_.data(), color_hex.c_str(), color_hex_buf_.size() - 1);
    color_hex_buf_[color_hex_buf_.size() - 1] = '\0';
    lv_subject_copy_string(&color_hex_, color_hex_buf_.data());

    lv_subject_set_int(&td_value_, get_td_value());

    spdlog::trace("[ColorSensorManager] Subjects updated: color_hex={}, td_value={}",
                  lv_subject_get_string(&color_hex_), lv_subject_get_int(&td_value_));
}

} // namespace helix::sensors
