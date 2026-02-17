// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_system_path_canvas.h"

#include "ui_fonts.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"
#include "nozzle_renderer_bambu.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <memory>
#include <unordered_map>

// ============================================================================
// Constants
// ============================================================================

// Default dimensions
static constexpr int32_t DEFAULT_WIDTH = 300;
static constexpr int32_t DEFAULT_HEIGHT = 150;

// Layout ratios (as fraction of widget height)
static constexpr float ENTRY_Y_RATIO = 0.05f;    // Top entry points for unit outputs
static constexpr float MERGE_Y_RATIO = 0.25f;    // Where unit lines converge to center
static constexpr float HUB_Y_RATIO = 0.40f;      // Hub center
static constexpr float HUB_HEIGHT_RATIO = 0.10f; // Hub box height
static constexpr float TOOLS_Y_RATIO = 0.62f;    // Tool nozzle row (multi-tool mode)
static constexpr float NOZZLE_Y_RATIO = 0.72f;   // Nozzle center (well below hub, above bottom)

// ============================================================================
// Widget State
// ============================================================================

struct SystemPathData {
    int unit_count = 0;
    static constexpr int MAX_UNITS = 8;
    static constexpr int MAX_TOOLS = 16;
    int32_t unit_x_positions[MAX_UNITS] = {}; // X center of each unit card
    int active_unit = -1;                     // -1 = none active
    uint32_t active_color = 0x4488FF;         // Filament color of active path
    bool filament_loaded = false;             // Whether filament reaches nozzle
    char status_text[64] = {};                // Status label drawn to left of nozzle

    // Bypass support
    bool has_bypass = false;          // Whether to show bypass path
    bool bypass_active = false;       // Whether bypass is the active path (current_slot == -2)
    uint32_t bypass_color = 0x888888; // Color when bypass active

    // Per-unit hub sensor states
    bool unit_hub_triggered[MAX_UNITS] = {};  // Per-unit hub sensor state
    bool unit_has_hub_sensor[MAX_UNITS] = {}; // Per-unit hub sensor capability

    // Toolhead sensor state
    bool has_toolhead_sensor = false;       // System has a toolhead entry sensor
    bool toolhead_sensor_triggered = false; // Filament detected at toolhead

    // Per-unit tool routing (mixed topology support)
    int unit_tool_count[MAX_UNITS] = {}; // Tools per unit (BT=4, OpenAMS=1)
    int unit_first_tool[MAX_UNITS] = {}; // First tool index for this unit
    int unit_topology[MAX_UNITS] = {};   // 0=LINEAR, 1=HUB, 2=PARALLEL
    int total_tools = 0;                 // Total tool count across all units
    int active_tool = -1;                // Currently active tool (-1=none)

    // Theme-derived colors (cached)
    lv_color_t color_idle;
    lv_color_t color_hub_bg;
    lv_color_t color_hub_border;
    lv_color_t color_nozzle;
    lv_color_t color_text;

    // Theme-derived sizes
    int32_t line_width_idle = 2;
    int32_t line_width_active = 4;
    int32_t hub_width = 80;
    int32_t hub_height = 30;
    int32_t border_radius = 6;
    int32_t extruder_scale = 10;
    const lv_font_t* label_font = nullptr;
};

// Registry of widget data
static std::unordered_map<lv_obj_t*, SystemPathData*> s_registry;

static SystemPathData* get_data(lv_obj_t* obj) {
    auto it = s_registry.find(obj);
    return (it != s_registry.end()) ? it->second : nullptr;
}

