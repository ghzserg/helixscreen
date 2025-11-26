// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_test.h"

#include "app_globals.h"
#include "printer_state.h"
#include "ui_keyboard.h"
#include "ui_theme.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <memory>

// ============================================================================
// CONSTRUCTOR
// ============================================================================

TestPanel::TestPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // TestPanel doesn't use PrinterState or MoonrakerAPI, but we accept
    // them for interface consistency with other panels
}

// ============================================================================
// PANELBASE IMPLEMENTATION
// ============================================================================

void TestPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // TestPanel has no subjects to initialize
    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized (none required)", get_name());
}

void TestPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    // Populate diagnostic labels
    populate_labels();

    // Register keyboard for textarea
    lv_obj_t* keyboard_textarea = lv_obj_find_by_name(panel_, "keyboard_test_textarea");
    if (keyboard_textarea) {
        ui_keyboard_register_textarea(keyboard_textarea);
        spdlog::info("[{}] Registered keyboard for textarea", get_name());
    }
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void TestPanel::populate_labels() {
    // Get screen dimensions using custom breakpoints optimized for our hardware
    lv_display_t* display = lv_display_get_default();
    int32_t hor_res = lv_display_get_horizontal_resolution(display);
    int32_t ver_res = lv_display_get_vertical_resolution(display);
    int32_t greater_res = LV_MAX(hor_res, ver_res);

    // Determine screen size category
    const char* size_category;
    int switch_width, switch_height;
    int row_height;

    if (greater_res <= UI_BREAKPOINT_SMALL_MAX) { // ≤480: 480x320
        size_category = "SMALL";
        switch_width = 36;
        switch_height = 18;
        row_height = 26;
    } else if (greater_res <= UI_BREAKPOINT_MEDIUM_MAX) { // 481-800: 800x480
        size_category = "MEDIUM";
        switch_width = 64;
        switch_height = 32;
        row_height = 40;
    } else { // >800: 1024x600+
        size_category = "LARGE";
        switch_width = 88;
        switch_height = 44;
        row_height = 56;
    }

    // Find info labels
    lv_obj_t* screen_size_label = lv_obj_find_by_name(panel_, "screen_size_label");
    lv_obj_t* switch_size_label = lv_obj_find_by_name(panel_, "switch_size_label");
    lv_obj_t* row_height_label = lv_obj_find_by_name(panel_, "row_height_label");

    // Populate labels
    char buffer[128];

    if (screen_size_label) {
        snprintf(buffer, sizeof(buffer), "Screen Size: %s (%dx%d, max=%d)",
                 size_category, hor_res, ver_res, greater_res);
        lv_label_set_text(screen_size_label, buffer);
    }

    if (switch_size_label) {
        snprintf(buffer, sizeof(buffer), "Switch Size: %dx%dpx (knob padding varies)",
                 switch_width, switch_height);
        lv_label_set_text(switch_size_label, buffer);
    }

    if (row_height_label) {
        snprintf(buffer, sizeof(buffer), "Row Height: %dpx (fits switch + padding)",
                 row_height);
        lv_label_set_text(row_height_label, buffer);
    }

    spdlog::info("[{}] Setup complete: {} ({}x{}, max={}), switch={}x{}, row={}px",
                 get_name(), size_category, hor_res, ver_res, greater_res,
                 switch_width, switch_height, row_height);
}

// ============================================================================
// GLOBAL INSTANCE (needed by main.cpp)
// ============================================================================

static std::unique_ptr<TestPanel> g_test_panel;

TestPanel& get_global_test_panel() {
    if (!g_test_panel) {
        g_test_panel = std::make_unique<TestPanel>(get_printer_state(), nullptr);
    }
    return *g_test_panel;
}
