#pragma once

#include "lvgl/lvgl.h"

// Network connection types
typedef enum {
    NETWORK_WIFI,
    NETWORK_ETHERNET,
    NETWORK_DISCONNECTED
} network_type_t;

// Initialize subjects for reactive data binding
// MUST be called BEFORE creating any XML that references home panel subjects
void ui_panel_home_init_subjects();

// Setup observers after XML panel is created
void ui_panel_home_setup_observers(lv_obj_t* panel);

// Create the home panel from XML (idle/ready state)
// Uses home_panel.xml component with dynamic widget resolution
// NOTE: ui_panel_home_init_subjects() must be called first!
lv_obj_t* ui_panel_home_create(lv_obj_t* parent);

// Update home panel with printer state
void ui_panel_home_update(const char* status_text, int temp);

// Update network status display
void ui_panel_home_set_network(network_type_t type);

// Update light state display
void ui_panel_home_set_light(bool is_on);

// Get current light state
bool ui_panel_home_get_light_state();
