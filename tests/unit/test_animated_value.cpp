// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_animated_value.cpp
 * @brief Unit tests for AnimatedValue template class
 *
 * Tests the animated value transitions, retargeting behavior,
 * threshold skipping, and RAII cleanup.
 */

#include "../lvgl_test_fixture.h"
#include "settings_manager.h"
#include "ui/animated_value.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;

// ============================================================================
// Test Fixture with Settings Initialization
// ============================================================================

class AnimatedValueTestFixture : public LVGLTestFixture {
  public:
    AnimatedValueTestFixture() : LVGLTestFixture() {
        // Ensure SettingsManager subjects are initialized for animations_enabled check
        // init_subjects() is idempotent - safe to call multiple times
        SettingsManager::instance().init_subjects();
        // Enable animations for all tests by default
        SettingsManager::instance().set_animations_enabled(true);
    }
};

// ============================================================================
// Basic Binding Tests
// ============================================================================

TEST_CASE_METHOD(AnimatedValueTestFixture,
                 "AnimatedValue: bind invokes callback with initial value", "[animated_value]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 100);

    int received_value = -1;
    AnimatedValue<int> animated;

    animated.bind(&subject, [&](int v) { received_value = v; });

    // Initial value should be received immediately
    REQUIRE(received_value == 100);
    REQUIRE(animated.display_value() == 100);
    REQUIRE(animated.target_value() == 100);
    REQUIRE(animated.is_bound());

    animated.unbind();
    lv_subject_deinit(&subject);
}

TEST_CASE_METHOD(AnimatedValueTestFixture, "AnimatedValue: unbind clears state",
                 "[animated_value]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    int callback_count = 0;
    AnimatedValue<int> animated;

    animated.bind(&subject, [&](int) { callback_count++; });

    // Reset count after initial callback
    callback_count = 0;

    // Unbind
    animated.unbind();
    REQUIRE(!animated.is_bound());

    // Subject changes after unbind should not trigger callbacks
    // (No process_lvgl needed - observer was already removed)
    lv_subject_set_int(&subject, 50);

    REQUIRE(callback_count == 0);

    lv_subject_deinit(&subject);
}

// ============================================================================
// Animation Behavior Tests
// ============================================================================

TEST_CASE_METHOD(AnimatedValueTestFixture, "AnimatedValue: starts animation on value change",
                 "[animated_value]") {
    lv_subject_t subject;
    // Use non-zero initial value - AnimatedValue skips animation when display_value_ is 0
    // (by design, to handle startup where values arrive rapidly)
    lv_subject_init_int(&subject, 50);

    int last_value = -1;
    AnimatedValue<int> animated;

    animated.bind(&subject, [&](int v) { last_value = v; }, {.duration_ms = 50, .threshold = 0});

    // Set new value - should start animation
    lv_subject_set_int(&subject, 100);

    // Animation should be running
    REQUIRE(animated.is_animating());
    REQUIRE(animated.target_value() == 100);

    // Display value is animating from 50 toward 100
    REQUIRE(animated.display_value() >= 50);
    REQUIRE(animated.display_value() <= 100);

    animated.unbind();
    lv_subject_deinit(&subject);
}

TEST_CASE_METHOD(AnimatedValueTestFixture, "AnimatedValue: retargets on new value",
                 "[animated_value]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    int last_value = -1;
    AnimatedValue<int> animated;

    animated.bind(&subject, [&](int v) { last_value = v; }, {.duration_ms = 100, .threshold = 0});

    // Start animation to 100
    lv_subject_set_int(&subject, 100);
    REQUIRE(animated.target_value() == 100);

    // Retarget to 200 mid-animation
    lv_subject_set_int(&subject, 200);
    REQUIRE(animated.target_value() == 200);

    // Animation should still be running (chasing new target)
    REQUIRE(animated.is_animating());

    animated.unbind();
    lv_subject_deinit(&subject);
}

// ============================================================================
// Threshold Tests
// ============================================================================

TEST_CASE_METHOD(AnimatedValueTestFixture, "AnimatedValue: skips animation below threshold",
                 "[animated_value]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 100);

    int callback_count = 0;
    AnimatedValue<int> animated;

    animated.bind(&subject, [&](int) { callback_count++; },
                  {.duration_ms = 50, .threshold = 10}); // 10-unit threshold

    // Reset count after initial callback
    callback_count = 0;

    // Change by less than threshold - should be silent (no animation started)
    lv_subject_set_int(&subject, 105); // Only 5 units

    // No animation should be started for small changes
    REQUIRE(!animated.is_animating());
    REQUIRE(callback_count == 0);
    REQUIRE(animated.target_value() == 105);

    animated.unbind();
    lv_subject_deinit(&subject);
}

