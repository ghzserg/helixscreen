// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_temperature_history_manager.cpp
 * @brief TDD unit tests for TemperatureHistoryManager
 *
 * Tests the temperature history collection manager that:
 * - Collects temperature samples from PrinterState subjects at app startup
 * - Stores 20 minutes of history (1200 samples @ 1Hz) per heater
 * - Supports multiple heaters (extruder, bed, chamber)
 * - Provides observer notifications when new samples arrive
 * - Thread-safe reads with mutex protection
 *
 * These tests define the expected behavior BEFORE implementation exists (TDD).
 */

// Include the real implementation header
#include "../../include/printer_state.h"
#include "../../include/temperature_history_manager.h"
#include "../../include/ui_update_queue.h"
#include "../../lvgl/lvgl.h"
#include "../test_helpers/update_queue_test_access.h"
#include "../ui_test_utils.h"

class TemperatureHistoryManagerTestAccess {
  public:
    static bool add_sample(TemperatureHistoryManager& m, const std::string& heater_name,
                           int temp_centi, int target_centi, int64_t timestamp_ms) {
        bool stored;
        {
            std::lock_guard<std::mutex> lock(m.mutex_);
            stored = m.add_sample_internal(heater_name, temp_centi, target_centi, timestamp_ms);
        }
        if (stored) {
            m.notify_observers(heater_name);
        }
        return stored;
    }
};

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../catch_amalgamated.hpp"

// ============================================================================
// Global LVGL Initialization
// ============================================================================

namespace {
struct LVGLInitializerTempHistory {
    LVGLInitializerTempHistory() {
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

static LVGLInitializerTempHistory lvgl_init;
} // namespace

// ============================================================================
// Test Fixture
// ============================================================================

class TemperatureHistoryManagerTestFixture {
    static bool queue_initialized;

  public:
    TemperatureHistoryManagerTestFixture() {
        // Initialize update queue once (static guard) - CRITICAL for ui_queue_update()
        if (!queue_initialized) {
            ui_update_queue_init();
            queue_initialized = true;
        }

        printer_state_.init_subjects(false);
        manager_ = std::make_unique<TemperatureHistoryManager>(printer_state_);
    }

    ~TemperatureHistoryManagerTestFixture() {
        // Destroy managed objects first
        manager_.reset();

        // Drain pending callbacks
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Shutdown queue
        ui_update_queue_shutdown();

        // Reset static flag for next test
        queue_initialized = false;
    }

  protected:
    // ========================================================================
    // Temperature Subject Helpers
    // ========================================================================

    /**
     * @brief Set extruder temperature via PrinterState subject
     *
     * Simulates a temperature update from Moonraker notification.
     * Value is in centidegrees (temp * 10).
     */
    void set_extruder_temp(int centidegrees) {
        lv_subject_set_int(printer_state_.get_extruder_temp_subject(), centidegrees);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
    }

    /**
     * @brief Set extruder target temperature
     */
    void set_extruder_target(int centidegrees) {
        lv_subject_set_int(printer_state_.get_extruder_target_subject(), centidegrees);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
    }

    /**
     * @brief Set bed temperature via PrinterState subject
     */
    void set_bed_temp(int centidegrees) {
        lv_subject_set_int(printer_state_.get_bed_temp_subject(), centidegrees);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
    }

    /**
     * @brief Set bed target temperature
     */
    void set_bed_target(int centidegrees) {
        lv_subject_set_int(printer_state_.get_bed_target_subject(), centidegrees);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
    }

    // ========================================================================
    // Wait/Polling Helpers
    // ========================================================================

