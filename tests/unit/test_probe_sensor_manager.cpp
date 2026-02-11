// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_probe_sensor_manager.cpp
 * @brief Unit tests for ProbeSensorManager
 *
 * Tests cover:
 * - Type helpers (role/type string conversion)
 * - Sensor discovery from Klipper object names (probe, bltouch, smart_effector, probe_eddy_current)
 * - Role assignment (Z_PROBE)
 * - State updates from Moonraker status JSON
 * - Subject value correctness for UI binding
 * - Config persistence
 */

#include "../ui_test_utils.h"
#include "probe_sensor_manager.h"
#include "probe_sensor_types.h"

#include <spdlog/spdlog.h>

#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::sensors;
using json = nlohmann::json;

// ============================================================================
// Test Access
// ============================================================================

namespace helix::sensors {
class ProbeSensorManagerTestAccess {
  public:
    static void reset(ProbeSensorManager& obj) {
        std::lock_guard<std::recursive_mutex> lock(obj.mutex_);
        obj.sensors_.clear();
        obj.states_.clear();
        obj.sync_mode_ = true;
        // Reset subject values but keep subjects initialized
        if (obj.subjects_initialized_) {
            lv_subject_set_int(&obj.sensor_count_, 0);
            lv_subject_set_int(&obj.probe_triggered_, -1);
            lv_subject_set_int(&obj.probe_last_z_, -1);
            lv_subject_set_int(&obj.probe_z_offset_, -1);
        }
    }
};
} // namespace helix::sensors

// ============================================================================
// Test Fixture
// ============================================================================

class ProbeSensorTestFixture {
  public:
    ProbeSensorTestFixture() {
        // Initialize LVGL (safe version avoids "already initialized" warnings)
        lv_init_safe();

        // Create a headless display for testing
        if (!display_created_) {
            display_ = lv_display_create(480, 320);
            alignas(64) static lv_color_t buf[480 * 10];
            lv_display_set_buffers(display_, buf, nullptr, sizeof(buf),
                                   LV_DISPLAY_RENDER_MODE_PARTIAL);
            lv_display_set_flush_cb(display_,
                                    [](lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
                                        lv_display_flush_ready(disp);
                                    });
            display_created_ = true;
        }

        // Initialize subjects (idempotent)
        mgr().init_subjects();

        // Reset state for test isolation
        ProbeSensorManagerTestAccess::reset(mgr());
    }

    ~ProbeSensorTestFixture() {
        // Reset after each test
        ProbeSensorManagerTestAccess::reset(mgr());
    }

  protected:
    ProbeSensorManager& mgr() {
        return ProbeSensorManager::instance();
    }

    // Helper to discover standard test sensors
    void discover_test_sensors() {
        std::vector<std::string> sensors = {"probe", "bltouch"};
        mgr().discover(sensors);
    }

    // Helper to simulate Moonraker status update
    void update_sensor_state(const std::string& klipper_name, float last_z_result, float z_offset) {
        json status;
        status[klipper_name]["last_z_result"] = last_z_result;
        status[klipper_name]["z_offset"] = z_offset;
        mgr().update_from_status(status);
    }

  private:
    static lv_display_t* display_;
    static bool display_created_;
};

// Static members
lv_display_t* ProbeSensorTestFixture::display_ = nullptr;
bool ProbeSensorTestFixture::display_created_ = false;

// ============================================================================
// Type Helper Tests (probe_sensor_types.h)
// ============================================================================

