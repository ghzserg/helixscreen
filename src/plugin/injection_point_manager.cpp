// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "injection_point_manager.h"

#include "ui_utils.h"

#include "spdlog/spdlog.h"

#include <algorithm>

// LVGL includes
#include "lvgl.h"

namespace helix::plugin {

// ============================================================================
// Singleton Instance
// ============================================================================

InjectionPointManager& InjectionPointManager::instance() {
    static InjectionPointManager instance;
    return instance;
}

// ============================================================================
// Panel Registration
// ============================================================================

void InjectionPointManager::register_point(const std::string& point_id, lv_obj_t* container) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (container == nullptr) {
        spdlog::error("[InjectionPointManager] Cannot register point '{}': null container",
                      point_id);
        return;
    }

    // Check for duplicate registration
    auto it = points_.find(point_id);
    if (it != points_.end()) {
        if (it->second == container) {
            // Same container - just a duplicate call, ignore silently
            spdlog::debug(
                "[InjectionPointManager] Point '{}' already registered with same container",
                point_id);
            return;
        }
        // Different container - warn and update
        spdlog::warn("[InjectionPointManager] Point '{}' re-registered with different container",
                     point_id);
    }

    points_[point_id] = container;
    spdlog::debug("[InjectionPointManager] Registered injection point: {}", point_id);
}

void InjectionPointManager::unregister_point(const std::string& point_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = points_.find(point_id);
    if (it == points_.end()) {
        spdlog::debug("[InjectionPointManager] Point '{}' not registered, nothing to unregister",
                      point_id);
        return;
    }

    // Remove from points map
    points_.erase(it);

    // Remove tracking for any widgets that were in this point
    // Note: The actual LVGL widgets are deleted by LVGL when the container is deleted
    // We just remove our tracking records
    auto widget_it = injected_widgets_.begin();
    while (widget_it != injected_widgets_.end()) {
        if (widget_it->injection_point == point_id) {
            spdlog::debug("[InjectionPointManager] Removing tracking for widget from unregistered "
                          "point '{}' (plugin: {})",
                          point_id, widget_it->plugin_id);
            // Invalidate pointer before removal as a defensive measure
            // Prevents any racing code from using a potentially stale pointer
            widget_it->widget = nullptr;
            widget_it = injected_widgets_.erase(widget_it);
        } else {
            ++widget_it;
        }
    }

    spdlog::info("[InjectionPointManager] Unregistered injection point: {}", point_id);
}

// ============================================================================
// Plugin Injection
// ============================================================================

bool InjectionPointManager::inject_widget(const std::string& plugin_id, const std::string& point_id,
                                          const std::string& xml_component,
                                          const WidgetCallbacks& callbacks) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Find the injection point container
    auto it = points_.find(point_id);
    if (it == points_.end()) {
        spdlog::error("[InjectionPointManager] Cannot inject into '{}': point not registered",
                      point_id);
        return false;
    }

    lv_obj_t* container = it->second;
    if (container == nullptr) {
        spdlog::error("[InjectionPointManager] Cannot inject into '{}': container is null",
                      point_id);
        return false;
    }

    // Create widget from XML component
    // Note: lv_xml_create returns void* so we cast to lv_obj_t*
    lv_obj_t* widget =
        static_cast<lv_obj_t*>(lv_xml_create(container, xml_component.c_str(), nullptr));

    if (widget == nullptr) {
        spdlog::error("[InjectionPointManager] Failed to create XML component '{}' for plugin '{}'",
                      xml_component, plugin_id);
        return false;
    }

    // Track the injected widget
    InjectedWidget injected;
    injected.plugin_id = plugin_id;
    injected.injection_point = point_id;
    injected.component_name = xml_component;
    injected.widget = widget;
    injected.callbacks = callbacks;

    injected_widgets_.push_back(injected);

    spdlog::info("[InjectionPointManager] Plugin '{}' injected '{}' into '{}'", plugin_id,
                 xml_component, point_id);

    // Invoke on_create callback AFTER widget is added to container
    if (callbacks.on_create) {
        try {
            callbacks.on_create(widget);
        } catch (const std::exception& e) {
            spdlog::error("[InjectionPointManager] on_create callback threw exception for plugin "
                          "'{}': {}",
                          plugin_id, e.what());
        }
    }

    return true;
}

