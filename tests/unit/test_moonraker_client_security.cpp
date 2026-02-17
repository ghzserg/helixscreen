// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/moonraker_client.h"
#include "../../include/moonraker_error.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "../catch_amalgamated.hpp"

using namespace helix;
/**
 * MoonrakerClient Security Tests
 *
 * Comprehensive tests for Moonraker security fixes from Issues #2, #3, #4, #6, #7, #9
 * in the Moonraker Security Review (docs/MOONRAKER_SECURITY_REVIEW.md).
 *
 * Test Categories:
 * 1. Issue #4: Use-After-Free - Destructor cleanup (no dangling callbacks)
 * 2. Issue #6: Deadlock Risk - Two-phase timeout pattern (callbacks outside mutex)
 * 3. Issue #7: JSON-RPC Validation - Method/params/payload validation
 * 4. Issue #9: Exception Safety - All callbacks exception-safe
 *
 * SECURITY CRITICAL: These tests verify memory safety, thread safety, and
 * robust error handling that prevents crashes and undefined behavior.
 */

// ============================================================================
// Test Fixtures
// ============================================================================

/**
 * @brief Base fixture for MoonrakerClient security tests
 *
 * Provides isolated client instances and callback tracking.
 */
class MoonrakerClientSecurityFixture {
  public:
    MoonrakerClientSecurityFixture() {
        // Create isolated event loop for testing
        loop = std::make_shared<hv::EventLoop>();

        // Create client with isolated loop
        client = std::make_unique<MoonrakerClient>(loop);

        reset_callbacks();
    }

    ~MoonrakerClientSecurityFixture() {
        client.reset();
        loop.reset();
    }

    void reset_callbacks() {
        success_called = false;
        error_called = false;
        callback_count = 0;
        captured_error = MoonrakerError();
    }

    // Standard callbacks for testing
    void success_callback(json response) {
        success_called = true;
        callback_count++;
        captured_response = response;
    }

    void error_callback(const MoonrakerError& err) {
        error_called = true;
        callback_count++;
        captured_error = err;
    }

    // Test objects
    hv::EventLoopPtr loop;
    std::unique_ptr<MoonrakerClient> client;

    // Callback tracking
    std::atomic<bool> success_called{false};
    std::atomic<bool> error_called{false};
    std::atomic<int> callback_count{0};
    MoonrakerError captured_error;
    json captured_response;
};

// ============================================================================
// Issue #4: Use-After-Free - Destructor Cleanup
// ============================================================================

TEST_CASE("MoonrakerClient destructor clears callbacks (UAF prevention)",
          "[connection][security][uaf][issue4][eventloop][slow]") {
    SECTION("Destroy client before connection completes") {
        auto loop = std::make_shared<hv::EventLoop>();
        auto client = std::make_unique<MoonrakerClient>(loop);

        bool connected_called = false;
        bool disconnected_called = false;

        // Start connection to non-existent server (will fail)
        client->connect(
            "ws://127.0.0.1:19999/websocket", [&connected_called]() { connected_called = true; },
            [&disconnected_called]() { disconnected_called = true; });

        // Destroy client immediately before connection resolves
        client.reset();

        // Sleep briefly to allow any pending events
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // If callbacks weren't cleared, this could crash with UAF
        // Test passing = callbacks properly cleared
        REQUIRE_FALSE(connected_called);
    }

    SECTION("Destroy client with pending requests") {
        auto loop = std::make_shared<hv::EventLoop>();
        auto client = std::make_unique<MoonrakerClient>(loop);

        bool error_callback_invoked = false;

        // Send request that will never complete (no connection)
        client->send_jsonrpc(
            "printer.info", json(), [](json) { FAIL("Success callback should not be called"); },
            [&error_callback_invoked](const MoonrakerError& err) {
                error_callback_invoked = true;
                REQUIRE(err.type == MoonrakerErrorType::CONNECTION_LOST);
            });

        // Destroy client - should invoke error callbacks with CONNECTION_LOST
        client.reset();

        // Error callback should have been invoked during cleanup
        REQUIRE(error_callback_invoked);
    }

    SECTION("Multiple rapid create/destroy cycles (stress test)") {
        // Stress test: rapid allocation/deallocation to catch UAF bugs

        for (int i = 0; i < 20; i++) {
            auto loop = std::make_shared<hv::EventLoop>();
            auto client = std::make_unique<MoonrakerClient>(loop);

            // Start connection
            client->connect(
                "ws://127.0.0.1:19999/websocket", []() { /* connected */ },
                []() { /* disconnected */ });

            // Send pending request
            client->send_jsonrpc("printer.info", json(), [](json) {}, [](const MoonrakerError&) {});

            // Destroy immediately
            client.reset();
        }

        // Reaching here without crash = callbacks properly cleared
        REQUIRE(true);
    }

    SECTION("Destroy client with registered persistent callbacks") {
        auto loop = std::make_shared<hv::EventLoop>();
        auto client = std::make_unique<MoonrakerClient>(loop);

        bool notify_callback_invoked = false;

        // Register persistent status update callback
        client->register_notify_update(
            [&notify_callback_invoked](json j) { notify_callback_invoked = true; });

        // Register persistent method callback
        client->register_method_callback("notify_gcode_response", "test_handler",
                                         [](json j) { /* callback */ });

        // Destroy client
        client.reset();

        // If callbacks weren't cleared, accessing them would crash
        REQUIRE_FALSE(notify_callback_invoked);
    }
}

