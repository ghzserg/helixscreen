// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

/**
 * @file advanced_panel_types.h
 * @brief Data structures for Advanced Panel features
 *
 * Shared types used across bed leveling, input shaping, Spoolman,
 * machine limits, and macro execution panels.
 */

// ============================================================================
// Bed Leveling Types
// ============================================================================

/**
 * @brief Result from SCREWS_TILT_CALCULATE command
 *
 * Represents a single bed adjustment screw with its measured height
 * deviation and the required adjustment.
 */
struct ScrewTiltResult {
    std::string screw_name; ///< Screw identifier (e.g., "front_left", "rear_right")
    float x_pos = 0.0f;     ///< Bed X coordinate of screw position (mm)
    float y_pos = 0.0f;     ///< Bed Y coordinate of screw position (mm)
    float z_height = 0.0f;  ///< Probed Z height at screw position
    std::string
        adjustment; ///< Adjustment string (e.g., "CW 0:15" for clockwise 0 turns 15 minutes)
    bool is_reference = false; ///< True if this is the reference screw (no adjustment needed)

    /**
     * @brief Check if adjustment is needed
     * @return true if this screw needs turning
     */
    [[nodiscard]] bool needs_adjustment() const {
        return !is_reference && !adjustment.empty() && adjustment != "00:00";
    }

    /**
     * @brief Get prettified screw name for display
     *
     * Converts snake_case to Title Case (e.g., "front_left" -> "Front Left")
     * @return Human-readable screw name
     */
    [[nodiscard]] std::string display_name() const {
        std::string result;
        bool capitalize_next = true;
        for (char c : screw_name) {
            if (c == '_') {
                result += ' ';
                capitalize_next = true;
            } else if (capitalize_next) {
                result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                capitalize_next = false;
            } else {
                result += c;
            }
        }
        return result;
    }

    /**
     * @brief Get user-friendly adjustment description
     *
     * Converts "CW 00:18" to "Tighten ¼" or "Loosen ½" etc.
     * Uses intuitive terms: tighten (CW) = raise that corner, loosen (CCW) = lower it
     * @return Human-friendly adjustment string
     */
    [[nodiscard]] std::string friendly_adjustment() const {
        if (is_reference) {
            return "Reference"; // This screw is the baseline - no adjustment needed
        }
        if (adjustment.empty() || adjustment == "00:00") {
            return "Level";
        }

        // Parse "CW 00:18" or "CCW 01:30" format
        bool is_clockwise = adjustment.find("CW") == 0 && adjustment.find("CCW") != 0;
        bool is_counter = adjustment.find("CCW") == 0;

        // Extract minutes from format "XX:MM" (after the space)
        int total_minutes = 0;
        size_t space_pos = adjustment.find(' ');
        if (space_pos != std::string::npos) {
            std::string time_part = adjustment.substr(space_pos + 1);
            size_t colon_pos = time_part.find(':');
            if (colon_pos != std::string::npos) {
                int turns = std::atoi(time_part.substr(0, colon_pos).c_str());
                int mins = std::atoi(time_part.substr(colon_pos + 1).c_str());
                total_minutes = turns * 60 + mins;
            }
        }

        // Determine specific magnitude description
        std::string amount;
        if (total_minutes <= 5) {
            return "Level"; // Within tolerance
        } else if (total_minutes <= 10) {
            amount = "1/8 turn";
        } else if (total_minutes <= 20) {
            amount = "1/4 turn";
        } else if (total_minutes <= 35) {
            amount = "1/2 turn";
        } else if (total_minutes <= 50) {
            amount = "3/4 turn";
        } else if (total_minutes <= 70) {
            amount = "1 turn";
        } else {
            // Multiple turns - show approximate count
            int approx_turns = (total_minutes + 30) / 60;
            amount = std::to_string(approx_turns) + " turn" + (approx_turns > 1 ? "s" : "");
        }

        // Use intuitive direction: tighten raises corner, loosen lowers it
        if (is_clockwise) {
            return "Tighten " + amount;
        } else if (is_counter) {
            return "Loosen " + amount;
        }
        return adjustment; // Fallback to raw format
    }
};

