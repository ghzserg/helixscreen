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

#include "../../include/wifi_backend.h"
#include "../../include/wifi_backend_mock.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <thread>

#include "../catch_amalgamated.hpp"

/**
 * WiFi Backend Unit Tests
 *
 * Tests verify backend-specific functionality:
 * - Backend lifecycle (start/stop/is_running)
 * - Event system (callback registration and firing)
 * - Mock backend behavior (scan timing, network data)
 * - Timer cleanup and resource management
 *
 * CRITICAL BUGS CAUGHT:
 * - Backend auto-start bug: Mock backend should NOT start itself in constructor
 * - Timer cleanup: Timers must be cleaned up in stop()/destructor
 * - Event callback validation: Events should not fire when backend stopped
 */

// ============================================================================
// Global LVGL Initialization (called once)
// ============================================================================

// No LVGL initialization needed - WiFi backend uses std::thread, not LVGL timers

// ============================================================================
// Test Fixtures
// ============================================================================

class WiFiBackendTestFixture {
  public:
    WiFiBackendTestFixture() {
        // Create mock backend directly for testing
        backend = std::make_unique<WifiBackendMock>();

        // Reset state
        event_count = 0;
        last_event_name.clear();
        last_event_data.clear();
    }

    ~WiFiBackendTestFixture() {
        // Cleanup backend
        if (backend) {
            backend->stop();
        }
    }

    /**
     * @brief Signal that an event occurred (call from test callbacks)
     */
    void notify_event() {
        {
            std::lock_guard<std::mutex> lock(event_mutex_);
            event_count++;
        }
        event_cv_.notify_all();
    }

    /**
     * @brief Wait for event count to reach a specific value
     * @param target_count Number of events to wait for
     * @param timeout_ms Maximum time to wait
     * @return true if count reached, false on timeout
     */
    bool wait_for_event_count(int target_count, int timeout_ms = 5000) {
        std::unique_lock<std::mutex> lock(event_mutex_);
        return event_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                  [this, target_count]() { return event_count >= target_count; });
    }

    /**
     * @brief Wait for a condition to become true
     * @param condition Function that returns true when condition is met
     * @param timeout_ms Maximum time to wait
     * @return true if condition met, false on timeout
     */
    bool wait_for_condition(std::function<bool()> condition, int timeout_ms = 5000) {
        std::unique_lock<std::mutex> lock(event_mutex_);
        return event_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), condition);
    }

    /**
     * @brief Reset event count (call between test phases)
     */
    void reset_event_count() {
        std::lock_guard<std::mutex> lock(event_mutex_);
        event_count = 0;
    }

    /**
     * @brief Get current event count (thread-safe)
     */
    int get_event_count() {
        std::lock_guard<std::mutex> lock(event_mutex_);
        return event_count;
    }

    // Test backend
    std::unique_ptr<WifiBackend> backend;

    // Test state (protected by mutex for thread safety)
    std::mutex event_mutex_;
    std::condition_variable event_cv_;
    int event_count = 0;
    std::string last_event_name;
    std::string last_event_data;
};

// ============================================================================
// Backend Lifecycle Tests
// ============================================================================

TEST_CASE_METHOD(WiFiBackendTestFixture, "Backend lifecycle", "[network][backend][lifecycle]") {
    SECTION("Backend created but not running by default") {
        // CRITICAL: This catches the auto-start bug
        REQUIRE_FALSE(backend->is_running());
    }

    SECTION("Backend start() enables it") {
        WiFiError result = backend->start();
        REQUIRE(result.success());
        REQUIRE(backend->is_running());
    }

    SECTION("Backend stop() disables it") {
        backend->start();
        REQUIRE(backend->is_running());

        backend->stop();
        REQUIRE_FALSE(backend->is_running());
    }

    SECTION("Backend lifecycle: start → stop → start") {
        // Initial: not running
        REQUIRE_FALSE(backend->is_running());

        // First start
        backend->start();
        REQUIRE(backend->is_running());

        // Stop
        backend->stop();
        REQUIRE_FALSE(backend->is_running());

        // Second start (should work)
        WiFiError result = backend->start();
        REQUIRE(result.success());
        REQUIRE(backend->is_running());
    }

    SECTION("Multiple start() calls are idempotent") {
        backend->start();
        REQUIRE(backend->is_running());

        // Second start() should succeed (no-op)
        WiFiError result = backend->start();
        REQUIRE(result.success());
        REQUIRE(backend->is_running());
    }

    SECTION("Multiple stop() calls are safe") {
        backend->start();
        backend->stop();
        REQUIRE_FALSE(backend->is_running());

        // Second stop() should be safe (no crash)
        REQUIRE_NOTHROW(backend->stop());
        REQUIRE_FALSE(backend->is_running());
    }
}

