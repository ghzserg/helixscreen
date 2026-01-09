// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_capability_matrix.cpp
 * @brief Tests for CapabilityMatrix - unified capability source management
 *
 * CapabilityMatrix unifies three sources of pre-print operation capabilities:
 * 1. DATABASE - PrinterDetector's PrintStartCapabilities (highest priority)
 * 2. MACRO_ANALYSIS - PrintStartAnalyzer's PrintStartAnalysis (medium priority)
 * 3. FILE_SCAN - GCodeOpsDetector's ScanResult (lowest priority)
 *
 * Priority ordering ensures that database-defined capabilities (which are
 * curated and tested) take precedence over dynamically detected ones.
 */

#include "capability_matrix.h"
#include "gcode_ops_detector.h"
#include "operation_patterns.h"
#include "print_start_analyzer.h"
#include "printer_detector.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::gcode;

// ============================================================================
// Test Helpers
// ============================================================================

/**
 * @brief Create a database-style capability for testing
 */
static PrintStartCapabilities make_database_caps(const std::string& macro_name = "START_PRINT") {
    PrintStartCapabilities caps;
    caps.macro_name = macro_name;
    return caps;
}

/**
 * @brief Add a capability param to PrintStartCapabilities
 */
static void add_cap_param(PrintStartCapabilities& caps, const std::string& cap_name,
                          const std::string& param, const std::string& skip_val,
                          const std::string& enable_val) {
    PrintStartParamCapability cap;
    cap.param = param;
    cap.skip_value = skip_val;
    cap.enable_value = enable_val;
    caps.params[cap_name] = cap;
}

/**
 * @brief Create a PrintStartAnalysis result for testing
 */
static PrintStartAnalysis make_macro_analysis(const std::string& macro_name = "PRINT_START") {
    PrintStartAnalysis analysis;
    analysis.found = true;
    analysis.macro_name = macro_name;
    return analysis;
}

/**
 * @brief Add an operation to PrintStartAnalysis
 */
static void add_analysis_op(PrintStartAnalysis& analysis, PrintStartOpCategory category,
                            const std::string& name, bool has_skip = false,
                            const std::string& skip_param = "",
                            ParameterSemantic semantic = ParameterSemantic::OPT_OUT) {
    PrintStartOperation op;
    op.name = name;
    op.category = category;
    op.has_skip_param = has_skip;
    op.skip_param_name = skip_param;
    op.param_semantic = semantic;
    op.line_number = analysis.operations.size() + 1;
    analysis.operations.push_back(op);
    analysis.total_ops_count = analysis.operations.size();
    if (has_skip) {
        analysis.controllable_count++;
        analysis.is_controllable = true;
    }
}

/**
 * @brief Create a ScanResult for testing
 */
static ScanResult make_file_scan() {
    ScanResult result;
    result.lines_scanned = 100;
    result.bytes_scanned = 5000;
    return result;
}

/**
 * @brief Add a detected operation to ScanResult
 */
static void add_scan_op(ScanResult& result, OperationType type, OperationEmbedding embedding,
                        const std::string& macro_name, size_t line_number = 1) {
    DetectedOperation op;
    op.type = type;
    op.embedding = embedding;
    op.macro_name = macro_name;
    op.line_number = line_number;
    result.operations.push_back(op);
}

// ============================================================================
// Test Cases: Empty Matrix Behavior
// ============================================================================

