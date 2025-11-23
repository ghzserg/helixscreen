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
#include "bed_mesh_coordinate_transform.h"

using namespace BedMeshCoordinateTransform;
using Catch::Approx;

// ============================================================================
// mesh_col_to_world_x() Tests
// ============================================================================

TEST_CASE("Bed Mesh Transform: mesh_col_to_world_x - center column", "[bed_mesh][transform]") {
    SECTION("3x3 mesh, center column") {
        // Column 1 is center of 3 columns (0, 1, 2)
        double x = mesh_col_to_world_x(1, 3, 10.0);
        REQUIRE(x == Approx(0.0));
    }

    SECTION("5x5 mesh, center column") {
        // Column 2 is center of 5 columns (0, 1, 2, 3, 4)
        double x = mesh_col_to_world_x(2, 5, 10.0);
        REQUIRE(x == Approx(0.0));
    }

    SECTION("7x7 mesh, center column") {
        // Column 3 is center of 7 columns
        double x = mesh_col_to_world_x(3, 7, 10.0);
        REQUIRE(x == Approx(0.0));
    }
}

TEST_CASE("Bed Mesh Transform: mesh_col_to_world_x - left columns", "[bed_mesh][transform]") {
    SECTION("3x3 mesh, leftmost column") {
        double x = mesh_col_to_world_x(0, 3, 10.0);
        REQUIRE(x == Approx(-10.0));
    }

    SECTION("5x5 mesh, leftmost column") {
        double x = mesh_col_to_world_x(0, 5, 10.0);
        REQUIRE(x == Approx(-20.0));
    }

    SECTION("5x5 mesh, second column") {
        double x = mesh_col_to_world_x(1, 5, 10.0);
        REQUIRE(x == Approx(-10.0));
    }
}

TEST_CASE("Bed Mesh Transform: mesh_col_to_world_x - right columns", "[bed_mesh][transform]") {
    SECTION("3x3 mesh, rightmost column") {
        double x = mesh_col_to_world_x(2, 3, 10.0);
        REQUIRE(x == Approx(10.0));
    }

    SECTION("5x5 mesh, rightmost column") {
        double x = mesh_col_to_world_x(4, 5, 10.0);
        REQUIRE(x == Approx(20.0));
    }

    SECTION("5x5 mesh, second from right") {
        double x = mesh_col_to_world_x(3, 5, 10.0);
        REQUIRE(x == Approx(10.0));
    }
}

TEST_CASE("Bed Mesh Transform: mesh_col_to_world_x - different scales", "[bed_mesh][transform]") {
    SECTION("Scale 5.0") {
        double x = mesh_col_to_world_x(0, 3, 5.0);
        REQUIRE(x == Approx(-5.0));
    }

    SECTION("Scale 20.0") {
        double x = mesh_col_to_world_x(2, 3, 20.0);
        REQUIRE(x == Approx(20.0));
    }

    SECTION("Scale 1.0") {
        double x = mesh_col_to_world_x(1, 5, 1.0);
        REQUIRE(x == Approx(-1.0));
    }
}

TEST_CASE("Bed Mesh Transform: mesh_col_to_world_x - edge cases", "[bed_mesh][transform][edge]") {
    SECTION("Single column mesh") {
        double x = mesh_col_to_world_x(0, 1, 10.0);
        REQUIRE(x == Approx(0.0));
    }

    SECTION("Even number of columns") {
        // 4 columns: center is between column 1 and 2
        double x0 = mesh_col_to_world_x(0, 4, 10.0);
        double x1 = mesh_col_to_world_x(1, 4, 10.0);
        double x2 = mesh_col_to_world_x(2, 4, 10.0);
        double x3 = mesh_col_to_world_x(3, 4, 10.0);

        REQUIRE(x0 == Approx(-15.0));
        REQUIRE(x1 == Approx(-5.0));
        REQUIRE(x2 == Approx(5.0));
        REQUIRE(x3 == Approx(15.0));
    }
}

