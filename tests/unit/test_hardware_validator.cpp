// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2025 HelixScreen

/**
 * @file test_hardware_validator.cpp
 * @brief Unit tests for HardwareValidator hardware validation system
 *
 * Tests the HardwareValidator class which validates config expectations
 * against Moonraker hardware discovery:
 * - HardwareSnapshot serialization/comparison
 * - HardwareValidationResult aggregation
 * - Critical hardware detection
 * - Configured vs discovered hardware validation
 * - Optional hardware marking
 */

#include "hardware_validator.h"
#include "moonraker_client_mock.h"
#include "printer_capabilities.h"

#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using json = nlohmann::json;

// ============================================================================
// HardwareSnapshot Tests
// ============================================================================

TEST_CASE("HardwareSnapshot - JSON serialization", "[hardware][validator]") {
    SECTION("Serializes to JSON correctly") {
        HardwareSnapshot snapshot;
        snapshot.timestamp = "2025-01-01T12:00:00Z";
        snapshot.heaters = {"extruder", "heater_bed"};
        snapshot.sensors = {"temperature_sensor chamber"};
        snapshot.fans = {"fan", "heater_fan hotend_fan"};
        snapshot.leds = {"neopixel chamber_light"};
        snapshot.filament_sensors = {"filament_switch_sensor fsensor"};

        json j = snapshot.to_json();

        REQUIRE(j["timestamp"] == "2025-01-01T12:00:00Z");
        REQUIRE(j["heaters"].size() == 2);
        REQUIRE(j["heaters"][0] == "extruder");
        REQUIRE(j["heaters"][1] == "heater_bed");
        REQUIRE(j["sensors"].size() == 1);
        REQUIRE(j["fans"].size() == 2);
        REQUIRE(j["leds"].size() == 1);
        REQUIRE(j["filament_sensors"].size() == 1);
    }

    SECTION("Deserializes from JSON correctly") {
        json j = {{"timestamp", "2025-01-01T12:00:00Z"},
                  {"heaters", {"extruder", "heater_bed"}},
                  {"sensors", {"temperature_sensor chamber"}},
                  {"fans", {"fan"}},
                  {"leds", {"neopixel test"}},
                  {"filament_sensors", {"filament_switch_sensor fs"}}};

        HardwareSnapshot snapshot = HardwareSnapshot::from_json(j);

        REQUIRE(snapshot.timestamp == "2025-01-01T12:00:00Z");
        REQUIRE(snapshot.heaters.size() == 2);
        REQUIRE(snapshot.sensors.size() == 1);
        REQUIRE(snapshot.fans.size() == 1);
        REQUIRE(snapshot.leds.size() == 1);
        REQUIRE(snapshot.filament_sensors.size() == 1);
    }

    SECTION("Handles missing fields gracefully") {
        json j = {{"timestamp", "2025-01-01T12:00:00Z"}, {"heaters", {"extruder"}}};

        HardwareSnapshot snapshot = HardwareSnapshot::from_json(j);

        REQUIRE(snapshot.timestamp == "2025-01-01T12:00:00Z");
        REQUIRE(snapshot.heaters.size() == 1);
        REQUIRE(snapshot.sensors.empty());
        REQUIRE(snapshot.fans.empty());
        REQUIRE(snapshot.leds.empty());
        REQUIRE(snapshot.filament_sensors.empty());
    }

    SECTION("Returns empty snapshot on invalid JSON") {
        json j = "not an object";

        HardwareSnapshot snapshot = HardwareSnapshot::from_json(j);

        REQUIRE(snapshot.is_empty());
    }
}

