// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 356C LLC

#include "touch_calibration.h"

#include "../catch_amalgamated.hpp"

using Catch::Approx;
using namespace helix;

// ============================================================================
// Coefficient Computation Tests
// ============================================================================

TEST_CASE("TouchCalibration: identity transformation", "[touch-calibration][compute]") {
    // When screen points equal touch points, coefficients should give identity
    // identity: a=1, b=0, c=0, d=0, e=1, f=0
    Point screen_points[3] = {{0, 0}, {100, 0}, {0, 100}};
    Point touch_points[3] = {{0, 0}, {100, 0}, {0, 100}};

    TouchCalibration cal;
    bool result = compute_calibration(screen_points, touch_points, cal);

    REQUIRE(result == true);
    REQUIRE(cal.valid == true);
    REQUIRE(cal.a == Approx(1.0f));
    REQUIRE(cal.b == Approx(0.0f));
    REQUIRE(cal.c == Approx(0.0f));
    REQUIRE(cal.d == Approx(0.0f));
    REQUIRE(cal.e == Approx(1.0f));
    REQUIRE(cal.f == Approx(0.0f));
}

TEST_CASE("TouchCalibration: simple scaling", "[touch-calibration][compute]") {
    // Touch range 0-1000 maps to screen 0-800 x 0-480
    Point screen_points[3] = {{0, 0}, {800, 0}, {0, 480}};
    Point touch_points[3] = {{0, 0}, {1000, 0}, {0, 1000}};

    TouchCalibration cal;
    bool result = compute_calibration(screen_points, touch_points, cal);

    REQUIRE(result == true);
    REQUIRE(cal.valid == true);

    // Verify transformation produces correct screen coordinates
    Point p1 = transform_point(cal, {1000, 0});
    REQUIRE(p1.x == Approx(800).margin(1));
    REQUIRE(p1.y == Approx(0).margin(1));

    Point p2 = transform_point(cal, {0, 1000});
    REQUIRE(p2.x == Approx(0).margin(1));
    REQUIRE(p2.y == Approx(480).margin(1));

    Point p3 = transform_point(cal, {500, 500});
    REQUIRE(p3.x == Approx(400).margin(1));
    REQUIRE(p3.y == Approx(240).margin(1));
}

TEST_CASE("TouchCalibration: translation offset", "[touch-calibration][compute]") {
    // Touch 0,0 maps to screen 100,100 (offset mapping)
    Point screen_points[3] = {{100, 100}, {700, 100}, {100, 380}};
    Point touch_points[3] = {{0, 0}, {600, 0}, {0, 280}};

    TouchCalibration cal;
    bool result = compute_calibration(screen_points, touch_points, cal);

    REQUIRE(result == true);
    REQUIRE(cal.valid == true);

    // transform(0, 0) should give (100, 100)
    Point p = transform_point(cal, {0, 0});
    REQUIRE(p.x == Approx(100).margin(1));
    REQUIRE(p.y == Approx(100).margin(1));

    // transform(600, 0) should give (700, 100)
    Point p2 = transform_point(cal, {600, 0});
    REQUIRE(p2.x == Approx(700).margin(1));
    REQUIRE(p2.y == Approx(100).margin(1));

    // transform(0, 280) should give (100, 380)
    Point p3 = transform_point(cal, {0, 280});
    REQUIRE(p3.x == Approx(100).margin(1));
    REQUIRE(p3.y == Approx(380).margin(1));
}

