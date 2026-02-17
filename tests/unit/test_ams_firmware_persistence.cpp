// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_firmware_persistence.cpp
 * @brief Tests for has_firmware_spool_persistence() across AMS backends
 */

#include "ams_backend_afc.h"
#include "ams_backend_happy_hare.h"
#include "ams_backend_mock.h"
#include "ams_backend_toolchanger.h"

#include "../catch_amalgamated.hpp"

using namespace helix::printer;

// =============================================================================
// has_firmware_spool_persistence() tests
// =============================================================================

TEST_CASE("AmsBackendMock: no firmware spool persistence by default",
          "[ams][backend][spool-persistence]") {
    auto mock = AmsBackendMock::create_mock();
    REQUIRE_FALSE(mock->has_firmware_spool_persistence());
}

TEST_CASE("AmsBackendHappyHare: has firmware spool persistence",
          "[ams][backend][spool-persistence]") {
    // Construct directly with nullptr — constructor sets capability flags
    auto backend = std::make_unique<AmsBackendHappyHare>(nullptr, nullptr);
    REQUIRE(backend->has_firmware_spool_persistence());
}

TEST_CASE("AmsBackendAfc: has firmware spool persistence", "[ams][backend][spool-persistence]") {
    auto backend = std::make_unique<AmsBackendAfc>(nullptr, nullptr);
    REQUIRE(backend->has_firmware_spool_persistence());
}

TEST_CASE("AmsBackendToolChanger: no firmware spool persistence",
          "[ams][backend][spool-persistence]") {
    auto backend = std::make_unique<AmsBackendToolChanger>(nullptr, nullptr);
    REQUIRE_FALSE(backend->has_firmware_spool_persistence());
}
