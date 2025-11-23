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
#include "ui_utils.h"
#include "ui_theme.h"
#include "../ui_test_utils.h"

using Catch::Approx;

// ============================================================================
// format_print_time() Tests
// ============================================================================

TEST_CASE("UI Utils: format_print_time - minutes only", "[ui_utils][format]") {
    REQUIRE(format_print_time(0) == "0m");
    REQUIRE(format_print_time(5) == "5m");
    REQUIRE(format_print_time(59) == "59m");
}

TEST_CASE("UI Utils: format_print_time - hours and minutes", "[ui_utils][format]") {
    REQUIRE(format_print_time(60) == "1h");
    REQUIRE(format_print_time(90) == "1h30m");
    REQUIRE(format_print_time(125) == "2h5m");
    REQUIRE(format_print_time(785) == "13h5m");
}

TEST_CASE("UI Utils: format_print_time - exact hours", "[ui_utils][format]") {
    REQUIRE(format_print_time(120) == "2h");
    REQUIRE(format_print_time(180) == "3h");
    REQUIRE(format_print_time(1440) == "24h");
}

TEST_CASE("UI Utils: format_print_time - edge cases", "[ui_utils][format][edge]") {
    SECTION("Zero minutes") {
        REQUIRE(format_print_time(0) == "0m");
    }

    SECTION("Very large values") {
        REQUIRE(format_print_time(10000) == "166h40m");
    }

    SECTION("One minute") {
        REQUIRE(format_print_time(1) == "1m");
    }

    SECTION("One hour exactly") {
        REQUIRE(format_print_time(60) == "1h");
    }

    SECTION("Almost two hours") {
        REQUIRE(format_print_time(119) == "1h59m");
    }
}

// ============================================================================
// format_filament_weight() Tests
// ============================================================================

TEST_CASE("UI Utils: format_filament_weight - less than 1 gram", "[ui_utils][format]") {
    REQUIRE(format_filament_weight(0.0f) == "0.0g");
    REQUIRE(format_filament_weight(0.5f) == "0.5g");
    REQUIRE(format_filament_weight(0.9f) == "0.9g");
}

TEST_CASE("UI Utils: format_filament_weight - 1-10 grams", "[ui_utils][format]") {
    REQUIRE(format_filament_weight(1.0f) == "1.0g");
    REQUIRE(format_filament_weight(2.5f) == "2.5g");
    REQUIRE(format_filament_weight(9.9f) == "9.9g");
}

TEST_CASE("UI Utils: format_filament_weight - 10+ grams", "[ui_utils][format]") {
    REQUIRE(format_filament_weight(10.0f) == "10g");
    REQUIRE(format_filament_weight(45.7f) == "46g");
    REQUIRE(format_filament_weight(120.3f) == "120g");
    REQUIRE(format_filament_weight(999.9f) == "1000g");
}

TEST_CASE("UI Utils: format_filament_weight - edge cases", "[ui_utils][format][edge]") {
    SECTION("Exactly 1 gram boundary") {
        REQUIRE(format_filament_weight(0.99f) == "1.0g");
        REQUIRE(format_filament_weight(1.0f) == "1.0g");
    }

    SECTION("Exactly 10 gram boundary") {
        REQUIRE(format_filament_weight(9.99f) == "10.0g");
        REQUIRE(format_filament_weight(10.0f) == "10g");
    }

    SECTION("Very large values") {
        REQUIRE(format_filament_weight(10000.0f) == "10000g");
    }
}

// ============================================================================
// format_file_size() Tests
// ============================================================================

TEST_CASE("UI Utils: format_file_size - bytes", "[ui_utils][format]") {
    REQUIRE(format_file_size(0) == "0 B");
    REQUIRE(format_file_size(512) == "512 B");
    REQUIRE(format_file_size(1023) == "1023 B");
}

TEST_CASE("UI Utils: format_file_size - kilobytes", "[ui_utils][format]") {
    REQUIRE(format_file_size(1024) == "1.0 KB");
    REQUIRE(format_file_size(1536) == "1.5 KB");
    REQUIRE(format_file_size(10240) == "10.0 KB");
    REQUIRE(format_file_size(1048575) == "1024.0 KB");
}

TEST_CASE("UI Utils: format_file_size - megabytes", "[ui_utils][format]") {
    REQUIRE(format_file_size(1048576) == "1.0 MB");
    REQUIRE(format_file_size(5242880) == "5.0 MB");
    REQUIRE(format_file_size(52428800) == "50.0 MB");
}

TEST_CASE("UI Utils: format_file_size - gigabytes", "[ui_utils][format]") {
    REQUIRE(format_file_size(1073741824) == "1.00 GB");
    REQUIRE(format_file_size(2147483648) == "2.00 GB");
    REQUIRE(format_file_size(5368709120) == "5.00 GB");
}

TEST_CASE("UI Utils: format_file_size - edge cases", "[ui_utils][format][edge]") {
    SECTION("Exactly at boundaries") {
        REQUIRE(format_file_size(1024) == "1.0 KB");
        REQUIRE(format_file_size(1048576) == "1.0 MB");
        REQUIRE(format_file_size(1073741824) == "1.00 GB");
    }

    SECTION("One byte before boundaries") {
        REQUIRE(format_file_size(1023) == "1023 B");
        REQUIRE(format_file_size(1048575) == "1024.0 KB");
    }

    SECTION("Common G-code file sizes") {
        REQUIRE(format_file_size(125000) == "122.1 KB");  // ~125 KB file
        REQUIRE(format_file_size(5800000) == "5.5 MB");   // ~5.8 MB file
    }
}

