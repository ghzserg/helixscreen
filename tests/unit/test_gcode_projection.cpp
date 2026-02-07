// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_projection.h"

#include "../catch_amalgamated.hpp"

using namespace helix::gcode;

// ============================================================================
// project() TESTS
// ============================================================================

TEST_CASE("project: TOP_DOWN maps center to canvas center", "[projection]") {
    ProjectionParams params;
    params.view_mode = ViewMode::TOP_DOWN;
    params.scale = 1.0f;
    params.offset_x = 50.0f;
    params.offset_y = 50.0f;
    params.canvas_width = 100;
    params.canvas_height = 100;

    auto p = project(params, 50.0f, 50.0f);
    REQUIRE(p.x == 50);
    REQUIRE(p.y == 50);
}

TEST_CASE("project: TOP_DOWN positive Y moves up (lower pixel Y)", "[projection]") {
    ProjectionParams params;
    params.view_mode = ViewMode::TOP_DOWN;
    params.scale = 1.0f;
    params.offset_x = 0.0f;
    params.offset_y = 0.0f;
    params.canvas_width = 100;
    params.canvas_height = 100;

    auto center = project(params, 0.0f, 0.0f);
    auto above = project(params, 0.0f, 10.0f);

    // Positive Y in world = lower pixel Y (toward top of screen)
    REQUIRE(above.y < center.y);
    REQUIRE(above.x == center.x);
}

TEST_CASE("project: TOP_DOWN ignores Z coordinate", "[projection]") {
    ProjectionParams params;
    params.view_mode = ViewMode::TOP_DOWN;
    params.scale = 1.0f;
    params.offset_x = 0.0f;
    params.offset_y = 0.0f;
    params.canvas_width = 100;
    params.canvas_height = 100;

    auto at_z0 = project(params, 10.0f, 20.0f, 0.0f);
    auto at_z100 = project(params, 10.0f, 20.0f, 100.0f);

    REQUIRE(at_z0.x == at_z100.x);
    REQUIRE(at_z0.y == at_z100.y);
}

TEST_CASE("project: FRONT view center maps to canvas center", "[projection]") {
    ProjectionParams params;
    params.view_mode = ViewMode::FRONT;
    params.scale = 1.0f;
    params.offset_x = 50.0f;
    params.offset_y = 50.0f;
    params.offset_z = 5.0f;
    params.canvas_width = 100;
    params.canvas_height = 100;

    auto p = project(params, 50.0f, 50.0f, 5.0f);
    REQUIRE(p.x == 50);
    REQUIRE(p.y == 50);
}

TEST_CASE("project: FRONT view higher Z moves up (lower pixel Y)", "[projection]") {
    ProjectionParams params;
    params.view_mode = ViewMode::FRONT;
    params.scale = 1.0f;
    params.offset_x = 0.0f;
    params.offset_y = 0.0f;
    params.offset_z = 0.0f;
    params.canvas_width = 100;
    params.canvas_height = 100;

    auto low = project(params, 0.0f, 0.0f, 0.0f);
    auto high = project(params, 0.0f, 0.0f, 10.0f);

    // Higher Z = lower pixel Y (toward top of screen)
    REQUIRE(high.y < low.y);
}

TEST_CASE("project: FRONT view Z contributes to Y displacement", "[projection]") {
    ProjectionParams params;
    params.view_mode = ViewMode::FRONT;
    params.scale = 1.0f;
    params.offset_x = 0.0f;
    params.offset_y = 0.0f;
    params.offset_z = 0.0f;
    params.canvas_width = 100;
    params.canvas_height = 100;

    // Two points same XY but different Z should have different screen positions
    auto z0 = project(params, 10.0f, 10.0f, 0.0f);
    auto z10 = project(params, 10.0f, 10.0f, 10.0f);

    REQUIRE(z0.y != z10.y);
}

TEST_CASE("project: content_offset_y_percent shifts Y", "[projection]") {
    ProjectionParams params;
    params.view_mode = ViewMode::TOP_DOWN;
    params.scale = 1.0f;
    params.offset_x = 0.0f;
    params.offset_y = 0.0f;
    params.canvas_width = 100;
    params.canvas_height = 100;

    params.content_offset_y_percent = 0.0f;
    auto no_offset = project(params, 0.0f, 0.0f);

    params.content_offset_y_percent = 0.1f;
    auto with_offset = project(params, 0.0f, 0.0f);

    // 10% of 100px = 10px shift down
    REQUIRE(with_offset.y == no_offset.y + 10);
}

