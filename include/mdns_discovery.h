// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace helix {

/**
 * @brief Represents a discovered Moonraker printer on the local network
 */
struct DiscoveredPrinter {
    std::string name;       ///< Display name (hostname without .local)
    std::string hostname;   ///< Full hostname (e.g., "voron.local")
    std::string ip_address; ///< Resolved IPv4 address
    uint16_t port;          ///< Service port (usually 7125)

    /**
     * @brief Equality operator for deduplication
     *
     * Two printers are considered equal if they have the same IP and port,
     * regardless of hostname differences (same service, different resolution paths).
     */
    bool operator==(const DiscoveredPrinter& other) const {
        return ip_address == other.ip_address && port == other.port;
    }
};

/**
 * @brief Abstract interface for mDNS discovery
 *
 * Allows dependency injection of mock implementations for testing.
 */
class IMdnsDiscovery {
  public:
    using DiscoveryCallback = std::function<void(const std::vector<DiscoveredPrinter>&)>;

    virtual ~IMdnsDiscovery() = default;

    virtual void start_discovery(DiscoveryCallback on_update) = 0;
    virtual void stop_discovery() = 0;
    virtual bool is_discovering() const = 0;
    virtual std::vector<DiscoveredPrinter> get_discovered_printers() const = 0;
};

/**
 * @brief mDNS discovery service for finding Moonraker instances on the local network
 *
 * This class provides network discovery of Moonraker 3D printer API servers using
 * mDNS/DNS-SD (Bonjour/Avahi). It queries for `_moonraker._tcp.local` services
 * and resolves them to IP addresses.
 *
 * Threading model:
 * - Discovery runs on a background thread
 * - Callbacks are dispatched to the main LVGL thread via helix::ui::async_call()
 * - stop_discovery() blocks until the background thread exits
 *
 * Usage:
 * @code
 * MdnsDiscovery discovery;
 * discovery.start_discovery([](const std::vector<DiscoveredPrinter>& printers) {
 *     for (const auto& printer : printers) {
 *         spdlog::info("Found: {} at {}:{}", printer.name, printer.ip_address, printer.port);
 *     }
 * });
 *
 * // Later...
 * discovery.stop_discovery();
 * @endcode
 */
class MdnsDiscovery : public IMdnsDiscovery {
  public:
    /**
     * @brief Callback type for discovery updates
     *
     * Called on the main thread whenever the list of discovered printers changes.
     * The vector contains all currently known printers (not just new ones).
     */
    using DiscoveryCallback = std::function<void(const std::vector<DiscoveredPrinter>&)>;

    MdnsDiscovery();
    ~MdnsDiscovery();

    // Non-copyable (owns background thread)
    MdnsDiscovery(const MdnsDiscovery&) = delete;
    MdnsDiscovery& operator=(const MdnsDiscovery&) = delete;

    /**
     * @brief Start discovering Moonraker instances on the network
     *
     * Begins periodic mDNS queries for `_moonraker._tcp.local` services.
     * The callback is invoked on the main thread whenever the list of
     * discovered printers changes.
     *
     * If discovery is already running, the callback is updated and
     * an immediate update is dispatched with current results.
     *
     * @param on_update Callback invoked with updated printer list
     */
    void start_discovery(DiscoveryCallback on_update) override;

    /**
     * @brief Stop discovering printers
     *
     * Stops the background discovery thread and clears the callback.
     * This method blocks until the thread has fully exited.
     *
     * Safe to call multiple times or when not discovering.
     */
    void stop_discovery() override;

    /**
     * @brief Check if discovery is currently active
     *
     * @return true if actively discovering, false otherwise
     */
    bool is_discovering() const override;

    /**
     * @brief Get the current list of discovered printers
     *
     * Thread-safe snapshot of currently known printers.
     *
     * @return Vector of discovered printers (may be empty)
     */
    std::vector<DiscoveredPrinter> get_discovered_printers() const override;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace helix