TEST_CASE("HardwareSnapshot - Comparison", "[hardware][validator]") {
    SECTION("get_removed finds items in old but not in current") {
        HardwareSnapshot old_snapshot;
        old_snapshot.heaters = {"extruder", "heater_bed"};
        old_snapshot.fans = {"fan", "heater_fan hotend_fan"};
        old_snapshot.leds = {"neopixel chamber_light"};

        HardwareSnapshot current;
        current.heaters = {"extruder", "heater_bed"};
        current.fans = {"fan"}; // hotend_fan removed
        current.leds = {};      // LED removed

        auto removed = old_snapshot.get_removed(current);

        REQUIRE(removed.size() == 2);
        REQUIRE(std::find(removed.begin(), removed.end(), "heater_fan hotend_fan") !=
                removed.end());
        REQUIRE(std::find(removed.begin(), removed.end(), "neopixel chamber_light") !=
                removed.end());
    }

    SECTION("get_added finds items in current but not in old") {
        HardwareSnapshot old_snapshot;
        old_snapshot.heaters = {"extruder"};
        old_snapshot.fans = {"fan"};

        HardwareSnapshot current;
        current.heaters = {"extruder", "heater_bed"}; // bed added
        current.fans = {"fan", "controller_fan mcu"}; // controller fan added
        current.leds = {"neopixel strip"};            // LED added

        auto added = old_snapshot.get_added(current);

        REQUIRE(added.size() == 3);
        REQUIRE(std::find(added.begin(), added.end(), "heater_bed") != added.end());
        REQUIRE(std::find(added.begin(), added.end(), "controller_fan mcu") != added.end());
        REQUIRE(std::find(added.begin(), added.end(), "neopixel strip") != added.end());
    }

    SECTION("Returns empty when snapshots are identical") {
        HardwareSnapshot snapshot;
        snapshot.heaters = {"extruder", "heater_bed"};
        snapshot.fans = {"fan"};

        REQUIRE(snapshot.get_removed(snapshot).empty());
        REQUIRE(snapshot.get_added(snapshot).empty());
    }
}

TEST_CASE("HardwareSnapshot - is_empty", "[hardware][validator]") {
    SECTION("Returns true for default-constructed snapshot") {
        HardwareSnapshot snapshot;
        REQUIRE(snapshot.is_empty());
    }

    SECTION("Returns false when any list has items") {
        HardwareSnapshot snapshot;

        snapshot.heaters = {"extruder"};
        REQUIRE_FALSE(snapshot.is_empty());

        snapshot.heaters.clear();
        snapshot.fans = {"fan"};
        REQUIRE_FALSE(snapshot.is_empty());
    }
}

// ============================================================================
// HardwareIssue Factory Tests
// ============================================================================

TEST_CASE("HardwareIssue - Factory methods", "[hardware][validator]") {
    SECTION("critical() creates CRITICAL severity issue") {
        auto issue = HardwareIssue::critical("extruder", HardwareType::HEATER, "Missing extruder");

        REQUIRE(issue.hardware_name == "extruder");
        REQUIRE(issue.hardware_type == HardwareType::HEATER);
        REQUIRE(issue.severity == HardwareIssueSeverity::CRITICAL);
        REQUIRE(issue.message == "Missing extruder");
        REQUIRE_FALSE(issue.is_optional);
    }

    SECTION("warning() creates WARNING severity issue") {
        auto issue =
            HardwareIssue::warning("neopixel test", HardwareType::LED, "LED not found", true);

        REQUIRE(issue.hardware_name == "neopixel test");
        REQUIRE(issue.hardware_type == HardwareType::LED);
        REQUIRE(issue.severity == HardwareIssueSeverity::WARNING);
        REQUIRE(issue.is_optional == true);
    }

    SECTION("info() creates INFO severity issue") {
        auto issue = HardwareIssue::info("filament_switch_sensor fs", HardwareType::FILAMENT_SENSOR,
                                         "New sensor discovered");

        REQUIRE(issue.severity == HardwareIssueSeverity::INFO);
        REQUIRE_FALSE(issue.is_optional);
    }
}

// ============================================================================
// HardwareValidationResult Tests
// ============================================================================

