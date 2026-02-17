// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file wifi_manager.cpp
 * @brief High-level WiFi operations manager wrapping platform backends
 *
 * @pattern Manager with weak self_ reference for callback safety
 * @threading Backend callbacks may run on background thread
 * @gotchas Uses fprintf in destructor instead of spdlog; clears callbacks BEFORE stopping backend
 *
 * @see wifi_backend.cpp
 */

#include "wifi_manager.h"

#include "ui_error_reporting.h"
#include "ui_update_queue.h"

#include "lvgl/lvgl.h"
#include "safe_log.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

using namespace helix;

// ============================================================================
// Constructor / Destructor
// ============================================================================

WiFiManager::WiFiManager(bool silent) : scan_timer_(nullptr), scan_pending_(false) {
    spdlog::debug("[WiFiManager] Initializing with backend system{}",
                  silent ? " (silent mode)" : "");

    // Create platform-appropriate backend (already started by factory)
    backend_ = WifiBackend::create(silent);
    if (!backend_) {
        if (!silent) {
            NOTIFY_ERROR_MODAL("WiFi Unavailable",
                               "Could not initialize WiFi hardware. Check system configuration.");
        } else {
            spdlog::debug("[WiFiManager] WiFi unavailable (silent mode - no modal)");
        }
        return;
    }

    // Check backend status immediately after creation
    if (backend_->is_running()) {
        spdlog::debug("[WiFiManager] WiFi backend initialized and running");
    } else if (!silent) {
        NOTIFY_WARNING("WiFi backend created but not running. Check system permissions.");
    }

    // Register event callbacks
    backend_->register_event_callback(
        "SCAN_COMPLETE", [this](const std::string& data) { handle_scan_complete(data); });
    backend_->register_event_callback("CONNECTED",
                                      [this](const std::string& data) { handle_connected(data); });
    backend_->register_event_callback(
        "DISCONNECTED", [this](const std::string& data) { handle_disconnected(data); });
    backend_->register_event_callback(
        "AUTH_FAILED", [this](const std::string& data) { handle_auth_failed(data); });
    backend_->register_event_callback("INIT_FAILED", [](const std::string& msg) {
        // Backend initialization failed asynchronously - notify user
        NOTIFY_ERROR("WiFi initialization failed: {}", msg);
    });
}

void WiFiManager::init_self_reference(std::shared_ptr<WiFiManager> self) {
    self_ = self;
    spdlog::debug("[WiFiManager] Self-reference initialized for async callback safety");
}

WiFiManager::~WiFiManager() {
    // Use fprintf - spdlog may be destroyed during static cleanup
    fprintf(stderr, "[WiFiManager] Destructor called\n");

    // Clean up scanning
    stop_scan();

    // Clear callbacks BEFORE stopping backend
    // Pending lv_async_call operations check for null callbacks before invoking
    scan_callback_ = nullptr;
    connect_callback_ = nullptr;

    // Stop backend (this stops backend threads)
    if (backend_) {
        backend_->stop();
    }
}

// ============================================================================
// Network Scanning
// ============================================================================

std::vector<WiFiNetwork> WiFiManager::scan_once() {
    if (!backend_) {
        LOG_WARN_INTERNAL("No backend available for scan");
        return {};
    }

    spdlog::debug("[WiFiManager] Performing single scan");

    // Trigger scan and wait briefly for results
    WiFiError scan_result = backend_->trigger_scan();
    if (!scan_result.success()) {
        LOG_WARN_INTERNAL("Failed to trigger scan: {}", scan_result.technical_msg);
        return {};
    }

    // For synchronous scan, we need to get existing results
    // Note: This may not include the just-triggered scan results immediately
    std::vector<WiFiNetwork> networks;
    WiFiError get_result = backend_->get_scan_results(networks);
    if (!get_result.success()) {
        LOG_WARN_INTERNAL("Failed to get scan results: {}", get_result.technical_msg);
        return {};
    }

    return networks;
}