// ============================================================================
// mesh_row_to_world_y() Tests
// ============================================================================

TEST_CASE("Bed Mesh Transform: mesh_row_to_world_y - center row", "[bed_mesh][transform]") {
    SECTION("3x3 mesh, center row") {
        // Row 1 is center, but y-axis is inverted
        double y = mesh_row_to_world_y(1, 3, 10.0);
        REQUIRE(y == Approx(0.0));
    }

    SECTION("5x5 mesh, center row") {
        double y = mesh_row_to_world_y(2, 5, 10.0);
        REQUIRE(y == Approx(0.0));
    }
}

TEST_CASE("Bed Mesh Transform: mesh_row_to_world_y - top rows (inverted)", "[bed_mesh][transform]") {
    SECTION("3x3 mesh, top row (row 0)") {
        // Top row in mesh -> positive Y in world (inverted)
        double y = mesh_row_to_world_y(0, 3, 10.0);
        REQUIRE(y == Approx(10.0));
    }

    SECTION("5x5 mesh, top row (row 0)") {
        double y = mesh_row_to_world_y(0, 5, 10.0);
        REQUIRE(y == Approx(20.0));
    }

    SECTION("5x5 mesh, second row") {
        double y = mesh_row_to_world_y(1, 5, 10.0);
        REQUIRE(y == Approx(10.0));
    }
}

TEST_CASE("Bed Mesh Transform: mesh_row_to_world_y - bottom rows (inverted)", "[bed_mesh][transform]") {
    SECTION("3x3 mesh, bottom row (row 2)") {
        // Bottom row in mesh -> negative Y in world (inverted)
        double y = mesh_row_to_world_y(2, 3, 10.0);
        REQUIRE(y == Approx(-10.0));
    }

    SECTION("5x5 mesh, bottom row (row 4)") {
        double y = mesh_row_to_world_y(4, 5, 10.0);
        REQUIRE(y == Approx(-20.0));
    }

    SECTION("5x5 mesh, second from bottom") {
        double y = mesh_row_to_world_y(3, 5, 10.0);
        REQUIRE(y == Approx(-10.0));
    }
}

TEST_CASE("Bed Mesh Transform: mesh_row_to_world_y - different scales", "[bed_mesh][transform]") {
    SECTION("Scale 5.0") {
        double y = mesh_row_to_world_y(0, 3, 5.0);
        REQUIRE(y == Approx(5.0));
    }

    SECTION("Scale 20.0") {
        double y = mesh_row_to_world_y(2, 3, 20.0);
        REQUIRE(y == Approx(-20.0));
    }

    SECTION("Scale 1.0") {
        double y = mesh_row_to_world_y(1, 5, 1.0);
        REQUIRE(y == Approx(1.0));
    }
}

TEST_CASE("Bed Mesh Transform: mesh_row_to_world_y - edge cases", "[bed_mesh][transform][edge]") {
    SECTION("Single row mesh") {
        double y = mesh_row_to_world_y(0, 1, 10.0);
        REQUIRE(y == Approx(0.0));
    }

    SECTION("Even number of rows") {
        // 4 rows: center is between row 1 and 2
        double y0 = mesh_row_to_world_y(0, 4, 10.0);
        double y1 = mesh_row_to_world_y(1, 4, 10.0);
        double y2 = mesh_row_to_world_y(2, 4, 10.0);
        double y3 = mesh_row_to_world_y(3, 4, 10.0);

        REQUIRE(y0 == Approx(15.0));
        REQUIRE(y1 == Approx(5.0));
        REQUIRE(y2 == Approx(-5.0));
        REQUIRE(y3 == Approx(-15.0));
    }
}

// ============================================================================
// mesh_z_to_world_z() Tests
// ============================================================================

