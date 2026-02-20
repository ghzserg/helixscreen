// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wifi_backend_networkmanager.h"

#include "spdlog/spdlog.h"

#if !defined(__APPLE__) && !defined(__ANDROID__)
// ============================================================================
// Linux Implementation: NetworkManager fallback via nmcli
// ============================================================================

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>

// ============================================================================
// Lifecycle
// ============================================================================

WifiBackendNetworkManager::WifiBackendNetworkManager() {
    spdlog::debug("[WifiBackend] Initialized (NetworkManager mode)");
}

WifiBackendNetworkManager::~WifiBackendNetworkManager() {
    spdlog::trace("[WifiBackend] NM destructor called");
    stop();
    // Use fprintf - spdlog may be destroyed during static cleanup
    fprintf(stderr, "[WifiBackend] NetworkManager backend destroyed\n");
}

WiFiError WifiBackendNetworkManager::start() {
    spdlog::debug("[WifiBackend] Starting NetworkManager backend...");

    if (running_) {
        spdlog::debug("[WifiBackend] Already running");
        return WiFiErrorHelper::success();
    }

    // Check that NM is running and nmcli is available
    WiFiError prereq = check_system_prerequisites();
    if (!prereq.success()) {
        if (is_silent()) {
            spdlog::debug("[WifiBackend] NM pre-flight failed (silent mode): {}",
                          prereq.technical_msg);
        }
        return prereq;
    }

    // Detect WiFi interface
    wifi_interface_ = detect_wifi_interface();
    if (wifi_interface_.empty()) {
        WiFiError err = WiFiErrorHelper::hardware_not_available();
        if (is_silent()) {
            spdlog::debug("[WifiBackend] No WiFi interface found via NM (silent mode)");
        }
        return err;
    }

    spdlog::info("[WifiBackend] NetworkManager WiFi interface: {}", wifi_interface_);
    running_ = true;
    return WiFiErrorHelper::success();
}

void WifiBackendNetworkManager::stop() {
    if (!running_) {
        return;
    }

    spdlog::info("[WifiBackend] Stopping NetworkManager backend");

    // Signal threads to cancel
    scan_active_ = false;
    connect_active_ = false;

    // Join threads (MUST join, not detach - prevents use-after-free)
    if (scan_thread_.joinable()) {
        scan_thread_.join();
    }
    if (connect_thread_.joinable()) {
        connect_thread_.join();
    }

    running_ = false;
    spdlog::debug("[WifiBackend] NetworkManager backend stopped");
}

bool WifiBackendNetworkManager::is_running() const {
    return running_;
}

// ============================================================================
// Event System
// ============================================================================

void WifiBackendNetworkManager::register_event_callback(
    const std::string& name, std::function<void(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);

    auto it = callbacks_.find(name);
    if (it == callbacks_.end()) {
        callbacks_.insert({name, callback});
        spdlog::debug("[WifiBackend] NM: Registered callback '{}'", name);
    } else {
        spdlog::warn("[WifiBackend] NM: Callback '{}' already registered (not replacing)", name);
    }
}

void WifiBackendNetworkManager::fire_event(const std::string& event_name, const std::string& data) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    auto it = callbacks_.find(event_name);
    if (it != callbacks_.end()) {
        spdlog::debug("[WifiBackend] NM: Firing event '{}'", event_name);
        try {
            it->second(data);
        } catch (const std::exception& e) {
            spdlog::error("[WifiBackend] NM: Exception in callback '{}': {}", event_name, e.what());
        } catch (...) {
            spdlog::error("[WifiBackend] NM: Unknown exception in callback '{}'", event_name);
        }
    } else {
        spdlog::trace("[WifiBackend] NM: No callback registered for '{}'", event_name);
    }
}

// ============================================================================
// System Prerequisites
// ============================================================================

std::string WifiBackendNetworkManager::exec_nmcli(const std::string& args) {
    std::string cmd = "nmcli " + args + " 2>/dev/null";
    spdlog::trace("[WifiBackend] NM: exec: {}", cmd);

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        spdlog::debug("[WifiBackend] NM: popen failed for: {}", cmd);
        return "";
    }

    std::string result;
    std::array<char, 512> buffer;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

    int ret = pclose(pipe);
    int exit_code = WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;

    if (exit_code != 0) {
        spdlog::trace("[WifiBackend] NM: nmcli exited with code {}", exit_code);
    }

    return result;
}