void WiFiManager::start_scan(
    std::function<void(const std::vector<WiFiNetwork>&)> on_networks_updated) {
    if (!backend_) {
        NOTIFY_ERROR("WiFi unavailable. Cannot scan for networks.");
        return;
    }

    spdlog::debug("[WiFiManager] start_scan ENTRY, callback is {}",
                  on_networks_updated ? "NOT NULL" : "NULL");

    scan_callback_ = on_networks_updated;
    spdlog::debug("[WiFiManager] Scan callback registered");

    // Stop existing timer if running
    stop_scan();

    spdlog::info("[WiFiManager] Starting periodic network scan (every 7 seconds)");

    // Create timer for periodic scanning
    scan_timer_ = lv_timer_create(scan_timer_callback, 7000, this);
    spdlog::debug("[WiFiManager] Timer created: {}", (void*)scan_timer_);

    // Trigger immediate scan
    spdlog::debug("[WiFiManager] About to trigger initial scan");
    scan_pending_ = true; // Mark scan as pending - cleared after first SCAN_COMPLETE processed
    WiFiError scan_result = backend_->trigger_scan();
    if (!scan_result.success()) {
        scan_pending_ = false;
        NOTIFY_WARNING("WiFi scan failed. Try again.");
    } else {
        spdlog::debug("[WiFiManager] Initial scan triggered successfully");
    }
}

void WiFiManager::stop_scan() {
    if (scan_timer_ && lv_is_initialized()) {
        lv_timer_delete(scan_timer_);
        scan_timer_ = nullptr;
        spdlog::info("[WiFiManager] Stopped network scanning");
    }
    // Note: Callback is NOT cleared here - callers can clear it explicitly if needed
}

void WiFiManager::scan_timer_callback(lv_timer_t* timer) {
    WiFiManager* manager = static_cast<WiFiManager*>(lv_timer_get_user_data(timer));
    if (manager && manager->backend_) {
        // Trigger scan - results will arrive via SCAN_COMPLETE event
        manager->scan_pending_ = true; // Mark scan as pending
        WiFiError result = manager->backend_->trigger_scan();
        if (!result.success()) {
            manager->scan_pending_ = false;
            LOG_WARN_INTERNAL("Periodic scan failed: {}", result.technical_msg);
        }
    }
}

// ============================================================================
// Connection Management
// ============================================================================

void WiFiManager::connect(const std::string& ssid, const std::string& password,
                          std::function<void(bool success, const std::string& error)> on_complete) {
    if (!backend_) {
        NOTIFY_ERROR("WiFi unavailable. Cannot connect to network.");
        if (on_complete) {
            on_complete(false, "No WiFi backend available");
        }
        return;
    }

    spdlog::info("[WiFiManager] Connecting to '{}'", ssid);

    connect_callback_ = on_complete;
    connecting_in_progress_ = true; // Ignore DISCONNECTED events during connection attempt
    spdlog::debug("[WiFiManager] Connect callback registered for '{}'", ssid);

    // Use backend's connect method
    WiFiError result = backend_->connect_network(ssid, password);
    if (!result.success()) {
        connecting_in_progress_ = false; // Clear on sync failure
        NOTIFY_ERROR("Failed to connect to WiFi network '{}'", ssid);
        if (connect_callback_) {
            connect_callback_(false,
                              result.user_msg.empty() ? result.technical_msg : result.user_msg);
            connect_callback_ = nullptr;
        }
    }
    // Success/failure will be reported via CONNECTED/AUTH_FAILED events
}

void WiFiManager::disconnect() {
    if (!backend_) {
        LOG_WARN_INTERNAL("No backend available for disconnect");
        return;
    }

    spdlog::info("[WiFiManager] Disconnecting");
    WiFiError result = backend_->disconnect_network();
    if (!result.success()) {
        NOTIFY_WARNING("Could not disconnect from WiFi");
    }
}

// ============================================================================
// Status Queries
// ============================================================================

bool WiFiManager::is_connected() {
    if (!backend_)
        return false;

    WifiBackend::ConnectionStatus status = backend_->get_status();
    return status.connected;
}

std::string WiFiManager::get_connected_ssid() {
    if (!backend_)
        return "";

    WifiBackend::ConnectionStatus status = backend_->get_status();
    return status.ssid;
}

std::string WiFiManager::get_ip_address() {
    if (!backend_)
        return "";

    WifiBackend::ConnectionStatus status = backend_->get_status();
    return status.ip_address;
}

std::string WiFiManager::get_mac_address() {
    if (!backend_)
        return "";

    WifiBackend::ConnectionStatus status = backend_->get_status();
    return status.mac_address;
}

int WiFiManager::get_signal_strength() {
    if (!backend_)
        return 0;

    WifiBackend::ConnectionStatus status = backend_->get_status();
    return status.signal_strength;
}

