// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "color_sensor_types.h"
#include "lvgl.h"
#include "sensor_registry.h"
#include "subject_managed_panel.h"

#include <array>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace helix::sensors {

/**
 * @brief Manager for TD-1 color sensors
 *
 * Implements ISensorManager interface for integration with SensorRegistry.
 * Provides:
 * - Discovery of TD-1 devices by device ID
 * - Role assignment for filament color detection
 * - Real-time state tracking from Moonraker TD-1 updates
 * - LVGL subjects for reactive UI binding
 *
 * Thread-safe for state updates from Moonraker callbacks.
 *
 * Device IDs: td1_lane0, td1_lane1, etc.
 *
 * Status JSON format (from Moonraker):
 * @code
 * {
 *   "td1_lane0": {
 *     "color": "#FF5733",
 *     "td": 1.25
 *   }
 * }
 * @endcode
 */
class ColorSensorManager : public ISensorManager {
  public:
    /**
     * @brief Get singleton instance
     */
    static ColorSensorManager& instance();

    // Prevent copying
    ColorSensorManager(const ColorSensorManager&) = delete;
    ColorSensorManager& operator=(const ColorSensorManager&) = delete;

    // ========================================================================
    // ISensorManager Interface
    // ========================================================================

    /// @brief Get category name for registry
    [[nodiscard]] std::string category_name() const override;

    /**
     * @brief Discover sensors from Moonraker API info
     *
     * Color sensors (TD-1) come from Moonraker, not Klipper. The Moonraker
     * info should contain a "td1_devices" array with device IDs.
     *
     * @param moonraker_info JSON object with "td1_devices" array
     * @note MUST be called from main LVGL thread (updates subjects directly)
     */
    void discover_from_moonraker(const nlohmann::json& moonraker_info) override;

    /// @brief Update state from Moonraker TD-1 status JSON
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
    [[nodiscard]] std::vector<ColorSensorConfig> get_sensors() const;

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
     * @param device_id Device ID (e.g., "td1_lane0")
     * @param role New role assignment
     */
    void set_sensor_role(const std::string& device_id, ColorSensorRole role);

    /**
     * @brief Enable or disable a sensor
     * @note MUST be called from main LVGL thread (updates subjects directly)
     *
     * @param device_id Device ID
     * @param enabled Whether sensor should be monitored
     */
    void set_sensor_enabled(const std::string& device_id, bool enabled);

    // ========================================================================
    // State Queries
    // ========================================================================

    /**
     * @brief Get current state for a sensor by role (thread-safe copy)
     *
     * @param role The sensor role to query
     * @return State copy if sensor assigned to role, empty optional otherwise
     */
    [[nodiscard]] std::optional<ColorSensorState> get_sensor_state(ColorSensorRole role) const;

    /**
     * @brief Check if a sensor is available (exists and enabled)
     *
     * @param role The sensor role to check
     * @return true if sensor exists, is enabled, and is available
     */
    [[nodiscard]] bool is_sensor_available(ColorSensorRole role) const;

    /**
     * @brief Get current filament color hex for FILAMENT_COLOR role
     *
     * @return Color hex string (e.g., "#FF5733"), or empty if no sensor assigned
     */
    [[nodiscard]] std::string get_filament_color_hex() const;

    // ========================================================================
    // LVGL Subjects
    // ========================================================================

    /**
     * @brief Get subject for filament color hex
     * @return Subject (string: "#RRGGBB" or empty if no sensor assigned)
     */
    [[nodiscard]] lv_subject_t* get_color_hex_subject();

    /**
     * @brief Get subject for TD value
     * @return Subject (int: TD x 100, -1 if no sensor assigned)
     */
    [[nodiscard]] lv_subject_t* get_td_value_subject();

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

    friend class ColorSensorManagerTestAccess;

  private:
    ColorSensorManager();
    ~ColorSensorManager();

    /**
     * @brief Generate display name from device ID
     *
     * @param device_id Device ID like "td1_lane0"
     * @return Display name like "TD-1 Lane 0"
     */
    [[nodiscard]] std::string generate_display_name(const std::string& device_id) const;

    /**
     * @brief Find config by device ID
     * @return Pointer to config, or nullptr if not found
     */
    ColorSensorConfig* find_config(const std::string& device_id);
    const ColorSensorConfig* find_config(const std::string& device_id) const;

    /**
     * @brief Find config by assigned role
     * @return Pointer to config, or nullptr if no sensor has this role
     */
    const ColorSensorConfig* find_config_by_role(ColorSensorRole role) const;

    /**
     * @brief Update all LVGL subjects from current state
     * @note Internal method - MUST only be called from main LVGL thread
     */
    void update_subjects();

    // Recursive mutex for thread-safe state access
    mutable std::recursive_mutex mutex_;

    // Configuration
    std::vector<ColorSensorConfig> sensors_;

    // Runtime state (keyed by device_id)
    std::map<std::string, ColorSensorState> states_;

    // Test mode: when true, update_from_status() calls update_subjects() synchronously
    bool sync_mode_ = false;

    // LVGL subjects
    bool subjects_initialized_ = false;
    SubjectManager subjects_;
    lv_subject_t color_hex_;
    lv_subject_t td_value_;
    lv_subject_t sensor_count_;

    // Buffer for string subject
    static constexpr size_t COLOR_HEX_BUF_SIZE = 16;
    std::array<char, COLOR_HEX_BUF_SIZE> color_hex_buf_;
};

} // namespace helix::sensors
