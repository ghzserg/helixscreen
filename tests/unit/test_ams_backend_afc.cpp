// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_afc.h"
#include "ams_types.h"
#include "moonraker_api.h"

#include <algorithm>
#include <vector>

#include "../catch_amalgamated.hpp"

/**
 * @brief Test helper class providing access to AmsBackendAfc internals
 *
 * This class provides controlled access to private members for unit testing.
 * It does NOT start the backend (no Moonraker connection needed).
 */
class AmsBackendAfcTestHelper : public AmsBackendAfc {
  public:
    AmsBackendAfcTestHelper() : AmsBackendAfc(nullptr, nullptr) {}

    // Version testing helpers
    void set_afc_version(const std::string& version) {
        afc_version_ = version;
    }

    bool test_version_at_least(const std::string& required) const {
        return version_at_least(required);
    }

    // Sensor state setters for compute_filament_segment_unlocked testing
    void set_tool_end_sensor(bool state) {
        tool_end_sensor_ = state;
    }
    void set_tool_start_sensor(bool state) {
        tool_start_sensor_ = state;
    }
    void set_hub_sensor(bool state) {
        hub_sensor_ = state;
    }

    void set_current_lane(const std::string& lane_name) {
        current_lane_name_ = lane_name;
        // Update lane_name_to_index if not present
        if (!lane_name.empty() && lane_name_to_index_.count(lane_name) == 0) {
            int idx = static_cast<int>(lane_names_.size());
            lane_names_.push_back(lane_name);
            lane_name_to_index_[lane_name] = idx;
        }
    }

    void initialize_test_lanes(int count) {
        lane_names_.clear();
        lane_name_to_index_.clear();
        for (int i = 0; i < count; ++i) {
            std::string name = "lane" + std::to_string(i + 1);
            lane_names_.push_back(name);
            lane_name_to_index_[name] = i;
        }
        // Reset lane sensors
        for (int i = 0; i < 16; ++i) {
            lane_sensors_[i] = LaneSensors{};
        }
    }

    void set_lane_prep_sensor(int lane_index, bool state) {
        if (lane_index >= 0 && lane_index < 16) {
            lane_sensors_[lane_index].prep = state;
        }
    }

    void set_lane_load_sensor(int lane_index, bool state) {
        if (lane_index >= 0 && lane_index < 16) {
            lane_sensors_[lane_index].load = state;
        }
    }

    void set_lane_loaded_to_hub(int lane_index, bool state) {
        if (lane_index >= 0 && lane_index < 16) {
            lane_sensors_[lane_index].loaded_to_hub = state;
        }
    }

    void set_filament_loaded(bool state) {
        system_info_.filament_loaded = state;
    }

    void set_current_slot(int slot) {
        system_info_.current_slot = slot;
    }

    PathSegment test_compute_filament_segment() const {
        return compute_filament_segment_unlocked();
    }

    // Discovery testing helpers
    const std::vector<std::string>& get_lane_names() const {
        return lane_names_;
    }

    const std::vector<std::string>& get_hub_names() const {
        return hub_names_;
    }

    void initialize_lanes_from_discovery() {
        // Simulates what start() does when lanes are pre-set via set_discovered_lanes()
        if (!lane_names_.empty() && !lanes_initialized_) {
            initialize_lanes(lane_names_);
        }
    }