// Load theme-aware colors, fonts, and sizes
static void load_theme_colors(SystemPathData* data) {
    bool dark_mode = theme_manager_is_dark_mode();

    // Try theme-specific tokens first, fall back to standard tokens if they resolve to black
    data->color_idle =
        theme_manager_get_color(dark_mode ? "filament_idle_dark" : "filament_idle_light");
    if (data->color_idle.red == 0 && data->color_idle.green == 0 && data->color_idle.blue == 0) {
        data->color_idle = theme_manager_get_color("text_muted");
    }

    data->color_hub_bg =
        theme_manager_get_color(dark_mode ? "filament_hub_bg_dark" : "filament_hub_bg_light");
    if (data->color_hub_bg.red == 0 && data->color_hub_bg.green == 0 &&
        data->color_hub_bg.blue == 0) {
        data->color_hub_bg = theme_manager_get_color("card_bg");
    }

    data->color_hub_border = theme_manager_get_color(dark_mode ? "filament_hub_border_dark"
                                                               : "filament_hub_border_light");
    if (data->color_hub_border.red == 0 && data->color_hub_border.green == 0 &&
        data->color_hub_border.blue == 0) {
        data->color_hub_border = theme_manager_get_color("border");
    }

    data->color_nozzle =
        theme_manager_get_color(dark_mode ? "filament_nozzle_dark" : "filament_nozzle_light");
    if (data->color_nozzle.red == 0 && data->color_nozzle.green == 0 &&
        data->color_nozzle.blue == 0) {
        data->color_nozzle = theme_manager_get_color("text_muted");
    }

    data->color_text = theme_manager_get_color("text");

    int32_t space_xs = theme_manager_get_spacing("space_xs");
    int32_t space_md = theme_manager_get_spacing("space_md");
    data->line_width_idle = LV_MAX(2, space_xs / 2);
    data->line_width_active = LV_MAX(4, space_xs);
    data->hub_width = LV_MAX(70, space_md * 6);
    data->hub_height = LV_MAX(24, space_md * 2);
    data->border_radius = LV_MAX(4, space_xs);
    data->extruder_scale = LV_MAX(8, space_md);

    const char* font_name = lv_xml_get_const(nullptr, "font_small");
    data->label_font = font_name ? lv_xml_get_font(nullptr, font_name) : &noto_sans_12;

    spdlog::trace("[SystemPath] Theme colors loaded (dark={})", dark_mode);
}

// ============================================================================
// Drawing Helpers
// ============================================================================

static void draw_line(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                      lv_color_t color, int32_t width) {
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = color;
    line_dsc.width = width;
    line_dsc.p1.x = x1;
    line_dsc.p1.y = y1;
    line_dsc.p2.x = x2;
    line_dsc.p2.y = y2;
    line_dsc.round_start = true;
    line_dsc.round_end = true;
    lv_draw_line(layer, &line_dsc);
}

static void draw_vertical_line(lv_layer_t* layer, int32_t x, int32_t y1, int32_t y2,
                               lv_color_t color, int32_t width) {
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = color;
    line_dsc.width = width;
    line_dsc.p1.x = x;
    line_dsc.p1.y = y1;
    line_dsc.p2.x = x;
    line_dsc.p2.y = y2;
    line_dsc.round_start = true;
    line_dsc.round_end = true;
    lv_draw_line(layer, &line_dsc);
}

static void draw_sensor_dot(lv_layer_t* layer, int32_t cx, int32_t cy, lv_color_t color,
                            bool filled, int32_t radius) {
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.center.x = cx;
    arc_dsc.center.y = cy;
    arc_dsc.radius = static_cast<uint16_t>(radius);
    arc_dsc.start_angle = 0;
    arc_dsc.end_angle = 360;

    if (filled) {
        arc_dsc.width = static_cast<uint16_t>(radius * 2);
        arc_dsc.color = color;
    } else {
        arc_dsc.width = 2;
        arc_dsc.color = color;
    }

    lv_draw_arc(layer, &arc_dsc);
}

static void draw_hub_box(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t width, int32_t height,
                         lv_color_t bg_color, lv_color_t border_color, lv_color_t text_color,
                         const lv_font_t* font, int32_t radius, const char* label) {
    // Background
    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);
    fill_dsc.color = bg_color;
    fill_dsc.radius = radius;

    lv_area_t box_area = {cx - width / 2, cy - height / 2, cx + width / 2, cy + height / 2};
    lv_draw_fill(layer, &fill_dsc, &box_area);

    // Border
    lv_draw_border_dsc_t border_dsc;
    lv_draw_border_dsc_init(&border_dsc);
    border_dsc.color = border_color;
    border_dsc.width = 2;
    border_dsc.radius = radius;
    lv_draw_border(layer, &border_dsc, &box_area);

    // Label
    if (label && label[0] && font) {
        lv_draw_label_dsc_t label_dsc;
        lv_draw_label_dsc_init(&label_dsc);
        label_dsc.color = text_color;
        label_dsc.font = font;
        label_dsc.align = LV_TEXT_ALIGN_CENTER;
        label_dsc.text = label;

        int32_t font_h = lv_font_get_line_height(font);
        lv_area_t label_area = {cx - width / 2, cy - font_h / 2, cx + width / 2, cy + font_h / 2};
        lv_draw_label(layer, &label_dsc, &label_area);
    }
}