/**
 * @brief Bed leveling method selection
 */
enum class BedLevelingMethod {
    AUTO_MESH,     ///< BED_MESH_CALIBRATE - Automatic probing grid
    MANUAL_SCREWS, ///< SCREWS_TILT_CALCULATE - Manual screw adjustment guidance
    QUAD_GANTRY,   ///< QUAD_GANTRY_LEVEL - Voron-style gantry leveling
    Z_TILT         ///< Z_TILT_ADJUST - Multi-motor Z adjustment
};

// ============================================================================
// Input Shaping Types
// ============================================================================

/**
 * @brief Result from resonance testing (TEST_RESONANCES or Klippain)
 *
 * Contains the recommended shaper configuration for one axis.
 */
struct InputShaperResult {
    char axis = 'X';          ///< Axis tested ('X' or 'Y')
    std::string shaper_type;  ///< Recommended shaper (e.g., "mzv", "ei", "2hump_ei", "3hump_ei")
    float shaper_freq = 0.0f; ///< Recommended frequency in Hz
    float max_accel = 0.0f;   ///< Maximum recommended acceleration in mm/s²
    float smoothing = 0.0f;   ///< Smoothing value (0.0-1.0, lower is better)
    float vibrations = 0.0f;  ///< Remaining vibrations percentage

    /// Frequency response data for graphing (frequency Hz, amplitude)
    std::vector<std::pair<float, float>> freq_response;

    /**
     * @brief Check if result contains valid data
     */
    [[nodiscard]] bool is_valid() const {
        return !shaper_type.empty() && shaper_freq > 0.0f;
    }
};

/**
 * @brief Alternative shaper recommendation
 */
struct ShaperAlternative {
    std::string shaper_type;
    float shaper_freq = 0.0f;
    float max_accel = 0.0f;
    float smoothing = 0.0f;
    std::string description; ///< Human-readable description of trade-offs
};

// ============================================================================
// Spoolman Types
// ============================================================================

/**
 * @brief Filament spool information from Spoolman
 */
struct SpoolInfo {
    int id = 0;                    ///< Spoolman spool ID
    std::string vendor;            ///< Filament vendor (e.g., "Hatchbox", "Prusament")
    std::string material;          ///< Material type (e.g., "PLA", "PETG", "ABS", "TPU")
    std::string color_name;        ///< Color name (e.g., "Galaxy Black", "Jet Black")
    std::string color_hex;         ///< Hex color code (e.g., "#1A1A2E")
    double remaining_weight_g = 0; ///< Remaining filament weight in grams
    double remaining_length_m = 0; ///< Remaining filament length in meters
    double spool_weight_g = 0;     ///< Empty spool weight in grams
    double initial_weight_g = 0;   ///< Initial filament weight when new
    bool is_active = false;        ///< True if this is the currently tracked spool

    // Temperature recommendations from filament database
    int nozzle_temp_min = 0;
    int nozzle_temp_max = 0;
    int nozzle_temp_recommended = 0;
    int bed_temp_min = 0;
    int bed_temp_max = 0;
    int bed_temp_recommended = 0;

    /**
     * @brief Get remaining percentage
     * @return Percentage of filament remaining (0-100)
     */
    [[nodiscard]] double remaining_percent() const {
        if (initial_weight_g <= 0)
            return 0;
        return (remaining_weight_g / initial_weight_g) * 100.0;
    }

    /**
     * @brief Check if filament is running low
     * @param threshold_grams Warning threshold in grams
     * @return true if remaining weight is below threshold
     */
    [[nodiscard]] bool is_low(double threshold_grams = 100.0) const {
        return remaining_weight_g < threshold_grams;
    }

    /**
     * @brief Get display name combining vendor, material, and color
     */
    [[nodiscard]] std::string display_name() const {
        std::string name;
        if (!vendor.empty())
            name += vendor + " ";
        if (!material.empty())
            name += material;
        if (!color_name.empty())
            name += " - " + color_name;
        return name.empty() ? "Unknown Spool" : name;
    }
};

/**
 * @brief Filament usage record for history tracking
 */
struct FilamentUsageRecord {
    int spool_id = 0;
    double used_weight_g = 0;
    double used_length_m = 0;
    std::string print_filename;
    double timestamp = 0; ///< Unix timestamp
};

