// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file print_start_analyzer.h
 * @brief Analyzer for PRINT_START macros to detect controllable operations
 *
 * The "black box problem": If a slicer's start G-code simply calls PRINT_START,
 * and that macro internally runs bed mesh, QGL, nozzle cleaning, etc., we cannot
 * control those operations by modifying the G-code file.
 *
 * This analyzer fetches the user's PRINT_START macro (or variants like START_PRINT),
 * parses it to detect embedded operations, and determines whether those operations
 * can be controlled via parameters (e.g., SKIP_BED_MESH=1).
 *
 * If the macro doesn't support skip parameters, the UI can offer to enhance the
 * macro by adding conditional logic.
 */

#include "moonraker_error.h"

#include <functional>
#include <string>
#include <vector>

// Forward declarations
class MoonrakerAPI;

namespace helix {

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Categories of operations detected in PRINT_START
 *
 * These category names (via category_to_string()) serve as capability keys
 * in printer_database.json's print_start_capabilities section.
 */
enum class PrintStartOpCategory {
    BED_LEVELING, // BED_MESH_CALIBRATE, G29
    QGL,          // QUAD_GANTRY_LEVEL
    Z_TILT,       // Z_TILT_ADJUST
    NOZZLE_CLEAN, // CLEAN_NOZZLE, NOZZLE_WIPE, BRUSH_NOZZLE
    PRIMING,      // PURGE_LINE, PRIME_LINE, PRIME_NOZZLE
    SKEW_CORRECT, // SKEW_PROFILE, SET_SKEW
    HOMING,       // G28
    CHAMBER_SOAK, // HEAT_SOAK, CHAMBER_SOAK
    UNKNOWN       // Unrecognized operation
};

/**
 * @brief Get string representation of operation category
 */
[[nodiscard]] const char* category_to_string(PrintStartOpCategory category);

/**
 * @brief An operation detected within a PRINT_START macro
 */
struct PrintStartOperation {
    std::string name; ///< G-code command (e.g., "BED_MESH_CALIBRATE")
    PrintStartOpCategory category = PrintStartOpCategory::UNKNOWN;
    bool has_skip_param = false; ///< true if already wrapped in conditional
    std::string skip_param_name; ///< e.g., "SKIP_BED_MESH" if detected
    size_t line_number = 0;      ///< Line number in macro gcode (1-indexed)
};

/**
 * @brief Result of analyzing a PRINT_START macro
 */
struct PrintStartAnalysis {
    // === Macro Discovery ===
    bool found = false;      ///< A print start macro was found
    std::string macro_name;  ///< Actual name found (e.g., "PRINT_START", "START_PRINT")
    std::string source_file; ///< Config file containing the macro (e.g., "macros.cfg")
    std::string raw_gcode;   ///< Full macro gcode content

    // === Detected Operations ===
    std::vector<PrintStartOperation> operations;

    // === Existing Parameters ===
    std::vector<std::string> known_params; ///< e.g., ["BED", "EXTRUDER", "CHAMBER"]

    // === Controllability ===
    bool is_controllable = false;  ///< At least one op has skip param
    size_t controllable_count = 0; ///< How many ops are already controllable
    size_t total_ops_count = 0;    ///< Total detected operations

    // === Helper Methods ===

    /**
     * @brief Check if a specific operation category was detected
     */
    [[nodiscard]] bool has_operation(PrintStartOpCategory category) const;

    /**
     * @brief Get operation by category (or nullptr if not found)
     */
    [[nodiscard]] const PrintStartOperation* get_operation(PrintStartOpCategory category) const;

    /**
     * @brief Get all operations that are NOT yet controllable
     */
    [[nodiscard]] std::vector<const PrintStartOperation*> get_uncontrollable_operations() const;

