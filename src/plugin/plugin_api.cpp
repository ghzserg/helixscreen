// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2025 HelixScreen

#include "plugin_api.h"

#include "lvgl.h"
#include "moonraker_client.h"
#include "plugin_registry.h"
#include "printer_state.h"
#include "spdlog/spdlog.h"

namespace helix::plugin {

// ============================================================================
// PluginAPI Implementation
// ============================================================================

PluginAPI::PluginAPI(MoonrakerAPI* api, MoonrakerClient* client, PrinterState& state,
                     Config* config, const std::string& plugin_id)
    : moonraker_api_(api), moonraker_client_(client), printer_state_(state), config_(config),
      plugin_id_(plugin_id) {
    spdlog::debug("[plugin:{}] API instance created", plugin_id_);
}

PluginAPI::~PluginAPI() {
    cleanup();
    spdlog::debug("[plugin:{}] API instance destroyed", plugin_id_);
}

// ============================================================================
// Event System
// ============================================================================

EventSubscriptionId PluginAPI::on_event(const std::string& event_name, EventCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);

    EventSubscriptionId id = EventDispatcher::instance().subscribe(event_name, std::move(callback));
    event_subscriptions_.push_back(id);

    spdlog::debug("[plugin:{}] Subscribed to event: {}", plugin_id_, event_name);
    return id;
}

bool PluginAPI::off_event(EventSubscriptionId id) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Remove from our tracking list
    auto it = std::find(event_subscriptions_.begin(), event_subscriptions_.end(), id);
    if (it != event_subscriptions_.end()) {
        event_subscriptions_.erase(it);
    }

    return EventDispatcher::instance().unsubscribe(id);
}

// ============================================================================
// Moonraker Subscription
// ============================================================================

MoonrakerSubscriptionId PluginAPI::subscribe_moonraker(const std::vector<std::string>& objects,
                                                       MoonrakerCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);

    MoonrakerSubscriptionId id = next_moonraker_sub_id_++;

    // Check if Moonraker is connected
    if (moonraker_client_ != nullptr) {
        // Subscribe immediately
        // Note: MoonrakerClient::register_notify_update returns a SubscriptionId
        // We wrap the callback to filter for our subscribed objects
        // The returned sub_id from MoonrakerClient is stored for potential future
        // unsubscription, but currently we track our own IDs separately
        (void)moonraker_client_->register_notify_update([callback, objects](const json& update) {
            // Filter update to only include objects we subscribed to
            // The update is in format: { "object_name": { ... }, ... }
            json filtered;
            for (const auto& obj : objects) {
                if (update.contains(obj)) {
                    filtered[obj] = update[obj];
                }
            }
            if (!filtered.empty()) {
                callback(filtered);
            }
        });

        // Track our internal subscription ID
        active_moonraker_subscriptions_.push_back(id);
        spdlog::debug("[plugin:{}] Moonraker subscription active (id={})", plugin_id_, id);
    } else {
        // Queue for later when Moonraker connects
        deferred_subscriptions_.push_back({id, objects, callback});
        spdlog::debug("[plugin:{}] Moonraker subscription deferred (id={})", plugin_id_, id);
    }

    return id;
}

bool PluginAPI::unsubscribe_moonraker(MoonrakerSubscriptionId id) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check deferred subscriptions
    auto deferred_it = std::find_if(deferred_subscriptions_.begin(), deferred_subscriptions_.end(),
                                    [id](const DeferredSubscription& sub) { return sub.id == id; });

    if (deferred_it != deferred_subscriptions_.end()) {
        deferred_subscriptions_.erase(deferred_it);
        spdlog::debug("[plugin:{}] Deferred Moonraker subscription removed (id={})", plugin_id_,
                      id);
        return true;
    }

    // Check active subscriptions
    auto active_it = std::find(active_moonraker_subscriptions_.begin(),
                               active_moonraker_subscriptions_.end(), id);

    if (active_it != active_moonraker_subscriptions_.end()) {
        active_moonraker_subscriptions_.erase(active_it);
        // Note: We don't have a way to unsubscribe from MoonrakerClient's
        // register_notify_update currently. The subscription will remain
        // until the client is destroyed. This is a known limitation.
        spdlog::debug("[plugin:{}] Active Moonraker subscription marked removed (id={})",
                      plugin_id_, id);
        return true;
    }

    return false;
}

// ============================================================================
// Subject Registration
// ============================================================================

void PluginAPI::register_subject(const std::string& name, lv_subject_t* subject) {
    std::lock_guard<std::mutex> lock(mutex_);

    // TODO: Integrate with LVGL XML subject registration system
    // For now, just track the name for cleanup. The subject pointer will be used
    // in Phase 2 when UI integration is implemented.
    (void)subject;
    registered_subjects_.push_back(name);

    spdlog::debug("[plugin:{}] Subject registered: {}", plugin_id_, name);
}

bool PluginAPI::unregister_subject(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find(registered_subjects_.begin(), registered_subjects_.end(), name);
    if (it != registered_subjects_.end()) {
        registered_subjects_.erase(it);
        // TODO: Unregister from LVGL XML system
        spdlog::debug("[plugin:{}] Subject unregistered: {}", plugin_id_, name);
        return true;
    }

    return false;
}

// ============================================================================
// Service Registration
// ============================================================================

