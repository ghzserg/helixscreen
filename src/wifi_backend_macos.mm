// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

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

#ifdef __APPLE__

#include "wifi_backend_macos.h"

#include "safe_log.h"

#include <spdlog/spdlog.h>

#import <CoreLocation/CoreLocation.h>
#import <CoreWLAN/CoreWLAN.h>
#import <Foundation/Foundation.h>

// ============================================================================
// Constructor / Destructor
// ============================================================================

WifiBackendMacOS::WifiBackendMacOS()
    : running_(false), wifi_client_(nullptr), interface_(nullptr), scan_timer_(nullptr),
      connect_timer_(nullptr), connection_in_progress_(false) {
    spdlog::debug("[WiFiMacOS] Backend created");
}

WifiBackendMacOS::~WifiBackendMacOS() {
    stop();

    @autoreleasepool {
        if (wifi_client_) {
            CWWiFiClient* client = (__bridge CWWiFiClient*)wifi_client_;
            [client release];
            wifi_client_ = nullptr;
        }
        // Note: interface_ is owned by wifi_client_, no need to release
        interface_ = nullptr;
    }

    // Use fprintf - spdlog may be destroyed during static cleanup
    fprintf(stderr, "[WiFiMacOS] Backend destroyed\n");
}

// ============================================================================
// Lifecycle Management
// ============================================================================

WiFiError WifiBackendMacOS::start() {
    if (running_) {
        spdlog::warn("[WiFiMacOS] Backend already running");
        return WiFiErrorHelper::success();
    }

    spdlog::debug("[WiFiMacOS] Starting CoreWLAN backend");

    // Check system prerequisites
    WiFiError prereq_check = check_system_prerequisites();
    if (!prereq_check.success()) {
        spdlog::error("[WiFiMacOS] System prerequisites check failed: {}",
                      prereq_check.technical_msg);
        return prereq_check;
    }

    @autoreleasepool {
        // Initialize CoreWLAN client
        CWWiFiClient* client = [[CWWiFiClient alloc] init];
        if (!client) {
            spdlog::error("[WiFiMacOS] Failed to create CWWiFiClient");
            return WiFiError(WiFiResult::BACKEND_ERROR, "Failed to create CWWiFiClient",
                             "WiFi system initialization failed", "Restart the application");
        }
        wifi_client_ = (__bridge void*)client;

        // Get primary WiFi interface
        CWInterface* iface = [client interface];
        if (!iface) {
            spdlog::error("[WiFiMacOS] No WiFi interface found");
            [client release];
            wifi_client_ = nullptr;
            return WiFiErrorHelper::hardware_not_available();
        }
        interface_ = (__bridge void*)iface;

        NSString* ifaceName = [iface interfaceName];
        spdlog::info("[WiFiMacOS] Using interface: {}",
                     ifaceName ? [ifaceName UTF8String] : "unknown");
    }

    running_ = true;
    spdlog::info("[WiFiMacOS] CoreWLAN backend started successfully");

    return WiFiErrorHelper::success();
}

void WifiBackendMacOS::stop() {
    if (!running_) {
        return;
    }

    // Use fprintf - spdlog may be destroyed during static cleanup
    fprintf(stderr, "[WiFiMacOS] Stopping CoreWLAN backend\n");

    // Cancel any pending timers
    if (scan_timer_) {
        lv_timer_del(scan_timer_);
        scan_timer_ = nullptr;
    }
    if (connect_timer_) {
        lv_timer_del(connect_timer_);
        connect_timer_ = nullptr;
    }

    running_ = false;
    connection_in_progress_ = false;

    fprintf(stderr, "[WiFiMacOS] CoreWLAN backend stopped\n");
}

bool WifiBackendMacOS::is_running() const {
    return running_;
}

// ============================================================================
// Event System
// ============================================================================

void WifiBackendMacOS::register_event_callback(const std::string& name,
                                               std::function<void(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_[name] = callback;
    spdlog::debug("[WiFiMacOS] Registered callback for event: {}", name);
}

void WifiBackendMacOS::fire_event(const std::string& event_name, const std::string& data) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);

    auto it = callbacks_.find(event_name);
    if (it != callbacks_.end()) {
        spdlog::debug("[WiFiMacOS] Firing event: {}", event_name);
        it->second(data);
    }
}

