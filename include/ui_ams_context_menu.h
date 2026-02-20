// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_context_menu.h"

#include <functional>
#include <lvgl.h>
#include <string>

// Forward declaration
class AmsBackend;

namespace helix::ui {

/**
 * @file ui_ams_context_menu.h
 * @brief Context menu for AMS slot operations
 *
 * Displays a popup menu near a slot with options to load, unload,
 * edit, or assign a Spoolman spool. Automatically positions itself
 * relative to the target slot widget.
 *
 * Extends the generic ContextMenu with AMS-specific features:
 * - Slot loaded/can-load subjects for button states
 * - Tool mapping dropdown
 * - Endless spool backup dropdown
 *
 * ## Usage:
 * @code
 * helix::ui::AmsContextMenu menu;
 * menu.set_action_callback([](MenuAction action, int slot_index) {
 *     switch (action) {
 *         case MenuAction::LOAD: // load filament...
 *         case MenuAction::UNLOAD: // unload filament...
 *         case MenuAction::EDIT: // show edit modal...
 *         case MenuAction::SPOOLMAN: // show spoolman picker...
 *     }
 * });
 * menu.show_near_widget(parent, slot_index, slot_widget);
 * @endcode
 */
class AmsContextMenu : public ContextMenu {
  public:
    enum class MenuAction {
        CANCELLED,   ///< User dismissed menu without action
        LOAD,        ///< Load filament from this slot
        UNLOAD,      ///< Unload filament from toolhead
        EJECT,       ///< Eject filament from lane (release spool)
        RESET_LANE,  ///< Reset lane to known-good state
        EDIT,        ///< Edit slot properties
        CLEAR_SPOOL, ///< Clear assigned spool from empty slot
        SPOOLMAN     ///< Assign Spoolman spool
    };

    using ActionCallback = std::function<void(MenuAction action, int slot_index)>;

    AmsContextMenu();
    ~AmsContextMenu() override;

    // Non-copyable
    AmsContextMenu(const AmsContextMenu&) = delete;
    AmsContextMenu& operator=(const AmsContextMenu&) = delete;

    // Movable
    AmsContextMenu(AmsContextMenu&& other) noexcept;
    AmsContextMenu& operator=(AmsContextMenu&& other) noexcept;

    /**
     * @brief Show context menu near a slot widget
     * @param parent Parent screen for the menu
     * @param slot_index Slot this menu is for (0-based)
     * @param near_widget Widget to position menu near (typically slot widget)
     * @param is_loaded True if filament is loaded to extruder (enables Unload button)
     * @param backend Optional backend pointer for tool mapping/endless spool features
     * @return true if menu was shown successfully
     */
    bool show_near_widget(lv_obj_t* parent, int slot_index, lv_obj_t* near_widget,
                          bool is_loaded = false, AmsBackend* backend = nullptr);

    /**
     * @brief Show context menu for external spool (bypass/direct feed)
     *
     * Shows a reduced menu with only EDIT and CLEAR_SPOOL actions
     * (no LOAD/UNLOAD/EJECT since external spool is not managed by backend).
     *
     * @param parent Parent screen for the menu
     * @param anchor_widget Widget to position menu near (for click point)
     * @return true if menu was shown successfully
     */
    bool show_for_external_spool(lv_obj_t* parent, lv_obj_t* anchor_widget);

    /**
     * @brief Get slot index the menu is currently shown for
     */
    [[nodiscard]] int get_slot_index() const {
        return get_item_index();
    }

    /**
     * @brief Set callback for menu actions
     */
    void set_action_callback(ActionCallback callback);

  protected:
    const char* xml_component_name() const override {
        return "ams_context_menu";
    }
    void on_created(lv_obj_t* menu_obj) override;

  private:
    // === AMS-specific state ===
    ActionCallback action_callback_;

    /**
     * @brief Common pattern: clear static instance, hide, invoke callback
     */
    void dispatch_ams_action(MenuAction action);

    // === Subjects for button enable/disable states ===
    lv_subject_t slot_is_loaded_subject_; ///< 1 = loaded (Unload enabled), 0 = not loaded
    lv_subject_t slot_can_load_subject_;  ///< 1 = has filament (Load enabled), 0 = empty
    bool subject_initialized_ = false;

    // === Backend reference for dropdown operations ===
    AmsBackend* backend_ = nullptr;
    int total_slots_ = 0;

    // === Dropdown widget pointers ===
    lv_obj_t* tool_dropdown_ = nullptr;
    lv_obj_t* backup_dropdown_ = nullptr;

    // === Pending state for on_created ===
    bool pending_is_loaded_ = false;
    bool eject_mode_ = false;          ///< True when showing "Eject" instead of "Unload"
    bool clear_spool_mode_ = false;    ///< True when showing "Clear Spool" instead of "Spool Info"
    bool external_spool_mode_ = false; ///< True when showing menu for external spool (bypass)

    // === Event Handlers ===
    void handle_backdrop_clicked();
    void handle_load();
    void handle_unload();
    void handle_reset_lane();
    void handle_edit();
    void handle_spoolman();
    void handle_tool_changed();
    void handle_backup_changed();

    // === Dropdown Configuration ===
    void configure_dropdowns();
    void populate_tool_dropdown();
    void populate_backup_dropdown();
    std::string build_tool_options() const;
    std::string build_backup_options() const;
    int get_current_tool_for_slot() const;
    int get_current_backup_for_slot() const;

    // === Static Callback Registration ===
    static void register_callbacks();
    static bool callbacks_registered_;

    // === Static Callbacks ===
    static AmsContextMenu* s_active_instance_;
    static AmsContextMenu* get_active_instance();
    static void on_backdrop_cb(lv_event_t* e);
    static void on_load_cb(lv_event_t* e);
    static void on_unload_cb(lv_event_t* e);
    static void on_reset_lane_cb(lv_event_t* e);
    static void on_edit_cb(lv_event_t* e);
    static void on_spoolman_cb(lv_event_t* e);
    static void on_tool_changed_cb(lv_event_t* e);
    static void on_backup_changed_cb(lv_event_t* e);
};

} // namespace helix::ui
