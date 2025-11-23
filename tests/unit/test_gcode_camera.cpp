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
#include "gcode_camera.h"

using namespace gcode;
using Catch::Approx;

// ============================================================================
// Initialization and Reset Tests
// ============================================================================

TEST_CASE("GCode Camera: Default initialization", "[gcode_camera][init]") {
    GCodeCamera camera;

    REQUIRE(camera.get_azimuth() == Approx(45.0f));
    REQUIRE(camera.get_elevation() == Approx(30.0f));
    REQUIRE(camera.get_distance() == Approx(100.0f));
    REQUIRE(camera.get_zoom_level() == Approx(1.4f));
    REQUIRE(camera.get_projection_type() == GCodeCamera::ProjectionType::ORTHOGRAPHIC);
}

TEST_CASE("GCode Camera: Reset to defaults", "[gcode_camera][init]") {
    GCodeCamera camera;

    // Modify camera
    camera.set_azimuth(90.0f);
    camera.set_elevation(60.0f);
    camera.set_zoom_level(2.0f);

    // Reset
    camera.reset();

    REQUIRE(camera.get_azimuth() == Approx(45.0f));
    REQUIRE(camera.get_elevation() == Approx(30.0f));
    REQUIRE(camera.get_distance() == Approx(100.0f));
    REQUIRE(camera.get_zoom_level() == Approx(1.4f));
}

// ============================================================================
// Rotation Tests
// ============================================================================

TEST_CASE("GCode Camera: Rotate azimuth", "[gcode_camera][rotate]") {
    GCodeCamera camera;

    SECTION("Positive rotation") {
        camera.rotate(45.0f, 0.0f);
        REQUIRE(camera.get_azimuth() == Approx(90.0f));
    }

    SECTION("Negative rotation") {
        camera.rotate(-45.0f, 0.0f);
        REQUIRE(camera.get_azimuth() == Approx(0.0f));
    }

    SECTION("Full rotation wraps around") {
        camera.rotate(360.0f, 0.0f);
        REQUIRE(camera.get_azimuth() == Approx(45.0f));
    }

    SECTION("Wrap around to positive") {
        camera.rotate(-90.0f, 0.0f);
        REQUIRE(camera.get_azimuth() == Approx(315.0f));
    }
}

TEST_CASE("GCode Camera: Rotate elevation", "[gcode_camera][rotate]") {
    GCodeCamera camera;

    SECTION("Positive rotation") {
        camera.rotate(0.0f, 30.0f);
        REQUIRE(camera.get_elevation() == Approx(60.0f));
    }

    SECTION("Negative rotation") {
        camera.rotate(0.0f, -30.0f);
        REQUIRE(camera.get_elevation() == Approx(0.0f));
    }

    SECTION("Clamp at upper limit (89°)") {
        camera.rotate(0.0f, 100.0f);
        REQUIRE(camera.get_elevation() == Approx(89.0f));
    }

    SECTION("Clamp at lower limit (-89°)") {
        camera.rotate(0.0f, -200.0f);
        REQUIRE(camera.get_elevation() == Approx(-89.0f));
    }
}

TEST_CASE("GCode Camera: Combined rotation", "[gcode_camera][rotate]") {
    GCodeCamera camera;

    camera.rotate(45.0f, 15.0f);

    REQUIRE(camera.get_azimuth() == Approx(90.0f));
    REQUIRE(camera.get_elevation() == Approx(45.0f));
}

// ============================================================================
// Zoom Tests
// ============================================================================

TEST_CASE("GCode Camera: Zoom in", "[gcode_camera][zoom]") {
    GCodeCamera camera;

    camera.zoom(2.0f);

    REQUIRE(camera.get_zoom_level() == Approx(2.8f));  // 1.4 * 2.0
}

TEST_CASE("GCode Camera: Zoom out", "[gcode_camera][zoom]") {
    GCodeCamera camera;

    camera.zoom(0.5f);

    REQUIRE(camera.get_zoom_level() == Approx(0.7f));  // 1.4 * 0.5
}

TEST_CASE("GCode Camera: Zoom clamping", "[gcode_camera][zoom][edge]") {
    GCodeCamera camera;

    SECTION("Clamp at minimum (0.1)") {
        camera.zoom(0.01f);
        REQUIRE(camera.get_zoom_level() == Approx(0.1f));
    }

    SECTION("Clamp at maximum (100.0)") {
        camera.zoom(1000.0f);
        REQUIRE(camera.get_zoom_level() == Approx(100.0f));
    }
}