// Color blending helper (same pattern as filament_path_canvas)
static lv_color_t sp_blend(lv_color_t c1, lv_color_t c2, float factor) {
    factor = LV_CLAMP(factor, 0.0f, 1.0f);
    return lv_color_make((uint8_t)(c1.red + (c2.red - c1.red) * factor),
                         (uint8_t)(c1.green + (c2.green - c1.green) * factor),
                         (uint8_t)(c1.blue + (c2.blue - c1.blue) * factor));
}

// ============================================================================
// Main Draw Callback
// ============================================================================

// Helper: calculate horizontal X position for a tool in the tools row
static int32_t calc_tool_x(int tool_index, int total_tools, int32_t x_off, int32_t width) {
    if (total_tools <= 1) {
        return x_off + width / 2;
    }
    // Distribute tools evenly across 20%-80% of widget width
    int32_t margin = width / 5;
    int32_t usable = width - 2 * margin;
    if (total_tools == 1) {
        return x_off + width / 2;
    }
    return x_off + margin + (usable * tool_index) / (total_tools - 1);
}

static void system_path_draw_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    SystemPathData* data = get_data(obj);
    if (!data)
        return;

    if (data->unit_count <= 0) {
        spdlog::trace("[SystemPath] No units to draw");
        return;
    }

    // Get widget dimensions
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    int32_t width = lv_area_get_width(&obj_coords);
    int32_t height = lv_area_get_height(&obj_coords);
    int32_t x_off = obj_coords.x1;
    int32_t y_off = obj_coords.y1;

    // Determine if multi-tool routing is needed
    bool multi_tool = (data->total_tools > 1);

    // Calculate Y positions
    int32_t entry_y = y_off + (int32_t)(height * ENTRY_Y_RATIO);
    int32_t merge_y = y_off + (int32_t)(height * MERGE_Y_RATIO);
    int32_t hub_y = y_off + (int32_t)(height * HUB_Y_RATIO);
    int32_t hub_h = (int32_t)(height * HUB_HEIGHT_RATIO);
    int32_t tools_y = y_off + (int32_t)(height * TOOLS_Y_RATIO);
    int32_t nozzle_y = y_off + (int32_t)(height * NOZZLE_Y_RATIO);
    int32_t center_x = x_off + width / 2;

    // Colors
    lv_color_t idle_color = data->color_idle;
    lv_color_t active_color_lv = lv_color_hex(data->active_color);
    lv_color_t hub_bg = data->color_hub_bg;
    lv_color_t hub_border = data->color_hub_border;
    lv_color_t nozzle_color = data->color_nozzle;

    // Sizes
    int32_t line_idle = data->line_width_idle;
    int32_t line_active = data->line_width_active;
    int32_t sensor_r = LV_MAX(5, data->line_width_active);

    // Shift center_x left when bypass is supported to make room for bypass path on the right
    if (data->has_bypass && !multi_tool) {
        center_x -= width / 10; // Shift hub/toolhead ~10% left
    }

    if (multi_tool) {
        // ====================================================================
        // MULTI-TOOL MODE: Per-unit routing to individual tool positions
        // Note: Bypass rendering is intentionally omitted here — bypass mode
        // is not applicable to multi-extruder toolchanger setups since each
        // tool has its own filament path.
        // ====================================================================

        // Draw unit entry lines and per-unit routing
        for (int i = 0; i < data->unit_count && i < SystemPathData::MAX_UNITS; i++) {
            int32_t unit_x = x_off + data->unit_x_positions[i];
            bool is_active = (i == data->active_unit);
            int topology = data->unit_topology[i];
            int tool_count = data->unit_tool_count[i];
            int first_tool = data->unit_first_tool[i];

            lv_color_t line_color = is_active ? active_color_lv : idle_color;
            int32_t line_w = is_active ? line_active : line_idle;

            // Hub sensor dot on the vertical segment
            bool has_sensor = data->unit_has_hub_sensor[i];
            int32_t sensor_dot_y = entry_y + (merge_y - entry_y) * 3 / 5;

            if (topology == 2) {
                // PARALLEL topology (Box Turtle): each lane fans to its own tool
                // Draw a single entry line from unit card down to merge, then fan out
                if (has_sensor) {
                    draw_vertical_line(layer, unit_x, entry_y, sensor_dot_y - sensor_r, line_color,
                                       line_w);
                    draw_vertical_line(layer, unit_x, sensor_dot_y + sensor_r, merge_y, line_color,
                                       line_w);
                    bool filled = data->unit_hub_triggered[i];
                    lv_color_t dot_color =
                        filled ? (is_active ? active_color_lv : idle_color) : idle_color;
                    draw_sensor_dot(layer, unit_x, sensor_dot_y, dot_color, filled, sensor_r);
                } else {
                    draw_vertical_line(layer, unit_x, entry_y, merge_y, line_color, line_w);
                }

                // Fan out from merge point to each tool position
                for (int t = 0; t < tool_count && (first_tool + t) < data->total_tools; ++t) {
                    int tool_idx = first_tool + t;
                    int32_t tool_x = calc_tool_x(tool_idx, data->total_tools, x_off, width);
                    bool tool_active = is_active && (tool_idx == data->active_tool);

                    lv_color_t fan_color = tool_active ? active_color_lv : idle_color;
                    int32_t fan_w = tool_active ? line_active : line_idle;

                    draw_line(layer, unit_x, merge_y, tool_x, tools_y, fan_color, fan_w);
                }
            } else {
                // HUB topology (OpenAMS/default): all lanes converge to hub, then to one tool
                if (has_sensor) {
                    draw_vertical_line(layer, unit_x, entry_y, sensor_dot_y - sensor_r, line_color,
                                       line_w);
                    draw_vertical_line(layer, unit_x, sensor_dot_y + sensor_r, merge_y, line_color,
                                       line_w);
                    bool filled = data->unit_hub_triggered[i];
                    lv_color_t dot_color =
                        filled ? (is_active ? active_color_lv : idle_color) : idle_color;
                    draw_sensor_dot(layer, unit_x, sensor_dot_y, dot_color, filled, sensor_r);
                } else {
                    draw_vertical_line(layer, unit_x, entry_y, merge_y, line_color, line_w);
                }

                // Converge to the first (and only) tool for this hub unit
                if (tool_count > 0 && first_tool < data->total_tools) {
                    int32_t tool_x = calc_tool_x(first_tool, data->total_tools, x_off, width);

                    // Draw a small hub box at merge level, centered on tool_x
                    int32_t mini_hub_w = data->hub_width * 2 / 3;
                    int32_t mini_hub_h = hub_h * 2 / 3;
                    int32_t mini_hub_y = merge_y + (tools_y - merge_y) / 3;

                    bool hub_has_filament = is_active && data->filament_loaded;
                    lv_color_t mini_hub_bg = hub_bg;
                    if (hub_has_filament) {
                        mini_hub_bg = sp_blend(hub_bg, active_color_lv, 0.33f);
                    }

                    // Line from unit to mini hub
                    draw_line(layer, unit_x, merge_y, tool_x, mini_hub_y - mini_hub_h / 2,
                              line_color, line_w);

                    draw_hub_box(layer, tool_x, mini_hub_y, mini_hub_w, mini_hub_h, mini_hub_bg,
                                 hub_border, data->color_text, data->label_font,
                                 data->border_radius, "Hub");

                    // Line from mini hub to tool
                    bool tool_active = is_active && (first_tool == data->active_tool);
                    lv_color_t out_color = tool_active ? active_color_lv : idle_color;
                    int32_t out_w = tool_active ? line_active : line_idle;
                    draw_vertical_line(layer, tool_x, mini_hub_y + mini_hub_h / 2, tools_y,
                                       out_color, out_w);
                }
            }
        }

        // Draw tool nozzles at the bottom
        int32_t small_scale = LV_MAX(6, data->extruder_scale * 3 / 4);
        for (int t = 0; t < data->total_tools && t < SystemPathData::MAX_TOOLS; ++t) {
            int32_t tool_x = calc_tool_x(t, data->total_tools, x_off, width);
            bool is_active_tool = (t == data->active_tool) && data->filament_loaded;

            lv_color_t noz_color = is_active_tool ? active_color_lv : nozzle_color;
            draw_nozzle_bambu(layer, tool_x, tools_y, noz_color, small_scale);

            // Tool label (T0, T1, etc.) below nozzle
            if (data->label_font) {
                char tool_label[8];
                snprintf(tool_label, sizeof(tool_label), "T%d", t);

                lv_draw_label_dsc_t label_dsc;
                lv_draw_label_dsc_init(&label_dsc);
                label_dsc.color = is_active_tool ? active_color_lv : data->color_text;
                label_dsc.font = data->label_font;
                label_dsc.align = LV_TEXT_ALIGN_CENTER;
                label_dsc.text = tool_label;

                int32_t font_h = lv_font_get_line_height(data->label_font);
                int32_t label_top = tools_y + small_scale * 2 + 2;
                lv_area_t label_area = {tool_x - 15, label_top, tool_x + 15, label_top + font_h};
                lv_draw_label(layer, &label_dsc, &label_area);
            }
        }

        // Draw status text at the bottom
        if (data->status_text[0] && data->label_font) {
            lv_draw_label_dsc_t status_dsc;
            lv_draw_label_dsc_init(&status_dsc);
            status_dsc.color = data->color_text;
            status_dsc.font = data->label_font;
            status_dsc.align = LV_TEXT_ALIGN_CENTER;
            status_dsc.text = data->status_text;

            int32_t font_h = lv_font_get_line_height(data->label_font);
            int32_t status_y = y_off + height - font_h - 2;
            lv_area_t status_area = {x_off + 4, status_y, x_off + width - 4, status_y + font_h};
            lv_draw_label(layer, &status_dsc, &status_area);
        }

    } else {
        // ====================================================================
        // SINGLE-TOOL MODE: Original hub convergence (unchanged)
        // ====================================================================

        // Draw unit entry lines (one per unit, from entry to merge point)
        for (int i = 0; i < data->unit_count && i < SystemPathData::MAX_UNITS; i++) {
            int32_t unit_x = x_off + data->unit_x_positions[i];
            bool is_active = (i == data->active_unit);

            lv_color_t line_color = is_active ? active_color_lv : idle_color;
            int32_t line_w = is_active ? line_active : line_idle;

            // Hub sensor dot interrupts the vertical segment
            bool has_sensor = data->unit_has_hub_sensor[i];
            int32_t sensor_dot_y = entry_y + (merge_y - entry_y) * 3 / 5;

            if (has_sensor) {
                draw_vertical_line(layer, unit_x, entry_y, sensor_dot_y - sensor_r, line_color,
                                   line_w);
                draw_vertical_line(layer, unit_x, sensor_dot_y + sensor_r, merge_y, line_color,
                                   line_w);
                bool filled = data->unit_hub_triggered[i];
                lv_color_t dot_color =
                    filled ? (is_active ? active_color_lv : idle_color) : idle_color;
                draw_sensor_dot(layer, unit_x, sensor_dot_y, dot_color, filled, sensor_r);
            } else {
                draw_vertical_line(layer, unit_x, entry_y, merge_y, line_color, line_w);
            }

            // Angled segment from unit position at merge_y to hub top
            draw_line(layer, unit_x, merge_y, center_x, hub_y - hub_h / 2, line_color, line_w);
        }

        // Draw bypass path (if supported)
        if (data->has_bypass) {
            int32_t hub_right = center_x + data->hub_width / 2;
            int32_t bypass_x = hub_right + width / 8;
            bool bp_active = data->bypass_active;

            lv_color_t bp_color = bp_active ? lv_color_hex(data->bypass_color) : idle_color;
            int32_t bp_width = bp_active ? line_active : line_idle;

            int32_t hub_bottom = hub_y + hub_h / 2;
            int32_t bypass_merge_y = hub_bottom + (nozzle_y - hub_bottom) / 3;

            if (data->label_font) {
                lv_draw_label_dsc_t bp_label_dsc;
                lv_draw_label_dsc_init(&bp_label_dsc);
                bp_label_dsc.color =
                    bp_active ? lv_color_hex(data->bypass_color) : data->color_text;
                bp_label_dsc.font = data->label_font;
                bp_label_dsc.align = LV_TEXT_ALIGN_CENTER;
                bp_label_dsc.text = "Bypass";

                int32_t font_h = lv_font_get_line_height(data->label_font);
                int32_t label_top = bypass_merge_y - font_h - 4;
                lv_area_t bp_label_area = {bypass_x - 30, label_top, bypass_x + 30,
                                           label_top + font_h};
                lv_draw_label(layer, &bp_label_dsc, &bp_label_area);
            }

            draw_sensor_dot(layer, bypass_x, bypass_merge_y, bp_color, bp_active, sensor_r);
            draw_line(layer, bypass_x - sensor_r, bypass_merge_y, center_x, bypass_merge_y,
                      bp_color, bp_width);
        }

        // Draw combiner hub
        {
            bool hub_has_filament = (data->active_unit >= 0 && data->filament_loaded);
            lv_color_t hub_bg_tinted = hub_bg;
            if (hub_has_filament) {
                hub_bg_tinted = sp_blend(hub_bg, active_color_lv, 0.33f);
            }
            draw_hub_box(layer, center_x, hub_y, data->hub_width, hub_h, hub_bg_tinted, hub_border,
                         data->color_text, data->label_font, data->border_radius, "Hub");
        }

        // Draw output line from hub to nozzle (with sensor dots)
        {
            bool unit_active = (data->active_unit >= 0 && data->filament_loaded);
            bool bp_active = (data->bypass_active && data->filament_loaded);
            bool any_active = unit_active || bp_active;

            int32_t hub_bottom = hub_y + hub_h / 2;
            int32_t extruder_half_height = data->extruder_scale * 2;
            int32_t nozzle_top = nozzle_y - extruder_half_height;
            int32_t bypass_merge_y = hub_bottom + (nozzle_y - hub_bottom) / 3;
            int32_t toolhead_sensor_y = hub_bottom + (nozzle_top - hub_bottom) * 2 / 3;

            lv_color_t active_output_color =
                bp_active ? lv_color_hex(data->bypass_color) : active_color_lv;

            if (bp_active) {
                draw_vertical_line(layer, center_x, hub_bottom, bypass_merge_y, idle_color,
                                   line_idle);
                draw_vertical_line(layer, center_x, bypass_merge_y, nozzle_top,
                                   lv_color_hex(data->bypass_color), line_active);
            } else if (unit_active) {
                draw_vertical_line(layer, center_x, hub_bottom, nozzle_top, active_color_lv,
                                   line_active);
            } else {
                draw_vertical_line(layer, center_x, hub_bottom, nozzle_top, idle_color, line_idle);
            }

            if (data->has_toolhead_sensor) {
                bool th_filled = data->toolhead_sensor_triggered;
                lv_color_t th_dot_color = th_filled ? active_output_color : idle_color;
                if (!any_active)
                    th_dot_color = idle_color;
                draw_sensor_dot(layer, center_x, toolhead_sensor_y, th_dot_color, th_filled,
                                sensor_r);
            }

            lv_color_t noz_color = nozzle_color;
            if (bp_active) {
                noz_color = lv_color_hex(data->bypass_color);
            } else if (unit_active) {
                noz_color = active_color_lv;
            }

            draw_nozzle_bambu(layer, center_x, nozzle_y, noz_color, data->extruder_scale);

            if (data->status_text[0] && data->label_font) {
                lv_draw_label_dsc_t status_dsc;
                lv_draw_label_dsc_init(&status_dsc);
                status_dsc.color = data->color_text;
                status_dsc.font = data->label_font;
                status_dsc.align = LV_TEXT_ALIGN_RIGHT;
                status_dsc.text = data->status_text;

                int32_t font_h = lv_font_get_line_height(data->label_font);
                int32_t label_right = center_x - data->extruder_scale * 3;
                int32_t label_left = x_off + 4;
                lv_area_t status_area = {label_left, nozzle_y - font_h / 2, label_right,
                                         nozzle_y + font_h / 2};
                lv_draw_label(layer, &status_dsc, &status_area);
            }
        }
    }

    spdlog::trace("[SystemPath] Draw: units={}, active={}, loaded={}, tools={}, active_tool={}, "
                  "bypass={}(active={})",
                  data->unit_count, data->active_unit, data->filament_loaded, data->total_tools,
                  data->active_tool, data->has_bypass, data->bypass_active);
}

