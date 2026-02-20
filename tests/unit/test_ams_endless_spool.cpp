// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_endless_spool.cpp
 * @brief TDD tests for unified endless spool abstraction
 *
 * Tests for Phase 0: Unified Endless Spool Abstraction
 * - EndlessSpoolCapabilities struct
 * - EndlessSpoolConfig struct
 * - get_endless_spool_capabilities() virtual method
 * - get_endless_spool_config() virtual method
 * - set_endless_spool_backup() virtual method
 * - Backend-specific implementations (AFC, Happy Hare, Mock)
 */

#include "ams_backend_mock.h"
#include "ams_types.h"

#include "../catch_amalgamated.hpp"

// Uncomment these as implementations are added:
#include "ams_backend_afc.h"
#include "ams_backend_happy_hare.h"

using namespace helix;
using namespace helix::printer;

// =============================================================================
// Type Tests - EndlessSpoolCapabilities and EndlessSpoolConfig
// =============================================================================

TEST_CASE("EndlessSpoolCapabilities struct exists and has required fields",
          "[ams][endless_spool][types]") {
    SECTION("default construction") {
        EndlessSpoolCapabilities caps;

        // Default should be not supported
        CHECK(caps.supported == false);
        CHECK(caps.editable == false);
        CHECK(caps.description.empty());
    }

    SECTION("can construct with values") {
        EndlessSpoolCapabilities caps{true, true, "Per-slot backup"};

        CHECK(caps.supported == true);
        CHECK(caps.editable == true);
        CHECK(caps.description == "Per-slot backup");
    }

    SECTION("read-only capabilities") {
        EndlessSpoolCapabilities caps{true, false, "Group-based"};

        CHECK(caps.supported == true);
        CHECK(caps.editable == false);
        CHECK(caps.description == "Group-based");
    }
}

TEST_CASE("EndlessSpoolConfig struct exists and has required fields",
          "[ams][endless_spool][types]") {
    SECTION("default construction") {
        EndlessSpoolConfig config;

        CHECK(config.slot_index == 0);
        CHECK(config.backup_slot == -1); // -1 = no backup
    }

    SECTION("can construct with values") {
        EndlessSpoolConfig config{2, 5};

        CHECK(config.slot_index == 2);
        CHECK(config.backup_slot == 5);
    }

    SECTION("no backup configured") {
        EndlessSpoolConfig config{0, -1};

        CHECK(config.slot_index == 0);
        CHECK(config.backup_slot == -1);
    }
}

// =============================================================================
// Base Class Interface Tests
// =============================================================================

TEST_CASE("AmsBackend base class has endless spool virtual methods",
          "[ams][endless_spool][interface]") {
    // This test verifies the interface exists by using the mock
    AmsBackendMock backend(4);
    backend.set_operation_delay(0);
    REQUIRE(backend.start());

    SECTION("get_endless_spool_capabilities returns valid struct") {
        auto caps = backend.get_endless_spool_capabilities();

        // Mock should return supported=true, editable=true by default
        CHECK(caps.supported == true);
        CHECK(caps.editable == true);
    }

    SECTION("get_endless_spool_config returns vector of configs") {
        auto configs = backend.get_endless_spool_config();

        // Mock with 4 slots should return 4 configs
        REQUIRE(configs.size() == 4);

        // Each config should have correct slot index
        for (size_t i = 0; i < configs.size(); ++i) {
            CHECK(configs[i].slot_index == static_cast<int>(i));
        }
    }

    SECTION("set_endless_spool_backup returns AmsError") {
        auto result = backend.set_endless_spool_backup(0, 2);

        // Mock should succeed
        CHECK(result);
        CHECK(result.technical_msg.empty());
    }

    backend.stop();
}

// =============================================================================
// Mock Backend Tests
// =============================================================================

