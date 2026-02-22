// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_request_tracker.h"

#include "ui_error_reporting.h"

#include "abort_manager.h"
#include "hv/WebSocketClient.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include "hv/json.hpp"

namespace helix {

RequestId MoonrakerRequestTracker::send(hv::WebSocketClient& ws, const std::string& method,
                                        const json& params, std::function<void(json)> success_cb,
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
            LOG_ERROR_INTERNAL("[Request Tracker] Request ID {} already has a registered callback",
                               id);
            return INVALID_REQUEST_ID;
        }
        pending_requests_.insert({id, request});
        spdlog::trace("[Request Tracker] Registered request {} for method {}, total pending: {}",
                      id, method, pending_requests_.size());
    }

    // Build and send JSON-RPC message with the registered ID
    json rpc;
    rpc["jsonrpc"] = "2.0";
    rpc["method"] = method;
    rpc["id"] = id;

    // Only include params if not null or empty
    if (!params.is_null() && !params.empty()) {
        rpc["params"] = params;
    }

    spdlog::trace("[Request Tracker] send: {}", rpc.dump());
    int result = ws.send(rpc.dump());
    spdlog::trace("[Request Tracker] send({}) returned {}", method, result);

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
        spdlog::error("[Request Tracker] Failed to send request {} ({}), removed from pending", id,
                      method_name.empty() ? "unknown" : method_name);

        // Invoke error callback outside lock (prevents deadlock if callback sends new request)
        if (error_callback_copy) {
            try {
                error_callback_copy(MoonrakerError::connection_lost(method_name));
            } catch (const std::exception& e) {
                spdlog::error("[Request Tracker] Error callback threw exception: {}", e.what());
            }
        }
        return INVALID_REQUEST_ID;
    }

    return id;
}

int MoonrakerRequestTracker::send_fire_and_forget(hv::WebSocketClient& ws,
                                                  const std::string& method, const json& params) {
    json rpc;
    rpc["jsonrpc"] = "2.0";
    rpc["method"] = method;
    rpc["id"] = request_id_.fetch_add(1) + 1;

    // Only include params if not null or empty
    if (!params.is_null() && !params.empty()) {
        rpc["params"] = params;
    }

    spdlog::trace("[Request Tracker] send_fire_and_forget: {}", rpc.dump());
    int result = ws.send(rpc.dump());
    return result < 0 ? result : 0;
}

bool MoonrakerRequestTracker::route_response(
    const json& msg,
    std::function<void(MoonrakerEventType, const std::string&, bool, const std::string&)>
        emit_event) {
    // Check if this is a response message (has "id" field)
    if (!msg.contains("id")) {
        return false;
    }

    // Validate 'id' field type
    if (!msg["id"].is_number_integer()) {
        LOG_ERROR_INTERNAL("[Request Tracker] Invalid 'id' type in response: {}",
                           msg["id"].type_name());
        return false;
    }

    uint64_t id = msg["id"].get<uint64_t>();

    spdlog::trace("[Request Tracker] Got response for id={}", id);

    // Copy callbacks out before invoking to avoid deadlock
    std::function<void(json)> success_cb;
    std::function<void(const MoonrakerError&)> error_cb;
    std::string method_name;
    bool has_error = false;
    bool is_silent = false;
    bool found = false;
    MoonrakerError error;

    {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        auto it = pending_requests_.find(id);
        if (it != pending_requests_.end()) {
            found = true;
            PendingRequest& request = it->second;
            method_name = request.method;
            is_silent = request.silent;

            // Check for JSON-RPC error
            if (msg.contains("error")) {
                has_error = true;
                error = MoonrakerError::from_json_rpc(msg["error"], request.method);
                error_cb = request.error_callback;
            } else {
                success_cb = request.success_callback;
            }

            pending_requests_.erase(it);
        }
    } // Lock released here

    if (!found) {
        return false;
    }

    // Invoke callbacks outside the lock to avoid deadlock
    if (has_error) {
        // Suppress toast notifications during shutdown handling to avoid
        // confusing errors appearing behind the abort modal
        bool suppress_toast = AbortManager::instance().is_handling_shutdown();

        if (!is_silent && !suppress_toast) {
            spdlog::error("[Request Tracker] Request {} failed: {}", method_name, error.message);

            // Emit RPC error event (only for non-silent requests)
            emit_event(MoonrakerEventType::RPC_ERROR,
                       fmt::format("Printer command '{}' failed: {}", method_name, error.message),
                       true, method_name);
        } else if (suppress_toast) {
            spdlog::debug("[Request Tracker] Request {} failed during shutdown (suppressed): {}",
                          method_name, error.message);
        } else {
            spdlog::debug("[Request Tracker] Silent request {} failed: {}", method_name,
                          error.message);
        }

        if (error_cb) {
            error_cb(error);
        }
    } else if (success_cb) {
        try {
            success_cb(msg);
        } catch (const std::exception& e) {
            LOG_ERROR_INTERNAL("[Request Tracker] Success callback for '{}' threw exception: {}",
                               method_name, e.what());
            // Do NOT re-throw: stack unwinding between here and the outer
            // handler can leave libhv's event loop in a corrupt state,
            // leading to SIGSEGV on the next message cycle.
        }
    }

    return true;
}

