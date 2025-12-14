// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_common.h
/// @brief Common helper functions for nozzle/toolhead rendering

#pragma once

#include "lvgl/lvgl.h"

// ============================================================================
// Color Manipulation Helpers
// ============================================================================

/// @brief Darken a color by reducing RGB components
/// @param c Base color
/// @param amt Amount to subtract from each component (0-255)
/// @return Darkened color
inline lv_color_t nr_darken(lv_color_t c, uint8_t amt) {
    return lv_color_make(c.red > amt ? c.red - amt : 0, c.green > amt ? c.green - amt : 0,
                         c.blue > amt ? c.blue - amt : 0);
}

/// @brief Lighten a color by increasing RGB components
/// @param c Base color
/// @param amt Amount to add to each component (0-255)
/// @return Lightened color
inline lv_color_t nr_lighten(lv_color_t c, uint8_t amt) {
    return lv_color_make((c.red + amt > 255) ? 255 : c.red + amt,
                         (c.green + amt > 255) ? 255 : c.green + amt,
                         (c.blue + amt > 255) ? 255 : c.blue + amt);
}

/// @brief Blend two colors by a factor
/// @param c1 First color (factor=0.0)
/// @param c2 Second color (factor=1.0)
/// @param factor Blend factor 0.0-1.0
/// @return Blended color
inline lv_color_t nr_blend(lv_color_t c1, lv_color_t c2, float factor) {
    factor = LV_CLAMP(factor, 0.0f, 1.0f);
    return lv_color_make((uint8_t)(c1.red + (c2.red - c1.red) * factor),
                         (uint8_t)(c1.green + (c2.green - c1.green) * factor),
                         (uint8_t)(c1.blue + (c2.blue - c1.blue) * factor));
}

// ============================================================================
// Drawing Primitives
// ============================================================================

/// @brief Draw a rectangle with vertical gradient
/// @param layer Draw layer
/// @param x1 Left edge
/// @param y1 Top edge
/// @param x2 Right edge
/// @param y2 Bottom edge
/// @param top_color Color at top
/// @param bottom_color Color at bottom
inline void nr_draw_gradient_rect(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                                  lv_color_t top_color, lv_color_t bottom_color) {
    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);
    fill_dsc.opa = LV_OPA_COVER;

    int32_t height = y2 - y1;
    if (height <= 0)
        return;

    for (int32_t y = y1; y <= y2; y++) {
        float factor = (float)(y - y1) / (float)height;
        fill_dsc.color = nr_blend(top_color, bottom_color, factor);
        lv_area_t line = {x1, y, x2, y};
        lv_draw_fill(layer, &fill_dsc, &line);
    }
}

/// @brief Draw isometric side face (parallelogram)
/// @param layer Draw layer
/// @param x Left edge X
/// @param y1 Top Y
/// @param y2 Bottom Y
/// @param depth Isometric depth (horizontal offset)
/// @param top_color Color at top
/// @param bottom_color Color at bottom
inline void nr_draw_iso_side(lv_layer_t* layer, int32_t x, int32_t y1, int32_t y2, int32_t depth,
                             lv_color_t top_color, lv_color_t bottom_color) {
    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);
    fill_dsc.opa = LV_OPA_COVER;

    int32_t height = y2 - y1;
    if (height <= 0 || depth <= 0)
        return;

    int32_t y_offset = depth / 2;

    for (int32_t d = 0; d <= depth; d++) {
        float horiz_factor = (float)d / (float)depth;
        int32_t col_x = x + d;
        int32_t col_y1 = y1 - (int32_t)(horiz_factor * y_offset);
        int32_t col_y2 = y2 - (int32_t)(horiz_factor * y_offset);

        for (int32_t y = col_y1; y <= col_y2; y++) {
            float vert_factor = (float)(y - col_y1) / (float)(col_y2 - col_y1);
            fill_dsc.color = nr_blend(top_color, bottom_color, vert_factor);
            lv_area_t pixel = {col_x, y, col_x, y};
            lv_draw_fill(layer, &fill_dsc, &pixel);
        }
    }
}

/// @brief Draw isometric top face (parallelogram tilting up-right)
/// @param layer Draw layer
/// @param cx Center X
/// @param y Front edge Y
/// @param half_width Half the width of front edge
/// @param depth Isometric depth
/// @param color Fill color
inline void nr_draw_iso_top(lv_layer_t* layer, int32_t cx, int32_t y, int32_t half_width,
                            int32_t depth, lv_color_t color) {
    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);
    fill_dsc.color = color;
    fill_dsc.opa = LV_OPA_COVER;

    int32_t y_offset = depth / 2;

    for (int32_t d = 0; d <= depth; d++) {
        float factor = (float)d / (float)depth;
        int32_t row_y = y - (int32_t)(factor * y_offset);
        int32_t x_start = cx - half_width + d;
        int32_t x_end = cx + half_width + d;

        lv_area_t line = {x_start, row_y, x_end, row_y};
        lv_draw_fill(layer, &fill_dsc, &line);
    }
}

/// @brief Draw tapered nozzle tip shape
/// @param layer Draw layer
/// @param cx Center X
/// @param top_y Top Y position
/// @param top_width Width at top
/// @param bottom_width Width at bottom
/// @param height Height of nozzle tip
/// @param left_color Color for left half (highlight side)
/// @param right_color Color for right half (shadow side)
inline void nr_draw_nozzle_tip(lv_layer_t* layer, int32_t cx, int32_t top_y, int32_t top_width,
                               int32_t bottom_width, int32_t height, lv_color_t left_color,
                               lv_color_t right_color) {
    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);
    fill_dsc.opa = LV_OPA_COVER;

    if (height <= 0)
        return;

    for (int32_t y = 0; y < height; y++) {
        float factor = (float)y / (float)height;
        int32_t half_width =
            (int32_t)(top_width / 2.0f + (bottom_width / 2.0f - top_width / 2.0f) * factor);

        // Left half (lighter)
        fill_dsc.color = left_color;
        lv_area_t left = {cx - half_width, top_y + y, cx, top_y + y};
        lv_draw_fill(layer, &fill_dsc, &left);

        // Right half (darker for 3D effect)
        fill_dsc.color = right_color;
        lv_area_t right = {cx + 1, top_y + y, cx + half_width, top_y + y};
        lv_draw_fill(layer, &fill_dsc, &right);
    }
}
