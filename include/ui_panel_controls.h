// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_heating_animator.h"
#include "ui_observer_guard.h"
#include "ui_panel_base.h"
#include "ui_print_tune_overlay.h"

#include "config.h"
#include "operation_timeout_guard.h"
#include "standard_macros.h"
#include "subject_managed_panel.h"
#include "ui/position_observer_bundle.h"
#include "ui/temperature_observer_bundle.h"
#include "ui/ui_modal_guard.h"

#include <optional>

// Forward declaration
class TempControlPanel;

/**
 * @file ui_panel_controls.h
 * @brief Controls Panel V2 - Dashboard with 5 smart cards
 *
 * A card-based dashboard providing quick access to printer controls with
 * live data display. Uses proper reactive XML event_cb bindings.
 *
 * ## V2 Layout (3+1 Grid):
 * - Row 1: Quick Actions | Temperatures | Cooling
 * - Row 2: Calibration & Tools (centered)
 *
 * ## Key Features:
 * - Combined nozzle + bed temperature card with dual progress bars
 * - Quick Actions: Home buttons (All/XY/Z) + configurable macro slots
 * - Cooling: Part fan hero slider + secondary fans list
 * - Calibration: Bed mesh, Z-offset, screws, motor disable
 *
 * ## Event Binding Pattern:
 * - Button event handlers: XML `event_cb` + `lv_xml_register_event_cb()`
 * - Card background clicks: Manual `lv_obj_add_event_cb()` with user_data
 * - Observer callbacks: RAII ObserverGuard for automatic cleanup
 *
 * @see PanelBase for base class documentation
 * @see ui_nav for overlay navigation
 */
class ControlsPanel : public PanelBase {
  public:
    /**
     * @brief Construct ControlsPanel with injected dependencies
     *
     * @param printer_state Reference to helix::PrinterState
     * @param api Pointer to MoonrakerAPI (may be nullptr)
     */
    ControlsPanel(helix::PrinterState& printer_state, MoonrakerAPI* api);

    ~ControlsPanel() override;

    /**
     * @brief Set reference to TempControlPanel for temperature sub-screens
     *
     * Must be called before setup() if temperature panels should work.
     * @param temp_panel Pointer to TempControlPanel instance
     */
    void set_temp_control_panel(TempControlPanel* temp_panel);

    //
    // === PanelBase Implementation ===
    //

    /**
     * @brief Initialize subjects and register XML event callbacks
     *
     * Registers all V2 dashboard subjects for reactive data binding
     * and registers XML event_cb handlers for buttons.
     */
    void init_subjects() override;

    /**
     * @brief Deinitialize subjects for clean shutdown
     *
     * Calls lv_subject_deinit() on all local lv_subject_t members.
     * Must be called before lv_deinit() to prevent dangling observers.
     */
    void deinit_subjects();

    /**
     * @brief Setup the controls panel with card navigation handlers
     *
     * Wires up card background click handlers for navigation to full panels.
     * All button handlers are already wired via XML event_cb in init_subjects().
     *
     * @param panel Root panel object from lv_xml_create()
     * @param parent_screen Parent screen (needed for overlay panel creation)
     */
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;

    const char* get_name() const override {
        return "Controls Panel";
    }
    const char* get_xml_component_name() const override {
        return "controls_panel";
    }

    /**
     * @brief Called when panel becomes visible
     *
     * Refreshes the secondary fans list to handle cases where fan discovery
     * completed after initial setup or when switching between connections.
     */
    void on_activate() override;

  private:
    //
    // === Dependencies ===
    //

    TempControlPanel* temp_control_panel_ = nullptr;

    //
    // === Configurable Macro Buttons (StandardMacros integration) ===
    //

    std::optional<StandardMacroSlot> macro_1_slot_; ///< Slot for macro button 1
    std::optional<StandardMacroSlot> macro_2_slot_; ///< Slot for macro button 2

    /**
     * @brief Refresh macro button labels and visibility
     *
     * Called after StandardMacros config changes to update button text
     * and hide buttons for empty slots.
     */
    void refresh_macro_buttons();

