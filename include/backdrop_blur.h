// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"

#include <cstdint>

namespace helix::ui {

/// Returns an lv_image widget containing a blurred snapshot of the current
/// screen, or nullptr if blur is unavailable or permanently disabled.
/// On failure, permanently disables blur for the rest of the app lifecycle
/// (circuit breaker pattern).
///
/// @param parent  Parent object for the image widget
/// @param dim_opacity  Opacity of the dark tint overlay (0-255)
/// @return Image widget with blurred backdrop, or nullptr (caller should fall back)
lv_obj_t* create_blurred_backdrop(lv_obj_t* parent, lv_opa_t dim_opacity);

/// Free cached GPU resources (shaders, FBOs, textures).
/// Also resets the circuit breaker, allowing blur to be retried.
/// Call on shutdown or display resize.
void backdrop_blur_cleanup();

// ---- Internal helpers exposed for testing ----

namespace detail {

/// Box blur a single-channel or multi-channel ARGB8888 buffer in-place.
/// @param data     Pixel buffer (ARGB8888 format, 4 bytes per pixel)
/// @param width    Image width in pixels
/// @param height   Image height in pixels
/// @param iterations  Number of box blur passes (3 ≈ Gaussian σ≈2.5)
void box_blur_argb8888(uint8_t* data, int width, int height, int iterations = 3);

/// Downscale an ARGB8888 buffer by 2x using 2x2 averaging.
/// Caller must allocate dst with (width/2) * (height/2) * 4 bytes.
/// @param src       Source pixel buffer
/// @param dst       Destination pixel buffer (half dimensions)
/// @param src_width  Source width (must be even)
/// @param src_height Source height (must be even)
void downscale_2x_argb8888(const uint8_t* src, uint8_t* dst, int src_width, int src_height);

/// Reset the circuit breaker (for testing only).
void reset_circuit_breaker();

/// Check if blur is permanently disabled.
bool is_blur_disabled();

} // namespace detail
} // namespace helix::ui
