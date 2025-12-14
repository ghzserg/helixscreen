// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_filament_path_canvas.h"

#include "ui_fonts.h"
#include "ui_theme.h"
#include "ui_widget_memory.h"

#include "ams_types.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"
#include "nozzle_renderer_bambu.h"
#include "nozzle_renderer_faceted.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstring>
#include <unordered_map>

// ============================================================================
// Constants
// ============================================================================

// Default dimensions
static constexpr int32_t DEFAULT_WIDTH = 300;
static constexpr int32_t DEFAULT_HEIGHT = 200;
static constexpr int DEFAULT_GATE_COUNT = 4;

// Layout ratios (as fraction of widget height)
// Entry points at very top to connect visually with slot grid above
static constexpr float ENTRY_Y_RATIO = 0.02f;    // Top entry points (very close to top)
static constexpr float PREP_Y_RATIO = 0.10f;     // Prep sensor position
static constexpr float MERGE_Y_RATIO = 0.20f;    // Where lanes merge
static constexpr float HUB_Y_RATIO = 0.30f;      // Hub/selector center
static constexpr float HUB_HEIGHT_RATIO = 0.10f; // Hub box height
static constexpr float OUTPUT_Y_RATIO = 0.42f;   // Hub sensor (below hub)
static constexpr float TOOLHEAD_Y_RATIO = 0.54f; // Toolhead sensor
static constexpr float NOZZLE_Y_RATIO =
    0.75f; // Nozzle/extruder center (needs more room for larger extruder)

// Bypass entry point position (right side of widget, below spool area)
static constexpr float BYPASS_X_RATIO = 0.85f;       // Right side for bypass entry
static constexpr float BYPASS_ENTRY_Y_RATIO = 0.32f; // Below spools, at hub level
static constexpr float BYPASS_MERGE_Y_RATIO = 0.42f; // Where bypass joins main path (at OUTPUT)

// Line widths (scaled by space_xs for responsiveness)
static constexpr int LINE_WIDTH_IDLE_BASE = 2;
static constexpr int LINE_WIDTH_ACTIVE_BASE = 4;
static constexpr int SENSOR_RADIUS_BASE = 4;

// Default filament color (used when no active filament)
static constexpr uint32_t DEFAULT_FILAMENT_COLOR = 0x4488FF;

// ============================================================================
// Widget State
// ============================================================================

// Animation constants
static constexpr int SEGMENT_ANIM_DURATION_MS = 300; // Duration for segment-to-segment animation
static constexpr int ERROR_PULSE_DURATION_MS = 800;  // Error pulse cycle duration
static constexpr lv_opa_t ERROR_PULSE_OPA_MIN = 100; // Minimum opacity during error pulse
static constexpr lv_opa_t ERROR_PULSE_OPA_MAX = 255; // Maximum opacity during error pulse

// Animation direction
enum class AnimDirection {
    NONE = 0,
    LOADING = 1,  // Animating toward nozzle
    UNLOADING = 2 // Animating away from nozzle
};

// Per-gate filament state for visualizing all installed filaments
struct GateFilamentState {
    PathSegment segment = PathSegment::NONE; // How far filament extends
    uint32_t color = 0x808080;               // Filament color (gray default)
};

struct FilamentPathData {
    int topology = 1;                    // 0=LINEAR, 1=HUB
    int gate_count = DEFAULT_GATE_COUNT; // Number of gates
    int active_gate = -1;                // Currently active gate (-1=none)
    int filament_segment = 0;            // PathSegment enum value (target)
    int error_segment = 0;               // Error location (0=none)
    int anim_progress = 0;               // Animation progress 0-100 (for segment transition)
    uint32_t filament_color = DEFAULT_FILAMENT_COLOR;

    // Per-gate filament state (for showing all installed filaments, not just active)
    static constexpr int MAX_GATES = 16;
    GateFilamentState gate_filament_states[MAX_GATES] = {};

    // Animation state
    int prev_segment = 0; // Previous segment (for smooth transition)
    AnimDirection anim_direction = AnimDirection::NONE;
    bool segment_anim_active = false;        // Segment transition animation running
    bool error_pulse_active = false;         // Error pulse animation running
    lv_opa_t error_pulse_opa = LV_OPA_COVER; // Current error segment opacity

    // Bypass mode state
    bool bypass_active = false;       // External spool bypass mode
    uint32_t bypass_color = 0x888888; // Default gray for bypass filament

    // Toolhead renderer style
    bool use_faceted_toolhead = false; // false = Bambu-style, true = faceted red style

    // Callbacks
    filament_path_gate_cb_t gate_callback = nullptr;
    void* gate_user_data = nullptr;
    filament_path_bypass_cb_t bypass_callback = nullptr;
    void* bypass_user_data = nullptr;

    // Theme-derived colors (cached for performance)
    lv_color_t color_idle;
    lv_color_t color_error;
    lv_color_t color_hub_bg;
    lv_color_t color_hub_border;
    lv_color_t color_nozzle;
    lv_color_t color_text;

    // Theme-derived sizes
    int32_t line_width_idle = LINE_WIDTH_IDLE_BASE;
    int32_t line_width_active = LINE_WIDTH_ACTIVE_BASE;
    int32_t sensor_radius = SENSOR_RADIUS_BASE;
    int32_t hub_width = 60;
    int32_t border_radius = 6;
    int32_t extruder_scale = 10; // Scale unit for extruder (based on space_md)

