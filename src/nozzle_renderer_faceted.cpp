// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_faceted.cpp
/// @brief Faceted red toolhead renderer implementation

#include "nozzle_renderer_faceted.h"

#include "ui_theme.h"

#include "nozzle_renderer_common.h"

#include <cmath>

// ============================================================================
// Facet Geometry Data (normalized coordinates 0.0-1.0)
// ============================================================================

// Precise measurements from image analysis:
// - Aspect ratio: 1.92:1 (height:width)
// - Body max width: 0.52 relative to height
// - Light source: top-left (315°)

struct Facet {
    float x[4];       // Normalized X coords (0.0-1.0, center at 0.5)
    float y[4];       // Normalized Y coords (0.0-1.0)
    int count;        // 3 for triangle, 4 for quad
    float brightness; // -1.0 (shadow) to +1.0 (highlight)
};

// Body facets - creating the angular/faceted appearance
// X: 0.0 = left edge, 0.5 = center, 1.0 = right edge of body_width
// Y: 0.0 = top, 1.0 = bottom of body_height
// "COKE BOTTLE" silhouette: wider bottom, narrow waist, medium top
// Front face is MUCH DARKER than angled side bevels
static const Facet BODY_FACETS[] = {
    // TOP SECTION (Y: 0.00 → 0.12) - medium width, hexagonal shoulders
    // T1: Left angled shoulder - VERY BRIGHT
    {{0.18f, 0.32f, 0.22f, 0.12f}, {0.00f, 0.00f, 0.08f, 0.12f}, 4, 0.50f},
    // T2: Top center flat - DARK front face
    {{0.32f, 0.68f, 0.78f, 0.22f}, {0.00f, 0.00f, 0.12f, 0.12f}, 4, -0.30f},
    // T3: Right angled shoulder - VERY DARK
    {{0.68f, 0.82f, 0.88f, 0.78f}, {0.00f, 0.00f, 0.08f, 0.12f}, 4, -0.50f},

    // UPPER BODY (Y: 0.12 → 0.32) - NARROW "waist" area
    // U1: Left bevel - VERY BRIGHT
    {{0.12f, 0.22f, 0.18f, 0.08f}, {0.12f, 0.12f, 0.32f, 0.32f}, 4, 0.50f},
    // U2: Front upper face - DARK
    {{0.22f, 0.78f, 0.82f, 0.18f}, {0.12f, 0.12f, 0.32f, 0.32f}, 4, -0.35f},
    // U3: Right bevel - VERY DARK
    {{0.78f, 0.88f, 0.92f, 0.82f}, {0.12f, 0.12f, 0.32f, 0.32f}, 4, -0.50f},

    // MIDDLE BODY (Y: 0.32 → 0.75) - WIDEST section (around fan)
    // M1: Left bevel - VERY BRIGHT
    {{0.08f, 0.18f, 0.15f, 0.02f}, {0.32f, 0.32f, 0.75f, 0.75f}, 4, 0.50f},
    // M2: Front main face - DARK (contains fan)
    {{0.18f, 0.82f, 0.85f, 0.15f}, {0.32f, 0.32f, 0.75f, 0.75f}, 4, -0.35f},
    // M3: Right bevel - VERY DARK
    {{0.82f, 0.92f, 0.98f, 0.85f}, {0.32f, 0.32f, 0.75f, 0.75f}, 4, -0.50f},

    // BOTTOM TAPER (Y: 0.75 → 0.88) - tapers from wide to nozzle
    // B1: Left taper bevel - BRIGHT
    {{0.02f, 0.15f, 0.30f, 0.30f}, {0.75f, 0.75f, 0.88f, 0.88f}, 4, 0.30f},
    // B2: Front taper face - DARK
    {{0.15f, 0.85f, 0.70f, 0.30f}, {0.75f, 0.75f, 0.88f, 0.88f}, 4, -0.40f},
    // B3: Right taper bevel - VERY DARK
    {{0.85f, 0.98f, 0.70f, 0.70f}, {0.75f, 0.75f, 0.88f, 0.88f}, 4, -0.50f},
};

static constexpr int NUM_BODY_FACETS = sizeof(BODY_FACETS) / sizeof(BODY_FACETS[0]);

// ============================================================================
// Helper Functions
// ============================================================================

/// @brief Calculate facet color from base color and brightness
static lv_color_t facet_color(lv_color_t base, float brightness) {
    if (brightness > 0) {
        return nr_lighten(base, (uint8_t)(brightness * 80)); // Max lighten by 80
    } else {
        return nr_darken(base, (uint8_t)(-brightness * 80)); // Max darken by 80
    }
}