// ============================================================================
// Network Scanning
// ============================================================================

WiFiError WifiBackendMacOS::trigger_scan() {
    if (!running_) {
        return WiFiError(WiFiResult::NOT_INITIALIZED, "Backend not started",
                         "WiFi system not initialized", "");
    }

    spdlog::info("[WiFiMacOS] Triggering network scan");

    // Cancel any existing scan timer
    if (scan_timer_) {
        lv_timer_del(scan_timer_);
        scan_timer_ = nullptr;
    }

    // Perform scan in background (simulate async with timer)
    // Use lambda capture to avoid accessing private timer fields
    scan_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            // Get backend pointer from timer user_data (set below)
            WifiBackendMacOS* backend =
                static_cast<WifiBackendMacOS*>(lv_timer_get_user_data(timer));
            if (backend) {
                backend->scan_timer_callback(timer);
            }
        },
        100, nullptr);

    if (scan_timer_) {
        lv_timer_set_user_data(scan_timer_, this);
        lv_timer_set_repeat_count(scan_timer_, 1); // Fire once
    }

    return WiFiErrorHelper::success();
}

void WifiBackendMacOS::scan_timer_callback([[maybe_unused]] lv_timer_t* timer) {
    scan_timer_ = nullptr; // Timer will be auto-deleted

    @autoreleasepool {
        CWInterface* iface = (__bridge CWInterface*)interface_;
        if (!iface) {
            spdlog::error("[WiFiMacOS] No WiFi interface available for scan");
            fire_event("SCAN_COMPLETE", "");
            return;
        }

        NSError* error = nil;
        NSSet<CWNetwork*>* networks = [iface scanForNetworksWithSSID:nil error:&error];

        if (error) {
            spdlog::error("[WiFiMacOS] Scan failed: {}", [[error localizedDescription] UTF8String]);
            fire_event("SCAN_COMPLETE", "");
            return;
        }

        // Convert CoreWLAN networks to our format
        std::vector<WiFiNetwork> discovered;
        for (CWNetwork* network in networks) {
            NSString* ssid = [network ssid];
            if (!ssid || [ssid length] == 0) {
                continue; // Skip hidden networks
            }

            WiFiNetwork wifi_net;
            wifi_net.ssid = [ssid UTF8String];
            wifi_net.signal_strength = rssi_to_percentage(static_cast<int>([network rssiValue]));

            // Determine security type
            bool is_secured = false;
            wifi_net.security_type = extract_security_type((__bridge void*)network, is_secured);
            wifi_net.is_secured = is_secured;

            discovered.push_back(wifi_net);
        }

        // Sort by signal strength (strongest first)
        std::sort(discovered.begin(), discovered.end(),
                  [](const WiFiNetwork& a, const WiFiNetwork& b) {
                      return a.signal_strength > b.signal_strength;
                  });

        // Cache results
        {
            std::lock_guard<std::mutex> lock(networks_mutex_);
            cached_networks_ = discovered;
        }

        spdlog::info("[WiFiMacOS] Scan complete: {} networks found", discovered.size());
        fire_event("SCAN_COMPLETE", "");
    }
}

WiFiError WifiBackendMacOS::get_scan_results(std::vector<WiFiNetwork>& networks) {
    if (!running_) {
        return WiFiError(WiFiResult::NOT_INITIALIZED, "Backend not started",
                         "WiFi system not initialized", "");
    }

    std::lock_guard<std::mutex> lock(networks_mutex_);
    networks = cached_networks_;

    spdlog::debug("[WiFiMacOS] Returning {} cached scan results", networks.size());
    return WiFiErrorHelper::success();
}

// ============================================================================
// Connection Management
// ============================================================================

