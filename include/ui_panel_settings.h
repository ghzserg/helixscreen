// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_panel_base.h"

/**
 * @file ui_panel_settings.h
 * @brief Settings panel - Launcher menu for configuration screens
 *
 * A card-based launcher panel providing access to various configuration
 * and calibration screens (network, display, bed mesh, Z-offset, etc.).
 *
 * ## Key Features:
 * - Card-based launcher menu with 6 settings categories
 * - Lazy creation of overlay panels (bed mesh, etc.)
 * - Navigation stack integration for overlay management
 *
 * ## Launcher Pattern:
 * Each card click handler:
 * 1. Creates the target panel on first access (lazy initialization)
 * 2. Pushes it onto the navigation stack via ui_nav_push_overlay()
 * 3. Stores panel reference for subsequent clicks
 *
 * ## Migration Notes:
 * First Phase 3 panel migrated to class-based architecture.
 * Demonstrates the launcher pattern where clicks spawn overlay panels.
 *
 * @see PanelBase for base class documentation
 * @see ui_nav for overlay navigation
 */
class SettingsPanel : public PanelBase {
  public:
    /**
     * @brief Construct SettingsPanel with injected dependencies
     *
     * @param printer_state Reference to PrinterState (for future use)
     * @param api Pointer to MoonrakerAPI (for future use)
     *
     * @note Dependencies passed for interface consistency with PanelBase.
     *       Currently only bed mesh uses these indirectly.
     */
    SettingsPanel(PrinterState& printer_state, MoonrakerAPI* api);

    ~SettingsPanel() override = default;

    //
    // === PanelBase Implementation ===
    //

    /**
     * @brief Initialize subjects for child panels
     *
     * Delegates to ui_panel_bed_mesh_init_subjects() since the bed mesh
     * panel may be lazily created when its card is clicked.
     */
    void init_subjects() override;

    /**
     * @brief Setup the settings panel with launcher card event handlers
     *
     * Finds all launcher cards by name and wires up click handlers.
     * Currently only the bed mesh card is fully active; others are placeholders.
     *
     * @param panel Root panel object from lv_xml_create()
     * @param parent_screen Parent screen (needed for overlay panel creation)
     */
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;

    const char* get_name() const override { return "Settings Panel"; }
    const char* get_xml_component_name() const override { return "settings_panel"; }

  private:
    //
    // === Instance State ===
    //

    /// Lazily-created bed mesh visualization panel
    lv_obj_t* bed_mesh_panel_ = nullptr;

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

    void handle_network_clicked();
    void handle_display_clicked();
    void handle_bed_mesh_clicked();
    void handle_z_offset_clicked();
    void handle_printer_info_clicked();
    void handle_about_clicked();

    //
    // === Static Trampolines ===
    //
    // LVGL callbacks must be static. These trampolines extract the
    // SettingsPanel* from user_data and delegate to instance methods.
    //

    static void on_network_clicked(lv_event_t* e);
    static void on_display_clicked(lv_event_t* e);
    static void on_bed_mesh_clicked(lv_event_t* e);
    static void on_z_offset_clicked(lv_event_t* e);
    static void on_printer_info_clicked(lv_event_t* e);
    static void on_about_clicked(lv_event_t* e);
};

// Global instance accessor (needed by main.cpp)
SettingsPanel& get_global_settings_panel();
