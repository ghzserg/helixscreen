// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace helix {

/**
 * @file operation_patterns.h
 * @brief Shared pattern definitions for detecting pre-print operations
 *
 * This file consolidates operation detection patterns used by both:
 * - PrintStartAnalyzer (scans PRINT_START macro in printer.cfg)
 * - GCodeOpsDetector (scans G-code file content)
 *
 * Having a single source of truth ensures consistency and makes it easy
 * to add new patterns that work across both analyzers.
 */

/**
 * @brief Categories of pre-print operations
 *
 * These represent the semantic meaning of operations, not the specific
 * command names (which vary by printer/config).
 */
enum class OperationCategory {
    BED_LEVELING, ///< Bed mesh calibration (BED_MESH_CALIBRATE, G29)
    QGL,          ///< Quad gantry leveling (QUAD_GANTRY_LEVEL)
    Z_TILT,       ///< Z-tilt adjustment (Z_TILT_ADJUST)
    NOZZLE_CLEAN, ///< Nozzle cleaning/wiping (CLEAN_NOZZLE, BRUSH_NOZZLE)
    PURGE_LINE,   ///< Purge/prime line (PURGE_LINE, PRIME_LINE)
    HOMING,       ///< Homing axes (G28)
    CHAMBER_SOAK, ///< Chamber heat soak (HEAT_SOAK)
    SKEW_CORRECT, ///< Skew correction (SKEW_PROFILE, SET_SKEW)
    START_PRINT,  ///< The print start macro itself (PRINT_START, START_PRINT)
    UNKNOWN,      ///< Unrecognized operation
};

/**
 * @brief A single operation keyword pattern
 */
struct OperationKeyword {
    const char* keyword;        ///< Command/macro name to match (e.g., "BED_MESH_CALIBRATE")
    OperationCategory category; ///< Semantic category
    const char* skip_param;     ///< Suggested skip parameter name (e.g., "SKIP_BED_MESH")
    bool exact_match;           ///< True for G-codes (exact), false for macros (substring)
};

/**
 * @brief Master list of operation keywords
 *
 * This is the single source of truth for all operation detection.
 * Both PrintStartAnalyzer and GCodeOpsDetector use this list.
 */