TEST_CASE("TouchCalibration: AD5M-like calibration", "[touch-calibration][compute][ad5m]") {
    // Real-world scenario: 800x480 screen with 15% inset calibration points
    // Screen points at 15% inset from edges
    Point screen_points[3] = {
        {120, 144}, // 15% from left, 30% from top
        {400, 408}, // center-ish X, 85% from top
        {680, 72}   // 85% from left, 15% from top
    };

    // Simulated raw touch values from resistive touchscreen
    Point touch_points[3] = {
        {500, 3200}, // Top-left region
        {2040, 900}, // Bottom-center region
        {3580, 3500} // Top-right region
    };

    TouchCalibration cal;
    bool result = compute_calibration(screen_points, touch_points, cal);

    REQUIRE(result == true);
    REQUIRE(cal.valid == true);

    // Verify the calibration points transform correctly
    Point p1 = transform_point(cal, {500, 3200});
    REQUIRE(p1.x == Approx(120).margin(2));
    REQUIRE(p1.y == Approx(144).margin(2));

    Point p2 = transform_point(cal, {2040, 900});
    REQUIRE(p2.x == Approx(400).margin(2));
    REQUIRE(p2.y == Approx(408).margin(2));

    Point p3 = transform_point(cal, {3580, 3500});
    REQUIRE(p3.x == Approx(680).margin(2));
    REQUIRE(p3.y == Approx(72).margin(2));
}

