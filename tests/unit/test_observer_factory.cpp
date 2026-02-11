// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_observer_factory.cpp
 * @brief Unit tests for observer factory helpers
 *
 * Tests the observe_* factory functions for correctness,
 * edge cases, and behavior preservation.
 */

#include "ui_temperature_utils.h"
#include "ui_update_queue.h"

#include "../lvgl_test_fixture.h"
#include "../test_helpers/update_queue_test_access.h"
#include "observer_factory.h"

#include <atomic>
#include <cstring>

#include "../catch_amalgamated.hpp"

using namespace helix::ui;
using helix::ui::temperature::centi_to_degrees;

// ============================================================================
// Test Panel Class - Mimics a real panel for testing
// ============================================================================

class TestPanel {
  public:
    int int_value = 0;
    int callback_count = 0;
    bool update_called = false;
    std::string string_value;

    void on_value_update() {
        update_called = true;
        callback_count++;
    }

    void reset() {
        int_value = 0;
        callback_count = 0;
        update_called = false;
        string_value.clear();
    }
};

// ============================================================================
// observe_int_sync Tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe_int_sync stores value", "[factory][observer]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    TestPanel panel;

    auto guard = observe_int_sync<TestPanel>(&subject, &panel,
                                             [](TestPanel* p, int value) { p->int_value = value; });

    // Initial callback fires on subscription
    REQUIRE(panel.int_value == 0);

    // Value change triggers callback
    lv_subject_set_int(&subject, 42);
    REQUIRE(panel.int_value == 42);

    // Another change
    lv_subject_set_int(&subject, 100);
    REQUIRE(panel.int_value == 100);

    guard.release();
    lv_subject_deinit(&subject);
}

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe_int_sync with transformation",
                 "[factory][observer]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    TestPanel panel;

    // Use transformation inside lambda
    auto guard = observe_int_sync<TestPanel>(
        &subject, &panel, [](TestPanel* p, int raw) { p->int_value = centi_to_degrees(raw); });

    // Set to 210C (centidegrees = 2100)
    lv_subject_set_int(&subject, 2100);
    REQUIRE(panel.int_value == 210);

    // Set to 245C
    lv_subject_set_int(&subject, 2450);
    REQUIRE(panel.int_value == 245);

    guard.release();
    lv_subject_deinit(&subject);
}

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe_int_sync null subject returns empty guard",
                 "[factory][observer][edge]") {
    TestPanel panel;

    auto guard = observe_int_sync<TestPanel>(nullptr, &panel,
                                             [](TestPanel* p, int value) { p->int_value = value; });

    REQUIRE_FALSE(guard); // Guard should be empty
}

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe_int_sync null panel returns empty guard",
                 "[factory][observer][edge]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 42);

    auto guard = observe_int_sync<TestPanel>(&subject, nullptr,
                                             [](TestPanel* p, int value) { p->int_value = value; });

    REQUIRE_FALSE(guard); // Guard should be empty

    lv_subject_deinit(&subject);
}

// ============================================================================
// observe_int_async Tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe_int_async calls value handler",
                 "[factory][observer]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    TestPanel panel;

    auto guard = observe_int_async<TestPanel>(
        &subject, &panel, [](TestPanel* p, int value) { p->int_value = value; },
        [](TestPanel* p) { p->on_value_update(); });

    // Initial callback fires on subscription
    REQUIRE(panel.int_value == 0);

    // Value change triggers callback
    lv_subject_set_int(&subject, 42);
    REQUIRE(panel.int_value == 42);

    // Process async queue to trigger update handler
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
    REQUIRE(panel.update_called == true);

    guard.release();
    lv_subject_deinit(&subject);
}

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe_int_async with temperature transform",
                 "[factory][observer][temperature]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    TestPanel panel;

    auto guard = observe_int_async<TestPanel>(
        &subject, &panel, [](TestPanel* p, int raw) { p->int_value = centi_to_degrees(raw); },
        [](TestPanel* p) { p->on_value_update(); });

    // Set to 210C (centidegrees = 2100)
    lv_subject_set_int(&subject, 2100);
    REQUIRE(panel.int_value == 210);

    // Process async queue
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
    REQUIRE(panel.update_called == true);
    panel.reset();
    panel.int_value = 210; // Keep transformed value

    // Set to 60C bed temp
    lv_subject_set_int(&subject, 600);
    REQUIRE(panel.int_value == 60);

    // Drain async queue before releasing guard - ensures pending callbacks
    // execute while panel is still valid (L054 pattern)
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

    guard.release();
    lv_subject_deinit(&subject);
}