TEST_CASE("HardwareValidationResult - Aggregation", "[hardware][validator]") {
    SECTION("has_issues returns false when empty") {
        HardwareValidationResult result;
        REQUIRE_FALSE(result.has_issues());
    }

    SECTION("has_issues returns true with any issues") {
        HardwareValidationResult result;
        result.newly_discovered.push_back(
            HardwareIssue::info("neopixel test", HardwareType::LED, "New LED"));

        REQUIRE(result.has_issues());
    }

    SECTION("has_critical returns true only for critical issues") {
        HardwareValidationResult result;

        // Add warning - not critical
        result.expected_missing.push_back(
            HardwareIssue::warning("fan", HardwareType::FAN, "Missing"));
        REQUIRE_FALSE(result.has_critical());

        // Add critical - now critical
        result.critical_missing.push_back(
            HardwareIssue::critical("extruder", HardwareType::HEATER, "Missing"));
        REQUIRE(result.has_critical());
    }

    SECTION("total_issue_count sums all categories") {
        HardwareValidationResult result;
        result.critical_missing.push_back(
            HardwareIssue::critical("extruder", HardwareType::HEATER, "Missing"));
        result.expected_missing.push_back(
            HardwareIssue::warning("fan", HardwareType::FAN, "Missing"));
        result.expected_missing.push_back(
            HardwareIssue::warning("led", HardwareType::LED, "Missing"));
        result.newly_discovered.push_back(
            HardwareIssue::info("sensor", HardwareType::SENSOR, "New"));

        REQUIRE(result.total_issue_count() == 4);
    }

    SECTION("max_severity returns highest severity") {
        HardwareValidationResult result;

        // Empty = INFO (default)
        REQUIRE(result.max_severity() == HardwareIssueSeverity::INFO);

        // Add info
        result.newly_discovered.push_back(HardwareIssue::info("led", HardwareType::LED, "New"));
        REQUIRE(result.max_severity() == HardwareIssueSeverity::INFO);

        // Add warning - now WARNING
        result.expected_missing.push_back(
            HardwareIssue::warning("fan", HardwareType::FAN, "Missing"));
        REQUIRE(result.max_severity() == HardwareIssueSeverity::WARNING);

        // Add critical - now CRITICAL
        result.critical_missing.push_back(
            HardwareIssue::critical("extruder", HardwareType::HEATER, "Missing"));
        REQUIRE(result.max_severity() == HardwareIssueSeverity::CRITICAL);
    }
}

// ============================================================================
// HardwareValidator Tests
// ============================================================================

TEST_CASE("HardwareValidator - Critical hardware detection", "[hardware][validator]") {
    MoonrakerClientMock client;
    PrinterCapabilities caps;

    SECTION("Detects missing extruder as critical") {
        // Mock client with no extruder
        client.set_heaters({"heater_bed"});

        HardwareValidator validator;
        auto result = validator.validate(nullptr, &client, caps);

        REQUIRE(result.has_critical());
        REQUIRE(result.critical_missing.size() == 1);
        REQUIRE(result.critical_missing[0].hardware_name == "extruder");
    }

    SECTION("No critical issue when extruder exists") {
        client.set_heaters({"extruder", "heater_bed"});

        HardwareValidator validator;
        auto result = validator.validate(nullptr, &client, caps);

        REQUIRE_FALSE(result.has_critical());
    }

    SECTION("Detects extruder with numbered variant") {
        client.set_heaters({"extruder0", "heater_bed"});

        HardwareValidator validator;
        auto result = validator.validate(nullptr, &client, caps);

        REQUIRE_FALSE(result.has_critical());
    }
}

TEST_CASE("HardwareValidator - New hardware discovery", "[hardware][validator]") {
    MoonrakerClientMock client;
    PrinterCapabilities caps;

    SECTION("Suggests LED when discovered but not configured") {
        client.set_heaters({"extruder", "heater_bed"});
        client.set_leds({"neopixel chamber_light"});

        HardwareValidator validator;
        // Pass nullptr for config = no configured LED
        auto result = validator.validate(nullptr, &client, caps);

        // Should suggest the LED
        bool found_led = false;
        for (const auto& issue : result.newly_discovered) {
            if (issue.hardware_type == HardwareType::LED) {
                found_led = true;
                break;
            }
        }
        REQUIRE(found_led);
    }
}

TEST_CASE("HardwareValidator - Session changes", "[hardware][validator]") {
    MoonrakerClientMock client;
    PrinterCapabilities caps;

    SECTION("Detects hardware removed since last session") {
        // Create a "previous" snapshot with LED
        HardwareSnapshot previous;
        previous.heaters = {"extruder", "heater_bed"};
        previous.leds = {"neopixel chamber_light"};

        // Current discovery has no LED
        HardwareSnapshot current;
        current.heaters = {"extruder", "heater_bed"};
        current.leds = {};

        auto removed = previous.get_removed(current);

        REQUIRE(removed.size() == 1);
        REQUIRE(removed[0] == "neopixel chamber_light");
    }
}

TEST_CASE("HardwareValidator - Helper functions", "[hardware][validator]") {
    SECTION("hardware_type_to_string returns correct strings") {
        REQUIRE(std::string(hardware_type_to_string(HardwareType::HEATER)) == "heater");
        REQUIRE(std::string(hardware_type_to_string(HardwareType::SENSOR)) == "sensor");
        REQUIRE(std::string(hardware_type_to_string(HardwareType::FAN)) == "fan");
        REQUIRE(std::string(hardware_type_to_string(HardwareType::LED)) == "led");
        REQUIRE(std::string(hardware_type_to_string(HardwareType::FILAMENT_SENSOR)) ==
                "filament_sensor");
        REQUIRE(std::string(hardware_type_to_string(HardwareType::OTHER)) == "hardware");
    }
}

