// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace helix {

/// Write pixel data as an LVGL 9 binary image file (.bin)
/// Uses atomic write (temp file + rename) to prevent corruption.
/// @param path Output file path
/// @param width Image width in pixels
/// @param height Image height in pixels
/// @param color_format LVGL color format (e.g., LV_COLOR_FORMAT_ARGB8888 = 0x10)
/// @param pixel_data Raw pixel data
/// @param data_size Size of pixel_data in bytes
/// @return true on success
bool write_lvgl_bin(const std::string& path, int width, int height, uint8_t color_format,
                    const uint8_t* pixel_data, size_t data_size);

} // namespace helix