bool WiFiManager::supports_5ghz() {
    if (!backend_)
        return false;

    return backend_->supports_5ghz();
}

// ============================================================================
// Hardware Detection (Legacy Compatibility)
// ============================================================================

bool WiFiManager::has_hardware() {
    // Backend creation handles hardware availability
    return (backend_ != nullptr);
}

bool WiFiManager::is_enabled() {
    if (!backend_)
        return false;
    return backend_->is_running();
}

bool WiFiManager::set_enabled(bool enabled) {
    if (!backend_)
        return false;

    spdlog::debug("[WiFiManager] set_enabled({})", enabled);

    if (enabled) {
        WiFiError result = backend_->start();
        if (!result.success()) {
            NOTIFY_ERROR("Failed to enable WiFi: {}",
                         result.user_msg.empty() ? result.technical_msg : result.user_msg);
        } else {
            spdlog::debug("[WiFiManager] WiFi backend started successfully");
        }
        return result.success();
    } else {
        backend_->stop();
        spdlog::debug("[WiFiManager] WiFi backend stopped");
        return true;
    }
}

// ============================================================================
// Event Handling
// ============================================================================

// Helper struct for async callback dispatch
// Uses weak_ptr to safely handle manager destruction before callback executes
struct ScanCallbackData {
    std::weak_ptr<WiFiManager> manager;
    std::vector<WiFiNetwork> networks;
};

void WiFiManager::handle_scan_complete(const std::string& event_data) {
    (void)event_data; // Unused for now

    spdlog::debug("[WiFiManager] handle_scan_complete ENTRY (backend thread)");

    // Debounce: wpa_supplicant can emit duplicate SCAN_RESULTS events
    // Only process the first one per scan cycle
    if (!scan_pending_) {
        spdlog::trace("[WiFiManager] Ignoring duplicate SCAN_COMPLETE (already processed)");
        return;
    }
    scan_pending_ = false; // Clear flag - subsequent events for this scan will be ignored

    if (!scan_callback_) {
        LOG_WARN_INTERNAL("Scan complete but no callback registered");
        return;
    }

    // CRITICAL: This is called from backend thread - must dispatch to LVGL thread!
    spdlog::debug("[WiFiManager] Scan callback is registered, fetching results");
    std::vector<WiFiNetwork> networks;
    WiFiError result = backend_->get_scan_results(networks);

    if (result.success()) {
        spdlog::debug("[WiFiManager] Got {} scan results, dispatching to LVGL thread",
                      networks.size());

        // Use RAII-safe async callback wrapper
        helix::ui::queue_update<ScanCallbackData>(
            std::make_unique<ScanCallbackData>(ScanCallbackData{self_, networks}),
            [](ScanCallbackData* data) {
                spdlog::debug("[WiFiManager] async_call executing in LVGL thread with {} networks",
                              data->networks.size());

                // Safely check if manager still exists
                if (auto manager = data->manager.lock()) {
                    if (manager->scan_callback_) {
                        manager->scan_callback_(data->networks);
                        spdlog::debug("[WiFiManager] scan_callback_ completed successfully");
                    } else {
                        spdlog::warn(
                            "[WiFiManager] scan_callback_ was cleared before async dispatch");
                    }
                } else {
                    spdlog::debug(
                        "[WiFiManager] Manager destroyed before async callback - safely ignored");
                }
            });

    } else {
        LOG_WARN_INTERNAL("Failed to get scan results: {}", result.technical_msg);

        // Use RAII-safe async callback wrapper
        helix::ui::queue_update<ScanCallbackData>(
            std::make_unique<ScanCallbackData>(ScanCallbackData{self_, {}}),
            [](ScanCallbackData* data) {
                LOG_WARN_INTERNAL("async_call: calling callback with empty results");
                if (auto manager = data->manager.lock()) {
                    if (manager->scan_callback_) {
                        manager->scan_callback_({});
                    }
                } else {
                    spdlog::debug(
                        "[WiFiManager] Manager destroyed before async callback - safely ignored");
                }
            });
    }

    spdlog::debug("[WiFiManager] handle_scan_complete EXIT (dispatch queued)");
}

// Helper struct for connection callback dispatch
// Uses weak_ptr to safely handle manager destruction before callback executes
struct ConnectCallbackData {
    std::weak_ptr<WiFiManager> manager;
    bool success;
    std::string error;
};

