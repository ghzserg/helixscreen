// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "hv/json.hpp"

namespace helix::sensors {

/// @brief Interface for sensor category managers
class ISensorManager {
  public:
    virtual ~ISensorManager() = default;

    /// @brief Get the category name (e.g., "switch", "humidity")
    [[nodiscard]] virtual std::string category_name() const = 0;

    /// @brief Discover sensors from Klipper object list
    virtual void discover(const std::vector<std::string>& klipper_objects) = 0;

    /// @brief Update state from Moonraker status JSON
    virtual void update_from_status(const nlohmann::json& status) = 0;

    /// @brief Load configuration from JSON
    virtual void load_config(const nlohmann::json& config) = 0;

    /// @brief Save configuration to JSON
    [[nodiscard]] virtual nlohmann::json save_config() const = 0;
};

/// @brief Central registry for all sensor managers
class SensorRegistry {
  public:
    SensorRegistry() = default;
    ~SensorRegistry() = default;

    // Non-copyable, non-movable (contains mutex)
    SensorRegistry(const SensorRegistry&) = delete;
    SensorRegistry& operator=(const SensorRegistry&) = delete;
    SensorRegistry(SensorRegistry&&) = delete;
    SensorRegistry& operator=(SensorRegistry&&) = delete;

    /// @brief Register a sensor manager
    void register_manager(std::string category, std::unique_ptr<ISensorManager> manager);

    /// @brief Get a manager by category name
    [[nodiscard]] ISensorManager* get_manager(const std::string& category) const;

    /// @brief Discover sensors in all registered managers
    void discover_all(const std::vector<std::string>& klipper_objects);

    /// @brief Route status update to all managers
    void update_all_from_status(const nlohmann::json& status);

    /// @brief Load config for all managers
    void load_config(const nlohmann::json& root_config);

    /// @brief Save config from all managers
    [[nodiscard]] nlohmann::json save_config() const;

  private:
    mutable std::recursive_mutex mutex_;
    std::map<std::string, std::unique_ptr<ISensorManager>> managers_;
};

} // namespace helix::sensors
