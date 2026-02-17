// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_bed_mesh.h"

#include "ui_fonts.h"
#include "ui_utils.h"

#include "bed_mesh_renderer.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

using namespace helix;

#include <cstdlib>
#include <cstring>
#include <memory>

// Canvas dimensions and rotation defaults are now in ui_bed_mesh.h

/**
 * Widget instance data stored in user_data
 */
typedef struct {
    bed_mesh_renderer_t* renderer; // 3D renderer instance
    int rotation_x;                // Current tilt angle (degrees)
    int rotation_z;                // Current spin angle (degrees)

    // Touch drag state
    bool is_dragging;         // Currently in drag gesture
    lv_point_t last_drag_pos; // Last touch position for delta calculation

    // Deferred redraw state (for panels created while hidden)
    bool had_valid_size;    // Has widget ever had non-zero dimensions
    bool mesh_data_pending; // Mesh data was set before widget had valid size
} bed_mesh_widget_data_t;

/**
 * Draw event handler - renders bed mesh using DRAW_POST pattern
 */
static void bed_mesh_draw_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    bed_mesh_widget_data_t* data = (bed_mesh_widget_data_t*)lv_obj_get_user_data(obj);

    if (!data || !layer) {
        return;
    }

    if (!data->renderer) {
        spdlog::warn("[bed_mesh] draw_cb: renderer not initialized");
        return;
    }

    // Get widget's absolute screen coordinates (stable across partial redraws)
    lv_area_t widget_coords;
    lv_obj_get_coords(obj, &widget_coords);
    int width = lv_area_get_width(&widget_coords);
    int height = lv_area_get_height(&widget_coords);

    spdlog::trace("[bed_mesh] draw_cb: rendering at {}x{}", width, height);

    if (width <= 0 || height <= 0) {
        spdlog::debug("[bed_mesh] draw_cb: invalid dimensions {}x{}", width, height);
        return;
    }

    // Render mesh directly to layer (matches G-code viewer pattern)
    if (!bed_mesh_renderer_render(data->renderer, layer, width, height, widget_coords.x1,
                                  widget_coords.y1)) {
        return;
    }

    spdlog::trace("[bed_mesh] Render complete");
}

/**
 * Touch press event handler - start drag gesture or show 2D tooltip
 */
static void bed_mesh_press_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    bed_mesh_widget_data_t* data = (bed_mesh_widget_data_t*)lv_obj_get_user_data(obj);

    if (!data || !data->renderer)
        return;

    lv_indev_t* indev = lv_indev_active();
    if (!indev)
        return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    // Get widget's absolute screen coordinates (not relative to parent)
    // lv_indev_get_point returns screen coordinates, so we need absolute position
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    int local_x = point.x - obj_coords.x1;
    int local_y = point.y - obj_coords.y1;
    int width = lv_area_get_width(&obj_coords);
    int height = lv_area_get_height(&obj_coords);

    // In 2D mode: show cell tooltip on touch
    if (bed_mesh_renderer_is_using_2d(data->renderer)) {
        if (bed_mesh_renderer_handle_touch(data->renderer, local_x, local_y, width, height)) {
            lv_obj_invalidate(obj); // Redraw to show tooltip
            spdlog::trace("[bed_mesh] 2D touch at ({}, {}) - showing tooltip", local_x, local_y);
        }
        return; // Don't start dragging in 2D mode
    }

    // 3D mode: start drag gesture
    data->is_dragging = true;
    data->last_drag_pos = point;

    // Update renderer dragging state for fast solid-color rendering
    bed_mesh_renderer_set_dragging(data->renderer, true);

    spdlog::trace("[bed_mesh] Press at ({}, {}), switching to solid", point.x, point.y);
}

/**
 * Touch pressing event handler - handle drag for rotation
 */
