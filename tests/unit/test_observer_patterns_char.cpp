// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_observer_patterns_char.cpp
 * @brief Characterization tests for observer patterns used in pilot panels
 *
 * These tests document the EXISTING behavior of observer patterns before
 * the observer factory refactor. Run with: ./build/bin/helix-tests "[characterization]"
 */

#include "ui_observer_guard.h"
#include "ui_temperature_utils.h"

#include "../lvgl_test_fixture.h"

#include <atomic>
#include <cstring>

#include "../catch_amalgamated.hpp"

using helix::ui::temperature::centi_to_degrees;
using helix::ui::temperature::centi_to_degrees_f;

// ============================================================================
// CHARACTERIZATION: Temperature Unit Conversion (centidegrees -> degrees)
// ============================================================================

TEST_CASE("CHAR: centi_to_degrees converts centidegrees to degrees",
          "[characterization][observer][temperature]") {
    // Note: centi_to_degrees divides by 10 (decidegrees), not 100
    REQUIRE(centi_to_degrees(2100) == 210); // 210.0C
    REQUIRE(centi_to_degrees(2105) == 210); // 210.5C truncates
    REQUIRE(centi_to_degrees(2450) == 245); // 245.0C
    REQUIRE(centi_to_degrees(600) == 60);   // 60.0C bed temp
    REQUIRE(centi_to_degrees(0) == 0);      // Off
    REQUIRE(centi_to_degrees(5) == 0);      // 0.5C truncates to 0
}

TEST_CASE("CHAR: centi_to_degrees_f preserves decimals",
          "[characterization][observer][temperature]") {
    REQUIRE(centi_to_degrees_f(2100) == Catch::Approx(210.0f));
    REQUIRE(centi_to_degrees_f(2105) == Catch::Approx(210.5f));
    REQUIRE(centi_to_degrees_f(2109) == Catch::Approx(210.9f));
}

// ============================================================================
// CHARACTERIZATION: FilamentPanel Temperature Pattern
// ============================================================================

/**
 * FilamentPanel pattern: transforms centidegrees to degrees in callback
 *
 * extruder_temp_observer_ = ObserverGuard(
 *     printer_state_.get_active_extruder_temp_subject(),
 *     [](lv_observer_t* observer, lv_subject_t* subject) {
 *         auto* self = static_cast<FilamentPanel*>(lv_observer_get_user_data(observer));
 *         self->nozzle_current_ = centi_to_degrees(lv_subject_get_int(subject));
 *         helix::ui::async_call(...);  // Queue UI updates
 *     }, this);
 */
TEST_CASE_METHOD(LVGLTestFixture, "CHAR: FilamentPanel transforms centidegrees in callback",
                 "[characterization][observer][filament]") {
    // LVGLTestFixture handles initialization

    lv_subject_t temp_subject;
    lv_subject_init_int(&temp_subject, 0);

    struct State {
        int raw_value = 0;
        int transformed_value = 0;
    } state;

    auto cb = [](lv_observer_t* obs, lv_subject_t* subj) {
        auto* s = static_cast<State*>(lv_observer_get_user_data(obs));
        s->raw_value = lv_subject_get_int(subj);
        s->transformed_value = centi_to_degrees(s->raw_value);
    };

    ObserverGuard guard(&temp_subject, cb, &state);

    // Set to 210C (centidegrees = 2100)
    lv_subject_set_int(&temp_subject, 2100);
    REQUIRE(state.raw_value == 2100);
    REQUIRE(state.transformed_value == 210);

    // Set to 245C
    lv_subject_set_int(&temp_subject, 2450);
    REQUIRE(state.raw_value == 2450);
    REQUIRE(state.transformed_value == 245);

    guard.release();
    lv_subject_deinit(&temp_subject);
}

// ============================================================================
// CHARACTERIZATION: ControlsPanel Raw Caching Pattern
// ============================================================================

/**
 * ControlsPanel pattern: caches RAW centidegrees, transforms in display method
 *
 * void ControlsPanel::on_extruder_temp_changed(...) {
 *     self->cached_extruder_temp_ = lv_subject_get_int(subject);  // Raw!
 *     self->update_nozzle_temp_display();  // Transforms later
 * }
 */
TEST_CASE_METHOD(LVGLTestFixture, "CHAR: ControlsPanel caches raw centidegrees value",
                 "[characterization][observer][controls]") {
    // LVGLTestFixture handles initialization

    lv_subject_t temp_subject;
    lv_subject_init_int(&temp_subject, 0);

    int cached_raw = 0;

    auto cb = [](lv_observer_t* obs, lv_subject_t* subj) {
        auto* cached = static_cast<int*>(lv_observer_get_user_data(obs));
        *cached = lv_subject_get_int(subj); // Raw, no transform
    };

    ObserverGuard guard(&temp_subject, cb, &cached_raw);

    lv_subject_set_int(&temp_subject, 2100);
    REQUIRE(cached_raw == 2100); // Raw centidegrees, not 210

    // Transform happens separately in display update
    int display_degrees = centi_to_degrees(cached_raw);
    REQUIRE(display_degrees == 210);

    guard.release();
    lv_subject_deinit(&temp_subject);
}

