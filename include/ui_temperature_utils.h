// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl/lvgl.h"

#include <cstddef>

/**
 * @file ui_temperature_utils.h
 * @brief Shared temperature validation, formatting, and display utilities
 *
 * This module provides centralized temperature validation, clamping,
 * formatting, and color-coding logic used across multiple temperature-related
 * panels (controls/temp, filament, extrusion).
 *
 * ## Formatting Functions
 *
 * Use these for consistent temperature display across the UI:
 * - `format_temperature()` - Single temp: "210°C"
 * - `format_temperature_pair()` - Current/target: "210 / 245°C"
 *
 * ## Color-Coding Function
 *
 * Use `get_heating_state_color()` for consistent 4-state thermal feedback:
 * - Off (target=0): gray (text_secondary)
 * - Heating (current < target-2): red (primary_color)
 * - At-temp (within ±2): green (success_color)
 * - Cooling (current > target+2): blue (info_color)
 */

namespace helix {
namespace ui {
namespace temperature {

// ============================================================================
// Unit Conversion Functions
// ============================================================================

/**
 * @brief Converts centidegrees to degrees (integer)
 *
 * PrinterState stores temperatures as centidegrees (×10) for 0.1°C resolution.
 * Use this function for integer display (e.g., "210°C").
 *
 * @param centi Temperature in centidegrees (e.g., 2100 for 210°C)
 * @return Temperature in degrees (e.g., 210)
 */
inline int centi_to_degrees(int centi) {
    return centi / 10;
}

/**
 * @brief Converts centidegrees to degrees (float for precision display)
 *
 * Use this function when 0.1°C precision is needed (e.g., graph data points).
 *
 * @param centi Temperature in centidegrees (e.g., 2105 for 210.5°C)
 * @return Temperature in degrees (e.g., 210.5f)
 */
inline float centi_to_degrees_f(int centi) {
    return static_cast<float>(centi) / 10.0f;
}

/**
 * @brief Converts degrees to centidegrees
 *
 * Use when setting temperatures from user input (e.g., keyboard entry).
 *
 * @param degrees Temperature in degrees (e.g., 210)
 * @return Temperature in centidegrees (e.g., 2100)
 */
inline int degrees_to_centi(int degrees) {
    return degrees * 10;
}

// ============================================================================
// Validation Functions
// ============================================================================

/**
 * @brief Validates and clamps a temperature value to safe limits
 *
 * If the temperature is outside the valid range, it will be clamped to
 * the nearest valid value and a warning will be logged.
 *
 * @param temp Temperature value to validate (modified in-place if clamped)
 * @param min_temp Minimum valid temperature
 * @param max_temp Maximum valid temperature
 * @param context Logging context (e.g., "Temp", "Filament", "Extrusion")
 * @param temp_type Temperature type for logging (e.g., "current", "target")
 * @return true if temperature was valid, false if it was clamped
 */
bool validate_and_clamp(int& temp, int min_temp, int max_temp, const char* context,
                        const char* temp_type);

/**
 * @brief Validates and clamps a temperature pair (current + target)
 *
 * Convenience function that validates both current and target temperatures.
 *
 * @param current Current temperature (modified in-place if clamped)
 * @param target Target temperature (modified in-place if clamped)
 * @param min_temp Minimum valid temperature
 * @param max_temp Maximum valid temperature
 * @param context Logging context (e.g., "Temp", "Filament")
 * @return true if both temperatures were valid, false if either was clamped
 */
bool validate_and_clamp_pair(int& current, int& target, int min_temp, int max_temp,
                             const char* context);

/**
 * @brief Checks if the current temperature is safe for extrusion
 *
 * Extrusion operations require the nozzle to be at or above a minimum
 * temperature (typically 170°C) to avoid damaging the extruder.
 *
 * @param current_temp Current nozzle temperature
 * @param min_extrusion_temp Minimum safe extrusion temperature
 * @return true if safe to extrude, false otherwise
 */
bool is_extrusion_safe(int current_temp, int min_extrusion_temp);

/**
 * @brief Gets a human-readable safety status message
 *
 * @param current_temp Current nozzle temperature
 * @param min_extrusion_temp Minimum safe extrusion temperature
 * @return Status message (e.g., "Ready" or "Heating (45°C below minimum)")
 */
const char* get_extrusion_safety_status(int current_temp, int min_extrusion_temp);

// ============================================================================
// Formatting Functions
// ============================================================================

/**
 * @brief Format a temperature value with degree symbol
 *
 * Formats as "210°C" for consistent display across the UI.
 *
 * @param temp Temperature in degrees (not centidegrees)
 * @param buffer Output buffer
 * @param buffer_size Size of buffer (recommended: 16)
 * @return Pointer to buffer for chaining convenience
 *
 * @code{.cpp}
 * char buf[16];
 * lv_label_set_text(label, format_temperature(210, buf, sizeof(buf)));
 * @endcode
 */
char* format_temperature(int temp, char* buffer, size_t buffer_size);

/**
 * @brief Format a current/target temperature pair
 *
 * Formats as "210 / 245°C" or "210 / --°C" when target is 0 (heater off).
 *
 * @param current Current temperature in degrees
 * @param target Target temperature in degrees (0 = heater off, shows "--")
 * @param buffer Output buffer
 * @param buffer_size Size of buffer (recommended: 24)
 * @return Pointer to buffer for chaining convenience
 */
char* format_temperature_pair(int current, int target, char* buffer, size_t buffer_size);

/**
 * @brief Format a temperature value with one decimal place
 *
 * Formats as "210.5°C" for precision display (graphs, PID tuning).
 *
 * @param temp Temperature in degrees (float)
 * @param buffer Output buffer
 * @param buffer_size Size of buffer (recommended: 16)
 * @return Pointer to buffer for chaining convenience
 */
char* format_temperature_f(float temp, char* buffer, size_t buffer_size);

/**
 * @brief Format a float current/target temperature pair
 *
 * Formats as "210.5 / 215.0°C" or "180.5 / --°C" when target is 0.
 *
 * @param current Current temperature in degrees (float)
 * @param target Target temperature in degrees (float, 0 = heater off)
 * @param buffer Output buffer
 * @param buffer_size Size of buffer (recommended: 32)
 * @return Pointer to buffer for chaining convenience
 */
char* format_temperature_pair_f(float current, float target, char* buffer, size_t buffer_size);

/**
 * @brief Format a temperature range for material specs
 *
 * Formats as "200-230°C" for AMS material temperature ranges.
 *
 * @param min_temp Minimum temperature in degrees
 * @param max_temp Maximum temperature in degrees
 * @param buffer Output buffer
 * @param buffer_size Size of buffer (recommended: 16)
 * @return Pointer to buffer for chaining convenience
 */
char* format_temperature_range(int min_temp, int max_temp, char* buffer, size_t buffer_size);

// ============================================================================
// Display Color Functions
// ============================================================================

/** Default tolerance for "at temperature" state detection (±degrees) */
constexpr int DEFAULT_AT_TEMP_TOLERANCE = 2;

/**
 * @brief Get theme color for temperature display based on 4-state heating logic
 *
 * Returns a color indicating the thermal state of a heater:
 * - **Off** (target=0): text_secondary (gray) - heater disabled
 * - **Heating** (current < target - tolerance): primary_color (red) - actively heating
 * - **At-temp** (within ±tolerance): success_color (green) - stable at target
 * - **Cooling** (current > target + tolerance): info_color (blue) - cooling down
 *
 * This function provides consistent color-coding across all temperature displays
 * (temp_display widget, filament panel, etc.).
 *
 * @param current_deg Current temperature in degrees
 * @param target_deg Target temperature in degrees (0 = heater off)
 * @param tolerance Degrees tolerance for "at temp" state (default: 2)
 * @return lv_color_t Theme color based on thermal state
 *
 * @code{.cpp}
 * lv_color_t color = get_heating_state_color(nozzle_current, nozzle_target);
 * lv_obj_set_style_text_color(temp_label, color, LV_PART_MAIN);
 * @endcode
 */
lv_color_t get_heating_state_color(int current_deg, int target_deg,
                                   int tolerance = DEFAULT_AT_TEMP_TOLERANCE);

} // namespace temperature
} // namespace ui
} // namespace helix