    //
    // === Subject Manager (RAII cleanup) ===
    //

    SubjectManager subjects_; ///< RAII subject manager - auto-deinits all subjects

    //
    // === V2 Dashboard Subjects (for XML bind_text/bind_value) ===
    //

    // Nozzle label (dynamic: "Nozzle:" or "Nozzle N:" for multi-tool)
    lv_subject_t nozzle_label_subject_{};
    char nozzle_label_buf_[32] = {};
    ObserverGuard active_tool_observer_;
    void update_nozzle_label();

    // Nozzle temperature display
    lv_subject_t nozzle_temp_subject_{};
    char nozzle_temp_buf_[32] = {};
    lv_subject_t nozzle_pct_subject_{};
    lv_subject_t nozzle_status_subject_{};
    char nozzle_status_buf_[16] = {};
    HeatingIconAnimator nozzle_heater_animator_;

    // Bed temperature display
    lv_subject_t bed_temp_subject_{};
    char bed_temp_buf_[32] = {};
    lv_subject_t bed_pct_subject_{};
    lv_subject_t bed_status_subject_{};
    char bed_status_buf_[16] = {};
    HeatingIconAnimator bed_heater_animator_;

    // Fan speed display
    lv_subject_t fan_speed_subject_{};
    char fan_speed_buf_[16] = {};
    lv_subject_t fan_pct_subject_{};
    uint32_t last_fan_slider_input_ = 0; // Tick of last slider interaction (suppression window)

    // Macro button subjects for declarative binding
    lv_subject_t macro_1_visible_{};
    lv_subject_t macro_2_visible_{};
    lv_subject_t macro_1_name_{};
    lv_subject_t macro_2_name_{};
    char macro_1_name_buf_[64] = {};
    char macro_2_name_buf_[64] = {};

    //
    // === Cached Values (for display update efficiency) ===
    //

    int cached_extruder_temp_ = 0;
    int cached_extruder_target_ = 0;
    int cached_bed_temp_ = 0;
    int cached_bed_target_ = 0;

    //
    // === Observer Guards (RAII cleanup) ===
    //

    /// @brief Temperature observer bundle (nozzle + bed temps)
    helix::ui::TemperatureObserverBundle<ControlsPanel> temp_observers_;
    ObserverGuard fan_observer_;
    ObserverGuard fans_version_observer_;      // Multi-fan list changes
    ObserverGuard temp_sensor_count_observer_; // Temp sensor list changes

    //
    // === Lazily-Created Child Panels ===
    //

    lv_obj_t* motion_panel_ = nullptr;
    lv_obj_t* nozzle_temp_panel_ = nullptr;
    lv_obj_t* bed_temp_panel_ = nullptr;
    lv_obj_t* fan_control_panel_ = nullptr;
    lv_obj_t* bed_mesh_panel_ = nullptr;
    lv_obj_t* zoffset_panel_ = nullptr;
    lv_obj_t* screws_panel_ = nullptr;

    //
    // === Modal Dialog State ===
    //

    helix::ui::ModalGuard motors_confirmation_dialog_;
    helix::ui::ModalGuard save_z_offset_confirmation_dialog_;
    OperationTimeoutGuard operation_guard_;
    bool save_z_offset_in_progress_ = false; ///< Guard against double-click race condition

    //
    // === Dynamic UI Containers ===
    //

    lv_obj_t* secondary_fans_list_ = nullptr; // Container for dynamic fan rows

    /// @brief Info for a secondary fan row for reactive speed updates
    struct SecondaryFanRow {
        std::string object_name;
        lv_obj_t* speed_label = nullptr;
    };
    std::vector<SecondaryFanRow> secondary_fan_rows_;    ///< Tracked for reactive updates
    std::vector<ObserverGuard> secondary_fan_observers_; ///< Per-fan speed observers
    uint32_t fan_populate_gen_ = 0; ///< Incremented on each populate; stale callbacks skip

    lv_obj_t* secondary_temps_list_ = nullptr; // Container for dynamic temp sensor rows

