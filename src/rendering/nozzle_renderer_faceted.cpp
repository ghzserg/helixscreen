// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_faceted.cpp
/// @brief Voron Stealthburner toolhead renderer implementation
///
/// Ported from toolhead_icon_scaled.c - a traced SVG of the Stealthburner
/// using LVGL polygon primitives for vector rendering.

#include "nozzle_renderer_faceted.h"

#include "nozzle_renderer_common.h"

#include <cmath>

// ============================================================================
// Polygon Data (1000x1000 design space, centered at 500,500)
// Traced from Voron Stealthburner SVG
// ============================================================================

static const lv_point_t pts_housing[] = {
    {583, 928}, {560, 920}, {554, 914}, {538, 872}, {530, 877}, {529, 896}, {504, 900}, {497, 908},
    {492, 910}, {484, 900}, {459, 896}, {456, 892}, {458, 877}, {446, 870}, {430, 916}, {408, 928},
    {380, 926}, {296, 892}, {274, 832}, {290, 774}, {287, 770}, {280, 769}, {278, 738}, {298, 736},
    {302, 726}, {302, 679}, {308, 672}, {316, 575}, {316, 466}, {290, 328}, {290, 296}, {292, 233},
    {304, 203}, {318, 191}, {308, 180}, {286, 169}, {286, 156}, {292, 152}, {335, 160}, {359, 158},
    {389, 134}, {402, 126}, {436, 114}, {463, 112}, {478, 97},  {478, 84},  {483, 78},  {488, 81},
    {484, 90},  {490, 96},  {497, 96},  {502, 91},  {500, 80},  {504, 78},  {508, 85},  {508, 97},
    {524, 112}, {546, 112}, {590, 128}, {682, 204}, {694, 234}, {698, 286}, {698, 333}, {668, 528},
    {668, 577}, {682, 719}, {710, 833}, {694, 885}, {688, 892}, {618, 920}, {583, 928},
};
static constexpr int pts_housing_cnt = sizeof(pts_housing) / sizeof(pts_housing[0]);

static const lv_point_t pts_plate[] = {
    {580, 926}, {561, 920}, {554, 914}, {538, 864}, {521, 848}, {499, 840}, {485, 840}, {463, 848},
    {444, 867}, {438, 895}, {430, 914}, {408, 926}, {384, 924}, {317, 898}, {312, 894}, {314, 888},
    {290, 796}, {318, 726}, {326, 629}, {316, 604}, {316, 587}, {318, 459}, {306, 348}, {310, 270},
    {322, 217}, {332, 203}, {403, 144}, {426, 132}, {558, 132}, {568, 136}, {641, 194}, {658, 211},
    {674, 266}, {676, 355}, {668, 443}, {668, 591}, {668, 604}, {656, 632}, {660, 638}, {666, 725},
    {694, 791}, {694, 799}, {670, 894}, {603, 924}, {580, 926},
};
static constexpr int pts_plate_cnt = sizeof(pts_plate) / sizeof(pts_plate[0]);

static const lv_point_t pts_top_circle[] = {
    {507, 398}, {476, 398}, {444, 392}, {422, 367}, {400, 327}, {396, 304},
    {408, 272}, {432, 238}, {451, 224}, {496, 218}, {537, 226}, {564, 256},
    {586, 302}, {578, 338}, {556, 373}, {540, 390}, {507, 398},
};
static constexpr int pts_top_circle_cnt = sizeof(pts_top_circle) / sizeof(pts_top_circle[0]);

// Bottom fan is now drawn as a simple circle (see draw_circle in draw_nozzle_faceted)
// The original complex polygon with fan blade details caused triangulation artifacts

static const lv_point_t pts_logo_1[] = {
    {457, 498}, {472, 472}, {474, 470}, {485, 470}, {469, 499},
};
static constexpr int pts_logo_1_cnt = sizeof(pts_logo_1) / sizeof(pts_logo_1[0]);

static const lv_point_t pts_logo_2[] = {
    {468, 530}, {502, 471}, {515, 470}, {481, 529}, {479, 531},
};
static constexpr int pts_logo_2_cnt = sizeof(pts_logo_2) / sizeof(pts_logo_2[0]);

static const lv_point_t pts_logo_3[] = {
    {497, 530},
    {513, 502},
    {525, 502},
    {509, 531},
};
static constexpr int pts_logo_3_cnt = sizeof(pts_logo_3) / sizeof(pts_logo_3[0]);

