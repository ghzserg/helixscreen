// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

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
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#include "ui_theme.h"

#include "lvgl/lvgl.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <string>

// Test result tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message)                                                            \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(condition)) {                                                                        \
            tests_failed++;                                                                        \
            spdlog::error("FAIL: {} - {}", __func__, message);                                     \
            return false;                                                                          \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

#define RUN_TEST(test_func)                                                                        \
    do {                                                                                           \
        spdlog::info("Running: {}", #test_func);                                                   \
        if (test_func()) {                                                                         \
            spdlog::info("  ✓ PASSED");                                                            \
        } else {                                                                                   \
            spdlog::error("  ✗ FAILED");                                                           \
        }                                                                                          \
    } while (0)

// Helper: Create a test display with specific resolution
static lv_display_t* create_test_display(int32_t hor_res, int32_t ver_res) {
    lv_display_t* display = lv_display_create(hor_res, ver_res);
    if (!display) {
        spdlog::error("Failed to create display: {}x{}", hor_res, ver_res);
        return nullptr;
    }

    // Allocate draw buffers
    size_t buf_size = hor_res * 10;
    lv_color_t* buf1 = (lv_color_t*)malloc(buf_size * sizeof(lv_color_t));
    lv_color_t* buf2 = (lv_color_t*)malloc(buf_size * sizeof(lv_color_t));

    if (!buf1 || !buf2) {
        spdlog::error("Failed to allocate draw buffers");
        if (buf1)
            free(buf1);
        if (buf2)
            free(buf2);
        lv_display_delete(display);
        return nullptr;
    }

    lv_display_set_buffers(display, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Log DPI for debugging
    int32_t dpi = lv_display_get_dpi(display);
    spdlog::debug("Created display {}x{} with DPI={}", hor_res, ver_res, dpi);

    return display;
}

// Helper: Create a test display with specific resolution AND custom DPI
static lv_display_t* create_test_display_with_dpi(int32_t hor_res, int32_t ver_res, int32_t dpi) {
    lv_display_t* display = create_test_display(hor_res, ver_res);
    if (display) {
        lv_display_set_dpi(display, dpi);
        spdlog::debug("Set display DPI to {}", dpi);
    }
    return display;
}

// Helper: Get widget padding value
static int32_t get_widget_pad_all(lv_obj_t* obj) {
    return lv_obj_get_style_pad_left(obj, LV_PART_MAIN);
}

//==============================================================================
// Test Suite A: LVGL Theme Breakpoint Classification
//==============================================================================

bool test_breakpoint_small_480x320() {
    lv_display_t* disp = create_test_display(480, 320);
    TEST_ASSERT(disp != nullptr, "Failed to create display");

    lv_theme_t* theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    TEST_ASSERT(theme != nullptr, "Theme init failed");

    // Note: We can't directly access disp_size (private), but we can verify padding values
    // which are derived from it. For 480x320, LVGL should use DISP_SMALL → PAD_DEF=12
    int32_t dpi = lv_display_get_dpi(disp);
    lv_obj_t* test_obj = lv_obj_create(lv_screen_active());
    int32_t pad = get_widget_pad_all(test_obj);

    spdlog::info("480x320 DPI={}, actual widget padding: {}", dpi, pad);
    TEST_ASSERT(pad == 12, ("Expected PAD_DEF=12 for SMALL screen, got " + std::to_string(pad) +
                            " (DPI=" + std::to_string(dpi) + ")")
                               .c_str());

    lv_obj_delete(test_obj);
    lv_display_delete(disp);
    return true;
}

bool test_breakpoint_small_320x480_rotated() {
    lv_display_t* disp = create_test_display(320, 480);
    TEST_ASSERT(disp != nullptr, "Failed to create display");

    lv_theme_t* theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    TEST_ASSERT(theme != nullptr, "Theme init failed");

    // max(320, 480) = 480 → DISP_SMALL → PAD_DEF=12
    lv_obj_t* test_obj = lv_obj_create(lv_screen_active());
    int32_t pad = get_widget_pad_all(test_obj);

    spdlog::debug("320x480 widget padding: {}", pad);
    TEST_ASSERT(pad == 12, "Expected PAD_DEF=12 for rotated SMALL screen");

    lv_obj_delete(test_obj);
    lv_display_delete(disp);
    return true;
}

bool test_breakpoint_medium_800x480() {
    lv_display_t* disp = create_test_display(800, 480);
    TEST_ASSERT(disp != nullptr, "Failed to create display");

    lv_theme_t* theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    TEST_ASSERT(theme != nullptr, "Theme init failed");

    // max(800, 480) = 800 → DISP_MEDIUM → PAD_DEF=16
    lv_obj_t* test_obj = lv_obj_create(lv_screen_active());
    int32_t pad = get_widget_pad_all(test_obj);

    spdlog::debug("800x480 widget padding: {}", pad);
    TEST_ASSERT(pad == 16, "Expected PAD_DEF=16 for MEDIUM screen");

    lv_obj_delete(test_obj);
    lv_display_delete(disp);
    return true;
}

bool test_breakpoint_medium_480x800_rotated() {
    lv_display_t* disp = create_test_display(480, 800);
    TEST_ASSERT(disp != nullptr, "Failed to create display");

    lv_theme_t* theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    TEST_ASSERT(theme != nullptr, "Theme init failed");

    // max(480, 800) = 800 → DISP_MEDIUM → PAD_DEF=16
    lv_obj_t* test_obj = lv_obj_create(lv_screen_active());
    int32_t pad = get_widget_pad_all(test_obj);

    spdlog::debug("480x800 widget padding: {}", pad);
    TEST_ASSERT(pad == 16, "Expected PAD_DEF=16 for rotated MEDIUM screen");

    lv_obj_delete(test_obj);
    lv_display_delete(disp);
    return true;
}

bool test_breakpoint_large_1024x600() {
    lv_display_t* disp = create_test_display(1024, 600);
    TEST_ASSERT(disp != nullptr, "Failed to create display");

    lv_theme_t* theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    TEST_ASSERT(theme != nullptr, "Theme init failed");

    // max(1024, 600) = 1024 → DISP_LARGE → PAD_DEF=20
    lv_obj_t* test_obj = lv_obj_create(lv_screen_active());
    int32_t pad = get_widget_pad_all(test_obj);

    spdlog::debug("1024x600 widget padding: {}", pad);
    TEST_ASSERT(pad == 20, "Expected PAD_DEF=20 for LARGE screen");

    lv_obj_delete(test_obj);
    lv_display_delete(disp);
    return true;
}

bool test_breakpoint_large_1280x720() {
    lv_display_t* disp = create_test_display(1280, 720);
    TEST_ASSERT(disp != nullptr, "Failed to create display");

    lv_theme_t* theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    TEST_ASSERT(theme != nullptr, "Theme init failed");

    // max(1280, 720) = 1280 → DISP_LARGE → PAD_DEF=20
    lv_obj_t* test_obj = lv_obj_create(lv_screen_active());
    int32_t pad = get_widget_pad_all(test_obj);

    spdlog::debug("1280x720 widget padding: {}", pad);
    TEST_ASSERT(pad == 20, "Expected PAD_DEF=20 for LARGE screen");

    lv_obj_delete(test_obj);
    lv_display_delete(disp);
    return true;
}

//==============================================================================
// Test Suite B: Edge Cases - Exact Boundaries
//==============================================================================

bool test_edge_case_479px() {
    lv_display_t* disp = create_test_display(479, 320);
    TEST_ASSERT(disp != nullptr, "Failed to create display");

    lv_theme_t* theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    TEST_ASSERT(theme != nullptr, "Theme init failed");

    // max(479, 320) = 479 ≤ 480 → DISP_SMALL → PAD_DEF=12
    lv_obj_t* test_obj = lv_obj_create(lv_screen_active());
    int32_t pad = get_widget_pad_all(test_obj);

    spdlog::debug("479x320 widget padding: {}", pad);
    TEST_ASSERT(pad == 12, "479px should be SMALL (≤480)");

    lv_obj_delete(test_obj);
    lv_display_delete(disp);
    return true;
}

bool test_edge_case_480px_exact() {
    lv_display_t* disp = create_test_display(480, 480);
    TEST_ASSERT(disp != nullptr, "Failed to create display");

    lv_theme_t* theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    TEST_ASSERT(theme != nullptr, "Theme init failed");

    // max(480, 480) = 480 ≤ 480 → DISP_SMALL → PAD_DEF=12
    lv_obj_t* test_obj = lv_obj_create(lv_screen_active());
    int32_t pad = get_widget_pad_all(test_obj);

    spdlog::debug("480x480 widget padding: {}", pad);
    TEST_ASSERT(pad == 12, "480px should be SMALL (≤480)");

    lv_obj_delete(test_obj);
    lv_display_delete(disp);
    return true;
}

bool test_edge_case_481px() {
    lv_display_t* disp = create_test_display(481, 320);
    TEST_ASSERT(disp != nullptr, "Failed to create display");

    lv_theme_t* theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    TEST_ASSERT(theme != nullptr, "Theme init failed");

    // max(481, 320) = 481 > 480, ≤ 800 → DISP_MEDIUM → PAD_DEF=16
    lv_obj_t* test_obj = lv_obj_create(lv_screen_active());
    int32_t pad = get_widget_pad_all(test_obj);

    spdlog::debug("481x320 widget padding: {}", pad);
    TEST_ASSERT(pad == 16, "481px should be MEDIUM (>480, ≤800)");

    lv_obj_delete(test_obj);
    lv_display_delete(disp);
    return true;
}

bool test_edge_case_800px_exact() {
    lv_display_t* disp = create_test_display(800, 800);
    TEST_ASSERT(disp != nullptr, "Failed to create display");

    lv_theme_t* theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    TEST_ASSERT(theme != nullptr, "Theme init failed");

    // max(800, 800) = 800 ≤ 800 → DISP_MEDIUM → PAD_DEF=16
    lv_obj_t* test_obj = lv_obj_create(lv_screen_active());
    int32_t pad = get_widget_pad_all(test_obj);

    spdlog::debug("800x800 widget padding: {}", pad);
    TEST_ASSERT(pad == 16, "800px should be MEDIUM (≤800)");

    lv_obj_delete(test_obj);
    lv_display_delete(disp);
    return true;
}

bool test_edge_case_801px() {
    lv_display_t* disp = create_test_display(801, 480);
    TEST_ASSERT(disp != nullptr, "Failed to create display");

    lv_theme_t* theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    TEST_ASSERT(theme != nullptr, "Theme init failed");

    // max(801, 480) = 801 > 800 → DISP_LARGE → PAD_DEF=20
    lv_obj_t* test_obj = lv_obj_create(lv_screen_active());
    int32_t pad = get_widget_pad_all(test_obj);

    spdlog::debug("801x480 widget padding: {}", pad);
    TEST_ASSERT(pad == 20, "801px should be LARGE (>800)");

    lv_obj_delete(test_obj);
    lv_display_delete(disp);
    return true;
}

//==============================================================================
// Test Suite C: Theme Toggle - Verify Breakpoints Preserved
//==============================================================================

bool test_theme_toggle_dark_light() {
    lv_display_t* disp = create_test_display(800, 480);
    TEST_ASSERT(disp != nullptr, "Failed to create display");

    // Init dark theme
    lv_theme_t* dark_theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    TEST_ASSERT(dark_theme != nullptr, "Dark theme init failed");

    lv_obj_t* test_obj = lv_obj_create(lv_screen_active());
    int32_t dark_pad = get_widget_pad_all(test_obj);
    spdlog::debug("Dark theme padding: {}", dark_pad);
    TEST_ASSERT(dark_pad == 16, "Dark theme should have PAD_DEF=16");

    // Toggle to light theme
    lv_theme_t* light_theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), false, &lv_font_montserrat_16);
    TEST_ASSERT(light_theme != nullptr, "Light theme init failed");

    // Create new widget with light theme
    lv_obj_delete(test_obj);
    test_obj = lv_obj_create(lv_screen_active());
    int32_t light_pad = get_widget_pad_all(test_obj);
    spdlog::debug("Light theme padding: {}", light_pad);
    TEST_ASSERT(light_pad == 16, "Light theme should preserve PAD_DEF=16");

    lv_obj_delete(test_obj);
    lv_display_delete(disp);
    return true;
}