// ============================================================================
// Event Handlers
// ============================================================================

static void system_path_delete_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    auto it = s_registry.find(obj);
    if (it != s_registry.end()) {
        std::unique_ptr<SystemPathData> data(it->second);
        s_registry.erase(it);
        // data automatically freed when unique_ptr goes out of scope
    }
}

// ============================================================================
// XML Widget Interface
// ============================================================================

static void* system_path_xml_create(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);

    void* parent = lv_xml_state_get_parent(state);
    lv_obj_t* obj = lv_obj_create(static_cast<lv_obj_t*>(parent));
    if (!obj)
        return nullptr;

    auto data_ptr = std::make_unique<SystemPathData>();
    s_registry[obj] = data_ptr.get();
    auto* data = data_ptr.release();

    // Load theme-aware colors, fonts, and sizes
    load_theme_colors(data);

    // Configure object
    lv_obj_set_size(obj, DEFAULT_WIDTH, DEFAULT_HEIGHT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);

    // Register event handlers
    lv_obj_add_event_cb(obj, system_path_draw_cb, LV_EVENT_DRAW_POST, nullptr);
    lv_obj_add_event_cb(obj, system_path_delete_cb, LV_EVENT_DELETE, nullptr);

    spdlog::debug("[SystemPath] Created widget via XML");
    return obj;
}