TEST_CASE("CapabilityMatrix: Empty matrix behavior", "[capability_matrix][print_preparation]") {
    CapabilityMatrix matrix;

    SECTION("is_controllable returns false for any operation") {
        REQUIRE(matrix.is_controllable(OperationCategory::BED_MESH) == false);
        REQUIRE(matrix.is_controllable(OperationCategory::QGL) == false);
        REQUIRE(matrix.is_controllable(OperationCategory::Z_TILT) == false);
        REQUIRE(matrix.is_controllable(OperationCategory::NOZZLE_CLEAN) == false);
        REQUIRE(matrix.is_controllable(OperationCategory::PURGE_LINE) == false);
        REQUIRE(matrix.is_controllable(OperationCategory::HOMING) == false);
    }

    SECTION("get_best_source returns nullopt") {
        REQUIRE(matrix.get_best_source(OperationCategory::BED_MESH) == std::nullopt);
        REQUIRE(matrix.get_best_source(OperationCategory::QGL) == std::nullopt);
    }

    SECTION("get_all_sources returns empty vector") {
        auto sources = matrix.get_all_sources(OperationCategory::BED_MESH);
        REQUIRE(sources.empty());
    }

    SECTION("has_any_controllable returns false") {
        REQUIRE(matrix.has_any_controllable() == false);
    }

    SECTION("get_controllable_operations returns empty vector") {
        auto ops = matrix.get_controllable_operations();
        REQUIRE(ops.empty());
    }

    SECTION("get_skip_param returns nullopt") {
        REQUIRE(matrix.get_skip_param(OperationCategory::BED_MESH) == std::nullopt);
    }
}

// ============================================================================
// Test Cases: Single Source - Database
// ============================================================================

TEST_CASE("CapabilityMatrix: Single source - database (AD5M style)",
          "[capability_matrix][print_preparation]") {
    CapabilityMatrix matrix;

    // Create AD5M-style database capabilities
    PrintStartCapabilities caps = make_database_caps("START_PRINT");
    add_cap_param(caps, "bed_mesh", "FORCE_LEVELING", "false", "true");

    matrix.add_from_database(caps);

    SECTION("is_controllable returns true for BED_MESH") {
        REQUIRE(matrix.is_controllable(OperationCategory::BED_MESH) == true);
    }

    SECTION("is_controllable returns false for operations not in database") {
        REQUIRE(matrix.is_controllable(OperationCategory::QGL) == false);
        REQUIRE(matrix.is_controllable(OperationCategory::NOZZLE_CLEAN) == false);
    }

    SECTION("get_best_source returns DATABASE origin") {
        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source.has_value());
        REQUIRE(source->origin == CapabilityOrigin::DATABASE);
        REQUIRE(source->param_name == "FORCE_LEVELING");
        REQUIRE(source->skip_value == "false");
        REQUIRE(source->enable_value == "true");
    }

    SECTION("get_skip_param returns correct param and value") {
        auto skip = matrix.get_skip_param(OperationCategory::BED_MESH);
        REQUIRE(skip.has_value());
        REQUIRE(skip->first == "FORCE_LEVELING");
        REQUIRE(skip->second == "false");
    }

    SECTION("has_any_controllable returns true") {
        REQUIRE(matrix.has_any_controllable() == true);
    }

    SECTION("get_controllable_operations returns BED_MESH") {
        auto ops = matrix.get_controllable_operations();
        REQUIRE(ops.size() == 1);
        REQUIRE(ops[0] == OperationCategory::BED_MESH);
    }
}

// ============================================================================
// Test Cases: Single Source - Macro Analysis
// ============================================================================

TEST_CASE("CapabilityMatrix: Single source - macro analysis",
          "[capability_matrix][print_preparation]") {
    CapabilityMatrix matrix;

    // Create macro analysis with SKIP_BED_MESH detected
    PrintStartAnalysis analysis = make_macro_analysis("PRINT_START");
    add_analysis_op(analysis, PrintStartOpCategory::BED_MESH, "BED_MESH_CALIBRATE", true,
                    "SKIP_BED_MESH", ParameterSemantic::OPT_OUT);

    matrix.add_from_macro_analysis(analysis);

    SECTION("is_controllable returns true for BED_MESH") {
        REQUIRE(matrix.is_controllable(OperationCategory::BED_MESH) == true);
    }

    SECTION("get_best_source returns MACRO_ANALYSIS origin") {
        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source.has_value());
        REQUIRE(source->origin == CapabilityOrigin::MACRO_ANALYSIS);
        REQUIRE(source->param_name == "SKIP_BED_MESH");
        REQUIRE(source->semantic == ParameterSemantic::OPT_OUT);
    }

    SECTION("OPT_OUT semantic sets skip_value=1 and enable_value=0") {
        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source.has_value());
        // For OPT_OUT (SKIP_*), skip=1 means skip, skip=0 means do
        REQUIRE(source->skip_value == "1");
        REQUIRE(source->enable_value == "0");
    }

    SECTION("get_skip_param returns correct param and value for OPT_OUT") {
        auto skip = matrix.get_skip_param(OperationCategory::BED_MESH);
        REQUIRE(skip.has_value());
        REQUIRE(skip->first == "SKIP_BED_MESH");
        REQUIRE(skip->second == "1"); // OPT_OUT: 1 means skip
    }
}

