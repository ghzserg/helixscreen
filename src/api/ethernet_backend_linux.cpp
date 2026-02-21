// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if !defined(__ANDROID__)

#include "ethernet_backend_linux.h"

#include "ifconfig.h" // libhv's cross-platform ifconfig utility

#include <spdlog/spdlog.h>

#include <algorithm>
#include <dirent.h>
#include <fstream>
#include <memory>

EthernetBackendLinux::EthernetBackendLinux() {
    spdlog::debug("[EthernetLinux] Linux backend created");
}

EthernetBackendLinux::~EthernetBackendLinux() {
    // Use fprintf - spdlog may be destroyed during static cleanup
    fprintf(stderr, "[EthernetLinux] Linux backend destroyed\n");
}

bool EthernetBackendLinux::is_ethernet_interface(const std::string& name) {
    // Linux physical Ethernet interface patterns:
    // - eth0, eth1, ...           (traditional naming)
    // - eno1, eno2, ...           (onboard, firmware naming)
    // - enp<bus>s<slot>           (PCI bus/slot naming, e.g., enp3s0)
    // - enP<domain>p<bus>s<slot>  (Rockchip/Orange Pi PCI domain naming)
    // - ens<slot>                 (hot-plug naming, e.g., ens33)
    //
    // Exclude:
    // - lo                        (loopback)
    // - wlan*, wlp*               (WiFi)
    // - docker*, br-*, virbr*     (virtual bridges)
    // - veth*                     (virtual Ethernet pairs for containers)
    // - tun*, tap*                (VPN/tunnels)

    // Accept eth*
    if (name.compare(0, 3, "eth") == 0) {
        return true;
    }

    // Accept eno* (onboard Ethernet)
    if (name.compare(0, 3, "eno") == 0) {
        return true;
    }

    // Accept enp* (PCI naming)
    if (name.compare(0, 3, "enp") == 0) {
        return true;
    }

    // Accept enP* (Orange Pi / Rockchip specific naming)
    if (name.compare(0, 3, "enP") == 0) {
        return true;
    }

    // Accept ens* (hot-plug naming)
    if (name.compare(0, 3, "ens") == 0) {
        return true;
    }

    // Reject everything else (lo, wlan*, docker*, etc.)
    return false;
}

std::string EthernetBackendLinux::read_operstate(const std::string& interface) {
    // Read /sys/class/net/<interface>/operstate
    std::string path = "/sys/class/net/" + interface + "/operstate";
    std::ifstream file(path);

    if (!file.is_open()) {
        spdlog::warn("[EthernetLinux] Cannot read operstate: {}", path);
        return "";
    }

    std::string state;
    std::getline(file, state);
    file.close();

    // Trim whitespace
    state.erase(std::remove_if(state.begin(), state.end(), ::isspace), state.end());

    spdlog::trace("[EthernetLinux] {} operstate: {}", interface, state);
    return state;
}

std::vector<std::string> EthernetBackendLinux::scan_sysfs_interfaces() {
    std::vector<std::string> ethernet_interfaces;

    // Scan /sys/class/net/ directly - this finds interfaces regardless of IP assignment
    const char* sysfs_net = "/sys/class/net";

    // RAII guard: unique_ptr with custom deleter ensures closedir() is always called
    auto dir_deleter = [](DIR* d) {
        if (d)
            closedir(d);
    };
    std::unique_ptr<DIR, decltype(dir_deleter)> dir(opendir(sysfs_net), dir_deleter);

    if (!dir) {
        spdlog::warn("[EthernetLinux] Cannot open {}", sysfs_net);
        return ethernet_interfaces;
    }

    struct dirent* entry;
    while ((entry = readdir(dir.get())) != nullptr) {
        // Skip . and ..
        if (entry->d_name[0] == '.') {
            continue;
        }

        std::string name = entry->d_name;
        if (is_ethernet_interface(name)) {
            spdlog::debug("[EthernetLinux] Found Ethernet interface via sysfs: {}", name);
            ethernet_interfaces.push_back(name);
        }
    }

    // No manual closedir() needed - RAII handles cleanup
    return ethernet_interfaces;
}