// ============================================================================
// format_modified_date() Tests
// ============================================================================

TEST_CASE("UI Utils: format_modified_date - valid timestamps", "[ui_utils][format]") {
    // January 15, 2025 14:30:00
    time_t timestamp = 1736954400; // Approximate timestamp

    std::string result = format_modified_date(timestamp);

    // Result should be in format "Jan 15 HH:MM" or similar
    // Just verify it's not empty and has expected length
    REQUIRE(!result.empty());
    REQUIRE(result.length() > 5);
}

TEST_CASE("UI Utils: format_modified_date - edge cases", "[ui_utils][format][edge]") {
    SECTION("Zero timestamp (epoch)") {
        std::string result = format_modified_date(0);
        REQUIRE(!result.empty());
    }

    SECTION("Recent timestamp") {
        time_t now = time(nullptr);
        std::string result = format_modified_date(now);
        REQUIRE(!result.empty());
    }
}

// ============================================================================
// ui_get_header_content_padding() Tests
// ============================================================================

TEST_CASE("UI Utils: ui_get_header_content_padding - screen sizes", "[ui_utils][responsive]") {
    SECTION("Tiny screen (320px)") {
        REQUIRE(ui_get_header_content_padding(320) == UI_PADDING_TINY);
    }

    SECTION("Small screen (480px)") {
        REQUIRE(ui_get_header_content_padding(480) == UI_PADDING_SMALL);
    }

    SECTION("Small screen upper boundary (599px)") {
        REQUIRE(ui_get_header_content_padding(599) == UI_PADDING_SMALL);
    }

    SECTION("Medium screen (600px)") {
        REQUIRE(ui_get_header_content_padding(600) == UI_PADDING_NORMAL);
    }

    SECTION("Large screen (800px)") {
        REQUIRE(ui_get_header_content_padding(800) == UI_PADDING_NORMAL);
    }

    SECTION("Extra large screen (1080px)") {
        REQUIRE(ui_get_header_content_padding(1080) == UI_PADDING_NORMAL);
    }
}

TEST_CASE("UI Utils: ui_get_header_content_padding - boundary values", "[ui_utils][responsive][edge]") {
    SECTION("At tiny/small boundary (479px)") {
        REQUIRE(ui_get_header_content_padding(479) == UI_PADDING_TINY);
    }

    SECTION("At small/medium boundary (600px)") {
        REQUIRE(ui_get_header_content_padding(600) == UI_PADDING_NORMAL);
    }

    SECTION("One pixel before medium (599px)") {
        REQUIRE(ui_get_header_content_padding(599) == UI_PADDING_SMALL);
    }
}

// ============================================================================
// ui_get_responsive_header_height() Tests
// ============================================================================

TEST_CASE("UI Utils: ui_get_responsive_header_height - screen sizes", "[ui_utils][responsive]") {
    SECTION("Tiny screen (320px)") {
        REQUIRE(ui_get_responsive_header_height(320) == 40);
    }

    SECTION("Small screen (480px)") {
        REQUIRE(ui_get_responsive_header_height(480) == 48);
    }

    SECTION("Small screen upper boundary (599px)") {
        REQUIRE(ui_get_responsive_header_height(599) == 48);
    }

    SECTION("Medium screen (600px)") {
        REQUIRE(ui_get_responsive_header_height(600) == 60);
    }

    SECTION("Large screen (800px)") {
        REQUIRE(ui_get_responsive_header_height(800) == 60);
    }

    SECTION("Extra large screen (1080px)") {
        REQUIRE(ui_get_responsive_header_height(1080) == 60);
    }
}

TEST_CASE("UI Utils: ui_get_responsive_header_height - boundary values", "[ui_utils][responsive][edge]") {
    SECTION("At tiny/small boundary (479px)") {
        REQUIRE(ui_get_responsive_header_height(479) == 40);
    }

    SECTION("Exactly at small threshold (480px)") {
        REQUIRE(ui_get_responsive_header_height(480) == 48);
    }

    SECTION("At small/medium boundary (600px)") {
        REQUIRE(ui_get_responsive_header_height(600) == 60);
    }

    SECTION("One pixel before medium (599px)") {
        REQUIRE(ui_get_responsive_header_height(599) == 48);
    }
}

// ============================================================================
// Image Scaling Tests (require LVGL)
// ============================================================================

TEST_CASE("UI Utils: ui_image_scale_to_cover - null widget", "[ui_utils][image][error]") {
    REQUIRE(ui_image_scale_to_cover(nullptr, 100, 100) == false);
}

TEST_CASE("UI Utils: ui_image_scale_to_contain - null widget", "[ui_utils][image][error]") {
    REQUIRE(ui_image_scale_to_contain(nullptr, 100, 100) == false);
}

// Note: Testing actual image scaling requires creating LVGL image widgets
// with valid image data, which is more complex. The basic error handling
// is tested above. Full integration tests would go in a separate test file.