void PluginAPI::register_service(const std::string& name, void* service) {
    std::lock_guard<std::mutex> lock(mutex_);

    PluginRegistry::instance().register_service(name, service);
    registered_services_.push_back(name);

    spdlog::debug("[plugin:{}] Service registered: {}", plugin_id_, name);
}

bool PluginAPI::unregister_service(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find(registered_services_.begin(), registered_services_.end(), name);
    if (it != registered_services_.end()) {
        registered_services_.erase(it);
        PluginRegistry::instance().unregister_service(name);
        spdlog::debug("[plugin:{}] Service unregistered: {}", plugin_id_, name);
        return true;
    }

    return false;
}

void* PluginAPI::get_service(const std::string& name) const {
    return PluginRegistry::instance().get_service(name);
}

// ============================================================================
// Logging
// ============================================================================

void PluginAPI::log_info(const std::string& message) const {
    spdlog::info("[plugin:{}] {}", plugin_id_, message);
}

void PluginAPI::log_warn(const std::string& message) const {
    spdlog::warn("[plugin:{}] {}", plugin_id_, message);
}

void PluginAPI::log_error(const std::string& message) const {
    spdlog::error("[plugin:{}] {}", plugin_id_, message);
}

void PluginAPI::log_debug(const std::string& message) const {
    spdlog::debug("[plugin:{}] {}", plugin_id_, message);
}

// ============================================================================
// UI Injection
// ============================================================================

bool PluginAPI::inject_widget(const std::string& point_id, const std::string& xml_component,
                              const WidgetCallbacks& callbacks) {
    // Delegate to InjectionPointManager with our plugin ID
    bool success = InjectionPointManager::instance().inject_widget(plugin_id_, point_id,
                                                                   xml_component, callbacks);

    if (success) {
        spdlog::info("[plugin:{}] Injected widget '{}' into '{}'", plugin_id_, xml_component,
                     point_id);
    } else {
        spdlog::error("[plugin:{}] Failed to inject widget '{}' into '{}'", plugin_id_,
                      xml_component, point_id);
    }

    return success;
}

bool PluginAPI::register_xml_component(const std::string& plugin_dir, const std::string& filename) {
    // Build full path to the XML file
    std::string full_path = plugin_dir;
    if (!full_path.empty() && full_path.back() != '/') {
        full_path += '/';
    }
    full_path += filename;

    // Derive component name from filename (strip .xml extension)
    std::string component_name = filename;
    size_t ext_pos = component_name.rfind(".xml");
    if (ext_pos != std::string::npos) {
        component_name = component_name.substr(0, ext_pos);
    }

    // Register with LVGL XML system
    // Note: lv_xml_register_component_from_file expects the file path
    bool success = lv_xml_register_component_from_file(full_path.c_str());

    if (success) {
        spdlog::info("[plugin:{}] Registered XML component '{}' from '{}'", plugin_id_,
                     component_name, full_path);
    } else {
        spdlog::error("[plugin:{}] Failed to register XML component from '{}'", plugin_id_,
                      full_path);
    }

    return success;
}

bool PluginAPI::has_injection_point(const std::string& point_id) const {
    return InjectionPointManager::instance().has_point(point_id);
}

// ============================================================================
// Internal Methods
// ============================================================================

void PluginAPI::set_moonraker(MoonrakerAPI* api, MoonrakerClient* client) {
    std::lock_guard<std::mutex> lock(mutex_);
    moonraker_api_ = api;
    moonraker_client_ = client;
    spdlog::debug("[plugin:{}] Moonraker services updated", plugin_id_);
}

void PluginAPI::apply_deferred_subscriptions() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (moonraker_client_ == nullptr) {
        spdlog::warn("[plugin:{}] Cannot apply deferred subscriptions: client is null", plugin_id_);
        return;
    }

    if (deferred_subscriptions_.empty()) {
        return;
    }

    spdlog::info("[plugin:{}] Applying {} deferred Moonraker subscriptions", plugin_id_,
                 deferred_subscriptions_.size());

    for (auto& sub : deferred_subscriptions_) {
        // Now apply the subscription
        auto callback = sub.callback;
        auto objects = sub.objects;

        moonraker_client_->register_notify_update([callback, objects](const json& update) {
            json filtered;
            for (const auto& obj : objects) {
                if (update.contains(obj)) {
                    filtered[obj] = update[obj];
                }
            }
            if (!filtered.empty()) {
                callback(filtered);
            }
        });

        active_moonraker_subscriptions_.push_back(sub.id);
    }

    deferred_subscriptions_.clear();
}

void PluginAPI::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Unsubscribe from all events
    for (auto id : event_subscriptions_) {
        EventDispatcher::instance().unsubscribe(id);
    }
    event_subscriptions_.clear();

    // Clear Moonraker subscriptions
    // Note: Actual MoonrakerClient subscriptions persist until client destruction
    deferred_subscriptions_.clear();
    active_moonraker_subscriptions_.clear();

    // Unregister all services
    for (const auto& name : registered_services_) {
        PluginRegistry::instance().unregister_service(name);
    }
    registered_services_.clear();

    // Clear subjects
    // TODO: Unregister from LVGL XML system when implemented
    registered_subjects_.clear();

    spdlog::debug("[plugin:{}] Cleanup complete", plugin_id_);
}

} // namespace helix::plugin