// clang-format off
inline const OperationKeyword OPERATION_KEYWORDS[] = {
    // === Bed Leveling ===
    // Substring matching: AUTO_BED_LEVEL matches BED_LEVEL, etc.
    {"BED_MESH",             OperationCategory::BED_LEVELING, "SKIP_BED_MESH",     false},
    {"BED_LEVEL",            OperationCategory::BED_LEVELING, "SKIP_BED_MESH",     false},
    {"G29",                  OperationCategory::BED_LEVELING, "SKIP_BED_MESH",     true},

    // === Quad Gantry Level ===
    {"QUAD_GANTRY_LEVEL",    OperationCategory::QGL,          "SKIP_QGL",          false},
    {"QGL",                  OperationCategory::QGL,          "SKIP_QGL",          false},

    // === Z Tilt ===
    {"Z_TILT_ADJUST",        OperationCategory::Z_TILT,       "SKIP_Z_TILT",       false},
    {"Z_TILT",               OperationCategory::Z_TILT,       "SKIP_Z_TILT",       false},

    // === Nozzle Cleaning ===
    // Substring matching: _CLEAN_NOZZLE matches CLEAN_NOZZLE, etc.
    {"CLEAN_NOZZLE",         OperationCategory::NOZZLE_CLEAN, "SKIP_NOZZLE_CLEAN", false},
    {"NOZZLE_CLEAN",         OperationCategory::NOZZLE_CLEAN, "SKIP_NOZZLE_CLEAN", false},
    {"NOZZLE_WIPE",          OperationCategory::NOZZLE_CLEAN, "SKIP_NOZZLE_CLEAN", false},
    {"WIPE_NOZZLE",          OperationCategory::NOZZLE_CLEAN, "SKIP_NOZZLE_CLEAN", false},
    {"BRUSH_NOZZLE",         OperationCategory::NOZZLE_CLEAN, "SKIP_NOZZLE_CLEAN", false},
    {"NOZZLE_BRUSH",         OperationCategory::NOZZLE_CLEAN, "SKIP_NOZZLE_CLEAN", false},

    // === Purge/Prime Line ===
    // Substring matching: _PRIME_NOZZLE matches PRIME_NOZZLE, etc.
    {"PURGE",                OperationCategory::PURGE_LINE,   "SKIP_PURGE",        false},
    {"PRIME",                OperationCategory::PURGE_LINE,   "SKIP_PURGE",        false},
    {"INTRO_LINE",           OperationCategory::PURGE_LINE,   "SKIP_PURGE",        false},

    // === Homing ===
    {"G28",                  OperationCategory::HOMING,       "SKIP_HOMING",       true},
    {"SAFE_HOME",            OperationCategory::HOMING,       "SKIP_HOMING",       false},

    // === Chamber Soak ===
    {"HEAT_SOAK",            OperationCategory::CHAMBER_SOAK, "SKIP_SOAK",         false},
    {"CHAMBER_SOAK",         OperationCategory::CHAMBER_SOAK, "SKIP_SOAK",         false},
    {"SET_HEATER_TEMPERATURE HEATER=chamber", OperationCategory::CHAMBER_SOAK, "SKIP_SOAK", false},

    // === Skew Correction ===
    {"SKEW_PROFILE",         OperationCategory::SKEW_CORRECT, "SKIP_SKEW",         false},
    {"SET_SKEW",             OperationCategory::SKEW_CORRECT, "SKIP_SKEW",         false},
    {"SKEW",                 OperationCategory::SKEW_CORRECT, "SKIP_SKEW",         false},
};
// clang-format on

inline constexpr size_t OPERATION_KEYWORDS_COUNT =
    sizeof(OPERATION_KEYWORDS) / sizeof(OPERATION_KEYWORDS[0]);

/**
 * @brief Skip parameter variations for detecting controllability
 *
 * When scanning a macro, we look for these parameter names in {% if %} blocks
 * to determine if an operation can be skipped.
 */
// clang-format off
inline const std::vector<std::string> SKIP_PARAM_VARIATIONS[] = {
    // Index 0: BED_LEVELING
    {"SKIP_BED_MESH", "SKIP_MESH", "SKIP_BED_LEVELING", "NO_BED_MESH", "SKIP_LEVEL"},
    // Index 1: QGL
    {"SKIP_QGL", "SKIP_GANTRY", "NO_QGL", "SKIP_QUAD_GANTRY_LEVEL"},
    // Index 2: Z_TILT
    {"SKIP_Z_TILT", "SKIP_TILT", "NO_Z_TILT", "SKIP_Z_TILT_ADJUST"},
    // Index 3: NOZZLE_CLEAN
    {"SKIP_NOZZLE_CLEAN", "SKIP_CLEAN", "NO_CLEAN"},
    // Index 4: PURGE_LINE
    {"SKIP_PURGE", "SKIP_PRIME", "NO_PURGE", "NO_PRIME", "DISABLE_PRIMING"},
    // Index 5: HOMING
    {"SKIP_HOMING", "SKIP_HOME", "NO_HOME"},
    // Index 6: CHAMBER_SOAK
    {"SKIP_SOAK", "SKIP_HEAT_SOAK", "NO_SOAK", "SKIP_CHAMBER"},
    // Index 7: SKEW_CORRECT
    {"SKIP_SKEW", "NO_SKEW", "DISABLE_SKEW", "DISABLE_SKEW_CORRECT"},
};
// clang-format on

/**
 * @brief Get human-readable name for a category
 */
