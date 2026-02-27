// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_discovery_klippy_gate.cpp
 * @brief Tests for Klippy-readiness gate in the discovery sequence
 *
 * Verifies that the real MoonrakerDiscoverySequence checks klippy_state
 * via server.info BEFORE calling printer.objects.list, and aborts
 * discovery when Klippy is not ready (STARTUP/ERROR states).
 */

#include "../lvgl_test_fixture.h"
#include "moonraker_client_mock.h"

#include <atomic>
#include <string>

#include "../catch_amalgamated.hpp"

/**
 * @brief Test subclass that exposes the base class discover_printer()
 *
 * MoonrakerClientMock overrides discover_printer() with mock logic.
 * This subclass provides access to the REAL discovery sequence
 * (MoonrakerClient::discover_printer → discovery_.start()) while
 * still routing send_jsonrpc() through the mock handler dispatch.
 */
class TestDiscoveryClient : public MoonrakerClientMock {
  public:
    using MoonrakerClientMock::MoonrakerClientMock;

    /**
     * @brief Run the REAL discovery sequence (not the mock override)
     *
     * Calls MoonrakerClient::discover_printer which calls discovery_.start(),
     * exercising the real discovery sequence code. send_jsonrpc() calls
     * within the sequence are dispatched through the mock handler registry.
     */
    void discover_printer_real(std::function<void()> on_complete,
                               std::function<void(const std::string&)> on_error) {
        MoonrakerClient::discover_printer(on_complete, on_error);
    }
};

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("Discovery succeeds when Klippy is ready", "[discovery][klippy_gate]") {
    LVGLTestFixture fixture;

    TestDiscoveryClient client;
    client.set_klippy_state(MoonrakerClientMock::KlippyState::READY);

    bool completed = false;
    bool errored = false;
    std::string error_reason;

    client.discover_printer_real([&completed]() { completed = true; },
                                 [&errored, &error_reason](const std::string& reason) {
                                     errored = true;
                                     error_reason = reason;
                                 });

    REQUIRE(completed);
    REQUIRE_FALSE(errored);
}

TEST_CASE("Discovery aborts when Klippy in STARTUP state", "[discovery][klippy_gate]") {
    LVGLTestFixture fixture;

    TestDiscoveryClient client;
    client.set_klippy_state(MoonrakerClientMock::KlippyState::STARTUP);

    bool completed = false;
    bool errored = false;
    std::string error_reason;

    client.discover_printer_real([&completed]() { completed = true; },
                                 [&errored, &error_reason](const std::string& reason) {
                                     errored = true;
                                     error_reason = reason;
                                 });

    REQUIRE_FALSE(completed);
    REQUIRE(errored);
    REQUIRE(error_reason.find("startup") != std::string::npos);
}

TEST_CASE("Discovery aborts when Klippy in ERROR state", "[discovery][klippy_gate]") {
    LVGLTestFixture fixture;

    TestDiscoveryClient client;
    client.set_klippy_state(MoonrakerClientMock::KlippyState::ERROR);

    bool completed = false;
    bool errored = false;
    std::string error_reason;

    client.discover_printer_real([&completed]() { completed = true; },
                                 [&errored, &error_reason](const std::string& reason) {
                                     errored = true;
                                     error_reason = reason;
                                 });

    REQUIRE_FALSE(completed);
    REQUIRE(errored);
    REQUIRE(error_reason.find("error") != std::string::npos);
}

TEST_CASE("Discovery succeeds when Klippy in SHUTDOWN state", "[discovery][klippy_gate]") {
    LVGLTestFixture fixture;

    TestDiscoveryClient client;
    client.set_klippy_state(MoonrakerClientMock::KlippyState::SHUTDOWN);

    bool completed = false;
    bool errored = false;
    std::string error_reason;

    client.discover_printer_real([&completed]() { completed = true; },
                                 [&errored, &error_reason](const std::string& reason) {
                                     errored = true;
                                     error_reason = reason;
                                 });

    REQUIRE(completed);
    REQUIRE_FALSE(errored);
}

TEST_CASE("Discovery does not call printer.objects.list when Klippy not ready",
          "[discovery][klippy_gate]") {
    LVGLTestFixture fixture;

    TestDiscoveryClient client;
    client.set_klippy_state(MoonrakerClientMock::KlippyState::STARTUP);

    // Track whether printer.objects.list was called by counting send_jsonrpc calls
    // The mock handler dispatch already tracks this implicitly - if Klippy is STARTUP
    // and the gate works, printer.objects.list should never be called.
    // We verify this by checking that the error callback fires with a Klippy-related
    // message (not a "Method not found" from printer.objects.list failing).
    bool errored = false;
    std::string error_reason;

    client.discover_printer_real(
        []() { FAIL("Discovery should not succeed when Klippy is in STARTUP"); },
        [&errored, &error_reason](const std::string& reason) {
            errored = true;
            error_reason = reason;
        });

    REQUIRE(errored);
    // The error should mention "Klippy not ready" (from the gate),
    // NOT "Method not found" (from printer.objects.list failing)
    REQUIRE(error_reason.find("Klippy not ready") != std::string::npos);
    REQUIRE(error_reason.find("Method not found") == std::string::npos);
}
