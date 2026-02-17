// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_preprint_predictor.cpp
 * @brief Unit tests for PreprintPredictor weighted average and remaining time
 *
 * Tests pure prediction logic without LVGL or Config dependencies.
 */

#include "preprint_predictor.h"

#include <set>

#include "../catch_amalgamated.hpp"

using namespace helix;
using helix::PreprintEntry;
using helix::PreprintPredictor;

// ============================================================================
// Empty State
// ============================================================================

TEST_CASE("PreprintPredictor: no predictions without history", "[print][predictor]") {
    PreprintPredictor predictor;

    REQUIRE_FALSE(predictor.has_predictions());
    REQUIRE(predictor.predicted_total() == 0);
    REQUIRE(predictor.predicted_phases().empty());
    REQUIRE(predictor.remaining_seconds({}, 0, 0) == 0);
}

// ============================================================================
// Single Entry
// ============================================================================

TEST_CASE("PreprintPredictor: single entry uses 100% weight", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({{185, 1700000000, {{2, 25}, {3, 90}, {7, 30}, {9, 20}}}});

    REQUIRE(predictor.has_predictions());
    REQUIRE(predictor.predicted_total() == 165); // 25+90+30+20

    auto phases = predictor.predicted_phases();
    REQUIRE(phases[2] == 25);
    REQUIRE(phases[3] == 90);
    REQUIRE(phases[7] == 30);
    REQUIRE(phases[9] == 20);
}

// ============================================================================
// Two Entries (60/40 weighting)
// ============================================================================

TEST_CASE("PreprintPredictor: two entries use 60/40 weighting", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({
        {100, 1700000000, {{2, 20}}}, // older: 40%
        {100, 1700000001, {{2, 30}}}, // newer: 60%
    });

    auto phases = predictor.predicted_phases();
    // 30*0.6 + 20*0.4 = 18 + 8 = 26
    REQUIRE(phases[2] == 26);
    REQUIRE(predictor.predicted_total() == 26);
}

// ============================================================================
// Three Entries (50/30/20 weighting)
// ============================================================================

TEST_CASE("PreprintPredictor: three entries use 50/30/20 weighting", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({
        {100, 1700000000, {{2, 10}}}, // oldest: 20%
        {100, 1700000001, {{2, 20}}}, // middle: 30%
        {100, 1700000002, {{2, 30}}}, // newest: 50%
    });

    auto phases = predictor.predicted_phases();
    // 30*0.5 + 20*0.3 + 10*0.2 = 15 + 6 + 2 = 23
    REQUIRE(phases[2] == 23);
}

// ============================================================================
// FIFO Trimming
// ============================================================================

TEST_CASE("PreprintPredictor: add_entry trims to 3 (FIFO)", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({
        {100, 1700000000, {{2, 10}}},
        {100, 1700000001, {{2, 20}}},
        {100, 1700000002, {{2, 30}}},
    });

    // Add a 4th entry
    predictor.add_entry({100, 1700000003, {{2, 40}}});

    auto entries = predictor.get_entries();
    REQUIRE(entries.size() == 3);

    // Oldest (10s) should be gone
    // Now: 20, 30, 40
    auto phases = predictor.predicted_phases();
    // 40*0.5 + 30*0.3 + 20*0.2 = 20 + 9 + 4 = 33
    REQUIRE(phases[2] == 33);
}

// ============================================================================
// 15-Minute Cap
// ============================================================================

TEST_CASE("PreprintPredictor: entries over 15 min are rejected", "[print][predictor]") {
    PreprintPredictor predictor;

    // Entry with total > 900s should be ignored
    predictor.add_entry({901, 1700000000, {{2, 500}}});
    REQUIRE_FALSE(predictor.has_predictions());
    REQUIRE(predictor.get_entries().empty());

    // Entry at exactly 900s should be accepted
    predictor.add_entry({900, 1700000001, {{2, 500}}});
    REQUIRE(predictor.has_predictions());
    REQUIRE(predictor.get_entries().size() == 1);
}

// ============================================================================
// Phases That Appear in Only Some Entries
// ============================================================================

TEST_CASE("PreprintPredictor: phases appearing in subset of entries", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({
        {100, 1700000000, {{2, 20}, {3, 80}}},           // has homing + heating
        {100, 1700000001, {{2, 25}}},                    // only homing
        {100, 1700000002, {{2, 30}, {3, 100}, {7, 40}}}, // homing + heating + mesh
    });

    auto phases = predictor.predicted_phases();

    // Phase 2 (homing): all three entries
    // 30*0.5 + 25*0.3 + 20*0.2 = 15 + 7.5 + 4 = 26.5 -> 27 (rounded)
    REQUIRE(phases[2] == 27);

    // Phase 3 (heating): entries 0 and 2 only
    // Weight redistribution: entry2=50/(50+20)=71.4%, entry0=20/(50+20)=28.6%
    // 100*0.714 + 80*0.286 = 71.4 + 22.9 = 94.3 -> 94
    REQUIRE(phases[3] == 94);

    // Phase 7 (mesh): only entry 2
    // Only one entry with this phase -> 100% weight
    REQUIRE(phases[7] == 40);
}