/// @brief Draw a filled quad (4-vertex polygon) as two triangles
static void draw_quad(lv_layer_t* layer, int32_t x[4], int32_t y[4], lv_color_t color) {
    // Triangle 1: vertices 0, 1, 2
    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);
    tri_dsc.p[0].x = x[0];
    tri_dsc.p[0].y = y[0];
    tri_dsc.p[1].x = x[1];
    tri_dsc.p[1].y = y[1];
    tri_dsc.p[2].x = x[2];
    tri_dsc.p[2].y = y[2];
    tri_dsc.color = color;
    tri_dsc.opa = LV_OPA_COVER;
    lv_draw_triangle(layer, &tri_dsc);

    // Triangle 2: vertices 0, 2, 3
    tri_dsc.p[1].x = x[2];
    tri_dsc.p[1].y = y[2];
    tri_dsc.p[2].x = x[3];
    tri_dsc.p[2].y = y[3];
    lv_draw_triangle(layer, &tri_dsc);
}

/// @brief Draw a filled triangle
static void draw_triangle(lv_layer_t* layer, int32_t x[3], int32_t y[3], lv_color_t color) {
    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);
    tri_dsc.p[0].x = x[0];
    tri_dsc.p[0].y = y[0];
    tri_dsc.p[1].x = x[1];
    tri_dsc.p[1].y = y[1];
    tri_dsc.p[2].x = x[2];
    tri_dsc.p[2].y = y[2];
    tri_dsc.color = color;
    tri_dsc.opa = LV_OPA_COVER;
    lv_draw_triangle(layer, &tri_dsc);
}

/// @brief Draw a filled circle
static void draw_filled_circle(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t radius,
                               lv_color_t color) {
    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);
    fill_dsc.color = color;
    fill_dsc.opa = LV_OPA_COVER;
    fill_dsc.radius = radius;

    lv_area_t area = {cx - radius, cy - radius, cx + radius, cy + radius};
    lv_draw_fill(layer, &fill_dsc, &area);
}

/// @brief Draw a circle outline (ring)
static void draw_circle_ring(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t radius,
                             int32_t width, lv_color_t color) {
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.center.x = cx;
    arc_dsc.center.y = cy;
    arc_dsc.radius = radius;
    arc_dsc.start_angle = 0;
    arc_dsc.end_angle = 360;
    arc_dsc.width = width;
    arc_dsc.color = color;
    arc_dsc.opa = LV_OPA_COVER;
    lv_draw_arc(layer, &arc_dsc);
}

// ============================================================================
// Main Drawing Function
// ============================================================================