// ============================================================================
// Machine Limits Types
// ============================================================================

/**
 * @brief Printer motion limits (velocity, acceleration)
 *
 * Represents current or target machine limits. Can be applied temporarily
 * via SET_VELOCITY_LIMIT or permanently via SAVE_CONFIG.
 */
struct MachineLimits {
    double max_velocity = 0;           ///< Maximum velocity in mm/s
    double max_accel = 0;              ///< Maximum acceleration in mm/s²
    double max_accel_to_decel = 0;     ///< Maximum acceleration to deceleration in mm/s²
    double square_corner_velocity = 0; ///< Square corner velocity in mm/s
    double max_z_velocity = 0;         ///< Maximum Z velocity in mm/s
    double max_z_accel = 0;            ///< Maximum Z acceleration in mm/s²

    /**
     * @brief Check if limits contain valid data
     */
    [[nodiscard]] bool is_valid() const {
        return max_velocity > 0 && max_accel > 0;
    }

    /**
     * @brief Compare two limit sets for equality
     */
    [[nodiscard]] bool operator==(const MachineLimits& other) const {
        return max_velocity == other.max_velocity && max_accel == other.max_accel &&
               max_accel_to_decel == other.max_accel_to_decel &&
               square_corner_velocity == other.square_corner_velocity &&
               max_z_velocity == other.max_z_velocity && max_z_accel == other.max_z_accel;
    }

    [[nodiscard]] bool operator!=(const MachineLimits& other) const {
        return !(*this == other);
    }
};

// ============================================================================
// Macro Types
// ============================================================================

/**
 * @brief Information about a G-code macro
 */
struct MacroInfo {
    std::string name;                ///< Macro name (e.g., "CLEAN_NOZZLE", "PRINT_START")
    std::string description;         ///< Description from gcode_macro description field
    std::vector<std::string> params; ///< Detected parameter names

    bool is_helix_macro = false;  ///< True if HELIX_* prefix
    bool is_system_macro = false; ///< True if _* prefix (hidden by default)
    bool is_dangerous = false;    ///< True if potentially destructive (SAVE_CONFIG, etc.)

    /**
     * @brief Check if macro accepts parameters
     */
    [[nodiscard]] bool has_params() const {
        return !params.empty();
    }

    /**
     * @brief Get display name (without HELIX_ prefix if applicable)
     */
    [[nodiscard]] std::string display_name() const {
        if (is_helix_macro && name.length() > 6) {
            return name.substr(6); // Remove "HELIX_" prefix
        }
        return name;
    }
};

/**
 * @brief Category for grouping macros in the browser
 */
enum class MacroCategory {
    COMMON,      ///< Frequently used (G28, G32, CLEAN_NOZZLE, etc.)
    CALIBRATION, ///< Calibration macros (BED_MESH_CALIBRATE, Z_TILT_ADJUST, etc.)
    HELIX,       ///< HelixScreen helper macros (HELIX_*)
    USER,        ///< User-defined macros
    SYSTEM       ///< System/internal macros (_* prefix)
};

// ============================================================================
// Callback Type Aliases
// ============================================================================

/// Success callback (no data)
using AdvancedSuccessCallback = std::function<void()>;

/// Error callback with message
using AdvancedErrorCallback = std::function<void(const std::string& error)>;

/// Progress callback (0-100 percent)
using AdvancedProgressCallback = std::function<void(int percent)>;

/// Bed screw results callback
using ScrewTiltCallback = std::function<void(const std::vector<ScrewTiltResult>&)>;

/// Input shaper result callback
using InputShaperCallback = std::function<void(const InputShaperResult&)>;

/// Spool list callback
using SpoolListCallback = std::function<void(const std::vector<SpoolInfo>&)>;

/// Machine limits callback
using MachineLimitsCallback = std::function<void(const MachineLimits&)>;

/// Macro list callback
using MacroListCallback = std::function<void(const std::vector<MacroInfo>&)>;

/// Filament usage history callback
using FilamentUsageCallback = std::function<void(const std::vector<FilamentUsageRecord>&)>;
