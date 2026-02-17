// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "ams_state.h"
#include "ams_types.h"
#include "theme_manager.h"
#include "ui/ams_drawing_utils.h"

#include "../catch_amalgamated.hpp"

// ============================================================================
// SlotInfo::is_present tests
// ============================================================================

TEST_CASE("SlotInfo::is_present returns false for EMPTY", "[ams_draw][slot_info]") {
    SlotInfo slot;
    slot.status = SlotStatus::EMPTY;
    REQUIRE_FALSE(slot.is_present());
}

TEST_CASE("SlotInfo::is_present returns false for UNKNOWN", "[ams_draw][slot_info]") {
    SlotInfo slot;
    slot.status = SlotStatus::UNKNOWN;
    REQUIRE_FALSE(slot.is_present());
}

TEST_CASE("SlotInfo::is_present returns true for AVAILABLE", "[ams_draw][slot_info]") {
    SlotInfo slot;
    slot.status = SlotStatus::AVAILABLE;
    REQUIRE(slot.is_present());
}

TEST_CASE("SlotInfo::is_present returns true for LOADED", "[ams_draw][slot_info]") {
    SlotInfo slot;
    slot.status = SlotStatus::LOADED;
    REQUIRE(slot.is_present());
}

TEST_CASE("SlotInfo::is_present returns true for FROM_BUFFER", "[ams_draw][slot_info]") {
    SlotInfo slot;
    slot.status = SlotStatus::FROM_BUFFER;
    REQUIRE(slot.is_present());
}

TEST_CASE("SlotInfo::is_present returns true for BLOCKED", "[ams_draw][slot_info]") {
    SlotInfo slot;
    slot.status = SlotStatus::BLOCKED;
    REQUIRE(slot.is_present());
}

// ============================================================================
// Color utility tests
// ============================================================================

TEST_CASE("ams_draw::lighten_color adds amount clamped to 255", "[ams_draw][color]") {
    lv_color_t c = lv_color_make(100, 200, 250);
    lv_color_t result = ams_draw::lighten_color(c, 50);
    REQUIRE(result.red == 150);
    REQUIRE(result.green == 250);
    REQUIRE(result.blue == 255);
}

TEST_CASE("ams_draw::darken_color subtracts amount clamped to 0", "[ams_draw][color]") {
    lv_color_t c = lv_color_make(30, 100, 200);
    lv_color_t result = ams_draw::darken_color(c, 50);
    REQUIRE(result.red == 0);
    REQUIRE(result.green == 50);
    REQUIRE(result.blue == 150);
}

TEST_CASE("ams_draw::blend_color interpolates between colors", "[ams_draw][color]") {
    lv_color_t black = lv_color_make(0, 0, 0);
    lv_color_t white = lv_color_make(255, 255, 255);

    lv_color_t at_zero = ams_draw::blend_color(black, white, 0.0f);
    REQUIRE(at_zero.red == 0);

    lv_color_t at_one = ams_draw::blend_color(black, white, 1.0f);
    REQUIRE(at_one.red == 255);

    lv_color_t mid = ams_draw::blend_color(black, white, 0.5f);
    REQUIRE(mid.red >= 126);
    REQUIRE(mid.red <= 128);
}

TEST_CASE("ams_draw::blend_color clamps factor to [0,1]", "[ams_draw][color]") {
    lv_color_t a = lv_color_make(100, 100, 100);
    lv_color_t b = lv_color_make(200, 200, 200);

    lv_color_t below = ams_draw::blend_color(a, b, -1.0f);
    REQUIRE(below.red == 100);

    lv_color_t above = ams_draw::blend_color(a, b, 2.0f);
    REQUIRE(above.red == 200);
}

// ============================================================================
// Severity & Error tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "ams_draw::severity_color maps ERROR to danger",
                 "[ams_draw][severity]") {
    lv_color_t result = ams_draw::severity_color(SlotError::ERROR);
    lv_color_t expected = theme_manager_get_color("danger");
    REQUIRE(result.red == expected.red);
    REQUIRE(result.green == expected.green);
    REQUIRE(result.blue == expected.blue);
}

