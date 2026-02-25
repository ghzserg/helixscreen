// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_component_header_bar.h"

#include "ui_utils.h"

#include "display_manager.h"
#include "spdlog/spdlog.h"
#include "theme_manager.h"

#include <algorithm>
#include <stdio.h>
#include <vector>

// ============================================================================
// LIFECYCLE & RESPONSIVE BEHAVIOR
// ============================================================================

// Track all header_bar instances for resize handling
static std::vector<lv_obj_t*> header_instances;

// Event handler for DELETE event (cleanup)
static void header_bar_delete_cb(lv_event_t* e) {
    lv_obj_t* header = (lv_obj_t*)lv_event_get_target(e);

    // Remove from tracking list
    auto it = std::find(header_instances.begin(), header_instances.end(), header);
    if (it != header_instances.end()) {
        header_instances.erase(it);
        spdlog::debug("[HeaderBar] Removed from tracking ({} remain)", header_instances.size());
    }
}

// Global resize callback (called by app resize handler system)
static void on_app_resize() {
    for (lv_obj_t* header : header_instances) {
        if (!header)
            continue;

        lv_obj_t* screen = lv_obj_get_screen(header);
        if (!screen)
            continue;

        lv_coord_t header_height = ui_get_responsive_header_height(lv_obj_get_height(screen));
        lv_obj_set_height(header, header_height);
    }

    if (!header_instances.empty()) {
        spdlog::debug("[HeaderBar] Updated {} header heights on resize", header_instances.size());
    }
}

// ============================================================================
// HEADER BAR API
// ============================================================================

bool ui_header_bar_show_action_button(lv_obj_t* header_bar_widget) {
    if (!header_bar_widget) {
        return false;
    }

    lv_obj_t* button = lv_obj_find_by_name(header_bar_widget, "action_button");
    if (button) {
        lv_obj_remove_flag(button, LV_OBJ_FLAG_HIDDEN);
        return true;
    }

    return false;
}

bool ui_header_bar_hide_action_button(lv_obj_t* header_bar_widget) {
    if (!header_bar_widget) {
        return false;
    }

    lv_obj_t* button = lv_obj_find_by_name(header_bar_widget, "action_button");
    if (button) {
        lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
        return true;
    }

    return false;
}

bool ui_header_bar_set_action_button_text(lv_obj_t* header_bar_widget, const char* text) {
    if (!header_bar_widget || !text) {
        return false;
    }

    lv_obj_t* button = lv_obj_find_by_name(header_bar_widget, "action_button");
    if (!button) {
        return false;
    }

    // Find the label child of the button
    lv_obj_t* label = lv_obj_find_by_name(button, "action_button_label");
    if (label) {
        lv_label_set_text(label, text);
        return true;
    }

    return false;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void ui_component_header_bar_init() {
    // Register global resize callback for all header_bar instances
    if (auto* dm = DisplayManager::instance()) {
        dm->register_resize_callback(on_app_resize);
    }

    spdlog::trace("[HeaderBar] Component system initialized");
}

void ui_component_header_bar_setup(lv_obj_t* header, lv_obj_t* screen) {
    if (!header || !screen) {
        return;
    }

    // Attach DELETE event for cleanup
    lv_obj_add_event_cb(header, header_bar_delete_cb, LV_EVENT_DELETE, nullptr);

    // Track this instance for global resize handling
    header_instances.push_back(header);

    // Apply responsive height immediately
    lv_coord_t header_height = ui_get_responsive_header_height(lv_obj_get_height(screen));
    lv_obj_set_height(header, header_height);

    // Extend back button's click area for easier touch targeting.
    // Use half the header height so the hit area spans the full header vertically
    // and is generously wide (~header_height total width beyond the icon).
    lv_obj_t* back_btn = lv_obj_find_by_name(header, "back_button");
    if (back_btn) {
        lv_obj_set_ext_click_area(back_btn, header_height / 2);
    }

    spdlog::trace("[HeaderBar] Setup complete: height={}px", header_height);
}