// ============================================================================
// Preset View Tests
// ============================================================================

TEST_CASE("GCode Camera: Top view", "[gcode_camera][preset]") {
    GCodeCamera camera;

    camera.set_top_view();

    REQUIRE(camera.get_azimuth() == Approx(0.0f));
    REQUIRE(camera.get_elevation() == Approx(89.0f));  // Almost straight down
}

TEST_CASE("GCode Camera: Front view", "[gcode_camera][preset]") {
    GCodeCamera camera;

    camera.set_front_view();

    REQUIRE(camera.get_azimuth() == Approx(0.0f));
    REQUIRE(camera.get_elevation() == Approx(0.0f));
}

TEST_CASE("GCode Camera: Side view", "[gcode_camera][preset]") {
    GCodeCamera camera;

    camera.set_side_view();

    REQUIRE(camera.get_azimuth() == Approx(90.0f));
    REQUIRE(camera.get_elevation() == Approx(0.0f));
}

TEST_CASE("GCode Camera: Isometric view", "[gcode_camera][preset]") {
    GCodeCamera camera;

    camera.set_isometric_view();

    REQUIRE(camera.get_azimuth() == Approx(45.0f));
    REQUIRE(camera.get_elevation() == Approx(30.0f));
}

// ============================================================================
// Set Azimuth/Elevation Tests
// ============================================================================

TEST_CASE("GCode Camera: Set azimuth", "[gcode_camera][setter]") {
    GCodeCamera camera;

    SECTION("Set to 180°") {
        camera.set_azimuth(180.0f);
        REQUIRE(camera.get_azimuth() == Approx(180.0f));
    }

    SECTION("Wrap around from 400° to 40°") {
        camera.set_azimuth(400.0f);
        REQUIRE(camera.get_azimuth() == Approx(40.0f));
    }

    SECTION("Wrap around from -45° to 315°") {
        camera.set_azimuth(-45.0f);
        REQUIRE(camera.get_azimuth() == Approx(315.0f));
    }
}

TEST_CASE("GCode Camera: Set elevation", "[gcode_camera][setter]") {
    GCodeCamera camera;

    SECTION("Set to 60°") {
        camera.set_elevation(60.0f);
        REQUIRE(camera.get_elevation() == Approx(60.0f));
    }

    SECTION("Clamp at 100° to 89°") {
        camera.set_elevation(100.0f);
        REQUIRE(camera.get_elevation() == Approx(89.0f));
    }

    SECTION("Clamp at -100° to -89°") {
        camera.set_elevation(-100.0f);
        REQUIRE(camera.get_elevation() == Approx(-89.0f));
    }
}

// ============================================================================
// Zoom Level Tests
// ============================================================================

TEST_CASE("GCode Camera: Set zoom level", "[gcode_camera][zoom]") {
    GCodeCamera camera;

    SECTION("Set to 2.0") {
        camera.set_zoom_level(2.0f);
        REQUIRE(camera.get_zoom_level() == Approx(2.0f));
    }

    SECTION("Clamp at 0.01 to 0.1") {
        camera.set_zoom_level(0.01f);
        REQUIRE(camera.get_zoom_level() == Approx(0.1f));
    }

    SECTION("Clamp at 200.0 to 100.0") {
        camera.set_zoom_level(200.0f);
        REQUIRE(camera.get_zoom_level() == Approx(100.0f));
    }
}

// ============================================================================
// Projection Type Tests
// ============================================================================

TEST_CASE("GCode Camera: Set projection type", "[gcode_camera][projection]") {
    GCodeCamera camera;

    SECTION("Default is orthographic") {
        REQUIRE(camera.get_projection_type() == GCodeCamera::ProjectionType::ORTHOGRAPHIC);
    }

    SECTION("Set to perspective") {
        camera.set_projection_type(GCodeCamera::ProjectionType::PERSPECTIVE);
        REQUIRE(camera.get_projection_type() == GCodeCamera::ProjectionType::PERSPECTIVE);
    }

    SECTION("Set back to orthographic") {
        camera.set_projection_type(GCodeCamera::ProjectionType::PERSPECTIVE);
        camera.set_projection_type(GCodeCamera::ProjectionType::ORTHOGRAPHIC);
        REQUIRE(camera.get_projection_type() == GCodeCamera::ProjectionType::ORTHOGRAPHIC);
    }
}

// ============================================================================
// Viewport Size Tests
// ============================================================================