TEST_CASE("Mock backend endless spool - configurable behavior", "[ams][endless_spool][mock]") {
    AmsBackendMock backend(4);
    backend.set_operation_delay(0);
    REQUIRE(backend.start());

    SECTION("default capabilities are editable") {
        auto caps = backend.get_endless_spool_capabilities();

        CHECK(caps.supported == true);
        CHECK(caps.editable == true);
        CHECK_FALSE(caps.description.empty());
    }

    SECTION("can configure as read-only (Happy Hare mode)") {
        backend.set_endless_spool_editable(false);

        auto caps = backend.get_endless_spool_capabilities();
        CHECK(caps.supported == true);
        CHECK(caps.editable == false);
    }

    SECTION("can disable endless spool support entirely") {
        backend.set_endless_spool_supported(false);

        auto caps = backend.get_endless_spool_capabilities();
        CHECK(caps.supported == false);
        CHECK(caps.editable == false);
    }

    SECTION("set_endless_spool_backup updates config") {
        auto result = backend.set_endless_spool_backup(0, 2);
        REQUIRE(result);

        auto configs = backend.get_endless_spool_config();
        REQUIRE(configs.size() >= 1);
        CHECK(configs[0].backup_slot == 2);
    }

    SECTION("set_endless_spool_backup with -1 removes backup") {
        // First set a backup
        backend.set_endless_spool_backup(0, 2);

        // Then remove it
        auto result = backend.set_endless_spool_backup(0, -1);
        REQUIRE(result);

        auto configs = backend.get_endless_spool_config();
        CHECK(configs[0].backup_slot == -1);
    }

    SECTION("set_endless_spool_backup returns error when read-only") {
        backend.set_endless_spool_editable(false);

        auto result = backend.set_endless_spool_backup(0, 2);

        CHECK_FALSE(result);
        CHECK(result.result == AmsResult::NOT_SUPPORTED);
    }

    SECTION("set_endless_spool_backup validates slot indices") {
        // Invalid source slot (too high)
        auto result1 = backend.set_endless_spool_backup(99, 2);
        CHECK_FALSE(result1);
        CHECK(result1.result == AmsResult::INVALID_SLOT);

        // Invalid backup slot (too high, but -1 is valid for "no backup")
        auto result2 = backend.set_endless_spool_backup(0, 99);
        CHECK_FALSE(result2);
        CHECK(result2.result == AmsResult::INVALID_SLOT);

        // Negative source slot (invalid)
        auto result3 = backend.set_endless_spool_backup(-1, 2);
        CHECK_FALSE(result3);
        CHECK(result3.result == AmsResult::INVALID_SLOT);

        // Negative backup slot other than -1 (invalid)
        auto result4 = backend.set_endless_spool_backup(0, -2);
        CHECK_FALSE(result4);
        CHECK(result4.result == AmsResult::INVALID_SLOT);
    }

    backend.stop();
}

// =============================================================================
// AFC Backend Tests - DISABLED until AFC backend is implemented
// =============================================================================
// These tests will be enabled when ams_backend_afc.h is implemented with
// endless spool support. Currently marked as [.disabled] to skip.

#if 1 // AFC implementation pending - enable when ready
// Helper class to test AFC without real Moonraker connection
class AmsBackendAfcEndlessSpoolHelper : public AmsBackendAfc {
  public:
    AmsBackendAfcEndlessSpoolHelper() : AmsBackendAfc(nullptr, nullptr) {}

    void initialize_test_lanes(int count) {
        system_info_.units.clear();
        system_info_.total_slots = count;
        std::vector<std::string> names;

        AmsUnit unit;
        unit.unit_index = 0;
        unit.name = "AFC Test Unit";
        unit.slot_count = count;
        unit.first_slot_global_index = 0;

        for (int i = 0; i < count; ++i) {
            std::string name = "lane" + std::to_string(i + 1);
            names.push_back(name);

            SlotInfo slot;
            slot.slot_index = i;
            slot.global_index = i;
            slot.status = SlotStatus::AVAILABLE;
            slot.mapped_tool = i;
            unit.slots.push_back(slot);
        }

        system_info_.units.push_back(unit);
        slots_.initialize("AFC Test Unit", names);
    }

    // G-code capture for verification
    std::vector<std::string> captured_gcodes;

    AmsError execute_gcode(const std::string& gcode) override {
        captured_gcodes.push_back(gcode);
        return AmsErrorHelper::success();
    }

    bool has_gcode(const std::string& expected) const {
        return std::find(captured_gcodes.begin(), captured_gcodes.end(), expected) !=
               captured_gcodes.end();
    }

