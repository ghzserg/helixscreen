// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "humidity_sensor_manager.h"

#include "ui_update_queue.h"

#include "format_utils.h"
#include "spdlog/spdlog.h"

#include <algorithm>

// CRITICAL: Subject updates trigger lv_obj_invalidate() which asserts if called
// during LVGL rendering. WebSocket callbacks run on libhv's event loop thread,
// not the main LVGL thread. We must defer subject updates to the main thread
// via ui_async_call to avoid the "Invalidate area not allowed during rendering"
// assertion.

namespace {

/// @brief Async callback to update subjects on the main LVGL thread
void async_update_humidity_subjects_callback(void* /*user_data*/) {
    helix::sensors::HumiditySensorManager::instance().update_subjects_on_main_thread();
}

} // namespace

namespace helix::sensors {

// ============================================================================
// Singleton
// ============================================================================

HumiditySensorManager& HumiditySensorManager::instance() {
    static HumiditySensorManager instance;
    return instance;
}

HumiditySensorManager::HumiditySensorManager() = default;

HumiditySensorManager::~HumiditySensorManager() = default;

// ============================================================================
// ISensorManager Interface
// ============================================================================

std::string HumiditySensorManager::category_name() const {
    return "humidity";
}

void HumiditySensorManager::discover(const std::vector<std::string>& klipper_objects) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[HumiditySensorManager] Discovering humidity sensors from {} objects",
                  klipper_objects.size());

    // Clear existing sensors
    sensors_.clear();

    for (const auto& klipper_name : klipper_objects) {
        std::string sensor_name;
        HumiditySensorType type = HumiditySensorType::BME280;

        if (!parse_klipper_name(klipper_name, sensor_name, type)) {
            continue;
        }

        HumiditySensorConfig config(klipper_name, sensor_name, type);
        sensors_.push_back(config);

        // Initialize state if not already present
        if (states_.find(klipper_name) == states_.end()) {
            HumiditySensorState state;
            state.available = true;
            states_[klipper_name] = state;
        } else {
            states_[klipper_name].available = true;
        }

        spdlog::debug("[HumiditySensorManager] Discovered sensor: {} (type: {})", sensor_name,
                      humidity_type_to_string(type));
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

    spdlog::info("[HumiditySensorManager] Discovered {} humidity sensors", sensors_.size());

    // Update subjects to reflect new state
    update_subjects();
}

void HumiditySensorManager::update_from_status(const nlohmann::json& status) {
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
            HumiditySensorState old_state = state;

            // Update humidity
            if (sensor_data.contains("humidity")) {
                state.humidity = sensor_data["humidity"].get<float>();
            }

            // Update temperature
            if (sensor_data.contains("temperature")) {
                state.temperature = sensor_data["temperature"].get<float>();
            }

            // Update pressure (BME280 only - HTU21D doesn't have pressure)
            if (sensor_data.contains("pressure")) {
                state.pressure = sensor_data["pressure"].get<float>();
            }

            // Check for state change
            if (state.humidity != old_state.humidity ||
                state.temperature != old_state.temperature ||
                state.pressure != old_state.pressure) {
                any_changed = true;
                spdlog::debug(
                    "[HumiditySensorManager] Sensor {} updated: humidity={:.1f}%, temp={:.1f}C, "
                    "pressure={:.1f}hPa",
                    sensor.sensor_name, state.humidity, state.temperature, state.pressure);
            }
        }

        if (any_changed) {
            if (sync_mode_) {
                spdlog::debug("[HumiditySensorManager] sync_mode: updating subjects synchronously");
                update_subjects();
            } else {
                spdlog::debug("[HumiditySensorManager] async_mode: deferring via ui_async_call");
                ui_async_call(async_update_humidity_subjects_callback, nullptr);
            }
        }
    }
}

void HumiditySensorManager::inject_mock_sensors(std::vector<std::string>& objects,
                                                nlohmann::json& /*config_keys*/,
                                                nlohmann::json& /*moonraker_info*/) {
    // Humidity sensors are discovered from Klipper objects
    objects.emplace_back("bme280 chamber");
    objects.emplace_back("htu21d dryer");
    spdlog::debug("[HumiditySensorManager] Injected mock sensors: bme280 chamber, htu21d dryer");
}

void HumiditySensorManager::inject_mock_status(nlohmann::json& status) {
    // BME280 has humidity, temperature, pressure
    status["bme280 chamber"] = {
        {"humidity", 45.0f}, {"temperature", 25.0f}, {"pressure", 1013.25f}};
    // HTU21D has humidity and temperature (no pressure)
    status["htu21d dryer"] = {{"humidity", 15.0f}, {"temperature", 55.0f}};
}

