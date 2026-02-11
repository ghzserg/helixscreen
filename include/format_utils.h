// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>

namespace helix::fmt {

// =============================================================================
// Constants
// =============================================================================

/**
 * @brief Unavailable/unknown value placeholder (em dash)
 *
 * Use this constant for consistent display of unavailable data across the UI.
 * Example: sensor disconnected, value not yet received, etc.
 */
inline constexpr const char* UNAVAILABLE = "—";

// =============================================================================
// Percentage Formatting
// =============================================================================

/**
 * @brief Format integer percentage with % suffix
 *
 * @param percent Percentage value (0-100 typically, but any int supported)
 * @param buf Output buffer
 * @param size Buffer size (recommended: 8)
 * @return Pointer to buffer for chaining
 */
char* format_percent(int percent, char* buf, size_t size);

/**
 * @brief Format percentage or show UNAVAILABLE when data is missing
 *
 * @param percent Percentage value
 * @param available Whether the data is available
 * @param buf Output buffer
 * @param size Buffer size
 * @return Pointer to buffer for chaining
 */
char* format_percent_or_unavailable(int percent, bool available, char* buf, size_t size);

/**
 * @brief Format float percentage with configurable decimal places
 *
 * @param percent Percentage value (float)
 * @param decimals Decimal places (0-2)
 * @param buf Output buffer
 * @param size Buffer size (recommended: 12)
 * @return Pointer to buffer for chaining
 */
char* format_percent_float(double percent, int decimals, char* buf, size_t size);

/**
 * @brief Format humidity from x10 value (e.g., 455 → "45%")
 *
 * Sensors often report humidity as value×10 for 0.1% resolution.
 * This function divides by 10 and displays as whole percent.
 *
 * @param humidity_x10 Humidity × 10 (e.g., 455 for 45.5%)
 * @param buf Output buffer
 * @param size Buffer size (recommended: 8)
 * @return Pointer to buffer for chaining
 */
char* format_humidity(int humidity_x10, char* buf, size_t size);

// =============================================================================
// Distance/Length Formatting
// =============================================================================

/**
 * @brief Format distance in millimeters with configurable precision
 *
 * @param mm Distance in millimeters
 * @param precision Decimal places (0-3)
 * @param buf Output buffer
 * @param size Buffer size (recommended: 16)
 * @return Pointer to buffer for chaining
 */
char* format_distance_mm(double mm, int precision, char* buf, size_t size);

/**
 * @brief Format filament diameter (always 2 decimal places)
 *
 * Convenience function for common 1.75mm/2.85mm filament diameters.
 *
 * @param mm Diameter in millimeters
 * @param buf Output buffer
 * @param size Buffer size (recommended: 16)
 * @return Pointer to buffer for chaining
 */
char* format_diameter_mm(float mm, char* buf, size_t size);

// =============================================================================
// Speed Formatting
// =============================================================================

/**
 * @brief Format speed in mm/s (whole number)
 *
 * @param speed Speed in mm/s
 * @param buf Output buffer
 * @param size Buffer size (recommended: 16)
 * @return Pointer to buffer for chaining
 */
char* format_speed_mm_s(double speed, char* buf, size_t size);

/**
 * @brief Format speed in mm/min (whole number)
 *
 * @param speed Speed in mm/min
 * @param buf Output buffer
 * @param size Buffer size (recommended: 16)
 * @return Pointer to buffer for chaining
 */
char* format_speed_mm_min(double speed, char* buf, size_t size);

// =============================================================================
// Acceleration Formatting
// =============================================================================

/**
 * @brief Format acceleration in mm/s²
 *
 * @param accel Acceleration in mm/s²
 * @param buf Output buffer
 * @param size Buffer size (recommended: 16)
 * @return Pointer to buffer for chaining
 */
char* format_accel_mm_s2(double accel, char* buf, size_t size);

// =============================================================================
// Frequency Formatting
// =============================================================================

/**
 * @brief Format frequency in Hz (1 decimal place)
 *
 * @param hz Frequency in Hz
 * @param buf Output buffer
 * @param size Buffer size (recommended: 16)
 * @return Pointer to buffer for chaining
 */
char* format_frequency_hz(double hz, char* buf, size_t size);

// =============================================================================
// Temperature Formatting
// =============================================================================

/**
 * @brief Format single temperature with °C suffix
 *
 * Produces consistent output like "210°C" (no space before °C).
 *
 * @param temp_c Temperature in degrees Celsius
 * @param buf Output buffer
 * @param size Buffer size (recommended: 16)
 * @return Pointer to buffer for chaining
 */
char* format_temp(int temp_c, char* buf, size_t size);

/**
 * @brief Format temperature pair as "current / target°C"
 *
 * Produces output like "150 / 200°C" or "150 / —°C" when target is 0 (heater off).
 *
 * @param current_c Current temperature in degrees Celsius
 * @param target_c Target temperature in degrees Celsius (0 = heater off, shows "—")
 * @param buf Output buffer
 * @param size Buffer size (recommended: 24)
 * @return Pointer to buffer for chaining
 */
char* format_temp_pair(int current_c, int target_c, char* buf, size_t size);

/**
 * @brief Format temperature range as "min-max°C"
 *
 * Produces output like "200-230°C" for material temperature ranges.
 *
 * @param min_c Minimum temperature in degrees Celsius
 * @param max_c Maximum temperature in degrees Celsius
 * @param buf Output buffer
 * @param size Buffer size (recommended: 16)
 * @return Pointer to buffer for chaining
 */
char* format_temp_range(int min_c, int max_c, char* buf, size_t size);

// =============================================================================
// Duration Formatting
// =============================================================================

/**
 * @brief Format duration in seconds to human-readable string
 *
 * Produces output like:
 * - "30s" for durations under 1 minute
 * - "45m" for durations under 1 hour (no seconds shown)
 * - "2h" for exact hours
 * - "2h 15m" for hours with minutes
 *
 * @param total_seconds Duration in seconds (negative values treated as 0)
 * @return Formatted string
 */
std::string duration(int total_seconds);

/**
 * @brief Format duration with "remaining" suffix for countdowns
 *
 * Produces output like:
 * - "45 min left" for durations under 1 hour
 * - "1:30 left" for durations 1 hour or more (HH:MM format)
 *
 * @param total_seconds Duration in seconds (negative values treated as 0)
 * @return Formatted string with " left" suffix
 */
std::string duration_remaining(int total_seconds);

/**
 * @brief Format print time estimate from minutes
 *
 * Produces output like:
 * - "45 min" for durations under 1 hour
 * - "2h" for exact hours
 * - "2h 15m" for hours with minutes
 *
 * @param total_minutes Duration in minutes (negative values treated as 0)
 * @return Formatted string
 */
std::string duration_from_minutes(int total_minutes);

/**
 * @brief Format duration to a fixed-size buffer (for legacy code)
 *
 * Same output format as duration() but writes to a provided buffer.
 * Useful for code that needs to avoid allocations or use C-style buffers.
 *
 * @param buf Output buffer
 * @param buf_size Size of output buffer (recommended minimum: 16)
 * @param total_seconds Duration in seconds
 * @return Number of characters written (excluding null terminator), or 0 on error
 */
size_t duration_to_buffer(char* buf, size_t buf_size, int total_seconds);

/**
 * @brief Format duration with zero-padded minutes (for progress displays)
 *
 * Produces output like:
 * - "45m" for durations under 1 hour
 * - "2h 05m" for durations 1 hour or more (minutes always 2 digits)
 *
 * @param total_seconds Duration in seconds (negative values treated as 0)
 * @return Formatted string
 */
std::string duration_padded(int total_seconds);

/**
 * @brief Format filament length in mm to human-readable string
 * @param mm Filament length in millimeters
 * @return Formatted string (e.g., "850mm", "12.5m", "1.23km")
 */
std::string format_filament_length(double mm);

/**
 * @brief Result of formatting a heater display
 *
 * Contains all the information needed to display a heater status:
 * - temp: formatted temperature string (e.g., "150°C" or "150 / 200°C")
 * - status: semantic status ("Off", "Heating...", "Ready", or "Cooling")
 * - pct: percentage towards target (0-100, clamped)
 */
struct HeaterDisplayResult {
    std::string temp;
    std::string status;
    int pct;
};

/**
 * @brief Format heater display information from centi-degree values
 *
 * Takes current and target temperatures in centi-degrees (100 = 1°C) and
 * produces a consistent display result used across all heater displays.
 *
 * Status logic (2°C tolerance, matches get_heating_state_color):
 * - target <= 0: "Off"
 * - current < target - 2: "Heating..."
 * - current > target + 2: "Cooling"
 * - within ±2°C: "Ready"
 *
 * @param current_centi Current temperature in centi-degrees
 * @param target_centi Target temperature in centi-degrees (0 = off)
 * @return HeaterDisplayResult with formatted temp, status, and percentage
 */
HeaterDisplayResult heater_display(int current_centi, int target_centi);

} // namespace helix::fmt
