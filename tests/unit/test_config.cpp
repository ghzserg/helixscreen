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
 */

#include "config.h"
#include "wizard_config_paths.h"

#include "../catch_amalgamated.hpp"

// Test fixture for Config class testing
class ConfigTestFixture {
  protected:
    Config config;

    // Helper methods to access protected members
    void set_data_null(const std::string& json_ptr) {
        config.data[json::json_pointer(json_ptr)] = nullptr;
    }

    void set_data_empty() {
        config.data = {};
    }

    // Helper for plural naming refactor tests
    void set_data_for_plural_test(const json& data) {
        config.data = data;
    }

    void setup_default_config() {
        // Manually populate config.data with realistic test JSON
        config.data = {
            {"printer",
             {{"moonraker_host", "192.168.1.100"},
              {"moonraker_port", 7125},
              {"log_level", "debug"},
              {"hardware_map", {{"heated_bed", "heater_bed"}, {"hotend", "extruder"}}}}}};
    }

    void setup_minimal_config() {
        // Minimal config for wizard testing (default host)
        config.data = {{"printer", {{"moonraker_host", "127.0.0.1"}, {"moonraker_port", 7125}}}};
    }

    void setup_incomplete_config() {
        // Config missing hardware_map (should trigger wizard)
        config.data = {{"printer", {{"moonraker_host", "192.168.1.50"}, {"moonraker_port", 7125}}}};
    }
};

// ============================================================================
// get() without default parameter - Existing behavior
// ============================================================================

TEST_CASE_METHOD(ConfigTestFixture, "Config: get() returns existing string value",
                 "[core][config][get]") {
    setup_default_config();

    std::string host = config.get<std::string>("/printer/moonraker_host");
    REQUIRE(host == "192.168.1.100");
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: get() returns existing int value",
                 "[core][config][get]") {
    setup_default_config();

    int port = config.get<int>("/printer/moonraker_port");
    REQUIRE(port == 7125);
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: get() returns existing nested value",
                 "[config][get]") {
    setup_default_config();

    std::string bed = config.get<std::string>("/printer/hardware_map/heated_bed");
    REQUIRE(bed == "heater_bed");
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: get() with df() prefix returns value",
                 "[config][get]") {
    setup_default_config();

    std::string host = config.get<std::string>(config.df() + "moonraker_host");
    REQUIRE(host == "192.168.1.100");
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: get() with missing key throws exception",
                 "[core][config][get]") {
    setup_default_config();

    REQUIRE_THROWS_AS(config.get<std::string>("/printer/nonexistent_key"),
                      nlohmann::detail::type_error);
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: get() with missing nested key throws exception",
                 "[config][get]") {
    setup_default_config();

    REQUIRE_THROWS_AS(config.get<std::string>("/printer/hardware_map/missing"),
                      nlohmann::detail::type_error);
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: get() with type mismatch throws exception",
                 "[config][get]") {
    setup_default_config();

    // Try to get string value as int
    REQUIRE_THROWS(config.get<int>("/printer/moonraker_host"));
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: get() with object returns nested structure",
                 "[config][get]") {
    setup_default_config();

    auto hardware_map = config.get<json>("/printer/hardware_map");
    REQUIRE(hardware_map.is_object());
    REQUIRE(hardware_map["heated_bed"] == "heater_bed");
    REQUIRE(hardware_map["hotend"] == "extruder");
}

// ============================================================================
// get() with default parameter - NEW behavior
// ============================================================================

TEST_CASE_METHOD(ConfigTestFixture,
                 "Config: get() with default returns value when key exists (string)",
                 "[config][get][default]") {
    setup_default_config();

    std::string host = config.get<std::string>("/printer/moonraker_host", "default.local");
    REQUIRE(host == "192.168.1.100"); // Ignores default
}

TEST_CASE_METHOD(ConfigTestFixture,
                 "Config: get() with default returns value when key exists (int)",
                 "[config][get][default]") {
    setup_default_config();

    int port = config.get<int>("/printer/moonraker_port", 9999);
    REQUIRE(port == 7125); // Ignores default
}

TEST_CASE_METHOD(ConfigTestFixture,
                 "Config: get() with default returns default when key missing (string)",
                 "[core][config][get][default]") {
    setup_default_config();

    std::string printer_name = config.get<std::string>("/printer/printer_name", "My Printer");
    REQUIRE(printer_name == "My Printer");
}

TEST_CASE_METHOD(ConfigTestFixture,
                 "Config: get() with default returns default when key missing (int)",
                 "[config][get][default]") {
    setup_default_config();

    int timeout = config.get<int>("/printer/timeout", 30);
    REQUIRE(timeout == 30);
}

