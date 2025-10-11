#pragma once

#include "lvgl/lvgl.h"

// Panel indices
#define PANEL_HOME 0
#define PANEL_CONTROLS 1
#define PANEL_FILAMENT 2
#define PANEL_SETTINGS 3
#define PANEL_ADVANCED 4
#define PANEL_COUNT 5

// Initialize navigation system with active panel subject
// MUST be called BEFORE creating navigation bar XML
void ui_navigation_init();

// Create navigation bar from XML and wire up icon highlighting
// Returns the navbar widget
// NOTE: ui_navigation_init() must be called first!
lv_obj_t* ui_navigation_create(lv_obj_t* parent);

// Switch to a specific panel (triggers icon color updates)
void ui_navigation_switch_to_panel(int panel_index);

// Get current active panel index
int ui_navigation_get_active_panel();

// Set panel widgets for show/hide management
// panels array should have PANEL_COUNT elements (can have NULLs for not-yet-created panels)
void ui_navigation_set_panels(lv_obj_t** panels);