// Facet polygons for 3D shading effect
static const lv_point_t pts_facet_1[] = {
    {663, 898}, {640, 869}, {658, 787}, {648, 758}, {640, 628}, {592, 445}, {600, 423}, {610, 418},
    {606, 409}, {612, 406}, {624, 372}, {630, 369}, {628, 362}, {642, 334}, {640, 299}, {588, 206},
    {580, 200}, {584, 202}, {584, 195}, {578, 198}, {578, 189}, {574, 188}, {601, 180}, {605, 184},
    {606, 178}, {611, 186}, {614, 174}, {612, 188}, {605, 186}, {604, 194}, {601, 192}, {595, 200},
    {588, 196}, {586, 201}, {598, 204}, {601, 196}, {608, 192}, {612, 194}, {616, 188}, {618, 192},
    {609, 202}, {627, 200}, {627, 192}, {632, 198}, {636, 192}, {643, 196}, {660, 215}, {674, 269},
    {676, 355}, {666, 445}, {666, 605}, {656, 632}, {666, 725}, {694, 799}, {672, 888}, {663, 898},
};
static constexpr int pts_facet_1_cnt = sizeof(pts_facet_1) / sizeof(pts_facet_1[0]);

static const lv_point_t pts_facet_2[] = {
    {587, 892}, {584, 884}, {582, 890}, {570, 888}, {574, 879}, {562, 877}, {562, 865}, {552, 854},
    {558, 849}, {544, 839}, {550, 836}, {555, 842}, {556, 831}, {541, 838}, {512, 822}, {516, 832},
    {512, 836}, {508, 822}, {520, 818}, {508, 804}, {520, 806}, {519, 796}, {525, 804}, {526, 795},
    {542, 788}, {550, 794}, {550, 787}, {596, 747}, {606, 722}, {617, 736}, {634, 732}, {636, 746},
    {636, 732}, {643, 730}, {648, 772}, {642, 779}, {650, 782}, {644, 785}, {648, 794}, {656, 795},
    {648, 825}, {638, 834}, {644, 837}, {638, 846}, {642, 848}, {638, 864}, {622, 876}, {614, 866},
    {616, 876}, {609, 882}, {592, 876}, {590, 884}, {602, 881}, {587, 892},
};
static constexpr int pts_facet_2_cnt = sizeof(pts_facet_2) / sizeof(pts_facet_2[0]);

static const lv_point_t pts_facet_3[] = {
    {498, 790}, {481, 784}, {468, 770}, {464, 750}, {470, 738}, {442, 718}, {436, 718}, {442, 710},
    {432, 691}, {432, 674}, {440, 648}, {456, 630}, {484, 618}, {520, 624}, {526, 612}, {534, 616},
    {543, 610}, {546, 590}, {534, 574}, {546, 583}, {552, 596}, {546, 620}, {528, 633}, {544, 653},
    {550, 667}, {548, 705}, {540, 720}, {554, 732}, {582, 742}, {566, 750}, {550, 750}, {527, 730},
    {503, 740}, {472, 738}, {472, 765}, {486, 780}, {495, 780}, {498, 790},
};
static constexpr int pts_facet_3_cnt = sizeof(pts_facet_3) / sizeof(pts_facet_3[0]);

static const lv_point_t pts_facet_4[] = {
    {343, 626}, {318, 554}, {320, 461}, {316, 445}, {320, 442}, {314, 438}, {312, 420}, {338, 348},
    {336, 344}, {340, 343}, {340, 334}, {364, 384}, {362, 389}, {368, 395}, {390, 443}, {343, 626},
};
static constexpr int pts_facet_4_cnt = sizeof(pts_facet_4) / sizeof(pts_facet_4[0]);

static const lv_point_t pts_facet_5[] = {
    {391, 206}, {374, 204}, {373, 198}, {367, 202}, {344, 196}, {425, 134},
    {559, 134}, {576, 145}, {576, 150}, {550, 178}, {428, 176}, {391, 206},
};
static constexpr int pts_facet_5_cnt = sizeof(pts_facet_5) / sizeof(pts_facet_5[0]);

