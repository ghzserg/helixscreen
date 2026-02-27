// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "json_fwd.h"
#include "printer_discovery.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace helix {

class MoonrakerClient; // Forward declaration

/**
 * @brief Owns the multi-step async printer discovery flow
 *
 * Discovery timeline:
 * 1. server.connection.identify → identified
 * 2. server.info → klippy_state gate (abort if not ready/shutdown)
 * 3. printer.objects.list → parse_objects() → on_hardware_discovered (silent)
 * 4. server.info → Moonraker version, Spoolman/webcam detection
 * 5. printer.info → hostname, software_version
 * 6. MCU queries → firmware versions
 * 7. printer.objects.subscribe → initial state dispatched
 * 8. on_discovery_complete
 */
class MoonrakerDiscoverySequence {
  public:
    explicit MoonrakerDiscoverySequence(MoonrakerClient& client);

    // Non-copyable (has mutex, references)
    MoonrakerDiscoverySequence(const MoonrakerDiscoverySequence&) = delete;
    MoonrakerDiscoverySequence& operator=(const MoonrakerDiscoverySequence&) = delete;

    /**
     * @brief Start the discovery sequence
     *
     * Begins with server.connection.identify, then checks Klippy readiness
     * via server.info gate before chaining through
     * objects.list → server.info → printer.info → MCU queries → subscribe.
     *
     * @param on_complete Called when discovery finishes successfully
     * @param on_error Called if discovery fails (e.g., Klippy not connected)
     */
    void start(std::function<void()> on_complete,
               std::function<void(const std::string& reason)> on_error = nullptr);

    /**
     * @brief Parse Klipper object list into typed hardware vectors
     *
     * Categorizes objects into heaters, sensors, fans, LEDs, steppers,
     * AFC objects, and filament sensors.
     *
     * @param objects JSON array of object name strings
     */
    void parse_objects(const json& objects);

    /**
     * @brief Forward bed mesh data to registered callback
     * @param bed_mesh JSON from bed_mesh subscription
     */
    void parse_bed_mesh(const json& bed_mesh);

    /** @brief Reset identification state (call on disconnect) */
    void reset_identified() {
        identified_.store(false);
    }

    /** @brief Check if identified to Moonraker */
    [[nodiscard]] bool is_identified() const {
        return identified_.load();
    }

    /** @brief Clear all cached discovery data (vectors + hardware) */
    void clear_cache();

    /** @brief Get discovered hardware data (const) */
    [[nodiscard]] const PrinterDiscovery& hardware() const {
        return hardware_;
    }

    /** @brief Get discovered hardware data (mutable, for kinematics update) */
    PrinterDiscovery& hardware() {
        return hardware_;
    }

    /** @brief Set callback for early hardware discovery phase (after parse_objects) */
    void set_on_hardware_discovered(std::function<void(const PrinterDiscovery&)> cb) {
        on_hardware_discovered_ = std::move(cb);
    }

    /** @brief Set callback for discovery completion (after subscription) */
    void set_on_discovery_complete(std::function<void(const PrinterDiscovery&)> cb) {
        on_discovery_complete_ = std::move(cb);
    }

    /** @brief Set callback for bed mesh updates */
    void set_bed_mesh_callback(std::function<void(const json&)> cb) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        bed_mesh_callback_ = std::move(cb);
    }

    // ======== Callback invocation (for mock to trigger discovery callbacks) ========

    /** @brief Invoke the on_hardware_discovered callback with current hardware */
    void invoke_hardware_discovered() {
        if (on_hardware_discovered_)
            on_hardware_discovered_(hardware_);
    }

    /** @brief Invoke the on_discovery_complete callback with current hardware */
    void invoke_discovery_complete() {
        if (on_discovery_complete_)
            on_discovery_complete_(hardware_);
    }

    // ======== Hardware vector accessors (for mock to populate directly) ========
    // Thread safety: mutable accessors must only be called before start() or
    // from the same thread as discovery callbacks. Not safe for concurrent use.

    std::vector<std::string>& heaters() {
        return heaters_;
    }
    std::vector<std::string>& sensors() {
        return sensors_;
    }
    std::vector<std::string>& fans() {
        return fans_;
    }
    std::vector<std::string>& leds() {
        return leds_;
    }
    std::vector<std::string>& steppers() {
        return steppers_;
    }
    std::vector<std::string>& afc_objects() {
        return afc_objects_;
    }
    std::vector<std::string>& filament_sensors() {
        return filament_sensors_;
    }

    const std::vector<std::string>& heaters() const {
        return heaters_;
    }
    const std::vector<std::string>& sensors() const {
        return sensors_;
    }
    const std::vector<std::string>& fans() const {
        return fans_;
    }
    const std::vector<std::string>& leds() const {
        return leds_;
    }
    const std::vector<std::string>& steppers() const {
        return steppers_;
    }
    const std::vector<std::string>& afc_objects() const {
        return afc_objects_;
    }
    const std::vector<std::string>& filament_sensors() const {
        return filament_sensors_;
    }

  private:
    /**
     * @brief Check if the current discovery sequence is stale
     *
     * Returns true if the connection has been recycled since start() was called,
     * meaning any in-flight callbacks should be silently dropped.
     */
    bool is_stale() const;

    /**
     * @brief Continue discovery after server.connection.identify
     *
     * Calls server.info to check klippy_state (gate). If Klippy is ready
     * or shutdown, proceeds to continue_discovery_objects(). Otherwise aborts.
     */
    void continue_discovery();

    /**
     * @brief Continue discovery after Klippy readiness gate passes
     *
     * Chains: objects.list → server.info → printer.info → MCU queries → subscribe
     */
    void continue_discovery_objects();

    /**
     * @brief Complete discovery by subscribing to printer objects
     *
     * Builds subscription JSON from discovered objects, subscribes,
     * dispatches initial state to all registered callbacks.
     */
    void complete_discovery_subscription();

    MoonrakerClient& client_;

    // Discovery sequence callbacks (stored once in start(), used by chained methods)
    std::function<void()> on_complete_discovery_;
    std::function<void(const std::string&)> on_error_discovery_;
    uint64_t discovery_generation_{0};

    // Hardware vectors
    std::vector<std::string> heaters_;
    std::vector<std::string> sensors_;
    std::vector<std::string> fans_;
    std::vector<std::string> leds_;
    std::vector<std::string> steppers_;
    std::vector<std::string> afc_objects_;
    std::vector<std::string> filament_sensors_;

    PrinterDiscovery hardware_;
    std::atomic<bool> identified_{false};

    // Callbacks
    std::function<void(const PrinterDiscovery&)> on_hardware_discovered_;
    std::function<void(const PrinterDiscovery&)> on_discovery_complete_;
    std::function<void(const json&)> bed_mesh_callback_;
    std::mutex callbacks_mutex_;
};

} // namespace helix
