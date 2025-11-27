// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_panel_base.h"

/**
 * @file ui_panel_extrusion.h
 * @brief Extrusion control panel - filament extrude/retract with safety checks
 *
 * Provides manual filament control with:
 * - Amount selector (5, 10, 25, 50mm)
 * - Extrude/Retract buttons
 * - Cold extrusion prevention (requires nozzle >= 170°C)
 * - Safety warning card when too cold
 *
 * ## Phase 4 Migration - Cross-Panel Observer Pattern:
 *
 * This panel demonstrates WATCHING subjects owned by another panel.
 * The nozzle temperature subject is owned by TempControlPanel, but
 * ExtrusionPanel observes it to enable/disable controls.
 *
 * Key difference from Phase 3 panels:
 * - Uses lv_xml_get_subject(NULL, name) to find external subjects
 * - Registers observer with register_observer() for RAII cleanup
 * - Safety logic depends on real-time temperature updates
 *
 * ## Reactive Subjects (owned by this panel):
 * - `extrusion_temp_status` - Temperature display string (e.g., "185 / 200°C ✓")
 * - `extrusion_warning_temps` - Warning card text (e.g., "Current: 25°C\nTarget: 0°C")
 *
 * ## External Subjects (observed, not owned):
 * - `nozzle_temp_current` - Current nozzle temperature (owned by TempControlPanel)
 *
 * @see PanelBase for base class documentation
 * @see TempControlPanel for the subject owner
 */

class ExtrusionPanel : public PanelBase {
  public:
    /**
     * @brief Construct ExtrusionPanel with injected dependencies
     *
     * @param printer_state Reference to PrinterState
     * @param api Pointer to MoonrakerAPI (for extrude/retract commands)
     */
    ExtrusionPanel(PrinterState& printer_state, MoonrakerAPI* api);

    ~ExtrusionPanel() override = default;

    //
    // === PanelBase Implementation ===
    //

    /**
     * @brief Initialize subjects for XML binding
     *
     * Registers: extrusion_temp_status, extrusion_warning_temps
     */
    void init_subjects() override;

    /**
     * @brief Setup button handlers and subscribe to temperature updates
     *
     * - Wires amount selector buttons
     * - Wires extrude/retract buttons
     * - Subscribes to nozzle temperature subject (if available)
     *
     * @param panel Root panel object from lv_xml_create()
     * @param parent_screen Parent screen for navigation
     */
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;

    const char* get_name() const override { return "Extrusion Panel"; }
    const char* get_xml_component_name() const override { return "extrusion_panel"; }

    //
    // === Public API ===
    //

    /**
     * @brief Update nozzle temperature display and safety state
     *
     * @param current Current nozzle temperature in °C
     * @param target Target nozzle temperature in °C
     */
    void set_temp(int current, int target);

    /**
     * @brief Get currently selected extrusion amount
     * @return Extrusion amount in mm (5, 10, 25, or 50)
     */
    int get_amount() const { return selected_amount_; }

    /**
     * @brief Check if extrusion is allowed (nozzle hot enough)
     * @return true if nozzle >= MIN_EXTRUSION_TEMP (170°C)
     */
    bool is_extrusion_allowed() const;

    /**
     * @brief Set temperature validation limits
     *
     * Call after querying Moonraker for heater configuration.
     *
     * @param min_temp Minimum safe nozzle temperature in °C
     * @param max_temp Maximum safe nozzle temperature in °C
     */
    void set_limits(int min_temp, int max_temp);

  private:
    //
    // === Subjects (owned by this panel) ===
    //

    lv_subject_t temp_status_subject_;
    lv_subject_t warning_temps_subject_;

    // Subject storage buffers
    char temp_status_buf_[64];
    char warning_temps_buf_[64];

    //
    // === Instance State ===
    //

    int nozzle_current_ = 25;
    int nozzle_target_ = 0;
    int selected_amount_ = 10; // Default: 10mm

    // Temperature limits (can be updated from Moonraker)
    int nozzle_min_temp_ = 0;
    int nozzle_max_temp_ = 500;

    // Child widgets
    lv_obj_t* btn_extrude_ = nullptr;
    lv_obj_t* btn_retract_ = nullptr;
    lv_obj_t* safety_warning_ = nullptr;
    lv_obj_t* amount_buttons_[4] = {nullptr};

    static constexpr int AMOUNT_VALUES[4] = {5, 10, 25, 50};

    //
    // === Private Helpers ===
    //

    void setup_amount_buttons();
    void setup_action_buttons();
    void setup_temperature_observer();

    void update_temp_status();
    void update_warning_text();
    void update_safety_state();
    void update_amount_buttons_visual();

    //
    // === Instance Handlers ===
    //

    void handle_amount_button(lv_obj_t* btn);
    void handle_extrude();
    void handle_retract();

    //
    // === Static Trampolines ===
    //

    static void on_amount_button_clicked(lv_event_t* e);
    static void on_extrude_clicked(lv_event_t* e);
    static void on_retract_clicked(lv_event_t* e);

    /**
     * @brief Observer callback for nozzle temperature changes
     *
     * Called when the external nozzle_temp_current subject updates.
     * Updates local state and refreshes UI.
     */
    static void on_nozzle_temp_changed(lv_observer_t* observer, lv_subject_t* subject);
};


// Global instance accessor (needed by main.cpp)
ExtrusionPanel& get_global_controls_extrusion_panel();
