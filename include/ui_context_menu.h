// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <lvgl.h>
#include <string>

namespace helix::ui {

/**
 * @file ui_context_menu.h
 * @brief Generic context menu component for popup menus near widgets
 *
 * Provides the common mechanics for context menus:
 * - Full-screen semi-transparent backdrop (click to dismiss)
 * - Card positioned near the triggering widget (smart left/right/vertical placement)
 * - Action callback dispatch via integer action IDs
 * - lv_obj_delete_async() for safe dismissal during event processing
 *
 * Subclasses define their own XML component, subjects, and specific actions.
 *
 * ## Usage:
 * @code
 * class MyContextMenu : public ContextMenu {
 * protected:
 *     const char* xml_component_name() const override { return "my_context_menu"; }
 *     const char* menu_card_name() const override { return "context_menu"; }
 *     void on_created(lv_obj_t* menu) override {
 *         // Configure menu-specific widgets after XML creation
 *     }
 * };
 * @endcode
 */
class ContextMenu {
  public:
    using ActionCallback = std::function<void(int action, int item_index)>;

    ContextMenu();
    virtual ~ContextMenu();

    // Non-copyable
    ContextMenu(const ContextMenu&) = delete;
    ContextMenu& operator=(const ContextMenu&) = delete;

    // Movable
    ContextMenu(ContextMenu&& other) noexcept;
    ContextMenu& operator=(ContextMenu&& other) noexcept;

    /**
     * @brief Show context menu near a widget
     * @param parent Parent screen for the menu
     * @param item_index Index of the item this menu is for
     * @param near_widget Widget to position menu near
     * @return true if menu was shown successfully
     */
    bool show_near_widget(lv_obj_t* parent, int item_index, lv_obj_t* near_widget);

    /**
     * @brief Hide the context menu
     */
    void hide();

    /**
     * @brief Check if menu is currently visible
     */
    [[nodiscard]] bool is_visible() const {
        return menu_ != nullptr;
    }

    /**
     * @brief Get item index the menu is currently shown for
     */
    [[nodiscard]] int get_item_index() const {
        return item_index_;
    }

    /**
     * @brief Set callback for menu actions
     */
    void set_action_callback(ActionCallback callback);

  protected:
    /**
     * @brief Get the XML component name for this menu
     * Subclasses must override to provide their menu's XML component.
     */
    virtual const char* xml_component_name() const = 0;

    /**
     * @brief Get the name of the card widget inside the XML for positioning
     * Default: "context_menu"
     */
    virtual const char* menu_card_name() const {
        return "context_menu";
    }

    /**
     * @brief Called after the menu XML is created, before positioning
     * Subclasses override to configure menu-specific widgets (dropdowns, headers, etc.)
     */
    virtual void on_created(lv_obj_t* menu) {
        (void)menu;
    }

    /**
     * @brief Called when the backdrop is clicked (before hide)
     * Default: dispatches action -1 (cancelled) via the action callback
     */
    virtual void on_backdrop_clicked();

    /**
     * @brief Dispatch an action and hide the menu
     * Captures callback, hides, then invokes with action + item_index.
     */
    void dispatch_action(int action);

    // Accessors for subclass use
    [[nodiscard]] lv_obj_t* menu() const {
        return menu_;
    }
    [[nodiscard]] lv_obj_t* parent() const {
        return parent_;
    }

  private:
    lv_obj_t* menu_ = nullptr;
    lv_obj_t* parent_ = nullptr;
    int item_index_ = -1;
    ActionCallback action_callback_;

    /**
     * @brief Position the menu card near the target widget
     */
    void position_near_widget(lv_obj_t* menu_card, lv_obj_t* near_widget);
};

} // namespace helix::ui
