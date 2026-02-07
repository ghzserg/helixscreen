// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_projection.h"

#include <algorithm>

namespace helix::gcode {

using namespace projection;

// ============================================================================
// PROJECTION
// ============================================================================

glm::ivec2 project(const ProjectionParams& params, float x, float y, float z) {
    float sx, sy;
    const float half_w = static_cast<float>(params.canvas_width) / 2.0f;
    const float half_h = static_cast<float>(params.canvas_height) / 2.0f;

    switch (params.view_mode) {
    case ViewMode::FRONT: {
        // Isometric-style view: 45° horizontal rotation + 30° elevation
        // Creates a "corner view looking down" perspective

        // 90° CCW rotation around Z to match slicer thumbnail orientation
        float raw_dx = x - params.offset_x;
        float raw_dy = y - params.offset_y;
        float dx = -raw_dy; // 90° CCW: new_x = -old_y
        float dy = raw_dx;  // 90° CCW: new_y = old_x
        float dz = z - params.offset_z;

        // Horizontal rotation (around Z axis)
        float rx = dx * kCosH - dy * kSinH;
        float ry = dx * kSinH + dy * kCosH;

        // Elevation (tilt camera down)
        sx = rx * params.scale + half_w;
        sy = half_h - (dz * kCosE + ry * kSinE) * params.scale;
        break;
    }

    case ViewMode::ISOMETRIC: {
        // Isometric projection (45° rotation with Y compression)
        float dx = x - params.offset_x;
        float dy = y - params.offset_y;

        float iso_x = (dx - dy) * kIsoAngle;
        float iso_y = (dx + dy) * kIsoAngle * kIsoYScale;

        sx = iso_x * params.scale + half_w;
        sy = half_h - iso_y * params.scale;
        break;
    }

    case ViewMode::TOP_DOWN:
    default: {
        // Top-down: X → screen X, Y → screen Y (flipped)
        float dx = x - params.offset_x;
        float dy = y - params.offset_y;
        sx = dx * params.scale + half_w;
        sy = half_h - dy * params.scale;
        break;
    }
    }

    // Apply content offset (shifts render for UI overlap - used by layer renderer)
    sy += params.content_offset_y_percent * static_cast<float>(params.canvas_height);

    return {static_cast<int>(sx), static_cast<int>(sy)};
}

// ============================================================================
// AUTO-FIT
// ============================================================================

AutoFitResult compute_auto_fit(const AABB& bb, ViewMode view_mode, int canvas_width,
                               int canvas_height, float padding) {
    AutoFitResult result;

    float range_x, range_y;

    switch (view_mode) {
    case ViewMode::FRONT: {
        float xy_range_x = bb.max.x - bb.min.x;
        float xy_range_y = bb.max.y - bb.min.y;
        float z_range = bb.max.z - bb.min.z;

        // Horizontal extent after 45° rotation (cos(-45°) = cos(45°) = 0.7071)
        range_x = (xy_range_x + xy_range_y) * kCosH;

        // Vertical extent: Z * cos(30°) + Y_depth * sin(30°)
        float y_depth = (xy_range_x + xy_range_y) * kCosH;
        range_y = z_range * kCosE + y_depth * kSinE;

        result.offset_z = (bb.min.z + bb.max.z) / 2.0f;
        break;
    }

    case ViewMode::ISOMETRIC: {
        float xy_range_x = bb.max.x - bb.min.x;
        float xy_range_y = bb.max.y - bb.min.y;
        range_x = (xy_range_x + xy_range_y) * kIsoAngle;
        range_y = (xy_range_x + xy_range_y) * kIsoAngle * kIsoYScale;
        break;
    }

    case ViewMode::TOP_DOWN:
    default:
        range_x = bb.max.x - bb.min.x;
        range_y = bb.max.y - bb.min.y;
        break;
    }

    // Handle degenerate cases
    if (range_x < 0.001f)
        range_x = 1.0f;
    if (range_y < 0.001f)
        range_y = 1.0f;

    // Add padding
    range_x *= (1.0f + 2.0f * padding);
    range_y *= (1.0f + 2.0f * padding);

    // Scale to fit canvas (maintain aspect ratio)
    float scale_x = static_cast<float>(canvas_width) / range_x;
    float scale_y = static_cast<float>(canvas_height) / range_y;
    result.scale = std::min(scale_x, scale_y);

    result.offset_x = (bb.min.x + bb.max.x) / 2.0f;
    result.offset_y = (bb.min.y + bb.max.y) / 2.0f;

    return result;
}

} // namespace helix::gcode
