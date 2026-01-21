// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_happy_hare.h"
#include "ams_types.h"
#include "moonraker_api.h"

#include <algorithm>
#include <vector>

#include "../catch_amalgamated.hpp"

/**
 * @brief Test helper class providing access to AmsBackendHappyHare internals
 *
 * This class provides controlled access to private members for unit testing.
 * It does NOT start the backend (no Moonraker connection needed).
 */
class AmsBackendHappyHareTestHelper : public AmsBackendHappyHare {
  public:
    AmsBackendHappyHareTestHelper() : AmsBackendHappyHare(nullptr, nullptr) {}

    /**
     * @brief Initialize test gates with default SlotInfo
     * @param count Number of gates to create
     */
    void initialize_test_gates(int count) {
        system_info_.units.clear();

        AmsUnit unit;
        unit.unit_index = 0;
        unit.name = "Happy Hare MMU";
        unit.slot_count = count;
        unit.first_slot_global_index = 0;

        for (int i = 0; i < count; ++i) {
            SlotInfo slot;
            slot.slot_index = i;
            slot.global_index = i;
            slot.status = SlotStatus::AVAILABLE;
            slot.mapped_tool = i;
            slot.color_rgb = AMS_DEFAULT_SLOT_COLOR;
            unit.slots.push_back(slot);
        }

        system_info_.units.push_back(unit);
        system_info_.total_slots = count;
        gates_initialized_ = true;

        // Also initialize tool_to_slot_map for reset_tool_mappings tests
        system_info_.tool_to_slot_map.clear();
        for (int i = 0; i < count; ++i) {
            system_info_.tool_to_slot_map.push_back(i);
        }
    }

    /**
     * @brief Get mutable slot pointer for test setup
     * @param slot_index Global slot index
     * @return Pointer to SlotInfo or nullptr
     */
    SlotInfo* get_mutable_slot(int slot_index) {
        return system_info_.get_slot_global(slot_index);
    }

    // G-code capture for persistence tests
    std::vector<std::string> captured_gcodes;

    /**
     * @brief Override execute_gcode to capture commands for testing
     * @param gcode The G-code command
     * @return Success
     */
    AmsError execute_gcode(const std::string& gcode) override {
        captured_gcodes.push_back(gcode);
        return AmsErrorHelper::success();
    }

    void clear_captured_gcodes() {
        captured_gcodes.clear();
    }

    /**
     * @brief Check if exact G-code was captured
     * @param expected Exact G-code string to find
     * @return true if found
     */
    bool has_gcode(const std::string& expected) const {
        return std::find(captured_gcodes.begin(), captured_gcodes.end(), expected) !=
               captured_gcodes.end();
    }

    /**
     * @brief Check if any captured G-code starts with prefix
     * @param prefix Prefix to search for
     * @return true if any G-code starts with prefix
     */
    bool has_gcode_starting_with(const std::string& prefix) const {
        for (const auto& gcode : captured_gcodes) {
            if (gcode.rfind(prefix, 0) == 0)
                return true;
        }
        return false;
    }

    /**
     * @brief Check if any captured G-code contains substring
     * @param substring String to search for
     * @return true if any G-code contains substring
     */
    bool has_gcode_containing(const std::string& substring) const {
        for (const auto& gcode : captured_gcodes) {
            if (gcode.find(substring) != std::string::npos)
                return true;
        }
        return false;
    }
};

// ============================================================================
// set_slot_info() Persistence Tests - Happy Hare MMU_GATE_MAP
// ============================================================================
//
// These tests verify that set_slot_info() sends the appropriate MMU_GATE_MAP
// G-code commands to persist filament properties in Happy Hare.
//
// Command format:
// - MMU_GATE_MAP GATE={n} COLOR={RRGGBB} MATERIAL={type} SPOOLID={id}
//
// NOTE: These tests are designed to FAIL initially (test-first approach).
// The set_slot_info() method currently only updates local state and does NOT
// send G-code commands. Implementation must be added to make these tests pass.
// ============================================================================

TEST_CASE("Happy Hare persistence: MMU_GATE_MAP basic format", "[ams][happy_hare][persistence]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    SlotInfo info;
    info.color_rgb = 0xFF0000; // Red - need something to trigger command

    helper.set_slot_info(0, info);

    // Should send MMU_GATE_MAP with GATE=0
    // FAILS: set_slot_info doesn't call execute_gcode yet
    REQUIRE(helper.has_gcode_starting_with("MMU_GATE_MAP GATE=0"));
}

