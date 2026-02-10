// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_temp_graph.h"

#include "ui_utils.h"

#include "theme_manager.h"

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
    // Blend toward chart background based on opacity
    // opa=255 = full color, opa=0 = full background
    // Use theme token for chart background (dark mode typically used for graphs)
    lv_color_t bg = theme_manager_get_color("graph_bg");
    uint8_t r = (color.red * opa + bg.red * (255 - opa)) / 255;
    uint8_t g = (color.green * opa + bg.green * (255 - opa)) / 255;
    uint8_t b = (color.blue * opa + bg.blue * (255 - opa)) / 255;
    return lv_color_make(r, g, b);
}

// Helper: Convert temperature value to pixel Y coordinate
// LVGL chart cursors are drawn with obj->coords.y1 as origin (not content area).
// So we must add pad_top to convert from content-relative to object-relative.
static int32_t temp_to_pixel_y(ui_temp_graph_t* graph, float temp) {
    int32_t chart_height = lv_obj_get_content_height(graph->chart);
    if (chart_height <= 0) {
        return 0; // Chart not laid out yet
    }

    // Get padding offset - cursor origin is at obj->coords.y1, not content area
    int32_t pad_top = lv_obj_get_style_pad_top(graph->chart, LV_PART_MAIN);

    // Map temperature to pixel position within content area (inverted for Y axis)
    // Use chart_height directly (not chart_height-1) to match LVGL's internal formula
    // temp=max_temp → Y=0 (top of content), temp=min_temp → Y=chart_height (bottom)
    int32_t content_y = chart_height - lv_map((int32_t)temp, (int32_t)graph->min_temp,
                                              (int32_t)graph->max_temp, 0, chart_height);

    // Return object-relative Y (includes padding offset)
    return pad_top + content_y;
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

// Helper: Update max visible temperature across all series
// Called when data changes to maintain gradient reference point
static void update_max_visible_temp(ui_temp_graph_t* graph) {
    if (!graph)
        return;

    float max_temp = graph->min_temp; // Start at minimum

    // Scan all series to find the maximum visible temperature
    for (int i = 0; i < graph->series_count; i++) {
        ui_temp_series_meta_t* meta = &graph->series_meta[i];
        if (!meta->chart_series || !meta->visible)
            continue;

        // Get series data array from LVGL chart
        uint32_t point_count = 0;
        int32_t* y_points = lv_chart_get_y_array(graph->chart, meta->chart_series);
        if (!y_points)
            continue;

        point_count = lv_chart_get_point_count(graph->chart);

        // Find max in this series (skip uninitialized LV_CHART_POINT_NONE values)
        for (uint32_t j = 0; j < point_count; j++) {
            // Skip uninitialized points (LVGL sets these to LV_CHART_POINT_NONE = INT32_MAX)
            if (y_points[j] == LV_CHART_POINT_NONE)
                continue;

            float temp = static_cast<float>(y_points[j]);
            if (temp > graph->min_temp && temp > max_temp) {
                max_temp = temp;
            }
        }
    }

    // Ensure we have at least some gradient span (avoid division by zero)
    if (max_temp <= graph->min_temp) {
        max_temp = graph->min_temp + 1.0f;
    }

    graph->max_visible_temp = max_temp;
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

    // Get line draw descriptor early for debug logging
    lv_draw_line_dsc_t* line_dsc =
        static_cast<lv_draw_line_dsc_t*>(lv_draw_task_get_draw_dsc(draw_task));

    // Get chart coordinates for bottom reference
    lv_area_t coords;
    lv_obj_get_coords(chart, &coords);

    // Filter out garbage/uninitialized data lines:
    // When the chart has sparse data (few real points among LV_CHART_POINT_NONE values),
    // LVGL may emit line segments where one endpoint is at the chart's top edge
    // (from clamped INT32_MAX values). These appear as vertical lines from the top of chart to
    // data. Skip if either point is at/above chart top (clamped garbage value)
    int32_t chart_top = coords.y1;
    if (line_dsc->p1.y <= chart_top || line_dsc->p2.y <= chart_top) {
        spdlog::trace(
            "[TempGraph] Skipping garbage line: ({},{}) to ({},{}) - point at/above chart top {}",
            static_cast<int>(line_dsc->p1.x), static_cast<int>(line_dsc->p1.y),
            static_cast<int>(line_dsc->p2.x), static_cast<int>(line_dsc->p2.y), chart_top);
        return; // Skip gradient fill for this garbage line
    }

    // Find the series this line belongs to (match by color)
    ui_temp_series_meta_t* meta = find_series_by_color(graph, line_dsc->color);
    lv_opa_t top_opa =
        meta ? meta->gradient_top_opa : static_cast<lv_opa_t>(UI_TEMP_GRAPH_GRADIENT_TOP_OPA);
    lv_opa_t bottom_opa =
        meta ? meta->gradient_bottom_opa : static_cast<lv_opa_t>(UI_TEMP_GRAPH_GRADIENT_BOTTOM_OPA);
    lv_color_t ser_color = line_dsc->color;

    // Get line segment Y coordinates
    int32_t line_y_upper = LV_MIN(line_dsc->p1.y, line_dsc->p2.y);
    int32_t line_y_lower = LV_MAX(line_dsc->p1.y, line_dsc->p2.y);
    int32_t chart_bottom = coords.y2;

    // Calculate global gradient span based on max visible temperature
    // This makes gradient intensity relative to the data range, creating a "heat map" effect
    // where lower temperatures appear with less intense colors
    int32_t max_y = temp_to_pixel_y(graph, graph->max_visible_temp);
    int32_t global_gradient_span = chart_bottom - max_y;
    if (global_gradient_span <= 0)
        global_gradient_span = 1;

    // Calculate where this line sits in the global gradient (0.0 = bottom, 1.0 = max)
    int32_t line_height_from_bottom = chart_bottom - line_y_upper;
    float line_fraction =
        static_cast<float>(line_height_from_bottom) / static_cast<float>(global_gradient_span);
    line_fraction = LV_CLAMP(0.0f, line_fraction, 1.0f);

    // Opacity at the line is proportional to its position in the global gradient
    lv_opa_t opa_upper = static_cast<lv_opa_t>(bottom_opa + (top_opa - bottom_opa) * line_fraction);

    // For opa_lower (at lower vertex of line segment), calculate similarly
    int32_t lower_height_from_bottom = chart_bottom - line_y_lower;
    float lower_fraction =
        static_cast<float>(lower_height_from_bottom) / static_cast<float>(global_gradient_span);
    lower_fraction = LV_CLAMP(0.0f, lower_fraction, 1.0f);
    lv_opa_t opa_lower =
        static_cast<lv_opa_t>(bottom_opa + (top_opa - bottom_opa) * lower_fraction);

    // Draw triangle from line segment down to the lower point
    // Use maximum gradient stops (8) to reduce visible banding
    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);
    tri_dsc.p[0].x = line_dsc->p1.x;
    tri_dsc.p[0].y = line_dsc->p1.y;
    tri_dsc.p[1].x = line_dsc->p2.x;
    tri_dsc.p[1].y = line_dsc->p2.y;
    tri_dsc.p[2].x = line_dsc->p1.y < line_dsc->p2.y ? line_dsc->p1.x : line_dsc->p2.x;
    tri_dsc.p[2].y = LV_MAX(line_dsc->p1.y, line_dsc->p2.y);

    tri_dsc.grad.dir = LV_GRAD_DIR_VER;
    // Use 8 stops for smoother triangle gradient
    constexpr int TRI_STOPS = 8;
    for (int i = 0; i < TRI_STOPS; i++) {
        lv_opa_t stop_opa = static_cast<lv_opa_t>(
            opa_upper + (static_cast<int32_t>(opa_lower) - static_cast<int32_t>(opa_upper)) * i /
                            (TRI_STOPS - 1));
        uint8_t stop_frac = static_cast<uint8_t>(255 * i / (TRI_STOPS - 1));
        tri_dsc.grad.stops[i].color = ser_color;
        tri_dsc.grad.stops[i].opa = stop_opa;
        tri_dsc.grad.stops[i].frac = stop_frac;
    }
    tri_dsc.grad.stops_count = TRI_STOPS;

    lv_draw_triangle(base_dsc->layer, &tri_dsc);

    // Draw rectangle from the lower line point down to chart bottom
    // Use maximum gradient stops (8) to reduce visible banding
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_grad.dir = LV_GRAD_DIR_VER;

    // Use 8 evenly-spaced stops for smoother gradient
    constexpr int RECT_STOPS = 8;
    for (int i = 0; i < RECT_STOPS; i++) {
        lv_opa_t stop_opa = static_cast<lv_opa_t>(
            opa_lower + (static_cast<int32_t>(bottom_opa) - static_cast<int32_t>(opa_lower)) * i /
                            (RECT_STOPS - 1));
        uint8_t stop_frac = static_cast<uint8_t>(255 * i / (RECT_STOPS - 1));
        rect_dsc.bg_grad.stops[i].color = ser_color;
        rect_dsc.bg_grad.stops[i].opa = stop_opa;
        rect_dsc.bg_grad.stops[i].frac = stop_frac;
    }
    rect_dsc.bg_grad.stops_count = RECT_STOPS;

    lv_area_t rect_area;
    rect_area.x1 = static_cast<int32_t>(LV_MIN(line_dsc->p1.x, line_dsc->p2.x));
    rect_area.x2 = static_cast<int32_t>(LV_MAX(line_dsc->p1.x, line_dsc->p2.x));
    if (rect_area.x2 <= rect_area.x1) {
        rect_area.x2 = rect_area.x1 + 1;
    }
    rect_area.y1 = static_cast<int32_t>(LV_MAX(line_dsc->p1.y, line_dsc->p2.y));
    rect_area.y2 = static_cast<int32_t>(coords.y2);

    lv_draw_rect(base_dsc->layer, &rect_dsc, &rect_area);
}

