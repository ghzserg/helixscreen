// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file injection_point_manager.h
 * @brief Manages UI injection points for plugin widget injection
 *
 * This singleton manages named injection point containers where plugins can
 * inject their own UI widgets. Panels register containers, plugins inject widgets,
 * and cleanup is handled when plugins unload.
 *
 * @pattern Singleton, Observer
 * @threading Main thread only (LVGL constraints)
 *
 * @see plugin_api.h for plugin injection interface
 * @see ui_panel_home.cpp for injection point registration example
 */

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declaration for LVGL types
typedef struct _lv_obj_t lv_obj_t;

namespace helix::plugin {

// ============================================================================
// Widget Callback Types
// ============================================================================

/**
 * @brief Callbacks for widget lifecycle events
 *
 * Plugins can provide these to be notified when their widgets are created
 * or about to be destroyed. Useful for binding subjects or cleanup.
 */
struct WidgetCallbacks {
    std::function<void(lv_obj_t*)>
        on_create; ///< Called after widget created and added to container
    std::function<void(lv_obj_t*)> on_destroy; ///< Called before widget is deleted
};

// ============================================================================
// Injected Widget Tracking
// ============================================================================

/**
 * @brief Tracks a single injected widget
 *
 * Used internally to track which plugin injected which widget,
 * allowing proper cleanup when plugins unload.
 */
struct InjectedWidget {
    std::string plugin_id;       ///< Plugin that injected this widget
    std::string injection_point; ///< Which injection point it was added to
    std::string component_name;  ///< XML component name used to create widget
    lv_obj_t* widget = nullptr;  ///< The actual LVGL widget (owned by parent)
    WidgetCallbacks callbacks;   ///< Lifecycle callbacks
};

// ============================================================================
// Injection Point Manager
// ============================================================================

/**
 * @brief Singleton managing UI injection points
 *
 * Provides the bridge between panels (which register injection point containers)
 * and plugins (which inject widgets into those containers).
 *
 * Typical flow:
 * 1. Panel creates and calls register_point("home_widget_area", container)
 * 2. Plugin calls PluginAPI::inject_widget("home_widget_area", "my_component", callbacks)
 * 3. Manager creates widget via lv_xml_create() and adds to container
 * 4. When plugin unloads, remove_plugin_widgets() cleans up all its widgets
 *
 * Thread safety: All methods must be called from main thread (LVGL constraint).
 * The mutex protects internal data structures during multi-step operations.
 */
class InjectionPointManager {
  public:
    /**
     * @brief Get singleton instance
     */
    static InjectionPointManager& instance();

    // Non-copyable, non-movable
    InjectionPointManager(const InjectionPointManager&) = delete;
    InjectionPointManager& operator=(const InjectionPointManager&) = delete;

    // ========================================================================
    // Panel Registration (called by panels during init)
    // ========================================================================

    /**
     * @brief Register an injection point container
     *
     * Called by panels to register a container where plugins can inject widgets.
     * The container is typically an lv_obj created from XML with flex layout.
     *
     * @param point_id Unique identifier for this injection point (e.g., "home_widget_area")
     * @param container LVGL container object (caller retains ownership)
     */
    void register_point(const std::string& point_id, lv_obj_t* container);

    /**
     * @brief Unregister an injection point
     *
     * Called when a panel is destroyed. Any widgets in the container
     * will be automatically deleted by LVGL when the container is deleted.
     *
     * @param point_id Injection point identifier
     */
    void unregister_point(const std::string& point_id);

    // ========================================================================
    // Plugin Injection (called via PluginAPI)
    // ========================================================================

    /**
     * @brief Inject a widget into an injection point
     *
     * Creates an instance of the XML component and adds it to the container.
     * The widget is tracked for cleanup when the plugin unloads.
     *
     * @param plugin_id ID of the plugin injecting the widget
     * @param point_id Injection point to inject into
     * @param xml_component Name of XML component to instantiate
     * @param callbacks Optional lifecycle callbacks
     * @return true if injection succeeded, false otherwise
     */
    bool inject_widget(const std::string& plugin_id, const std::string& point_id,
                       const std::string& xml_component, const WidgetCallbacks& callbacks);

    /**
     * @brief Remove all widgets injected by a plugin
     *
     * Called when a plugin unloads. Invokes on_destroy callbacks and
     * deletes all widgets created by the specified plugin.
     *
     * @param plugin_id Plugin ID whose widgets should be removed
     */
    void remove_plugin_widgets(const std::string& plugin_id);

    /**
     * @brief Remove a specific widget
     *
     * Removes a single injected widget by its LVGL object pointer.
     *
     * @param widget The widget to remove
     * @return true if widget was found and removed
     */
    bool remove_widget(lv_obj_t* widget);

    // ========================================================================
    // Query Methods
    // ========================================================================

    /**
     * @brief Check if an injection point is registered
     *
     * @param point_id Injection point identifier
     * @return true if the point is registered
     */
    bool has_point(const std::string& point_id) const;

    /**
     * @brief Get all registered injection point IDs
     *
     * @return Vector of point IDs
     */
    std::vector<std::string> get_registered_points() const;

    /**
     * @brief Get all widgets injected by a plugin
     *
     * @param plugin_id Plugin ID to query
     * @return Vector of InjectedWidget info (widget pointers may be invalid if deleted)
     */
    std::vector<InjectedWidget> get_plugin_widgets(const std::string& plugin_id) const;

    /**
     * @brief Get count of widgets at an injection point
     *
     * @param point_id Injection point identifier
     * @return Number of injected widgets, or 0 if point not registered
     */
    size_t get_widget_count(const std::string& point_id) const;

  private:
    friend class InjectionPointManagerTestAccess;
    InjectionPointManager() = default;
    ~InjectionPointManager() = default;

    // Injection point containers (point_id -> container)
    std::unordered_map<std::string, lv_obj_t*> points_;

    // All injected widgets (for tracking and cleanup)
    std::vector<InjectedWidget> injected_widgets_;

    // Thread safety for data structure access
    mutable std::mutex mutex_;
};

} // namespace helix::plugin