    // Persistence testing helpers
    void initialize_test_lanes_with_slots(int count) {
        lane_names_.clear();
        lane_name_to_index_.clear();
        system_info_.units.clear();

        AmsUnit unit;
        unit.unit_index = 0;
        unit.name = "Box Turtle 1";
        unit.slot_count = count;
        unit.first_slot_global_index = 0;

        for (int i = 0; i < count; ++i) {
            std::string name = "lane" + std::to_string(i + 1);
            lane_names_.push_back(name);
            lane_name_to_index_[name] = i;

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
        lanes_initialized_ = true;
    }

    SlotInfo* get_mutable_slot(int slot_index) {
        return system_info_.get_slot_global(slot_index);
    }

    // Initialize endless spool configs for reset testing
    void initialize_endless_spool_configs(int count) {
        endless_spool_configs_.clear();
        for (int i = 0; i < count; ++i) {
            helix::printer::EndlessSpoolConfig config;
            config.slot_index = i;
            config.backup_slot = -1;
            endless_spool_configs_.push_back(config);
        }
    }

    // Set a specific endless spool backup for testing
    void set_endless_spool_config(int slot, int backup) {
        if (slot >= 0 && slot < static_cast<int>(endless_spool_configs_.size())) {
            endless_spool_configs_[slot].backup_slot = backup;
        }
    }

    // For persistence tests: capture G-code commands
    std::vector<std::string> captured_gcodes;

    // Override execute_gcode to capture commands for testing
    AmsError execute_gcode(const std::string& gcode) override {
        captured_gcodes.push_back(gcode);
        return AmsErrorHelper::success();
    }

    void clear_captured_gcodes() {
        captured_gcodes.clear();
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
// version_at_least() - Semantic Version Comparison Tests
// ============================================================================

TEST_CASE("AFC version_at_least: equal versions", "[ams][afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("1.0.32");

    REQUIRE(helper.test_version_at_least("1.0.32") == true);
}

TEST_CASE("AFC version_at_least: greater patch version", "[ams][afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("1.0.33");

    REQUIRE(helper.test_version_at_least("1.0.32") == true);
}

TEST_CASE("AFC version_at_least: greater minor version", "[ams][afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("1.1.0");

    REQUIRE(helper.test_version_at_least("1.0.32") == true);
}

TEST_CASE("AFC version_at_least: greater major version", "[ams][afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("2.0.0");

    REQUIRE(helper.test_version_at_least("1.0.32") == true);
}

TEST_CASE("AFC version_at_least: lesser patch version fails", "[ams][afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("1.0.31");

    REQUIRE(helper.test_version_at_least("1.0.32") == false);
}

TEST_CASE("AFC version_at_least: unknown version fails", "[ams][afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("unknown");

    REQUIRE(helper.test_version_at_least("1.0.32") == false);
}

TEST_CASE("AFC version_at_least: empty version fails", "[ams][afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("");

    REQUIRE(helper.test_version_at_least("1.0.32") == false);
}

TEST_CASE("AFC version_at_least: lesser minor version fails", "[ams][afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("1.0.0");

    REQUIRE(helper.test_version_at_least("1.1.0") == false);
}

TEST_CASE("AFC version_at_least: lesser major version fails", "[ams][afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("1.99.99");

    REQUIRE(helper.test_version_at_least("2.0.0") == false);
}

TEST_CASE("AFC version_at_least: high patch vs low minor", "[ams][afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("1.0.100");

    // 1.0.100 is still < 1.1.0 because minor takes precedence
    REQUIRE(helper.test_version_at_least("1.1.0") == false);
}

TEST_CASE("AFC version_at_least: handles two-part version", "[ams][afc][version]") {
    AmsBackendAfcTestHelper helper;
    // Version parsing uses istringstream which may handle partial versions
    helper.set_afc_version("1.0");

    // Should treat missing patch as 0, so 1.0.0 >= 1.0.0
    REQUIRE(helper.test_version_at_least("1.0.0") == true);
}

// ============================================================================
// compute_filament_segment_unlocked() - Sensor-to-Segment Mapping Tests
// ============================================================================

TEST_CASE("AFC segment: no sensors triggered returns NONE", "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);

    // No sensors triggered, no filament loaded, no current slot
    REQUIRE(helper.test_compute_filament_segment() == PathSegment::NONE);
}

TEST_CASE("AFC segment: filament loaded flag returns SPOOL when no sensors",
          "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_filament_loaded(true);

    // Filament is "loaded" but no sensors triggered - implies at spool
    REQUIRE(helper.test_compute_filament_segment() == PathSegment::SPOOL);
}

TEST_CASE("AFC segment: current slot set returns SPOOL when no sensors", "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_current_slot(0);

    // A slot is selected but no sensors - filament at spool area
    REQUIRE(helper.test_compute_filament_segment() == PathSegment::SPOOL);
}

TEST_CASE("AFC segment: prep sensor triggered returns PREP", "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_current_lane("lane1");
    helper.set_lane_prep_sensor(0, true);

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::PREP);
}

TEST_CASE("AFC segment: prep and load sensors return LANE", "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_current_lane("lane1");
    helper.set_lane_prep_sensor(0, true);
    helper.set_lane_load_sensor(0, true);

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::LANE);
}

TEST_CASE("AFC segment: loaded_to_hub returns HUB", "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_current_lane("lane1");
    helper.set_lane_prep_sensor(0, true);
    helper.set_lane_load_sensor(0, true);
    helper.set_lane_loaded_to_hub(0, true);

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::HUB);
}

