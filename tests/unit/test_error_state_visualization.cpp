// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_afc.h"
#include "ams_backend_happy_hare.h"
#include "ams_types.h"

#include <algorithm>
#include <vector>

#include "../catch_amalgamated.hpp"

// ============================================================================
// Task 1: Data Model Tests — SlotError, BufferHealth, SlotInfo, AmsUnit
// ============================================================================

TEST_CASE("SlotError construction with default severity", "[ams][error_state]") {
    SlotError err;
    err.message = "Lane 1 load failed";

    REQUIRE(err.message == "Lane 1 load failed");
    REQUIRE(err.severity == SlotError::ERROR); // Default severity
}

TEST_CASE("SlotError severity levels", "[ams][error_state]") {
    SlotError info_err;
    info_err.message = "Buffer trailing";
    info_err.severity = SlotError::INFO;
    REQUIRE(info_err.severity == SlotError::INFO);

    SlotError warn_err;
    warn_err.message = "Buffer approaching fault";
    warn_err.severity = SlotError::WARNING;
    REQUIRE(warn_err.severity == SlotError::WARNING);

    SlotError error_err;
    error_err.message = "Lane error";
    error_err.severity = SlotError::ERROR;
    REQUIRE(error_err.severity == SlotError::ERROR);
}

TEST_CASE("BufferHealth defaults", "[ams][error_state]") {
    BufferHealth health;

    REQUIRE(health.fault_detection_enabled == false);
    REQUIRE(health.distance_to_fault == 0.0f);
    REQUIRE(health.state.empty());
}

TEST_CASE("BufferHealth with values", "[ams][error_state]") {
    BufferHealth health;
    health.fault_detection_enabled = true;
    health.distance_to_fault = 42.5f;
    health.state = "Advancing";

    REQUIRE(health.fault_detection_enabled == true);
    REQUIRE(health.distance_to_fault == Catch::Approx(42.5f));
    REQUIRE(health.state == "Advancing");
}

TEST_CASE("SlotInfo with no error", "[ams][error_state]") {
    SlotInfo slot;
    slot.slot_index = 0;
    slot.status = SlotStatus::AVAILABLE;

    REQUIRE_FALSE(slot.error.has_value());
}

TEST_CASE("SlotInfo with error", "[ams][error_state]") {
    SlotInfo slot;
    slot.slot_index = 0;
    slot.status = SlotStatus::AVAILABLE;

    SlotError err;
    err.message = "Lane error";
    err.severity = SlotError::ERROR;
    slot.error = err;

    REQUIRE(slot.error.has_value());
    REQUIRE(slot.error->message == "Lane error");
    REQUIRE(slot.error->severity == SlotError::ERROR);
}

TEST_CASE("AmsUnit with buffer health", "[ams][error_state]") {
    AmsUnit unit;
    unit.unit_index = 0;

    BufferHealth health;
    health.fault_detection_enabled = true;
    health.distance_to_fault = 10.0f;
    health.state = "Trailing";
    unit.buffer_health = health;

    REQUIRE(unit.buffer_health.has_value());
    REQUIRE(unit.buffer_health->fault_detection_enabled == true);
    REQUIRE(unit.buffer_health->distance_to_fault == Catch::Approx(10.0f));
    REQUIRE(unit.buffer_health->state == "Trailing");
}

TEST_CASE("SlotInfo error can be cleared", "[ams][error_state]") {
    SlotInfo slot;
    slot.error = SlotError{"some error", SlotError::ERROR};

    REQUIRE(slot.error.has_value());

    slot.error.reset();

    REQUIRE_FALSE(slot.error.has_value());
}

TEST_CASE("AmsUnit::has_any_error with no errors", "[ams][error_state]") {
    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;

    for (int i = 0; i < 4; ++i) {
        SlotInfo slot;
        slot.slot_index = i;
        slot.status = SlotStatus::AVAILABLE;
        unit.slots.push_back(slot);
    }

    REQUIRE_FALSE(unit.has_any_error());
}

