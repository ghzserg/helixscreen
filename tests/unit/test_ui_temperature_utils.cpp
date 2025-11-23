/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../catch_amalgamated.hpp"
#include "ui_temperature_utils.h"

using namespace UITemperatureUtils;

// ============================================================================
// validate_and_clamp() Tests
// ============================================================================

TEST_CASE("Temperature Utils: validate_and_clamp - valid temperature", "[temp_utils][validate]") {
    int temp = 200;
    bool result = validate_and_clamp(temp, 0, 300, "Test", "current");

    REQUIRE(result == true);
    REQUIRE(temp == 200);
}

TEST_CASE("Temperature Utils: validate_and_clamp - boundary values", "[temp_utils][validate]") {
    SECTION("At minimum boundary") {
        int temp = 0;
        bool result = validate_and_clamp(temp, 0, 300, "Test", "current");
        REQUIRE(result == true);
        REQUIRE(temp == 0);
    }

    SECTION("At maximum boundary") {
        int temp = 300;
        bool result = validate_and_clamp(temp, 0, 300, "Test", "current");
        REQUIRE(result == true);
        REQUIRE(temp == 300);
    }
}

TEST_CASE("Temperature Utils: validate_and_clamp - below minimum", "[temp_utils][validate][clamp]") {
    int temp = -10;
    bool result = validate_and_clamp(temp, 0, 300, "Test", "current");

    REQUIRE(result == false);
    REQUIRE(temp == 0);  // Clamped to minimum
}

TEST_CASE("Temperature Utils: validate_and_clamp - above maximum", "[temp_utils][validate][clamp]") {
    int temp = 350;
    bool result = validate_and_clamp(temp, 0, 300, "Test", "current");

    REQUIRE(result == false);
    REQUIRE(temp == 300);  // Clamped to maximum
}

TEST_CASE("Temperature Utils: validate_and_clamp - extreme values", "[temp_utils][validate][edge]") {
    SECTION("Very negative") {
        int temp = -1000;
        validate_and_clamp(temp, 0, 300, "Test", "current");
        REQUIRE(temp == 0);
    }

    SECTION("Very high") {
        int temp = 10000;
        validate_and_clamp(temp, 0, 300, "Test", "current");
        REQUIRE(temp == 300);
    }
}

TEST_CASE("Temperature Utils: validate_and_clamp - typical ranges", "[temp_utils][validate]") {
    SECTION("Bed temperature range (0-120°C)") {
        int temp = 60;
        bool result = validate_and_clamp(temp, 0, 120, "Bed", "target");
        REQUIRE(result == true);
        REQUIRE(temp == 60);

        temp = 130;
        result = validate_and_clamp(temp, 0, 120, "Bed", "target");
        REQUIRE(result == false);
        REQUIRE(temp == 120);
    }

    SECTION("Nozzle temperature range (0-300°C)") {
        int temp = 210;
        bool result = validate_and_clamp(temp, 0, 300, "Nozzle", "target");
        REQUIRE(result == true);
        REQUIRE(temp == 210);

        temp = 350;
        result = validate_and_clamp(temp, 0, 300, "Nozzle", "target");
        REQUIRE(result == false);
        REQUIRE(temp == 300);
    }
}

// ============================================================================
// validate_and_clamp_pair() Tests
// ============================================================================

TEST_CASE("Temperature Utils: validate_and_clamp_pair - both valid", "[temp_utils][validate]") {
    int current = 200;
    int target = 210;

    bool result = validate_and_clamp_pair(current, target, 0, 300, "Test");

    REQUIRE(result == true);
    REQUIRE(current == 200);
    REQUIRE(target == 210);
}

TEST_CASE("Temperature Utils: validate_and_clamp_pair - current invalid", "[temp_utils][validate][clamp]") {
    int current = -10;
    int target = 210;

    bool result = validate_and_clamp_pair(current, target, 0, 300, "Test");

    REQUIRE(result == false);
    REQUIRE(current == 0);  // Clamped
    REQUIRE(target == 210);  // Unchanged
}

TEST_CASE("Temperature Utils: validate_and_clamp_pair - target invalid", "[temp_utils][validate][clamp]") {
    int current = 200;
    int target = 350;

    bool result = validate_and_clamp_pair(current, target, 0, 300, "Test");

    REQUIRE(result == false);
    REQUIRE(current == 200);  // Unchanged
    REQUIRE(target == 300);  // Clamped
}

