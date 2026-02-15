// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_context_menu.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

// ============================================================================
// Construction / Destruction
// ============================================================================

ContextMenu::ContextMenu() {
    spdlog::trace("[ContextMenu] Constructed");
}

ContextMenu::~ContextMenu() {
    hide();
    spdlog::trace("[ContextMenu] Destroyed");
}

ContextMenu::ContextMenu(ContextMenu&& other) noexcept
    : menu_(other.menu_), parent_(other.parent_), item_index_(other.item_index_),
      action_callback_(std::move(other.action_callback_)) {
    other.menu_ = nullptr;
    other.parent_ = nullptr;
    other.item_index_ = -1;
}

ContextMenu& ContextMenu::operator=(ContextMenu&& other) noexcept {
    if (this != &other) {
        hide();
        menu_ = other.menu_;
        parent_ = other.parent_;
        item_index_ = other.item_index_;
        action_callback_ = std::move(other.action_callback_);
        other.menu_ = nullptr;
        other.parent_ = nullptr;
        other.item_index_ = -1;
    }
    return *this;
}

// ============================================================================
// Public API
// ============================================================================

void ContextMenu::set_action_callback(ActionCallback callback) {
    action_callback_ = std::move(callback);
}

bool ContextMenu::show_near_widget(lv_obj_t* parent, int item_index, lv_obj_t* near_widget) {
    // Hide any existing menu first
    hide();

    if (!parent || !near_widget) {
        spdlog::warn("[ContextMenu] Cannot show - missing parent or widget");
        return false;
    }

    // Store state
    parent_ = parent;
    item_index_ = item_index;

    // Create context menu from XML
    menu_ = static_cast<lv_obj_t*>(lv_xml_create(parent, xml_component_name(), nullptr));
    if (!menu_) {
        spdlog::error("[ContextMenu] Failed to create menu from XML: {}", xml_component_name());
        return false;
    }

    // Let subclass configure the menu
    on_created(menu_);

    // Position the menu card near the target widget
    lv_obj_t* menu_card = lv_obj_find_by_name(menu_, menu_card_name());
    if (menu_card) {
        position_near_widget(menu_card, near_widget);
    }

    spdlog::debug("[ContextMenu] Shown '{}' for item {}", xml_component_name(), item_index);
    return true;
}

void ContextMenu::hide() {
    if (!menu_)
        return;

    // Use async delete since we may be called during event processing
    if (lv_is_initialized()) {
        lv_obj_delete_async(menu_);
    }
    menu_ = nullptr;
    item_index_ = -1;
    spdlog::debug("[ContextMenu] hide()");
}

// ============================================================================
// Protected Helpers
// ============================================================================

void ContextMenu::on_backdrop_clicked() {
    dispatch_action(-1); // -1 = cancelled
}

void ContextMenu::dispatch_action(int action) {
    int item = item_index_;
    ActionCallback callback_copy = action_callback_;
    spdlog::debug("[ContextMenu] Dispatch action {} for item {}", action, item);

    hide();

    if (callback_copy) {
        callback_copy(action, item);
    }
}

// ============================================================================
// Positioning
// ============================================================================

void ContextMenu::position_near_widget(lv_obj_t* menu_card, lv_obj_t* near_widget) {
    // Update layout to get accurate dimensions
    lv_obj_update_layout(menu_card);

    // Get the position of the target widget in screen coordinates
    lv_area_t widget_area;
    lv_obj_get_coords(near_widget, &widget_area);

    // Calculate positioning
    int32_t screen_width = lv_obj_get_width(parent_);
    int32_t menu_width = lv_obj_get_width(menu_card);
    int32_t widget_center_x = (widget_area.x1 + widget_area.x2) / 2;
    int32_t widget_center_y = (widget_area.y1 + widget_area.y2) / 2;

    // Position to the right of widget, or left if near screen edge
    int32_t menu_x = widget_center_x + 20;
    if (menu_x + menu_width > screen_width - 10) {
        menu_x = widget_center_x - menu_width - 20;
    }

    // Center vertically on the widget
    int32_t menu_y = widget_center_y - lv_obj_get_height(menu_card) / 2;

    // Clamp to screen bounds
    int32_t screen_height = lv_obj_get_height(parent_);
    if (menu_y < 10) {
        menu_y = 10;
    }
    if (menu_y + lv_obj_get_height(menu_card) > screen_height - 10) {
        menu_y = screen_height - lv_obj_get_height(menu_card) - 10;
    }

    lv_obj_set_pos(menu_card, menu_x, menu_y);
}

} // namespace helix::ui