TEST_CASE("AmsUnit::has_any_error with one slot in error", "[ams][error_state]") {
    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;

    for (int i = 0; i < 4; ++i) {
        SlotInfo slot;
        slot.slot_index = i;
        slot.status = SlotStatus::AVAILABLE;
        if (i == 2) {
            slot.error = SlotError{"Lane 3 error", SlotError::ERROR};
        }
        unit.slots.push_back(slot);
    }

    REQUIRE(unit.has_any_error());
}

TEST_CASE("AmsUnit::has_any_error with mixed error states", "[ams][error_state]") {
    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;

    for (int i = 0; i < 4; ++i) {
        SlotInfo slot;
        slot.slot_index = i;
        slot.status = SlotStatus::AVAILABLE;
        if (i == 1) {
            slot.error = SlotError{"Warning on lane 2", SlotError::WARNING};
        }
        if (i == 3) {
            slot.error = SlotError{"Error on lane 4", SlotError::ERROR};
        }
        unit.slots.push_back(slot);
    }

    REQUIRE(unit.has_any_error());
}

TEST_CASE("AmsUnit::has_any_error with empty slots vector", "[ams][error_state]") {
    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 0;

    REQUIRE_FALSE(unit.has_any_error());
}

// ============================================================================
// Task 2: AFC Backend — Slot Errors from Lane Status
// ============================================================================

// Re-use the test helper from test_ams_backend_afc.cpp pattern
class AfcErrorStateHelper : public AmsBackendAfc {
  public:
    AfcErrorStateHelper() : AmsBackendAfc(nullptr, nullptr) {}

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

        // Initialize lane sensors
        for (int i = 0; i < 16; ++i) {
            lane_sensors_[i] = LaneSensors{};
        }
    }

    void feed_status_update(const nlohmann::json& params_inner) {
        nlohmann::json notification;
        notification["params"] = nlohmann::json::array({params_inner, 0.0});
        handle_status_update(notification);
    }

    void feed_afc_state(const nlohmann::json& afc_data) {
        nlohmann::json params;
        params["AFC"] = afc_data;
        feed_status_update(params);
    }

    void feed_afc_stepper(const std::string& lane_name, const nlohmann::json& data) {
        nlohmann::json params;
        params["AFC_stepper " + lane_name] = data;
        feed_status_update(params);
    }

    void feed_afc_buffer(const std::string& buffer_name, const nlohmann::json& data) {
        nlohmann::json params;
        params["AFC_buffer " + buffer_name] = data;
        feed_status_update(params);
    }

    void set_buffer_names(const std::vector<std::string>& names) {
        buffer_names_ = names;
    }

    const SlotInfo* get_slot(int idx) const {
        return system_info_.get_slot_global(idx);
    }

    SlotInfo* get_slot_mut(int idx) {
        return system_info_.get_slot_global(idx);
    }

    AmsSystemInfo& get_system_info_mut() {
        return system_info_;
    }
};

TEST_CASE("AFC lane error: Error status populates slot.error", "[ams][afc][error_state]") {
    AfcErrorStateHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // Feed lane1 with Error status
    nlohmann::json lane_data;
    lane_data["status"] = "Error";
    lane_data["prep"] = true;
    lane_data["load"] = false;
    helper.feed_afc_stepper("lane1", lane_data);

    const auto* slot = helper.get_slot(0);
    REQUIRE(slot != nullptr);
    REQUIRE(slot->error.has_value());
    REQUIRE(slot->error->severity == SlotError::ERROR);
}

TEST_CASE("AFC lane error: default message when no system message", "[ams][afc][error_state]") {
    AfcErrorStateHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    nlohmann::json lane_data;
    lane_data["status"] = "Error";
    helper.feed_afc_stepper("lane1", lane_data);

    const auto* slot = helper.get_slot(0);
    REQUIRE(slot != nullptr);
    REQUIRE(slot->error.has_value());
    REQUIRE(slot->error->message == "Lane error");
}