// ============================================================================
// Remaining Time: All Future Phases
// ============================================================================

TEST_CASE("PreprintPredictor: remaining_seconds with no progress", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({{185, 1700000000, {{2, 25}, {3, 90}, {7, 30}, {9, 20}}}});

    // No completed phases, current=IDLE(0), no elapsed
    int remaining = predictor.remaining_seconds({}, 0, 0);
    // All phases are future: 25+90+30+20 = 165
    REQUIRE(remaining == 165);
}

// ============================================================================
// Remaining Time: Some Completed, Current Active
// ============================================================================

TEST_CASE("PreprintPredictor: remaining with completed and current phase", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({{185, 1700000000, {{2, 25}, {3, 90}, {7, 30}, {9, 20}}}});

    // Homing done, currently heating bed for 30s
    std::set<int> completed = {2};
    int remaining = predictor.remaining_seconds(completed, 3, 30);
    // Current phase (3): max(0, 90-30) = 60
    // Future phases (7, 9): 30+20 = 50
    // Total: 60+50 = 110
    REQUIRE(remaining == 110);
}

// ============================================================================
// Remaining Time: Elapsed Exceeds Prediction
// ============================================================================

TEST_CASE("PreprintPredictor: elapsed exceeds prediction returns 0 for current",
          "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({{185, 1700000000, {{2, 25}, {3, 90}, {7, 30}, {9, 20}}}});

    // Heating bed, but we've been at it for 120s (predicted 90s)
    std::set<int> completed = {2};
    int remaining = predictor.remaining_seconds(completed, 3, 120);
    // Current phase: max(0, 90-120) = 0
    // Future phases: 30+20 = 50
    REQUIRE(remaining == 50);
}

// ============================================================================
// Remaining Time: All Phases Completed
// ============================================================================

TEST_CASE("PreprintPredictor: all phases completed returns 0", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({{185, 1700000000, {{2, 25}, {3, 90}, {7, 30}, {9, 20}}}});

    std::set<int> completed = {2, 3, 7, 9};
    int remaining = predictor.remaining_seconds(completed, 0, 0);
    REQUIRE(remaining == 0);
}

// ============================================================================
// Remaining Time: Current Phase Not in History
// ============================================================================

TEST_CASE("PreprintPredictor: unknown current phase contributes 0", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({{100, 1700000000, {{2, 25}, {3, 90}}}});

    // Current phase 5 (QGL) not in history - contributes 0 predicted
    std::set<int> completed = {2};
    int remaining = predictor.remaining_seconds(completed, 5, 10);
    // Current (5): not in history -> 0
    // Future: phase 3 is future (not completed, not current) -> 90
    REQUIRE(remaining == 90);
}

// ============================================================================
// Single Phase Entry
// ============================================================================

TEST_CASE("PreprintPredictor: single phase entry", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({{30, 1700000000, {{3, 30}}}});

    REQUIRE(predictor.predicted_total() == 30);

    auto phases = predictor.predicted_phases();
    REQUIRE(phases.size() == 1);
    REQUIRE(phases[3] == 30);

    // In the middle of the only phase
    int remaining = predictor.remaining_seconds({}, 3, 10);
    REQUIRE(remaining == 20);
}

// ============================================================================
// load_entries Replaces Existing
// ============================================================================

TEST_CASE("PreprintPredictor: load_entries replaces existing data", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({{100, 1700000000, {{2, 50}}}});
    REQUIRE(predictor.predicted_total() == 50);

    predictor.load_entries({{100, 1700000001, {{3, 30}}}});
    REQUIRE(predictor.predicted_total() == 30);

    auto phases = predictor.predicted_phases();
    REQUIRE(phases.count(2) == 0); // Old data gone
    REQUIRE(phases[3] == 30);
}

// ============================================================================
// load_entries Caps at 3
// ============================================================================

TEST_CASE("PreprintPredictor: load_entries caps at 3", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({
        {100, 1700000000, {{2, 10}}},
        {100, 1700000001, {{2, 20}}},
        {100, 1700000002, {{2, 30}}},
        {100, 1700000003, {{2, 40}}},
        {100, 1700000004, {{2, 50}}},
    });

    // Should keep only the last 3
    REQUIRE(predictor.get_entries().size() == 3);
}

// ============================================================================
// Zero Elapsed in Current Phase
// ============================================================================

TEST_CASE("PreprintPredictor: zero elapsed in current phase", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({{100, 1700000000, {{2, 25}, {3, 90}}}});

    // Just entered phase 3, 0 elapsed
    std::set<int> completed = {2};
    int remaining = predictor.remaining_seconds(completed, 3, 0);
    // Current: 90-0=90, future: none
    REQUIRE(remaining == 90);
}
