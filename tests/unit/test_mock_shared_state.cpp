// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_mock_shared_state.cpp
 * @brief Unit tests for MockPrinterState shared state between mocks
 *
 * Tests that MoonrakerClientMock and MoonrakerAPIMock share consistent state
 * when configured with a common MockPrinterState instance.
 *
 * Test Categories:
 * 1. Basic MockPrinterState operations (get/set/clear)
 * 2. Excluded objects synchronization between mocks
 * 3. Print start clearing excluded objects
 * 4. Restart clearing excluded objects
 * 5. Backward compatibility (mocks work without shared state)
 */

#include "../catch_amalgamated.hpp"
#include "../mocks/mock_printer_state.h"
#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"

#include <memory>
#include <thread>

// ============================================================================
// MockPrinterState Unit Tests
// ============================================================================

TEST_CASE("MockPrinterState basic operations", "[mock][shared_state]") {
    MockPrinterState state;

    SECTION("Initial state has no excluded objects") {
        auto excluded = state.get_excluded_objects();
        REQUIRE(excluded.empty());
    }

    SECTION("Add excluded object") {
        state.add_excluded_object("Part_1");
        auto excluded = state.get_excluded_objects();
        REQUIRE(excluded.size() == 1);
        REQUIRE(excluded.count("Part_1") == 1);
    }

    SECTION("Add multiple excluded objects") {
        state.add_excluded_object("Part_1");
        state.add_excluded_object("Part_2");
        state.add_excluded_object("Part_3");

        auto excluded = state.get_excluded_objects();
        REQUIRE(excluded.size() == 3);
        REQUIRE(excluded.count("Part_1") == 1);
        REQUIRE(excluded.count("Part_2") == 1);
        REQUIRE(excluded.count("Part_3") == 1);
    }

    SECTION("Duplicate excluded object is ignored (set behavior)") {
        state.add_excluded_object("Part_1");
        state.add_excluded_object("Part_1");

        auto excluded = state.get_excluded_objects();
        REQUIRE(excluded.size() == 1);
    }

    SECTION("Clear excluded objects") {
        state.add_excluded_object("Part_1");
        state.add_excluded_object("Part_2");
        state.clear_excluded_objects();

        auto excluded = state.get_excluded_objects();
        REQUIRE(excluded.empty());
    }

    SECTION("Available objects") {
        std::vector<std::string> objects = {"Object_A", "Object_B", "Object_C"};
        state.set_available_objects(objects);

        auto retrieved = state.get_available_objects();
        REQUIRE(retrieved.size() == 3);
        REQUIRE(retrieved[0] == "Object_A");
        REQUIRE(retrieved[1] == "Object_B");
        REQUIRE(retrieved[2] == "Object_C");
    }

    SECTION("Reset clears all state") {
        state.extruder_temp = 200.0;
        state.bed_temp = 60.0;
        state.print_state = 1;
        state.add_excluded_object("Part_1");
        state.set_available_objects({"Object_A"});
        state.set_current_filename("test.gcode");

        state.reset();

        REQUIRE(state.extruder_temp == 25.0);
        REQUIRE(state.bed_temp == 25.0);
        REQUIRE(state.print_state == 0);
        REQUIRE(state.get_excluded_objects().empty());
        REQUIRE(state.get_available_objects().empty());
        REQUIRE(state.get_current_filename().empty());
    }
}

TEST_CASE("MockPrinterState thread safety", "[mock][shared_state][threading]") {
    MockPrinterState state;

    SECTION("Concurrent reads and writes to excluded objects") {
        // Start multiple threads adding objects
        std::vector<std::thread> writers;
        for (int i = 0; i < 10; i++) {
            writers.emplace_back([&state, i]() {
                for (int j = 0; j < 100; j++) {
                    state.add_excluded_object("Part_" + std::to_string(i) + "_" + std::to_string(j));
                }
            });
        }

        // Start a reader thread
        std::atomic<int> read_count{0};
        std::thread reader([&state, &read_count]() {
            for (int i = 0; i < 100; i++) {
                auto excluded = state.get_excluded_objects();
                read_count++;
                std::this_thread::yield();
            }
        });

        // Join all threads
        for (auto& t : writers) {
            t.join();
        }
        reader.join();

        // Verify all objects were added
        auto excluded = state.get_excluded_objects();
        REQUIRE(excluded.size() == 1000); // 10 writers * 100 objects each
        REQUIRE(read_count >= 1);         // Reader ran at least once
    }
}

