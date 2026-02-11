// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl.h"
#include "sensor_registry.h"
#include "subject_managed_panel.h"
#include "temperature_sensor_types.h"

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace helix::sensors {

/**
 * @brief Manager for temperature sensors (temperature_sensor and temperature_fan)
 *
 * Implements ISensorManager interface for integration with SensorRegistry.
 * Provides:
 * - Auto-discovery of temperature sensors from Klipper objects list
 * - Auto-categorization by role (CHAMBER, MCU, HOST, AUXILIARY)
 * - Real-time state tracking from Moonraker updates
 * - Per-sensor dynamic LVGL subjects for reactive UI binding (centidegrees)
 *
 * Thread-safe for state updates from Moonraker callbacks.
 *
 * Klipper object names:
 * - temperature_sensor <name>  - Read-only temperature sensor
 * - temperature_fan <name>     - Temperature-controlled fan (has target and speed)
 *
 * Excludes: extruder, extruder1, heater_bed (managed by PrinterState)
 *
 * Status JSON format:
 * @code
 * {
 *   "temperature_sensor mcu_temp": {
 *     "temperature": 45.2
 *   },
 *   "temperature_fan exhaust_fan": {
 *     "temperature": 35.0,
 *     "target": 40.0,
 *     "speed": 0.5
 *   }
 * }
 * @endcode
 */
class TemperatureSensorManager : public ISensorManager {
  public:
    /**
     * @brief Dynamic integer subject for per-sensor temperature binding
     *
     * Wraps an lv_subject_t with lifecycle management. These are NOT registered
     * with the XML system since they are created dynamically per-sensor.
     * Values stored as centidegrees (temperature * 100).
     */
    struct DynamicIntSubject {
        lv_subject_t subject{};
        bool initialized = false;

        ~DynamicIntSubject() {
            if (initialized && lv_is_initialized()) {
                lv_subject_deinit(&subject);
            }
            initialized = false;
        }
    };

    /**
     * @brief Get singleton instance
     */
    static TemperatureSensorManager& instance();

    // Prevent copying
    TemperatureSensorManager(const TemperatureSensorManager&) = delete;
    TemperatureSensorManager& operator=(const TemperatureSensorManager&) = delete;

    // ========================================================================
    // ISensorManager Interface
    // ========================================================================

    /// @brief Get category name for registry
    [[nodiscard]] std::string category_name() const override;

    /**
     * @brief Discover sensors from Klipper objects list
     * @note MUST be called from main LVGL thread (updates subjects directly)
     */
    void discover(const std::vector<std::string>& klipper_objects) override;

    /// @brief Update state from Moonraker status JSON
    void update_from_status(const nlohmann::json& status) override;

    /// @brief Inject mock sensor objects for testing UI
    void inject_mock_sensors(std::vector<std::string>& objects, nlohmann::json& config_keys,
                             nlohmann::json& moonraker_info) override;

    /// @brief Inject mock status data for testing UI
    void inject_mock_status(nlohmann::json& status) override;

    /**
     * @brief Load sensor configuration from JSON
     * @note MUST be called from main LVGL thread (updates subjects directly)
     */
    void load_config(const nlohmann::json& config) override;

    /// @brief Save configuration to JSON
    [[nodiscard]] nlohmann::json save_config() const override;

    // ========================================================================
    // Initialization
    // ========================================================================

    /**
     * @brief Initialize LVGL subjects for UI binding
     *
     * Must be called before creating any XML components that bind to sensor subjects.
     * Safe to call multiple times (idempotent).
     */
    void init_subjects();

    /**
     * @brief Deinitialize LVGL subjects
     *
     * Must be called before lv_deinit() to properly disconnect observers.
     */
    void deinit_subjects();

    // ========================================================================
    // Sensor Queries
    // ========================================================================

    /**
     * @brief Check if any sensors have been discovered
     */
    [[nodiscard]] bool has_sensors() const;

    /**
     * @brief Get all discovered sensor configurations (thread-safe copy)
     */
    [[nodiscard]] std::vector<TemperatureSensorConfig> get_sensors() const;

    /**
     * @brief Get sensors sorted by priority (lower first), then by display_name
     */
    [[nodiscard]] std::vector<TemperatureSensorConfig> get_sensors_sorted() const;

    /**
     * @brief Get sensor count
     */
    [[nodiscard]] size_t sensor_count() const;

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Assign a role to a sensor
     * @note MUST be called from main LVGL thread (updates subjects directly)
     *
     * @param klipper_name Full Klipper object name
     * @param role New role assignment
     */
    void set_sensor_role(const std::string& klipper_name, TemperatureSensorRole role);

    /**
     * @brief Enable or disable a sensor
     * @note MUST be called from main LVGL thread (updates subjects directly)
     *
     * @param klipper_name Full Klipper object name
     * @param enabled Whether sensor should be monitored
     */
    void set_sensor_enabled(const std::string& klipper_name, bool enabled);

    // ========================================================================
    // State Queries
    // ========================================================================

    /**
     * @brief Get current state for a sensor by klipper_name (thread-safe copy)
     *
     * @param klipper_name The full Klipper name to query
     * @return State copy if sensor exists, empty optional otherwise
     */
    [[nodiscard]] std::optional<TemperatureSensorState>
    get_sensor_state(const std::string& klipper_name) const;

    // ========================================================================
    // LVGL Subjects
    // ========================================================================

    /**
     * @brief Get dynamic subject for a specific sensor's temperature
     * @param klipper_name Full Klipper object name
     * @return Subject (int: centidegrees), or nullptr if sensor unknown
     */
    [[nodiscard]] lv_subject_t* get_temp_subject(const std::string& klipper_name);

    /**
     * @brief Get subject for sensor count (for conditional UI visibility)
     * @return Subject (int: number of discovered sensors)
     */
    [[nodiscard]] lv_subject_t* get_sensor_count_subject();

    /**
     * @brief Enable synchronous mode for testing
     *
     * When enabled, update_from_status() calls update_subjects() synchronously
     * instead of using lv_async_call().
     */
    void set_sync_mode(bool enabled);

    /**
     * @brief Update subjects on main LVGL thread (called by async callback)
     */
    void update_subjects_on_main_thread();

    friend class TemperatureSensorManagerTestAccess;

  private:
    TemperatureSensorManager();
    ~TemperatureSensorManager();

    /**
     * @brief Parse Klipper object name to determine if it's a temperature sensor
     *
     * @param klipper_name Full name like "temperature_sensor mcu_temp"
     * @param[out] sensor_name Extracted short name (e.g., "mcu_temp")
     * @param[out] type Detected sensor type
     * @return true if successfully parsed as temperature sensor
     */
    bool parse_klipper_name(const std::string& klipper_name, std::string& sensor_name,
                            TemperatureSensorType& type) const;

    /**
     * @brief Find config by Klipper name
     * @return Pointer to config, or nullptr if not found
     */
    TemperatureSensorConfig* find_config(const std::string& klipper_name);
    const TemperatureSensorConfig* find_config(const std::string& klipper_name) const;

    /**
     * @brief Update all LVGL subjects from current state
     * @note Internal method - MUST only be called from main LVGL thread
     */
    void update_subjects();

    /**
     * @brief Ensure a dynamic subject exists for a sensor
     * @param klipper_name Full Klipper object name
     */
    void ensure_sensor_subject(const std::string& klipper_name);

    // Recursive mutex for thread-safe state access
    mutable std::recursive_mutex mutex_;

    // Configuration
    std::vector<TemperatureSensorConfig> sensors_;

    // Runtime state (keyed by klipper_name)
    std::map<std::string, TemperatureSensorState> states_;

    // Per-sensor dynamic subjects (keyed by klipper_name, value in centidegrees)
    std::map<std::string, std::unique_ptr<DynamicIntSubject>> temp_subjects_;

    // Test mode: when true, update_from_status() calls update_subjects() synchronously
    bool sync_mode_ = false;

    // LVGL subjects
    bool subjects_initialized_ = false;
    SubjectManager subjects_;
    lv_subject_t sensor_count_{};
};

} // namespace helix::sensors
