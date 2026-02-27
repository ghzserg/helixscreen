// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 356C LLC

#include "geometry_budget_manager.h"

#include "../catch_amalgamated.hpp"

using namespace helix::gcode;

// Memory parsing tests
TEST_CASE("Budget: parse MemAvailable from /proc/meminfo", "[gcode][budget]") {
    const std::string meminfo = R"(MemTotal:        3884136 kB
MemFree:         1363424 kB
MemAvailable:    3768880 kB
Buffers:          104872 kB
Cached:          2091048 kB)";
    REQUIRE(GeometryBudgetManager::parse_meminfo_available_kb(meminfo) == 3768880);
}

TEST_CASE("Budget: parse MemAvailable from 1GB system", "[gcode][budget]") {
    const std::string meminfo = R"(MemTotal:         999936 kB
MemFree:          102400 kB
MemAvailable:     307200 kB)";
    REQUIRE(GeometryBudgetManager::parse_meminfo_available_kb(meminfo) == 307200);
}

TEST_CASE("Budget: parse MemAvailable returns 0 on missing field", "[gcode][budget]") {
    const std::string meminfo = R"(MemTotal:        3884136 kB
MemFree:         1363424 kB)";
    REQUIRE(GeometryBudgetManager::parse_meminfo_available_kb(meminfo) == 0);
}

TEST_CASE("Budget: parse MemAvailable from AD5M (256MB)", "[gcode][budget]") {
    const std::string meminfo = R"(MemTotal:         253440 kB
MemFree:           12288 kB
MemAvailable:      38912 kB)";
    REQUIRE(GeometryBudgetManager::parse_meminfo_available_kb(meminfo) == 38912);
}

// Budget calculation tests
TEST_CASE("Budget: 25% of available memory", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    size_t budget = mgr.calculate_budget(3768880);
    REQUIRE(budget == 256 * 1024 * 1024); // Capped at 256MB
}

TEST_CASE("Budget: 1GB Pi with 300MB free", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    size_t budget = mgr.calculate_budget(307200);
    REQUIRE(budget == 307200 * 1024 / 4);
}

TEST_CASE("Budget: AD5M with 38MB available", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    size_t budget = mgr.calculate_budget(38912);
    REQUIRE(budget == 38912 * 1024 / 4);
}

TEST_CASE("Budget: hard cap at 256MB even with 8GB free", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    size_t budget = mgr.calculate_budget(6144000);
    REQUIRE(budget == 256 * 1024 * 1024);
}

TEST_CASE("Budget: 0 available memory returns 0", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    size_t budget = mgr.calculate_budget(0);
    REQUIRE(budget == 0);
}