inline const char* category_name(OperationCategory cat) {
    switch (cat) {
    case OperationCategory::BED_LEVELING:
        return "Bed leveling";
    case OperationCategory::QGL:
        return "Quad gantry leveling";
    case OperationCategory::Z_TILT:
        return "Z-tilt adjustment";
    case OperationCategory::NOZZLE_CLEAN:
        return "Nozzle cleaning";
    case OperationCategory::PURGE_LINE:
        return "Purge line";
    case OperationCategory::HOMING:
        return "Homing";
    case OperationCategory::CHAMBER_SOAK:
        return "Chamber heat soak";
    case OperationCategory::SKEW_CORRECT:
        return "Skew correction";
    case OperationCategory::START_PRINT:
        return "Start print";
    case OperationCategory::UNKNOWN:
    default:
        return "Unknown";
    }
}

/**
 * @brief Get machine-readable key for a category (for deduplication)
 */
inline const char* category_key(OperationCategory cat) {
    switch (cat) {
    case OperationCategory::BED_LEVELING:
        return "bed_leveling";
    case OperationCategory::QGL:
        return "qgl";
    case OperationCategory::Z_TILT:
        return "z_tilt";
    case OperationCategory::NOZZLE_CLEAN:
        return "nozzle_clean";
    case OperationCategory::PURGE_LINE:
        return "purge_line";
    case OperationCategory::HOMING:
        return "homing";
    case OperationCategory::CHAMBER_SOAK:
        return "chamber_soak";
    case OperationCategory::SKEW_CORRECT:
        return "skew_correct";
    case OperationCategory::START_PRINT:
        return "start_print";
    case OperationCategory::UNKNOWN:
    default:
        return "unknown";
    }
}

/**
 * @brief Get skip parameter variations for a category
 *
 * @param cat The operation category
 * @return Vector of skip parameter name variations, or empty if none
 */
inline const std::vector<std::string>& get_skip_variations(OperationCategory cat) {
    static const std::vector<std::string> empty;
    size_t idx = static_cast<size_t>(cat);
    constexpr size_t count = sizeof(SKIP_PARAM_VARIATIONS) / sizeof(SKIP_PARAM_VARIATIONS[0]);
    if (idx < count) {
        return SKIP_PARAM_VARIATIONS[idx];
    }
    return empty;
}

/**
 * @brief Find keyword entry by pattern string (substring match, case-insensitive)
 *
 * Uses substring matching so `_PRIME_NOZZLE` matches `PRIME_NOZZLE`,
 * `AUTO_BED_LEVEL` matches `BED_LEVEL`, etc. This catches custom macro
 * prefixes/suffixes automatically.
 *
 * G-codes use exact matching to avoid false positives (G28 inside FOO_G28_BAR).
 * All matching is case-insensitive.
 *
 * @param pattern Command to search for
 * @return Pointer to keyword entry, or nullptr if not found
 */
inline const OperationKeyword* find_keyword(const std::string& pattern) {
    // Always uppercase for case-insensitive comparison
    std::string pat = pattern;
    std::transform(pat.begin(), pat.end(), pat.begin(), ::toupper);

    for (size_t i = 0; i < OPERATION_KEYWORDS_COUNT; ++i) {
        std::string keyword = OPERATION_KEYWORDS[i].keyword;
        std::transform(keyword.begin(), keyword.end(), keyword.begin(), ::toupper);

        if (OPERATION_KEYWORDS[i].exact_match) {
            // G-codes: exact match only (avoid G28 matching inside FOO_G28_BAR)
            if (pat == keyword) {
                return &OPERATION_KEYWORDS[i];
            }
        } else {
            // Macros: substring match (catches _PRIME_NOZZLE, AUTO_BED_LEVEL, etc.)
            if (pat.find(keyword) != std::string::npos) {
                return &OPERATION_KEYWORDS[i];
            }
        }
    }
    return nullptr;
}

} // namespace helix
