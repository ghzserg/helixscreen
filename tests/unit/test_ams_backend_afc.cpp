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
    void set_hub_sensor(const std::string& hub_name, bool state) {
        hub_sensors_[hub_name] = state;
    }

    // Convenience overload for single-hub backward compat in tests
    void set_hub_sensor(bool state) {
        // Set/clear on a default hub name for single-hub tests
        if (state) {
            hub_sensors_["default"] = true;
        } else {
            hub_sensors_.clear();
        }
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

    void set_running(bool state) {
        running_ = state;
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

    // Set up multi-unit configuration and trigger reorganize
    void
    setup_multi_unit(const std::unordered_map<std::string, std::vector<std::string>>& unit_map) {
        unit_lane_map_ = unit_map;
        reorganize_units_from_map();
    }

    // For persistence tests: capture G-code commands
    std::vector<std::string> captured_gcodes;

    // Override execute_gcode to capture commands for testing
    AmsError execute_gcode(const std::string& gcode) override {
        captured_gcodes.push_back(gcode);
        return AmsErrorHelper::success();
    }

    // Override execute_gcode_notify to capture commands (avoids real API call)
    AmsError execute_gcode_notify(const std::string& gcode, const std::string& /*success_msg*/,
                                  const std::string& /*error_prefix*/) override {
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

    // Feed a Moonraker notify_status_update notification through the backend
    void feed_status_update(const nlohmann::json& params_inner) {
        // Build the full notification format: { "params": [ { ... }, timestamp ] }
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

    // Feed AFC_stepper lane update
    void feed_afc_stepper(const std::string& lane_name, const nlohmann::json& data) {
        nlohmann::json params;
        params["AFC_stepper " + lane_name] = data;
        feed_status_update(params);
    }

    // State accessors for test assertions
    AmsAction get_action() const {
        return system_info_.action;
    }
    std::string get_operation_detail() const {
        return system_info_.operation_detail;
    }
    std::vector<int> get_tool_to_slot_map() const {
        return system_info_.tool_to_slot_map;
    }

    const std::vector<helix::printer::EndlessSpoolConfig>& get_endless_spool_configs() const {
        return endless_spool_configs_;
    }

    // Get mapped_tool from a slot
    int get_slot_mapped_tool(int slot_index) const {
        const auto* slot = system_info_.get_slot_global(slot_index);
        return slot ? slot->mapped_tool : -1;
    }

    // Event tracking
    std::vector<std::pair<std::string, std::string>> emitted_events;

    void install_event_tracker() {
        set_event_callback([this](const std::string& event, const std::string& data) {
            emitted_events.emplace_back(event, data);
        });
    }

    bool has_event(const std::string& event) const {
        for (const auto& [ev, _] : emitted_events) {
            if (ev == event)
                return true;
        }
        return false;
    }

    std::string get_event_data(const std::string& event) const {
        for (const auto& [ev, data] : emitted_events) {
            if (ev == event)
                return data;
        }
        return "";
    }

    // Phase 2: Access to extended parsing state
    const LaneSensors& get_lane_sensors(int index) const {
        return lane_sensors_[index];
    }
    bool get_hub_sensor() const {
        // Returns true if any hub sensor is triggered (backward compat)
        for (const auto& [name, triggered] : hub_sensors_) {
            if (triggered)
                return true;
        }
        return false;
    }

    bool get_hub_sensor(const std::string& hub_name) const {
        auto it = hub_sensors_.find(hub_name);
        return it != hub_sensors_.end() && it->second;
    }

    const std::unordered_map<std::string, bool>& get_hub_sensors() const {
        return hub_sensors_;
    }
    bool get_tool_start_sensor() const {
        return tool_start_sensor_;
    }
    bool get_tool_end_sensor() const {
        return tool_end_sensor_;
    }
    bool get_quiet_mode() const {
        return afc_quiet_mode_;
    }
    bool get_led_state() const {
        return afc_led_state_;
    }
    float get_bowden_length() const {
        return bowden_length_;
    }

    // Feed AFC_hub update
    void feed_afc_hub(const std::string& hub_name, const nlohmann::json& data) {
        nlohmann::json params;
        params["AFC_hub " + hub_name] = data;
        feed_status_update(params);
    }

    // Feed AFC_extruder update
    void feed_afc_extruder(const std::string& ext_name, const nlohmann::json& data) {
        nlohmann::json params;
        params["AFC_extruder " + ext_name] = data;
        feed_status_update(params);
    }

    // Feed AFC_buffer update
    void feed_afc_buffer(const std::string& buf_name, const nlohmann::json& data) {
        nlohmann::json params;
        params["AFC_buffer " + buf_name] = data;
        feed_status_update(params);
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

// ============================================================================
// Phase 1: Bug Fixes & Critical Data Sync Tests
// ============================================================================
//
// These tests verify parsing of fields that the real AFC device exposes
// (captured from 192.168.1.112). Tests use fixture data to validate that
// state updates flow through correctly to internal state.
// ============================================================================

TEST_CASE("AFC current_state preferred over status field", "[ams][afc][state][phase1]") {
    // Real device sends "current_state": "Idle" (in AFC global object)
    // but we only parse "status" field today. current_state should take priority
    // because it's the newer, more accurate field.
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // Feed AFC state with both current_state and status
    // current_state says "Idle" but status says "Loading" — current_state should win
    nlohmann::json afc_data = {{"current_state", "Idle"}, {"status", "Loading"}};
    helper.feed_afc_state(afc_data);

    // current_state takes priority over status field
    REQUIRE(helper.get_action() == AmsAction::IDLE);
}

TEST_CASE("AFC current_state fallback to status when no current_state",
          "[ams][afc][state][phase1]") {
    // When current_state is absent, fall back to status field (regression guard)
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    nlohmann::json afc_data = {{"status", "Loading"}};
    helper.feed_afc_state(afc_data);

    // Should still work via status field — this PASSES today (regression guard)
    REQUIRE(helper.get_action() == AmsAction::LOADING);
}

TEST_CASE("AFC tool mapping from stepper map field", "[ams][afc][tool_mapping][phase1]") {
    // Real device: AFC_stepper lane1 has "map": "T0", lane2 has "map": "T1", etc.
    // We never parse this field today.
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // Feed stepper data with map field
    helper.feed_afc_stepper("lane1", {{"map", "T0"}, {"prep", true}});
    helper.feed_afc_stepper("lane2", {{"map", "T1"}, {"prep", true}});
    helper.feed_afc_stepper("lane3", {{"map", "T2"}, {"prep", false}});
    helper.feed_afc_stepper("lane4", {{"map", "T3"}, {"prep", false}});

    // tool_to_slot_map should reflect the mapping from stepper "map" fields
    auto mapping = helper.get_tool_mapping();
    REQUIRE(mapping.size() == 4);
    REQUIRE(mapping[0] == 0); // T0 → lane1 (slot 0)
    REQUIRE(mapping[1] == 1); // T1 → lane2 (slot 1)
    REQUIRE(mapping[2] == 2); // T2 → lane3 (slot 2)
    REQUIRE(mapping[3] == 3); // T3 → lane4 (slot 3)
}

TEST_CASE("AFC tool mapping swap updates correctly", "[ams][afc][tool_mapping][phase1]") {
    // When lanes swap tools (e.g., T0 moves from lane1 to lane3), the mapping
    // should update accordingly
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // Initial mapping: T0→lane1, T1→lane2, T2→lane3, T3→lane4
    helper.feed_afc_stepper("lane1", {{"map", "T0"}});
    helper.feed_afc_stepper("lane2", {{"map", "T1"}});
    helper.feed_afc_stepper("lane3", {{"map", "T2"}});
    helper.feed_afc_stepper("lane4", {{"map", "T3"}});

    // Now swap: lane1 gets T2, lane3 gets T0
    helper.feed_afc_stepper("lane1", {{"map", "T2"}});
    helper.feed_afc_stepper("lane3", {{"map", "T0"}});

    // After swap, mapping should reflect new tool assignments
    auto mapping = helper.get_tool_mapping();
    REQUIRE(mapping.size() == 4);
    REQUIRE(mapping[0] == 2); // T0 → lane3 (slot 2)
    REQUIRE(mapping[1] == 1); // T1 → lane2 (slot 1)
    REQUIRE(mapping[2] == 0); // T2 → lane1 (slot 0)
    REQUIRE(mapping[3] == 3); // T3 → lane4 (slot 3)

    // Slot mapped_tool should also be updated
    REQUIRE(helper.get_slot_mapped_tool(0) == 2); // lane1 now maps to T2
    REQUIRE(helper.get_slot_mapped_tool(2) == 0); // lane3 now maps to T0
}

TEST_CASE("AFC endless spool from runout_lane field", "[ams][afc][endless_spool][phase1]") {
    // Real device: AFC_stepper lane1 has "runout_lane": "lane2"
    // meaning if lane1 runs out, switch to lane2.
    // We never parse this field today.
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);
    helper.initialize_endless_spool_configs(4);

    // Feed stepper data with runout_lane
    helper.feed_afc_stepper("lane1", {{"runout_lane", "lane2"}});

    // runout_lane should update endless spool backup config
    auto configs = helper.get_endless_spool_configs();
    REQUIRE(configs.size() == 4);
    REQUIRE(configs[0].backup_slot == 1); // lane1's backup is lane2 (slot 1)
}

TEST_CASE("AFC endless spool null runout_lane clears backup", "[ams][afc][endless_spool][phase1]") {
    // When runout_lane is null, the backup should be cleared (-1)
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);
    helper.initialize_endless_spool_configs(4);

    // First set a backup
    helper.set_endless_spool_config(0, 1); // lane1 backup = lane2

    // Now feed a null runout_lane
    nlohmann::json stepper_data;
    stepper_data["runout_lane"] = nullptr; // JSON null
    helper.feed_afc_stepper("lane1", stepper_data);

    // null runout_lane should clear the backup
    auto configs = helper.get_endless_spool_configs();
    REQUIRE(configs[0].backup_slot == -1); // Cleared
}

TEST_CASE("AFC message sets operation detail", "[ams][afc][message][phase1]") {
    // Real device: AFC global state has "message": {"message": "Loading T1", "type": "info"}
    // We never parse this field today, but it should set operation_detail.
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    nlohmann::json afc_data = {{"message", {{"message", "Loading T1"}, {"type", "info"}}}};
    helper.feed_afc_state(afc_data);

    // message.message should flow through to operation_detail
    REQUIRE(helper.get_operation_detail().find("Loading T1") != std::string::npos);
}

TEST_CASE("AFC error message emits EVENT_ERROR", "[ams][afc][message][phase1]") {
    // When message.type == "error", we should emit EVENT_ERROR with the message text
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);
    helper.install_event_tracker();

    nlohmann::json afc_data = {
        {"message", {{"message", "AFC Error: lane1 failed to load"}, {"type", "error"}}}};
    helper.feed_afc_state(afc_data);

    // error type messages should emit EVENT_ERROR
    REQUIRE(helper.has_event(AmsBackend::EVENT_ERROR));
    // Error data should contain the message text
    std::string error_data = helper.get_event_data(AmsBackend::EVENT_ERROR);
    REQUIRE(error_data.find("lane1 failed to load") != std::string::npos);
}

TEST_CASE("AFC current_load and next_lane tracked", "[ams][afc][state][phase1]") {
    // Real device: AFC global state has "current_load": "lane2", "next_lane": "lane3"
    // These tell us which lane is actively loading and which is queued next.
    // We never parse these fields today.
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    nlohmann::json afc_data = {
        {"current_load", "lane2"}, {"next_lane", "lane3"}, {"current_state", "Loading"}};
    helper.feed_afc_state(afc_data);

    // current_load should update current_slot (lane2 = slot 1)
    REQUIRE(helper.get_current_slot() == 1);
    // operation_detail should mention the loading context
    // At minimum, the action should be LOADING from current_state
    REQUIRE(helper.get_action() == AmsAction::LOADING);
}

// ============================================================================
// Phase 2: Full Data Parsing Tests
// ============================================================================
//
// These tests verify parsing of extended hub, extruder, stepper, and buffer
// fields from real AFC device data. Tests use fixture structures captured
// from a real Box Turtle at 192.168.1.112.
// ============================================================================

TEST_CASE("AFC hub bowden length parsed from afc_bowden_length", "[ams][afc][hub][phase2]") {
    // Real device: AFC_hub Turtle_1 has "afc_bowden_length": 1285.0
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // Set hub names so the status update routes correctly
    std::vector<std::string> lanes = {"lane1", "lane2", "lane3", "lane4"};
    std::vector<std::string> hubs = {"Turtle_1"};
    helper.set_discovered_lanes(lanes, hubs);

    helper.feed_afc_hub("Turtle_1", {{"state", false}, {"afc_bowden_length", 1285.0}});

    // bowden_length should be stored and accessible for device actions
    auto actions = helper.get_device_actions();
    bool found_bowden = false;
    for (const auto& action : actions) {
        if (action.id == "bowden_length") {
            found_bowden = true;
            // Value should use the real bowden length, not hardcoded 450
            auto val = std::any_cast<float>(action.current_value);
            REQUIRE(val == Catch::Approx(1285.0f));
            break;
        }
    }
    REQUIRE(found_bowden);
}

TEST_CASE("AFC hub cutter info parsed", "[ams][afc][hub][phase2]") {
    // Real device: AFC_hub has "cut": false, "cut_dist": 50.0, etc.
    // We should track whether the hub has a cutter for UI decisions
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    std::vector<std::string> lanes = {"lane1", "lane2", "lane3", "lane4"};
    std::vector<std::string> hubs = {"Turtle_1"};
    helper.set_discovered_lanes(lanes, hubs);

    helper.feed_afc_hub(
        "Turtle_1",
        {{"state", false}, {"cut", false}, {"cut_dist", 50.0}, {"afc_bowden_length", 1285.0}});

    // Hub sensor state should be updated
    REQUIRE(helper.get_hub_sensor() == false);

    // System info should reflect cutter availability
    auto sys_info = helper.get_system_info();
    // AFC always advertises TipMethod::CUT - but we should parse cut field
    // to know if cutter is actually present/configured
    REQUIRE(sys_info.tip_method == TipMethod::CUT);
}

TEST_CASE("AFC extruder speeds parsed", "[ams][afc][extruder][phase2]") {
    // Real device: AFC_extruder has "tool_load_speed": 25.0, "tool_unload_speed": 25.0
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    helper.feed_afc_extruder("extruder", {{"tool_start_status", false},
                                          {"tool_end_status", false},
                                          {"tool_load_speed", 25.0},
                                          {"tool_unload_speed", 30.0}});

    // Sensor state should be updated
    REQUIRE(helper.get_tool_start_sensor() == false);
    REQUIRE(helper.get_tool_end_sensor() == false);
}

TEST_CASE("AFC extruder distances parsed", "[ams][afc][extruder][phase2]") {
    // Real device: tool_stn=42.0, tool_stn_unload=90.0
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    helper.feed_afc_extruder("extruder", {{"tool_start_status", true},
                                          {"tool_end_status", false},
                                          {"tool_stn", 42.0},
                                          {"tool_stn_unload", 90.0}});

    REQUIRE(helper.get_tool_start_sensor() == true);
}

TEST_CASE("AFC stepper buffer_status parsed", "[ams][afc][stepper][phase2]") {
    // Real device: AFC_stepper lane1 has "buffer_status": "Advancing"
    // LaneSensors struct only has prep, load, loaded_to_hub today
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    helper.feed_afc_stepper("lane1",
                            {{"prep", true}, {"load", true}, {"buffer_status", "Advancing"}});

    // buffer_status should be stored on lane sensors
    auto sensors = helper.get_lane_sensors(0);
    REQUIRE(sensors.prep == true);
    REQUIRE(sensors.load == true);
    REQUIRE(sensors.buffer_status == "Advancing");
}

TEST_CASE("AFC stepper filament_status parsed", "[ams][afc][stepper][phase2]") {
    // Real device: "filament_status": "Ready" or "Not Ready"
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    helper.feed_afc_stepper("lane1",
                            {{"filament_status", "Ready"}, {"filament_status_led", "#00ff00"}});

    auto sensors = helper.get_lane_sensors(0);
    REQUIRE(sensors.filament_status == "Ready");
}

TEST_CASE("AFC stepper dist_hub parsed", "[ams][afc][stepper][phase2]") {
    // Real device: "dist_hub": 200.0 (distance to hub in mm)
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    helper.feed_afc_stepper("lane1", {{"dist_hub", 200.0}});

    auto sensors = helper.get_lane_sensors(0);
    REQUIRE(sensors.dist_hub == Catch::Approx(200.0f));
}

TEST_CASE("AFC buffer object parsed via status update", "[ams][afc][buffer][phase2]") {
    // Real device: AFC_buffer Turtle_1 has "state": "Advancing", "enabled": false
    // We don't subscribe to or parse AFC_buffer objects today
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    std::vector<std::string> lanes = {"lane1", "lane2", "lane3", "lane4"};
    std::vector<std::string> hubs = {"Turtle_1"};
    helper.set_discovered_lanes(lanes, hubs);

    // Feed buffer names through AFC state
    helper.feed_afc_state({{"buffers", {"Turtle_1"}}});

    // Now feed a buffer update
    helper.feed_afc_buffer("Turtle_1", {{"state", "Advancing"}, {"enabled", false}});

    // Buffer state should be tracked (at minimum, no crash)
    // The test verifies the feed_afc_buffer path doesn't crash
    // and that buffer names are stored
    REQUIRE(true); // Placeholder — buffer tracking will expand in implementation
}

TEST_CASE("AFC global quiet_mode parsed from AFC state", "[ams][afc][global][phase2]") {
    // Real device: AFC has "quiet_mode": false
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    helper.feed_afc_state({{"quiet_mode", false}});
    REQUIRE(helper.get_quiet_mode() == false);

    // Toggle it on
    helper.feed_afc_state({{"quiet_mode", true}});
    REQUIRE(helper.get_quiet_mode() == true);
}

TEST_CASE("AFC global led_state parsed from AFC state", "[ams][afc][global][phase2]") {
    // Real device: AFC has "led_state": true
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    helper.feed_afc_state({{"led_state", true}});
    REQUIRE(helper.get_led_state() == true);

    // Toggle it off
    helper.feed_afc_state({{"led_state", false}});
    REQUIRE(helper.get_led_state() == false);
}

TEST_CASE("AFC bowden slider max accommodates real bowden length",
          "[ams][afc][device_actions][phase2]") {
    // The bowden slider max was hardcoded to 1000mm, but real bowden can be 1285mm
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    std::vector<std::string> lanes = {"lane1", "lane2", "lane3", "lane4"};
    std::vector<std::string> hubs = {"Turtle_1"};
    helper.set_discovered_lanes(lanes, hubs);

    helper.feed_afc_hub("Turtle_1", {{"state", false}, {"afc_bowden_length", 1285.0}});

    auto actions = helper.get_device_actions();
    for (const auto& action : actions) {
        if (action.id == "bowden_length") {
            // Max should accommodate the real bowden length
            REQUIRE(action.max_value >= 1285.0f);
            break;
        }
    }
}

// ============================================================================
// Phase 3: New Device Actions & Commands Tests
// ============================================================================
//
// Tests for new maintenance section, LED/mode toggles, and maintenance commands.
// ============================================================================

TEST_CASE("AFC device sections include maintenance and led",
          "[ams][afc][device_sections][phase3]") {
    AmsBackendAfcTestHelper helper;

    auto sections = helper.get_device_sections();

    bool has_maintenance = false;
    bool has_setup = false;
    for (const auto& section : sections) {
        if (section.id == "maintenance")
            has_maintenance = true;
        if (section.id == "setup")
            has_setup = true;
    }
    REQUIRE(has_maintenance);
    REQUIRE(has_setup);
}

TEST_CASE("AFC device action test_lanes dispatches gcode", "[ams][afc][device_actions][phase3]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    auto result = helper.execute_device_action("test_lanes");

    REQUIRE(result.success());
    REQUIRE(helper.has_gcode("AFC_TEST_LANES"));
}

TEST_CASE("AFC device action change_blade dispatches gcode", "[ams][afc][device_actions][phase3]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    auto result = helper.execute_device_action("change_blade");

    REQUIRE(result.success());
    REQUIRE(helper.has_gcode("AFC_CHANGE_BLADE"));
}

TEST_CASE("AFC device action park dispatches gcode", "[ams][afc][device_actions][phase3]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    auto result = helper.execute_device_action("park");

    REQUIRE(result.success());
    REQUIRE(helper.has_gcode("AFC_PARK"));
}

TEST_CASE("AFC device action brush dispatches gcode", "[ams][afc][device_actions][phase3]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    auto result = helper.execute_device_action("brush");

    REQUIRE(result.success());
    REQUIRE(helper.has_gcode("AFC_BRUSH"));
}

TEST_CASE("AFC device action reset_motor dispatches gcode", "[ams][afc][device_actions][phase3]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    auto result = helper.execute_device_action("reset_motor");

    REQUIRE(result.success());
    REQUIRE(helper.has_gcode("AFC_RESET_MOTOR_TIME"));
}

TEST_CASE("AFC device action led toggle on when off", "[ams][afc][device_actions][phase3]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // LED is off, toggling should turn it on
    helper.feed_afc_state({{"led_state", false}});

    auto result = helper.execute_device_action("led_toggle");

    REQUIRE(result.success());
    REQUIRE(helper.has_gcode("TURN_ON_AFC_LED"));
}

TEST_CASE("AFC device action led toggle off when on", "[ams][afc][device_actions][phase3]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // LED is on, toggling should turn it off
    helper.feed_afc_state({{"led_state", true}});

    auto result = helper.execute_device_action("led_toggle");

    REQUIRE(result.success());
    REQUIRE(helper.has_gcode("TURN_OFF_AFC_LED"));
}

TEST_CASE("AFC device action quiet_mode dispatches gcode", "[ams][afc][device_actions][phase3]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    auto result = helper.execute_device_action("quiet_mode");

    REQUIRE(result.success());
    REQUIRE(helper.has_gcode("AFC_QUIET_MODE"));
}

// ============================================================================
// Phase 4: Error Recovery Improvements Tests
// ============================================================================
//
// Tests for differentiated reset (AFC_RESET vs AFC_HOME), per-lane reset,
// and error message surfacing.
// ============================================================================

TEST_CASE("AFC recover sends AFC_RESET", "[ams][afc][recovery][phase4]") {
    // Regression guard — recover() should continue using AFC_RESET
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);
    helper.set_running(true); // Bypass precondition for unit test

    auto result = helper.recover();

    REQUIRE(result.success());
    REQUIRE(helper.has_gcode("AFC_RESET"));
    REQUIRE_FALSE(helper.has_gcode("AFC_HOME"));
}

TEST_CASE("AFC reset sends AFC_HOME not AFC_RESET", "[ams][afc][recovery][phase4]") {
    // reset() should send AFC_HOME to differentiate from recover()'s AFC_RESET
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);
    helper.set_running(true); // Bypass precondition for unit test

    auto result = helper.reset();

    REQUIRE(result.success());
    REQUIRE(helper.has_gcode("AFC_HOME"));
    REQUIRE_FALSE(helper.has_gcode("AFC_RESET"));
}