TEST_CASE("CapabilityMatrix: Macro analysis with OPT_IN semantic",
          "[capability_matrix][print_preparation]") {
    CapabilityMatrix matrix;

    // Create macro analysis with PERFORM_BED_MESH detected
    PrintStartAnalysis analysis = make_macro_analysis("PRINT_START");
    add_analysis_op(analysis, PrintStartOpCategory::BED_MESH, "BED_MESH_CALIBRATE", true,
                    "PERFORM_BED_MESH", ParameterSemantic::OPT_IN);

    matrix.add_from_macro_analysis(analysis);

    SECTION("OPT_IN semantic sets skip_value=0 and enable_value=1") {
        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source.has_value());
        REQUIRE(source->semantic == ParameterSemantic::OPT_IN);
        // For OPT_IN (PERFORM_*), param=0 means skip, param=1 means do
        REQUIRE(source->skip_value == "0");
        REQUIRE(source->enable_value == "1");
    }

    SECTION("get_skip_param returns correct param and value for OPT_IN") {
        auto skip = matrix.get_skip_param(OperationCategory::BED_MESH);
        REQUIRE(skip.has_value());
        REQUIRE(skip->first == "PERFORM_BED_MESH");
        REQUIRE(skip->second == "0"); // OPT_IN: 0 means skip
    }
}

// ============================================================================
// Test Cases: Single Source - File Scan
// ============================================================================

TEST_CASE("CapabilityMatrix: Single source - file scan", "[capability_matrix][print_preparation]") {
    CapabilityMatrix matrix;

    // Create file scan with direct BED_MESH_CALIBRATE command detected
    ScanResult scan = make_file_scan();
    add_scan_op(scan, OperationType::BED_MESH, OperationEmbedding::DIRECT_COMMAND,
                "BED_MESH_CALIBRATE", 15);

    matrix.add_from_file_scan(scan);

    SECTION("is_controllable returns true for BED_MESH") {
        REQUIRE(matrix.is_controllable(OperationCategory::BED_MESH) == true);
    }

    SECTION("get_best_source returns FILE_SCAN origin") {
        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source.has_value());
        REQUIRE(source->origin == CapabilityOrigin::FILE_SCAN);
        REQUIRE(source->line_number == 15);
    }

    SECTION("FILE_SCAN source indicates file modification needed") {
        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source.has_value());
        // File scan doesn't have a macro parameter - it needs file modification
        REQUIRE(source->param_name.empty());
    }

    SECTION("get_skip_param returns nullopt for file scan source") {
        // File scan operations cannot be controlled via parameters
        // They require file modification, which is a different code path
        auto skip = matrix.get_skip_param(OperationCategory::BED_MESH);
        REQUIRE(skip == std::nullopt);
    }
}

TEST_CASE("CapabilityMatrix: File scan with MACRO_PARAMETER embedding",
          "[capability_matrix][print_preparation]") {
    CapabilityMatrix matrix;

    // Create file scan with MACRO_PARAMETER detected in START_PRINT call
    ScanResult scan = make_file_scan();
    DetectedOperation op;
    op.type = OperationType::BED_MESH;
    op.embedding = OperationEmbedding::MACRO_PARAMETER;
    op.macro_name = "START_PRINT";
    op.param_name = "FORCE_LEVELING";
    op.param_value = "true";
    op.line_number = 5;
    scan.operations.push_back(op);

    matrix.add_from_file_scan(scan);

    SECTION("Macro parameter source has correct semantic and values") {
        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source.has_value());
        REQUIRE(source->origin == CapabilityOrigin::FILE_SCAN);
        REQUIRE(source->param_name == "FORCE_LEVELING");
        REQUIRE(source->semantic == ParameterSemantic::OPT_IN); // FORCE_* is OPT_IN
        // For OPT_IN: skip=0, enable=1
        REQUIRE(source->skip_value == "0");
        REQUIRE(source->enable_value == "1");
    }

    SECTION("get_skip_param returns correct value for OPT_IN") {
        auto skip = matrix.get_skip_param(OperationCategory::BED_MESH);
        REQUIRE(skip.has_value());
        REQUIRE(skip->first == "FORCE_LEVELING");
        REQUIRE(skip->second == "0"); // OPT_IN: 0 means skip
    }

    SECTION("Preserves line number") {
        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source.has_value());
        REQUIRE(source->line_number == 5);
    }
}