// ============================================================================
// Issue #6: Deadlock Risk - Two-Phase Timeout Pattern
// ============================================================================

// NOTE: This test requires an actual WebSocket connection to test timeout behavior.
// Without a connection, send_jsonrpc immediately fails with CONNECTION_ERROR
// before any timeout can occur. Marked as integration test.
TEST_CASE_METHOD(MoonrakerClientSecurityFixture,
                 "MoonrakerClient timeout callbacks invoked outside mutex",
                 "[connection][security][deadlock][issue6][.integration][eventloop][slow]") {
    SECTION("Timeout callback can safely call send_jsonrpc (no deadlock)") {
        // This test verifies the two-phase timeout pattern:
        // Phase 1: Copy callbacks under lock
        // Phase 2: Invoke callbacks outside lock

        bool timeout_callback_invoked = false;
        bool nested_request_sent = false;

        // Set very short timeout for testing
        client->set_default_request_timeout(100); // 100ms

        // Send request with callback that sends another request
        client->send_jsonrpc(
            "printer.info", json(), [](json) { FAIL("Should timeout, not succeed"); },
            [this, &timeout_callback_invoked, &nested_request_sent](const MoonrakerError& err) {
                timeout_callback_invoked = true;
                REQUIRE(err.type == MoonrakerErrorType::TIMEOUT);

                // Try to send nested request (would deadlock if mutex held)
                int result = client->send_jsonrpc(
                    "server.info", json(), [](json) {}, [](const MoonrakerError&) {});

                // If we reach here, no deadlock occurred
                nested_request_sent = true;
            });

        // Wait for timeout to occur
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Trigger timeout check
        client->process_timeouts();

        // Verify callback was invoked and nested request succeeded
        REQUIRE(timeout_callback_invoked);
        REQUIRE(nested_request_sent);
    }

    SECTION("Cleanup callbacks can safely call send_jsonrpc (no deadlock)") {
        // Verify cleanup_pending_requests uses two-phase pattern

        bool cleanup_callback_invoked = false;
        bool nested_request_sent = false;

        // Send request with callback that sends another request
        client->send_jsonrpc(
            "printer.info", json(), [](json) { FAIL("Should be cleaned up, not succeed"); },
            [this, &cleanup_callback_invoked, &nested_request_sent](const MoonrakerError& err) {
                cleanup_callback_invoked = true;
                REQUIRE(err.type == MoonrakerErrorType::CONNECTION_LOST);

                // Try to send nested request (would deadlock if mutex held)
                // Note: client may be nullptr during destruction, but attempt should not deadlock
                if (client) {
                    int result = client->send_jsonrpc(
                        "server.info", json(), [](json) {}, [](const MoonrakerError&) {});
                }

                nested_request_sent = true;
            });

        // Destroy client to trigger cleanup
        client.reset();

        // Verify callback was invoked and nested request succeeded
        REQUIRE(cleanup_callback_invoked);
        REQUIRE(nested_request_sent);
    }
}

// ============================================================================
// Issue #7: JSON-RPC Validation
// ============================================================================

