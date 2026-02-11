// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file plugin_events.h
 * @brief Event system for plugin communication
 *
 * Provides a fire-and-forget event system for plugins to observe application
 * events. Plugins can register callbacks for specific events but cannot modify
 * or intercept them (observe-only pattern).
 *
 * Thread safety: Event registration is thread-safe. Callbacks are invoked
 * on the main thread only.
 *
 * @see plugin_api.h for plugin-facing API
 * @see plugin_manager.h for plugin lifecycle
 */

#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "hv/json.hpp"

namespace helix::plugin {

using json = nlohmann::json;

// ============================================================================
// Event Name Constants
// ============================================================================

/**
 * @brief Standard plugin event names
 *
 * Events are fire-and-forget notifications. Plugins observe but cannot modify.
 */
namespace events {

/// Moonraker WebSocket connected and identified
constexpr const char* PRINTER_CONNECTED = "printer_connected";

/// Moonraker WebSocket disconnected
constexpr const char* PRINTER_DISCONNECTED = "printer_disconnected";

/// Print job started (filename in payload)
constexpr const char* PRINT_STARTED = "print_started";

/// Print job paused
constexpr const char* PRINT_PAUSED = "print_paused";

/// Print job resumed from pause
constexpr const char* PRINT_RESUMED = "print_resumed";

/// Print job completed successfully
constexpr const char* PRINT_COMPLETED = "print_completed";

/// Print job cancelled by user
constexpr const char* PRINT_CANCELLED = "print_cancelled";

/// Print job failed with error
constexpr const char* PRINT_ERROR = "print_error";

/// Any heater temperature changed (heater name, current, target in payload)
constexpr const char* TEMPERATURE_UPDATED = "temperature_updated";

/// Filament loaded into extruder (slot, material, color in payload)
constexpr const char* FILAMENT_LOADED = "filament_loaded";

/// Filament unloaded from extruder (slot in payload)
constexpr const char* FILAMENT_UNLOADED = "filament_unloaded";

/// Klipper state changed (ready, shutdown, error, startup)
constexpr const char* KLIPPER_STATE_CHANGED = "klipper_state_changed";

/// Application theme changed (light/dark)
constexpr const char* THEME_CHANGED = "theme_changed";

/// Navigation changed (panel name in payload)
constexpr const char* NAVIGATION_CHANGED = "navigation_changed";

} // namespace events

// ============================================================================
// Event Data Types
// ============================================================================

/**
 * @brief Event payload container
 *
 * Carries event-specific data as JSON for flexibility.
 * Plugins should check for expected fields before accessing.
 */
struct EventData {
    std::string event_name;  ///< Event identifier (events::* constant)
    json payload;            ///< Event-specific data (may be empty object)
    double timestamp_ms = 0; ///< Event timestamp (milliseconds since app start)
};

/**
 * @brief Event callback signature
 *
 * Callbacks receive immutable event data. Must not block.
 */
using EventCallback = std::function<void(const EventData&)>;

/**
 * @brief Handle for event subscription (for unsubscription)
 */
using EventSubscriptionId = uint64_t;

/// Invalid subscription ID
constexpr EventSubscriptionId INVALID_EVENT_SUBSCRIPTION = 0;

// ============================================================================
// Event Dispatcher (Internal)
// ============================================================================

/**
 * @brief Central event dispatcher singleton
 *
 * Manages event subscriptions and dispatches events to registered callbacks.
 * Internal use only - plugins use PluginAPI::on_event() instead.
 *
 * Thread safety:
 * - subscribe()/unsubscribe() are thread-safe
 * - emit() must be called from main thread only
 * - Callbacks are invoked synchronously on main thread
 */
class EventDispatcher {
  public:
    /**
     * @brief Get singleton instance
     */
    static EventDispatcher& instance();

    // Non-copyable singleton
    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    /**
     * @brief Subscribe to an event
     *
     * @param event_name Event to subscribe to (events::* constant)
     * @param callback Callback to invoke when event occurs
     * @return Subscription ID for later unsubscription
     */
    EventSubscriptionId subscribe(const std::string& event_name, EventCallback callback);

    /**
     * @brief Unsubscribe from an event
     *
     * @param id Subscription ID from subscribe()
     * @return true if subscription was found and removed
     */
    bool unsubscribe(EventSubscriptionId id);

    /**
     * @brief Emit an event to all subscribers
     *
     * Must be called from main thread only.
     *
     * @param event_name Event identifier
     * @param payload Event-specific data
     */
    void emit(const std::string& event_name, const json& payload = json::object());

    /**
     * @brief Get count of active subscriptions (for testing/debugging)
     */
    size_t subscription_count() const;

    /**
     * @brief Clear all subscriptions (for testing/shutdown)
     */
    void clear();

  private:
    friend class EventDispatcherTestAccess;
    EventDispatcher() = default;

    struct Subscription {
        EventSubscriptionId id;
        std::string event_name;
        EventCallback callback;
    };

    std::vector<Subscription> subscriptions_;
    EventSubscriptionId next_id_ = 1;
    mutable std::mutex mutex_;
};

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Create EventData with current timestamp
 *
 * @param event_name Event identifier
 * @param payload Event-specific data
 * @return EventData with timestamp populated
 */
EventData make_event(const std::string& event_name, const json& payload = json::object());

} // namespace helix::plugin
