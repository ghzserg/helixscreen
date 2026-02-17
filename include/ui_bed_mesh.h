// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "bed_mesh_renderer.h" // For camera angle constants
#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bed mesh canvas dimensions
#define BED_MESH_CANVAS_WIDTH 600
#define BED_MESH_CANVAS_HEIGHT 400

// Rotation angle ranges - use constants from bed_mesh_renderer.h
// These are integer versions for the widget's int-based rotation tracking
#define BED_MESH_ROTATION_X_MIN ((int)BED_MESH_ANGLE_X_MIN)
#define BED_MESH_ROTATION_X_MAX ((int)BED_MESH_ANGLE_X_MAX)
#define BED_MESH_ROTATION_X_DEFAULT ((int)BED_MESH_DEFAULT_ANGLE_X)
#define BED_MESH_ROTATION_Z_MIN 0
#define BED_MESH_ROTATION_Z_MAX 360
// Convert negative angle to positive (0-360 range): -40 -> 320
#define BED_MESH_ROTATION_Z_DEFAULT (360 + (int)BED_MESH_DEFAULT_ANGLE_Z)

/**
 * @brief Register <bed_mesh> widget with LVGL XML system
 *
 * Creates a canvas widget (600×400 RGB888) optimized for 3D bed mesh rendering.
 * Automatically allocates buffer memory and renderer in create handler.
 *
 * Usage in XML:
 * @code{.xml}
 * <bed_mesh name="my_canvas" width="600" height="400"/>
 * @endcode
 */
void ui_bed_mesh_register(void);

/**
 * @brief Set mesh data for rendering
 *
 * Updates the renderer with new mesh height data. Mesh layout is row-major:
 * - mesh[row][col] where row = Y-axis (front to back)
 * - col = X-axis (left to right)
 * - values are absolute Z heights from printer bed
 *
 * @param canvas The bed_mesh canvas widget
 * @param mesh 2D array of height values (row-major)
 * @param rows Number of rows in mesh
 * @param cols Number of columns in mesh
 * @return true on success, false on error (NULL pointer, invalid dimensions)
 */
bool ui_bed_mesh_set_data(lv_obj_t* canvas, const float* const* mesh, int rows, int cols);

/**
 * @brief Set coordinate bounds for bed and mesh
 *
 * The bed bounds define the full print bed area (used for grid/walls).
 * The mesh bounds define where probing occurred (mesh is rendered within these).
 * Call this AFTER set_data() to position the mesh correctly within the bed.
 *
 * Works with any printer origin convention (corner at 0,0 or center at origin).
 *
 * @param canvas The bed_mesh canvas widget
 * @param bed_x_min Full bed minimum X coordinate
 * @param bed_x_max Full bed maximum X coordinate
 * @param bed_y_min Full bed minimum Y coordinate
 * @param bed_y_max Full bed maximum Y coordinate
 * @param mesh_x_min Mesh probe area minimum X coordinate
 * @param mesh_x_max Mesh probe area maximum X coordinate
 * @param mesh_y_min Mesh probe area minimum Y coordinate
 * @param mesh_y_max Mesh probe area maximum Y coordinate
 */
void ui_bed_mesh_set_bounds(lv_obj_t* canvas, double bed_x_min, double bed_x_max, double bed_y_min,
                            double bed_y_max, double mesh_x_min, double mesh_x_max,
                            double mesh_y_min, double mesh_y_max);

/**
 * @brief Set camera rotation angles
 *
 * @param canvas The bed_mesh canvas widget
 * @param angle_x Tilt angle in degrees (typically -85 to -10, negative = looking down)
 * @param angle_z Spin angle in degrees (horizontal rotation around vertical axis)
 */
void ui_bed_mesh_set_rotation(lv_obj_t* canvas, int angle_x, int angle_z);

/**
 * @brief Force redraw of mesh visualization
 *
 * Clears the canvas and re-renders the mesh with current rotation angles.
 *
 * @param canvas The bed_mesh canvas widget
 */
void ui_bed_mesh_redraw(lv_obj_t* canvas);

/**
 * @brief Evaluate render mode based on FPS history
 *
 * Should be called when the bed mesh panel becomes visible (panel entry).
 * Mode evaluation only happens on panel entry, never during viewing,
 * to prevent jarring mode switches while the user is interacting.
 *
 * @param canvas The bed_mesh canvas widget
 */
void ui_bed_mesh_evaluate_render_mode(lv_obj_t* canvas);

/**
 * @brief Get current render mode
 *
 * @param canvas The bed_mesh canvas widget
 * @return Current render mode (AUTO, FORCE_3D, or FORCE_2D)
 */
helix::BedMeshRenderMode ui_bed_mesh_get_render_mode(lv_obj_t* canvas);

/**
 * @brief Set render mode
 *
 * @param canvas The bed_mesh canvas widget
 * @param mode Render mode to use (AUTO, FORCE_3D, or FORCE_2D)
 */
void ui_bed_mesh_set_render_mode(lv_obj_t* canvas, helix::BedMeshRenderMode mode);

/**
 * @brief Show or hide the zero reference plane
 *
 * The zero plane is a translucent reference surface at Z=0 that intersects
 * the mesh, showing where the nozzle touches the bed. Parts of the mesh
 * above the plane obscure it; parts below are visible through the plane.
 *
 * @param canvas The bed_mesh canvas widget
 * @param visible true to show the zero plane, false to hide it
 */
void ui_bed_mesh_set_zero_plane_visible(lv_obj_t* canvas, bool visible);

/**
 * @brief Set Z display offset for axis labels and tooltips
 *
 * When mesh data is normalized, this offset is added back so displayed Z values
 * match the original probe heights.
 *
 * @param canvas The bed_mesh canvas widget
 * @param offset_mm The mean Z value that was subtracted during normalization
 */
void ui_bed_mesh_set_z_display_offset(lv_obj_t* canvas, double offset_mm);

#ifdef __cplusplus
}
#endif
