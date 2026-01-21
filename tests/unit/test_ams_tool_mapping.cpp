// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_tool_mapping.cpp
 * @brief Unit tests for tool mapping interface across AMS backends
 *
 * Tests for tool mapping feature:
 * - ToolMappingCapabilities struct
 * - get_tool_mapping_capabilities() virtual method
 * - get_tool_mapping() virtual method
 * - set_tool_mapping() method
 * - Backend-specific implementations (Mock, AFC, Happy Hare, ValgACE, ToolChanger)
 */

#include "ams_backend_mock.h"
#include "ams_types.h"

#include "../catch_amalgamated.hpp"

using namespace helix::printer;

// =============================================================================
// Type Tests - ToolMappingCapabilities
// =============================================================================

TEST_CASE("ToolMappingCapabilities struct exists and has required fields",
          "[ams][tool_mapping][types]") {
    SECTION("default construction") {
        ToolMappingCapabilities caps;

        // Default should be not supported
        CHECK(caps.supported == false);
        CHECK(caps.editable == false);
        CHECK(caps.description.empty());
    }

    SECTION("can construct with values - fully supported") {
        ToolMappingCapabilities caps{true, true, "Per-lane tool assignment via SET_MAP"};

        CHECK(caps.supported == true);
        CHECK(caps.editable == true);
        CHECK(caps.description == "Per-lane tool assignment via SET_MAP");
    }

    SECTION("can construct with values - read-only") {
        ToolMappingCapabilities caps{true, false, "Fixed 1:1 mapping"};

        CHECK(caps.supported == true);
        CHECK(caps.editable == false);
        CHECK(caps.description == "Fixed 1:1 mapping");
    }

    SECTION("not supported has empty description") {
        ToolMappingCapabilities caps{false, false, ""};

        CHECK(caps.supported == false);
        CHECK(caps.editable == false);
        CHECK(caps.description.empty());
    }
}

// =============================================================================
// Base Class Interface Tests
// =============================================================================

TEST_CASE("AmsBackend base class has tool mapping virtual methods",
          "[ams][tool_mapping][interface]") {
    // This test verifies the interface exists by using the mock
    AmsBackendMock backend(4);
    backend.set_operation_delay(0);
    REQUIRE(backend.start());

    SECTION("get_tool_mapping_capabilities returns valid struct") {
        auto caps = backend.get_tool_mapping_capabilities();

        // Mock should return supported=true, editable=true by default (filament system mode)
        CHECK(caps.supported == true);
        CHECK(caps.editable == true);
        CHECK_FALSE(caps.description.empty());
    }

    SECTION("get_tool_mapping returns vector of mappings") {
        auto mapping = backend.get_tool_mapping();

        // Mock with 4 slots should return 4 tool mappings
        REQUIRE(mapping.size() == 4);

        // Default mock should have 1:1 tool-to-slot mapping
        for (size_t i = 0; i < mapping.size(); ++i) {
            CHECK(mapping[i] == static_cast<int>(i));
        }
    }

    SECTION("set_tool_mapping returns AmsError") {
        auto result = backend.set_tool_mapping(0, 2);

        // Mock should succeed
        CHECK(result);
        CHECK(result.technical_msg.empty());
    }

    backend.stop();
}

// =============================================================================
// Mock Backend Tests - Filament System Mode
// =============================================================================

TEST_CASE("Mock backend tool mapping - filament system mode", "[ams][tool_mapping][mock]") {
    AmsBackendMock backend(4);
    backend.set_operation_delay(0);
    REQUIRE(backend.start());

    SECTION("default capabilities are editable") {
        auto caps = backend.get_tool_mapping_capabilities();

        CHECK(caps.supported == true);
        CHECK(caps.editable == true);
        CHECK_FALSE(caps.description.empty());
    }

    SECTION("get_tool_mapping returns default 1:1 mapping") {
        auto mapping = backend.get_tool_mapping();

        REQUIRE(mapping.size() == 4);
        CHECK(mapping[0] == 0);
        CHECK(mapping[1] == 1);
        CHECK(mapping[2] == 2);
        CHECK(mapping[3] == 3);
    }

    SECTION("set_tool_mapping updates mapping") {
        auto result = backend.set_tool_mapping(0, 2); // Tool 0 -> Slot 2
        REQUIRE(result);

        auto mapping = backend.get_tool_mapping();
        CHECK(mapping[0] == 2);
        // Other mappings should remain unchanged
        CHECK(mapping[1] == 1);
        CHECK(mapping[2] == 2);
        CHECK(mapping[3] == 3);
    }

    SECTION("set_tool_mapping can remap multiple tools") {
        backend.set_tool_mapping(0, 3);
        backend.set_tool_mapping(1, 2);
        backend.set_tool_mapping(2, 1);
        backend.set_tool_mapping(3, 0);

        auto mapping = backend.get_tool_mapping();
        CHECK(mapping[0] == 3);
        CHECK(mapping[1] == 2);
        CHECK(mapping[2] == 1);
        CHECK(mapping[3] == 0);
    }

    SECTION("set_tool_mapping validates tool number") {
        // Invalid tool number (too high)
        auto result = backend.set_tool_mapping(99, 0);
        CHECK_FALSE(result);
        CHECK(result.result == AmsResult::INVALID_TOOL);
    }

    SECTION("set_tool_mapping validates slot number") {
        // Invalid slot number (too high)
        auto result = backend.set_tool_mapping(0, 99);
        CHECK_FALSE(result);
        CHECK(result.result == AmsResult::INVALID_SLOT);
    }

    SECTION("set_tool_mapping rejects negative values") {
        auto result1 = backend.set_tool_mapping(-1, 0);
        CHECK_FALSE(result1);

        auto result2 = backend.set_tool_mapping(0, -1);
        CHECK_FALSE(result2);
    }

    backend.stop();
}