    bool has_gcode_containing(const std::string& substring) const {
        return std::any_of(
            captured_gcodes.begin(), captured_gcodes.end(),
            [&](const std::string& gc) { return gc.find(substring) != std::string::npos; });
    }

    void clear_gcodes() {
        captured_gcodes.clear();
    }
};

TEST_CASE("AFC backend endless spool - full implementation", "[ams][endless_spool][afc]") {
    AmsBackendAfcEndlessSpoolHelper helper;
    helper.initialize_test_lanes(4);

    SECTION("capabilities show editable=true") {
        auto caps = helper.get_endless_spool_capabilities();

        CHECK(caps.supported == true);
        CHECK(caps.editable == true);
        CHECK(caps.description.find("AFC") != std::string::npos);
    }

    SECTION("get_endless_spool_config returns all lanes") {
        auto configs = helper.get_endless_spool_config();

        REQUIRE(configs.size() == 4);
        for (int i = 0; i < 4; ++i) {
            CHECK(configs[i].slot_index == i);
            CHECK(configs[i].backup_slot == -1); // No backup by default
        }
    }

    SECTION("set_endless_spool_backup sends SET_RUNOUT G-code") {
        auto result = helper.set_endless_spool_backup(0, 2);

        REQUIRE(result);
        // AFC command: SET_RUNOUT LANE=lane1 RUNOUT=lane3
        CHECK(helper.has_gcode_containing("SET_RUNOUT"));
        CHECK(helper.has_gcode_containing("LANE=lane1"));
        CHECK(helper.has_gcode_containing("RUNOUT=lane3"));
    }

    SECTION("set_endless_spool_backup with -1 disables backup") {
        auto result = helper.set_endless_spool_backup(0, -1);

        REQUIRE(result);
        // Should send command to disable runout for this lane
        CHECK(helper.has_gcode_containing("SET_RUNOUT"));
        CHECK(helper.has_gcode_containing("LANE=lane1"));
        // Might send RUNOUT_LANE= (empty) or a specific disable command
    }

    SECTION("config updates after set_endless_spool_backup") {
        helper.set_endless_spool_backup(1, 3);

        auto configs = helper.get_endless_spool_config();
        CHECK(configs[1].backup_slot == 3);
    }
}
#endif // AFC implementation pending

// =============================================================================
// Happy Hare Backend Tests - DISABLED until Happy Hare backend is implemented
// =============================================================================
// These tests will be enabled when ams_backend_happy_hare.h is implemented with
// endless spool support. Currently marked as [.disabled] to skip.

#if 1 // Happy Hare implementation enabled
// Helper class to test Happy Hare without real Moonraker connection
class AmsBackendHappyHareEndlessSpoolHelper : public AmsBackendHappyHare {
  public:
    AmsBackendHappyHareEndlessSpoolHelper() : AmsBackendHappyHare(nullptr, nullptr) {}

    void initialize_test_gates(int count) {
        system_info_.units.clear();
        system_info_.total_slots = count;

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
            slot.endless_spool_group = -1; // No group by default
            unit.slots.push_back(slot);
        }

        system_info_.units.push_back(unit);

        // Initialize SlotRegistry to match
        std::vector<std::string> slot_names;
        for (int i = 0; i < count; ++i) {
            slot_names.push_back(std::to_string(i));
        }
        slots_.initialize("MMU", slot_names);
        for (int i = 0; i < count; ++i) {
            auto* entry = slots_.get_mut(i);
            if (entry) {
                entry->info.status = SlotStatus::AVAILABLE;
                entry->info.endless_spool_group = -1;
            }
        }
    }

    void set_endless_spool_groups(const std::vector<int>& groups) {
        // Simulate data from printer.mmu.endless_spool_groups via registry
        for (size_t i = 0; i < groups.size(); ++i) {
            auto* entry = slots_.get_mut(static_cast<int>(i));
            if (entry) {
                entry->info.endless_spool_group = groups[i];
            }
        }
    }
};

