// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#include "ams_backend_afc.h"
#include "ams_types.h"

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

    void set_current_gate(int gate) {
        system_info_.current_gate = gate;
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
};

// ============================================================================
// version_at_least() - Semantic Version Comparison Tests
// ============================================================================

TEST_CASE("AFC version_at_least: equal versions", "[afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("1.0.32");

    REQUIRE(helper.test_version_at_least("1.0.32") == true);
}

TEST_CASE("AFC version_at_least: greater patch version", "[afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("1.0.33");

    REQUIRE(helper.test_version_at_least("1.0.32") == true);
}

TEST_CASE("AFC version_at_least: greater minor version", "[afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("1.1.0");

    REQUIRE(helper.test_version_at_least("1.0.32") == true);
}

TEST_CASE("AFC version_at_least: greater major version", "[afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("2.0.0");

    REQUIRE(helper.test_version_at_least("1.0.32") == true);
}

TEST_CASE("AFC version_at_least: lesser patch version fails", "[afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("1.0.31");

    REQUIRE(helper.test_version_at_least("1.0.32") == false);
}

TEST_CASE("AFC version_at_least: unknown version fails", "[afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("unknown");

    REQUIRE(helper.test_version_at_least("1.0.32") == false);
}

TEST_CASE("AFC version_at_least: empty version fails", "[afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("");

    REQUIRE(helper.test_version_at_least("1.0.32") == false);
}

TEST_CASE("AFC version_at_least: lesser minor version fails", "[afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("1.0.0");

    REQUIRE(helper.test_version_at_least("1.1.0") == false);
}

TEST_CASE("AFC version_at_least: lesser major version fails", "[afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("1.99.99");

    REQUIRE(helper.test_version_at_least("2.0.0") == false);
}

TEST_CASE("AFC version_at_least: high patch vs low minor", "[afc][version]") {
    AmsBackendAfcTestHelper helper;
    helper.set_afc_version("1.0.100");

    // 1.0.100 is still < 1.1.0 because minor takes precedence
    REQUIRE(helper.test_version_at_least("1.1.0") == false);
}

TEST_CASE("AFC version_at_least: handles two-part version", "[afc][version]") {
    AmsBackendAfcTestHelper helper;
    // Version parsing uses istringstream which may handle partial versions
    helper.set_afc_version("1.0");

    // Should treat missing patch as 0, so 1.0.0 >= 1.0.0
    REQUIRE(helper.test_version_at_least("1.0.0") == true);
}

// ============================================================================
// compute_filament_segment_unlocked() - Sensor-to-Segment Mapping Tests
// ============================================================================

TEST_CASE("AFC segment: no sensors triggered returns NONE", "[afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);

    // No sensors triggered, no filament loaded, no current gate
    REQUIRE(helper.test_compute_filament_segment() == PathSegment::NONE);
}

TEST_CASE("AFC segment: filament loaded flag returns SPOOL when no sensors", "[afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_filament_loaded(true);

    // Filament is "loaded" but no sensors triggered - implies at spool
    REQUIRE(helper.test_compute_filament_segment() == PathSegment::SPOOL);
}

TEST_CASE("AFC segment: current gate set returns SPOOL when no sensors", "[afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_current_gate(0);

    // A gate is selected but no sensors - filament at spool area
    REQUIRE(helper.test_compute_filament_segment() == PathSegment::SPOOL);
}

TEST_CASE("AFC segment: prep sensor triggered returns PREP", "[afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_current_lane("lane1");
    helper.set_lane_prep_sensor(0, true);

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::PREP);
}

TEST_CASE("AFC segment: prep and load sensors return LANE", "[afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_current_lane("lane1");
    helper.set_lane_prep_sensor(0, true);
    helper.set_lane_load_sensor(0, true);

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::LANE);
}

TEST_CASE("AFC segment: loaded_to_hub returns HUB", "[afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_current_lane("lane1");
    helper.set_lane_prep_sensor(0, true);
    helper.set_lane_load_sensor(0, true);
    helper.set_lane_loaded_to_hub(0, true);

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::HUB);
}

TEST_CASE("AFC segment: hub_sensor returns OUTPUT", "[afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_current_lane("lane1");
    helper.set_lane_loaded_to_hub(0, true);
    helper.set_hub_sensor(true);

    // Hub sensor indicates filament past the hub merger, heading to toolhead
    REQUIRE(helper.test_compute_filament_segment() == PathSegment::OUTPUT);
}

