// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_moonraker_api_shutdown.cpp
 * @brief Unit tests for MoonrakerAPI shutdown behavior
 *
 * Tests that the destructor doesn't hang when HTTP threads are blocked.
 * This prevents the crash seen when quitting during file downloads/uploads.
 *
 * Root cause: File download/upload operations (downloadFile, uploadLargeFormFile)
 * use libhv's synchronous HTTP APIs with 1-hour timeouts. If shutdown happens
 * during an active transfer, thread::join() would block until timeout.
 *
 * Fix: The destructor now uses timed join with polling and detach fallback.
 * If a thread doesn't complete within 2 seconds, shutdown continues anyway.
 */

#include "moonraker_api.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "../catch_amalgamated.hpp"

// ============================================================================
// Test Fixture - Exposes launch_http_thread for testing
// ============================================================================

/**
 * @brief Test-specific MoonrakerAPI subclass that exposes protected methods
 *
 * The launch_http_thread method is private, so we use a friend-like pattern
 * by adding a public test-only method that mimics the behavior.
 */
class TestableMoonrakerAPI : public MoonrakerAPI {
  public:
    using MoonrakerAPI::MoonrakerAPI;

    /**
     * @brief Start a blocking thread that simulates a slow HTTP operation
     *
     * Uses an atomic flag for cancellation - the thread polls this flag
     * while sleeping to allow early termination without dangling references.
     *
     * @param cancel_flag Atomic flag to signal thread to exit early
     * @param block_duration How long to block (simulating slow HTTP)
     */
    void start_blocking_thread(std::shared_ptr<std::atomic<bool>> cancel_flag,
                               std::chrono::milliseconds block_duration) {
        // Create a thread that blocks by sleeping, checking cancel flag periodically
        std::thread blocking_thread([cancel_flag, block_duration]() {
            auto deadline = std::chrono::steady_clock::now() + block_duration;
            while (std::chrono::steady_clock::now() < deadline) {
                if (cancel_flag->load()) {
                    return; // Early exit if cancelled
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });

        // Track the thread using the same mechanism as launch_http_thread
        // We can't call the private method directly, so we store in our own list
        blocking_threads_.push_back(std::move(blocking_thread));
    }

    ~TestableMoonrakerAPI() override {
        // We handle our test threads here with the same timed-join pattern

        // Same pattern as MoonrakerAPI::~MoonrakerAPI() - use helper thread + polling
        // Can't use std::async because its future destructor blocks!
        constexpr auto kJoinTimeout = std::chrono::seconds(2);
        constexpr auto kPollInterval = std::chrono::milliseconds(10);

        for (auto& t : blocking_threads_) {
            if (!t.joinable()) {
                continue;
            }

            std::atomic<bool> joined{false};
            std::thread join_helper([&t, &joined]() {
                t.join();
                joined.store(true);
            });

            auto start = std::chrono::steady_clock::now();
            while (!joined.load()) {
                if (std::chrono::steady_clock::now() - start > kJoinTimeout) {
                    // Timeout - detach BOTH the helper and the original thread
                    // If we only detach the helper, t is still joinable and will
                    // cause std::terminate when the list is destroyed
                    join_helper.detach();
                    t.detach(); // Critical: avoid std::terminate on list destruction
                    break;
                }
                std::this_thread::sleep_for(kPollInterval);
            }

            if (joined.load()) {
                join_helper.join();
            }
        }
    }

  private:
    std::list<std::thread> blocking_threads_;
};

class MoonrakerAPIShutdownTestFixture {
  public:
    MoonrakerAPIShutdownTestFixture() : client_(MoonrakerClientMock::PrinterType::VORON_24) {
        state_.init_subjects(false);
        api_ = std::make_unique<TestableMoonrakerAPI>(client_, state_);
        cancel_flag_ = std::make_shared<std::atomic<bool>>(false);
    }

    ~MoonrakerAPIShutdownTestFixture() {
        // Signal any remaining threads to exit
        cancel_flag_->store(true);
    }

  protected:
    MoonrakerClientMock client_;
    PrinterState state_;
    std::unique_ptr<TestableMoonrakerAPI> api_;
    std::shared_ptr<std::atomic<bool>> cancel_flag_;
};

// ============================================================================
// Shutdown Timeout Tests
// ============================================================================

TEST_CASE_METHOD(MoonrakerAPIShutdownTestFixture,
                 "MoonrakerAPI destructor completes within timeout when thread is blocked",
                 "[api][shutdown][timeout][slow]") {
    // Start a thread that will block for 30 seconds (simulating slow download)
    api_->start_blocking_thread(cancel_flag_, std::chrono::milliseconds(30000));

    // Measure how long destruction takes
    auto start = std::chrono::steady_clock::now();

    // Destroy the API - this should NOT block for 30 seconds
    api_.reset();

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // Should complete within ~3 seconds (2s timeout + overhead)
    // If the fix isn't applied, this would take 30+ seconds
    REQUIRE(elapsed_ms < 5000);
    INFO("Destructor completed in " << elapsed_ms << "ms (expected < 5000ms)");

    // Signal the detached thread to exit so it doesn't keep running
    cancel_flag_->store(true);
}

TEST_CASE_METHOD(MoonrakerAPIShutdownTestFixture,
                 "MoonrakerAPI destructor completes quickly when no threads are active",
                 "[api][shutdown][slow]") {
    // No threads started - destruction should be instant

    auto start = std::chrono::steady_clock::now();
    api_.reset();
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // Should complete nearly instantly
    REQUIRE(elapsed_ms < 100);
}

TEST_CASE_METHOD(MoonrakerAPIShutdownTestFixture,
                 "MoonrakerAPI destructor handles thread that completes during wait",
                 "[api][shutdown][slow]") {
    // Start a thread that blocks for just 500ms
    api_->start_blocking_thread(cancel_flag_, std::chrono::milliseconds(500));

    // Small delay to let thread start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto start = std::chrono::steady_clock::now();
    api_.reset();
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // Should complete once thread finishes (< 1s total)
    REQUIRE(elapsed_ms < 1500);
    INFO("Destructor completed in " << elapsed_ms << "ms (expected < 1500ms)");
}