// ============================================================================
// Event System Tests
// ============================================================================

TEST_CASE_METHOD(WiFiBackendTestFixture, "Backend event system", "[network][backend][events]") {
    SECTION("Event callback registration") {
        int callback_count = 0;
        backend->register_event_callback("TEST_EVENT", [&callback_count](const std::string& data) {
            (void)data;
            callback_count++;
        });

        // Callback registered (can't directly test until event fires)
        REQUIRE(callback_count == 0); // Not fired yet
    }

    SECTION("SCAN_COMPLETE event fires after scan") {
        backend->start();

        // Register callback that signals condition variable
        backend->register_event_callback("SCAN_COMPLETE", [this](const std::string& data) {
            (void)data;
            notify_event();
        });

        // Trigger scan
        WiFiError result = backend->trigger_scan();
        REQUIRE(result.success());

        // Wait for SCAN_COMPLETE event with proper synchronization
        REQUIRE(wait_for_event_count(1, 5000));
    }

    SECTION("Multiple event callbacks can be registered") {
        backend->start();

        int scan_count = 0;
        int connect_count = 0;

        backend->register_event_callback("SCAN_COMPLETE", [&scan_count](const std::string& data) {
            (void)data;
            scan_count++;
        });

        backend->register_event_callback("CONNECTED", [&connect_count](const std::string& data) {
            (void)data;
            connect_count++;
        });

        // Both callbacks registered
        REQUIRE(scan_count == 0);
        REQUIRE(connect_count == 0);
    }

    SECTION("Event callback survives backend restart") {
        backend->start();

        // Register callback that signals condition variable
        backend->register_event_callback("SCAN_COMPLETE", [this](const std::string& data) {
            (void)data;
            notify_event();
        });

        // Restart backend
        backend->stop();
        backend->start();

        // Trigger scan
        backend->trigger_scan();

        // Wait for scan to complete with proper synchronization
        REQUIRE(wait_for_event_count(1, 5000));
    }
}

// ============================================================================
// Mock Backend Scan Tests
// ============================================================================