bool EthernetBackendLinux::has_interface() {
    // Use sysfs scan which finds interfaces regardless of IP assignment
    // This is more reliable than ifconfig() which may not return interfaces without IPs
    auto interfaces = scan_sysfs_interfaces();

    if (!interfaces.empty()) {
        spdlog::debug("[EthernetLinux] has_interface() = true ({} found)", interfaces[0]);
        return true;
    }

    spdlog::debug("[EthernetLinux] No Ethernet interface found");
    return false;
}

EthernetInfo EthernetBackendLinux::get_info() {
    EthernetInfo info;

    std::vector<ifconfig_t> interfaces;
    int result = ifconfig(interfaces);

    if (result != 0) {
        spdlog::error("[EthernetLinux] ifconfig() failed with code: {}", result);
        info.status = "Error querying interfaces";
        return info;
    }

    // Strategy: Find first Ethernet interface with "up" operstate and valid IP
    // Preference order:
    // 1. First eth*/eno*/enp*/ens* with operstate "up" and valid IP
    // 2. First eth*/eno*/enp*/ens* with valid IP (ignore operstate)
    // 3. First eth*/eno*/enp*/ens* interface found (even without IP)

    ifconfig_t* first_ethernet = nullptr;
    ifconfig_t* ip_ethernet = nullptr;
    ifconfig_t* up_ethernet = nullptr;

    for (auto& iface : interfaces) {
        std::string name = iface.name;
        std::string ip = iface.ip;

        if (!is_ethernet_interface(name)) {
            continue;
        }

        // Remember first Ethernet interface
        if (!first_ethernet) {
            first_ethernet = &iface;
        }

        // Check if it has a valid IP
        bool has_ip = !ip.empty() && ip != "0.0.0.0" && ip != "127.0.0.1";
        if (has_ip && !ip_ethernet) {
            ip_ethernet = &iface;
        }

        // Check operstate
        std::string operstate = read_operstate(name);
        if (operstate == "up" && has_ip) {
            up_ethernet = &iface;
            break; // Found best match, use it
        }
    }

    // Use best available interface
    ifconfig_t* selected = up_ethernet ? up_ethernet : ip_ethernet ? ip_ethernet : first_ethernet;

    if (!selected) {
        // Fall back to sysfs scan for interfaces without IPs
        auto sysfs_interfaces = scan_sysfs_interfaces();
        if (sysfs_interfaces.empty()) {
            info.status = "No Ethernet interface";
            spdlog::debug("[EthernetLinux] No Ethernet interface found");
            return info;
        }

        // Found interface via sysfs - populate basic info
        info.interface = sysfs_interfaces[0];
        std::string operstate = read_operstate(info.interface);

        if (operstate == "down") {
            info.connected = false;
            info.status = "No cable";
            spdlog::debug("[EthernetLinux] Ethernet cable disconnected: {} (operstate: {})",
                          info.interface, operstate);
        } else {
            info.connected = false;
            info.status = "No connection";
            spdlog::debug("[EthernetLinux] Ethernet interface {} has no IP (operstate: {})",
                          info.interface, operstate);
        }

        return info;
    }

    // Populate info from selected interface
    info.interface = selected->name;
    info.ip_address = selected->ip;
    info.mac_address = selected->mac;

    // Read operstate for status determination
    std::string operstate = read_operstate(info.interface);

    // Determine connection status based on IP and operstate
    bool has_ip =
        !info.ip_address.empty() && info.ip_address != "0.0.0.0" && info.ip_address != "127.0.0.1";

    if (has_ip && operstate == "up") {
        info.connected = true;
        info.status = "Connected";
        spdlog::debug("[EthernetLinux] Ethernet connected: {} ({}, operstate: {})", info.interface,
                      info.ip_address, operstate);
    } else if (has_ip) {
        info.connected = true;
        info.status = "Connected";
        spdlog::info("[EthernetLinux] Ethernet has IP: {} ({}, operstate: {})", info.interface,
                     info.ip_address, operstate);
    } else if (operstate == "down") {
        info.connected = false;
        info.status = "No cable";
        spdlog::debug("[EthernetLinux] Ethernet cable disconnected: {} (operstate: {})",
                      info.interface, operstate);
    } else {
        info.connected = false;
        info.status = "No connection";
        spdlog::debug("[EthernetLinux] Ethernet interface {} has no IP (operstate: {})",
                      info.interface, operstate);
    }

    return info;
}

#endif // !__ANDROID__