static const lv_point_t pts_facet_6[] = {
    {431, 452}, {420, 449}, {424, 430}, {404, 410}, {392, 388}, {387, 384}, {384, 388}, {380, 385},
    {388, 379}, {378, 374}, {384, 371}, {376, 368}, {378, 362}, {364, 343}, {366, 340}, {361, 334},
    {358, 340}, {354, 337}, {362, 321}, {356, 316}, {376, 274}, {372, 266}, {376, 260}, {382, 262},
    {386, 256}, {378, 248}, {384, 244}, {392, 248}, {406, 224}, {406, 211}, {411, 208}, {413, 216},
    {418, 210}, {424, 213}, {416, 233}, {416, 246}, {408, 249}, {408, 255}, {402, 263}, {404, 273},
    {398, 279}, {400, 289}, {396, 296}, {394, 320}, {410, 354}, {436, 390}, {436, 408}, {432, 418},
    {426, 420}, {434, 427}, {426, 428}, {426, 434}, {434, 439}, {428, 443}, {434, 449}, {431, 452},
};
static constexpr int pts_facet_6_cnt = sizeof(pts_facet_6) / sizeof(pts_facet_6[0]);

static const lv_point_t pts_facet_7[] = {
    {406, 272}, {404, 265}, {410, 263}, {407, 258}, {402, 261}, {408, 255}, {408, 249}, {416, 246},
    {418, 228}, {426, 211}, {422, 207}, {428, 202}, {514, 196}, {547, 200}, {556, 207}, {552, 212},
    {552, 234}, {535, 224}, {510, 218}, {445, 224}, {420, 249}, {406, 272},
};
static constexpr int pts_facet_7_cnt = sizeof(pts_facet_7) / sizeof(pts_facet_7[0]);

static const lv_point_t pts_facet_8[] = {
    {490, 436}, {484, 430}, {482, 436}, {479, 430}, {475, 434}, {464, 428}, {453, 436},
    {450, 434}, {451, 428}, {436, 430}, {430, 423}, {433, 416}, {436, 418}, {434, 403},
    {440, 396}, {436, 389}, {454, 396}, {486, 400}, {539, 396}, {545, 426}, {536, 424},
    {537, 432}, {527, 428}, {521, 436}, {518, 426}, {513, 432}, {506, 428}, {490, 436},
};
static constexpr int pts_facet_8_cnt = sizeof(pts_facet_8) / sizeof(pts_facet_8[0]);

static const lv_point_t pts_facet_9[] = {
    {596, 306}, {588, 304}, {576, 269}, {558, 246}, {560, 238}, {554, 234}, {557, 190}, {572, 195},
    {560, 201}, {570, 204}, {562, 209}, {576, 208}, {584, 217}, {576, 227}, {580, 232}, {586, 225},
    {586, 243}, {594, 238}, {586, 237}, {595, 224}, {600, 232}, {594, 237}, {602, 236}, {602, 247},
    {614, 255}, {592, 245}, {588, 253}, {599, 264}, {601, 254}, {606, 263}, {596, 268}, {596, 277},
    {609, 276}, {611, 270}, {603, 274}, {602, 270}, {618, 262}, {624, 285}, {630, 284}, {628, 290},
    {636, 294}, {624, 292}, {624, 302}, {616, 296}, {610, 276}, {602, 282}, {604, 304}, {596, 306},
};
static constexpr int pts_facet_9_cnt = sizeof(pts_facet_9) / sizeof(pts_facet_9[0]);

static const lv_point_t pts_facet_10[] = {
    {323, 898}, {314, 889}, {318, 885}, {312, 884}, {312, 870}, {302, 841}, {308, 838}, {300, 837},
    {290, 796}, {291, 792}, {293, 796}, {307, 788}, {324, 791}, {344, 867}, {323, 898},
};
static constexpr int pts_facet_10_cnt = sizeof(pts_facet_10) / sizeof(pts_facet_10[0]);

// Maximum polygon size (pts_housing has 71 points)
static constexpr int MAX_POLYGON_POINTS = 80;

// ============================================================================
// Helper Functions
// ============================================================================