static void system_path_xml_apply(lv_xml_parser_state_t* state, const char** attrs) {
    void* item = lv_xml_state_get_item(state);
    lv_obj_t* obj = static_cast<lv_obj_t*>(item);
    if (!obj)
        return;

    lv_xml_obj_apply(state, attrs);

    auto* data = get_data(obj);
    if (!data)
        return;

    bool needs_redraw = false;

    for (int i = 0; attrs[i]; i += 2) {
        const char* name = attrs[i];
        const char* value = attrs[i + 1];

        if (strcmp(name, "unit_count") == 0) {
            data->unit_count = LV_CLAMP(atoi(value), 0, SystemPathData::MAX_UNITS);
            needs_redraw = true;
        } else if (strcmp(name, "active_unit") == 0) {
            data->active_unit = atoi(value);
            needs_redraw = true;
        } else if (strcmp(name, "active_color") == 0) {
            data->active_color = strtoul(value, nullptr, 0);
            needs_redraw = true;
        } else if (strcmp(name, "filament_loaded") == 0) {
            data->filament_loaded = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
            needs_redraw = true;
        }
    }

    if (needs_redraw) {
        lv_obj_invalidate(obj);
    }
}

// ============================================================================
// Public API
// ============================================================================

void ui_system_path_canvas_register(void) {
    lv_xml_register_widget("system_path_canvas", system_path_xml_create, system_path_xml_apply);
    spdlog::info("[SystemPath] Registered system_path_canvas widget with XML system");
}

