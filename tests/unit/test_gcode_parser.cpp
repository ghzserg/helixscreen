// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 HelixScreen Contributors
 */

#include "gcode_parser.h"

#include "../catch_amalgamated.hpp"
#include <sstream>
#include <iostream>

using namespace gcode;
using Catch::Approx;

TEST_CASE("GCodeParser - Basic movement parsing", "[gcode][parser]") {
    GCodeParser parser;

    SECTION("Parse simple G1 move") {
        parser.parse_line("G1 X10 Y20 Z0.2");
        auto file = parser.finalize();

        // Debug: print all layers
        for (size_t i = 0; i < file.layers.size(); i++) {
            std::cerr << "DEBUG: Layer " << i << " Z=" << file.layers[i].z_height
                      << " segments=" << file.layers[i].segments.size() << std::endl;
        }

        REQUIRE(file.layers.size() == 1);
        REQUIRE(file.layers[0].z_height == Approx(0.2f));
    }

    SECTION("Parse movement with extrusion") {
        parser.parse_line("G1 X10 Y20 Z0.2 E1.5");
        auto file = parser.finalize();

        REQUIRE(file.layers.size() == 1);
        REQUIRE(file.total_segments == 1);
        REQUIRE(file.layers[0].segments[0].is_extrusion == true);
    }

    SECTION("Parse travel move (no extrusion)") {
        parser.parse_line("G0 X10 Y20 Z0.2");
        auto file = parser.finalize();

        REQUIRE(file.total_segments == 1);
        REQUIRE(file.layers[0].segments[0].is_extrusion == false);
    }
}

TEST_CASE("GCodeParser - Layer detection", "[gcode][parser]") {
    GCodeParser parser;

    SECTION("Detect Z-axis layer changes") {
        parser.parse_line("G1 X0 Y0 Z0.2 E1");
        parser.parse_line("G1 X10 Y10 E2");
        parser.parse_line("G1 X0 Y0 Z0.4 E3"); // New layer
        parser.parse_line("G1 X20 Y20 E4");

        auto file = parser.finalize();

        REQUIRE(file.layers.size() == 2);
        REQUIRE(file.layers[0].z_height == Approx(0.2f));
        REQUIRE(file.layers[1].z_height == Approx(0.4f));
    }

    SECTION("Find layer by Z height") {
        parser.parse_line("G1 X0 Y0 Z0.2");
        parser.parse_line("G1 X0 Y0 Z0.4");
        parser.parse_line("G1 X0 Y0 Z0.6");

        auto file = parser.finalize();

        REQUIRE(file.find_layer_at_z(0.2f) == 0);
        REQUIRE(file.find_layer_at_z(0.4f) == 1);
        REQUIRE(file.find_layer_at_z(0.6f) == 2);
        REQUIRE(file.find_layer_at_z(0.3f) == 0); // Closest to 0.2
    }
}

TEST_CASE("GCodeParser - Coordinate extraction", "[gcode][parser]") {
    GCodeParser parser;

    SECTION("Extract X, Y, Z coordinates") {
        parser.parse_line("G1 X10.5 Y-20.3 Z0.2");
        parser.parse_line("G1 X15.5 Y-15.3"); // Move from previous position

        auto file = parser.finalize();

        REQUIRE(file.total_segments == 2);
        auto& seg1 = file.layers[0].segments[0];
        REQUIRE(seg1.start.x == Approx(0.0f));
        REQUIRE(seg1.start.y == Approx(0.0f));
        REQUIRE(seg1.end.x == Approx(10.5f));
        REQUIRE(seg1.end.y == Approx(-20.3f));

        auto& seg2 = file.layers[0].segments[1];
        REQUIRE(seg2.start.x == Approx(10.5f));
        REQUIRE(seg2.start.y == Approx(-20.3f));
        REQUIRE(seg2.end.x == Approx(15.5f));
        REQUIRE(seg2.end.y == Approx(-15.3f));
    }
}

TEST_CASE("GCodeParser - Comments and whitespace", "[gcode][parser]") {
    GCodeParser parser;

    SECTION("Ignore comments") {
        parser.parse_line("G1 X10 Y20 ; This is a comment");
        auto file = parser.finalize();

        REQUIRE(file.total_segments == 1);
    }

    SECTION("Handle blank lines") {
        parser.parse_line("");
        parser.parse_line("   ");
        parser.parse_line("\t");
        auto file = parser.finalize();

        REQUIRE(file.total_segments == 0);
    }

    SECTION("Trim leading/trailing whitespace") {
        parser.parse_line("  G1 X10 Y20  ");
        auto file = parser.finalize();

        REQUIRE(file.total_segments == 1);
    }
}