// ============================================================================
// Integration-style Tests
// ============================================================================

TEST_CASE("HardwareValidator - Full validation scenario", "[hardware][validator]") {
    MoonrakerClientMock client;
    PrinterCapabilities caps;

    SECTION("Healthy printer with all expected hardware") {
        client.set_heaters({"extruder", "heater_bed"});
        client.set_fans({"fan", "heater_fan hotend_fan"});
        client.set_leds({"neopixel chamber_light"});

        HardwareValidator validator;
        auto result = validator.validate(nullptr, &client, caps);

        // No critical issues (extruder present)
        REQUIRE_FALSE(result.has_critical());

        // May have info about new hardware (LED not configured)
        // but no expected_missing since config is null
        REQUIRE(result.expected_missing.empty());
    }

    SECTION("Printer missing extruder reports critical") {
        client.set_heaters({"heater_bed"}); // No extruder!
        client.set_fans({"fan"});

        HardwareValidator validator;
        auto result = validator.validate(nullptr, &client, caps);

        REQUIRE(result.has_critical());
        REQUIRE(result.has_issues());
        REQUIRE(result.max_severity() == HardwareIssueSeverity::CRITICAL);
    }
}

TEST_CASE("HardwareValidator - Snapshot roundtrip", "[hardware][validator]") {
    SECTION("Snapshot survives JSON roundtrip") {
        HardwareSnapshot original;
        original.timestamp = "2025-01-01T12:00:00Z";
        original.heaters = {"extruder", "heater_bed", "heater_generic chamber"};
        original.sensors = {"temperature_sensor raspberry_pi", "temperature_sensor chamber"};
        original.fans = {"fan", "heater_fan hotend_fan", "controller_fan electronics"};
        original.leds = {"neopixel chamber_light", "led status"};
        original.filament_sensors = {"filament_switch_sensor fsensor"};

        // Serialize and deserialize
        json j = original.to_json();
        HardwareSnapshot restored = HardwareSnapshot::from_json(j);

        // Verify all fields match
        REQUIRE(restored.timestamp == original.timestamp);
        REQUIRE(restored.heaters == original.heaters);
        REQUIRE(restored.sensors == original.sensors);
        REQUIRE(restored.fans == original.fans);
        REQUIRE(restored.leds == original.leds);
        REQUIRE(restored.filament_sensors == original.filament_sensors);
    }
}

// ============================================================================
// Config Integration Tests (Optional/Expected Hardware)
// ============================================================================

#include "config.h"

// Test fixture for Config-dependent HardwareValidator tests
// NOTE: After the plural naming refactor, hardware config moves from
// /hardware/ to /printer/hardware/
class HardwareValidatorConfigFixture {
  protected:
    Config config;

    // Helper to check if a JSON pointer path exists in config data
    bool config_contains(const std::string& json_ptr) {
        return config.data.contains(json::json_pointer(json_ptr));
    }

    void setup_empty_hardware_config() {
        // NEW structure: hardware is under /printer/hardware/
        config.data = {{"printer",
                        {{"moonraker_host", "127.0.0.1"},
                         {"moonraker_port", 7125},
                         {"hardware",
                          {{"optional", json::array()},
                           {"expected", json::array()},
                           {"last_snapshot", json::object()}}}}}};
    }

    void setup_hardware_with_optional() {
        // NEW structure: hardware is under /printer/hardware/
        config.data = {{"printer",
                        {{"moonraker_host", "127.0.0.1"},
                         {"moonraker_port", 7125},
                         {"hardware",
                          {{"optional", {"neopixel chamber_light", "fan exhaust"}},
                           {"expected", json::array()},
                           {"last_snapshot", json::object()}}}}}};
    }

    void setup_hardware_with_expected() {
        // NEW structure: hardware is under /printer/hardware/
        config.data = {{"printer",
                        {{"moonraker_host", "127.0.0.1"},
                         {"moonraker_port", 7125},
                         {"hardware",
                          {{"optional", json::array()},
                           {"expected", {"temperature_sensor chamber", "neopixel status"}},
                           {"last_snapshot", json::object()}}}}}};
    }

