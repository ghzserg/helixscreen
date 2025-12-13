// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_step_progress.h"

#include "ui_fonts.h" // For mdi_icons_16 font
#include "ui_theme.h"
#include "ui_widget_memory.h"

#include <spdlog/spdlog.h>

#include <stdlib.h>
#include <string.h>

// Internal widget data stored in user_data
typedef struct {
    char** label_buffers;    // String storage for labels
    ui_step_state_t* states; // Current state for each step
    int step_count;
    bool horizontal;
} step_progress_data_t;

// Theme-aware color variables (loaded from component scope or defaults)
static lv_color_t color_pending;
static lv_color_t color_active;
static lv_color_t color_completed;
static lv_color_t color_number_pending;
static lv_color_t color_number_active;
static lv_color_t color_label_active;
static lv_color_t color_label_inactive;

// Initialize colors from component scope
static void init_step_progress_colors(const char* scope_name) {
    lv_xml_component_scope_t* scope = scope_name ? lv_xml_component_get_scope(scope_name) : nullptr;
    bool use_dark_mode = ui_theme_is_dark_mode();

    if (scope) {
        // Read theme-aware colors from component scope
        const char* pending =
            lv_xml_get_const(scope, use_dark_mode ? "step_pending_dark" : "step_pending_light");
        const char* active =
            lv_xml_get_const(scope, use_dark_mode ? "step_active_dark" : "step_active_light");
        const char* completed =
            lv_xml_get_const(scope, use_dark_mode ? "step_completed_dark" : "step_completed_light");
        const char* num_pending = lv_xml_get_const(
            scope, use_dark_mode ? "step_number_pending_dark" : "step_number_pending_light");
        const char* num_active = lv_xml_get_const(
            scope, use_dark_mode ? "step_number_active_dark" : "step_number_active_light");
        const char* lbl_active = lv_xml_get_const(scope, use_dark_mode ? "step_label_active_dark"
                                                                       : "step_label_active_light");
        const char* lbl_inactive = lv_xml_get_const(
            scope, use_dark_mode ? "step_label_inactive_dark" : "step_label_inactive_light");

        color_pending = ui_theme_parse_color(pending ? pending : "#808080");
        color_active = ui_theme_parse_color(active ? active : "#FF4444");
        color_completed = ui_theme_parse_color(completed ? completed : "#4CAF50");
        color_number_pending = ui_theme_parse_color(
            num_pending ? num_pending : (use_dark_mode ? "#000000" : "#FFFFFF"));
        color_number_active = ui_theme_parse_color(num_active ? num_active : "#FFFFFF");
        color_label_active =
            ui_theme_parse_color(lbl_active ? lbl_active : (use_dark_mode ? "#FFFFFF" : "#000000"));
        color_label_inactive = ui_theme_parse_color(
            lbl_inactive ? lbl_inactive : (use_dark_mode ? "#CCCCCC" : "#666666"));

        spdlog::debug("[StepProgress] Colors loaded from scope '{}' for {} mode", scope_name,
                      use_dark_mode ? "dark" : "light");
    } else {
        // Fallback to theme token defaults
        color_pending = ui_theme_get_color("step_pending");
        color_active = ui_theme_get_color("step_active");
        color_completed = ui_theme_get_color("step_completed");
        color_number_pending =
            use_dark_mode ? ui_theme_get_color("ams_hub_dark") : lv_color_white();
        color_number_active = lv_color_white();
        color_label_active = use_dark_mode ? lv_color_white() : ui_theme_get_color("ams_hub_dark");
        color_label_inactive = ui_theme_get_color(use_dark_mode ? "step_label_inactive_dark"
                                                                : "step_label_inactive_light");

        spdlog::debug("[StepProgress] Using fallback colors for {} mode",
                      use_dark_mode ? "dark" : "light");
    }
}

/**
 * Apply state-based styling to a step item's indicator and label
 */
