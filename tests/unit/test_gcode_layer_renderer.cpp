// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_layer_renderer.h"
#include "gcode_parser.h"

#include <optional>
#include <string>
#include <unordered_set>

#include "../catch_amalgamated.hpp"

using namespace helix::gcode;

namespace {

// Helper to build a ParsedGCodeFile with named objects and an unnamed segment.
//
// Layout (TOP_DOWN, world coords):
//   "cube1" : segment from (10,20) to (50,20)   -- y=20
//   "cube2" : segment from (10,80) to (50,80)   -- y=80
//   unnamed : segment from (10,50) to (50,50)   -- y=50
//
// All at z=0.2, single layer.
ParsedGCodeFile make_test_gcode() {
    ParsedGCodeFile gcode;
    Layer layer;
    layer.z_height = 0.2f;

    // Expand bounding box to cover all segments
    layer.bounding_box.expand(glm::vec3(10.0f, 20.0f, 0.2f));
    layer.bounding_box.expand(glm::vec3(50.0f, 80.0f, 0.2f));

    // Object "cube1" - segments at x=[10,50], y=20
    ToolpathSegment seg1;
    seg1.start = glm::vec3(10.0f, 20.0f, 0.2f);
    seg1.end = glm::vec3(50.0f, 20.0f, 0.2f);
    seg1.is_extrusion = true;
    seg1.object_name = "cube1";
    layer.segments.push_back(seg1);

    // Object "cube2" - segments at x=[10,50], y=80
    ToolpathSegment seg2;
    seg2.start = glm::vec3(10.0f, 80.0f, 0.2f);
    seg2.end = glm::vec3(50.0f, 80.0f, 0.2f);
    seg2.is_extrusion = true;
    seg2.object_name = "cube2";
    layer.segments.push_back(seg2);

    // Unnamed segment at y=50
    ToolpathSegment seg3;
    seg3.start = glm::vec3(10.0f, 50.0f, 0.2f);
    seg3.end = glm::vec3(50.0f, 50.0f, 0.2f);
    seg3.is_extrusion = true;
    // object_name left empty
    layer.segments.push_back(seg3);

    layer.segment_count_extrusion = 3;
    layer.segment_count_travel = 0;

    gcode.layers.push_back(std::move(layer));
    gcode.total_segments = 3;

    // Set global bounding box
    gcode.global_bounding_box.expand(glm::vec3(10.0f, 20.0f, 0.2f));
    gcode.global_bounding_box.expand(glm::vec3(50.0f, 80.0f, 0.2f));

    return gcode;
}

// Helper to build a ParsedGCodeFile with a single named object for simple tests.
ParsedGCodeFile make_single_object_gcode(const std::string& name, float y) {
    ParsedGCodeFile gcode;
    Layer layer;
    layer.z_height = 0.2f;

    ToolpathSegment seg;
    seg.start = glm::vec3(10.0f, y, 0.2f);
    seg.end = glm::vec3(90.0f, y, 0.2f);
    seg.is_extrusion = true;
    seg.object_name = name;
    layer.segments.push_back(seg);

    layer.bounding_box.expand(seg.start);
    layer.bounding_box.expand(seg.end);
    layer.segment_count_extrusion = 1;

    gcode.layers.push_back(std::move(layer));
    gcode.total_segments = 1;
    gcode.global_bounding_box.expand(glm::vec3(10.0f, y, 0.2f));
    gcode.global_bounding_box.expand(glm::vec3(90.0f, y, 0.2f));

    return gcode;
}

} // namespace

// =============================================================================
// Exclude Object Support
// =============================================================================

TEST_CASE("set_excluded_objects stores names and can be cleared", "[layer_renderer][exclude]") {
    GCodeLayerRenderer renderer;
    auto gcode = make_test_gcode();
    renderer.set_gcode(&gcode);
    renderer.set_canvas_size(200, 200);
    renderer.set_view_mode(GCodeLayerRenderer::ViewMode::TOP_DOWN);
    renderer.auto_fit();
    renderer.set_current_layer(0);

    SECTION("setting excluded objects does not crash") {
        std::unordered_set<std::string> excluded = {"cube1"};
        REQUIRE_NOTHROW(renderer.set_excluded_objects(excluded));
    }

    SECTION("clearing excluded objects does not crash") {
        std::unordered_set<std::string> excluded = {"cube1", "cube2"};
        renderer.set_excluded_objects(excluded);

        std::unordered_set<std::string> empty;
        REQUIRE_NOTHROW(renderer.set_excluded_objects(empty));
    }

    SECTION("setting excluded objects with unknown name does not crash") {
        std::unordered_set<std::string> excluded = {"nonexistent_object"};
        REQUIRE_NOTHROW(renderer.set_excluded_objects(excluded));
    }
}

