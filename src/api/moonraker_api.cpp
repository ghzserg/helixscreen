// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_api.h"

#include "moonraker_api_internal.h"
#include "spdlog/spdlog.h"

#include <chrono>
#include <cmath>
#include <sstream>
#include <thread>

using namespace helix;

using namespace moonraker_internal;

// ============================================================================
// MoonrakerAPI Implementation
// ============================================================================

MoonrakerAPI::MoonrakerAPI(MoonrakerClient& client, PrinterState& state) : client_(client) {
    // state parameter reserved for future use
    (void)state;

    // Create sub-APIs
    history_api_ = std::make_unique<MoonrakerHistoryAPI>(client);
    spoolman_api_ = std::make_unique<MoonrakerSpoolmanAPI>(client);
    timelapse_api_ = std::make_unique<MoonrakerTimelapseAPI>(client, http_base_url_);

    // Initialize build_volume_version subject for change notifications
    lv_subject_init_int(&build_volume_version_, 0);

    // Wire up hardware discovery callbacks: Client pushes data to API during discovery
    client_.set_on_hardware_discovered([this](const helix::PrinterDiscovery& hw) {
        hardware_ = hw;
        spdlog::debug("[MoonrakerAPI] Hardware discovered: {} heaters, {} fans, {} sensors",
                      hardware_.heaters().size(), hardware_.fans().size(),
                      hardware_.sensors().size());
    });

    client_.set_on_discovery_complete([this](const helix::PrinterDiscovery& hw) {
        hardware_ = hw;
        spdlog::debug("[MoonrakerAPI] Discovery complete: hostname='{}', kinematics='{}'",
                      hardware_.hostname(), hardware_.kinematics());
    });

    // Wire up bed mesh callback: Client pushes data to API when it arrives from WebSocket
    client_.set_bed_mesh_callback(
        [this](const json& bed_mesh) { this->update_bed_mesh(bed_mesh); });
}

MoonrakerAPI::~MoonrakerAPI() {
    // Deinit LVGL subject before destruction to prevent dangling observer crashes
    // (same pattern as StaticSubjectRegistry — observers must be disconnected before lv_deinit)
    lv_subject_deinit(&build_volume_version_);

    // Signal shutdown and wait for HTTP threads with timeout
    // File downloads/uploads can have long timeouts (up to 1 hour in libhv),
    // so we use a timed join to avoid blocking shutdown indefinitely.
    shutting_down_.store(true);

    std::list<std::thread> threads_to_join;
    {
        std::lock_guard<std::mutex> lock(http_threads_mutex_);
        threads_to_join = std::move(http_threads_);
    }

    if (threads_to_join.empty()) {
        return;
    }

    spdlog::debug("[Moonraker API] Waiting for {} HTTP thread(s) to finish...",
                  threads_to_join.size());

    // Timed join pattern: use a helper thread to do the join, poll for completion.
    // We can't use std::async because its std::future destructor blocks!
    constexpr auto kJoinTimeout = std::chrono::seconds(2);
    constexpr auto kPollInterval = std::chrono::milliseconds(10);

    for (auto& t : threads_to_join) {
        if (!t.joinable()) {
            continue;
        }

        // Launch helper thread to do the join
        std::atomic<bool> joined{false};
        std::thread join_helper([&t, &joined]() {
            t.join();
            joined.store(true);
        });

        // Poll for completion with timeout
        auto start = std::chrono::steady_clock::now();
        while (!joined.load()) {
            if (std::chrono::steady_clock::now() - start > kJoinTimeout) {
                // Timeout - detach BOTH the helper AND the original thread
                // We must detach `t` to avoid std::terminate when http_threads_ is destroyed
                // L033 warns about detach on ARM/glibc with static linking, but:
                // 1. This only happens during shutdown with stuck HTTP requests
                // 2. The alternative (blocking forever or std::terminate) is worse
                // 3. Most deployments use dynamic linking
                spdlog::warn("[Moonraker API] HTTP thread still running after {}s - "
                             "will terminate with process",
                             kJoinTimeout.count());
                join_helper.detach();
                t.detach(); // Critical: avoid std::terminate on list destruction
                break;
            }
            std::this_thread::sleep_for(kPollInterval);
        }

        // Use joinable() check - returns false after detach(), preventing UB
        // from calling join() on a detached thread (race between timeout and
        // helper completion)
        if (join_helper.joinable()) {
            join_helper.join();
        }
    }
}