// ============================================================================
// observe_string Tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe_string handles string values",
                 "[factory][observer][string]") {
    static char buf[32] = "";
    lv_subject_t subject;
    lv_subject_init_string(&subject, buf, nullptr, sizeof(buf), "");

    TestPanel panel;

    auto guard = observe_string<TestPanel>(
        &subject, &panel, [](TestPanel* p, const char* str) { p->string_value = str; });

    // Initial callback fires on subscription
    REQUIRE(panel.string_value == "");

    // Value change triggers callback
    lv_subject_copy_string(&subject, "test");
    REQUIRE(panel.string_value == "test");

    // Another change
    lv_subject_copy_string(&subject, "hello world");
    REQUIRE(panel.string_value == "hello world");

    guard.release();
    lv_subject_deinit(&subject);
}

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe_string parses axes like ControlsPanel",
                 "[factory][observer][string]") {
    static char buf[16] = "";
    lv_subject_t subject;
    lv_subject_init_string(&subject, buf, nullptr, sizeof(buf), "");

    struct AxesState {
        bool x = false, y = false, z = false;
        bool all = false;
    } state;

    auto guard = observe_string<AxesState>(&subject, &state, [](AxesState* s, const char* axes) {
        s->x = strchr(axes, 'x') != nullptr;
        s->y = strchr(axes, 'y') != nullptr;
        s->z = strchr(axes, 'z') != nullptr;
        s->all = s->x && s->y && s->z;
    });

    // Empty = nothing homed
    REQUIRE(state.x == false);
    REQUIRE(state.all == false);

    // All homed
    lv_subject_copy_string(&subject, "xyz");
    REQUIRE(state.x == true);
    REQUIRE(state.y == true);
    REQUIRE(state.z == true);
    REQUIRE(state.all == true);

    // Partial homing
    lv_subject_copy_string(&subject, "xy");
    REQUIRE(state.x == true);
    REQUIRE(state.y == true);
    REQUIRE(state.z == false);
    REQUIRE(state.all == false);

    guard.release();
    lv_subject_deinit(&subject);
}

// ============================================================================
// observe_string_async Tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe_string_async calls update handler",
                 "[factory][observer][string]") {
    static char buf[32] = "";
    lv_subject_t subject;
    lv_subject_init_string(&subject, buf, nullptr, sizeof(buf), "");

    TestPanel panel;

    auto guard = observe_string_async<TestPanel>(
        &subject, &panel, [](TestPanel* p, const char* str) { p->string_value = str; },
        [](TestPanel* p) { p->on_value_update(); });

    // Value change triggers callback
    lv_subject_copy_string(&subject, "test");
    REQUIRE(panel.string_value == "test");

    // Process async queue
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
    REQUIRE(panel.update_called == true);

    guard.release();
    lv_subject_deinit(&subject);
}

// ============================================================================
// RAII Cleanup Tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: ObserverGuard RAII cleanup works",
                 "[factory][observer][raii]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    std::atomic<int> callback_count{0};

    {
        TestPanel panel;
        auto guard = observe_int_sync<TestPanel>(
            &subject, &panel, [&callback_count](TestPanel*, int) { callback_count++; });

        REQUIRE(callback_count.load() == 1); // Initial

        lv_subject_set_int(&subject, 42);
        REQUIRE(callback_count.load() == 2);

        // Guard goes out of scope here
    }

    // After guard destroyed, no more callbacks
    callback_count.store(0);
    lv_subject_set_int(&subject, 100);
    REQUIRE(callback_count.load() == 0);

    lv_subject_deinit(&subject);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: LVGL optimizes unchanged values",
                 "[factory][observer][edge]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 50);

    int callback_count = 0;
    TestPanel panel;

    auto guard = observe_int_sync<TestPanel>(
        &subject, &panel, [&callback_count](TestPanel*, int) { callback_count++; });

    callback_count = 0; // Reset after initial

    // Same value - no callback
    lv_subject_set_int(&subject, 50);
    REQUIRE(callback_count == 0);

    // Different value - callback
    lv_subject_set_int(&subject, 51);
    REQUIRE(callback_count == 1);

    guard.release();
    lv_subject_deinit(&subject);
}
