// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_tool_state.cpp
 * @brief Tests for ToolInfo struct, DetectState enum, and ToolState singleton
 */

#include "../ui_test_utils.h"
#include "printer_discovery.h"
#include "tool_state.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// ToolInfo struct tests
// ============================================================================

TEST_CASE("ToolInfo: default construction", "[tool][tool-state]") {
    ToolInfo info;

    REQUIRE(info.index == 0);
    REQUIRE(info.name == "T0");
    REQUIRE(info.extruder_name.has_value());
    REQUIRE(info.extruder_name.value() == "extruder");
    REQUIRE_FALSE(info.heater_name.has_value());
    REQUIRE_FALSE(info.fan_name.has_value());
    REQUIRE(info.gcode_x_offset == 0.0f);
    REQUIRE(info.gcode_y_offset == 0.0f);
    REQUIRE(info.gcode_z_offset == 0.0f);
    REQUIRE_FALSE(info.active);
    REQUIRE_FALSE(info.mounted);
    REQUIRE(info.detect_state == DetectState::UNAVAILABLE);
    REQUIRE(info.backend_index == -1);
    REQUIRE(info.backend_slot == -1);
}

TEST_CASE("ToolInfo: default backend mapping is unassigned", "[tool][tool-state]") {
    ToolInfo info;
    REQUIRE(info.backend_index == -1);
    REQUIRE(info.backend_slot == -1);
}

TEST_CASE("ToolInfo: effective_heater prefers heater_name", "[tool][tool-state]") {
    ToolInfo info;
    info.heater_name = "heater_generic chamber";
    info.extruder_name = "extruder1";

    REQUIRE(info.effective_heater() == "heater_generic chamber");
}

TEST_CASE("ToolInfo: effective_heater falls back to extruder_name", "[tool][tool-state]") {
    ToolInfo info;
    info.extruder_name = "extruder1";
    // heater_name not set

    REQUIRE(info.effective_heater() == "extruder1");
}

TEST_CASE("ToolInfo: effective_heater fallback when nothing set", "[tool][tool-state]") {
    ToolInfo info;
    info.extruder_name = std::nullopt;
    info.heater_name = std::nullopt;

    REQUIRE(info.effective_heater() == "extruder");
}

// ============================================================================
// DetectState enum tests
// ============================================================================

TEST_CASE("DetectState: enum values", "[tool][tool-state]") {
    REQUIRE(static_cast<int>(DetectState::PRESENT) == 0);
    REQUIRE(static_cast<int>(DetectState::ABSENT) == 1);
    REQUIRE(static_cast<int>(DetectState::UNAVAILABLE) == 2);
}

// ============================================================================
// ToolState singleton tests
// ============================================================================

TEST_CASE("ToolState: singleton access", "[tool][tool-state]") {
    ToolState& a = ToolState::instance();
    ToolState& b = ToolState::instance();

    REQUIRE(&a == &b);
}

TEST_CASE("ToolState: init_subjects creates subjects", "[tool][tool-state]") {
    lv_init_safe();

    ToolState& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    REQUIRE(ts.get_active_tool_subject() != nullptr);
    REQUIRE(ts.get_tool_count_subject() != nullptr);
    REQUIRE(ts.get_tools_version_subject() != nullptr);

    REQUIRE(lv_subject_get_int(ts.get_active_tool_subject()) == 0);
    REQUIRE(lv_subject_get_int(ts.get_tool_count_subject()) == 0);
    REQUIRE(lv_subject_get_int(ts.get_tools_version_subject()) == 0);
}

TEST_CASE("ToolState: double init is safe", "[tool][tool-state]") {
    lv_init_safe();

    ToolState& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);
    ts.init_subjects(false); // Should be a no-op

    REQUIRE(lv_subject_get_int(ts.get_active_tool_subject()) == 0);
}

TEST_CASE("ToolState: deinit then re-init", "[tool][tool-state]") {
    lv_init_safe();

    ToolState& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    // Set a value
    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array({"gcode_move"});
    hw.parse_objects(objects);
    ts.init_tools(hw);
    REQUIRE(ts.tool_count() == 1);

    // Deinit clears state
    ts.deinit_subjects();
    REQUIRE(ts.tool_count() == 0);

    // Re-init works
    ts.init_subjects(false);
    REQUIRE(lv_subject_get_int(ts.get_tool_count_subject()) == 0);
}

// ============================================================================
// init_tools tests
// ============================================================================

