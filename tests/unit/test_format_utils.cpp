// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "format_utils.h"

#include <cstring>
#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix::fmt;

// =============================================================================
// UNAVAILABLE constant
// =============================================================================

TEST_CASE("UNAVAILABLE constant is em dash", "[format_utils]") {
    CHECK(std::string(UNAVAILABLE) == "—");
}

// =============================================================================
// Percentage formatting
// =============================================================================

TEST_CASE("format_percent basic cases", "[format_utils][percent]") {
    char buf[16];

    SECTION("formats integer percentages") {
        CHECK(std::string(format_percent(0, buf, sizeof(buf))) == "0%");
        CHECK(std::string(format_percent(45, buf, sizeof(buf))) == "45%");
        CHECK(std::string(format_percent(100, buf, sizeof(buf))) == "100%");
    }

    SECTION("handles boundary values") {
        CHECK(std::string(format_percent(-5, buf, sizeof(buf))) == "-5%");
        CHECK(std::string(format_percent(255, buf, sizeof(buf))) == "255%");
    }
}

TEST_CASE("format_percent_or_unavailable", "[format_utils][percent]") {
    char buf[16];

    SECTION("returns formatted percent when available") {
        CHECK(std::string(format_percent_or_unavailable(50, true, buf, sizeof(buf))) == "50%");
    }

    SECTION("returns UNAVAILABLE when not available") {
        CHECK(std::string(format_percent_or_unavailable(50, false, buf, sizeof(buf))) == "—");
    }
}

TEST_CASE("format_percent_float with decimals", "[format_utils][percent]") {
    char buf[16];

    SECTION("formats with 0 decimals") {
        CHECK(std::string(format_percent_float(45.7, 0, buf, sizeof(buf))) == "46%");
        CHECK(std::string(format_percent_float(100.0, 0, buf, sizeof(buf))) == "100%");
    }

    SECTION("formats with 1 decimal") {
        CHECK(std::string(format_percent_float(45.5, 1, buf, sizeof(buf))) == "45.5%");
        CHECK(std::string(format_percent_float(99.9, 1, buf, sizeof(buf))) == "99.9%");
    }

    SECTION("formats with 2 decimals") {
        CHECK(std::string(format_percent_float(45.55, 2, buf, sizeof(buf))) == "45.55%");
    }
}

TEST_CASE("format_humidity from x10 value", "[format_utils][percent]") {
    char buf[16];

    SECTION("converts x10 values to whole percent") {
        CHECK(std::string(format_humidity(455, buf, sizeof(buf))) == "45%");
        CHECK(std::string(format_humidity(1000, buf, sizeof(buf))) == "100%");
        CHECK(std::string(format_humidity(0, buf, sizeof(buf))) == "0%");
    }

    SECTION("rounds correctly") {
        CHECK(std::string(format_humidity(456, buf, sizeof(buf))) == "45%");
        CHECK(std::string(format_humidity(459, buf, sizeof(buf))) == "45%");
    }
}

// =============================================================================
// Distance formatting
// =============================================================================

TEST_CASE("format_distance_mm with precision", "[format_utils][distance]") {
    char buf[32];

    SECTION("formats with specified precision") {
        CHECK(std::string(format_distance_mm(1.234, 2, buf, sizeof(buf))) == "1.23 mm");
        CHECK(std::string(format_distance_mm(0.1, 3, buf, sizeof(buf))) == "0.100 mm");
        CHECK(std::string(format_distance_mm(10.0, 0, buf, sizeof(buf))) == "10 mm");
    }

    SECTION("handles negative values") {
        CHECK(std::string(format_distance_mm(-0.5, 2, buf, sizeof(buf))) == "-0.50 mm");
    }
}

TEST_CASE("format_diameter_mm fixed 2 decimals", "[format_utils][distance]") {
    char buf[32];

    CHECK(std::string(format_diameter_mm(1.75f, buf, sizeof(buf))) == "1.75 mm");
    CHECK(std::string(format_diameter_mm(2.85f, buf, sizeof(buf))) == "2.85 mm");
    CHECK(std::string(format_diameter_mm(1.0f, buf, sizeof(buf))) == "1.00 mm");
}

// =============================================================================
// Speed formatting
// =============================================================================