WiFiError WifiBackendNetworkManager::check_system_prerequisites() {
    spdlog::debug("[WifiBackend] NM: Checking prerequisites");

    // Check if nmcli is available and NM is running
    std::string status = exec_nmcli("-t general status");
    if (status.empty()) {
        return WiFiErrorHelper::service_not_running(
            "NetworkManager (nmcli not available or NM not running)");
    }

    // nmcli general status returns something like:
    // connected:full:enabled:enabled
    // Check that it's not "error" or empty
    if (status.find("error") != std::string::npos) {
        return WiFiErrorHelper::service_not_running("NetworkManager (reported error: " + status +
                                                    ")");
    }

    spdlog::debug("[WifiBackend] NM: Prerequisites check passed");
    return WiFiErrorHelper::success();
}

std::string WifiBackendNetworkManager::detect_wifi_interface() {
    // nmcli -t -f DEVICE,TYPE device status
    // Output: wlan0:wifi:connected:MyNetwork
    std::string output = exec_nmcli("-t -f DEVICE,TYPE device status");
    if (output.empty()) {
        return "";
    }

    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        auto fields = split_nmcli_fields(line);
        if (fields.size() >= 2 && fields[1] == "wifi") {
            std::string iface = fields[0];
            // SECURITY: Validate interface name to prevent shell injection
            // Interface names should only contain alphanumeric chars, hyphens, underscores
            bool valid = !iface.empty();
            for (char c : iface) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                spdlog::warn("[WifiBackend] NM: Suspicious interface name '{}', skipping", iface);
                continue;
            }
            spdlog::debug("[WifiBackend] NM: Detected WiFi interface: {}", iface);
            return iface;
        }
    }

    spdlog::debug("[WifiBackend] NM: No WiFi interface found in NM device list");
    return "";
}

// ============================================================================
// Scanning
// ============================================================================

WiFiError WifiBackendNetworkManager::trigger_scan() {
    if (!running_) {
        return WiFiError(WiFiResult::NOT_INITIALIZED, "Backend not started",
                         "WiFi system not ready");
    }

    // Clean up existing scan thread
    scan_active_ = false;
    if (scan_thread_.joinable()) {
        scan_thread_.join();
    }

    // Launch new scan thread
    scan_active_ = true;
    scan_thread_ = std::thread(&WifiBackendNetworkManager::scan_thread_func, this);
    return WiFiErrorHelper::success();
}

void WifiBackendNetworkManager::scan_thread_func() {
    spdlog::debug("[WifiBackend] NM: Scan thread started");

    // Request a rescan (may take a few seconds)
    exec_nmcli("device wifi rescan ifname " + wifi_interface_);

    // Wait for scan to complete (nmcli rescan is async)
    std::this_thread::sleep_for(std::chrono::seconds(2));

    if (!scan_active_) {
        spdlog::debug("[WifiBackend] NM: Scan cancelled");
        return;
    }

    // Get scan results
    std::string output =
        exec_nmcli("-t -f IN-USE,SSID,SIGNAL,SECURITY device wifi list ifname " + wifi_interface_);

    if (!scan_active_) {
        spdlog::debug("[WifiBackend] NM: Scan cancelled after fetch");
        return;
    }

    auto networks = parse_scan_output(output);

    // Cache results
    {
        std::lock_guard<std::mutex> lock(networks_mutex_);
        cached_networks_ = std::move(networks);
    }

    spdlog::debug("[WifiBackend] NM: Scan complete, {} networks found", cached_networks_.size());

    if (scan_active_) {
        fire_event("SCAN_COMPLETE");
    }
}

WiFiError WifiBackendNetworkManager::get_scan_results(std::vector<WiFiNetwork>& networks) {
    if (!running_) {
        return WiFiError(WiFiResult::NOT_INITIALIZED, "Backend not started",
                         "WiFi system not ready");
    }

    std::lock_guard<std::mutex> lock(networks_mutex_);
    networks = cached_networks_;
    return WiFiErrorHelper::success();
}

// ============================================================================
// Parsing
// ============================================================================

std::vector<std::string> WifiBackendNetworkManager::split_nmcli_fields(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;

    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) {
            char next = line[i + 1];
            if (next == ':') {
                // Escaped colon - literal ':'
                current += ':';
                ++i;
            } else if (next == '\\') {
                // Escaped backslash - literal '\'
                current += '\\';
                ++i;
            } else {
                // Other escape - keep as-is
                current += line[i];
            }
        } else if (line[i] == ':') {
            // Field separator
            fields.push_back(current);
            current.clear();
        } else {
            current += line[i];
        }
    }

    // Don't forget the last field
    fields.push_back(current);
    return fields;
}