TEST_CASE("ToolState: init_tools with no tools creates implicit tool", "[tool][tool-state]") {
    lv_init_safe();

    ToolState& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array({"extruder", "heater_bed", "fan", "gcode_move"});
    hw.parse_objects(objects);

    ts.init_tools(hw);

    REQUIRE(ts.tool_count() == 1);
    REQUIRE(lv_subject_get_int(ts.get_tool_count_subject()) == 1);

    const auto& tools = ts.tools();
    REQUIRE(tools.size() == 1);
    REQUIRE(tools[0].name == "T0");
    REQUIRE(tools[0].extruder_name.value() == "extruder");
    REQUIRE(tools[0].fan_name.value() == "fan");
    REQUIRE(tools[0].active == true);
    REQUIRE(tools[0].index == 0);
}

TEST_CASE("ToolState: init_tools with toolchanger creates N tools", "[tool][tool-state]") {
    lv_init_safe();

    ToolState& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects =
        nlohmann::json::array({"toolchanger", "tool T0", "tool T1", "tool T2", "extruder",
                               "extruder1", "extruder2", "heater_bed", "gcode_move"});
    hw.parse_objects(objects);

    int version_before = lv_subject_get_int(ts.get_tools_version_subject());
    ts.init_tools(hw);
    int version_after = lv_subject_get_int(ts.get_tools_version_subject());

    REQUIRE(ts.tool_count() == 3);
    REQUIRE(version_after == version_before + 1);

    const auto& tools = ts.tools();
    REQUIRE(tools[0].name == "T0");
    REQUIRE(tools[0].extruder_name.value() == "extruder");
    REQUIRE(tools[1].name == "T1");
    REQUIRE(tools[1].extruder_name.value() == "extruder1");
    REQUIRE(tools[2].name == "T2");
    REQUIRE(tools[2].extruder_name.value() == "extruder2");
}

TEST_CASE("ToolState: active_tool accessors", "[tool][tool-state]") {
    lv_init_safe();

    ToolState& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array(
        {"toolchanger", "tool T0", "tool T1", "extruder", "extruder1", "heater_bed", "gcode_move"});
    hw.parse_objects(objects);
    ts.init_tools(hw);

    REQUIRE(ts.active_tool_index() == 0);
    REQUIRE(ts.active_tool() != nullptr);
    REQUIRE(ts.active_tool()->name == "T0");
}

TEST_CASE("ToolState: re-init with different tool count", "[tool][tool-state]") {
    lv_init_safe();

    ToolState& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    // First init: 1 implicit tool
    helix::PrinterDiscovery hw1;
    nlohmann::json objects1 = nlohmann::json::array({"extruder", "gcode_move"});
    hw1.parse_objects(objects1);
    ts.init_tools(hw1);

    int v1 = lv_subject_get_int(ts.get_tools_version_subject());
    REQUIRE(ts.tool_count() == 1);

    // Second init: 2 tools
    helix::PrinterDiscovery hw2;
    nlohmann::json objects2 = nlohmann::json::array(
        {"toolchanger", "tool T0", "tool T1", "extruder", "extruder1", "gcode_move"});
    hw2.parse_objects(objects2);
    ts.init_tools(hw2);

    int v2 = lv_subject_get_int(ts.get_tools_version_subject());
    REQUIRE(ts.tool_count() == 2);
    REQUIRE(v2 == v1 + 1);
}

// ============================================================================
// update_from_status tests
// ============================================================================

TEST_CASE("ToolState: update_from_status sets active tool", "[tool][tool-state]") {
    lv_init_safe();

    ToolState& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array(
        {"toolchanger", "tool T0", "tool T1", "extruder", "extruder1", "gcode_move"});
    hw.parse_objects(objects);
    ts.init_tools(hw);

    nlohmann::json status = {{"toolchanger", {{"tool_number", 1}}}};
    ts.update_from_status(status);

    REQUIRE(ts.active_tool_index() == 1);
    REQUIRE(lv_subject_get_int(ts.get_active_tool_subject()) == 1);
    REQUIRE(ts.active_tool()->name == "T1");
}

