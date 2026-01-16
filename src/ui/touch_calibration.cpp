// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 356C LLC

#include "touch_calibration.h"

#include <cmath>

namespace helix {

bool compute_calibration(const Point screen_points[3], const Point touch_points[3],
                         TouchCalibration& out) {
    // Initialize output to invalid state
    out.valid = false;
    out.a = 1.0f;
    out.b = 0.0f;
    out.c = 0.0f;
    out.d = 0.0f;
    out.e = 1.0f;
    out.f = 0.0f;

    // Extract touch coordinates for readability
    float Xt1 = static_cast<float>(touch_points[0].x);
    float Yt1 = static_cast<float>(touch_points[0].y);
    float Xt2 = static_cast<float>(touch_points[1].x);
    float Yt2 = static_cast<float>(touch_points[1].y);
    float Xt3 = static_cast<float>(touch_points[2].x);
    float Yt3 = static_cast<float>(touch_points[2].y);

    // Extract screen coordinates for readability
    float Xs1 = static_cast<float>(screen_points[0].x);
    float Ys1 = static_cast<float>(screen_points[0].y);
    float Xs2 = static_cast<float>(screen_points[1].x);
    float Ys2 = static_cast<float>(screen_points[1].y);
    float Xs3 = static_cast<float>(screen_points[2].x);
    float Ys3 = static_cast<float>(screen_points[2].y);

    // Compute divisor (determinant) using Maxim Integrated AN5296 algorithm
    // Reference: https://pdfserv.maximintegrated.com/en/an/AN5296.pdf
    // div = (Xt1-Xt3)*(Yt2-Yt3) - (Xt2-Xt3)*(Yt1-Yt3)
    float div = (Xt1 - Xt3) * (Yt2 - Yt3) - (Xt2 - Xt3) * (Yt1 - Yt3);

    // Check for degenerate case (collinear or duplicate points)
    // Use scale-relative epsilon based on coordinate magnitudes.
    // For typical touchscreens (12-bit ADC, 0-4095 range), valid triangles
    // produce determinants >> 1000, so 0.01% of max coordinate is safe.
    float max_coord = std::max(
        {std::abs(Xt1), std::abs(Yt1), std::abs(Xt2), std::abs(Yt2), std::abs(Xt3), std::abs(Yt3)});
    float epsilon = std::max(1.0f, max_coord * 0.0001f);
    if (std::abs(div) < epsilon) {
        return false;
    }

    // Compute affine transformation coefficients
    // screen_x = a*touch_x + b*touch_y + c
    out.a = ((Xs1 - Xs3) * (Yt2 - Yt3) - (Xs2 - Xs3) * (Yt1 - Yt3)) / div;
    out.b = ((Xt1 - Xt3) * (Xs2 - Xs3) - (Xt2 - Xt3) * (Xs1 - Xs3)) / div;
    out.c = Xs1 - out.a * Xt1 - out.b * Yt1;

    // screen_y = d*touch_x + e*touch_y + f
    out.d = ((Ys1 - Ys3) * (Yt2 - Yt3) - (Ys2 - Ys3) * (Yt1 - Yt3)) / div;
    out.e = ((Xt1 - Xt3) * (Ys2 - Ys3) - (Xt2 - Xt3) * (Ys1 - Ys3)) / div;
    out.f = Ys1 - out.d * Xt1 - out.e * Yt1;

    out.valid = true;
    return true;
}

Point transform_point(const TouchCalibration& cal, Point raw, int max_x, int max_y) {
    // If calibration is invalid, return input unchanged
    if (!cal.valid) {
        return raw;
    }

    // Apply affine transformation with rounding
    float raw_x = static_cast<float>(raw.x);
    float raw_y = static_cast<float>(raw.y);

    Point result;
    result.x = static_cast<int>(std::round(cal.a * raw_x + cal.b * raw_y + cal.c));
    result.y = static_cast<int>(std::round(cal.d * raw_x + cal.e * raw_y + cal.f));

    // Clamp to screen bounds if specified (prevents out-of-bounds coordinates
    // from corrupted calibration data)
    if (max_x > 0) {
        result.x = std::max(0, std::min(result.x, max_x));
    }
    if (max_y > 0) {
        result.y = std::max(0, std::min(result.y, max_y));
    }

    return result;
}

bool is_calibration_valid(const TouchCalibration& cal) {
    if (!cal.valid) {
        return false;
    }

    // Check all coefficients are finite (not NaN or Infinity)
    if (!std::isfinite(cal.a) || !std::isfinite(cal.b) || !std::isfinite(cal.c) ||
        !std::isfinite(cal.d) || !std::isfinite(cal.e) || !std::isfinite(cal.f)) {
        return false;
    }

    // Check coefficients are within reasonable bounds
    if (std::abs(cal.a) > MAX_CALIBRATION_COEFFICIENT ||
        std::abs(cal.b) > MAX_CALIBRATION_COEFFICIENT ||
        std::abs(cal.c) > MAX_CALIBRATION_COEFFICIENT ||
        std::abs(cal.d) > MAX_CALIBRATION_COEFFICIENT ||
        std::abs(cal.e) > MAX_CALIBRATION_COEFFICIENT ||
        std::abs(cal.f) > MAX_CALIBRATION_COEFFICIENT) {
        return false;
    }

    return true;
}

} // namespace helix
