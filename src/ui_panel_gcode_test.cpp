// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 HelixScreen Contributors
 *
 * This file is part of HelixScreen, which is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * See <https://www.gnu.org/licenses/>.
 */

#include "ui_panel_gcode_test.h"

#include "ui_event_safety.h"
#include "ui_gcode_viewer.h"
#include "ui_theme.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <dirent.h>
#include <string>
#include <vector>

// Panel state
static lv_obj_t* panel_root = nullptr;
static lv_obj_t* gcode_viewer = nullptr;
static lv_obj_t* stats_label = nullptr;
static lv_obj_t* file_picker_overlay = nullptr;

// Path to default sample G-code file
static const char* TEST_GCODE_PATH = "assets/single_line_test.gcode";
static const char* ASSETS_DIR = "assets";

// Store available files
static std::vector<std::string> gcode_files;

// ==============================================
// File Browser
// ==============================================

/**
 * @brief Scan assets directory for .gcode files
 */
static void scan_gcode_files() {
    gcode_files.clear();

    DIR* dir = opendir(ASSETS_DIR);
    if (!dir) {
        spdlog::error("[GCodeTest] Failed to open assets directory");
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;

        // Check if file ends with .gcode
        if (filename.length() > 6 && filename.substr(filename.length() - 6) == ".gcode") {
            std::string full_path = std::string(ASSETS_DIR) + "/" + filename;
            gcode_files.push_back(full_path);
            spdlog::debug("[GCodeTest] Found G-code file: {}", full_path);
        }
    }
    closedir(dir);

    // Sort files alphabetically
    std::sort(gcode_files.begin(), gcode_files.end());

    spdlog::info("[GCodeTest] Found {} G-code files", gcode_files.size());
}

/**
 * @brief File list item click handler
 */
static void on_file_selected(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[GCodeTest] on_file_selected");
    uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    if (index >= gcode_files.size()) {
        spdlog::error("[GCodeTest] Invalid file index: {}", index);
        return;
    }

    const std::string& filepath = gcode_files[index];
    spdlog::info("[GCodeTest] Loading selected file: {}", filepath);

    // Load the file
    if (gcode_viewer) {
        ui_gcode_viewer_load_file(gcode_viewer, filepath.c_str());

        // Update stats
        int layer_count = ui_gcode_viewer_get_layer_count(gcode_viewer);
        gcode_viewer_state_enum_t state = ui_gcode_viewer_get_state(gcode_viewer);

        if (stats_label) {
            if (state == GCODE_VIEWER_STATE_LOADED) {
                char buf[256];
                // Extract filename from path
                size_t last_slash = filepath.find_last_of('/');
                std::string filename =
                    (last_slash != std::string::npos) ? filepath.substr(last_slash + 1) : filepath;

                snprintf(buf, sizeof(buf), "%s | %d layers", filename.c_str(), layer_count);
                lv_label_set_text(stats_label, buf);
            } else if (state == GCODE_VIEWER_STATE_ERROR) {
                lv_label_set_text(stats_label, "Error loading file");
            }
        }
    }

    // Close the file picker
    if (file_picker_overlay) {
        lv_obj_del(file_picker_overlay);
        file_picker_overlay = nullptr;
    }
    LVGL_SAFE_EVENT_CB_END();
}

/**
 * @brief Close button handler for file picker
 */
static void on_file_picker_close(lv_event_t*) {
    LVGL_SAFE_EVENT_CB_BEGIN("[GCodeTest] on_file_picker_close");
    if (file_picker_overlay) {
        lv_obj_del(file_picker_overlay);
        file_picker_overlay = nullptr;
    }
    LVGL_SAFE_EVENT_CB_END();
}

/**
 * @brief Create and show file picker overlay
 */
