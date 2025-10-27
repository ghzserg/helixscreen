/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <string>
#include <vector>
#include <functional>

/**
 * @brief WiFi network information
 */
struct WiFiNetwork {
    std::string ssid;           ///< Network name (SSID)
    int signal_strength;        ///< Signal strength (0-100 percentage)
    bool is_secured;            ///< True if network requires password
    std::string security_type;  ///< Security type ("WPA2", "WPA3", "WEP", "Open")

    WiFiNetwork() : signal_strength(0), is_secured(false) {}

    WiFiNetwork(const std::string& ssid_, int strength, bool secured, const std::string& security = "")
        : ssid(ssid_), signal_strength(strength), is_secured(secured), security_type(security) {}
};

/**
 * @brief WiFi Manager - Platform-abstracted WiFi operations
 *
 * Provides network scanning, connection management, and status monitoring.
 * Implementation is platform-specific:
 * - Linux: Uses NetworkManager (nmcli) for real WiFi operations
 * - macOS: Mock implementation for simulator testing
 */
namespace WiFiManager {
    /**
     * @brief Check if WiFi hardware is available
     *
     * Linux: Checks /sys/class/net for wireless interfaces
     * macOS: Always returns true (mock mode)
     *
     * @return true if WiFi hardware detected
     */
    bool has_hardware();

    /**
     * @brief Check if WiFi is currently enabled
     *
     * @return true if WiFi radio is enabled
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
     * @brief Start periodic network scanning
     *
     * Scans for available networks and invokes callback with results.
     * Scanning continues automatically every 5-10 seconds until stop_scan() called.
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
    void connect(const std::string& ssid,
                const std::string& password,
                std::function<void(bool success, const std::string& error)> on_complete);

    /**
     * @brief Disconnect from current network
     */
    void disconnect();

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
     * @brief Check if Ethernet hardware is present
     *
     * Linux: Checks for eth*, en*, or similar wired interfaces
     * macOS: Always returns true (mock mode)
     *
     * @return true if Ethernet interface detected
     */
    bool has_ethernet();

    /**
     * @brief Get Ethernet connection status and IP address
     *
     * @return IP address if connected, empty string if not connected or no Ethernet
     */
    std::string get_ethernet_ip();
}