/// @brief Draw a filled circle using triangle fan (clean, simple rendering)
/// @param layer LVGL draw layer
/// @param cx Center X in screen coordinates
/// @param cy Center Y in screen coordinates
/// @param radius Circle radius in pixels
/// @param color Fill color
/// @param segments Number of segments (more = smoother, 24 is good)
static void draw_circle(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t radius, lv_color_t color,
                        int segments = 24) {
    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);
    tri_dsc.color = color;
    tri_dsc.opa = LV_OPA_COVER;

    for (int i = 0; i < segments; i++) {
        float angle1 = (float)i * 2.0f * 3.14159265f / (float)segments;
        float angle2 = (float)(i + 1) * 2.0f * 3.14159265f / (float)segments;

        tri_dsc.p[0].x = cx;
        tri_dsc.p[0].y = cy;
        tri_dsc.p[1].x = cx + (int32_t)(radius * cosf(angle1));
        tri_dsc.p[1].y = cy + (int32_t)(radius * sinf(angle1));
        tri_dsc.p[2].x = cx + (int32_t)(radius * cosf(angle2));
        tri_dsc.p[2].y = cy + (int32_t)(radius * sinf(angle2));
        lv_draw_triangle(layer, &tri_dsc);
    }
}

/// @brief Scale and translate a polygon from 1000x1000 design space to screen coordinates
/// @param pts_in Source points in design space
/// @param cnt Number of points
/// @param pts_out Output buffer for scaled points (must be at least cnt elements)
/// @param cx Center X in screen coordinates
/// @param cy Center Y in screen coordinates
/// @param scale Scale factor (design_space / screen_size)
// Visual center of the Stealthburner polygon data (not exactly 500,500)
// Measured from bounding box: X=[274,710] midpoint=492, Y=[78,928] midpoint=503
static constexpr int DESIGN_CENTER_X = 492;
static constexpr int DESIGN_CENTER_Y = 500;

static void scale_polygon(const lv_point_t* pts_in, int cnt, lv_point_t* pts_out, int32_t cx,
                          int32_t cy, float scale) {
    for (int i = 0; i < cnt; i++) {
        pts_out[i].x = cx + (int32_t)((pts_in[i].x - DESIGN_CENTER_X) * scale);
        pts_out[i].y = cy + (int32_t)((pts_in[i].y - DESIGN_CENTER_Y) * scale);
    }
}

// ============================================================================
// Ear-Clipping Triangulation for Concave Polygons
// ============================================================================

/// @brief Compute cross product sign for three points (used to determine winding/convexity)
/// @return Positive if CCW turn, negative if CW turn, zero if collinear
static int64_t cross_product_sign(const lv_point_t& a, const lv_point_t& b, const lv_point_t& c) {
    return (int64_t)(b.x - a.x) * (c.y - a.y) - (int64_t)(b.y - a.y) * (c.x - a.x);
}

/// @brief Check if point P is inside triangle ABC using barycentric coordinates
static bool point_in_triangle(const lv_point_t& p, const lv_point_t& a, const lv_point_t& b,
                              const lv_point_t& c) {
    int64_t d1 = cross_product_sign(p, a, b);
    int64_t d2 = cross_product_sign(p, b, c);
    int64_t d3 = cross_product_sign(p, c, a);

    bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

    return !(has_neg && has_pos);
}

/// @brief Check if vertex at index i is convex (interior angle < 180°)
/// @param indices Working list of remaining vertex indices
/// @param idx_cnt Number of vertices remaining
/// @param i Position in indices array to check
/// @param pts Original polygon points
/// @param ccw True if polygon has counter-clockwise winding
static bool is_convex_vertex(const int* indices, int idx_cnt, int i, const lv_point_t* pts,
                             bool ccw) {
    int prev_i = (i - 1 + idx_cnt) % idx_cnt;
    int next_i = (i + 1) % idx_cnt;

    const lv_point_t& prev = pts[indices[prev_i]];
    const lv_point_t& curr = pts[indices[i]];
    const lv_point_t& next = pts[indices[next_i]];

    int64_t cross = cross_product_sign(prev, curr, next);
    return ccw ? (cross > 0) : (cross < 0);
}

/// @brief Check if vertex at index i is an "ear" (can be clipped)
static bool is_ear(const int* indices, int idx_cnt, int i, const lv_point_t* pts, bool ccw) {
    if (!is_convex_vertex(indices, idx_cnt, i, pts, ccw)) {
        return false;
    }

    int prev_i = (i - 1 + idx_cnt) % idx_cnt;
    int next_i = (i + 1) % idx_cnt;

    const lv_point_t& a = pts[indices[prev_i]];
    const lv_point_t& b = pts[indices[i]];
    const lv_point_t& c = pts[indices[next_i]];

    // Check that no other vertices are inside this triangle
    for (int j = 0; j < idx_cnt; j++) {
        if (j == prev_i || j == i || j == next_i)
            continue;
        if (point_in_triangle(pts[indices[j]], a, b, c)) {
            return false;
        }
    }
    return true;
}