void InjectionPointManager::remove_plugin_widgets(const std::string& plugin_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Collect widgets to remove (can't modify while iterating)
    std::vector<InjectedWidget> to_remove;
    for (const auto& injected : injected_widgets_) {
        if (injected.plugin_id == plugin_id) {
            to_remove.push_back(injected);
        }
    }

    if (to_remove.empty()) {
        spdlog::debug("[InjectionPointManager] No widgets to remove for plugin '{}'", plugin_id);
        return;
    }

    spdlog::info("[InjectionPointManager] Removing {} widget(s) for plugin '{}'", to_remove.size(),
                 plugin_id);

    // Remove each widget
    for (const auto& injected : to_remove) {
        // Invoke on_destroy callback BEFORE deleting widget
        if (injected.callbacks.on_destroy && injected.widget != nullptr) {
            try {
                injected.callbacks.on_destroy(injected.widget);
            } catch (const std::exception& e) {
                spdlog::error("[InjectionPointManager] on_destroy callback threw exception for "
                              "plugin '{}': {}",
                              plugin_id, e.what());
            }
        }

        // Delete the LVGL widget (LVGL handles child cleanup)
        // Use local copy since lv_obj_safe_delete takes ref-to-pointer for auto-null
        lv_obj_t* widget = injected.widget;
        if (lv_obj_safe_delete(widget)) {
            spdlog::debug("[InjectionPointManager] Deleted widget '{}' from point '{}'",
                          injected.component_name, injected.injection_point);
        }
    }

    // Remove from tracking list
    auto it = injected_widgets_.begin();
    while (it != injected_widgets_.end()) {
        if (it->plugin_id == plugin_id) {
            it = injected_widgets_.erase(it);
        } else {
            ++it;
        }
    }
}

bool InjectionPointManager::remove_widget(lv_obj_t* widget) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find_if(injected_widgets_.begin(), injected_widgets_.end(),
                           [widget](const InjectedWidget& w) { return w.widget == widget; });

    if (it == injected_widgets_.end()) {
        spdlog::debug("[InjectionPointManager] Widget not found in tracking list");
        return false;
    }

    // Invoke on_destroy callback
    if (it->callbacks.on_destroy && widget != nullptr) {
        try {
            it->callbacks.on_destroy(widget);
        } catch (const std::exception& e) {
            spdlog::error("[InjectionPointManager] on_destroy callback threw exception: {}",
                          e.what());
        }
    }

    // Delete the widget
    lv_obj_safe_delete(widget);

    std::string component = it->component_name;
    std::string point = it->injection_point;

    injected_widgets_.erase(it);

    spdlog::debug("[InjectionPointManager] Removed widget '{}' from point '{}'", component, point);
    return true;
}

// ============================================================================
// Query Methods
// ============================================================================

bool InjectionPointManager::has_point(const std::string& point_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return points_.find(point_id) != points_.end();
}

std::vector<std::string> InjectionPointManager::get_registered_points() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;
    result.reserve(points_.size());

    for (const auto& [point_id, container] : points_) {
        result.push_back(point_id);
    }

    return result;
}

std::vector<InjectedWidget>
InjectionPointManager::get_plugin_widgets(const std::string& plugin_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<InjectedWidget> result;

    for (const auto& injected : injected_widgets_) {
        if (injected.plugin_id == plugin_id) {
            result.push_back(injected);
        }
    }

    return result;
}

size_t InjectionPointManager::get_widget_count(const std::string& point_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    size_t count = 0;
    for (const auto& injected : injected_widgets_) {
        if (injected.injection_point == point_id) {
            ++count;
        }
    }

    return count;
}

} // namespace helix::plugin
