// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"

/**
 * Step states for progress indicator
 */
namespace helix {
enum class StepState { Pending = 0, Active = 1, Completed = 2 };
} // namespace helix

/**
 * Step definition structure
 */
typedef struct {
    const char* label;      // Step text (e.g., "Nozzle heating")
    helix::StepState state; // Current state
} ui_step_t;

/**
 * Create a step progress indicator widget
 *
 * @param parent Parent container
 * @param steps Array of step definitions
 * @param step_count Number of steps
 * @param horizontal true for horizontal layout, false for vertical (default)
 * @param scope_name Component scope name for loading theme colors (e.g., "step_progress_test"), or
 * nullptr for defaults
 * @return Created widget (flex container with step items)
 */
lv_obj_t* ui_step_progress_create(lv_obj_t* parent, const ui_step_t* steps, int step_count,
                                  bool horizontal, const char* scope_name = nullptr);

/**
 * Update the current active step (automatically sets previous steps to completed)
 *
 * @param widget Step progress widget
 * @param step_index 0-based step index to mark as active
 */
void ui_step_progress_set_current(lv_obj_t* widget, int step_index);

/**
 * Mark a specific step as completed
 *
 * @param widget Step progress widget
 * @param step_index 0-based step index to mark as completed
 */
void ui_step_progress_set_completed(lv_obj_t* widget, int step_index);

/**
 * Update a step's label text
 *
 * @param widget Step progress widget
 * @param step_index 0-based step index
 * @param new_label New label text
 */
void ui_step_progress_set_label(lv_obj_t* widget, int step_index, const char* new_label);
