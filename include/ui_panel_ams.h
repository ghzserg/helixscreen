// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_ams_context_menu.h"
#include "ui_ams_detail.h"
#include "ui_ams_dryer_card.h"
#include "ui_ams_edit_modal.h"
#include "ui_ams_loading_error_modal.h"
#include "ui_color_picker.h"
#include "ui_observer_guard.h"
#include "ui_panel_base.h"

#include "ams_state.h"
#include "ams_step_operation.h"
#include "ams_types.h" // For SlotInfo

#include <memory>

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
 * - Slot colors: `ams_slot_N_color` (int, RGB packed)
 * - Slot status: `ams_slot_N_status` (int, SlotStatus enum)
 * - Current slot: `ams_current_slot` (int, -1 if none)
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
     * @param printer_state Reference to global helix::PrinterState
     * @param api Pointer to MoonrakerAPI (may be nullptr)
     */
    AmsPanel(helix::PrinterState& printer_state, MoonrakerAPI* api);
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

    /**
     * @brief Scope detail view to show only one unit's slots
     * @param unit_index Unit index to show (-1 = all units, default)
     */
    void set_unit_scope(int unit_index);

    /**
     * @brief Clear unit scope, showing all slots
     */
    void clear_unit_scope();

  private:
    // === Slot Management ===

    static constexpr int MAX_VISIBLE_SLOTS =
        16; ///< Max slots displayed (increased for 8+ gate systems)
    lv_obj_t* slot_widgets_[MAX_VISIBLE_SLOTS] = {nullptr};
    lv_obj_t* label_widgets_[MAX_VISIBLE_SLOTS] = {nullptr}; ///< Separate label layer for z-order
    AmsDetailWidgets detail_widgets_;                        ///< Shared component widget pointers

    // === Extracted UI Modules ===

    std::unique_ptr<helix::ui::AmsContextMenu> context_menu_;      ///< Slot context menu
    std::unique_ptr<helix::ui::AmsEditModal> edit_modal_;          ///< Edit filament modal
    std::unique_ptr<helix::ui::AmsDryerCard> dryer_card_;          ///< Dryer card and modal
    std::unique_ptr<helix::ui::AmsLoadingErrorModal> error_modal_; ///< Loading error modal

    // === Observers (RAII cleanup via ObserverGuard) ===

    ObserverGuard slots_version_observer_;
    ObserverGuard action_observer_;
    ObserverGuard current_slot_observer_;
    ObserverGuard slot_count_observer_;
    ObserverGuard path_segment_observer_;
    ObserverGuard path_topology_observer_;
    ObserverGuard extruder_temp_observer_;  ///< For preheat completion detection
    ObserverGuard backend_count_observer_;  ///< For backend selector visibility
    ObserverGuard external_spool_observer_; ///< Reactive updates when external spool color changes

    // === Dynamic Slot State ===

    int scoped_unit_index_ = -1;    ///< Unit scope: -1 = all units, >=0 = specific unit
    int current_slot_count_ = 0;    ///< Number of slots currently created
    lv_obj_t* slot_grid_ = nullptr; ///< Container for dynamically created slots
    // === Preheat State for Filament Loading ===

    int pending_load_slot_ = -1;                  ///< Slot awaiting preheat completion (-1 = none)
    int pending_load_target_temp_ = 0;            ///< Target temp for pending load (°C)
    bool ui_initiated_heat_ = false;              ///< True if UI heated for this load (for cooling)
    AmsAction prev_ams_action_ = AmsAction::IDLE; ///< Previous action for transition detection

    // === Step Progress Operation Type ===

    StepOperationType current_operation_type_ =
        StepOperationType::LOAD_FRESH; ///< Current operation
    int current_step_count_ = 4;       ///< Steps in current
    int target_load_slot_ = -1;        ///< Target slot for current operation (for pulse animation)

    // === Filament Path Canvas ===

    lv_obj_t* path_canvas_ = nullptr; ///< Filament path visualization widget

    // === Step Progress Widget ===

    lv_obj_t* step_progress_ = nullptr;           ///< Step progress stepper widget
    lv_obj_t* step_progress_container_ = nullptr; ///< Container for step progress

    // === Endless Spool Arrows Canvas ===

    lv_obj_t* endless_arrows_ = nullptr; ///< Endless spool backup chain visualization

    // === Backend Selector State ===

    int active_backend_idx_ = 0; ///< Currently selected backend index

    // === Backend Selector Helpers ===

    void rebuild_backend_selector();
    void on_backend_segment_selected(int index);

    // === Setup Helpers ===

    void setup_system_header();
    void setup_slots();
    void setup_action_buttons();
    void setup_status_display();
    void setup_path_canvas();
    void update_path_canvas_from_backend();
    void setup_endless_arrows();
    void update_endless_arrows_from_backend();
    void setup_step_progress();
    void update_step_progress(AmsAction action);
    void recreate_step_progress_for_operation(StepOperationType op_type);
    int get_step_index_for_action(AmsAction action, StepOperationType op_type);

    /**
     * @brief Start an operation with known type and target slot
     *
     * Called BEFORE backend operation to set up correct step progress
     * and start pulse animation on target slot. This avoids heuristic
     * detection which can fail due to brief IDLE states between phases.
     *
     * @param op_type The type of operation (LOAD_FRESH, LOAD_SWAP, UNLOAD)
     * @param target_slot Slot index being loaded/unloaded
     */
    void start_operation(StepOperationType op_type, int target_slot);

    /**
     * @brief Create slot widgets dynamically based on slot count
     * @param count Number of slots to create (0 to max 8)
     *
     * Deletes existing slots and creates new ones. Uses lv_xml_create()
     * to instantiate ams_slot C++ widgets, then sets their slot_index.
     */
    void create_slots(int count);

    // === Slot Count Observer ===
    // on_slot_count_changed migrated to lambda observer factory

    // === UI Update Handlers ===

    void update_slot_colors();
    void update_slot_status(int slot_index);
    void update_action_display(AmsAction action);
    void update_current_slot_highlight(int slot_index);
    void update_current_loaded_display(int slot_index);

    // === Event Callbacks (static trampolines) ===

    static void on_slot_clicked(lv_event_t* e);
    static void on_unload_clicked(lv_event_t* e);
    static void on_reset_clicked(lv_event_t* e);

    // === Observer Callbacks ===
    // on_action_changed migrated to lambda observer factory

    static void on_slots_version_changed(lv_observer_t* observer, lv_subject_t* subject);
    static void on_current_slot_changed(lv_observer_t* observer, lv_subject_t* subject);
    static void on_path_state_changed(lv_observer_t* observer, lv_subject_t* subject);

    // === Path Canvas Callbacks ===

    static void on_path_slot_clicked(int slot_index, void* user_data);
    static void on_bypass_spool_clicked(void* user_data);

    /**
     * @brief Handle click on bypass spool box in path canvas
     *
     * Opens the edit modal for the external spool (slot_index -2).
     */
    void handle_bypass_spool_click();

    // === Spoolman Integration ===

    void sync_spoolman_active_spool();

    // === Preheat Logic for Filament Loading ===

    /**
     * @brief Get the temperature to use for loading filament from a slot
     *
     * Priority: SlotInfo::nozzle_temp_min > FilamentDatabase lookup > DEFAULT_LOAD_PREHEAT_TEMP
     *
     * @param slot_index Slot index to get load temperature for
     * @return Target temperature in degrees Celsius
     */
    int get_load_temp_for_slot(int slot_index);

    /**
     * @brief Handle load request with automatic preheat if needed
     *
     * If the extruder is already hot enough, loads immediately.
     * Otherwise, starts preheating and defers load until temperature reached.
     *
     * @param slot_index Slot to load from
     */
    void handle_load_with_preheat(int slot_index);

    /**
     * @brief Check if pending load can proceed (called when temperature updates)
     *
     * Called by extruder temperature observer. If a load is pending and
     * the extruder has reached target temperature, triggers the load.
     */
    void check_pending_load();

    /**
     * @brief Handle load completion - turn off heater if UI initiated it
     *
     * Called when AMS action transitions from LOADING to IDLE.
     * If the UI initiated the preheat (ui_initiated_heat_), turns off the extruder
     * heater to save energy and prevent filament damage.
     */
    void handle_load_complete();

    /**
     * @brief Show visual feedback during UI-managed preheat
     *
     * Displays step progress at step 0 (Heat nozzle) and updates status text
     * to show the heating target temperature. Called when handle_load_with_preheat()
     * starts heating.
     *
     * @param slot_index Slot that will be loaded after preheat
     * @param target_temp Target temperature in °C
     */
    void show_preheat_feedback(int slot_index, int target_temp);

    // === UI Module Helpers (internal, show modals with callbacks) ===

    void show_context_menu(int slot_index, lv_obj_t* near_widget, lv_point_t click_pt);
    void show_edit_modal(int slot_index);
    void show_loading_error_modal();

    // === Action Handlers (public for XML event callbacks) ===
  public:
    void handle_slot_tap(int slot_index, lv_point_t click_pt);
    void handle_unload();
    void handle_reset();
    void handle_bypass_toggle();
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
