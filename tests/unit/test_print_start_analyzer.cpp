// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_types.h"
#include "operation_patterns.h"
#include "print_start_analyzer.h"

#include <algorithm> // for std::find

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// Test Macros (representative samples from real printers)
// ============================================================================

// Basic Voron-style PRINT_START with bed mesh and QGL
static const char* BASIC_PRINT_START = R"(
; Basic PRINT_START with common operations
G28                             ; Home all axes
QUAD_GANTRY_LEVEL               ; Level the gantry
BED_MESH_CALIBRATE              ; Create bed mesh
CLEAN_NOZZLE                    ; Clean the nozzle
M109 S{params.EXTRUDER|default(210)|float}
)";

// Advanced PRINT_START with skip parameters already defined
static const char* CONTROLLABLE_PRINT_START = R"(
{% set BED_TEMP = params.BED|default(60)|float %}
{% set EXTRUDER_TEMP = params.EXTRUDER|default(210)|float %}
{% set SKIP_BED_MESH = params.SKIP_BED_MESH|default(0)|int %}
{% set SKIP_QGL = params.SKIP_QGL|default(0)|int %}

G28                             ; Home all axes

{% if SKIP_QGL == 0 %}
    QUAD_GANTRY_LEVEL           ; Level the gantry
{% endif %}

{% if SKIP_BED_MESH == 0 %}
    BED_MESH_CALIBRATE          ; Create bed mesh
{% endif %}

M190 S{BED_TEMP}
M109 S{EXTRUDER_TEMP}
)";

// PRINT_START with only some operations controllable
static const char* PARTIAL_CONTROLLABLE = R"(
{% set SKIP_MESH = params.SKIP_MESH|default(0)|int %}
{% set BED = params.BED|default(60)|float %}

G28
QUAD_GANTRY_LEVEL               ; Always runs - not controllable

{% if SKIP_MESH == 0 %}
    BED_MESH_CALIBRATE
{% endif %}

CLEAN_NOZZLE                    ; Always runs - not controllable
M109 S{params.EXTRUDER|default(210)|float}
)";

// Empty/minimal macro
static const char* MINIMAL_PRINT_START = R"(
G28
M109 S{params.EXTRUDER}
M190 S{params.BED}
)";

// Macro with alternative parameter patterns
static const char* ALT_PATTERN_PRINT_START = R"(
{% set bed_temp = params.BED_TEMP|default(60)|float %}
{% set nozzle_temp = params.NOZZLE_TEMP|default(210)|float %}
{% set force_level = params.FORCE_LEVEL|default(0)|int %}

G28
{% if not SKIP_GANTRY %}
QUAD_GANTRY_LEVEL
{% endif %}

BED_MESH_CALIBRATE PROFILE=default
M109 S{nozzle_temp}
M190 S{bed_temp}
)";

// ============================================================================
// Tests: Operation Detection
// ============================================================================

TEST_CASE("PrintStartAnalyzer: Basic operation detection", "[print_start][parsing]") {
    auto result = PrintStartAnalyzer::parse_macro("PRINT_START", BASIC_PRINT_START);

    REQUIRE(result.found == true);
    REQUIRE(result.macro_name == "PRINT_START");

    SECTION("Detects all operations") {
        REQUIRE(result.total_ops_count >= 4);
        REQUIRE(result.has_operation(PrintStartOpCategory::HOMING));
        REQUIRE(result.has_operation(PrintStartOpCategory::QGL));
        REQUIRE(result.has_operation(PrintStartOpCategory::BED_MESH));
        REQUIRE(result.has_operation(PrintStartOpCategory::NOZZLE_CLEAN));
    }

    SECTION("No operations are controllable in basic macro") {
        REQUIRE(result.is_controllable == false);
        REQUIRE(result.controllable_count == 0);
    }

    SECTION("Can get specific operations") {
        auto qgl = result.get_operation(PrintStartOpCategory::QGL);
        REQUIRE(qgl != nullptr);
        REQUIRE(qgl->name == "QUAD_GANTRY_LEVEL");
        REQUIRE(qgl->has_skip_param == false);
    }
}

