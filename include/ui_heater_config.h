// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file ui_heater_config.h
 * @brief Shared heater configuration structure and utilities
 *
 * This module provides a unified configuration structure for heaters (nozzle, bed)
 * and helper functions to eliminate duplicate setup code across temperature panels.
 */

#pragma once

#include "lvgl/lvgl.h"

/**
 * @brief Heater type enumeration
 */
namespace helix {
enum class HeaterType { Nozzle = 0, Bed = 1, Chamber = 2 };
constexpr int HEATER_TYPE_COUNT = 3;
} // namespace helix

/**
 * @brief Heater configuration structure
 *
 * This structure encapsulates all configuration needed for a heater panel,
 * including display colors, temperature ranges, presets, and keypad ranges.
 */
typedef struct {
    helix::HeaterType type; ///< Heater type (nozzle or bed)
    const char* name;       ///< Short name (e.g., "nozzle", "bed")
    const char* title;      ///< Display title (e.g., "Nozzle Temperature")
    lv_color_t color;       ///< Theme color for this heater
    float temp_range_max;   ///< Maximum temperature for graph Y-axis
    int y_axis_increment;   ///< Y-axis label increment (e.g., 50°C, 100°C)

    struct {
        int off;  ///< "Off" preset (0°C)
        int pla;  ///< PLA preset
        int petg; ///< PETG preset
        int abs;  ///< ABS preset
    } presets;

    struct {
        float min; ///< Minimum keypad input value
        float max; ///< Maximum keypad input value
    } keypad_range;
} heater_config_t;
