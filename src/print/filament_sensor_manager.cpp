// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filament_sensor_manager.h"

#include "ui_error_reporting.h"
#include "ui_update_queue.h"

#include "ams_state.h"
#include "app_constants.h"
#include "app_globals.h"
#include "config.h"
#include "spdlog/spdlog.h"
#include "static_subject_registry.h"

#include <algorithm>

// CRITICAL: Subject updates trigger lv_obj_invalidate() which asserts if called
// during LVGL rendering. WebSocket callbacks run on libhv's event loop thread,
// not the main LVGL thread. We must defer subject updates to the main thread
// via ui_async_call to avoid the "Invalidate area not allowed during rendering"
// assertion.

namespace helix {

// ============================================================================
// Singleton
// ============================================================================

FilamentSensorManager& FilamentSensorManager::instance() {
    static FilamentSensorManager instance;
    return instance;
}

FilamentSensorManager::FilamentSensorManager() : startup_time_(std::chrono::steady_clock::now()) {}

FilamentSensorManager::~FilamentSensorManager() = default;

// ============================================================================
// ISensorManager Interface
// ============================================================================

std::string FilamentSensorManager::category_name() const {
    return "filament_switch";
}

void FilamentSensorManager::discover(const std::vector<std::string>& klipper_objects) {
    // Filter to only filament sensor objects and delegate to discover_sensors
    std::vector<std::string> sensor_names;
    for (const auto& obj : klipper_objects) {
        // Match filament_switch_sensor and filament_motion_sensor prefixes
        if (obj.rfind("filament_switch_sensor ", 0) == 0 ||
            obj.rfind("filament_motion_sensor ", 0) == 0) {
            sensor_names.push_back(obj);
        }
    }
    discover_sensors(sensor_names);
}

void FilamentSensorManager::load_config(const nlohmann::json& /*config*/) {
    // This manager uses legacy Config-based persistence
    // Delegate to the file-based config loader
    load_config_from_file();
}

nlohmann::json FilamentSensorManager::save_config() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Build and return the config JSON
    // Also save to file for legacy compatibility
    nlohmann::json config;
    config["master_enabled"] = master_enabled_;

    nlohmann::json sensors_array = nlohmann::json::array();
    for (const auto& sensor : sensors_) {
        nlohmann::json sensor_json;
        sensor_json["klipper_name"] = sensor.klipper_name;
        sensor_json["role"] = role_to_config_string(sensor.role);
        sensor_json["enabled"] = sensor.enabled;
        sensor_json["type"] = type_to_config_string(sensor.type);
        sensors_array.push_back(sensor_json);
    }
    config["sensors"] = sensors_array;

    return config;
}

// ============================================================================
// Initialization
// ============================================================================

void FilamentSensorManager::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    spdlog::trace("[FilamentSensorManager] Initializing subjects");

    // Initialize all subjects with SubjectManager for automatic cleanup
    // -1 = no sensor, 0 = no filament/not triggered, 1 = filament detected/triggered
    UI_MANAGED_SUBJECT_INT(runout_detected_, -1, "filament_runout_detected", subjects_);
    UI_MANAGED_SUBJECT_INT(toolhead_detected_, -1, "filament_toolhead_detected", subjects_);
    UI_MANAGED_SUBJECT_INT(entry_detected_, -1, "filament_entry_detected", subjects_);
    UI_MANAGED_SUBJECT_INT(probe_triggered_, -1, "probe_triggered", subjects_);
    UI_MANAGED_SUBJECT_INT(any_runout_, 0, "filament_any_runout", subjects_);
    UI_MANAGED_SUBJECT_INT(motion_active_, 0, "filament_motion_active", subjects_);
    UI_MANAGED_SUBJECT_INT(master_enabled_subject_, master_enabled_ ? 1 : 0,
                           "filament_master_enabled", subjects_);
    UI_MANAGED_SUBJECT_INT(sensor_count_, 0, "filament_sensor_count", subjects_);

    subjects_initialized_ = true;

    // Self-register cleanup — ensures deinit runs before lv_deinit()
    StaticSubjectRegistry::instance().register_deinit(
        "FilamentSensorManager", []() { FilamentSensorManager::instance().deinit_subjects(); });

    spdlog::trace("[FilamentSensorManager] Subjects initialized");
}

void FilamentSensorManager::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::trace("[FilamentSensorManager] Deinitializing subjects");

    // Deinitialize all subjects to disconnect observers before lv_deinit()
    subjects_.deinit_all();

    subjects_initialized_ = false;
    spdlog::trace("[FilamentSensorManager] Subjects deinitialized");
}

