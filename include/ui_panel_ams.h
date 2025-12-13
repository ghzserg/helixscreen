// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"
#include "ui_panel_base.h"

#include "ams_state.h"

/**
 * @file ui_panel_ams.h
 * @brief AMS/Multi-filament panel - slot visualization and operations
 *
 * Displays a Bambu-inspired visualization of multi-filament units (Happy Hare, AFC)
 * with colored slots, status indicators, and load/unload operations.
 *
 * ## UI Layout (480x800 primary target):
 * ```
 * ┌─────────────────────────────────────────┐
 * │ header_bar: "Multi-Filament"            │
 * ├─────────────────────────────────────────┤
 * │  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐   │
 * │  │ Slot │ │ Slot │ │ Slot │ │ Slot │   │
 * │  │  0   │ │  1   │ │  2   │ │  3   │   │
 * │  └──────┘ └──────┘ └──────┘ └──────┘   │
 * │                                         │
 * │  [Status: Idle / Loading / etc.]        │
 * │                                         │
 * │  [Action buttons: Unload, Home, etc.]   │
 * └─────────────────────────────────────────┘
 * ```
 *
 * ## Reactive Bindings:
 * - Gate colors: `ams_gate_N_color` (int, RGB packed)
 * - Gate status: `ams_gate_N_status` (int, GateStatus enum)
 * - Current gate: `ams_current_gate` (int, -1 if none)
 * - Action: `ams_action` (int, AmsAction enum)
 * - Action detail: `ams_action_detail` (string)
 *
 * @see AmsState for subject definitions
 * @see AmsBackend for backend operations
 */
class AmsPanel : public PanelBase {
  public:
    /**
     * @brief Construct AMS panel with dependencies
     * @param printer_state Reference to global PrinterState
     * @param api Pointer to MoonrakerAPI (may be nullptr)
     */
    AmsPanel(PrinterState& printer_state, MoonrakerAPI* api);
    ~AmsPanel() override = default;

    // === PanelBase Interface ===

    void init_subjects() override;
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;
    void on_activate() override;
    void on_deactivate() override;

    [[nodiscard]] const char* get_name() const override {
        return "AMS Panel";
    }

    [[nodiscard]] const char* get_xml_component_name() const override {
        return "ams_panel";
    }

    // === Public API ===

    /**
     * @brief Get the root panel object
     * @return Panel widget, or nullptr if not setup
     */
    [[nodiscard]] lv_obj_t* get_panel() const {
        return panel_;
    }

    /**
     * @brief Refresh slot display from backend state
     *
     * Call this after backend operations complete to update UI.
     * Normally handled automatically via AmsState observer callbacks.
     */
    void refresh_slots();

    /**
     * @brief Clear internal panel reference before UI destruction
     *
     * Called by destroy_ams_panel_ui() before deleting the LVGL object.
     * Clears panel_, slot_widgets_, and other widget references to prevent
     * dangling pointers.
     */
    void clear_panel_reference();

  private:
    // === Slot Management ===

    static constexpr int MAX_VISIBLE_SLOTS = 8; ///< Max slots displayed (2 rows of 4)
    lv_obj_t* slot_widgets_[MAX_VISIBLE_SLOTS] = {nullptr};

    // === Context Menu ===

    lv_obj_t* context_menu_ = nullptr; ///< Active context menu (nullptr if hidden)
    int context_menu_slot_ = -1;       ///< Slot index for active context menu

    // === Observers (RAII cleanup via ObserverGuard) ===

    ObserverGuard gates_version_observer_;
    ObserverGuard action_observer_;
    ObserverGuard current_gate_observer_;
    ObserverGuard gate_count_observer_;
    ObserverGuard path_segment_observer_;
    ObserverGuard path_topology_observer_;

    // === Dynamic Slot State ===

    int current_slot_count_ = 0;    ///< Number of slots currently created
    lv_obj_t* slot_grid_ = nullptr; ///< Container for dynamically created slots

    // === Filament Path Canvas ===

    lv_obj_t* path_canvas_ = nullptr; ///< Filament path visualization widget

    // === Setup Helpers ===

    void setup_slots();
    void setup_action_buttons();
    void setup_status_display();
    void setup_path_canvas();
    void update_path_canvas_from_backend();

    /**
     * @brief Create slot widgets dynamically based on gate count
     * @param count Number of slots to create (0 to max 8)
     *
     * Deletes existing slots and creates new ones. Uses lv_xml_create()
     * to instantiate ams_slot C++ widgets, then sets their slot_index.
     */
    void create_slots(int count);

    // === Gate Count Observer ===

    static void on_gate_count_changed(lv_observer_t* observer, lv_subject_t* subject);

    // === UI Update Handlers ===

    void update_slot_colors();
    void update_slot_status(int gate_index);
    void update_action_display(AmsAction action);
    void update_current_gate_highlight(int gate_index);
    void update_current_loaded_display(int gate_index);

    // === Event Callbacks (static trampolines) ===

    static void on_slot_clicked(lv_event_t* e);
    static void on_unload_clicked(lv_event_t* e);
    static void on_reset_clicked(lv_event_t* e);

    // === Observer Callbacks ===

    static void on_gates_version_changed(lv_observer_t* observer, lv_subject_t* subject);
    static void on_action_changed(lv_observer_t* observer, lv_subject_t* subject);
    static void on_current_gate_changed(lv_observer_t* observer, lv_subject_t* subject);
    static void on_path_state_changed(lv_observer_t* observer, lv_subject_t* subject);

    // === Path Canvas Callback ===

    static void on_path_gate_clicked(int gate_index, void* user_data);

    // === Context Menu Callbacks (static trampolines) ===

    static void on_context_backdrop_clicked(lv_event_t* e);
    static void on_context_load_clicked(lv_event_t* e);
    static void on_context_unload_clicked(lv_event_t* e);
    static void on_context_edit_clicked(lv_event_t* e);

    // === Context Menu Management ===

    void show_context_menu(int slot_index, lv_obj_t* near_widget);
    void hide_context_menu();

    // === Bypass Button State ===

    void update_bypass_button_visibility();
    void update_bypass_button_state();

    // === Action Handlers (public for XML event callbacks) ===
  public:
    void handle_slot_tap(int slot_index);
    void handle_unload();
    void handle_reset();
    void handle_bypass_toggle();

  private:
    void handle_context_load();
    void handle_context_unload();
    void handle_context_edit();
};

/**
 * @brief Get global AMS panel singleton
 *
 * Creates the panel on first call, returns cached instance thereafter.
 * Panel is lazily initialized - widgets registered and XML created on first access.
 *
 * @return Reference to global AmsPanel instance
 */
AmsPanel& get_global_ams_panel();

/**
 * @brief Destroy the AMS panel UI to free memory
 *
 * Deletes the LVGL panel object and canvas buffers. The C++ AmsPanel
 * object and widget registrations remain for quick recreation.
 * Call this when the panel is closed to free memory on embedded systems.
 */
void destroy_ams_panel_ui();
