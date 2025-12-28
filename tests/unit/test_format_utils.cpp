// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 HelixScreen Authors

#include "format_utils.h"

#include "../catch_amalgamated.hpp"

using namespace helix::fmt;

// ============================================================================
// duration() tests
// ============================================================================

TEST_CASE("duration() handles edge cases", "[format_utils][duration]") {
    SECTION("zero seconds") {
        REQUIRE(duration(0) == "0s");
    }

    SECTION("negative values treated as zero") {
        REQUIRE(duration(-1) == "0s");
        REQUIRE(duration(-100) == "0s");
    }
}

TEST_CASE("duration() formats seconds correctly", "[format_utils][duration]") {
    SECTION("1 second") {
        REQUIRE(duration(1) == "1s");
    }

    SECTION("30 seconds") {
        REQUIRE(duration(30) == "30s");
    }

    SECTION("59 seconds") {
        REQUIRE(duration(59) == "59s");
    }
}

TEST_CASE("duration() formats minutes correctly", "[format_utils][duration]") {
    SECTION("exactly 1 minute") {
        REQUIRE(duration(60) == "1m");
    }

    SECTION("1 minute 30 seconds (rounds to 1m)") {
        REQUIRE(duration(90) == "1m");
    }

    SECTION("30 minutes") {
        REQUIRE(duration(1800) == "30m");
    }

    SECTION("59 minutes") {
        REQUIRE(duration(3540) == "59m");
    }
}

TEST_CASE("duration() formats hours correctly", "[format_utils][duration]") {
    SECTION("exactly 1 hour") {
        REQUIRE(duration(3600) == "1h");
    }

    SECTION("1 hour 30 minutes") {
        REQUIRE(duration(5400) == "1h 30m");
    }

    SECTION("2 hours") {
        REQUIRE(duration(7200) == "2h");
    }

    SECTION("2 hours 5 minutes") {
        REQUIRE(duration(7500) == "2h 5m");
    }

    SECTION("24 hours") {
        REQUIRE(duration(86400) == "24h");
    }

    SECTION("over 24 hours") {
        REQUIRE(duration(90000) == "25h");
    }

    SECTION("100 hours") {
        REQUIRE(duration(360000) == "100h");
    }
}

TEST_CASE("duration() handles extreme values", "[format_utils][duration][edge]") {
    SECTION("INT_MAX seconds (~68 years)") {
        // 2147483647 seconds = 596523h 14m 7s
        std::string result = duration(2147483647);
        // Should format as hours + minutes (dropping seconds for >1hr)
        REQUIRE(result.find("596523h") != std::string::npos);
        REQUIRE(result.length() < 32); // Ensure no buffer overflow
    }

    SECTION("one year in seconds") {
        // 365 * 24 * 3600 = 31536000 seconds = 8760h
        REQUIRE(duration(31536000) == "8760h");
    }
}

// ============================================================================
// duration_remaining() tests
// ============================================================================

TEST_CASE("duration_remaining() handles edge cases", "[format_utils][duration_remaining]") {
    SECTION("zero seconds shows 0 min left") {
        REQUIRE(duration_remaining(0) == "0 min left");
    }

    SECTION("negative values treated as zero") {
        REQUIRE(duration_remaining(-1) == "0 min left");
    }
}

TEST_CASE("duration_remaining() formats short durations", "[format_utils][duration_remaining]") {
    SECTION("30 seconds shows 1 min left (minimum)") {
        REQUIRE(duration_remaining(30) == "1 min left");
    }

    SECTION("1 minute") {
        REQUIRE(duration_remaining(60) == "1 min left");
    }

    SECTION("45 minutes") {
        REQUIRE(duration_remaining(2700) == "45 min left");
    }

    SECTION("59 minutes") {
        REQUIRE(duration_remaining(3540) == "59 min left");
    }
}

TEST_CASE("duration_remaining() formats long durations as H:MM",
          "[format_utils][duration_remaining]") {
    SECTION("exactly 1 hour") {
        REQUIRE(duration_remaining(3600) == "1:00 left");
    }

    SECTION("1 hour 5 minutes (zero-padded)") {
        REQUIRE(duration_remaining(3900) == "1:05 left");
    }

    SECTION("1 hour 30 minutes") {
        REQUIRE(duration_remaining(5400) == "1:30 left");
    }

    SECTION("2 hours 15 minutes") {
        REQUIRE(duration_remaining(8100) == "2:15 left");
    }

    SECTION("10 hours") {
        REQUIRE(duration_remaining(36000) == "10:00 left");
    }
}

// ============================================================================
// duration_from_minutes() tests
// ============================================================================