TEST_CASE("format_speed_mm_s", "[format_utils][speed]") {
    char buf[32];

    CHECK(std::string(format_speed_mm_s(150.0, buf, sizeof(buf))) == "150 mm/s");
    CHECK(std::string(format_speed_mm_s(0.0, buf, sizeof(buf))) == "0 mm/s");
    CHECK(std::string(format_speed_mm_s(300.5, buf, sizeof(buf))) == "300 mm/s");
}

TEST_CASE("format_speed_mm_min", "[format_utils][speed]") {
    char buf[32];

    CHECK(std::string(format_speed_mm_min(300.0, buf, sizeof(buf))) == "300 mm/min");
    CHECK(std::string(format_speed_mm_min(0.0, buf, sizeof(buf))) == "0 mm/min");
}

// =============================================================================
// Acceleration formatting
// =============================================================================

TEST_CASE("format_accel_mm_s2", "[format_utils][accel]") {
    char buf[32];

    CHECK(std::string(format_accel_mm_s2(3000.0, buf, sizeof(buf))) == "3000 mm/s²");
    CHECK(std::string(format_accel_mm_s2(500.0, buf, sizeof(buf))) == "500 mm/s²");
    CHECK(std::string(format_accel_mm_s2(0.0, buf, sizeof(buf))) == "0 mm/s²");
}

// =============================================================================
// Frequency formatting
// =============================================================================

TEST_CASE("format_frequency_hz", "[format_utils][frequency]") {
    char buf[32];

    CHECK(std::string(format_frequency_hz(48.5, buf, sizeof(buf))) == "48.5 Hz");
    CHECK(std::string(format_frequency_hz(60.0, buf, sizeof(buf))) == "60.0 Hz");
    CHECK(std::string(format_frequency_hz(0.0, buf, sizeof(buf))) == "0.0 Hz");
}

// =============================================================================
// Buffer safety
// =============================================================================

TEST_CASE("formatters handle small buffers safely", "[format_utils][safety]") {
    char tiny[4];

    SECTION("percent truncates safely") {
        format_percent(100, tiny, sizeof(tiny));
        CHECK(tiny[sizeof(tiny) - 1] == '\0');
    }

    SECTION("distance truncates safely") {
        format_distance_mm(123.456, 2, tiny, sizeof(tiny));
        CHECK(tiny[sizeof(tiny) - 1] == '\0');
    }
}

// ============================================================================
// Temperature formatting tests
// ============================================================================

TEST_CASE("format_temp basic cases", "[format_utils][temperature]") {
    char buf[16];

    SECTION("formats positive temperatures") {
        CHECK(std::string(format_temp(0, buf, sizeof(buf))) == "0°C");
        CHECK(std::string(format_temp(25, buf, sizeof(buf))) == "25°C");
        CHECK(std::string(format_temp(210, buf, sizeof(buf))) == "210°C");
    }

    SECTION("handles negative temperatures") {
        CHECK(std::string(format_temp(-10, buf, sizeof(buf))) == "-10°C");
        CHECK(std::string(format_temp(-40, buf, sizeof(buf))) == "-40°C");
    }

    SECTION("handles high temperatures") {
        CHECK(std::string(format_temp(300, buf, sizeof(buf))) == "300°C");
        CHECK(std::string(format_temp(500, buf, sizeof(buf))) == "500°C");
    }
}

TEST_CASE("format_temp_pair basic cases", "[format_utils][temperature]") {
    char buf[32];

    SECTION("formats current/target pair") {
        CHECK(std::string(format_temp_pair(150, 200, buf, sizeof(buf))) == "150 / 200°C");
        CHECK(std::string(format_temp_pair(0, 60, buf, sizeof(buf))) == "0 / 60°C");
        CHECK(std::string(format_temp_pair(210, 210, buf, sizeof(buf))) == "210 / 210°C");
    }

    SECTION("shows em dash when target is 0 (heater off)") {
        CHECK(std::string(format_temp_pair(25, 0, buf, sizeof(buf))) == "25 / —°C");
        CHECK(std::string(format_temp_pair(0, 0, buf, sizeof(buf))) == "0 / —°C");
    }
}

TEST_CASE("format_temp_range basic cases", "[format_utils][temperature]") {
    char buf[24];

    SECTION("formats min-max range") {
        CHECK(std::string(format_temp_range(200, 230, buf, sizeof(buf))) == "200-230°C");
        CHECK(std::string(format_temp_range(60, 80, buf, sizeof(buf))) == "60-80°C");
        CHECK(std::string(format_temp_range(180, 220, buf, sizeof(buf))) == "180-220°C");
    }

    SECTION("handles same min and max") {
        CHECK(std::string(format_temp_range(200, 200, buf, sizeof(buf))) == "200-200°C");
    }
}

