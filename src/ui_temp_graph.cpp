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

#include "ui_temp_graph.h"

#include "ui_theme.h"

#include <spdlog/spdlog.h>

#include <memory>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Helper: Find series metadata by ID
static ui_temp_series_meta_t* find_series(ui_temp_graph_t* graph, int series_id) {
    if (!graph || series_id < 0 || series_id >= UI_TEMP_GRAPH_MAX_SERIES) {
        return nullptr;
    }

    for (int i = 0; i < graph->series_count; i++) {
        if (graph->series_meta[i].id == series_id &&
            graph->series_meta[i].chart_series != nullptr) {
            return &graph->series_meta[i];
        }
    }
    return nullptr;
}

// Helper: Create a muted (reduced opacity) version of a color
// Since LVGL chart cursors don't support opacity, we blend toward the background
static lv_color_t mute_color(lv_color_t color, lv_opa_t opa) {
    // Blend toward dark gray (chart background) based on opacity
    // opa=255 = full color, opa=0 = full background
    lv_color_t bg = lv_color_hex(0x2D2D2D); // Dark chart background
    uint8_t r = (color.red * opa + bg.red * (255 - opa)) / 255;
    uint8_t g = (color.green * opa + bg.green * (255 - opa)) / 255;
    uint8_t b = (color.blue * opa + bg.blue * (255 - opa)) / 255;
    return lv_color_make(r, g, b);
}

// Helper: Convert temperature value to pixel Y coordinate
// LVGL chart cursor position is relative to object's top-left corner,
// but data is plotted in the content area (after padding).
// So we need to add padding offset to match where data points are drawn.
static int32_t temp_to_pixel_y(ui_temp_graph_t* graph, float temp) {
    int32_t chart_height = lv_obj_get_content_height(graph->chart);
    if (chart_height <= 0) {
        return 0; // Chart not laid out yet
    }

    // Get padding offset (cursor coords are relative to object, not content area)
    int32_t pad_top = lv_obj_get_style_pad_top(graph->chart, LV_PART_MAIN);
    int32_t border_w = lv_obj_get_style_border_width(graph->chart, LV_PART_MAIN);
    int32_t y_ofs = pad_top + border_w;

    // Map temperature to pixel position within content area (inverted for Y axis)
    // lv_map(value, in_min, in_max, out_min, out_max)
    int32_t content_y = chart_height - lv_map((int32_t)temp, (int32_t)graph->min_temp,
                                              (int32_t)graph->max_temp, 0, chart_height);

    // Add offset to convert from content-relative to object-relative coordinates
    return content_y + y_ofs;
}

// Helper: Update all cursor positions (called on resize)
static void update_all_cursor_positions(ui_temp_graph_t* graph) {
    if (!graph)
        return;

    for (int i = 0; i < UI_TEMP_GRAPH_MAX_SERIES; i++) {
        ui_temp_series_meta_t* meta = &graph->series_meta[i];
        if (meta->chart_series && meta->target_cursor && meta->show_target) {
            int32_t pixel_y = temp_to_pixel_y(graph, meta->target_temp);
            lv_chart_set_cursor_pos_y(graph->chart, meta->target_cursor, pixel_y);
        }
    }
}

// Event callback: Recalculate cursor positions when chart is resized
static void chart_resize_cb(lv_event_t* e) {
    lv_obj_t* chart = lv_event_get_target_obj(e);
    ui_temp_graph_t* graph = static_cast<ui_temp_graph_t*>(lv_obj_get_user_data(chart));
    if (graph) {
        update_all_cursor_positions(graph);
    }
}

// Helper: Find series metadata by color (for draw task matching)
static ui_temp_series_meta_t* find_series_by_color(ui_temp_graph_t* graph, lv_color_t color) {
    if (!graph)
        return nullptr;

    for (int i = 0; i < graph->series_count; i++) {
        if (graph->series_meta[i].chart_series &&
            lv_color_to_u32(graph->series_meta[i].color) == lv_color_to_u32(color)) {
            return &graph->series_meta[i];
        }
    }
    return nullptr;
}