void HumiditySensorManager::load_config(const nlohmann::json& config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[HumiditySensorManager] Loading config");

    if (!config.contains("sensors") || !config["sensors"].is_array()) {
        spdlog::debug("[HumiditySensorManager] No sensors config found");
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
                sensor->role = humidity_role_from_string(sensor_json["role"].get<std::string>());
            }
            if (sensor_json.contains("enabled")) {
                sensor->enabled = sensor_json["enabled"].get<bool>();
            }
            spdlog::debug("[HumiditySensorManager] Loaded config for {}: role={}, enabled={}",
                          klipper_name, humidity_role_to_string(sensor->role), sensor->enabled);
        }
    }

    update_subjects();
    spdlog::info("[HumiditySensorManager] Config loaded");
}

nlohmann::json HumiditySensorManager::save_config() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[HumiditySensorManager] Saving config");

    nlohmann::json config;
    nlohmann::json sensors_array = nlohmann::json::array();

    for (const auto& sensor : sensors_) {
        nlohmann::json sensor_json;
        sensor_json["klipper_name"] = sensor.klipper_name;
        sensor_json["role"] = humidity_role_to_string(sensor.role);
        sensor_json["enabled"] = sensor.enabled;
        sensor_json["type"] = humidity_type_to_string(sensor.type);
        sensors_array.push_back(sensor_json);
    }

    config["sensors"] = sensors_array;

    spdlog::info("[HumiditySensorManager] Config saved");
    return config;
}

// ============================================================================
// Initialization
// ============================================================================

void HumiditySensorManager::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    spdlog::trace("[HumiditySensorManager] Initializing subjects");

    // Initialize subjects with SubjectManager for automatic cleanup
    // -1 = no sensor assigned, 0+ = humidity x 10
    UI_MANAGED_SUBJECT_INT(chamber_humidity_, -1, "chamber_humidity", subjects_);
    // -1 = no sensor assigned, 0+ = pressure in Pa
    UI_MANAGED_SUBJECT_INT(chamber_pressure_, -1, "chamber_pressure", subjects_);
    // -1 = no sensor assigned, 0+ = humidity x 10
    UI_MANAGED_SUBJECT_INT(dryer_humidity_, -1, "dryer_humidity", subjects_);
    UI_MANAGED_SUBJECT_INT(sensor_count_, 0, "humidity_sensor_count", subjects_);
    // Text subject for display (formatted as "45%" or "--")
    UI_MANAGED_SUBJECT_STRING(chamber_humidity_text_, chamber_humidity_text_buf_, "--",
                              "chamber_humidity_text", subjects_);

    subjects_initialized_ = true;
    spdlog::trace("[HumiditySensorManager] Subjects initialized");
}

void HumiditySensorManager::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::trace("[HumiditySensorManager] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::trace("[HumiditySensorManager] Subjects deinitialized");
}

// ============================================================================
// Sensor Queries
// ============================================================================

bool HumiditySensorManager::has_sensors() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return !sensors_.empty();
}

std::vector<HumiditySensorConfig> HumiditySensorManager::get_sensors() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_; // Return thread-safe copy
}

size_t HumiditySensorManager::sensor_count() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_.size();
}

// ============================================================================
// Configuration
// ============================================================================

void HumiditySensorManager::set_sensor_role(const std::string& klipper_name,
                                            HumiditySensorRole role) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // If assigning a role, clear it from any other sensor first
    if (role != HumiditySensorRole::NONE) {
        for (auto& sensor : sensors_) {
            if (sensor.role == role && sensor.klipper_name != klipper_name) {
                spdlog::debug("[HumiditySensorManager] Clearing role {} from {}",
                              humidity_role_to_string(role), sensor.sensor_name);
                sensor.role = HumiditySensorRole::NONE;
            }
        }
    }

    auto* sensor = find_config(klipper_name);
    if (sensor) {
        sensor->role = role;
        spdlog::info("[HumiditySensorManager] Set role for {} to {}", sensor->sensor_name,
                     humidity_role_to_string(role));
        update_subjects();
    }
}

void HumiditySensorManager::set_sensor_enabled(const std::string& klipper_name, bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto* sensor = find_config(klipper_name);
    if (sensor) {
        sensor->enabled = enabled;
        spdlog::info("[HumiditySensorManager] Set enabled for {} to {}", sensor->sensor_name,
                     enabled);
        update_subjects();
    }
}

// ============================================================================
// State Queries
// ============================================================================

std::optional<HumiditySensorState>
HumiditySensorManager::get_sensor_state(HumiditySensorRole role) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (role == HumiditySensorRole::NONE) {
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

bool HumiditySensorManager::is_sensor_available(HumiditySensorRole role) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (role == HumiditySensorRole::NONE) {
        return false;
    }

    const auto* config = find_config_by_role(role);
    if (!config || !config->enabled) {
        return false;
    }

    auto it = states_.find(config->klipper_name);
    return it != states_.end() && it->second.available;
}

// ============================================================================
// LVGL Subjects
// ============================================================================

lv_subject_t* HumiditySensorManager::get_chamber_humidity_subject() {
    return &chamber_humidity_;
}

lv_subject_t* HumiditySensorManager::get_chamber_pressure_subject() {
    return &chamber_pressure_;
}

lv_subject_t* HumiditySensorManager::get_dryer_humidity_subject() {
    return &dryer_humidity_;
}

