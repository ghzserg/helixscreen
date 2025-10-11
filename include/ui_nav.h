#pragma once

#include "lvgl/lvgl.h"

// Navigation panel IDs
typedef enum {
    UI_PANEL_HOME,
    UI_PANEL_CONTROLS,
    UI_PANEL_FILAMENT,
    UI_PANEL_SETTINGS,
    UI_PANEL_ADVANCED,
    UI_PANEL_COUNT
} ui_panel_id_t;

// Initialize navigation system with reactive subjects
// MUST be called BEFORE creating navigation bar XML
void ui_nav_init();

// Wire up event handlers to an existing navbar widget created from XML
// Call this after creating navigation_bar component from XML
void ui_nav_wire_events(lv_obj_t* navbar);

// Set active panel (triggers reactive icon color updates)
void ui_nav_set_active(ui_panel_id_t panel_id);

// Get current active panel
ui_panel_id_t ui_nav_get_active();

// Set panel widgets for show/hide management
// panels array should have UI_PANEL_COUNT elements (can have NULLs for not-yet-created panels)
void ui_nav_set_panels(lv_obj_t** panels);