TEST_CASE("AFC segment: hub_sensor returns OUTPUT", "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_current_lane("lane1");
    helper.set_lane_loaded_to_hub(0, true);
    helper.set_hub_sensor(true);

    // Hub sensor indicates filament past the hub merger, heading to toolhead
    REQUIRE(helper.test_compute_filament_segment() == PathSegment::OUTPUT);
}

TEST_CASE("AFC segment: tool_start_sensor returns TOOLHEAD", "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_hub_sensor(true);
    helper.set_tool_start_sensor(true);

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::TOOLHEAD);
}

TEST_CASE("AFC segment: tool_end_sensor returns NOZZLE", "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_hub_sensor(true);
    helper.set_tool_start_sensor(true);
    helper.set_tool_end_sensor(true);

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::NOZZLE);
}

TEST_CASE("AFC segment: tool_end_sensor alone returns NOZZLE (overrides all)",
          "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    // Only end sensor, no others - still returns NOZZLE as it's furthest
    helper.set_tool_end_sensor(true);

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::NOZZLE);
}

TEST_CASE("AFC segment: fallback scans all lanes for prep sensor", "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    // No current lane set, but lane3 has prep sensor triggered
    helper.set_lane_prep_sensor(2, true); // lane3 is index 2

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::PREP);
}

TEST_CASE("AFC segment: fallback scans all lanes for load sensor", "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    // No current lane set, but lane2 has load sensor triggered
    helper.set_lane_load_sensor(1, true); // lane2 is index 1

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::LANE);
}

TEST_CASE("AFC segment: fallback scans all lanes for loaded_to_hub", "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    // No current lane set, but lane4 has loaded_to_hub
    helper.set_lane_loaded_to_hub(3, true); // lane4 is index 3

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::HUB);
}

TEST_CASE("AFC segment: hub sensor takes priority over lane sensors", "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_current_lane("lane1");
    helper.set_lane_prep_sensor(0, true);
    helper.set_lane_load_sensor(0, true);
    helper.set_lane_loaded_to_hub(0, true);
    helper.set_hub_sensor(true);

    // Hub sensor should return OUTPUT even with all lane sensors triggered
    REQUIRE(helper.test_compute_filament_segment() == PathSegment::OUTPUT);
}

TEST_CASE("AFC segment: toolhead sensors take priority over hub", "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_hub_sensor(true);
    helper.set_tool_start_sensor(true);

    // tool_start_sensor should return TOOLHEAD even with hub sensor triggered
    REQUIRE(helper.test_compute_filament_segment() == PathSegment::TOOLHEAD);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("AFC segment: no lanes initialized returns NONE", "[ams][afc][segment][edge]") {
    AmsBackendAfcTestHelper helper;
    // Don't call initialize_test_lanes - lane_names_ is empty

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::NONE);
}

TEST_CASE("AFC segment: current lane not in map uses fallback scan", "[ams][afc][segment][edge]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    // Set a lane name that doesn't exist in the map
    helper.set_current_lane("nonexistent");
    helper.set_lane_prep_sensor(0, true);

    // Should fall back to scanning all lanes
    REQUIRE(helper.test_compute_filament_segment() == PathSegment::PREP);
}

TEST_CASE("AFC version_at_least: dev version string", "[ams][afc][version][edge]") {
    AmsBackendAfcTestHelper helper;
    // Some systems may have dev/beta suffixes, but our parser ignores them
    // "1.0.32-dev" will parse as 1.0.32 (istringstream stops at non-digit)
    helper.set_afc_version("1.0.32-dev");

    // This should still satisfy >= 1.0.32 since the numeric parts match
    REQUIRE(helper.test_version_at_least("1.0.32") == true);
}

