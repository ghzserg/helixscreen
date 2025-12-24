// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2025 HelixScreen Authors

#include "filament_sensor_manager.h"

#include "ui_error_reporting.h"
#include "ui_update_queue.h"

#include "config.h"
#include "spdlog/spdlog.h"

#include <algorithm>

// CRITICAL: Subject updates trigger lv_obj_invalidate() which asserts if called
// during LVGL rendering. WebSocket callbacks run on libhv's event loop thread,
// not the main LVGL thread. We must defer subject updates to the main thread
// via ui_async_call to avoid the "Invalidate area not allowed during rendering"
// assertion.

namespace {

/// @brief Async callback to update subjects on the main LVGL thread
void async_update_subjects_callback(void* /*user_data*/) {
    helix::FilamentSensorManager::instance().update_subjects_on_main_thread();
}

} // namespace

namespace helix {

// ============================================================================
// Singleton
// ============================================================================

FilamentSensorManager& FilamentSensorManager::instance() {
    static FilamentSensorManager instance;
    return instance;
}

FilamentSensorManager::FilamentSensorManager() = default;

FilamentSensorManager::~FilamentSensorManager() = default;

// ============================================================================
// Initialization
// ============================================================================

void FilamentSensorManager::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    spdlog::debug("[FilamentSensorManager] Initializing subjects");

    // Initialize all subjects
    // -1 = no sensor, 0 = no filament, 1 = filament detected
    lv_subject_init_int(&runout_detected_, -1);
    lv_subject_init_int(&toolhead_detected_, -1);
    lv_subject_init_int(&entry_detected_, -1);
    lv_subject_init_int(&any_runout_, 0);
    lv_subject_init_int(&motion_active_, 0);
    lv_subject_init_int(&master_enabled_subject_, master_enabled_ ? 1 : 0);
    lv_subject_init_int(&sensor_count_, 0);

    // Register subjects for XML binding
    lv_xml_register_subject(nullptr, "filament_runout_detected", &runout_detected_);
    lv_xml_register_subject(nullptr, "filament_toolhead_detected", &toolhead_detected_);
    lv_xml_register_subject(nullptr, "filament_entry_detected", &entry_detected_);
    lv_xml_register_subject(nullptr, "filament_any_runout", &any_runout_);
    lv_xml_register_subject(nullptr, "filament_motion_active", &motion_active_);
    lv_xml_register_subject(nullptr, "filament_master_enabled", &master_enabled_subject_);
    lv_xml_register_subject(nullptr, "filament_sensor_count", &sensor_count_);

    subjects_initialized_ = true;
    spdlog::info("[FilamentSensorManager] Subjects initialized");
}

void FilamentSensorManager::discover_sensors(const std::vector<std::string>& klipper_sensor_names) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::info("[FilamentSensorManager] Discovering {} sensors", klipper_sensor_names.size());

    // Clear existing sensors but preserve state for reconnection
    sensors_.clear();

    for (const auto& klipper_name : klipper_sensor_names) {
        std::string sensor_name;
        FilamentSensorType type;

        if (!parse_klipper_name(klipper_name, sensor_name, type)) {
            spdlog::warn("[FilamentSensorManager] Failed to parse sensor name: {}", klipper_name);
            continue;
        }

        FilamentSensorConfig config(klipper_name, sensor_name, type);
        sensors_.push_back(config);

        // Initialize state if not already present
        if (states_.find(klipper_name) == states_.end()) {
            FilamentSensorState state;
            state.available = true;
            states_[klipper_name] = state;
        } else {
            states_[klipper_name].available = true;
        }

        spdlog::debug("[FilamentSensorManager] Discovered sensor: {} (type: {})", sensor_name,
                      type == FilamentSensorType::MOTION ? "motion" : "switch");
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

    // Update sensor count subject
    if (subjects_initialized_) {
        lv_subject_set_int(&sensor_count_, static_cast<int>(sensors_.size()));
    }

    spdlog::info("[FilamentSensorManager] Discovered {} filament sensors", sensors_.size());
}

