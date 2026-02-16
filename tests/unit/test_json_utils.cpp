// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "json_utils.h"

#include "../catch_amalgamated.hpp"

using nlohmann::json;

// ============================================================================
// safe_string tests
// ============================================================================

TEST_CASE("safe_string returns value for normal string", "[json_utils]") {
    json j = {{"name", "PLA Red"}};
    CHECK(helix::json::safe_string(j, "name") == "PLA Red");
}

TEST_CASE("safe_string returns default for null field", "[json_utils]") {
    json j = {{"name", nullptr}};
    CHECK(helix::json::safe_string(j, "name") == "");
    CHECK(helix::json::safe_string(j, "name", "fallback") == "fallback");
}

TEST_CASE("safe_string returns default for missing field", "[json_utils]") {
    json j = {{"other", "value"}};
    CHECK(helix::json::safe_string(j, "name") == "");
    CHECK(helix::json::safe_string(j, "name", "default") == "default");
}

TEST_CASE("safe_string returns default for non-string type", "[json_utils]") {
    json j = {{"name", 42}};
    CHECK(helix::json::safe_string(j, "name") == "");
}

TEST_CASE("safe_string handles empty string", "[json_utils]") {
    json j = {{"name", ""}};
    CHECK(helix::json::safe_string(j, "name") == "");
}

// ============================================================================
// safe_int tests
// ============================================================================

TEST_CASE("safe_int returns value for normal int", "[json_utils]") {
    json j = {{"id", 42}};
    CHECK(helix::json::safe_int(j, "id") == 42);
}

TEST_CASE("safe_int returns default for null field", "[json_utils]") {
    json j = {{"id", nullptr}};
    CHECK(helix::json::safe_int(j, "id") == 0);
    CHECK(helix::json::safe_int(j, "id", -1) == -1);
}

TEST_CASE("safe_int returns default for missing field", "[json_utils]") {
    json j = {{"other", 1}};
    CHECK(helix::json::safe_int(j, "id") == 0);
    CHECK(helix::json::safe_int(j, "id", 99) == 99);
}

TEST_CASE("safe_int parses string integers", "[json_utils]") {
    json j = {{"id", "123"}};
    CHECK(helix::json::safe_int(j, "id") == 123);
}

TEST_CASE("safe_int returns default for non-numeric string", "[json_utils]") {
    json j = {{"id", "not-a-number"}};
    CHECK(helix::json::safe_int(j, "id") == 0);
    CHECK(helix::json::safe_int(j, "id", -1) == -1);
}

TEST_CASE("safe_int parses leading digits from mixed string", "[json_utils]") {
    // stoi("3d-fuel...") parses the leading "3" — this is expected behavior
    json j = {{"id", "3d-fuel_pla+_almond"}};
    CHECK(helix::json::safe_int(j, "id") == 3);
}

TEST_CASE("safe_int handles float JSON values", "[json_utils]") {
    json j = {{"id", 3.7}};
    CHECK(helix::json::safe_int(j, "id") == 3);
}

// ============================================================================
// safe_float tests
// ============================================================================

TEST_CASE("safe_float returns value for normal float", "[json_utils]") {
    json j = {{"density", 1.24f}};
    CHECK(helix::json::safe_float(j, "density") == Catch::Approx(1.24f));
}

TEST_CASE("safe_float returns default for null field", "[json_utils]") {
    json j = {{"density", nullptr}};
    CHECK(helix::json::safe_float(j, "density") == 0.0f);
    CHECK(helix::json::safe_float(j, "density", 1.0f) == 1.0f);
}

TEST_CASE("safe_float returns default for missing field", "[json_utils]") {
    json j = {{"other", 1}};
    CHECK(helix::json::safe_float(j, "density") == 0.0f);
}

TEST_CASE("safe_float parses string floats", "[json_utils]") {
    json j = {{"density", "1.24"}};
    CHECK(helix::json::safe_float(j, "density") == Catch::Approx(1.24f));
}

TEST_CASE("safe_float returns default for non-numeric string", "[json_utils]") {
    json j = {{"density", "unknown"}};
    CHECK(helix::json::safe_float(j, "density") == 0.0f);
}

// ============================================================================
// safe_double tests
// ============================================================================

TEST_CASE("safe_double returns value for normal double", "[json_utils]") {
    json j = {{"weight", 1000.5}};
    CHECK(helix::json::safe_double(j, "weight") == Catch::Approx(1000.5));
}

TEST_CASE("safe_double returns default for null field", "[json_utils]") {
    json j = {{"weight", nullptr}};
    CHECK(helix::json::safe_double(j, "weight") == 0.0);
    CHECK(helix::json::safe_double(j, "weight", -1.0) == -1.0);
}

TEST_CASE("safe_double parses string doubles", "[json_utils]") {
    json j = {{"weight", "1000.5"}};
    CHECK(helix::json::safe_double(j, "weight") == Catch::Approx(1000.5));
}