TEST_CASE("AFC segment: tool_start_sensor returns TOOLHEAD", "[afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_hub_sensor(true);
    helper.set_tool_start_sensor(true);

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::TOOLHEAD);
}

TEST_CASE("AFC segment: tool_end_sensor returns NOZZLE", "[afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    helper.set_hub_sensor(true);
    helper.set_tool_start_sensor(true);
    helper.set_tool_end_sensor(true);

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::NOZZLE);
}

TEST_CASE("AFC segment: tool_end_sensor alone returns NOZZLE (overrides all)", "[afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    // Only end sensor, no others - still returns NOZZLE as it's furthest
    helper.set_tool_end_sensor(true);

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::NOZZLE);
}

TEST_CASE("AFC segment: fallback scans all lanes for prep sensor", "[afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    // No current lane set, but lane3 has prep sensor triggered
    helper.set_lane_prep_sensor(2, true); // lane3 is index 2

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::PREP);
}

TEST_CASE("AFC segment: fallback scans all lanes for load sensor", "[afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    // No current lane set, but lane2 has load sensor triggered
    helper.set_lane_load_sensor(1, true); // lane2 is index 1

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::LANE);
}

TEST_CASE("AFC segment: fallback scans all lanes for loaded_to_hub", "[afc][segment]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    // No current lane set, but lane4 has loaded_to_hub
    helper.set_lane_loaded_to_hub(3, true); // lane4 is index 3

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::HUB);
}

TEST_CASE("AFC segment: hub sensor takes priority over lane sensors", "[afc][segment]") {
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

TEST_CASE("AFC segment: toolhead sensors take priority over hub", "[afc][segment]") {
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

TEST_CASE("AFC segment: no lanes initialized returns NONE", "[afc][segment][edge]") {
    AmsBackendAfcTestHelper helper;
    // Don't call initialize_test_lanes - lane_names_ is empty

    REQUIRE(helper.test_compute_filament_segment() == PathSegment::NONE);
}

TEST_CASE("AFC segment: current lane not in map uses fallback scan", "[afc][segment][edge]") {
    AmsBackendAfcTestHelper helper;
    helper.initialize_test_lanes(4);
    // Set a lane name that doesn't exist in the map
    helper.set_current_lane("nonexistent");
    helper.set_lane_prep_sensor(0, true);

    // Should fall back to scanning all lanes
    REQUIRE(helper.test_compute_filament_segment() == PathSegment::PREP);
}

TEST_CASE("AFC version_at_least: dev version string", "[afc][version][edge]") {
    AmsBackendAfcTestHelper helper;
    // Some systems may have dev/beta suffixes, but our parser ignores them
    // "1.0.32-dev" will parse as 1.0.32 (istringstream stops at non-digit)
    helper.set_afc_version("1.0.32-dev");

    // This should still satisfy >= 1.0.32 since the numeric parts match
    REQUIRE(helper.test_version_at_least("1.0.32") == true);
}

TEST_CASE("AFC segment: multiple lanes with sensors uses first match in order", "[afc][segment]") {
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

TEST_CASE("AFC segment: fallback prioritizes hub over lane sensors per-lane", "[afc][segment]") {
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

TEST_CASE("AFC set_discovered_lanes: sets lane names correctly", "[afc][discovery]") {
    AmsBackendAfcTestHelper helper;

    std::vector<std::string> lanes = {"lane1", "lane2", "lane3", "lane4"};
    std::vector<std::string> hubs = {"Turtle_1"};

    helper.set_discovered_lanes(lanes, hubs);

    // After setting lanes, they should be accessible
    REQUIRE(helper.get_lane_names().size() == 4);
    REQUIRE(helper.get_lane_names()[0] == "lane1");
    REQUIRE(helper.get_lane_names()[3] == "lane4");
}

TEST_CASE("AFC set_discovered_lanes: sets hub names correctly", "[afc][discovery]") {
    AmsBackendAfcTestHelper helper;

    std::vector<std::string> lanes = {"lane1", "lane2"};
    std::vector<std::string> hubs = {"Turtle_1", "Turtle_2"};

    helper.set_discovered_lanes(lanes, hubs);

    REQUIRE(helper.get_hub_names().size() == 2);
    REQUIRE(helper.get_hub_names()[0] == "Turtle_1");
}

TEST_CASE("AFC set_discovered_lanes: empty lanes doesn't overwrite existing", "[afc][discovery]") {
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

TEST_CASE("AFC segment: works with discovered lanes", "[afc][discovery][segment]") {
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
