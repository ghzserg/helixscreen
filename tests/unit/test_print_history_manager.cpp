// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_print_history_manager.cpp
 * @brief Unit tests for PrintHistoryManager (TDD)
 *
 * Tests the centralized print history cache that provides:
 * - Raw jobs list for HistoryDashboardPanel/HistoryListPanel
 * - Aggregated filename stats for PrintSelectPanel status indicators
 * - Observer notification when data changes
 */

#include "../../include/moonraker_api.h"
#include "../../include/moonraker_client_mock.h"
#include "../../include/print_history_data.h"
#include "../../include/print_history_manager.h"
#include "../../include/printer_state.h"
#include "../../include/ui_update_queue.h"
#include "../../lvgl/lvgl.h"
#include "../test_helpers/update_queue_test_access.h"
#include "../ui_test_utils.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "../catch_amalgamated.hpp"

// ============================================================================
// Global LVGL Initialization
// ============================================================================

namespace {
struct LVGLInitializerHistoryManager {
    LVGLInitializerHistoryManager() {
        static bool initialized = false;
        if (!initialized) {
            lv_init_safe();
            lv_display_t* disp = lv_display_create(800, 480);
            alignas(64) static lv_color_t buf[800 * 10];
            lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
            initialized = true;
        }
    }
};

static LVGLInitializerHistoryManager lvgl_init;
} // namespace

// ============================================================================
// Test Fixture
// ============================================================================

class HistoryManagerTestFixture {
    static bool queue_initialized;

  public:
    HistoryManagerTestFixture() : client_(MoonrakerClientMock::PrinterType::VORON_24, 1000.0) {
        // Initialize update queue once (static guard) - CRITICAL for ui_queue_update()
        if (!queue_initialized) {
            ui_update_queue_init();
            queue_initialized = true;
        }

        printer_state_.init_subjects(false);
        client_.connect("ws://mock/websocket", []() {}, []() {});
        api_ = std::make_unique<MoonrakerAPI>(client_, printer_state_);
        manager_ = std::make_unique<PrintHistoryManager>(api_.get(), &client_);
    }

    ~HistoryManagerTestFixture() {
        // Destroy managed objects first
        manager_.reset();
        api_.reset();
        client_.disconnect();

        // Drain pending callbacks
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Shutdown queue
        ui_update_queue_shutdown();

        // Reset static flag for next test
        queue_initialized = false;
    }