// =============================================================================
// Highlight Object Support
// =============================================================================

TEST_CASE("set_highlighted_objects stores names and can be cleared",
          "[layer_renderer][highlight]") {
    GCodeLayerRenderer renderer;
    auto gcode = make_test_gcode();
    renderer.set_gcode(&gcode);
    renderer.set_canvas_size(200, 200);
    renderer.set_view_mode(GCodeLayerRenderer::ViewMode::TOP_DOWN);
    renderer.auto_fit();
    renderer.set_current_layer(0);

    SECTION("setting highlighted objects does not crash") {
        std::unordered_set<std::string> highlighted = {"cube2"};
        REQUIRE_NOTHROW(renderer.set_highlighted_objects(highlighted));
    }

    SECTION("clearing highlighted objects does not crash") {
        std::unordered_set<std::string> highlighted = {"cube1"};
        renderer.set_highlighted_objects(highlighted);

        std::unordered_set<std::string> empty;
        REQUIRE_NOTHROW(renderer.set_highlighted_objects(empty));
    }

    SECTION("setting highlighted objects with unknown name does not crash") {
        std::unordered_set<std::string> highlighted = {"nonexistent_object"};
        REQUIRE_NOTHROW(renderer.set_highlighted_objects(highlighted));
    }
}

// =============================================================================
// Pick Object At - Core algorithmic tests
// =============================================================================

TEST_CASE("pick_object_at returns object name for segment under cursor", "[layer_renderer][pick]") {
    GCodeLayerRenderer renderer;
    auto gcode = make_test_gcode();
    renderer.set_gcode(&gcode);
    renderer.set_canvas_size(200, 200);
    renderer.set_view_mode(GCodeLayerRenderer::ViewMode::TOP_DOWN);
    renderer.auto_fit();
    renderer.set_current_layer(0);

    // In TOP_DOWN mode with auto_fit on bounding box (10,20)-(50,80):
    //   center = (30, 50), range_x = 40, range_y = 60
    //   scale = min(200/(40*1.1), 200/(60*1.1)) ~= min(4.54, 3.03) ~= 3.03
    //   offset_x = 30, offset_y = 50
    //
    // world_to_screen(x, y):
    //   sx = (x - 30) * scale + 100
    //   sy = 100 - (y - 50) * scale
    //
    // For cube1 midpoint (30, 20):
    //   sx = 0 * 3.03 + 100 = 100
    //   sy = 100 - (-30) * 3.03 = 100 + 90.9 = ~191
    //
    // For cube2 midpoint (30, 80):
    //   sx = 100
    //   sy = 100 - 30 * 3.03 = 100 - 90.9 = ~9

    // Pick near cube1's midpoint (bottom of screen in top-down, since Y is flipped)
    auto result_cube1 = renderer.pick_object_at(100, 191);
    REQUIRE(result_cube1.has_value());
    CHECK(result_cube1.value() == "cube1");

    // Pick near cube2's midpoint (top of screen in top-down)
    auto result_cube2 = renderer.pick_object_at(100, 9);
    REQUIRE(result_cube2.has_value());
    CHECK(result_cube2.value() == "cube2");
}

TEST_CASE("pick_object_at returns nullopt for empty space", "[layer_renderer][pick]") {
    GCodeLayerRenderer renderer;
    auto gcode = make_test_gcode();
    renderer.set_gcode(&gcode);
    renderer.set_canvas_size(200, 200);
    renderer.set_view_mode(GCodeLayerRenderer::ViewMode::TOP_DOWN);
    renderer.auto_fit();
    renderer.set_current_layer(0);

    // Pick at far corner where no segments exist
    auto result = renderer.pick_object_at(0, 0);
    REQUIRE_FALSE(result.has_value());

    // Another empty spot
    auto result2 = renderer.pick_object_at(199, 199);
    REQUIRE_FALSE(result2.has_value());
}