TEST_CASE("AFC lane error: message flows from system message", "[ams][afc][error_state]") {
    AfcErrorStateHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // First feed system-level message
    nlohmann::json afc_state;
    afc_state["message"] = {{"message", "Lane 1 load failed: filament jam"}, {"type", "error"}};
    helper.feed_afc_state(afc_state);

    // Then feed lane error
    nlohmann::json lane_data;
    lane_data["status"] = "Error";
    helper.feed_afc_stepper("lane1", lane_data);

    const auto* slot = helper.get_slot(0);
    REQUIRE(slot != nullptr);
    REQUIRE(slot->error.has_value());
    REQUIRE(slot->error->message == "Lane 1 load failed: filament jam");
    REQUIRE(slot->error->severity == SlotError::ERROR);
}

TEST_CASE("AFC lane error: severity from system message type", "[ams][afc][error_state]") {
    AfcErrorStateHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // Feed system warning message
    nlohmann::json afc_state;
    afc_state["message"] = {{"message", "Low filament detected"}, {"type", "warning"}};
    helper.feed_afc_state(afc_state);

    nlohmann::json lane_data;
    lane_data["status"] = "Error";
    helper.feed_afc_stepper("lane1", lane_data);

    const auto* slot = helper.get_slot(0);
    REQUIRE(slot != nullptr);
    REQUIRE(slot->error.has_value());
    REQUIRE(slot->error->severity == SlotError::WARNING);
}

TEST_CASE("AFC lane error: cleared when status leaves Error", "[ams][afc][error_state]") {
    AfcErrorStateHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    // Put lane into error
    nlohmann::json lane_error;
    lane_error["status"] = "Error";
    helper.feed_afc_stepper("lane1", lane_error);

    const auto* slot = helper.get_slot(0);
    REQUIRE(slot->error.has_value());

    // Lane recovers to normal
    nlohmann::json lane_ready;
    lane_ready["status"] = "Ready";
    lane_ready["prep"] = true;
    helper.feed_afc_stepper("lane1", lane_ready);

    REQUIRE_FALSE(slot->error.has_value());
}

TEST_CASE("AFC lane error: only errored lane gets error, not others", "[ams][afc][error_state]") {
    AfcErrorStateHelper helper;
    helper.initialize_test_lanes_with_slots(4);

    nlohmann::json lane_error;
    lane_error["status"] = "Error";
    helper.feed_afc_stepper("lane2", lane_error);

    // Lane 2 (index 1) should have error
    REQUIRE(helper.get_slot(1)->error.has_value());

    // Other lanes should NOT have error
    REQUIRE_FALSE(helper.get_slot(0)->error.has_value());
    REQUIRE_FALSE(helper.get_slot(2)->error.has_value());
    REQUIRE_FALSE(helper.get_slot(3)->error.has_value());
}

// ============================================================================
// Task 3: AFC Backend — Buffer Health Parsing (unit-level)
// ============================================================================

TEST_CASE("AFC buffer health: parsed to unit level from buffer update",
          "[ams][afc][buffer_health]") {
    AfcErrorStateHelper helper;
    helper.initialize_test_lanes_with_slots(4);
    helper.set_buffer_names({"Turtle_1"});

    nlohmann::json buffer_data;
    buffer_data["fault_detection_enabled"] = true;
    buffer_data["distance_to_fault"] = 25.5f;
    buffer_data["state"] = "Advancing";
    buffer_data["lanes"] = nlohmann::json::array({"lane1", "lane2", "lane3", "lane4"});
    helper.feed_afc_buffer("Turtle_1", buffer_data);

    // Buffer health should be set on the unit, not per-slot
    const auto& info = helper.get_system_info();
    REQUIRE(info.units.size() == 1);
    REQUIRE(info.units[0].buffer_health.has_value());
    REQUIRE(info.units[0].buffer_health->fault_detection_enabled == true);
    REQUIRE(info.units[0].buffer_health->distance_to_fault == Catch::Approx(25.5f));
    REQUIRE(info.units[0].buffer_health->state == "Advancing");

    // Slots should NOT have buffer health (it's unit-level now)
    REQUIRE_FALSE(helper.get_slot(0)->error.has_value());
}