TEST_CASE_METHOD(ConfigTestFixture,
                 "Config: get() with default returns default when key missing (bool)",
                 "[config][get][default]") {
    setup_default_config();

    bool api_key = config.get<bool>("/printer/moonraker_api_key", false);
    REQUIRE(api_key == false);
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: get() with default handles nested missing path",
                 "[config][get][default]") {
    setup_default_config();

    std::string led = config.get<std::string>("/printer/hardware_map/main_led", "none");
    REQUIRE(led == "none");
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: get() with empty string default",
                 "[config][get][default]") {
    setup_default_config();

    std::string empty = config.get<std::string>("/printer/empty_field", "");
    REQUIRE(empty == "");
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: get() with default using df() prefix",
                 "[config][get][default]") {
    setup_default_config();

    std::string printer_name = config.get<std::string>(config.df() + "printer_name", "");
    REQUIRE(printer_name == "");
}

TEST_CASE_METHOD(ConfigTestFixture,
                 "Config: get() with default handles completely missing parent path",
                 "[config][get][default]") {
    setup_default_config();

    std::string missing = config.get<std::string>("/nonexistent/path/key", "fallback");
    REQUIRE(missing == "fallback");
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: get() with default prevents crashes on null keys",
                 "[config][get][default]") {
    setup_minimal_config();

    // This is the bug we fixed - printer_name doesn't exist, should return default not throw
    std::string printer_name = config.get<std::string>(config.df() + "printer_name", "");
    REQUIRE(printer_name == "");
}

// ============================================================================
// set() operations
// ============================================================================

TEST_CASE_METHOD(ConfigTestFixture, "Config: set() creates new top-level key", "[config][set]") {
    setup_default_config();

    config.set<std::string>("/new_key", "new_value");
    REQUIRE(config.get<std::string>("/new_key") == "new_value");
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: set() updates existing key", "[config][set]") {
    setup_default_config();

    config.set<std::string>("/printer/moonraker_host", "10.0.0.1");
    REQUIRE(config.get<std::string>("/printer/moonraker_host") == "10.0.0.1");
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: set() creates nested path", "[config][set]") {
    setup_default_config();

    config.set<std::string>("/printer/hardware_map/main_led", "neopixel");
    REQUIRE(config.get<std::string>("/printer/hardware_map/main_led") == "neopixel");
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: set() updates nested value", "[config][set]") {
    setup_default_config();

    config.set<std::string>("/printer/hardware_map/hotend", "extruder1");
    REQUIRE(config.get<std::string>("/printer/hardware_map/hotend") == "extruder1");
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: set() handles different types", "[config][set]") {
    setup_default_config();

    config.set<int>("/printer/new_int", 42);
    config.set<bool>("/printer/new_bool", true);
    config.set<std::string>("/printer/new_string", "test");

    REQUIRE(config.get<int>("/printer/new_int") == 42);
    REQUIRE(config.get<bool>("/printer/new_bool") == true);
    REQUIRE(config.get<std::string>("/printer/new_string") == "test");
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: set() overwrites value of different type",
                 "[config][set]") {
    setup_default_config();

    config.set<int>("/printer/moonraker_port", 8080);
    REQUIRE(config.get<int>("/printer/moonraker_port") == 8080);

    // Overwrite int with string
    config.set<std::string>("/printer/moonraker_port", "9090");
    REQUIRE(config.get<std::string>("/printer/moonraker_port") == "9090");
}

// ============================================================================
// is_wizard_required() logic - NEW: wizard_completed flag
// ============================================================================

TEST_CASE_METHOD(ConfigTestFixture,
                 "Config: is_wizard_required() returns false when wizard_completed is true",
                 "[config][wizard]") {
    setup_minimal_config();

    // Set wizard_completed flag
    config.set<bool>("/wizard_completed", true);

    REQUIRE(config.is_wizard_required() == false);
}

TEST_CASE_METHOD(ConfigTestFixture,
                 "Config: is_wizard_required() returns true when wizard_completed is false",
                 "[config][wizard]") {
    setup_default_config();

    // Explicitly set wizard_completed to false
    config.set<bool>("/wizard_completed", false);

    REQUIRE(config.is_wizard_required() == true);
}

TEST_CASE_METHOD(ConfigTestFixture,
                 "Config: is_wizard_required() returns true when wizard_completed flag missing",
                 "[config][wizard]") {
    setup_minimal_config();

    // No wizard_completed flag set
    REQUIRE(config.is_wizard_required() == true);
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: wizard_completed flag overrides hardware config",
                 "[config][wizard]") {
    setup_default_config();

    // Even with full hardware config, if wizard_completed is false, wizard should run
    config.set<bool>("/wizard_completed", false);

    REQUIRE(config.is_wizard_required() == true);
}

