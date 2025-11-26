// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_panel_base.h"

#include <string>
#include <vector>

/**
 * @file ui_panel_gcode_test.h
 * @brief G-Code Viewer test/demonstration panel (class-based)
 *
 * Test panel for the 3D G-code visualization widget. Provides:
 * - Full-screen G-code viewer with TinyGL rendering
 * - Camera control buttons (preset views: top, front, side, isometric)
 * - Zoom controls and camera reset
 * - File picker for loading different G-code files
 * - Specular/shininess sliders for material tuning
 * - Statistics display (filename, layer count, filament type)
 *
 * Access via: ./build/bin/helix-ui-proto -p gcode-test
 *
 * ## Migration Notes:
 * This panel doesn't use reactive subjects or MoonrakerAPI - it's a
 * standalone test panel for the G-code viewer widget. The file picker
 * overlay is created dynamically and destroyed when closed.
 *
 * @see PanelBase for base class documentation
 * @see ui_gcode_viewer.h for the G-code viewer widget API
 */
class GcodeTestPanel : public PanelBase {
  public:
    /**
     * @brief Construct GcodeTestPanel with injected dependencies
     *
     * @param printer_state Reference to PrinterState (not actively used)
     * @param api Pointer to MoonrakerAPI (not actively used)
     *
     * @note Dependencies are passed for interface consistency with PanelBase,
     *       but this panel doesn't require printer connectivity.
     */
    GcodeTestPanel(PrinterState& printer_state, MoonrakerAPI* api);

    /**
     * @brief Destructor - cleans up file picker overlay if open
     *
     * @note Does NOT call LVGL functions (static destruction order safety).
     *       File picker overlay is managed by LVGL's widget tree.
     */
    ~GcodeTestPanel() override;

    //
    // === PanelBase Implementation ===
    //

    /**
     * @brief No-op for GcodeTestPanel (no subjects to initialize)
     */
    void init_subjects() override;

    /**
     * @brief Setup the G-code test panel
     *
     * Wires up all button callbacks, slider callbacks, and initiates
     * auto-loading of the default G-code file.
     *
     * @param panel Root panel object from lv_xml_create()
     * @param parent_screen Parent screen (unused for this panel)
     */
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;

    const char* get_name() const override { return "G-Code Test Panel"; }
    const char* get_xml_component_name() const override { return "gcode_test_panel"; }

    //
    // === Panel-Specific API ===
    //

    /**
     * @brief Show the file picker overlay
     *
     * Scans the assets/gcode directory for .gcode files and displays
     * a selection dialog. Selected file is loaded asynchronously.
     */
    void show_file_picker();

    /**
     * @brief Close the file picker overlay if open
     */
    void close_file_picker();

    /**
     * @brief Load a G-code file into the viewer
     *
     * @param filepath Path to the G-code file
     */
    void load_file(const char* filepath);

    /**
     * @brief Clear the current G-code from the viewer
     */
    void clear_viewer();

  private:
    //
    // === Widget References ===
    //

    lv_obj_t* gcode_viewer_ = nullptr;
    lv_obj_t* stats_label_ = nullptr;
    lv_obj_t* file_picker_overlay_ = nullptr;

    //
    // === File Browser State ===
    //

    std::vector<std::string> gcode_files_;

    //
    // === Constants ===
    //

    static constexpr const char* ASSETS_DIR = "assets/gcode";
    static constexpr const char* DEFAULT_TEST_FILE = "OrcaCube AD5M.gcode";

    //
    // === Internal Methods ===
    //

    /**
     * @brief Scan assets directory for .gcode files
     */
    void scan_gcode_files();

    /**
     * @brief Update stats label with file information
     *
     * @param filename Displayed filename
     * @param layer_count Number of layers
     * @param filament_type Filament type string (may be semicolon-separated)
     */
    void update_stats_label(const char* filename, int layer_count, const char* filament_type);

    /**
     * @brief Wire up event callbacks for all buttons and sliders
     */
    void setup_callbacks();

    /**
     * @brief Apply runtime config camera settings
     */
    void apply_runtime_config();

    //
    // === Static Callbacks (trampolines) ===
    //

    /**
     * @brief Callback invoked when async G-code loading completes
     */
    static void on_gcode_load_complete_static(lv_obj_t* viewer, void* user_data, bool success);

    /**
     * @brief File list item click handler
     */
    static void on_file_selected_static(lv_event_t* e);

    /**
     * @brief Close button handler for file picker
     */
    static void on_file_picker_close_static(lv_event_t* e);

    /**
     * @brief View preset button click handler
     */
    static void on_view_preset_clicked_static(lv_event_t* e);

    /**
     * @brief Zoom button click handler
     */
    static void on_zoom_clicked_static(lv_event_t* e);

    /**
     * @brief Load file button click handler
     */
    static void on_load_test_file_static(lv_event_t* e);

    /**
     * @brief Clear button click handler
     */
    static void on_clear_static(lv_event_t* e);

    /**
     * @brief Specular intensity slider callback
     */
    static void on_specular_intensity_changed_static(lv_event_t* e);

    /**
     * @brief Shininess slider callback
     */
    static void on_shininess_changed_static(lv_event_t* e);

    //
    // === Instance Methods (called by trampolines) ===
    //

    void handle_gcode_load_complete(bool success);
    void handle_file_selected(uint32_t index);
    void handle_view_preset(const char* button_name, lv_obj_t* btn);
    void handle_zoom(const char* button_name);
    void handle_specular_change(lv_obj_t* slider);
    void handle_shininess_change(lv_obj_t* slider);
};

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================
//
// Single global instance for compatibility with main.cpp panel creation.
// Created lazily on first use.
// ============================================================================

/**
 * @brief Get or create the global GcodeTestPanel instance
 *
 * @param printer_state Reference to PrinterState
 * @param api Pointer to MoonrakerAPI (may be nullptr)
 * @return Pointer to the global instance
 */
GcodeTestPanel* get_gcode_test_panel(PrinterState& printer_state, MoonrakerAPI* api);


// Global instance accessor (needed by main.cpp)
GcodeTestPanel& get_global_gcode_test_panel();

// Legacy create wrapper (test panel - still used by main.cpp)
lv_obj_t* ui_panel_gcode_test_create(lv_obj_t* parent);