static void show_file_picker() {
    if (file_picker_overlay) {
        // Already open
        return;
    }

    // Scan for files
    scan_gcode_files();

    if (gcode_files.empty()) {
        spdlog::warn("[GCodeTest] No G-code files found in assets directory");
        return;
    }

    // Create full-screen overlay
    file_picker_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(file_picker_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(file_picker_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(file_picker_overlay, 200, 0); // Semi-transparent
    lv_obj_set_style_pad_all(file_picker_overlay, 40, 0);

    // Create card for file list
    lv_obj_t* card = lv_obj_create(file_picker_overlay);
    lv_obj_set_size(card, LV_PCT(80), LV_PCT(80));
    lv_obj_center(card);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_gap(card, 12, 0);

    // Header
    lv_obj_t* header = lv_label_create(card);
    lv_label_set_text(header, "Select G-Code File");

    // File list container
    lv_obj_t* list_container = lv_obj_create(card);
    lv_obj_set_width(list_container, LV_PCT(100));
    lv_obj_set_flex_grow(list_container, 1);
    lv_obj_set_flex_flow(list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list_container, 8, 0);
    lv_obj_set_style_pad_gap(list_container, 8, 0);
    lv_obj_set_scroll_dir(list_container, LV_DIR_VER);

    // Add file buttons
    for (size_t i = 0; i < gcode_files.size(); i++) {
        // Extract filename from path
        size_t last_slash = gcode_files[i].find_last_of('/');
        std::string filename = (last_slash != std::string::npos)
                                   ? gcode_files[i].substr(last_slash + 1)
                                   : gcode_files[i];

        lv_obj_t* btn = lv_button_create(list_container);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 50);
        lv_obj_add_event_cb(btn, on_file_selected, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, filename.c_str());
        lv_obj_center(label);
    }

    // Close button
    lv_obj_t* close_btn = lv_button_create(card);
    lv_obj_set_width(close_btn, LV_PCT(100));
    lv_obj_set_height(close_btn, 50);
    lv_obj_add_event_cb(close_btn, on_file_picker_close, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "Cancel");
    lv_obj_center(close_label);
}

// ==============================================
// Event Callbacks
// ==============================================

/**
 * @brief View preset button click handler
 */
static void on_view_preset_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[GCodeTest] on_view_preset_clicked");
    lv_obj_t* btn = lv_event_get_target_obj(e);
    const char* name = lv_obj_get_name(btn);

    if (!gcode_viewer || !name) {
        LVGL_SAFE_EVENT_CB_END();
        return;
    }

    spdlog::info("[GCodeTest] View preset clicked: {}", name);

    if (strcmp(name, "btn_travels") == 0) {
        // Toggle travel moves visibility
        bool is_checked = lv_obj_has_state(btn, LV_STATE_CHECKED);
        ui_gcode_viewer_set_show_travels(gcode_viewer, is_checked);
        spdlog::info("[GCodeTest] Travel moves: {}", is_checked ? "shown" : "hidden");
    } else if (strcmp(name, "btn_top") == 0) {
        ui_gcode_viewer_set_view(gcode_viewer, GCODE_VIEWER_VIEW_TOP);
    } else if (strcmp(name, "btn_front") == 0) {
        ui_gcode_viewer_set_view(gcode_viewer, GCODE_VIEWER_VIEW_FRONT);
    } else if (strcmp(name, "btn_side") == 0) {
        ui_gcode_viewer_set_view(gcode_viewer, GCODE_VIEWER_VIEW_SIDE);
    } else if (strcmp(name, "btn_reset") == 0) {
        ui_gcode_viewer_reset_camera(gcode_viewer);
    }
    LVGL_SAFE_EVENT_CB_END();
}

/**
 * @brief Zoom button click handler
 */
static void on_zoom_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[GCodeTest] on_zoom_clicked");
    lv_obj_t* btn = lv_event_get_target_obj(e);
    const char* name = lv_obj_get_name(btn);

    if (!gcode_viewer || !name) {
        LVGL_SAFE_EVENT_CB_END();
        return;
    }

    float zoom_step = 1.2f; // 20% zoom per click

    if (strcmp(name, "btn_zoom_in") == 0) {
        ui_gcode_viewer_zoom(gcode_viewer, zoom_step);
        spdlog::debug("[GCodeTest] Zoom in clicked");
    } else if (strcmp(name, "btn_zoom_out") == 0) {
        ui_gcode_viewer_zoom(gcode_viewer, 1.0f / zoom_step);
        spdlog::debug("[GCodeTest] Zoom out clicked");
    }
    LVGL_SAFE_EVENT_CB_END();
}