    // Theme-derived font
    const lv_font_t* label_font = nullptr;
};

// Load theme-aware colors, fonts, and sizes
static void load_theme_colors(FilamentPathData* data) {
    bool dark_mode = ui_theme_is_dark_mode();

    // Use theme tokens with dark/light mode awareness
    data->color_idle = ui_theme_get_color(dark_mode ? "filament_idle_dark" : "filament_idle_light");
    data->color_error = ui_theme_get_color("filament_error");
    data->color_hub_bg =
        ui_theme_get_color(dark_mode ? "filament_hub_bg_dark" : "filament_hub_bg_light");
    data->color_hub_border =
        ui_theme_get_color(dark_mode ? "filament_hub_border_dark" : "filament_hub_border_light");
    data->color_nozzle =
        ui_theme_get_color(dark_mode ? "filament_nozzle_dark" : "filament_nozzle_light");
    data->color_text = ui_theme_get_color("text_primary");

    // Get responsive sizing from theme
    int32_t space_xs = ui_theme_get_spacing("space_xs");
    int32_t space_md = ui_theme_get_spacing("space_md");

    // Scale line widths based on spacing (responsive)
    data->line_width_idle = LV_MAX(2, space_xs / 2);
    data->line_width_active = LV_MAX(4, space_xs);
    data->sensor_radius = LV_MAX(4, space_xs);
    data->hub_width = LV_MAX(50, space_md * 5);
    data->border_radius = LV_MAX(4, space_xs);
    data->extruder_scale = LV_MAX(8, space_md); // Extruder scales with space_md

    // Get responsive font from globals.xml (font_small → responsive variant)
    const char* font_name = lv_xml_get_const(nullptr, "font_small");
    data->label_font = font_name ? lv_xml_get_font(nullptr, font_name) : &noto_sans_12;

    spdlog::trace("[FilamentPath] Theme colors loaded (dark={}, font={})", dark_mode,
                  font_name ? font_name : "fallback");
}

static std::unordered_map<lv_obj_t*, FilamentPathData*> s_registry;

static FilamentPathData* get_data(lv_obj_t* obj) {
    auto it = s_registry.find(obj);
    return (it != s_registry.end()) ? it->second : nullptr;
}

// ============================================================================
// Helper Functions
// ============================================================================

// Calculate X position for a gate's entry point (distributed across width)
static int32_t get_gate_x(int gate_index, int gate_count, int32_t width) {
    if (gate_count <= 1)
        return width / 2;

    // Match the slot_grid layout: flex_grow=1 slots in a container inset by card_padding
    // slot_grid is inside ams_unit_card with style_pad_all="#space_sm" (8px)
    // Gate centers align with slot centers: card_padding + (2*i + 1) * effective_width /
    // (2*gate_count)
    constexpr int32_t card_padding = 8; // space_sm
    int32_t effective_width = width - 2 * card_padding;
    int32_t segment_width = effective_width / gate_count;
    return card_padding + segment_width / 2 + gate_index * segment_width;
}

// Check if a segment should be drawn as "active" (filament present at or past it)
static bool is_segment_active(PathSegment segment, PathSegment filament_segment) {
    return static_cast<int>(segment) <= static_cast<int>(filament_segment) &&
           filament_segment != PathSegment::NONE;
}

// ============================================================================
// Animation Callbacks
// ============================================================================

// Forward declarations for animation callbacks
static void segment_anim_cb(void* var, int32_t value);
static void error_pulse_anim_cb(void* var, int32_t value);

// Start segment transition animation
static void start_segment_animation(lv_obj_t* obj, FilamentPathData* data, int from_segment,
                                    int to_segment) {
    if (!obj || !data)
        return;

    // Stop any existing animation
    lv_anim_delete(obj, segment_anim_cb);

    // Determine animation direction
    if (to_segment > from_segment) {
        data->anim_direction = AnimDirection::LOADING;
    } else if (to_segment < from_segment) {
        data->anim_direction = AnimDirection::UNLOADING;
    } else {
        data->anim_direction = AnimDirection::NONE;
        return; // No change, no animation needed
    }

    data->prev_segment = from_segment;
    data->segment_anim_active = true;
    data->anim_progress = 0;

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_values(&anim, 0, 100);
    lv_anim_set_duration(&anim, SEGMENT_ANIM_DURATION_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, segment_anim_cb);
    lv_anim_start(&anim);

    spdlog::trace("[FilamentPath] Started segment animation: {} -> {} ({})", from_segment,
                  to_segment,
                  data->anim_direction == AnimDirection::LOADING ? "loading" : "unloading");
}

// Stop segment animation
static void stop_segment_animation(lv_obj_t* obj, FilamentPathData* data) {
    if (!obj || !data)
        return;

    lv_anim_delete(obj, segment_anim_cb);
    data->segment_anim_active = false;
    data->anim_progress = 100;
    data->anim_direction = AnimDirection::NONE;
}

// Segment animation callback
static void segment_anim_cb(void* var, int32_t value) {
    lv_obj_t* obj = static_cast<lv_obj_t*>(var);
    FilamentPathData* data = get_data(obj);
    if (!data)
        return;

    data->anim_progress = value;

    // Animation complete
    if (value >= 100) {
        data->segment_anim_active = false;
        data->anim_direction = AnimDirection::NONE;
        data->prev_segment = data->filament_segment;
    }

    lv_obj_invalidate(obj);
}

