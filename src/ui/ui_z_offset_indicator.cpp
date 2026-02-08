// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_z_offset_indicator.h"

#include "ui_update_queue.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"
#include "nozzle_renderer_bambu.h"
#include "nozzle_renderer_faceted.h"
#include "settings_manager.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

// ============================================================================
// Widget Data
// ============================================================================

struct ZOffsetIndicatorData {
    int32_t current_pos = 0;   // Current animated position (0.1 micron units for smooth anim)
    int32_t target_pos = 0;    // Target position (0.1 micron units)
    int32_t arrow_opacity = 0; // 0-255, animated for direction flash
    int arrow_direction = 0;   // +1 (farther/up) or -1 (closer/down)
    bool use_faceted_toolhead = false; // Which nozzle renderer to use
};

// Scale range in microns (±2mm)
static constexpr int SCALE_RANGE_MICRONS = 2000;

// ============================================================================
// Animation Callbacks (forward declarations)
// ============================================================================

static void position_anim_cb(void* var, int32_t value);
static void arrow_anim_cb(void* var, int32_t value);

// ============================================================================
// Drawing
// ============================================================================

/// Convert microns to a Y pixel position on the vertical scale.
/// +2000 microns (farther from bed) is at the top, -2000 (closer) at bottom.
static int32_t microns_to_scale_y(int microns, int32_t scale_top, int32_t scale_bottom) {
    int clamped = microns;
    if (clamped > SCALE_RANGE_MICRONS)
        clamped = SCALE_RANGE_MICRONS;
    if (clamped < -SCALE_RANGE_MICRONS)
        clamped = -SCALE_RANGE_MICRONS;
    // +2000 -> scale_top, -2000 -> scale_bottom, 0 -> center
    int32_t center = (scale_top + scale_bottom) / 2;
    int32_t half_range = (scale_bottom - scale_top) / 2;
    return center - (clamped * half_range) / SCALE_RANGE_MICRONS;
}

