// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_spool_canvas.h"

#include "ui_utils.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"
#include "theme_manager.h"
#include "ui/ams_drawing_utils.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <unordered_map>

// Geometry constants for Bambu-style 3D spool (SIDE VIEW)
// Spool axis is HORIZONTAL - we view from an angle
// Shows: back flange (left), filament cylinder (middle), front flange (right), hub hole (center)
static constexpr float FLANGE_RADIUS = 0.42f; // Flange radius (vertical)
static constexpr float ELLIPSE_RATIO =
    0.45f;                                  // Horizontal compression (narrower = more angled view)
static constexpr float HUB_RADIUS = 0.10f;  // Center hub hole radius
static constexpr float SPOOL_DEPTH = 0.35f; // Depth/width of spool (distance between flanges)
static constexpr int32_t DEFAULT_SIZE = 64;
static constexpr uint32_t DEFAULT_COLOR = 0xE0E0E0; // Default white/light filament

// Note: Spool body colors now come from theme tokens in globals.xml:
// - spool_body: Front flange color
// - spool_body_shade: Back flange color (darker shade)
// - spool_hub_top, spool_hub_bottom: Center hub gradient

struct SpoolCanvasData {
    lv_obj_t* canvas = nullptr;
    lv_draw_buf_t* draw_buf = nullptr;
    int32_t size = DEFAULT_SIZE;
    lv_color_t color = lv_color_hex(DEFAULT_COLOR);
    float fill_level = 1.0f;
};

static std::unordered_map<lv_obj_t*, SpoolCanvasData*> s_registry;

static SpoolCanvasData* get_data(lv_obj_t* obj) {
    auto it = s_registry.find(obj);
    return (it != s_registry.end()) ? it->second : nullptr;
}

// Draw ellipse with vertical gradient (top_color at top, bottom_color at bottom)
// Includes coverage-based anti-aliasing at left/right edges
static void draw_gradient_ellipse(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t rx, int32_t ry,
                                  lv_color_t top_color, lv_color_t bottom_color) {
    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);

    for (int32_t y = -ry; y <= ry; y++) {
        float y_norm = (float)y / (float)ry;
        float x_extent = rx * sqrtf(1.0f - y_norm * y_norm);

        // Gradient factor: 0.0 at top (-ry), 1.0 at bottom (+ry)
        float gradient_factor = (float)(y + ry) / (float)(2 * ry);
        gradient_factor = sqrtf(gradient_factor); // Quick transition from light to dark
        fill_dsc.color = ams_draw::blend_color(top_color, bottom_color, gradient_factor);

        // Handle pole pixels (very narrow scanlines near top/bottom)
        if (x_extent < 0.5f) {
            // Draw single center pixel with proportional opacity
            float pole_opa = (x_extent > 0.01f) ? (x_extent * 2.0f) : 0.3f;
            fill_dsc.opa = (lv_opa_t)(pole_opa * 255.0f);
            lv_area_t pole_pixel = {cx, cy + y, cx, cy + y};
            lv_draw_fill(layer, &fill_dsc, &pole_pixel);
            continue;
        }

        // Calculate integer bounds and fractional coverage
        int32_t x_inner = (int32_t)x_extent;
        float x_frac = x_extent - (float)x_inner;

        // Draw anti-aliased left/right edge pixels
        if (x_frac > 0.01f) {
            fill_dsc.opa = (lv_opa_t)(x_frac * 255.0f);
            lv_area_t left_edge = {cx - x_inner - 1, cy + y, cx - x_inner - 1, cy + y};
            lv_draw_fill(layer, &fill_dsc, &left_edge);
            lv_area_t right_edge = {cx + x_inner + 1, cy + y, cx + x_inner + 1, cy + y};
            lv_draw_fill(layer, &fill_dsc, &right_edge);
        }

        // Draw fully opaque interior
        if (x_inner > 0) {
            fill_dsc.opa = LV_OPA_COVER;
            lv_area_t line_area = {cx - x_inner, cy + y, cx + x_inner, cy + y};
            lv_draw_fill(layer, &fill_dsc, &line_area);
        }
    }
}

// Draw rectangle with vertical gradient (top_color at top, bottom_color at bottom)
static void draw_gradient_rect(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                               lv_color_t top_color, lv_color_t bottom_color) {
    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);
    fill_dsc.opa = LV_OPA_COVER;

    int32_t height = y2 - y1;
    if (height <= 0)
        return;

    // Draw scanline by scanline with gradient
    for (int32_t y = y1; y <= y2; y++) {
        float gradient_factor = (float)(y - y1) / (float)height;
        gradient_factor = sqrtf(gradient_factor); // Quick transition from light to dark
        fill_dsc.color = ams_draw::blend_color(top_color, bottom_color, gradient_factor);

        lv_area_t line = {x1, y, x2, y};
        lv_draw_fill(layer, &fill_dsc, &line);
    }
}