std::vector<WiFiNetwork> WifiBackendNetworkManager::parse_scan_output(const std::string& output) {
    std::vector<WiFiNetwork> networks;
    if (output.empty()) {
        return networks;
    }

    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }

        // nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY device wifi list
        // Format: IN-USE:SSID:SIGNAL:SECURITY
        // IN-USE is " " or "*"
        auto fields = split_nmcli_fields(line);
        if (fields.size() < 4) {
            spdlog::trace("[WifiBackend] NM: Skipping malformed scan line ({} fields): {}",
                          fields.size(), line);
            continue;
        }

        // fields[0] = IN-USE (* or space)
        std::string ssid = fields[1];
        std::string signal_str = fields[2];
        std::string security = fields[3];

        // Skip hidden networks (empty SSID)
        if (ssid.empty()) {
            continue;
        }

        // Parse signal strength (nmcli reports 0-100 percentage directly)
        int signal = 0;
        try {
            signal = std::stoi(signal_str);
        } catch (const std::exception&) {
            spdlog::trace("[WifiBackend] NM: Invalid signal '{}' for SSID '{}'", signal_str, ssid);
            continue;
        }

        // Clamp to valid range
        signal = std::max(0, std::min(100, signal));

        // Determine security type
        bool is_secured = false;
        std::string security_type = "Open";

        if (security.find("WPA3") != std::string::npos) {
            is_secured = true;
            security_type = "WPA3";
        } else if (security.find("WPA2") != std::string::npos) {
            is_secured = true;
            security_type = "WPA2";
        } else if (security.find("WPA") != std::string::npos) {
            is_secured = true;
            security_type = "WPA";
        } else if (security.find("WEP") != std::string::npos) {
            is_secured = true;
            security_type = "WEP";
        } else if (!security.empty() && security != "--") {
            // Unknown but non-empty security
            is_secured = true;
            security_type = security;
        }

        networks.emplace_back(ssid, signal, is_secured, security_type);
    }

    // Deduplicate by SSID (keep strongest signal) - same as wpa_supplicant backend
    if (networks.size() > 1) {
        std::unordered_map<std::string, size_t> best_by_ssid;
        for (size_t i = 0; i < networks.size(); ++i) {
            auto it = best_by_ssid.find(networks[i].ssid);
            if (it == best_by_ssid.end()) {
                best_by_ssid[networks[i].ssid] = i;
            } else if (networks[i].signal_strength > networks[it->second].signal_strength) {
                it->second = i;
            }
        }

        if (best_by_ssid.size() < networks.size()) {
            std::vector<WiFiNetwork> deduped;
            deduped.reserve(best_by_ssid.size());
            for (const auto& [ssid, idx] : best_by_ssid) {
                deduped.push_back(networks[idx]);
            }
            spdlog::debug("[WifiBackend] NM: Deduplicated {} networks to {} unique SSIDs",
                          networks.size(), deduped.size());
            networks = std::move(deduped);
        }
    }

    spdlog::debug("[WifiBackend] NM: Parsed {} networks from scan output", networks.size());
    return networks;
}

// ============================================================================
// Input Validation
// ============================================================================

std::string WifiBackendNetworkManager::validate_input(const std::string& input,
                                                      const std::string& field_name) {
    if (input.empty()) {
        spdlog::error("[WifiBackend] NM: Empty {}", field_name);
        return "";
    }

    if (input.length() > 255) {
        spdlog::error("[WifiBackend] NM: {} too long ({} chars)", field_name, input.length());
        return "";
    }

    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        // Reject control chars (0x00-0x1F) and DEL (0x7F)
        if (c < 32 || c == 127) {
            spdlog::error("[WifiBackend] NM: Invalid character in {}: ASCII {}", field_name,
                          static_cast<int>(c));
            return "";
        }
    }

    return input;
}

// ============================================================================
// Connection Management
// ============================================================================

WiFiError WifiBackendNetworkManager::connect_network(const std::string& ssid,
                                                     const std::string& password) {
    if (!running_) {
        return WiFiError(WiFiResult::NOT_INITIALIZED, "Backend not started",
                         "WiFi system not ready");
    }

    // Validate inputs
    std::string clean_ssid = validate_input(ssid, "SSID");
    if (clean_ssid.empty()) {
        return WiFiError(WiFiResult::INVALID_PARAMETERS,
                         "SSID contains invalid characters or is empty", "Invalid network name",
                         "Check that the network name is correct");
    }

    if (!password.empty()) {
        std::string clean_pass = validate_input(password, "password");
        if (clean_pass.empty()) {
            return WiFiErrorHelper::authentication_failed(
                ssid + " (password contains invalid characters)");
        }
    }

    spdlog::info("[WifiBackend] NM: Connecting to network '{}'", clean_ssid);

    // Clean up existing connect thread
    connect_active_ = false;
    if (connect_thread_.joinable()) {
        connect_thread_.join();
    }

    // Launch connection thread (uses fork/exec for security)
    // Pass SSID/password by value to avoid shared state race conditions
    connect_active_ = true;
    connect_thread_ =
        std::thread(&WifiBackendNetworkManager::connect_thread_func, this, clean_ssid, password);

    return WiFiErrorHelper::success();
}