TEST_CASE("PrintStartAnalyzer: Controllable operation detection", "[print_start][parsing]") {
    auto result = PrintStartAnalyzer::parse_macro("PRINT_START", CONTROLLABLE_PRINT_START);

    SECTION("Detects controllable operations") {
        REQUIRE(result.is_controllable == true);
        REQUIRE(result.controllable_count >= 2);
    }

    SECTION("QGL is controllable via SKIP_QGL") {
        auto qgl = result.get_operation(PrintStartOpCategory::QGL);
        REQUIRE(qgl != nullptr);
        REQUIRE(qgl->has_skip_param == true);
        REQUIRE(qgl->skip_param_name == "SKIP_QGL");
    }

    SECTION("Bed mesh is controllable via SKIP_BED_MESH") {
        auto mesh = result.get_operation(PrintStartOpCategory::BED_MESH);
        REQUIRE(mesh != nullptr);
        REQUIRE(mesh->has_skip_param == true);
        REQUIRE(mesh->skip_param_name == "SKIP_BED_MESH");
    }

    SECTION("Homing is always detected but not controllable") {
        auto homing = result.get_operation(PrintStartOpCategory::HOMING);
        REQUIRE(homing != nullptr);
        REQUIRE(homing->has_skip_param == false);
    }

    SECTION("Extracts known parameters") {
        REQUIRE(result.known_params.size() >= 4);
        // Should include BED, EXTRUDER, SKIP_BED_MESH, SKIP_QGL
        auto has_param = [&](const std::string& name) {
            return std::find(result.known_params.begin(), result.known_params.end(), name) !=
                   result.known_params.end();
        };
        REQUIRE(has_param("BED"));
        REQUIRE(has_param("EXTRUDER"));
        REQUIRE(has_param("SKIP_BED_MESH"));
        REQUIRE(has_param("SKIP_QGL"));
    }
}

TEST_CASE("PrintStartAnalyzer: Partial controllability", "[print_start][parsing]") {
    auto result = PrintStartAnalyzer::parse_macro("PRINT_START", PARTIAL_CONTROLLABLE);

    SECTION("Detects mixed controllability") {
        REQUIRE(result.is_controllable == true);
        REQUIRE(result.controllable_count == 1);
        REQUIRE(result.total_ops_count >= 3);
    }

    SECTION("Bed mesh is controllable via SKIP_MESH variant") {
        auto mesh = result.get_operation(PrintStartOpCategory::BED_MESH);
        REQUIRE(mesh != nullptr);
        REQUIRE(mesh->has_skip_param == true);
        REQUIRE(mesh->skip_param_name == "SKIP_MESH");
    }

    SECTION("QGL is NOT controllable") {
        auto qgl = result.get_operation(PrintStartOpCategory::QGL);
        REQUIRE(qgl != nullptr);
        REQUIRE(qgl->has_skip_param == false);
    }

    SECTION("get_uncontrollable_operations returns QGL and NOZZLE_CLEAN") {
        auto uncontrollable = result.get_uncontrollable_operations();
        // Should include QGL and NOZZLE_CLEAN, but NOT HOMING (excluded by design)
        REQUIRE(uncontrollable.size() >= 2);

        bool has_qgl = false;
        bool has_clean = false;
        for (auto* op : uncontrollable) {
            if (op->category == PrintStartOpCategory::QGL)
                has_qgl = true;
            if (op->category == PrintStartOpCategory::NOZZLE_CLEAN)
                has_clean = true;
        }
        REQUIRE(has_qgl);
        REQUIRE(has_clean);
    }
}

