// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_humidity_sensor_manager.cpp
 * @brief Unit tests for HumiditySensorManager
 *
 * Tests cover:
 * - Type helpers: role/type string conversion
 * - Sensor discovery from Klipper object names (bme280, htu21d)
 * - Role assignment (CHAMBER, DRYER)
 * - State updates from Moonraker status JSON
 * - Subject value correctness for UI binding
 * - Config persistence
 */

#include "../ui_test_utils.h"
#include "humidity_sensor_manager.h"
#include "humidity_sensor_types.h"

#include <spdlog/spdlog.h>

#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::sensors;
using json = nlohmann::json;

// ============================================================================
// Test Access
// ============================================================================

namespace helix::sensors {
class HumiditySensorManagerTestAccess {
  public:
    static void reset(HumiditySensorManager& obj) {
        std::lock_guard<std::recursive_mutex> lock(obj.mutex_);
        obj.sensors_.clear();
        obj.states_.clear();
        obj.sync_mode_ = true;
        obj.deinit_subjects();
    }
};
} // namespace helix::sensors

// ============================================================================
// Test Fixture
// ============================================================================

class HumiditySensorTestFixture {
  public:
    HumiditySensorTestFixture() {
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

        // Reset state for test isolation first (clears subjects)
        HumiditySensorManagerTestAccess::reset(mgr());

        // Initialize subjects after reset
        mgr().init_subjects();
    }

    ~HumiditySensorTestFixture() {
        // Reset after each test
        HumiditySensorManagerTestAccess::reset(mgr());
    }

  protected:
    HumiditySensorManager& mgr() {
        return HumiditySensorManager::instance();
    }

    // Helper to discover standard test sensors
    void discover_test_sensors() {
        std::vector<std::string> sensors = {"bme280 chamber", "htu21d dryer"};
        mgr().discover(sensors);
    }

    // Helper to simulate Moonraker status update
    void update_sensor_state(const std::string& klipper_name, float humidity, float temperature,
                             float pressure = 0.0f) {
        json status;
        status[klipper_name]["humidity"] = humidity;
        status[klipper_name]["temperature"] = temperature;
        if (pressure > 0.0f) {
            status[klipper_name]["pressure"] = pressure;
        }
        mgr().update_from_status(status);
    }

  private:
    static lv_display_t* display_;
    static bool display_created_;
};

// Static members
lv_display_t* HumiditySensorTestFixture::display_ = nullptr;
bool HumiditySensorTestFixture::display_created_ = false;

// ============================================================================
// Type Helper Tests (humidity_sensor_types.h)
// ============================================================================

TEST_CASE("HumiditySensorTypes - role string conversion", "[humidity][types]") {
    SECTION("humidity_role_to_string") {
        REQUIRE(humidity_role_to_string(HumiditySensorRole::NONE) == "none");
        REQUIRE(humidity_role_to_string(HumiditySensorRole::CHAMBER) == "chamber");
        REQUIRE(humidity_role_to_string(HumiditySensorRole::DRYER) == "dryer");
    }

    SECTION("humidity_role_from_string") {
        REQUIRE(humidity_role_from_string("none") == HumiditySensorRole::NONE);
        REQUIRE(humidity_role_from_string("chamber") == HumiditySensorRole::CHAMBER);
        REQUIRE(humidity_role_from_string("dryer") == HumiditySensorRole::DRYER);
        REQUIRE(humidity_role_from_string("invalid") == HumiditySensorRole::NONE);
        REQUIRE(humidity_role_from_string("") == HumiditySensorRole::NONE);
    }

    SECTION("humidity_role_to_display_string") {
        REQUIRE(humidity_role_to_display_string(HumiditySensorRole::NONE) == "Unassigned");
        REQUIRE(humidity_role_to_display_string(HumiditySensorRole::CHAMBER) == "Chamber");
        REQUIRE(humidity_role_to_display_string(HumiditySensorRole::DRYER) == "Dryer");
    }
}