// Draw X-axis time labels (rendered directly on graph canvas)
// Uses LV_EVENT_DRAW_POST to draw after chart content is rendered
static void draw_x_axis_labels_cb(lv_event_t* e) {
    lv_obj_t* chart = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    ui_temp_graph_t* graph = static_cast<ui_temp_graph_t*>(lv_event_get_user_data(e));

    if (!layer || !graph || graph->visible_point_count == 0) {
        spdlog::trace("[TempGraph] X-axis skip: layer={}, graph={}, points={}", layer != nullptr,
                      graph != nullptr, graph ? graph->visible_point_count : -1);
        return; // No data to label yet
    }

    spdlog::trace("[TempGraph] Drawing X-axis labels: {} points, first={}ms, latest={}ms",
                  graph->visible_point_count, graph->first_point_time_ms,
                  graph->latest_point_time_ms);

    // Get chart bounds
    lv_area_t coords;
    lv_obj_get_coords(chart, &coords);
    int32_t content_width = lv_obj_get_content_width(chart);

    // Calculate content area (inside padding)
    int32_t pad_left = lv_obj_get_style_pad_left(chart, LV_PART_MAIN);
    int32_t pad_right = lv_obj_get_style_pad_right(chart, LV_PART_MAIN);
    int32_t content_x1 = coords.x1 + pad_left;
    int32_t content_x2 = coords.x2 - pad_right;

    // Setup label descriptor - match Y-axis label style exactly
    // Y-axis labels use configurable font and get their color from LVGL's theme default
    // We get the text color from the chart's LV_PART_TICKS style (used for axis labels)
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_obj_get_style_text_color(chart, LV_PART_MAIN); // Use chart's text color
    label_dsc.font = graph->axis_font;                                  // Configurable axis font
    label_dsc.align = LV_TEXT_ALIGN_CENTER;
    label_dsc.opa = lv_obj_get_style_text_opa(chart, LV_PART_MAIN); // Use chart's text opacity

    // The chart has a fixed number of points (1200 by default = 20 minutes at 1 sample/sec)
    // Each data point represents 1 second, so the total time span is fixed
    int64_t total_display_time_ms =
        static_cast<int64_t>(graph->point_count) * 1000; // 20 min = 1,200,000 ms

    // The "now" time is always at the rightmost edge
    int64_t latest_ms = graph->latest_point_time_ms;

    // Calculate what time corresponds to the leftmost edge of the graph
    // This is "now - total_display_time"
    int64_t leftmost_ms = latest_ms - total_display_time_ms;

    // Label positioning: Y is aligned with bottom Y-axis label (0° baseline)
    // The Y-axis labels use space_between layout, with 0° at the chart content bottom
    int32_t pad_bottom = lv_obj_get_style_pad_bottom(chart, LV_PART_MAIN);
    int32_t label_height =
        theme_manager_get_font_height(graph->axis_font); // Configurable axis font
    // Add small gap between chart content and X-axis labels
    int32_t space_xs = theme_manager_get_spacing("space_xs"); // 4/5/6px gap
    // Position below chart content with responsive gap
    int32_t label_y = coords.y2 - pad_bottom + space_xs;

    // Determine label interval based on total display time (fixed)
    // For 20 minutes (1200s), show labels every 5 minutes
    int64_t label_interval_ms = 5 * 60 * 1000; // 5 minutes default
    if (total_display_time_ms < 2 * 60 * 1000) {
        label_interval_ms = 30 * 1000; // 30 seconds for < 2 min
    } else if (total_display_time_ms < 10 * 60 * 1000) {
        label_interval_ms = 2 * 60 * 1000; // 2 minutes for < 10 min
    }

    // Track previous label to skip duplicates
    char prev_label[12] = ""; // Sized for 12H format: "12:30 PM"

    // Draw labels at regular time intervals
    // Start from the first time that's on a nice boundary after the left edge
    int64_t first_label_ms = (leftmost_ms / label_interval_ms) * label_interval_ms;
    if (first_label_ms < leftmost_ms) {
        first_label_ms += label_interval_ms;
    }

    for (int64_t label_time_ms = first_label_ms; label_time_ms <= latest_ms;
         label_time_ms += label_interval_ms) {
        // Calculate X position for this time
        // Position is proportional: (time - leftmost) / total_display_time * width
        int64_t time_offset = label_time_ms - leftmost_ms;
        int32_t label_x = content_x1 + static_cast<int32_t>((time_offset * content_width) /
                                                            total_display_time_ms);

        // Skip if outside chart bounds
        if (label_x < content_x1 || label_x > content_x2) {
            continue;
        }

        // Format time string based on user preference (12H or 24H)
        time_t time_sec = static_cast<time_t>(label_time_ms / 1000);
        struct tm* tm_info = localtime(&time_sec);
        // Use static buffer array - LVGL may defer draw and need persistent strings
        // Buffer sized for 12H format: "12:30 PM" + null = 9 chars
        static char time_str_buf[8][12]; // 8 labels max, 12 chars each
        static int time_str_idx = 0;
        char* time_str = time_str_buf[time_str_idx++ % 8];
        strftime(time_str, 12, get_time_format_string(), tm_info);
        // Trim leading space from %l (space-padded hour in 12H format)
        if (time_str[0] == ' ') {
            memmove(time_str, time_str + 1, strlen(time_str));
        }

        // Skip duplicate labels (same HH:MM)
        if (strcmp(time_str, prev_label) == 0) {
            continue;
        }
        strncpy(prev_label, time_str, sizeof(prev_label) - 1);

        // Create label area (centered on label_x)
        // Sized for 12H format like "12:30 PM" (wider than 24H "14:30")
        lv_area_t label_area;
        label_area.x1 = label_x - 40; // 80px width, centered (fits "12:30 PM")
        label_area.y1 = label_y;
        label_area.x2 = label_x + 40;
        label_area.y2 = label_y + label_height;

        label_dsc.text = time_str;
        lv_draw_label(layer, &label_dsc, &label_area);
    }

    // Show "now" label at rightmost edge ONLY when chart is reasonably full
    // (at least 80% of points have data) - prevents overlap with time-based labels
    if (graph->visible_point_count >= (graph->point_count * 4 / 5)) {
        time_t now_sec = static_cast<time_t>(latest_ms / 1000);
        struct tm* tm_info = localtime(&now_sec);
        // Use static buffer for the "now" label (sized for 12H format)
        static char now_str[12];
        strftime(now_str, sizeof(now_str), get_time_format_string(), tm_info);
        // Trim leading space from %l (space-padded hour in 12H format)
        if (now_str[0] == ' ') {
            memmove(now_str, now_str + 1, strlen(now_str));
        }

        // Only draw if different from last label
        if (strcmp(now_str, prev_label) != 0) {
            // Sized for 12H format like "12:30 PM" (wider than 24H "14:30")
            lv_area_t label_area;
            label_area.x1 = content_x2 - 44; // 80px width, right-aligned
            label_area.y1 = label_y;
            label_area.x2 = content_x2 + 36;
            label_area.y2 = label_y + label_height;

            label_dsc.text = now_str;
            label_dsc.align = LV_TEXT_ALIGN_RIGHT; // Right-align the "now" label
            lv_draw_label(layer, &label_dsc, &label_area);
        }
    }
}