void WifiBackendNetworkManager::connect_thread_func(std::string ssid, std::string password) {
    spdlog::debug("[WifiBackend] NM: Connect thread started for '{}'", ssid);

    // SECURITY: Use fork/exec to avoid shell injection with SSID/password
    // nmcli device wifi connect "<ssid>" password "<pass>" ifname <iface>

    pid_t pid = fork();

    if (pid < 0) {
        spdlog::error("[WifiBackend] NM: Fork failed: {}", strerror(errno));
        if (connect_active_) {
            fire_event("DISCONNECTED", "Fork failed");
        }
        return;
    }

    if (pid == 0) {
        // Child process - exec nmcli directly (no shell)
        if (password.empty()) {
            // Open network
            execlp("nmcli", "nmcli", "device", "wifi", "connect", ssid.c_str(), "ifname",
                   wifi_interface_.c_str(), nullptr);
        } else {
            // Secured network
            execlp("nmcli", "nmcli", "device", "wifi", "connect", ssid.c_str(), "password",
                   password.c_str(), "ifname", wifi_interface_.c_str(), nullptr);
        }
        // exec failed
        _exit(127);
    }

    // Parent process - wait for child with timeout
    constexpr int CONNECT_TIMEOUT_SECONDS = 30;
    constexpr int POLL_INTERVAL_MS = 100;
    auto start_time = std::chrono::steady_clock::now();

    int status = 0;
    bool timed_out = false;
    bool child_done = false;

    while (!child_done) {
        // Check cancellation
        if (!connect_active_) {
            kill(pid, SIGTERM);
            waitpid(pid, &status, 0);
            spdlog::debug("[WifiBackend] NM: Connect cancelled");
            return;
        }

        pid_t wait_result = waitpid(pid, &status, WNOHANG);

        if (wait_result == pid) {
            child_done = true;
        } else if (wait_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            spdlog::error("[WifiBackend] NM: waitpid error: {}", strerror(errno));
            if (connect_active_) {
                fire_event("DISCONNECTED", "Internal error");
            }
            return;
        } else {
            // Check timeout
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed > std::chrono::seconds(CONNECT_TIMEOUT_SECONDS)) {
                timed_out = true;
                kill(pid, SIGTERM);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                child_done = true;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
            }
        }
    }

    if (!connect_active_) {
        return;
    }

    if (timed_out) {
        spdlog::warn("[WifiBackend] NM: Connection to '{}' timed out", ssid);
        fire_event("DISCONNECTED", "Connection timed out");
        return;
    }

    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    if (exit_code == 0) {
        spdlog::info("[WifiBackend] NM: Connected to '{}'", ssid);
        fire_event("CONNECTED");
    } else {
        // nmcli exit codes: 0=success, non-zero=failure
        // Common failures: wrong password, network not found, timeout
        spdlog::warn("[WifiBackend] NM: Connection to '{}' failed (exit code {})", ssid, exit_code);
        // nmcli uses exit code 10 for connection timeout, but doesn't distinguish
        // auth failure well. Fire AUTH_FAILED as best guess for secured networks.
        if (!password.empty()) {
            fire_event("AUTH_FAILED", "Connection failed");
        } else {
            fire_event("DISCONNECTED", "Connection failed");
        }
    }
}

WiFiError WifiBackendNetworkManager::disconnect_network() {
    if (!running_) {
        return WiFiError(WiFiResult::NOT_INITIALIZED, "Backend not started",
                         "WiFi system not ready");
    }

    spdlog::info("[WifiBackend] NM: Disconnecting from WiFi");

    // nmcli device disconnect <iface>
    std::string result = exec_nmcli("device disconnect " + wifi_interface_);

    // nmcli reports success even if already disconnected
    spdlog::debug("[WifiBackend] NM: Disconnect result: {}", result);
    fire_event("DISCONNECTED");
    return WiFiErrorHelper::success();
}

// ============================================================================
// Status
// ============================================================================

