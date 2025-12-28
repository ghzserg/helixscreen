// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 HelixScreen Authors

#include "unit_conversions.h"

#include "../catch_amalgamated.hpp"

using namespace helix::units;
using json = nlohmann::json;

// ============================================================================
// Temperature Conversion Tests
// ============================================================================

TEST_CASE("to_centidegrees converts correctly", "[unit_conversions][temperature]") {
    SECTION("zero degrees") {
        REQUIRE(to_centidegrees(0.0) == 0);
    }

    SECTION("positive temperatures") {
        REQUIRE(to_centidegrees(25.0) == 250);
        REQUIRE(to_centidegrees(25.5) == 255);
        REQUIRE(to_centidegrees(200.0) == 2000);
        REQUIRE(to_centidegrees(210.7) == 2107);
    }

    SECTION("decimal precision") {
        REQUIRE(to_centidegrees(25.15) == 251); // Truncates to int
        REQUIRE(to_centidegrees(25.99) == 259); // Truncates, not rounds
    }

    SECTION("negative temperatures") {
        REQUIRE(to_centidegrees(-10.0) == -100);
        REQUIRE(to_centidegrees(-0.5) == -5);
    }
}

TEST_CASE("from_centidegrees converts correctly", "[unit_conversions][temperature]") {
    REQUIRE(from_centidegrees(0) == 0.0);
    REQUIRE(from_centidegrees(250) == 25.0);
    REQUIRE(from_centidegrees(255) == 25.5);
    REQUIRE(from_centidegrees(-100) == -10.0);
}

TEST_CASE("json_to_centidegrees extracts correctly", "[unit_conversions][temperature]") {
    SECTION("valid temperature") {
        json obj = {{"temperature", 25.5}};
        REQUIRE(json_to_centidegrees(obj, "temperature") == 255);
    }

    SECTION("missing key returns default") {
        json obj = {{"other", 100}};
        REQUIRE(json_to_centidegrees(obj, "temperature") == 0);
        REQUIRE(json_to_centidegrees(obj, "temperature", -1) == -1);
    }

    SECTION("non-number value returns default") {
        json obj = {{"temperature", "hot"}};
        REQUIRE(json_to_centidegrees(obj, "temperature") == 0);
    }

    SECTION("null value returns default") {
        json obj = {{"temperature", nullptr}};
        REQUIRE(json_to_centidegrees(obj, "temperature") == 0);
    }

    SECTION("non-object JSON returns default") {
        json arr = json::array({1, 2, 3});
        json num = 42;
        json null_json = nullptr;
        json str = "hello";
        REQUIRE(json_to_centidegrees(arr, "temp") == 0);
        REQUIRE(json_to_centidegrees(num, "temp") == 0);
        REQUIRE(json_to_centidegrees(null_json, "temp") == 0);
        REQUIRE(json_to_centidegrees(str, "temp") == 0);
        REQUIRE(json_to_centidegrees(arr, "temp", -999) == -999);
    }
}

// ============================================================================
// Percent Conversion Tests
// ============================================================================

TEST_CASE("to_percent converts correctly", "[unit_conversions][percent]") {
    SECTION("standard values") {
        REQUIRE(to_percent(0.0) == 0);
        REQUIRE(to_percent(0.5) == 50);
        REQUIRE(to_percent(1.0) == 100);
        REQUIRE(to_percent(0.75) == 75);
    }

    SECTION("over 100%") {
        REQUIRE(to_percent(1.5) == 150);
        REQUIRE(to_percent(2.0) == 200);
    }

    SECTION("small values") {
        REQUIRE(to_percent(0.01) == 1);
        REQUIRE(to_percent(0.001) == 0); // Truncates
    }
}

TEST_CASE("from_percent converts correctly", "[unit_conversions][percent]") {
    REQUIRE(from_percent(0) == 0.0);
    REQUIRE(from_percent(50) == 0.5);
    REQUIRE(from_percent(100) == 1.0);
    REQUIRE(from_percent(150) == 1.5);
}

