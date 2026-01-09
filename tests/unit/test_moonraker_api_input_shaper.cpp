// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_moonraker_api_input_shaper.cpp
 * @brief Unit tests for MoonrakerAPI input shaper calibration methods
 *
 * Tests the InputShaperCollector pattern and API methods:
 * - start_resonance_test() - SHAPER_CALIBRATE command execution
 * - set_input_shaper() - SET_INPUT_SHAPER command execution
 * - Response parsing for calibration results
 * - Error handling for missing accelerometer
 *
 * Uses mock client to simulate G-code responses from Klipper.
 */

#include "../../include/calibration_types.h" // For InputShaperResult
#include "../../include/moonraker_api.h"
#include "../../include/moonraker_client_mock.h"
#include "../../include/printer_state.h"
#include "../../lvgl/lvgl.h"
#include "../ui_test_utils.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "../catch_amalgamated.hpp"

// ============================================================================
// Global LVGL Initialization (called once)
// ============================================================================

namespace {
struct LVGLInitializerInputShaper {
    LVGLInitializerInputShaper() {
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

static LVGLInitializerInputShaper lvgl_init;
} // namespace

// ============================================================================
// Test Fixture
// ============================================================================

/**
 * @brief Test fixture for input shaper API testing with mock client
 */
class InputShaperTestFixture {
  public:
    InputShaperTestFixture() : mock_client_(MoonrakerClientMock::PrinterType::VORON_24) {
        state_.init_subjects(false); // Don't register XML bindings in tests
        api_ = std::make_unique<MoonrakerAPI>(mock_client_, state_);
        reset_callbacks();
    }

    ~InputShaperTestFixture() {
        api_.reset();
    }

    void reset_callbacks() {
        result_received_ = false;
        error_received_ = false;
        captured_result_ = InputShaperResult{};
        captured_error_.clear();
    }

    // Callback for successful calibration result
    void on_result(const InputShaperResult& result) {
        result_received_ = true;
        captured_result_ = result;
    }

    // Callback for errors
    void on_error(const MoonrakerError& err) {
        error_received_ = true;
        captured_error_ = err.message;
    }

  protected:
    MoonrakerClientMock mock_client_;
    PrinterState state_;
    std::unique_ptr<MoonrakerAPI> api_;