  protected:
    /// Wait for async fetch to complete
    bool wait_for_loaded(int timeout_ms = 500) {
        for (int i = 0; i < timeout_ms / 10; ++i) {
            // Drain the update queue to process callbacks scheduled via ui_queue_update
            UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

            if (manager_->is_loaded()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    MoonrakerClientMock client_;
    PrinterState printer_state_;
    std::unique_ptr<MoonrakerAPI> api_;
    std::unique_ptr<PrintHistoryManager> manager_;
};
bool HistoryManagerTestFixture::queue_initialized = false;

// ============================================================================
// Basic Functionality Tests
// ============================================================================

TEST_CASE_METHOD(HistoryManagerTestFixture, "PrintHistoryManager starts unloaded",
                 "[history_manager]") {
    REQUIRE_FALSE(manager_->is_loaded());
    REQUIRE(manager_->get_jobs().empty());
    REQUIRE(manager_->get_filename_stats().empty());
}

TEST_CASE_METHOD(HistoryManagerTestFixture, "PrintHistoryManager fetches history from API",
                 "[history_manager]") {
    // When: fetch is called
    manager_->fetch();

    // Then: wait for async completion
    REQUIRE(wait_for_loaded());

    // And: jobs are populated
    REQUIRE_FALSE(manager_->get_jobs().empty());
    REQUIRE(manager_->is_loaded());
}

TEST_CASE_METHOD(HistoryManagerTestFixture, "PrintHistoryManager builds filename stats map",
                 "[history_manager]") {
    // When: fetch completes
    manager_->fetch();
    REQUIRE(wait_for_loaded());

    // Then: filename stats map is populated
    const auto& stats = manager_->get_filename_stats();
    REQUIRE_FALSE(stats.empty());

    // And: each entry has valid data
    for (const auto& [filename, info] : stats) {
        REQUIRE_FALSE(filename.empty());
        // At least one count should be non-zero (success or failure)
        bool has_history = (info.success_count > 0 || info.failure_count > 0);
        REQUIRE(has_history);
    }
}

// ============================================================================
// Aggregation Tests
// ============================================================================

TEST_CASE_METHOD(HistoryManagerTestFixture,
                 "PrintHistoryManager aggregates success count correctly", "[history_manager]") {
    // When: fetch completes
    manager_->fetch();
    REQUIRE(wait_for_loaded());

    // Then: check that COMPLETED jobs are counted as successes
    const auto& jobs = manager_->get_jobs();
    const auto& stats = manager_->get_filename_stats();

    // Count completed jobs manually for verification
    int total_completed = 0;
    for (const auto& job : jobs) {
        if (job.status == PrintJobStatus::COMPLETED) {
            total_completed++;
        }
    }

    // Sum up success counts from stats
    int total_success_in_stats = 0;
    for (const auto& [_, info] : stats) {
        total_success_in_stats += info.success_count;
    }

    REQUIRE(total_success_in_stats == total_completed);
}

TEST_CASE_METHOD(HistoryManagerTestFixture,
                 "PrintHistoryManager aggregates failure count correctly", "[history_manager]") {
    // When: fetch completes
    manager_->fetch();
    REQUIRE(wait_for_loaded());

    const auto& jobs = manager_->get_jobs();
    const auto& stats = manager_->get_filename_stats();

    // Count cancelled + error jobs manually
    int total_failures = 0;
    for (const auto& job : jobs) {
        if (job.status == PrintJobStatus::CANCELLED || job.status == PrintJobStatus::ERROR) {
            total_failures++;
        }
    }

    // Sum up failure counts from stats
    int total_failure_in_stats = 0;
    for (const auto& [_, info] : stats) {
        total_failure_in_stats += info.failure_count;
    }

    REQUIRE(total_failure_in_stats == total_failures);
}

TEST_CASE_METHOD(HistoryManagerTestFixture, "PrintHistoryManager tracks most recent job status",
                 "[history_manager]") {
    // When: fetch completes
    manager_->fetch();
    REQUIRE(wait_for_loaded());

    const auto& jobs = manager_->get_jobs();
    const auto& stats = manager_->get_filename_stats();

    // For each filename, verify the last_status matches the most recent job
    for (const auto& [filename, info] : stats) {
        // Find most recent job for this filename
        double most_recent_time = 0.0;
        PrintJobStatus most_recent_status = PrintJobStatus::UNKNOWN;

        for (const auto& job : jobs) {
            // Strip path from job filename for comparison
            std::string job_basename = job.filename;
            auto slash_pos = job_basename.rfind('/');
            if (slash_pos != std::string::npos) {
                job_basename = job_basename.substr(slash_pos + 1);
            }

            if (job_basename == filename && job.start_time > most_recent_time) {
                most_recent_time = job.start_time;
                most_recent_status = job.status;
            }
        }

        if (most_recent_time > 0.0) {
            REQUIRE(info.last_status == most_recent_status);
        }
    }
}

// ============================================================================
// Path Stripping Tests
// ============================================================================

TEST_CASE_METHOD(HistoryManagerTestFixture,
                 "PrintHistoryManager strips path from filename for aggregation",
                 "[history_manager]") {
    // When: fetch completes
    manager_->fetch();
    REQUIRE(wait_for_loaded());

    const auto& stats = manager_->get_filename_stats();

    // All keys should be basenames (no slashes)
    for (const auto& [filename, _] : stats) {
        REQUIRE(filename.find('/') == std::string::npos);
    }
}

// ============================================================================
// Observer Pattern Tests
// ============================================================================

TEST_CASE_METHOD(HistoryManagerTestFixture, "PrintHistoryManager notifies observers on fetch",
                 "[history_manager]") {
    std::atomic<int> callback_count{0};

    // Given: an observer is registered (store in variable, pass pointer)
    HistoryChangedCallback callback = [&callback_count]() { callback_count++; };
    manager_->add_observer(&callback);

    // When: fetch completes
    manager_->fetch();
    REQUIRE(wait_for_loaded());

    // Then: observer was notified
    REQUIRE(callback_count.load() >= 1);
}

TEST_CASE_METHOD(HistoryManagerTestFixture, "PrintHistoryManager supports multiple observers",
                 "[history_manager]") {
    std::atomic<int> callback1_count{0};
    std::atomic<int> callback2_count{0};

    // Given: multiple observers registered (store in variables, pass pointers)
    HistoryChangedCallback callback1 = [&callback1_count]() { callback1_count++; };
    HistoryChangedCallback callback2 = [&callback2_count]() { callback2_count++; };
    manager_->add_observer(&callback1);
    manager_->add_observer(&callback2);

    // When: fetch completes
    manager_->fetch();
    REQUIRE(wait_for_loaded());

    // Then: both observers were notified
    REQUIRE(callback1_count.load() >= 1);
    REQUIRE(callback2_count.load() >= 1);
}

// ============================================================================
// Cache Invalidation Tests
// ============================================================================

TEST_CASE_METHOD(HistoryManagerTestFixture, "PrintHistoryManager invalidate clears loaded state",
                 "[history_manager]") {
    // Given: manager has loaded data
    manager_->fetch();
    REQUIRE(wait_for_loaded());
    REQUIRE(manager_->is_loaded());

    // When: invalidate is called
    manager_->invalidate();

    // Then: loaded state is cleared
    REQUIRE_FALSE(manager_->is_loaded());
}

TEST_CASE_METHOD(HistoryManagerTestFixture, "PrintHistoryManager can re-fetch after invalidate",
                 "[history_manager]") {
    // Given: manager was loaded then invalidated
    manager_->fetch();
    REQUIRE(wait_for_loaded());
    manager_->invalidate();
    REQUIRE_FALSE(manager_->is_loaded());

    // When: fetch is called again
    manager_->fetch();

    // Then: data is reloaded
    REQUIRE(wait_for_loaded());
    REQUIRE_FALSE(manager_->get_jobs().empty());
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_CASE_METHOD(HistoryManagerTestFixture, "PrintHistoryManager handles concurrent fetch calls",
                 "[history_manager]") {
    std::atomic<int> callback_count{0};
    HistoryChangedCallback callback = [&callback_count]() { callback_count++; };
    manager_->add_observer(&callback);

    // When: multiple fetches are called rapidly
    manager_->fetch();
    manager_->fetch(); // Should be ignored while first is in progress
    manager_->fetch(); // Should be ignored while first is in progress

    REQUIRE(wait_for_loaded());

    // Then: only one fetch completes (subsequent ones ignored)
    REQUIRE(callback_count.load() == 1);
}

TEST_CASE_METHOD(HistoryManagerTestFixture, "PrintHistoryManager handles empty history",
                 "[history_manager]") {
    // Note: Mock returns 20 jobs by default, so this test verifies
    // that the manager handles the case gracefully
    manager_->fetch();
    REQUIRE(wait_for_loaded());

    // Stats should not crash with empty/null data
    const auto& stats = manager_->get_filename_stats();
    // Just verify we can access it without crash
    (void)stats.size();
}

// ============================================================================
// UUID/Size-Based Matching Tests
// ============================================================================
// These tests verify the UUID and file size fields that enable precise
// history matching (prevents false positives with same-named files).

TEST_CASE("PrintHistoryJob has uuid field", "[history][uuid]") {
    PrintHistoryJob job;
    job.uuid = "test-uuid-12345";
    REQUIRE(job.uuid == "test-uuid-12345");

    job.uuid = "";
    REQUIRE(job.uuid.empty());
}

TEST_CASE("PrintHistoryJob has size_bytes field", "[history][uuid]") {
    PrintHistoryJob job;
    job.size_bytes = 807487;
    REQUIRE(job.size_bytes == 807487);

    job.size_bytes = 0;
    REQUIRE(job.size_bytes == 0);
}

TEST_CASE("PrintHistoryStats has uuid field", "[history][uuid]") {
    PrintHistoryStats stats;
    stats.uuid = "stats-uuid-67890";
    REQUIRE(stats.uuid == "stats-uuid-67890");
}

TEST_CASE("PrintHistoryStats has size_bytes field", "[history][uuid]") {
    PrintHistoryStats stats;
    stats.size_bytes = 2178649;
    REQUIRE(stats.size_bytes == 2178649);
}

TEST_CASE_METHOD(HistoryManagerTestFixture, "UUID field is populated from history response",
                 "[history][uuid]") {
    manager_->fetch();
    REQUIRE(wait_for_loaded());

    const auto& jobs = manager_->get_jobs();
    REQUIRE_FALSE(jobs.empty());

    // At least one job should have uuid populated (mock returns uuid in metadata)
    bool found_uuid = false;
    for (const auto& job : jobs) {
        if (!job.uuid.empty()) {
            found_uuid = true;
            break;
        }
    }
    REQUIRE(found_uuid);
}

TEST_CASE_METHOD(HistoryManagerTestFixture, "size_bytes field is populated from history response",
                 "[history][uuid]") {
    manager_->fetch();
    REQUIRE(wait_for_loaded());

    const auto& jobs = manager_->get_jobs();
    REQUIRE_FALSE(jobs.empty());

    // At least one job should have size_bytes populated
    bool found_size = false;
    for (const auto& job : jobs) {
        if (job.size_bytes > 0) {
            found_size = true;
            break;
        }
    }
    REQUIRE(found_size);
}

TEST_CASE_METHOD(HistoryManagerTestFixture, "PrintHistoryStats includes uuid from most recent job",
                 "[history][uuid]") {
    manager_->fetch();
    REQUIRE(wait_for_loaded());

    const auto& stats = manager_->get_filename_stats();
    REQUIRE_FALSE(stats.empty());

    // Stats entries should include uuid from the most recent job
    bool found_stats_with_uuid = false;
    for (const auto& [filename, stat] : stats) {
        if (!stat.uuid.empty()) {
            found_stats_with_uuid = true;
            break;
        }
    }
    REQUIRE(found_stats_with_uuid);
}

TEST_CASE_METHOD(HistoryManagerTestFixture,
                 "PrintHistoryStats includes size_bytes from most recent job", "[history][uuid]") {
    manager_->fetch();
    REQUIRE(wait_for_loaded());

    const auto& stats = manager_->get_filename_stats();
    REQUIRE_FALSE(stats.empty());

    // Stats entries should include size from the most recent job
    bool found_stats_with_size = false;
    for (const auto& [filename, stat] : stats) {
        if (stat.size_bytes > 0) {
            found_stats_with_size = true;
            break;
        }
    }
    REQUIRE(found_stats_with_size);
}