TEST_CASE("project: ISOMETRIC view center maps to canvas center", "[projection]") {
    ProjectionParams params;
    params.view_mode = ViewMode::ISOMETRIC;
    params.scale = 1.0f;
    params.offset_x = 50.0f;
    params.offset_y = 50.0f;
    params.canvas_width = 100;
    params.canvas_height = 100;

    auto p = project(params, 50.0f, 50.0f);
    REQUIRE(p.x == 50);
    REQUIRE(p.y == 50);
}

// ============================================================================
// compute_auto_fit() TESTS
// ============================================================================

TEST_CASE("compute_auto_fit: basic square bounding box", "[projection]") {
    AABB bb;
    bb.expand(glm::vec3(0.0f, 0.0f, 0.0f));
    bb.expand(glm::vec3(100.0f, 100.0f, 0.0f));

    auto fit = compute_auto_fit(bb, ViewMode::TOP_DOWN, 100, 100);

    REQUIRE(fit.scale > 0.0f);
    REQUIRE(fit.offset_x == Catch::Approx(50.0f));
    REQUIRE(fit.offset_y == Catch::Approx(50.0f));
}

TEST_CASE("compute_auto_fit: wider canvas gives same scale as taller", "[projection]") {
    AABB bb;
    bb.expand(glm::vec3(0.0f, 0.0f, 0.0f));
    bb.expand(glm::vec3(100.0f, 100.0f, 0.0f));

    // Square object in wide canvas vs tall canvas
    auto fit_wide = compute_auto_fit(bb, ViewMode::TOP_DOWN, 200, 100);
    auto fit_tall = compute_auto_fit(bb, ViewMode::TOP_DOWN, 100, 200);

    // Both constrained by the smaller dimension, so scale should be the same
    REQUIRE(fit_wide.scale == Catch::Approx(fit_tall.scale));
}

TEST_CASE("compute_auto_fit: degenerate bbox gets valid result", "[projection]") {
    AABB bb;
    bb.expand(glm::vec3(50.0f, 50.0f, 0.0f));
    bb.expand(glm::vec3(50.0f, 50.0f, 0.0f)); // Zero-size

    auto fit = compute_auto_fit(bb, ViewMode::TOP_DOWN, 100, 100);

    // Should not produce inf/nan
    REQUIRE(std::isfinite(fit.scale));
    REQUIRE(fit.scale > 0.0f);
}

TEST_CASE("compute_auto_fit: FRONT view includes Z in fitting", "[projection]") {
    // Flat object (no Z extent)
    AABB flat;
    flat.expand(glm::vec3(0.0f, 0.0f, 0.0f));
    flat.expand(glm::vec3(100.0f, 100.0f, 0.2f));

    // Tall object (large Z extent)
    AABB tall;
    tall.expand(glm::vec3(0.0f, 0.0f, 0.0f));
    tall.expand(glm::vec3(100.0f, 100.0f, 200.0f));

    auto fit_flat = compute_auto_fit(flat, ViewMode::FRONT, 100, 100);
    auto fit_tall = compute_auto_fit(tall, ViewMode::FRONT, 100, 100);

    // Tall object needs smaller scale to fit
    REQUIRE(fit_tall.scale < fit_flat.scale);

    // FRONT view should set offset_z
    REQUIRE(fit_tall.offset_z == Catch::Approx(100.0f));
}

TEST_CASE("compute_auto_fit: padding reduces effective scale", "[projection]") {
    AABB bb;
    bb.expand(glm::vec3(0.0f, 0.0f, 0.0f));
    bb.expand(glm::vec3(100.0f, 100.0f, 0.0f));

    auto no_pad = compute_auto_fit(bb, ViewMode::TOP_DOWN, 100, 100, 0.0f);
    auto with_pad = compute_auto_fit(bb, ViewMode::TOP_DOWN, 100, 100, 0.1f);

    REQUIRE(with_pad.scale < no_pad.scale);
}

TEST_CASE("compute_auto_fit: TOP_DOWN offset_z stays zero", "[projection]") {
    AABB bb;
    bb.expand(glm::vec3(0.0f, 0.0f, 0.0f));
    bb.expand(glm::vec3(100.0f, 100.0f, 50.0f));

    auto fit = compute_auto_fit(bb, ViewMode::TOP_DOWN, 100, 100);

    // TOP_DOWN should not set offset_z
    REQUIRE(fit.offset_z == Catch::Approx(0.0f));
}
