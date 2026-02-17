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
using namespace helix::ui;
using helix::ui::temperature::centi_to_degrees;

// Helper to drain the update queue after subject changes.
// observe_int_sync and observe_string defer callbacks via ui_queue_update.
static void drain() {
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
}

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
// observe_int_sync Tests (deferred via ui_queue_update)
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe_int_sync stores value", "[factory][observer]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    TestPanel panel;

    auto guard = observe_int_sync<TestPanel>(&subject, &panel,
                                             [](TestPanel* p, int value) { p->int_value = value; });

    // Initial callback fires on subscription (deferred)
    drain();
    REQUIRE(panel.int_value == 0);

    // Value change triggers callback
    lv_subject_set_int(&subject, 42);
    drain();
    REQUIRE(panel.int_value == 42);

    // Another change
    lv_subject_set_int(&subject, 100);
    drain();
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
    drain();
    REQUIRE(panel.int_value == 210);

    // Set to 245C
    lv_subject_set_int(&subject, 2450);
    drain();
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
// observe_int_immediate Tests (synchronous, no deferral)
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe_int_immediate fires synchronously",
                 "[factory][observer]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    TestPanel panel;

    auto guard = observe_int_immediate<TestPanel>(
        &subject, &panel, [](TestPanel* p, int value) { p->int_value = value; });

    // No drain needed — immediate fires synchronously
    REQUIRE(panel.int_value == 0);

    lv_subject_set_int(&subject, 42);
    REQUIRE(panel.int_value == 42);

    guard.release();
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
    drain();
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
    drain();
    REQUIRE(panel.update_called == true);
    panel.reset();
    panel.int_value = 210; // Keep transformed value

    // Set to 60C bed temp
    lv_subject_set_int(&subject, 600);
    REQUIRE(panel.int_value == 60);

    // Drain async queue before releasing guard - ensures pending callbacks
    // execute while panel is still valid (L054 pattern)
    drain();

    guard.release();
    lv_subject_deinit(&subject);
}

// ============================================================================
// observe_string Tests (deferred via ui_queue_update)
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe_string handles string values",
                 "[factory][observer][string]") {
    static char buf[32] = "";
    lv_subject_t subject;
    lv_subject_init_string(&subject, buf, nullptr, sizeof(buf), "");

    TestPanel panel;

    auto guard = observe_string<TestPanel>(
        &subject, &panel, [](TestPanel* p, const char* str) { p->string_value = str; });

    // Initial callback fires on subscription (deferred)
    drain();
    REQUIRE(panel.string_value == "");

    // Value change triggers callback
    lv_subject_copy_string(&subject, "test");
    drain();
    REQUIRE(panel.string_value == "test");

    // Another change
    lv_subject_copy_string(&subject, "hello world");
    drain();
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
    drain();
    REQUIRE(state.x == false);
    REQUIRE(state.all == false);

    // All homed
    lv_subject_copy_string(&subject, "xyz");
    drain();
    REQUIRE(state.x == true);
    REQUIRE(state.y == true);
    REQUIRE(state.z == true);
    REQUIRE(state.all == true);

    // Partial homing
    lv_subject_copy_string(&subject, "xy");
    drain();
    REQUIRE(state.x == true);
    REQUIRE(state.y == true);
    REQUIRE(state.z == false);
    REQUIRE(state.all == false);

    guard.release();
    lv_subject_deinit(&subject);
}

// ============================================================================
// observe_string_immediate Tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe_string_immediate fires synchronously",
                 "[factory][observer][string]") {
    static char buf[32] = "";
    lv_subject_t subject;
    lv_subject_init_string(&subject, buf, nullptr, sizeof(buf), "");

    TestPanel panel;

    auto guard = observe_string_immediate<TestPanel>(
        &subject, &panel, [](TestPanel* p, const char* str) { p->string_value = str; });

    // No drain needed — immediate fires synchronously
    REQUIRE(panel.string_value == "");

    lv_subject_copy_string(&subject, "test");
    REQUIRE(panel.string_value == "test");

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
    drain();
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

        drain();
        REQUIRE(callback_count.load() == 1); // Initial

        lv_subject_set_int(&subject, 42);
        drain();
        REQUIRE(callback_count.load() == 2);

        // Guard goes out of scope here — drain any pending before panel dies
        drain();
    }

    // After guard destroyed, no more callbacks
    callback_count.store(0);
    lv_subject_set_int(&subject, 100);
    drain();
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

    drain();
    callback_count = 0; // Reset after initial

    // Same value - no callback
    lv_subject_set_int(&subject, 50);
    drain();
    REQUIRE(callback_count == 0);

    // Different value - callback
    lv_subject_set_int(&subject, 51);
    drain();
    REQUIRE(callback_count == 1);

    guard.release();
    lv_subject_deinit(&subject);
}

// ============================================================================
// Deferred safety test — observer reassignment during notification
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe_int_sync safe under observer reassignment",
                 "[factory][observer][safety]") {
    lv_subject_t subject_a;
    lv_subject_t subject_b;
    lv_subject_init_int(&subject_a, 0);
    lv_subject_init_int(&subject_b, 100);

    TestPanel panel;
    ObserverGuard inner_guard;

    // Outer observer reassigns inner_guard when notified — this is the
    // exact pattern that caused the crash in issue #82.
    inner_guard = observe_int_sync<TestPanel>(
        &subject_b, &panel, [](TestPanel* p, int value) { p->int_value = value; });
    drain();

    auto outer_guard = observe_int_sync<TestPanel>(
        &subject_a, &panel, [&inner_guard, &subject_b](TestPanel* p, int /*value*/) {
            // Reassign inner observer — old one is destroyed here.
            // With deferred execution, this is safe.
            inner_guard = observe_int_sync<TestPanel>(
                &subject_b, p, [](TestPanel* pp, int v) { pp->int_value = v * 2; });
        });
    drain();

    // Trigger the outer observer — should safely reassign inner
    lv_subject_set_int(&subject_a, 1);
    drain(); // outer fires, reassigns inner_guard
    drain(); // inner's initial callback fires

    // Now inner should use the new handler (value * 2)
    lv_subject_set_int(&subject_b, 50);
    drain();
    REQUIRE(panel.int_value == 100); // 50 * 2

    outer_guard.release();
    inner_guard.release();
    lv_subject_deinit(&subject_a);
    lv_subject_deinit(&subject_b);
}