TEST_CASE("Temperature Utils: validate_and_clamp_pair - both invalid", "[temp_utils][validate][clamp]") {
    int current = -50;
    int target = 400;

    bool result = validate_and_clamp_pair(current, target, 0, 300, "Test");

    REQUIRE(result == false);
    REQUIRE(current == 0);  // Clamped to min
    REQUIRE(target == 300);  // Clamped to max
}

TEST_CASE("Temperature Utils: validate_and_clamp_pair - realistic scenarios", "[temp_utils][validate]") {
    SECTION("Heating up bed") {
        int current = 25;  // Room temp
        int target = 60;

        bool result = validate_and_clamp_pair(current, target, 0, 120, "Bed");

        REQUIRE(result == true);
        REQUIRE(current == 25);
        REQUIRE(target == 60);
    }

    SECTION("Cooling down nozzle") {
        int current = 180;
        int target = 0;

        bool result = validate_and_clamp_pair(current, target, 0, 300, "Nozzle");

        REQUIRE(result == true);
        REQUIRE(current == 180);
        REQUIRE(target == 0);
    }

    SECTION("At target temperature") {
        int current = 210;
        int target = 210;

        bool result = validate_and_clamp_pair(current, target, 0, 300, "Nozzle");

        REQUIRE(result == true);
        REQUIRE(current == 210);
        REQUIRE(target == 210);
    }
}

// ============================================================================
// is_extrusion_safe() Tests
// ============================================================================

TEST_CASE("Temperature Utils: is_extrusion_safe - above minimum", "[temp_utils][safety]") {
    REQUIRE(is_extrusion_safe(200, 170) == true);
    REQUIRE(is_extrusion_safe(250, 170) == true);
    REQUIRE(is_extrusion_safe(300, 170) == true);
}

TEST_CASE("Temperature Utils: is_extrusion_safe - at minimum", "[temp_utils][safety]") {
    REQUIRE(is_extrusion_safe(170, 170) == true);
}

TEST_CASE("Temperature Utils: is_extrusion_safe - below minimum", "[temp_utils][safety]") {
    REQUIRE(is_extrusion_safe(169, 170) == false);
    REQUIRE(is_extrusion_safe(100, 170) == false);
    REQUIRE(is_extrusion_safe(25, 170) == false);
    REQUIRE(is_extrusion_safe(0, 170) == false);
}

TEST_CASE("Temperature Utils: is_extrusion_safe - edge cases", "[temp_utils][safety][edge]") {
    SECTION("Exactly at boundary") {
        REQUIRE(is_extrusion_safe(170, 170) == true);
    }

    SECTION("One degree below") {
        REQUIRE(is_extrusion_safe(169, 170) == false);
    }

    SECTION("One degree above") {
        REQUIRE(is_extrusion_safe(171, 170) == true);
    }
}

TEST_CASE("Temperature Utils: is_extrusion_safe - different minimums", "[temp_utils][safety]") {
    SECTION("Low minimum (150°C)") {
        REQUIRE(is_extrusion_safe(160, 150) == true);
        REQUIRE(is_extrusion_safe(140, 150) == false);
    }

    SECTION("High minimum (200°C)") {
        REQUIRE(is_extrusion_safe(210, 200) == true);
        REQUIRE(is_extrusion_safe(190, 200) == false);
    }

    SECTION("Zero minimum (testing only)") {
        REQUIRE(is_extrusion_safe(0, 0) == true);
        REQUIRE(is_extrusion_safe(100, 0) == true);
    }
}

// ============================================================================
// get_extrusion_safety_status() Tests
// ============================================================================

TEST_CASE("Temperature Utils: get_extrusion_safety_status - safe", "[temp_utils][safety]") {
    const char* status = get_extrusion_safety_status(200, 170);
    REQUIRE(std::string(status) == "Ready");
}

TEST_CASE("Temperature Utils: get_extrusion_safety_status - at minimum", "[temp_utils][safety]") {
    const char* status = get_extrusion_safety_status(170, 170);
    REQUIRE(std::string(status) == "Ready");
}

TEST_CASE("Temperature Utils: get_extrusion_safety_status - heating", "[temp_utils][safety]") {
    SECTION("10°C below minimum") {
        const char* status = get_extrusion_safety_status(160, 170);
        std::string status_str(status);
        REQUIRE(status_str.find("Heating") != std::string::npos);
        REQUIRE(status_str.find("10") != std::string::npos);
    }

    SECTION("50°C below minimum") {
        const char* status = get_extrusion_safety_status(120, 170);
        std::string status_str(status);
        REQUIRE(status_str.find("Heating") != std::string::npos);
        REQUIRE(status_str.find("50") != std::string::npos);
    }

    SECTION("1°C below minimum") {
        const char* status = get_extrusion_safety_status(169, 170);
        std::string status_str(status);
        REQUIRE(status_str.find("Heating") != std::string::npos);
        REQUIRE(status_str.find("1") != std::string::npos);
    }
}

