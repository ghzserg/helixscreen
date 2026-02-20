// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_afc_multi_extruder.cpp
 * @brief Unit tests for AFC toolchanger multi-extruder data layer
 *
 * When AFC detects a toolchanger, the webhook status at /printer/afc/status
 * includes system.num_extruders and per-extruder info in system.extruders.
 * This file tests parsing, storage, and device action generation for that data.
 *
 * Test tags: [ams][afc][multi_extruder]
 */

#include "ams_backend_afc.h"
#include "ams_types.h"

#include <algorithm>
#include <any>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix;
// ============================================================================
// Test helper for multi-extruder AFC parsing
// ============================================================================

/**
 * @brief Test helper exposing AFC internals for multi-extruder testing
 *
 * Extends AmsBackendAfc to provide access to extruder state and
 * the ability to feed mock status updates.
 */
class AmsBackendAfcMultiExtruderHelper : public AmsBackendAfc {
  public:
    AmsBackendAfcMultiExtruderHelper() : AmsBackendAfc(nullptr, nullptr) {}

    // Feed a Moonraker notify_status_update notification through the backend
    void feed_status_update(const nlohmann::json& params_inner) {
        nlohmann::json notification;
        notification["params"] = nlohmann::json::array({params_inner, 0.0});
        handle_status_update(notification);
    }

    // Feed AFC global state update
    void feed_afc_state(const nlohmann::json& afc_data) {
        nlohmann::json params;
        params["AFC"] = afc_data;
        feed_status_update(params);
    }

    // Feed AFC_extruder update
    void feed_afc_extruder(const std::string& ext_name, const nlohmann::json& data) {
        nlohmann::json params;
        params["AFC_extruder " + ext_name] = data;
        feed_status_update(params);
    }

