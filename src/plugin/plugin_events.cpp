// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugin_events.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <chrono>

namespace helix::plugin {

// ============================================================================
// EventDispatcher Implementation
// ============================================================================

EventDispatcher& EventDispatcher::instance() {
    static EventDispatcher instance;
    return instance;
}

EventSubscriptionId EventDispatcher::subscribe(const std::string& event_name,
                                               EventCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);

    EventSubscriptionId id = next_id_++;
    subscriptions_.push_back({id, event_name, std::move(callback)});

    spdlog::debug("[plugin] Event subscription added: {} (id={})", event_name, id);
    return id;
}

bool EventDispatcher::unsubscribe(EventSubscriptionId id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find_if(subscriptions_.begin(), subscriptions_.end(),
                           [id](const Subscription& sub) { return sub.id == id; });

    if (it != subscriptions_.end()) {
        spdlog::debug("[plugin] Event subscription removed: {} (id={})", it->event_name, id);
        subscriptions_.erase(it);
        return true;
    }

    return false;
}

void EventDispatcher::emit(const std::string& event_name, const json& payload) {
    // NOTE: Events should be emitted from the main thread only.
    // LVGL is not thread-safe, and plugin callbacks may interact with LVGL widgets.
    // If you need to emit events from a background thread (e.g., network callbacks),
    // use ui_async_call() to defer to the main thread first.

    // Create event data with timestamp
    EventData event = make_event(event_name, payload);

    // Copy callbacks under lock, then invoke outside lock to avoid deadlock
    std::vector<EventCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& sub : subscriptions_) {
            if (sub.event_name == event_name) {
                callbacks.push_back(sub.callback);
            }
        }
    }

    // Invoke callbacks outside lock
    spdlog::debug("[plugin] Emitting event: {} ({} subscribers)", event_name, callbacks.size());
    for (const auto& callback : callbacks) {
        try {
            callback(event);
        } catch (const std::exception& e) {
            spdlog::error("[plugin] Event callback exception for {}: {}", event_name, e.what());
        }
    }
}

size_t EventDispatcher::subscription_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.size();
}

void EventDispatcher::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    subscriptions_.clear();
    spdlog::debug("[plugin] All event subscriptions cleared");
}

// ============================================================================
// Helper Functions
// ============================================================================

EventData make_event(const std::string& event_name, const json& payload) {
    // Get milliseconds since app start (using steady_clock for monotonic time)
    static const auto start_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);

    return EventData{
        .event_name = event_name,
        .payload = payload,
        .timestamp_ms = static_cast<double>(duration.count()),
    };
}

} // namespace helix::plugin