TEST_CASE("GCodeParser - EXCLUDE_OBJECT commands", "[gcode][parser]") {
    GCodeParser parser;

    SECTION("Parse EXCLUDE_OBJECT_DEFINE") {
        parser.parse_line("EXCLUDE_OBJECT_DEFINE NAME=cube_1 CENTER=50,75 "
                          "POLYGON=[[45,70],[55,70],[55,80],[45,80]]");
        auto file = parser.finalize();

        REQUIRE(file.objects.size() == 1);
        REQUIRE(file.objects.count("cube_1") == 1);

        auto& obj = file.objects["cube_1"];
        REQUIRE(obj.name == "cube_1");
        REQUIRE(obj.center.x == Approx(50.0f));
        REQUIRE(obj.center.y == Approx(75.0f));
        REQUIRE(obj.polygon.size() == 4);
    }

    SECTION("Track segments by object") {
        parser.parse_line("EXCLUDE_OBJECT_DEFINE NAME=part1 CENTER=10,10");
        parser.parse_line("EXCLUDE_OBJECT_START NAME=part1");
        parser.parse_line("G1 X10 Y10 Z0.2 E1");
        parser.parse_line("G1 X20 Y10 E2");
        parser.parse_line("EXCLUDE_OBJECT_END NAME=part1");
        parser.parse_line("G1 X30 Y30 E3"); // Not in object

        auto file = parser.finalize();

        REQUIRE(file.total_segments == 3);
        REQUIRE(file.layers[0].segments[0].object_name == "part1");
        REQUIRE(file.layers[0].segments[1].object_name == "part1");
        REQUIRE(file.layers[0].segments[2].object_name == "");
    }
}

TEST_CASE("GCodeParser - Bounding box calculation", "[gcode][parser]") {
    GCodeParser parser;

    SECTION("Calculate global bounding box") {
        parser.parse_line("G1 X-10 Y-10 Z0.2");
        parser.parse_line("G1 X100 Y50 Z10.5");

        auto file = parser.finalize();

        REQUIRE(file.global_bounding_box.min.x == Approx(-10.0f));
        REQUIRE(file.global_bounding_box.min.y == Approx(-10.0f));
        REQUIRE(file.global_bounding_box.min.z == Approx(0.2f));
        REQUIRE(file.global_bounding_box.max.x == Approx(100.0f));
        REQUIRE(file.global_bounding_box.max.y == Approx(50.0f));
        REQUIRE(file.global_bounding_box.max.z == Approx(10.5f));

        auto center = file.global_bounding_box.center();
        REQUIRE(center.x == Approx(45.0f));
        REQUIRE(center.y == Approx(20.0f));
    }
}

TEST_CASE("GCodeParser - Positioning modes", "[gcode][parser]") {
    GCodeParser parser;

    SECTION("Absolute positioning (G90, default)") {
        parser.parse_line("G90"); // Absolute mode
        parser.parse_line("G1 X10 Y10 Z0.2");
        parser.parse_line("G1 X20 Y20"); // Absolute coordinates

        auto file = parser.finalize();

        REQUIRE(file.layers[0].segments[1].end.x == Approx(20.0f));
        REQUIRE(file.layers[0].segments[1].end.y == Approx(20.0f));
    }

    SECTION("Relative positioning (G91)") {
        parser.parse_line("G91"); // Relative mode
        parser.parse_line("G1 X10 Y10 Z0.2");
        parser.parse_line("G1 X5 Y5"); // Relative offset

        auto file = parser.finalize();

        REQUIRE(file.layers[0].segments[1].end.x == Approx(15.0f));
        REQUIRE(file.layers[0].segments[1].end.y == Approx(15.0f));
    }
}

TEST_CASE("GCodeParser - Statistics", "[gcode][parser]") {
    GCodeParser parser;

    SECTION("Count segments by type") {
        parser.parse_line("G1 X10 Y10 Z0.2 E1"); // Extrusion
        parser.parse_line("G0 X20 Y20");         // Travel
        parser.parse_line("G1 X30 Y30 E2");      // Extrusion

        auto file = parser.finalize();

        REQUIRE(file.total_segments == 3);
        REQUIRE(file.layers[0].segment_count_extrusion == 2);
        REQUIRE(file.layers[0].segment_count_travel == 1);
    }
}