TEST_CASE("Bed Mesh Transform: mesh_z_to_world_z - centered at zero", "[bed_mesh][transform]") {
    SECTION("Z height equals center") {
        double z = mesh_z_to_world_z(0.5, 0.5, 1.0);
        REQUIRE(z == Approx(0.0));
    }

    SECTION("Different center values") {
        REQUIRE(mesh_z_to_world_z(1.0, 1.0, 1.0) == Approx(0.0));
        REQUIRE(mesh_z_to_world_z(-0.5, -0.5, 1.0) == Approx(0.0));
    }
}

TEST_CASE("Bed Mesh Transform: mesh_z_to_world_z - above center", "[bed_mesh][transform]") {
    SECTION("0.1mm above center") {
        double z = mesh_z_to_world_z(0.6, 0.5, 1.0);
        REQUIRE(z == Approx(0.1));
    }

    SECTION("1.0mm above center") {
        double z = mesh_z_to_world_z(1.5, 0.5, 1.0);
        REQUIRE(z == Approx(1.0));
    }
}

TEST_CASE("Bed Mesh Transform: mesh_z_to_world_z - below center", "[bed_mesh][transform]") {
    SECTION("0.1mm below center") {
        double z = mesh_z_to_world_z(0.4, 0.5, 1.0);
        REQUIRE(z == Approx(-0.1));
    }

    SECTION("1.0mm below center") {
        double z = mesh_z_to_world_z(-0.5, 0.5, 1.0);
        REQUIRE(z == Approx(-1.0));
    }
}

TEST_CASE("Bed Mesh Transform: mesh_z_to_world_z - different scales", "[bed_mesh][transform]") {
    SECTION("Scale 10.0 - amplify variations") {
        double z = mesh_z_to_world_z(0.6, 0.5, 10.0);
        REQUIRE(z == Approx(1.0));  // 0.1 * 10.0
    }

    SECTION("Scale 0.5 - reduce variations") {
        double z = mesh_z_to_world_z(0.6, 0.5, 0.5);
        REQUIRE(z == Approx(0.05));  // 0.1 * 0.5
    }

    SECTION("Scale 100.0 - extreme amplification") {
        double z = mesh_z_to_world_z(0.51, 0.5, 100.0);
        REQUIRE(z == Approx(1.0));  // 0.01 * 100.0
    }
}

TEST_CASE("Bed Mesh Transform: mesh_z_to_world_z - edge cases", "[bed_mesh][transform][edge]") {
    SECTION("Zero scale") {
        double z = mesh_z_to_world_z(0.6, 0.5, 0.0);
        REQUIRE(z == Approx(0.0));
    }

    SECTION("Negative scale (invert)") {
        double z = mesh_z_to_world_z(0.6, 0.5, -1.0);
        REQUIRE(z == Approx(-0.1));
    }

    SECTION("Very small variations") {
        double z = mesh_z_to_world_z(0.501, 0.5, 1.0);
        REQUIRE(z == Approx(0.001));
    }
}

// ============================================================================
// Integration Tests - Complete Mesh Transformation
// ============================================================================

TEST_CASE("Bed Mesh Transform: Integration - 3x3 mesh", "[bed_mesh][transform][integration]") {
    int cols = 3, rows = 3;
    double scale = 10.0;

    SECTION("Corner points") {
        // Top-left (row=0, col=0)
        double x_tl = mesh_col_to_world_x(0, cols, scale);
        double y_tl = mesh_row_to_world_y(0, rows, scale);
        REQUIRE(x_tl == Approx(-10.0));
        REQUIRE(y_tl == Approx(10.0));

        // Top-right (row=0, col=2)
        double x_tr = mesh_col_to_world_x(2, cols, scale);
        double y_tr = mesh_row_to_world_y(0, rows, scale);
        REQUIRE(x_tr == Approx(10.0));
        REQUIRE(y_tr == Approx(10.0));

        // Bottom-left (row=2, col=0)
        double x_bl = mesh_col_to_world_x(0, cols, scale);
        double y_bl = mesh_row_to_world_y(2, rows, scale);
        REQUIRE(x_bl == Approx(-10.0));
        REQUIRE(y_bl == Approx(-10.0));

        // Bottom-right (row=2, col=2)
        double x_br = mesh_col_to_world_x(2, cols, scale);
        double y_br = mesh_row_to_world_y(2, rows, scale);
        REQUIRE(x_br == Approx(10.0));
        REQUIRE(y_br == Approx(-10.0));
    }

    SECTION("Center point") {
        double x = mesh_col_to_world_x(1, cols, scale);
        double y = mesh_row_to_world_y(1, rows, scale);
        REQUIRE(x == Approx(0.0));
        REQUIRE(y == Approx(0.0));
    }
}