TEST_CASE("PrintStartAnalyzer: Minimal macro", "[print_start][parsing]") {
    auto result = PrintStartAnalyzer::parse_macro("PRINT_START", MINIMAL_PRINT_START);

    SECTION("Detects only homing") {
        REQUIRE(result.total_ops_count == 1);
        REQUIRE(result.has_operation(PrintStartOpCategory::HOMING));
        REQUIRE_FALSE(result.has_operation(PrintStartOpCategory::BED_MESH));
        REQUIRE_FALSE(result.has_operation(PrintStartOpCategory::QGL));
    }

    SECTION("Extracts basic parameters") {
        REQUIRE(result.known_params.size() >= 2);
        auto has_param = [&](const std::string& name) {
            return std::find(result.known_params.begin(), result.known_params.end(), name) !=
                   result.known_params.end();
        };
        REQUIRE(has_param("EXTRUDER"));
        REQUIRE(has_param("BED"));
    }
}

TEST_CASE("PrintStartAnalyzer: Alternative skip parameter patterns", "[print_start][parsing]") {
    auto result = PrintStartAnalyzer::parse_macro("PRINT_START", ALT_PATTERN_PRINT_START);

    SECTION("Detects QGL with SKIP_GANTRY variant") {
        auto qgl = result.get_operation(PrintStartOpCategory::QGL);
        REQUIRE(qgl != nullptr);
        REQUIRE(qgl->has_skip_param == true);
        REQUIRE(qgl->skip_param_name == "SKIP_GANTRY");
    }

    SECTION("Extracts alternative parameter names") {
        auto has_param = [&](const std::string& name) {
            return std::find(result.known_params.begin(), result.known_params.end(), name) !=
                   result.known_params.end();
        };
        REQUIRE(has_param("BED_TEMP"));
        REQUIRE(has_param("NOZZLE_TEMP"));
        REQUIRE(has_param("FORCE_LEVEL"));
    }
}

// ============================================================================
// Tests: Helper Functions
// ============================================================================

TEST_CASE("PrintStartAnalyzer: categorize_operation", "[print_start][helpers]") {
    REQUIRE(PrintStartAnalyzer::categorize_operation("BED_MESH_CALIBRATE") ==
            PrintStartOpCategory::BED_MESH);
    REQUIRE(PrintStartAnalyzer::categorize_operation("G29") == PrintStartOpCategory::BED_MESH);
    REQUIRE(PrintStartAnalyzer::categorize_operation("QUAD_GANTRY_LEVEL") ==
            PrintStartOpCategory::QGL);
    REQUIRE(PrintStartAnalyzer::categorize_operation("Z_TILT_ADJUST") ==
            PrintStartOpCategory::Z_TILT);
    REQUIRE(PrintStartAnalyzer::categorize_operation("CLEAN_NOZZLE") ==
            PrintStartOpCategory::NOZZLE_CLEAN);
    REQUIRE(PrintStartAnalyzer::categorize_operation("G28") == PrintStartOpCategory::HOMING);
    REQUIRE(PrintStartAnalyzer::categorize_operation("UNKNOWN_CMD") ==
            PrintStartOpCategory::UNKNOWN);
}

TEST_CASE("PrintStartAnalyzer: get_suggested_skip_param", "[print_start][helpers]") {
    REQUIRE(PrintStartAnalyzer::get_suggested_skip_param("BED_MESH_CALIBRATE") == "SKIP_BED_MESH");
    REQUIRE(PrintStartAnalyzer::get_suggested_skip_param("QUAD_GANTRY_LEVEL") == "SKIP_QGL");
    REQUIRE(PrintStartAnalyzer::get_suggested_skip_param("Z_TILT_ADJUST") == "SKIP_Z_TILT");
    REQUIRE(PrintStartAnalyzer::get_suggested_skip_param("CLEAN_NOZZLE") == "SKIP_NOZZLE_CLEAN");

    // Unknown operation should return SKIP_ + name
    REQUIRE(PrintStartAnalyzer::get_suggested_skip_param("CUSTOM_OP") == "SKIP_CUSTOM_OP");
}