/// @brief Draw a filled polygon using ear-clipping triangulation
/// Correctly handles both convex and concave simple polygons
static void draw_polygon(lv_layer_t* layer, const lv_point_t* pts, int cnt, lv_color_t color) {
    if (cnt < 3)
        return;
    if (cnt > MAX_POLYGON_POINTS) {
        // Safety: prevent buffer overflow - truncate to max
        cnt = MAX_POLYGON_POINTS;
    }

    // For very simple polygons, just draw directly
    if (cnt == 3) {
        lv_draw_triangle_dsc_t tri_dsc;
        lv_draw_triangle_dsc_init(&tri_dsc);
        tri_dsc.color = color;
        tri_dsc.opa = LV_OPA_COVER;
        tri_dsc.p[0].x = pts[0].x;
        tri_dsc.p[0].y = pts[0].y;
        tri_dsc.p[1].x = pts[1].x;
        tri_dsc.p[1].y = pts[1].y;
        tri_dsc.p[2].x = pts[2].x;
        tri_dsc.p[2].y = pts[2].y;
        lv_draw_triangle(layer, &tri_dsc);
        return;
    }

    // Determine polygon winding direction (CCW or CW)
    // Sum of (x2-x1)*(y2+y1) - positive = CW, negative = CCW
    int64_t winding_sum = 0;
    for (int i = 0; i < cnt; i++) {
        int next = (i + 1) % cnt;
        winding_sum += (int64_t)(pts[next].x - pts[i].x) * (pts[next].y + pts[i].y);
    }
    bool ccw = (winding_sum < 0);

    // Working list of vertex indices (we'll remove ears as we go)
    int indices[MAX_POLYGON_POINTS];
    for (int i = 0; i < cnt; i++) {
        indices[i] = i;
    }
    int idx_cnt = cnt;

    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);
    tri_dsc.color = color;
    tri_dsc.opa = LV_OPA_COVER;

    // Ear clipping loop
    int safety_counter = cnt * cnt; // Prevent infinite loops
    while (idx_cnt > 3 && safety_counter-- > 0) {
        bool ear_found = false;

        for (int i = 0; i < idx_cnt; i++) {
            if (is_ear(indices, idx_cnt, i, pts, ccw)) {
                // Found an ear - draw the triangle
                int prev_i = (i - 1 + idx_cnt) % idx_cnt;
                int next_i = (i + 1) % idx_cnt;

                tri_dsc.p[0].x = pts[indices[prev_i]].x;
                tri_dsc.p[0].y = pts[indices[prev_i]].y;
                tri_dsc.p[1].x = pts[indices[i]].x;
                tri_dsc.p[1].y = pts[indices[i]].y;
                tri_dsc.p[2].x = pts[indices[next_i]].x;
                tri_dsc.p[2].y = pts[indices[next_i]].y;
                lv_draw_triangle(layer, &tri_dsc);

                // Remove the ear vertex from working list
                for (int j = i; j < idx_cnt - 1; j++) {
                    indices[j] = indices[j + 1];
                }
                idx_cnt--;
                ear_found = true;
                break;
            }
        }

        if (!ear_found) {
            // Fallback: use centroid-based fan for remaining vertices
            // Calculate centroid
            int64_t cx = 0, cy = 0;
            for (int j = 0; j < idx_cnt; j++) {
                cx += pts[indices[j]].x;
                cy += pts[indices[j]].y;
            }
            cx /= idx_cnt;
            cy /= idx_cnt;

            // Draw triangles from centroid to each edge
            for (int j = 0; j < idx_cnt; j++) {
                int next_j = (j + 1) % idx_cnt;
                tri_dsc.p[0].x = (int32_t)cx;
                tri_dsc.p[0].y = (int32_t)cy;
                tri_dsc.p[1].x = pts[indices[j]].x;
                tri_dsc.p[1].y = pts[indices[j]].y;
                tri_dsc.p[2].x = pts[indices[next_j]].x;
                tri_dsc.p[2].y = pts[indices[next_j]].y;
                lv_draw_triangle(layer, &tri_dsc);
            }
            return;
        }
    }

    // Draw final triangle
    if (idx_cnt == 3) {
        tri_dsc.p[0].x = pts[indices[0]].x;
        tri_dsc.p[0].y = pts[indices[0]].y;
        tri_dsc.p[1].x = pts[indices[1]].x;
        tri_dsc.p[1].y = pts[indices[1]].y;
        tri_dsc.p[2].x = pts[indices[2]].x;
        tri_dsc.p[2].y = pts[indices[2]].y;
        lv_draw_triangle(layer, &tri_dsc);
    }
}

