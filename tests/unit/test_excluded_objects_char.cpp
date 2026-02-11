// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_excluded_objects_char.cpp
 * @brief Characterization tests for excluded objects domain in PrinterState
 *
 * These tests document the EXISTING behavior of excluded objects subjects.
 * Run with: ./build/bin/helix-tests "[excluded_objects]"
 *
 * Subjects tested:
 * - excluded_objects_version_: Integer subject incremented when set changes
 * - excluded_objects_: std::unordered_set<string> (NOT a subject, member variable)
 *
 * Key behavior:
 * - get_excluded_objects_version_subject() returns the version subject
 * - get_excluded_objects() returns const reference to the set
 * - set_excluded_objects(set) compares new vs current, increments version if different
 * - Version ONLY increments when set contents actually change
 */

#include "ui_observer_guard.h"

#include "../lvgl_test_fixture.h"
#include "../test_helpers/printer_state_test_access.h"
#include "printer_state.h"

#include <string>
#include <unordered_set>

#include "../catch_amalgamated.hpp"

// ============================================================================
// CHARACTERIZATION: Set Update Tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "CHAR: Setting empty set to non-empty set increments version",
                 "[characterization][excluded_objects]") {
    PrinterState state;
    state.init_subjects(false);

    // Document: Adding objects increments version from 0 to 1
    std::unordered_set<std::string> objects = {"Part_1", "Part_2"};
    state.set_excluded_objects(objects);

    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 1);
    REQUIRE(state.get_excluded_objects().size() == 2);
    REQUIRE(state.get_excluded_objects().count("Part_1") == 1);
    REQUIRE(state.get_excluded_objects().count("Part_2") == 1);

    PrinterStateTestAccess::reset(state);
}

TEST_CASE_METHOD(LVGLTestFixture, "CHAR: Setting same set again does NOT increment version",
                 "[characterization][excluded_objects]") {
    PrinterState state;
    state.init_subjects(false);

    // Set initial objects
    std::unordered_set<std::string> objects = {"Benchy_hull", "Benchy_cabin"};
    state.set_excluded_objects(objects);
    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 1);

    // Document: Setting identical set does NOT change version
    state.set_excluded_objects(objects);
    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 1);

    // Even with a new set object containing same strings
    std::unordered_set<std::string> same_objects = {"Benchy_hull", "Benchy_cabin"};
    state.set_excluded_objects(same_objects);
    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 1);

    PrinterStateTestAccess::reset(state);
}

TEST_CASE_METHOD(LVGLTestFixture, "CHAR: Setting different set increments version",
                 "[characterization][excluded_objects]") {
    PrinterState state;
    state.init_subjects(false);

    // Set initial objects
    std::unordered_set<std::string> objects1 = {"Part_1"};
    state.set_excluded_objects(objects1);
    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 1);

    // Document: Adding a new object increments version
    std::unordered_set<std::string> objects2 = {"Part_1", "Part_2"};
    state.set_excluded_objects(objects2);
    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 2);

    // Document: Removing an object also increments version
    std::unordered_set<std::string> objects3 = {"Part_2"};
    state.set_excluded_objects(objects3);
    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 3);

    // Document: Completely different set increments version
    std::unordered_set<std::string> objects4 = {"NewObject_A", "NewObject_B"};
    state.set_excluded_objects(objects4);
    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 4);

    PrinterStateTestAccess::reset(state);
}

TEST_CASE_METHOD(LVGLTestFixture, "CHAR: Setting back to empty set increments version",
                 "[characterization][excluded_objects]") {
    PrinterState state;
    state.init_subjects(false);

    // Set initial objects
    std::unordered_set<std::string> objects = {"Part_1", "Part_2"};
    state.set_excluded_objects(objects);
    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 1);
    REQUIRE(state.get_excluded_objects().size() == 2);

    // Document: Clearing all objects increments version
    std::unordered_set<std::string> empty_set;
    state.set_excluded_objects(empty_set);
    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 2);
    REQUIRE(state.get_excluded_objects().empty());

    PrinterStateTestAccess::reset(state);
}

// ============================================================================
// CHARACTERIZATION: Version Increment Tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "CHAR: Multiple changes increment version by 1 each time",
                 "[characterization][excluded_objects]") {
    PrinterState state;
    state.init_subjects(false);

    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 0);

    // Document: Each actual change increments version by exactly 1
    state.set_excluded_objects({"A"});
    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 1);

    state.set_excluded_objects({"A", "B"});
    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 2);

    state.set_excluded_objects({"A", "B", "C"});
    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 3);

    state.set_excluded_objects({"B", "C"});
    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 4);

    state.set_excluded_objects({});
    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 5);

    PrinterStateTestAccess::reset(state);
}

TEST_CASE_METHOD(LVGLTestFixture, "CHAR: Version does not skip or jump values",
                 "[characterization][excluded_objects]") {
    PrinterState state;
    state.init_subjects(false);

    int expected_version = 0;

    // Make 10 changes and verify version increments linearly
    for (int i = 1; i <= 10; ++i) {
        std::unordered_set<std::string> objects;
        for (int j = 0; j < i; ++j) {
            objects.insert("Object_" + std::to_string(j));
        }
        state.set_excluded_objects(objects);
        expected_version++;
        REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) ==
                expected_version);
    }

    PrinterStateTestAccess::reset(state);
}