    /**
     * @brief Wait for sample count to reach expected value
     *
     * @param heater Heater name (e.g., "extruder")
     * @param count Expected sample count
     * @param timeout_ms Maximum time to wait
     * @return true if count reached, false if timeout
     */
    bool wait_for_sample_count(const std::string& heater, int count, int timeout_ms = 500) {
        for (int i = 0; i < timeout_ms / 10; ++i) {
            UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
            if (manager_->get_sample_count(heater) >= count) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    /**
     * @brief Get current Unix timestamp in milliseconds
     */
    static int64_t now_ms() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    PrinterState printer_state_;
    std::unique_ptr<TemperatureHistoryManager> manager_;
};
bool TemperatureHistoryManagerTestFixture::queue_initialized = false;

// ============================================================================
// Test Case 1: Initial State
// ============================================================================

TEST_CASE_METHOD(TemperatureHistoryManagerTestFixture,
                 "TemperatureHistoryManager starts with no samples", "[temperature_history]") {
    // Given: a freshly created manager

    // Then: no samples exist
    REQUIRE(manager_->get_sample_count("extruder") == 0);
    REQUIRE(manager_->get_sample_count("heater_bed") == 0);
    REQUIRE(manager_->get_samples("extruder").empty());
    REQUIRE(manager_->get_samples("heater_bed").empty());
}

// ============================================================================
// Test Case 2: Heater Discovery
// ============================================================================

TEST_CASE_METHOD(TemperatureHistoryManagerTestFixture,
                 "TemperatureHistoryManager returns known heater names", "[temperature_history]") {
    // Given: manager is initialized with PrinterState

    // When: we query heater names
    auto heaters = manager_->get_heater_names();

    // Then: standard heaters are known
    // Note: Implementation should at minimum know about extruder and heater_bed
    REQUIRE(heaters.size() >= 2);

    bool has_extruder = std::find(heaters.begin(), heaters.end(), "extruder") != heaters.end();
    bool has_bed = std::find(heaters.begin(), heaters.end(), "heater_bed") != heaters.end();

    REQUIRE(has_extruder);
    REQUIRE(has_bed);
}

// ============================================================================
// Test Case 3: Sample Collection from Subject
// ============================================================================

TEST_CASE_METHOD(TemperatureHistoryManagerTestFixture,
                 "TemperatureHistoryManager stores sample when temperature subject changes",
                 "[temperature_history]") {
    // Given: manager is observing temperature subjects
    int64_t before_ms = now_ms();

    // When: extruder temperature changes
    set_extruder_temp(2053);   // 205.3°C
    set_extruder_target(2100); // 210.0°C target

    // Then: a sample should be stored
    // Note: This test depends on implementation subscribing to subjects
    REQUIRE(wait_for_sample_count("extruder", 1, 100));

    auto samples = manager_->get_samples("extruder");
    REQUIRE(samples.size() == 1);
    REQUIRE(samples[0].temp_centi == 2053);
    REQUIRE(samples[0].target_centi == 2100);
    REQUIRE(samples[0].timestamp_ms >= before_ms);
    REQUIRE(samples[0].timestamp_ms <= now_ms());
}

// ============================================================================
// Test Case 4: Throttling (1Hz max)
// ============================================================================

TEST_CASE_METHOD(TemperatureHistoryManagerTestFixture,
                 "TemperatureHistoryManager throttles rapid updates to 1Hz",
                 "[temperature_history]") {
    // Given: manager is tracking samples
    int64_t ts = now_ms();

    // When: we inject samples rapidly (simulating 4Hz updates)
    bool stored1 =
        TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder", 2000, 2100, ts);
    bool stored2 = TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder", 2010,
                                                                   2100, ts + 250); // +250ms
    bool stored3 = TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder", 2020,
                                                                   2100, ts + 500); // +500ms
    bool stored4 = TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder", 2030,
                                                                   2100, ts + 750); // +750ms
    bool stored5 = TemperatureHistoryManagerTestAccess::add_sample(
        *manager_, "extruder", 2040, 2100, ts + 1000); // +1000ms (new second)

    // Then: only samples at 1Hz intervals should be stored
    // First sample always stored
    REQUIRE(stored1);
    // Samples 2-4 should be throttled (within same second)
    REQUIRE_FALSE(stored2);
    REQUIRE_FALSE(stored3);
    REQUIRE_FALSE(stored4);
    // Sample 5 should be stored (1 second later)
    REQUIRE(stored5);

    REQUIRE(manager_->get_sample_count("extruder") == 2);
}

// ============================================================================
// Test Case 5: Circular Buffer Eviction
// ============================================================================

TEST_CASE_METHOD(TemperatureHistoryManagerTestFixture,
                 "TemperatureHistoryManager evicts oldest samples at HISTORY_SIZE",
                 "[temperature_history]") {
    // Given: we fill the buffer with HISTORY_SIZE samples
    int64_t base_ts = now_ms();

    for (int i = 0; i < TemperatureHistoryManager::HISTORY_SIZE; ++i) {
        int64_t ts = base_ts + (i * TemperatureHistoryManager::SAMPLE_INTERVAL_MS);
        bool stored = TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder",
                                                                      2000 + i, 2100, ts);
        REQUIRE(stored);
    }

    REQUIRE(manager_->get_sample_count("extruder") == TemperatureHistoryManager::HISTORY_SIZE);