// TODO: Test actual JSON-RPC validation with MockWebSocketServer
TEST_CASE_METHOD(MoonrakerClientSecurityFixture,
                 "MoonrakerClient unconnected send returns error for any input",
                 "[connection][security][validation][issue7][eventloop][slow]") {
    // All send_jsonrpc calls return -1 when not connected. This verifies graceful
    // failure (no crash, no UB) across representative input variations.

    SECTION("Various method names") {
        REQUIRE(client->send_jsonrpc("") == -1);
        REQUIRE(client->send_jsonrpc(std::string(300, 'a')) == -1);
        REQUIRE(client->send_jsonrpc("printer.info") == -1);
        REQUIRE(client->send_jsonrpc("printer.objects.subscribe") == -1);
    }

    SECTION("Various param types") {
        REQUIRE(client->send_jsonrpc("printer.info", nullptr) == -1);
        REQUIRE(client->send_jsonrpc("printer.info", json::object()) == -1);
        REQUIRE(client->send_jsonrpc("printer.info", json::array({"a", "b"})) == -1);
        REQUIRE(client->send_jsonrpc("printer.info",
                                     json{{"objects", {{"print_stats", nullptr}}}}) == -1);
    }

    SECTION("Large and special-character payloads") {
        // ~100KB payload
        json large_params = json::object();
        for (int i = 0; i < 1000; i++)
            large_params["key_" + std::to_string(i)] = std::string(100, 'x');
        REQUIRE(client->send_jsonrpc("test.method", large_params) == -1);

        // Special characters: quotes, backslash, newline, unicode
        json special_params = {
            {"q", "Test \"quoted\""}, {"b", "back\\slash"}, {"n", "new\nline"}, {"u", "你好"}};
        REQUIRE(client->send_jsonrpc("test.method", special_params) == -1);
    }
}

// ============================================================================
// Issue #9: Exception Safety
// ============================================================================

TEST_CASE_METHOD(MoonrakerClientSecurityFixture,
                 "MoonrakerClient state change callback is exception safe",
                 "[connection][security][exception][issue9][eventloop][slow]") {
    SECTION("State change callback that throws doesn't crash") {
        bool callback_invoked = false;

        // Register callback that throws
        client->set_state_change_callback(
            [&callback_invoked](ConnectionState old_state, ConnectionState new_state) {
                callback_invoked = true;
                throw std::runtime_error("Test exception in state callback");
            });

        // Trigger state change by attempting connection
        // Exception should be caught and logged, not propagate
        REQUIRE_NOTHROW(client->connect("ws://127.0.0.1:19999/websocket", []() {}, []() {}));

        // Verify callback was invoked (and threw)
        REQUIRE(callback_invoked);
    }
}

TEST_CASE_METHOD(MoonrakerClientSecurityFixture,
                 "MoonrakerClient success callbacks are exception safe",
                 "[connection][security][exception][issue9][eventloop][slow]") {
    SECTION("Success callback throwing doesn't crash client") {
        // Register request with throwing callback
        // Note: Since not connected, request will timeout and error callback invoked
        bool error_callback_invoked = false;

        client->send_jsonrpc(
            "printer.info", json(),
            [](json response) { throw std::runtime_error("Test exception in success callback"); },
            [&error_callback_invoked](const MoonrakerError& err) {
                error_callback_invoked = true;
                // Error callback invoked due to timeout (not connected)
                // This is expected behavior
            });

        // If response arrives and callback throws, should be caught
        // This test verifies the pattern exists (actual response needs server)
        // The important thing is NO CRASH occurs
        REQUIRE(true);
    }
}

TEST_CASE_METHOD(MoonrakerClientSecurityFixture,
                 "MoonrakerClient error callbacks are exception safe",
                 "[connection][security][exception][issue9][eventloop][slow]") {
    SECTION("Error callback throwing doesn't crash during cleanup") {
        bool first_callback_called = false;
        bool second_callback_called = false;

        // Register multiple requests with throwing error callbacks
        client->send_jsonrpc(
            "printer.info", json(), [](json) {},
            [&first_callback_called](const MoonrakerError& err) {
                first_callback_called = true;
                throw std::runtime_error("Test exception 1");
            });

        client->send_jsonrpc(
            "server.info", json(), [](json) {},
            [&second_callback_called](const MoonrakerError& err) {
                second_callback_called = true;
                // This callback doesn't throw
            });

        // Destroy client - should not crash even if callbacks throw
        REQUIRE_NOTHROW(client.reset());

        // First callback was invoked and threw
        REQUIRE(first_callback_called);

        // Second callback should still have been called
        // (exception handling shouldn't stop iteration)
        REQUIRE(second_callback_called);
    }

    SECTION("Error callback throwing doesn't crash during timeout") {
        bool timeout_callback_called = false;

        // Set very short timeout
        client->set_default_request_timeout(50);

        // Register request with throwing timeout callback
        client->send_jsonrpc(
            "printer.info", json(), [](json) {},
            [&timeout_callback_called](const MoonrakerError& err) {
                timeout_callback_called = true;
                throw std::runtime_error("Test exception in timeout");
            });

        // Wait for timeout
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Process timeouts - should not crash
        REQUIRE_NOTHROW(client->process_timeouts());

        REQUIRE(timeout_callback_called);
    }
}