void FilamentSensorManager::discover_sensors(const std::vector<std::string>& klipper_sensor_names) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Reset grace period timer - now anchored to Moonraker connection, not app startup
    // This ensures we wait for sensor state to stabilize AFTER connection is established
    startup_time_ = std::chrono::steady_clock::now();

    spdlog::debug("[FilamentSensorManager] Discovering {} sensors", klipper_sensor_names.size());

    // Clear existing sensors but preserve state for reconnection
    sensors_.clear();

    for (const auto& klipper_name : klipper_sensor_names) {
        std::string sensor_name;
        FilamentSensorType type = FilamentSensorType::SWITCH; // Default, overwritten by parse

        if (!parse_klipper_name(klipper_name, sensor_name, type)) {
            spdlog::warn("[FilamentSensorManager] Failed to parse sensor name: {}", klipper_name);
            continue;
        }

        FilamentSensorConfig config(klipper_name, sensor_name, type);

        // Auto-assign RUNOUT role if sensor name suggests it's a runout sensor
        // (e.g., "runout", "fsensor_runout", "runout_sensor")
        // Only assign if no other sensor already has RUNOUT role
        std::string lower_name = sensor_name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        if (lower_name.find("runout") != std::string::npos) {
            bool runout_already_assigned = false;
            for (const auto& s : sensors_) {
                if (s.role == FilamentSensorRole::RUNOUT) {
                    runout_already_assigned = true;
                    break;
                }
            }
            if (!runout_already_assigned) {
                config.role = FilamentSensorRole::RUNOUT;
                spdlog::debug(
                    "[FilamentSensorManager] Auto-assigned RUNOUT role to '{}' based on name",
                    sensor_name);
            }
        }

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

    spdlog::debug("[FilamentSensorManager] Discovered {} filament sensors", sensors_.size());
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

void FilamentSensorManager::load_config_from_file() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[FilamentSensorManager] Loading config from file");

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

    // Log final state of all sensors at INFO for debugging
    spdlog::debug("[FilamentSensorManager] Config loaded, master_enabled={}", master_enabled_);
    for (const auto& sensor : sensors_) {
        spdlog::debug("[FilamentSensorManager]   {} -> role={}, enabled={}", sensor.klipper_name,
                      role_to_config_string(sensor.role), sensor.enabled);
    }
}