bool FilamentSensorManager::has_sensors() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return !sensors_.empty();
}

std::vector<FilamentSensorConfig> FilamentSensorManager::get_sensors() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_; // Return thread-safe copy
}

size_t FilamentSensorManager::sensor_count() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_.size();
}

// ============================================================================
// Configuration
// ============================================================================

void FilamentSensorManager::load_config() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[FilamentSensorManager] Loading config");

    Config* config = Config::get_instance();
    if (!config) {
        spdlog::warn("[FilamentSensorManager] Config not initialized");
        return;
    }

    // Build path using default printer prefix
    std::string base_path = config->df() + "filament_sensors";

    // Load master enable
    master_enabled_ = config->get<bool>(base_path + "/master_enabled", true);
    if (subjects_initialized_) {
        lv_subject_set_int(&master_enabled_subject_, master_enabled_ ? 1 : 0);
    }

    // Load per-sensor config
    try {
        json& sensors_json = config->get_json(base_path + "/sensors");
        if (sensors_json.is_array()) {
            for (const auto& sensor_json : sensors_json) {
                if (!sensor_json.contains("klipper_name")) {
                    continue;
                }

                std::string klipper_name = sensor_json["klipper_name"].get<std::string>();
                auto* sensor = find_config(klipper_name);

                if (sensor) {
                    // Update existing sensor config
                    if (sensor_json.contains("role")) {
                        sensor->role =
                            role_from_config_string(sensor_json["role"].get<std::string>());
                    }
                    if (sensor_json.contains("enabled")) {
                        sensor->enabled = sensor_json["enabled"].get<bool>();
                    }
                    spdlog::debug(
                        "[FilamentSensorManager] Loaded config for {}: role={}, enabled={}",
                        klipper_name, role_to_config_string(sensor->role), sensor->enabled);
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::debug("[FilamentSensorManager] No sensor config found: {}", e.what());
    }

    update_subjects();
    spdlog::info("[FilamentSensorManager] Config loaded, master_enabled={}", master_enabled_);
}

void FilamentSensorManager::save_config() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[FilamentSensorManager] Saving config");

    Config* config = Config::get_instance();
    if (!config) {
        spdlog::warn("[FilamentSensorManager] Config not initialized");
        return;
    }

    // Build path using default printer prefix
    std::string base_path = config->df() + "filament_sensors";

    // Build filament_sensors config
    json fs_config;
    fs_config["master_enabled"] = master_enabled_;

    json sensors_array = json::array();
    for (const auto& sensor : sensors_) {
        json sensor_json;
        sensor_json["klipper_name"] = sensor.klipper_name;
        sensor_json["role"] = role_to_config_string(sensor.role);
        sensor_json["enabled"] = sensor.enabled;
        sensor_json["type"] = type_to_config_string(sensor.type);
        sensors_array.push_back(sensor_json);
    }
    fs_config["sensors"] = sensors_array;

    // Set the config using JSON pointer path
    config->get_json(base_path) = fs_config;
    config->save();

    spdlog::info("[FilamentSensorManager] Config saved");
}

void FilamentSensorManager::set_sensor_role(const std::string& klipper_name,
                                            FilamentSensorRole role) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // If assigning a role, clear it from any other sensor first
    if (role != FilamentSensorRole::NONE) {
        for (auto& sensor : sensors_) {
            if (sensor.role == role && sensor.klipper_name != klipper_name) {
                spdlog::debug("[FilamentSensorManager] Clearing role {} from {}",
                              role_to_config_string(role), sensor.sensor_name);
                sensor.role = FilamentSensorRole::NONE;
            }
        }
    }

    auto* sensor = find_config(klipper_name);
    if (sensor) {
        sensor->role = role;
        spdlog::info("[FilamentSensorManager] Set role for {} to {}", sensor->sensor_name,
                     role_to_config_string(role));
        update_subjects();
    }
}