lv_obj_t* ui_system_path_canvas_create(lv_obj_t* parent) {
    if (!parent) {
        spdlog::error("[SystemPath] Cannot create: parent is null");
        return nullptr;
    }

    lv_obj_t* obj = lv_obj_create(parent);
    if (!obj) {
        spdlog::error("[SystemPath] Failed to create object");
        return nullptr;
    }

    auto data_ptr = std::make_unique<SystemPathData>();
    s_registry[obj] = data_ptr.get();
    auto* data = data_ptr.release();

    // Load theme-aware colors, fonts, and sizes
    load_theme_colors(data);

    // Configure object
    lv_obj_set_size(obj, DEFAULT_WIDTH, DEFAULT_HEIGHT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);

    // Register event handlers
    lv_obj_add_event_cb(obj, system_path_draw_cb, LV_EVENT_DRAW_POST, nullptr);
    lv_obj_add_event_cb(obj, system_path_delete_cb, LV_EVENT_DELETE, nullptr);

    spdlog::debug("[SystemPath] Created widget programmatically");
    return obj;
}

void ui_system_path_canvas_set_unit_count(lv_obj_t* obj, int count) {
    auto* data = get_data(obj);
    if (data) {
        data->unit_count = LV_CLAMP(count, 0, SystemPathData::MAX_UNITS);
        lv_obj_invalidate(obj);
    }
}