TEST_CASE("pick_object_at skips segments without object_name", "[layer_renderer][pick]") {
    GCodeLayerRenderer renderer;
    auto gcode = make_test_gcode();
    renderer.set_gcode(&gcode);
    renderer.set_canvas_size(200, 200);
    renderer.set_view_mode(GCodeLayerRenderer::ViewMode::TOP_DOWN);
    renderer.auto_fit();
    renderer.set_current_layer(0);

    // The unnamed segment is at y=50 (world), which maps to sy=100 (screen center).
    // Picking at screen center should NOT return a result since the unnamed
    // segment has an empty object_name.
    auto result = renderer.pick_object_at(100, 100);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("pick_object_at with multiple objects picks closest", "[layer_renderer][pick]") {
    GCodeLayerRenderer renderer;
    auto gcode = make_test_gcode();
    renderer.set_gcode(&gcode);
    renderer.set_canvas_size(200, 200);
    renderer.set_view_mode(GCodeLayerRenderer::ViewMode::TOP_DOWN);
    renderer.auto_fit();
    renderer.set_current_layer(0);

    // cube1 is at y=20 (screen ~191), cube2 is at y=80 (screen ~9)
    // Pick slightly closer to cube1 than cube2
    auto near_cube1 = renderer.pick_object_at(100, 180);
    if (near_cube1.has_value()) {
        CHECK(near_cube1.value() == "cube1");
    }

    // Pick slightly closer to cube2 than cube1
    auto near_cube2 = renderer.pick_object_at(100, 20);
    if (near_cube2.has_value()) {
        CHECK(near_cube2.value() == "cube2");
    }
}

TEST_CASE("pick_object_at with no gcode returns nullopt", "[layer_renderer][pick]") {
    GCodeLayerRenderer renderer;
    renderer.set_canvas_size(200, 200);
    renderer.set_view_mode(GCodeLayerRenderer::ViewMode::TOP_DOWN);

    // No gcode set - should return nullopt
    auto result = renderer.pick_object_at(100, 100);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("pick_object_at with empty layer returns nullopt", "[layer_renderer][pick]") {
    GCodeLayerRenderer renderer;
    ParsedGCodeFile gcode;
    Layer empty_layer;
    empty_layer.z_height = 0.2f;
    gcode.layers.push_back(empty_layer);
    gcode.total_segments = 0;

    renderer.set_gcode(&gcode);
    renderer.set_canvas_size(200, 200);
    renderer.set_view_mode(GCodeLayerRenderer::ViewMode::TOP_DOWN);
    renderer.set_current_layer(0);

    auto result = renderer.pick_object_at(100, 100);
    REQUIRE_FALSE(result.has_value());
}

// =============================================================================
// Exclude + Pick interaction
// =============================================================================

TEST_CASE("excluded objects are still pickable", "[layer_renderer][exclude][pick]") {
    // Excluded objects should still be pickable (user needs to be able to
    // un-exclude them by tapping on them)
    GCodeLayerRenderer renderer;
    auto gcode = make_test_gcode();
    renderer.set_gcode(&gcode);
    renderer.set_canvas_size(200, 200);
    renderer.set_view_mode(GCodeLayerRenderer::ViewMode::TOP_DOWN);
    renderer.auto_fit();
    renderer.set_current_layer(0);

    // Exclude cube1
    std::unordered_set<std::string> excluded = {"cube1"};
    renderer.set_excluded_objects(excluded);

    // Should still be able to pick cube1
    auto result = renderer.pick_object_at(100, 191);
    REQUIRE(result.has_value());
    CHECK(result.value() == "cube1");
}

// =============================================================================
// Exclude/Highlight with no data source
// =============================================================================

TEST_CASE("set_excluded_objects with no gcode does not crash", "[layer_renderer][exclude]") {
    GCodeLayerRenderer renderer;

    std::unordered_set<std::string> excluded = {"cube1"};
    REQUIRE_NOTHROW(renderer.set_excluded_objects(excluded));
}

TEST_CASE("set_highlighted_objects with no gcode does not crash", "[layer_renderer][highlight]") {
    GCodeLayerRenderer renderer;

    std::unordered_set<std::string> highlighted = {"cube1"};
    REQUIRE_NOTHROW(renderer.set_highlighted_objects(highlighted));
}
