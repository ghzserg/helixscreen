// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_mock.h"
#include "ams_types.h"
#include "ui/ams_drawing_utils.h"

#include "../catch_amalgamated.hpp"

/**
 * @file test_ams_system_tool_layout.cpp
 * @brief Unit tests for compute_system_tool_layout()
 *
 * Tests the physical nozzle position calculation that fixes the bug where
 * HUB units with unique per-lane mapped_tool values (real AFC behavior)
 * inflated the total nozzle count in the system path canvas.
 */

using namespace ams_draw;

// ============================================================================
// Core: HUB units with unique per-lane mapped_tools (the user's bug)
// ============================================================================

TEST_CASE("SystemToolLayout: 3 HUB units with unique mapped_tools", "[ams][tool_layout]") {
    // 3 HUB units, slots have mapped_tool {0-3}, {4-7}, {8-11}
    // Each HUB unit should be 1 physical nozzle regardless of mapped_tool spread
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    for (int u = 0; u < 3; ++u) {
        AmsUnit unit;
        unit.unit_index = u;
        unit.slot_count = 4;
        unit.first_slot_global_index = u * 4;
        unit.topology = PathTopology::HUB;
        for (int s = 0; s < 4; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = u * 4 + s;
            slot.mapped_tool = u * 4 + s; // Unique per lane
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }
    info.total_slots = 12;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 3);
    REQUIRE(layout.units.size() == 3);
    for (int u = 0; u < 3; ++u) {
        CHECK(layout.units[u].tool_count == 1);
        CHECK(layout.units[u].first_physical_tool == u);
    }
}

// ============================================================================
// User's exact mixed setup (Box Turtle + 2x OpenAMS)
// ============================================================================

TEST_CASE("SystemToolLayout: user's exact mixed setup", "[ams][tool_layout]") {
    // HUB(mapped 0-3) + HUB(mapped 4-7) + PARALLEL(mapped 8-11)
    AmsBackendMock backend(4);
    backend.set_mixed_topology_mode(true);

    auto info = backend.get_system_info();

    // Apply real AFC mapped_tool values (unique per lane, even for HUB units)
    for (int i = 0; i < 4; ++i) {
        info.get_slot_global(i)->mapped_tool = i;         // BT: T0-T3
        info.get_slot_global(4 + i)->mapped_tool = 4 + i; // AMS_1: T4-T7
        info.get_slot_global(8 + i)->mapped_tool = 8 + i; // AMS_2: T8-T11
    }

    auto layout = compute_system_tool_layout(info, &backend);

    CHECK(layout.total_physical_tools == 6);
    REQUIRE(layout.units.size() == 3);

    // Unit 0: Box Turtle (PARALLEL) → 4 nozzles
    CHECK(layout.units[0].first_physical_tool == 0);
    CHECK(layout.units[0].tool_count == 4);

    // Unit 1: AMS_1 (HUB) → 1 nozzle
    CHECK(layout.units[1].first_physical_tool == 4);
    CHECK(layout.units[1].tool_count == 1);

    // Unit 2: AMS_2 (HUB) → 1 nozzle
    CHECK(layout.units[2].first_physical_tool == 5);
    CHECK(layout.units[2].tool_count == 1);
}

// ============================================================================
// Mock mixed topology (HUB + HUB + PARALLEL)
// ============================================================================

TEST_CASE("SystemToolLayout: mock mixed topology", "[ams][tool_layout]") {
    AmsBackendMock backend(4);
    backend.set_mixed_topology_mode(true);

    auto info = backend.get_system_info();

    // After mock update, HUB units should have unique per-lane mapped_tool values
    auto layout = compute_system_tool_layout(info, &backend);

    CHECK(layout.total_physical_tools == 6);
    REQUIRE(layout.units.size() == 3);

    // PARALLEL unit: 4 tools
    CHECK(layout.units[0].tool_count == 4);
    // HUB units: 1 tool each
    CHECK(layout.units[1].tool_count == 1);
    CHECK(layout.units[2].tool_count == 1);
}

// ============================================================================
// All-PARALLEL system (tool changer, 3 units)
// ============================================================================