TEST_CASE("GCode Camera: Set viewport size", "[gcode_camera][viewport]") {
    GCodeCamera camera;

    SECTION("Set to 800x480") {
        camera.set_viewport_size(800, 480);
        // Matrices should be updated (tested indirectly via matrices)
    }

    SECTION("Set to 1920x1080") {
        camera.set_viewport_size(1920, 1080);
        // Matrices should be updated
    }
}

// ============================================================================
// Fit to Bounds Tests
// ============================================================================

TEST_CASE("GCode Camera: Fit to bounds", "[gcode_camera][bounds]") {
    GCodeCamera camera;
    camera.set_viewport_size(800, 480);

    SECTION("Fit to simple cubic bounds") {
        AABB bounds;
        bounds.min = glm::vec3(-50.0f, -50.0f, 0.0f);
        bounds.max = glm::vec3(50.0f, 50.0f, 100.0f);

        camera.fit_to_bounds(bounds);

        // Target should be at center
        glm::vec3 target = camera.get_target();
        REQUIRE(target.x == Approx(0.0f));
        REQUIRE(target.y == Approx(0.0f));
        REQUIRE(target.z == Approx(50.0f));

        // Distance should be set based on max dimension
        REQUIRE(camera.get_distance() > 0.0f);
    }

    SECTION("Fit to asymmetric bounds") {
        AABB bounds;
        bounds.min = glm::vec3(0.0f, 0.0f, 0.0f);
        bounds.max = glm::vec3(220.0f, 220.0f, 50.0f);

        camera.fit_to_bounds(bounds);

        // Target should be at center of bounds
        glm::vec3 target = camera.get_target();
        REQUIRE(target.x == Approx(110.0f));
        REQUIRE(target.y == Approx(110.0f));
        REQUIRE(target.z == Approx(25.0f));
    }

    SECTION("Zoom preserved if custom") {
        camera.set_zoom_level(5.0f);  // Custom zoom > 1.4

        AABB bounds;
        bounds.min = glm::vec3(-50.0f, -50.0f, 0.0f);
        bounds.max = glm::vec3(50.0f, 50.0f, 100.0f);

        camera.fit_to_bounds(bounds);

        // Custom zoom should be preserved
        REQUIRE(camera.get_zoom_level() == Approx(5.0f));
    }

    SECTION("Zoom reset if default") {
        camera.set_zoom_level(1.4f);  // Default zoom

        AABB bounds;
        bounds.min = glm::vec3(-50.0f, -50.0f, 0.0f);
        bounds.max = glm::vec3(50.0f, 50.0f, 100.0f);

        camera.fit_to_bounds(bounds);

        // Should reset to default
        REQUIRE(camera.get_zoom_level() == Approx(1.4f));
    }
}

// ============================================================================
// Camera Position Tests
// ============================================================================

TEST_CASE("GCode Camera: Compute camera position", "[gcode_camera][position]") {
    GCodeCamera camera;

    SECTION("Isometric view position (default)") {
        camera.set_isometric_view();
        camera.set_target(glm::vec3(0, 0, 0));
        camera.set_distance(100.0f);

        glm::vec3 pos = camera.compute_camera_position();

        // At azimuth 45°, elevation 30°, distance 100:
        // Should be somewhere in positive X, positive Y, positive Z
        REQUIRE(pos.x > 0.0f);
        REQUIRE(pos.y > 0.0f);
        REQUIRE(pos.z > 0.0f);

        // Distance from target should be ~100
        float dist = glm::length(pos - camera.get_target());
        REQUIRE(dist == Approx(100.0f).epsilon(0.01));
    }

    SECTION("Top view position") {
        camera.set_top_view();
        camera.set_target(glm::vec3(0, 0, 0));
        camera.set_distance(100.0f);

        glm::vec3 pos = camera.compute_camera_position();

        // Should be directly above target
        REQUIRE(pos.x == Approx(0.0f).margin(0.1));
        REQUIRE(pos.y == Approx(0.0f).margin(0.1));
        REQUIRE(pos.z == Approx(100.0f).epsilon(0.01));
    }

    SECTION("Front view position") {
        camera.set_front_view();
        camera.set_target(glm::vec3(0, 0, 0));
        camera.set_distance(100.0f);

        glm::vec3 pos = camera.compute_camera_position();

        // Should be in front (positive Y), at Z=0
        REQUIRE(pos.x == Approx(0.0f).margin(0.1));
        REQUIRE(pos.y == Approx(100.0f).epsilon(0.01));
        REQUIRE(pos.z == Approx(0.0f).margin(0.1));
    }

    SECTION("Side view position") {
        camera.set_side_view();
        camera.set_target(glm::vec3(0, 0, 0));
        camera.set_distance(100.0f);

        glm::vec3 pos = camera.compute_camera_position();

        // Should be to the side (positive X), at Z=0
        REQUIRE(pos.x == Approx(100.0f).epsilon(0.01));
        REQUIRE(pos.y == Approx(0.0f).margin(0.1));
        REQUIRE(pos.z == Approx(0.0f).margin(0.1));
    }
}