// ============================================================================
// Main Drawing Function
// ============================================================================

void draw_nozzle_faceted(lv_layer_t* layer, int32_t cx, int32_t cy, lv_color_t filament_color,
                         int32_t scale_unit, lv_opa_t opa) {
    // The design space is 1000x1000, but the actual toolhead spans about
    // 440 units wide (280-720) and 850 units tall (78-928)
    // Stealthburner is larger than Bambu toolhead, so render at 2x
    int32_t render_size = scale_unit * 10;
    float scale = (float)render_size / 1000.0f;

    // Dim helper: blend color toward black by opa factor (255=full, 0=invisible)
    // This avoids inter-layer bleed that per-draw-call alpha would cause
    auto dim = [opa](lv_color_t c) -> lv_color_t {
        if (opa >= LV_OPA_COVER)
            return c;
        float f = (float)opa / 255.0f;
        return lv_color_make((uint8_t)(c.red * f), (uint8_t)(c.green * f), (uint8_t)(c.blue * f));
    };

    // Body color is ALWAYS Voron red - the toolhead housing doesn't change
    lv_color_t primary = dim(lv_color_hex(0xD11D1D));

    // Nozzle tip color uses filament color when loaded
    // Detect "unloaded" by checking for known idle/nozzle colors
    static constexpr uint32_t NOZZLE_UNLOADED = 0x3A3A3A;
    lv_color_t tip_color = dim(filament_color);
    bool has_filament = !lv_color_eq(filament_color, lv_color_hex(NOZZLE_UNLOADED)) &&
                        !lv_color_eq(filament_color, lv_color_hex(0x808080)) &&
                        !lv_color_eq(filament_color, lv_color_black());

    // Temporary buffer for scaled points
    lv_point_t tmp[MAX_POLYGON_POINTS];

    // Housing (dark frame outline)
    scale_polygon(pts_housing, pts_housing_cnt, tmp, cx, cy, scale);
    draw_polygon(layer, tmp, pts_housing_cnt, dim(lv_color_hex(0x121212)));

    // Main plate (themed primary color)
    scale_polygon(pts_plate, pts_plate_cnt, tmp, cx, cy, scale);
    draw_polygon(layer, tmp, pts_plate_cnt, primary);

    // Facet shading colors using nr_lighten/nr_darken for consistent 3D effect
    // Original SVG colors: highlights=#f55c5b, shadows=#c31615/#bd0f10, deep=#4b1514
    lv_color_t highlight = nr_lighten(primary, 60);  // Bright highlight facets
    lv_color_t mid_shadow = nr_darken(primary, 30);  // Slight shadow
    lv_color_t shadow = nr_darken(primary, 50);      // Medium shadow
    lv_color_t deep_shadow = nr_darken(primary, 80); // Deep shadow (was 120, too black)

    // Facet 1 - highlight (right side, lit by top-left light)
    scale_polygon(pts_facet_1, pts_facet_1_cnt, tmp, cx, cy, scale);
    draw_polygon(layer, tmp, pts_facet_1_cnt, highlight);

    // Facet 2 - shadow (bottom area)
    scale_polygon(pts_facet_2, pts_facet_2_cnt, tmp, cx, cy, scale);
    draw_polygon(layer, tmp, pts_facet_2_cnt, mid_shadow);

    // Facet 3 - deep shadow (fan area shadow)
    scale_polygon(pts_facet_3, pts_facet_3_cnt, tmp, cx, cy, scale);
    draw_polygon(layer, tmp, pts_facet_3_cnt, deep_shadow);

    // Facet 4 - highlight (left side)
    scale_polygon(pts_facet_4, pts_facet_4_cnt, tmp, cx, cy, scale);
    draw_polygon(layer, tmp, pts_facet_4_cnt, highlight);

    // Facet 5 - slight highlight (top area)
    scale_polygon(pts_facet_5, pts_facet_5_cnt, tmp, cx, cy, scale);
    draw_polygon(layer, tmp, pts_facet_5_cnt, nr_lighten(primary, 20));

    // Facet 6 - shadow (left bevel)
    scale_polygon(pts_facet_6, pts_facet_6_cnt, tmp, cx, cy, scale);
    draw_polygon(layer, tmp, pts_facet_6_cnt, shadow);

    // Facet 7 - deep shadow (motor recess area)
    scale_polygon(pts_facet_7, pts_facet_7_cnt, tmp, cx, cy, scale);
    draw_polygon(layer, tmp, pts_facet_7_cnt, deep_shadow);

    // Facet 8 - deep shadow (top center recess)
    scale_polygon(pts_facet_8, pts_facet_8_cnt, tmp, cx, cy, scale);
    draw_polygon(layer, tmp, pts_facet_8_cnt, deep_shadow);

    // Facet 9 - shadow (right bevel)
    scale_polygon(pts_facet_9, pts_facet_9_cnt, tmp, cx, cy, scale);
    draw_polygon(layer, tmp, pts_facet_9_cnt, shadow);

    // Facet 10 - highlight (bottom left)
    scale_polygon(pts_facet_10, pts_facet_10_cnt, tmp, cx, cy, scale);
    draw_polygon(layer, tmp, pts_facet_10_cnt, highlight);

    // Top circle (extruder motor recess)
    scale_polygon(pts_top_circle, pts_top_circle_cnt, tmp, cx, cy, scale);
    draw_polygon(layer, tmp, pts_top_circle_cnt, dim(lv_color_hex(0x100C0B)));

    // Bottom circle (fan) - simple filled circle instead of complex polygon
    // Fan center is at (490, 690) in design space with radius ~115
    int32_t fan_cx = cx + (int32_t)((490 - DESIGN_CENTER_X) * scale);
    int32_t fan_cy = cy + (int32_t)((690 - DESIGN_CENTER_Y) * scale);
    int32_t fan_radius = (int32_t)(115 * scale);
    draw_circle(layer, fan_cx, fan_cy, fan_radius, dim(lv_color_hex(0x100C0B)), 32);

    // Logo stripes (Voron logo)
    scale_polygon(pts_logo_1, pts_logo_1_cnt, tmp, cx, cy, scale);
    draw_polygon(layer, tmp, pts_logo_1_cnt, dim(lv_color_black()));

    scale_polygon(pts_logo_2, pts_logo_2_cnt, tmp, cx, cy, scale);
    draw_polygon(layer, tmp, pts_logo_2_cnt, dim(lv_color_black()));

    scale_polygon(pts_logo_3, pts_logo_3_cnt, tmp, cx, cy, scale);
    draw_polygon(layer, tmp, pts_logo_3_cnt, dim(lv_color_black()));

    // Nozzle tip indicator at bottom (shows filament color when loaded)
    // Position below the Stealthburner body (body bottom is ~Y=898)
    // Y=920 places the tip just below the housing where the nozzle emerges
    // Offset +2px right, -4px up to align with the Stealthburner body's nozzle opening
    int32_t tip_cx = cx;
    int32_t nozzle_top_y = cy + (int32_t)((920 - DESIGN_CENTER_Y) * scale) - 6;
    int32_t nozzle_height = (int32_t)(40 * scale); // Small tip
    int32_t nozzle_top_width = (int32_t)(60 * scale);
    int32_t nozzle_bottom_width = (int32_t)(20 * scale);

    // Draw tapered nozzle tip using common renderer function
    lv_color_t nozzle_metal = dim(lv_color_hex(NOZZLE_UNLOADED));
    lv_color_t tip_left = has_filament ? nr_lighten(tip_color, 30) : nr_lighten(nozzle_metal, 30);
    lv_color_t tip_right = has_filament ? nr_darken(tip_color, 20) : nr_darken(nozzle_metal, 10);
    nr_draw_nozzle_tip(layer, tip_cx, nozzle_top_y, nozzle_top_width, nozzle_bottom_width,
                       nozzle_height, tip_left, tip_right);

    // Bright glint at tip bottom
    lv_draw_fill_dsc_t glint_dsc;
    lv_draw_fill_dsc_init(&glint_dsc);
    glint_dsc.color = dim(lv_color_hex(0xFFFFFF));
    glint_dsc.opa = LV_OPA_70;
    int32_t glint_y = nozzle_top_y + nozzle_height - 1;
    lv_area_t glint = {tip_cx - 1, glint_y, tip_cx + 1, glint_y + 1};
    lv_draw_fill(layer, &glint_dsc, &glint);
}