// LVGL 9 draw task callback for gradient fills under chart lines
// Called for each draw task when LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS is set
static void draw_task_cb(lv_event_t* e) {
    lv_draw_task_t* draw_task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t* base_dsc =
        static_cast<lv_draw_dsc_base_t*>(lv_draw_task_get_draw_dsc(draw_task));

    // Only process line draws for chart series (LV_PART_ITEMS)
    if (base_dsc->part != LV_PART_ITEMS ||
        lv_draw_task_get_type(draw_task) != LV_DRAW_TASK_TYPE_LINE) {
        return;
    }

    lv_obj_t* chart = lv_event_get_target_obj(e);
    ui_temp_graph_t* graph = static_cast<ui_temp_graph_t*>(lv_event_get_user_data(e));
    if (!graph)
        return;

    // Get chart coordinates for bottom reference
    lv_area_t coords;
    lv_obj_get_coords(chart, &coords);

    // Get line draw descriptor with endpoint coordinates
    lv_draw_line_dsc_t* line_dsc =
        static_cast<lv_draw_line_dsc_t*>(lv_draw_task_get_draw_dsc(draw_task));

    // Find the series this line belongs to (match by color)
    ui_temp_series_meta_t* meta = find_series_by_color(graph, line_dsc->color);
    lv_opa_t top_opa = meta ? meta->gradient_top_opa : UI_TEMP_GRAPH_GRADIENT_TOP_OPA;
    lv_opa_t bottom_opa = meta ? meta->gradient_bottom_opa : UI_TEMP_GRAPH_GRADIENT_BOTTOM_OPA;
    lv_color_t ser_color = line_dsc->color;

    // Calculate opacity fractions based on Y position within chart
    // Higher Y = lower on screen = closer to bottom = more transparent
    int32_t full_h = lv_obj_get_height(chart);
    int32_t fract_upper =
        (int32_t)(LV_MIN(line_dsc->p1.y, line_dsc->p2.y) - coords.y1) * 255 / full_h;
    int32_t fract_lower =
        (int32_t)(LV_MAX(line_dsc->p1.y, line_dsc->p2.y) - coords.y1) * 255 / full_h;

    // Calculate interpolated opacity at each point (top_opa at top, bottom_opa at bottom)
    lv_opa_t opa_upper =
        static_cast<lv_opa_t>(top_opa - (top_opa - bottom_opa) * fract_upper / 255);
    lv_opa_t opa_lower =
        static_cast<lv_opa_t>(top_opa - (top_opa - bottom_opa) * fract_lower / 255);

    // Draw triangle from line segment down to the lower of the two points
    // This fills the gap between the line and a horizontal at the lower point
    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);
    tri_dsc.p[0].x = line_dsc->p1.x;
    tri_dsc.p[0].y = line_dsc->p1.y;
    tri_dsc.p[1].x = line_dsc->p2.x;
    tri_dsc.p[1].y = line_dsc->p2.y;
    // Third point: at the x of the higher point, at the y of the lower point
    tri_dsc.p[2].x = line_dsc->p1.y < line_dsc->p2.y ? line_dsc->p1.x : line_dsc->p2.x;
    tri_dsc.p[2].y = LV_MAX(line_dsc->p1.y, line_dsc->p2.y);

    tri_dsc.grad.dir = LV_GRAD_DIR_VER;
    tri_dsc.grad.stops[0].color = ser_color;
    tri_dsc.grad.stops[0].opa = opa_upper;
    tri_dsc.grad.stops[0].frac = 0;
    tri_dsc.grad.stops[1].color = ser_color;
    tri_dsc.grad.stops[1].opa = opa_lower;
    tri_dsc.grad.stops[1].frac = 255;

    lv_draw_triangle(base_dsc->layer, &tri_dsc);

    // Draw rectangle from the lower line point down to chart bottom
    // This completes the gradient fill to the bottom of the chart
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_grad.dir = LV_GRAD_DIR_VER;
    rect_dsc.bg_grad.stops[0].color = ser_color;
    rect_dsc.bg_grad.stops[0].opa = opa_lower;
    rect_dsc.bg_grad.stops[0].frac = 0;
    rect_dsc.bg_grad.stops[1].color = ser_color;
    rect_dsc.bg_grad.stops[1].opa = bottom_opa;
    rect_dsc.bg_grad.stops[1].frac = 255;

    lv_area_t rect_area;
    rect_area.x1 = static_cast<int32_t>(line_dsc->p1.x);
    rect_area.x2 = static_cast<int32_t>(line_dsc->p2.x) - 1;
    rect_area.y1 = static_cast<int32_t>(LV_MAX(line_dsc->p1.y, line_dsc->p2.y));
    rect_area.y2 = static_cast<int32_t>(coords.y2);

    lv_draw_rect(base_dsc->layer, &rect_dsc, &rect_area);
}

