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

#pragma once

#include "lvgl/lvgl.h"

// Default configuration
#define UI_TEMP_GRAPH_MAX_SERIES 8            // Maximum concurrent temperature series
#define UI_TEMP_GRAPH_DEFAULT_POINTS 300      // Default point count (5 min @ 1s)
#define UI_TEMP_GRAPH_DEFAULT_MIN_TEMP 0.0f   // Default Y-axis minimum
#define UI_TEMP_GRAPH_DEFAULT_MAX_TEMP 100.0f // Default Y-axis maximum

// Gradient opacity defaults (stock chart style: visible at line, fades to transparent)
#define UI_TEMP_GRAPH_GRADIENT_TOP_OPA LV_OPA_80    // At the line (80% = prominent but not overwhelming)
#define UI_TEMP_GRAPH_GRADIENT_BOTTOM_OPA LV_OPA_0  // At chart bottom (fully transparent)

/**
 * Temperature series metadata
 * Stores information about each temperature series (heater/sensor)
 */
typedef struct {
    int id;                           // Series ID (index in series_meta array)
    lv_chart_series_t* chart_series;  // LVGL chart series
    lv_chart_cursor_t* target_cursor; // Target temperature cursor (horizontal line)
    lv_color_t color;                 // Series color
    char name[32];                    // Series name (e.g., "Nozzle", "Bed")
    bool visible;                     // Show/hide series
    bool show_target;                 // Show/hide target temperature line
    float target_temp;                // Target temperature for cursor
    lv_opa_t gradient_bottom_opa;     // Bottom gradient opacity
    lv_opa_t gradient_top_opa;        // Top gradient opacity
} ui_temp_series_meta_t;

/**
 * Temperature graph widget
 * Manages an LVGL chart with dynamic series for real-time temperature monitoring
 */
typedef struct {
    lv_obj_t* chart;                                             // LVGL chart widget
    ui_temp_series_meta_t series_meta[UI_TEMP_GRAPH_MAX_SERIES]; // Series metadata
    int series_count;                                            // Current number of series
    int next_series_id;                                          // Next available series ID
    int point_count;                                             // Number of points per series
    float min_temp;                                              // Y-axis minimum temperature
    float max_temp;                                              // Y-axis maximum temperature
} ui_temp_graph_t;

/**
 * Core API
 */

/**
 * Create a new temperature graph widget
 *
 * @param parent Parent LVGL object
 * @return Pointer to graph structure (NULL on error)
 */
ui_temp_graph_t* ui_temp_graph_create(lv_obj_t* parent);

/**
 * Destroy the temperature graph widget
 *
 * @param graph Graph instance to destroy
 */
void ui_temp_graph_destroy(ui_temp_graph_t* graph);

/**
 * Get the underlying LVGL chart widget (for custom styling)
 *
 * @param graph Graph instance
 * @return LVGL chart object
 */
lv_obj_t* ui_temp_graph_get_chart(ui_temp_graph_t* graph);

/**
 * Series Management API
 */

/**
 * Add a new temperature series to the graph
 *
 * @param graph Graph instance
 * @param name Series name (max 31 chars)
 * @param color Series color (for line and gradient)
 * @return Series ID (>=0 on success, -1 on error)
 */
int ui_temp_graph_add_series(ui_temp_graph_t* graph, const char* name, lv_color_t color);

/**
 * Remove a temperature series from the graph
 *
 * @param graph Graph instance
 * @param series_id Series ID to remove
 */
void ui_temp_graph_remove_series(ui_temp_graph_t* graph, int series_id);

/**
 * Show or hide a temperature series
 *
 * @param graph Graph instance
 * @param series_id Series ID
 * @param visible true to show, false to hide
 */
void ui_temp_graph_show_series(ui_temp_graph_t* graph, int series_id, bool visible);

/**
 * Data Update API
 */

/**
 * Add a single temperature point to a series (push mode)
 * Uses circular buffer with shift update mode
 *
 * @param graph Graph instance
 * @param series_id Series ID
 * @param temp Temperature value
 */
void ui_temp_graph_update_series(ui_temp_graph_t* graph, int series_id, float temp);

/**
 * Replace all data points for a series (array mode)
 *
 * @param graph Graph instance
 * @param series_id Series ID
 * @param temps Array of temperature values
 * @param count Number of temperatures in array
 */
void ui_temp_graph_set_series_data(ui_temp_graph_t* graph, int series_id, const float* temps,
                                   int count);

/**
 * Clear all data points in the graph (all series)
 */
void ui_temp_graph_clear(ui_temp_graph_t* graph);

/**
 * Clear data points for a specific series
 *
 * @param graph Graph instance
 * @param series_id Series ID
 */
void ui_temp_graph_clear_series(ui_temp_graph_t* graph, int series_id);

/**
 * Target Temperature API
 */

/**
 * Set target temperature and visibility for a series
 *
 * @param graph Graph instance
 * @param series_id Series ID
 * @param target Target temperature (in same units as data)
 * @param show true to show target line, false to hide
 */
void ui_temp_graph_set_series_target(ui_temp_graph_t* graph, int series_id, float target,
                                     bool show);

/**
 * Show or hide target temperature line for a series
 *
 * @param graph Graph instance
 * @param series_id Series ID
 * @param show true to show, false to hide
 */
void ui_temp_graph_show_target(ui_temp_graph_t* graph, int series_id, bool show);

/**
 * Configuration API
 */

/**
 * Set the Y-axis temperature range
 *
 * @param graph Graph instance
 * @param min Minimum temperature
 * @param max Maximum temperature
 */
void ui_temp_graph_set_temp_range(ui_temp_graph_t* graph, float min, float max);

/**
 * Set the number of data points per series (capacity)
 *
 * @param graph Graph instance
 * @param count Number of points (e.g., 300 for 5 min @ 1s)
 */
void ui_temp_graph_set_point_count(ui_temp_graph_t* graph, int count);

/**
 * Set gradient opacity for a series
 *
 * @param graph Graph instance
 * @param series_id Series ID
 * @param bottom_opa Bottom opacity (0-255, default 60% = LV_OPA_60)
 * @param top_opa Top opacity (0-255, default 10% = LV_OPA_10)
 */
void ui_temp_graph_set_series_gradient(ui_temp_graph_t* graph, int series_id, lv_opa_t bottom_opa,
                                       lv_opa_t top_opa);