WifiBackend::ConnectionStatus WifiBackendNetworkManager::get_status() {
    ConnectionStatus status = {};
    status.connected = false;
    status.signal_strength = 0;

    if (!running_) {
        return status;
    }

    // Query GENERAL fields from device show for state, MAC, connection profile.
    // Note: WIFI.SSID is NOT a valid field for "device show" — it causes the
    // entire query to fail. The actual SSID must come from "device wifi list".
    std::string dev_info = exec_nmcli("-t -f GENERAL device show " + wifi_interface_);
    if (dev_info.empty()) {
        return status;
    }

    // Parse key:value pairs from device show output
    std::istringstream stream(dev_info);
    std::string line;

    while (std::getline(stream, line)) {
        auto fields = split_nmcli_fields(line);
        if (fields.size() < 2) {
            continue;
        }

        if (fields[0] == "GENERAL.STATE") {
            // State like "100 (connected)" or "30 (disconnected)"
            status.connected = (fields[1].find("connected") != std::string::npos) &&
                               (fields[1].find("disconnected") == std::string::npos);
        } else if (fields[0] == "GENERAL.HWADDR") {
            // nmcli doesn't escape colons in MAC addresses even in -t mode,
            // so split_nmcli_fields splits "2C:CF:67:2B:3C:01" into multiple fields.
            // Rejoin all fields after the key with ':' to reconstruct the MAC.
            std::string mac;
            for (size_t i = 1; i < fields.size(); ++i) {
                if (!mac.empty())
                    mac += ':';
                mac += fields[i];
            }
            status.mac_address = mac;
        } else if (fields[0] == "GENERAL.CONNECTION") {
            // NM profile name — used as fallback SSID if wifi list lookup fails
            if (fields[1] != "--") {
                status.ssid = fields[1];
            }
        }
    }

    // If connected, get SSID + signal from wifi list, and IP from device show.
    // GENERAL.CONNECTION gives the NM profile name which can be "preconfigured"
    // or other non-SSID values. The wifi list gives the real broadcast SSID.
    if (status.connected) {
        // Get actual SSID and signal from the IN-USE network in wifi list
        std::string wifi_info =
            exec_nmcli("-t -f IN-USE,SSID,SIGNAL device wifi list ifname " + wifi_interface_);
        if (!wifi_info.empty()) {
            std::istringstream wifi_stream(wifi_info);
            std::string wifi_line;
            while (std::getline(wifi_stream, wifi_line)) {
                auto wifi_fields = split_nmcli_fields(wifi_line);
                if (wifi_fields.size() >= 3 && wifi_fields[0] == "*") {
                    if (!wifi_fields[1].empty()) {
                        status.ssid = wifi_fields[1];
                    }
                    try {
                        status.signal_strength =
                            std::max(0, std::min(100, std::stoi(wifi_fields[2])));
                    } catch (...) {
                    }
                    break;
                }
            }
        }

        // Get IP address from device show (uses validated wifi_interface_)
        std::string ip_info = exec_nmcli("-t -f IP4.ADDRESS device show " + wifi_interface_);
        if (!ip_info.empty()) {
            std::istringstream ip_stream(ip_info);
            std::string ip_line;
            while (std::getline(ip_stream, ip_line)) {
                auto ip_fields = split_nmcli_fields(ip_line);
                if (ip_fields.size() >= 2 &&
                    ip_fields[0].find("IP4.ADDRESS") != std::string::npos) {
                    // Value is like "192.168.1.100/24"
                    std::string ip = ip_fields[1];
                    size_t slash = ip.find('/');
                    if (slash != std::string::npos) {
                        ip = ip.substr(0, slash);
                    }
                    status.ip_address = ip;
                    break;
                }
            }
        }
    }

    spdlog::trace("[WifiBackend] NM: Status: connected={} ssid='{}' ip='{}' signal={}%",
                  status.connected, status.ssid, status.ip_address, status.signal_strength);

    return status;
}

bool WifiBackendNetworkManager::supports_5ghz() const {
    if (!running_ || wifi_interface_.empty()) {
        return false;
    }

    // Check WiFi capabilities via nmcli
    // nmcli -t -f WIFI-PROPERTIES device show <iface>
    // Look for "WIFI-PROPERTIES.FREQ" or similar indicating 5GHz support
    std::string props = const_cast<WifiBackendNetworkManager*>(this)->exec_nmcli(
        "-t -f WIFI-PROPERTIES device show " + wifi_interface_);

    if (props.empty()) {
        return false;
    }

    // Look for 5GHz capability indicator
    if (props.find("5GHz") != std::string::npos || props.find("5 GHz") != std::string::npos) {
        return true;
    }

    // Default to false for embedded targets (same as wpa_supplicant backend)
    return false;
}

#endif // !__APPLE__ && !__ANDROID__