// ============================================================================
// Shared State Between Mocks
// ============================================================================

class SharedStateTestFixture {
  public:
    SharedStateTestFixture() : client_(MoonrakerClientMock::PrinterType::VORON_24) {
        // Create shared state
        shared_state_ = std::make_shared<MockPrinterState>();

        // Attach shared state to client mock
        client_.set_mock_state(shared_state_);

        // Initialize printer state for API mock
        printer_state_.init_subjects(false);

        // Create API mock with shared state
        api_ = std::make_unique<MoonrakerAPIMock>(client_, printer_state_);
        api_->set_mock_state(shared_state_);
    }

  protected:
    std::shared_ptr<MockPrinterState> shared_state_;
    MoonrakerClientMock client_;
    PrinterState printer_state_;
    std::unique_ptr<MoonrakerAPIMock> api_;
};

TEST_CASE_METHOD(SharedStateTestFixture,
                 "Excluded objects added via ClientMock appear in APIMock",
                 "[mock][shared_state]") {
    // Exclude an object via G-code (simulating Klipper command)
    client_.gcode_script("EXCLUDE_OBJECT NAME=Part_1");

    // Verify it appears in the API mock's query
    auto excluded = api_->get_excluded_objects_from_mock();
    REQUIRE(excluded.size() == 1);
    REQUIRE(excluded.count("Part_1") == 1);

    // Also verify it's in the client mock
    auto client_excluded = client_.get_excluded_objects();
    REQUIRE(client_excluded.size() == 1);
    REQUIRE(client_excluded.count("Part_1") == 1);
}

TEST_CASE_METHOD(SharedStateTestFixture,
                 "Multiple excluded objects synchronize correctly",
                 "[mock][shared_state]") {
    // Exclude multiple objects via various command formats
    client_.gcode_script("EXCLUDE_OBJECT NAME=Part_1");
    client_.gcode_script("EXCLUDE_OBJECT NAME=Part_2");
    client_.gcode_script("EXCLUDE_OBJECT NAME=\"Part With Spaces\"");

    // Verify all appear in API mock
    auto excluded = api_->get_excluded_objects_from_mock();
    REQUIRE(excluded.size() == 3);
    REQUIRE(excluded.count("Part_1") == 1);
    REQUIRE(excluded.count("Part_2") == 1);
    REQUIRE(excluded.count("Part With Spaces") == 1);
}

TEST_CASE_METHOD(SharedStateTestFixture,
                 "Print start clears excluded objects in shared state",
                 "[mock][shared_state]") {
    // Add some excluded objects
    client_.gcode_script("EXCLUDE_OBJECT NAME=Part_1");
    client_.gcode_script("EXCLUDE_OBJECT NAME=Part_2");

    // Verify they exist
    REQUIRE(api_->get_excluded_objects_from_mock().size() == 2);

    // Start a new print via G-code (this should clear excluded objects)
    client_.gcode_script("SDCARD_PRINT_FILE FILENAME=\"3DBenchy.gcode\"");

    // Verify excluded objects are cleared
    auto excluded = api_->get_excluded_objects_from_mock();
    REQUIRE(excluded.empty());
}

TEST_CASE_METHOD(SharedStateTestFixture,
                 "RESTART clears excluded objects in shared state",
                 "[mock][shared_state]") {
    // Add some excluded objects
    client_.gcode_script("EXCLUDE_OBJECT NAME=Part_1");

    // Verify it exists
    REQUIRE(api_->get_excluded_objects_from_mock().size() == 1);

    // Issue RESTART command
    client_.gcode_script("RESTART");

    // Wait briefly for the async restart to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify excluded objects are cleared
    auto excluded = api_->get_excluded_objects_from_mock();
    REQUIRE(excluded.empty());
}