//==============================================================================
// Test Suite D: DPI Scaling
//==============================================================================

// Helper: Calculate expected padding after DPI scaling
// Formula: LV_DPX_CALC(dpi, value) = (dpi * value + 80) / 160
static int32_t calc_expected_padding(int32_t dpi, int32_t base_value) {
    return (dpi * base_value + 80) / 160;
}

bool test_dpi_scaling_160_reference() {
    // Reference DPI (160) - no scaling
    lv_display_t* disp = create_test_display_with_dpi(480, 320, 160);
    TEST_ASSERT(disp != nullptr, "Failed to create display");

    lv_theme_t* theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    TEST_ASSERT(theme != nullptr, "Theme init failed");

    lv_obj_t* test_obj = lv_obj_create(lv_screen_active());
    int32_t pad = get_widget_pad_all(test_obj);

    // At DPI=160 (reference), SMALL screen should use PAD_DEF=12 with no scaling
    int32_t expected = calc_expected_padding(160, 12); // (160*12+80)/160 = 12
    spdlog::info("DPI=160 (reference): expected={}, actual={}", expected, pad);
    TEST_ASSERT(pad == expected, "160 DPI should give exact base values");

    lv_obj_delete(test_obj);
    lv_display_delete(disp);
    return true;
}

bool test_dpi_scaling_170_7inch() {
    // 7" @ 1024x600 ≈ 170 DPI
    lv_display_t* disp = create_test_display_with_dpi(1024, 600, 170);
    TEST_ASSERT(disp != nullptr, "Failed to create display");

    lv_theme_t* theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    TEST_ASSERT(theme != nullptr, "Theme init failed");

    lv_obj_t* test_obj = lv_obj_create(lv_screen_active());
    int32_t pad = get_widget_pad_all(test_obj);

    // LARGE screen (>800), PAD_DEF=20, scaled by DPI
    int32_t expected = calc_expected_padding(170, 20); // (170*20+80)/160 = 21
    spdlog::info("DPI=170 (7\" screen): expected={}, actual={}", expected, pad);
    TEST_ASSERT(pad == expected, "170 DPI should scale correctly");

    lv_obj_delete(test_obj);
    lv_display_delete(disp);
    return true;
}