TEST_CASE("AFC buffer health: no fault when distance_to_fault is 0", "[ams][afc][buffer_health]") {
    AfcErrorStateHelper helper;
    helper.initialize_test_lanes_with_slots(4);
    helper.set_buffer_names({"Turtle_1"});

    nlohmann::json buffer_data;
    buffer_data["fault_detection_enabled"] = true;
    buffer_data["distance_to_fault"] = 0.0f;
    buffer_data["state"] = "Advancing";
    buffer_data["lanes"] = nlohmann::json::array({"lane1", "lane2"});
    helper.feed_afc_buffer("Turtle_1", buffer_data);

    const auto& info = helper.get_system_info();
    REQUIRE(info.units[0].buffer_health.has_value());
    REQUIRE(info.units[0].buffer_health->distance_to_fault == Catch::Approx(0.0f));
}

TEST_CASE("AFC buffer health: fault_detection_enabled false stored on unit",
          "[ams][afc][buffer_health]") {
    AfcErrorStateHelper helper;
    helper.initialize_test_lanes_with_slots(4);
    helper.set_buffer_names({"Turtle_1"});

    nlohmann::json buffer_data;
    buffer_data["fault_detection_enabled"] = false;
    buffer_data["distance_to_fault"] = 5.0f;
    buffer_data["state"] = "Trailing";
    buffer_data["lanes"] = nlohmann::json::array({"lane1"});
    helper.feed_afc_buffer("Turtle_1", buffer_data);

    const auto& info = helper.get_system_info();
    REQUIRE(info.units[0].buffer_health.has_value());
    REQUIRE(info.units[0].buffer_health->fault_detection_enabled == false);
}

// ============================================================================
// Task 4: Happy Hare Backend — Slot Errors from System Error
// ============================================================================

class HappyHareErrorStateHelper : public AmsBackendHappyHare {
  public:
    HappyHareErrorStateHelper() : AmsBackendHappyHare(nullptr, nullptr) {}