TEST_CASE("Happy Hare persistence: MMU_GATE_MAP with color", "[ams][happy_hare][persistence]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    SlotInfo info;
    info.color_rgb = 0xFF0000; // Red

    helper.set_slot_info(0, info);

    // Should send: MMU_GATE_MAP GATE=0 COLOR=FF0000
    // Color: uppercase hex, no # prefix
    // FAILS: set_slot_info doesn't call execute_gcode yet
    REQUIRE(helper.has_gcode("MMU_GATE_MAP GATE=0 COLOR=FF0000"));
}

TEST_CASE("Happy Hare persistence: MMU_GATE_MAP color uppercase no prefix",
          "[ams][happy_hare][persistence]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    SlotInfo info;
    info.color_rgb = 0x00FF00; // Green

    helper.set_slot_info(1, info);

    // Should send: MMU_GATE_MAP GATE=1 COLOR=00FF00 (uppercase, no #)
    // FAILS: set_slot_info doesn't call execute_gcode yet
    REQUIRE(helper.has_gcode("MMU_GATE_MAP GATE=1 COLOR=00FF00"));
}

TEST_CASE("Happy Hare persistence: MMU_GATE_MAP with material", "[ams][happy_hare][persistence]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    SlotInfo info;
    info.material = "PLA";

    helper.set_slot_info(1, info);

    // Should send: MMU_GATE_MAP GATE=1 MATERIAL=PLA
    // FAILS: set_slot_info doesn't call execute_gcode yet
    REQUIRE(helper.has_gcode("MMU_GATE_MAP GATE=1 MATERIAL=PLA"));
}

TEST_CASE("Happy Hare persistence: MMU_GATE_MAP with Spoolman ID",
          "[ams][happy_hare][persistence]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    SlotInfo info;
    info.spoolman_id = 42;

    helper.set_slot_info(2, info);

    // Should contain: SPOOLID=42
    // FAILS: set_slot_info doesn't call execute_gcode yet
    REQUIRE(helper.has_gcode_containing("SPOOLID=42"));
}

TEST_CASE("Happy Hare persistence: MMU_GATE_MAP clear Spoolman with -1",
          "[ams][happy_hare][persistence]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    // Pre-set existing spoolman_id on slot
    SlotInfo* existing_slot = helper.get_mutable_slot(0);
    REQUIRE(existing_slot != nullptr);
    existing_slot->spoolman_id = 123;

    // Now clear it by setting spoolman_id = 0
    SlotInfo new_info;
    new_info.spoolman_id = 0;

    helper.set_slot_info(0, new_info);

    // Should send: SPOOLID=-1 to clear
    // FAILS: set_slot_info doesn't call execute_gcode yet
    REQUIRE(helper.has_gcode_containing("SPOOLID=-1"));
}

TEST_CASE("Happy Hare persistence: full slot info generates complete command",
          "[ams][happy_hare][persistence]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    SlotInfo info;
    info.color_rgb = 0x0000FF; // Blue
    info.material = "PETG";
    info.spoolman_id = 99;

    helper.set_slot_info(0, info);

    // Should send: MMU_GATE_MAP GATE=0 COLOR=0000FF MATERIAL=PETG SPOOLID=99
    // FAILS: set_slot_info doesn't call execute_gcode yet
    REQUIRE(helper.has_gcode("MMU_GATE_MAP GATE=0 COLOR=0000FF MATERIAL=PETG SPOOLID=99"));
}

TEST_CASE("Happy Hare persistence: skips COLOR for default grey",
          "[ams][happy_hare][persistence]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    SlotInfo info;
    info.color_rgb = 0x808080; // Default grey - should NOT include COLOR
    info.material = "PLA";     // But material should still be sent

    helper.set_slot_info(0, info);

    // Should NOT include COLOR parameter for grey default
    // But should still send the command if other values are present
    if (!helper.captured_gcodes.empty()) {
        // If command was sent, it should not contain COLOR
        REQUIRE_FALSE(helper.has_gcode_containing("COLOR="));
    }
    // This test verifies COLOR is skipped - currently passes since nothing is sent
}

TEST_CASE("Happy Hare persistence: skips COLOR for zero", "[ams][happy_hare][persistence]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    SlotInfo info;
    info.color_rgb = 0;    // Zero color - should NOT include COLOR
    info.material = "ABS"; // But material should still be sent

    helper.set_slot_info(0, info);

    // Should NOT include COLOR parameter for zero
    if (!helper.captured_gcodes.empty()) {
        REQUIRE_FALSE(helper.has_gcode_containing("COLOR="));
    }
    // This test verifies COLOR is skipped - currently passes since nothing is sent
}

TEST_CASE("Happy Hare persistence: skips MATERIAL for empty string",
          "[ams][happy_hare][persistence]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    SlotInfo info;
    info.material = "";        // Empty - should NOT include MATERIAL
    info.color_rgb = 0xFF0000; // But color should still be sent

    helper.set_slot_info(0, info);

    // Should NOT include MATERIAL parameter for empty
    if (!helper.captured_gcodes.empty()) {
        REQUIRE_FALSE(helper.has_gcode_containing("MATERIAL="));
    }
    // This test verifies MATERIAL is skipped - currently passes since nothing is sent
}

