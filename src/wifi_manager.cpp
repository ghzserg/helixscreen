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

#include "wifi_manager.h"
#include "lvgl/lvgl.h"
#include "spdlog/spdlog.h"

#include <sys/stat.h>
#include <dirent.h>
#include <cstring>
#include <algorithm>

// Platform detection
#ifdef __APPLE__
#define WIFI_MOCK_MODE 1
#else
#define WIFI_MOCK_MODE 0
#endif

namespace WiFiManager {

// Internal state
static bool wifi_enabled = false;
static bool wifi_connected = false;
static std::string connected_ssid;
static std::string connected_ip;
static lv_timer_t* scan_timer = nullptr;
static std::function<void(const std::vector<WiFiNetwork>&)> scan_callback;

// Mock network list (for simulator) - ~10 networks with varying signal/encryption
static std::vector<WiFiNetwork> mock_networks = {
    WiFiNetwork("HomeNetwork-5G", 95, true, "WPA2"),
    WiFiNetwork("CoffeeShop_Free", 72, false, "Open"),
    WiFiNetwork("Printers-WiFi", 88, true, "WPA2"),
    WiFiNetwork("Neighbor-Network", 45, true, "WPA3"),
    WiFiNetwork("Guest-Access", 58, false, "Open"),
    WiFiNetwork("Office-Main", 82, true, "WPA2"),
    WiFiNetwork("IoT-Devices", 65, true, "WPA"),
    WiFiNetwork("Public-Hotspot", 38, false, "Open"),
    WiFiNetwork("Printer-Direct", 91, true, "WPA2"),
    WiFiNetwork("SmartHome-Net", 54, true, "WPA3")
};

// ============================================================================
// Hardware Detection
// ============================================================================

bool has_hardware() {
#if WIFI_MOCK_MODE
    // macOS simulator: always report WiFi available for testing
    spdlog::debug("[WiFi] Mock mode: WiFi hardware detected");
    return true;
#else
    // Linux: Check for WiFi interfaces in /sys/class/net
    DIR* dir = opendir("/sys/class/net");
    if (!dir) {
        spdlog::warn("[WiFi] Cannot access /sys/class/net");
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        std::string wireless_path = std::string("/sys/class/net/") +
                                   entry->d_name + "/wireless";
        struct stat st;
        if (stat(wireless_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            spdlog::info("[WiFi] WiFi hardware detected: {}", entry->d_name);
            closedir(dir);
            return true;
        }
    }
    closedir(dir);
    spdlog::info("[WiFi] No WiFi hardware detected");
    return false;
#endif
}

// ============================================================================
// WiFi State Management
// ============================================================================

bool is_enabled() {
    return wifi_enabled;
}

bool set_enabled(bool enabled) {
#if WIFI_MOCK_MODE
    wifi_enabled = enabled;
    spdlog::info("[WiFi] Mock: WiFi {}", enabled ? "enabled" : "disabled");
    return true;
#else
    // TODO: Linux implementation using nmcli radio wifi on/off
    wifi_enabled = enabled;
    spdlog::warn("[WiFi] Linux set_enabled not yet implemented");
    return false;
#endif
}

// ============================================================================
// Network Scanning
// ============================================================================

static void scan_timer_callback(lv_timer_t* timer) {
    (void)timer;

    if (!scan_callback) {
        spdlog::warn("[WiFi] Scan callback not set");
        return;
    }

#if WIFI_MOCK_MODE
    // Mock: Return static network list with slight randomization in signal strength
    std::vector<WiFiNetwork> networks = mock_networks;
    for (auto& net : networks) {
        // Vary signal strength by ±5% for realism
        int variation = (rand() % 11) - 5;  // -5 to +5
        net.signal_strength = std::max(0, std::min(100, net.signal_strength + variation));
    }

    spdlog::debug("[WiFi] Mock scan: {} networks found", networks.size());
    scan_callback(networks);
#else
    // TODO: Linux implementation using nmcli device wifi list
    spdlog::warn("[WiFi] Linux scan not yet implemented");
    scan_callback(std::vector<WiFiNetwork>());
#endif
}

void start_scan(std::function<void(const std::vector<WiFiNetwork>&)> on_networks_updated) {
    scan_callback = on_networks_updated;

    // Stop existing timer if running
    if (scan_timer) {
        lv_timer_delete(scan_timer);
        scan_timer = nullptr;
    }

    spdlog::info("[WiFi] Starting periodic network scan (every 7 seconds)");

    // Create timer for periodic scanning (7 second interval)
    scan_timer = lv_timer_create(scan_timer_callback, 7000, nullptr);

    // Trigger immediate scan
    scan_timer_callback(nullptr);
}

void stop_scan() {
    if (scan_timer) {
        lv_timer_delete(scan_timer);
        scan_timer = nullptr;
        spdlog::info("[WiFi] Stopped network scanning");
    }
    scan_callback = nullptr;
}

// ============================================================================
// Connection Management
// ============================================================================

// Connection state for async callback
static std::function<void(bool, const std::string&)> connect_callback;
static std::string connecting_ssid;
static std::string connecting_password;

static void connect_timer_callback(lv_timer_t* timer) {
    (void)timer;

#if WIFI_MOCK_MODE
    // Mock: Always succeed after delay
    wifi_connected = true;
    connected_ssid = connecting_ssid;
    connected_ip = "192.168.1.42";  // Mock IP
    spdlog::info("[WiFi] Mock: Connected to \"{}\"", connecting_ssid);

    if (connect_callback) {
        connect_callback(true, "");
        connect_callback = nullptr;
    }
#else
    // TODO: Linux implementation using nmcli device wifi connect
    spdlog::warn("[WiFi] Linux connect not yet implemented");
    if (connect_callback) {
        connect_callback(false, "Not implemented");
        connect_callback = nullptr;
    }
#endif
}

void connect(const std::string& ssid,
            const std::string& password,
            std::function<void(bool success, const std::string& error)> on_complete) {

    spdlog::info("[WiFi] Connecting to \"{}\"...", ssid);

    // Store connection parameters and callback
    connecting_ssid = ssid;
    connecting_password = password;
    connect_callback = on_complete;

    // Simulate connection delay (2 seconds)
    lv_timer_t* timer = lv_timer_create(connect_timer_callback, 2000, nullptr);
    lv_timer_set_repeat_count(timer, 1);  // One-shot timer
}

void disconnect() {
    wifi_connected = false;
    connected_ssid.clear();
    connected_ip.clear();
    spdlog::info("[WiFi] Disconnected");
}

// ============================================================================
// Status Queries
// ============================================================================

bool is_connected() {
    return wifi_connected;
}

std::string get_connected_ssid() {
    return connected_ssid;
}

std::string get_ip_address() {
    return connected_ip;
}

// ============================================================================
// Ethernet Detection
// ============================================================================

bool has_ethernet() {
#if WIFI_MOCK_MODE
    // macOS simulator: always report Ethernet available
    spdlog::debug("[Ethernet] Mock mode: Ethernet detected");
    return true;
#else
    // Linux: Check for Ethernet interfaces (eth*, en*, eno*, ens*)
    DIR* dir = opendir("/sys/class/net");
    if (!dir) {
        spdlog::warn("[Ethernet] Cannot access /sys/class/net");
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        std::string iface = entry->d_name;
        // Match common Ethernet interface names
        if (iface.compare(0, 3, "eth") == 0 ||  // eth0, eth1, etc.
            iface.compare(0, 2, "en") == 0 ||   // enp*, eno*, ens*
            iface == "end0") {  // Some systems use end0

            spdlog::info("[Ethernet] Ethernet interface detected: {}", iface);
            closedir(dir);
            return true;
        }
    }
    closedir(dir);
    spdlog::info("[Ethernet] No Ethernet interface detected");
    return false;
#endif
}

std::string get_ethernet_ip() {
#if WIFI_MOCK_MODE
    // macOS simulator: return mock Ethernet IP
    return "192.168.1.150";
#else
    // TODO: Linux implementation
    // For now, return empty (implementation deferred for simplicity)
    spdlog::warn("[Ethernet] IP detection not yet implemented on Linux");
    return "";
#endif
}

} // namespace WiFiManager