// Draw a highlight edge along the LEFT side of an ellipse (simulates 3D thickness)
// width_px: how many pixels wide the highlight band is
static void draw_ellipse_left_edge(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t rx,
                                   int32_t ry, lv_color_t top_color, lv_color_t bottom_color,
                                   int32_t width_px) {
    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);

    // Draw left edge highlight following ellipse curvature
    for (int32_t y = -ry; y <= ry; y++) {
        float y_norm = (float)y / (float)ry;
        float x_extent = rx * sqrtf(1.0f - y_norm * y_norm);
        if (x_extent < 0.5f)
            continue;

        // Gradient factor for vertical shading (same curve as main ellipse)
        float gradient_factor = (float)(y + ry) / (float)(2 * ry);
        gradient_factor = sqrtf(gradient_factor); // Quick transition from light to dark
        fill_dsc.color = ams_draw::blend_color(top_color, bottom_color, gradient_factor);
        fill_dsc.opa = LV_OPA_COVER;

        // Draw only the leftmost pixels (the edge highlight)
        // Use floorf to align with ellipse edge (truncation causes 1px offset at middle)
        int32_t left_edge = cx - (int32_t)floorf(x_extent + 0.5f);
        int32_t right_edge = left_edge + width_px - 1;
        if (right_edge > cx)
            right_edge = cx; // Don't go past center

        lv_area_t edge = {left_edge, cy + y, right_edge, cy + y};
        lv_draw_fill(layer, &fill_dsc, &edge);
    }
}