TEST_CASE("SystemToolLayout: all-PARALLEL system", "[ams][tool_layout]") {
    AmsSystemInfo info;
    info.type = AmsType::TOOL_CHANGER;

    for (int u = 0; u < 3; ++u) {
        AmsUnit unit;
        unit.unit_index = u;
        unit.slot_count = 4;
        unit.first_slot_global_index = u * 4;
        unit.topology = PathTopology::PARALLEL;
        for (int s = 0; s < 4; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = u * 4 + s;
            slot.mapped_tool = u * 4 + s;
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }
    info.total_slots = 12;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 12);
    for (int u = 0; u < 3; ++u) {
        CHECK(layout.units[u].tool_count == 4);
        CHECK(layout.units[u].first_physical_tool == u * 4);
    }
}

// ============================================================================
// Virtual→physical mapping for active tool highlighting
// ============================================================================

TEST_CASE("SystemToolLayout: virtual to physical mapping", "[ams][tool_layout]") {
    // HUB unit with mapped_tool {4,5,6,7} → all map to same physical nozzle
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::HUB;
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = 4 + s;
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 1);

    // All virtual tools 4-7 should map to physical nozzle 0
    for (int v = 4; v <= 7; ++v) {
        auto it = layout.virtual_to_physical.find(v);
        REQUIRE(it != layout.virtual_to_physical.end());
        CHECK(it->second == 0);
    }
}

// ============================================================================
// Physical→virtual label mapping
// ============================================================================

TEST_CASE("SystemToolLayout: physical to virtual label mapping", "[ams][tool_layout]") {
    // HUB(mapped 0-3) + HUB(mapped 4-7)
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    for (int u = 0; u < 2; ++u) {
        AmsUnit unit;
        unit.unit_index = u;
        unit.slot_count = 4;
        unit.first_slot_global_index = u * 4;
        unit.topology = PathTopology::HUB;
        for (int s = 0; s < 4; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = u * 4 + s;
            slot.mapped_tool = u * 4 + s;
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }
    info.total_slots = 8;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 2);
    REQUIRE(layout.physical_to_virtual_label.size() == 2);
    CHECK(layout.physical_to_virtual_label[0] == 0); // Min of {0,1,2,3}
    CHECK(layout.physical_to_virtual_label[1] == 4); // Min of {4,5,6,7}
}

// ============================================================================
// Single HUB unit (no multi-tool)
// ============================================================================

TEST_CASE("SystemToolLayout: single HUB unit", "[ams][tool_layout]") {
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::HUB;
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = s;
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 1);
    REQUIRE(layout.units.size() == 1);
    CHECK(layout.units[0].tool_count == 1);
    CHECK(layout.units[0].first_physical_tool == 0);
}

// ============================================================================
// Empty system
// ============================================================================

TEST_CASE("SystemToolLayout: empty system", "[ams][tool_layout]") {
    AmsSystemInfo info;
    info.type = AmsType::NONE;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 0);
    CHECK(layout.units.empty());
    CHECK(layout.virtual_to_physical.empty());
    CHECK(layout.physical_to_virtual_label.empty());
}

// ============================================================================
// PARALLEL unit with no mapped_tool data (fallback)
// ============================================================================

TEST_CASE("SystemToolLayout: PARALLEL with no mapped_tool data", "[ams][tool_layout]") {
    AmsSystemInfo info;
    info.type = AmsType::TOOL_CHANGER;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::PARALLEL;
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = -1; // No mapping data
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 4);
    CHECK(layout.units[0].tool_count == 4);
}

// ============================================================================
// HUB unit with no mapped_tool data
// ============================================================================

TEST_CASE("SystemToolLayout: HUB with no mapped_tool data", "[ams][tool_layout]") {
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::HUB;
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = -1; // No mapping data
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 1);
    CHECK(layout.units[0].tool_count == 1);
}

// ============================================================================
// Full user scenario: virtual→physical for active tool in mixed setup
// ============================================================================