TEST_CASE_METHOD(AnimatedValueTestFixture, "AnimatedValue: animates at or above threshold",
                 "[animated_value]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 100);

    int callback_count = 0;
    AnimatedValue<int> animated;

    animated.bind(&subject, [&](int) { callback_count++; }, {.duration_ms = 50, .threshold = 10});

    // Reset count after initial callback
    callback_count = 0;

    // Change by exactly threshold
    lv_subject_set_int(&subject, 110); // Exactly 10 units

    // Animation should be started for changes >= threshold
    REQUIRE(animated.is_animating());
    REQUIRE(animated.target_value() == 110);

    animated.unbind();
    lv_subject_deinit(&subject);
}

// ============================================================================
// Animations Disabled Tests
// ============================================================================

TEST_CASE_METHOD(AnimatedValueTestFixture, "AnimatedValue: instant update when animations disabled",
                 "[animated_value]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    // Disable animations
    SettingsManager::instance().set_animations_enabled(false);

    int received_value = -1;
    AnimatedValue<int> animated;

    animated.bind(&subject, [&](int v) { received_value = v; }, {.duration_ms = 500});

    // Set new value - should be instant (no animation)
    lv_subject_set_int(&subject, 100);

    // No animation should be running - value should be set instantly
    REQUIRE(!animated.is_animating());
    REQUIRE(animated.display_value() == 100);
    REQUIRE(received_value == 100);

    // Re-enable animations for other tests
    SettingsManager::instance().set_animations_enabled(true);

    animated.unbind();
    lv_subject_deinit(&subject);
}

// ============================================================================
// RAII Cleanup Tests
// ============================================================================

TEST_CASE_METHOD(AnimatedValueTestFixture, "AnimatedValue: destructor cleans up",
                 "[animated_value]") {
    lv_subject_t subject;
    // Use non-zero initial value to trigger animation
    lv_subject_init_int(&subject, 50);

    bool callback_invoked = false;

    {
        AnimatedValue<int> animated;
        animated.bind(&subject, [&](int) { callback_invoked = true; }, {.duration_ms = 100});

        // Start animation
        lv_subject_set_int(&subject, 100);
        REQUIRE(animated.is_animating());

        // AnimatedValue goes out of scope here - should cleanup
    }

    // Resetting after destruction should work without issues
    callback_invoked = false;
    lv_subject_set_int(&subject, 200);

    // After destruction, callback should not be invoked
    REQUIRE(!callback_invoked);

    lv_subject_deinit(&subject);
}

TEST_CASE_METHOD(AnimatedValueTestFixture, "AnimatedValue: move construction transfers ownership",
                 "[animated_value]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 50);

    AnimatedValue<int> original;
    original.bind(&subject, [&](int) {});

    REQUIRE(original.is_bound());
    REQUIRE(original.display_value() == 50);

    // Move construct
    AnimatedValue<int> moved(std::move(original));

    // Original should be cleared
    REQUIRE(!original.is_bound());

    // Moved should have the state
    REQUIRE(moved.is_bound());
    REQUIRE(moved.display_value() == 50);
    REQUIRE(moved.target_value() == 50);

    // Note: After move, the observer's user_data still points to original's address.
    // Subject changes after move may not work correctly. This is a known limitation.
    // For proper move semantics, re-bind after move.

    moved.unbind();
    lv_subject_deinit(&subject);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE_METHOD(AnimatedValueTestFixture, "AnimatedValue: null subject bind is no-op",
                 "[animated_value][edge]") {
    AnimatedValue<int> animated;
    animated.bind(nullptr, [](int) {});

    REQUIRE(!animated.is_bound());
}

TEST_CASE_METHOD(AnimatedValueTestFixture, "AnimatedValue: null callback bind is no-op",
                 "[animated_value][edge]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    AnimatedValue<int> animated;
    animated.bind(&subject, nullptr);

    REQUIRE(!animated.is_bound());

    lv_subject_deinit(&subject);
}

TEST_CASE_METHOD(AnimatedValueTestFixture, "AnimatedValue: rebind cleans up previous binding",
                 "[animated_value]") {
    lv_subject_t subject1, subject2;
    lv_subject_init_int(&subject1, 10);
    lv_subject_init_int(&subject2, 20);

    int value = -1;
    AnimatedValue<int> animated;

    animated.bind(&subject1, [&](int v) { value = v; });
    REQUIRE(value == 10);

    // Rebind to different subject
    animated.bind(&subject2, [&](int v) { value = v * 2; });
    REQUIRE(value == 40); // 20 * 2

    // Changes to new subject should work
    lv_subject_set_int(&subject2, 50);
    REQUIRE(animated.target_value() == 50);
    REQUIRE(animated.is_animating());

    animated.unbind();
    lv_subject_deinit(&subject1);
    lv_subject_deinit(&subject2);
}