// Draw custom grid lines constrained to content area (not extending into label areas)
// Uses LV_EVENT_DRAW_MAIN to draw before chart content
static void draw_grid_lines_cb(lv_event_t* e) {
    lv_obj_t* chart = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    ui_temp_graph_t* graph = static_cast<ui_temp_graph_t*>(lv_event_get_user_data(e));

    if (!layer || !graph) {
        return;
    }

    // Get chart bounds
    lv_area_t coords;
    lv_obj_get_coords(chart, &coords);

    // Calculate content area (where data is drawn, excluding label areas)
    int32_t pad_top = lv_obj_get_style_pad_top(chart, LV_PART_MAIN);
    int32_t pad_left = lv_obj_get_style_pad_left(chart, LV_PART_MAIN);
    int32_t pad_right = lv_obj_get_style_pad_right(chart, LV_PART_MAIN);
    int32_t pad_bottom = lv_obj_get_style_pad_bottom(chart, LV_PART_MAIN);

    int32_t content_x1 = coords.x1 + pad_left;
    int32_t content_x2 = coords.x2 - pad_right;
    int32_t content_y1 = coords.y1 + pad_top;
    int32_t content_y2 = coords.y2 - pad_bottom;
    int32_t content_width = content_x2 - content_x1;
    int32_t content_height = content_y2 - content_y1;

    if (content_width <= 0 || content_height <= 0) {
        return; // Chart not laid out yet
    }

    // Setup line style - use explicit theme token for consistent grid appearance
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = theme_manager_get_color("elevated_bg"); // Match bed mesh grid
    line_dsc.width = 1;
    line_dsc.opa = LV_OPA_30;

    // Draw horizontal grid lines (5 lines = 4 divisions)
    constexpr int H_DIVISIONS = 5;
    for (int i = 0; i <= H_DIVISIONS; i++) {
        int32_t y = content_y1 + (content_height * i) / H_DIVISIONS;
        line_dsc.p1.x = content_x1;
        line_dsc.p1.y = y;
        line_dsc.p2.x = content_x2;
        line_dsc.p2.y = y;
        lv_draw_line(layer, &line_dsc);
    }

    // Draw vertical grid lines (10 lines = 9 divisions)
    constexpr int V_DIVISIONS = 10;
    for (int i = 0; i <= V_DIVISIONS; i++) {
        int32_t x = content_x1 + (content_width * i) / V_DIVISIONS;
        line_dsc.p1.x = x;
        line_dsc.p1.y = content_y1;
        line_dsc.p2.x = x;
        line_dsc.p2.y = content_y2;
        lv_draw_line(layer, &line_dsc);
    }
}