bool MoonrakerAPI::ensure_http_base_url() {
    if (!http_base_url_.empty()) {
        return true;
    }

    // Try to derive from WebSocket URL
    const std::string& ws_url = client_.get_last_url();
    if (!ws_url.empty() && ws_url.find("ws://") == 0) {
        // Convert ws://host:port/websocket -> http://host:port
        std::string host_port = ws_url.substr(5); // Skip "ws://"
        auto slash_pos = host_port.find('/');
        if (slash_pos != std::string::npos) {
            host_port = host_port.substr(0, slash_pos);
        }
        http_base_url_ = "http://" + host_port;
        spdlog::info("[Moonraker API] Auto-derived HTTP base URL from WebSocket: {}",
                     http_base_url_);
        return true;
    }

    spdlog::error("[Moonraker API] HTTP base URL not configured and cannot derive from WebSocket");
    return false;
}

void MoonrakerAPI::launch_http_thread(std::function<void()> func) {
    // Check if we're shutting down - don't spawn new threads
    if (shutting_down_.load()) {
        return;
    }

    std::lock_guard<std::mutex> lock(http_threads_mutex_);

    // Clean up any finished threads first (they've set themselves to non-joinable id)
    http_threads_.remove_if([](std::thread& t) { return !t.joinable(); });

    // Launch the new thread
    http_threads_.emplace_back([func = std::move(func)]() {
        func();
        // Thread auto-removed during next launch or destructor
    });
}

void MoonrakerAPI::notify_build_volume_changed() {
    // Increment version counter to notify observers
    build_volume_version_counter_++;
    lv_subject_set_int(&build_volume_version_, build_volume_version_counter_);
    spdlog::debug("[MoonrakerAPI] Build volume changed, version={}", build_volume_version_counter_);
}

// ============================================================================
// G-code Generation Helpers
// ============================================================================

std::string MoonrakerAPI::generate_home_gcode(const std::string& axes) {
    if (axes.empty()) {
        return "G28"; // Home all axes
    } else {
        std::ostringstream gcode;
        gcode << "G28";
        for (char axis : axes) {
            gcode << " " << static_cast<char>(std::toupper(axis));
        }
        return gcode.str();
    }
}

std::string MoonrakerAPI::generate_move_gcode(char axis, double distance, double feedrate) {
    if (std::isnan(distance) || std::isinf(distance) || std::isnan(feedrate) ||
        std::isinf(feedrate)) {
        spdlog::warn("[Moonraker API] generate_move_gcode: Rejecting G-code generation: "
                     "invalid value (NaN/Inf)");
        return "";
    }

    std::ostringstream gcode;
    gcode << "G91\n"; // Relative positioning
    gcode << "G0 " << static_cast<char>(std::toupper(axis)) << distance;
    if (feedrate > 0) {
        gcode << " F" << feedrate;
    }
    gcode << "\nG90"; // Back to absolute positioning
    return gcode.str();
}

std::string MoonrakerAPI::generate_absolute_move_gcode(char axis, double position,
                                                       double feedrate) {
    if (std::isnan(position) || std::isinf(position) || std::isnan(feedrate) ||
        std::isinf(feedrate)) {
        spdlog::warn("[Moonraker API] generate_absolute_move_gcode: Rejecting G-code generation: "
                     "invalid value (NaN/Inf)");
        return "";
    }

    std::ostringstream gcode;
    gcode << "G90\n"; // Absolute positioning
    gcode << "G0 " << static_cast<char>(std::toupper(axis)) << position;
    if (feedrate > 0) {
        gcode << " F" << feedrate;
    }
    return gcode.str();
}

// ============================================================================
// Connection and Subscription Proxies
// ============================================================================

bool MoonrakerAPI::is_connected() const {
    return client_.get_connection_state() == ConnectionState::CONNECTED;
}

ConnectionState MoonrakerAPI::get_connection_state() const {
    return client_.get_connection_state();
}

std::string MoonrakerAPI::get_websocket_url() const {
    return client_.get_last_url();
}