TEST_CASE("CapabilityMatrix: Database capability with empty param is skipped",
          "[capability_matrix][print_preparation][edge_cases]") {
    CapabilityMatrix matrix;

    PrintStartCapabilities caps = make_database_caps("START_PRINT");
    PrintStartParamCapability cap;
    cap.param = ""; // Empty param
    cap.skip_value = "false";
    cap.enable_value = "true";
    caps.params["bed_mesh"] = cap;

    matrix.add_from_database(caps);

    SECTION("Empty param capability is not added") {
        REQUIRE(matrix.is_controllable(OperationCategory::BED_MESH) == false);
        REQUIRE(matrix.has_any_controllable() == false);
    }
}

// ============================================================================
// Test Cases: Priority Ordering (Critical)
// ============================================================================

TEST_CASE("CapabilityMatrix: Priority ordering - DATABASE > MACRO_ANALYSIS > FILE_SCAN",
          "[capability_matrix][print_preparation][priority]") {
    CapabilityMatrix matrix;

    // Add all three sources for the same operation
    // They should be prioritized: DATABASE > MACRO_ANALYSIS > FILE_SCAN

    // Source 1: Database (highest priority)
    PrintStartCapabilities caps = make_database_caps("START_PRINT");
    add_cap_param(caps, "bed_mesh", "FORCE_LEVELING", "false", "true");

    // Source 2: Macro analysis (medium priority)
    PrintStartAnalysis analysis = make_macro_analysis("PRINT_START");
    add_analysis_op(analysis, PrintStartOpCategory::BED_MESH, "BED_MESH_CALIBRATE", true,
                    "SKIP_BED_MESH", ParameterSemantic::OPT_OUT);

    // Source 3: File scan (lowest priority)
    ScanResult scan = make_file_scan();
    add_scan_op(scan, OperationType::BED_MESH, OperationEmbedding::DIRECT_COMMAND,
                "BED_MESH_CALIBRATE", 15);

    matrix.add_from_database(caps);
    matrix.add_from_macro_analysis(analysis);
    matrix.add_from_file_scan(scan);

    SECTION("get_best_source returns DATABASE when all three present") {
        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source.has_value());
        REQUIRE(source->origin == CapabilityOrigin::DATABASE);
        REQUIRE(source->param_name == "FORCE_LEVELING");
    }

    SECTION("get_all_sources returns all three in priority order") {
        auto sources = matrix.get_all_sources(OperationCategory::BED_MESH);
        REQUIRE(sources.size() == 3);
        REQUIRE(sources[0].origin == CapabilityOrigin::DATABASE);
        REQUIRE(sources[1].origin == CapabilityOrigin::MACRO_ANALYSIS);
        REQUIRE(sources[2].origin == CapabilityOrigin::FILE_SCAN);
    }

    SECTION("get_skip_param uses DATABASE source") {
        auto skip = matrix.get_skip_param(OperationCategory::BED_MESH);
        REQUIRE(skip.has_value());
        REQUIRE(skip->first == "FORCE_LEVELING"); // From database, not macro analysis
    }
}

