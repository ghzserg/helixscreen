// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file moonraker_client.cpp
 * @brief WebSocket client for Moonraker printer API communication
 *
 * @pattern libhv WebSocketClient with atomic state machine
 * @threading Callbacks run on libhv event loop thread - use helix::ui::async_call() for LVGL
 * @gotchas is_destroying_ flag blocks callbacks during destruction; skip cleanup during static
 * destruction
 *
 * @see moonraker_manager.cpp, printer_state.cpp
 */

#include "moonraker_client.h"

#include "ui_error_reporting.h"

#include "abort_manager.h"
#include "app_globals.h"
#include "helix_version.h"
#include "printer_state.h"

#include <algorithm> // For std::sort in MCU query handling
#include <sstream>   // For annotate_gcode()

using namespace helix;

using namespace hv;

// Anonymous namespace for file-scoped state
namespace {
// Rate limiting flags for reconnection notifications
std::atomic<bool> g_already_notified_max_attempts{false};
std::atomic<bool> g_already_notified_disconnect{false};

// Reset notification flags on successful connection
void reset_notification_flags() {
    g_already_notified_max_attempts.store(false);
    g_already_notified_disconnect.store(false);
}

// Annotate G-code with source comment for traceability
// Handles multi-line G-code by adding comment to each line
std::string annotate_gcode(const std::string& gcode) {
    constexpr const char* GCODE_SOURCE_COMMENT = " ; from helixscreen";

    std::string result;
    result.reserve(gcode.size() + 20 * std::count(gcode.begin(), gcode.end(), '\n') + 20);

    std::istringstream stream(gcode);
    std::string line;
    bool first = true;

    while (std::getline(stream, line)) {
        if (!first) {
            result += '\n';
        }
        first = false;

        // Only add comment to non-empty lines
        if (!line.empty() && line.find_first_not_of(" \t\r") != std::string::npos) {
            result += line + GCODE_SOURCE_COMMENT;
        } else {
            result += line;
        }
    }

    return result;
}
} // namespace

MoonrakerClient::MoonrakerClient(EventLoopPtr loop)
    : WebSocketClient(loop), request_id_(0), was_connected_(false),
      connection_state_(ConnectionState::DISCONNECTED),
      connection_timeout_ms_(10000) // Default 10 seconds
      ,
      default_request_timeout_ms_(30000) // Default 30 seconds
      ,
      keepalive_interval_ms_(10000) // Default 10 seconds
      ,
      reconnect_min_delay_ms_(200) // Default 200ms
      ,
      reconnect_max_delay_ms_(2000) { // Default 2 seconds
}

MoonrakerClient::~MoonrakerClient() {
    // CRITICAL: Reset lifetime guard FIRST, before any other destruction.
    // This invalidates all weak_ptr captures in callbacks, causing them to
    // return early when they try to lock() the weak_ptr. This prevents
    // use-after-free when callbacks execute on the event loop thread after
    // we've started destruction.
    lifetime_guard_.reset();

    // Set destroying flag - backup check for any code paths that don't use the guard
    is_destroying_.store(true);

    // Disable auto-reconnect BEFORE closing - prevents libhv from attempting
    // reconnection after we've started destruction (avoids stderr "No route to host")
    setReconnect(nullptr);

    // Close WebSocket connection - replace callbacks with no-ops to prevent new callbacks
    // from firing during destruction. The base class destructor will handle socket cleanup.
    onopen = []() {};
    onmessage = [](const std::string&) {};
    onclose = []() {};

    // Clear state change callback without locking (destructor context)
    state_change_callback_ = nullptr;

    // Clear pending requests without invoking error callbacks.
    // During destruction, callback targets (UI panels, file providers, etc.) may
    // already be destroyed — invoking them would be use-after-free. Just drop them.
    if (requests_mutex_.try_lock()) {
        pending_requests_.clear();
        requests_mutex_.unlock();
    }
    // If try_lock failed, we're likely in static destruction - skip cleanup

    // Clear method callbacks safely. Lambdas in this map may capture shared_ptrs
    // to objects (e.g. NoiseCheckCollector) whose destructors call
    // unregister_method_callback(). By moving the map to a local first, the member
    // is empty when those destructors fire, so unregister finds nothing and returns
    // harmlessly. The mutex is safe here since we're on the main thread and not
    // in static destruction (we got here through normal Application::shutdown()).
    if (callbacks_mutex_.try_lock()) {
        decltype(method_callbacks_) doomed_callbacks = std::move(method_callbacks_);
        method_callbacks_.clear();
        callbacks_mutex_.unlock();
        // doomed_callbacks destructs here - lambda destructors may call unregister,
        // but method_callbacks_ is now empty so they'll no-op
    }
}

void MoonrakerClient::set_connection_state(ConnectionState new_state) {
    ConnectionState old_state = connection_state_.exchange(new_state);

    if (old_state != new_state) {
        const char* state_names[] = {"DISCONNECTED", "CONNECTING", "CONNECTED", "RECONNECTING",
                                     "FAILED"};
        spdlog::debug("[Moonraker Client] Connection state: {} -> {}",
                      state_names[static_cast<int>(old_state)],
                      state_names[static_cast<int>(new_state)]);

        // Handle state-specific logic
        if (new_state == ConnectionState::RECONNECTING) {
            reconnect_attempts_++;
            if (max_reconnect_attempts_ > 0 && reconnect_attempts_ >= max_reconnect_attempts_) {
                spdlog::error("[Moonraker Client] Max reconnect attempts ({}) exceeded",
                              max_reconnect_attempts_);

                // Emit event only once during reconnect sequence
                if (!g_already_notified_max_attempts.load()) {
                    emit_event(MoonrakerEventType::CONNECTION_FAILED,
                               fmt::format("Unable to reach printer after {} attempts. "
                                           "Check power and network connection.",
                                           max_reconnect_attempts_),
                               true);
                    g_already_notified_max_attempts.store(true);
                }

                set_connection_state(ConnectionState::FAILED);
                return;
            }
        } else if (new_state == ConnectionState::CONNECTED) {
            reconnect_attempts_ = 0; // Reset on successful connection
        }

        // Copy callback under lock to prevent race with destructor clearing it
        // We invoke OUTSIDE the lock so we don't hold mutex during LVGL operations
        std::function<void(ConnectionState, ConnectionState)> callback_copy;
        {
            std::lock_guard<std::mutex> lock(state_callback_mutex_);
            if (state_change_callback_ && !is_destroying_.load()) {
                callback_copy = state_change_callback_;
            }
        }

        // Double-check is_destroying_ AFTER releasing lock but BEFORE invoking callback
        // This catches the race where destructor set the flag between our copy and invocation
        if (callback_copy && !is_destroying_.load()) {
            try {
                callback_copy(old_state, new_state);
            } catch (const std::exception& e) {
                LOG_ERROR_INTERNAL("[Moonraker Client] State change callback threw exception: {}",
                                   e.what());
            } catch (...) {
                LOG_ERROR_INTERNAL(
                    "[Moonraker Client] State change callback threw unknown exception");
            }
        }
    }
}

void MoonrakerClient::disconnect() {
    ConnectionState current_state = connection_state_.load();

    // Only log if we're actually connected/connecting
    if (current_state != ConnectionState::DISCONNECTED &&
        current_state != ConnectionState::FAILED) {
        spdlog::debug("[Moonraker Client] Disconnecting from WebSocket server");
    }

    // Disable auto-reconnect BEFORE closing to prevent spurious reconnection attempts
    setReconnect(nullptr);

    // Close the WebSocket connection FIRST (before replacing callbacks)
    // This allows the is_destroying_ flag check in callbacks to prevent execution
    // The callbacks will check is_destroying_ and early-return if true
    close();

    // Now replace callbacks with no-op lambdas to prevent any late invocations
    onopen = []() { /* no-op */ };
    onmessage = [](const std::string&) { /* no-op */ };
    onclose = []() { /* no-op */ };

    // Clean up any pending requests
    cleanup_pending_requests();

    // Reset connection state
    set_connection_state(ConnectionState::DISCONNECTED);
    reconnect_attempts_ = 0;
}