TEST_CASE("AFC segment: multiple lanes with sensors uses first match in order",
          "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    // Multiple lanes have sensors triggered, but no current lane set
    // The algorithm iterates through lanes in order and returns on first sensor found
    helper.set_lane_prep_sensor(0, true);
    helper.set_lane_load_sensor(1, true);
    helper.set_lane_loaded_to_hub(2, true);

    // Fallback iterates by lane, checking loaded_to_hub > load > prep for each lane
    // Lane 0: loaded_to_hub=false, load=false, prep=true -> returns PREP
    // The algorithm returns the first sensor state found, not the furthest overall
    REQUIRE(helper.test_compute_filament_segment() == PathSegment::PREP);
}

TEST_CASE("AFC segment: fallback prioritizes hub over lane sensors per-lane",
          "[ams][afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    // Lane 2 has all sensors including loaded_to_hub, lane 0 only has prep
    // Since the algorithm checks loaded_to_hub first for each lane,
    // lane 0's loaded_to_hub=false means it continues to check load, then prep
    // But loaded_to_hub IS checked before load/prep for each individual lane
    helper.set_lane_loaded_to_hub(0, true);
    helper.set_lane_prep_sensor(1, true);

    // Lane 0 has loaded_to_hub=true, so it returns HUB
    REQUIRE(helper.test_compute_filament_segment() == PathSegment::HUB);
}

// ============================================================================
// set_discovered_lanes() - Lane Discovery from PrinterCapabilities Tests
// ============================================================================

TEST_CASE("AFC set_discovered_lanes: sets lane names correctly", "[ams][afc][discovery]") {
    AmsBackendAfcTestHelper helper;

    std::vector<std::string> lanes = {"lane1", "lane2", "lane3", "lane4"};
    std::vector<std::string> hubs = {"Turtle_1"};

    helper.set_discovered_lanes(lanes, hubs);

    // After setting lanes, they should be accessible
    REQUIRE(helper.get_lane_names().size() == 4);
    REQUIRE(helper.get_lane_names()[0] == "lane1");
    REQUIRE(helper.get_lane_names()[3] == "lane4");
}

TEST_CASE("AFC set_discovered_lanes: sets hub names correctly", "[ams][afc][discovery]") {
    AmsBackendAfcTestHelper helper;

    std::vector<std::string> lanes = {"lane1", "lane2"};
    std::vector<std::string> hubs = {"Turtle_1", "Turtle_2"};

    helper.set_discovered_lanes(lanes, hubs);

    REQUIRE(helper.get_hub_names().size() == 2);
    REQUIRE(helper.get_hub_names()[0] == "Turtle_1");
}

TEST_CASE("AFC set_discovered_lanes: empty lanes doesn't overwrite existing",
          "[ams][afc][discovery]") {
    AmsBackendAfcTestHelper helper;

    // First set some lanes
    std::vector<std::string> lanes = {"lane1", "lane2"};
    std::vector<std::string> hubs = {"Turtle_1"};
    helper.set_discovered_lanes(lanes, hubs);

    // Then call with empty lanes - should not overwrite
    std::vector<std::string> empty_lanes;
    std::vector<std::string> new_hubs = {"NewHub"};
    helper.set_discovered_lanes(empty_lanes, new_hubs);

    // Lanes should remain unchanged
    REQUIRE(helper.get_lane_names().size() == 2);
    // But hubs should be updated
    REQUIRE(helper.get_hub_names().size() == 1);
    REQUIRE(helper.get_hub_names()[0] == "NewHub");
}

TEST_CASE("AFC segment: works with discovered lanes", "[ams][afc][discovery][segment]") {
    AmsBackendAfcTestHelper helper;

    // Set lanes via discovery (like PrinterCapabilities would)
    std::vector<std::string> lanes = {"lane1", "lane2", "lane3", "lane4"};
    std::vector<std::string> hubs = {"Turtle_1"};
    helper.set_discovered_lanes(lanes, hubs);

    // Initialize the lanes (like start() would do)
    helper.initialize_lanes_from_discovery();

    // Now test that sensors work correctly
    helper.set_current_lane("lane2");
    helper.set_lane_prep_sensor(1, true);
    helper.set_lane_load_sensor(1, true);

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::LANE);
}