TEST_CASE("Temperature Utils: get_extrusion_safety_status - cold start", "[temp_utils][safety]") {
    const char* status = get_extrusion_safety_status(25, 170);
    std::string status_str(status);

    REQUIRE(status_str.find("Heating") != std::string::npos);
    REQUIRE(status_str.find("145") != std::string::npos);  // 170 - 25 = 145
}

TEST_CASE("Temperature Utils: get_extrusion_safety_status - edge cases", "[temp_utils][safety][edge]") {
    SECTION("One degree below") {
        const char* status = get_extrusion_safety_status(169, 170);
        std::string status_str(status);
        REQUIRE(status_str.find("1") != std::string::npos);
        REQUIRE(status_str.find("below minimum") != std::string::npos);
    }

    SECTION("Exactly at minimum") {
        const char* status = get_extrusion_safety_status(170, 170);
        REQUIRE(std::string(status) == "Ready");
    }

    SECTION("Well above minimum") {
        const char* status = get_extrusion_safety_status(250, 170);
        REQUIRE(std::string(status) == "Ready");
    }
}

// ============================================================================
// Integration Scenarios
// ============================================================================

TEST_CASE("Temperature Utils: Integration - PLA printing scenario", "[temp_utils][integration]") {
    int nozzle_current = 205;
    int nozzle_target = 210;
    int bed_current = 60;
    int bed_target = 60;

    // Validate nozzle temps
    bool nozzle_valid = validate_and_clamp_pair(nozzle_current, nozzle_target, 0, 300, "Nozzle");
    REQUIRE(nozzle_valid == true);

    // Validate bed temps
    bool bed_valid = validate_and_clamp_pair(bed_current, bed_target, 0, 120, "Bed");
    REQUIRE(bed_valid == true);

    // Check extrusion safety
    REQUIRE(is_extrusion_safe(nozzle_current, 170) == true);

    const char* status = get_extrusion_safety_status(nozzle_current, 170);
    REQUIRE(std::string(status) == "Ready");
}

TEST_CASE("Temperature Utils: Integration - Cold start scenario", "[temp_utils][integration]") {
    int nozzle_current = 22;  // Room temperature
    int nozzle_target = 210;

    // Validate temps
    bool valid = validate_and_clamp_pair(nozzle_current, nozzle_target, 0, 300, "Nozzle");
    REQUIRE(valid == true);

    // Not safe for extrusion yet
    REQUIRE(is_extrusion_safe(nozzle_current, 170) == false);

    const char* status = get_extrusion_safety_status(nozzle_current, 170);
    std::string status_str(status);
    REQUIRE(status_str.find("Heating") != std::string::npos);
    REQUIRE(status_str.find("148") != std::string::npos);  // 170 - 22 = 148
}

TEST_CASE("Temperature Utils: Integration - Invalid input handling", "[temp_utils][integration][error]") {
    int nozzle_current = 500;  // Way too high
    int nozzle_target = -50;   // Invalid negative

    // Should clamp both
    bool valid = validate_and_clamp_pair(nozzle_current, nozzle_target, 0, 300, "Nozzle");
    REQUIRE(valid == false);
    REQUIRE(nozzle_current == 300);  // Clamped to max
    REQUIRE(nozzle_target == 0);     // Clamped to min

    // After clamping, nozzle is safe for extrusion
    REQUIRE(is_extrusion_safe(nozzle_current, 170) == true);
}

TEST_CASE("Temperature Utils: Integration - ABS printing scenario", "[temp_utils][integration]") {
    int nozzle_current = 245;
    int nozzle_target = 250;
    int bed_current = 100;
    int bed_target = 100;

    // Validate nozzle temps (higher for ABS)
    bool nozzle_valid = validate_and_clamp_pair(nozzle_current, nozzle_target, 0, 300, "Nozzle");
    REQUIRE(nozzle_valid == true);

    // Validate bed temps (higher for ABS)
    bool bed_valid = validate_and_clamp_pair(bed_current, bed_target, 0, 120, "Bed");
    REQUIRE(bed_valid == true);

    // Check extrusion safety (higher minimum for ABS)
    REQUIRE(is_extrusion_safe(nozzle_current, 220) == true);

    const char* status = get_extrusion_safety_status(nozzle_current, 220);
    REQUIRE(std::string(status) == "Ready");
}