TEST_CASE("ProbeSensorTypes - role string conversion", "[probe][types]") {
    SECTION("probe_role_to_string") {
        REQUIRE(probe_role_to_string(ProbeSensorRole::NONE) == "none");
        REQUIRE(probe_role_to_string(ProbeSensorRole::Z_PROBE) == "z_probe");
    }

    SECTION("probe_role_from_string") {
        REQUIRE(probe_role_from_string("none") == ProbeSensorRole::NONE);
        REQUIRE(probe_role_from_string("z_probe") == ProbeSensorRole::Z_PROBE);
        REQUIRE(probe_role_from_string("invalid") == ProbeSensorRole::NONE);
        REQUIRE(probe_role_from_string("") == ProbeSensorRole::NONE);
    }

    SECTION("probe_role_to_display_string") {
        REQUIRE(probe_role_to_display_string(ProbeSensorRole::NONE) == "Unassigned");
        REQUIRE(probe_role_to_display_string(ProbeSensorRole::Z_PROBE) == "Z Probe");
    }
}

TEST_CASE("ProbeSensorTypes - type string conversion", "[probe][types]") {
    SECTION("probe_type_to_string") {
        REQUIRE(probe_type_to_string(ProbeSensorType::STANDARD) == "standard");
        REQUIRE(probe_type_to_string(ProbeSensorType::BLTOUCH) == "bltouch");
        REQUIRE(probe_type_to_string(ProbeSensorType::SMART_EFFECTOR) == "smart_effector");
        REQUIRE(probe_type_to_string(ProbeSensorType::EDDY_CURRENT) == "eddy_current");
    }

    SECTION("probe_type_from_string") {
        REQUIRE(probe_type_from_string("standard") == ProbeSensorType::STANDARD);
        REQUIRE(probe_type_from_string("bltouch") == ProbeSensorType::BLTOUCH);
        REQUIRE(probe_type_from_string("smart_effector") == ProbeSensorType::SMART_EFFECTOR);
        REQUIRE(probe_type_from_string("eddy_current") == ProbeSensorType::EDDY_CURRENT);
        REQUIRE(probe_type_from_string("invalid") == ProbeSensorType::STANDARD);
        REQUIRE(probe_type_from_string("") == ProbeSensorType::STANDARD);
    }
}

// ============================================================================
// Sensor Discovery Tests
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - discovery", "[probe][discovery]") {
    SECTION("Discovers standard probe") {
        std::vector<std::string> sensors = {"probe"};
        mgr().discover(sensors);

        REQUIRE(mgr().has_sensors());
        REQUIRE(mgr().sensor_count() == 1);

        auto configs = mgr().get_sensors();
        REQUIRE(configs.size() == 1);
        REQUIRE(configs[0].klipper_name == "probe");
        REQUIRE(configs[0].sensor_name == "probe");
        REQUIRE(configs[0].type == ProbeSensorType::STANDARD);
        REQUIRE(configs[0].enabled == true);
        REQUIRE(configs[0].role == ProbeSensorRole::NONE);
    }

    SECTION("Discovers BLTouch probe") {
        std::vector<std::string> sensors = {"bltouch"};
        mgr().discover(sensors);

        REQUIRE(mgr().sensor_count() == 1);

        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "bltouch");
        REQUIRE(configs[0].sensor_name == "bltouch");
        REQUIRE(configs[0].type == ProbeSensorType::BLTOUCH);
    }

    SECTION("Discovers Smart Effector probe") {
        std::vector<std::string> sensors = {"smart_effector"};
        mgr().discover(sensors);

        REQUIRE(mgr().sensor_count() == 1);

        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "smart_effector");
        REQUIRE(configs[0].sensor_name == "smart_effector");
        REQUIRE(configs[0].type == ProbeSensorType::SMART_EFFECTOR);
    }

    SECTION("Discovers Eddy Current probe with name parameter") {
        std::vector<std::string> sensors = {"probe_eddy_current btt"};
        mgr().discover(sensors);

        REQUIRE(mgr().sensor_count() == 1);

        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "probe_eddy_current btt");
        REQUIRE(configs[0].sensor_name == "btt");
        REQUIRE(configs[0].type == ProbeSensorType::EDDY_CURRENT);
    }

    SECTION("Discovers multiple probe types") {
        std::vector<std::string> sensors = {"probe", "bltouch", "probe_eddy_current scanner"};
        mgr().discover(sensors);

        REQUIRE(mgr().sensor_count() == 3);

        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].type == ProbeSensorType::STANDARD);
        REQUIRE(configs[1].type == ProbeSensorType::BLTOUCH);
        REQUIRE(configs[2].type == ProbeSensorType::EDDY_CURRENT);
        REQUIRE(configs[2].sensor_name == "scanner");
    }

    SECTION("Ignores unrelated objects") {
        std::vector<std::string> sensors = {"probe", "filament_switch_sensor runout",
                                            "temperature_sensor chamber", "extruder"};
        mgr().discover(sensors);

        REQUIRE(mgr().sensor_count() == 1);
        REQUIRE(mgr().get_sensors()[0].klipper_name == "probe");
    }

    SECTION("Empty sensor list clears previous sensors") {
        discover_test_sensors();
        REQUIRE(mgr().sensor_count() == 2);

        mgr().discover({});
        REQUIRE(mgr().sensor_count() == 0);
        REQUIRE_FALSE(mgr().has_sensors());
    }

    SECTION("Re-discovery replaces sensor list") {
        std::vector<std::string> sensors1 = {"probe"};
        mgr().discover(sensors1);
        REQUIRE(mgr().get_sensors()[0].klipper_name == "probe");

        std::vector<std::string> sensors2 = {"bltouch"};
        mgr().discover(sensors2);
        REQUIRE(mgr().sensor_count() == 1);
        REQUIRE(mgr().get_sensors()[0].klipper_name == "bltouch");
    }

    SECTION("Sensor count subject is updated") {
        lv_subject_t* count_subject = mgr().get_sensor_count_subject();
        REQUIRE(lv_subject_get_int(count_subject) == 0);

        discover_test_sensors();
        REQUIRE(lv_subject_get_int(count_subject) == 2);

        mgr().discover({});
        REQUIRE(lv_subject_get_int(count_subject) == 0);
    }
}