TEST_CASE("TouchCalibration: Y-axis inversion", "[touch-calibration][compute]") {
    // Common on resistive touchscreens: raw Y increases but screen Y decreases
    // Screen: origin at top-left, Y increases downward
    // Touch: origin at bottom-left, Y increases upward
    Point screen_points[3] = {{0, 0}, {800, 0}, {0, 480}};
    Point touch_points[3] = {{0, 480}, {800, 480}, {0, 0}}; // Y inverted

    TouchCalibration cal;
    bool result = compute_calibration(screen_points, touch_points, cal);

    REQUIRE(result == true);
    REQUIRE(cal.valid == true);

    // Touch (0, 480) -> Screen (0, 0)
    Point p1 = transform_point(cal, {0, 480});
    REQUIRE(p1.x == Approx(0).margin(1));
    REQUIRE(p1.y == Approx(0).margin(1));

    // Touch (0, 0) -> Screen (0, 480)
    Point p2 = transform_point(cal, {0, 0});
    REQUIRE(p2.x == Approx(0).margin(1));
    REQUIRE(p2.y == Approx(480).margin(1));

    // Touch (400, 240) -> Screen (400, 240) - center stays center
    Point p3 = transform_point(cal, {400, 240});
    REQUIRE(p3.x == Approx(400).margin(1));
    REQUIRE(p3.y == Approx(240).margin(1));
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST_CASE("TouchCalibration: degenerate points - collinear", "[touch-calibration][edge]") {
    // All three touch points on a line - cannot compute unique transform
    Point screen_points[3] = {{0, 0}, {100, 100}, {200, 200}};
    Point touch_points[3] = {{0, 0}, {100, 100}, {200, 200}}; // All on diagonal

    TouchCalibration cal;
    bool result = compute_calibration(screen_points, touch_points, cal);

    REQUIRE(result == false);
    REQUIRE(cal.valid == false);
}

TEST_CASE("TouchCalibration: degenerate points - duplicates", "[touch-calibration][edge]") {
    // Two identical touch points
    Point screen_points[3] = {{0, 0}, {100, 0}, {0, 100}};
    Point touch_points[3] = {{50, 50}, {50, 50}, {100, 100}}; // First two identical

    TouchCalibration cal;
    bool result = compute_calibration(screen_points, touch_points, cal);

    REQUIRE(result == false);
    REQUIRE(cal.valid == false);
}

TEST_CASE("TouchCalibration: degenerate points - nearly collinear", "[touch-calibration][edge]") {
    // Points almost on a line - should detect and fail
    Point screen_points[3] = {{0, 0}, {100, 100}, {200, 201}}; // Third point barely off line
    Point touch_points[3] = {{0, 0}, {100, 100}, {200, 200}};  // Collinear

    TouchCalibration cal;
    bool result = compute_calibration(screen_points, touch_points, cal);

    REQUIRE(result == false);
    REQUIRE(cal.valid == false);
}

// ============================================================================
// Point Transformation Tests
// ============================================================================

TEST_CASE("TouchCalibration: transform maintains precision", "[touch-calibration][transform]") {
    // Set up a known scaling transformation
    Point screen_points[3] = {{0, 0}, {100, 0}, {0, 100}};
    Point touch_points[3] = {{0, 0}, {200, 0}, {0, 200}}; // 2x touch range

    TouchCalibration cal;
    REQUIRE(compute_calibration(screen_points, touch_points, cal) == true);

    // Test various points
    SECTION("origin") {
        Point p = transform_point(cal, {0, 0});
        REQUIRE(p.x == Approx(0).margin(1));
        REQUIRE(p.y == Approx(0).margin(1));
    }

    SECTION("max x") {
        Point p = transform_point(cal, {200, 0});
        REQUIRE(p.x == Approx(100).margin(1));
        REQUIRE(p.y == Approx(0).margin(1));
    }

    SECTION("max y") {
        Point p = transform_point(cal, {0, 200});
        REQUIRE(p.x == Approx(0).margin(1));
        REQUIRE(p.y == Approx(100).margin(1));
    }

    SECTION("center") {
        Point p = transform_point(cal, {100, 100});
        REQUIRE(p.x == Approx(50).margin(1));
        REQUIRE(p.y == Approx(50).margin(1));
    }
}

TEST_CASE("TouchCalibration: transform with rotation", "[touch-calibration][transform]") {
    // 90-degree rotation: touch X becomes screen Y, touch Y becomes -screen X
    Point screen_points[3] = {{0, 0}, {0, 100}, {100, 0}}; // Rotated
    Point touch_points[3] = {{0, 0}, {100, 0}, {0, 100}};  // Normal

    TouchCalibration cal;
    bool result = compute_calibration(screen_points, touch_points, cal);

    REQUIRE(result == true);
    REQUIRE(cal.valid == true);

    // Touch (100, 0) -> Screen (0, 100)
    Point p1 = transform_point(cal, {100, 0});
    REQUIRE(p1.x == Approx(0).margin(1));
    REQUIRE(p1.y == Approx(100).margin(1));

    // Touch (0, 100) -> Screen (100, 0)
    Point p2 = transform_point(cal, {0, 100});
    REQUIRE(p2.x == Approx(100).margin(1));
    REQUIRE(p2.y == Approx(0).margin(1));
}

TEST_CASE("TouchCalibration: transform extrapolation beyond calibration points",
          "[touch-calibration][transform]") {
    // Verify transform works for points outside the calibration triangle
    Point screen_points[3] = {{100, 100}, {200, 100}, {100, 200}};
    Point touch_points[3] = {{100, 100}, {200, 100}, {100, 200}}; // Identity at offset

    TouchCalibration cal;
    REQUIRE(compute_calibration(screen_points, touch_points, cal) == true);

    // Point outside calibration triangle
    Point p = transform_point(cal, {300, 300});
    REQUIRE(p.x == Approx(300).margin(1));
    REQUIRE(p.y == Approx(300).margin(1));

    // Point at origin (outside triangle)
    Point p2 = transform_point(cal, {0, 0});
    REQUIRE(p2.x == Approx(0).margin(1));
    REQUIRE(p2.y == Approx(0).margin(1));
}

// ============================================================================
// Coefficient Validation Tests
// ============================================================================

TEST_CASE("TouchCalibration: coefficient values for known transforms",
          "[touch-calibration][coefficients]") {
    SECTION("pure X scaling by 0.8") {
        // screen_x = 0.8 * touch_x + 0 * touch_y + 0
        // screen_y = 0 * touch_x + 1 * touch_y + 0
        Point screen_points[3] = {{0, 0}, {80, 0}, {0, 100}};
        Point touch_points[3] = {{0, 0}, {100, 0}, {0, 100}};

        TouchCalibration cal;
        REQUIRE(compute_calibration(screen_points, touch_points, cal) == true);

        REQUIRE(cal.a == Approx(0.8f).margin(0.001f));
        REQUIRE(cal.b == Approx(0.0f).margin(0.001f));
        REQUIRE(cal.c == Approx(0.0f).margin(0.001f));
        REQUIRE(cal.d == Approx(0.0f).margin(0.001f));
        REQUIRE(cal.e == Approx(1.0f).margin(0.001f));
        REQUIRE(cal.f == Approx(0.0f).margin(0.001f));
    }

    SECTION("pure Y scaling by 0.48") {
        // screen_x = 1 * touch_x + 0 * touch_y + 0
        // screen_y = 0 * touch_x + 0.48 * touch_y + 0
        Point screen_points[3] = {{0, 0}, {100, 0}, {0, 48}};
        Point touch_points[3] = {{0, 0}, {100, 0}, {0, 100}};

        TouchCalibration cal;
        REQUIRE(compute_calibration(screen_points, touch_points, cal) == true);

        REQUIRE(cal.a == Approx(1.0f).margin(0.001f));
        REQUIRE(cal.b == Approx(0.0f).margin(0.001f));
        REQUIRE(cal.c == Approx(0.0f).margin(0.001f));
        REQUIRE(cal.d == Approx(0.0f).margin(0.001f));
        REQUIRE(cal.e == Approx(0.48f).margin(0.001f));
        REQUIRE(cal.f == Approx(0.0f).margin(0.001f));
    }

    SECTION("pure translation") {
        // screen_x = 1 * touch_x + 0 * touch_y + 50
        // screen_y = 0 * touch_x + 1 * touch_y + 30
        Point screen_points[3] = {{50, 30}, {150, 30}, {50, 130}};
        Point touch_points[3] = {{0, 0}, {100, 0}, {0, 100}};

        TouchCalibration cal;
        REQUIRE(compute_calibration(screen_points, touch_points, cal) == true);

        REQUIRE(cal.a == Approx(1.0f).margin(0.001f));
        REQUIRE(cal.b == Approx(0.0f).margin(0.001f));
        REQUIRE(cal.c == Approx(50.0f).margin(0.001f));
        REQUIRE(cal.d == Approx(0.0f).margin(0.001f));
        REQUIRE(cal.e == Approx(1.0f).margin(0.001f));
        REQUIRE(cal.f == Approx(30.0f).margin(0.001f));
    }
}

// ============================================================================
// Invalid Calibration State Tests
// ============================================================================

TEST_CASE("TouchCalibration: default state is invalid", "[touch-calibration][state]") {
    TouchCalibration cal;
    REQUIRE(cal.valid == false);
}

TEST_CASE("TouchCalibration: transform with invalid calibration", "[touch-calibration][state]") {
    TouchCalibration cal;
    cal.valid = false;

    // Transformation with invalid calibration should return input unchanged
    // (or some sensible default behavior)
    Point raw = {500, 300};
    Point result = transform_point(cal, raw);

    // Expected behavior: return input unchanged when calibration is invalid
    REQUIRE(result.x == raw.x);
    REQUIRE(result.y == raw.y);
}

// ============================================================================
// USB Input Device Detection Tests
// ============================================================================

TEST_CASE("TouchCalibration: USB input phys detection", "[touch-calibration][usb-detect]") {
    SECTION("typical USB HID touchscreen") {
        // BTT HDMI touchscreens, Waveshare, etc.
        REQUIRE(is_usb_input_phys("usb-0000:01:00.0-1.3/input0") == true);
    }

    SECTION("USB with different bus format") {
        REQUIRE(is_usb_input_phys("usb-3f980000.usb-1.2/input0") == true);
    }

    SECTION("platform resistive touchscreen (empty phys)") {
        // AD5M sun4i_ts has empty phys
        REQUIRE(is_usb_input_phys("") == false);
    }

    SECTION("platform resistive touchscreen (named phys)") {
        REQUIRE(is_usb_input_phys("sun4i_ts") == false);
    }

    SECTION("I2C capacitive touchscreen") {
        // Goodix/FocalTech over I2C
        REQUIRE(is_usb_input_phys("i2c-1/1-005d") == false);
    }

    SECTION("SPI touchscreen") {
        REQUIRE(is_usb_input_phys("spi0.0/input0") == false);
    }

    SECTION("USB composite device with touch") {
        REQUIRE(is_usb_input_phys("usb-xhci-hcd.0-1/input1") == true);
    }
}

// ============================================================================
// Known Touchscreen Name Detection Tests
// ============================================================================

TEST_CASE("TouchCalibration: known touchscreen name detection",
          "[touch-calibration][touchscreen-detect]") {
    // --- Real touchscreen controllers should match ---

    SECTION("AD5M sun4i resistive touchscreen") {
        REQUIRE(is_known_touchscreen_name("sun4i-ts") == true);
    }

    SECTION("Goodix capacitive touchscreen") {
        REQUIRE(is_known_touchscreen_name("Goodix Capacitive TouchScreen") == true);
    }

    SECTION("FocalTech FT5x touchscreen") {
        REQUIRE(is_known_touchscreen_name("ft5x06_ts") == true);
    }

    SECTION("Goodix GT911 touchscreen") {
        REQUIRE(is_known_touchscreen_name("gt911") == true);
    }

    SECTION("ILI2130 touchscreen") {
        REQUIRE(is_known_touchscreen_name("ili2130_ts") == true);
    }

    SECTION("generic touch device") {
        REQUIRE(is_known_touchscreen_name("Generic Touchscreen") == true);
    }

    SECTION("EDT FocalTech display") {
        REQUIRE(is_known_touchscreen_name("edt-ft5x06") == true);
    }

    SECTION("case insensitive matching") {
        REQUIRE(is_known_touchscreen_name("GOODIX Touch") == true);
        REQUIRE(is_known_touchscreen_name("SUN4I-TS") == true);
    }

    // --- Non-touch devices must NOT match ---

    SECTION("HDMI CEC remote control") {
        REQUIRE(is_known_touchscreen_name("vc4-hdmi") == false);
    }

    SECTION("HDMI CEC variant") {
        REQUIRE(is_known_touchscreen_name("vc4-hdmi HDMI Jack") == false);
    }

    SECTION("generic keyboard") {
        REQUIRE(is_known_touchscreen_name("AT Translated Set 2 keyboard") == false);
    }

    SECTION("USB mouse") {
        REQUIRE(is_known_touchscreen_name("Logitech USB Mouse") == false);
    }

    SECTION("power button") {
        REQUIRE(is_known_touchscreen_name("Power Button") == false);
    }

    SECTION("GPIO keys") {
        REQUIRE(is_known_touchscreen_name("gpio-keys") == false);
    }

    SECTION("empty name") {
        REQUIRE(is_known_touchscreen_name("") == false);
    }

    SECTION("IR remote") {
        REQUIRE(is_known_touchscreen_name("rc-cec") == false);
    }
}

// ============================================================================
// Unified Calibration Decision Tests (device_needs_calibration)
// ============================================================================

// ============================================================================
// Resistive Touchscreen Detection Tests (is_resistive_touchscreen_name)
// ============================================================================

TEST_CASE("TouchCalibration: is_resistive_touchscreen_name",
          "[touch-calibration][resistive-detection]") {
    // --- Resistive controllers that NEED calibration ---

    SECTION("sun4i resistive (AD5M)") {
        REQUIRE(is_resistive_touchscreen_name("sun4i-ts") == true);
    }

    SECTION("resistive touch panel") {
        REQUIRE(is_resistive_touchscreen_name("rtp") == true);
    }

    SECTION("touch screen controller") {
        REQUIRE(is_resistive_touchscreen_name("tsc2046") == true);
    }

    SECTION("case insensitive") {
        REQUIRE(is_resistive_touchscreen_name("SUN4I-TS") == true);
    }

    // --- Capacitive controllers that do NOT need calibration ---

    SECTION("Goodix capacitive") {
        REQUIRE(is_resistive_touchscreen_name("Goodix Capacitive TouchScreen") == false);
    }

    SECTION("Goodix GT911") {
        REQUIRE(is_resistive_touchscreen_name("gt911") == false);
    }

    SECTION("FocalTech capacitive") {
        REQUIRE(is_resistive_touchscreen_name("ft5x06_ts") == false);
    }

    SECTION("ILI capacitive") {
        REQUIRE(is_resistive_touchscreen_name("ili2130_ts") == false);
    }

    SECTION("EDT FocalTech") {
        REQUIRE(is_resistive_touchscreen_name("edt-ft5x06") == false);
    }

    SECTION("Atmel capacitive") {
        REQUIRE(is_resistive_touchscreen_name("atmel_mxt_ts") == false);
    }
}

// ============================================================================
// Unified Calibration Decision Tests (device_needs_calibration)
// ============================================================================

TEST_CASE("TouchCalibration: device_needs_calibration",
          "[touch-calibration][calibration-decision]") {
    // --- Devices that NEED calibration (resistive touchscreens only) ---

    SECTION("AD5M sun4i resistive touchscreen needs calibration") {
        // Platform resistive touchscreen: has ABS, not USB, resistive controller
        REQUIRE(device_needs_calibration("sun4i-ts", "sun4i_ts", true) == true);
    }

    SECTION("Generic resistive touch panel needs calibration") {
        REQUIRE(device_needs_calibration("rtp", "", true) == true);
    }

    // --- Capacitive touchscreens do NOT need calibration ---

    SECTION("Goodix I2C capacitive (BTT HDMI7) does not need calibration") {
        // I2C Goodix: has ABS, not USB, but capacitive — factory-calibrated
        REQUIRE(device_needs_calibration("Goodix Capacitive TouchScreen", "", true) == false);
    }

    SECTION("Goodix GT911 I2C does not need calibration") {
        REQUIRE(device_needs_calibration("gt911", "", true) == false);
    }

    SECTION("FocalTech capacitive does not need calibration") {
        REQUIRE(device_needs_calibration("ft5x06_ts", "", true) == false);
    }

    SECTION("EDT FocalTech display does not need calibration") {
        REQUIRE(device_needs_calibration("edt-ft5x06", "", true) == false);
    }

    // --- USB devices do NOT need calibration ---

    SECTION("USB HID touchscreen (BTT HDMI5) does not need calibration") {
        // USB touchscreen: has ABS, IS USB → no calibration
        REQUIRE(device_needs_calibration("BIQU BTT-HDMI5", "usb-5101400.usb-1/input0", true) ==
                false);
    }

    SECTION("USB HID generic touchscreen does not need calibration") {
        REQUIRE(device_needs_calibration("USB Touchscreen", "usb-0000:01:00.0-1.3/input0", true) ==
                false);
    }

    // --- Other non-calibration devices ---

    SECTION("Virtual touchscreen (VNC uinput) does not need calibration") {
        // Virtual device: has ABS, not USB, but name contains "virtual"
        REQUIRE(device_needs_calibration("virtual-touchscreen", "", true) == false);
    }

    SECTION("HDMI CEC remote does not need calibration") {
        // CEC remote: no ABS capabilities
        REQUIRE(device_needs_calibration("vc4-hdmi", "vc4-hdmi/input0", false) == false);
    }

    SECTION("HDMI audio jack does not need calibration") {
        REQUIRE(device_needs_calibration("vc4-hdmi HDMI Jack", "ALSA", false) == false);
    }

    SECTION("Device without ABS never needs calibration") {
        // Even a known touchscreen name without ABS should not trigger calibration
        REQUIRE(device_needs_calibration("Goodix Touch", "", false) == false);
    }

    SECTION("Unknown device with ABS does not need calibration") {
        // Has ABS but unrecognized name → safer to skip (not a known touchscreen)
        REQUIRE(device_needs_calibration("Random Input Device", "", true) == false);
    }

    SECTION("Keyboard does not need calibration") {
        REQUIRE(device_needs_calibration("AT Translated Set 2 keyboard", "", false) == false);
    }

    SECTION("USB mouse does not need calibration") {
        REQUIRE(device_needs_calibration("Logitech USB Mouse", "usb-0000:00:14.0-1/input0",
                                         false) == false);
    }

    SECTION("Empty device does not need calibration") {
        REQUIRE(device_needs_calibration("", "", false) == false);
    }

    SECTION("GPIO keys do not need calibration") {
        REQUIRE(device_needs_calibration("gpio-keys", "", false) == false);
    }
}