// ============================================================================
// CHARACTERIZATION: Observer Notification Tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "CHAR: Observer on version subject fires when set changes",
                 "[characterization][excluded_objects]") {
    PrinterState state;
    state.init_subjects(false);

    int callback_count = 0;
    int last_version = -1;

    auto cb = [](lv_observer_t* obs, lv_subject_t* subj) {
        auto* count = static_cast<int*>(lv_observer_get_user_data(obs));
        (*count)++;
    };

    ObserverGuard guard(state.get_excluded_objects_version_subject(), cb, &callback_count);

    // Document: Observer fires immediately on subscription
    REQUIRE(callback_count == 1);

    // Document: Observer fires when set changes
    state.set_excluded_objects({"Part_1"});
    REQUIRE(callback_count == 2);

    state.set_excluded_objects({"Part_1", "Part_2"});
    REQUIRE(callback_count == 3);

    // Document: Observer does NOT fire when set is unchanged
    state.set_excluded_objects({"Part_1", "Part_2"});
    REQUIRE(callback_count == 3);

    // Observer fires again on actual change
    state.set_excluded_objects({});
    REQUIRE(callback_count == 4);

    guard.release();
    PrinterStateTestAccess::reset(state);
}

TEST_CASE_METHOD(LVGLTestFixture, "CHAR: Observer receives correct version value",
                 "[characterization][excluded_objects]") {
    PrinterState state;
    state.init_subjects(false);

    int observed_version = -1;

    auto cb = [](lv_observer_t* obs, lv_subject_t* subj) {
        auto* ver = static_cast<int*>(lv_observer_get_user_data(obs));
        *ver = lv_subject_get_int(subj);
    };

    ObserverGuard guard(state.get_excluded_objects_version_subject(), cb, &observed_version);

    // Initial callback sees version 0
    REQUIRE(observed_version == 0);

    // After first change, observer sees version 1
    state.set_excluded_objects({"Object_A"});
    REQUIRE(observed_version == 1);

    // After second change, observer sees version 2
    state.set_excluded_objects({"Object_B"});
    REQUIRE(observed_version == 2);

    guard.release();
    PrinterStateTestAccess::reset(state);
}

// ============================================================================
// CHARACTERIZATION: Edge Cases
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "CHAR: Single object in set works correctly",
                 "[characterization][excluded_objects]") {
    PrinterState state;
    state.init_subjects(false);

    state.set_excluded_objects({"SingleObject"});

    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 1);
    REQUIRE(state.get_excluded_objects().size() == 1);
    REQUIRE(state.get_excluded_objects().count("SingleObject") == 1);

    PrinterStateTestAccess::reset(state);
}

TEST_CASE_METHOD(LVGLTestFixture, "CHAR: Object names with special characters work",
                 "[characterization][excluded_objects]") {
    PrinterState state;
    state.init_subjects(false);

    // Document: Object names from Klipper can contain various characters
    std::unordered_set<std::string> objects = {
        "Benchy_hull",        // underscore
        "Part-1",             // hyphen
        "Object.stl",         // dot
        "Model 123",          // space
        "Complex_Object-v2.0" // mixed
    };
    state.set_excluded_objects(objects);

    REQUIRE(lv_subject_get_int(state.get_excluded_objects_version_subject()) == 1);
    REQUIRE(state.get_excluded_objects().size() == 5);

    for (const auto& obj : objects) {
        REQUIRE(state.get_excluded_objects().count(obj) == 1);
    }

    PrinterStateTestAccess::reset(state);
}

TEST_CASE_METHOD(LVGLTestFixture, "CHAR: get_excluded_objects returns const reference",
                 "[characterization][excluded_objects]") {
    PrinterState state;
    state.init_subjects(false);

    state.set_excluded_objects({"Part_1", "Part_2"});

    // Document: get_excluded_objects() returns const reference for read-only access
    const std::unordered_set<std::string>& ref1 = state.get_excluded_objects();
    const std::unordered_set<std::string>& ref2 = state.get_excluded_objects();

    // Same reference returned each time
    REQUIRE(&ref1 == &ref2);
    REQUIRE(ref1.size() == 2);

    PrinterStateTestAccess::reset(state);
}

// ============================================================================
// Documentation: Excluded Objects Domain Summary
// ============================================================================

/**
 * SUMMARY OF EXCLUDED OBJECTS DOMAIN PATTERNS:
 *
 * 1. EXCLUDED_OBJECTS_VERSION_ SUBJECT:
 *    - Type: Integer subject
 *    - Default: 0 (no changes yet)
 *    - Increment: By 1 on each actual set content change
 *    - Purpose: Notify UI observers that excluded set has changed
 *    - Pattern: Observer watches version, then calls get_excluded_objects()
 *
 * 2. EXCLUDED_OBJECTS_ SET (not a subject):
 *    - Type: std::unordered_set<std::string>
 *    - Default: Empty set
 *    - Access: get_excluded_objects() returns const reference
 *    - Update: set_excluded_objects() compares and only updates if different
 *
 * 3. SET_EXCLUDED_OBJECTS() BEHAVIOR:
 *    - Compares new set with current set using operator!=
 *    - Only updates if sets are different
 *    - Increments version subject by 1 on actual change
 *    - No-op if sets are identical (version unchanged)
 *
 * 4. OBSERVER PATTERN:
 *    - Observers subscribe to excluded_objects_version_ subject
 *    - When notified, call get_excluded_objects() to get updated set
 *    - This pattern avoids exposing set as a subject (sets not natively supported)
 *
 * KEY OBSERVATIONS:
 * - Version-based change notification is common LVGL pattern for complex data
 * - Set comparison uses std::unordered_set::operator!= (element-wise)
 * - No thread safety in set_excluded_objects() - assumed main thread only
 * - Object names come from Klipper's EXCLUDE_OBJECT feature
 */
