// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_panel_base.h"

// Forward declaration
class TempControlPanel;

/**
 * @file ui_panel_controls.h
 * @brief Controls panel - Launcher menu for manual printer control screens
 *
 * A card-based launcher panel providing access to motion control, temperature
 * management, and extrusion control screens. Each card click lazily creates
 * the corresponding overlay panel.
 *
 * ## Key Features:
 * - Card-based launcher menu with 6 control categories
 * - Lazy creation of overlay panels (motion, nozzle temp, bed temp, extrusion)
 * - Navigation stack integration for overlay management
 *
 * ## Launcher Pattern:
 * Each card click handler:
 * 1. Creates the target panel on first access (lazy initialization)
 * 2. Pushes it onto the navigation stack via ui_nav_push_overlay()
 * 3. Stores panel reference for subsequent clicks
 *
 * ## Cards:
 * - Motion: Jog controls, homing, XYZ positioning
 * - Nozzle Temp: Extruder temperature control
 * - Bed Temp: Heatbed temperature control
 * - Extrusion: Filament extrusion/retraction controls
 * - Fan: (Phase 2 - placeholder)
 * - Motors: Disable steppers (TODO)
 *
 * ## Migration Notes:
 * Phase 3 migration - similar to SettingsPanel launcher pattern.
 * Demonstrates panel composition where a launcher manages child panels.
 *
 * @see PanelBase for base class documentation
 * @see ui_nav for overlay navigation
 */
class ControlsPanel : public PanelBase {
  public:
    /**
     * @brief Construct ControlsPanel with injected dependencies
     *
     * @param printer_state Reference to PrinterState
     * @param api Pointer to MoonrakerAPI (may be nullptr)
     *
     * @note Dependencies passed for interface consistency with PanelBase.
     *       Child panels (motion, temp, etc.) may use these when wired.
     */
    ControlsPanel(PrinterState& printer_state, MoonrakerAPI* api);

    ~ControlsPanel() override = default;

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
     * @brief Initialize subjects for child panels
     *
     * Currently a no-op as launcher-level doesn't own subjects.
     * Child panels initialize their own subjects when created.
     */
    void init_subjects() override;

    /**
     * @brief Setup the controls panel with launcher card event handlers
     *
     * Finds all launcher cards by name and wires up click handlers.
     * Cards: motion, nozzle_temp, bed_temp, extrusion, fan (disabled), motors
     *
     * @param panel Root panel object from lv_xml_create()
     * @param parent_screen Parent screen (needed for overlay panel creation)
     */
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;

    const char* get_name() const override { return "Controls Panel"; }
    const char* get_xml_component_name() const override { return "controls_panel"; }

  private:
    //
    // === Dependencies ===
    //

    TempControlPanel* temp_control_panel_ = nullptr;

    //
    // === Lazily-Created Child Panels ===
    //

    lv_obj_t* motion_panel_ = nullptr;
    lv_obj_t* nozzle_temp_panel_ = nullptr;
    lv_obj_t* bed_temp_panel_ = nullptr;
    lv_obj_t* extrusion_panel_ = nullptr;

    //
    // === Private Helpers ===
    //

    /**
     * @brief Wire up click handlers for all launcher cards
     */
    void setup_card_handlers();

    //
    // === Card Click Handlers ===
    //

    void handle_motion_clicked();
    void handle_nozzle_temp_clicked();
    void handle_bed_temp_clicked();
    void handle_extrusion_clicked();
    void handle_fan_clicked();
    void handle_motors_clicked();

    //
    // === Static Trampolines ===
    //
    // LVGL callbacks must be static. These trampolines extract the
    // ControlsPanel* from user_data and delegate to instance methods.
    //

    static void on_motion_clicked(lv_event_t* e);
    static void on_nozzle_temp_clicked(lv_event_t* e);
    static void on_bed_temp_clicked(lv_event_t* e);
    static void on_extrusion_clicked(lv_event_t* e);
    static void on_fan_clicked(lv_event_t* e);
    static void on_motors_clicked(lv_event_t* e);
};

// Global instance accessor (needed by main.cpp)
ControlsPanel& get_global_controls_panel();