TEST_CASE("Bed Mesh Transform: Integration - 5x5 mesh with Z values", "[bed_mesh][transform][integration]") {
    int cols = 5, rows = 5;
    double scale = 10.0;
    double z_center = 0.0;
    double z_scale = 50.0;

    SECTION("Center point at average height") {
        double x = mesh_col_to_world_x(2, cols, scale);
        double y = mesh_row_to_world_y(2, rows, scale);
        double z = mesh_z_to_world_z(0.0, z_center, z_scale);

        REQUIRE(x == Approx(0.0));
        REQUIRE(y == Approx(0.0));
        REQUIRE(z == Approx(0.0));
    }

    SECTION("High point in corner") {
        double x = mesh_col_to_world_x(0, cols, scale);
        double y = mesh_row_to_world_y(0, rows, scale);
        double z = mesh_z_to_world_z(0.02, z_center, z_scale);  // 0.02mm high

        REQUIRE(x == Approx(-20.0));
        REQUIRE(y == Approx(20.0));
        REQUIRE(z == Approx(1.0));  // 0.02 * 50.0
    }

    SECTION("Low point in opposite corner") {
        double x = mesh_col_to_world_x(4, cols, scale);
        double y = mesh_row_to_world_y(4, rows, scale);
        double z = mesh_z_to_world_z(-0.02, z_center, z_scale);  // 0.02mm low

        REQUIRE(x == Approx(20.0));
        REQUIRE(y == Approx(-20.0));
        REQUIRE(z == Approx(-1.0));  // -0.02 * 50.0
    }
}

TEST_CASE("Bed Mesh Transform: Integration - Realistic printer mesh", "[bed_mesh][transform][integration]") {
    // Simulate 7x7 mesh for 220x220mm bed with probe points every 36mm
    int cols = 7, rows = 7;
    double scale = 36.0;  // Spacing between probe points
    double z_center = 0.0;
    double z_scale = 100.0;  // Amplify small variations for visualization

    SECTION("Full bed coverage") {
        // Leftmost point
        double x_left = mesh_col_to_world_x(0, cols, scale);
        REQUIRE(x_left == Approx(-108.0));  // -3 * 36

        // Rightmost point
        double x_right = mesh_col_to_world_x(6, cols, scale);
        REQUIRE(x_right == Approx(108.0));  // +3 * 36

        // Total X range
        REQUIRE((x_right - x_left) == Approx(216.0));

        // Top point
        double y_top = mesh_row_to_world_y(0, rows, scale);
        REQUIRE(y_top == Approx(108.0));

        // Bottom point
        double y_bottom = mesh_row_to_world_y(6, rows, scale);
        REQUIRE(y_bottom == Approx(-108.0));

        // Total Y range
        REQUIRE((y_top - y_bottom) == Approx(216.0));
    }

    SECTION("Typical mesh variations") {
        // 0.05mm variation at corner
        double z_corner = mesh_z_to_world_z(0.05, z_center, z_scale);
        REQUIRE(z_corner == Approx(5.0));

        // -0.03mm variation at another point
        double z_low = mesh_z_to_world_z(-0.03, z_center, z_scale);
        REQUIRE(z_low == Approx(-3.0));
    }
}