WiFiError WifiBackendMacOS::connect_network(const std::string& ssid, const std::string& password) {
    if (!running_) {
        return WiFiError(WiFiResult::NOT_INITIALIZED, "Backend not started",
                         "WiFi system not initialized", "");
    }

    if (connection_in_progress_) {
        return WiFiError(WiFiResult::BACKEND_ERROR, "Connection already in progress",
                         "Please wait for current connection to complete", "");
    }

    spdlog::info("[WiFiMacOS] Connecting to network: {}", ssid);

    connecting_ssid_ = ssid;
    connecting_password_ = password;
    connection_in_progress_ = true;

    // Cancel any existing connect timer
    if (connect_timer_) {
        lv_timer_del(connect_timer_);
        connect_timer_ = nullptr;
    }

    // Perform connection in background (use timer with public API)
    connect_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            WifiBackendMacOS* backend =
                static_cast<WifiBackendMacOS*>(lv_timer_get_user_data(timer));
            if (backend) {
                backend->connect_timer_callback(timer);
            }
        },
        100, nullptr);

    if (connect_timer_) {
        lv_timer_set_user_data(connect_timer_, this);
        lv_timer_set_repeat_count(connect_timer_, 1); // Fire once
    }

    return WiFiErrorHelper::success();
}

void WifiBackendMacOS::connect_timer_callback([[maybe_unused]] lv_timer_t* timer) {
    connect_timer_ = nullptr; // Timer will be auto-deleted

    @autoreleasepool {
        CWInterface* iface = (__bridge CWInterface*)interface_;
        if (!iface) {
            spdlog::error("[WiFiMacOS] No WiFi interface available for connection");
            connection_in_progress_ = false;
            fire_event("DISCONNECTED", "");
            return;
        }

        // Find the network in scan results
        NSString* targetSSID = [NSString stringWithUTF8String:connecting_ssid_.c_str()];
        NSError* error = nil;
        NSData* ssidData = [targetSSID dataUsingEncoding:NSUTF8StringEncoding];
        NSSet<CWNetwork*>* networks = [iface scanForNetworksWithSSID:ssidData error:&error];

        if (error || !networks || [networks count] == 0) {
            spdlog::error("[WiFiMacOS] Network not found: {}", connecting_ssid_);
            connection_in_progress_ = false;
            fire_event("DISCONNECTED", connecting_ssid_);
            return;
        }

        // Get the first matching network
        CWNetwork* network = [[networks allObjects] firstObject];

        // Prepare password if needed
        NSString* password_ns = nil;
        if (!connecting_password_.empty()) {
            password_ns = [NSString stringWithUTF8String:connecting_password_.c_str()];
        }

        // Attempt connection
        BOOL success = [iface associateToNetwork:network password:password_ns error:&error];

        if (success) {
            spdlog::info("[WiFiMacOS] Successfully connected to: {}", connecting_ssid_);
            fire_event("CONNECTED", connecting_ssid_);
        } else {
            spdlog::error("[WiFiMacOS] Connection failed: {}",
                          error ? [[error localizedDescription] UTF8String] : "unknown error");
            fire_event("AUTH_FAILED", connecting_ssid_);
        }

        connection_in_progress_ = false;
    }
}

WiFiError WifiBackendMacOS::disconnect_network() {
    if (!running_) {
        return WiFiError(WiFiResult::NOT_INITIALIZED, "Backend not started",
                         "WiFi system not initialized", "");
    }

    @autoreleasepool {
        CWInterface* iface = (__bridge CWInterface*)interface_;
        if (!iface) {
            return WiFiError(WiFiResult::BACKEND_ERROR, "No WiFi interface",
                             "WiFi interface unavailable", "");
        }

        [iface disassociate];
        spdlog::info("[WiFiMacOS] Disconnected from network");
        fire_event("DISCONNECTED", "");
    }

    return WiFiErrorHelper::success();
}

// ============================================================================
// Status Queries
// ============================================================================

WifiBackend::ConnectionStatus WifiBackendMacOS::get_status() {
    ConnectionStatus status;
    status.connected = false;
    status.signal_strength = 0;

    if (!running_ || !interface_) {
        return status;
    }

    @autoreleasepool {
        CWInterface* iface = (__bridge CWInterface*)interface_;

        // Check if we're associated with a network
        if ([iface serviceActive]) {
            status.connected = true;

            NSString* ssid = [iface ssid];
            if (ssid) {
                status.ssid = [ssid UTF8String];
            }

            NSString* bssid = [iface bssid];
            if (bssid) {
                status.bssid = [bssid UTF8String];
            }

            status.signal_strength = rssi_to_percentage(static_cast<int>([iface rssiValue]));

            // Get IP address (requires additional work, simplified for now)
            // Would need to query network interfaces via getifaddrs() or similar
            status.ip_address = ""; // TODO: Implement IP address query
        }
    }

    return status;
}