/**
 * @brief Load file button click handler - shows file picker
 */
static void on_load_test_file(lv_event_t*) {
    LVGL_SAFE_EVENT_CB_BEGIN("[GCodeTest] on_load_test_file");
    show_file_picker();
    LVGL_SAFE_EVENT_CB_END();
}

/**
 * @brief Clear button click handler
 */
static void on_clear(lv_event_t*) {
    LVGL_SAFE_EVENT_CB_BEGIN("[GCodeTest] on_clear");
    if (!gcode_viewer) {
        LVGL_SAFE_EVENT_CB_END();
        return;
    }

    spdlog::info("[GCodeTest] Clearing viewer");
    ui_gcode_viewer_clear(gcode_viewer);

    if (stats_label) {
        lv_label_set_text(stats_label, "No file loaded");
    }
    LVGL_SAFE_EVENT_CB_END();
}

/**
 * @brief Specular intensity slider callback
 */
static void on_specular_intensity_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[GCodeTest] on_specular_intensity_changed");
    if (!gcode_viewer) {
        LVGL_SAFE_EVENT_CB_END();
        return;
    }

    lv_obj_t* slider = lv_event_get_target_obj(e);
    int32_t value = lv_slider_get_value(slider);
    float intensity = value / 100.0f; // 0-20 → 0.0-0.2

    // Update value label
    lv_obj_t* container = lv_obj_get_parent(slider);
    lv_obj_t* label = lv_obj_find_by_name(container, "specular_value_label");
    if (label) {
        lv_label_set_text_fmt(label, "%.2f", intensity);
    }

    // Get current shininess value
    lv_obj_t* shininess_slider = lv_obj_find_by_name(panel_root, "shininess_slider");
    float shininess = 15.0f;
    if (shininess_slider) {
        shininess = (float)lv_slider_get_value(shininess_slider);
    }

    // Update TinyGL material
    ui_gcode_viewer_set_specular(gcode_viewer, intensity, shininess);
    LVGL_SAFE_EVENT_CB_END();
}

/**
 * @brief Shininess slider callback
 */
static void on_shininess_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[GCodeTest] on_shininess_changed");
    if (!gcode_viewer) {
        LVGL_SAFE_EVENT_CB_END();
        return;
    }

    lv_obj_t* slider = lv_event_get_target_obj(e);
    int32_t value = lv_slider_get_value(slider);

    // Update value label
    lv_obj_t* container = lv_obj_get_parent(slider);
    lv_obj_t* label = lv_obj_find_by_name(container, "shininess_value_label");
    if (label) {
        lv_label_set_text_fmt(label, "%d", (int)value);
    }

    // Get current specular intensity value
    lv_obj_t* intensity_slider = lv_obj_find_by_name(panel_root, "specular_slider");
    float intensity = 0.05f;
    if (intensity_slider) {
        intensity = lv_slider_get_value(intensity_slider) / 100.0f;
    }

    // Update TinyGL material
    ui_gcode_viewer_set_specular(gcode_viewer, intensity, (float)value);
    LVGL_SAFE_EVENT_CB_END();
}

// ==============================================
// Public API
// ==============================================