void FilamentSensorManager::set_sensor_enabled(const std::string& klipper_name, bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto* sensor = find_config(klipper_name);
    if (sensor) {
        sensor->enabled = enabled;
        spdlog::info("[FilamentSensorManager] Set enabled for {} to {}", sensor->sensor_name,
                     enabled);
        update_subjects();
    }
}

void FilamentSensorManager::set_master_enabled(bool enabled) {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        master_enabled_ = enabled;
    }

    if (subjects_initialized_) {
        lv_subject_set_int(&master_enabled_subject_, enabled ? 1 : 0);
    }

    spdlog::info("[FilamentSensorManager] Master enabled set to {}", enabled);
    update_subjects();
}

bool FilamentSensorManager::is_master_enabled() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return master_enabled_;
}

// ============================================================================
// State Queries
// ============================================================================

bool FilamentSensorManager::is_filament_detected(FilamentSensorRole role) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!master_enabled_ || role == FilamentSensorRole::NONE) {
        return false;
    }

    const auto* config = find_config_by_role(role);
    if (!config || !config->enabled) {
        return false;
    }

    auto it = states_.find(config->klipper_name);
    if (it == states_.end() || !it->second.available) {
        return false;
    }

    return it->second.filament_detected;
}

bool FilamentSensorManager::is_sensor_available(FilamentSensorRole role) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!master_enabled_ || role == FilamentSensorRole::NONE) {
        return false;
    }

    const auto* config = find_config_by_role(role);
    if (!config || !config->enabled) {
        return false;
    }

    auto it = states_.find(config->klipper_name);
    return it != states_.end() && it->second.available;
}

std::optional<FilamentSensorState>
FilamentSensorManager::get_sensor_state(FilamentSensorRole role) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

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

bool FilamentSensorManager::has_any_runout() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!master_enabled_) {
        return false;
    }

    for (const auto& sensor : sensors_) {
        if (!sensor.enabled || sensor.role == FilamentSensorRole::NONE) {
            continue;
        }

        auto it = states_.find(sensor.klipper_name);
        if (it != states_.end() && it->second.available && !it->second.filament_detected) {
            return true;
        }
    }

    return false;
}

bool FilamentSensorManager::is_motion_active() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!master_enabled_) {
        return false;
    }

    for (const auto& sensor : sensors_) {
        if (sensor.type != FilamentSensorType::MOTION || !sensor.enabled) {
            continue;
        }

        auto it = states_.find(sensor.klipper_name);
        if (it != states_.end() && it->second.available && it->second.enabled) {
            // Motion sensor is active when Klipper reports it as enabled
            // and we've seen recent detection events
            return true;
        }
    }

    return false;
}

// ============================================================================
// State Updates
// ============================================================================