    void setup_hardware_with_snapshot() {
        // NEW structure: includes last_snapshot for session change detection
        json snapshot = {
            {"timestamp", "2025-01-01T12:00:00Z"},       {"heaters", {"extruder", "heater_bed"}},
            {"sensors", {"temperature_sensor chamber"}}, {"fans", {"fan", "heater_fan hotend_fan"}},
            {"leds", {"neopixel chamber_light"}},        {"filament_sensors", json::array()}};

        config.data = {{"printer",
                        {{"moonraker_host", "127.0.0.1"},
                         {"moonraker_port", 7125},
                         {"hardware",
                          {{"optional", json::array()},
                           {"expected", json::array()},
                           {"last_snapshot", snapshot}}}}}};
    }
};

TEST_CASE_METHOD(HardwareValidatorConfigFixture,
                 "HardwareValidator - is_hardware_optional with empty config",
                 "[hardware][validator][config]") {
    setup_empty_hardware_config();

    REQUIRE_FALSE(HardwareValidator::is_hardware_optional(&config, "neopixel chamber_light"));
    REQUIRE_FALSE(HardwareValidator::is_hardware_optional(&config, "anything"));
}

TEST_CASE_METHOD(HardwareValidatorConfigFixture,
                 "HardwareValidator - is_hardware_optional detects optional hardware",
                 "[hardware][validator][config]") {
    setup_hardware_with_optional();

    REQUIRE(HardwareValidator::is_hardware_optional(&config, "neopixel chamber_light"));
    REQUIRE(HardwareValidator::is_hardware_optional(&config, "fan exhaust"));
    REQUIRE_FALSE(HardwareValidator::is_hardware_optional(&config, "not_in_list"));
}

TEST_CASE_METHOD(HardwareValidatorConfigFixture,
                 "HardwareValidator - set_hardware_optional adds to list",
                 "[hardware][validator][config]") {
    setup_empty_hardware_config();

    // Initially not optional
    REQUIRE_FALSE(HardwareValidator::is_hardware_optional(&config, "new_hardware"));

    // Mark as optional
    HardwareValidator::set_hardware_optional(&config, "new_hardware", true);

    // Now should be optional
    REQUIRE(HardwareValidator::is_hardware_optional(&config, "new_hardware"));
}

TEST_CASE_METHOD(HardwareValidatorConfigFixture,
                 "HardwareValidator - set_hardware_optional removes from list",
                 "[hardware][validator][config]") {
    setup_hardware_with_optional();

    // Initially optional
    REQUIRE(HardwareValidator::is_hardware_optional(&config, "neopixel chamber_light"));

    // Unmark as optional
    HardwareValidator::set_hardware_optional(&config, "neopixel chamber_light", false);

    // Should no longer be optional
    REQUIRE_FALSE(HardwareValidator::is_hardware_optional(&config, "neopixel chamber_light"));
}

TEST_CASE_METHOD(HardwareValidatorConfigFixture,
                 "HardwareValidator - set_hardware_optional handles duplicates",
                 "[hardware][validator][config]") {
    setup_empty_hardware_config();

    // Add twice - should only appear once
    HardwareValidator::set_hardware_optional(&config, "test_hw", true);
    HardwareValidator::set_hardware_optional(&config, "test_hw", true);

    // Check the list only has one entry
    // NEW path: /printer/hardware/optional (was /hardware/optional)
    json& optional_list = config.get_json("/printer/hardware/optional");
    int count = 0;
    for (const auto& item : optional_list) {
        if (item == "test_hw") {
            count++;
        }
    }
    REQUIRE(count == 1);
}