TEST_CASE_METHOD(SharedStateTestFixture,
                 "FIRMWARE_RESTART clears excluded objects in shared state",
                 "[mock][shared_state]") {
    // Add some excluded objects
    client_.gcode_script("EXCLUDE_OBJECT NAME=Part_1");
    client_.gcode_script("EXCLUDE_OBJECT NAME=Part_2");

    // Verify they exist
    REQUIRE(api_->get_excluded_objects_from_mock().size() == 2);

    // Issue FIRMWARE_RESTART command
    client_.gcode_script("FIRMWARE_RESTART");

    // Wait briefly for the async restart to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify excluded objects are cleared
    auto excluded = api_->get_excluded_objects_from_mock();
    REQUIRE(excluded.empty());
}

// ============================================================================
// Backward Compatibility Tests
// ============================================================================

TEST_CASE("MoonrakerClientMock works without shared state", "[mock][shared_state][backward_compat]") {
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::VORON_24);
    // Note: NOT setting shared state

    SECTION("Excluded objects work with local state") {
        client.gcode_script("EXCLUDE_OBJECT NAME=Part_1");

        auto excluded = client.get_excluded_objects();
        REQUIRE(excluded.size() == 1);
        REQUIRE(excluded.count("Part_1") == 1);
    }

    SECTION("get_mock_state returns nullptr when not set") {
        REQUIRE(client.get_mock_state() == nullptr);
    }
}

TEST_CASE("MoonrakerAPIMock returns empty collections without shared state",
          "[mock][shared_state][backward_compat]") {
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::VORON_24);
    PrinterState state;
    state.init_subjects(false);
    MoonrakerAPIMock api(client, state);
    // Note: NOT setting shared state

    SECTION("get_excluded_objects_from_mock returns empty set") {
        auto excluded = api.get_excluded_objects_from_mock();
        REQUIRE(excluded.empty());
    }

    SECTION("get_available_objects_from_mock returns empty vector") {
        auto available = api.get_available_objects_from_mock();
        REQUIRE(available.empty());
    }

    SECTION("get_mock_state returns nullptr when not set") {
        REQUIRE(api.get_mock_state() == nullptr);
    }
}

// ============================================================================
// Available Objects Tests
// ============================================================================

TEST_CASE_METHOD(SharedStateTestFixture,
                 "Available objects set via shared state are accessible from APIMock",
                 "[mock][shared_state]") {
    // Set available objects directly in shared state
    // (In real usage, this would be populated from EXCLUDE_OBJECT_DEFINE parsing)
    shared_state_->set_available_objects({"Body", "Support_1", "Support_2", "Brim"});

    // Verify they're accessible from API mock
    auto available = api_->get_available_objects_from_mock();
    REQUIRE(available.size() == 4);
    REQUIRE(available[0] == "Body");
    REQUIRE(available[1] == "Support_1");
    REQUIRE(available[2] == "Support_2");
    REQUIRE(available[3] == "Brim");
}

// ============================================================================
// Temperature State Tests
// ============================================================================

TEST_CASE("MockPrinterState temperature state", "[mock][shared_state]") {
    MockPrinterState state;

    SECTION("Default temperatures are room temperature") {
        REQUIRE(state.extruder_temp == 25.0);
        REQUIRE(state.bed_temp == 25.0);
        REQUIRE(state.extruder_target == 0.0);
        REQUIRE(state.bed_target == 0.0);
    }

    SECTION("Temperature updates are atomic") {
        state.extruder_temp = 200.0;
        state.extruder_target = 210.0;
        state.bed_temp = 60.0;
        state.bed_target = 65.0;

        // Read back
        REQUIRE(state.extruder_temp == 200.0);
        REQUIRE(state.extruder_target == 210.0);
        REQUIRE(state.bed_temp == 60.0);
        REQUIRE(state.bed_target == 65.0);
    }
}