static void redraw_spool(SpoolCanvasData* data) {
    if (!data || !data->canvas || !data->draw_buf)
        return;

    int32_t size = data->size;
    int32_t cy = size / 2; // Vertical center

    // Calculate dimensions - vertical radius and horizontal (compressed) radius
    int32_t flange_ry = (int32_t)(size * FLANGE_RADIUS);      // Vertical radius
    int32_t flange_rx = (int32_t)(flange_ry * ELLIPSE_RATIO); // Horizontal (narrower)
    int32_t hub_ry = (int32_t)(size * HUB_RADIUS);
    int32_t hub_rx = (int32_t)(hub_ry * ELLIPSE_RATIO);
    int32_t spool_width = (int32_t)(size * SPOOL_DEPTH);

    // X positions for left (back) and right (front) flanges
    int32_t center_x = size / 2;
    int32_t left_x = center_x - spool_width / 2;  // Left side (back flange)
    int32_t right_x = center_x + spool_width / 2; // Right side (front flange)

    // Fill level determines wound filament radius
    // Max filament is smaller than flange so flanges always show as "taller"
    float fill = LV_CLAMP(data->fill_level, 0.0f, 1.0f);
    int32_t max_filament_ry = (int32_t)(flange_ry * 0.85f); // Flanges 15% taller than full filament
    int32_t filament_ry = hub_ry + (int32_t)((max_filament_ry - hub_ry) * fill);
    int32_t filament_rx = (int32_t)(filament_ry * ELLIPSE_RATIO);

    // Colors (from theme tokens)
    lv_color_t back_color = theme_manager_get_color("spool_body_shade");
    lv_color_t front_color = theme_manager_get_color("spool_body");
    lv_color_t filament_color = data->color;
    lv_color_t filament_side = ams_draw::darken_color(filament_color, 30);

    // Clear canvas
    lv_canvas_fill_bg(data->canvas, lv_color_black(), LV_OPA_TRANSP);

    lv_layer_t layer;
    lv_canvas_init_layer(data->canvas, &layer);

    // ========================================
    // STEP 1: Draw BACK FLANGE (left side) with gradient + edge highlight
    // ========================================
    {
        lv_color_t bf_light = ams_draw::lighten_color(back_color, 40);
        lv_color_t bf_dark = ams_draw::darken_color(back_color, 25);
        // Main flange ellipse with gradient
        draw_gradient_ellipse(&layer, left_x, cy, flange_rx, flange_ry, bf_light, bf_dark);
        // Edge highlight on left side (gives 3D thickness illusion)
        // Dramatic gradient: very bright at top, dark at bottom
        lv_color_t edge_light = ams_draw::lighten_color(back_color, 100);
        lv_color_t edge_dark = ams_draw::darken_color(back_color, 40);
        draw_ellipse_left_edge(&layer, left_x, cy, flange_rx, flange_ry, edge_light, edge_dark, 2);
    }

    // ========================================
    // STEP 2: Draw complete FILAMENT cylinder
    // Back ellipse + rectangle body + front ellipse
    // Gradient: lighter at top (lit), darker at bottom (shadow)
    // ========================================
    if (fill > 0.01f) {
        // Gradient colors for 3D lighting effect (stronger gradient)
        lv_color_t fil_light = ams_draw::lighten_color(filament_side, 70); // Top: much brighter
        lv_color_t fil_dark = ams_draw::darken_color(filament_side, 35);   // Bottom: darker

        // 2a: Back face ellipse (with gradient)
        draw_gradient_ellipse(&layer, left_x, cy, filament_rx, filament_ry, fil_light, fil_dark);

        // 2b: Rectangle body connecting the two faces (with gradient)
        int32_t fil_top = cy - filament_ry;
        int32_t fil_bottom = cy + filament_ry;
        draw_gradient_rect(&layer, left_x, fil_top, right_x, fil_bottom, fil_light, fil_dark);

        // 2c: Front face ellipse (will be covered by front flange anyway)
        lv_color_t front_light = ams_draw::lighten_color(filament_color, 70);
        lv_color_t front_dark = ams_draw::darken_color(filament_color, 35);
        draw_gradient_ellipse(&layer, right_x, cy, filament_rx, filament_ry, front_light,
                              front_dark);
    }

    // ========================================
    // STEP 3: Draw FRONT FLANGE (right side) with gradient + edge highlight
    // ========================================
    {
        lv_color_t ff_light = ams_draw::lighten_color(front_color, 40);
        lv_color_t ff_dark = ams_draw::darken_color(front_color, 25);
        // Main flange ellipse with gradient
        draw_gradient_ellipse(&layer, right_x, cy, flange_rx, flange_ry, ff_light, ff_dark);
        // Edge highlight on left side (gives 3D thickness illusion)
        // Dramatic gradient: very bright at top, dark at bottom
        lv_color_t edge_light = ams_draw::lighten_color(front_color, 100);
        lv_color_t edge_dark = ams_draw::darken_color(front_color, 40);
        draw_ellipse_left_edge(&layer, right_x, cy, flange_rx, flange_ry, edge_light, edge_dark, 2);
    }

    // ========================================
    // STEP 4: Draw CENTER HOLE ellipse (hub)
    // Stronger gradient: dark at top (deep shadow), lighter at bottom (illuminated)
    // ========================================
    lv_color_t hub_top =
        theme_manager_get_color("spool_hub_top"); // Nearly black at top (deep in shadow)
    lv_color_t hub_bottom =
        theme_manager_get_color("spool_hub_bottom"); // Noticeably lighter at bottom (light hits it)
    draw_gradient_ellipse(&layer, right_x, cy, hub_rx, hub_ry, hub_top, hub_bottom);

    lv_canvas_finish_layer(data->canvas, &layer);

    spdlog::trace("[SpoolCanvas] Redrawn: size={}, fill={:.0f}%", size, fill * 100.0f);
}

static void spool_canvas_event_cb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        lv_obj_t* obj = lv_event_get_target_obj(e);
        auto it = s_registry.find(obj);
        if (it != s_registry.end()) {
            std::unique_ptr<SpoolCanvasData> data(it->second);
            if (data && data->draw_buf) {
                lv_draw_buf_destroy(data->draw_buf);
            }
            lv_obj_set_user_data(obj, nullptr);
            s_registry.erase(it);
            // data automatically freed
        }
    }
}

static void* spool_canvas_xml_create(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);

    void* parent = lv_xml_state_get_parent(state);
    lv_obj_t* canvas = lv_canvas_create(static_cast<lv_obj_t*>(parent));
    if (!canvas)
        return nullptr;

    auto data_ptr = std::make_unique<SpoolCanvasData>();
    data_ptr->canvas = canvas;
    data_ptr->size = DEFAULT_SIZE;
    data_ptr->color = lv_color_hex(DEFAULT_COLOR);
    data_ptr->fill_level = 1.0f;

    // Create draw buffer
    data_ptr->draw_buf =
        lv_draw_buf_create(data_ptr->size, data_ptr->size, LV_COLOR_FORMAT_ARGB8888, 0);
    if (data_ptr->draw_buf) {
        lv_canvas_set_draw_buf(canvas, data_ptr->draw_buf);
    }

    lv_obj_set_size(canvas, data_ptr->size, data_ptr->size);
    s_registry[canvas] = data_ptr.get();
    lv_obj_add_event_cb(canvas, spool_canvas_event_cb, LV_EVENT_DELETE, nullptr);

    SpoolCanvasData* data = data_ptr.release();

    redraw_spool(data);

    spdlog::debug("[SpoolCanvas] Created widget");
    return canvas;
}