void MoonrakerClient::force_reconnect() {
    spdlog::info("[Moonraker Client] Force reconnect requested - full state reset");

    // Copy stored connection info under lock
    std::string url;
    std::function<void()> on_connected;
    std::function<void()> on_disconnected;
    std::function<void()> on_discovery_complete;
    {
        std::lock_guard<std::mutex> lock(reconnect_mutex_);
        url = last_url_;
        on_connected = last_on_connected_;
        on_disconnected = last_on_disconnected_;
        on_discovery_complete = last_discovery_complete_;
    }

    // Verify we have stored connection info
    if (url.empty()) {
        spdlog::warn(
            "[Moonraker Client] force_reconnect() called but no previous connection info - "
            "call connect() first");
        return;
    }

    // 1. Disconnect cleanly (clears pending requests, resets state)
    disconnect();

    // 2. Connect using stored URL and callbacks
    int result = connect(url.c_str(), on_connected, on_disconnected);
    if (result != 0) {
        spdlog::error("[Moonraker Client] force_reconnect() connect failed: {}", result);
        return;
    }

    // 3. Re-run discovery if we have a stored callback
    //    Note: discover_printer() is typically called in on_connected callback,
    //    so it will be triggered automatically. But if the caller wants explicit
    //    discovery, we provide the mechanism.
    spdlog::debug("[Moonraker Client] force_reconnect() complete - connection initiated");
}