// ============================================================================
// Role Assignment Tests
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - role assignment", "[probe][roles]") {
    discover_test_sensors();

    SECTION("Can set Z_PROBE role") {
        mgr().set_sensor_role("probe", ProbeSensorRole::Z_PROBE);

        auto configs = mgr().get_sensors();
        auto it = std::find_if(configs.begin(), configs.end(),
                               [](const auto& c) { return c.klipper_name == "probe"; });
        REQUIRE(it != configs.end());
        REQUIRE(it->role == ProbeSensorRole::Z_PROBE);
    }

    SECTION("Role assignment is unique - assigning same role clears previous") {
        mgr().set_sensor_role("probe", ProbeSensorRole::Z_PROBE);
        mgr().set_sensor_role("bltouch", ProbeSensorRole::Z_PROBE);

        auto configs = mgr().get_sensors();

        auto probe_it = std::find_if(configs.begin(), configs.end(),
                                     [](const auto& c) { return c.klipper_name == "probe"; });
        REQUIRE(probe_it->role == ProbeSensorRole::NONE);

        auto bltouch_it = std::find_if(configs.begin(), configs.end(),
                                       [](const auto& c) { return c.klipper_name == "bltouch"; });
        REQUIRE(bltouch_it->role == ProbeSensorRole::Z_PROBE);
    }

    SECTION("Can assign NONE without affecting other sensors") {
        mgr().set_sensor_role("probe", ProbeSensorRole::Z_PROBE);

        mgr().set_sensor_role("probe", ProbeSensorRole::NONE);

        auto configs = mgr().get_sensors();
        auto it = std::find_if(configs.begin(), configs.end(),
                               [](const auto& c) { return c.klipper_name == "probe"; });
        REQUIRE(it->role == ProbeSensorRole::NONE);
    }

    SECTION("Assigning role to unknown sensor does nothing") {
        mgr().set_sensor_role("nonexistent_sensor", ProbeSensorRole::Z_PROBE);

        for (const auto& config : mgr().get_sensors()) {
            REQUIRE(config.role == ProbeSensorRole::NONE);
        }
    }
}