TEST_CASE("PrintStartAnalyzer: category_to_string", "[print_start][helpers]") {
    REQUIRE(std::string(category_to_string(PrintStartOpCategory::BED_MESH)) == "bed_mesh");
    REQUIRE(std::string(category_to_string(PrintStartOpCategory::QGL)) == "qgl");
    REQUIRE(std::string(category_to_string(PrintStartOpCategory::Z_TILT)) == "z_tilt");
    REQUIRE(std::string(category_to_string(PrintStartOpCategory::NOZZLE_CLEAN)) == "nozzle_clean");
    REQUIRE(std::string(category_to_string(PrintStartOpCategory::HOMING)) == "homing");
    REQUIRE(std::string(category_to_string(PrintStartOpCategory::UNKNOWN)) == "unknown");
}

TEST_CASE("PrintStartAnalyzer: summary generation", "[print_start][helpers]") {
    SECTION("Found macro summary") {
        auto result = PrintStartAnalyzer::parse_macro("PRINT_START", CONTROLLABLE_PRINT_START);
        auto summary = result.summary();

        REQUIRE(summary.find("PRINT_START") != std::string::npos);
        REQUIRE(summary.find("controllable") != std::string::npos);
    }

    SECTION("Not found summary") {
        PrintStartAnalysis result;
        result.found = false;
        auto summary = result.summary();

        REQUIRE(summary.find("No print start macro found") != std::string::npos);
    }
}

// ============================================================================
// Tests: Edge Cases
// ============================================================================

TEST_CASE("PrintStartAnalyzer: Empty macro", "[print_start][edge]") {
    auto result = PrintStartAnalyzer::parse_macro("PRINT_START", "");

    REQUIRE(result.found == true);
    REQUIRE(result.total_ops_count == 0);
    REQUIRE(result.is_controllable == false);
}

TEST_CASE("PrintStartAnalyzer: Comments only", "[print_start][edge]") {
    const char* comments_only = R"(
; This is a comment
# This is also a comment
    ; Indented comment
)";
    auto result = PrintStartAnalyzer::parse_macro("PRINT_START", comments_only);

    REQUIRE(result.total_ops_count == 0);
}

TEST_CASE("PrintStartAnalyzer: Operations with parameters", "[print_start][edge]") {
    const char* ops_with_params = R"(
G28 X Y                         ; Home X and Y only
BED_MESH_CALIBRATE PROFILE=default
QUAD_GANTRY_LEVEL RETRIES=5
)";
    auto result = PrintStartAnalyzer::parse_macro("PRINT_START", ops_with_params);

    REQUIRE(result.has_operation(PrintStartOpCategory::HOMING));
    REQUIRE(result.has_operation(PrintStartOpCategory::BED_MESH));
    REQUIRE(result.has_operation(PrintStartOpCategory::QGL));
}

TEST_CASE("PrintStartAnalyzer: Case insensitive operation detection", "[print_start][edge]") {
    const char* mixed_case = R"(
g28
bed_mesh_calibrate
Quad_Gantry_Level
)";
    auto result = PrintStartAnalyzer::parse_macro("PRINT_START", mixed_case);

    REQUIRE(result.has_operation(PrintStartOpCategory::HOMING));
    REQUIRE(result.has_operation(PrintStartOpCategory::BED_MESH));
    REQUIRE(result.has_operation(PrintStartOpCategory::QGL));
}

// ============================================================================
// Tests: New Helper Functions (operation_patterns.h)
// ============================================================================

TEST_CASE("PrintStart: is_bed_level_category helper", "[print_start]") {
    using helix::is_bed_level_category;
    using helix::OperationCategory;

    SECTION("Returns true for physical leveling categories") {
        REQUIRE(is_bed_level_category(OperationCategory::BED_LEVEL) == true);
        REQUIRE(is_bed_level_category(OperationCategory::QGL) == true);
        REQUIRE(is_bed_level_category(OperationCategory::Z_TILT) == true);
    }

    SECTION("Returns false for other categories") {
        REQUIRE(is_bed_level_category(OperationCategory::BED_MESH) == false);
        REQUIRE(is_bed_level_category(OperationCategory::NOZZLE_CLEAN) == false);
        REQUIRE(is_bed_level_category(OperationCategory::HOMING) == false);
    }
}