bool test_dpi_scaling_187_5inch() {
    // 5" @ 800x480 ≈ 187 DPI
    lv_display_t* disp = create_test_display_with_dpi(800, 480, 187);
    TEST_ASSERT(disp != nullptr, "Failed to create display");

    lv_theme_t* theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    TEST_ASSERT(theme != nullptr, "Theme init failed");

    lv_obj_t* test_obj = lv_obj_create(lv_screen_active());
    int32_t pad = get_widget_pad_all(test_obj);

    // MEDIUM screen (>480, ≤800), PAD_DEF=16, scaled by DPI
    int32_t expected = calc_expected_padding(187, 16); // (187*16+80)/160 = 19
    spdlog::info("DPI=187 (5\" screen): expected={}, actual={}", expected, pad);
    TEST_ASSERT(pad == expected, "187 DPI should scale correctly");

    lv_obj_delete(test_obj);
    lv_display_delete(disp);
    return true;
}

bool test_dpi_scaling_201_4_3inch() {
    // 4.3" @ 720x480 (AD5M) ≈ 201 DPI
    lv_display_t* disp = create_test_display_with_dpi(720, 480, 201);
    TEST_ASSERT(disp != nullptr, "Failed to create display");

    lv_theme_t* theme =
        lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    TEST_ASSERT(theme != nullptr, "Theme init failed");

    lv_obj_t* test_obj = lv_obj_create(lv_screen_active());
    int32_t pad = get_widget_pad_all(test_obj);

    // MEDIUM screen (>480, ≤800), PAD_DEF=16, scaled by DPI
    int32_t expected = calc_expected_padding(201, 16); // (201*16+80)/160 = 20
    spdlog::info("DPI=201 (4.3\" screen): expected={}, actual={}", expected, pad);
    TEST_ASSERT(pad == expected, "201 DPI should scale correctly");

    lv_obj_delete(test_obj);
    lv_display_delete(disp);
    return true;
}

