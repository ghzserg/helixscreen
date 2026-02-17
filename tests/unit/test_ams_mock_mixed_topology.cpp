// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_mock.h"
#include "ams_types.h"

#include "../catch_amalgamated.hpp"

/**
 * @file test_ams_mock_mixed_topology.cpp
 * @brief Unit tests for mixed topology mock backend (HELIX_MOCK_AMS=mixed)
 *
 * Simulates J0eB0l's real hardware: 6-tool toolchanger with mixed AFC hardware.
 * - Unit 0: Box Turtle (4 lanes, PARALLEL, 1:1 lane->tool, buffers, no hub sensor)
 * - Unit 1: OpenAMS (4 lanes, HUB, 4:1 lane->tool T4, no buffers, hub sensor)
 * - Unit 2: OpenAMS (4 lanes, HUB, 4:1 lane->tool T5, no buffers, hub sensor)
 */

TEST_CASE("Mixed topology mock creates 3 units", "[ams][mock][mixed]") {
    AmsBackendMock backend(4);
    backend.set_mixed_topology_mode(true);

    auto info = backend.get_system_info();

    REQUIRE(info.units.size() == 3);
    REQUIRE(info.total_slots == 12);

    CHECK(info.units[0].name == "Turtle_1");
    CHECK(info.units[1].name == "AMS_1");
    CHECK(info.units[2].name == "AMS_2");
}

TEST_CASE("Mixed topology unit 0 is Box Turtle with PARALLEL topology", "[ams][mock][mixed]") {
    AmsBackendMock backend(4);
    backend.set_mixed_topology_mode(true);

    auto info = backend.get_system_info();
    const auto& unit0 = info.units[0];

    CHECK(unit0.slot_count == 4);
    CHECK(unit0.first_slot_global_index == 0);
    CHECK(unit0.has_hub_sensor == false);

    // Buffer health should be set for Box Turtle (has TurtleNeck buffers)
    CHECK(unit0.buffer_health.has_value());

    // Per-unit topology: Box Turtle uses PARALLEL (1:1 lane->tool)
    CHECK(backend.get_unit_topology(0) == PathTopology::PARALLEL);
    CHECK(unit0.topology == PathTopology::PARALLEL);
}

TEST_CASE("Mixed topology units 1-2 are OpenAMS with HUB topology", "[ams][mock][mixed]") {
    AmsBackendMock backend(4);
    backend.set_mixed_topology_mode(true);

    auto info = backend.get_system_info();

    // Unit 1: OpenAMS
    const auto& unit1 = info.units[1];
    CHECK(unit1.slot_count == 4);
    CHECK(unit1.first_slot_global_index == 4);
    CHECK(unit1.has_hub_sensor == true);
    CHECK(backend.get_unit_topology(1) == PathTopology::HUB);
    CHECK(unit1.topology == PathTopology::HUB);

    // Unit 2: OpenAMS
    const auto& unit2 = info.units[2];
    CHECK(unit2.slot_count == 4);
    CHECK(unit2.first_slot_global_index == 8);
    CHECK(unit2.has_hub_sensor == true);
    CHECK(backend.get_unit_topology(2) == PathTopology::HUB);
    CHECK(unit2.topology == PathTopology::HUB);
}

TEST_CASE("Mixed topology lane-to-tool mapping", "[ams][mock][mixed]") {
    AmsBackendMock backend(4);
    backend.set_mixed_topology_mode(true);

    auto info = backend.get_system_info();

    // Box Turtle slots 0-3 map to T0-T3 (1:1)
    for (int i = 0; i < 4; ++i) {
        const auto* slot = info.get_slot_global(i);
        REQUIRE(slot != nullptr);
        CHECK(slot->mapped_tool == i);
    }

    // OpenAMS 1 slots 4-7 all map to T4 (4:1)
    for (int i = 4; i < 8; ++i) {
        const auto* slot = info.get_slot_global(i);
        REQUIRE(slot != nullptr);
        CHECK(slot->mapped_tool == 4);
    }

    // OpenAMS 2 slots 8-11 all map to T5 (4:1)
    for (int i = 8; i < 12; ++i) {
        const auto* slot = info.get_slot_global(i);
        REQUIRE(slot != nullptr);
        CHECK(slot->mapped_tool == 5);
    }

    // tool_to_slot_map should have 6 entries (T0-T5)
    REQUIRE(info.tool_to_slot_map.size() == 6);
    // T0->slot0, T1->slot1, T2->slot2, T3->slot3
    CHECK(info.tool_to_slot_map[0] == 0);
    CHECK(info.tool_to_slot_map[1] == 1);
    CHECK(info.tool_to_slot_map[2] == 2);
    CHECK(info.tool_to_slot_map[3] == 3);
    // T4->slot4 (first slot of OpenAMS 1), T5->slot8 (first slot of OpenAMS 2)
    CHECK(info.tool_to_slot_map[4] == 4);
    CHECK(info.tool_to_slot_map[5] == 8);
}

TEST_CASE("Mixed topology Box Turtle slots have buffers", "[ams][mock][mixed]") {
    AmsBackendMock backend(4);
    backend.set_mixed_topology_mode(true);

    auto info = backend.get_system_info();

    // Unit 0 (Box Turtle) should have buffer_health set
    REQUIRE(info.units[0].buffer_health.has_value());
    CHECK(info.units[0].buffer_health->state.size() > 0);
}

TEST_CASE("Mixed topology OpenAMS slots have no buffers", "[ams][mock][mixed]") {
    AmsBackendMock backend(4);
    backend.set_mixed_topology_mode(true);

    auto info = backend.get_system_info();

    // Units 1-2 (OpenAMS) should NOT have buffer_health
    CHECK_FALSE(info.units[1].buffer_health.has_value());
    CHECK_FALSE(info.units[2].buffer_health.has_value());
}

TEST_CASE("Mixed topology get_topology returns HUB as default", "[ams][mock][mixed]") {
    AmsBackendMock backend(4);
    backend.set_mixed_topology_mode(true);

    // System-wide topology should still return HUB (backward compat default)
    CHECK(backend.get_topology() == PathTopology::HUB);

    // Per-unit topology is accessed via get_unit_topology()
    CHECK(backend.get_unit_topology(0) == PathTopology::PARALLEL);
    CHECK(backend.get_unit_topology(1) == PathTopology::HUB);
    CHECK(backend.get_unit_topology(2) == PathTopology::HUB);

    // Out-of-range falls back to system topology
    CHECK(backend.get_unit_topology(99) == PathTopology::HUB);
    CHECK(backend.get_unit_topology(-1) == PathTopology::HUB);
}

TEST_CASE("Non-mixed mock: get_unit_topology falls back to system topology",
          "[ams][mock][backward_compat]") {
    // Standard mock (not mixed): unit_topologies_ is empty,
    // so get_unit_topology() should fall back to topology_ (HUB by default)
    AmsBackendMock backend(4);

    REQUIRE(backend.get_topology() == PathTopology::HUB);
    REQUIRE(backend.get_unit_topology(0) == PathTopology::HUB);
    REQUIRE(backend.get_unit_topology(1) == PathTopology::HUB);
    REQUIRE(backend.get_unit_topology(-1) == PathTopology::HUB);
    REQUIRE(backend.get_unit_topology(99) == PathTopology::HUB);
}

TEST_CASE("Mixed topology system type is AFC", "[ams][mock][mixed]") {
    AmsBackendMock backend(4);
    backend.set_mixed_topology_mode(true);

    CHECK(backend.get_type() == AmsType::AFC);
}
