/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "lvgl/lvgl.h"

// Color scheme constants
#define UI_COLOR_NAV_BG          lv_color_hex(0x242424)     // rgb(36, 36, 36) - Nav bar background
#define UI_COLOR_PRIMARY         lv_color_hex(0xFF4444)     // rgb(255, 68, 68) - Primary/Active color (matches nav icons)
#define UI_COLOR_ACCENT          UI_COLOR_PRIMARY           // Accent color for status indicators
#define UI_COLOR_PANEL_BG        lv_color_hex(0x141414)     // rgb(20, 20, 20) - Main panel background

// Text colors
#define UI_COLOR_TEXT_PRIMARY    lv_color_hex(0xFFFFFF)     // rgb(255, 255, 255) - White text
#define UI_COLOR_TEXT_SECONDARY  lv_color_hex(0xAAAAAA)     // rgb(170, 170, 170) - Gray text
#define UI_COLOR_TEXT_MUTED      lv_color_hex(0xAFAFAF)     // rgb(175, 175, 175) - Muted text

// Navigation colors
#define UI_COLOR_NAV_INACTIVE    lv_color_hex(0xC8C8C8)     // rgb(200, 200, 200) - Inactive nav icons

// Button colors
#define UI_COLOR_BUTTON_PRIMARY   UI_COLOR_PRIMARY          // Primary button (same as active nav)
#define UI_COLOR_BUTTON_SECONDARY lv_color_hex(0x4B4B4B)   // rgb(75, 75, 75) - Secondary button

// Layout constants
#define UI_NAV_WIDTH_PERCENT   10                          // Nav bar is 1/10th of screen width
#define UI_NAV_ICON_SIZE       64                          // Base icon size for 1024x800
#define UI_NAV_PADDING         16                          // Padding between elements

// Calculate nav width based on actual screen
#define UI_NAV_WIDTH(screen_w) ((screen_w) / 10)

// Screen size targets
#define UI_SCREEN_LARGE_W      1280
#define UI_SCREEN_LARGE_H      720
#define UI_SCREEN_MEDIUM_W     1024
#define UI_SCREEN_MEDIUM_H     600
#define UI_SCREEN_SMALL_W      800
#define UI_SCREEN_SMALL_H      480
#define UI_SCREEN_TINY_W       480
#define UI_SCREEN_TINY_H       320

// Padding constants (matching globals.xml values)
#define UI_PADDING_NORMAL      20  // padding_normal - standard padding
#define UI_PADDING_MEDIUM      12  // padding_medium - buttons, flex items
#define UI_PADDING_SMALL       10  // padding_small - compact layouts
#define UI_PADDING_TINY        6   // padding_tiny - very small screens

// Opacity constants (matching globals.xml values)
#define UI_DISABLED_OPA        128 // disabled_opa - 50% opacity for disabled/dimmed elements

// Responsive navigation bar sizing (applied in C++ based on screen height)
// Tiny screens (320px): 42px buttons, 0px padding
// With space_evenly: 6×42 = 252px buttons, 320 - 252 = 68px for 7 gaps (~9.7px each)
#define UI_NAV_BUTTON_SIZE_TINY    42
#define UI_NAV_PADDING_TINY        0

// Small screens (480px): 60px buttons, 8px padding = 6×60 + 2×8 = 376px total
#define UI_NAV_BUTTON_SIZE_SMALL   60
#define UI_NAV_PADDING_SMALL       8

// Medium screens (600px): 70px buttons, 12px padding
#define UI_NAV_BUTTON_SIZE_MEDIUM  70
#define UI_NAV_PADDING_MEDIUM      12

// Large screens (720px+): 70px buttons, 16px padding
#define UI_NAV_BUTTON_SIZE_LARGE   70
#define UI_NAV_PADDING_LARGE       16

// Responsive navigation bar width (applied in C++ based on screen size)
#define UI_NAV_WIDTH_TINY          64   // Tiny screens: 42px button + margins
#define UI_NAV_WIDTH_SMALL         76   // Small screens: 60px button + 8px padding each side
#define UI_NAV_WIDTH_MEDIUM        94   // Medium screens: 70px button + 12px padding each side
#define UI_NAV_WIDTH_LARGE         102  // Large screens: 70px button + 16px padding each side