TEST_CASE("ToolState: update_from_status sets mounted state", "[tool][tool-state]") {
    lv_init_safe();

    ToolState& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array(
        {"toolchanger", "tool T0", "tool T1", "extruder", "extruder1", "gcode_move"});
    hw.parse_objects(objects);
    ts.init_tools(hw);

    nlohmann::json status = {{"tool T0", {{"mounted", true}, {"active", true}}},
                             {"tool T1", {{"mounted", false}, {"active", false}}}};
    ts.update_from_status(status);

    REQUIRE(ts.tools()[0].mounted == true);
    REQUIRE(ts.tools()[0].active == true);
    REQUIRE(ts.tools()[1].mounted == false);
    REQUIRE(ts.tools()[1].active == false);
}

TEST_CASE("ToolState: update_from_status parses offsets", "[tool][tool-state]") {
    lv_init_safe();

    ToolState& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array(
        {"toolchanger", "tool T0", "tool T1", "extruder", "extruder1", "gcode_move"});
    hw.parse_objects(objects);
    ts.init_tools(hw);

    nlohmann::json status = {
        {"tool T1", {{"gcode_x_offset", 1.5}, {"gcode_y_offset", -2.3}, {"gcode_z_offset", 0.15}}}};
    ts.update_from_status(status);

    REQUIRE(ts.tools()[1].gcode_x_offset == Catch::Approx(1.5f));
    REQUIRE(ts.tools()[1].gcode_y_offset == Catch::Approx(-2.3f));
    REQUIRE(ts.tools()[1].gcode_z_offset == Catch::Approx(0.15f));
}

TEST_CASE("ToolState: update_from_status with no tools is safe", "[tool][tool-state]") {
    lv_init_safe();

    ToolState& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    // No init_tools called, tools_ is empty
    nlohmann::json status = {{"toolchanger", {{"tool_number", 1}}}};
    ts.update_from_status(status); // Should not crash
}

TEST_CASE("ToolState: update_from_status tool_number -1 means no tool", "[tool][tool-state]") {
    lv_init_safe();

    ToolState& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array(
        {"toolchanger", "tool T0", "tool T1", "extruder", "extruder1", "gcode_move"});
    hw.parse_objects(objects);
    ts.init_tools(hw);

    // Set active to T1 first
    nlohmann::json status1 = {{"toolchanger", {{"tool_number", 1}}}};
    ts.update_from_status(status1);
    REQUIRE(ts.active_tool_index() == 1);
    REQUIRE(ts.active_tool() != nullptr);

    // -1 means no tool mounted
    nlohmann::json status2 = {{"toolchanger", {{"tool_number", -1}}}};
    ts.update_from_status(status2);
    REQUIRE(ts.active_tool_index() == -1);
    REQUIRE(ts.active_tool() == nullptr);
}

// ============================================================================
// Lifecycle edge case tests
// ============================================================================

TEST_CASE("ToolState: update_from_status captures extruder and fan from Klipper",
          "[tool][tool-state]") {
    lv_init_safe();

    ToolState& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array(
        {"toolchanger", "tool T0", "tool T1", "extruder", "extruder1", "heater_bed", "gcode_move"});
    hw.parse_objects(objects);
    ts.init_tools(hw);

    // Klipper sends extruder association in tool status
    nlohmann::json status = {{"tool T0", {{"extruder", "extruder"}, {"fan", "part_fan_T0"}}},
                             {"tool T1", {{"extruder", "extruder1"}, {"fan", "part_fan_T1"}}}};
    ts.update_from_status(status);

    REQUIRE(ts.tools()[0].extruder_name.value() == "extruder");
    REQUIRE(ts.tools()[0].fan_name.value() == "part_fan_T0");
    REQUIRE(ts.tools()[1].extruder_name.value() == "extruder1");
    REQUIRE(ts.tools()[1].fan_name.value() == "part_fan_T1");

    ts.deinit_subjects();
}

TEST_CASE("ToolState: detect_state parsed from status", "[tool][tool-state]") {
    lv_init_safe();

    ToolState& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects =
        nlohmann::json::array({"toolchanger", "tool T0", "extruder", "heater_bed", "gcode_move"});
    hw.parse_objects(objects);
    ts.init_tools(hw);

    nlohmann::json status = {{"tool T0", {{"detect_state", "present"}}}};
    ts.update_from_status(status);
    REQUIRE(ts.tools()[0].detect_state == DetectState::PRESENT);

    // Also test "absent"
    nlohmann::json status2 = {{"tool T0", {{"detect_state", "absent"}}}};
    ts.update_from_status(status2);
    REQUIRE(ts.tools()[0].detect_state == DetectState::ABSENT);

    ts.deinit_subjects();
}