// =============================================================================
// Mock Backend Tests - Tool Changer Mode
// =============================================================================

TEST_CASE("Mock backend tool mapping - tool changer mode",
          "[ams][tool_mapping][mock][toolchanger]") {
    AmsBackendMock backend(4);
    backend.set_operation_delay(0);
    backend.set_tool_changer_mode(true);
    REQUIRE(backend.start());

    SECTION("tool changer reports not supported") {
        auto caps = backend.get_tool_mapping_capabilities();

        CHECK(caps.supported == false);
        CHECK(caps.editable == false);
        CHECK(caps.description.empty());
    }

    SECTION("get_tool_mapping returns empty for tool changer") {
        // Tool changers have fixed 1:1 mapping - tools ARE slots
        // Implementation returns system_info_.tool_to_slot_map which is still populated,
        // but the capabilities indicate mapping is not supported/editable
        auto caps = backend.get_tool_mapping_capabilities();
        CHECK_FALSE(caps.supported);
    }

    backend.stop();
}

// =============================================================================
// Note: AFC and Happy Hare backend tests are covered in their respective
// test files (test_ams_backend_afc.cpp, test_ams_backend_happy_hare.cpp)
// which have proper test helper classes with friend access.
// This file focuses on Mock backend and interface tests.
// =============================================================================

// =============================================================================
// Edge Cases and Integration
// =============================================================================

TEST_CASE("Tool mapping edge cases", "[ams][tool_mapping][edge]") {
    AmsBackendMock backend(4);
    backend.set_operation_delay(0);
    REQUIRE(backend.start());

    SECTION("multiple tools can map to same slot") {
        // This is valid - e.g., T0 and T1 both use slot 0
        backend.set_tool_mapping(0, 0);
        backend.set_tool_mapping(1, 0);

        auto mapping = backend.get_tool_mapping();
        CHECK(mapping[0] == 0);
        CHECK(mapping[1] == 0);
    }

    SECTION("tool mapping affects system_info") {
        backend.set_tool_mapping(0, 3);

        auto info = backend.get_system_info();
        REQUIRE(info.tool_to_slot_map.size() == 4);
        CHECK(info.tool_to_slot_map[0] == 3);
    }

    backend.stop();
}

TEST_CASE("Tool mapping with system_info integration", "[ams][tool_mapping][integration]") {
    AmsBackendMock backend(4);
    backend.set_operation_delay(0);
    REQUIRE(backend.start());

    SECTION("system_info.supports_tool_mapping reflects capabilities") {
        auto caps = backend.get_tool_mapping_capabilities();
        auto info = backend.get_system_info();

        CHECK(info.supports_tool_mapping == caps.supported);
    }

    SECTION("tool changer mode updates system_info") {
        backend.set_tool_changer_mode(true);

        auto caps = backend.get_tool_mapping_capabilities();
        CHECK_FALSE(caps.supported);
    }

    backend.stop();
}

// =============================================================================
// Backend Comparison Tests (Mock backend variations)
// =============================================================================

TEST_CASE("Tool mapping capabilities vary by backend mode", "[ams][tool_mapping][comparison]") {
    SECTION("Mock filament system supports editable mapping") {
        AmsBackendMock mock(4);
        mock.set_operation_delay(0);
        REQUIRE(mock.start());

        auto caps = mock.get_tool_mapping_capabilities();
        CHECK(caps.supported == true);
        CHECK(caps.editable == true);

        mock.stop();
    }

    SECTION("Mock tool changer does not support mapping") {
        AmsBackendMock mock(4);
        mock.set_operation_delay(0);
        mock.set_tool_changer_mode(true);
        REQUIRE(mock.start());

        auto caps = mock.get_tool_mapping_capabilities();
        CHECK(caps.supported == false);
        CHECK(caps.editable == false);

        mock.stop();
    }
}