bool MoonrakerRequestTracker::cancel(RequestId id) {
    if (id == INVALID_REQUEST_ID) {
        return false;
    }

    std::lock_guard<std::mutex> lock(requests_mutex_);
    auto it = pending_requests_.find(id);
    if (it != pending_requests_.end()) {
        spdlog::debug("[Request Tracker] Cancelled request {} ({})", id, it->second.method);
        pending_requests_.erase(it);
        return true;
    }

    spdlog::debug("[Request Tracker] Cancel failed: request {} not found (already completed?)", id);
    return false;
}

void MoonrakerRequestTracker::check_timeouts(
    std::function<void(MoonrakerEventType, const std::string&, bool, const std::string&)>
        emit_event) {
    // Two-phase pattern: collect events and callbacks under lock, invoke outside lock
    // This prevents deadlock if event handler or callback tries to send new request
    struct TimeoutInfo {
        std::string method_name;
        uint32_t timeout_ms;
        std::function<void()> error_callback;
    };
    std::vector<TimeoutInfo> timed_out;

    // Phase 1: Find timed out requests and copy data (under lock)
    {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        std::vector<uint64_t> timed_out_ids;

        for (auto& [id, request] : pending_requests_) {
            if (request.is_timed_out()) {
                spdlog::warn("[Request Tracker] Request {} ({}) timed out after {}ms", id,
                             request.method, request.get_elapsed_ms());

                TimeoutInfo info;
                info.method_name = request.method;
                info.timeout_ms = request.timeout_ms;

                // Capture error callback in lambda if present
                if (request.error_callback) {
                    MoonrakerError error =
                        MoonrakerError::timeout(request.method, request.timeout_ms);
                    std::string method_name = request.method;
                    info.error_callback = [cb = request.error_callback, error, method_name]() {
                        try {
                            cb(error);
                        } catch (const std::exception& e) {
                            LOG_ERROR_INTERNAL("[Request Tracker] Timeout error callback for {} "
                                               "threw exception: {}",
                                               method_name, e.what());
                        } catch (...) {
                            LOG_ERROR_INTERNAL("[Request Tracker] Timeout error callback for {} "
                                               "threw unknown exception",
                                               method_name);
                        }
                    };
                }

                timed_out.push_back(std::move(info));
                timed_out_ids.push_back(id);
            }
        }

        // Remove timed out requests while still holding lock
        for (uint64_t id : timed_out_ids) {
            pending_requests_.erase(id);
        }
    } // Lock released here

    // Phase 2: Emit events and invoke callbacks outside lock
    // (safe - event handlers and callbacks can call send without deadlock)
    for (auto& info : timed_out) {
        emit_event(MoonrakerEventType::REQUEST_TIMEOUT,
                   fmt::format("Printer command '{}' timed out after {}ms", info.method_name,
                               info.timeout_ms),
                   false, info.method_name);

        if (info.error_callback) {
            info.error_callback();
        }
    }
}

void MoonrakerRequestTracker::cleanup_all() {
    // Two-phase pattern: collect callbacks under lock, invoke outside lock
    // This prevents deadlock if callback tries to send new request
    std::vector<std::function<void()>> cleanup_callbacks;

    // Phase 1: Copy callbacks and clear map (under lock)
    {
        std::lock_guard<std::mutex> lock(requests_mutex_);

        if (!pending_requests_.empty()) {
            spdlog::debug("[Request Tracker] Cleaning up {} pending requests due to disconnect",
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
                            LOG_ERROR_INTERNAL("[Request Tracker] Cleanup error callback for {} "
                                               "threw exception: {}",
                                               method_name, e.what());
                        } catch (...) {
                            LOG_ERROR_INTERNAL("[Request Tracker] Cleanup error callback for {} "
                                               "threw unknown exception",
                                               method_name);
                        }
                    });
                }
            }

            pending_requests_.clear();
        }
    } // Lock released here

    // Phase 2: Invoke callbacks outside lock (safe - callbacks can call send)
    for (auto& callback : cleanup_callbacks) {
        callback();
    }
}

} // namespace helix