// ============================================================================
// set_slot_info() Persistence Tests - AFC >= 1.0.20
// ============================================================================
//
// These tests verify that set_slot_info() sends the appropriate G-code commands
// to persist filament properties when AFC version >= 1.0.20.
//
// Commands expected:
// - SET_COLOR LANE=<name> COLOR=<RRGGBB>
// - SET_MATERIAL LANE=<name> MATERIAL=<type>
// - SET_WEIGHT LANE=<name> WEIGHT=<grams>
// - SET_SPOOL_ID LANE=<name> SPOOL_ID=<id>
//
// NOTE: These tests are designed to FAIL initially. The set_slot_info() method
// currently only updates local state and does NOT send G-code commands.
// Implementation must be added to make these tests pass.
//
// Testing approach: Since MoonrakerAPI::execute_gcode() is not virtual,
// the test helper captures G-code via the protected execute_gcode() method
// that AmsBackendAfc exposes. The implementation must call execute_gcode()
// for these tests to pass.
// ============================================================================

TEST_CASE("AFC persistence: old version skips G-code commands", "[ams][afc][persistence]") {
    AmsBackendAfcTestHelper helper;

    helper.set_afc_version("1.0.19"); // Below 1.0.20 threshold
    helper.initialize_test_lanes_with_slots(4);

    SlotInfo info;
    info.color_rgb = 0xFF0000; // Red
    info.material = "PLA";
    info.remaining_weight_g = 850;
    info.spoolman_id = 42;

    helper.set_slot_info(0, info);

    // Old version should NOT send any persistence commands
    // This test PASSES currently since no G-code is sent at all
    REQUIRE(helper.captured_gcodes.empty());
}

TEST_CASE("AFC persistence: SET_COLOR command format", "[ams][afc][persistence]") {
    AmsBackendAfcTestHelper helper;

    helper.set_afc_version("1.0.20");
    helper.initialize_test_lanes_with_slots(4);

    SlotInfo info;
    info.color_rgb = 0xFF0000; // Red

    helper.set_slot_info(0, info);

    // Should send: SET_COLOR LANE=lane1 COLOR=FF0000
    // FAILS: set_slot_info doesn't call execute_gcode yet
    REQUIRE(helper.has_gcode("SET_COLOR LANE=lane1 COLOR=FF0000"));
}

TEST_CASE("AFC persistence: SET_COLOR uppercase hex no prefix", "[ams][afc][persistence]") {
    AmsBackendAfcTestHelper helper;

    helper.set_afc_version("1.0.20");
    helper.initialize_test_lanes_with_slots(4);

    SlotInfo info;
    info.color_rgb = 0x00FF00; // Green

    helper.set_slot_info(1, info);

    // Should send: SET_COLOR LANE=lane2 COLOR=00FF00 (uppercase, no #)
    // FAILS: set_slot_info doesn't call execute_gcode yet
    REQUIRE(helper.has_gcode("SET_COLOR LANE=lane2 COLOR=00FF00"));
}

TEST_CASE("AFC persistence: SET_MATERIAL command format", "[ams][afc][persistence]") {
    AmsBackendAfcTestHelper helper;

    helper.set_afc_version("1.0.20");
    helper.initialize_test_lanes_with_slots(4);

    SlotInfo info;
    info.material = "PLA";

    helper.set_slot_info(1, info);

    // Should send: SET_MATERIAL LANE=lane2 MATERIAL=PLA
    // FAILS: set_slot_info doesn't call execute_gcode yet
    REQUIRE(helper.has_gcode("SET_MATERIAL LANE=lane2 MATERIAL=PLA"));
}

TEST_CASE("AFC persistence: SET_WEIGHT command format", "[ams][afc][persistence]") {
    AmsBackendAfcTestHelper helper;

    helper.set_afc_version("1.0.20");
    helper.initialize_test_lanes_with_slots(4);

    SlotInfo info;
    info.remaining_weight_g = 850.5f; // Should be sent as integer

    helper.set_slot_info(0, info);

    // Should send: SET_WEIGHT LANE=lane1 WEIGHT=850 (no decimals)
    // FAILS: set_slot_info doesn't call execute_gcode yet
    REQUIRE(helper.has_gcode("SET_WEIGHT LANE=lane1 WEIGHT=850"));
}