static void apply_step_styling(lv_obj_t* step_item, ui_step_state_t state) {
    if (!step_item)
        return;

    // Find widgets by traversing children (known structure)
    lv_obj_t* indicator_column = lv_obj_get_child(step_item, 0);
    if (!indicator_column)
        return;

    lv_obj_t* circle = lv_obj_get_child(indicator_column, 0);
    lv_obj_t* connector = (lv_obj_get_child_count(indicator_column) > 1)
                              ? lv_obj_get_child(indicator_column, 1)
                              : nullptr;
    lv_obj_t* step_number = circle ? lv_obj_get_child(circle, 0) : nullptr;
    lv_obj_t* checkmark = circle ? lv_obj_get_child(circle, 1) : nullptr;
    lv_obj_t* label = lv_obj_get_child(step_item, 1);

    lv_color_t color;
    lv_opa_t fill_opa;

    switch (state) {
    case UI_STEP_STATE_PENDING:
        color = color_pending;
        fill_opa = LV_OPA_COVER; // Filled gray circle
        break;
    case UI_STEP_STATE_ACTIVE:
        color = color_active;
        fill_opa = LV_OPA_COVER; // Filled red circle
        break;
    case UI_STEP_STATE_COMPLETED:
        color = color_completed;
        fill_opa = LV_OPA_COVER; // Fully filled circle (255)
        break;
    default:
        color = color_pending;
        fill_opa = LV_OPA_COVER;
        break;
    }

    // Apply circle styling
    if (circle) {
        lv_obj_set_style_border_color(circle, color, 0);
        lv_obj_set_style_bg_color(circle, color, 0);
        lv_obj_set_style_bg_opa(circle, fill_opa, 0);
    }

    // Toggle step number and checkmark visibility, and set number color
    if (state == UI_STEP_STATE_COMPLETED) {
        // Completed: hide step number, show checkmark
        if (step_number)
            lv_obj_add_flag(step_number, LV_OBJ_FLAG_HIDDEN);
        if (checkmark)
            lv_obj_clear_flag(checkmark, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Pending/Active: show step number, hide checkmark
        if (step_number) {
            lv_obj_clear_flag(step_number, LV_OBJ_FLAG_HIDDEN);
            // Use theme-aware number colors
            lv_color_t number_color =
                (state == UI_STEP_STATE_PENDING) ? color_number_pending : color_number_active;
            lv_obj_set_style_text_color(step_number, number_color, 0);
        }
        if (checkmark)
            lv_obj_add_flag(checkmark, LV_OBJ_FLAG_HIDDEN);
    }

    // Apply vertical connector line color (inside indicator_column for vertical layout)
    if (connector) {
        lv_obj_set_style_bg_color(connector, color, 0);
    }

    // Note: Horizontal connectors are now siblings in the main container, not children of step_item
    // They will be updated separately in ui_step_progress_set_current()

    // Apply label styling (larger + brighter for active, dimmed for others)
    if (label) {
        if (state == UI_STEP_STATE_ACTIVE) {
            lv_obj_set_style_text_font(label, UI_FONT_HEADING, 0);     // Larger
            lv_obj_set_style_text_color(label, color_label_active, 0); // Theme-aware active color
        } else {
            lv_obj_set_style_text_font(label, UI_FONT_BODY, 0); // Normal size
            lv_obj_set_style_text_color(label, color_label_inactive,
                                        0); // Theme-aware inactive color
        }
    }
}

/**
 * Cleanup callback for widget deletion
 */
static void step_progress_delete_cb(lv_event_t* e) {
    lv_obj_t* widget = lv_event_get_target_obj(e);
    // Transfer ownership to RAII wrapper - automatic cleanup
    lvgl_unique_ptr<step_progress_data_t> data((step_progress_data_t*)lv_obj_get_user_data(widget));
    lv_obj_set_user_data(widget, nullptr);

    if (data) {
        // Free nested allocations
        if (data->label_buffers) {
            for (int i = 0; i < data->step_count; i++) {
                // Use RAII for each label buffer
                lvgl_unique_ptr<char> label(data->label_buffers[i]);
            }
            // Wrap array itself
            lvgl_unique_ptr<char*> buffers(data->label_buffers);
        }

        if (data->states) {
            lvgl_unique_ptr<ui_step_state_t> states(data->states);
        }
        // data itself automatically freed via ~unique_ptr()
    }
}

lv_obj_t* ui_step_progress_create(lv_obj_t* parent, const ui_step_t* steps, int step_count,
                                  bool horizontal, const char* scope_name) {
    if (!parent || !steps || step_count <= 0) {
        spdlog::error("[Step Progress] Invalid parameters for step progress widget");
        return nullptr;
    }

    // Initialize theme-aware colors from component scope
    init_step_progress_colors(scope_name);

    // Allocate widget data using RAII
    auto data_ptr = lvgl_make_unique<step_progress_data_t>();
    if (!data_ptr) {
        spdlog::error("[Step Progress] Failed to allocate step progress data");
        return nullptr;
    }

    step_progress_data_t* data = data_ptr.get();
    data->step_count = step_count;
    data->horizontal = horizontal;

    // Allocate arrays using RAII
    auto label_buffers_ptr = lvgl_make_unique_array<char*>(static_cast<size_t>(step_count));
    auto states_ptr = lvgl_make_unique_array<ui_step_state_t>(static_cast<size_t>(step_count));

    if (!label_buffers_ptr || !states_ptr) {
        spdlog::error("[Step Progress] Failed to allocate step data arrays");
        return nullptr;
    }

    data->label_buffers = label_buffers_ptr.get();
    data->states = states_ptr.get();

    // Copy initial data
    for (int i = 0; i < step_count; i++) {
        auto label_buf = lvgl_make_unique_array<char>(128);
        if (label_buf) {
            snprintf(label_buf.get(), 128, "%s", steps[i].label);
            data->label_buffers[i] = label_buf.release();
        } else {
            data->label_buffers[i] = nullptr;
        }
        data->states[i] = steps[i].state;
    }

    // Release ownership of nested arrays (now owned by data struct)
    label_buffers_ptr.release();
    states_ptr.release();

    // Create container widget
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_size(container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_flex_flow(container, horizontal ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    if (horizontal) {
        lv_obj_set_style_pad_column(container, 0, 0); // No gap - connectors fill space
    } else {
        lv_obj_set_style_pad_row(container, 24, 0); // 24px spacing between vertical steps
    }

    // Transfer ownership to LVGL widget
    lv_obj_set_user_data(container, data_ptr.release());

    // Register cleanup callback
    lv_obj_add_event_cb(container, step_progress_delete_cb, LV_EVENT_DELETE, nullptr);

    // Create step items programmatically
    for (int i = 0; i < step_count; i++) {
        // Create step item container
        lv_obj_t* step_item = lv_obj_create(container);
        // Set explicit minimum width to bootstrap layout calculation
        // Horizontal: minimum width for label text, Vertical: use full parent width
        if (horizontal) {
            lv_obj_set_width(step_item, LV_SIZE_CONTENT); // Auto-size to content
            lv_obj_set_flex_grow(step_item, 1);           // Distribute space evenly across steps
        } else {
            lv_obj_set_width(step_item, LV_PCT(100)); // Vertical: responsive to parent
        }
        lv_obj_set_height(step_item, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(step_item, LV_OPA_0, 0);
        lv_obj_set_style_border_width(step_item, 0, 0);
        lv_obj_set_style_pad_all(step_item, 0, 0);
        lv_obj_set_style_pad_gap(step_item, 0, 0); // No gap
        // Horizontal: column layout (circle + connector + label stacked), Vertical: row layout
        lv_obj_set_flex_flow(step_item, horizontal ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
        // Center labels beneath circles in horizontal mode, vertically center in vertical mode
        lv_obj_set_flex_align(step_item, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, // Center cross-axis for both orientations
                              LV_FLEX_ALIGN_START);

        // Create indicator column (circle + connector line)
        lv_obj_t* indicator_column = lv_obj_create(step_item);
        if (horizontal) {
            lv_obj_set_size(indicator_column, 24, LV_SIZE_CONTENT); // Horizontal: just the circle
        } else {
            // Vertical: circle only (connectors are positioned separately)
            lv_obj_set_size(indicator_column, 24, 24); // step_indicator_size
        }
        lv_obj_set_style_bg_opa(indicator_column, LV_OPA_0, 0);
        lv_obj_set_style_border_width(indicator_column, 0, 0);
        lv_obj_set_style_pad_all(indicator_column, 0, 0);
        lv_obj_set_style_pad_gap(indicator_column, 0, 0); // No gap between circle and connector
        lv_obj_set_flex_flow(indicator_column, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(indicator_column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_START);

        // Create circle indicator
        lv_obj_t* circle = lv_obj_create(indicator_column);
        lv_obj_set_size(circle, 24, 24); // step_indicator_size
        lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(circle, 2, 0);
        lv_obj_set_style_pad_all(circle, 0, 0);
        lv_obj_set_style_margin_all(circle, 0, 0); // No margin around circle

        // Create step number label (shown for PENDING/ACTIVE states)
        lv_obj_t* step_number = lv_label_create(circle);
        char num_buf[16]; // Increased buffer size to handle larger step numbers
        snprintf(num_buf, sizeof(num_buf), "%d", i + 1); // 1-indexed step numbers
        lv_label_set_text(step_number, num_buf);
        lv_obj_align(step_number, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_font(step_number, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(step_number, color_number_active,
                                    0); // Theme-aware, will be updated by apply_step_styling()

        // Create checkmark label (shown for COMPLETED state)
        // Use MDI check icon (F012C) instead of LV_SYMBOL_OK which isn't in our fonts
        lv_obj_t* checkmark = lv_label_create(circle);
        lv_label_set_text(checkmark, "\xF3\xB0\x84\xAC"); // MDI check icon (F012C)
        lv_obj_align(checkmark, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_font(checkmark, &mdi_icons_16, 0); // Use MDI icon font
        lv_obj_set_style_text_color(checkmark, color_number_active,
                                    0);                 // Theme-aware checkmark color
        lv_obj_add_flag(checkmark, LV_OBJ_FLAG_HIDDEN); // Hidden by default

        // Vertical connectors will be created dynamically after layout

        // Create step label
        lv_obj_t* label = lv_label_create(step_item);
        lv_label_set_text(label, data->label_buffers[i]);
        lv_obj_set_style_text_color(label, color_label_inactive,
                                    0); // Theme-aware, will be updated by apply_step_styling()
        if (horizontal) {
            // Horizontal: label below circle, centered
            lv_obj_set_width(label, LV_SIZE_CONTENT);          // Auto-size to text
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP); // Enable wrapping if needed
            lv_obj_set_style_pad_top(label, 8, 0);             // Space between circle and label
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_max_width(label, 120, 0); // Max width for wrapping long labels
        } else {
            // Vertical: label to right of circle
            lv_obj_set_style_pad_left(label, 16, 0); // step_spacing
            lv_obj_set_style_pad_top(label, 2, 0);
            lv_obj_set_flex_grow(label, 1);
        }

        // Apply initial styling based on state
        apply_step_styling(step_item, steps[i].state);

        // Horizontal connectors will be created dynamically after layout
    }

    // For vertical layout, create connector lines AFTER layout is calculated
    // This allows us to measure actual positions and draw lines that properly connect circles
    if (!horizontal) {
        lv_obj_update_layout(container); // Force layout calculation

        spdlog::debug("[Step Progress] Creating vertical connectors for {} steps", step_count);

        for (int i = 0; i < step_count - 1; i++) {
            lv_obj_t* current_step = lv_obj_get_child(container, i);
            lv_obj_t* next_step = lv_obj_get_child(container, i + 1);

            if (current_step && next_step) {
                // Get positions after layout
                lv_coord_t current_y = lv_obj_get_y(current_step);
                lv_coord_t next_y = lv_obj_get_y(next_step);

                // Calculate connector position and height
                // Circle is 24px with 2px border - connector should overlap slightly to eliminate
                // gaps
                lv_coord_t connector_y = current_y + 22; // Start 2px before bottom edge
                lv_coord_t connector_height =
                    (next_y - connector_y) + 2; // Extend 2px into next circle

                spdlog::debug("[Step Progress] Connector {}: current_y={}, next_y={}, "
                              "connector_y={}, height={}",
                              i, current_y, next_y, connector_y, connector_height);

                // Create connector line
                lv_obj_t* connector = lv_obj_create(container);
                lv_obj_remove_flag(connector,
                                   LV_OBJ_FLAG_FLEX_IN_NEW_TRACK); // Remove from flex layout
                lv_obj_set_size(connector, 1, connector_height);
                lv_obj_set_pos(connector, 11,
                               connector_y); // x=11 (center of 24px indicator, 1px line)
                lv_obj_set_style_bg_opa(connector, LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(connector, 0, 0);
                lv_obj_set_style_pad_all(connector, 0, 0);
                lv_obj_set_style_radius(connector, 0, 0);
                lv_obj_add_flag(connector, LV_OBJ_FLAG_IGNORE_LAYOUT); // Don't let flex move this
                // Connector color: green only for COMPLETED steps, gray for all others
                lv_color_t connector_color =
                    (steps[i].state == UI_STEP_STATE_COMPLETED) ? color_completed : color_pending;
                lv_obj_set_style_bg_color(connector, connector_color, 0);

                spdlog::debug("[Step Progress] Connector {} created at pos ({}, {}) size ({}, {})",
                              i, lv_obj_get_x(connector), lv_obj_get_y(connector),
                              lv_obj_get_width(connector), lv_obj_get_height(connector));
            }
        }
    } else {
        // For horizontal layout, create connector lines AFTER layout is calculated
        lv_obj_update_layout(container);

        for (int i = 0; i < step_count - 1; i++) {
            lv_obj_t* current_step = lv_obj_get_child(container, i);
            lv_obj_t* next_step = lv_obj_get_child(container, i + 1);

            if (current_step && next_step) {
                // Get indicator_column widgets (first child of each step_item, contains circle)
                lv_obj_t* current_indicator = lv_obj_get_child(current_step, 0);
                lv_obj_t* next_indicator = lv_obj_get_child(next_step, 0);

                if (!current_indicator || !next_indicator) {
                    spdlog::warn(
                        "[Step Progress] Missing indicator widget for horizontal connector {}", i);
                    continue;
                }

                // Get circle widgets (first child of indicator_column)
                lv_obj_t* current_circle = lv_obj_get_child(current_indicator, 0);
                lv_obj_t* next_circle = lv_obj_get_child(next_indicator, 0);

                if (!current_circle || !next_circle) {
                    spdlog::warn(
                        "[Step Progress] Missing circle widget for horizontal connector {}", i);
                    continue;
                }

                // Get actual X positions of circles and container
                lv_coord_t current_circle_x = lv_obj_get_x(current_step) +
                                              lv_obj_get_x(current_indicator) +
                                              lv_obj_get_x(current_circle);
                lv_coord_t next_circle_x = lv_obj_get_x(next_step) + lv_obj_get_x(next_indicator) +
                                           lv_obj_get_x(next_circle);

                // Calculate circle center X positions (circle is 24px wide)
                lv_coord_t current_circle_center_x = current_circle_x + 12;
                lv_coord_t next_circle_center_x = next_circle_x + 12;

                // Position connector to touch circle edges accounting for 2px border
                // Circles are 24px diameter (12px radius) with 2px border drawn inside
                // Visual edge is at radius + border_half = 12 + 1 = 13px from center
                lv_coord_t connector_x =
                    current_circle_center_x + 13; // Right edge including border
                lv_coord_t connector_end_x =
                    next_circle_center_x - 13; // Left edge including border
                lv_coord_t connector_width = connector_end_x - connector_x;
                lv_coord_t connector_y =
                    lv_obj_get_y(current_circle) + 11; // Vertically centered with 24px circle

                lv_obj_t* connector = lv_obj_create(container);
                lv_obj_remove_flag(connector,
                                   LV_OBJ_FLAG_FLEX_IN_NEW_TRACK); // Remove from flex layout
                lv_obj_set_size(connector, connector_width, 1);
                lv_obj_set_pos(connector, connector_x, connector_y);
                lv_obj_set_style_bg_opa(connector, LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(connector, 0, 0);
                lv_obj_set_style_pad_all(connector, 0, 0);
                lv_obj_set_style_radius(connector, 0, 0);
                lv_obj_add_flag(connector, LV_OBJ_FLAG_IGNORE_LAYOUT); // Don't let flex move this

                // Connector color: green only for COMPLETED steps, gray for all others
                lv_color_t connector_color =
                    (steps[i].state == UI_STEP_STATE_COMPLETED) ? color_completed : color_pending;
                lv_obj_set_style_bg_color(connector, connector_color, 0);

                spdlog::debug(
                    "[Step Progress] Horizontal connector {}: circle_centers=({}, {}), connector "
                    "pos=({}, {}) size=({}, {})",
                    i, current_circle_center_x, next_circle_center_x, lv_obj_get_x(connector),
                    lv_obj_get_y(connector), lv_obj_get_width(connector),
                    lv_obj_get_height(connector));
            }
        }
    }

    return container;
}

void ui_step_progress_set_current(lv_obj_t* widget, int step_index) {
    if (!widget)
        return;

    step_progress_data_t* data = (step_progress_data_t*)lv_obj_get_user_data(widget);
    if (!data || step_index < 0 || step_index >= data->step_count) {
        spdlog::warn("[Step Progress] Invalid step index: {}", step_index);
        return;
    }

    // Mark all previous steps as completed
    for (int i = 0; i < step_index; i++) {
        data->states[i] = UI_STEP_STATE_COMPLETED;
    }

    // Mark current step as active
    data->states[step_index] = UI_STEP_STATE_ACTIVE;

    // Mark remaining steps as pending
    for (int i = step_index + 1; i < data->step_count; i++) {
        data->states[i] = UI_STEP_STATE_PENDING;
    }

    // Update styling for all steps and horizontal connectors
    uint32_t child_count = lv_obj_get_child_count(widget);
    int step_item_index = 0;
    int connector_index = 0;
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(widget, static_cast<int32_t>(i));

        // Check if this is a step_item (has children: indicator_column + label)
        if (lv_obj_get_child_count(child) >= 2) {
            if (step_item_index < data->step_count) {
                apply_step_styling(child, data->states[step_item_index]);
                step_item_index++;
            }
        }
        // Otherwise it's a horizontal connector - update its color based on the step it connects
        // FROM
        else if (data->horizontal && connector_index < data->step_count - 1) {
            // Connector N connects FROM step N (using step N's state for color)
            lv_color_t connector_color = (data->states[connector_index] == UI_STEP_STATE_COMPLETED)
                                             ? color_completed
                                             : color_pending;
            lv_obj_set_style_bg_color(child, connector_color, 0);
            connector_index++;
        }
    }
}

void ui_step_progress_set_completed(lv_obj_t* widget, int step_index) {
    if (!widget)
        return;

    step_progress_data_t* data = (step_progress_data_t*)lv_obj_get_user_data(widget);
    if (!data || step_index < 0 || step_index >= data->step_count) {
        spdlog::warn("[Step Progress] Invalid step index: {}", step_index);
        return;
    }

    data->states[step_index] = UI_STEP_STATE_COMPLETED;

    // Update styling
    lv_obj_t* step_item = lv_obj_get_child(widget, step_index);
    if (step_item) {
        apply_step_styling(step_item, UI_STEP_STATE_COMPLETED);
    }
}

void ui_step_progress_set_label(lv_obj_t* widget, int step_index, const char* new_label) {
    if (!widget || !new_label)
        return;

    step_progress_data_t* data = (step_progress_data_t*)lv_obj_get_user_data(widget);
    if (!data || step_index < 0 || step_index >= data->step_count) {
        spdlog::warn("[Step Progress] Invalid step index: {}", step_index);
        return;
    }

    // Update buffer
    snprintf(data->label_buffers[step_index], 128, "%s", new_label);

    // Update label widget
    lv_obj_t* step_item = lv_obj_get_child(widget, step_index);
    if (step_item) {
        lv_obj_t* label = lv_obj_get_child(step_item, 1); // Label is second child
        if (label) {
            lv_label_set_text(label, data->label_buffers[step_index]);
        }
    }
}
