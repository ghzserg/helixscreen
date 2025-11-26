// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "moonraker_client.h"  // For MoonrakerClient::BedMeshProfile
#include "ui_panel_base.h"

#include <vector>

/**
 * @file ui_panel_bed_mesh.h
 * @brief Bed mesh visualization panel with TinyGL 3D renderer
 *
 * Interactive 3D visualization of printer bed mesh height maps with:
 * - Touch-drag rotation controls
 * - Color-coded height mapping (red=high, blue=low)
 * - Profile dropdown for switching between saved meshes
 * - Statistics display (dimensions, Z range, variance)
 *
 * ## Phase 4 Migration - Non-Reactive Visual State:
 *
 * Unlike most panels, BedMeshPanel doesn't use observers for its primary
 * visualization. The TinyGL 3D renderer is purely imperative:
 * - Call set_mesh_data() → renderer stores data
 * - Call redraw() → renderer clears canvas and re-renders
 *
 * LVGL subjects are still used for info labels (dimensions, Z range, variance),
 * but the 3D canvas itself bypasses the reactive system for performance.
 *
 * ## RAII Resource Management:
 *
 * The TinyGL renderer is managed by the <bed_mesh> XML widget, which
 * automatically allocates/frees the renderer in its create/delete callbacks.
 * BedMeshPanel just holds a pointer to the canvas - no manual cleanup needed.
 *
 * ## Moonraker Integration:
 *
 * Subscribes to bed mesh updates via MoonrakerClient notification callback.
 * When mesh data changes (BED_MESH_PROFILE LOAD=...), the callback updates
 * both the 3D visualization and the info label subjects.
 *
 * ## Reactive Subjects (owned by this panel):
 * - `bed_mesh_available` - Int: 0=no mesh, 1=mesh loaded
 * - `bed_mesh_profile_name` - String: active profile name
 * - `bed_mesh_dimensions` - String: "10x10 points"
 * - `bed_mesh_z_range` - String: "Max [0,50] = 0.35mm / Min [100,50] = 0.05mm"
 * - `bed_mesh_variance` - String: "Range: 0.457 mm"
 *
 * @see ui_bed_mesh.h for TinyGL widget API
 * @see PanelBase for base class documentation
 */

class BedMeshPanel : public PanelBase {
  public:
    /**
     * @brief Construct BedMeshPanel with injected dependencies
     *
     * @param printer_state Reference to PrinterState
     * @param api Pointer to MoonrakerAPI (for profile loading)
     */
    BedMeshPanel(PrinterState& printer_state, MoonrakerAPI* api);

    ~BedMeshPanel() override;

    //
    // === PanelBase Implementation ===
    //

    /**
     * @brief Initialize subjects for XML binding
     *
     * Registers: bed_mesh_available, bed_mesh_profile_name,
     *            bed_mesh_dimensions, bed_mesh_z_range, bed_mesh_variance
     */
    void init_subjects() override;

    /**
     * @brief Setup 3D renderer and Moonraker subscription
     *
     * - Finds <bed_mesh> canvas widget in XML
     * - Populates profile dropdown from Moonraker
     * - Registers for mesh update notifications
     * - Loads initial mesh data (if available)
     *
     * @param panel Root panel object from lv_xml_create()
     * @param parent_screen Parent screen for navigation
     */
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;

    const char* get_name() const override { return "Bed Mesh Panel"; }
    const char* get_xml_component_name() const override { return "bed_mesh_panel"; }

    //
    // === Public API ===
    //

    /**
     * @brief Load mesh data and render
     *
     * Updates the renderer with new mesh height data and triggers a redraw.
     * Also updates info label subjects (dimensions, Z range, variance).
     *
     * @param mesh_data 2D vector of height values (row-major order)
     */
    void set_mesh_data(const std::vector<std::vector<float>>& mesh_data);

    /**
     * @brief Force redraw of bed mesh visualization
     *
     * Clears the canvas and re-renders the mesh with current rotation angles.
     */
    void redraw();

  private:
    //
    // === Subjects (owned by this panel) ===
    //

    lv_subject_t bed_mesh_available_;
    lv_subject_t bed_mesh_profile_name_;
    lv_subject_t bed_mesh_dimensions_;
    lv_subject_t bed_mesh_z_range_;
    lv_subject_t bed_mesh_variance_;

    // Subject storage buffers (LVGL requires persistent memory)
    char profile_name_buf_[64];
    char dimensions_buf_[64];
    char z_range_buf_[96];  // Larger for coordinate display
    char variance_buf_[64];

    //
    // === Instance State ===
    //

    lv_obj_t* canvas_ = nullptr;
    lv_obj_t* profile_dropdown_ = nullptr;

    //
    // === Private Helpers ===
    //

    void setup_profile_dropdown();
    void setup_moonraker_subscription();
    void on_mesh_update_internal(const MoonrakerClient::BedMeshProfile& mesh);
    void update_info_subjects(const std::vector<std::vector<float>>& mesh_data, int cols, int rows);

    //
    // === Static Trampolines ===
    //

    static void on_panel_delete(lv_event_t* e);
    static void on_profile_dropdown_changed(lv_event_t* e);
};


// Global instance accessor (needed by main.cpp)
BedMeshPanel& get_global_bed_mesh_panel();