// Start error pulse animation
static void start_error_pulse(lv_obj_t* obj, FilamentPathData* data) {
    if (!obj || !data || data->error_pulse_active)
        return;

    data->error_pulse_active = true;
    data->error_pulse_opa = ERROR_PULSE_OPA_MAX;

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_values(&anim, ERROR_PULSE_OPA_MIN, ERROR_PULSE_OPA_MAX);
    lv_anim_set_duration(&anim, ERROR_PULSE_DURATION_MS);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_playback_duration(&anim, ERROR_PULSE_DURATION_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&anim, error_pulse_anim_cb);
    lv_anim_start(&anim);

    spdlog::trace("[FilamentPath] Started error pulse animation");
}

// Stop error pulse animation
static void stop_error_pulse(lv_obj_t* obj, FilamentPathData* data) {
    if (!obj || !data)
        return;

    lv_anim_delete(obj, error_pulse_anim_cb);
    data->error_pulse_active = false;
    data->error_pulse_opa = LV_OPA_COVER;
}

// Error pulse animation callback
static void error_pulse_anim_cb(void* var, int32_t value) {
    lv_obj_t* obj = static_cast<lv_obj_t*>(var);
    FilamentPathData* data = get_data(obj);
    if (!data)
        return;

    data->error_pulse_opa = static_cast<lv_opa_t>(value);
    lv_obj_invalidate(obj);
}

// ============================================================================
// Drawing Functions
// ============================================================================

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

