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

// Screen size targets
#define UI_SCREEN_LARGE_W      1280
#define UI_SCREEN_LARGE_H      720
#define UI_SCREEN_MEDIUM_W     1024
#define UI_SCREEN_MEDIUM_H     800
#define UI_SCREEN_SMALL_W      800
#define UI_SCREEN_SMALL_H      480
#define UI_SCREEN_TINY_W       480
#define UI_SCREEN_TINY_H       400

// Calculate nav width based on actual screen
#define UI_NAV_WIDTH(screen_w) ((screen_w) / 10)