static void bed_mesh_pressing_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    bed_mesh_widget_data_t* data = (bed_mesh_widget_data_t*)lv_obj_get_user_data(obj);

    if (!data || !data->renderer)
        return;

    lv_indev_t* indev = lv_indev_active();
    if (!indev)
        return;

    // In 2D mode: update tooltip as finger drags across cells
    if (bed_mesh_renderer_is_using_2d(data->renderer)) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);

        // Get widget's absolute screen coordinates
        lv_area_t obj_coords;
        lv_obj_get_coords(obj, &obj_coords);
        int local_x = point.x - obj_coords.x1;
        int local_y = point.y - obj_coords.y1;
        int width = lv_area_get_width(&obj_coords);
        int height = lv_area_get_height(&obj_coords);

        // Update touch position - if cell changed, redraw
        if (bed_mesh_renderer_handle_touch(data->renderer, local_x, local_y, width, height)) {
            lv_obj_invalidate(obj);
        }
        return;
    }

    // 3D mode: handle rotation drag
    if (!data->is_dragging)
        return;

    // Safety check: verify input device is still pressed
    lv_indev_state_t state = lv_indev_get_state(indev);
    if (state != LV_INDEV_STATE_PRESSED) {
        // Input was released but we missed the event - force cleanup
        spdlog::warn("[bed_mesh] Detected missed release event (state={}), forcing gradient mode",
                     (int)state);
        data->is_dragging = false;
        if (data->renderer) {
            bed_mesh_renderer_set_dragging(data->renderer, false);
        }
        lv_obj_invalidate(obj); // Trigger redraw with gradient
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    // Calculate delta from last position
    int dx = point.x - data->last_drag_pos.x;
    int dy = point.y - data->last_drag_pos.y;

    if (dx != 0 || dy != 0) {
        // Convert pixel movement to rotation angles
        // Scale factor: ~0.5 degrees per pixel (matching G-code viewer)
        // Horizontal drag (dx) = spin rotation (rotation_z)
        // Vertical drag (dy) = tilt rotation (rotation_x), inverted for intuitive control
        data->rotation_z += (int)(dx * 0.5f);
        data->rotation_x -= (int)(dy * 0.5f); // Flip Y for intuitive tilt

        // Clamp tilt to configured range (use header-defined limits)
        if (data->rotation_x < BED_MESH_ROTATION_X_MIN)
            data->rotation_x = BED_MESH_ROTATION_X_MIN;
        if (data->rotation_x > BED_MESH_ROTATION_X_MAX)
            data->rotation_x = BED_MESH_ROTATION_X_MAX;

        // Wrap spin around 360 degrees
        data->rotation_z = data->rotation_z % 360;
        if (data->rotation_z < 0)
            data->rotation_z += 360;

        // Update renderer rotation
        if (data->renderer) {
            bed_mesh_renderer_set_rotation(data->renderer, data->rotation_x, data->rotation_z);
        }

        // Trigger redraw
        lv_obj_invalidate(obj);

        data->last_drag_pos = point;

        spdlog::trace("[bed_mesh] Drag ({}, {}) -> rotation({}, {})", dx, dy, data->rotation_x,
                      data->rotation_z);
    }
}

/**
 * Touch release event handler - end drag gesture or hide 2D tooltip
 */
static void bed_mesh_release_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    bed_mesh_widget_data_t* data = (bed_mesh_widget_data_t*)lv_obj_get_user_data(obj);

    if (!data || !data->renderer)
        return;

    // In 2D mode: clear tooltip on release
    if (bed_mesh_renderer_is_using_2d(data->renderer)) {
        bed_mesh_renderer_clear_touch(data->renderer);
        lv_obj_invalidate(obj); // Redraw to hide tooltip
        spdlog::trace("[bed_mesh] 2D touch released - hiding tooltip");
        return;
    }

    // 3D mode: end drag gesture
    data->is_dragging = false;

    // Update renderer dragging state for high-quality gradient rendering
    bed_mesh_renderer_set_dragging(data->renderer, false);

    // Force immediate redraw to switch back to gradient rendering
    lv_obj_invalidate(obj);

    spdlog::trace("[bed_mesh] Release - final rotation({}, {}), switching to gradient",
                  data->rotation_x, data->rotation_z);
}

/**
 * Size changed event handler - update widget on resize
 *
 * Critical for panels created while hidden: When mesh data is set before the
 * widget has valid dimensions, we defer the redraw until SIZE_CHANGED fires
 * with non-zero dimensions.
 */