TEST_CASE_METHOD(LVGLTestFixture, "ams_draw::severity_color maps WARNING to warning",
                 "[ams_draw][severity]") {
    lv_color_t result = ams_draw::severity_color(SlotError::WARNING);
    lv_color_t expected = theme_manager_get_color("warning");
    REQUIRE(result.red == expected.red);
}

TEST_CASE_METHOD(LVGLTestFixture, "ams_draw::severity_color maps INFO to text_muted",
                 "[ams_draw][severity]") {
    lv_color_t result = ams_draw::severity_color(SlotError::INFO);
    lv_color_t expected = theme_manager_get_color("text_muted");
    REQUIRE(result.red == expected.red);
}

TEST_CASE("ams_draw::worst_unit_severity returns INFO for no errors", "[ams_draw][severity]") {
    AmsUnit unit;
    unit.slots.resize(4);
    REQUIRE(ams_draw::worst_unit_severity(unit) == SlotError::INFO);
}

TEST_CASE("ams_draw::worst_unit_severity finds ERROR among warnings", "[ams_draw][severity]") {
    AmsUnit unit;
    unit.slots.resize(4);
    unit.slots[1].error = SlotError{"warn", SlotError::WARNING};
    unit.slots[3].error = SlotError{"err", SlotError::ERROR};
    REQUIRE(ams_draw::worst_unit_severity(unit) == SlotError::ERROR);
}

// ============================================================================
// Fill percent tests
// ============================================================================

TEST_CASE("ams_draw::fill_percent_from_slot with known weight", "[ams_draw][fill]") {
    SlotInfo slot;
    slot.remaining_weight_g = 500.0f;
    slot.total_weight_g = 1000.0f;
    REQUIRE(ams_draw::fill_percent_from_slot(slot) == 50);
}

TEST_CASE("ams_draw::fill_percent_from_slot clamps to min_pct", "[ams_draw][fill]") {
    SlotInfo slot;
    slot.remaining_weight_g = 1.0f;
    slot.total_weight_g = 1000.0f;
    REQUIRE(ams_draw::fill_percent_from_slot(slot) == 5);
}

TEST_CASE("ams_draw::fill_percent_from_slot returns 100 for unknown weight", "[ams_draw][fill]") {
    SlotInfo slot;
    slot.remaining_weight_g = -1.0f;
    slot.total_weight_g = 0.0f;
    REQUIRE(ams_draw::fill_percent_from_slot(slot) == 100);
}

TEST_CASE("ams_draw::fill_percent_from_slot custom min_pct", "[ams_draw][fill]") {
    SlotInfo slot;
    slot.remaining_weight_g = 0.0f;
    slot.total_weight_g = 1000.0f;
    REQUIRE(ams_draw::fill_percent_from_slot(slot, 0) == 0);
    REQUIRE(ams_draw::fill_percent_from_slot(slot, 10) == 10);
}

// ============================================================================
// Bar width tests
// ============================================================================

TEST_CASE("ams_draw::calc_bar_width distributes evenly", "[ams_draw][bar_width]") {
    int32_t w = ams_draw::calc_bar_width(100, 4, 2, 6, 14);
    REQUIRE(w == 14);
}

TEST_CASE("ams_draw::calc_bar_width respects min", "[ams_draw][bar_width]") {
    int32_t w = ams_draw::calc_bar_width(20, 16, 2, 6, 14);
    REQUIRE(w == 6);
}

TEST_CASE("ams_draw::calc_bar_width with container_pct", "[ams_draw][bar_width]") {
    int32_t w = ams_draw::calc_bar_width(100, 1, 2, 6, 14, 90);
    REQUIRE(w == 14);
}

TEST_CASE("ams_draw::calc_bar_width handles zero slots", "[ams_draw][bar_width]") {
    int32_t w = ams_draw::calc_bar_width(100, 0, 2, 6, 14);
    REQUIRE(w == 14);
}