TEST_CASE("AFC persistence: SET_SPOOL_ID command format", "[ams][afc][persistence]") {
    AmsBackendAfcTestHelper helper;

    helper.set_afc_version("1.0.20");
    helper.initialize_test_lanes_with_slots(4);

    SlotInfo info;
    info.spoolman_id = 42;

    helper.set_slot_info(0, info);

    // Should send: SET_SPOOL_ID LANE=lane1 SPOOL_ID=42
    // FAILS: set_slot_info doesn't call execute_gcode yet
    REQUIRE(helper.has_gcode("SET_SPOOL_ID LANE=lane1 SPOOL_ID=42"));
}

TEST_CASE("AFC persistence: SET_SPOOL_ID clear with empty string", "[ams][afc][persistence]") {
    AmsBackendAfcTestHelper helper;

    helper.set_afc_version("1.0.20");
    helper.initialize_test_lanes_with_slots(4);

    // Pre-set existing spoolman_id on slot
    SlotInfo* existing_slot = helper.get_mutable_slot(0);
    REQUIRE(existing_slot != nullptr);
    existing_slot->spoolman_id = 123;

    // Now clear it by setting spoolman_id = 0
    SlotInfo new_info;
    new_info.spoolman_id = 0;

    helper.set_slot_info(0, new_info);

    // Should send: SET_SPOOL_ID LANE=lane1 SPOOL_ID= (empty to clear)
    // FAILS: set_slot_info doesn't call execute_gcode yet
    REQUIRE(helper.has_gcode("SET_SPOOL_ID LANE=lane1 SPOOL_ID="));
}

TEST_CASE("AFC persistence: skips SET_COLOR for default grey", "[ams][afc][persistence]") {
    AmsBackendAfcTestHelper helper;

    helper.set_afc_version("1.0.20");
    helper.initialize_test_lanes_with_slots(4);

    SlotInfo info;
    info.color_rgb = 0x808080; // Default grey - should NOT send

    helper.set_slot_info(0, info);

    // Should NOT send SET_COLOR for grey default
    // PASSES: no G-code sent at all currently
    REQUIRE_FALSE(helper.has_gcode_starting_with("SET_COLOR"));
}

TEST_CASE("AFC persistence: skips SET_COLOR for zero", "[ams][afc][persistence]") {
    AmsBackendAfcTestHelper helper;

    helper.set_afc_version("1.0.20");
    helper.initialize_test_lanes_with_slots(4);

    SlotInfo info;
    info.color_rgb = 0; // Zero color - should NOT send

    helper.set_slot_info(0, info);

    // Should NOT send SET_COLOR for zero
    // PASSES: no G-code sent at all currently
    REQUIRE_FALSE(helper.has_gcode_starting_with("SET_COLOR"));
}

TEST_CASE("AFC persistence: skips SET_MATERIAL for empty string", "[ams][afc][persistence]") {
    AmsBackendAfcTestHelper helper;

    helper.set_afc_version("1.0.20");
    helper.initialize_test_lanes_with_slots(4);

    SlotInfo info;
    info.material = ""; // Empty - should NOT send

    helper.set_slot_info(0, info);

    // Should NOT send SET_MATERIAL for empty
    // PASSES: no G-code sent at all currently
    REQUIRE_FALSE(helper.has_gcode_starting_with("SET_MATERIAL"));
}

TEST_CASE("AFC persistence: skips SET_WEIGHT for zero or negative", "[ams][afc][persistence]") {
    AmsBackendAfcTestHelper helper;

    helper.set_afc_version("1.0.20");
    helper.initialize_test_lanes_with_slots(4);

    SECTION("zero weight") {
        SlotInfo info;
        info.remaining_weight_g = 0;
        helper.set_slot_info(0, info);
        // PASSES: no G-code sent at all currently
        REQUIRE_FALSE(helper.has_gcode_starting_with("SET_WEIGHT"));
    }

    SECTION("negative weight (unknown)") {
        helper.clear_captured_gcodes();
        SlotInfo info;
        info.remaining_weight_g = -1;
        helper.set_slot_info(0, info);
        // PASSES: no G-code sent at all currently
        REQUIRE_FALSE(helper.has_gcode_starting_with("SET_WEIGHT"));
    }
}