    void initialize_test_gates(int count) {
        system_info_.units.clear();

        AmsUnit unit;
        unit.unit_index = 0;
        unit.name = "Happy Hare MMU";
        unit.slot_count = count;
        unit.first_slot_global_index = 0;

        for (int i = 0; i < count; ++i) {
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
        gates_initialized_ = true;
    }

    void feed_status_update(const nlohmann::json& params_inner) {
        nlohmann::json notification;
        notification["params"] = nlohmann::json::array({params_inner, 0.0});
        handle_status_update(notification);
    }

    void feed_mmu_state(const nlohmann::json& mmu_data) {
        nlohmann::json params;
        params["mmu"] = mmu_data;
        feed_status_update(params);
    }

    const SlotInfo* get_slot(int idx) const {
        return system_info_.get_slot_global(idx);
    }

    AmsAction get_action() const {
        return system_info_.action;
    }

    AmsError execute_gcode(const std::string& /*gcode*/) override {
        return AmsErrorHelper::success();
    }
};

TEST_CASE("Happy Hare error: system error sets slot.error on current_slot",
          "[ams][happy_hare][error_state]") {
    HappyHareErrorStateHelper helper;
    helper.initialize_test_gates(4);

    // Set current slot to gate 2
    nlohmann::json idle_state;
    idle_state["gate"] = 2;
    idle_state["action"] = "Idle";
    helper.feed_mmu_state(idle_state);

    // Transition to error
    nlohmann::json error_state;
    error_state["action"] = "Error";
    helper.feed_mmu_state(error_state);

    const auto* slot = helper.get_slot(2);
    REQUIRE(slot != nullptr);
    REQUIRE(slot->error.has_value());
    REQUIRE(slot->error->severity == SlotError::ERROR);
}

TEST_CASE("Happy Hare error: error cleared on IDLE transition", "[ams][happy_hare][error_state]") {
    HappyHareErrorStateHelper helper;
    helper.initialize_test_gates(4);

    // Set gate and enter error
    nlohmann::json setup;
    setup["gate"] = 1;
    setup["action"] = "Idle";
    helper.feed_mmu_state(setup);

    nlohmann::json error_state;
    error_state["action"] = "Error";
    helper.feed_mmu_state(error_state);

    REQUIRE(helper.get_slot(1)->error.has_value());

    // Recover to idle
    nlohmann::json idle_state;
    idle_state["action"] = "Idle";
    helper.feed_mmu_state(idle_state);

    REQUIRE_FALSE(helper.get_slot(1)->error.has_value());
}

TEST_CASE("Happy Hare error: error message from operation_detail",
          "[ams][happy_hare][error_state]") {
    HappyHareErrorStateHelper helper;
    helper.initialize_test_gates(4);

    nlohmann::json setup;
    setup["gate"] = 0;
    setup["action"] = "Idle";
    helper.feed_mmu_state(setup);

    nlohmann::json error_state;
    error_state["action"] = "Error";
    helper.feed_mmu_state(error_state);

    const auto* slot = helper.get_slot(0);
    REQUIRE(slot != nullptr);
    REQUIRE(slot->error.has_value());
    // The message should come from the action string (operation_detail)
    REQUIRE_FALSE(slot->error->message.empty());
}

TEST_CASE("Happy Hare error: only current_slot gets error, not all slots",
          "[ams][happy_hare][error_state]") {
    HappyHareErrorStateHelper helper;
    helper.initialize_test_gates(4);

    nlohmann::json setup;
    setup["gate"] = 2;
    setup["action"] = "Idle";
    helper.feed_mmu_state(setup);

    nlohmann::json error_state;
    error_state["action"] = "Error";
    helper.feed_mmu_state(error_state);

    // Only slot 2 should have error
    REQUIRE(helper.get_slot(2)->error.has_value());
    REQUIRE_FALSE(helper.get_slot(0)->error.has_value());
    REQUIRE_FALSE(helper.get_slot(1)->error.has_value());
    REQUIRE_FALSE(helper.get_slot(3)->error.has_value());
}

TEST_CASE("Happy Hare error: reason_for_pause used as error message when available",
          "[ams][happy_hare][error_state]") {
    HappyHareErrorStateHelper helper;
    helper.initialize_test_gates(4);

    nlohmann::json setup;
    setup["gate"] = 0;
    setup["action"] = "Idle";
    setup["reason_for_pause"] = "";
    helper.feed_mmu_state(setup);

    // Feed reason_for_pause before error
    nlohmann::json reason;
    reason["reason_for_pause"] = "Filament not detected at extruder after load";
    helper.feed_mmu_state(reason);

    nlohmann::json error_state;
    error_state["action"] = "Error";
    helper.feed_mmu_state(error_state);

    const auto* slot = helper.get_slot(0);
    REQUIRE(slot != nullptr);
    REQUIRE(slot->error.has_value());
    REQUIRE(slot->error->message == "Filament not detected at extruder after load");
}

TEST_CASE("Happy Hare error: no slot error when no gate selected",
          "[ams][happy_hare][error_state]") {
    HappyHareErrorStateHelper helper;
    helper.initialize_test_gates(4);

    // No gate set (default is -1)
    nlohmann::json error_state;
    error_state["action"] = "Error";
    helper.feed_mmu_state(error_state);

    // No slot should have error since current_slot is -1
    for (int i = 0; i < 4; ++i) {
        REQUIRE_FALSE(helper.get_slot(i)->error.has_value());
    }
}