TEST_CASE("SystemToolLayout: mixed setup active tool mapping", "[ams][tool_layout]") {
    AmsBackendMock backend(4);
    backend.set_mixed_topology_mode(true);

    auto info = backend.get_system_info();

    // Apply real AFC mapped_tool values
    for (int i = 0; i < 4; ++i) {
        info.get_slot_global(i)->mapped_tool = i;
        info.get_slot_global(4 + i)->mapped_tool = 4 + i;
        info.get_slot_global(8 + i)->mapped_tool = 8 + i;
    }

    auto layout = compute_system_tool_layout(info, &backend);

    // BT virtual tools 0-3 → physical 0-3 (PARALLEL, each maps to own nozzle)
    for (int v = 0; v < 4; ++v) {
        auto it = layout.virtual_to_physical.find(v);
        REQUIRE(it != layout.virtual_to_physical.end());
        CHECK(it->second == v);
    }

    // AMS_1 virtual tools 4-7 → physical 4 (single HUB nozzle)
    for (int v = 4; v < 8; ++v) {
        auto it = layout.virtual_to_physical.find(v);
        REQUIRE(it != layout.virtual_to_physical.end());
        CHECK(it->second == 4);
    }

    // AMS_2 virtual tools 8-11 → physical 5 (single HUB nozzle)
    for (int v = 8; v < 12; ++v) {
        auto it = layout.virtual_to_physical.find(v);
        REQUIRE(it != layout.virtual_to_physical.end());
        CHECK(it->second == 5);
    }
}

// ============================================================================
// hub_tool_label overrides min_virtual_tool for labels
// ============================================================================

TEST_CASE("SystemToolLayout: hub_tool_label overrides min_virtual_tool for labels",
          "[ams][tool_layout]") {
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::HUB;
    unit.hub_tool_label = 4;
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = 4 + s;
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 1);
    REQUIRE(layout.physical_to_virtual_label.size() == 1);
    CHECK(layout.physical_to_virtual_label[0] == 4);
}

// ============================================================================
// hub_tool_label maps to physical nozzle
// ============================================================================

TEST_CASE("SystemToolLayout: hub_tool_label used for display label not virtual mapping",
          "[ams][tool_layout]") {
    // hub_tool_label affects physical_to_virtual_label (display) but NOT virtual_to_physical
    // (to avoid conflicts when hub_tool_label overlaps another unit's virtual tool range)
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::HUB;
    unit.hub_tool_label = 5;
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = 8 + s;
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 1);
    // Virtual tools 8-11 map to physical 0
    for (int v = 8; v <= 11; ++v) {
        CHECK(layout.virtual_to_physical[v] == 0);
    }
    // hub_tool_label=5 should NOT be in virtual_to_physical (would conflict with other units)
    CHECK(layout.virtual_to_physical.find(5) == layout.virtual_to_physical.end());
    // But it SHOULD be used for the display label
    REQUIRE(layout.physical_to_virtual_label.size() == 1);
    CHECK(layout.physical_to_virtual_label[0] == 5);
}

// ============================================================================
// Mixed setup correct labels with hub_tool_label
// ============================================================================

TEST_CASE("SystemToolLayout: mixed setup correct labels with hub_tool_label",
          "[ams][tool_layout]") {
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    // Unit 0: PARALLEL with 4 tools
    {
        AmsUnit unit;
        unit.unit_index = 0;
        unit.slot_count = 4;
        unit.first_slot_global_index = 0;
        unit.topology = PathTopology::PARALLEL;
        for (int s = 0; s < 4; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = s;
            slot.mapped_tool = s;
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }

    // Unit 1: HUB with hub_tool_label=4
    {
        AmsUnit unit;
        unit.unit_index = 1;
        unit.slot_count = 4;
        unit.first_slot_global_index = 4;
        unit.topology = PathTopology::HUB;
        unit.hub_tool_label = 4;
        for (int s = 0; s < 4; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = 4 + s;
            slot.mapped_tool = 4 + s;
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }

    // Unit 2: HUB with hub_tool_label=5
    {
        AmsUnit unit;
        unit.unit_index = 2;
        unit.slot_count = 4;
        unit.first_slot_global_index = 8;
        unit.topology = PathTopology::HUB;
        unit.hub_tool_label = 5;
        for (int s = 0; s < 4; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = 8 + s;
            slot.mapped_tool = 8 + s;
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }

    info.total_slots = 12;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 6);
    REQUIRE(layout.physical_to_virtual_label.size() == 6);
    CHECK(layout.physical_to_virtual_label[0] == 0);
    CHECK(layout.physical_to_virtual_label[1] == 1);
    CHECK(layout.physical_to_virtual_label[2] == 2);
    CHECK(layout.physical_to_virtual_label[3] == 3);
    CHECK(layout.physical_to_virtual_label[4] == 4);
    CHECK(layout.physical_to_virtual_label[5] == 5);
}