TEST_CASE("PrintStart: get_all_skip_variations includes unified BED_LEVEL", "[print_start]") {
    using helix::get_all_skip_variations;
    using helix::get_skip_variations;
    using helix::OperationCategory;

    SECTION("QGL includes both QGL and BED_LEVEL variations") {
        auto qgl_all = get_all_skip_variations(OperationCategory::QGL);
        auto qgl_own = get_skip_variations(OperationCategory::QGL);
        auto bed_level = get_skip_variations(OperationCategory::BED_LEVEL);

        // Should include own variations
        for (const auto& var : qgl_own) {
            REQUIRE(std::find(qgl_all.begin(), qgl_all.end(), var) != qgl_all.end());
        }
        // Should include BED_LEVEL variations
        for (const auto& var : bed_level) {
            REQUIRE(std::find(qgl_all.begin(), qgl_all.end(), var) != qgl_all.end());
        }
    }

    SECTION("Z_TILT includes both Z_TILT and BED_LEVEL variations") {
        auto ztilt_all = get_all_skip_variations(OperationCategory::Z_TILT);
        auto ztilt_own = get_skip_variations(OperationCategory::Z_TILT);
        auto bed_level = get_skip_variations(OperationCategory::BED_LEVEL);

        // Should include own variations
        for (const auto& var : ztilt_own) {
            REQUIRE(std::find(ztilt_all.begin(), ztilt_all.end(), var) != ztilt_all.end());
        }
        // Should include BED_LEVEL variations
        for (const auto& var : bed_level) {
            REQUIRE(std::find(ztilt_all.begin(), ztilt_all.end(), var) != ztilt_all.end());
        }
    }

    SECTION("BED_MESH only includes own variations (not BED_LEVEL)") {
        auto mesh_all = get_all_skip_variations(OperationCategory::BED_MESH);
        auto mesh_own = get_skip_variations(OperationCategory::BED_MESH);

        REQUIRE(mesh_all.size() == mesh_own.size());
    }
}

// ============================================================================
// Tests: PERFORM_* Opt-In Parameter Detection (NEW)
// ============================================================================

// Opt-in macro using PERFORM_* pattern (HelixScreen standard)
static const char* PERFORM_PRINT_START = R"(
{% set bed_temp = params.BED_TEMP|default(60)|float %}
{% set extruder_temp = params.EXTRUDER_TEMP|default(200)|float %}
{% set perform_qgl = params.PERFORM_QGL|default(0)|int %}
{% set perform_bed_mesh = params.PERFORM_BED_MESH|default(0)|int %}

G28

{% if perform_qgl == 1 %}
    QUAD_GANTRY_LEVEL
{% endif %}

{% if perform_bed_mesh == 1 %}
    BED_MESH_CALIBRATE
{% endif %}

M190 S{bed_temp}
M109 S{extruder_temp}
)";

// Opt-in macro using DO_* pattern (compatibility with existing helix_macros.cfg)
static const char* DO_STYLE_PRINT_START = R"(
{% set do_qgl = params.DO_QGL|default(0)|int %}
{% set do_bed_mesh = params.DO_BED_MESH|default(0)|int %}
{% set do_nozzle_clean = params.DO_NOZZLE_CLEAN|default(0)|int %}

G28

{% if do_qgl == 1 %}
    QUAD_GANTRY_LEVEL
{% endif %}

{% if do_bed_mesh == 1 %}
    BED_MESH_CALIBRATE
{% endif %}

{% if do_nozzle_clean == 1 %}
    CLEAN_NOZZLE
{% endif %}
)";