TEST_CASE("CapabilityMatrix: Priority - MACRO_ANALYSIS wins over FILE_SCAN",
          "[capability_matrix][print_preparation][priority]") {
    CapabilityMatrix matrix;

    // Only macro analysis and file scan (no database)
    PrintStartAnalysis analysis = make_macro_analysis("PRINT_START");
    add_analysis_op(analysis, PrintStartOpCategory::BED_MESH, "BED_MESH_CALIBRATE", true,
                    "SKIP_BED_MESH", ParameterSemantic::OPT_OUT);

    ScanResult scan = make_file_scan();
    add_scan_op(scan, OperationType::BED_MESH, OperationEmbedding::DIRECT_COMMAND,
                "BED_MESH_CALIBRATE", 15);

    matrix.add_from_macro_analysis(analysis);
    matrix.add_from_file_scan(scan);

    SECTION("get_best_source returns MACRO_ANALYSIS when database absent") {
        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source.has_value());
        REQUIRE(source->origin == CapabilityOrigin::MACRO_ANALYSIS);
        REQUIRE(source->param_name == "SKIP_BED_MESH");
    }

    SECTION("get_all_sources returns both in priority order") {
        auto sources = matrix.get_all_sources(OperationCategory::BED_MESH);
        REQUIRE(sources.size() == 2);
        REQUIRE(sources[0].origin == CapabilityOrigin::MACRO_ANALYSIS);
        REQUIRE(sources[1].origin == CapabilityOrigin::FILE_SCAN);
    }
}

TEST_CASE("CapabilityMatrix: Priority - FILE_SCAN used when only source",
          "[capability_matrix][print_preparation][priority]") {
    CapabilityMatrix matrix;

    // Only file scan (no database or macro analysis)
    ScanResult scan = make_file_scan();
    add_scan_op(scan, OperationType::BED_MESH, OperationEmbedding::DIRECT_COMMAND,
                "BED_MESH_CALIBRATE", 15);

    matrix.add_from_file_scan(scan);

    SECTION("get_best_source returns FILE_SCAN when only source") {
        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source.has_value());
        REQUIRE(source->origin == CapabilityOrigin::FILE_SCAN);
    }
}

// ============================================================================
// Test Cases: Multiple Operations
// ============================================================================

TEST_CASE("CapabilityMatrix: Multiple operations", "[capability_matrix][print_preparation]") {
    CapabilityMatrix matrix;

    // Add capabilities for multiple operations from database
    PrintStartCapabilities caps = make_database_caps("START_PRINT");
    add_cap_param(caps, "bed_mesh", "FORCE_LEVELING", "false", "true");
    add_cap_param(caps, "qgl", "FORCE_QGL", "false", "true");
    add_cap_param(caps, "nozzle_clean", "NOZZLE_CLEAN", "false", "true");

    matrix.add_from_database(caps);

    SECTION("Each operation is independently queryable") {
        REQUIRE(matrix.is_controllable(OperationCategory::BED_MESH) == true);
        REQUIRE(matrix.is_controllable(OperationCategory::QGL) == true);
        REQUIRE(matrix.is_controllable(OperationCategory::NOZZLE_CLEAN) == true);
        // Operations not added should still be false
        REQUIRE(matrix.is_controllable(OperationCategory::Z_TILT) == false);
        REQUIRE(matrix.is_controllable(OperationCategory::PURGE_LINE) == false);
    }

    SECTION("get_controllable_operations returns all three") {
        auto ops = matrix.get_controllable_operations();
        REQUIRE(ops.size() == 3);

        // Check all three are present (order may vary)
        bool has_bed_mesh = false, has_qgl = false, has_nozzle_clean = false;
        for (auto op : ops) {
            if (op == OperationCategory::BED_MESH)
                has_bed_mesh = true;
            if (op == OperationCategory::QGL)
                has_qgl = true;
            if (op == OperationCategory::NOZZLE_CLEAN)
                has_nozzle_clean = true;
        }
        REQUIRE(has_bed_mesh);
        REQUIRE(has_qgl);
        REQUIRE(has_nozzle_clean);
    }

    SECTION("Each operation has correct source") {
        auto bed_mesh = matrix.get_best_source(OperationCategory::BED_MESH);
        auto qgl = matrix.get_best_source(OperationCategory::QGL);
        auto nozzle = matrix.get_best_source(OperationCategory::NOZZLE_CLEAN);

        REQUIRE(bed_mesh->param_name == "FORCE_LEVELING");
        REQUIRE(qgl->param_name == "FORCE_QGL");
        REQUIRE(nozzle->param_name == "NOZZLE_CLEAN");
    }
}