TEST_CASE("Happy Hare backend endless spool - read-only implementation",
          "[ams][endless_spool][happy_hare]") {
    AmsBackendHappyHareEndlessSpoolHelper helper;
    helper.initialize_test_gates(4);

    SECTION("capabilities show editable=false") {
        auto caps = helper.get_endless_spool_capabilities();

        CHECK(caps.supported == true);
        CHECK(caps.editable == false); // Happy Hare is read-only
        // Check description contains "group" (case-insensitive via separate checks)
        CHECK((caps.description.find("group") != std::string::npos ||
               caps.description.find("Group") != std::string::npos));
    }

    SECTION("get_endless_spool_config converts groups to slot mapping") {
        // Set up groups: slots 0,1 in group 0; slots 2,3 in group 1
        helper.set_endless_spool_groups({0, 0, 1, 1});

        auto configs = helper.get_endless_spool_config();

        REQUIRE(configs.size() == 4);

        // Slot 0 -> backup is slot 1 (same group)
        CHECK(configs[0].slot_index == 0);
        CHECK(configs[0].backup_slot == 1);

        // Slot 1 -> backup is slot 0 (same group)
        CHECK(configs[1].slot_index == 1);
        CHECK(configs[1].backup_slot == 0);

        // Slot 2 -> backup is slot 3 (same group)
        CHECK(configs[2].slot_index == 2);
        CHECK(configs[2].backup_slot == 3);

        // Slot 3 -> backup is slot 2 (same group)
        CHECK(configs[3].slot_index == 3);
        CHECK(configs[3].backup_slot == 2);
    }

    SECTION("slots with group -1 have no backup") {
        helper.set_endless_spool_groups({-1, -1, 0, 0});

        auto configs = helper.get_endless_spool_config();

        CHECK(configs[0].backup_slot == -1); // No group
        CHECK(configs[1].backup_slot == -1); // No group
        CHECK(configs[2].backup_slot == 3);  // Group 0
        CHECK(configs[3].backup_slot == 2);  // Group 0
    }

    SECTION("single slot in group has no backup") {
        helper.set_endless_spool_groups({0, 1, 2, 3}); // Each slot alone in group

        auto configs = helper.get_endless_spool_config();

        // All should have no backup since they're alone in their groups
        for (const auto& config : configs) {
            CHECK(config.backup_slot == -1);
        }
    }

    SECTION("set_endless_spool_backup returns NOT_SUPPORTED") {
        auto result = helper.set_endless_spool_backup(0, 2);

        CHECK_FALSE(result);
        CHECK(result.result == AmsResult::NOT_SUPPORTED);
    }
}
#endif // Happy Hare implementation enabled

// =============================================================================
// Edge Cases and Integration
// =============================================================================

TEST_CASE("Endless spool edge cases", "[ams][endless_spool][edge]") {
    AmsBackendMock backend(4);
    backend.set_operation_delay(0);
    REQUIRE(backend.start());

    SECTION("cannot set slot as its own backup") {
        auto result = backend.set_endless_spool_backup(0, 0);

        CHECK_FALSE(result);
        CHECK(result.result == AmsResult::INVALID_SLOT);
    }

    SECTION("circular backup is allowed (A->B, B->A)") {
        auto result1 = backend.set_endless_spool_backup(0, 1);
        auto result2 = backend.set_endless_spool_backup(1, 0);

        CHECK(result1);
        CHECK(result2);

        auto configs = backend.get_endless_spool_config();
        CHECK(configs[0].backup_slot == 1);
        CHECK(configs[1].backup_slot == 0);
    }

    SECTION("chain backup is allowed (A->B->C)") {
        backend.set_endless_spool_backup(0, 1);
        backend.set_endless_spool_backup(1, 2);

        auto configs = backend.get_endless_spool_config();
        CHECK(configs[0].backup_slot == 1);
        CHECK(configs[1].backup_slot == 2);
        CHECK(configs[2].backup_slot == -1);
    }

    backend.stop();
}

TEST_CASE("Endless spool with system_info integration", "[ams][endless_spool][integration]") {
    AmsBackendMock backend(4);
    backend.set_operation_delay(0);
    REQUIRE(backend.start());

    SECTION("system_info.supports_endless_spool reflects capabilities") {
        auto caps = backend.get_endless_spool_capabilities();
        auto info = backend.get_system_info();

        CHECK(info.supports_endless_spool == caps.supported);
    }

    SECTION("disabling support updates system_info") {
        backend.set_endless_spool_supported(false);

        auto info = backend.get_system_info();
        CHECK(info.supports_endless_spool == false);
    }

    backend.stop();
}