TEST_CASE_METHOD(HardwareValidatorConfigFixture,
                 "HardwareValidator - add_expected_hardware adds to list",
                 "[hardware][validator][config]") {
    setup_empty_hardware_config();

    // Add expected hardware
    HardwareValidator::add_expected_hardware(&config, "temperature_sensor chamber");

    // Verify it's in the list
    // NEW path: /printer/hardware/expected (was /hardware/expected)
    json& expected_list = config.get_json("/printer/hardware/expected");
    bool found = false;
    for (const auto& item : expected_list) {
        if (item == "temperature_sensor chamber") {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE_METHOD(HardwareValidatorConfigFixture,
                 "HardwareValidator - add_expected_hardware handles duplicates",
                 "[hardware][validator][config]") {
    setup_empty_hardware_config();

    // Add same hardware twice
    HardwareValidator::add_expected_hardware(&config, "neopixel test");
    HardwareValidator::add_expected_hardware(&config, "neopixel test");

    // Check the list only has one entry
    // NEW path: /printer/hardware/expected (was /hardware/expected)
    json& expected_list = config.get_json("/printer/hardware/expected");
    int count = 0;
    for (const auto& item : expected_list) {
        if (item == "neopixel test") {
            count++;
        }
    }
    REQUIRE(count == 1);
}

TEST_CASE_METHOD(HardwareValidatorConfigFixture,
                 "HardwareValidator - add_expected_hardware ignores empty names",
                 "[hardware][validator][config]") {
    setup_empty_hardware_config();

    // Try to add empty name - should be ignored
    HardwareValidator::add_expected_hardware(&config, "");

    // NEW path: /printer/hardware/expected (was /hardware/expected)
    json& expected_list = config.get_json("/printer/hardware/expected");
    REQUIRE(expected_list.empty());
}

TEST_CASE_METHOD(HardwareValidatorConfigFixture,
                 "HardwareValidator - handles nullptr config gracefully",
                 "[hardware][validator][config]") {
    // These should not crash with nullptr
    REQUIRE_FALSE(HardwareValidator::is_hardware_optional(nullptr, "anything"));

    // These should be no-ops with nullptr (no crash)
    HardwareValidator::set_hardware_optional(nullptr, "test", true);
    HardwareValidator::add_expected_hardware(nullptr, "test");

    // If we got here without crashing, the test passes
    REQUIRE(true);
}

// ============================================================================
// Hardware Path Structure Tests - NEW /printer/hardware/ paths
// These tests define the contract for the refactored hardware config paths.
// They SHOULD FAIL until the implementation is updated.
// ============================================================================

TEST_CASE_METHOD(HardwareValidatorConfigFixture,
                 "HardwareValidator - optional hardware uses /printer/hardware/optional path",
                 "[hardware][validator][config][paths][plural]") {
    setup_empty_hardware_config();

    // Mark hardware as optional - should write to /printer/hardware/optional
    HardwareValidator::set_hardware_optional(&config, "test_led", true);

    // Verify the path is /printer/hardware/optional (not /hardware/optional)
    REQUIRE(config_contains("/printer/hardware/optional"));
    json& optional_list = config.get_json("/printer/hardware/optional");
    REQUIRE(optional_list.is_array());

    bool found = false;
    for (const auto& item : optional_list) {
        if (item == "test_led") {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE_METHOD(HardwareValidatorConfigFixture,
                 "HardwareValidator - expected hardware uses /printer/hardware/expected path",
                 "[hardware][validator][config][paths][plural]") {
    setup_empty_hardware_config();

    // Add expected hardware - should write to /printer/hardware/expected
    HardwareValidator::add_expected_hardware(&config, "temperature_sensor test");

    // Verify the path is /printer/hardware/expected (not /hardware/expected)
    REQUIRE(config_contains("/printer/hardware/expected"));
    json& expected_list = config.get_json("/printer/hardware/expected");
    REQUIRE(expected_list.is_array());

    bool found = false;
    for (const auto& item : expected_list) {
        if (item == "temperature_sensor test") {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE_METHOD(HardwareValidatorConfigFixture,
                 "HardwareValidator - last_snapshot uses /printer/hardware/last_snapshot path",
                 "[hardware][validator][config][paths][plural]") {
    setup_hardware_with_snapshot();

    // Verify the path is /printer/hardware/last_snapshot (not /hardware_session/last_snapshot)
    REQUIRE(config_contains("/printer/hardware/last_snapshot"));
    json& snapshot = config.get_json("/printer/hardware/last_snapshot");
    REQUIRE(snapshot.is_object());
    REQUIRE(snapshot.contains("timestamp"));
    REQUIRE(snapshot.contains("heaters"));
    REQUIRE(snapshot.contains("fans"));
}

TEST_CASE_METHOD(HardwareValidatorConfigFixture,
                 "HardwareValidator - hardware section is under /printer/ not root",
                 "[hardware][validator][config][paths][plural]") {
    setup_empty_hardware_config();

    // The hardware section should be under /printer/, not at root level
    REQUIRE(config_contains("/printer/hardware"));
    REQUIRE_FALSE(config_contains("/hardware"));

    json& hardware = config.get_json("/printer/hardware");
    REQUIRE(hardware.is_object());
    REQUIRE(hardware.contains("optional"));
    REQUIRE(hardware.contains("expected"));
    REQUIRE(hardware.contains("last_snapshot"));
}
