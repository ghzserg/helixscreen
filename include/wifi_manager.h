// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"
#include "wifi_backend.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace helix {

/**
 * @brief WiFi Manager - Clean interface using backend system
 *
 * Provides network scanning, connection management, and status monitoring.
 * Uses pluggable backend system:
 * - Linux: WifiBackendWpaSupplicant for real wpa_supplicant integration
 * - macOS: WifiBackendMock for simulator testing
 *
 * Key improvements over old implementation:
 * - No platform ifdefs in manager code
 * - Event-driven architecture with proper callbacks
 * - Thread-safe communication between backend and UI
 * - Cleaner separation between WiFi operations and UI timer management
 */
class WiFiManager {
  public:
    /**
     * @brief Initialize WiFi manager with appropriate backend
     *
     * Automatically selects platform-appropriate backend and starts it.
     *
     * @param silent If true, suppress error modals on startup (used when WiFi
     *               wasn't previously configured and we're just probing availability)
     */
    explicit WiFiManager(bool silent = false);

    /**
     * @brief Destructor - ensures clean shutdown
     */
    ~WiFiManager();

    // ========================================================================
    // Network Scanning
    // ========================================================================

    /**
     * @brief Perform a single network scan (synchronous)
     *
     * Triggers scan and returns results immediately.
     * Uses backend's get_scan_results() after triggering scan.
     *
     * @return Vector of discovered WiFi networks
     */
    std::vector<WiFiNetwork> scan_once();

    /**
     * @brief Start periodic network scanning
     *
     * Scans for available networks and invokes callback with results.
     * Scanning continues automatically every 7 seconds until stop_scan() called.
     *
     * @param on_networks_updated Callback invoked with scan results
     */
    void start_scan(std::function<void(const std::vector<WiFiNetwork>&)> on_networks_updated);

    /**
     * @brief Stop periodic network scanning
     *
     * Cancels auto-refresh timer and any pending scan operations.
     */
    void stop_scan();

    // ========================================================================
    // Connection Management
    // ========================================================================

    /**
     * @brief Connect to WiFi network
     *
     * Attempts to connect to the specified network. Operation is asynchronous;
     * callback invoked when connection succeeds or fails.
     *
     * @param ssid Network name
     * @param password Network password (empty for open networks)
     * @param on_complete Callback with (success, error_message)
     */
    void connect(const std::string& ssid, const std::string& password,
                 std::function<void(bool success, const std::string& error)> on_complete);

    /**
     * @brief Disconnect from current network
     */
    void disconnect();

    // ========================================================================
    // Status Queries
    // ========================================================================

    /**
     * @brief Check if connected to any network
     *
     * @return true if connected
     */
    bool is_connected();

    /**
     * @brief Get currently connected network name
     *
     * @return SSID of connected network, or empty string if not connected
     */
    std::string get_connected_ssid();

    /**
     * @brief Get current IP address
     *
     * @return IP address string (e.g., "192.168.1.100"), or empty if not connected
     */
    std::string get_ip_address();

    /**
     * @brief Get WiFi adapter MAC address
     *
     * @return MAC address string (e.g., "aa:bb:cc:dd:ee:ff"), or empty if unavailable
     */
    std::string get_mac_address();

    /**
     * @brief Get signal strength of connected network
     *
     * @return Signal strength 0-100%, or 0 if not connected
     */
    int get_signal_strength();

    /**
     * @brief Check if WiFi hardware supports 5GHz band
     *
     * Returns true if the WiFi adapter can connect to 5GHz networks.
     * Used to conditionally show "Only 2.4GHz networks" in the UI.
     *
     * @return true if 5GHz is supported, false if only 2.4GHz
     */
    bool supports_5ghz();

    // ========================================================================
    // Hardware Detection (Legacy Compatibility)
    // ========================================================================

    /**
     * @brief Check if WiFi hardware is available
     *
     * Always returns true - backend creation handles hardware availability.
     * Kept for compatibility with existing UI code.
     *
     * @return true if WiFi backend is available
     */
    bool has_hardware();

    /**
     * @brief Check if WiFi is currently enabled
     *
     * @return true if backend is running
     */
    bool is_enabled();

    /**
     * @brief Enable or disable WiFi radio
     *
     * @param enabled true to enable, false to disable
     * @return true on success
     */
    bool set_enabled(bool enabled);

    /**
     * @brief Initialize self-reference for async callback safety
     *
     * MUST be called immediately after construction when using shared_ptr.
     * Enables async callbacks to safely check if manager still exists.
     *
     * @param self Shared pointer to this WiFiManager instance
     */
    void init_self_reference(std::shared_ptr<WiFiManager> self);

  private:
    std::unique_ptr<WifiBackend> backend_;

    // Self-reference for async callback safety
    // Weak pointers in async callbacks can safely check if manager still exists
    std::shared_ptr<WiFiManager> self_;

    // Scanning state
    lv_timer_t* scan_timer_;
    std::function<void(const std::vector<WiFiNetwork>&)> scan_callback_;
    bool scan_pending_; // True when scan triggered, cleared after first SCAN_COMPLETE processed

    // Connection state
    std::function<void(bool, const std::string&)> connect_callback_;
    bool connecting_in_progress_ =
        false; // True during connect attempt, prevents false failure on DISCONNECTED

    // Event handling
    void handle_scan_complete(const std::string& event_data);
    void handle_connected(const std::string& event_data);
    void handle_disconnected(const std::string& event_data);
    void handle_auth_failed(const std::string& event_data);

    // Timer callbacks (must be static for LVGL)
    static void scan_timer_callback(lv_timer_t* timer);
};

/**
 * @brief Get the global WiFiManager instance
 *
 * Returns a lazily-created singleton WiFiManager. Use this from all
 * components (wizard, home panel, etc.) rather than creating instances.
 *
 * @return Shared pointer to the global WiFiManager instance
 */
std::shared_ptr<WiFiManager> get_wifi_manager();

} // namespace helix
