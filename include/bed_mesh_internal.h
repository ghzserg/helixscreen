// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "bed_mesh_renderer.h"

#include <vector>

/**
 * @file bed_mesh_internal.h
 * @brief Internal structures for bed mesh renderer module
 *
 * This header exposes the internal bed_mesh_renderer struct definition
 * for use by bed mesh rendering modules (overlays, geometry, etc.).
 *
 * DO NOT include this header from UI code - use bed_mesh_renderer.h instead.
 */

/**
 * @brief Renderer lifecycle state
 *
 * State transitions:
 * - UNINITIALIZED → MESH_LOADED: set_mesh_data() called
 * - MESH_LOADED → MESH_LOADED: set_z_scale() or set_color_range() invalidates quads
 * - MESH_LOADED → READY_TO_RENDER: quads generated and projected
 * - READY_TO_RENDER → MESH_LOADED: view state changes (rotation, FOV)
 * - ANY → ERROR: validation failure in public API
 *
 * Invariants:
 * - UNINITIALIZED: has_mesh_data == false, quads.empty()
 * - MESH_LOADED: has_mesh_data == true, quads may be stale (regenerate before render)
 * - READY_TO_RENDER: has_mesh_data == true, quads valid, projections cached
 * - ERROR: renderer unusable, must be destroyed
 */
enum class RendererState {
    UNINITIALIZED,   // Created, no mesh data
    MESH_LOADED,     // Mesh data loaded, quads may need regeneration
    READY_TO_RENDER, // Projection cached, ready for render()
    ERROR            // Invalid state (e.g., set_mesh_data failed)
};

// Internal renderer state structure
struct bed_mesh_renderer {
    // State machine
    RendererState state;

    // Mesh data storage
    std::vector<std::vector<double>> mesh; // mesh[row][col] = Z height
    int rows;
    int cols;
    double mesh_min_z;
    double mesh_max_z;
    double cached_z_center; // (mesh_min_z + mesh_max_z) / 2, updated by compute_mesh_bounds()
    bool has_mesh_data;     // Redundant with state, kept for backwards compatibility

    // Bed XY bounds (full print bed in mm - used for grid/walls)
    double bed_min_x;
    double bed_min_y;
    double bed_max_x;
    double bed_max_y;
    bool has_bed_bounds;

    // Mesh XY bounds (probe area in mm - used for positioning mesh surface)
    double mesh_area_min_x;
    double mesh_area_min_y;
    double mesh_area_max_x;
    double mesh_area_max_y;
    bool has_mesh_bounds;

    // Computed geometry parameters (derived from bounds)
    double bed_center_x;    // (bed_min_x + bed_max_x) / 2
    double bed_center_y;    // (bed_min_y + bed_max_y) / 2
    double coord_scale;     // World units per mm (normalizes bed to target world size)
    bool geometry_computed; // True if bed_center and coord_scale are valid

    // Color range configuration
    bool auto_color_range;
    double color_min_z;
    double color_max_z;

    // View/camera state
    bed_mesh_view_state_t view_state;

    // Computed rendering state
    std::vector<bed_mesh_quad_3d_t> quads; // Generated geometry

    // Cached projected screen coordinates (SOA layout for better cache efficiency)
    // Only stores screen_x/screen_y - no unused fields (world x/y/z, depth)
    // Old AOS: 40 bytes/point (5 doubles + 2 ints), New SOA: 8 bytes/point (2 ints)
    // Memory savings: 80% reduction (16 KB → 3.2 KB for 20×20 mesh)
    std::vector<std::vector<int>> projected_screen_x; // [row][col] → screen X coordinate
    std::vector<std::vector<int>> projected_screen_y; // [row][col] → screen Y coordinate
};