    /// @brief Info for a secondary temperature sensor row for reactive temp updates
    struct SecondaryTempRow {
        std::string klipper_name; // e.g., "temperature_sensor mcu_temp"
        lv_obj_t* temp_label = nullptr;
    };
    std::vector<SecondaryTempRow> secondary_temp_rows_;   ///< Tracked for reactive updates
    std::vector<ObserverGuard> secondary_temp_observers_; ///< Per-sensor temp observers
    uint32_t temp_populate_gen_ = 0; ///< Incremented on each populate; stale callbacks skip

    //
    // === Z-Offset Banner (reactive binding - no widget caching needed) ===
    //

    lv_subject_t z_offset_delta_display_subject_{}; // Formatted delta string (e.g., "+0.05mm")
    char z_offset_delta_display_buf_[32] = {};
    ObserverGuard pending_z_offset_observer_; // Observer to update display when delta changes

    //
    // === Homing Status Subjects (for bind_style visual feedback) ===
    //

    lv_subject_t x_homed_{};            // 1 if X is homed (for position indicator)
    lv_subject_t y_homed_{};            // 1 if Y is homed (for position indicator)
    lv_subject_t xy_homed_{};           // 1 if X and Y are homed
    lv_subject_t z_homed_{};            // 1 if Z is homed
    lv_subject_t all_homed_{};          // 1 if all axes are homed
    ObserverGuard homed_axes_observer_; // Observer for PrinterState::homed_axes_

    //
    // === Position Display Subjects (for Position card) ===
    //

    lv_subject_t controls_pos_x_subject_{};
    lv_subject_t controls_pos_y_subject_{};
    lv_subject_t controls_pos_z_subject_{};
    char controls_pos_x_buf_[32] = {};
    char controls_pos_y_buf_[32] = {};
    char controls_pos_z_buf_[32] = {};
    helix::ui::PositionObserverBundle<ControlsPanel> pos_observers_;

    //
    // === Z-Offset Live Tuning ===
    //

    char controls_z_offset_buf_[16] = {};
    lv_subject_t controls_z_offset_subject_{};
    ObserverGuard gcode_z_offset_observer_;

    //
    // === Speed/Flow Override Subjects ===
    //

    lv_subject_t speed_override_subject_{};
    lv_subject_t flow_override_subject_{};
    char speed_override_buf_[16] = {};
    char flow_override_buf_[16] = {};
    ObserverGuard speed_factor_observer_;
    // Note: Flow factor observer uses extrude_factor from helix::PrinterState

    //
    // === Macro Slots 3 & 4 ===
    //

    std::optional<StandardMacroSlot> macro_3_slot_;
    std::optional<StandardMacroSlot> macro_4_slot_;
    lv_subject_t macro_3_visible_{};
    lv_subject_t macro_4_visible_{};
    lv_subject_t macro_3_name_{};
    lv_subject_t macro_4_name_{};
    char macro_3_name_buf_[64] = {};
    char macro_4_name_buf_[64] = {};

    //
    // === Private Helpers ===
    //

    void setup_card_handlers();
    void register_observers();

    // Display update helpers
    void update_nozzle_temp_display();
    void update_bed_temp_display();
    void update_fan_display();
    void populate_secondary_fans();  // Build fan list from helix::PrinterState
    void populate_secondary_temps(); // Build temp sensor list from TemperatureSensorManager
    void update_z_offset_delta_display(int delta_microns); // Format delta for banner

    // Z-Offset save handler
    void handle_save_z_offset();
    void handle_save_z_offset_confirm();
    void handle_save_z_offset_cancel();

    //
    // === V2 Card Click Handlers (navigation to full panels) ===
    //

    void handle_quick_actions_clicked();
    void handle_temperatures_clicked();
    void handle_nozzle_temp_clicked();
    void handle_bed_temp_clicked();
    void handle_cooling_clicked();
    void handle_secondary_fans_clicked();
    void handle_secondary_temps_clicked();

    //
    // === Quick Action Button Handlers ===
    //

    void handle_home_all();
    void handle_home_x();
    void handle_home_y();
    void handle_home_xy();
    void handle_home_z();
    void handle_qgl();
    void handle_z_tilt();