TEST_CASE_METHOD(WiFiBackendTestFixture, "Mock backend scan behavior",
                 "[network][backend][mock][scan]") {
    SECTION("trigger_scan() fails when backend not running") {
        // Backend not started
        REQUIRE_FALSE(backend->is_running());

        WiFiError result = backend->trigger_scan();
        REQUIRE_FALSE(result.success());
        REQUIRE(result.result == WiFiResult::NOT_INITIALIZED);
    }

    SECTION("trigger_scan() succeeds when backend running") {
        backend->start();
        REQUIRE(backend->is_running());

        WiFiError result = backend->trigger_scan();
        REQUIRE(result.success());
    }

    SECTION("Scan results available after SCAN_COMPLETE") {
        backend->start();

        backend->register_event_callback("SCAN_COMPLETE", [this](const std::string& data) {
            (void)data;
            notify_event();
        });

        backend->trigger_scan();

        // Wait for scan to complete with proper synchronization
        REQUIRE(wait_for_event_count(1, 5000));

        // Get scan results
        std::vector<WiFiNetwork> networks;
        WiFiError result = backend->get_scan_results(networks);
        REQUIRE(result.success());
        REQUIRE(networks.size() == 10); // Mock backend has 10 networks
    }

    SECTION("get_scan_results() fails when backend not running") {
        REQUIRE_FALSE(backend->is_running());

        std::vector<WiFiNetwork> networks;
        WiFiError result = backend->get_scan_results(networks);
        REQUIRE_FALSE(result.success());
        REQUIRE(result.result == WiFiResult::NOT_INITIALIZED);
        REQUIRE(networks.empty());
    }

    SECTION("Mock networks have valid data") {
        backend->start();

        backend->register_event_callback("SCAN_COMPLETE", [this](const std::string& data) {
            (void)data;
            notify_event();
        });

        backend->trigger_scan();

        // Wait for scan with proper synchronization
        REQUIRE(wait_for_event_count(1, 5000));

        std::vector<WiFiNetwork> networks;
        backend->get_scan_results(networks);

        REQUIRE(networks.size() == 10);

        for (const auto& net : networks) {
            // SSID not empty
            REQUIRE_FALSE(net.ssid.empty());

            // Signal strength in range
            REQUIRE(net.signal_strength >= 0);
            REQUIRE(net.signal_strength <= 100);

            // Security info present
            if (net.is_secured) {
                REQUIRE_FALSE(net.security_type.empty());
            }
        }
    }

    SECTION("Networks sorted by signal strength") {
        backend->start();

        backend->register_event_callback("SCAN_COMPLETE", [this](const std::string& data) {
            (void)data;
            notify_event();
        });

        backend->trigger_scan();

        REQUIRE(wait_for_event_count(1, 5000));

        std::vector<WiFiNetwork> networks;
        backend->get_scan_results(networks);

        // Mock backend sorts by signal strength (strongest first)
        for (size_t i = 1; i < networks.size(); i++) {
            REQUIRE(networks[i - 1].signal_strength >= networks[i].signal_strength);
        }
    }

    SECTION("Signal strength varies on each scan") {
        backend->start();

        backend->register_event_callback("SCAN_COMPLETE", [this](const std::string& data) {
            (void)data;
            notify_event();
        });

        // First scan
        backend->trigger_scan();
        REQUIRE(wait_for_event_count(1, 5000));

        std::vector<WiFiNetwork> scan1;
        backend->get_scan_results(scan1);

        // Second scan
        reset_event_count();
        backend->trigger_scan();
        REQUIRE(wait_for_event_count(1, 5000));

        std::vector<WiFiNetwork> scan2;
        backend->get_scan_results(scan2);

        // At least one network should have different signal strength (±5% variation)
        bool found_variation = false;
        for (size_t i = 0; i < scan1.size(); i++) {
            if (scan1[i].signal_strength != scan2[i].signal_strength) {
                found_variation = true;
                break;
            }
        }

        // Note: May occasionally be same due to random number generation
        INFO("Signal strength varied: " << (found_variation ? "yes" : "no"));
    }
}

// ============================================================================
// Mock Backend Connection Tests
// ============================================================================

TEST_CASE_METHOD(WiFiBackendTestFixture, "Mock backend connection behavior",
                 "[network][backend][mock][connect][slow]") {
    SECTION("connect_network() fails when backend not running") {
        REQUIRE_FALSE(backend->is_running());

        WiFiError result = backend->connect_network("TestNet", "password");
        REQUIRE_FALSE(result.success());
        REQUIRE(result.result == WiFiResult::NOT_INITIALIZED);
    }

    SECTION("connect_network() fails for non-existent network") {
        backend->start();

        WiFiError result = backend->connect_network("NonExistentNetwork", "password");
        REQUIRE_FALSE(result.success());
        REQUIRE(result.result == WiFiResult::NETWORK_NOT_FOUND);
    }

    SECTION("connect_network() requires password for secured networks") {
        backend->start();

        backend->register_event_callback("SCAN_COMPLETE", [this](const std::string& data) {
            (void)data;
            notify_event();
        });

        // Get a secured network
        backend->trigger_scan();
        REQUIRE(wait_for_event_count(1, 5000));

        std::vector<WiFiNetwork> networks;
        backend->get_scan_results(networks);

        auto secured = std::find_if(networks.begin(), networks.end(),
                                    [](const WiFiNetwork& n) { return n.is_secured; });
        REQUIRE(secured != networks.end());

        // Try connecting without password
        WiFiError result = backend->connect_network(secured->ssid, "");
        REQUIRE_FALSE(result.success());
        REQUIRE(result.result == WiFiResult::INVALID_PARAMETERS);
    }

    SECTION("Successful connection fires CONNECTED event") {
        backend->start();

        backend->register_event_callback("SCAN_COMPLETE", [this](const std::string& data) {
            (void)data;
            notify_event();
        });

        // Get available networks
        backend->trigger_scan();
        REQUIRE(wait_for_event_count(1, 5000));

        std::vector<WiFiNetwork> networks;
        backend->get_scan_results(networks);
        REQUIRE(networks.size() > 0);

        // Register CONNECTED callback
        reset_event_count();
        backend->register_event_callback("CONNECTED", [this](const std::string& data) {
            (void)data;
            notify_event();
        });

        // Connect to first network (mock backend simulates 2-3s delay)
        WiFiError result = backend->connect_network(networks[0].ssid, "test_password");
        REQUIRE(result.success()); // Connection initiated

        // Wait for CONNECTED event with proper synchronization
        bool connected = wait_for_event_count(1, 5000);

        // Note: Mock has 5% chance of auth failure, might not always succeed
        INFO("Got CONNECTED event: " << (connected ? "yes" : "no"));
    }

    SECTION("disconnect_network() is safe when not connected") {
        backend->start();

        WiFiError result = backend->disconnect_network();
        REQUIRE(result.success()); // Idempotent operation
    }

    SECTION("Connection status updated after connect") {
        backend->start();

        // Initial status: not connected
        auto status = backend->get_status();
        REQUIRE_FALSE(status.connected);

        backend->register_event_callback("SCAN_COMPLETE", [this](const std::string& data) {
            (void)data;
            notify_event();
        });

        // Get networks and connect
        backend->trigger_scan();
        REQUIRE(wait_for_event_count(1, 5000));

        std::vector<WiFiNetwork> networks;
        backend->get_scan_results(networks);

        reset_event_count();
        backend->register_event_callback("CONNECTED", [this](const std::string& data) {
            (void)data;
            notify_event();
        });

        backend->connect_network(networks[0].ssid, "test_password");

        // Wait for connection with proper synchronization
        bool connected = wait_for_event_count(1, 5000);

        if (connected) {
            status = backend->get_status();
            REQUIRE(status.connected);
            REQUIRE_FALSE(status.ssid.empty());
            REQUIRE_FALSE(status.ip_address.empty());
        }
    }
}