// ============================================================================
// CHARACTERIZATION: String Subject (Minimal Test)
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "CHAR: String subject minimal test",
                 "[characterization][observer][string]") {
    // Minimal test to isolate string subject behavior
    static char buf[16] = "";
    lv_subject_t subject;
    lv_subject_init_string(&subject, buf, nullptr, sizeof(buf), "");

    // Just test that init works
    REQUIRE(lv_subject_get_string(&subject) != nullptr);
    REQUIRE(strcmp(lv_subject_get_string(&subject), "") == 0);

    // Test that copy works
    lv_subject_copy_string(&subject, "test");
    REQUIRE(strcmp(lv_subject_get_string(&subject), "test") == 0);

    lv_subject_deinit(&subject);
}

// ============================================================================
// CHARACTERIZATION: ControlsPanel Homed Axes String Parsing
// ============================================================================

/**
 * ControlsPanel parses "xyz" string to set individual homed flags
 */
TEST_CASE_METHOD(LVGLTestFixture, "CHAR: ControlsPanel parses homed axes string",
                 "[characterization][observer][controls]") {
    // LVGLTestFixture handles initialization

    // Use local non-static buffer for this test
    char axes_buf[16];
    std::memset(axes_buf, 0, sizeof(axes_buf)); // Explicitly zero
    lv_subject_t homed_axes;
    lv_subject_init_string(&homed_axes, axes_buf, nullptr, sizeof(axes_buf), "");

    INFO("Subject initialized, type=" << homed_axes.type);
    INFO("Buffer address=" << (void*)axes_buf);
    INFO("Subject pointer=" << homed_axes.value.pointer);

    struct HomedState {
        bool x = false, y = false, z = false;
        bool all = false;
    } state;

    auto cb = [](lv_observer_t* obs, lv_subject_t* subj) {
        auto* s = static_cast<HomedState*>(lv_observer_get_user_data(obs));
        if (!s)
            return; // Safety check
        const char* axes = lv_subject_get_string(subj);
        if (!axes)
            axes = "";

        s->x = strchr(axes, 'x') != nullptr;
        s->y = strchr(axes, 'y') != nullptr;
        s->z = strchr(axes, 'z') != nullptr;
        s->all = s->x && s->y && s->z;
    };

    INFO("About to create ObserverGuard");
    ObserverGuard guard(&homed_axes, cb, &state);
    INFO("ObserverGuard created successfully");

    // Empty = nothing homed
    REQUIRE(state.x == false);
    REQUIRE(state.all == false);

    // All homed
    lv_subject_copy_string(&homed_axes, "xyz");
    REQUIRE(state.x == true);
    REQUIRE(state.y == true);
    REQUIRE(state.z == true);
    REQUIRE(state.all == true);

    // Partial homing
    lv_subject_copy_string(&homed_axes, "xy");
    REQUIRE(state.x == true);
    REQUIRE(state.y == true);
    REQUIRE(state.z == false);
    REQUIRE(state.all == false);

    guard.release();
    lv_subject_deinit(&homed_axes);
}

// ============================================================================
// CHARACTERIZATION: ObserverGuard RAII Cleanup
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "CHAR: ObserverGuard removes observer on destruction",
                 "[characterization][observer][raii]") {
    // LVGLTestFixture handles initialization

    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    std::atomic<int> callback_count{0};

    {
        auto cb = [](lv_observer_t* obs, lv_subject_t*) {
            auto* count = static_cast<std::atomic<int>*>(lv_observer_get_user_data(obs));
            (*count)++;
        };

        ObserverGuard guard(&subject, cb, &callback_count);
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
// CHARACTERIZATION: Speed/Flow Factor (No Transformation)
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "CHAR: Speed factor used directly as percentage",
                 "[characterization][observer][controls]") {
    // LVGLTestFixture handles initialization

    lv_subject_t speed_factor;
    lv_subject_init_int(&speed_factor, 100); // 100% default

    int speed_pct = 100;

    auto cb = [](lv_observer_t* obs, lv_subject_t* subj) {
        auto* pct = static_cast<int*>(lv_observer_get_user_data(obs));
        *pct = lv_subject_get_int(subj); // Direct, no transform
    };

    ObserverGuard guard(&speed_factor, cb, &speed_pct);

    lv_subject_set_int(&speed_factor, 150); // 150%
    REQUIRE(speed_pct == 150);

    lv_subject_set_int(&speed_factor, 50); // 50%
    REQUIRE(speed_pct == 50);

    guard.release();
    lv_subject_deinit(&speed_factor);
}

// ============================================================================
// Documentation: Observer Pattern Summary
// ============================================================================

/**
 * SUMMARY OF OBSERVER PATTERNS:
 *
 * 1. FilamentPanel (TRANSFORM IN CALLBACK):
 *    - Converts centidegrees→degrees in the callback
 *    - Stores transformed value
 *    - Uses ui_async_call for UI updates
 *
 * 2. ControlsPanel (CACHE RAW):
 *    - Stores raw centidegrees in callback
 *    - Transforms in display update method
 *    - Direct method calls (no async)
 *
 * 3. String Parsing (ControlsPanel homed_axes):
 *    - Parses string to set boolean flags
 *    - Multiple derived values from single subject
 *
 * 4. Direct Value (Speed/Flow factors):
 *    - Value used as-is (already in correct units)
 *    - No transformation needed
 *
 * KEY OBSERVATIONS:
 * - LVGL observers fire immediately on subscription
 * - LVGL optimizes: no callback for unchanged values
 * - ObserverGuard provides RAII cleanup
 */