TEST_CASE("duration_from_minutes() handles edge cases", "[format_utils][duration_from_minutes]") {
    SECTION("zero minutes") {
        REQUIRE(duration_from_minutes(0) == "0 min");
    }

    SECTION("negative values treated as zero") {
        REQUIRE(duration_from_minutes(-1) == "0 min");
    }
}

TEST_CASE("duration_from_minutes() formats correctly", "[format_utils][duration_from_minutes]") {
    SECTION("1 minute") {
        REQUIRE(duration_from_minutes(1) == "1 min");
    }

    SECTION("45 minutes") {
        REQUIRE(duration_from_minutes(45) == "45 min");
    }

    SECTION("59 minutes") {
        REQUIRE(duration_from_minutes(59) == "59 min");
    }

    SECTION("exactly 1 hour") {
        REQUIRE(duration_from_minutes(60) == "1h");
    }

    SECTION("1 hour 30 minutes") {
        REQUIRE(duration_from_minutes(90) == "1h 30m");
    }

    SECTION("2 hours 5 minutes") {
        REQUIRE(duration_from_minutes(125) == "2h 5m");
    }

    SECTION("24 hours") {
        REQUIRE(duration_from_minutes(1440) == "24h");
    }
}

// ============================================================================
// duration_to_buffer() tests
// ============================================================================

TEST_CASE("duration_to_buffer() handles edge cases", "[format_utils][duration_to_buffer]") {
    char buf[32];

    SECTION("null buffer returns 0") {
        REQUIRE(duration_to_buffer(nullptr, 32, 100) == 0);
    }

    SECTION("zero size returns 0") {
        REQUIRE(duration_to_buffer(buf, 0, 100) == 0);
    }

    SECTION("zero seconds") {
        size_t written = duration_to_buffer(buf, sizeof(buf), 0);
        REQUIRE(written == 2);
        REQUIRE(std::string(buf) == "0s");
    }

    SECTION("negative values") {
        size_t written = duration_to_buffer(buf, sizeof(buf), -100);
        REQUIRE(written == 2);
        REQUIRE(std::string(buf) == "0s");
    }
}

TEST_CASE("duration_to_buffer() formats correctly", "[format_utils][duration_to_buffer]") {
    char buf[32];

    SECTION("30 seconds") {
        size_t written = duration_to_buffer(buf, sizeof(buf), 30);
        REQUIRE(written == 3);
        REQUIRE(std::string(buf) == "30s");
    }

    SECTION("5 minutes") {
        size_t written = duration_to_buffer(buf, sizeof(buf), 300);
        REQUIRE(written == 2);
        REQUIRE(std::string(buf) == "5m");
    }

    SECTION("1 hour 30 minutes") {
        size_t written = duration_to_buffer(buf, sizeof(buf), 5400);
        REQUIRE(written == 6); // "1h 30m" = 6 chars
        REQUIRE(std::string(buf) == "1h 30m");
    }
}

TEST_CASE("duration_to_buffer() handles small buffers", "[format_utils][duration_to_buffer]") {
    char buf[4];

    SECTION("buffer too small for full output") {
        // "1h 30m" needs 7 bytes (6 chars + null), we only have 4
        // snprintf will truncate but return what it would have written
        size_t written = duration_to_buffer(buf, sizeof(buf), 5400);
        // snprintf returns what would have been written without truncation
        REQUIRE(written == 6); // "1h 30m" = 6 chars
        // Buffer will be truncated to "1h " (3 chars + null)
        REQUIRE(std::string(buf) == "1h ");
    }
}

// ============================================================================
// duration_padded() tests
// ============================================================================

TEST_CASE("duration_padded() handles edge cases", "[format_utils][duration_padded]") {
    SECTION("zero seconds") {
        REQUIRE(duration_padded(0) == "0m");
    }

    SECTION("negative values treated as zero") {
        REQUIRE(duration_padded(-1) == "0m");
    }
}

TEST_CASE("duration_padded() zero-pads minutes for hours", "[format_utils][duration_padded]") {
    SECTION("under 1 hour - no padding") {
        REQUIRE(duration_padded(300) == "5m");
        REQUIRE(duration_padded(1800) == "30m");
    }

    SECTION("exactly 1 hour - zero-padded") {
        REQUIRE(duration_padded(3600) == "1h 00m");
    }

    SECTION("1 hour 5 minutes - zero-padded") {
        REQUIRE(duration_padded(3900) == "1h 05m");
    }

    SECTION("1 hour 30 minutes") {
        REQUIRE(duration_padded(5400) == "1h 30m");
    }

    SECTION("2 hours") {
        REQUIRE(duration_padded(7200) == "2h 00m");
    }
}