    // When: we add one more sample
    int64_t overflow_ts = base_ts + (TemperatureHistoryManager::HISTORY_SIZE *
                                     TemperatureHistoryManager::SAMPLE_INTERVAL_MS);
    bool overflow_stored = TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder",
                                                                           9999, 2100, overflow_ts);
    REQUIRE(overflow_stored);

    // Then: count stays at HISTORY_SIZE (oldest evicted)
    REQUIRE(manager_->get_sample_count("extruder") == TemperatureHistoryManager::HISTORY_SIZE);

    // And: newest sample is present
    auto samples = manager_->get_samples("extruder");
    REQUIRE(samples.back().temp_centi == 9999);

    // And: original first sample (2000) is gone
    REQUIRE(samples.front().temp_centi != 2000);
    REQUIRE(samples.front().temp_centi == 2001); // Second sample is now first
}

// ============================================================================
// Test Case 6: Multi-Heater Isolation
// ============================================================================

TEST_CASE_METHOD(TemperatureHistoryManagerTestFixture,
                 "TemperatureHistoryManager maintains separate history per heater",
                 "[temperature_history]") {
    // Given: samples for different heaters
    int64_t ts = now_ms();

    // When: we add samples to different heaters
    TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder", 2000, 2100, ts);
    TemperatureHistoryManagerTestAccess::add_sample(*manager_, "heater_bed", 600, 700, ts);

    // Add more to extruder at different time
    TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder", 2050, 2100, ts + 1000);

    // Then: heaters have independent histories
    REQUIRE(manager_->get_sample_count("extruder") == 2);
    REQUIRE(manager_->get_sample_count("heater_bed") == 1);

    auto extruder_samples = manager_->get_samples("extruder");
    auto bed_samples = manager_->get_samples("heater_bed");

    REQUIRE(extruder_samples[0].temp_centi == 2000);
    REQUIRE(extruder_samples[1].temp_centi == 2050);
    REQUIRE(bed_samples[0].temp_centi == 600);
}

// ============================================================================
// Test Case 7: Observer Notification
// ============================================================================

TEST_CASE_METHOD(TemperatureHistoryManagerTestFixture,
                 "TemperatureHistoryManager notifies observers when samples stored",
                 "[temperature_history]") {
    std::atomic<int> callback_count{0};
    std::string last_heater;
    std::mutex heater_mutex;

    // Given: an observer is registered
    TemperatureHistoryManager::HistoryCallback callback = [&](const std::string& heater_name) {
        callback_count++;
        std::lock_guard<std::mutex> lock(heater_mutex);
        last_heater = heater_name;
    };
    manager_->add_observer(&callback);

    // When: a sample is added
    int64_t ts = now_ms();
    TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder", 2000, 2100, ts);

    // Then: observer should be notified
    REQUIRE(callback_count.load() == 1);
    {
        std::lock_guard<std::mutex> lock(heater_mutex);
        REQUIRE(last_heater == "extruder");
    }

    // And: subsequent samples trigger notifications
    TemperatureHistoryManagerTestAccess::add_sample(*manager_, "heater_bed", 600, 700, ts);
    REQUIRE(callback_count.load() == 2);
    {
        std::lock_guard<std::mutex> lock(heater_mutex);
        REQUIRE(last_heater == "heater_bed");
    }
}

// ============================================================================
// Test Case 8: Observer Removal
// ============================================================================

TEST_CASE_METHOD(TemperatureHistoryManagerTestFixture,
                 "TemperatureHistoryManager stops notifying removed observers",
                 "[temperature_history]") {
    std::atomic<int> callback_count{0};

    // Given: an observer is registered
    TemperatureHistoryManager::HistoryCallback callback = [&](const std::string&) {
        callback_count++;
    };
    manager_->add_observer(&callback);

    // And: a sample triggers notification
    int64_t ts = now_ms();
    TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder", 2000, 2100, ts);
    REQUIRE(callback_count.load() == 1);

    // When: observer is removed
    manager_->remove_observer(&callback);

    // And: another sample is added
    TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder", 2050, 2100, ts + 1000);

    // Then: observer should NOT be notified again
    REQUIRE(callback_count.load() == 1);
}

// ============================================================================
// Test Case 9: Time-Range Query
// ============================================================================

