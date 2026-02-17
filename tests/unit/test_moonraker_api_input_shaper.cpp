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

using namespace helix;
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
    for (int i = 0; i < 200 && !complete_called; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(complete_called);
    REQUIRE(captured_result.axis == 'X');
    REQUIRE(captured_result.is_valid());
    REQUIRE(captured_result.shaper_type == "mzv");
    REQUIRE(captured_result.shaper_freq == Catch::Approx(53.8f).margin(0.1f));
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
    for (int i = 0; i < 200 && !complete_called; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
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
            REQUIRE(result.shaper_freq == Catch::Approx(53.8f).margin(0.1f));
        },
        [&](const MoonrakerError&) { FAIL("Error callback should not be called"); });

    // Wait for async callback
    for (int i = 0; i < 200 && !complete_called; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
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
    for (int i = 0; i < 200 && !success_called; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
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
    for (int i = 0; i < 200 && !success_called; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
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
        for (int i = 0; i < 200 && !success_called; ++i) {
            lv_tick_inc(100);
            lv_timer_handler_safe();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
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

    // Pump LVGL timers to let the timer-based mock dispatch complete
    for (int i = 0; i < 50; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // set_input_shaper requires valid callbacks (by design), so we test with valid ones
    std::atomic<bool> success_called{false};
    REQUIRE_NOTHROW(
        api_->set_input_shaper('X', "mzv", 36.7, [&]() { success_called = true; }, nullptr));

    // Wait for async callback
    for (int i = 0; i < 200 && !success_called; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(success_called);
}

// ============================================================================
// Phase 1: New API Methods and Enhanced Results
// ============================================================================
//
// These tests are written BEFORE implementation (test-first methodology).
// They will FAIL to compile or link until the corresponding types and methods
// are implemented in:
//   - include/calibration_types.h (ShaperOption, InputShaperConfig)
//   - include/moonraker_api.h (measure_axes_noise, get_input_shaper_config)
//   - src/api/moonraker_api_advanced.cpp (NoiseCheckCollector, enhanced collector)
//   - src/api/moonraker_client_mock.cpp (mock implementations)
// ============================================================================

// ----------------------------------------------------------------------------
// New Types Tests
// ----------------------------------------------------------------------------

TEST_CASE("ShaperOption struct", "[input_shaper][types]") {
    SECTION("default construction") {
        ShaperOption option;

        // Default values should be zeroed/empty
        CHECK(option.type.empty());
        CHECK(option.frequency == 0.0f);
        CHECK(option.vibrations == 0.0f);
        CHECK(option.smoothing == 0.0f);
        CHECK(option.max_accel == 0.0f);
    }

    SECTION("can store fitted shaper data") {
        ShaperOption option;
        option.type = "mzv";
        option.frequency = 36.7f;
        option.vibrations = 7.2f;
        option.smoothing = 0.140f;
        option.max_accel = 5000.0f;

        REQUIRE(option.type == "mzv");
        REQUIRE(option.frequency == Catch::Approx(36.7f));
        REQUIRE(option.vibrations == Catch::Approx(7.2f));
        REQUIRE(option.smoothing == Catch::Approx(0.140f));
        REQUIRE(option.max_accel == Catch::Approx(5000.0f));
    }
}

TEST_CASE("InputShaperConfig struct", "[input_shaper][types]") {
    SECTION("default construction") {
        InputShaperConfig config;

        // Default should indicate unconfigured state
        CHECK(config.shaper_type_x.empty());
        CHECK(config.shaper_freq_x == 0.0f);
        CHECK(config.shaper_type_y.empty());
        CHECK(config.shaper_freq_y == 0.0f);
        CHECK(config.damping_ratio_x == 0.0f);
        CHECK(config.damping_ratio_y == 0.0f);
        CHECK_FALSE(config.is_configured);
    }

    SECTION("can store configured shaper settings") {
        InputShaperConfig config;
        config.shaper_type_x = "mzv";
        config.shaper_freq_x = 36.7f;
        config.shaper_type_y = "ei";
        config.shaper_freq_y = 47.6f;
        config.damping_ratio_x = 0.1f;
        config.damping_ratio_y = 0.1f;
        config.is_configured = true;

        REQUIRE(config.is_configured);
        REQUIRE(config.shaper_type_x == "mzv");
        REQUIRE(config.shaper_freq_x == Catch::Approx(36.7f));
        REQUIRE(config.shaper_type_y == "ei");
        REQUIRE(config.shaper_freq_y == Catch::Approx(47.6f));
    }
}

// ----------------------------------------------------------------------------
// Enhanced InputShaperResult Tests (all_shapers vector)
// ----------------------------------------------------------------------------

TEST_CASE("InputShaperResult has all_shapers vector", "[input_shaper][types]") {
    InputShaperResult result;

    // The all_shapers vector should exist and be empty by default
    REQUIRE(result.all_shapers.empty());

    // Should be able to add shaper options
    ShaperOption zv;
    zv.type = "zv";
    zv.frequency = 35.8f;
    zv.vibrations = 22.7f;
    zv.smoothing = 0.100f;

    ShaperOption mzv;
    mzv.type = "mzv";
    mzv.frequency = 36.7f;
    mzv.vibrations = 7.2f;
    mzv.smoothing = 0.140f;

    result.all_shapers.push_back(zv);
    result.all_shapers.push_back(mzv);

    REQUIRE(result.all_shapers.size() == 2);
    REQUIRE(result.all_shapers[0].type == "zv");
    REQUIRE(result.all_shapers[1].type == "mzv");
}

TEST_CASE_METHOD(InputShaperTestFixture, "start_resonance_test returns all shaper alternatives",
                 "[calibration][input_shaper]") {
    // The mock dispatches 3 shaper options: zv, mzv, ei
    // After enhancement, the result should contain ALL fitted shapers in all_shapers
    std::atomic<bool> complete_called{false};
    InputShaperResult captured_result;

    api_->start_resonance_test(
        'X', [](int) {}, // progress callback
        [&](const InputShaperResult& result) {
            captured_result = result;
            complete_called = true;
        },
        [&](const MoonrakerError& err) {
            FAIL("Error callback should not be called: " << err.message);
        });

    // Wait for async callback
    for (int i = 0; i < 200 && !complete_called; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(complete_called);
    REQUIRE(captured_result.is_valid());

    // Mock now outputs 5 fitted shapers with realistic values from AD5M
    REQUIRE(captured_result.all_shapers.size() == 5);

    // Verify the mock data matches expected values
    // Mock outputs: zv@59.0, mzv@53.8, ei@56.2, 2hump_ei@71.8, 3hump_ei@89.6

    auto find_shaper = [&](const std::string& type) -> const ShaperOption* {
        for (const auto& s : captured_result.all_shapers) {
            if (s.type == type)
                return &s;
        }
        return nullptr;
    };

    const ShaperOption* zv = find_shaper("zv");
    REQUIRE(zv != nullptr);
    CHECK(zv->frequency == Catch::Approx(59.0f).margin(0.1f));
    CHECK(zv->vibrations == Catch::Approx(5.2f).margin(0.1f));
    CHECK(zv->smoothing == Catch::Approx(0.045f).margin(0.01f));
    CHECK(zv->max_accel == Catch::Approx(13400.0f).margin(1.0f));

    const ShaperOption* mzv = find_shaper("mzv");
    REQUIRE(mzv != nullptr);
    CHECK(mzv->frequency == Catch::Approx(53.8f).margin(0.1f));
    CHECK(mzv->vibrations == Catch::Approx(1.6f).margin(0.1f));
    CHECK(mzv->smoothing == Catch::Approx(0.130f).margin(0.01f));
    CHECK(mzv->max_accel == Catch::Approx(4000.0f).margin(1.0f));

    const ShaperOption* ei = find_shaper("ei");
    REQUIRE(ei != nullptr);
    CHECK(ei->frequency == Catch::Approx(56.2f).margin(0.1f));
    CHECK(ei->vibrations == Catch::Approx(0.7f).margin(0.1f));
    CHECK(ei->smoothing == Catch::Approx(0.120f).margin(0.01f));
    CHECK(ei->max_accel == Catch::Approx(4600.0f).margin(1.0f));

    const ShaperOption* two_hump = find_shaper("2hump_ei");
    REQUIRE(two_hump != nullptr);
    CHECK(two_hump->frequency == Catch::Approx(71.8f).margin(0.1f));
    CHECK(two_hump->max_accel == Catch::Approx(8800.0f).margin(1.0f));

    const ShaperOption* three_hump = find_shaper("3hump_ei");
    REQUIRE(three_hump != nullptr);
    CHECK(three_hump->frequency == Catch::Approx(89.6f).margin(0.1f));
    CHECK(three_hump->max_accel == Catch::Approx(8800.0f).margin(1.0f));
}

// ----------------------------------------------------------------------------
// measure_axes_noise() Tests
// ----------------------------------------------------------------------------

TEST_CASE_METHOD(InputShaperTestFixture, "measure_axes_noise returns noise level",
                 "[calibration][input_shaper]") {
    // measure_axes_noise() runs MEASURE_AXES_NOISE G-code
    // Klipper output: "Axes noise for xy-axis accelerometer: 12.3 (x), 15.7 (y), 8.2 (z)"
    // Returns max(x, y) as overall noise level

    std::atomic<bool> complete_called{false};
    float captured_noise = -1.0f;

    api_->measure_axes_noise(
        [&](float noise_level) {
            captured_noise = noise_level;
            complete_called = true;
        },
        [&](const MoonrakerError& err) {
            FAIL("Error callback should not be called: " << err.message);
        });

    // Wait for async callback
    for (int i = 0; i < 200 && !complete_called; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(complete_called);
    // Mock dispatches: x=12.345678, y=15.678901, z=8.234567
    // Collector returns max(x, y) = 15.678901
    CHECK(captured_noise == Catch::Approx(15.678901f).margin(0.01f));
}

TEST_CASE_METHOD(InputShaperTestFixture, "measure_axes_noise handles no accelerometer error",
                 "[calibration][input_shaper]") {
    // When no ADXL is configured, MEASURE_AXES_NOISE should fail
    // This test requires the mock to be configured to simulate missing accelerometer
    // For now, we test the error callback path exists

    // Configure mock to simulate no accelerometer (implementation detail)
    mock_client_.set_accelerometer_available(false);

    std::atomic<bool> error_called{false};
    std::string captured_error;

    api_->measure_axes_noise(
        [&](float) { FAIL("Success callback should not be called when accelerometer missing"); },
        [&](const MoonrakerError& err) {
            error_called = true;
            captured_error = err.message;
        });

    // Wait for async callback
    for (int i = 0; i < 200 && !error_called; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(error_called);
    // Error message should mention accelerometer/ADXL
    CHECK((captured_error.find("accelerometer") != std::string::npos ||
           captured_error.find("ADXL") != std::string::npos ||
           captured_error.find("adxl") != std::string::npos));
}

// ----------------------------------------------------------------------------
// get_input_shaper_config() Tests
// ----------------------------------------------------------------------------

TEST_CASE_METHOD(InputShaperTestFixture, "get_input_shaper_config returns current settings",
                 "[calibration][input_shaper]") {
    // get_input_shaper_config() queries the current input shaper configuration
    // from printer state (via Moonraker's printer.objects.query)

    std::atomic<bool> complete_called{false};
    InputShaperConfig captured_config;

    api_->get_input_shaper_config(
        [&](const InputShaperConfig& config) {
            captured_config = config;
            complete_called = true;
        },
        [&](const MoonrakerError& err) {
            FAIL("Error callback should not be called: " << err.message);
        });

    // Wait for async callback
    for (int i = 0; i < 200 && !complete_called; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(complete_called);

    // Mock returns config from configfile.config.input_shaper (string values)
    // Expected: mzv@36.7Hz for X, ei@47.6Hz for Y
    REQUIRE(captured_config.is_configured);
    CHECK(captured_config.shaper_type_x == "mzv");
    CHECK(captured_config.shaper_freq_x == Catch::Approx(36.7f).margin(0.1f));
    CHECK(captured_config.shaper_type_y == "ei");
    CHECK(captured_config.shaper_freq_y == Catch::Approx(47.6f).margin(0.1f));
}

TEST_CASE_METHOD(InputShaperTestFixture, "get_input_shaper_config handles unconfigured shaper",
                 "[calibration][input_shaper]") {
    // When no input shaper is configured, is_configured should be false
    // This requires mock to simulate unconfigured state

    // Configure mock to simulate unconfigured input shaper
    mock_client_.set_input_shaper_configured(false);

    std::atomic<bool> complete_called{false};
    InputShaperConfig captured_config;

    api_->get_input_shaper_config(
        [&](const InputShaperConfig& config) {
            captured_config = config;
            complete_called = true;
        },
        [&](const MoonrakerError& err) {
            FAIL("Error callback should not be called: " << err.message);
        });

    // Wait for async callback
    for (int i = 0; i < 200 && !complete_called; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(complete_called);
    REQUIRE_FALSE(captured_config.is_configured);
}

// ============================================================================
// Enhanced Collector: New Recommendation Format + Max Accel + CSV Path
// ============================================================================

TEST_CASE_METHOD(InputShaperTestFixture, "collector parses new Klipper recommendation format",
                 "[calibration][input_shaper]") {
    std::atomic<bool> complete_called{false};
    InputShaperResult captured_result;

    api_->start_resonance_test(
        'X', [](int) {},
        [&](const InputShaperResult& result) {
            captured_result = result;
            complete_called = true;
        },
        [&](const MoonrakerError& err) {
            FAIL("Error callback should not be called: " << err.message);
        });

    for (int i = 0; i < 200 && !complete_called; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(complete_called);
    // New mock uses "Recommended shaper_type_x = mzv, shaper_freq_x = 53.8 Hz" format
    CHECK(captured_result.shaper_type == "mzv");
    CHECK(captured_result.shaper_freq == Catch::Approx(53.8f).margin(0.1f));
}

TEST_CASE_METHOD(InputShaperTestFixture, "collector parses max_accel per shaper",
                 "[calibration][input_shaper]") {
    std::atomic<bool> complete_called{false};
    InputShaperResult captured_result;

    api_->start_resonance_test(
        'X', [](int) {},
        [&](const InputShaperResult& result) {
            captured_result = result;
            complete_called = true;
        },
        [&](const MoonrakerError& err) { FAIL("Error: " << err.message); });

    for (int i = 0; i < 200 && !complete_called; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(complete_called);
    REQUIRE(captured_result.all_shapers.size() == 5);

    // Verify max_accel is parsed for each shaper
    auto find_shaper = [&](const std::string& type) -> const ShaperOption* {
        for (const auto& s : captured_result.all_shapers) {
            if (s.type == type)
                return &s;
        }
        return nullptr;
    };

    const ShaperOption* zv = find_shaper("zv");
    REQUIRE(zv != nullptr);
    CHECK(zv->max_accel == Catch::Approx(13400.0f).margin(1.0f));

    const ShaperOption* mzv = find_shaper("mzv");
    REQUIRE(mzv != nullptr);
    CHECK(mzv->max_accel == Catch::Approx(4000.0f).margin(1.0f));

    const ShaperOption* ei = find_shaper("ei");
    REQUIRE(ei != nullptr);
    CHECK(ei->max_accel == Catch::Approx(4600.0f).margin(1.0f));

    // Recommended shaper's max_accel should be on the result itself
    CHECK(captured_result.max_accel == Catch::Approx(4000.0f).margin(1.0f));
}

TEST_CASE_METHOD(InputShaperTestFixture, "collector captures CSV path",
                 "[calibration][input_shaper]") {
    std::atomic<bool> complete_called{false};
    InputShaperResult captured_result;

    api_->start_resonance_test(
        'X', [](int) {},
        [&](const InputShaperResult& result) {
            captured_result = result;
            complete_called = true;
        },
        [&](const MoonrakerError& err) { FAIL("Error: " << err.message); });

    for (int i = 0; i < 200 && !complete_called; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(complete_called);
    CHECK(captured_result.csv_path == "/tmp/calibration_data_x_mock.csv");
}

TEST_CASE_METHOD(InputShaperTestFixture, "collector emits progress callbacks during sweep",
                 "[calibration][input_shaper]") {
    std::atomic<bool> complete_called{false};
    std::vector<int> progress_values;

    api_->start_resonance_test(
        'X', [&](int percent) { progress_values.push_back(percent); },
        [&](const InputShaperResult&) { complete_called = true; },
        [&](const MoonrakerError& err) { FAIL("Error: " << err.message); });

    for (int i = 0; i < 200 && !complete_called; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(complete_called);
    // Should have received progress callbacks during sweep + calculation + completion
    CHECK(progress_values.size() > 10); // At least sweep lines + shaper fits + 100%

    // Progress should be monotonically non-decreasing
    for (size_t i = 1; i < progress_values.size(); ++i) {
        CHECK(progress_values[i] >= progress_values[i - 1]);
    }

    // Should start low (sweep) and end at 100
    CHECK(progress_values.front() <= 55); // Sweep range
    CHECK(progress_values.back() == 100); // Completion
}

TEST_CASE_METHOD(InputShaperTestFixture,
                 "collector returns 5 shaper alternatives with updated mock",
                 "[calibration][input_shaper]") {
    std::atomic<bool> complete_called{false};
    InputShaperResult captured_result;

    api_->start_resonance_test(
        'Y', [](int) {},
        [&](const InputShaperResult& result) {
            captured_result = result;
            complete_called = true;
        },
        [&](const MoonrakerError& err) { FAIL("Error: " << err.message); });

    for (int i = 0; i < 200 && !complete_called; ++i) {
        lv_tick_inc(100);
        lv_timer_handler_safe();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(complete_called);
    REQUIRE(captured_result.axis == 'Y');
    REQUIRE(captured_result.all_shapers.size() == 5);

    // Verify all 5 shaper types from the updated mock
    std::vector<std::string> expected_types = {"zv", "mzv", "ei", "2hump_ei", "3hump_ei"};
    for (size_t i = 0; i < expected_types.size(); ++i) {
        CHECK(captured_result.all_shapers[i].type == expected_types[i]);
        CHECK(captured_result.all_shapers[i].frequency > 0.0f);
        CHECK(captured_result.all_shapers[i].max_accel > 0.0f);
    }

    // Y axis CSV path should use 'y'
    CHECK(captured_result.csv_path == "/tmp/calibration_data_y_mock.csv");
}