void ui_system_path_canvas_set_unit_x(lv_obj_t* obj, int unit_index, int32_t center_x) {
    auto* data = get_data(obj);
    if (data && unit_index >= 0 && unit_index < SystemPathData::MAX_UNITS) {
        data->unit_x_positions[unit_index] = center_x;
        lv_obj_invalidate(obj);
    }
}

void ui_system_path_canvas_set_active_unit(lv_obj_t* obj, int unit_index) {
    auto* data = get_data(obj);
    if (data) {
        data->active_unit = unit_index;
        lv_obj_invalidate(obj);
    }
}

void ui_system_path_canvas_set_active_color(lv_obj_t* obj, uint32_t color) {
    auto* data = get_data(obj);
    if (data) {
        data->active_color = color;
        lv_obj_invalidate(obj);
    }
}

void ui_system_path_canvas_set_filament_loaded(lv_obj_t* obj, bool loaded) {
    auto* data = get_data(obj);
    if (data) {
        data->filament_loaded = loaded;
        lv_obj_invalidate(obj);
    }
}

void ui_system_path_canvas_set_status_text(lv_obj_t* obj, const char* text) {
    auto* data = get_data(obj);
    if (data) {
        if (text) {
            snprintf(data->status_text, sizeof(data->status_text), "%s", text);
        } else {
            data->status_text[0] = '\0';
        }
        lv_obj_invalidate(obj);
    }
}