void FilamentSensorManager::update_from_status(const json& status) {
    // Collect notifications to send after releasing lock (avoid deadlock)
    struct Notification {
        std::string klipper_name;
        std::string sensor_name;
        FilamentSensorState old_state;
        FilamentSensorState new_state;
        FilamentSensorRole role;
        bool should_toast;
    };
    std::vector<Notification> notifications;
    StateChangeCallback callback_copy;
    bool any_changed = false;

    // Phase 1: Update state under lock, collect notifications
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        // Copy callback for use outside lock
        callback_copy = state_change_callback_;

        // Process filament_switch_sensor updates
        for (const auto& sensor : sensors_) {
            // Build the Klipper object key (e.g., "filament_switch_sensor fsensor")
            std::string key = sensor.klipper_name;

            // Check if this sensor has an update
            // Moonraker sends updates with the full object name as key
            if (!status.contains(key)) {
                // Also try without the prefix for older Moonraker versions
                continue;
            }

            const auto& sensor_data = status[key];
            auto& state = states_[sensor.klipper_name];
            FilamentSensorState old_state = state;

            // Update filament_detected
            if (sensor_data.contains("filament_detected")) {
                state.filament_detected = sensor_data["filament_detected"].get<bool>();
            }

            // Motion sensors have additional fields
            if (sensor.type == FilamentSensorType::MOTION) {
                if (sensor_data.contains("enabled")) {
                    state.enabled = sensor_data["enabled"].get<bool>();
                }
                if (sensor_data.contains("detection_count")) {
                    state.detection_count = sensor_data["detection_count"].get<int>();
                }
            }

            // Check for state change
            if (state.filament_detected != old_state.filament_detected) {
                any_changed = true;

                spdlog::info("[FilamentSensorManager] Sensor {} state changed: {} -> {}",
                             sensor.sensor_name, old_state.filament_detected ? "detected" : "empty",
                             state.filament_detected ? "detected" : "empty");

                // Queue notification for after lock release
                Notification notif;
                notif.klipper_name = sensor.klipper_name;
                notif.sensor_name = sensor.sensor_name;
                notif.old_state = old_state;
                notif.new_state = state;
                notif.role = sensor.role;
                notif.should_toast =
                    master_enabled_ && sensor.enabled && sensor.role != FilamentSensorRole::NONE;
                notifications.push_back(notif);
            }
        }

        if (any_changed) {
            if (sync_mode_) {
                // In test mode, update subjects synchronously
                spdlog::info("[FilamentSensorManager] sync_mode: updating subjects synchronously");
                update_subjects();
            } else {
                // Defer subject updates to main LVGL thread via ui_async_call()
                // This avoids the "Invalidate area not allowed during rendering" assertion
                spdlog::info("[FilamentSensorManager] async_mode: deferring via ui_async_call");
                ui_async_call(async_update_subjects_callback, nullptr);
            }
        }
    }
    // Lock released here

    // Phase 2: Send notifications without holding lock (prevents deadlock)
    for (const auto& notif : notifications) {
        // Fire callback if registered
        if (callback_copy) {
            callback_copy(notif.klipper_name, notif.old_state, notif.new_state);
        }

        // Show toast notification
        if (notif.should_toast) {
            std::string role_name = role_to_display_string(notif.role);
            if (notif.new_state.filament_detected) {
                NOTIFY_INFO("{}: Filament inserted", role_name);
            } else {
                NOTIFY_WARNING("{}: Filament removed", role_name);
            }
        }
    }
}

void FilamentSensorManager::set_state_change_callback(StateChangeCallback callback) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    state_change_callback_ = std::move(callback);
}

// ============================================================================
// LVGL Subjects
// ============================================================================

lv_subject_t* FilamentSensorManager::get_runout_detected_subject() {
    return &runout_detected_;
}

lv_subject_t* FilamentSensorManager::get_toolhead_detected_subject() {
    return &toolhead_detected_;
}

lv_subject_t* FilamentSensorManager::get_entry_detected_subject() {
    return &entry_detected_;
}

lv_subject_t* FilamentSensorManager::get_any_runout_subject() {
    return &any_runout_;
}

lv_subject_t* FilamentSensorManager::get_motion_active_subject() {
    return &motion_active_;
}

lv_subject_t* FilamentSensorManager::get_master_enabled_subject() {
    return &master_enabled_subject_;
}

lv_subject_t* FilamentSensorManager::get_sensor_count_subject() {
    return &sensor_count_;
}

// ============================================================================
// Private Helpers
// ============================================================================