TEST_CASE("HumiditySensorTypes - type string conversion", "[humidity][types]") {
    SECTION("humidity_type_to_string") {
        REQUIRE(humidity_type_to_string(HumiditySensorType::BME280) == "bme280");
        REQUIRE(humidity_type_to_string(HumiditySensorType::HTU21D) == "htu21d");
    }

    SECTION("humidity_type_from_string") {
        REQUIRE(humidity_type_from_string("bme280") == HumiditySensorType::BME280);
        REQUIRE(humidity_type_from_string("htu21d") == HumiditySensorType::HTU21D);
        REQUIRE(humidity_type_from_string("invalid") == HumiditySensorType::BME280);
        REQUIRE(humidity_type_from_string("") == HumiditySensorType::BME280);
    }
}

// ============================================================================
// Sensor Discovery Tests
// ============================================================================

TEST_CASE_METHOD(HumiditySensorTestFixture, "HumiditySensorManager - discovery",
                 "[humidity][discovery]") {
    SECTION("Discovers BME280 sensor") {
        std::vector<std::string> sensors = {"bme280 chamber"};
        mgr().discover(sensors);

        REQUIRE(mgr().has_sensors());
        REQUIRE(mgr().sensor_count() == 1);

        auto configs = mgr().get_sensors();
        REQUIRE(configs.size() == 1);
        REQUIRE(configs[0].klipper_name == "bme280 chamber");
        REQUIRE(configs[0].sensor_name == "chamber");
        REQUIRE(configs[0].type == HumiditySensorType::BME280);
        REQUIRE(configs[0].enabled == true);
        REQUIRE(configs[0].role == HumiditySensorRole::NONE);
    }

    SECTION("Discovers HTU21D sensor") {
        std::vector<std::string> sensors = {"htu21d dryer"};
        mgr().discover(sensors);

        REQUIRE(mgr().sensor_count() == 1);

        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "htu21d dryer");
        REQUIRE(configs[0].sensor_name == "dryer");
        REQUIRE(configs[0].type == HumiditySensorType::HTU21D);
    }

    SECTION("Discovers multiple sensors") {
        discover_test_sensors();

        REQUIRE(mgr().sensor_count() == 2);

        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].sensor_name == "chamber");
        REQUIRE(configs[0].type == HumiditySensorType::BME280);
        REQUIRE(configs[1].sensor_name == "dryer");
        REQUIRE(configs[1].type == HumiditySensorType::HTU21D);
    }

    SECTION("Ignores unrelated objects") {
        std::vector<std::string> sensors = {"bme280 chamber", "filament_switch_sensor runout",
                                            "temperature_sensor chamber", "extruder"};
        mgr().discover(sensors);

        REQUIRE(mgr().sensor_count() == 1);
        REQUIRE(mgr().get_sensors()[0].sensor_name == "chamber");
    }

    SECTION("Empty sensor list clears previous sensors") {
        discover_test_sensors();
        REQUIRE(mgr().sensor_count() == 2);

        mgr().discover({});
        REQUIRE(mgr().sensor_count() == 0);
        REQUIRE_FALSE(mgr().has_sensors());
    }

    SECTION("Re-discovery replaces sensor list") {
        std::vector<std::string> sensors1 = {"bme280 chamber"};
        mgr().discover(sensors1);
        REQUIRE(mgr().get_sensors()[0].sensor_name == "chamber");

        std::vector<std::string> sensors2 = {"htu21d dryer"};
        mgr().discover(sensors2);
        REQUIRE(mgr().sensor_count() == 1);
        REQUIRE(mgr().get_sensors()[0].sensor_name == "dryer");
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

TEST_CASE_METHOD(HumiditySensorTestFixture, "HumiditySensorManager - role assignment",
                 "[humidity][roles]") {
    discover_test_sensors();

    SECTION("Can set CHAMBER role") {
        mgr().set_sensor_role("bme280 chamber", HumiditySensorRole::CHAMBER);

        auto configs = mgr().get_sensors();
        auto it = std::find_if(configs.begin(), configs.end(),
                               [](const auto& c) { return c.sensor_name == "chamber"; });
        REQUIRE(it != configs.end());
        REQUIRE(it->role == HumiditySensorRole::CHAMBER);
    }

    SECTION("Can set DRYER role") {
        mgr().set_sensor_role("htu21d dryer", HumiditySensorRole::DRYER);

        auto configs = mgr().get_sensors();
        auto it = std::find_if(configs.begin(), configs.end(),
                               [](const auto& c) { return c.sensor_name == "dryer"; });
        REQUIRE(it != configs.end());
        REQUIRE(it->role == HumiditySensorRole::DRYER);
    }

    SECTION("Role assignment is unique - assigning same role clears previous") {
        mgr().set_sensor_role("bme280 chamber", HumiditySensorRole::CHAMBER);
        mgr().set_sensor_role("htu21d dryer", HumiditySensorRole::CHAMBER);

        auto configs = mgr().get_sensors();

        auto chamber_it = std::find_if(configs.begin(), configs.end(),
                                       [](const auto& c) { return c.sensor_name == "chamber"; });
        REQUIRE(chamber_it->role == HumiditySensorRole::NONE);

        auto dryer_it = std::find_if(configs.begin(), configs.end(),
                                     [](const auto& c) { return c.sensor_name == "dryer"; });
        REQUIRE(dryer_it->role == HumiditySensorRole::CHAMBER);
    }

    SECTION("Can assign NONE without affecting other sensors") {
        mgr().set_sensor_role("bme280 chamber", HumiditySensorRole::CHAMBER);

        mgr().set_sensor_role("bme280 chamber", HumiditySensorRole::NONE);

        auto configs = mgr().get_sensors();
        auto it = std::find_if(configs.begin(), configs.end(),
                               [](const auto& c) { return c.sensor_name == "chamber"; });
        REQUIRE(it->role == HumiditySensorRole::NONE);
    }

    SECTION("Assigning role to unknown sensor does nothing") {
        mgr().set_sensor_role("nonexistent_sensor", HumiditySensorRole::CHAMBER);

        for (const auto& config : mgr().get_sensors()) {
            REQUIRE(config.role == HumiditySensorRole::NONE);
        }
    }
}

// ============================================================================
// State Update Tests
// ============================================================================

TEST_CASE_METHOD(HumiditySensorTestFixture, "HumiditySensorManager - state updates",
                 "[humidity][state]") {
    discover_test_sensors();
    mgr().set_sensor_role("bme280 chamber", HumiditySensorRole::CHAMBER);

    SECTION("Parses humidity, temperature, pressure from status JSON") {
        auto state = mgr().get_sensor_state(HumiditySensorRole::CHAMBER);
        REQUIRE(state.has_value());
        REQUIRE(state->humidity == 0.0f);
        REQUIRE(state->temperature == 0.0f);
        REQUIRE(state->pressure == 0.0f);

        json status;
        status["bme280 chamber"]["humidity"] = 45.5f;
        status["bme280 chamber"]["temperature"] = 25.3f;
        status["bme280 chamber"]["pressure"] = 1013.25f;
        mgr().update_from_status(status);

        state = mgr().get_sensor_state(HumiditySensorRole::CHAMBER);
        REQUIRE(state->humidity == Catch::Approx(45.5f));
        REQUIRE(state->temperature == Catch::Approx(25.3f));
        REQUIRE(state->pressure == Catch::Approx(1013.25f));
    }

    SECTION("HTU21D sensor has no pressure") {
        mgr().set_sensor_role("htu21d dryer", HumiditySensorRole::DRYER);

        json status;
        status["htu21d dryer"]["humidity"] = 20.1f;
        status["htu21d dryer"]["temperature"] = 55.0f;
        mgr().update_from_status(status);

        auto state = mgr().get_sensor_state(HumiditySensorRole::DRYER);
        REQUIRE(state->humidity == Catch::Approx(20.1f));
        REQUIRE(state->temperature == Catch::Approx(55.0f));
        REQUIRE(state->pressure == 0.0f); // HTU21D has no pressure sensor
    }

    SECTION("Status update for unknown sensor is ignored") {
        json status;
        status["unknown_sensor"]["humidity"] = 50.0f;
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

TEST_CASE_METHOD(HumiditySensorTestFixture, "HumiditySensorManager - subject values",
                 "[humidity][subjects]") {
    discover_test_sensors();

    SECTION("Chamber humidity subject shows -1 when no sensor assigned to role") {
        REQUIRE(lv_subject_get_int(mgr().get_chamber_humidity_subject()) == -1);
    }

    SECTION("Chamber humidity subject updates correctly (humidity x 10)") {
        mgr().set_sensor_role("bme280 chamber", HumiditySensorRole::CHAMBER);

        // After assignment, should show 0 since state defaults to 0.0
        REQUIRE(lv_subject_get_int(mgr().get_chamber_humidity_subject()) == 0);

        // Update state with humidity 45.5%
        update_sensor_state("bme280 chamber", 45.5f, 25.3f, 1013.25f);
        REQUIRE(lv_subject_get_int(mgr().get_chamber_humidity_subject()) == 455);

        // Update with different value
        update_sensor_state("bme280 chamber", 52.3f, 26.0f, 1010.0f);
        REQUIRE(lv_subject_get_int(mgr().get_chamber_humidity_subject()) == 523);
    }

    SECTION("Chamber pressure subject updates correctly (hPa to Pa)") {
        mgr().set_sensor_role("bme280 chamber", HumiditySensorRole::CHAMBER);

        // Before state update
        REQUIRE(lv_subject_get_int(mgr().get_chamber_pressure_subject()) == 0);

        // Update state - pressure 1013.25 hPa = 101325 Pa
        update_sensor_state("bme280 chamber", 45.5f, 25.3f, 1013.25f);
        REQUIRE(lv_subject_get_int(mgr().get_chamber_pressure_subject()) == 101325);
    }

    SECTION("Dryer humidity subject updates correctly") {
        mgr().set_sensor_role("htu21d dryer", HumiditySensorRole::DRYER);

        // Before state update
        REQUIRE(lv_subject_get_int(mgr().get_dryer_humidity_subject()) == 0);

        // Update state with humidity 20.1%
        update_sensor_state("htu21d dryer", 20.1f, 55.0f);
        REQUIRE(lv_subject_get_int(mgr().get_dryer_humidity_subject()) == 201);
    }

    SECTION("Dryer humidity subject shows -1 when no sensor assigned") {
        // No dryer role assigned
        REQUIRE(lv_subject_get_int(mgr().get_dryer_humidity_subject()) == -1);
    }

    SECTION("Chamber humidity subject shows -1 when sensor disabled") {
        mgr().set_sensor_role("bme280 chamber", HumiditySensorRole::CHAMBER);
        update_sensor_state("bme280 chamber", 45.5f, 25.3f, 1013.25f);

        mgr().set_sensor_enabled("bme280 chamber", false);
        REQUIRE(lv_subject_get_int(mgr().get_chamber_humidity_subject()) == -1);
    }

    SECTION("Chamber pressure subject shows -1 when no chamber sensor") {
        // No chamber role assigned
        REQUIRE(lv_subject_get_int(mgr().get_chamber_pressure_subject()) == -1);
    }
}

// ============================================================================
// Config Persistence Tests
// ============================================================================

TEST_CASE_METHOD(HumiditySensorTestFixture, "HumiditySensorManager - config persistence",
                 "[humidity][config]") {
    discover_test_sensors();

    SECTION("save_config returns JSON with role assignments") {
        mgr().set_sensor_role("bme280 chamber", HumiditySensorRole::CHAMBER);
        mgr().set_sensor_role("htu21d dryer", HumiditySensorRole::DRYER);

        json config = mgr().save_config();

        REQUIRE(config.is_object());
        REQUIRE(config.contains("sensors"));
        REQUIRE(config["sensors"].is_array());
        REQUIRE(config["sensors"].size() == 2);

        bool found_chamber = false;
        bool found_dryer = false;
        for (const auto& sensor : config["sensors"]) {
            if (sensor["klipper_name"] == "bme280 chamber") {
                REQUIRE(sensor["role"] == "chamber");
                found_chamber = true;
            }
            if (sensor["klipper_name"] == "htu21d dryer") {
                REQUIRE(sensor["role"] == "dryer");
                found_dryer = true;
            }
        }
        REQUIRE(found_chamber);
        REQUIRE(found_dryer);
    }

    SECTION("load_config restores role assignments") {
        // Set up config JSON
        json config;
        json sensors_array = json::array();
        json sensor1;
        sensor1["klipper_name"] = "bme280 chamber";
        sensor1["role"] = "chamber";
        sensor1["enabled"] = true;
        sensors_array.push_back(sensor1);
        json sensor2;
        sensor2["klipper_name"] = "htu21d dryer";
        sensor2["role"] = "dryer";
        sensor2["enabled"] = true;
        sensors_array.push_back(sensor2);
        config["sensors"] = sensors_array;

        mgr().load_config(config);

        auto configs = mgr().get_sensors();
        auto chamber_it = std::find_if(configs.begin(), configs.end(),
                                       [](const auto& c) { return c.sensor_name == "chamber"; });
        REQUIRE(chamber_it != configs.end());
        REQUIRE(chamber_it->role == HumiditySensorRole::CHAMBER);

        auto dryer_it = std::find_if(configs.begin(), configs.end(),
                                     [](const auto& c) { return c.sensor_name == "dryer"; });
        REQUIRE(dryer_it != configs.end());
        REQUIRE(dryer_it->role == HumiditySensorRole::DRYER);
    }

    SECTION("load_config with unknown sensor is handled gracefully") {
        json config;
        json sensors_array = json::array();
        json sensor1;
        sensor1["klipper_name"] = "unknown_sensor";
        sensor1["role"] = "chamber";
        sensors_array.push_back(sensor1);
        config["sensors"] = sensors_array;

        // Should not crash
        mgr().load_config(config);

        // Existing sensors should be unaffected
        for (const auto& sensor : mgr().get_sensors()) {
            REQUIRE(sensor.role == HumiditySensorRole::NONE);
        }
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE_METHOD(HumiditySensorTestFixture, "HumiditySensorManager - edge cases",
                 "[humidity][edge]") {
    SECTION("get_sensor_state returns nullopt for unassigned role") {
        discover_test_sensors();
        auto state = mgr().get_sensor_state(HumiditySensorRole::CHAMBER);
        REQUIRE_FALSE(state.has_value());
    }

    SECTION("get_sensor_state returns nullopt for NONE role") {
        discover_test_sensors();
        auto state = mgr().get_sensor_state(HumiditySensorRole::NONE);
        REQUIRE_FALSE(state.has_value());
    }

    SECTION("is_sensor_available checks role assignment and enabled") {
        discover_test_sensors();
        REQUIRE_FALSE(mgr().is_sensor_available(HumiditySensorRole::CHAMBER));

        mgr().set_sensor_role("bme280 chamber", HumiditySensorRole::CHAMBER);
        REQUIRE(mgr().is_sensor_available(HumiditySensorRole::CHAMBER));

        mgr().set_sensor_enabled("bme280 chamber", false);
        REQUIRE_FALSE(mgr().is_sensor_available(HumiditySensorRole::CHAMBER));
    }

    SECTION("category_name returns 'humidity'") {
        REQUIRE(mgr().category_name() == "humidity");
    }

    SECTION("BME280 sensor parses name with space correctly") {
        std::vector<std::string> sensors = {"bme280 my_custom_name"};
        mgr().discover(sensors);

        REQUIRE(mgr().sensor_count() == 1);
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "bme280 my_custom_name");
        REQUIRE(configs[0].sensor_name == "my_custom_name");
        REQUIRE(configs[0].type == HumiditySensorType::BME280);
    }

    SECTION("HTU21D sensor parses name with space correctly") {
        std::vector<std::string> sensors = {"htu21d my_dryer_sensor"};
        mgr().discover(sensors);

        REQUIRE(mgr().sensor_count() == 1);
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "htu21d my_dryer_sensor");
        REQUIRE(configs[0].sensor_name == "my_dryer_sensor");
        REQUIRE(configs[0].type == HumiditySensorType::HTU21D);
    }
}