static void bed_mesh_size_changed_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    bed_mesh_widget_data_t* data = (bed_mesh_widget_data_t*)lv_obj_get_user_data(obj);

    // Get new widget dimensions
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    int width = lv_area_get_width(&coords);
    int height = lv_area_get_height(&coords);

    spdlog::trace("[bed_mesh] SIZE_CHANGED: {}x{}", width, height);

    // Check if this is the first time we have valid dimensions
    if (data && width > 0 && height > 0 && !data->had_valid_size) {
        data->had_valid_size = true;
        spdlog::trace("[bed_mesh] First valid size received");

        // If mesh data was set while widget was 0x0, force a proper redraw now
        if (data->mesh_data_pending) {
            data->mesh_data_pending = false;
            spdlog::info("[bed_mesh] Triggering deferred render after gaining valid size");
        }
    }

    // Trigger redraw with new dimensions
    lv_obj_invalidate(obj);
}

/**
 * Delete event handler - cleanup resources
 */
static void bed_mesh_delete_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);

    // Wrap raw pointer in unique_ptr for RAII cleanup
    std::unique_ptr<bed_mesh_widget_data_t> data(
        static_cast<bed_mesh_widget_data_t*>(lv_obj_get_user_data(obj)));
    lv_obj_set_user_data(obj, nullptr);

    if (data) {
        // Destroy renderer
        if (data->renderer) {
            bed_mesh_renderer_destroy(data->renderer);
            data->renderer = nullptr;
            spdlog::trace("[bed_mesh] Destroyed renderer");
        }
        // data automatically freed via ~unique_ptr()
    }
}

/**
 * XML create handler for <bed_mesh>
 * Creates base object and uses DRAW_POST callback for rendering
 * (Architecture matches G-code viewer for touch event handling)
 */
