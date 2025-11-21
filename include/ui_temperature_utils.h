// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 HelixScreen Contributors
/**
 * @file ui_temperature_utils.h
 * @brief Shared temperature validation and safety utilities
 *
 * This module provides centralized temperature validation, clamping, and
 * safety checking logic used across multiple temperature-related panels
 * (controls/temp, filament, extrusion).
 */

#ifndef UI_TEMPERATURE_UTILS_H
#define UI_TEMPERATURE_UTILS_H

/**
 * @brief Temperature validation and safety utilities
 */
namespace UITemperatureUtils {

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
bool validate_and_clamp(int& temp, int min_temp, int max_temp,
                       const char* context, const char* temp_type);

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
bool validate_and_clamp_pair(int& current, int& target,
                             int min_temp, int max_temp,
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

} // namespace UITemperatureUtils

#endif // UI_TEMPERATURE_UTILS_H