void draw_nozzle_faceted(lv_layer_t* layer, int32_t cx, int32_t cy, lv_color_t filament_color,
                         int32_t scale_unit) {
    // ========================================
    // Calculate dimensions (from reference image analysis)
    // ========================================
    // Reference has ~2:1 aspect ratio (height:width)
    // Body width is approximately 52% of height
    int32_t body_height = scale_unit * 5;          // 50px at scale 10
    int32_t body_width = (body_height * 52) / 100; // ~26px at scale 10

    // Position: cy is center of the print head
    int32_t top_y = cy - body_height / 2;
    int32_t bot_y = cy + body_height / 2;

    // ========================================
    // Get base color from theme (single token)
    // ========================================
    lv_color_t base_color = ui_theme_get_color("toolhead_body");

    // Derived colors
    lv_color_t frame_color = lv_color_hex(0x2D2D2D);   // Dark charcoal frame
    lv_color_t recess_color = lv_color_hex(0x1A1A1A);  // Pure black recess
    lv_color_t fan_hub_color = lv_color_hex(0x404040); // Neutral gray hub
    lv_color_t nozzle_brass = lv_color_hex(0xC4A000);  // Brass nozzle

    // STEP 1: No background frame - we draw facets directly on transparent background

    // ========================================
    // STEP 2: Draw faceted red body
    // ========================================
    for (int i = 0; i < NUM_BODY_FACETS; i++) {
        const Facet& f = BODY_FACETS[i];

        // Convert normalized coords to pixel coords
        int32_t px[4], py[4];
        for (int j = 0; j < f.count; j++) {
            // X: 0.0-1.0 maps to (cx - half_width) to (cx + half_width)
            // Center is at 0.5, so: px = cx + (f.x - 0.5) * body_width
            px[j] = cx + (int32_t)((f.x[j] - 0.5f) * body_width);
            // Y: 0.0-1.0 maps to top_y to bot_y
            py[j] = top_y + (int32_t)(f.y[j] * body_height);
        }

        lv_color_t color = facet_color(base_color, f.brightness);

        if (f.count == 4) {
            draw_quad(layer, px, py, color);
        } else {
            draw_triangle(layer, px, py, color);
        }
    }

    // ========================================
    // STEP 2b: Draw dark edge outlines for "coke bottle" silhouette
    // ========================================
    {
        lv_draw_line_dsc_t line_dsc;
        lv_draw_line_dsc_init(&line_dsc);
        line_dsc.color = frame_color;
        line_dsc.width = 1;
        line_dsc.opa = LV_OPA_COVER;

        // Left edge: top shoulder → narrow waist → wide bottom
        // Top to waist (narrowing)
        line_dsc.p1.x = cx + (int32_t)((0.12f - 0.5f) * body_width);
        line_dsc.p1.y = top_y + (body_height * 12) / 100;
        line_dsc.p2.x = cx + (int32_t)((0.08f - 0.5f) * body_width);
        line_dsc.p2.y = top_y + (body_height * 32) / 100;
        lv_draw_line(layer, &line_dsc);
        // Waist to wide (widening)
        line_dsc.p1.x = cx + (int32_t)((0.08f - 0.5f) * body_width);
        line_dsc.p1.y = top_y + (body_height * 32) / 100;
        line_dsc.p2.x = cx + (int32_t)((0.02f - 0.5f) * body_width);
        line_dsc.p2.y = top_y + (body_height * 75) / 100;
        lv_draw_line(layer, &line_dsc);

        // Right edge: mirror of left
        line_dsc.p1.x = cx + (int32_t)((0.88f - 0.5f) * body_width);
        line_dsc.p1.y = top_y + (body_height * 12) / 100;
        line_dsc.p2.x = cx + (int32_t)((0.92f - 0.5f) * body_width);
        line_dsc.p2.y = top_y + (body_height * 32) / 100;
        lv_draw_line(layer, &line_dsc);
        line_dsc.p1.x = cx + (int32_t)((0.92f - 0.5f) * body_width);
        line_dsc.p1.y = top_y + (body_height * 32) / 100;
        line_dsc.p2.x = cx + (int32_t)((0.98f - 0.5f) * body_width);
        line_dsc.p2.y = top_y + (body_height * 75) / 100;
        lv_draw_line(layer, &line_dsc);

        // Top hexagonal edge
        line_dsc.p1.x = cx + (int32_t)((0.18f - 0.5f) * body_width);
        line_dsc.p1.y = top_y;
        line_dsc.p2.x = cx + (int32_t)((0.82f - 0.5f) * body_width);
        line_dsc.p2.y = top_y;
        lv_draw_line(layer, &line_dsc);

        // Left shoulder angle
        line_dsc.p1.x = cx + (int32_t)((0.18f - 0.5f) * body_width);
        line_dsc.p1.y = top_y;
        line_dsc.p2.x = cx + (int32_t)((0.12f - 0.5f) * body_width);
        line_dsc.p2.y = top_y + (body_height * 12) / 100;
        lv_draw_line(layer, &line_dsc);

        // Right shoulder angle
        line_dsc.p1.x = cx + (int32_t)((0.82f - 0.5f) * body_width);
        line_dsc.p1.y = top_y;
        line_dsc.p2.x = cx + (int32_t)((0.88f - 0.5f) * body_width);
        line_dsc.p2.y = top_y + (body_height * 12) / 100;
        lv_draw_line(layer, &line_dsc);
    }

    // ========================================
    // STEP 3: Draw top circular recess (extruder motor)
    // ========================================
    {
        // Center at Y = 0.22 (in upper body section 0.12-0.32)
        int32_t recess_cy = top_y + (body_height * 22) / 100;
        // Diameter = 0.35 * body_width (larger to fill space)
        int32_t recess_r = (body_width * 17) / 100;

        // Outer ring (subtle bezel)
        draw_circle_ring(layer, cx, recess_cy, recess_r + 1, 2, nr_darken(base_color, 40));

        // Inner dark recess
        draw_filled_circle(layer, cx, recess_cy, recess_r, recess_color);
    }

    // ========================================
    // STEP 4: Draw fan circle
    // ========================================
    {
        // Center at Y = 0.54 (in middle body section 0.32-0.75)
        int32_t fan_cy = top_y + (body_height * 54) / 100;
        // Outer diameter = 0.50 * body_width (larger fan, ~1.5x top recess)
        int32_t fan_outer_r = (body_width * 25) / 100;
        // Hub diameter = 0.12 * body_width → radius = 0.06
        int32_t hub_r = (body_width * 8) / 100;

        // Outer bezel ring
        draw_circle_ring(layer, cx, fan_cy, fan_outer_r + 1, 2, nr_darken(base_color, 30));

        // Dark blade area
        draw_filled_circle(layer, cx, fan_cy, fan_outer_r, recess_color);

        // Lighter hub center
        draw_filled_circle(layer, cx, fan_cy, hub_r, fan_hub_color);

        // Highlight arc on top-left (light reflection)
        lv_draw_arc_dsc_t arc_dsc;
        lv_draw_arc_dsc_init(&arc_dsc);
        arc_dsc.center.x = cx;
        arc_dsc.center.y = fan_cy;
        arc_dsc.radius = fan_outer_r;
        arc_dsc.start_angle = 200;
        arc_dsc.end_angle = 280;
        arc_dsc.width = 1;
        arc_dsc.color = nr_lighten(base_color, 40);
        arc_dsc.opa = LV_OPA_60;
        lv_draw_arc(layer, &arc_dsc);
    }

    // (Screws removed - too much visual noise at small sizes)

    // ========================================
    // STEP 5: Draw logo (3 diagonal stripes)
    // ========================================
    {
        // Logo center at Y = 0.38 (between top recess at 22% and fan at 54%)
        int32_t logo_cy = top_y + (body_height * 38) / 100;
        int32_t stripe_len = (body_width * 8) / 100;
        int32_t stripe_gap = (body_width * 2) / 100;

        lv_draw_line_dsc_t line_dsc;
        lv_draw_line_dsc_init(&line_dsc);
        line_dsc.color = nr_darken(base_color, 50);
        line_dsc.width = 1;

        // 3 stripes, angled at ~-60°
        for (int i = -1; i <= 1; i++) {
            int32_t offset_x = i * stripe_gap;
            // Diagonal from upper-left to lower-right
            line_dsc.p1.x = cx + offset_x - stripe_len / 3;
            line_dsc.p1.y = logo_cy - stripe_len / 2;
            line_dsc.p2.x = cx + offset_x + stripe_len / 3;
            line_dsc.p2.y = logo_cy + stripe_len / 2;
            lv_draw_line(layer, &line_dsc);
        }
    }

    // ========================================
    // STEP 7: Draw nozzle tip (brass, tapered)
    // ========================================
    {
        // Nozzle from Y = 0.88 to Y = 1.0 (starts where flat taper ends)
        int32_t noz_top_y = top_y + (body_height * 88) / 100;
        int32_t noz_bot_y = bot_y;
        int32_t noz_height = noz_bot_y - noz_top_y;

        // Width tapers from flat bottom (0.30-0.70 = 0.40 width) to nozzle tip
        int32_t noz_top_half = (body_width * 20) / 100; // Matches flat bottom width
        int32_t noz_bot_half = (body_width * 5) / 100;

        // Calculate nozzle colors (brass base, tinted with filament if loaded)
        lv_color_t noz_left = nr_lighten(nozzle_brass, 20);
        lv_color_t noz_right = nr_darken(nozzle_brass, 20);

        // Tint with filament color if not default
        lv_color_t default_nozzle = ui_theme_get_color("filament_nozzle_dark");
        if (!lv_color_eq(filament_color, default_nozzle) &&
            !lv_color_eq(filament_color, lv_color_hex(0x808080))) {
            noz_left = nr_blend(noz_left, filament_color, 0.4f);
            noz_right = nr_blend(noz_right, filament_color, 0.4f);
        }

        // Draw tapered nozzle
        nr_draw_nozzle_tip(layer, cx, noz_top_y, noz_top_half * 2, noz_bot_half * 2, noz_height,
                           noz_left, noz_right);

        // Bright glint at tip
        lv_draw_fill_dsc_t fill_dsc;
        lv_draw_fill_dsc_init(&fill_dsc);
        fill_dsc.color = lv_color_hex(0xFFFFFF);
        fill_dsc.opa = LV_OPA_70;
        lv_area_t glint = {cx - 1, noz_bot_y - 1, cx + 1, noz_bot_y};
        lv_draw_fill(layer, &fill_dsc, &glint);
    }
}
