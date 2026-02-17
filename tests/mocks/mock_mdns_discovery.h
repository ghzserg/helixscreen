// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MOCK_MDNS_DISCOVERY_H
#define MOCK_MDNS_DISCOVERY_H

/**
 * @file mock_mdns_discovery.h
 * @brief Mock mDNS discovery for testing
 *
 * Provides a no-op mDNS discovery that:
 * - Doesn't start background threads
 * - Doesn't do real network I/O
 * - Returns an empty printer list (or configured test printers)
 *
 * Use this in tests that would otherwise hang on mDNS timer processing.
 *
 * @example
 * MockMdnsDiscovery mock;
 * mock.start_discovery([](const std::vector<DiscoveredPrinter>& printers) {
 *     // Callback receives empty list by default
 * });
 */

#include "mdns_discovery.h"

#include <vector>

using namespace helix;

/**
 * @brief Mock mDNS discovery that finds nothing (by default)
 *
 * Does not start any threads or perform network I/O.
 * Optionally can be configured to return fake printers for testing.
 */
class MockMdnsDiscovery : public IMdnsDiscovery {
  public:
    MockMdnsDiscovery() = default;
    ~MockMdnsDiscovery() override = default;

    // Non-copyable
    MockMdnsDiscovery(const MockMdnsDiscovery&) = delete;
    MockMdnsDiscovery& operator=(const MockMdnsDiscovery&) = delete;

    /**
     * @brief Start "discovering" - immediately calls callback with configured printers
     *
     * Does NOT start any background threads. Callback is invoked synchronously
     * with the current set of fake printers (empty by default).
     */
    void start_discovery(DiscoveryCallback on_update) override {
        callback_ = std::move(on_update);
        discovering_ = true;

        // Immediately invoke callback with current (possibly empty) printer list
        if (callback_) {
            callback_(fake_printers_);
        }
    }

    /**
     * @brief Stop "discovering" - just clears state
     */
    void stop_discovery() override {
        discovering_ = false;
        callback_ = nullptr;
    }

    /**
     * @brief Check if mock is in "discovering" state
     */
    bool is_discovering() const override {
        return discovering_;
    }

    /**
     * @brief Get configured fake printers
     */
    std::vector<DiscoveredPrinter> get_discovered_printers() const override {
        return fake_printers_;
    }

    // =========================================================================
    // Test Control Methods
    // =========================================================================

    /**
     * @brief Add a fake printer for testing
     *
     * @param name Display name
     * @param hostname Full hostname (e.g., "voron.local")
     * @param ip IPv4 address
     * @param port Service port
     */
    void add_fake_printer(const std::string& name, const std::string& hostname,
                          const std::string& ip, uint16_t port = 7125) {
        fake_printers_.push_back({name, hostname, ip, port});
    }

    /**
     * @brief Clear all fake printers
     */
    void clear_fake_printers() {
        fake_printers_.clear();
    }

    /**
     * @brief Simulate discovering a printer (triggers callback)
     */
    void simulate_discovery() {
        if (callback_) {
            callback_(fake_printers_);
        }
    }

  private:
    bool discovering_ = false;
    DiscoveryCallback callback_;
    std::vector<DiscoveredPrinter> fake_printers_;
};

#endif // MOCK_MDNS_DISCOVERY_H