TEST_CASE("CapabilityMatrix: Mixed sources for different operations",
          "[capability_matrix][print_preparation]") {
    CapabilityMatrix matrix;

    // BED_MESH from database
    PrintStartCapabilities caps = make_database_caps("START_PRINT");
    add_cap_param(caps, "bed_mesh", "FORCE_LEVELING", "false", "true");

    // QGL from macro analysis
    PrintStartAnalysis analysis = make_macro_analysis("PRINT_START");
    add_analysis_op(analysis, PrintStartOpCategory::QGL, "QUAD_GANTRY_LEVEL", true, "SKIP_QGL",
                    ParameterSemantic::OPT_OUT);

    // NOZZLE_CLEAN from file scan
    ScanResult scan = make_file_scan();
    add_scan_op(scan, OperationType::NOZZLE_CLEAN, OperationEmbedding::MACRO_CALL, "CLEAN_NOZZLE",
                20);

    matrix.add_from_database(caps);
    matrix.add_from_macro_analysis(analysis);
    matrix.add_from_file_scan(scan);

    SECTION("Each operation uses its respective source") {
        auto bed_mesh = matrix.get_best_source(OperationCategory::BED_MESH);
        auto qgl = matrix.get_best_source(OperationCategory::QGL);
        auto nozzle = matrix.get_best_source(OperationCategory::NOZZLE_CLEAN);

        REQUIRE(bed_mesh->origin == CapabilityOrigin::DATABASE);
        REQUIRE(qgl->origin == CapabilityOrigin::MACRO_ANALYSIS);
        REQUIRE(nozzle->origin == CapabilityOrigin::FILE_SCAN);
    }

    SECTION("All three operations are controllable") {
        REQUIRE(matrix.is_controllable(OperationCategory::BED_MESH));
        REQUIRE(matrix.is_controllable(OperationCategory::QGL));
        REQUIRE(matrix.is_controllable(OperationCategory::NOZZLE_CLEAN));
    }
}

// ============================================================================
// Test Cases: Semantic Handling
// ============================================================================

TEST_CASE("CapabilityMatrix: Semantic handling - OPT_OUT vs OPT_IN",
          "[capability_matrix][print_preparation][semantic]") {
    SECTION("OPT_OUT: SKIP_BED_MESH=1 means skip") {
        CapabilityMatrix matrix;

        PrintStartAnalysis analysis = make_macro_analysis("PRINT_START");
        add_analysis_op(analysis, PrintStartOpCategory::BED_MESH, "BED_MESH_CALIBRATE", true,
                        "SKIP_BED_MESH", ParameterSemantic::OPT_OUT);
        matrix.add_from_macro_analysis(analysis);

        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source->semantic == ParameterSemantic::OPT_OUT);
        REQUIRE(source->skip_value == "1");   // 1 = skip
        REQUIRE(source->enable_value == "0"); // 0 = do it

        auto skip = matrix.get_skip_param(OperationCategory::BED_MESH);
        REQUIRE(skip->first == "SKIP_BED_MESH");
        REQUIRE(skip->second == "1");
    }

    SECTION("OPT_IN: PERFORM_BED_MESH=0 means skip") {
        CapabilityMatrix matrix;

        PrintStartAnalysis analysis = make_macro_analysis("PRINT_START");
        add_analysis_op(analysis, PrintStartOpCategory::BED_MESH, "BED_MESH_CALIBRATE", true,
                        "PERFORM_BED_MESH", ParameterSemantic::OPT_IN);
        matrix.add_from_macro_analysis(analysis);

        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source->semantic == ParameterSemantic::OPT_IN);
        REQUIRE(source->skip_value == "0");   // 0 = skip
        REQUIRE(source->enable_value == "1"); // 1 = do it

        auto skip = matrix.get_skip_param(OperationCategory::BED_MESH);
        REQUIRE(skip->first == "PERFORM_BED_MESH");
        REQUIRE(skip->second == "0");
    }

    SECTION("Database source preserves original values") {
        CapabilityMatrix matrix;

        // Database uses string values like "true"/"false" instead of 1/0
        PrintStartCapabilities caps = make_database_caps("START_PRINT");
        add_cap_param(caps, "bed_mesh", "FORCE_LEVELING", "false", "true");
        matrix.add_from_database(caps);

        auto source = matrix.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source->skip_value == "false");
        REQUIRE(source->enable_value == "true");

        auto skip = matrix.get_skip_param(OperationCategory::BED_MESH);
        REQUIRE(skip->second == "false"); // Database preserves original value
    }
}