// AD5M Klipper Mod style with FORCE_LEVELING (real-world compatibility)
static const char* FORCE_LEVELING_PRINT_START = R"(
{% set bed_temp = params.BED_TEMP|default(60)|float %}
{% set extruder_temp = params.EXTRUDER_TEMP|default(200)|float %}
{% set force_leveling = params.FORCE_LEVELING|default(false) %}

M140 S{bed_temp}
G28

{% if (not printer['bed_mesh'].profile_name) or force_leveling %}
    AUTO_BED_LEVEL BED_TEMP={bed_temp} EXTRUDER_TEMP={extruder_temp}
{% endif %}

M109 S{extruder_temp}
)";

// Mixed macro with both SKIP_* (opt-out) and PERFORM_* (opt-in)
static const char* MIXED_SEMANTIC_PRINT_START = R"(
{% set skip_qgl = params.SKIP_QGL|default(0)|int %}
{% set perform_bed_mesh = params.PERFORM_BED_MESH|default(0)|int %}

G28

{% if skip_qgl == 0 %}
    QUAD_GANTRY_LEVEL
{% endif %}

{% if perform_bed_mesh == 1 %}
    BED_MESH_CALIBRATE
{% endif %}
)";

// Uncontrollable macro - uses custom variable, not recognized pattern
static const char* UNCONTROLLABLE_PRINT_START = R"(
{% set bed_temp = params.BED_TEMP|default(60)|float %}

G28

{% if printer['bed_mesh'].profile_name == '' %}
    BED_MESH_CALIBRATE
{% endif %}

QUAD_GANTRY_LEVEL
)";

TEST_CASE("PrintStartAnalyzer: PERFORM_* opt-in pattern detection",
          "[print_start][perform][opt_in]") {
    auto result = PrintStartAnalyzer::parse_macro("PRINT_START", PERFORM_PRINT_START);

    REQUIRE(result.found == true);
    REQUIRE(result.is_controllable == true);

    SECTION("Detects QGL controllable via PERFORM_QGL") {
        auto qgl = result.get_operation(PrintStartOpCategory::QGL);
        REQUIRE(qgl != nullptr);
        REQUIRE(qgl->has_skip_param == true);
        REQUIRE(qgl->skip_param_name == "PERFORM_QGL");
    }

    SECTION("Detects bed mesh controllable via PERFORM_BED_MESH") {
        auto mesh = result.get_operation(PrintStartOpCategory::BED_MESH);
        REQUIRE(mesh != nullptr);
        REQUIRE(mesh->has_skip_param == true);
        REQUIRE(mesh->skip_param_name == "PERFORM_BED_MESH");
    }

    SECTION("PERFORM_* parameters have OPT_IN semantic") {
        auto qgl = result.get_operation(PrintStartOpCategory::QGL);
        auto mesh = result.get_operation(PrintStartOpCategory::BED_MESH);
        REQUIRE(qgl->param_semantic == ParameterSemantic::OPT_IN);
        REQUIRE(mesh->param_semantic == ParameterSemantic::OPT_IN);
    }
}

TEST_CASE("PrintStartAnalyzer: DO_* opt-in pattern detection", "[print_start][perform][opt_in]") {
    auto result = PrintStartAnalyzer::parse_macro("PRINT_START", DO_STYLE_PRINT_START);

    REQUIRE(result.found == true);
    REQUIRE(result.is_controllable == true);
    REQUIRE(result.controllable_count >= 3);

    SECTION("Detects QGL controllable via DO_QGL") {
        auto qgl = result.get_operation(PrintStartOpCategory::QGL);
        REQUIRE(qgl != nullptr);
        REQUIRE(qgl->has_skip_param == true);
        REQUIRE(qgl->skip_param_name == "DO_QGL");
        REQUIRE(qgl->param_semantic == ParameterSemantic::OPT_IN);
    }

    SECTION("Detects bed mesh controllable via DO_BED_MESH") {
        auto mesh = result.get_operation(PrintStartOpCategory::BED_MESH);
        REQUIRE(mesh != nullptr);
        REQUIRE(mesh->has_skip_param == true);
        REQUIRE(mesh->skip_param_name == "DO_BED_MESH");
        REQUIRE(mesh->param_semantic == ParameterSemantic::OPT_IN);
    }

    SECTION("Detects nozzle clean controllable via DO_NOZZLE_CLEAN") {
        auto clean = result.get_operation(PrintStartOpCategory::NOZZLE_CLEAN);
        REQUIRE(clean != nullptr);
        REQUIRE(clean->has_skip_param == true);
        REQUIRE(clean->skip_param_name == "DO_NOZZLE_CLEAN");
        REQUIRE(clean->param_semantic == ParameterSemantic::OPT_IN);
    }
}