TEST_CASE("json_to_percent extracts correctly", "[unit_conversions][percent]") {
    SECTION("valid ratio") {
        json obj = {{"progress", 0.75}};
        REQUIRE(json_to_percent(obj, "progress") == 75);
    }

    SECTION("missing key returns default") {
        json obj = {};
        REQUIRE(json_to_percent(obj, "progress") == 0);
        REQUIRE(json_to_percent(obj, "progress", 50) == 50);
    }

    SECTION("non-object JSON returns default") {
        json arr = json::array({0.5, 0.75});
        REQUIRE(json_to_percent(arr, "progress") == 0);
        REQUIRE(json_to_percent(arr, "progress", 100) == 100);
    }
}

// ============================================================================
// Length Conversion Tests
// ============================================================================

TEST_CASE("to_centimm converts correctly", "[unit_conversions][length]") {
    SECTION("standard values") {
        REQUIRE(to_centimm(0.0) == 0);
        REQUIRE(to_centimm(1.0) == 100);
        REQUIRE(to_centimm(1.25) == 125);
        REQUIRE(to_centimm(10.5) == 1050);
    }

    SECTION("small values") {
        REQUIRE(to_centimm(0.01) == 1);
        REQUIRE(to_centimm(0.001) == 0); // Truncates
    }

    SECTION("negative values") {
        REQUIRE(to_centimm(-1.0) == -100);
    }
}

TEST_CASE("from_centimm converts correctly", "[unit_conversions][length]") {
    REQUIRE(from_centimm(0) == 0.0);
    REQUIRE(from_centimm(100) == 1.0);
    REQUIRE(from_centimm(125) == 1.25);
    REQUIRE(from_centimm(-100) == -1.0);
}

TEST_CASE("json_to_centimm extracts correctly", "[unit_conversions][length]") {
    SECTION("valid length") {
        json obj = {{"retract_length", 1.25}};
        REQUIRE(json_to_centimm(obj, "retract_length") == 125);
    }

    SECTION("missing key returns default") {
        json empty = {};
        REQUIRE(json_to_centimm(empty, "retract_length") == 0);
        REQUIRE(json_to_centimm(empty, "retract_length", -1) == -1);
    }

    SECTION("non-object JSON returns default") {
        json arr = json::array({1.0, 2.0});
        REQUIRE(json_to_centimm(arr, "length") == 0);
        REQUIRE(json_to_centimm(arr, "length", 999) == 999);
    }
}

// ============================================================================
// Speed Conversion Tests
// ============================================================================

TEST_CASE("speed_factor_to_percent converts correctly", "[unit_conversions][speed]") {
    REQUIRE(speed_factor_to_percent(1.0) == 100);
    REQUIRE(speed_factor_to_percent(0.5) == 50);
    REQUIRE(speed_factor_to_percent(1.5) == 150); // Overdrive
    REQUIRE(speed_factor_to_percent(0.0) == 0);
}

TEST_CASE("mm_per_sec_to_mm_per_min converts correctly", "[unit_conversions][speed]") {
    REQUIRE(mm_per_sec_to_mm_per_min(1.0) == 60);
    REQUIRE(mm_per_sec_to_mm_per_min(10.0) == 600);
    REQUIRE(mm_per_sec_to_mm_per_min(0.5) == 30);
    REQUIRE(mm_per_sec_to_mm_per_min(100.0) == 6000);
}

// ============================================================================
// Round-trip Tests
// ============================================================================

TEST_CASE("round-trip conversions maintain precision", "[unit_conversions][roundtrip]") {
    SECTION("temperature round-trip") {
        double original = 25.5;
        int centi = to_centidegrees(original);
        double recovered = from_centidegrees(centi);
        REQUIRE(recovered == original);
    }

    SECTION("percent round-trip") {
        double original = 0.75;
        int pct = to_percent(original);
        double recovered = from_percent(pct);
        REQUIRE(recovered == original);
    }

    SECTION("length round-trip") {
        double original = 1.25;
        int centi = to_centimm(original);
        double recovered = from_centimm(centi);
        REQUIRE(recovered == original);
    }
}