void WiFiManager::handle_connected(const std::string& event_data) {
    (void)event_data; // Could parse IP address from event data

    spdlog::debug("[WiFiManager] Connected event received (backend thread)");

    connecting_in_progress_ = false; // Connection complete

    if (!connect_callback_) {
        spdlog::debug(
            "[WiFiManager] Connected event but no callback registered (normal on startup)");
        return;
    }

    // Use RAII-safe async callback wrapper
    helix::ui::queue_update<ConnectCallbackData>(
        std::make_unique<ConnectCallbackData>(ConnectCallbackData{self_, true, ""}),
        [](ConnectCallbackData* d) {
            if (auto manager = d->manager.lock()) {
                if (manager->connect_callback_) {
                    manager->connect_callback_(d->success, d->error);
                    manager->connect_callback_ = nullptr;
                }
            } else {
                spdlog::debug(
                    "[WiFiManager] Manager destroyed before connect callback - safely ignored");
            }
        });
}

void WiFiManager::handle_disconnected(const std::string& event_data) {
    (void)event_data; // Could parse reason from event data

    spdlog::debug("[WiFiManager] Disconnected event received (backend thread)");

    // During a connection attempt, wpa_supplicant fires DISCONNECTED before CONNECTED
    // when switching networks. Ignore DISCONNECTED during connection - only AUTH_FAILED
    // or subsequent CONNECTED should determine success/failure.
    if (connecting_in_progress_) {
        spdlog::debug("[WiFiManager] Ignoring DISCONNECTED during connection attempt");
        return;
    }

    if (!connect_callback_) {
        spdlog::debug("[WiFiManager] Disconnected event but no callback registered (normal)");
        return;
    }

    // Use RAII-safe async callback wrapper
    helix::ui::queue_update<ConnectCallbackData>(
        std::make_unique<ConnectCallbackData>(ConnectCallbackData{self_, false, "Disconnected"}),
        [](ConnectCallbackData* d) {
            if (auto manager = d->manager.lock()) {
                if (manager->connect_callback_) {
                    manager->connect_callback_(d->success, d->error);
                    manager->connect_callback_ = nullptr;
                }
            } else {
                spdlog::debug(
                    "[WiFiManager] Manager destroyed before disconnect callback - safely ignored");
            }
        });
}

void WiFiManager::handle_auth_failed(const std::string& event_data) {
    (void)event_data; // Could parse specific error from event data

    spdlog::warn("[WiFiManager] Authentication failed event received (backend thread)");

    connecting_in_progress_ = false; // Connection attempt complete (failed)

    if (!connect_callback_) {
        LOG_WARN_INTERNAL("Auth failed event but no callback registered");
        return;
    }

    // Use RAII-safe async callback wrapper
    helix::ui::queue_update<ConnectCallbackData>(
        std::make_unique<ConnectCallbackData>(
            ConnectCallbackData{self_, false, "Authentication failed"}),
        [](ConnectCallbackData* d) {
            if (auto manager = d->manager.lock()) {
                if (manager->connect_callback_) {
                    manager->connect_callback_(d->success, d->error);
                    manager->connect_callback_ = nullptr;
                }
            } else {
                spdlog::debug(
                    "[WiFiManager] Manager destroyed before auth_failed callback - safely ignored");
            }
        });
}

// ============================================================================
// Shared Singleton Instance
// ============================================================================

// Global shared WiFiManager instance
// Using static local ensures thread-safe lazy initialization (C++11 guarantee)
static std::shared_ptr<WiFiManager> g_shared_wifi_manager;
static std::mutex g_wifi_manager_mutex;

namespace helix {

std::shared_ptr<WiFiManager> get_wifi_manager() {
    std::lock_guard<std::mutex> lock(g_wifi_manager_mutex);

    if (!g_shared_wifi_manager) {
        spdlog::debug("[WiFiManager] Creating global instance");
        // Use silent=true for global instance since it's used for passive status monitoring
        // (e.g., home panel WiFi icon). Avoids modal popup when WiFi hardware is unavailable
        // on development machines or when WiFi is simply turned off.
        g_shared_wifi_manager = std::make_shared<WiFiManager>(/*silent=*/true);
        g_shared_wifi_manager->init_self_reference(g_shared_wifi_manager);
    }

    return g_shared_wifi_manager;
}

} // namespace helix