TEST_CASE("PrintStartAnalyzer: FORCE_LEVELING compatibility", "[print_start][perform][opt_in]") {
    auto result = PrintStartAnalyzer::parse_macro("START_PRINT", FORCE_LEVELING_PRINT_START);

    REQUIRE(result.found == true);

    SECTION("Detects bed mesh controllable via FORCE_LEVELING") {
        auto mesh = result.get_operation(PrintStartOpCategory::BED_MESH);
        REQUIRE(mesh != nullptr);
        REQUIRE(mesh->has_skip_param == true);
        REQUIRE(mesh->skip_param_name == "FORCE_LEVELING");
        REQUIRE(mesh->param_semantic == ParameterSemantic::OPT_IN);
    }
}

TEST_CASE("PrintStartAnalyzer: Mixed semantic detection (SKIP_* and PERFORM_*)",
          "[print_start][perform][mixed]") {
    auto result = PrintStartAnalyzer::parse_macro("PRINT_START", MIXED_SEMANTIC_PRINT_START);

    REQUIRE(result.found == true);
    REQUIRE(result.is_controllable == true);
    REQUIRE(result.controllable_count == 2);

    SECTION("QGL is opt-out via SKIP_QGL") {
        auto qgl = result.get_operation(PrintStartOpCategory::QGL);
        REQUIRE(qgl != nullptr);
        REQUIRE(qgl->has_skip_param == true);
        REQUIRE(qgl->skip_param_name == "SKIP_QGL");
        REQUIRE(qgl->param_semantic == ParameterSemantic::OPT_OUT);
    }

    SECTION("Bed mesh is opt-in via PERFORM_BED_MESH") {
        auto mesh = result.get_operation(PrintStartOpCategory::BED_MESH);
        REQUIRE(mesh != nullptr);
        REQUIRE(mesh->has_skip_param == true);
        REQUIRE(mesh->skip_param_name == "PERFORM_BED_MESH");
        REQUIRE(mesh->param_semantic == ParameterSemantic::OPT_IN);
    }
}

TEST_CASE("PrintStartAnalyzer: Existing SKIP_* patterns retain OPT_OUT semantic",
          "[print_start][perform][backward_compat]") {
    auto result = PrintStartAnalyzer::parse_macro("PRINT_START", CONTROLLABLE_PRINT_START);

    SECTION("SKIP_QGL has OPT_OUT semantic") {
        auto qgl = result.get_operation(PrintStartOpCategory::QGL);
        REQUIRE(qgl != nullptr);
        REQUIRE(qgl->param_semantic == ParameterSemantic::OPT_OUT);
    }

    SECTION("SKIP_BED_MESH has OPT_OUT semantic") {
        auto mesh = result.get_operation(PrintStartOpCategory::BED_MESH);
        REQUIRE(mesh != nullptr);
        REQUIRE(mesh->param_semantic == ParameterSemantic::OPT_OUT);
    }
}