void ui_system_path_canvas_set_bypass(lv_obj_t* obj, bool has_bypass, bool bypass_active,
                                      uint32_t bypass_color) {
    auto* data = get_data(obj);
    if (data) {
        data->has_bypass = has_bypass;
        data->bypass_active = bypass_active;
        data->bypass_color = bypass_color;
        lv_obj_invalidate(obj);
    }
}

void ui_system_path_canvas_set_unit_hub_sensor(lv_obj_t* obj, int unit_index, bool has_sensor,
                                               bool triggered) {
    auto* data = get_data(obj);
    if (data && unit_index >= 0 && unit_index < SystemPathData::MAX_UNITS) {
        data->unit_has_hub_sensor[unit_index] = has_sensor;
        data->unit_hub_triggered[unit_index] = triggered;
        lv_obj_invalidate(obj);
    }
}

void ui_system_path_canvas_set_toolhead_sensor(lv_obj_t* obj, bool has_toolhead_sensor,
                                               bool toolhead_sensor_triggered) {
    auto* data = get_data(obj);
    if (data) {
        data->has_toolhead_sensor = has_toolhead_sensor;
        data->toolhead_sensor_triggered = toolhead_sensor_triggered;
        lv_obj_invalidate(obj);
    }
}

void ui_system_path_canvas_set_unit_tools(lv_obj_t* obj, int unit_index, int tool_count,
                                          int first_tool) {
    auto* data = get_data(obj);
    if (data && unit_index >= 0 && unit_index < SystemPathData::MAX_UNITS) {
        data->unit_tool_count[unit_index] = tool_count;
        data->unit_first_tool[unit_index] = first_tool;
        lv_obj_invalidate(obj);
    }
}

void ui_system_path_canvas_set_unit_topology(lv_obj_t* obj, int unit_index, int topology) {
    auto* data = get_data(obj);
    if (data && unit_index >= 0 && unit_index < SystemPathData::MAX_UNITS) {
        data->unit_topology[unit_index] = topology;
        lv_obj_invalidate(obj);
    }
}

void ui_system_path_canvas_set_total_tools(lv_obj_t* obj, int total_tools) {
    auto* data = get_data(obj);
    if (data) {
        data->total_tools = LV_CLAMP(total_tools, 0, SystemPathData::MAX_TOOLS);
        lv_obj_invalidate(obj);
    }
}

void ui_system_path_canvas_set_active_tool(lv_obj_t* obj, int tool_index) {
    auto* data = get_data(obj);
    if (data) {
        data->active_tool = tool_index;
        lv_obj_invalidate(obj);
    }
}

void ui_system_path_canvas_refresh(lv_obj_t* obj) {
    lv_obj_invalidate(obj);
}
