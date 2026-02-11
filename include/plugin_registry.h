// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file plugin_registry.h
 * @brief Service locator for plugin-to-plugin communication
 *
 * Provides a thread-safe registry for plugins to expose services to other
 * plugins. Services are registered by name and can be retrieved by any
 * plugin that knows the service interface.
 *
 * @pattern Service Locator
 * @threading Thread-safe for all operations
 *
 * @see plugin_api.h for plugin-facing API
 */

#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

namespace helix::plugin {

/**
 * @brief Service registry singleton
 *
 * Allows plugins to register services that other plugins can discover
 * and use. Services are identified by name and stored as void pointers.
 * Type safety is the caller's responsibility.
 *
 * Usage example:
 * @code
 * // Plugin A registers a service
 * class LedController { ... };
 * LedController controller;
 * PluginRegistry::instance().register_service("led_controller", &controller);
 *
 * // Plugin B retrieves the service
 * auto* led = PluginRegistry::instance().get<LedController>("led_controller");
 * if (led) {
 *     led->set_color(0xFF0000);
 * }
 * @endcode
 *
 * Thread safety: All methods are thread-safe.
 */
class PluginRegistry {
  public:
    /**
     * @brief Get singleton instance
     */
    static PluginRegistry& instance();

    // Non-copyable singleton
    PluginRegistry(const PluginRegistry&) = delete;
    PluginRegistry& operator=(const PluginRegistry&) = delete;

    /**
     * @brief Register a service by name
     *
     * Overwrites any existing service with the same name.
     *
     * @param name Service identifier (convention: "plugin_id.service_name")
     * @param service Pointer to service instance (caller owns memory)
     */
    void register_service(const std::string& name, void* service);

    /**
     * @brief Unregister a service
     *
     * @param name Service identifier
     * @return true if service was found and removed
     */
    bool unregister_service(const std::string& name);

    /**
     * @brief Get a service by name (raw pointer)
     *
     * @param name Service identifier
     * @return Service pointer, or nullptr if not found
     */
    void* get_service(const std::string& name) const;

    /**
     * @brief Get a service by name with type casting
     *
     * Convenience template for type-safe service retrieval.
     *
     * @tparam T Service type
     * @param name Service identifier
     * @return Typed service pointer, or nullptr if not found
     */
    template <typename T> T* get(const std::string& name) const {
        return static_cast<T*>(get_service(name));
    }

    /**
     * @brief Check if a service is registered
     *
     * @param name Service identifier
     * @return true if service exists
     */
    bool has_service(const std::string& name) const;

    /**
     * @brief Get count of registered services (for testing/debugging)
     */
    size_t service_count() const;

    /**
     * @brief Clear all registered services (for testing/shutdown)
     */
    void clear();

  private:
    friend class PluginRegistryTestAccess;
    PluginRegistry() = default;

    std::unordered_map<std::string, void*> services_;
    mutable std::mutex mutex_;
};

} // namespace helix::plugin
