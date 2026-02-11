// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "humidity_sensor_types.h"
#include "lvgl.h"
#include "sensor_registry.h"
#include "subject_managed_panel.h"

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace helix::sensors {

/**
 * @brief Manager for humidity sensors (BME280 and HTU21D)
 *
 * Implements ISensorManager interface for integration with SensorRegistry.
 * Provides:
 * - Auto-discovery of humidity sensors from Klipper objects list
 * - Role assignment for CHAMBER and DRYER monitoring
 * - Real-time state tracking from Moonraker updates
 * - LVGL subjects for reactive UI binding
 *
 * Thread-safe for state updates from Moonraker callbacks.
 *
 * Klipper object names:
 * - bme280 <name>   - BME280 sensor (humidity, pressure, temperature)
 * - htu21d <name>   - HTU21D sensor (humidity, temperature)
 *
 * Status JSON format:
 * @code
 * {
 *   "bme280 chamber": {
 *     "humidity": 45.5,
 *     "pressure": 1013.25,
 *     "temperature": 25.3
 *   },
 *   "htu21d dryer": {
 *     "humidity": 20.1,
 *     "temperature": 55.0
 *   }
 * }
 * @endcode
 */
class HumiditySensorManager : public ISensorManager {
  public:
    /**
     * @brief Get singleton instance
     */
    static HumiditySensorManager& instance();

    // Prevent copying
    HumiditySensorManager(const HumiditySensorManager&) = delete;
    HumiditySensorManager& operator=(const HumiditySensorManager&) = delete;

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
    [[nodiscard]] std::vector<HumiditySensorConfig> get_sensors() const;

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
    void set_sensor_role(const std::string& klipper_name, HumiditySensorRole role);

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
     * @brief Get current state for a sensor by role (thread-safe copy)
     *
     * @param role The sensor role to query
     * @return State copy if sensor assigned to role, empty optional otherwise
     */
    [[nodiscard]] std::optional<HumiditySensorState>
    get_sensor_state(HumiditySensorRole role) const;

    /**
     * @brief Check if a sensor is available (exists and enabled)
     *
     * @param role The sensor role to check
     * @return true if sensor exists, is enabled, and is available in Klipper
     */
    [[nodiscard]] bool is_sensor_available(HumiditySensorRole role) const;

    // ========================================================================
    // LVGL Subjects
    // ========================================================================

    /**
     * @brief Get subject for chamber humidity
     * @return Subject (int: humidity x 10, -1 if no chamber sensor)
     */
    [[nodiscard]] lv_subject_t* get_chamber_humidity_subject();

    /**
     * @brief Get subject for chamber pressure
     * @return Subject (int: pressure in Pa, -1 if no chamber sensor)
     */
    [[nodiscard]] lv_subject_t* get_chamber_pressure_subject();

    /**
     * @brief Get subject for dryer humidity
     * @return Subject (int: humidity x 10, -1 if no dryer sensor)
     */
    [[nodiscard]] lv_subject_t* get_dryer_humidity_subject();

    /**
     * @brief Get subject for sensor count (for conditional UI visibility)
     * @return Subject (int: number of discovered sensors)
     */
    [[nodiscard]] lv_subject_t* get_sensor_count_subject();

    /**
     * @brief Get subject for chamber humidity text (formatted as "45%")
     * @return Subject (string: formatted humidity or "--" if no sensor)
     */
    [[nodiscard]] lv_subject_t* get_chamber_humidity_text_subject();

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

    friend class HumiditySensorManagerTestAccess;

  private:
    HumiditySensorManager();
    ~HumiditySensorManager();

    /**
     * @brief Parse Klipper object name to determine if it's a humidity sensor
     *
     * @param klipper_name Full name like "bme280 chamber" or "htu21d dryer"
     * @param[out] sensor_name Extracted short name (e.g., "chamber")
     * @param[out] type Detected sensor type
     * @return true if successfully parsed as humidity sensor
     */
    bool parse_klipper_name(const std::string& klipper_name, std::string& sensor_name,
                            HumiditySensorType& type) const;

    /**
     * @brief Find config by Klipper name
     * @return Pointer to config, or nullptr if not found
     */
    HumiditySensorConfig* find_config(const std::string& klipper_name);
    const HumiditySensorConfig* find_config(const std::string& klipper_name) const;

    /**
     * @brief Find config by assigned role
     * @return Pointer to config, or nullptr if no sensor has this role
     */
    const HumiditySensorConfig* find_config_by_role(HumiditySensorRole role) const;

    /**
     * @brief Update all LVGL subjects from current state
     * @note Internal method - MUST only be called from main LVGL thread
     */
    void update_subjects();

    // Recursive mutex for thread-safe state access
    mutable std::recursive_mutex mutex_;

    // Configuration
    std::vector<HumiditySensorConfig> sensors_;

    // Runtime state (keyed by klipper_name)
    std::map<std::string, HumiditySensorState> states_;

    // Test mode: when true, update_from_status() calls update_subjects() synchronously
    bool sync_mode_ = false;

    // LVGL subjects
    bool subjects_initialized_ = false;
    SubjectManager subjects_;
    lv_subject_t chamber_humidity_;
    lv_subject_t chamber_pressure_;
    lv_subject_t dryer_humidity_;
    lv_subject_t sensor_count_;
    lv_subject_t chamber_humidity_text_;
    char chamber_humidity_text_buf_[8]; ///< "45%" or "--"
};

} // namespace helix::sensors