static void spool_canvas_xml_apply(lv_xml_parser_state_t* state, const char** attrs) {
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

        if (strcmp(name, "color") == 0) {
            uint32_t hex = strtoul(value, nullptr, 0);
            data->color = lv_color_hex(hex);
            needs_redraw = true;
            spdlog::debug("[SpoolCanvas] Set color=0x{:06X}", hex);
        } else if (strcmp(name, "fill_level") == 0) {
            data->fill_level = strtof(value, nullptr);
            needs_redraw = true;
            spdlog::debug("[SpoolCanvas] Set fill_level={:.2f}", data->fill_level);
        } else if (strcmp(name, "size") == 0) {
            int32_t new_size = atoi(value);
            if (new_size != data->size && new_size > 0) {
                data->size = new_size;

                // Recreate draw buffer with new size
                if (data->draw_buf) {
                    lv_draw_buf_destroy(data->draw_buf);
                }
                data->draw_buf =
                    lv_draw_buf_create(new_size, new_size, LV_COLOR_FORMAT_ARGB8888, 0);
                if (data->draw_buf) {
                    lv_canvas_set_draw_buf(data->canvas, data->draw_buf);
                }
                lv_obj_set_size(data->canvas, new_size, new_size);
                needs_redraw = true;
                spdlog::debug("[SpoolCanvas] Set size={}", new_size);
            }
        }
    }

    if (needs_redraw) {
        redraw_spool(data);
    }
}

void ui_spool_canvas_register(void) {
    lv_xml_register_widget("spool_canvas", spool_canvas_xml_create, spool_canvas_xml_apply);
    spdlog::debug("[SpoolCanvas] Registered spool_canvas widget with XML system");
}

lv_obj_t* ui_spool_canvas_create(lv_obj_t* parent, int32_t size) {
    if (!parent) {
        spdlog::error("[SpoolCanvas] Cannot create: parent is null");
        return nullptr;
    }

    // Use default size if not specified
    if (size <= 0) {
        size = DEFAULT_SIZE;
    }

    lv_obj_t* canvas = lv_canvas_create(parent);
    if (!canvas) {
        spdlog::error("[SpoolCanvas] Failed to create canvas");
        return nullptr;
    }

    auto data_ptr = std::make_unique<SpoolCanvasData>();
    data_ptr->canvas = canvas;
    data_ptr->size = size;
    data_ptr->color = lv_color_hex(DEFAULT_COLOR);
    data_ptr->fill_level = 1.0f;

    // Create draw buffer
    data_ptr->draw_buf = lv_draw_buf_create(size, size, LV_COLOR_FORMAT_ARGB8888, 0);
    if (data_ptr->draw_buf) {
        lv_canvas_set_draw_buf(canvas, data_ptr->draw_buf);
    } else {
        spdlog::error("[SpoolCanvas] Failed to create draw buffer");
        helix::ui::safe_delete(canvas);
        return nullptr;
    }

    lv_obj_set_size(canvas, size, size);
    s_registry[canvas] = data_ptr.get();
    lv_obj_add_event_cb(canvas, spool_canvas_event_cb, LV_EVENT_DELETE, nullptr);

    SpoolCanvasData* data = data_ptr.release();

    redraw_spool(data);

    spdlog::debug("[SpoolCanvas] Created widget programmatically (size={})", size);
    return canvas;
}

void ui_spool_canvas_set_color(lv_obj_t* canvas, lv_color_t color) {
    auto* data = get_data(canvas);
    if (data) {
        data->color = color;
        redraw_spool(data);
    }
}

void ui_spool_canvas_set_fill_level(lv_obj_t* canvas, float fill_level) {
    auto* data = get_data(canvas);
    if (data) {
        data->fill_level = LV_CLAMP(fill_level, 0.0f, 1.0f);
        redraw_spool(data);
    }
}

void ui_spool_canvas_redraw(lv_obj_t* canvas) {
    auto* data = get_data(canvas);
    if (data) {
        redraw_spool(data);
    }
}

float ui_spool_canvas_get_fill_level(lv_obj_t* canvas) {
    auto* data = get_data(canvas);
    return data ? data->fill_level : -1.0f;
}

lv_color_t ui_spool_canvas_get_color(lv_obj_t* canvas) {
    auto* data = get_data(canvas);
    return data ? data->color : lv_color_hex(DEFAULT_COLOR);
}