bool WifiBackendMacOS::supports_5ghz() const {
    // macOS devices (MacBooks, iMacs, etc.) support 5GHz
    // Could query CWInterface supportedWLANChannels for precise detection,
    // but all modern Macs support 5GHz so we return true
    return true;
}

// ============================================================================
// System Validation
// ============================================================================

WiFiError WifiBackendMacOS::check_system_prerequisites() {
    // Check WiFi hardware
    WiFiError hw_check = check_wifi_hardware();
    if (!hw_check.success()) {
        return hw_check;
    }

    // Check location permission (required on macOS 10.15+)
    WiFiError perm_check = check_location_permission();
    if (!perm_check.success()) {
        // Location permission required for WiFi scanning - fail startup
        return perm_check;
    }

    return WiFiErrorHelper::success();
}

WiFiError WifiBackendMacOS::check_wifi_hardware() {
    @autoreleasepool {
        CWWiFiClient* client = [CWWiFiClient sharedWiFiClient];
        CWInterface* iface = [client interface];

        if (!iface) {
            return WiFiErrorHelper::hardware_not_available();
        }

        NSString* ifaceName = [iface interfaceName];
        spdlog::debug("[WiFiMacOS] WiFi interface found: {}",
                      ifaceName ? [ifaceName UTF8String] : "unknown");
    }

    return WiFiErrorHelper::success();
}

WiFiError WifiBackendMacOS::check_location_permission() {
    @autoreleasepool {
// Use class method (deprecated but compatible with macOS 10.15)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        CLAuthorizationStatus status = [CLLocationManager authorizationStatus];
#pragma clang diagnostic pop

        switch (status) {
        case kCLAuthorizationStatusAuthorizedAlways:
            spdlog::debug("[WiFiMacOS] Location permission granted");
            return WiFiErrorHelper::success();

        case kCLAuthorizationStatusNotDetermined:
            spdlog::warn("[WiFiMacOS] Location permission not determined");
            spdlog::warn("[WiFiMacOS] Grant location permission: System Preferences → Security & "
                         "Privacy → Location Services");
            return WiFiError(WiFiResult::PERMISSION_DENIED, "Location permission not determined",
                             "Location access required for WiFi scanning",
                             "Grant location permission in System Preferences");

        case kCLAuthorizationStatusDenied:
        case kCLAuthorizationStatusRestricted:
            spdlog::warn("[WiFiMacOS] Location permission denied/restricted");
            spdlog::warn("[WiFiMacOS] Grant location permission: System Preferences → Security & "
                         "Privacy → Location Services");
            return WiFiError(WiFiResult::PERMISSION_DENIED, "Location permission denied",
                             "Location access required for WiFi scanning",
                             "Grant location permission in System Preferences > Privacy");

        default:
            return WiFiErrorHelper::success();
        }
    }
}

// ============================================================================
// Helper Functions
// ============================================================================

int WifiBackendMacOS::rssi_to_percentage(int rssi) {
    // Standard WiFi RSSI to percentage conversion
    // -50 dBm or better = 100%
    // -100 dBm or worse = 0%

    if (rssi >= -50)
        return 100;
    if (rssi <= -100)
        return 0;

    // Linear interpolation
    return 2 * (rssi + 100);
}

std::string WifiBackendMacOS::extract_security_type(void* network_ptr, bool& is_secured) {
    @autoreleasepool {
        CWNetwork* network = (__bridge CWNetwork*)network_ptr;

        // Check if network is open
        if (![network supportsSecurity:kCWSecurityWPAPersonal] &&
            ![network supportsSecurity:kCWSecurityWPA2Personal] &&
            ![network supportsSecurity:kCWSecurityWPA3Personal] &&
            ![network supportsSecurity:kCWSecurityWEP]) {
            is_secured = false;
            return "Open";
        }

        is_secured = true;

        // Determine specific security type
        if ([network supportsSecurity:kCWSecurityWPA3Personal]) {
            return "WPA3";
        }
        if ([network supportsSecurity:kCWSecurityWPA2Personal]) {
            return "WPA2";
        }
        if ([network supportsSecurity:kCWSecurityWPAPersonal]) {
            return "WPA";
        }
        if ([network supportsSecurity:kCWSecurityWEP]) {
            return "WEP";
        }

        return "Secured"; // Unknown security type but requires password
    }
}

#endif // __APPLE__