TEST_CASE("temperature formatters handle small buffers safely",
          "[format_utils][temperature][safety]") {
    char tiny[4];

    SECTION("format_temp truncates safely") {
        format_temp(999, tiny, sizeof(tiny));
        CHECK(tiny[sizeof(tiny) - 1] == '\0');
    }

    SECTION("format_temp_pair truncates safely") {
        format_temp_pair(100, 200, tiny, sizeof(tiny));
        CHECK(tiny[sizeof(tiny) - 1] == '\0');
    }

    SECTION("format_temp_range truncates safely") {
        format_temp_range(100, 200, tiny, sizeof(tiny));
        CHECK(tiny[sizeof(tiny) - 1] == '\0');
    }
}

// ============================================================================
// heater_display() tests
// ============================================================================

TEST_CASE("heater_display() cold heater shows temperature only", "[format_utils][heater_display]") {
    // 2500 centi-degrees = 25.00°C, target 0 = off
    auto result = heater_display(2500, 0);
    REQUIRE(result.temp == "25°C");
    REQUIRE(result.status == "Off");
    REQUIRE(result.pct == 0);
}

TEST_CASE("heater_display() heating shows current/target and percentage",
          "[format_utils][heater_display]") {
    // 15000 centi = 150°C, target 20000 centi = 200°C -> 75%
    auto result = heater_display(15000, 20000);
    REQUIRE(result.temp == "150 / 200°C");
    REQUIRE(result.status == "Heating...");
    REQUIRE(result.pct == 75);
}

TEST_CASE("heater_display() at temperature shows Ready", "[format_utils][heater_display]") {
    // 19800 centi = 198°C / 20000 centi = 200°C target -> 99%
    auto result = heater_display(19800, 20000);
    REQUIRE(result.temp == "198 / 200°C");
    REQUIRE(result.status == "Ready");
    REQUIRE(result.pct == 99);
}

TEST_CASE("heater_display() cooling shows Cooling when above target",
          "[format_utils][heater_display]") {
    // 21000 centi = 210°C with 200°C target -> over by 10°C -> Cooling
    auto result = heater_display(21000, 20000);
    REQUIRE(result.pct == 100);
    REQUIRE(result.status == "Cooling");
}

TEST_CASE("heater_display() percentage clamps to 0-100", "[format_utils][heater_display]") {
    SECTION("over target clamps to 100") {
        auto result = heater_display(21000, 20000);
        REQUIRE(result.pct == 100);
    }

    SECTION("negative temperature clamps to 0") {
        // Edge case: negative temp (shouldn't happen but be safe)
        auto result = heater_display(-100, 20000);
        REQUIRE(result.pct == 0);
    }
}

TEST_CASE("heater_display() edge cases", "[format_utils][heater_display]") {
    SECTION("within tolerance of target shows Ready") {
        // 199°C with 200°C target -> within ±2°C -> Ready
        auto result = heater_display(19900, 20000);
        REQUIRE(result.pct == 99);
        REQUIRE(result.status == "Ready");
    }

    SECTION("just outside heating tolerance shows Heating") {
        // 197°C with 200°C target -> 197 < 200-2=198 -> Heating
        auto result = heater_display(19700, 20000);
        REQUIRE(result.pct == 98);
        REQUIRE(result.status == "Heating...");
    }

    SECTION("just outside cooling tolerance shows Cooling") {
        // 203°C with 200°C target -> 203 > 200+2=202 -> Cooling
        auto result = heater_display(20300, 20000);
        REQUIRE(result.pct == 100);
        REQUIRE(result.status == "Cooling");
    }

    SECTION("exactly at lower tolerance boundary shows Ready") {
        // 198°C with 200°C target -> 198 >= 200-2 -> Ready
        auto result = heater_display(19800, 20000);
        REQUIRE(result.status == "Ready");
    }

    SECTION("exactly at upper tolerance boundary shows Ready") {
        // 202°C with 200°C target -> 202 <= 200+2 -> Ready
        auto result = heater_display(20200, 20000);
        REQUIRE(result.status == "Ready");
    }

    SECTION("very low target temperature") {
        // 4000 centi = 40°C with 50°C target
        auto result = heater_display(4000, 5000);
        REQUIRE(result.temp == "40 / 50°C");
        REQUIRE(result.pct == 80);
        REQUIRE(result.status == "Heating...");
    }

    SECTION("zero current temperature") {
        auto result = heater_display(0, 20000);
        REQUIRE(result.temp == "0 / 200°C");
        REQUIRE(result.pct == 0);
        REQUIRE(result.status == "Heating...");
    }
}