bool FilamentSensorManager::parse_klipper_name(const std::string& klipper_name,
                                               std::string& sensor_name,
                                               FilamentSensorType& type) const {
    const std::string switch_prefix = "filament_switch_sensor ";
    const std::string motion_prefix = "filament_motion_sensor ";

    if (klipper_name.rfind(switch_prefix, 0) == 0) {
        sensor_name = klipper_name.substr(switch_prefix.length());
        type = FilamentSensorType::SWITCH;
        return !sensor_name.empty();
    }

    if (klipper_name.rfind(motion_prefix, 0) == 0) {
        sensor_name = klipper_name.substr(motion_prefix.length());
        type = FilamentSensorType::MOTION;
        return !sensor_name.empty();
    }

    return false;
}

FilamentSensorConfig* FilamentSensorManager::find_config(const std::string& klipper_name) {
    for (auto& sensor : sensors_) {
        if (sensor.klipper_name == klipper_name) {
            return &sensor;
        }
    }
    return nullptr;
}

const FilamentSensorConfig*
FilamentSensorManager::find_config(const std::string& klipper_name) const {
    for (const auto& sensor : sensors_) {
        if (sensor.klipper_name == klipper_name) {
            return &sensor;
        }
    }
    return nullptr;
}

const FilamentSensorConfig*
FilamentSensorManager::find_config_by_role(FilamentSensorRole role) const {
    for (const auto& sensor : sensors_) {
        if (sensor.role == role) {
            return &sensor;
        }
    }
    return nullptr;
}

void FilamentSensorManager::update_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    // Helper to get subject value for a role
    auto get_role_value = [this](FilamentSensorRole role) -> int {
        if (!master_enabled_) {
            return -1; // Disabled
        }

        const auto* config = find_config_by_role(role);
        if (!config || !config->enabled) {
            return -1; // No sensor assigned or disabled
        }

        auto it = states_.find(config->klipper_name);
        if (it == states_.end() || !it->second.available) {
            return -1; // Sensor unavailable
        }

        return it->second.filament_detected ? 1 : 0;
    };

    // Update per-role subjects
    lv_subject_set_int(&runout_detected_, get_role_value(FilamentSensorRole::RUNOUT));
    lv_subject_set_int(&toolhead_detected_, get_role_value(FilamentSensorRole::TOOLHEAD));
    lv_subject_set_int(&entry_detected_, get_role_value(FilamentSensorRole::ENTRY));

    // Update aggregate subjects
    lv_subject_set_int(&any_runout_, has_any_runout() ? 1 : 0);
    lv_subject_set_int(&motion_active_, is_motion_active() ? 1 : 0);

    spdlog::trace(
        "[FilamentSensorManager] Subjects updated: runout={}, toolhead={}, entry={}, any_runout={}",
        lv_subject_get_int(&runout_detected_), lv_subject_get_int(&toolhead_detected_),
        lv_subject_get_int(&entry_detected_), lv_subject_get_int(&any_runout_));
}

void FilamentSensorManager::reset_for_testing() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Clear all sensors and states
    sensors_.clear();
    states_.clear();

    // Reset master enabled
    master_enabled_ = true;

    // Clear callback
    state_change_callback_ = nullptr;

    // Enable sync mode for testing (avoids lv_async_call)
    sync_mode_ = true;

    // Reset subjects if initialized
    if (subjects_initialized_) {
        lv_subject_set_int(&runout_detected_, -1);
        lv_subject_set_int(&toolhead_detected_, -1);
        lv_subject_set_int(&entry_detected_, -1);
        lv_subject_set_int(&any_runout_, 0);
        lv_subject_set_int(&motion_active_, 0);
        lv_subject_set_int(&master_enabled_subject_, 1);
        lv_subject_set_int(&sensor_count_, 0);
    }

    spdlog::debug("[FilamentSensorManager] Reset for testing");
}

void FilamentSensorManager::set_sync_mode(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sync_mode_ = enabled;
}

void FilamentSensorManager::update_subjects_on_main_thread() {
    // This is called by lv_async_call from the main LVGL thread
    // It's safe to update subjects here without causing render-phase assertions
    update_subjects();
}

} // namespace helix