// Draw a partial line (for animation) - draws from start to a point determined by progress (0-100)
static void draw_partial_line(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                              lv_color_t color, int32_t width, int progress, bool reverse) {
    if (progress <= 0)
        return;
    if (progress >= 100) {
        draw_line(layer, x1, y1, x2, y2, color, width);
        return;
    }

    float factor = progress / 100.0f;
    int32_t end_x, end_y;

    if (reverse) {
        // Draw from (x2,y2) backwards by factor
        end_x = x2 - (int32_t)((x2 - x1) * factor);
        end_y = y2 - (int32_t)((y2 - y1) * factor);
        draw_line(layer, end_x, end_y, x2, y2, color, width);
    } else {
        // Draw from (x1,y1) forward by factor
        end_x = x1 + (int32_t)((x2 - x1) * factor);
        end_y = y1 + (int32_t)((y2 - y1) * factor);
        draw_line(layer, x1, y1, end_x, end_y, color, width);
    }
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

// ============================================================================
// Isometric Print Head Drawing
// ============================================================================
// Creates a Bambu-style 3D print head with:
// - Heater block (main body with gradient shading)
// - Heat break throat (narrower section)
// - Nozzle tip (tapered bottom)
// - Cooling fan hint (side detail)
// Uses isometric projection with gradients for 3D depth effect.

// Color manipulation helpers (similar to spool_canvas.cpp)
static lv_color_t ph_darken(lv_color_t c, uint8_t amt) {
    return lv_color_make(c.red > amt ? c.red - amt : 0, c.green > amt ? c.green - amt : 0,
                         c.blue > amt ? c.blue - amt : 0);
}

static lv_color_t ph_lighten(lv_color_t c, uint8_t amt) {
    return lv_color_make((c.red + amt > 255) ? 255 : c.red + amt,
                         (c.green + amt > 255) ? 255 : c.green + amt,
                         (c.blue + amt > 255) ? 255 : c.blue + amt);
}

static lv_color_t ph_blend(lv_color_t c1, lv_color_t c2, float factor) {
    factor = LV_CLAMP(factor, 0.0f, 1.0f);
    return lv_color_make((uint8_t)(c1.red + (c2.red - c1.red) * factor),
                         (uint8_t)(c1.green + (c2.green - c1.green) * factor),
                         (uint8_t)(c1.blue + (c2.blue - c1.blue) * factor));
}

// Draw animated filament tip (a glowing dot that moves along the path)
static void draw_filament_tip(lv_layer_t* layer, int32_t x, int32_t y, lv_color_t color,
                              int32_t radius) {
    // Outer glow (lighter, larger)
    lv_color_t glow_color = ph_lighten(color, 60);
    draw_sensor_dot(layer, x, y, glow_color, true, radius + 2);

    // Inner core (bright)
    lv_color_t core_color = ph_lighten(color, 100);
    draw_sensor_dot(layer, x, y, core_color, true, radius);
}

// ============================================================================
// Main Draw Callback
// ============================================================================

static void filament_path_draw_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    FilamentPathData* data = get_data(obj);
    if (!data)
        return;

    // Get widget dimensions
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    int32_t width = lv_area_get_width(&obj_coords);
    int32_t height = lv_area_get_height(&obj_coords);
    int32_t x_off = obj_coords.x1;
    int32_t y_off = obj_coords.y1;

    // Calculate Y positions
    int32_t entry_y = y_off + (int32_t)(height * ENTRY_Y_RATIO);
    int32_t prep_y = y_off + (int32_t)(height * PREP_Y_RATIO);
    int32_t merge_y = y_off + (int32_t)(height * MERGE_Y_RATIO);
    int32_t hub_y = y_off + (int32_t)(height * HUB_Y_RATIO);
    int32_t hub_h = (int32_t)(height * HUB_HEIGHT_RATIO);
    int32_t output_y = y_off + (int32_t)(height * OUTPUT_Y_RATIO);
    int32_t toolhead_y = y_off + (int32_t)(height * TOOLHEAD_Y_RATIO);
    int32_t nozzle_y = y_off + (int32_t)(height * NOZZLE_Y_RATIO);
    int32_t center_x = x_off + width / 2;

    // Colors from theme
    lv_color_t idle_color = data->color_idle;
    lv_color_t active_color = lv_color_hex(data->filament_color);
    lv_color_t hub_bg = data->color_hub_bg;
    lv_color_t hub_border = data->color_hub_border;
    lv_color_t nozzle_color = data->color_nozzle;

    // Error color with pulse effect - blend toward idle based on opacity
    lv_color_t error_color = data->color_error;
    if (data->error_pulse_active && data->error_pulse_opa < LV_OPA_COVER) {
        // Blend error color with a darker version for pulsing effect
        float blend_factor = (float)(LV_OPA_COVER - data->error_pulse_opa) /
                             (float)(LV_OPA_COVER - ERROR_PULSE_OPA_MIN);
        error_color = ph_blend(data->color_error, ph_darken(data->color_error, 80), blend_factor);
    }

    // Sizes from theme
    int32_t line_idle = data->line_width_idle;
    int32_t line_active = data->line_width_active;
    int32_t sensor_r = data->sensor_radius;

    // Determine which segment has error (if any)
    bool has_error = data->error_segment > 0;
    PathSegment error_seg = static_cast<PathSegment>(data->error_segment);
    PathSegment fil_seg = static_cast<PathSegment>(data->filament_segment);

    // Animation state
    bool is_animating = data->segment_anim_active;
    int anim_progress = data->anim_progress;
    PathSegment prev_seg = static_cast<PathSegment>(data->prev_segment);
    bool is_loading = (data->anim_direction == AnimDirection::LOADING);

    // ========================================================================
    // Draw lane lines (one per gate, from entry to merge point)
    // Shows all installed filaments' colors, not just the active gate
    // ========================================================================
    for (int i = 0; i < data->gate_count; i++) {
        int32_t gate_x = x_off + get_gate_x(i, data->gate_count, width);
        bool is_active_gate = (i == data->active_gate);

        // Determine line color and width for this gate's lane
        // Priority: active gate > per-gate filament state > idle
        lv_color_t lane_color = idle_color;
        int32_t lane_width = line_idle;
        bool has_filament = false;
        PathSegment gate_segment = PathSegment::NONE;

        if (is_active_gate && data->filament_segment > 0) {
            // Active gate - use active filament color
            has_filament = true;
            lane_color = active_color;
            lane_width = line_active;
            gate_segment = fil_seg;

            // Check for error in lane segments
            if (has_error && (error_seg == PathSegment::PREP || error_seg == PathSegment::LANE)) {
                lane_color = error_color;
            }
        } else if (i < FilamentPathData::MAX_GATES &&
                   data->gate_filament_states[i].segment != PathSegment::NONE) {
            // Non-active gate with installed filament - show its color to its sensor position
            has_filament = true;
            lane_color = lv_color_hex(data->gate_filament_states[i].color);
            lane_width = line_active;
            gate_segment = data->gate_filament_states[i].segment;
        }

        // For non-active gates with filament:
        // - Color the line FROM spool TO sensor (we know filament is here)
        // - Color the sensor dot (filament detected)
        // - Gray the line PAST sensor to merge (we don't know extent beyond sensor)
        bool is_non_active_with_filament = !is_active_gate && has_filament;

        // Line from entry to prep sensor: colored if filament present
        lv_color_t entry_line_color = has_filament ? lane_color : idle_color;
        int32_t entry_line_width = has_filament ? lane_width : line_idle;
        draw_vertical_line(layer, gate_x, entry_y, prep_y - sensor_r, entry_line_color,
                           entry_line_width);

        // Draw prep sensor dot (AFC topology shows these prominently)
        if (data->topology == 1) { // HUB topology
            bool prep_active = has_filament && is_segment_active(PathSegment::PREP, gate_segment);
            draw_sensor_dot(layer, gate_x, prep_y, prep_active ? lane_color : idle_color,
                            prep_active, sensor_r);
        }

        // Line from prep to merge: gray for non-active gates (don't imply extent past sensor)
        lv_color_t merge_line_color = is_non_active_with_filament ? idle_color : lane_color;
        int32_t merge_line_width = is_non_active_with_filament ? line_idle : lane_width;
        // For gates with no filament, use idle color
        if (!has_filament) {
            merge_line_color = idle_color;
            merge_line_width = line_idle;
        }
        draw_line(layer, gate_x, prep_y + sensor_r, center_x, merge_y, merge_line_color,
                  merge_line_width);
    }

    // ========================================================================
    // Draw bypass entry and path (right side, below spool area, direct to output)
    // ========================================================================
    {
        int32_t bypass_x = x_off + (int32_t)(width * BYPASS_X_RATIO);
        int32_t bypass_entry_y = y_off + (int32_t)(height * BYPASS_ENTRY_Y_RATIO);
        int32_t bypass_merge_y = y_off + (int32_t)(height * BYPASS_MERGE_Y_RATIO);

        // Determine bypass colors
        lv_color_t bypass_line_color = idle_color;
        int32_t bypass_line_width = line_idle;

        if (data->bypass_active) {
            bypass_line_color = lv_color_hex(data->bypass_color);
            bypass_line_width = line_active;
        }

        // Draw bypass entry point (below spool area)
        draw_sensor_dot(layer, bypass_x, bypass_entry_y, bypass_line_color, data->bypass_active,
                        sensor_r + 2);

        // Draw vertical line from bypass entry down to merge level
        draw_vertical_line(layer, bypass_x, bypass_entry_y + sensor_r + 2, bypass_merge_y,
                           bypass_line_color, bypass_line_width);

        // Draw horizontal line from bypass to center (joins at output_y level)
        draw_line(layer, bypass_x, bypass_merge_y, center_x, bypass_merge_y, bypass_line_color,
                  bypass_line_width);

        // Draw "Bypass" label above entry point
        if (data->label_font) {
            lv_draw_label_dsc_t label_dsc;
            lv_draw_label_dsc_init(&label_dsc);
            label_dsc.color = data->bypass_active ? bypass_line_color : data->color_text;
            label_dsc.font = data->label_font;
            label_dsc.align = LV_TEXT_ALIGN_CENTER;
            label_dsc.text = "Bypass";

            int32_t font_h = lv_font_get_line_height(data->label_font);
            lv_area_t label_area = {bypass_x - 40, bypass_entry_y - font_h - 4, bypass_x + 40,
                                    bypass_entry_y - 4};
            lv_draw_label(layer, &label_dsc, &label_area);
        }
    }

    // ========================================================================
    // Draw hub/selector section
    // ========================================================================
    {
        // Line from merge point to hub
        lv_color_t hub_line_color = idle_color;
        int32_t hub_line_width = line_idle;
        bool hub_has_filament = false;

        if (data->active_gate >= 0 && is_segment_active(PathSegment::HUB, fil_seg)) {
            hub_line_color = active_color;
            hub_line_width = line_active;
            hub_has_filament = true;
            if (has_error && error_seg == PathSegment::HUB) {
                hub_line_color = error_color;
            }
        }

        draw_vertical_line(layer, center_x, merge_y, hub_y - hub_h / 2, hub_line_color,
                           hub_line_width);

        // Hub box - tint background with filament color when filament passes through
        lv_color_t hub_bg_tinted = hub_bg;
        if (hub_has_filament) {
            // Subtle 33% blend of filament color into hub background
            hub_bg_tinted = ph_blend(hub_bg, active_color, 0.33f);
        }

        const char* hub_label = (data->topology == 0) ? "SELECTOR" : "HUB";
        draw_hub_box(layer, center_x, hub_y, data->hub_width, hub_h, hub_bg_tinted, hub_border,
                     data->color_text, data->label_font, data->border_radius, hub_label);
    }

    // ========================================================================
    // Draw output section (hub to toolhead)
    // ========================================================================
    {
        lv_color_t output_color = idle_color;
        int32_t output_width = line_idle;

        // Bypass or normal gate active?
        bool output_active = false;
        if (data->bypass_active) {
            // Bypass active - use bypass color for output path
            output_color = lv_color_hex(data->bypass_color);
            output_width = line_active;
            output_active = true;
        } else if (data->active_gate >= 0 && is_segment_active(PathSegment::OUTPUT, fil_seg)) {
            output_color = active_color;
            output_width = line_active;
            output_active = true;
            if (has_error && error_seg == PathSegment::OUTPUT) {
                output_color = error_color;
            }
        }

        // Hub output sensor
        int32_t hub_bottom = hub_y + hub_h / 2;
        draw_vertical_line(layer, center_x, hub_bottom, output_y - sensor_r, output_color,
                           output_width);

        draw_sensor_dot(layer, center_x, output_y, output_active ? output_color : idle_color,
                        output_active, sensor_r);
    }

    // ========================================================================
    // Draw toolhead section
    // ========================================================================
    {
        lv_color_t toolhead_color = idle_color;
        int32_t toolhead_width = line_idle;

        // Bypass or normal gate active?
        bool toolhead_active = false;
        if (data->bypass_active) {
            // Bypass active - use bypass color for toolhead path
            toolhead_color = lv_color_hex(data->bypass_color);
            toolhead_width = line_active;
            toolhead_active = true;
        } else if (data->active_gate >= 0 && is_segment_active(PathSegment::TOOLHEAD, fil_seg)) {
            toolhead_color = active_color;
            toolhead_width = line_active;
            toolhead_active = true;
            if (has_error && error_seg == PathSegment::TOOLHEAD) {
                toolhead_color = error_color;
            }
        }

        // Line from output sensor to toolhead sensor
        draw_vertical_line(layer, center_x, output_y + sensor_r, toolhead_y - sensor_r,
                           toolhead_color, toolhead_width);

        // Toolhead sensor
        draw_sensor_dot(layer, center_x, toolhead_y, toolhead_active ? toolhead_color : idle_color,
                        toolhead_active, sensor_r);
    }

    // ========================================================================
    // Draw nozzle
    // ========================================================================
    {
        lv_color_t noz_color = nozzle_color;

        // Bypass or normal gate active?
        if (data->bypass_active) {
            // Bypass active - use bypass color for nozzle
            noz_color = lv_color_hex(data->bypass_color);
        } else if (data->active_gate >= 0 && is_segment_active(PathSegment::NOZZLE, fil_seg)) {
            noz_color = active_color;
            if (has_error && error_seg == PathSegment::NOZZLE) {
                noz_color = error_color;
            }
        }

        // Line from toolhead sensor to extruder (adjust gap for tall extruder body)
        int32_t extruder_half_height = data->extruder_scale * 2; // Half of body_height
        draw_vertical_line(layer, center_x, toolhead_y + sensor_r, nozzle_y - extruder_half_height,
                           noz_color, line_active);

        // Extruder/print head icon (responsive size)
        if (data->use_faceted_toolhead) {
            draw_nozzle_faceted(layer, center_x, nozzle_y, noz_color, data->extruder_scale);
        } else {
            draw_nozzle_bambu(layer, center_x, nozzle_y, noz_color, data->extruder_scale);
        }
    }

    // ========================================================================
    // Draw animated filament tip (during segment transitions)
    // ========================================================================
    if (is_animating && data->active_gate >= 0) {
        // Calculate Y positions for each segment (same as above)
        // Map segment to Y position on the path
        auto get_segment_y = [&](PathSegment seg) -> int32_t {
            switch (seg) {
            case PathSegment::NONE:
            case PathSegment::SPOOL:
                return entry_y;
            case PathSegment::PREP:
                return prep_y;
            case PathSegment::LANE:
                return merge_y;
            case PathSegment::HUB:
                return hub_y;
            case PathSegment::OUTPUT:
                return output_y;
            case PathSegment::TOOLHEAD:
                return toolhead_y;
            case PathSegment::NOZZLE:
                return nozzle_y - data->extruder_scale * 2; // Top of extruder
            default:
                return entry_y;
            }
        };

        int32_t from_y = get_segment_y(prev_seg);
        int32_t to_y = get_segment_y(fil_seg);

        // Interpolate position based on animation progress
        float progress_factor = anim_progress / 100.0f;
        int32_t tip_y = from_y + (int32_t)((to_y - from_y) * progress_factor);

        // Calculate X position - for lanes, interpolate from gate to center
        int32_t tip_x = center_x;
        if ((prev_seg <= PathSegment::PREP || fil_seg <= PathSegment::PREP) &&
            data->active_gate >= 0) {
            int32_t gate_x = x_off + get_gate_x(data->active_gate, data->gate_count, width);
            if (is_loading) {
                // Moving from gate toward center
                if (prev_seg <= PathSegment::PREP && fil_seg > PathSegment::PREP) {
                    // Transitioning from lane to hub area - interpolate X
                    tip_x = gate_x + (int32_t)((center_x - gate_x) * progress_factor);
                } else if (prev_seg <= PathSegment::PREP) {
                    tip_x = gate_x;
                }
            } else {
                // Unloading - moving from center toward gate
                if (fil_seg <= PathSegment::PREP && prev_seg > PathSegment::PREP) {
                    tip_x = center_x + (int32_t)((gate_x - center_x) * progress_factor);
                } else if (fil_seg <= PathSegment::PREP) {
                    tip_x = gate_x;
                }
            }
        }

        // Draw the glowing filament tip
        draw_filament_tip(layer, tip_x, tip_y, active_color, sensor_r);
    }

    spdlog::trace("[FilamentPath] Draw: gates={}, active={}, segment={}, anim={}", data->gate_count,
                  data->active_gate, data->filament_segment, is_animating ? anim_progress : -1);
}