// =============================================================================
// Duration formatting (padded)
// =============================================================================

TEST_CASE("duration_padded shows seconds under 5 minutes", "[format_utils][duration]") {
    SECTION("zero seconds") {
        CHECK(duration_padded(0) == "0s");
    }

    SECTION("negative values") {
        CHECK(duration_padded(-10) == "0s");
    }

    SECTION("under 1 minute shows seconds only") {
        CHECK(duration_padded(5) == "5s");
        CHECK(duration_padded(30) == "30s");
        CHECK(duration_padded(59) == "59s");
    }

    SECTION("1 to 4 minutes shows minutes and seconds") {
        CHECK(duration_padded(60) == "1m 00s");
        CHECK(duration_padded(90) == "1m 30s");
        CHECK(duration_padded(150) == "2m 30s");
        CHECK(duration_padded(299) == "4m 59s");
    }

    SECTION("5 minutes and above shows minutes only") {
        CHECK(duration_padded(300) == "5m");
        CHECK(duration_padded(360) == "6m");
        CHECK(duration_padded(600) == "10m");
        CHECK(duration_padded(3540) == "59m");
    }

    SECTION("hours shows hours and padded minutes") {
        CHECK(duration_padded(3600) == "1h 00m");
        CHECK(duration_padded(3660) == "1h 01m");
        CHECK(duration_padded(7200) == "2h 00m");
        CHECK(duration_padded(7830) == "2h 10m");
    }
}

// =============================================================================
// Duration remaining formatting
// =============================================================================

TEST_CASE("duration_remaining shows seconds under 5 minutes", "[format_utils][duration]") {
    SECTION("zero seconds") {
        CHECK(duration_remaining(0) == "0 min left");
    }

    SECTION("negative values") {
        CHECK(duration_remaining(-10) == "0 min left");
    }

    SECTION("under 1 minute shows 0:SS") {
        CHECK(duration_remaining(5) == "0:05 left");
        CHECK(duration_remaining(30) == "0:30 left");
        CHECK(duration_remaining(59) == "0:59 left");
    }

    SECTION("1 to 4 minutes shows M:SS") {
        CHECK(duration_remaining(60) == "1:00 left");
        CHECK(duration_remaining(90) == "1:30 left");
        CHECK(duration_remaining(150) == "2:30 left");
        CHECK(duration_remaining(299) == "4:59 left");
    }

    SECTION("5 minutes and above shows minutes") {
        CHECK(duration_remaining(300) == "5 min left");
        CHECK(duration_remaining(360) == "6 min left");
        CHECK(duration_remaining(600) == "10 min left");
    }

    SECTION("hours shows H:MM") {
        CHECK(duration_remaining(3600) == "1:00 left");
        CHECK(duration_remaining(3660) == "1:01 left");
        CHECK(duration_remaining(7200) == "2:00 left");
    }
}

// =============================================================================
// Filament length formatting
// =============================================================================

TEST_CASE("format_filament_length formats correctly", "[format_utils][filament]") {
    SECTION("sub-meter values show as mm") {
        CHECK(format_filament_length(0) == "0mm");
        CHECK(format_filament_length(1) == "1mm");
        CHECK(format_filament_length(500) == "500mm");
        CHECK(format_filament_length(999) == "999mm");
    }

    SECTION("meter-range values show as meters with 1 decimal") {
        CHECK(format_filament_length(1000) == "1.0m");
        CHECK(format_filament_length(1500) == "1.5m");
        CHECK(format_filament_length(12500) == "12.5m");
        CHECK(format_filament_length(999999) == "1000.0m");
    }

    SECTION("kilometer-range values show as km with 2 decimals") {
        CHECK(format_filament_length(1000000) == "1.00km");
        CHECK(format_filament_length(1230000) == "1.23km");
    }
}