static void indicator_draw_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    auto* data = static_cast<ZOffsetIndicatorData*>(lv_obj_get_user_data(obj));
    if (!data)
        return;

    // Get widget dimensions
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    int32_t w = lv_area_get_width(&coords);
    int32_t h = lv_area_get_height(&coords);

    // Layout: scale on left ~30%, nozzle on right ~70%
    // Vertical margins for the scale
    int32_t margin_v = h / 10;
    int32_t scale_top = coords.y1 + margin_v;
    int32_t scale_bottom = coords.y1 + h - margin_v;
    int32_t scale_x = coords.x1 + w / 4; // Vertical line at 25% from left

    lv_color_t muted_color = theme_manager_get_color("text_muted");
    lv_color_t text_color = theme_manager_get_color("text");
    lv_color_t primary_color = theme_manager_get_color("primary");

    // --- Vertical scale line ---
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = muted_color;
    line_dsc.width = 2;
    line_dsc.round_start = true;
    line_dsc.round_end = true;
    line_dsc.p1.x = scale_x;
    line_dsc.p1.y = scale_top;
    line_dsc.p2.x = scale_x;
    line_dsc.p2.y = scale_bottom;
    lv_draw_line(layer, &line_dsc);

    // --- Tick marks and labels at -2, -1, 0, +1, +2 ---
    const int tick_values[] = {2, 1, 0, -1, -2};
    int32_t tick_half_w = w / 16; // Responsive tick width
    const lv_font_t* font = lv_font_get_default();
    int32_t font_h = lv_font_get_line_height(font);

    for (int val : tick_values) {
        int32_t y = microns_to_scale_y(val * 1000, scale_top, scale_bottom);

        // Tick mark
        lv_draw_line_dsc_t tick_dsc;
        lv_draw_line_dsc_init(&tick_dsc);
        tick_dsc.color = muted_color;
        tick_dsc.width = (val == 0) ? 2 : 1; // Bolder zero line
        tick_dsc.p1.x = scale_x - tick_half_w;
        tick_dsc.p1.y = y;
        tick_dsc.p2.x = scale_x + tick_half_w;
        tick_dsc.p2.y = y;
        lv_draw_line(layer, &tick_dsc);

        // Label to the left of the tick
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d", val);
        lv_draw_label_dsc_t lbl_dsc;
        lv_draw_label_dsc_init(&lbl_dsc);
        lbl_dsc.color = muted_color;
        lbl_dsc.font = font;
        lbl_dsc.align = LV_TEXT_ALIGN_RIGHT;
        lbl_dsc.text = buf;
        lv_area_t lbl_area = {coords.x1 + 2, y - font_h / 2, scale_x - tick_half_w - 4,
                              y + font_h / 2};
        lv_draw_label(layer, &lbl_dsc, &lbl_area);
    }

    // --- Position marker on scale ---
    // current_pos is in 0.1-micron units, convert to microns for Y mapping
    int32_t current_microns = data->current_pos / 10;
    int32_t marker_y = microns_to_scale_y(current_microns, scale_top, scale_bottom);

    // Triangular marker pointing right (drawn as a small filled area)
    int32_t tri_size = LV_MAX(4, h / 20);
    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);
    tri_dsc.color = primary_color;
    tri_dsc.opa = LV_OPA_COVER;
    tri_dsc.p[0].x = scale_x + 3;
    tri_dsc.p[0].y = marker_y;
    tri_dsc.p[1].x = scale_x + 3 + tri_size;
    tri_dsc.p[1].y = marker_y - tri_size;
    tri_dsc.p[2].x = scale_x + 3 + tri_size;
    tri_dsc.p[2].y = marker_y + tri_size;
    lv_draw_triangle(layer, &tri_dsc);

    // --- Nozzle icon to the right of the scale ---
    int32_t nozzle_cx = coords.x1 + (w * 5) / 8; // 62.5% from left
    int32_t nozzle_scale = LV_MAX(6, h / 8);     // Prominent nozzle
    lv_color_t nozzle_color = theme_manager_get_color("text");

    if (data->use_faceted_toolhead) {
        draw_nozzle_faceted(layer, nozzle_cx, marker_y, nozzle_color, nozzle_scale);
    } else {
        draw_nozzle_bambu(layer, nozzle_cx, marker_y, nozzle_color, nozzle_scale);
    }

    // --- Direction arrow flash (to the right of nozzle) ---
    if (data->arrow_opacity > 0) {
        lv_draw_label_dsc_t arrow_dsc;
        lv_draw_label_dsc_init(&arrow_dsc);
        arrow_dsc.color = text_color;
        arrow_dsc.opa = static_cast<lv_opa_t>(data->arrow_opacity);
        arrow_dsc.align = LV_TEXT_ALIGN_CENTER;
        arrow_dsc.font = font;
        arrow_dsc.text = (data->arrow_direction > 0) ? LV_SYMBOL_UP : LV_SYMBOL_DOWN;

        int32_t arrow_x = nozzle_cx + nozzle_scale * 4;
        lv_area_t arrow_area = {arrow_x - 10, marker_y - font_h / 2, arrow_x + 10,
                                marker_y + font_h / 2};
        lv_draw_label(layer, &arrow_dsc, &arrow_area);
    }
}

// ============================================================================
// Animation Callbacks
// ============================================================================

static void position_anim_cb(void* var, int32_t value) {
    lv_obj_t* obj = static_cast<lv_obj_t*>(var);
    auto* data = static_cast<ZOffsetIndicatorData*>(lv_obj_get_user_data(obj));
    if (!data)
        return;

    data->current_pos = value;

    // Defer invalidation to avoid calling during render phase
    ui_async_call(
        [](void* obj_ptr) {
            auto* o = static_cast<lv_obj_t*>(obj_ptr);
            if (lv_obj_is_valid(o)) {
                lv_obj_invalidate(o);
            }
        },
        obj);
}

static void arrow_anim_cb(void* var, int32_t value) {
    lv_obj_t* obj = static_cast<lv_obj_t*>(var);
    auto* data = static_cast<ZOffsetIndicatorData*>(lv_obj_get_user_data(obj));
    if (!data)
        return;

    data->arrow_opacity = value;

    // Defer invalidation to avoid calling during render phase
    ui_async_call(
        [](void* obj_ptr) {
            auto* o = static_cast<lv_obj_t*>(obj_ptr);
            if (lv_obj_is_valid(o)) {
                lv_obj_invalidate(o);
            }
        },
        obj);
}

