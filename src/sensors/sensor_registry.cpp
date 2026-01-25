// SPDX-License-Identifier: GPL-3.0-or-later
#include "sensor_registry.h"

#include <spdlog/spdlog.h>

namespace helix::sensors {

void SensorRegistry::register_manager(std::string category,
                                      std::unique_ptr<ISensorManager> manager) {
    if (!manager) {
        spdlog::warn("[SensorRegistry] Attempted to register null manager for category '{}'",
                     category);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    spdlog::info("[SensorRegistry] Registering sensor manager: {}", category);
    managers_[std::move(category)] = std::move(manager);
}

ISensorManager* SensorRegistry::get_manager(const std::string& category) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = managers_.find(category);
    if (it != managers_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void SensorRegistry::discover_all(const std::vector<std::string>& klipper_objects) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    spdlog::debug("[SensorRegistry] Discovering sensors in {} managers", managers_.size());
    for (auto& [category, manager] : managers_) {
        try {
            manager->discover(klipper_objects);
        } catch (const std::exception& e) {
            spdlog::error("[SensorRegistry] Exception during discover for '{}': {}", category,
                          e.what());
        }
    }
}

void SensorRegistry::update_all_from_status(const nlohmann::json& status) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (auto& [category, manager] : managers_) {
        try {
            manager->update_from_status(status);
        } catch (const std::exception& e) {
            spdlog::error("[SensorRegistry] Exception during status update for '{}': {}", category,
                          e.what());
        }
    }
}

void SensorRegistry::load_config(const nlohmann::json& root_config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!root_config.contains("sensors")) {
        spdlog::debug("[SensorRegistry] No 'sensors' key in config, skipping load");
        return;
    }

    spdlog::debug("[SensorRegistry] Loading config");
    const auto& sensors_config = root_config["sensors"];
    int loaded_count = 0;
    for (auto& [category, manager] : managers_) {
        if (sensors_config.contains(category)) {
            try {
                manager->load_config(sensors_config[category]);
                loaded_count++;
            } catch (const std::exception& e) {
                spdlog::warn("[SensorRegistry] Failed to load config for '{}': {}", category,
                             e.what());
            }
        }
    }
    spdlog::info("[SensorRegistry] Config loaded for {} of {} managers", loaded_count,
                 managers_.size());
}

nlohmann::json SensorRegistry::save_config() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    spdlog::debug("[SensorRegistry] Saving config for {} managers", managers_.size());

    nlohmann::json result;
    nlohmann::json sensors_config;

    for (const auto& [category, manager] : managers_) {
        try {
            sensors_config[category] = manager->save_config();
        } catch (const std::exception& e) {
            spdlog::warn("[SensorRegistry] Failed to save config for '{}': {}", category, e.what());
        }
    }

    result["sensors"] = sensors_config;
    return result;
}

} // namespace helix::sensors