// Draw Y-axis temperature labels (rendered directly on graph canvas)
// Uses LV_EVENT_DRAW_POST to draw after chart content is rendered
static void draw_y_axis_labels_cb(lv_event_t* e) {
    lv_obj_t* chart = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    ui_temp_graph_t* graph = static_cast<ui_temp_graph_t*>(lv_event_get_user_data(e));

    if (!layer || !graph || !graph->show_y_axis || graph->y_axis_increment <= 0) {
        return; // Y-axis labels disabled or invalid config
    }

    // Get chart bounds and content area
    lv_area_t coords;
    lv_obj_get_coords(chart, &coords);
    int32_t pad_top = lv_obj_get_style_pad_top(chart, LV_PART_MAIN);

    // Chart content area (where data is drawn)
    // Account for extra X-axis label space in bottom padding (matches create() formula)
    int32_t x_axis_label_height =
        theme_manager_get_font_height(theme_manager_get_font("font_small"));
    int32_t space_sm = theme_manager_get_spacing("space_sm");
    int32_t space_md = theme_manager_get_spacing("space_md");
    int32_t content_top = coords.y1 + pad_top;
    int32_t content_bottom = coords.y2 - (space_sm + x_axis_label_height + space_md);
    int32_t content_height = content_bottom - content_top;

    // Setup label descriptor - same style as X-axis
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_obj_get_style_text_color(chart, LV_PART_MAIN);
    label_dsc.font = graph->axis_font;     // Configurable axis font
    label_dsc.align = LV_TEXT_ALIGN_RIGHT; // Right-align Y-axis labels
    label_dsc.opa = lv_obj_get_style_text_opa(chart, LV_PART_MAIN);

    // Y-axis label dimensions (for positioning)
    int32_t label_height = theme_manager_get_font_height(graph->axis_font);
    int32_t label_width = graph->y_axis_width; // Use configured width

    // Temperature range
    float temp_range = graph->max_temp - graph->min_temp;
    if (temp_range <= 0)
        return;

    // Draw labels at each temperature increment
    // Use static buffer array - LVGL may defer draw and need persistent strings
    static char temp_str_buf[8][8]; // 8 labels max (0, 80, 160, 240, 320 = 5 for nozzle)
    static int temp_str_idx = 0;
    temp_str_idx = 0; // Reset each draw cycle

    for (float temp = graph->min_temp; temp <= graph->max_temp; temp += graph->y_axis_increment) {
        // Calculate Y position: (max_temp - temp) / range * height
        // Top = max_temp, Bottom = min_temp
        float temp_fraction = (graph->max_temp - temp) / temp_range;
        int32_t label_y = content_top + static_cast<int32_t>(temp_fraction * content_height);

        // Center label vertically on the temperature line
        label_y -= label_height / 2;

        // Format temperature string into persistent buffer
        char* temp_str = temp_str_buf[temp_str_idx++ % 8];
        snprintf(temp_str, 8, "%d°", static_cast<int>(temp));

        // Draw label in left padding area (to the left of chart content)
        lv_area_t label_area;
        label_area.x1 = coords.x1;
        label_area.y1 = label_y;
        label_area.x2 = coords.x1 + label_width;
        label_area.y2 = label_y + label_height;

        label_dsc.text = temp_str;
        lv_draw_label(layer, &label_dsc, &label_area);
    }
}

