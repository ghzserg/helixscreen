// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gcode_parser.h"

#include <glm/glm.hpp>

namespace helix::gcode {

// ============================================================================
// VIEW MODES
// ============================================================================

/// View mode for 2D projection of 3D toolpath data.
/// Shared by all renderers (layer renderer, thumbnail renderer, etc.)
enum class ViewMode {
    TOP_DOWN, ///< X/Y plane from above
    FRONT,    ///< Isometric-style: -45° horizontal + 30° elevation (default)
    ISOMETRIC ///< X/Y plane with isometric projection (45° rotation, Y compressed)
};

// ============================================================================
// PROJECTION CONSTANTS
// ============================================================================

/// Projection constants for FRONT view (-45° azimuth, 30° elevation).
/// Matching the default camera angle in GCodeCamera.
namespace projection {

// 90° CCW pre-rotation (applied before horizontal rotation)
// new_x = -old_y, new_y = old_x

// Horizontal rotation: -45° (view from front-right corner)
constexpr float kCosH = 0.7071f;  // cos(45°)
constexpr float kSinH = -0.7071f; // sin(-45°)

// Elevation angle: 30° looking down
constexpr float kCosE = 0.866f; // cos(30°)
constexpr float kSinE = 0.5f;   // sin(30°)

// Isometric constants
constexpr float kIsoAngle = 0.7071f; // cos(45°)
constexpr float kIsoYScale = 0.5f;   // Y compression factor

} // namespace projection

// ============================================================================
// PROJECTION PARAMETERS
// ============================================================================

/// Parameters for world-to-screen coordinate transformation.
/// Captured as a snapshot for thread-safe rendering.
struct ProjectionParams {
    ViewMode view_mode = ViewMode::FRONT;
    float scale = 1.0f;
    float offset_x = 0.0f; ///< World-space center X
    float offset_y = 0.0f; ///< World-space center Y
    float offset_z = 0.0f; ///< World-space center Z (FRONT view only)
    int canvas_width = 0;
    int canvas_height = 0;
    float content_offset_y_percent =
        0.0f; ///< Vertical shift for UI overlap (layer renderer only, 0.0 for thumbnails)
};

// ============================================================================
// PROJECTION FUNCTIONS
// ============================================================================

/// Convert world coordinates to screen pixel coordinates.
///
/// This is the single source of truth for 2D projection across all renderers.
/// Supports TOP_DOWN, FRONT, and ISOMETRIC view modes.
///
/// @param params  Projection parameters (view mode, scale, offsets, canvas size)
/// @param x       World X coordinate (mm)
/// @param y       World Y coordinate (mm)
/// @param z       World Z coordinate (mm) - used by FRONT view
/// @return Screen coordinates in pixels (origin at top-left of canvas)
glm::ivec2 project(const ProjectionParams& params, float x, float y, float z = 0.0f);

/// Result of auto-fit computation.
struct AutoFitResult {
    float scale = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float offset_z = 0.0f;
};

/// Compute projection scale and offsets to fit a bounding box within a canvas.
///
/// @param bb            Bounding box to fit (world coordinates, mm)
/// @param view_mode     Projection mode
/// @param canvas_width  Canvas width in pixels
/// @param canvas_height Canvas height in pixels
/// @param padding       Fractional padding around content (e.g. 0.05 = 5% each side)
/// @return Scale and offset parameters for use with project()
AutoFitResult compute_auto_fit(const AABB& bb, ViewMode view_mode, int canvas_width,
                               int canvas_height, float padding = 0.05f);

} // namespace helix::gcode