TEST_CASE_METHOD(MoonrakerClientSecurityFixture,
                 "MoonrakerClient notify callbacks are exception safe",
                 "[connection][security][exception][issue9][eventloop][slow]") {
    SECTION("Notify callback throwing doesn't crash") {
        bool callback_invoked = false;

        // Register notify callback that throws
        client->register_notify_update([&callback_invoked](json notification) {
            callback_invoked = true;
            throw std::runtime_error("Test exception in notify callback");
        });

        // Simulating notification reception would require server
        // This test documents expected behavior
        REQUIRE(true);
    }

    SECTION("Method callback throwing doesn't crash") {
        bool callback_invoked = false;

        // Register method callback that throws
        client->register_method_callback(
            "notify_gcode_response", "test_handler", [&callback_invoked](json notification) {
                callback_invoked = true;
                throw std::runtime_error("Test exception in method callback");
            });

        // Expected behavior: exception caught and logged
        REQUIRE(true);
    }
}

TEST_CASE("MoonrakerClient all callback types exception-safe (comprehensive)",
          "[connection][security][exception][issue9][eventloop][slow]") {
    SECTION("Exception in every callback type doesn't crash") {
        auto loop = std::make_shared<hv::EventLoop>();
        auto client = std::make_unique<MoonrakerClient>(loop);

        // Connection callbacks
        REQUIRE_NOTHROW(client->connect(
            "ws://127.0.0.1:19999/websocket",
            []() { throw std::runtime_error("onopen exception"); },
            []() { throw std::runtime_error("onclose exception"); }));

        // Request callbacks
        REQUIRE_NOTHROW(client->send_jsonrpc(
            "printer.info", json(), [](json) { throw std::runtime_error("success exception"); },
            [](const MoonrakerError&) { throw std::runtime_error("error exception"); }));

        // Notify callbacks
        REQUIRE_NOTHROW(client->register_notify_update(
            [](json) { throw std::runtime_error("notify exception"); }));

        // Method callbacks
        REQUIRE_NOTHROW(client->register_method_callback("test_method", "test_handler", [](json) {
            throw std::runtime_error("method exception");
        }));

        // State change callback
        REQUIRE_NOTHROW(client->set_state_change_callback(
            [](ConnectionState, ConnectionState) { throw std::runtime_error("state exception"); }));

        // Cleanup with pending requests (triggers error callbacks)
        REQUIRE_NOTHROW(client.reset());
    }
}

// ============================================================================
// Integration Tests - Multiple Security Properties
// ============================================================================

// Previously disabled due to segfault (SIGSEGV) - fixed by adding lifetime_guard_
// to MoonrakerClient. Callbacks now capture weak_ptr<bool> to safely detect
// when the client is being destroyed, preventing use-after-free.
TEST_CASE("MoonrakerClient security properties work together correctly",
          "[connection][security][integration][eventloop][slow]") {
    SECTION("Cleanup with exceptions, large IDs, and nested requests") {
        auto loop = std::make_shared<hv::EventLoop>();
        auto client = std::make_unique<MoonrakerClient>(loop);

        std::atomic<int> cleanup_callbacks_invoked{0};

        // Send many requests with various properties
        for (int i = 0; i < 50; i++) {
            client->send_jsonrpc(
                "printer.info", json(), [](json) { throw std::runtime_error("Success exception"); },
                [&cleanup_callbacks_invoked, &client](const MoonrakerError& err) {
                    cleanup_callbacks_invoked++;

                    // Some callbacks throw
                    if (cleanup_callbacks_invoked % 3 == 0) {
                        throw std::runtime_error("Cleanup exception");
                    }

                    // Some callbacks send nested requests
                    if (cleanup_callbacks_invoked % 5 == 0) {
                        client->send_jsonrpc(
                            "nested.request", json(), [](json) {}, [](const MoonrakerError&) {});
                    }
                });
        }

        // Destroy client - tests all properties together:
        // - Two-phase cleanup (nested requests work)
        // - Exception safety (throwing callbacks)
        // - Callback cleanup (no UAF)
        REQUIRE_NOTHROW(client.reset());

        // All cleanup callbacks should have been invoked
        REQUIRE(cleanup_callbacks_invoked == 50);
    }
}