// ============================================================================
// State Update Tests
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - state updates", "[probe][state]") {
    discover_test_sensors();
    mgr().set_sensor_role("probe", ProbeSensorRole::Z_PROBE);

    SECTION("Parses last_z_result and z_offset from status JSON") {
        auto state = mgr().get_sensor_state(ProbeSensorRole::Z_PROBE);
        REQUIRE(state.has_value());
        REQUIRE(state->last_z_result == 0.0f);
        REQUIRE(state->z_offset == 0.0f);

        json status;
        status["probe"]["last_z_result"] = 0.125f;
        status["probe"]["z_offset"] = -1.5f;
        mgr().update_from_status(status);

        state = mgr().get_sensor_state(ProbeSensorRole::Z_PROBE);
        REQUIRE(state->last_z_result == Catch::Approx(0.125f));
        REQUIRE(state->z_offset == Catch::Approx(-1.5f));
    }

    SECTION("Status update for unknown sensor is ignored") {
        json status;
        status["unknown_sensor"]["last_z_result"] = 0.125f;
        mgr().update_from_status(status);

        REQUIRE(mgr().sensor_count() == 2);
    }

    SECTION("Empty status update is handled") {
        json status = json::object();
        mgr().update_from_status(status);

        REQUIRE(mgr().has_sensors());
    }
}

// ============================================================================
// Subject Value Tests
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - subject values",
                 "[probe][subjects]") {
    discover_test_sensors();

    SECTION("Probe triggered subject shows -1 when no sensor assigned to role") {
        REQUIRE(lv_subject_get_int(mgr().get_probe_triggered_subject()) == -1);
    }

    SECTION("Last Z result subject shows -1 when no sensor assigned to role") {
        REQUIRE(lv_subject_get_int(mgr().get_probe_last_z_subject()) == -1);
    }

    SECTION("Z offset subject shows -1 when no sensor assigned") {
        REQUIRE(lv_subject_get_int(mgr().get_probe_z_offset_subject()) == -1);
    }

    SECTION("Last Z result subject updates correctly (value x 1000 = microns)") {
        mgr().set_sensor_role("probe", ProbeSensorRole::Z_PROBE);

        // After assignment, should show 0 since state defaults to 0.0
        REQUIRE(lv_subject_get_int(mgr().get_probe_last_z_subject()) == 0);

        // Update state with last_z_result = 0.125mm = 125 microns
        update_sensor_state("probe", 0.125f, -1.5f);
        REQUIRE(lv_subject_get_int(mgr().get_probe_last_z_subject()) == 125);

        // Update with different value
        update_sensor_state("probe", 0.250f, -1.5f);
        REQUIRE(lv_subject_get_int(mgr().get_probe_last_z_subject()) == 250);
    }

    SECTION("Z offset subject updates correctly (value x 1000 = microns)") {
        mgr().set_sensor_role("probe", ProbeSensorRole::Z_PROBE);

        // After assignment, should show 0 since state defaults to 0.0
        REQUIRE(lv_subject_get_int(mgr().get_probe_z_offset_subject()) == 0);

        // Update state with z_offset = -1.5mm = -1500 microns
        update_sensor_state("probe", 0.125f, -1.5f);
        REQUIRE(lv_subject_get_int(mgr().get_probe_z_offset_subject()) == -1500);

        // Update with different value
        update_sensor_state("probe", 0.125f, -2.25f);
        REQUIRE(lv_subject_get_int(mgr().get_probe_z_offset_subject()) == -2250);
    }

    SECTION("Subjects show -1 when sensor disabled") {
        mgr().set_sensor_role("probe", ProbeSensorRole::Z_PROBE);
        update_sensor_state("probe", 0.125f, -1.5f);

        mgr().set_sensor_enabled("probe", false);
        REQUIRE(lv_subject_get_int(mgr().get_probe_triggered_subject()) == -1);
        REQUIRE(lv_subject_get_int(mgr().get_probe_last_z_subject()) == -1);
        REQUIRE(lv_subject_get_int(mgr().get_probe_z_offset_subject()) == -1);
    }
}