lv_subject_t* HumiditySensorManager::get_sensor_count_subject() {
    return &sensor_count_;
}

lv_subject_t* HumiditySensorManager::get_chamber_humidity_text_subject() {
    return &chamber_humidity_text_;
}

// ============================================================================
// Testing Support
// ============================================================================

void HumiditySensorManager::set_sync_mode(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sync_mode_ = enabled;
}

void HumiditySensorManager::update_subjects_on_main_thread() {
    update_subjects();
}

// ============================================================================
// Private Helpers
// ============================================================================

bool HumiditySensorManager::parse_klipper_name(const std::string& klipper_name,
                                               std::string& sensor_name,
                                               HumiditySensorType& type) const {
    const std::string bme280_prefix = "bme280 ";
    const std::string htu21d_prefix = "htu21d ";

    if (klipper_name.rfind(bme280_prefix, 0) == 0) {
        // Starts with "bme280 " - extract the name after the prefix
        sensor_name = klipper_name.substr(bme280_prefix.length());
        type = HumiditySensorType::BME280;
        return true;
    }

    if (klipper_name.rfind(htu21d_prefix, 0) == 0) {
        // Starts with "htu21d " - extract the name after the prefix
        sensor_name = klipper_name.substr(htu21d_prefix.length());
        type = HumiditySensorType::HTU21D;
        return true;
    }

    return false;
}

HumiditySensorConfig* HumiditySensorManager::find_config(const std::string& klipper_name) {
    for (auto& sensor : sensors_) {
        if (sensor.klipper_name == klipper_name) {
            return &sensor;
        }
    }
    return nullptr;
}

const HumiditySensorConfig*
HumiditySensorManager::find_config(const std::string& klipper_name) const {
    for (const auto& sensor : sensors_) {
        if (sensor.klipper_name == klipper_name) {
            return &sensor;
        }
    }
    return nullptr;
}

const HumiditySensorConfig*
HumiditySensorManager::find_config_by_role(HumiditySensorRole role) const {
    for (const auto& sensor : sensors_) {
        if (sensor.role == role) {
            return &sensor;
        }
    }
    return nullptr;
}

void HumiditySensorManager::update_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    // Get chamber humidity value
    auto get_chamber_humidity_value = [this]() -> int {
        const auto* config = find_config_by_role(HumiditySensorRole::CHAMBER);
        if (!config || !config->enabled) {
            return -1; // No sensor assigned or disabled
        }

        auto it = states_.find(config->klipper_name);
        if (it == states_.end() || !it->second.available) {
            return -1; // Sensor unavailable
        }

        // Convert humidity to int (% x 10)
        return static_cast<int>(it->second.humidity * 10.0f);
    };

    // Get chamber pressure value
    auto get_chamber_pressure_value = [this]() -> int {
        const auto* config = find_config_by_role(HumiditySensorRole::CHAMBER);
        if (!config || !config->enabled) {
            return -1; // No sensor assigned or disabled
        }

        auto it = states_.find(config->klipper_name);
        if (it == states_.end() || !it->second.available) {
            return -1; // Sensor unavailable
        }

        // Convert pressure from hPa to Pa (hPa * 100 = Pa)
        return static_cast<int>(it->second.pressure * 100.0f);
    };

    // Get dryer humidity value
    auto get_dryer_humidity_value = [this]() -> int {
        const auto* config = find_config_by_role(HumiditySensorRole::DRYER);
        if (!config || !config->enabled) {
            return -1; // No sensor assigned or disabled
        }

        auto it = states_.find(config->klipper_name);
        if (it == states_.end() || !it->second.available) {
            return -1; // Sensor unavailable
        }

        // Convert humidity to int (% x 10)
        return static_cast<int>(it->second.humidity * 10.0f);
    };

    int chamber_humidity = get_chamber_humidity_value();
    lv_subject_set_int(&chamber_humidity_, chamber_humidity);
    lv_subject_set_int(&chamber_pressure_, get_chamber_pressure_value());
    lv_subject_set_int(&dryer_humidity_, get_dryer_humidity_value());

    // Update text subject: format as "45%" or "—" if unavailable
    if (chamber_humidity >= 0) {
        helix::fmt::format_humidity(chamber_humidity, chamber_humidity_text_buf_,
                                    sizeof(chamber_humidity_text_buf_));
    } else {
        snprintf(chamber_humidity_text_buf_, sizeof(chamber_humidity_text_buf_), "%s",
                 helix::fmt::UNAVAILABLE);
    }
    lv_subject_copy_string(&chamber_humidity_text_, chamber_humidity_text_buf_);

    spdlog::trace("[HumiditySensorManager] Subjects updated: chamber_humidity={}, "
                  "chamber_pressure={}, dryer_humidity={}, text={}",
                  lv_subject_get_int(&chamber_humidity_), lv_subject_get_int(&chamber_pressure_),
                  lv_subject_get_int(&dryer_humidity_), chamber_humidity_text_buf_);
}

} // namespace helix::sensors