int MoonrakerClient::connect(const char* url, std::function<void()> on_connected,
                             std::function<void()> on_disconnected) {
    // Reset WebSocket state from previous connection attempt BEFORE setting new callbacks.
    // This prevents libhv from rejecting the new open() call if we're already connecting/connected.
    // Note: close() is safe to call even if already closed (idempotent).
    close();

    // Apply connection timeout to libhv (must be called before open())
    setConnectTimeout(static_cast<int>(connection_timeout_ms_));

    spdlog::debug("[Moonraker Client] WebSocket connecting to {}", url);
    set_connection_state(ConnectionState::CONNECTING);

    // Connection opened callback
    // Wrap entire callback body in try-catch to prevent any exception from escaping to libhv
    // Capture weak_ptr to lifetime_guard_ to safely detect destruction from event loop thread
    onopen = [this, weak_guard = std::weak_ptr<bool>(lifetime_guard_), on_connected, url]() {
        try {
            // Check lifetime guard FIRST - if lock fails, destructor has started
            // This is thread-safe: weak_ptr::lock() is atomic with shared_ptr::reset()
            auto guard = weak_guard.lock();
            if (!guard) {
                return; // Client is being destroyed, abort callback
            }

            // Backup check (may be redundant but keeps existing logging paths)
            if (is_destroying_.load()) {
                return;
            }

            // Note: getHttpResponse() available here if needed for upgrade response inspection
            spdlog::debug("[Moonraker Client] WebSocket connected to {}", url);

            // Check if this is a reconnection (was_connected_ is true from previous session)
            // Emit RECONNECTED event BEFORE updating was_connected_
            if (was_connected_.load()) {
                emit_event(MoonrakerEventType::RECONNECTED, "Connection restored", false);
            }

            was_connected_ = true;
            set_connection_state(ConnectionState::CONNECTED);

            // Reset notification flags on successful connection
            reset_notification_flags();

            // Invoke user callback with exception safety
            try {
                on_connected();
            } catch (const std::exception& e) {
                LOG_ERROR_INTERNAL("[Moonraker Client] Connection callback threw exception: {}",
                                   e.what());
            } catch (...) {
                LOG_ERROR_INTERNAL(
                    "[Moonraker Client] Connection callback threw unknown exception");
            }
        } catch (const std::exception& e) {
            LOG_ERROR_INTERNAL("[Moonraker Client] onopen callback threw unexpected exception: {}",
                               e.what());
        } catch (...) {
            LOG_ERROR_INTERNAL("[Moonraker Client] onopen callback threw unknown exception");
        }
    };

    // Message received callback
    // Wrap entire callback body in try-catch to prevent any exception from escaping to libhv
    // Capture weak_ptr to lifetime_guard_ to safely detect destruction from event loop thread
    onmessage = [this, weak_guard = std::weak_ptr<bool>(lifetime_guard_), on_connected,
                 on_disconnected](const std::string& msg) {
        // DEBUG: Log every raw message received to diagnose AD5M WebSocket issue
        spdlog::trace("[Moonraker Client] onmessage received {} bytes", msg.size());

        try {
            // Check lifetime guard FIRST - if lock fails, destructor has started
            // This is thread-safe: weak_ptr::lock() is atomic with shared_ptr::reset()
            auto guard = weak_guard.lock();
            if (!guard) {
                return; // Client is being destroyed, abort callback
            }

            // Backup check (may be redundant but keeps existing logging paths)
            if (is_destroying_.load()) {
                return;
            }

            // Validate message size to prevent memory exhaustion
            static constexpr size_t MAX_MESSAGE_SIZE = 5 * 1024 * 1024; // 5 MB
            if (msg.size() > MAX_MESSAGE_SIZE) {
                spdlog::error("[Moonraker Client] Message too large: {} bytes (max: {})",
                              msg.size(), MAX_MESSAGE_SIZE);

                // Emit event - this indicates a protocol problem
                emit_event(MoonrakerEventType::MESSAGE_OVERSIZED,
                           fmt::format("Received oversized data from printer ({} bytes). "
                                       "This may indicate a communication error.",
                                       msg.size()),
                           true);

                disconnect();
                return;
            }

            // Check for timed out requests on each message (opportunistic cleanup)
            check_request_timeouts();

            // DEBUG: Log large messages to help diagnose history issue
            if (msg.size() > 50000) {
                spdlog::debug("[Moonraker Client] Received large message: {} bytes", msg.size());
            }

            // Parse JSON message
            json j;
            try {
                j = json::parse(msg);
            } catch (const json::parse_error& e) {
                LOG_ERROR_INTERNAL("[Moonraker Client] JSON parse error: {}", e.what());
                return;
            }

            // Handle responses with request IDs (one-time callbacks)
            if (j.contains("id")) {
                // Validate 'id' field type
                if (!j["id"].is_number_integer()) {
                    LOG_ERROR_INTERNAL("[Moonraker Client] Invalid 'id' type in response: {}",
                                       j["id"].type_name());
                    return;
                }

                uint64_t id = j["id"].get<uint64_t>();

                // DEBUG: Log every response ID to diagnose history issue
                spdlog::trace("[Moonraker Client] Got response for id={}, size={} bytes", id,
                              msg.size());

                // Copy callbacks out before invoking to avoid deadlock
                std::function<void(json)> success_cb;
                std::function<void(const MoonrakerError&)> error_cb;
                std::string method_name;
                bool has_error = false;
                bool is_silent = false;
                MoonrakerError error;

                {
                    std::lock_guard<std::mutex> lock(requests_mutex_);
                    auto it = pending_requests_.find(id);
                    if (it != pending_requests_.end()) {
                        PendingRequest& request = it->second;
                        method_name = request.method;
                        is_silent = request.silent;

                        // Check for JSON-RPC error
                        if (j.contains("error")) {
                            has_error = true;
                            error = MoonrakerError::from_json_rpc(j["error"], request.method);
                            error_cb = request.error_callback;
                        } else {
                            success_cb = request.success_callback;
                        }

                        pending_requests_.erase(it); // Remove before invoking callbacks
                    }
                } // Lock released here

                // Invoke callbacks outside the lock to avoid deadlock
                if (has_error) {
                    // Suppress toast notifications during shutdown handling to avoid
                    // confusing errors appearing behind the abort modal
                    bool suppress_toast = helix::AbortManager::instance().is_handling_shutdown();

                    if (!is_silent && !suppress_toast) {
                        spdlog::error("[Moonraker Client] Request {} failed: {}", method_name,
                                      error.message);

                        // Emit RPC error event (only for non-silent requests)
                        emit_event(MoonrakerEventType::RPC_ERROR,
                                   fmt::format("Printer command '{}' failed: {}", method_name,
                                               error.message),
                                   true, method_name);
                    } else if (suppress_toast) {
                        spdlog::debug(
                            "[Moonraker Client] Request {} failed during shutdown (suppressed): {}",
                            method_name, error.message);
                    } else {
                        spdlog::debug("[Moonraker Client] Silent request {} failed: {}",
                                      method_name, error.message);
                    }

                    if (error_cb) {
                        error_cb(error);
                    }
                } else if (success_cb) {
                    try {
                        success_cb(j);
                    } catch (const std::exception& e) {
                        LOG_ERROR_INTERNAL(
                            "[Moonraker Client] Success callback for '{}' threw exception: {}",
                            method_name, e.what());
                        // Do NOT re-throw: stack unwinding between here and the outer
                        // handler can leave libhv's event loop in a corrupt state,
                        // leading to SIGSEGV on the next message cycle.
                    }
                }
            }

            // Handle notifications (no request ID)
            if (j.contains("method")) {
                // Validate 'method' field type
                if (!j["method"].is_string()) {
                    LOG_ERROR_INTERNAL(
                        "[Moonraker Client] Invalid 'method' type in notification: {}",
                        j["method"].type_name());
                    return;
                }

                std::string method = j["method"].get<std::string>();

                // Copy callbacks to invoke (to avoid holding lock during callback execution)
                std::vector<std::function<void(json)>> callbacks_to_invoke;

                {
                    std::lock_guard<std::mutex> lock(callbacks_mutex_);

                    // Printer status updates (most common)
                    if (method == "notify_status_update" || method == "notify_filelist_changed") {
                        // Copy all notify callbacks from map
                        callbacks_to_invoke.reserve(notify_callbacks_.size());
                        for (const auto& [id, cb] : notify_callbacks_) {
                            callbacks_to_invoke.push_back(cb);
                        }
                    }

                    // Method-specific persistent callbacks
                    auto method_it = method_callbacks_.find(method);
                    if (method_it != method_callbacks_.end()) {
                        for (auto& [handler_name, cb] : method_it->second) {
                            callbacks_to_invoke.push_back(cb);
                        }
                    }
                } // Release lock

                // Parse bed mesh updates before invoking user callbacks
                if (method == "notify_status_update" && j.contains("params") &&
                    j["params"].is_array() && !j["params"].empty()) {
                    const json& params = j["params"][0];
                    if (params.contains("bed_mesh") && params["bed_mesh"].is_object()) {
                        parse_bed_mesh(params["bed_mesh"]);
                    }
                }

                // Invoke callbacks outside lock to prevent deadlock
                for (auto& cb : callbacks_to_invoke) {
                    try {
                        cb(j);
                    } catch (const std::exception& e) {
                        LOG_ERROR_INTERNAL("[Moonraker Client] Callback for {} threw exception: {}",
                                           method, e.what());
                    } catch (...) {
                        LOG_ERROR_INTERNAL(
                            "[Moonraker Client] Callback for {} threw unknown exception", method);
                    }
                }

                // Klippy disconnected from Moonraker
                if (method == "notify_klippy_disconnected") {
                    spdlog::warn("[Moonraker Client] Klipper disconnected from Moonraker");

                    // Update klippy state in PrinterState (SHUTDOWN = firmware disconnected)
                    get_printer_state().set_klippy_state(KlippyState::SHUTDOWN);

                    // Emit event for UI layer to handle
                    emit_event(MoonrakerEventType::KLIPPY_DISCONNECTED,
                               "Klipper has disconnected from Moonraker. Check for errors in your "
                               "printer interface.",
                               true);

                    // Invoke user callback with exception safety
                    try {
                        on_disconnected();
                    } catch (const std::exception& e) {
                        LOG_ERROR_INTERNAL(
                            "[Moonraker Client] Disconnection callback threw exception: {}",
                            e.what());
                    } catch (...) {
                        LOG_ERROR_INTERNAL(
                            "[Moonraker Client] Disconnection callback threw unknown exception");
                    }
                }
                // Klippy reconnected to Moonraker
                else if (method == "notify_klippy_ready") {
                    spdlog::info("[Moonraker Client] Klipper ready");

                    // Update klippy state in PrinterState (READY = firmware ready)
                    get_printer_state().set_klippy_state(KlippyState::READY);

                    // Emit event for UI layer to show success toast
                    emit_event(MoonrakerEventType::KLIPPY_READY, "Klipper ready", false);

                    // Invoke user callback with exception safety
                    try {
                        on_connected();
                    } catch (const std::exception& e) {
                        LOG_ERROR_INTERNAL(
                            "[Moonraker Client] Connection callback threw exception: {}", e.what());
                    } catch (...) {
                        LOG_ERROR_INTERNAL(
                            "[Moonraker Client] Connection callback threw unknown exception");
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR_INTERNAL(
                "[Moonraker Client] onmessage callback threw unexpected exception: {}", e.what());
        } catch (...) {
            LOG_ERROR_INTERNAL("[Moonraker Client] onmessage callback threw unknown exception");
        }
    };

    // Connection closed callback
    // Wrap entire callback body in try-catch to prevent any exception from escaping
    // to libhv (which may not handle exceptions properly or may be noexcept)
    // Capture weak_ptr to lifetime_guard_ to safely detect destruction from event loop thread
    onclose = [this, weak_guard = std::weak_ptr<bool>(lifetime_guard_), on_disconnected]() {
        try {
            spdlog::debug("[Moonraker Client] onclose callback invoked");

            // Check lifetime guard FIRST - if lock fails, destructor has started
            // This is thread-safe: weak_ptr::lock() is atomic with shared_ptr::reset()
            auto guard = weak_guard.lock();
            if (!guard) {
                spdlog::debug(
                    "[Moonraker Client] onclose callback early return - client destroyed");
                return; // Client is being destroyed, abort callback
            }

            // Backup check (may be redundant but keeps existing logging paths)
            if (is_destroying_.load()) {
                spdlog::debug(
                    "[Moonraker Client] onclose callback early return due to destruction");
                return;
            }

            ConnectionState current = connection_state_.load();

            // Cleanup all pending requests (invoke error callbacks)
            cleanup_pending_requests();

            if (was_connected_) {
                spdlog::warn("[Moonraker Client] WebSocket connection closed");
                was_connected_ = false;
                identified_.store(false); // Reset so re-identification happens on reconnect

                // Emit event with rate limiting to prevent spam during reconnect loop
                if (!g_already_notified_disconnect.load()) {
                    emit_event(MoonrakerEventType::CONNECTION_LOST,
                               "Connection to printer lost - attempting to reconnect...", false);
                    g_already_notified_disconnect.store(true);
                }

                // Check if this is a reconnection scenario
                if (current != ConnectionState::FAILED) {
                    set_connection_state(ConnectionState::RECONNECTING);
                }

                // Invoke user callback with exception safety
                try {
                    on_disconnected();
                } catch (const std::exception& e) {
                    LOG_ERROR_INTERNAL(
                        "[Moonraker Client] Disconnection callback threw exception: {}", e.what());
                } catch (...) {
                    LOG_ERROR_INTERNAL(
                        "[Moonraker Client] Disconnection callback threw unknown exception");
                }
            } else {
                spdlog::debug(
                    "[Moonraker Client] WebSocket connection failed (printer not available)");

                // Initial connection failed
                if (current == ConnectionState::CONNECTING) {
                    set_connection_state(ConnectionState::DISCONNECTED);
                }

                // Call on_disconnected() to notify about connection failure
                // Callers can use their own state tracking (e.g. connection_testing flag)
                // to distinguish initial connection failures from reconnection scenarios
                try {
                    on_disconnected();
                } catch (const std::exception& e) {
                    LOG_ERROR_INTERNAL(
                        "[Moonraker Client] Disconnection callback threw exception: {}", e.what());
                } catch (...) {
                    LOG_ERROR_INTERNAL(
                        "[Moonraker Client] Disconnection callback threw unknown exception");
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR_INTERNAL("[Moonraker Client] onclose callback threw unexpected exception: {}",
                               e.what());
        } catch (...) {
            LOG_ERROR_INTERNAL("[Moonraker Client] onclose callback threw unknown exception");
        }
    };

    // WebSocket ping (keepalive) - use configured interval
    setPingInterval(static_cast<int>(keepalive_interval_ms_));

    // Automatic reconnection with exponential backoff - use configured values
    reconn_setting_t reconn;
    reconn_setting_init(&reconn);
    reconn.min_delay = reconnect_min_delay_ms_;
    reconn.max_delay = reconnect_max_delay_ms_;
    reconn.delay_policy = 2; // Exponential backoff
    setReconnect(&reconn);

    // Store connection info for force_reconnect()
    {
        std::lock_guard<std::mutex> lock(reconnect_mutex_);
        last_url_ = url;
        last_on_connected_ = on_connected;
        last_on_disconnected_ = on_disconnected;
    }

    // Connect
    http_headers headers;
    return open(url, headers);
}

SubscriptionId MoonrakerClient::register_notify_update(std::function<void(json)> cb) {
    if (!cb) {
        spdlog::warn("[Moonraker Client] register_notify_update called with null callback");
        return INVALID_SUBSCRIPTION_ID;
    }

    SubscriptionId id = next_subscription_id_.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        notify_callbacks_.emplace(id, cb);
    }
    spdlog::trace("[Moonraker Client] Registered notify callback with ID {}", id);
    return id;
}

bool MoonrakerClient::unsubscribe_notify_update(SubscriptionId id) {
    if (id == INVALID_SUBSCRIPTION_ID) {
        return false;
    }

    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    auto it = notify_callbacks_.find(id);
    if (it != notify_callbacks_.end()) {
        notify_callbacks_.erase(it);
        spdlog::debug("[Moonraker Client] Unsubscribed notify callback ID {}", id);
        return true;
    }
    spdlog::debug("[Moonraker Client] Unsubscribe failed: notify callback ID {} not found", id);
    return false;
}

void MoonrakerClient::register_event_handler(MoonrakerEventCallback cb) {
    std::lock_guard<std::mutex> lock(event_handler_mutex_);
    event_handler_ = std::move(cb);
    spdlog::debug("[Moonraker Client] Event handler {}",
                  event_handler_ ? "registered" : "unregistered");
}

void MoonrakerClient::suppress_disconnect_modal(uint32_t duration_ms) {
    std::lock_guard<std::mutex> lock(suppress_mutex_);
    suppress_disconnect_modal_until_ =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms);
    spdlog::info("[Moonraker Client] Suppressing disconnect modal for {}ms", duration_ms);
}

bool MoonrakerClient::is_disconnect_modal_suppressed() const {
    std::lock_guard<std::mutex> lock(suppress_mutex_);
    return std::chrono::steady_clock::now() < suppress_disconnect_modal_until_;
}

void MoonrakerClient::emit_event(MoonrakerEventType type, const std::string& message, bool is_error,
                                 const std::string& details) {
    MoonrakerEventCallback handler;
    {
        std::lock_guard<std::mutex> lock(event_handler_mutex_);
        handler = event_handler_;
    }

    if (handler) {
        MoonrakerEvent evt{type, message, details, is_error};
        try {
            handler(evt);
        } catch (const std::exception& e) {
            spdlog::error("[Moonraker Client] Event handler threw exception: {}", e.what());
        }
    } else {
        // No handler registered - just log the event
        if (is_error) {
            spdlog::error("[Moonraker Event] {}: {}", static_cast<int>(type), message);
        } else {
            spdlog::warn("[Moonraker Event] {}: {}", static_cast<int>(type), message);
        }
    }
}

void MoonrakerClient::dispatch_status_update(const json& status) {
    // Parse bed mesh data before dispatching (mirrors WebSocket handler behavior)
    // This ensures bed mesh is populated on initial subscription response,
    // not just on subsequent notify_status_update messages
    if (status.contains("bed_mesh") && status["bed_mesh"].is_object()) {
        parse_bed_mesh(status["bed_mesh"]);
        // NOTE: Do NOT set build_volume from mesh bounds here!
        // Mesh bounds represent the probe area, not bed dimensions.
        // Actual bed dimensions come from stepper config in moonraker_api_motion.cpp.
    }

    // Extract kinematics type from toolhead data (for printer detection)
    if (status.contains("toolhead") && status["toolhead"].is_object()) {
        const json& toolhead = status["toolhead"];
        if (toolhead.contains("kinematics") && toolhead["kinematics"].is_string()) {
            auto kinematics = toolhead["kinematics"].get<std::string>();
            hardware_.set_kinematics(kinematics);
            spdlog::debug("[Moonraker Client] Kinematics type: {}", kinematics);
        }
    }

    // Wrap raw status into notify_status_update format
    json notification = {
        {"method", "notify_status_update"},
        {"params", json::array({status, 0.0})} // [status, eventtime]
    };

    // Dispatch to all registered callbacks
    // Two-phase: copy under lock, invoke outside to avoid deadlock
    std::vector<std::function<void(json)>> callbacks_copy;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks_copy.reserve(notify_callbacks_.size());
        for (const auto& [id, cb] : notify_callbacks_) {
            callbacks_copy.push_back(cb);
        }
    }

    for (const auto& cb : callbacks_copy) {
        if (cb) {
            cb(notification);
        }
    }

    spdlog::trace(
        "[Moonraker Client] Dispatched status update to {} callbacks (has print_stats: {})",
        callbacks_copy.size(), status.contains("print_stats"));
}

void MoonrakerClient::register_method_callback(const std::string& method,
                                               const std::string& handler_name,
                                               std::function<void(json)> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    auto it = method_callbacks_.find(method);
    if (it == method_callbacks_.end()) {
        spdlog::debug("[Moonraker Client] Registering new method callback: {} (handler: {})",
                      method, handler_name);
        std::map<std::string, std::function<void(json)>> handlers;
        handlers.insert({handler_name, cb});
        method_callbacks_.insert({method, handlers});
    } else {
        spdlog::debug("[Moonraker Client] Adding handler to existing method {}: {}", method,
                      handler_name);
        it->second.insert({handler_name, cb});
    }
}

bool MoonrakerClient::unregister_method_callback(const std::string& method,
                                                 const std::string& handler_name) {
    // During destruction, method_callbacks_ may already be cleared or mid-destruction.
    // Skip the erase to avoid use-after-free on the map's internal tree.
    if (is_destroying_.load()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    auto method_it = method_callbacks_.find(method);
    if (method_it == method_callbacks_.end()) {
        spdlog::debug("[Moonraker Client] Unregister failed: method '{}' not found", method);
        return false;
    }

    auto handler_it = method_it->second.find(handler_name);
    if (handler_it == method_it->second.end()) {
        spdlog::debug(
            "[Moonraker Client] Unregister failed: handler '{}' not found for method '{}'",
            handler_name, method);
        return false;
    }

    method_it->second.erase(handler_it);
    spdlog::debug("[Moonraker Client] Unregistered handler '{}' from method '{}'", handler_name,
                  method);

    // Clean up empty method entries to avoid memory leaks
    if (method_it->second.empty()) {
        method_callbacks_.erase(method_it);
        spdlog::debug("[Moonraker Client] Removed empty method entry for '{}'", method);
    }

    return true;
}

int MoonrakerClient::send_jsonrpc(const std::string& method) {
    json rpc;
    rpc["jsonrpc"] = "2.0";
    rpc["method"] = method;
    rpc["id"] = request_id_++;

    spdlog::trace("[Moonraker Client] send_jsonrpc: {}", rpc.dump());
    return send(rpc.dump());
}

int MoonrakerClient::send_jsonrpc(const std::string& method, const json& params) {
    json rpc;
    rpc["jsonrpc"] = "2.0";
    rpc["method"] = method;

    // Only include params if not null or empty
    if (!params.is_null() && !params.empty()) {
        rpc["params"] = params;
    }

    rpc["id"] = request_id_++;

    spdlog::trace("[Moonraker Client] send_jsonrpc: {}", rpc.dump());
    return send(rpc.dump());
}

RequestId MoonrakerClient::send_jsonrpc(const std::string& method, const json& params,
                                        std::function<void(json)> cb) {
    // Forward to new overload with null error callback
    return send_jsonrpc(method, params, cb, nullptr, 0);
}

RequestId MoonrakerClient::send_jsonrpc(const std::string& method, const json& params,
                                        std::function<void(json)> success_cb,
                                        std::function<void(const MoonrakerError&)> error_cb,
                                        uint32_t timeout_ms, bool silent) {
    // Atomically fetch and increment to avoid race condition in concurrent calls
    // Note: request_id_ starts at 0, but we increment FIRST, so actual IDs start at 1
    // This ensures we never return 0 (INVALID_REQUEST_ID) for a valid request
    RequestId id = request_id_.fetch_add(1) + 1;

    // Create pending request
    PendingRequest request;
    request.id = id;
    request.method = method;
    request.success_callback = success_cb;
    request.error_callback = error_cb;
    request.timestamp = std::chrono::steady_clock::now();
    request.timeout_ms = (timeout_ms > 0) ? timeout_ms : default_request_timeout_ms_;
    request.silent = silent;

    // Register request
    {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        auto it = pending_requests_.find(id);
        if (it != pending_requests_.end()) {
            LOG_ERROR_INTERNAL("[Moonraker Client] Request ID {} already has a registered callback",
                               id);
            return INVALID_REQUEST_ID;
        }
        pending_requests_.insert({id, request});
        spdlog::trace("[Moonraker Client] Registered request {} for method {}, total pending: {}",
                      id, method, pending_requests_.size());
    }

    // Build and send JSON-RPC message with the registered ID
    json rpc;
    rpc["jsonrpc"] = "2.0";
    rpc["method"] = method;
    rpc["id"] = id; // Use the ID we registered, not a new one

    // Only include params if not null or empty
    if (!params.is_null() && !params.empty()) {
        rpc["params"] = params;
    }

    spdlog::trace("[Moonraker Client] send_jsonrpc: {}", rpc.dump());
    int result = send(rpc.dump());
    spdlog::trace("[Moonraker Client] send_jsonrpc({}) returned {}", method, result);

    // Return the request ID on success, or INVALID_REQUEST_ID on send failure
    if (result < 0) {
        // Send failed - remove pending request and invoke error callback
        std::function<void(const MoonrakerError&)> error_callback_copy;
        std::string method_name;
        {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            auto it = pending_requests_.find(id);
            if (it != pending_requests_.end()) {
                error_callback_copy = it->second.error_callback;
                method_name = it->second.method;
                pending_requests_.erase(it);
            }
        }
        spdlog::error("[Moonraker Client] Failed to send request {} ({}), removed from pending", id,
                      method_name.empty() ? "unknown" : method_name);

        // Invoke error callback outside lock (prevents deadlock if callback sends new request)
        if (error_callback_copy) {
            try {
                error_callback_copy(MoonrakerError::connection_lost(method_name));
            } catch (const std::exception& e) {
                spdlog::error("[Moonraker Client] Error callback threw exception: {}", e.what());
            }
        }
        return INVALID_REQUEST_ID;
    }

    return id;
}

bool MoonrakerClient::cancel_request(RequestId id) {
    if (id == INVALID_REQUEST_ID) {
        return false;
    }

    std::lock_guard<std::mutex> lock(requests_mutex_);
    auto it = pending_requests_.find(id);
    if (it != pending_requests_.end()) {
        spdlog::debug("[Moonraker Client] Cancelled request {} ({})", id, it->second.method);
        pending_requests_.erase(it);
        return true;
    }

    spdlog::debug("[Moonraker Client] Cancel failed: request {} not found (already completed?)",
                  id);
    return false;
}

int MoonrakerClient::gcode_script(const std::string& gcode) {
    std::string annotated = annotate_gcode(gcode);
    json params = {{"script", annotated}};
    int result = send_jsonrpc("printer.gcode.script", params);
    // send() returns bytes sent (positive) on success, negative on error.
    // Normalize to match API contract: 0 = success, negative = error.
    return result < 0 ? result : 0;
}

void MoonrakerClient::get_gcode_store(
    int count, std::function<void(const std::vector<GcodeStoreEntry>&)> on_success,
    std::function<void(const MoonrakerError&)> on_error) {
    json params = {{"count", count}};

    send_jsonrpc(
        "server.gcode_store", params,
        [on_success](json response) {
            std::vector<GcodeStoreEntry> entries;

            // Parse response: {"result": {"gcode_store": [...]}}
            if (response.contains("result") && response["result"].contains("gcode_store")) {
                const auto& store = response["result"]["gcode_store"];
                entries.reserve(store.size());

                for (const auto& item : store) {
                    GcodeStoreEntry entry;
                    entry.message = item.value("message", "");
                    entry.time = item.value("time", 0.0);
                    entry.type = item.value("type", "response");
                    entries.push_back(entry);
                }
            }

            if (on_success) {
                on_success(entries);
            }
        },
        on_error);
}

void MoonrakerClient::discover_printer(std::function<void()> on_complete,
                                       std::function<void(const std::string& reason)> on_error) {
    spdlog::debug("[Moonraker Client] Starting printer auto-discovery");

    // Store callback for force_reconnect()
    {
        std::lock_guard<std::mutex> lock(reconnect_mutex_);
        last_discovery_complete_ = on_complete;
    }

    // Step 0: Identify ourselves to Moonraker to enable receiving notifications
    // Skip if we've already identified on this connection (e.g., wizard tested, then completed)
    if (identified_.load()) {
        spdlog::debug("[Moonraker Client] Already identified, skipping identify step");
        continue_discovery(on_complete, on_error);
        return;
    }

    json identify_params = {{"client_name", "HelixScreen"},
                            {"version", HELIX_VERSION},
                            {"type", "display"},
                            {"url", "https://github.com/helixscreen/helixscreen"}};

    send_jsonrpc(
        "server.connection.identify", identify_params,
        [this, on_complete, on_error](json identify_response) {
            if (identify_response.contains("result")) {
                auto conn_id = identify_response["result"].value("connection_id", 0);
                spdlog::info("[Moonraker Client] Identified to Moonraker (connection_id: {})",
                             conn_id);
                identified_.store(true);
            } else if (identify_response.contains("error")) {
                // Log but continue - older Moonraker versions may not support this
                spdlog::warn("[Moonraker Client] Failed to identify: {}",
                             identify_response["error"].dump());
            }

            // Continue with discovery regardless of identify result
            continue_discovery(on_complete, on_error);
        },
        [this, on_complete, on_error](const MoonrakerError& err) {
            // Log but continue - identify is not strictly required
            spdlog::warn("[Moonraker Client] Identify request failed: {}", err.message);
            continue_discovery(on_complete, on_error);
        });
}

void MoonrakerClient::continue_discovery(std::function<void()> on_complete,
                                         std::function<void(const std::string& reason)> on_error) {
    // Step 1: Query available printer objects (no params required)
    send_jsonrpc(
        "printer.objects.list", json(),
        [this, on_complete, on_error](json response) {
            // Debug: Log raw response
            spdlog::debug("[Moonraker Client] printer.objects.list response: {}", response.dump());

            // Validate response
            if (!response.contains("result") || !response["result"].contains("objects")) {
                // Extract error message from response if available
                std::string error_reason = "Failed to query printer objects from Moonraker";
                if (response.contains("error") && response["error"].contains("message")) {
                    error_reason = response["error"]["message"].get<std::string>();
                    spdlog::error("[Moonraker Client] printer.objects.list failed: {}",
                                  error_reason);
                } else {
                    spdlog::error(
                        "[Moonraker Client] printer.objects.list failed: invalid response");
                    if (response.contains("error")) {
                        spdlog::error("[Moonraker Client]   Error details: {}",
                                      response["error"].dump());
                    }
                }

                // Emit discovery failed event
                emit_event(MoonrakerEventType::DISCOVERY_FAILED, error_reason, true);

                // Invoke error callback if provided
                spdlog::debug(
                    "[Moonraker Client] Invoking discovery on_error callback, on_error={}",
                    on_error ? "valid" : "null");
                if (on_error) {
                    on_error(error_reason);
                }
                return;
            }

            // Parse discovered objects into typed arrays
            const json& objects = response["result"]["objects"];
            parse_objects(objects);

            // Early hardware discovery callback - allows AMS/MMU backends to initialize
            // BEFORE the subscription response arrives, so they can receive initial state naturally
            if (on_hardware_discovered_) {
                spdlog::debug("[Moonraker Client] Invoking early hardware discovery callback");
                on_hardware_discovered_(hardware_);
            }

            // Step 2: Get server information
            send_jsonrpc("server.info", {}, [this, on_complete](json info_response) {
                if (info_response.contains("result")) {
                    const json& result = info_response["result"];
                    std::string klippy_version = result.value("klippy_version", "unknown");
                    auto moonraker_version = result.value("moonraker_version", "unknown");
                    hardware_.set_moonraker_version(moonraker_version);

                    spdlog::debug("[Moonraker Client] Moonraker version: {}", moonraker_version);
                    spdlog::debug("[Moonraker Client] Klippy version: {}", klippy_version);

                    if (result.contains("components") && result["components"].is_array()) {
                        std::vector<std::string> components =
                            result["components"].get<std::vector<std::string>>();
                        spdlog::debug("[Moonraker Client] Server components: {}",
                                      json(components).dump());

                        // Check for Spoolman component and verify connection
                        bool has_spoolman_component =
                            std::find(components.begin(), components.end(), "spoolman") !=
                            components.end();
                        if (has_spoolman_component) {
                            spdlog::info("[Moonraker Client] Spoolman component detected, "
                                         "checking status...");
                            // Fire-and-forget status check - updates PrinterState async
                            // Use JSON-RPC directly since we're inside MoonrakerClient
                            send_jsonrpc(
                                "server.spoolman.status", json::object(),
                                [](json response) {
                                    bool connected = false;
                                    if (response.contains("result")) {
                                        connected =
                                            response["result"].value("spoolman_connected", false);
                                    }
                                    spdlog::info("[Moonraker Client] Spoolman status: connected={}",
                                                 connected);
                                    get_printer_state().set_spoolman_available(connected);
                                },
                                [](const MoonrakerError& err) {
                                    spdlog::warn(
                                        "[Moonraker Client] Spoolman status check failed: {}",
                                        err.message);
                                    get_printer_state().set_spoolman_available(false);
                                });
                        }
                    }
                }

                // Fire-and-forget webcam detection - independent of components list
                send_jsonrpc(
                    "server.webcams.list", json::object(),
                    [](json response) {
                        bool has_webcam = false;
                        if (response.contains("result") && response["result"].contains("webcams")) {
                            for (const auto& cam : response["result"]["webcams"]) {
                                if (cam.value("enabled", true)) {
                                    has_webcam = true;
                                    break;
                                }
                            }
                        }
                        spdlog::info("[Moonraker Client] Webcam detection: {}",
                                     has_webcam ? "found" : "none");
                        get_printer_state().set_webcam_available(has_webcam);
                    },
                    [](const MoonrakerError& err) {
                        spdlog::warn("[Moonraker Client] Webcam detection failed: {}", err.message);
                        get_printer_state().set_webcam_available(false);
                    });

                // Step 3: Get printer information
                send_jsonrpc("printer.info", {}, [this, on_complete](json printer_response) {
                    if (printer_response.contains("result")) {
                        const json& result = printer_response["result"];
                        auto hostname = result.value("hostname", "unknown");
                        auto software_version = result.value("software_version", "unknown");
                        hardware_.set_hostname(hostname);
                        hardware_.set_software_version(software_version);
                        std::string state = result.value("state", "");
                        std::string state_message = result.value("state_message", "");

                        spdlog::debug("[Moonraker Client] Printer hostname: {}", hostname);
                        spdlog::debug("[Moonraker Client] Klipper software version: {}",
                                      software_version);
                        if (!state_message.empty()) {
                            spdlog::info("[Moonraker Client] Printer state: {}", state_message);
                        }

                        // Set klippy state based on printer.info response
                        // This ensures we recognize shutdown/error states at startup
                        if (state == "shutdown") {
                            spdlog::warn(
                                "[Moonraker Client] Printer is in SHUTDOWN state at startup");
                            get_printer_state().set_klippy_state(KlippyState::SHUTDOWN);
                        } else if (state == "error") {
                            spdlog::warn("[Moonraker Client] Printer is in ERROR state at startup");
                            get_printer_state().set_klippy_state(KlippyState::ERROR);
                        } else if (state == "startup") {
                            spdlog::info("[Moonraker Client] Printer is starting up");
                            get_printer_state().set_klippy_state(KlippyState::STARTUP);
                        } else if (state == "ready") {
                            get_printer_state().set_klippy_state(KlippyState::READY);
                        }
                    }

                    // Step 4: Query configfile for accelerometer detection
                    // Klipper's objects/list only returns objects with get_status() methods.
                    // Accelerometers (adxl345, lis2dw, mpu9250, resonance_tester) don't have
                    // get_status() since they're on-demand calibration tools.
                    // Must check configfile.config keys instead.
                    send_jsonrpc(
                        "printer.objects.query",
                        {{"objects", json::object({{"configfile", json::array({"config"})}})}},
                        [this](json config_response) {
                            if (config_response.contains("result") &&
                                config_response["result"].contains("status") &&
                                config_response["result"]["status"].contains("configfile") &&
                                config_response["result"]["status"]["configfile"].contains(
                                    "config")) {
                                hardware_.parse_config_keys(
                                    config_response["result"]["status"]["configfile"]["config"]);
                            }
                        },
                        [](const MoonrakerError& err) {
                            // Configfile query failed - not critical, continue with discovery
                            spdlog::debug(
                                "[Moonraker Client] Configfile query failed, continuing: {}",
                                err.message);
                        });

                    // Step 4b: Query OS version from machine.system_info (parallel)
                    send_jsonrpc(
                        "machine.system_info", json::object(),
                        [this](json sys_response) {
                            // Extract distribution name: result.system_info.distribution.name
                            if (sys_response.contains("result") &&
                                sys_response["result"].contains("system_info") &&
                                sys_response["result"]["system_info"].contains("distribution") &&
                                sys_response["result"]["system_info"]["distribution"].contains(
                                    "name")) {
                                std::string os_name =
                                    sys_response["result"]["system_info"]["distribution"]["name"]
                                        .get<std::string>();
                                hardware_.set_os_version(os_name);
                                spdlog::debug("[Moonraker Client] OS version: {}", os_name);
                            }
                        },
                        [](const MoonrakerError& err) {
                            spdlog::debug(
                                "[Moonraker Client] machine.system_info query failed, continuing: "
                                "{}",
                                err.message);
                        });

                    // Step 5: Query MCU information for printer detection
                    // Find all MCU objects (e.g., "mcu", "mcu EBBCan", "mcu rpi")
                    std::vector<std::string> mcu_objects;
                    for (const auto& obj : hardware_.printer_objects()) {
                        // Match "mcu" or "mcu <name>" pattern
                        if (obj == "mcu" || obj.rfind("mcu ", 0) == 0) {
                            mcu_objects.push_back(obj);
                        }
                    }

                    if (mcu_objects.empty()) {
                        spdlog::debug(
                            "[Moonraker Client] No MCU objects found, skipping MCU query");
                        // Continue to subscription step
                        complete_discovery_subscription(on_complete);
                        return;
                    }

                    // Query all MCU objects in parallel using a shared counter
                    auto pending_mcu_queries =
                        std::make_shared<std::atomic<size_t>>(mcu_objects.size());
                    auto mcu_results =
                        std::make_shared<std::vector<std::pair<std::string, std::string>>>();
                    auto mcu_version_results =
                        std::make_shared<std::vector<std::pair<std::string, std::string>>>();
                    auto mcu_results_mutex = std::make_shared<std::mutex>();

                    for (const auto& mcu_obj : mcu_objects) {
                        json mcu_query = {{mcu_obj, nullptr}};
                        send_jsonrpc(
                            "printer.objects.query", {{"objects", mcu_query}},
                            [this, on_complete, mcu_obj, pending_mcu_queries, mcu_results,
                             mcu_version_results, mcu_results_mutex](json mcu_response) {
                                std::string chip_type;
                                std::string mcu_version;

                                // Extract MCU chip type and version from response
                                if (mcu_response.contains("result") &&
                                    mcu_response["result"].contains("status") &&
                                    mcu_response["result"]["status"].contains(mcu_obj)) {
                                    const json& mcu_data =
                                        mcu_response["result"]["status"][mcu_obj];

                                    if (mcu_data.contains("mcu_constants") &&
                                        mcu_data["mcu_constants"].is_object() &&
                                        mcu_data["mcu_constants"].contains("MCU") &&
                                        mcu_data["mcu_constants"]["MCU"].is_string()) {
                                        chip_type =
                                            mcu_data["mcu_constants"]["MCU"].get<std::string>();
                                        spdlog::debug("[Moonraker Client] Detected MCU '{}': {}",
                                                      mcu_obj, chip_type);
                                    }

                                    // Extract mcu_version for About section
                                    if (mcu_data.contains("mcu_version") &&
                                        mcu_data["mcu_version"].is_string()) {
                                        mcu_version = mcu_data["mcu_version"].get<std::string>();
                                        spdlog::debug("[Moonraker Client] MCU '{}' version: {}",
                                                      mcu_obj, mcu_version);
                                    }
                                }

                                // Store results thread-safely
                                {
                                    std::lock_guard<std::mutex> lock(*mcu_results_mutex);
                                    if (!chip_type.empty()) {
                                        mcu_results->push_back({mcu_obj, chip_type});
                                    }
                                    if (!mcu_version.empty()) {
                                        mcu_version_results->push_back({mcu_obj, mcu_version});
                                    }
                                }

                                // Check if all queries complete
                                if (pending_mcu_queries->fetch_sub(1) == 1) {
                                    // All MCU queries complete - populate mcu and mcu_list
                                    std::vector<std::string> mcu_list;
                                    std::string primary_mcu;

                                    // Sort results to ensure consistent ordering (primary "mcu"
                                    // first)
                                    std::lock_guard<std::mutex> lock(*mcu_results_mutex);
                                    auto sort_mcu_first = [](const auto& a, const auto& b) {
                                        // "mcu" comes first, then alphabetical
                                        if (a.first == "mcu")
                                            return true;
                                        if (b.first == "mcu")
                                            return false;
                                        return a.first < b.first;
                                    };
                                    std::sort(mcu_results->begin(), mcu_results->end(),
                                              sort_mcu_first);
                                    std::sort(mcu_version_results->begin(),
                                              mcu_version_results->end(), sort_mcu_first);

                                    for (const auto& [obj_name, chip] : *mcu_results) {
                                        mcu_list.push_back(chip);
                                        if (obj_name == "mcu" && primary_mcu.empty()) {
                                            primary_mcu = chip;
                                        }
                                    }

                                    // Update hardware discovery with MCU info
                                    hardware_.set_mcu(primary_mcu);
                                    hardware_.set_mcu_list(mcu_list);
                                    hardware_.set_mcu_versions(*mcu_version_results);

                                    if (!primary_mcu.empty()) {
                                        spdlog::info("[Moonraker Client] Primary MCU: {}",
                                                     primary_mcu);
                                    }
                                    if (mcu_list.size() > 1) {
                                        spdlog::info("[Moonraker Client] All MCUs: {}",
                                                     json(mcu_list).dump());
                                    }

                                    // Continue to subscription step
                                    complete_discovery_subscription(on_complete);
                                }
                            },
                            [this, on_complete, mcu_obj,
                             pending_mcu_queries](const MoonrakerError& err) {
                                spdlog::warn("[Moonraker Client] MCU query for '{}' failed: {}",
                                             mcu_obj, err.message);

                                // Check if all queries complete (even on error)
                                if (pending_mcu_queries->fetch_sub(1) == 1) {
                                    // Continue to subscription step even if some MCU queries failed
                                    complete_discovery_subscription(on_complete);
                                }
                            });
                    }
                });
            });
        },
        [this, on_error](const MoonrakerError& err) {
            spdlog::error("[Moonraker Client] printer.objects.list request failed: {}",
                          err.message);
            emit_event(MoonrakerEventType::DISCOVERY_FAILED, err.message, true);
            spdlog::debug("[Moonraker Client] Invoking discovery on_error callback, on_error={}",
                          on_error ? "valid" : "null");
            if (on_error) {
                on_error(err.message);
            }
        });
}

void MoonrakerClient::complete_discovery_subscription(std::function<void()> on_complete) {
    // Step 5: Subscribe to all discovered objects + core objects
    json subscription_objects;

    // Core non-optional objects
    subscription_objects["print_stats"] = nullptr;
    subscription_objects["virtual_sdcard"] = nullptr;
    subscription_objects["toolhead"] = nullptr;
    subscription_objects["gcode_move"] = nullptr;
    subscription_objects["motion_report"] = nullptr;
    subscription_objects["system_stats"] = nullptr;

    // All discovered heaters (extruders, beds, generic heaters)
    for (const auto& heater : heaters_) {
        subscription_objects[heater] = nullptr;
    }

    // All discovered sensors
    for (const auto& sensor : sensors_) {
        subscription_objects[sensor] = nullptr;
    }

    // All discovered fans
    spdlog::info("[Moonraker Client] Subscribing to {} fans: {}", fans_.size(), json(fans_).dump());
    for (const auto& fan : fans_) {
        subscription_objects[fan] = nullptr;
    }

    // All discovered LEDs
    for (const auto& led : leds_) {
        subscription_objects[led] = nullptr;
    }

    // All discovered LED effects (for tracking active/enabled state)
    for (const auto& effect : hardware_.led_effects()) {
        subscription_objects[effect] = nullptr;
    }

    // Bed mesh (for 3D visualization)
    subscription_objects["bed_mesh"] = nullptr;

    // Exclude object (for mid-print object exclusion)
    subscription_objects["exclude_object"] = nullptr;

    // Manual probe (for Z-offset calibration - PROBE_CALIBRATE, Z_ENDSTOP_CALIBRATE)
    subscription_objects["manual_probe"] = nullptr;

    // Stepper enable state (for motor enabled/disabled detection - updates immediately on M84)
    subscription_objects["stepper_enable"] = nullptr;

    // Idle timeout (for printer activity state - Ready/Printing/Idle)
    subscription_objects["idle_timeout"] = nullptr;

    // All discovered AFC objects (AFC, AFC_stepper, AFC_hub, AFC_extruder)
    // These provide lane status, sensor states, and filament info for MMU support
    for (const auto& afc_obj : afc_objects_) {
        subscription_objects[afc_obj] = nullptr;
    }

    // All discovered filament sensors (filament_switch_sensor, filament_motion_sensor)
    // These provide runout detection and encoder motion data
    for (const auto& sensor : filament_sensors_) {
        subscription_objects[sensor] = nullptr;
    }

    // All discovered tool objects (for toolchanger support)
    if (hardware_.has_tool_changer()) {
        subscription_objects["toolchanger"] = nullptr;
        for (const auto& tool_name : hardware_.tool_names()) {
            subscription_objects["tool " + tool_name] = nullptr;
        }
        spdlog::info("[Moonraker Client] Subscribing to toolchanger + {} tool objects",
                     hardware_.tool_names().size());
    }

    // Firmware retraction settings (if printer has firmware_retraction module)
    if (hardware_.has_firmware_retraction()) {
        subscription_objects["firmware_retraction"] = nullptr;
    }

    // Print start macros (for detecting when prep phase completes)
    // These are optional - printers without these macros will silently not receive updates
    // AD5M/KAMP macros:
    subscription_objects["gcode_macro _START_PRINT"] = nullptr;
    subscription_objects["gcode_macro START_PRINT"] = nullptr;
    // HelixScreen custom macro:
    subscription_objects["gcode_macro _HELIX_STATE"] = nullptr;

    json subscribe_params = {{"objects", subscription_objects}};

    send_jsonrpc(
        "printer.objects.subscribe", subscribe_params,
        [this, on_complete, subscription_objects](json sub_response) {
            if (sub_response.contains("result")) {
                spdlog::info("[Moonraker Client] Subscription complete: {} objects subscribed",
                             subscription_objects.size());

                // Process initial state from subscription response
                // Moonraker returns current values in result.status
                if (sub_response["result"].contains("status")) {
                    const auto& status = sub_response["result"]["status"];
                    spdlog::info(
                        "[Moonraker Client] Processing initial printer state from subscription");

                    // DEBUG: Log print_stats specifically to diagnose startup sync issues
                    if (status.contains("print_stats")) {
                        spdlog::info("[Moonraker Client] INITIAL print_stats: {}",
                                     status["print_stats"].dump());
                    } else {
                        spdlog::warn("[Moonraker Client] INITIAL status has NO print_stats!");
                    }

                    dispatch_status_update(status);
                }
            } else if (sub_response.contains("error")) {
                spdlog::error("[Moonraker Client] Subscription failed: {}",
                              sub_response["error"].dump());

                // Emit discovery failed event (subscription is part of discovery)
                std::string error_msg = sub_response["error"].dump();
                emit_event(MoonrakerEventType::DISCOVERY_FAILED,
                           fmt::format("Failed to subscribe to printer updates: {}", error_msg),
                           false); // Warning, not error - discovery still completes
            }

            // Discovery complete - notify observers
            if (on_discovery_complete_) {
                on_discovery_complete_(hardware_);
            }
            on_complete();
        });
}

void MoonrakerClient::parse_objects(const json& objects) {
    // Populate unified hardware discovery (Phase 2)
    hardware_.parse_objects(objects);

    heaters_.clear();
    sensors_.clear();
    fans_.clear();
    leds_.clear();
    steppers_.clear();
    afc_objects_.clear();
    filament_sensors_.clear();

    // Collect printer_objects for hardware_ as we iterate
    std::vector<std::string> all_objects;
    all_objects.reserve(objects.size());

    for (const auto& obj : objects) {
        std::string name = obj.template get<std::string>();

        // Store all objects for detection heuristics (object_exists, macro_match)
        all_objects.push_back(name);

        // Steppers (stepper_x, stepper_y, stepper_z, stepper_z1, etc.)
        if (name.rfind("stepper_", 0) == 0) {
            steppers_.push_back(name);
        }
        // Extruders (controllable heaters)
        // Match "extruder", "extruder1", etc., but NOT "extruder_stepper"
        else if (name.rfind("extruder", 0) == 0 && name.rfind("extruder_stepper", 0) != 0) {
            heaters_.push_back(name);
        }
        // Heated bed
        else if (name == "heater_bed") {
            heaters_.push_back(name);
        }
        // Generic heaters (e.g., "heater_generic chamber")
        else if (name.rfind("heater_generic ", 0) == 0) {
            heaters_.push_back(name);
        }
        // Read-only temperature sensors
        else if (name.rfind("temperature_sensor ", 0) == 0) {
            sensors_.push_back(name);
        }
        // Temperature-controlled fans (also act as sensors)
        else if (name.rfind("temperature_fan ", 0) == 0) {
            sensors_.push_back(name);
            fans_.push_back(name); // Also add to fans for control
        }
        // Part cooling fan
        else if (name == "fan") {
            fans_.push_back(name);
        }
        // Heater fans (e.g., "heater_fan hotend_fan")
        else if (name.rfind("heater_fan ", 0) == 0) {
            fans_.push_back(name);
        }
        // Generic fans
        else if (name.rfind("fan_generic ", 0) == 0) {
            fans_.push_back(name);
        }
        // Controller fans
        else if (name.rfind("controller_fan ", 0) == 0) {
            fans_.push_back(name);
        }
        // Output pins - classify as fan or LED based on name keywords
        else if (name.rfind("output_pin ", 0) == 0) {
            std::string lower_name = name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
            if (lower_name.find("fan") != std::string::npos) {
                fans_.push_back(name);
            } else if (lower_name.find("light") != std::string::npos ||
                       lower_name.find("led") != std::string::npos ||
                       lower_name.find("lamp") != std::string::npos) {
                leds_.push_back(name);
            }
        }
        // LED outputs
        else if (name.rfind("led ", 0) == 0 || name.rfind("neopixel ", 0) == 0 ||
                 name.rfind("dotstar ", 0) == 0) {
            leds_.push_back(name);
        }
        // AFC MMU objects (AFC_stepper, AFC_hub, AFC_extruder, AFC, AFC_lane, AFC_BoxTurtle,
        // AFC_OpenAMS, AFC_buffer) These need subscription for lane state, sensor data, and
        // filament info
        else if (name == "AFC" || name.rfind("AFC_stepper ", 0) == 0 ||
                 name.rfind("AFC_hub ", 0) == 0 || name.rfind("AFC_extruder ", 0) == 0 ||
                 name.rfind("AFC_lane ", 0) == 0 || name.rfind("AFC_BoxTurtle ", 0) == 0 ||
                 name.rfind("AFC_OpenAMS ", 0) == 0 || name.rfind("AFC_buffer ", 0) == 0) {
            afc_objects_.push_back(name);
        }
        // Filament sensors (switch or motion type)
        // These provide runout detection and encoder motion data
        else if (name.rfind("filament_switch_sensor ", 0) == 0 ||
                 name.rfind("filament_motion_sensor ", 0) == 0) {
            filament_sensors_.push_back(name);
        }
    }

    spdlog::debug("[Moonraker Client] Discovered: {} heaters, {} sensors, {} fans, {} LEDs, {} "
                  "steppers, {} AFC objects, {} filament sensors",
                  heaters_.size(), sensors_.size(), fans_.size(), leds_.size(), steppers_.size(),
                  afc_objects_.size(), filament_sensors_.size());

    // Debug output of discovered objects
    if (!heaters_.empty()) {
        spdlog::debug("[Moonraker Client] Heaters: {}", json(heaters_).dump());
    }
    if (!sensors_.empty()) {
        spdlog::debug("[Moonraker Client] Sensors: {}", json(sensors_).dump());
    }
    if (!fans_.empty()) {
        spdlog::debug("[Moonraker Client] Fans: {}", json(fans_).dump());
    }
    if (!leds_.empty()) {
        spdlog::debug("[Moonraker Client] LEDs: {}", json(leds_).dump());
    }
    if (!steppers_.empty()) {
        spdlog::debug("[Moonraker Client] Steppers: {}", json(steppers_).dump());
    }
    if (!afc_objects_.empty()) {
        spdlog::info("[Moonraker Client] AFC objects: {}", json(afc_objects_).dump());
    }
    if (!filament_sensors_.empty()) {
        spdlog::info("[Moonraker Client] Filament sensors: {}", json(filament_sensors_).dump());
    }

    // Store printer objects in hardware discovery (handles all capability parsing)
    hardware_.set_printer_objects(all_objects);
}

void MoonrakerClient::parse_bed_mesh(const json& bed_mesh) {
    // Invoke bed mesh callback for API layer
    // The API layer (MoonrakerAPI) owns the bed mesh data; Client is just the transport
    std::function<void(const json&)> callback_copy;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callback_copy = bed_mesh_callback_;
    }
    if (callback_copy) {
        try {
            callback_copy(bed_mesh);
        } catch (const std::exception& e) {
            spdlog::error("[Moonraker Client] Bed mesh callback threw exception: {}", e.what());
        }
    }
}

void MoonrakerClient::check_request_timeouts() {
    // Two-phase pattern: collect callbacks under lock, invoke outside lock
    // This prevents deadlock if callback tries to send new request
    std::vector<std::function<void()>> timed_out_callbacks;

    // Phase 1: Find timed out requests and copy callbacks (under lock)
    {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        std::vector<uint64_t> timed_out_ids;

        for (auto& [id, request] : pending_requests_) {
            if (request.is_timed_out()) {
                spdlog::warn("[Moonraker Client] Request {} ({}) timed out after {}ms", id,
                             request.method, request.get_elapsed_ms());

                // Emit timeout event
                std::string method_name = request.method;
                uint32_t timeout = request.timeout_ms;
                emit_event(
                    MoonrakerEventType::REQUEST_TIMEOUT,
                    fmt::format("Printer command '{}' timed out after {}ms", method_name, timeout),
                    false, method_name);

                // Capture callback in lambda if present
                if (request.error_callback) {
                    MoonrakerError error =
                        MoonrakerError::timeout(request.method, request.timeout_ms);
                    timed_out_callbacks.push_back([cb = request.error_callback, error,
                                                   method_name]() {
                        try {
                            cb(error);
                        } catch (const std::exception& e) {
                            LOG_ERROR_INTERNAL("[Moonraker Client] Timeout error callback for {} "
                                               "threw exception: {}",
                                               method_name, e.what());
                        } catch (...) {
                            LOG_ERROR_INTERNAL("[Moonraker Client] Timeout error callback for {} "
                                               "threw unknown exception",
                                               method_name);
                        }
                    });
                }

                timed_out_ids.push_back(id);
            }
        }

        // Remove timed out requests while still holding lock
        for (uint64_t id : timed_out_ids) {
            pending_requests_.erase(id);
        }
    } // Lock released here

    // Phase 2: Invoke callbacks outside lock (safe - callbacks can call send_jsonrpc)
    for (auto& callback : timed_out_callbacks) {
        callback();
    }
}

void MoonrakerClient::cleanup_pending_requests() {
    // Two-phase pattern: collect callbacks under lock, invoke outside lock
    // This prevents deadlock if callback tries to send new request
    std::vector<std::function<void()>> cleanup_callbacks;

    // Phase 1: Copy callbacks and clear map (under lock)
    {
        std::lock_guard<std::mutex> lock(requests_mutex_);

        if (!pending_requests_.empty()) {
            spdlog::debug("[Moonraker Client] Cleaning up {} pending requests due to disconnect",
                          pending_requests_.size());

            // Capture callbacks in lambdas
            for (auto& [id, request] : pending_requests_) {
                if (request.error_callback) {
                    MoonrakerError error = MoonrakerError::connection_lost(request.method);
                    std::string method_name = request.method;
                    cleanup_callbacks.push_back([cb = request.error_callback, error,
                                                 method_name]() {
                        try {
                            cb(error);
                        } catch (const std::exception& e) {
                            LOG_ERROR_INTERNAL("[Moonraker Client] Cleanup error callback for {} "
                                               "threw exception: {}",
                                               method_name, e.what());
                        } catch (...) {
                            LOG_ERROR_INTERNAL("[Moonraker Client] Cleanup error callback for {} "
                                               "threw unknown exception",
                                               method_name);
                        }
                    });
                }
            }

            pending_requests_.clear();
        }
    } // Lock released here

    // Phase 2: Invoke callbacks outside lock (safe - callbacks can call send_jsonrpc)
    for (auto& callback : cleanup_callbacks) {
        callback();
    }
}
