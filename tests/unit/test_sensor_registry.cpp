// SPDX-License-Identifier: GPL-3.0-or-later
#include "sensor_registry.h"

#include "../catch_amalgamated.hpp"

using namespace helix::sensors;

// Mock sensor manager for testing
class MockSensorManager : public ISensorManager {
  public:
    std::string name_;
    bool discovered_ = false;
    bool status_updated_ = false;
    bool config_loaded_ = false;
    nlohmann::json last_status_;
    nlohmann::json last_config_;
    nlohmann::json saved_config_;
    std::vector<std::string> discovered_objects_;

    explicit MockSensorManager(std::string name) : name_(std::move(name)) {}

    std::string category_name() const override { return name_; }

    void discover(const std::vector<std::string>& objects) override {
        discovered_ = true;
        discovered_objects_ = objects;
    }

    void update_from_status(const nlohmann::json& status) override {
        status_updated_ = true;
        last_status_ = status;
    }

    void load_config(const nlohmann::json& config) override {
        config_loaded_ = true;
        last_config_ = config;
    }

    nlohmann::json save_config() const override { return saved_config_; }
};

TEST_CASE("SensorRegistry registers managers", "[sensors]") {
    SensorRegistry registry;

    auto mock = std::make_unique<MockSensorManager>("test");
    auto* mock_ptr = mock.get();

    registry.register_manager("test", std::move(mock));

    REQUIRE(registry.get_manager("test") == mock_ptr);
    REQUIRE(registry.get_manager("nonexistent") == nullptr);
}

TEST_CASE("SensorRegistry ignores null manager registration", "[sensors]") {
    SensorRegistry registry;
    registry.register_manager("test", nullptr);
    REQUIRE(registry.get_manager("test") == nullptr);
}

TEST_CASE("SensorRegistry replaces manager with same category", "[sensors]") {
    SensorRegistry registry;

    auto mock1 = std::make_unique<MockSensorManager>("test");
    auto mock2 = std::make_unique<MockSensorManager>("test");
    auto* ptr2 = mock2.get();

    registry.register_manager("test", std::move(mock1));
    registry.register_manager("test", std::move(mock2));

    // Second registration should replace the first
    REQUIRE(registry.get_manager("test") == ptr2);
}

TEST_CASE("SensorRegistry routes discover to all managers", "[sensors]") {
    SensorRegistry registry;

    auto mock1 = std::make_unique<MockSensorManager>("sensor1");
    auto mock2 = std::make_unique<MockSensorManager>("sensor2");
    auto* ptr1 = mock1.get();
    auto* ptr2 = mock2.get();

    registry.register_manager("sensor1", std::move(mock1));
    registry.register_manager("sensor2", std::move(mock2));

    std::vector<std::string> objects = {"filament_switch_sensor foo", "probe bar"};
    registry.discover_all(objects);

    REQUIRE(ptr1->discovered_);
    REQUIRE(ptr2->discovered_);
    REQUIRE(ptr1->discovered_objects_ == objects);
    REQUIRE(ptr2->discovered_objects_ == objects);
}

TEST_CASE("SensorRegistry handles empty klipper_objects", "[sensors]") {
    SensorRegistry registry;
    auto mock = std::make_unique<MockSensorManager>("test");
    auto* mock_ptr = mock.get();
    registry.register_manager("test", std::move(mock));

    // Should not crash with empty objects
    registry.discover_all({});

    REQUIRE(mock_ptr->discovered_);
    REQUIRE(mock_ptr->discovered_objects_.empty());
}

TEST_CASE("SensorRegistry routes status updates to all managers", "[sensors]") {
    SensorRegistry registry;

    auto mock = std::make_unique<MockSensorManager>("test");
    auto* mock_ptr = mock.get();

    registry.register_manager("test", std::move(mock));

    nlohmann::json status = {{"filament_switch_sensor foo", {{"filament_detected", true}}}};
    registry.update_all_from_status(status);

    REQUIRE(mock_ptr->status_updated_);
    REQUIRE(mock_ptr->last_status_ == status);
}

TEST_CASE("SensorRegistry handles empty status update", "[sensors]") {
    SensorRegistry registry;
    auto mock = std::make_unique<MockSensorManager>("test");
    auto* mock_ptr = mock.get();
    registry.register_manager("test", std::move(mock));

    // Should not crash with empty status
    registry.update_all_from_status({});

    REQUIRE(mock_ptr->status_updated_);
}

TEST_CASE("SensorRegistry load_config routes to correct managers", "[sensors]") {
    SensorRegistry registry;

    auto mock1 = std::make_unique<MockSensorManager>("switch");
    auto mock2 = std::make_unique<MockSensorManager>("humidity");
    auto* ptr1 = mock1.get();
    auto* ptr2 = mock2.get();

    registry.register_manager("switch", std::move(mock1));
    registry.register_manager("humidity", std::move(mock2));

    nlohmann::json config = {
        {"sensors",
         {{"switch", {{"master_enabled", true}}}, {"humidity", {{"threshold", 60}}}}}};

    registry.load_config(config);

    REQUIRE(ptr1->config_loaded_);
    REQUIRE(ptr2->config_loaded_);
    REQUIRE(ptr1->last_config_ == nlohmann::json({{"master_enabled", true}}));
    REQUIRE(ptr2->last_config_ == nlohmann::json({{"threshold", 60}}));
}

TEST_CASE("SensorRegistry load_config handles missing sensors key", "[sensors]") {
    SensorRegistry registry;
    auto mock = std::make_unique<MockSensorManager>("test");
    auto* mock_ptr = mock.get();
    registry.register_manager("test", std::move(mock));

    // Config without "sensors" key
    nlohmann::json config = {{"other_setting", "value"}};
    registry.load_config(config);

    // Should not crash, and config should not be loaded
    REQUIRE_FALSE(mock_ptr->config_loaded_);
}

TEST_CASE("SensorRegistry load_config handles missing category", "[sensors]") {
    SensorRegistry registry;
    auto mock = std::make_unique<MockSensorManager>("test");
    auto* mock_ptr = mock.get();
    registry.register_manager("test", std::move(mock));

    // Config with sensors but not our category
    nlohmann::json config = {{"sensors", {{"other_category", {{"value", 1}}}}}};
    registry.load_config(config);

    // Should not crash, and config should not be loaded for our manager
    REQUIRE_FALSE(mock_ptr->config_loaded_);
}

TEST_CASE("SensorRegistry save_config aggregates from all managers", "[sensors]") {
    SensorRegistry registry;

    auto mock1 = std::make_unique<MockSensorManager>("switch");
    auto mock2 = std::make_unique<MockSensorManager>("humidity");

    mock1->saved_config_ = {{"master_enabled", true}, {"sensors", nlohmann::json::array()}};
    mock2->saved_config_ = {{"threshold", 60}};

    registry.register_manager("switch", std::move(mock1));
    registry.register_manager("humidity", std::move(mock2));

    nlohmann::json result = registry.save_config();

    REQUIRE(result.contains("sensors"));
    REQUIRE(result["sensors"].contains("switch"));
    REQUIRE(result["sensors"].contains("humidity"));
    REQUIRE(result["sensors"]["switch"]["master_enabled"] == true);
    REQUIRE(result["sensors"]["humidity"]["threshold"] == 60);
}

TEST_CASE("SensorRegistry save_config handles empty registry", "[sensors]") {
    SensorRegistry registry;

    nlohmann::json result = registry.save_config();

    REQUIRE(result.contains("sensors"));
    REQUIRE(result["sensors"].empty());
}