// ============================================================================
// Pan Tests
// ============================================================================

TEST_CASE("GCode Camera: Pan camera", "[gcode_camera][pan]") {
    GCodeCamera camera;
    camera.set_isometric_view();
    camera.set_target(glm::vec3(0, 0, 0));

    glm::vec3 initial_target = camera.get_target();

    SECTION("Pan right") {
        camera.pan(10.0f, 0.0f);
        glm::vec3 new_target = camera.get_target();

        // Target should have moved
        REQUIRE(new_target != initial_target);
    }

    SECTION("Pan up") {
        camera.pan(0.0f, 10.0f);
        glm::vec3 new_target = camera.get_target();

        // Target should have moved
        REQUIRE(new_target != initial_target);
    }

    SECTION("Pan diagonal") {
        camera.pan(10.0f, 10.0f);
        glm::vec3 new_target = camera.get_target();

        // Target should have moved
        REQUIRE(new_target != initial_target);
    }
}

// ============================================================================
// Matrix Tests
// ============================================================================

TEST_CASE("GCode Camera: View and projection matrices", "[gcode_camera][matrix]") {
    GCodeCamera camera;
    camera.set_viewport_size(800, 480);
    camera.set_isometric_view();
    camera.set_target(glm::vec3(0, 0, 0));

    SECTION("View matrix is non-identity") {
        const glm::mat4& view = camera.get_view_matrix();
        glm::mat4 identity(1.0f);

        REQUIRE(view != identity);
    }

    SECTION("Projection matrix is non-identity") {
        const glm::mat4& proj = camera.get_projection_matrix();
        glm::mat4 identity(1.0f);

        REQUIRE(proj != identity);
    }

    SECTION("Matrices update when camera moves") {
        const glm::mat4 view_before = camera.get_view_matrix();

        camera.rotate(45.0f, 0.0f);

        const glm::mat4& view_after = camera.get_view_matrix();

        REQUIRE(view_after != view_before);
    }
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_CASE("GCode Camera: Integration - Orbit around model", "[gcode_camera][integration]") {
    GCodeCamera camera;
    camera.set_viewport_size(800, 480);

    AABB model_bounds;
    model_bounds.min = glm::vec3(0, 0, 0);
    model_bounds.max = glm::vec3(100, 100, 50);

    camera.fit_to_bounds(model_bounds);

    // Verify camera is set up to view the model
    glm::vec3 target = camera.get_target();
    REQUIRE(target.x == Approx(50.0f));
    REQUIRE(target.y == Approx(50.0f));
    REQUIRE(target.z == Approx(25.0f));

    // Orbit 360° around the model
    for (int i = 0; i < 36; i++) {
        camera.rotate(10.0f, 0.0f);
    }

    // Should be back to original azimuth (within floating point error)
    REQUIRE(camera.get_azimuth() == Approx(45.0f).epsilon(0.1));

    // Target should not have changed
    glm::vec3 final_target = camera.get_target();
    REQUIRE(final_target.x == Approx(target.x));
    REQUIRE(final_target.y == Approx(target.y));
    REQUIRE(final_target.z == Approx(target.z));
}

TEST_CASE("GCode Camera: Integration - Zoom and rotate", "[gcode_camera][integration]") {
    GCodeCamera camera;
    camera.set_viewport_size(800, 480);

    AABB model_bounds;
    model_bounds.min = glm::vec3(-50, -50, 0);
    model_bounds.max = glm::vec3(50, 50, 100);

    camera.fit_to_bounds(model_bounds);

    // Zoom in
    camera.zoom(2.0f);
    REQUIRE(camera.get_zoom_level() == Approx(2.8f));

    // Rotate to top view
    camera.set_top_view();
    REQUIRE(camera.get_elevation() == Approx(89.0f));

    // Zoom out
    camera.zoom(0.5f);
    REQUIRE(camera.get_zoom_level() == Approx(1.4f));

    // Return to isometric
    camera.set_isometric_view();
    REQUIRE(camera.get_azimuth() == Approx(45.0f));
    REQUIRE(camera.get_elevation() == Approx(30.0f));
}
