// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_fan_arc_resize.h"

#include "lvgl_test_fixture.h"

#include "catch_amalgamated.hpp"

// Helper: create a card with dial_container and dial_arc children, zero padding/border
// for predictable math. Returns card, container, and arc pointers.
static void make_fan_card(lv_obj_t* parent, int card_w, int card_h, int container_w,
                          int container_h, lv_obj_t** out_card, lv_obj_t** out_container,
                          lv_obj_t** out_arc) {
    *out_card = lv_obj_create(parent);
    lv_obj_set_size(*out_card, card_w, card_h);
    lv_obj_set_style_pad_all(*out_card, 0, 0);
    lv_obj_set_style_border_width(*out_card, 0, 0);

    *out_container = lv_obj_create(*out_card);
    lv_obj_set_name(*out_container, "dial_container");
    lv_obj_set_size(*out_container, container_w, container_h);
    lv_obj_set_style_pad_all(*out_container, 0, 0);
    lv_obj_set_style_border_width(*out_container, 0, 0);

    *out_arc = lv_arc_create(*out_container);
    lv_obj_set_name(*out_arc, "dial_arc");
}

// ============================================================================
// fan_arc_resize_to_fit() — sizing math tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "fan_arc_resize_to_fit: null card is safe",
                 "[fan][arc][resize]") {
    helix::ui::fan_arc_resize_to_fit(nullptr);
}

TEST_CASE_METHOD(LVGLTestFixture, "fan_arc_resize_to_fit: missing children is safe",
                 "[fan][arc][resize]") {
    lv_obj_t* card = lv_obj_create(test_screen());
    helix::ui::fan_arc_resize_to_fit(card);
}

TEST_CASE_METHOD(LVGLTestFixture, "fan_arc_resize_to_fit: arc is square and tracks match",
                 "[fan][arc][resize]") {
    lv_obj_t *card, *container, *arc;
    make_fan_card(test_screen(), 200, 300, 180, 160, &card, &container, &arc);

    lv_obj_update_layout(test_screen());
    helix::ui::fan_arc_resize_to_fit(card);

    int32_t arc_w = lv_obj_get_width(arc);
    int32_t arc_h = lv_obj_get_height(arc);

    // Core invariant: arc must be square
    REQUIRE(arc_w == arc_h);
    REQUIRE(arc_w > 0);
    REQUIRE(arc_w >= 60); // Minimum size

    // Track widths: main and indicator must match
    int32_t track_w = lv_obj_get_style_arc_width(arc, LV_PART_MAIN);
    int32_t indicator_w = lv_obj_get_style_arc_width(arc, LV_PART_INDICATOR);
    REQUIRE(track_w == indicator_w);
    REQUIRE(track_w >= 6); // Minimum track width

    // Verify approximately 11:1 ratio (±1px for LVGL layout rounding)
    int32_t expected_track = LV_MAX(arc_w / 11, 6);
    REQUIRE(abs(track_w - expected_track) <= 1);
}

TEST_CASE_METHOD(LVGLTestFixture, "fan_arc_resize_to_fit: clamps to minimum 60px",
                 "[fan][arc][resize]") {
    lv_obj_t *card, *container, *arc;
    // Very small card — arc must clamp to minimum
    make_fan_card(test_screen(), 50, 50, 40, 40, &card, &container, &arc);

    lv_obj_update_layout(test_screen());
    helix::ui::fan_arc_resize_to_fit(card);
    lv_obj_update_layout(test_screen()); // Reflect new sizes

    int32_t arc_size = lv_obj_get_width(arc);
    REQUIRE(arc_size >= 60);
    REQUIRE(arc_size == lv_obj_get_height(arc)); // Still square

    // Track width at minimum size: 60/11 = 5 → clamped to 6
    int32_t track_w = lv_obj_get_style_arc_width(arc, LV_PART_MAIN);
    REQUIRE(track_w == LV_MAX(arc_size / 11, 6));
}

TEST_CASE_METHOD(LVGLTestFixture, "fan_arc_resize_to_fit: constrained by smaller dimension",
                 "[fan][arc][resize]") {
    lv_obj_t *card, *container, *arc;
    // Wide card, short container — arc should be constrained by container height
    make_fan_card(test_screen(), 300, 400, 280, 100, &card, &container, &arc);

    lv_obj_update_layout(test_screen());
    helix::ui::fan_arc_resize_to_fit(card);
    lv_obj_update_layout(test_screen()); // Reflect new sizes

    int32_t arc_size = lv_obj_get_width(arc);
    REQUIRE(arc_size == lv_obj_get_height(arc)); // Square

    // Arc should fit within both card content width and container content height
    int32_t content_w = lv_obj_get_content_width(card);
    int32_t container_h = lv_obj_get_content_height(container);
    REQUIRE(arc_size <= content_w);
    REQUIRE(arc_size <= container_h);
}

TEST_CASE_METHOD(LVGLTestFixture, "fan_arc_resize_to_fit: track scales with arc size",
                 "[fan][arc][resize]") {
    lv_obj_t *card, *container, *arc;
    make_fan_card(test_screen(), 300, 300, 260, 260, &card, &container, &arc);

    lv_obj_update_layout(test_screen());
    helix::ui::fan_arc_resize_to_fit(card);
    lv_obj_update_layout(test_screen()); // Reflect new sizes

    int32_t arc_size = lv_obj_get_width(arc);
    REQUIRE(arc_size > 100);

    int32_t track_w = lv_obj_get_style_arc_width(arc, LV_PART_MAIN);
    REQUIRE(track_w == LV_MAX(arc_size / 11, 6));
}

// ============================================================================
// fan_arc_attach_auto_resize() — callback attachment tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "fan_arc_attach_auto_resize: null is safe",
                 "[fan][arc][resize]") {
    helix::ui::fan_arc_attach_auto_resize(nullptr);
}

TEST_CASE_METHOD(LVGLTestFixture, "fan_arc_attach_auto_resize: triggers initial resize",
                 "[fan][arc][resize]") {
    lv_obj_t *card, *container, *arc;
    make_fan_card(test_screen(), 200, 300, 180, 160, &card, &container, &arc);

    lv_obj_update_layout(test_screen());

    // Attach should trigger immediate resize — arc should be square
    helix::ui::fan_arc_attach_auto_resize(card);

    int32_t arc_w = lv_obj_get_width(arc);
    int32_t arc_h = lv_obj_get_height(arc);
    REQUIRE(arc_w == arc_h); // Square
    REQUIRE(arc_w >= 60);    // At least minimum size
}

TEST_CASE_METHOD(LVGLTestFixture, "fan_arc_attach_auto_resize: resizes on SIZE_CHANGED",
                 "[fan][arc][resize]") {
    lv_obj_t *card, *container, *arc;
    make_fan_card(test_screen(), 200, 300, 180, 160, &card, &container, &arc);

    lv_obj_update_layout(test_screen());
    helix::ui::fan_arc_attach_auto_resize(card);

    int32_t initial_size = lv_obj_get_width(arc);
    REQUIRE(initial_size > 0);

    // Shrink the card — SIZE_CHANGED callback should resize arc
    lv_obj_set_size(card, 120, 200);
    lv_obj_set_size(container, 100, 100);
    lv_obj_update_layout(test_screen());
    process_lvgl(50);

    int32_t new_size = lv_obj_get_width(arc);
    REQUIRE(new_size != initial_size);           // Size should have changed
    REQUIRE(new_size == lv_obj_get_height(arc)); // Still square
}