TEST_CASE_METHOD(TemperatureHistoryManagerTestFixture,
                 "TemperatureHistoryManager filters samples by timestamp",
                 "[temperature_history]") {
    // Given: samples at different timestamps
    int64_t base_ts = now_ms();
    int64_t ts1 = base_ts;
    int64_t ts2 = base_ts + 1000; // +1s
    int64_t ts3 = base_ts + 2000; // +2s
    int64_t ts4 = base_ts + 3000; // +3s

    TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder", 2000, 2100, ts1);
    TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder", 2010, 2100, ts2);
    TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder", 2020, 2100, ts3);
    TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder", 2030, 2100, ts4);

    // When: querying samples since ts2
    auto filtered = manager_->get_samples_since("extruder", ts2);

    // Then: only samples after ts2 are returned (ts3 and ts4)
    REQUIRE(filtered.size() == 2);
    REQUIRE(filtered[0].temp_centi == 2020);
    REQUIRE(filtered[0].timestamp_ms == ts3);
    REQUIRE(filtered[1].temp_centi == 2030);
    REQUIRE(filtered[1].timestamp_ms == ts4);

    // And: query with future timestamp returns empty
    auto none = manager_->get_samples_since("extruder", ts4 + 1000);
    REQUIRE(none.empty());

    // And: query with very old timestamp returns all
    auto all = manager_->get_samples_since("extruder", 0);
    REQUIRE(all.size() == 4);
}

// ============================================================================
// Test Case 10: Thread Safety Smoke Test
// ============================================================================

TEST_CASE_METHOD(TemperatureHistoryManagerTestFixture,
                 "TemperatureHistoryManager handles concurrent reads and writes",
                 "[temperature_history]") {
    // This is a basic smoke test for thread safety.
    // It verifies that concurrent operations don't crash.

    std::atomic<bool> stop{false};
    std::atomic<int> reads_completed{0};
    std::atomic<int> writes_completed{0};

    // Start writer thread
    std::thread writer([&]() {
        int64_t ts = now_ms();
        for (int i = 0; i < 100 && !stop; ++i) {
            TemperatureHistoryManagerTestAccess::add_sample(
                *manager_, "extruder", 2000 + i, 2100,
                ts + (i * TemperatureHistoryManager::SAMPLE_INTERVAL_MS));
            writes_completed++;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    // Start reader threads
    std::thread reader1([&]() {
        while (!stop && reads_completed < 50) {
            auto samples = manager_->get_samples("extruder");
            (void)samples.size(); // Use result to prevent optimization
            reads_completed++;
        }
    });

    std::thread reader2([&]() {
        while (!stop && reads_completed < 50) {
            auto count = manager_->get_sample_count("extruder");
            (void)count;
            reads_completed++;
        }
    });

    // Wait for completion or timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop = true;

    writer.join();
    reader1.join();
    reader2.join();

    // Then: no crashes occurred and some operations completed
    REQUIRE(writes_completed > 0);
    REQUIRE(reads_completed > 0);

    // And: data is consistent (count matches vector size)
    auto final_samples = manager_->get_samples("extruder");
    auto final_count = manager_->get_sample_count("extruder");
    REQUIRE(static_cast<int>(final_samples.size()) == final_count);
}

// ============================================================================
// Test Case: Unknown Heater Returns Empty
// ============================================================================

TEST_CASE_METHOD(TemperatureHistoryManagerTestFixture,
                 "TemperatureHistoryManager returns empty for unknown heater",
                 "[temperature_history]") {
    // Given: a heater that doesn't exist
    std::string unknown = "heater_chamber_nonexistent";

    // Then: queries return empty/zero gracefully
    REQUIRE(manager_->get_sample_count(unknown) == 0);
    REQUIRE(manager_->get_samples(unknown).empty());
    REQUIRE(manager_->get_samples_since(unknown, 0).empty());
}

// ============================================================================
// Test Case: Multiple Observers
// ============================================================================

TEST_CASE_METHOD(TemperatureHistoryManagerTestFixture,
                 "TemperatureHistoryManager notifies all registered observers",
                 "[temperature_history]") {
    std::atomic<int> callback1_count{0};
    std::atomic<int> callback2_count{0};

    // Given: multiple observers registered
    TemperatureHistoryManager::HistoryCallback callback1 = [&](const std::string&) {
        callback1_count++;
    };
    TemperatureHistoryManager::HistoryCallback callback2 = [&](const std::string&) {
        callback2_count++;
    };
    manager_->add_observer(&callback1);
    manager_->add_observer(&callback2);

    // When: a sample is added
    int64_t ts = now_ms();
    TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder", 2000, 2100, ts);

    // Then: all observers are notified
    REQUIRE(callback1_count.load() == 1);
    REQUIRE(callback2_count.load() == 1);

    // When: one observer is removed
    manager_->remove_observer(&callback1);
    TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder", 2050, 2100, ts + 1000);

    // Then: only remaining observer is notified
    REQUIRE(callback1_count.load() == 1); // Unchanged
    REQUIRE(callback2_count.load() == 2); // Incremented
}