    // Initialize lanes and slots for testing
    void initialize_test_lanes_with_slots(int count) {
        system_info_.units.clear();
        std::vector<std::string> names;

        AmsUnit unit;
        unit.unit_index = 0;
        unit.name = "Box Turtle 1";
        unit.slot_count = count;
        unit.first_slot_global_index = 0;

        for (int i = 0; i < count; ++i) {
            std::string name = "lane" + std::to_string(i + 1);
            names.push_back(name);

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

        // Initialize tool-to-slot mapping
        system_info_.tool_to_slot_map.clear();
        for (int i = 0; i < count; ++i) {
            system_info_.tool_to_slot_map.push_back(i);
        }

        slots_.initialize("Box Turtle 1", names);
    }

    // Set discovered lanes (delegates to base)
    void setup_discovered_lanes(const std::vector<std::string>& lanes,
                                const std::vector<std::string>& hubs) {
        set_discovered_lanes(lanes, hubs);
    }

    // Accessors for extruder state
    int get_num_extruders() const {
        return num_extruders_;
    }

    const std::vector<AfcExtruderInfo>& get_extruders() const {
        return extruders_;
    }

    // Access system_info for assertions
    const AmsSystemInfo& get_system_info_ref() const {
        return system_info_;
    }

    // Override execute_gcode to capture commands
    std::vector<std::string> captured_gcodes;

    AmsError execute_gcode(const std::string& gcode) override {
        captured_gcodes.push_back(gcode);
        return AmsErrorHelper::success();
    }

    bool has_gcode(const std::string& expected) const {
        return std::find(captured_gcodes.begin(), captured_gcodes.end(), expected) !=
               captured_gcodes.end();
    }

    bool has_gcode_starting_with(const std::string& prefix) const {
        for (const auto& gcode : captured_gcodes) {
            if (gcode.rfind(prefix, 0) == 0)
                return true;
        }
        return false;
    }
};

// ============================================================================
// AfcExtruderInfo struct tests
// ============================================================================

TEST_CASE("AfcExtruderInfo default construction", "[ams][afc][multi_extruder]") {
    AfcExtruderInfo info{};

    CHECK(info.name.empty());
    CHECK(info.lane_loaded.empty());
    CHECK(info.available_lanes.empty());
}

TEST_CASE("AfcExtruderInfo construction with values", "[ams][afc][multi_extruder]") {
    AfcExtruderInfo info;
    info.name = "extruder";
    info.lane_loaded = "lane1";
    info.available_lanes = {"lane1", "lane2"};

    CHECK(info.name == "extruder");
    CHECK(info.lane_loaded == "lane1");
    CHECK(info.available_lanes.size() == 2);
    CHECK(info.available_lanes[0] == "lane1");
    CHECK(info.available_lanes[1] == "lane2");
}

// ============================================================================
// Single extruder (standard AFC, no toolchanger)
// ============================================================================

TEST_CASE("AFC single extruder: num_extruders defaults to 1", "[ams][afc][multi_extruder]") {
    AmsBackendAfcMultiExtruderHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // No extruder data fed — default state
    CHECK(helper.get_num_extruders() == 1);
}

TEST_CASE("AFC single extruder: explicit num_extruders=1 in AFC state",
          "[ams][afc][multi_extruder]") {
    AmsBackendAfcMultiExtruderHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // AFC reports a single extruder explicitly
    nlohmann::json afc_data = {
        {"system",
         {{"num_extruders", 1},
          {"extruders",
           {{"extruder",
             {{"lane_loaded", "lane1"}, {"lanes", {"lane1", "lane2", "lane3", "lane4"}}}}}}}}};
    helper.feed_afc_state(afc_data);

    CHECK(helper.get_num_extruders() == 1);
    REQUIRE(helper.get_extruders().size() == 1);
    CHECK(helper.get_extruders()[0].name == "extruder");
    CHECK(helper.get_extruders()[0].lane_loaded == "lane1");
    CHECK(helper.get_extruders()[0].available_lanes.size() == 4);
}

TEST_CASE("AFC single extruder: extruders_ populated with all lanes",
          "[ams][afc][multi_extruder]") {
    AmsBackendAfcMultiExtruderHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    nlohmann::json afc_data = {
        {"system",
         {{"num_extruders", 1},
          {"extruders",
           {{"extruder",
             {{"lane_loaded", "lane2"}, {"lanes", {"lane1", "lane2", "lane3", "lane4"}}}}}}}}};
    helper.feed_afc_state(afc_data);

    REQUIRE(helper.get_extruders().size() == 1);
    const auto& ext = helper.get_extruders()[0];
    CHECK(ext.lane_loaded == "lane2");
    CHECK(ext.available_lanes == std::vector<std::string>{"lane1", "lane2", "lane3", "lane4"});
}

// ============================================================================
// Multi-extruder (toolchanger with AFC)
// ============================================================================

TEST_CASE("AFC multi-extruder: num_extruders=2 with two extruder entries",
          "[ams][afc][multi_extruder]") {
    AmsBackendAfcMultiExtruderHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    nlohmann::json afc_data = {
        {"system",
         {{"num_extruders", 2},
          {"extruders",
           {{"extruder", {{"lane_loaded", "lane1"}, {"lanes", {"lane1", "lane2"}}}},
            {"extruder1", {{"lane_loaded", ""}, {"lanes", {"lane3", "lane4"}}}}}}}}};
    helper.feed_afc_state(afc_data);

    CHECK(helper.get_num_extruders() == 2);
    REQUIRE(helper.get_extruders().size() == 2);
}

TEST_CASE("AFC multi-extruder: extruder entries have correct names and lanes",
          "[ams][afc][multi_extruder]") {
    AmsBackendAfcMultiExtruderHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    nlohmann::json afc_data = {
        {"system",
         {{"num_extruders", 2},
          {"extruders",
           {{"extruder", {{"lane_loaded", "lane1"}, {"lanes", {"lane1", "lane2"}}}},
            {"extruder1", {{"lane_loaded", ""}, {"lanes", {"lane3", "lane4"}}}}}}}}};
    helper.feed_afc_state(afc_data);

    // Extruders should be sorted by name for deterministic ordering
    const auto& extruders = helper.get_extruders();

    // "extruder" sorts before "extruder1"
    CHECK(extruders[0].name == "extruder");
    CHECK(extruders[0].lane_loaded == "lane1");
    CHECK(extruders[0].available_lanes == std::vector<std::string>{"lane1", "lane2"});

    CHECK(extruders[1].name == "extruder1");
    CHECK(extruders[1].lane_loaded.empty());
    CHECK(extruders[1].available_lanes == std::vector<std::string>{"lane3", "lane4"});
}

TEST_CASE("AFC multi-extruder: lane_loaded tracks which lane feeds each extruder",
          "[ams][afc][multi_extruder]") {
    AmsBackendAfcMultiExtruderHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // Initially, extruder has lane1 loaded, extruder1 has lane4 loaded
    nlohmann::json afc_data = {
        {"system",
         {{"num_extruders", 2},
          {"extruders",
           {{"extruder", {{"lane_loaded", "lane1"}, {"lanes", {"lane1", "lane2"}}}},
            {"extruder1", {{"lane_loaded", "lane4"}, {"lanes", {"lane3", "lane4"}}}}}}}}};
    helper.feed_afc_state(afc_data);

    const auto& extruders = helper.get_extruders();
    CHECK(extruders[0].lane_loaded == "lane1");
    CHECK(extruders[1].lane_loaded == "lane4");
}

TEST_CASE("AFC multi-extruder: lane_loaded can be empty (no filament loaded)",
          "[ams][afc][multi_extruder]") {
    AmsBackendAfcMultiExtruderHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    nlohmann::json afc_data = {
        {"system",
         {{"num_extruders", 2},
          {"extruders",
           {{"extruder", {{"lane_loaded", ""}, {"lanes", {"lane1", "lane2"}}}},
            {"extruder1", {{"lane_loaded", ""}, {"lanes", {"lane3", "lane4"}}}}}}}}};
    helper.feed_afc_state(afc_data);

    const auto& extruders = helper.get_extruders();
    CHECK(extruders[0].lane_loaded.empty());
    CHECK(extruders[1].lane_loaded.empty());
}

// ============================================================================
// Lane-to-extruder mapping
// ============================================================================

TEST_CASE("AFC multi-extruder: each extruder tracks its available lanes",
          "[ams][afc][multi_extruder]") {
    AmsBackendAfcMultiExtruderHelper helper;
    helper.initialize_test_lanes_with_slots(8);

    nlohmann::json afc_data = {
        {"system",
         {{"num_extruders", 2},
          {"extruders",
           {{"extruder",
             {{"lane_loaded", "lane1"}, {"lanes", {"lane1", "lane2", "lane3", "lane4"}}}},
            {"extruder1",
             {{"lane_loaded", "lane5"}, {"lanes", {"lane5", "lane6", "lane7", "lane8"}}}}}}}}};
    helper.feed_afc_state(afc_data);

    const auto& extruders = helper.get_extruders();
    REQUIRE(extruders.size() == 2);
    CHECK(extruders[0].available_lanes.size() == 4);
    CHECK(extruders[1].available_lanes.size() == 4);
    CHECK(extruders[0].available_lanes[0] == "lane1");
    CHECK(extruders[1].available_lanes[0] == "lane5");
}

// ============================================================================
// Per-extruder bowden length device action
// ============================================================================

TEST_CASE("AFC single extruder: single bowden_length action", "[ams][afc][multi_extruder]") {
    AmsBackendAfcMultiExtruderHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // Single extruder — should get the standard single bowden_length action
    auto actions = helper.get_device_actions();

    // Count calibration-section bowden actions (not hub_bowden_length from config)
    int bowden_count = 0;
    for (const auto& action : actions) {
        if (action.id.find("bowden") != std::string::npos && action.section == "setup") {
            bowden_count++;
        }
    }
    CHECK(bowden_count == 1);
}

TEST_CASE("AFC multi-extruder: per-extruder bowden_length actions", "[ams][afc][multi_extruder]") {
    AmsBackendAfcMultiExtruderHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // Set up 2 extruders
    nlohmann::json afc_data = {
        {"system",
         {{"num_extruders", 2},
          {"extruders",
           {{"extruder", {{"lane_loaded", "lane1"}, {"lanes", {"lane1", "lane2"}}}},
            {"extruder1", {{"lane_loaded", ""}, {"lanes", {"lane3", "lane4"}}}}}}}}};
    helper.feed_afc_state(afc_data);

    auto actions = helper.get_device_actions();

    // Should have per-extruder bowden actions instead of single one
    bool has_bowden_t0 = false;
    bool has_bowden_t1 = false;
    bool has_generic_bowden = false;

    for (const auto& action : actions) {
        if (action.id == "bowden_T0")
            has_bowden_t0 = true;
        if (action.id == "bowden_T1")
            has_bowden_t1 = true;
        if (action.id == "bowden_length")
            has_generic_bowden = true;
    }

    CHECK(has_bowden_t0);
    CHECK(has_bowden_t1);
    // Generic bowden should be replaced by per-extruder bowdens
    CHECK_FALSE(has_generic_bowden);
}

TEST_CASE("AFC multi-extruder: bowden actions have correct labels", "[ams][afc][multi_extruder]") {
    AmsBackendAfcMultiExtruderHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    nlohmann::json afc_data = {
        {"system",
         {{"num_extruders", 2},
          {"extruders",
           {{"extruder", {{"lane_loaded", ""}, {"lanes", {"lane1", "lane2"}}}},
            {"extruder1", {{"lane_loaded", ""}, {"lanes", {"lane3", "lane4"}}}}}}}}};
    helper.feed_afc_state(afc_data);

    auto actions = helper.get_device_actions();

    for (const auto& action : actions) {
        if (action.id == "bowden_T0") {
            CHECK(action.label.find("T0") != std::string::npos);
            CHECK(action.section == "setup");
            CHECK(action.type == helix::printer::ActionType::SLIDER);
            CHECK(action.unit == "mm");
        }
        if (action.id == "bowden_T1") {
            CHECK(action.label.find("T1") != std::string::npos);
            CHECK(action.section == "setup");
            CHECK(action.type == helix::printer::ActionType::SLIDER);
            CHECK(action.unit == "mm");
        }
    }
}

// ============================================================================
// State update: extruder data updates on subsequent AFC state messages
// ============================================================================

TEST_CASE("AFC multi-extruder: state updates replace extruder data", "[ams][afc][multi_extruder]") {
    AmsBackendAfcMultiExtruderHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // First update: lane1 loaded in extruder
    nlohmann::json afc_data1 = {
        {"system",
         {{"num_extruders", 2},
          {"extruders",
           {{"extruder", {{"lane_loaded", "lane1"}, {"lanes", {"lane1", "lane2"}}}},
            {"extruder1", {{"lane_loaded", ""}, {"lanes", {"lane3", "lane4"}}}}}}}}};
    helper.feed_afc_state(afc_data1);

    CHECK(helper.get_extruders()[0].lane_loaded == "lane1");
    CHECK(helper.get_extruders()[1].lane_loaded.empty());

    // Second update: lane loaded changes
    nlohmann::json afc_data2 = {
        {"system",
         {{"num_extruders", 2},
          {"extruders",
           {{"extruder", {{"lane_loaded", "lane2"}, {"lanes", {"lane1", "lane2"}}}},
            {"extruder1", {{"lane_loaded", "lane3"}, {"lanes", {"lane3", "lane4"}}}}}}}}};
    helper.feed_afc_state(afc_data2);

    CHECK(helper.get_extruders()[0].lane_loaded == "lane2");
    CHECK(helper.get_extruders()[1].lane_loaded == "lane3");
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_CASE("AFC multi-extruder: missing system object is no-op", "[ams][afc][multi_extruder]") {
    AmsBackendAfcMultiExtruderHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // Feed AFC state with no system key
    nlohmann::json afc_data = {{"current_state", "Idle"}};
    helper.feed_afc_state(afc_data);

    // Should keep defaults
    CHECK(helper.get_num_extruders() == 1);
    CHECK(helper.get_extruders().empty());
}

TEST_CASE("AFC multi-extruder: system with no extruders key is no-op",
          "[ams][afc][multi_extruder]") {
    AmsBackendAfcMultiExtruderHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // system exists but has no extruders
    nlohmann::json afc_data = {{"system", {{"num_extruders", 2}}}};
    helper.feed_afc_state(afc_data);

    // num_extruders should be updated but extruders_ stays empty
    CHECK(helper.get_num_extruders() == 2);
    CHECK(helper.get_extruders().empty());
}

TEST_CASE("AFC multi-extruder: extruder with missing lanes array", "[ams][afc][multi_extruder]") {
    AmsBackendAfcMultiExtruderHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // extruder entry missing "lanes" key
    nlohmann::json afc_data = {
        {"system",
         {{"num_extruders", 1}, {"extruders", {{"extruder", {{"lane_loaded", "lane1"}}}}}}}};
    helper.feed_afc_state(afc_data);

    REQUIRE(helper.get_extruders().size() == 1);
    CHECK(helper.get_extruders()[0].name == "extruder");
    CHECK(helper.get_extruders()[0].lane_loaded == "lane1");
    CHECK(helper.get_extruders()[0].available_lanes.empty());
}

TEST_CASE("AFC multi-extruder: extruder with null lane_loaded", "[ams][afc][multi_extruder]") {
    AmsBackendAfcMultiExtruderHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    nlohmann::json afc_data = {
        {"system",
         {{"num_extruders", 1},
          {"extruders",
           {{"extruder", {{"lane_loaded", nullptr}, {"lanes", {"lane1", "lane2"}}}}}}}}};
    helper.feed_afc_state(afc_data);

    REQUIRE(helper.get_extruders().size() == 1);
    // null lane_loaded should result in empty string
    CHECK(helper.get_extruders()[0].lane_loaded.empty());
}