// ============================================================================
// Event Handlers
// ============================================================================

static void filament_path_click_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    FilamentPathData* data = get_data(obj);
    if (!data)
        return;

    lv_point_t point;
    lv_indev_t* indev = lv_indev_active();
    lv_indev_get_point(indev, &point);

    // Get widget dimensions
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    int32_t width = lv_area_get_width(&obj_coords);
    int32_t height = lv_area_get_height(&obj_coords);
    int32_t x_off = obj_coords.x1;
    int32_t y_off = obj_coords.y1;

    // Check if click is in the entry area (top portion)
    int32_t entry_y = y_off + (int32_t)(height * ENTRY_Y_RATIO);
    int32_t prep_y = y_off + (int32_t)(height * PREP_Y_RATIO);

    if (point.y < entry_y - 10 || point.y > prep_y + 20)
        return; // Click not in entry area

    // Check if bypass entry was clicked (right side)
    if (data->bypass_callback) {
        int32_t bypass_x = x_off + (int32_t)(width * BYPASS_X_RATIO);
        if (abs(point.x - bypass_x) < 25) {
            spdlog::debug("[FilamentPath] Bypass entry clicked");
            data->bypass_callback(data->bypass_user_data);
            return;
        }
    }

    // Find which gate was clicked
    if (data->gate_callback) {
        for (int i = 0; i < data->gate_count; i++) {
            int32_t gate_x = x_off + get_gate_x(i, data->gate_count, width);
            if (abs(point.x - gate_x) < 20) {
                spdlog::debug("[FilamentPath] Gate {} clicked", i);
                data->gate_callback(i, data->gate_user_data);
                return;
            }
        }
    }
}

