// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugin_registry.h"

#include "spdlog/spdlog.h"

namespace helix::plugin {

// ============================================================================
// PluginRegistry Implementation
// ============================================================================

PluginRegistry& PluginRegistry::instance() {
    static PluginRegistry instance;
    return instance;
}

void PluginRegistry::register_service(const std::string& name, void* service) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = services_.find(name);
    if (it != services_.end()) {
        spdlog::warn("[plugin] Service '{}' already registered, overwriting", name);
    }

    services_[name] = service;
    spdlog::debug("[plugin] Service registered: {}", name);
}

bool PluginRegistry::unregister_service(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = services_.find(name);
    if (it != services_.end()) {
        services_.erase(it);
        spdlog::debug("[plugin] Service unregistered: {}", name);
        return true;
    }

    return false;
}

void* PluginRegistry::get_service(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = services_.find(name);
    if (it != services_.end()) {
        return it->second;
    }

    return nullptr;
}

bool PluginRegistry::has_service(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return services_.find(name) != services_.end();
}

size_t PluginRegistry::service_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return services_.size();
}

void PluginRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    services_.clear();
    spdlog::debug("[plugin] All services cleared from registry");
}

} // namespace helix::plugin