    /**
     * @brief Generate a summary string for logging/debugging
     */
    [[nodiscard]] std::string summary() const;
};

// ============================================================================
// PrintStartAnalyzer Class
// ============================================================================

/**
 * @brief Analyzes PRINT_START macros to detect controllable operations
 *
 * Usage:
 *   PrintStartAnalyzer analyzer;
 *   analyzer.analyze(api,
 *       [](const PrintStartAnalysis& result) {
 *           if (result.found) {
 *               spdlog::info("Found {} in macro: {}", result.macro_name, result.summary());
 *           }
 *       },
 *       [](const MoonrakerError& err) { ... }
 *   );
 *
 * Supported macro names (searched in order):
 *   - PRINT_START (most common)
 *   - START_PRINT (alternative convention)
 *   - _PRINT_START (hidden variant)
 *   - _START_PRINT (hidden variant)
 */
class PrintStartAnalyzer {
  public:
    using AnalysisCallback = std::function<void(const PrintStartAnalysis&)>;
    using ErrorCallback = std::function<void(const MoonrakerError&)>;

    PrintStartAnalyzer() = default;
    ~PrintStartAnalyzer() = default;

    // Non-copyable (stateless, but prevent accidental copies)
    PrintStartAnalyzer(const PrintStartAnalyzer&) = delete;
    PrintStartAnalyzer& operator=(const PrintStartAnalyzer&) = delete;

    /**
     * @brief Analyze PRINT_START macro from connected printer
     *
     * Fetches printer config via Moonraker, finds the print start macro,
     * and parses it to detect operations and their controllability.
     *
     * @param api MoonrakerAPI instance (must be connected)
     * @param on_complete Callback with analysis result
     * @param on_error Error callback (connection failed, parse error, etc.)
     */
    void analyze(MoonrakerAPI* api, AnalysisCallback on_complete, ErrorCallback on_error);

    // === Static Parsing Methods (for unit testing) ===

    /**
     * @brief Parse macro gcode to detect operations
     *
     * This is the core parsing logic. It's static and public to enable
     * unit testing without a live Moonraker connection.
     *
     * @param gcode Raw macro gcode content
     * @return Analysis result with detected operations
     */
    [[nodiscard]] static PrintStartAnalysis parse_macro(const std::string& macro_name,
                                                        const std::string& gcode);

    /**
     * @brief Get the standard skip parameter name for an operation
     *
     * @param op_name Operation name (e.g., "BED_MESH_CALIBRATE")
     * @return Suggested skip parameter name (e.g., "SKIP_BED_MESH")
     */
    [[nodiscard]] static std::string get_suggested_skip_param(const std::string& op_name);

    /**
     * @brief Get the operation category for a G-code command
     *
     * @param command G-code command (e.g., "BED_MESH_CALIBRATE", "G29")
     * @return Category (or UNKNOWN if not recognized)
     */
    [[nodiscard]] static PrintStartOpCategory categorize_operation(const std::string& command);

    // === Macro Name Candidates (public for helper functions) ===
    static constexpr const char* MACRO_NAMES[] = {"PRINT_START", "START_PRINT", "_PRINT_START",
                                                  "_START_PRINT"};
    static constexpr size_t MACRO_NAMES_COUNT = 4;

  private:
    // === Parsing Helpers ===

    /**
     * @brief Detect operations in macro gcode
     */
    [[nodiscard]] static std::vector<PrintStartOperation>
    detect_operations(const std::string& gcode);

    /**
     * @brief Check if an operation is wrapped in a skip conditional
     *
     * Looks for patterns like:
     *   {% if SKIP_BED_MESH == 0 %} or {% if params.SKIP_BED_MESH|default(0) == 0 %}
     *   followed by the operation
     *
     * @param gcode Full macro gcode
     * @param op_name Operation to check
     * @param out_param_name Output: detected parameter name if found
     * @return true if operation is conditional
     */
    [[nodiscard]] static bool detect_skip_conditional(const std::string& gcode,
                                                      const std::string& op_name,
                                                      std::string& out_param_name);

    /**
     * @brief Extract known parameters from macro gcode
     *
     * Looks for patterns like:
     *   {% set BED = params.BED|default(60)|float %}
     *   params.EXTRUDER
     */
    [[nodiscard]] static std::vector<std::string> extract_parameters(const std::string& gcode);
};

} // namespace helix