TEST_CASE("PrintStartAnalyzer: Uncontrollable macro detection", "[print_start][perform][edge]") {
    auto result = PrintStartAnalyzer::parse_macro("PRINT_START", UNCONTROLLABLE_PRINT_START);

    REQUIRE(result.found == true);

    SECTION("Bed mesh is NOT controllable (uses custom variable)") {
        auto mesh = result.get_operation(PrintStartOpCategory::BED_MESH);
        REQUIRE(mesh != nullptr);
        REQUIRE(mesh->has_skip_param == false);
    }

    SECTION("QGL is NOT controllable (no conditional)") {
        auto qgl = result.get_operation(PrintStartOpCategory::QGL);
        REQUIRE(qgl != nullptr);
        REQUIRE(qgl->has_skip_param == false);
    }

    SECTION("Macro has operations but none are controllable") {
        REQUIRE(result.total_ops_count >= 2);
        REQUIRE(result.controllable_count == 0);
        REQUIRE(result.is_controllable == false);
    }
}

TEST_CASE("PrintStart: get_all_perform_variations helper", "[print_start][perform][helpers]") {
    using helix::get_all_perform_variations;
    using helix::OperationCategory;

    SECTION("BED_MESH includes PERFORM, DO, ENABLE, FORCE variants") {
        auto variations = get_all_perform_variations(OperationCategory::BED_MESH);

        REQUIRE(std::find(variations.begin(), variations.end(), "PERFORM_BED_MESH") !=
                variations.end());
        REQUIRE(std::find(variations.begin(), variations.end(), "DO_BED_MESH") != variations.end());
        REQUIRE(std::find(variations.begin(), variations.end(), "FORCE_BED_MESH") !=
                variations.end());
        REQUIRE(std::find(variations.begin(), variations.end(), "FORCE_LEVELING") !=
                variations.end());
    }

    SECTION("QGL includes PERFORM, DO, ENABLE, FORCE variants") {
        auto variations = get_all_perform_variations(OperationCategory::QGL);

        REQUIRE(std::find(variations.begin(), variations.end(), "PERFORM_QGL") != variations.end());
        REQUIRE(std::find(variations.begin(), variations.end(), "DO_QGL") != variations.end());
    }

    SECTION("NOZZLE_CLEAN includes PERFORM, DO variants") {
        auto variations = get_all_perform_variations(OperationCategory::NOZZLE_CLEAN);

        REQUIRE(std::find(variations.begin(), variations.end(), "PERFORM_NOZZLE_CLEAN") !=
                variations.end());
        REQUIRE(std::find(variations.begin(), variations.end(), "DO_NOZZLE_CLEAN") !=
                variations.end());
    }
}

// ============================================================================
// Tests: Subdirectory Path Handling (FileInfo.path vs filename)
// ============================================================================

TEST_CASE("PrintStartAnalyzer: get_config_file_path helper", "[print_start][path]") {
    // Tests the get_config_file_path() helper used in analyze() when processing FileInfo
    // Bug fix: Previously used f.filename which loses subdirectory info

    SECTION("Files in subdirectory - returns full path") {
        FileInfo file_in_subdir;
        file_in_subdir.path = "conf.d/autotune_motors.cfg";
        file_in_subdir.filename = "autotune_motors.cfg";

        REQUIRE(get_config_file_path(file_in_subdir) == "conf.d/autotune_motors.cfg");
    }

    SECTION("Files in root directory - path equals filename") {
        FileInfo file_in_root;
        file_in_root.path = "printer.cfg";
        file_in_root.filename = "printer.cfg";

        REQUIRE(get_config_file_path(file_in_root) == "printer.cfg");
    }

    SECTION("Legacy response - empty path, falls back to filename") {
        FileInfo legacy_file;
        legacy_file.path = "";
        legacy_file.filename = "printer.cfg";

        REQUIRE(get_config_file_path(legacy_file) == "printer.cfg");
    }

    SECTION("Nested subdirectory - preserves full path") {
        FileInfo nested_file;
        nested_file.path = "conf.d/macros/print_start.cfg";
        nested_file.filename = "print_start.cfg";

        REQUIRE(get_config_file_path(nested_file) == "conf.d/macros/print_start.cfg");
    }
}
