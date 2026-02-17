// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/// @file ui_status_pill.h
/// @brief Status pill/badge widget with semantic color variants.
/// @pattern Custom XML widget using shared ui_variant module.
/// @threading Main thread only
/// @gotchas Must call ui_status_pill_register_widget() BEFORE loading status_pill.xml

#pragma once

#include "lvgl/lvgl.h"

/// Register the status_pill widget with LVGL's XML system.
/// Call once at startup, before registering XML components.
void ui_status_pill_register_widget();

/// Change pill text at runtime.
void ui_status_pill_set_text(lv_obj_t* pill, const char* text);

/// Change pill variant at runtime ("success", "danger", "muted", etc.).
void ui_status_pill_set_variant(lv_obj_t* pill, const char* variant_str);