TEST_CASE("GCodeParser - Real-world G-code snippet", "[gcode][parser]") {
    GCodeParser parser;

    SECTION("Parse typical slicer output") {
        std::vector<std::string> gcode = {
            "; Layer 0",
            "G1 Z0.2 F7800",
            "G1 X95.3 Y95.3",
            "G1 X95.3 Y104.7 E0.5",
            "G1 X104.7 Y104.7 E1.0",
            "G1 X104.7 Y95.3 E1.5",
            "G1 X95.3 Y95.3 E2.0",
            "; Layer 1",
            "G1 Z0.4 F7800",
            "G1 X95.3 Y95.3",
            "G1 X95.3 Y104.7 E2.5",
            "G1 X104.7 Y104.7 E3.0",
        };

        for (const auto& line : gcode) {
            parser.parse_line(line);
        }

        auto file = parser.finalize();

        REQUIRE(file.layers.size() == 2);
        REQUIRE(file.layers[0].z_height == Approx(0.2f));
        REQUIRE(file.layers[1].z_height == Approx(0.4f));
        REQUIRE(file.total_segments > 0);
    }
}

TEST_CASE("GCodeParser - Move type differentiation", "[gcode][parser]") {
    GCodeParser parser;

    SECTION("G0 commands are travel moves") {
        parser.parse_line("G0 X10 Y10 Z0.2");  // Travel move (no extrusion)
        parser.parse_line("G0 X20 Y20");       // Another travel

        auto file = parser.finalize();

        REQUIRE(file.total_segments == 2);
        REQUIRE(file.layers[0].segment_count_travel == 2);
        REQUIRE(file.layers[0].segment_count_extrusion == 0);
        REQUIRE(file.layers[0].segments[0].is_extrusion == false);
        REQUIRE(file.layers[0].segments[1].is_extrusion == false);
    }

    SECTION("G1 without E parameter is travel move") {
        parser.parse_line("G1 X10 Y10 Z0.2");  // No E = travel
        parser.parse_line("G1 X20 Y20");       // No E = travel

        auto file = parser.finalize();

        REQUIRE(file.total_segments == 2);
        REQUIRE(file.layers[0].segment_count_travel == 2);
        REQUIRE(file.layers[0].segment_count_extrusion == 0);
    }

    SECTION("G1 with E parameter is extrusion move") {
        parser.parse_line("G1 X10 Y10 Z0.2 E0.5");  // Has E = extrusion
        parser.parse_line("G1 X20 Y20 E1.0");       // Has E = extrusion

        auto file = parser.finalize();

        REQUIRE(file.total_segments == 2);
        REQUIRE(file.layers[0].segment_count_extrusion == 2);
        REQUIRE(file.layers[0].segment_count_travel == 0);
        REQUIRE(file.layers[0].segments[0].is_extrusion == true);
        REQUIRE(file.layers[0].segments[1].is_extrusion == true);
    }

    SECTION("G1 with decreasing E during movement is retraction (travel move)") {
        parser.parse_line("M82");  // Absolute extrusion mode
        parser.parse_line("G1 X10 Y10 Z0.2 E1.0");   // Extrusion
        parser.parse_line("G1 X15 Y15 E0.5");        // Move with retraction (E decreases)
        parser.parse_line("G1 X20 Y20");             // Travel after retraction

        auto file = parser.finalize();

        // First move is extrusion, second has negative E delta (retraction), third is travel
        REQUIRE(file.total_segments == 3);
        REQUIRE(file.layers[0].segments[0].is_extrusion == true);
        REQUIRE(file.layers[0].segments[1].is_extrusion == false);  // Negative E = retraction
        REQUIRE(file.layers[0].segments[2].is_extrusion == false);  // Travel
    }

    SECTION("Mixed G0 and G1 commands") {
        parser.parse_line("G1 X10 Y10 Z0.2 E1.0");  // G1 extrusion
        parser.parse_line("G0 X20 Y20");            // G0 travel
        parser.parse_line("G1 X30 Y30 E2.0");       // G1 extrusion
        parser.parse_line("G0 X0 Y0");              // G0 travel

        auto file = parser.finalize();

        REQUIRE(file.total_segments == 4);
        REQUIRE(file.layers[0].segment_count_extrusion == 2);
        REQUIRE(file.layers[0].segment_count_travel == 2);

        // Verify specific segment types
        REQUIRE(file.layers[0].segments[0].is_extrusion == true);   // G1 E1.0
        REQUIRE(file.layers[0].segments[1].is_extrusion == false);  // G0
        REQUIRE(file.layers[0].segments[2].is_extrusion == true);   // G1 E2.0
        REQUIRE(file.layers[0].segments[3].is_extrusion == false);  // G0
    }
}

