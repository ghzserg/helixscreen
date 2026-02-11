// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "accel_sensor_types.h"
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
 * @brief Manager for accelerometer sensors (ADXL345, LIS2DW, LIS3DH, MPU9250, ICM20948)
 *
 * Implements ISensorManager interface for integration with SensorRegistry.
 * Provides:
 * - Auto-discovery of accelerometer sensors from Klipper objects list
 * - Role assignment for input shaping
 * - Real-time state tracking from Moonraker updates
 * - LVGL subjects for reactive UI binding
 *
 * Thread-safe for state updates from Moonraker callbacks.
 *
 * Klipper object names:
 * - adxl345 [name]
 * - lis2dw [name]
 * - lis3dh [name]
 * - mpu9250 [name]
 * - icm20948 [name]
 *
 * Status JSON format:
 * @code
 * {
 *   "adxl345": {
 *     "connected": true
 *   },
 *   "adxl345 bed": {
 *     "connected": true
 *   }
 * }
 * @endcode
 */
class AccelSensorManager : public ISensorManager {
  public:
    /**
     * @brief Get singleton instance
     */
    static AccelSensorManager& instance();

    // Prevent copying
    AccelSensorManager(const AccelSensorManager&) = delete;
    AccelSensorManager& operator=(const AccelSensorManager&) = delete;

    // ========================================================================
    // ISensorManager Interface
    // ========================================================================

    /// @brief Get category name for registry
    [[nodiscard]] std::string category_name() const override;

    /**
     * @brief Discover sensors from Klipper config keys (configfile.config)
     *
     * Accelerometers don't appear in printer.objects.list - they only exist
     * in configfile.config because they have no get_status() method.
     *
     * @param config_keys JSON object where keys are config section names
     * @note MUST be called from main LVGL thread (updates subjects directly)
     */
    void discover_from_config(const nlohmann::json& config_keys) override;

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
    [[nodiscard]] std::vector<AccelSensorConfig> get_sensors() const;

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
    void set_sensor_role(const std::string& klipper_name, AccelSensorRole role);

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
    [[nodiscard]] std::optional<AccelSensorState> get_sensor_state(AccelSensorRole role) const;

    /**
     * @brief Check if a sensor is available (exists and enabled)
     *
     * @param role The sensor role to check
     * @return true if sensor exists, is enabled, and is available in Klipper
     */
    [[nodiscard]] bool is_sensor_available(AccelSensorRole role) const;

    /**
     * @brief Check if the input shaper accelerometer is connected
     *
     * @return true if input shaper sensor is assigned and connected
     */
    [[nodiscard]] bool is_input_shaper_connected() const;

    // ========================================================================
    // LVGL Subjects
    // ========================================================================

    /**
     * @brief Get subject for accelerometer connection status
     * @return Subject (int: -1=no accel, 0=disconnected, 1=connected)
     */
    [[nodiscard]] lv_subject_t* get_connected_subject();

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

    friend class AccelSensorManagerTestAccess;

  private:
    AccelSensorManager();
    ~AccelSensorManager();

    /**
     * @brief Parse Klipper object name to determine if it's an accelerometer
     *
     * @param klipper_name Full name like "adxl345" or "adxl345 bed"
     * @param[out] sensor_name Extracted short name
     * @param[out] type Detected sensor type
     * @return true if successfully parsed as accelerometer
     */
    bool parse_klipper_name(const std::string& klipper_name, std::string& sensor_name,
                            AccelSensorType& type) const;

    /**
     * @brief Find config by Klipper name
     * @return Pointer to config, or nullptr if not found
     */
    AccelSensorConfig* find_config(const std::string& klipper_name);
    const AccelSensorConfig* find_config(const std::string& klipper_name) const;

    /**
     * @brief Find config by assigned role
     * @return Pointer to config, or nullptr if no sensor has this role
     */
    const AccelSensorConfig* find_config_by_role(AccelSensorRole role) const;

    /**
     * @brief Update all LVGL subjects from current state
     * @note Internal method - MUST only be called from main LVGL thread
     */
    void update_subjects();

    // Recursive mutex for thread-safe state access
    mutable std::recursive_mutex mutex_;

    // Configuration
    std::vector<AccelSensorConfig> sensors_;

    // Runtime state (keyed by klipper_name)
    std::map<std::string, AccelSensorState> states_;

    // Test mode: when true, update_from_status() calls update_subjects() synchronously
    bool sync_mode_ = false;

    // LVGL subjects
    bool subjects_initialized_ = false;
    SubjectManager subjects_;
    lv_subject_t connected_;
    lv_subject_t sensor_count_;
};

} // namespace helix::sensors