//==============================================================================
// Main Test Runner
//==============================================================================

int main(int, char**) {
    printf("HelixScreen Responsive Theme Test Suite\n");
    printf("========================================\n\n");

    // Initialize LVGL (headless mode - no SDL needed for these tests)
    lv_init();

    printf("Test Suite A: LVGL Theme Breakpoint Classification\n");
    printf("---------------------------------------------------\n");
    RUN_TEST(test_breakpoint_small_480x320);
    RUN_TEST(test_breakpoint_small_320x480_rotated);
    RUN_TEST(test_breakpoint_medium_800x480);
    RUN_TEST(test_breakpoint_medium_480x800_rotated);
    RUN_TEST(test_breakpoint_large_1024x600);
    RUN_TEST(test_breakpoint_large_1280x720);
    printf("\n");

    printf("Test Suite B: Edge Cases - Exact Boundaries\n");
    printf("--------------------------------------------\n");
    RUN_TEST(test_edge_case_479px);
    RUN_TEST(test_edge_case_480px_exact);
    RUN_TEST(test_edge_case_481px);
    RUN_TEST(test_edge_case_800px_exact);
    RUN_TEST(test_edge_case_801px);
    printf("\n");

    printf("Test Suite C: Theme Toggle\n");
    printf("--------------------------\n");
    RUN_TEST(test_theme_toggle_dark_light);
    printf("\n");

    printf("Test Suite D: DPI Scaling (Hardware Profiles)\n");
    printf("----------------------------------------------\n");
    RUN_TEST(test_dpi_scaling_160_reference);
    RUN_TEST(test_dpi_scaling_170_7inch);
    RUN_TEST(test_dpi_scaling_187_5inch);
    RUN_TEST(test_dpi_scaling_201_4_3inch);
    printf("\n");

    // Summary
    printf("========================================\n");
    printf("Test Summary:\n");
    printf("  Total:  %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("========================================\n");

    if (tests_failed == 0) {
        printf("\n✓ ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("\n✗ %d TEST(S) FAILED\n", tests_failed);
        return 1;
    }
}
