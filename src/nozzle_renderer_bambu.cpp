// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_bambu.cpp
/// @brief Bambu-style metallic gray toolhead renderer implementation

#include "nozzle_renderer_bambu.h"

#include "ui_theme.h"

#include "nozzle_renderer_common.h"

void draw_nozzle_bambu(lv_layer_t* layer, int32_t cx, int32_t cy, lv_color_t filament_color,
                       int32_t scale_unit) {
    // Bambu-style print head: tall rectangular body with large circular fan duct
    // Proportions: roughly 2:1 height to width ratio
    // cy is the CENTER of the entire print head assembly

    // Base colors - light gray metallic (like Bambu's silver/white head)
    lv_color_t metal_base = ui_theme_get_color("filament_metal");

    // Lighting: light comes from top-left
    lv_color_t front_light = nr_lighten(metal_base, 40);
    lv_color_t front_mid = metal_base;
    lv_color_t front_dark = nr_darken(metal_base, 25);
    lv_color_t side_color = nr_darken(metal_base, 40);
    lv_color_t top_color = nr_lighten(metal_base, 60);
    lv_color_t outline_color = nr_darken(metal_base, 50);

    // Dimensions scaled by scale_unit - TALL like Bambu (2:1 ratio)
    int32_t body_half_width = (scale_unit * 18) / 10; // ~18px at scale 10
    int32_t body_height = scale_unit * 4;             // ~40px at scale 10 (tall!)
    int32_t body_depth = (scale_unit * 6) / 10;       // ~6px isometric depth

    // Shift extruder left so filament line bisects the TOP edge of top surface
    // The top surface's back edge is shifted right by body_depth, so we compensate
    cx = cx - body_depth / 2;

    // Nozzle tip dimensions (small at bottom)
    int32_t tip_top_width = (scale_unit * 8) / 10;
    int32_t tip_bottom_width = (scale_unit * 3) / 10;
    int32_t tip_height = (scale_unit * 6) / 10;

    // Fan duct - large, centered on front face
    int32_t fan_radius = (scale_unit * 12) / 10; // Large fan taking most of front

    // Cap dimensions (raised narrower section on top)
    int32_t cap_height = body_height / 10;              // ~10% of body
    int32_t cap_half_width = (body_half_width * 3) / 4; // ~75% of body width
    int32_t bevel_height = cap_height;                  // Height of bevel transition zone

    // Calculate Y positions - body stays fixed, cap and bevels sit above it
    int32_t body_top = cy - body_height / 2; // Body top stays at original position
    int32_t body_bottom = cy + body_height / 2;
    int32_t cap_bottom = body_top - bevel_height; // Cap ends above bevel zone
    int32_t cap_top = cap_bottom - cap_height;    // Cap starts above that
    int32_t tip_top = body_bottom;
    int32_t tip_bottom = tip_top + tip_height;

    // ========================================
    // STEP 0: Draw tapered top section (cap + bevel as ONE continuous shape)
    // ========================================
    {
        int32_t bevel_width = body_half_width - cap_half_width;
        int32_t taper_height = body_top - cap_top;
        lv_draw_fill_dsc_t fill;
        lv_draw_fill_dsc_init(&fill);
        fill.opa = LV_OPA_COVER;

        // === TAPERED ISOMETRIC TOP ===
        for (int32_t dy = 0; dy <= taper_height; dy++) {
            float factor = (float)dy / (float)taper_height;
            int32_t half_w = cap_half_width + (int32_t)(bevel_width * factor);
            int32_t y_front = cap_top + dy;

            for (int32_t d = 0; d <= body_depth; d++) {
                float iso_factor = (float)d / (float)body_depth;
                int32_t y_offset = (int32_t)(iso_factor * body_depth / 2);
                int32_t y_row = y_front - y_offset;
                int32_t x_left = cx - half_w + d;
                int32_t x_right = cx + half_w + d;

                lv_color_t row_color = nr_blend(top_color, nr_darken(top_color, 20), iso_factor);
                fill.color = row_color;
                lv_area_t row = {x_left, y_row, x_right, y_row};
                lv_draw_fill(layer, &fill, &row);
            }
        }

        // === TAPERED FRONT FACE ===
        for (int32_t dy = 0; dy <= taper_height; dy++) {
            float factor = (float)dy / (float)taper_height;
            int32_t half_w = cap_half_width + (int32_t)(bevel_width * factor);
            int32_t y_row = cap_top + dy;

            lv_color_t base_color = nr_blend(front_light, front_dark, factor * 0.6f);

            for (int32_t x = cx - half_w; x <= cx + half_w; x++) {
                float x_factor = (float)(x - cx) / (float)half_w;

                lv_color_t pixel_color;
                if (x_factor < 0) {
                    pixel_color = nr_lighten(base_color, (int32_t)(-x_factor * 12));
                } else {
                    pixel_color = nr_darken(base_color, (int32_t)(x_factor * 12));
                }

                fill.color = pixel_color;
                lv_area_t pixel = {x, y_row, x, y_row};
                lv_draw_fill(layer, &fill, &pixel);
            }
        }

        // === TAPERED RIGHT SIDE ===
        for (int32_t dy = 0; dy <= taper_height; dy++) {
            float factor = (float)dy / (float)taper_height;
            int32_t half_w = cap_half_width + (int32_t)(bevel_width * factor);
            int32_t y_front = cap_top + dy;
            int32_t x_base = cx + half_w;

            for (int32_t d = 0; d <= body_depth; d++) {
                float iso_factor = (float)d / (float)body_depth;
                int32_t y_offset = (int32_t)(iso_factor * body_depth / 2);
                lv_color_t side_col = nr_blend(side_color, nr_darken(side_color, 30), iso_factor);
                fill.color = side_col;
                lv_area_t pixel = {x_base + d, y_front - y_offset, x_base + d, y_front - y_offset};
                lv_draw_fill(layer, &fill, &pixel);
            }
        }

        // === LEFT EDGE HIGHLIGHT ===
        lv_draw_line_dsc_t line_dsc;
        lv_draw_line_dsc_init(&line_dsc);
        line_dsc.color = nr_lighten(front_light, 30);
        line_dsc.width = 1;
        line_dsc.p1.x = cx - cap_half_width;
        line_dsc.p1.y = cap_top;
        line_dsc.p2.x = cx - body_half_width;
        line_dsc.p2.y = body_top;
        lv_draw_line(layer, &line_dsc);
    }

    // ========================================
    // STEP 1: Draw main body
    // ========================================
    {
        // Front face with vertical gradient
        nr_draw_gradient_rect(layer, cx - body_half_width, body_top, cx + body_half_width,
                              body_bottom, front_light, front_dark);

        // Right side face (darker, isometric depth)
        nr_draw_iso_side(layer, cx + body_half_width, body_top, body_bottom, body_depth, side_color,
                         nr_darken(side_color, 20));

        // Left edge highlight
        lv_draw_line_dsc_t line_dsc;
        lv_draw_line_dsc_init(&line_dsc);
        line_dsc.color = nr_lighten(front_light, 30);
        line_dsc.width = 1;
        line_dsc.p1.x = cx - body_half_width;
        line_dsc.p1.y = body_top;
        line_dsc.p2.x = cx - body_half_width;
        line_dsc.p2.y = body_bottom;
        lv_draw_line(layer, &line_dsc);

        // Outline for definition
        line_dsc.color = outline_color;
        line_dsc.p1.x = cx - body_half_width;
        line_dsc.p1.y = body_bottom;
        line_dsc.p2.x = cx + body_half_width;
        line_dsc.p2.y = body_bottom;
        lv_draw_line(layer, &line_dsc);
    }

    // ========================================
    // STEP 2: Draw large circular fan duct
    // ========================================
    {
        int32_t fan_cx = cx;
        int32_t fan_cy = cy - (scale_unit * 4) / 10;

        // Outer bezel ring
        lv_draw_arc_dsc_t arc_dsc;
        lv_draw_arc_dsc_init(&arc_dsc);
        arc_dsc.center.x = fan_cx;
        arc_dsc.center.y = fan_cy;
        arc_dsc.radius = fan_radius + 2;
        arc_dsc.start_angle = 0;
        arc_dsc.end_angle = 360;
        arc_dsc.width = 2;
        arc_dsc.color = nr_lighten(front_mid, 20);
        arc_dsc.opa = LV_OPA_COVER;
        lv_draw_arc(layer, &arc_dsc);

        // Main fan opening - dark blade area
        lv_draw_fill_dsc_t fill_dsc;
        lv_draw_fill_dsc_init(&fill_dsc);
        fill_dsc.color = nr_darken(metal_base, 80);
        fill_dsc.opa = LV_OPA_COVER;
        fill_dsc.radius = fan_radius;

        lv_area_t fan_area = {fan_cx - fan_radius, fan_cy - fan_radius, fan_cx + fan_radius,
                              fan_cy + fan_radius};
        lv_draw_fill(layer, &fill_dsc, &fan_area);

        // Inner hub circle
        int32_t hub_r = fan_radius / 3;
        fill_dsc.color = nr_darken(metal_base, 40);
        fill_dsc.radius = hub_r;
        lv_area_t hub_area = {fan_cx - hub_r, fan_cy - hub_r, fan_cx + hub_r, fan_cy + hub_r};
        lv_draw_fill(layer, &fill_dsc, &hub_area);

        // Highlight arc on top-left
        arc_dsc.radius = fan_radius + 1;
        arc_dsc.start_angle = 200;
        arc_dsc.end_angle = 290;
        arc_dsc.width = 1;
        arc_dsc.color = nr_lighten(front_light, 50);
        lv_draw_arc(layer, &arc_dsc);
    }

    // ========================================
    // STEP 3: Draw nozzle tip
    // ========================================
    {
        lv_color_t tip_left = nr_lighten(metal_base, 30);
        lv_color_t tip_right = nr_darken(metal_base, 20);

        // If filament loaded, tint the nozzle tip
        lv_color_t nozzle_dark = ui_theme_get_color("filament_nozzle_dark");
        lv_color_t nozzle_light = ui_theme_get_color("filament_nozzle_light");
        if (!lv_color_eq(filament_color, nr_darken(metal_base, 10)) &&
            !lv_color_eq(filament_color, nozzle_dark) &&
            !lv_color_eq(filament_color, nozzle_light)) {
            tip_left = nr_blend(tip_left, filament_color, 0.4f);
            tip_right = nr_blend(tip_right, filament_color, 0.4f);
        }

        nr_draw_nozzle_tip(layer, cx, tip_top, tip_top_width, tip_bottom_width, tip_height,
                           tip_left, tip_right);

        // Bright glint at tip
        lv_draw_fill_dsc_t fill_dsc;
        lv_draw_fill_dsc_init(&fill_dsc);
        fill_dsc.color = lv_color_hex(0xFFFFFF);
        fill_dsc.opa = LV_OPA_70;
        lv_area_t glint = {cx - 1, tip_bottom - 1, cx + 1, tip_bottom};
        lv_draw_fill(layer, &fill_dsc, &glint);
    }
}