SubscriptionId MoonrakerAPI::subscribe_notifications(std::function<void(json)> callback) {
    return client_.register_notify_update(std::move(callback));
}

bool MoonrakerAPI::unsubscribe_notifications(SubscriptionId id) {
    return client_.unsubscribe_notify_update(id);
}

std::weak_ptr<bool> MoonrakerAPI::client_lifetime_weak() const {
    return client_.lifetime_weak();
}

void MoonrakerAPI::register_method_callback(const std::string& method, const std::string& name,
                                            std::function<void(json)> callback) {
    client_.register_method_callback(method, name, std::move(callback));
}

bool MoonrakerAPI::unregister_method_callback(const std::string& method, const std::string& name) {
    return client_.unregister_method_callback(method, name);
}

void MoonrakerAPI::suppress_disconnect_modal(uint32_t duration_ms) {
    client_.suppress_disconnect_modal(duration_ms);
}

void MoonrakerAPI::get_gcode_store(
    int count, std::function<void(const std::vector<GcodeStoreEntry>&)> on_success,
    std::function<void(const MoonrakerError&)> on_error) {
    client_.get_gcode_store(count, std::move(on_success), std::move(on_error));
}

// ============================================================================
// Helix Plugin Operations
// ============================================================================

void MoonrakerAPI::get_phase_tracking_status(std::function<void(bool enabled)> on_success,
                                             ErrorCallback on_error) {
    client_.send_jsonrpc(
        "server.helix.phase_tracking.status", json::object(),
        [on_success](const json& result) {
            bool enabled = result.value("enabled", false);
            if (on_success)
                on_success(enabled);
        },
        [on_error](const MoonrakerError& err) {
            if (on_error)
                on_error(err);
        },
        0,     // timeout_ms: use default
        true); // silent: suppress RPC_ERROR events/toasts
}

void MoonrakerAPI::set_phase_tracking_enabled(bool enabled,
                                              std::function<void(bool success)> on_success,
                                              ErrorCallback on_error) {
    std::string method =
        enabled ? "server.helix.phase_tracking.enable" : "server.helix.phase_tracking.disable";
    client_.send_jsonrpc(
        method, json::object(),
        [on_success, on_error, method](const json& result) {
            bool ok = result.value("success", false);
            if (ok) {
                if (on_success)
                    on_success(true);
            } else {
                // Server returned a response but success=false - extract error detail
                std::string msg = result.value("message", std::string{});
                if (msg.empty()) {
                    msg = "Server returned success=false for " + method;
                }
                spdlog::warn("[MoonrakerAPI] {} failed: {}", method, msg);
                if (on_error) {
                    MoonrakerError err;
                    err.type = MoonrakerErrorType::JSON_RPC_ERROR;
                    err.message = msg;
                    err.method = method;
                    err.details = result;
                    on_error(err);
                } else if (on_success) {
                    on_success(false);
                }
            }
        },
        [on_error](const MoonrakerError& err) {
            if (on_error)
                on_error(err);
        });
}

// ============================================================================
// Database Operations
// ============================================================================

void MoonrakerAPI::database_get_item(const std::string& namespace_name, const std::string& key,
                                     std::function<void(const json&)> on_success,
                                     ErrorCallback on_error) {
    json params = {{"namespace", namespace_name}, {"key", key}};
    // Silent: missing keys are expected (first-time reads before any save).
    // Callers handle errors via their error callback — no need for a toast.
    client_.send_jsonrpc(
        "server.database.get_item", params,
        [on_success](const json& result) {
            if (on_success) {
                on_success(result.value("value", json{}));
            }
        },
        [on_error](const MoonrakerError& err) {
            if (on_error)
                on_error(err);
        },
        0,     // timeout_ms: use default
        true); // silent: suppress RPC_ERROR toast
}

void MoonrakerAPI::database_post_item(const std::string& namespace_name, const std::string& key,
                                      const json& value, std::function<void()> on_success,
                                      ErrorCallback on_error) {
    json params = {{"namespace", namespace_name}, {"key", key}, {"value", value}};
    client_.send_jsonrpc(
        "server.database.post_item", params,
        [on_success](const json&) {
            if (on_success)
                on_success();
        },
        [on_error](const MoonrakerError& err) {
            if (on_error)
                on_error(err);
        });
}