// Theme change callback: re-apply chart colors when theme toggles
static void theme_change_cb(lv_observer_t* observer, lv_subject_t* subject) {
    (void)subject;
    auto* graph = static_cast<ui_temp_graph_t*>(lv_observer_get_user_data(observer));
    if (!graph || !graph->chart) {
        return;
    }

    // Re-apply themed background color
    lv_obj_set_style_bg_color(graph->chart, theme_manager_get_color("graph_bg"), LV_PART_MAIN);

    // Re-apply themed text color for axis labels
    lv_obj_set_style_text_color(graph->chart, theme_manager_get_color("text"), LV_PART_MAIN);

    // Force full redraw so draw callbacks (grid, axis labels, gradients) pick up new colors
    lv_obj_invalidate(graph->chart);

    spdlog::debug("[TempGraph] Updated colors on theme change");
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
    graph->y_axis_increment = 0; // Disabled by default (caller must enable)
    graph->show_y_axis = false;
    graph->max_visible_temp = graph->min_temp + 1.0f; // Initialize to avoid zero gradient span
    graph->axis_font = theme_manager_get_font("font_small"); // Default axis label font
    graph->y_axis_width = 40;                                // Default Y-axis label width

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
    lv_obj_set_style_bg_color(graph->chart, theme_manager_get_color("graph_bg"), LV_PART_MAIN);
    lv_obj_set_style_border_width(graph->chart, 0, LV_PART_MAIN);
    // Use responsive spacing from theme constants
    int32_t space_md = theme_manager_get_spacing("space_md"); // 8/10/12px
    int32_t space_xs = theme_manager_get_spacing("space_xs"); // 4/5/6px for axis label gaps
    int32_t label_height = theme_manager_get_font_height(theme_manager_get_font("font_small"));
    int32_t y_axis_label_width = 40; // Width for Y-axis labels (fits "320°")

    lv_obj_set_style_pad_top(graph->chart, space_md, LV_PART_MAIN);
    lv_obj_set_style_pad_right(graph->chart, space_md, LV_PART_MAIN);
    // Extra left padding for Y-axis labels: label width + gap
    lv_obj_set_style_pad_left(graph->chart, y_axis_label_width + space_xs, LV_PART_MAIN);
    // Extra bottom padding for X-axis time labels: gap + label height
    // Use space_md for larger gap to accommodate 12-hour AM/PM format labels
    int32_t space_sm = theme_manager_get_spacing("space_sm"); // 6/8/10px
    lv_obj_set_style_pad_bottom(graph->chart, space_sm + label_height + space_md, LV_PART_MAIN);

    // Style division lines (theme handles colors)
    lv_obj_set_style_line_width(graph->chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_opa(graph->chart, LV_OPA_30, LV_PART_MAIN); // Subtle - 30% opacity

    // Style data series lines
    lv_obj_set_style_line_width(graph->chart, 2, LV_PART_ITEMS);          // Series line thickness
    lv_obj_set_style_line_opa(graph->chart, LV_OPA_COVER, LV_PART_ITEMS); // Full opacity for series

    // Hide point indicators (circles at each data point)
    lv_obj_set_style_width(graph->chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(graph->chart, 0, LV_PART_INDICATOR);

    // Style target temperature cursor (dashed line, thinner than series)
    // Note: cursor color is set per-cursor in ui_temp_graph_add_series()
    lv_obj_set_style_line_width(graph->chart, 1, LV_PART_CURSOR);      // Thinner than series (2px)
    lv_obj_set_style_line_dash_width(graph->chart, 6, LV_PART_CURSOR); // Dash length
    lv_obj_set_style_line_dash_gap(graph->chart, 4, LV_PART_CURSOR);   // Gap between dashes
    lv_obj_set_style_width(graph->chart, 0, LV_PART_CURSOR);           // No point marker
    lv_obj_set_style_height(graph->chart, 0, LV_PART_CURSOR);          // No point marker

    // Disable LVGL's built-in division lines - we draw custom ones constrained to content area
    lv_chart_set_div_line_count(graph->chart, 0, 0);

    // Enable LVGL 9 draw task events for gradient fills under chart lines
    lv_obj_add_flag(graph->chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(graph->chart, draw_task_cb, LV_EVENT_DRAW_TASK_ADDED, graph);

    // Store graph pointer in chart user data for retrieval
    lv_obj_set_user_data(graph->chart, graph);

    // Register resize callback to recalculate value-based cursor positions
    lv_obj_add_event_cb(graph->chart, chart_resize_cb, LV_EVENT_SIZE_CHANGED, nullptr);

    // Register custom grid drawing callback (draws lines constrained to content area)
    lv_obj_add_event_cb(graph->chart, draw_grid_lines_cb, LV_EVENT_DRAW_MAIN, graph);

    // Register X-axis label draw callback (renders time labels directly on canvas)
    lv_obj_add_event_cb(graph->chart, draw_x_axis_labels_cb, LV_EVENT_DRAW_POST, graph);

    // Register Y-axis label draw callback (renders temperature labels directly on canvas)
    lv_obj_add_event_cb(graph->chart, draw_y_axis_labels_cb, LV_EVENT_DRAW_POST, graph);

    // Subscribe to theme changes for live color updates
    lv_subject_t* theme_subject = theme_manager_get_changed_subject();
    if (theme_subject) {
        // Tie observer to chart widget — auto-removed when chart is deleted
        graph->theme_observer =
            lv_subject_add_observer_obj(theme_subject, theme_change_cb, graph->chart, graph);
    }

    spdlog::debug("[TempGraph] Created: {} points, {:.0f}-{:.0f}°C range", graph->point_count,
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

    // Delete chart widget — theme observer is auto-removed via lv_subject_add_observer_obj.
    // Manual lv_observer_remove() would free the observer, but LVGL's child-delete
    // cascade would then fire unsubscribe_on_delete_cb on freed memory → crash.
    if (graph_ptr->chart) {
        lv_obj_del(graph_ptr->chart);
    }

    // graph_ptr automatically freed via ~unique_ptr()
    spdlog::trace("[TempGraph] Destroyed");
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

    // Initialize all points to POINT_NONE (no data) so empty chart doesn't show false history.
    // The draw_task_cb filters out garbage lines from POINT_NONE values (clamped to chart top).
    lv_chart_set_all_values(graph->chart, ser, LV_CHART_POINT_NONE);

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
    meta->first_value_received = false;

    // Create target temperature cursor (horizontal dashed line, initially hidden)
    // Note: We don't use lv_chart_set_cursor_point because that binds the cursor
    // to a data point which scrolls. Instead we use lv_chart_set_cursor_pos for
    // a fixed Y position representing the target temperature.
    // Use moderately muted color so target line is visible but distinct from actual data
    lv_color_t cursor_color = mute_color(color, LV_OPA_50); // 50% opacity for visibility
    meta->target_cursor = lv_chart_add_cursor(graph->chart, cursor_color, LV_DIR_HOR);

    graph->series_count++;

    spdlog::trace("[TempGraph] Added series {} '{}' (slot {}, color 0x{:06X})", meta->id,
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

    // Add point to series (shifts old data left)
    lv_chart_set_next_value(graph->chart, meta->chart_series, (int32_t)temp);

    // Update max visible temperature for gradient rendering
    update_max_visible_temp(graph);
}

// Add temperature point with timestamp (for X-axis labels)
void ui_temp_graph_update_series_with_time(ui_temp_graph_t* graph, int series_id, float temp,
                                           int64_t timestamp_ms) {
    ui_temp_series_meta_t* meta = find_series(graph, series_id);
    if (!meta) {
        spdlog::error("[TempGraph] Series {} not found", series_id);
        return;
    }

    // On first real value, backfill all points to avoid spike from 0/uninitialized
    // This makes the graph start at the actual temperature instead of showing a ramp from 0
    if (!meta->first_value_received) {
        meta->first_value_received = true;
        lv_chart_set_all_values(graph->chart, meta->chart_series, static_cast<int32_t>(temp));
        spdlog::debug("[TempGraph] Series {} '{}' backfilled with initial temp {:.1f}°C", series_id,
                      meta->name, temp);
    }

    // Track timestamp for X-axis label rendering
    graph->latest_point_time_ms = timestamp_ms;
    graph->visible_point_count++;

    // When buffer is full, oldest point scrolls off - update first_point_time_ms
    // First point timestamp is latest - (display period) when full, or the first timestamp received
    if (graph->first_point_time_ms == 0) {
        graph->first_point_time_ms = timestamp_ms;
    } else if (graph->visible_point_count > graph->point_count) {
        // Buffer is full, oldest point scrolled off
        // First visible point is now: latest - (point_count - 1) samples back
        // At 1 sample/sec, that's (point_count - 1) seconds before latest
        graph->first_point_time_ms =
            timestamp_ms - static_cast<int64_t>(graph->point_count - 1) * 1000;
    }

    // Add point to series (shifts old data left)
    lv_chart_set_next_value(graph->chart, meta->chart_series, static_cast<int32_t>(temp));

    // Update max visible temperature for gradient rendering
    update_max_visible_temp(graph);
}

// Replace all data points (array mode)
void ui_temp_graph_set_series_data(ui_temp_graph_t* graph, int series_id, const float* temps,
                                   int count) {
    ui_temp_series_meta_t* meta = find_series(graph, series_id);
    if (!meta || !temps || count <= 0) {
        spdlog::error("[TempGraph] Invalid parameters");
        return;
    }

    // Clear existing data before setting new values
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

    // Update max visible temperature for gradient rendering
    update_max_visible_temp(graph);

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

    // Update max visible temperature for gradient rendering
    update_max_visible_temp(graph);

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

    // Update max visible temperature for gradient rendering
    update_max_visible_temp(graph);

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

    if (meta->target_cursor) {
        if (show) {
            // Convert temperature value to pixel coordinate
            // This abstraction allows callers to work with temperatures, not pixels
            lv_obj_update_layout(graph->chart); // Ensure dimensions are current
            int32_t pixel_y = temp_to_pixel_y(graph, target);
            lv_chart_set_cursor_pos_y(graph->chart, meta->target_cursor, pixel_y);
        } else {
            // LVGL cursors don't have a hide function - move off-screen to hide
            // Use a large negative value that's definitely outside the chart area
            lv_chart_set_cursor_pos_y(graph->chart, meta->target_cursor, -10000);
        }
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

    spdlog::trace("[TempGraph] Series {} gradient: bottom={}%, top={}%", series_id,
                  (bottom_opa * 100) / 255, (top_opa * 100) / 255);
}

// Set Y-axis label configuration
void ui_temp_graph_set_y_axis(ui_temp_graph_t* graph, float increment, bool show) {
    if (!graph) {
        spdlog::error("[TempGraph] NULL graph in set_y_axis");
        return;
    }

    graph->y_axis_increment = increment;
    graph->show_y_axis = show;

    // Force redraw to apply changes
    lv_obj_invalidate(graph->chart);

    spdlog::debug("[TempGraph] Y-axis config: increment={:.0f}°, show={}", increment, show);
}

// Set axis label font size
void ui_temp_graph_set_axis_size(ui_temp_graph_t* graph, const char* size) {
    if (!graph) {
        spdlog::error("[TempGraph] NULL graph in set_axis_size");
        return;
    }

    // Map size name to font token using shared helper
    const char* font_token = theme_manager_size_to_font_token(size, "sm");

    // Y-axis width varies by size (smaller fonts need less space)
    int32_t y_axis_width = 40; // default for "sm"
    if (size) {
        if (strcmp(size, "xs") == 0) {
            y_axis_width = 30;
        } else if (strcmp(size, "md") == 0) {
            y_axis_width = 45;
        } else if (strcmp(size, "lg") == 0) {
            y_axis_width = 50;
        }
    }

    graph->axis_font = theme_manager_get_font(font_token);
    graph->y_axis_width = y_axis_width;

    // Recalculate padding to match new font size
    int32_t space_xs = theme_manager_get_spacing("space_xs");
    int32_t space_sm = theme_manager_get_spacing("space_sm");
    int32_t space_md = theme_manager_get_spacing("space_md");
    int32_t label_height = theme_manager_get_font_height(graph->axis_font);

    // Update padding (tighter for smaller sizes)
    // Top padding must accommodate the full top Y-axis label above the top grid line
    bool is_xs = size && strcmp(size, "xs") == 0;
    int32_t min_top_for_label = label_height;
    int32_t top_pad =
        is_xs ? LV_MAX(space_sm, min_top_for_label) : LV_MAX(space_md, min_top_for_label);
    int32_t left_pad = y_axis_width + space_sm; // Add gap between labels and chart
    int32_t bottom_pad =
        is_xs ? (space_xs + label_height + space_xs) : (space_sm + label_height + space_md);

    lv_obj_set_style_pad_top(graph->chart, top_pad, LV_PART_MAIN);
    lv_obj_set_style_pad_left(graph->chart, left_pad, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(graph->chart, bottom_pad, LV_PART_MAIN);

    lv_obj_invalidate(graph->chart);

    spdlog::debug("[TempGraph] Axis size: {} -> {} (y_width={}, label_h={})", size ? size : "null",
                  font_token, y_axis_width, label_height);
}