static void filament_path_delete_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    auto it = s_registry.find(obj);
    if (it != s_registry.end()) {
        // Stop any running animations before deleting
        FilamentPathData* data = it->second;
        if (data) {
            lv_anim_delete(obj, segment_anim_cb);
            lv_anim_delete(obj, error_pulse_anim_cb);
        }
        delete data;
        s_registry.erase(it);
    }
}

// ============================================================================
// XML Widget Interface
// ============================================================================

static void* filament_path_xml_create(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);

    void* parent = lv_xml_state_get_parent(state);
    lv_obj_t* obj = lv_obj_create(static_cast<lv_obj_t*>(parent));
    if (!obj)
        return nullptr;

    auto* data = new FilamentPathData();
    s_registry[obj] = data;

    // Load theme-aware colors, fonts, and sizes
    load_theme_colors(data);

    // Configure object
    lv_obj_set_size(obj, DEFAULT_WIDTH, DEFAULT_HEIGHT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);

    // Register event handlers
    lv_obj_add_event_cb(obj, filament_path_draw_cb, LV_EVENT_DRAW_POST, nullptr);
    lv_obj_add_event_cb(obj, filament_path_click_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(obj, filament_path_delete_cb, LV_EVENT_DELETE, nullptr);

    spdlog::debug("[FilamentPath] Created widget");
    return obj;
}