// ============================================================================
// Config Persistence Tests
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - config persistence",
                 "[probe][config]") {
    discover_test_sensors();

    SECTION("save_config returns JSON with role assignments") {
        mgr().set_sensor_role("probe", ProbeSensorRole::Z_PROBE);

        json config = mgr().save_config();

        REQUIRE(config.is_object());
        REQUIRE(config.contains("sensors"));
        REQUIRE(config["sensors"].is_array());
        REQUIRE(config["sensors"].size() == 2);

        bool found_probe = false;
        for (const auto& sensor : config["sensors"]) {
            if (sensor["klipper_name"] == "probe") {
                REQUIRE(sensor["role"] == "z_probe");
                found_probe = true;
            }
        }
        REQUIRE(found_probe);
    }

    SECTION("load_config restores role assignments") {
        // Set up config JSON
        json config;
        json sensors_array = json::array();
        json sensor1;
        sensor1["klipper_name"] = "probe";
        sensor1["role"] = "z_probe";
        sensor1["enabled"] = true;
        sensors_array.push_back(sensor1);
        config["sensors"] = sensors_array;

        mgr().load_config(config);

        auto configs = mgr().get_sensors();
        auto it = std::find_if(configs.begin(), configs.end(),
                               [](const auto& c) { return c.klipper_name == "probe"; });
        REQUIRE(it != configs.end());
        REQUIRE(it->role == ProbeSensorRole::Z_PROBE);
    }

    SECTION("load_config with unknown sensor is handled gracefully") {
        json config;
        json sensors_array = json::array();
        json sensor1;
        sensor1["klipper_name"] = "unknown_sensor";
        sensor1["role"] = "z_probe";
        sensors_array.push_back(sensor1);
        config["sensors"] = sensors_array;

        // Should not crash
        mgr().load_config(config);

        // Existing sensors should be unaffected
        for (const auto& sensor : mgr().get_sensors()) {
            REQUIRE(sensor.role == ProbeSensorRole::NONE);
        }
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - edge cases", "[probe][edge]") {
    SECTION("get_sensor_state returns nullopt for unassigned role") {
        discover_test_sensors();
        auto state = mgr().get_sensor_state(ProbeSensorRole::Z_PROBE);
        REQUIRE_FALSE(state.has_value());
    }

    SECTION("get_sensor_state returns nullopt for NONE role") {
        discover_test_sensors();
        auto state = mgr().get_sensor_state(ProbeSensorRole::NONE);
        REQUIRE_FALSE(state.has_value());
    }

    SECTION("is_sensor_available checks role assignment and enabled") {
        discover_test_sensors();
        REQUIRE_FALSE(mgr().is_sensor_available(ProbeSensorRole::Z_PROBE));

        mgr().set_sensor_role("probe", ProbeSensorRole::Z_PROBE);
        REQUIRE(mgr().is_sensor_available(ProbeSensorRole::Z_PROBE));

        mgr().set_sensor_enabled("probe", false);
        REQUIRE_FALSE(mgr().is_sensor_available(ProbeSensorRole::Z_PROBE));
    }

    SECTION("category_name returns 'probe'") {
        REQUIRE(mgr().category_name() == "probe");
    }

    SECTION("Eddy current probe without name parameter is ignored") {
        // "probe_eddy_current" needs a name parameter
        std::vector<std::string> sensors = {"probe_eddy_current"};
        mgr().discover(sensors);

        // Should not discover (needs a name like "probe_eddy_current btt")
        REQUIRE(mgr().sensor_count() == 0);
    }
}