// ============================================================================
// Test Cases: Clear Behavior
// ============================================================================

TEST_CASE("CapabilityMatrix: Clear behavior", "[capability_matrix][print_preparation]") {
    CapabilityMatrix matrix;

    // Add sources
    PrintStartCapabilities caps = make_database_caps("START_PRINT");
    add_cap_param(caps, "bed_mesh", "FORCE_LEVELING", "false", "true");
    matrix.add_from_database(caps);

    PrintStartAnalysis analysis = make_macro_analysis("PRINT_START");
    add_analysis_op(analysis, PrintStartOpCategory::QGL, "QUAD_GANTRY_LEVEL", true, "SKIP_QGL",
                    ParameterSemantic::OPT_OUT);
    matrix.add_from_macro_analysis(analysis);

    // Verify data is present
    REQUIRE(matrix.is_controllable(OperationCategory::BED_MESH) == true);
    REQUIRE(matrix.is_controllable(OperationCategory::QGL) == true);
    REQUIRE(matrix.has_any_controllable() == true);

    // Clear
    matrix.clear();

    SECTION("After clear, matrix behaves as empty") {
        REQUIRE(matrix.is_controllable(OperationCategory::BED_MESH) == false);
        REQUIRE(matrix.is_controllable(OperationCategory::QGL) == false);
        REQUIRE(matrix.has_any_controllable() == false);
        REQUIRE(matrix.get_best_source(OperationCategory::BED_MESH) == std::nullopt);
        REQUIRE(matrix.get_controllable_operations().empty());
    }

    SECTION("Can add sources again after clear") {
        PrintStartCapabilities new_caps = make_database_caps("START_PRINT");
        add_cap_param(new_caps, "nozzle_clean", "CLEAN_NOZZLE", "false", "true");
        matrix.add_from_database(new_caps);

        REQUIRE(matrix.is_controllable(OperationCategory::NOZZLE_CLEAN) == true);
        // Old capabilities should not be present
        REQUIRE(matrix.is_controllable(OperationCategory::BED_MESH) == false);
    }
}

// ============================================================================
// Test Cases: Edge Cases
// ============================================================================

TEST_CASE("CapabilityMatrix: Edge cases", "[capability_matrix][print_preparation][edge]") {
    SECTION("Adding empty database caps does not add capabilities") {
        CapabilityMatrix matrix;
        PrintStartCapabilities caps; // Empty
        matrix.add_from_database(caps);

        REQUIRE(matrix.has_any_controllable() == false);
    }

    SECTION("Adding analysis with no controllable operations does not add capabilities") {
        CapabilityMatrix matrix;
        PrintStartAnalysis analysis = make_macro_analysis("PRINT_START");
        // Add operation without skip param
        add_analysis_op(analysis, PrintStartOpCategory::BED_MESH, "BED_MESH_CALIBRATE", false, "",
                        ParameterSemantic::OPT_OUT);
        matrix.add_from_macro_analysis(analysis);

        REQUIRE(matrix.has_any_controllable() == false);
    }

    SECTION("File scan with NOT_FOUND embedding is not added") {
        CapabilityMatrix matrix;
        ScanResult scan = make_file_scan();
        DetectedOperation op;
        op.type = OperationType::BED_MESH;
        op.embedding = OperationEmbedding::NOT_FOUND;
        scan.operations.push_back(op);
        matrix.add_from_file_scan(scan);

        REQUIRE(matrix.has_any_controllable() == false);
    }

    SECTION("Multiple calls to same add method accumulate") {
        CapabilityMatrix matrix;

        PrintStartCapabilities caps1 = make_database_caps("START_PRINT");
        add_cap_param(caps1, "bed_mesh", "FORCE_LEVELING", "false", "true");
        matrix.add_from_database(caps1);

        PrintStartCapabilities caps2 = make_database_caps("START_PRINT");
        add_cap_param(caps2, "qgl", "FORCE_QGL", "false", "true");
        matrix.add_from_database(caps2);

        REQUIRE(matrix.is_controllable(OperationCategory::BED_MESH) == true);
        REQUIRE(matrix.is_controllable(OperationCategory::QGL) == true);
    }
}