// ============================================================================
// Delete Callback
// ============================================================================

static void indicator_delete_cb(lv_event_t* e) {
    lv_obj_t* obj = (lv_obj_t*)lv_event_get_target(e);
    lv_anim_delete(obj, position_anim_cb);
    lv_anim_delete(obj, arrow_anim_cb);
    auto* data = static_cast<ZOffsetIndicatorData*>(lv_obj_get_user_data(obj));
    delete data;
    lv_obj_set_user_data(obj, nullptr);
}

// ============================================================================
// Public API
// ============================================================================

void ui_z_offset_indicator_set_value(lv_obj_t* obj, int microns) {
    if (!obj)
        return;
    auto* data = static_cast<ZOffsetIndicatorData*>(lv_obj_get_user_data(obj));
    if (!data)
        return;

    // Store in 0.1-micron units for smooth animation interpolation
    int32_t new_target = microns * 10;
    data->target_pos = new_target;

    // Stop any existing position animation
    lv_anim_delete(obj, position_anim_cb);

    if (SettingsManager::instance().get_animations_enabled()) {
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, obj);
        lv_anim_set_values(&anim, data->current_pos, new_target);
        lv_anim_set_duration(&anim, 200);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&anim, position_anim_cb);
        lv_anim_start(&anim);
    } else {
        // Jump directly to target
        data->current_pos = new_target;
        lv_obj_invalidate(obj);
    }

    spdlog::trace("[ZOffsetIndicator] Set value: {} microns", microns);
}

void ui_z_offset_indicator_flash_direction(lv_obj_t* obj, int direction) {
    if (!obj)
        return;
    auto* data = static_cast<ZOffsetIndicatorData*>(lv_obj_get_user_data(obj));
    if (!data)
        return;

    data->arrow_direction = direction;

    // Stop any existing arrow animation
    lv_anim_delete(obj, arrow_anim_cb);

    if (SettingsManager::instance().get_animations_enabled()) {
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, obj);
        lv_anim_set_values(&anim, 255, 0); // Fade from full opacity to transparent
        lv_anim_set_duration(&anim, 400);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
        lv_anim_set_exec_cb(&anim, arrow_anim_cb);
        lv_anim_start(&anim);
    } else {
        // No animation - skip arrow entirely
        data->arrow_opacity = 0;
    }

    spdlog::trace("[ZOffsetIndicator] Flash direction: {}", direction > 0 ? "up" : "down");
}

// ============================================================================
// XML Widget Registration
// ============================================================================

static void* z_offset_indicator_xml_create(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);

    void* parent = lv_xml_state_get_parent(state);
    lv_obj_t* obj = lv_obj_create((lv_obj_t*)parent);

    if (!obj) {
        spdlog::error("[ZOffsetIndicator] Failed to create lv_obj");
        return nullptr;
    }

    // Set default size (height is a fallback — prefer flex_grow in XML for responsiveness)
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(15));
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    // Remove default styles and make transparent
    lv_obj_remove_style_all(obj);

    // Allocate and attach widget data
    auto* data = new ZOffsetIndicatorData{};
    data->use_faceted_toolhead = false;
    lv_obj_set_user_data(obj, data);

    // Register draw and delete callbacks
    // NOTE: lv_obj_add_event_cb() is appropriate here - this is a custom widget, not a UI button
    lv_obj_add_event_cb(obj, indicator_draw_cb, LV_EVENT_DRAW_POST, nullptr);
    lv_obj_add_event_cb(obj, indicator_delete_cb, LV_EVENT_DELETE, nullptr);

    spdlog::trace("[ZOffsetIndicator] Created widget");
    return (void*)obj;
}

void ui_z_offset_indicator_register(void) {
    lv_xml_register_widget("z_offset_indicator", z_offset_indicator_xml_create, lv_xml_obj_apply);
    spdlog::trace("[ZOffsetIndicator] Registered <z_offset_indicator> widget");
}