lv_obj_t* ui_panel_gcode_test_create(lv_obj_t* parent) {
    // Load XML component (use registered component name, not file path)
    panel_root = (lv_obj_t*)lv_xml_create(parent, "gcode_test_panel", nullptr);
    if (!panel_root) {
        spdlog::error("[GCodeTest] Failed to load XML component");
        return nullptr;
    }

    // Get widget references
    gcode_viewer = lv_obj_find_by_name(panel_root, "gcode_viewer");
    stats_label = lv_obj_find_by_name(panel_root, "stats_label");

    if (!gcode_viewer) {
        spdlog::error("[GCodeTest] Failed to find gcode_viewer widget");
        return panel_root;
    }

    // Register event callbacks
    lv_obj_t* btn_isometric = lv_obj_find_by_name(panel_root, "btn_isometric");
    lv_obj_t* btn_top = lv_obj_find_by_name(panel_root, "btn_top");
    lv_obj_t* btn_front = lv_obj_find_by_name(panel_root, "btn_front");
    lv_obj_t* btn_side = lv_obj_find_by_name(panel_root, "btn_side");
    lv_obj_t* btn_reset = lv_obj_find_by_name(panel_root, "btn_reset");
    lv_obj_t* btn_zoom_in = lv_obj_find_by_name(panel_root, "btn_zoom_in");
    lv_obj_t* btn_zoom_out = lv_obj_find_by_name(panel_root, "btn_zoom_out");
    lv_obj_t* btn_load = lv_obj_find_by_name(panel_root, "btn_load_test");
    lv_obj_t* btn_clear = lv_obj_find_by_name(panel_root, "btn_clear");

    // Find sliders
    lv_obj_t* specular_slider = lv_obj_find_by_name(panel_root, "specular_slider");
    lv_obj_t* shininess_slider = lv_obj_find_by_name(panel_root, "shininess_slider");

    if (btn_isometric)
        lv_obj_add_event_cb(btn_isometric, on_view_preset_clicked, LV_EVENT_CLICKED, nullptr);
    if (btn_top)
        lv_obj_add_event_cb(btn_top, on_view_preset_clicked, LV_EVENT_CLICKED, nullptr);
    if (btn_front)
        lv_obj_add_event_cb(btn_front, on_view_preset_clicked, LV_EVENT_CLICKED, nullptr);
    if (btn_side)
        lv_obj_add_event_cb(btn_side, on_view_preset_clicked, LV_EVENT_CLICKED, nullptr);
    if (btn_reset)
        lv_obj_add_event_cb(btn_reset, on_view_preset_clicked, LV_EVENT_CLICKED, nullptr);
    if (btn_zoom_in)
        lv_obj_add_event_cb(btn_zoom_in, on_zoom_clicked, LV_EVENT_CLICKED, nullptr);
    if (btn_zoom_out)
        lv_obj_add_event_cb(btn_zoom_out, on_zoom_clicked, LV_EVENT_CLICKED, nullptr);
    if (btn_load)
        lv_obj_add_event_cb(btn_load, on_load_test_file, LV_EVENT_CLICKED, nullptr);
    if (btn_clear)
        lv_obj_add_event_cb(btn_clear, on_clear, LV_EVENT_CLICKED, nullptr);

    // Register slider callbacks
    if (specular_slider)
        lv_obj_add_event_cb(specular_slider, on_specular_intensity_changed, LV_EVENT_VALUE_CHANGED,
                            nullptr);
    if (shininess_slider)
        lv_obj_add_event_cb(shininess_slider, on_shininess_changed, LV_EVENT_VALUE_CHANGED,
                            nullptr);

    // Auto-load the default sample file
    spdlog::info("[GCodeTest] Auto-loading sample file: {}", TEST_GCODE_PATH);
    ui_gcode_viewer_load_file(gcode_viewer, TEST_GCODE_PATH);

    // Update stats after loading
    int layer_count = ui_gcode_viewer_get_layer_count(gcode_viewer);
    gcode_viewer_state_enum_t state = ui_gcode_viewer_get_state(gcode_viewer);

    if (stats_label) {
        if (state == GCODE_VIEWER_STATE_LOADED) {
            // Extract just the filename from the path
            const char* filename = strrchr(TEST_GCODE_PATH, '/');
            filename = filename ? filename + 1 : TEST_GCODE_PATH;
            char buf[256];
            snprintf(buf, sizeof(buf), "%s | %d layers", filename, layer_count);
            lv_label_set_text(stats_label, buf);
        } else if (state == GCODE_VIEWER_STATE_ERROR) {
            lv_label_set_text(stats_label, "Error loading file");
        } else {
            lv_label_set_text(stats_label, "Loading...");
        }
    }

    spdlog::info("[GCodeTest] Panel created");
    return panel_root;
}

void ui_panel_gcode_test_cleanup(void) {
    // Clean up file picker if open
    if (file_picker_overlay) {
        lv_obj_del(file_picker_overlay);
        file_picker_overlay = nullptr;
    }

    // Widgets are automatically cleaned up by LVGL when panel_root is deleted
    panel_root = nullptr;
    gcode_viewer = nullptr;
    stats_label = nullptr;

    spdlog::debug("[GCodeTest] Panel cleaned up");
}