TEST_CASE("Happy Hare persistence: skips SPOOLID when both old and new are zero/negative",
          "[ams][happy_hare][persistence]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    // Slot starts with spoolman_id = 0 (default)
    SlotInfo info;
    info.spoolman_id = 0;
    info.color_rgb = 0xFF0000; // Need something to potentially trigger command

    helper.set_slot_info(0, info);

    // Should NOT include SPOOLID parameter when both old and new are 0
    if (!helper.captured_gcodes.empty()) {
        REQUIRE_FALSE(helper.has_gcode_containing("SPOOLID="));
    }
    // This test verifies SPOOLID is skipped when not needed
}

TEST_CASE("Happy Hare persistence: skips command when all values are default/empty",
          "[ams][happy_hare][persistence]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    SlotInfo info;
    info.color_rgb = 0x808080; // Default grey
    info.material = "";        // Empty
    info.spoolman_id = 0;      // Zero (and no existing to clear)

    helper.set_slot_info(0, info);

    // Should NOT send any G-code when all values are default/empty
    // PASSES: no G-code sent at all currently
    REQUIRE(helper.captured_gcodes.empty());
}

TEST_CASE("Happy Hare persistence: different gate indices", "[ams][happy_hare][persistence]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(8);

    SECTION("Gate 0") {
        SlotInfo info;
        info.color_rgb = 0xFF0000;
        helper.set_slot_info(0, info);
        // FAILS: set_slot_info doesn't call execute_gcode yet
        REQUIRE(helper.has_gcode_starting_with("MMU_GATE_MAP GATE=0"));
    }

    SECTION("Gate 3") {
        SlotInfo info;
        info.color_rgb = 0xFF0000;
        helper.set_slot_info(3, info);
        // FAILS: set_slot_info doesn't call execute_gcode yet
        REQUIRE(helper.has_gcode_starting_with("MMU_GATE_MAP GATE=3"));
    }

    SECTION("Gate 7") {
        SlotInfo info;
        info.color_rgb = 0xFF0000;
        helper.set_slot_info(7, info);
        // FAILS: set_slot_info doesn't call execute_gcode yet
        REQUIRE(helper.has_gcode_starting_with("MMU_GATE_MAP GATE=7"));
    }
}

// ============================================================================
// reset_tool_mappings() Tests
// ============================================================================

TEST_CASE("Happy Hare reset_tool_mappings sends MMU_TTG_MAP for each tool",
          "[ams][happy_hare][tool_mapping][reset]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    auto result = helper.reset_tool_mappings();

    REQUIRE(result.success());
    // Should have sent 4 MMU_TTG_MAP commands (one per tool)
    REQUIRE(helper.captured_gcodes.size() == 4);
    REQUIRE(helper.has_gcode("MMU_TTG_MAP TOOL=0 GATE=0"));
    REQUIRE(helper.has_gcode("MMU_TTG_MAP TOOL=1 GATE=1"));
    REQUIRE(helper.has_gcode("MMU_TTG_MAP TOOL=2 GATE=2"));
    REQUIRE(helper.has_gcode("MMU_TTG_MAP TOOL=3 GATE=3"));
}

TEST_CASE("Happy Hare reset_tool_mappings with 8 tools", "[ams][happy_hare][tool_mapping][reset]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(8);

    auto result = helper.reset_tool_mappings();

    REQUIRE(result.success());
    REQUIRE(helper.captured_gcodes.size() == 8);
    // Verify first and last
    REQUIRE(helper.has_gcode("MMU_TTG_MAP TOOL=0 GATE=0"));
    REQUIRE(helper.has_gcode("MMU_TTG_MAP TOOL=7 GATE=7"));
}

TEST_CASE("Happy Hare reset_tool_mappings with zero tools is no-op",
          "[ams][happy_hare][tool_mapping][reset]") {
    AmsBackendHappyHareTestHelper helper;
    // Don't initialize gates - tool_to_slot_map is empty

    auto result = helper.reset_tool_mappings();

    REQUIRE(result.success());
    REQUIRE(helper.captured_gcodes.empty());
}

// ============================================================================
// reset_endless_spool() Tests
// ============================================================================

TEST_CASE("Happy Hare reset_endless_spool returns not_supported",
          "[ams][happy_hare][endless_spool][reset]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    auto result = helper.reset_endless_spool();

    CHECK_FALSE(result.success());
    CHECK(result.result == AmsResult::NOT_SUPPORTED);
    // Should NOT send any G-code commands
    REQUIRE(helper.captured_gcodes.empty());
}
