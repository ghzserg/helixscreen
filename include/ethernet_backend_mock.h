// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ethernet_backend.h"

#include <string>

/**
 * @brief Mock Ethernet backend for simulator and testing
 *
 * Provides fake Ethernet functionality with static data:
 * - Always reports interface as available
 * - Returns fixed IP address (192.168.1.150)
 * - Connected status
 * - Fake MAC address
 *
 * Perfect for:
 * - macOS/simulator development
 * - UI testing without real Ethernet hardware
 * - Automated testing scenarios
 * - Fallback when platform backends fail
 */
class EthernetBackendMock : public EthernetBackend {
  public:
    EthernetBackendMock();
    ~EthernetBackendMock() override;

    // ========================================================================
    // EthernetBackend Interface Implementation
    // ========================================================================

    bool has_interface() override;
    EthernetInfo get_info() override;

  private:
    std::string real_mac_; ///< Real MAC from system for realistic demo display
};