// ============================================================================
// Display name tests
// ============================================================================

TEST_CASE("ams_draw::get_unit_display_name uses name when set", "[ams_draw][display_name]") {
    AmsUnit unit;
    unit.name = "Box Turtle 1";
    REQUIRE(ams_draw::get_unit_display_name(unit, 0) == "Box Turtle 1");
}

TEST_CASE("ams_draw::get_unit_display_name falls back to Unit N", "[ams_draw][display_name]") {
    AmsUnit unit;
    REQUIRE(ams_draw::get_unit_display_name(unit, 0) == "Unit 1");
    REQUIRE(ams_draw::get_unit_display_name(unit, 2) == "Unit 3");
}

// ============================================================================
// Transparent Container
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "ams_draw::create_transparent_container basic properties",
                 "[ams_draw][container]") {
    lv_obj_t* c = ams_draw::create_transparent_container(test_screen());
    REQUIRE(c != nullptr);
    REQUIRE(lv_obj_get_style_bg_opa(c, LV_PART_MAIN) == LV_OPA_TRANSP);
    REQUIRE(lv_obj_get_style_border_width(c, LV_PART_MAIN) == 0);
    REQUIRE(lv_obj_get_style_pad_top(c, LV_PART_MAIN) == 0);
    REQUIRE(lv_obj_has_flag(c, LV_OBJ_FLAG_EVENT_BUBBLE));
    REQUIRE_FALSE(lv_obj_has_flag(c, LV_OBJ_FLAG_SCROLLABLE));
}

// ============================================================================
// Error Badge
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "ams_draw::create_error_badge creates circle",
                 "[ams_draw][badge]") {
    lv_obj_t* badge = ams_draw::create_error_badge(test_screen(), 12);
    REQUIRE(badge != nullptr);
    lv_obj_update_layout(badge);
    REQUIRE(lv_obj_get_width(badge) == 12);
    REQUIRE(lv_obj_get_height(badge) == 12);
    REQUIRE(lv_obj_get_style_radius(badge, LV_PART_MAIN) == LV_RADIUS_CIRCLE);
    REQUIRE(lv_obj_has_flag(badge, LV_OBJ_FLAG_HIDDEN)); // Hidden by default
}

TEST_CASE_METHOD(LVGLTestFixture, "ams_draw::update_error_badge shows on error",
                 "[ams_draw][badge]") {
    lv_obj_t* badge = ams_draw::create_error_badge(test_screen(), 12);

    ams_draw::update_error_badge(badge, true, SlotError::ERROR, false);
    REQUIRE_FALSE(lv_obj_has_flag(badge, LV_OBJ_FLAG_HIDDEN));

    ams_draw::update_error_badge(badge, false, SlotError::INFO, false);
    REQUIRE(lv_obj_has_flag(badge, LV_OBJ_FLAG_HIDDEN));
}

// ============================================================================
// Pulse Animation
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "ams_draw::start_pulse and stop_pulse don't crash",
                 "[ams_draw][pulse]") {
    lv_obj_t* dot = ams_draw::create_error_badge(test_screen(), 14);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
    process_lvgl(10);

    // Start pulse — should set border_color for anim callback reference
    ams_draw::start_pulse(dot, lv_color_hex(0xFF0000));
    process_lvgl(50);

    // Animation is running; just verify no crash occurred
    // Border color should be stored as the base color for the color callback
    lv_color_t border = lv_obj_get_style_border_color(dot, LV_PART_MAIN);
    REQUIRE(border.red == 0xFF);

    // Stop pulse — should restore defaults
    ams_draw::stop_pulse(dot);
    process_lvgl(10);
    REQUIRE(lv_obj_get_style_shadow_width(dot, LV_PART_MAIN) == 0);
}