TEST_CASE("AFC persistence: skips SET_SPOOL_ID when both old and new are zero",
          "[ams][afc][persistence]") {
    AmsBackendAfcTestHelper helper;

    helper.set_afc_version("1.0.20");
    helper.initialize_test_lanes_with_slots(4);

    // Slot starts with spoolman_id = 0 (default)
    SlotInfo info;
    info.spoolman_id = 0;

    helper.set_slot_info(0, info);

    // Should NOT send SET_SPOOL_ID when both old and new are 0
    // PASSES: no G-code sent at all currently
    REQUIRE_FALSE(helper.has_gcode_starting_with("SET_SPOOL_ID"));
}

TEST_CASE("AFC persistence: sends multiple commands for full slot info",
          "[ams][afc][persistence]") {
    AmsBackendAfcTestHelper helper;

    helper.set_afc_version("1.0.20");
    helper.initialize_test_lanes_with_slots(4);

    SlotInfo info;
    info.color_rgb = 0x0000FF; // Blue
    info.material = "PETG";
    info.remaining_weight_g = 750;
    info.spoolman_id = 99;

    helper.set_slot_info(0, info);

    // Should send all four commands
    // FAILS: set_slot_info doesn't call execute_gcode yet
    REQUIRE(helper.has_gcode("SET_COLOR LANE=lane1 COLOR=0000FF"));
    REQUIRE(helper.has_gcode("SET_MATERIAL LANE=lane1 MATERIAL=PETG"));
    REQUIRE(helper.has_gcode("SET_WEIGHT LANE=lane1 WEIGHT=750"));
    REQUIRE(helper.has_gcode("SET_SPOOL_ID LANE=lane1 SPOOL_ID=99"));
}

// ============================================================================
// reset_tool_mappings() Tests
// ============================================================================

TEST_CASE("AFC reset_tool_mappings sends RESET_AFC_MAPPING RUNOUT=no",
          "[ams][afc][tool_mapping][reset]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    auto result = helper.reset_tool_mappings();

    REQUIRE(result.success());
    REQUIRE(helper.has_gcode("RESET_AFC_MAPPING RUNOUT=no"));
}

TEST_CASE("AFC reset_tool_mappings sends single command regardless of lane count",
          "[ams][afc][tool_mapping][reset]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(8);

    auto result = helper.reset_tool_mappings();

    REQUIRE(result.success());
    // Should send exactly one command, not one per lane
    REQUIRE(helper.captured_gcodes.size() == 1);
    REQUIRE(helper.has_gcode("RESET_AFC_MAPPING RUNOUT=no"));
}

// ============================================================================
// reset_endless_spool() Tests
// ============================================================================

TEST_CASE("AFC reset_endless_spool clears all slots", "[ams][afc][endless_spool][reset]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);
    helper.initialize_endless_spool_configs(4);

    // Set some backups first
    helper.set_endless_spool_config(0, 1);
    helper.set_endless_spool_config(2, 3);

    auto result = helper.reset_endless_spool();

    REQUIRE(result.success());
    // Should have sent 4 SET_RUNOUT commands (one per slot)
    REQUIRE(helper.captured_gcodes.size() == 4);

    // Each should be setting RUNOUT_LANE= (empty) to disable
    REQUIRE(helper.has_gcode("SET_RUNOUT LANE=lane1 RUNOUT_LANE="));
    REQUIRE(helper.has_gcode("SET_RUNOUT LANE=lane2 RUNOUT_LANE="));
    REQUIRE(helper.has_gcode("SET_RUNOUT LANE=lane3 RUNOUT_LANE="));
    REQUIRE(helper.has_gcode("SET_RUNOUT LANE=lane4 RUNOUT_LANE="));
}

TEST_CASE("AFC reset_endless_spool with zero slots is no-op", "[ams][afc][endless_spool][reset]") {
    AmsBackendAfcTestHelper helper;
    // Don't initialize any lanes or configs

    auto result = helper.reset_endless_spool();

    REQUIRE(result.success());
    REQUIRE(helper.captured_gcodes.empty());
}

TEST_CASE("AFC reset_endless_spool continues on partial failure",
          "[ams][afc][endless_spool][reset]") {
    // This test verifies that if one slot fails, we still attempt the remaining slots
    // The implementation should return the first error but continue processing
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);
    helper.initialize_endless_spool_configs(4);

    auto result = helper.reset_endless_spool();

    // Should still have attempted all 4 slots even if one hypothetically failed
    REQUIRE(helper.captured_gcodes.size() == 4);
}