// ============================================================================
// Test Cases: Capability Name Mapping
// ============================================================================

TEST_CASE("CapabilityMatrix: Database capability name mapping",
          "[capability_matrix][print_preparation]") {
    SECTION("Maps 'bed_mesh' to OperationCategory::BED_MESH") {
        CapabilityMatrix matrix;
        PrintStartCapabilities caps = make_database_caps("START_PRINT");
        add_cap_param(caps, "bed_mesh", "FORCE_LEVELING", "false", "true");
        matrix.add_from_database(caps);

        REQUIRE(matrix.is_controllable(OperationCategory::BED_MESH) == true);
    }

    SECTION("Maps 'qgl' to OperationCategory::QGL") {
        CapabilityMatrix matrix;
        PrintStartCapabilities caps = make_database_caps("START_PRINT");
        add_cap_param(caps, "qgl", "SKIP_QGL", "1", "0");
        matrix.add_from_database(caps);

        REQUIRE(matrix.is_controllable(OperationCategory::QGL) == true);
    }

    SECTION("Maps 'bed_level' to both QGL and Z_TILT (unified)") {
        CapabilityMatrix matrix;
        PrintStartCapabilities caps = make_database_caps("START_PRINT");
        add_cap_param(caps, "bed_level", "SKIP_BED_LEVEL", "1", "0");
        matrix.add_from_database(caps);

        // bed_level should map to both QGL and Z_TILT as unified category
        REQUIRE(matrix.is_controllable(OperationCategory::BED_LEVEL) == true);
    }

    SECTION("Maps 'nozzle_clean' to OperationCategory::NOZZLE_CLEAN") {
        CapabilityMatrix matrix;
        PrintStartCapabilities caps = make_database_caps("START_PRINT");
        add_cap_param(caps, "nozzle_clean", "CLEAN_NOZZLE", "false", "true");
        matrix.add_from_database(caps);

        REQUIRE(matrix.is_controllable(OperationCategory::NOZZLE_CLEAN) == true);
    }

    SECTION("Maps 'purge_line' to OperationCategory::PURGE_LINE") {
        CapabilityMatrix matrix;
        PrintStartCapabilities caps = make_database_caps("START_PRINT");
        add_cap_param(caps, "purge_line", "SKIP_PURGE", "1", "0");
        matrix.add_from_database(caps);

        REQUIRE(matrix.is_controllable(OperationCategory::PURGE_LINE) == true);
    }
}

// ============================================================================
// Test Cases: Line Number Tracking
// ============================================================================

TEST_CASE("CapabilityMatrix: Line number tracking for file operations",
          "[capability_matrix][print_preparation]") {
    CapabilityMatrix matrix;

    ScanResult scan = make_file_scan();
    add_scan_op(scan, OperationType::BED_MESH, OperationEmbedding::DIRECT_COMMAND,
                "BED_MESH_CALIBRATE", 42);
    add_scan_op(scan, OperationType::QGL, OperationEmbedding::DIRECT_COMMAND, "QUAD_GANTRY_LEVEL",
                38);

    matrix.add_from_file_scan(scan);

    SECTION("Line numbers are preserved in source") {
        auto bed_mesh = matrix.get_best_source(OperationCategory::BED_MESH);
        auto qgl = matrix.get_best_source(OperationCategory::QGL);

        REQUIRE(bed_mesh->line_number == 42);
        REQUIRE(qgl->line_number == 38);
    }

    SECTION("Non-file sources have line_number=0") {
        CapabilityMatrix matrix2;
        PrintStartCapabilities caps = make_database_caps("START_PRINT");
        add_cap_param(caps, "bed_mesh", "FORCE_LEVELING", "false", "true");
        matrix2.add_from_database(caps);

        auto source = matrix2.get_best_source(OperationCategory::BED_MESH);
        REQUIRE(source->line_number == 0);
    }
}