// Create temperature graph widget
ui_temp_graph_t* ui_temp_graph_create(lv_obj_t* parent) {
    if (!parent) {
        spdlog::error("[TempGraph] NULL parent");
        return nullptr;
    }

    // Allocate graph structure using RAII
    auto graph_ptr = std::make_unique<ui_temp_graph_t>();
    if (!graph_ptr) {
        spdlog::error("[TempGraph] Failed to allocate graph structure");
        return nullptr;
    }

    ui_temp_graph_t* graph = graph_ptr.get();
    memset(graph, 0, sizeof(ui_temp_graph_t));

    // Initialize defaults
    graph->point_count = UI_TEMP_GRAPH_DEFAULT_POINTS;
    graph->min_temp = UI_TEMP_GRAPH_DEFAULT_MIN_TEMP;
    graph->max_temp = UI_TEMP_GRAPH_DEFAULT_MAX_TEMP;
    graph->series_count = 0;
    graph->next_series_id = 0;

    // Create LVGL chart
    graph->chart = lv_chart_create(parent);
    if (!graph->chart) {
        spdlog::error("[TempGraph] Failed to create chart widget");
        return nullptr; // graph_ptr auto-freed
    }

    // Configure chart
    lv_chart_set_type(graph->chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(graph->chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(graph->chart, static_cast<uint32_t>(graph->point_count));

    // Set Y-axis range
    lv_chart_set_axis_range(graph->chart, LV_CHART_AXIS_PRIMARY_Y,
                            static_cast<int32_t>(graph->min_temp),
                            static_cast<int32_t>(graph->max_temp));

    // Style chart background (theme handles colors)
    lv_obj_set_style_bg_opa(graph->chart, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(graph->chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(graph->chart, 12, LV_PART_MAIN);

    // Style division lines (theme handles colors)
    lv_obj_set_style_line_width(graph->chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_opa(graph->chart, LV_OPA_30, LV_PART_MAIN); // Subtle - 30% opacity

    // Style data series lines
    lv_obj_set_style_line_width(graph->chart, 2, LV_PART_ITEMS);          // Series line thickness
    lv_obj_set_style_line_opa(graph->chart, LV_OPA_COVER, LV_PART_ITEMS); // Full opacity for series

    // Hide point indicators (circles at each data point)
    lv_obj_set_style_width(graph->chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(graph->chart, 0, LV_PART_INDICATOR);

    // Configure division line count
    lv_chart_set_div_line_count(graph->chart, 5, 10); // 5 horizontal, 10 vertical division lines

    // Enable LVGL 9 draw task events for gradient fills under chart lines
    lv_obj_add_flag(graph->chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(graph->chart, draw_task_cb, LV_EVENT_DRAW_TASK_ADDED, graph);

    // Store graph pointer in chart user data for retrieval
    lv_obj_set_user_data(graph->chart, graph);

    // Register resize callback to recalculate value-based cursor positions
    lv_obj_add_event_cb(graph->chart, chart_resize_cb, LV_EVENT_SIZE_CHANGED, nullptr);

    spdlog::info("[TempGraph] Created: {} points, {:.0f}-{:.0f}°C range", graph->point_count,
                 graph->min_temp, graph->max_temp);

    // Transfer ownership to caller
    return graph_ptr.release();
}

// Destroy temperature graph widget
void ui_temp_graph_destroy(ui_temp_graph_t* graph) {
    if (!graph)
        return;

    // Transfer ownership to RAII wrapper - automatic cleanup
    std::unique_ptr<ui_temp_graph_t> graph_ptr(graph);

    // Remove all series (cursors will be cleaned up automatically)
    for (int i = 0; i < graph_ptr->series_count; i++) {
        if (graph_ptr->series_meta[i].chart_series) {
            lv_chart_remove_series(graph_ptr->chart, graph_ptr->series_meta[i].chart_series);
        }
    }

    // Delete chart widget
    if (graph_ptr->chart) {
        lv_obj_del(graph_ptr->chart);
    }

    // graph_ptr automatically freed via ~unique_ptr()
    spdlog::debug("[TempGraph] Destroyed");
}

// Get underlying chart widget
lv_obj_t* ui_temp_graph_get_chart(ui_temp_graph_t* graph) {
    return graph ? graph->chart : nullptr;
}

// Add a new temperature series
int ui_temp_graph_add_series(ui_temp_graph_t* graph, const char* name, lv_color_t color) {
    if (!graph || !name) {
        spdlog::error("[TempGraph] NULL graph or name");
        return -1;
    }

    if (graph->series_count >= UI_TEMP_GRAPH_MAX_SERIES) {
        spdlog::error("[TempGraph] Maximum series count ({}) reached", UI_TEMP_GRAPH_MAX_SERIES);
        return -1;
    }

    // Find next available slot
    int slot = -1;
    for (int i = 0; i < UI_TEMP_GRAPH_MAX_SERIES; i++) {
        if (graph->series_meta[i].chart_series == nullptr) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        spdlog::error("[TempGraph] No available series slots");
        return -1;
    }

    // Create LVGL chart series
    lv_chart_series_t* ser = lv_chart_add_series(graph->chart, color, LV_CHART_AXIS_PRIMARY_Y);
    if (!ser) {
        spdlog::error("[TempGraph] Failed to create chart series");
        return -1;
    }

    // Initialize series metadata
    ui_temp_series_meta_t* meta = &graph->series_meta[slot];
    meta->id = graph->next_series_id++;
    meta->chart_series = ser;
    meta->color = color;
    strncpy(meta->name, name, sizeof(meta->name) - 1);
    meta->name[sizeof(meta->name) - 1] = '\0';
    meta->visible = true;
    meta->show_target = false;
    meta->target_temp = 0.0f;
    meta->gradient_bottom_opa = UI_TEMP_GRAPH_GRADIENT_BOTTOM_OPA;
    meta->gradient_top_opa = UI_TEMP_GRAPH_GRADIENT_TOP_OPA;

    // Create target temperature cursor (horizontal line, initially hidden)
    // Note: We don't use lv_chart_set_cursor_point because that binds the cursor
    // to a data point which scrolls. Instead we use lv_chart_set_cursor_pos for
    // a fixed Y position representing the target temperature.
    // Use muted color (50% opacity) so target line is visually distinct from actual data
    lv_color_t cursor_color = mute_color(color, LV_OPA_20); // Very subtle target line
    meta->target_cursor = lv_chart_add_cursor(graph->chart, cursor_color, LV_DIR_HOR);

    graph->series_count++;

    spdlog::debug("[TempGraph] Added series {} '{}' (slot {}, color 0x{:06X})", meta->id,
                  meta->name, slot, lv_color_to_u32(color));

    return meta->id;
}

// Remove a temperature series
void ui_temp_graph_remove_series(ui_temp_graph_t* graph, int series_id) {
    ui_temp_series_meta_t* meta = find_series(graph, series_id);
    if (!meta) {
        spdlog::error("[TempGraph] Series {} not found", series_id);
        return;
    }

    // Remove cursor (if exists)
    if (meta->target_cursor) {
        // LVGL doesn't have lv_chart_remove_cursor, cursor is auto-freed with chart
        meta->target_cursor = nullptr;
    }

    // Remove chart series
    lv_chart_remove_series(graph->chart, meta->chart_series);

    // Clear metadata
    memset(meta, 0, sizeof(ui_temp_series_meta_t));
    meta->chart_series = nullptr;

    graph->series_count--;

    spdlog::debug("[TempGraph] Removed series {} ({} series remaining)", series_id,
                  graph->series_count);
}

// Show or hide a series
void ui_temp_graph_show_series(ui_temp_graph_t* graph, int series_id, bool visible) {
    ui_temp_series_meta_t* meta = find_series(graph, series_id);
    if (!meta) {
        spdlog::error("[TempGraph] Series {} not found", series_id);
        return;
    }

    meta->visible = visible;

    // Use LVGL's public API to hide/show series
    lv_chart_hide_series(graph->chart, meta->chart_series, !visible);

    lv_obj_invalidate(graph->chart);
    spdlog::debug("[TempGraph] Series {} '{}' {}", series_id, meta->name,
                  visible ? "shown" : "hidden");
}

// Add a single temperature point (push mode)
void ui_temp_graph_update_series(ui_temp_graph_t* graph, int series_id, float temp) {
    ui_temp_series_meta_t* meta = find_series(graph, series_id);
    if (!meta) {
        spdlog::error("[TempGraph] Series {} not found", series_id);
        return;
    }

    // Debug: Log first 10 values pushed to help track down "first value is 0" bug
    static int debug_count = 0;
    if (debug_count < 10) {
        spdlog::info("[TempGraph] DEBUG: Pushing value #{} to series '{}': {:.1f}°C", debug_count,
                     meta->name, temp);
        debug_count++;
    }

    // Add point to series (shifts old data left)
    lv_chart_set_next_value(graph->chart, meta->chart_series, (int32_t)temp);
}

// Replace all data points (array mode)
void ui_temp_graph_set_series_data(ui_temp_graph_t* graph, int series_id, const float* temps,
                                   int count) {
    ui_temp_series_meta_t* meta = find_series(graph, series_id);
    if (!meta || !temps || count <= 0) {
        spdlog::error("[TempGraph] Invalid parameters");
        return;
    }

    // Clear existing data using public API
    lv_chart_set_all_values(graph->chart, meta->chart_series, LV_CHART_POINT_NONE);

    // Convert float array to int32_t array for LVGL API (using RAII)
    int points_to_copy = count > graph->point_count ? graph->point_count : count;
    auto values = std::make_unique<int32_t[]>(static_cast<size_t>(points_to_copy));
    if (!values) {
        spdlog::error("[TempGraph] Failed to allocate conversion buffer");
        return;
    }

    for (size_t i = 0; i < static_cast<size_t>(points_to_copy); i++) {
        values[i] = static_cast<int32_t>(temps[i]);
    }

    // Set data using public API
    lv_chart_set_series_values(graph->chart, meta->chart_series, values.get(),
                               static_cast<size_t>(points_to_copy));

    // values automatically freed via ~unique_ptr()

    lv_chart_refresh(graph->chart);
    spdlog::debug("[TempGraph] Series {} '{}' data set ({} points)", series_id, meta->name,
                  points_to_copy);
}

// Clear all data
void ui_temp_graph_clear(ui_temp_graph_t* graph) {
    if (!graph)
        return;

    for (int i = 0; i < graph->series_count; i++) {
        ui_temp_series_meta_t* meta = &graph->series_meta[i];
        if (meta->chart_series) {
            lv_chart_set_all_values(graph->chart, meta->chart_series, LV_CHART_POINT_NONE);
        }
    }

    lv_chart_refresh(graph->chart);
    spdlog::debug("[TempGraph] All data cleared");
}

// Clear data for a specific series
void ui_temp_graph_clear_series(ui_temp_graph_t* graph, int series_id) {
    ui_temp_series_meta_t* meta = find_series(graph, series_id);
    if (!meta) {
        spdlog::error("[TempGraph] Series {} not found", series_id);
        return;
    }

    lv_chart_set_all_values(graph->chart, meta->chart_series, LV_CHART_POINT_NONE);

    lv_chart_refresh(graph->chart);
    spdlog::debug("[TempGraph] Series {} '{}' cleared", series_id, meta->name);
}

// Set target temperature and visibility
void ui_temp_graph_set_series_target(ui_temp_graph_t* graph, int series_id, float target,
                                     bool show) {
    ui_temp_series_meta_t* meta = find_series(graph, series_id);
    if (!meta) {
        spdlog::error("[TempGraph] Series {} not found", series_id);
        return;
    }

    // Store the value (used for recalculation on resize)
    meta->target_temp = target;
    meta->show_target = show;

    if (meta->target_cursor && show) {
        // Convert temperature value to pixel coordinate
        // This abstraction allows callers to work with temperatures, not pixels
        lv_obj_update_layout(graph->chart); // Ensure dimensions are current
        int32_t pixel_y = temp_to_pixel_y(graph, target);
        lv_chart_set_cursor_pos_y(graph->chart, meta->target_cursor, pixel_y);

        lv_obj_invalidate(graph->chart);
    }

    spdlog::debug("[TempGraph] Series {} target: {:.1f}°C ({})", series_id, target,
                  show ? "shown" : "hidden");
}

// Show or hide target temperature line
void ui_temp_graph_show_target(ui_temp_graph_t* graph, int series_id, bool show) {
    ui_temp_series_meta_t* meta = find_series(graph, series_id);
    if (!meta) {
        spdlog::error("[TempGraph] Series {} not found", series_id);
        return;
    }

    ui_temp_graph_set_series_target(graph, series_id, meta->target_temp, show);
}

// Set Y-axis temperature range
void ui_temp_graph_set_temp_range(ui_temp_graph_t* graph, float min, float max) {
    if (!graph || min >= max) {
        spdlog::error("[TempGraph] Invalid temperature range");
        return;
    }

    graph->min_temp = min;
    graph->max_temp = max;

    lv_chart_set_axis_range(graph->chart, LV_CHART_AXIS_PRIMARY_Y, static_cast<int32_t>(min),
                            static_cast<int32_t>(max));

    // Recalculate all cursor positions since value-to-pixel mapping changed
    update_all_cursor_positions(graph);

    spdlog::debug("[TempGraph] Temperature range set: {:.0f} - {:.0f}°C", min, max);
}

// Set point count
void ui_temp_graph_set_point_count(ui_temp_graph_t* graph, int count) {
    if (!graph || count <= 0) {
        spdlog::error("[TempGraph] Invalid point count");
        return;
    }

    graph->point_count = count;
    lv_chart_set_point_count(graph->chart, static_cast<uint32_t>(count));

    spdlog::debug("[TempGraph] Point count set: {}", count);
}

// Set gradient opacity for a series
void ui_temp_graph_set_series_gradient(ui_temp_graph_t* graph, int series_id, lv_opa_t bottom_opa,
                                       lv_opa_t top_opa) {
    ui_temp_series_meta_t* meta = find_series(graph, series_id);
    if (!meta) {
        spdlog::error("[TempGraph] Series {} not found", series_id);
        return;
    }

    meta->gradient_bottom_opa = bottom_opa;
    meta->gradient_top_opa = top_opa;

    lv_obj_invalidate(graph->chart);

    spdlog::debug("[TempGraph] Series {} gradient: bottom={}%, top={}%", series_id,
                  (bottom_opa * 100) / 255, (top_opa * 100) / 255);
}