// ============================================================================
// Slot Bar Column
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "ams_draw::create_slot_column creates all parts",
                 "[ams_draw][slot_bar]") {
    auto col = ams_draw::create_slot_column(test_screen(), 10, 40, 4);
    REQUIRE(col.container != nullptr);
    REQUIRE(col.bar_bg != nullptr);
    REQUIRE(col.bar_fill != nullptr);
    REQUIRE(col.status_line != nullptr);

    // bar_fill is child of bar_bg
    REQUIRE(lv_obj_get_parent(col.bar_fill) == col.bar_bg);
    // bar_bg and status_line are children of container
    REQUIRE(lv_obj_get_parent(col.bar_bg) == col.container);
    REQUIRE(lv_obj_get_parent(col.status_line) == col.container);
}

TEST_CASE_METHOD(LVGLTestFixture, "ams_draw::style_slot_bar loaded state", "[ams_draw][slot_bar]") {
    auto col = ams_draw::create_slot_column(test_screen(), 10, 40, 4);

    ams_draw::BarStyleParams params;
    params.color_rgb = 0xFF0000;
    params.fill_pct = 75;
    params.is_present = true;
    params.is_loaded = true;
    params.has_error = false;
    ams_draw::style_slot_bar(col, params, 4);

    // Loaded: 2px border, text color, 80% opacity
    REQUIRE(lv_obj_get_style_border_width(col.bar_bg, LV_PART_MAIN) == 2);
    REQUIRE(lv_obj_get_style_border_opa(col.bar_bg, LV_PART_MAIN) == LV_OPA_80);

    // Fill visible
    REQUIRE_FALSE(lv_obj_has_flag(col.bar_fill, LV_OBJ_FLAG_HIDDEN));

    // Status line hidden (loaded shown via border, not status line)
    REQUIRE(lv_obj_has_flag(col.status_line, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(LVGLTestFixture, "ams_draw::style_slot_bar error state shows status line",
                 "[ams_draw][slot_bar]") {
    auto col = ams_draw::create_slot_column(test_screen(), 10, 40, 4);

    ams_draw::BarStyleParams params;
    params.color_rgb = 0x00FF00;
    params.fill_pct = 50;
    params.is_present = true;
    params.is_loaded = false;
    params.has_error = true;
    params.severity = SlotError::ERROR;
    ams_draw::style_slot_bar(col, params, 4);

    // Error: status line visible
    REQUIRE_FALSE(lv_obj_has_flag(col.status_line, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(LVGLTestFixture, "ams_draw::style_slot_bar empty state ghosted",
                 "[ams_draw][slot_bar]") {
    auto col = ams_draw::create_slot_column(test_screen(), 10, 40, 4);

    ams_draw::BarStyleParams params;
    params.is_present = false;
    ams_draw::style_slot_bar(col, params, 4);

    // Empty: 20% border opacity, fill hidden, status line hidden
    REQUIRE(lv_obj_get_style_border_opa(col.bar_bg, LV_PART_MAIN) == LV_OPA_20);
    REQUIRE(lv_obj_has_flag(col.bar_fill, LV_OBJ_FLAG_HIDDEN));
    REQUIRE(lv_obj_has_flag(col.status_line, LV_OBJ_FLAG_HIDDEN));
}

// ============================================================================
// Logo Helper
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "ams_draw::apply_logo hides image when no logo found",
                 "[ams_draw][logo]") {
    lv_obj_t* img = lv_image_create(test_screen());
    lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);

    // Use a type name that won't match any logo
    ams_draw::apply_logo(img, "NonExistentType12345");
    REQUIRE(lv_obj_has_flag(img, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(LVGLTestFixture, "ams_draw::apply_logo with unit fallback", "[ams_draw][logo]") {
    lv_obj_t* img = lv_image_create(test_screen());

    AmsUnit unit;
    unit.name = "NonExistent";
    AmsSystemInfo info;
    info.type_name = "AlsoNonExistent";

    ams_draw::apply_logo(img, unit, info);
    // Both names unknown -> hidden
    REQUIRE(lv_obj_has_flag(img, LV_OBJ_FLAG_HIDDEN));
}