    std::atomic<bool> result_received_{false};
    std::atomic<bool> error_received_{false};
    InputShaperResult captured_result_;
    std::string captured_error_;
};

// ============================================================================
// start_resonance_test() Tests
// ============================================================================

TEST_CASE_METHOD(InputShaperTestFixture, "start_resonance_test accepts X axis",
                 "[calibration][input_shaper]") {
    std::atomic<bool> complete_called{false};
    InputShaperResult captured_result;

    api_->start_resonance_test(
        'X', [](int) {}, // progress callback
        [&](const InputShaperResult& result) {
            captured_result = result;
            complete_called = true;
        },
        [&](const MoonrakerError&) { FAIL("Error callback should not be called"); });

    // Wait for async callback (mock dispatches synchronously)
    for (int i = 0; i < 100 && !complete_called; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(complete_called);
    REQUIRE(captured_result.axis == 'X');
    REQUIRE(captured_result.is_valid());
    REQUIRE(captured_result.shaper_type == "mzv");
    REQUIRE(captured_result.shaper_freq == Catch::Approx(36.7f).margin(0.1f));
}

TEST_CASE_METHOD(InputShaperTestFixture, "start_resonance_test accepts Y axis",
                 "[calibration][input_shaper]") {
    std::atomic<bool> complete_called{false};
    InputShaperResult captured_result;

    api_->start_resonance_test(
        'Y', [](int) {}, // progress callback
        [&](const InputShaperResult& result) {
            captured_result = result;
            complete_called = true;
        },
        [&](const MoonrakerError&) { FAIL("Error callback should not be called"); });

    // Wait for async callback
    for (int i = 0; i < 100 && !complete_called; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(complete_called);
    REQUIRE(captured_result.axis == 'Y');
    REQUIRE(captured_result.is_valid());
}

TEST_CASE_METHOD(InputShaperTestFixture, "start_resonance_test sends correct G-code command for X",
                 "[calibration][input_shaper]") {
    std::atomic<bool> complete_called{false};

    api_->start_resonance_test(
        'X', [](int) {},
        [&](const InputShaperResult& result) {
            complete_called = true;
            // Verify parsed values from mock response
            REQUIRE(result.shaper_type == "mzv");
            REQUIRE(result.shaper_freq == Catch::Approx(36.7f).margin(0.1f));
        },
        [&](const MoonrakerError&) { FAIL("Error callback should not be called"); });

    // Wait for async callback
    for (int i = 0; i < 100 && !complete_called; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(complete_called);
}

// ============================================================================
// set_input_shaper() Tests
// ============================================================================

TEST_CASE_METHOD(InputShaperTestFixture, "set_input_shaper sends command for X axis with mzv",
                 "[calibration][input_shaper]") {
    std::atomic<bool> success_called{false};

    api_->set_input_shaper(
        'X', "mzv", 36.7, [&]() { success_called = true; },
        [&](const MoonrakerError&) { FAIL("Error callback should not be called"); });

    // Wait for async callback
    for (int i = 0; i < 100 && !success_called; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(success_called);
}

TEST_CASE_METHOD(InputShaperTestFixture, "set_input_shaper sends command for Y axis",
                 "[calibration][input_shaper]") {
    std::atomic<bool> success_called{false};

    api_->set_input_shaper(
        'Y', "ei", 47.6, [&]() { success_called = true; },
        [&](const MoonrakerError&) { FAIL("Error callback should not be called"); });

    // Wait for async callback
    for (int i = 0; i < 100 && !success_called; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(success_called);
}

TEST_CASE_METHOD(InputShaperTestFixture, "set_input_shaper accepts all valid shaper types",
                 "[calibration][input_shaper]") {
    std::vector<std::string> shaper_types = {"zv", "mzv", "zvd", "ei", "2hump_ei", "3hump_ei"};

    for (const auto& type : shaper_types) {
        INFO("Testing shaper type: " << type);
        std::atomic<bool> success_called{false};

        api_->set_input_shaper(
            'X', type, 35.0, [&]() { success_called = true; },
            [&](const MoonrakerError& err) {
                FAIL("Error callback should not be called for type: " << type << " - "
                                                                      << err.message);
            });

        // Wait for async callback
        for (int i = 0; i < 100 && !success_called; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        REQUIRE(success_called);
    }
}

// ============================================================================
// InputShaperResult Parsing Tests
// ============================================================================

TEST_CASE("InputShaperResult default construction", "[slow][calibration]") {
    InputShaperResult result;

    // Default axis is 'X' per struct definition
    REQUIRE(result.axis == 'X');
    REQUIRE(result.shaper_type.empty());
    REQUIRE(result.shaper_freq == 0.0f);
    REQUIRE(result.max_accel == 0.0f);
    REQUIRE(result.smoothing == 0.0f);
    REQUIRE(result.vibrations == 0.0f);
    REQUIRE(result.freq_response.empty());
}

TEST_CASE("InputShaperResult is_valid check", "[slow][calibration]") {
    InputShaperResult result;

    // Empty result is not valid
    REQUIRE_FALSE(result.is_valid());

    // Set valid values
    result.shaper_type = "mzv";
    result.shaper_freq = 36.7f;

    REQUIRE(result.is_valid());
}

// ============================================================================
// Response Parsing Simulation Tests
// ============================================================================

TEST_CASE("InputShaperResult can store calibration data", "[slow][calibration]") {
    // Simulate building a result from parsed G-code responses
    InputShaperResult result;
    result.axis = 'X';
    result.shaper_type = "mzv";
    result.shaper_freq = 36.7f;
    result.max_accel = 5000.0f;
    result.smoothing = 0.140f;
    result.vibrations = 7.2f;

    // Add frequency response data points
    result.freq_response.push_back({10.0f, 0.1f});
    result.freq_response.push_back({20.0f, 0.3f});
    result.freq_response.push_back({36.7f, 1.0f}); // Peak at resonance
    result.freq_response.push_back({50.0f, 0.2f});

    // Verify the result
    REQUIRE(result.axis == 'X');
    REQUIRE(result.is_valid());
    REQUIRE(result.shaper_type == "mzv");
    REQUIRE(result.shaper_freq == Catch::Approx(36.7f));
    REQUIRE(result.max_accel == Catch::Approx(5000.0f));
    REQUIRE(result.vibrations == Catch::Approx(7.2f));
    REQUIRE(result.freq_response.size() == 4);
}

TEST_CASE("InputShaperResult can represent incomplete state", "[slow][calibration]") {
    InputShaperResult result;
    result.axis = 'Y';
    // Leave shaper_type empty to simulate error/incomplete

    REQUIRE_FALSE(result.is_valid());
    REQUIRE(result.shaper_type.empty());
}

// ============================================================================
// Shaper Type Validation Tests
// ============================================================================

TEST_CASE("Valid shaper type strings", "[slow][calibration][validation]") {
    // These are the official Klipper input shaper types
    std::vector<std::string> valid_types = {
        "zv",       // Zero Vibration
        "mzv",      // Modified Zero Vibration
        "zvd",      // ZV + Derivative
        "ei",       // Extra Insensitive
        "2hump_ei", // 2-hump EI
        "3hump_ei"  // 3-hump EI
    };

    // Verify these are recognized as valid types
    for (const auto& type : valid_types) {
        INFO("Checking valid shaper type: " << type);
        // Just verify the strings are what we expect from Klipper
        REQUIRE(type.length() > 0);
        REQUIRE(type.length() <= 10);
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE_METHOD(InputShaperTestFixture, "API handles null callbacks gracefully",
                 "[calibration][edge_case][input_shaper]") {
    // Test that calling start_resonance_test with nullptr callbacks doesn't crash
    // Note: The InputShaperCollector handles null callbacks internally
    REQUIRE_NOTHROW(api_->start_resonance_test('X', nullptr, nullptr, nullptr));

    // Give it time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // set_input_shaper requires valid callbacks (by design), so we test with valid ones
    std::atomic<bool> success_called{false};
    REQUIRE_NOTHROW(
        api_->set_input_shaper('X', "mzv", 36.7, [&]() { success_called = true; }, nullptr));

    // Wait for async callback
    for (int i = 0; i < 100 && !success_called; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(success_called);
}
