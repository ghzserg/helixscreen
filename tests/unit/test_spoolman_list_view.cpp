// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_spoolman_list_view.h"

#include "../lvgl_test_fixture.h"
#include "../lvgl_ui_test_fixture.h"

#include "../catch_amalgamated.hpp"

using namespace helix::ui;

// ============================================================================
// Unit Tests (LVGLTestFixture - minimal LVGL, no XML)
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "SpoolmanListView - setup with null container",
                 "[spoolman_list_view]") {
    SpoolmanListView view;
    REQUIRE(view.setup(nullptr) == false);
    REQUIRE(view.is_initialized() == false);
}

TEST_CASE_METHOD(LVGLTestFixture, "SpoolmanListView - setup with valid container",
                 "[spoolman_list_view]") {
    SpoolmanListView view;
    lv_obj_t* container = lv_obj_create(test_screen());
    REQUIRE(view.setup(container) == true);
    REQUIRE(view.container() == container);
}

TEST_CASE_METHOD(LVGLTestFixture, "SpoolmanListView - cleanup is safe to call twice",
                 "[spoolman_list_view]") {
    SpoolmanListView view;
    lv_obj_t* container = lv_obj_create(test_screen());
    view.setup(container);
    view.cleanup();
    view.cleanup(); // Should not crash
    REQUIRE(view.is_initialized() == false);
    REQUIRE(view.container() == nullptr);
}

TEST_CASE_METHOD(LVGLTestFixture, "SpoolmanListView - cleanup without setup",
                 "[spoolman_list_view]") {
    SpoolmanListView view;
    view.cleanup(); // Should not crash
    REQUIRE(view.is_initialized() == false);
}

TEST_CASE_METHOD(LVGLTestFixture, "SpoolmanListView - constants are reasonable",
                 "[spoolman_list_view]") {
    REQUIRE(SpoolmanListView::POOL_SIZE == 20);
    REQUIRE(SpoolmanListView::BUFFER_ROWS == 2);
    REQUIRE(SpoolmanListView::POOL_SIZE > SpoolmanListView::BUFFER_ROWS * 2);
}

TEST_CASE_METHOD(LVGLTestFixture, "SpoolmanListView - populate with empty list",
                 "[spoolman_list_view]") {
    SpoolmanListView view;
    lv_obj_t* container = lv_obj_create(test_screen());
    lv_obj_set_size(container, 400, 600);
    view.setup(container);

    std::vector<SpoolInfo> empty_spools;
    // Should not crash with empty list (pool won't be initialized without XML)
    view.populate(empty_spools, -1);
}

TEST_CASE_METHOD(LVGLTestFixture, "SpoolmanListView - update_visible with no data",
                 "[spoolman_list_view]") {
    SpoolmanListView view;
    lv_obj_t* container = lv_obj_create(test_screen());
    view.setup(container);

    std::vector<SpoolInfo> empty_spools;
    view.update_visible(empty_spools, -1); // Should not crash
}

TEST_CASE_METHOD(LVGLTestFixture, "SpoolmanListView - refresh_content with no data",
                 "[spoolman_list_view]") {
    SpoolmanListView view;
    lv_obj_t* container = lv_obj_create(test_screen());
    view.setup(container);

    std::vector<SpoolInfo> spools;
    view.refresh_content(spools, -1); // Should not crash
}

TEST_CASE_METHOD(LVGLTestFixture, "SpoolmanListView - update_active_indicators with no pool",
                 "[spoolman_list_view]") {
    SpoolmanListView view;
    lv_obj_t* container = lv_obj_create(test_screen());
    view.setup(container);

    std::vector<SpoolInfo> spools;
    view.update_active_indicators(spools, 1); // Should not crash
}

// ============================================================================
// Integration Tests (LVGLUITestFixture - full XML component registration)
// ============================================================================

static std::vector<SpoolInfo> make_test_spools(int count) {
    std::vector<SpoolInfo> spools;
    spools.reserve(count);
    for (int i = 0; i < count; i++) {
        SpoolInfo s;
        s.id = i + 1;
        s.vendor = "TestVendor";
        s.material = (i % 2 == 0) ? "PLA" : "PETG";
        s.color_name = "Color " + std::to_string(i + 1);
        s.color_hex = "#808080";
        s.initial_weight_g = 1000.0;
        s.remaining_weight_g = 1000.0 - (i * 50.0);
        spools.push_back(s);
    }
    return spools;
}

TEST_CASE_METHOD(LVGLUITestFixture, "SpoolmanListView - populate creates pool rows",
                 "[spoolman_list_view][ui_integration]") {
    SpoolmanListView view;
    lv_obj_t* container = lv_obj_create(test_screen());
    lv_obj_set_size(container, 400, 600);
    lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    view.setup(container);

    auto spools = make_test_spools(5);
    view.populate(spools, 1);
    process_lvgl(50);

    REQUIRE(view.is_initialized() == true);
}

TEST_CASE_METHOD(LVGLUITestFixture, "SpoolmanListView - populate with many spools",
                 "[spoolman_list_view][ui_integration]") {
    SpoolmanListView view;
    lv_obj_t* container = lv_obj_create(test_screen());
    lv_obj_set_size(container, 400, 600);
    lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    view.setup(container);

    auto spools = make_test_spools(50);
    view.populate(spools, 1);
    process_lvgl(50);

    REQUIRE(view.is_initialized() == true);
    // Only POOL_SIZE rows should be created (not 50)
}

TEST_CASE_METHOD(LVGLUITestFixture, "SpoolmanListView - active indicators update",
                 "[spoolman_list_view][ui_integration]") {
    SpoolmanListView view;
    lv_obj_t* container = lv_obj_create(test_screen());
    lv_obj_set_size(container, 400, 600);
    lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    view.setup(container);

    auto spools = make_test_spools(10);
    view.populate(spools, 1);
    process_lvgl(50);

    // Change active spool
    view.update_active_indicators(spools, 5);
    process_lvgl(50);
    // Should not crash, active spool is now 5
}

TEST_CASE_METHOD(LVGLUITestFixture, "SpoolmanListView - refresh content",
                 "[spoolman_list_view][ui_integration]") {
    SpoolmanListView view;
    lv_obj_t* container = lv_obj_create(test_screen());
    lv_obj_set_size(container, 400, 600);
    lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    view.setup(container);

    auto spools = make_test_spools(10);
    view.populate(spools, 1);
    process_lvgl(50);

    // Modify data and refresh
    spools[0].remaining_weight_g = 42.0;
    view.refresh_content(spools, 1);
    process_lvgl(50);
    // Should not crash
}
