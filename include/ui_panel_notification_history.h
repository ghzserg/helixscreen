// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl.h"

/**
 * @brief Create notification history panel
 *
 * @param parent Parent object
 * @return Created panel object
 */
lv_obj_t* ui_panel_notification_history_create(lv_obj_t* parent);

/**
 * @brief Refresh notification list from history
 *
 * Called when panel is shown or filter changes.
 */
void ui_panel_notification_history_refresh();