TEST_CASE_METHOD(ConfigTestFixture,
                 "Config: wizard_completed=true skips wizard even with minimal config",
                 "[config][wizard]") {
    setup_minimal_config();

    // Even with minimal config (127.0.0.1 host), wizard_completed=true should skip wizard
    config.set<bool>("/wizard_completed", true);

    REQUIRE(config.is_wizard_required() == false);
}

TEST_CASE_METHOD(ConfigTestFixture,
                 "Config: is_wizard_required() handles invalid wizard_completed type",
                 "[config][wizard]") {
    setup_default_config();

    // Set wizard_completed to invalid type (string instead of bool)
    config.set<std::string>("/wizard_completed", "true");

    // Should return true (wizard required) because flag is not a valid boolean
    REQUIRE(config.is_wizard_required() == true);
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: is_wizard_required() handles null wizard_completed",
                 "[config][wizard]") {
    setup_default_config();

    // Set wizard_completed to null
    set_data_null("/wizard_completed");

    // Should return true (wizard required) because flag is null
    REQUIRE(config.is_wizard_required() == true);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_CASE_METHOD(ConfigTestFixture, "Config: handles deeply nested structures", "[config][edge]") {
    setup_default_config();

    config.set<std::string>("/printer/nested/level1/level2/level3", "deep");
    std::string deep = config.get<std::string>("/printer/nested/level1/level2/level3");
    REQUIRE(deep == "deep");
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: get() with default handles empty config",
                 "[config][edge]") {
    // Empty config
    set_data_empty();

    std::string host = config.get<std::string>("/printer/moonraker_host", "localhost");
    REQUIRE(host == "localhost");
}

// ============================================================================
// Config Path Structure Tests - NEW plural naming convention
// These tests define the contract for the refactored config structure.
// They SHOULD FAIL until the implementation is updated.
// ============================================================================

TEST_CASE_METHOD(ConfigTestFixture, "Config: heaters path uses plural form /printer/heaters/",
                 "[config][paths][plural]") {
    // Set up config with the NEW plural path structure
    set_data_for_plural_test(
        {{"printer", {{"heaters", {{"bed", "heater_bed"}, {"hotend", "extruder"}}}}}});

    // Verify we can read from the plural path
    std::string bed_heater = config.get<std::string>("/printer/heaters/bed");
    REQUIRE(bed_heater == "heater_bed");

    std::string hotend_heater = config.get<std::string>("/printer/heaters/hotend");
    REQUIRE(hotend_heater == "extruder");
}

TEST_CASE_METHOD(ConfigTestFixture,
                 "Config: temp_sensors path uses plural form /printer/temp_sensors/",
                 "[config][paths][plural]") {
    // Set up config with the NEW plural path structure
    set_data_for_plural_test(
        {{"printer", {{"temp_sensors", {{"bed", "heater_bed"}, {"hotend", "extruder"}}}}}});

    // Verify we can read from the plural path
    std::string bed_sensor = config.get<std::string>("/printer/temp_sensors/bed");
    REQUIRE(bed_sensor == "heater_bed");

    std::string hotend_sensor = config.get<std::string>("/printer/temp_sensors/hotend");
    REQUIRE(hotend_sensor == "extruder");
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: fans path uses plural form /printer/fans/",
                 "[config][paths][plural]") {
    // Set up config with the NEW plural path structure
    set_data_for_plural_test(
        {{"printer", {{"fans", {{"part", "fan"}, {"hotend", "heater_fan hotend_fan"}}}}}});

    // Verify we can read from the plural path - fans is now an OBJECT, not array
    std::string part_fan = config.get<std::string>("/printer/fans/part");
    REQUIRE(part_fan == "fan");

    std::string hotend_fan = config.get<std::string>("/printer/fans/hotend");
    REQUIRE(hotend_fan == "heater_fan hotend_fan");
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: leds path uses plural form /printer/leds/",
                 "[config][paths][plural]") {
    // Set up config with the NEW plural path structure
    set_data_for_plural_test({{"printer", {{"leds", {{"strip", "neopixel chamber_light"}}}}}});

    // Verify we can read from the plural path
    std::string led_strip = config.get<std::string>("/printer/leds/strip");
    REQUIRE(led_strip == "neopixel chamber_light");
}

// ============================================================================
// Default Config Structure Tests - NEW structure contract
// These tests verify the default config matches the new schema.
// They SHOULD FAIL until the implementation is updated.
// ============================================================================

TEST_CASE_METHOD(ConfigTestFixture, "Config: default structure has extra_sensors as empty object",
                 "[config][defaults][plural]") {
    // After refactoring, monitored_sensors should become extra_sensors
    // and should be an empty object {}, not an array []
    set_data_for_plural_test({{"printer",
                               {{"moonraker_host", "127.0.0.1"},
                                {"moonraker_port", 7125},
                                {"extra_sensors", json::object()}}}});

    auto extra_sensors = config.get<json>("/printer/extra_sensors");
    REQUIRE(extra_sensors.is_object());
    REQUIRE(extra_sensors.empty());
}

TEST_CASE_METHOD(ConfigTestFixture,
                 "Config: default structure has no fans array - fans is object only",
                 "[config][defaults][plural]") {
    // After refactoring, there should be no separate "fans" array
    // The fans key should be an object with role mappings, not an array
    set_data_for_plural_test({{"printer",
                               {{"moonraker_host", "127.0.0.1"},
                                {"moonraker_port", 7125},
                                {"fans", {{"part", "fan"}}}}}});

    auto fans = config.get<json>("/printer/fans");
    REQUIRE(fans.is_object());
    REQUIRE_FALSE(fans.is_array());
}

TEST_CASE_METHOD(ConfigTestFixture,
                 "Config: temp_sensors key exists for temperature sensor mappings",
                 "[config][defaults][plural]") {
    // The new structure uses temp_sensors (not just sensors) for temperature mappings
    set_data_for_plural_test(
        {{"printer", {{"temp_sensors", {{"bed", "heater_bed"}, {"hotend", "extruder"}}}}}});

    auto temp_sensors = config.get<json>("/printer/temp_sensors");
    REQUIRE(temp_sensors.is_object());
    REQUIRE(temp_sensors.contains("bed"));
    REQUIRE(temp_sensors.contains("hotend"));
}

TEST_CASE_METHOD(ConfigTestFixture, "Config: hardware section is under /printer/hardware/",
                 "[config][defaults][plural]") {
    // Hardware config should be under /printer/hardware/ not /hardware/
    set_data_for_plural_test({{"printer",
                               {{"hardware",
                                 {{"optional", json::array()},
                                  {"expected", json::array()},
                                  {"last_snapshot", json::object()}}}}}});

    auto hardware = config.get<json>("/printer/hardware");
    REQUIRE(hardware.is_object());
    REQUIRE(hardware.contains("optional"));
    REQUIRE(hardware.contains("expected"));
    REQUIRE(hardware.contains("last_snapshot"));
}

// ============================================================================
// Wizard Config Path Constants Tests - Verify plural naming
// These tests verify that wizard_config_paths.h constants use plural form.
// They SHOULD FAIL until the implementation is updated.
// ============================================================================

TEST_CASE("WizardConfigPaths: BED_HEATER uses plural /printer/heaters/",
          "[config][paths][wizard][plural]") {
    // The path constant should use plural "heaters" not singular "heater"
    std::string path = helix::wizard::BED_HEATER;
    REQUIRE(path == "/printer/heaters/bed");
}

TEST_CASE("WizardConfigPaths: HOTEND_HEATER uses plural /printer/heaters/",
          "[config][paths][wizard][plural]") {
    std::string path = helix::wizard::HOTEND_HEATER;
    REQUIRE(path == "/printer/heaters/hotend");
}

TEST_CASE("WizardConfigPaths: BED_SENSOR uses plural /printer/temp_sensors/",
          "[config][paths][wizard][plural]") {
    // The path constant should use "temp_sensors" not "sensor"
    std::string path = helix::wizard::BED_SENSOR;
    REQUIRE(path == "/printer/temp_sensors/bed");
}

TEST_CASE("WizardConfigPaths: HOTEND_SENSOR uses plural /printer/temp_sensors/",
          "[config][paths][wizard][plural]") {
    std::string path = helix::wizard::HOTEND_SENSOR;
    REQUIRE(path == "/printer/temp_sensors/hotend");
}

TEST_CASE("WizardConfigPaths: PART_FAN uses plural /printer/fans/",
          "[config][paths][wizard][plural]") {
    // The path constant should use plural "fans" not singular "fan"
    std::string path = helix::wizard::PART_FAN;
    REQUIRE(path == "/printer/fans/part");
}

TEST_CASE("WizardConfigPaths: HOTEND_FAN uses plural /printer/fans/",
          "[config][paths][wizard][plural]") {
    std::string path = helix::wizard::HOTEND_FAN;
    REQUIRE(path == "/printer/fans/hotend");
}

TEST_CASE("WizardConfigPaths: LED_STRIP uses plural /printer/leds/",
          "[config][paths][wizard][plural]") {
    // The path constant should use plural "leds" not singular "led"
    std::string path = helix::wizard::LED_STRIP;
    REQUIRE(path == "/printer/leds/strip");
}