    /**
     * @brief Execute a macro by slot index (0-3)
     *
     * Consolidates duplicate logic from handle_macro_1/2/3/4.
     * @param index Macro button index (0=macro_1, 1=macro_2, etc.)
     */
    void execute_macro(size_t index);

    /**
     * @brief Update a single macro button's visibility and label
     *
     * Used by refresh_macro_buttons() to update each button.
     * @param macros Reference to StandardMacros instance
     * @param slot Optional slot for this button (nullopt = hide)
     * @param visible_subject Subject controlling visibility binding
     * @param name_subject Subject controlling label text binding
     * @param button_num Button number for debug logging (1-4)
     */
    void update_macro_button(StandardMacros& macros, const std::optional<StandardMacroSlot>& slot,
                             lv_subject_t& visible_subject, lv_subject_t& name_subject,
                             int button_num);

    //
    // === Speed/Flow Override Handlers ===
    //

    void handle_speed_up();
    void handle_speed_down();
    void handle_flow_up();
    void handle_flow_down();
    void update_speed_display();
    void update_flow_display();

    //
    // === Z-Offset Control Handlers ===
    //

    void handle_zoffset_tune(); ///< Open Print Tune overlay for live Z-offset tuning
    void update_controls_z_offset_display(int offset_microns);

    //
    // === Fan Slider Handler ===
    //

    void handle_fan_slider_changed(int value);

    //
    // === Calibration & Motors Handlers ===
    //

    void handle_motors_clicked();
    void handle_motors_confirm();
    void handle_motors_cancel();
    void handle_calibration_bed_mesh();
    void handle_calibration_zoffset();
    void handle_calibration_screws();
    void handle_calibration_motors();

    //
    // === V2 Card Click Trampolines (manual wiring with user_data) ===
    //

    static void on_quick_actions_clicked(lv_event_t* e);
    static void on_temperatures_clicked(lv_event_t* e);
    static void on_nozzle_temp_clicked(lv_event_t* e);
    static void on_bed_temp_clicked(lv_event_t* e);
    static void on_cooling_clicked(lv_event_t* e);
    static void on_secondary_fans_clicked(lv_event_t* e);
    static void on_motors_confirm(lv_event_t* e);
    static void on_motors_cancel(lv_event_t* e);
    static void on_save_z_offset_confirm(lv_event_t* e);
    static void on_save_z_offset_cancel(lv_event_t* e);

    //
    // === Calibration Button Trampolines (XML event_cb - global accessor) ===
    //

    static void on_calibration_bed_mesh(lv_event_t* e);
    static void on_calibration_zoffset(lv_event_t* e);
    static void on_calibration_screws(lv_event_t* e);
    static void on_calibration_motors(lv_event_t* e);

    //
    // === V2 Button Trampolines (XML event_cb - global accessor) ===
    //

    static void on_home_all(lv_event_t* e);
    static void on_home_x(lv_event_t* e);
    static void on_home_y(lv_event_t* e);
    static void on_home_xy(lv_event_t* e);
    static void on_home_z(lv_event_t* e);
    static void on_qgl(lv_event_t* e);
    static void on_z_tilt(lv_event_t* e);
    static void on_macro(lv_event_t* e);
    static void on_fan_slider_changed(lv_event_t* e);
    static void on_save_z_offset(lv_event_t* e);
    static void on_speed_up(lv_event_t* e);
    static void on_speed_down(lv_event_t* e);
    static void on_flow_up(lv_event_t* e);
    static void on_flow_down(lv_event_t* e);

    //
    // === Z-Offset Trampolines (XML event_cb - global accessor) ===
    //

    static void on_zoffset_tune(lv_event_t* e);

    void subscribe_to_secondary_fan_speeds();
    void update_secondary_fan_speed(const std::string& object_name, int speed_pct);

    void subscribe_to_secondary_temp_subjects();
    void update_secondary_temp(const std::string& klipper_name, int centidegrees);
};

// Global instance accessor (needed by main.cpp and XML event_cb trampolines)
ControlsPanel& get_global_controls_panel();