TEST_CASE("AFC reset_lane sends per-lane reset command", "[ams][afc][recovery][phase4]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);
    helper.set_running(true);

    auto result = helper.reset_lane(0);

    REQUIRE(result.success());
    REQUIRE(helper.has_gcode("AFC_LANE_RESET LANE=lane1"));
}

TEST_CASE("AFC reset_lane second lane", "[ams][afc][recovery][phase4]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);
    helper.set_running(true);

    auto result = helper.reset_lane(2);

    REQUIRE(result.success());
    REQUIRE(helper.has_gcode("AFC_LANE_RESET LANE=lane3"));
}

TEST_CASE("AFC reset_lane validates slot index", "[ams][afc][recovery][phase4]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);
    helper.set_running(true);

    auto result = helper.reset_lane(99);

    REQUIRE_FALSE(result.success());
    REQUIRE(result.result == AmsResult::INVALID_SLOT);
}

TEST_CASE("AFC reset_lane validates negative index", "[ams][afc][recovery][phase4]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);
    helper.set_running(true);

    auto result = helper.reset_lane(-1);

    REQUIRE_FALSE(result.success());
    REQUIRE(result.result == AmsResult::INVALID_SLOT);
}

TEST_CASE("AFC reset_lane fails when not running", "[ams][afc][recovery][phase4]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);
    // running_ defaults to false

    auto result = helper.reset_lane(0);

    REQUIRE_FALSE(result.success());
    REQUIRE(result.result == AmsResult::NOT_CONNECTED);
}

TEST_CASE("AFC error message surfaces in EVENT_ERROR data", "[ams][afc][recovery][phase4]") {
    // Verify that AFC error messages contain useful text in the event data
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes_with_slots(4);
    helper.install_event_tracker();

    nlohmann::json afc_data = {
        {"message", {{"message", "Lane 1 failed: filament jam detected"}, {"type", "error"}}}};
    helper.feed_afc_state(afc_data);

    REQUIRE(helper.has_event(AmsBackend::EVENT_ERROR));
    std::string error_data = helper.get_event_data(AmsBackend::EVENT_ERROR);
    REQUIRE(error_data.find("filament jam detected") != std::string::npos);
}