void FilamentSensorManager::save_config_to_file() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[FilamentSensorManager] Saving config to file");

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

    spdlog::info("[FilamentSensorManager] Config saved to file");
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

    // During startup grace period, state is unreliable - don't report runout
    if (is_in_startup_grace_period()) {
        return false;
    }

    if (!master_enabled_) {
        return false;
    }

    for (const auto& sensor : sensors_) {
        if (!sensor.enabled || sensor.role == FilamentSensorRole::NONE) {
            spdlog::trace(
                "[FilamentSensorManager] has_any_runout: skipping {} (enabled={}, role={})",
                sensor.sensor_name, sensor.enabled, role_to_config_string(sensor.role));
            continue;
        }

        auto it = states_.find(sensor.klipper_name);
        if (it != states_.end() && it->second.available && !it->second.filament_detected) {
            spdlog::debug("[FilamentSensorManager] has_any_runout: TRUE - {} ({}) has no filament",
                          sensor.sensor_name, role_to_config_string(sensor.role));
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
    // Suppress toast notifications for initial state at startup
    // (similar to USB manager - users don't need to be told filament is present)
    auto now = std::chrono::steady_clock::now();
    bool within_grace_period =
        (now - startup_time_) < AppConstants::Startup::SENSOR_STABILIZATION_PERIOD;

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

            // Update filament_detected (use value() for exception safety — Moonraker
            // may send unexpected types during firmware restarts)
            if (sensor_data.contains("filament_detected")) {
                state.filament_detected =
                    sensor_data.value("filament_detected", state.filament_detected);
            }

            // Motion sensors have additional fields
            if (sensor.type == FilamentSensorType::MOTION) {
                if (sensor_data.contains("enabled")) {
                    state.enabled = sensor_data.value("enabled", state.enabled);
                }
                if (sensor_data.contains("detection_count")) {
                    state.detection_count =
                        sensor_data.value("detection_count", state.detection_count);
                }
            }

            // Check for state change
            if (state.filament_detected != old_state.filament_detected) {
                any_changed = true;

                // Log at WARN if this is a runout (filament gone) on an active sensor
                if (!state.filament_detected && sensor.role != FilamentSensorRole::NONE &&
                    sensor.enabled) {
                    spdlog::warn("[FilamentSensorManager] RUNOUT: {} ({}) filament gone",
                                 sensor.sensor_name, role_to_config_string(sensor.role));
                } else {
                    spdlog::debug("[FilamentSensorManager] Sensor {} state changed: {} -> {}",
                                  sensor.sensor_name,
                                  old_state.filament_detected ? "detected" : "empty",
                                  state.filament_detected ? "detected" : "empty");
                }

                // Queue notification for after lock release
                Notification notif;
                notif.klipper_name = sensor.klipper_name;
                notif.sensor_name = sensor.sensor_name;
                notif.old_state = old_state;
                notif.new_state = state;
                notif.role = sensor.role;
                // Suppress toasts during startup grace period, wizard setup,
                // and active AMS filament operations (load/unload moves filament
                // past sensors intentionally, generating spurious triggers)
                bool ams_active = AmsState::instance().is_filament_operation_active();
                notif.should_toast = !within_grace_period && !is_wizard_active() && !ams_active &&
                                     master_enabled_ && sensor.enabled &&
                                     sensor.role != FilamentSensorRole::NONE;
                if (within_grace_period && master_enabled_ && sensor.enabled &&
                    sensor.role != FilamentSensorRole::NONE) {
                    spdlog::debug("[FilamentSensorManager] Suppressing startup toast for {}",
                                  sensor.sensor_name);
                } else if (ams_active && master_enabled_ && sensor.enabled &&
                           sensor.role != FilamentSensorRole::NONE) {
                    spdlog::debug(
                        "[FilamentSensorManager] Suppressing toast during AMS operation for {}",
                        sensor.sensor_name);
                }
                notifications.push_back(notif);
            }
        }

        if (any_changed) {
            if (sync_mode_) {
                // In test mode, update subjects synchronously
                spdlog::info("[FilamentSensorManager] sync_mode: updating subjects synchronously");
                update_subjects();
            } else {
                // Defer subject updates to main LVGL thread via helix::ui::queue_update()
                // This avoids the "Invalidate area not allowed during rendering" assertion
                // and provides exception safety (try-catch wrapping)
                spdlog::debug("[FilamentSensorManager] async_mode: deferring via ui_queue_update");
                helix::ui::queue_update(
                    [] { FilamentSensorManager::instance().update_subjects_on_main_thread(); });
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

void FilamentSensorManager::inject_mock_sensors(std::vector<std::string>& objects,
                                                nlohmann::json& /*config_keys*/,
                                                nlohmann::json& /*moonraker_info*/) {
    // Filament sensors are discovered from Klipper objects
    objects.emplace_back("filament_switch_sensor runout");
    objects.emplace_back("filament_switch_sensor toolhead");
    spdlog::debug(
        "[FilamentSensorManager] Injected mock sensors: filament_switch_sensor runout, toolhead");
}

void FilamentSensorManager::inject_mock_status(nlohmann::json& status) {
    // Filament switch sensors report filament_detected and enabled state
    status["filament_switch_sensor runout"] = {{"filament_detected", true}, {"enabled", true}};
    status["filament_switch_sensor toolhead"] = {{"filament_detected", true}, {"enabled", true}};
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

lv_subject_t* FilamentSensorManager::get_probe_triggered_subject() {
    return &probe_triggered_;
}

bool FilamentSensorManager::is_probe_triggered() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!master_enabled_) {
        return false;
    }

    const auto* config = find_config_by_role(FilamentSensorRole::Z_PROBE);
    if (!config || !config->enabled) {
        return false;
    }

    auto it = states_.find(config->klipper_name);
    if (it == states_.end() || !it->second.available) {
        return false;
    }

    return it->second.filament_detected;
}

bool FilamentSensorManager::is_in_startup_grace_period() const {
    auto now = std::chrono::steady_clock::now();
    return (now - startup_time_) < AppConstants::Startup::SENSOR_STABILIZATION_PERIOD;
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
    lv_subject_set_int(&probe_triggered_, get_role_value(FilamentSensorRole::Z_PROBE));

    // Update aggregate subjects
    // Suppress any_runout during startup grace period to avoid false modal triggers
    // (Moonraker may report sensors as "empty" before Klipper fully initializes)
    bool in_grace = is_in_startup_grace_period();
    int any_runout_value = (in_grace || !has_any_runout()) ? 0 : 1;
    if (in_grace && has_any_runout()) {
        spdlog::info(
            "[FilamentSensorManager] Suppressing runout modal during startup grace period");
    }
    lv_subject_set_int(&any_runout_, any_runout_value);
    lv_subject_set_int(&motion_active_, is_motion_active() ? 1 : 0);

    spdlog::trace("[FilamentSensorManager] Subjects updated: runout={}, toolhead={}, entry={}, "
                  "probe={}, any_runout={}",
                  lv_subject_get_int(&runout_detected_), lv_subject_get_int(&toolhead_detected_),
                  lv_subject_get_int(&entry_detected_), lv_subject_get_int(&probe_triggered_),
                  lv_subject_get_int(&any_runout_));
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