static void* bed_mesh_xml_create(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);

    void* parent = lv_xml_state_get_parent(state);

    // Create base object (NOT canvas!)
    lv_obj_t* obj = lv_obj_create((lv_obj_t*)parent);
    if (!obj) {
        spdlog::error("[bed_mesh] Failed to create object");
        return nullptr;
    }

    // Configure appearance (transparent background, no border, no padding)
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE); // Touch events work automatically!

    // Allocate widget data struct with RAII (exception-safe)
    auto data_ptr = std::make_unique<bed_mesh_widget_data_t>();

    // Create renderer
    data_ptr->renderer = bed_mesh_renderer_create();
    if (!data_ptr->renderer) {
        spdlog::error("[bed_mesh] Failed to create renderer");
        helix::ui::safe_delete(obj);
        return nullptr; // unique_ptr automatically cleans up
    }

    // Set default rotation angles
    data_ptr->rotation_x = BED_MESH_ROTATION_X_DEFAULT;
    data_ptr->rotation_z = BED_MESH_ROTATION_Z_DEFAULT;
    bed_mesh_renderer_set_rotation(data_ptr->renderer, data_ptr->rotation_x, data_ptr->rotation_z);

    // Check for forced 2D mode via environment variable (for testing)
    const char* force_2d = std::getenv("HELIX_BED_MESH_2D");
    if (force_2d && std::strcmp(force_2d, "1") == 0) {
        bed_mesh_renderer_set_render_mode(data_ptr->renderer, BedMeshRenderMode::Force2D);
        spdlog::info("[bed_mesh] 2D heatmap mode forced via HELIX_BED_MESH_2D=1");
    }

    // Initialize touch drag state
    data_ptr->is_dragging = false;
    data_ptr->last_drag_pos = {0, 0};

    // Initialize deferred redraw state (for panels created while hidden)
    data_ptr->had_valid_size = false;
    data_ptr->mesh_data_pending = false;

    // Transfer ownership to LVGL user_data (will be cleaned up in delete callback)
    lv_obj_set_user_data(obj, data_ptr.release());

    // Register event handlers
    lv_obj_add_event_cb(obj, bed_mesh_draw_cb, LV_EVENT_DRAW_POST, nullptr); // Custom drawing
    lv_obj_add_event_cb(obj, bed_mesh_size_changed_cb, LV_EVENT_SIZE_CHANGED,
                        nullptr);                                           // Handle resize
    lv_obj_add_event_cb(obj, bed_mesh_delete_cb, LV_EVENT_DELETE, nullptr); // Cleanup

    // Register touch event handlers for drag rotation
    lv_obj_add_event_cb(obj, bed_mesh_press_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(obj, bed_mesh_pressing_cb, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(obj, bed_mesh_release_cb, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(obj, bed_mesh_release_cb, LV_EVENT_PRESS_LOST,
                        nullptr); // Handle drag outside widget

    // Set default size (will be overridden by XML width/height attributes)
    lv_obj_set_size(obj, BED_MESH_CANVAS_WIDTH, BED_MESH_CANVAS_HEIGHT);

    spdlog::debug("[bed_mesh] Created widget with DRAW_POST pattern, renderer initialized");

    return (void*)obj;
}

/**
 * XML apply handler for <bed_mesh>
 * Applies standard lv_obj attributes from XML
 */
static void bed_mesh_xml_apply(lv_xml_parser_state_t* state, const char** attrs) {
    void* item = lv_xml_state_get_item(state);
    lv_obj_t* obj = (lv_obj_t*)item;

    if (!obj) {
        spdlog::error("[bed_mesh] NULL object in xml_apply");
        return;
    }

    // Apply standard lv_obj properties from XML (size, style, align, etc.)
    lv_xml_obj_apply(state, attrs);

    spdlog::trace("[bed_mesh] Applied XML attributes");
}

/**
 * Register <bed_mesh> widget with LVGL XML system
 */
void ui_bed_mesh_register(void) {
    lv_xml_register_widget("bed_mesh", bed_mesh_xml_create, bed_mesh_xml_apply);
    spdlog::trace("[bed_mesh] Registered <bed_mesh> widget with XML system");
}

/**
 * Set mesh data for rendering
 *
 * If the widget hasn't been laid out yet (0x0 dimensions), the mesh data is
 * stored in the renderer but actual rendering is deferred until SIZE_CHANGED
 * fires with valid dimensions.
 */
bool ui_bed_mesh_set_data(lv_obj_t* widget, const float* const* mesh, int rows, int cols) {
    if (!widget) {
        spdlog::error("[bed_mesh] ui_bed_mesh_set_data: NULL widget");
        return false;
    }

    bed_mesh_widget_data_t* data = (bed_mesh_widget_data_t*)lv_obj_get_user_data(widget);
    if (!data || !data->renderer) {
        spdlog::error("[bed_mesh] ui_bed_mesh_set_data: widget data or renderer not initialized");
        return false;
    }

    if (!mesh || rows <= 0 || cols <= 0) {
        spdlog::error("[bed_mesh] ui_bed_mesh_set_data: invalid mesh data (rows={}, cols={})", rows,
                      cols);
        return false;
    }

    // Set mesh data in renderer
    if (!bed_mesh_renderer_set_mesh_data(data->renderer, mesh, rows, cols)) {
        spdlog::error("[bed_mesh] Failed to set mesh data in renderer");
        return false;
    }

    // Check if widget has valid dimensions yet
    int width = lv_obj_get_width(widget);
    int height = lv_obj_get_height(widget);

    if (width <= 0 || height <= 0) {
        // Widget hasn't been laid out yet - defer rendering to SIZE_CHANGED
        data->mesh_data_pending = true;
        spdlog::info("[bed_mesh] Mesh data loaded: {}x{} (deferred - widget {}x{})", rows, cols,
                     width, height);
    } else {
        data->mesh_data_pending = false;
        spdlog::info("[bed_mesh] Mesh data loaded: {}x{}", rows, cols);
    }

    // Request redraw (will succeed if widget has valid size, otherwise deferred)
    ui_bed_mesh_redraw(widget);

    return true;
}

/**
 * Set coordinate bounds for bed and mesh
 */
void ui_bed_mesh_set_bounds(lv_obj_t* widget, double bed_x_min, double bed_x_max, double bed_y_min,
                            double bed_y_max, double mesh_x_min, double mesh_x_max,
                            double mesh_y_min, double mesh_y_max) {
    if (!widget) {
        spdlog::error("[bed_mesh] ui_bed_mesh_set_bounds: NULL widget");
        return;
    }

    bed_mesh_widget_data_t* data = (bed_mesh_widget_data_t*)lv_obj_get_user_data(widget);
    if (!data || !data->renderer) {
        spdlog::error("[bed_mesh] ui_bed_mesh_set_bounds: widget data or renderer not initialized");
        return;
    }

    bed_mesh_renderer_set_bounds(data->renderer, bed_x_min, bed_x_max, bed_y_min, bed_y_max,
                                 mesh_x_min, mesh_x_max, mesh_y_min, mesh_y_max);

    // Request redraw to show updated bounds
    ui_bed_mesh_redraw(widget);
}

/**
 * Set camera rotation angles
 */
void ui_bed_mesh_set_rotation(lv_obj_t* widget, int angle_x, int angle_z) {
    if (!widget) {
        spdlog::error("[bed_mesh] ui_bed_mesh_set_rotation: NULL widget");
        return;
    }

    bed_mesh_widget_data_t* data = (bed_mesh_widget_data_t*)lv_obj_get_user_data(widget);
    if (!data || !data->renderer) {
        spdlog::error("[bed_mesh] ui_bed_mesh_set_rotation: widget data or renderer not "
                      "initialized");
        return;
    }

    // Update stored rotation angles
    data->rotation_x = angle_x;
    data->rotation_z = angle_z;

    // Update renderer
    bed_mesh_renderer_set_rotation(data->renderer, angle_x, angle_z);

    spdlog::debug("[bed_mesh] Rotation updated: tilt={}°, spin={}°", angle_x, angle_z);

    // Automatically redraw after rotation change
    ui_bed_mesh_redraw(widget);
}

/**
 * Force redraw of mesh visualization
 */
void ui_bed_mesh_redraw(lv_obj_t* widget) {
    if (!widget) {
        spdlog::warn("[bed_mesh] ui_bed_mesh_redraw: NULL widget");
        return;
    }

    // Trigger DRAW_POST event by invalidating widget
    lv_obj_invalidate(widget);

    spdlog::debug("[bed_mesh] Redraw requested");
}

/**
 * Evaluate and possibly switch render mode based on FPS history
 *
 * Should be called when the bed mesh panel becomes visible.
 * Mode evaluation only happens on panel entry, never during viewing.
 */
void ui_bed_mesh_evaluate_render_mode(lv_obj_t* widget) {
    if (!widget) {
        return;
    }

    bed_mesh_widget_data_t* data = (bed_mesh_widget_data_t*)lv_obj_get_user_data(widget);
    if (!data || !data->renderer) {
        return;
    }

    bed_mesh_renderer_evaluate_render_mode(data->renderer);
}

/**
 * Get current render mode for display in settings
 */
BedMeshRenderMode ui_bed_mesh_get_render_mode(lv_obj_t* widget) {
    if (!widget) {
        return BedMeshRenderMode::Auto;
    }

    bed_mesh_widget_data_t* data = (bed_mesh_widget_data_t*)lv_obj_get_user_data(widget);
    if (!data || !data->renderer) {
        return BedMeshRenderMode::Auto;
    }

    return bed_mesh_renderer_get_render_mode(data->renderer);
}

/**
 * Set render mode (for settings UI)
 */
void ui_bed_mesh_set_render_mode(lv_obj_t* widget, BedMeshRenderMode mode) {
    if (!widget) {
        return;
    }

    bed_mesh_widget_data_t* data = (bed_mesh_widget_data_t*)lv_obj_get_user_data(widget);
    if (!data || !data->renderer) {
        return;
    }

    bed_mesh_renderer_set_render_mode(data->renderer, mode);
    lv_obj_invalidate(widget); // Redraw with new mode
}

/**
 * Show or hide the zero reference plane
 */
void ui_bed_mesh_set_zero_plane_visible(lv_obj_t* widget, bool visible) {
    if (!widget) {
        return;
    }

    bed_mesh_widget_data_t* data = (bed_mesh_widget_data_t*)lv_obj_get_user_data(widget);
    if (!data || !data->renderer) {
        return;
    }

    bed_mesh_renderer_set_zero_plane_visible(data->renderer, visible);
    lv_obj_invalidate(widget); // Redraw with updated plane visibility
}

/**
 * Set Z display offset for axis labels
 *
 * When mesh data is normalized (mean-subtracted), this offset is added back
 * so axis labels and tooltips show original probe heights.
 */
void ui_bed_mesh_set_z_display_offset(lv_obj_t* widget, double offset_mm) {
    if (!widget) {
        return;
    }

    bed_mesh_widget_data_t* data = (bed_mesh_widget_data_t*)lv_obj_get_user_data(widget);
    if (!data || !data->renderer) {
        return;
    }

    bed_mesh_renderer_set_z_display_offset(data->renderer, offset_mm);
}