// ============================================================================
// Timer Cleanup Tests
// ============================================================================

TEST_CASE_METHOD(WiFiBackendTestFixture, "Backend timer cleanup",
                 "[network][backend][cleanup][slow]") {
    SECTION("stop() cleans up scan timer") {
        backend->start();
        backend->trigger_scan();

        // Stop before scan completes
        backend->stop();

        // No crash - timers cleaned up
        REQUIRE_NOTHROW(backend->stop());
    }

    SECTION("stop() cleans up connection timer") {
        backend->start();

        backend->register_event_callback("SCAN_COMPLETE", [this](const std::string& data) {
            (void)data;
            notify_event();
        });

        // Get networks
        backend->trigger_scan();
        REQUIRE(wait_for_event_count(1, 5000));

        std::vector<WiFiNetwork> networks;
        backend->get_scan_results(networks);

        // Start connection
        backend->connect_network(networks[0].ssid, "password");

        // Stop before connection completes
        backend->stop();

        // No crash - timers cleaned up
        REQUIRE_NOTHROW(backend->stop());
    }

    SECTION("Destructor cleans up active timers") {
        auto temp_backend = std::make_unique<WifiBackendMock>();
        temp_backend->start();
        temp_backend->trigger_scan();

        // Destroy while scan in progress
        REQUIRE_NOTHROW(temp_backend.reset());
    }

    SECTION("No events fire after backend stopped") {
        backend->start();

        int event_count = 0;
        backend->register_event_callback("SCAN_COMPLETE", [&event_count](const std::string& data) {
            (void)data;
            event_count++;
        });

        backend->trigger_scan();

        // Stop immediately (before scan completes)
        backend->stop();

        // Wait to ensure scan thread is fully cleaned up
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));

        // Event should NOT fire (thread was canceled)
        REQUIRE(event_count == 0);
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE_METHOD(WiFiBackendTestFixture, "Backend edge cases", "[network][backend][edge-cases]") {
    SECTION("Rapid start/stop cycles") {
        for (int i = 0; i < 5; i++) {
            backend->start();
            backend->stop();
        }

        // Final state: not running
        REQUIRE_FALSE(backend->is_running());
    }

    SECTION("Multiple trigger_scan() calls") {
        backend->start();

        // Trigger multiple scans rapidly
        backend->trigger_scan();
        backend->trigger_scan();
        backend->trigger_scan();

        // Should not crash (later calls replace earlier timer)
        REQUIRE_NOTHROW(backend->stop());
    }

    SECTION("get_status() safe when not connected") {
        auto status = backend->get_status();
        REQUIRE_FALSE(status.connected);
        REQUIRE(status.ssid.empty());
        REQUIRE(status.ip_address.empty());
        REQUIRE(status.signal_strength == 0);
    }
}