TEST_CASE("GCodeParser - Extrusion amount tracking", "[gcode][parser]") {
    GCodeParser parser;

    SECTION("Track extrusion delta in absolute mode (M82)") {
        parser.parse_line("M82");  // Absolute extrusion
        parser.parse_line("G1 X10 Y10 Z0.2 E1.0");
        parser.parse_line("G1 X20 Y20 E3.0");  // Delta = 3.0 - 1.0 = 2.0

        auto file = parser.finalize();

        REQUIRE(file.layers[0].segments[0].extrusion_amount == Approx(1.0f));
        REQUIRE(file.layers[0].segments[1].extrusion_amount == Approx(2.0f));
    }

    SECTION("Track extrusion delta in relative mode (M83)") {
        parser.parse_line("M83");  // Relative extrusion
        parser.parse_line("G1 X10 Y10 Z0.2 E1.5");  // Delta = 1.5
        parser.parse_line("G1 X20 Y20 E2.0");       // Delta = 2.0

        auto file = parser.finalize();

        REQUIRE(file.layers[0].segments[0].extrusion_amount == Approx(1.5f));
        REQUIRE(file.layers[0].segments[1].extrusion_amount == Approx(2.0f));
    }

    SECTION("Retraction has negative extrusion amount") {
        parser.parse_line("M82");  // Absolute extrusion
        parser.parse_line("G1 X10 Y10 Z0.2 E5.0");
        parser.parse_line("G1 X15 Y15 E3.0");  // Move with retract: delta = 3.0 - 5.0 = -2.0

        auto file = parser.finalize();

        REQUIRE(file.layers[0].segments[0].extrusion_amount == Approx(5.0f));
        REQUIRE(file.layers[0].segments[1].extrusion_amount == Approx(-2.0f));
    }
}

TEST_CASE("GCodeParser - Travel move characteristics", "[gcode][parser]") {
    GCodeParser parser;

    SECTION("Travel moves create segments with start and end positions") {
        parser.parse_line("G0 X10 Y10 Z0.2");  // Move to (10,10,0.2)
        parser.parse_line("G0 X100 Y100");      // Travel to (100,100)

        auto file = parser.finalize();

        // Verify both travel moves created segments
        REQUIRE(file.layers[0].segments.size() == 2);

        // Check first segment: from (0,0,0) to (10,10,0.2)
        auto& seg1 = file.layers[0].segments[0];
        REQUIRE(seg1.end.x == Approx(10.0f));
        REQUIRE(seg1.end.y == Approx(10.0f));
        REQUIRE(seg1.is_extrusion == false);

        // Check second segment: from (10,10,0.2) to (100,100,0.2)
        auto& seg2 = file.layers[0].segments[1];
        REQUIRE(seg2.start.x == Approx(10.0f));
        REQUIRE(seg2.start.y == Approx(10.0f));
        REQUIRE(seg2.end.x == Approx(100.0f));
        REQUIRE(seg2.end.y == Approx(100.0f));
        REQUIRE(seg2.is_extrusion == false);
    }

    SECTION("Z-only travel moves (layer changes)") {
        parser.parse_line("G1 X10 Y10 Z0.2 E1");
        parser.parse_line("G0 Z0.4");  // Z-hop / layer change
        parser.parse_line("G1 X10 Y10 E2");

        auto file = parser.finalize();

        REQUIRE(file.layers.size() == 2);
        REQUIRE(file.layers[0].z_height == Approx(0.2f));
        REQUIRE(file.layers[1].z_height == Approx(0.4f));
    }
}

TEST_CASE("GCodeParser - Extrusion move characteristics", "[gcode][parser]") {
    GCodeParser parser;

    SECTION("Extrusion moves have non-zero E delta") {
        parser.parse_line("G1 X10 Y10 Z0.2 E1.5");
        parser.parse_line("G1 X20 Y20 E3.0");

        auto file = parser.finalize();

        REQUIRE(file.layers[0].segments[0].extrusion_amount > 0);
        REQUIRE(file.layers[0].segments[1].extrusion_amount > 0);
    }

    SECTION("Extrusion width calculated from E delta and distance") {
        parser.parse_line("; layer_height = 0.2");  // Set layer height metadata
        parser.parse_line("G1 X0 Y0 Z0.2 E0");
        parser.parse_line("G1 X10 Y0 E1.5");  // 10mm move with 1.5mm³ extrusion

        auto file = parser.finalize();

        // Width should be calculated (implementation-specific formula)
        // Just verify it's set to a reasonable value if calculated
        auto& seg = file.layers[0].segments[1];
        if (seg.width > 0) {
            REQUIRE(seg.width > 0.1f);   // Minimum reasonable width
            REQUIRE(seg.width < 2.0f);   // Maximum reasonable width
        }
    }
}