static void filament_path_xml_apply(lv_xml_parser_state_t* state, const char** attrs) {
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

        if (strcmp(name, "topology") == 0) {
            if (strcmp(value, "linear") == 0 || strcmp(value, "0") == 0) {
                data->topology = 0;
            } else {
                data->topology = 1; // default to hub
            }
            needs_redraw = true;
        } else if (strcmp(name, "gate_count") == 0) {
            data->gate_count = LV_CLAMP(atoi(value), 1, 16);
            needs_redraw = true;
        } else if (strcmp(name, "active_gate") == 0) {
            data->active_gate = atoi(value);
            needs_redraw = true;
        } else if (strcmp(name, "filament_segment") == 0) {
            data->filament_segment = LV_CLAMP(atoi(value), 0, PATH_SEGMENT_COUNT - 1);
            needs_redraw = true;
        } else if (strcmp(name, "error_segment") == 0) {
            data->error_segment = LV_CLAMP(atoi(value), 0, PATH_SEGMENT_COUNT - 1);
            needs_redraw = true;
        } else if (strcmp(name, "anim_progress") == 0) {
            data->anim_progress = LV_CLAMP(atoi(value), 0, 100);
            needs_redraw = true;
        } else if (strcmp(name, "filament_color") == 0) {
            data->filament_color = strtoul(value, nullptr, 0);
            needs_redraw = true;
        } else if (strcmp(name, "bypass_active") == 0) {
            data->bypass_active = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
            needs_redraw = true;
        } else if (strcmp(name, "faceted_toolhead") == 0) {
            data->use_faceted_toolhead = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
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

void ui_filament_path_canvas_register(void) {
    lv_xml_register_widget("filament_path_canvas", filament_path_xml_create,
                           filament_path_xml_apply);
    spdlog::info("[FilamentPath] Registered filament_path_canvas widget with XML system");
}

lv_obj_t* ui_filament_path_canvas_create(lv_obj_t* parent) {
    if (!parent) {
        spdlog::error("[FilamentPath] Cannot create: parent is null");
        return nullptr;
    }

    lv_obj_t* obj = lv_obj_create(parent);
    if (!obj) {
        spdlog::error("[FilamentPath] Failed to create object");
        return nullptr;
    }

    auto* data = new FilamentPathData();
    s_registry[obj] = data;

    // Load theme-aware colors, fonts, and sizes
    load_theme_colors(data);

    // Configure object
    lv_obj_set_size(obj, DEFAULT_WIDTH, DEFAULT_HEIGHT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);

    // Register event handlers
    lv_obj_add_event_cb(obj, filament_path_draw_cb, LV_EVENT_DRAW_POST, nullptr);
    lv_obj_add_event_cb(obj, filament_path_click_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(obj, filament_path_delete_cb, LV_EVENT_DELETE, nullptr);

    spdlog::debug("[FilamentPath] Created widget programmatically");
    return obj;
}

void ui_filament_path_canvas_set_topology(lv_obj_t* obj, int topology) {
    auto* data = get_data(obj);
    if (data) {
        data->topology = topology;
        lv_obj_invalidate(obj);
    }
}

void ui_filament_path_canvas_set_gate_count(lv_obj_t* obj, int count) {
    auto* data = get_data(obj);
    if (data) {
        data->gate_count = LV_CLAMP(count, 1, 16);
        lv_obj_invalidate(obj);
    }
}

void ui_filament_path_canvas_set_active_gate(lv_obj_t* obj, int gate) {
    auto* data = get_data(obj);
    if (data) {
        data->active_gate = gate;
        lv_obj_invalidate(obj);
    }
}

void ui_filament_path_canvas_set_filament_segment(lv_obj_t* obj, int segment) {
    auto* data = get_data(obj);
    if (!data)
        return;

    int new_segment = LV_CLAMP(segment, 0, PATH_SEGMENT_COUNT - 1);
    int old_segment = data->filament_segment;

    if (new_segment != old_segment) {
        // Start animation from old to new segment
        start_segment_animation(obj, data, old_segment, new_segment);
        data->filament_segment = new_segment;
        spdlog::debug("[FilamentPath] Segment changed: {} -> {} (animating)", old_segment,
                      new_segment);
    }

    lv_obj_invalidate(obj);
}

void ui_filament_path_canvas_set_error_segment(lv_obj_t* obj, int segment) {
    auto* data = get_data(obj);
    if (!data)
        return;

    int new_error = LV_CLAMP(segment, 0, PATH_SEGMENT_COUNT - 1);
    int old_error = data->error_segment;

    data->error_segment = new_error;

    // Start or stop error pulse animation
    if (new_error > 0 && old_error == 0) {
        // Error appeared - start pulsing
        start_error_pulse(obj, data);
        spdlog::debug("[FilamentPath] Error at segment {} - starting pulse", new_error);
    } else if (new_error == 0 && old_error > 0) {
        // Error cleared - stop pulsing
        stop_error_pulse(obj, data);
        spdlog::debug("[FilamentPath] Error cleared - stopping pulse");
    }

    lv_obj_invalidate(obj);
}

void ui_filament_path_canvas_set_anim_progress(lv_obj_t* obj, int progress) {
    auto* data = get_data(obj);
    if (data) {
        data->anim_progress = LV_CLAMP(progress, 0, 100);
        lv_obj_invalidate(obj);
    }
}

void ui_filament_path_canvas_set_filament_color(lv_obj_t* obj, uint32_t color) {
    auto* data = get_data(obj);
    if (data) {
        data->filament_color = color;
        lv_obj_invalidate(obj);
    }
}

void ui_filament_path_canvas_refresh(lv_obj_t* obj) {
    lv_obj_invalidate(obj);
}

void ui_filament_path_canvas_set_gate_callback(lv_obj_t* obj, filament_path_gate_cb_t cb,
                                               void* user_data) {
    auto* data = get_data(obj);
    if (data) {
        data->gate_callback = cb;
        data->gate_user_data = user_data;
    }
}

void ui_filament_path_canvas_animate_segment(lv_obj_t* obj, int from_segment, int to_segment) {
    auto* data = get_data(obj);
    if (!data)
        return;

    int from = LV_CLAMP(from_segment, 0, PATH_SEGMENT_COUNT - 1);
    int to = LV_CLAMP(to_segment, 0, PATH_SEGMENT_COUNT - 1);

    if (from != to) {
        start_segment_animation(obj, data, from, to);
        data->filament_segment = to;
    }
}

bool ui_filament_path_canvas_is_animating(lv_obj_t* obj) {
    auto* data = get_data(obj);
    if (!data)
        return false;

    return data->segment_anim_active || data->error_pulse_active;
}

void ui_filament_path_canvas_stop_animations(lv_obj_t* obj) {
    auto* data = get_data(obj);
    if (!data)
        return;

    stop_segment_animation(obj, data);
    stop_error_pulse(obj, data);
    lv_obj_invalidate(obj);
}

void ui_filament_path_canvas_set_gate_filament(lv_obj_t* obj, int gate_index, int segment,
                                               uint32_t color) {
    auto* data = get_data(obj);
    if (!data || gate_index < 0 || gate_index >= FilamentPathData::MAX_GATES)
        return;

    auto& state = data->gate_filament_states[gate_index];
    PathSegment new_segment = static_cast<PathSegment>(segment);

    if (state.segment != new_segment || state.color != color) {
        state.segment = new_segment;
        state.color = color;
        spdlog::trace("[FilamentPath] Gate {} filament: segment={}, color=0x{:06X}", gate_index,
                      segment, color);
        lv_obj_invalidate(obj);
    }
}

void ui_filament_path_canvas_clear_gate_filaments(lv_obj_t* obj) {
    auto* data = get_data(obj);
    if (!data)
        return;

    bool changed = false;
    for (int i = 0; i < FilamentPathData::MAX_GATES; i++) {
        if (data->gate_filament_states[i].segment != PathSegment::NONE) {
            data->gate_filament_states[i].segment = PathSegment::NONE;
            data->gate_filament_states[i].color = 0x808080;
            changed = true;
        }
    }

    if (changed) {
        spdlog::trace("[FilamentPath] Cleared all gate filament states");
        lv_obj_invalidate(obj);
    }
}

void ui_filament_path_canvas_set_bypass_active(lv_obj_t* obj, bool active) {
    auto* data = get_data(obj);
    if (!data)
        return;

    if (data->bypass_active != active) {
        data->bypass_active = active;
        spdlog::debug("[FilamentPath] Bypass mode: {}", active ? "active" : "inactive");
        lv_obj_invalidate(obj);
    }
}

void ui_filament_path_canvas_set_bypass_callback(lv_obj_t* obj, filament_path_bypass_cb_t cb,
                                                 void* user_data) {
    auto* data = get_data(obj);
    if (data) {
        data->bypass_callback = cb;
        data->bypass_user_data = user_data;
    }
}

void ui_filament_path_canvas_set_faceted_toolhead(lv_obj_t* obj, bool faceted) {
    auto* data = get_data(obj);
    if (!data)
        return;

    if (data->use_faceted_toolhead != faceted) {
        data->use_faceted_toolhead = faceted;
        spdlog::debug("[FilamentPath] Toolhead style: {}", faceted ? "faceted" : "bambu");
        lv_obj_invalidate(obj);
    }
}
