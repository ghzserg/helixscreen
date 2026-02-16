// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "overlay_base.h"
#include "subject_managed_panel.h"

#include <lvgl.h>
#include <memory>
#include <string>

class MoonrakerAPI;

/**
 * @file ui_probe_overlay.h
 * @brief Probe management overlay with type-specific controls
 *
 * Provides a dedicated overlay for probe management:
 * - Header showing probe identity, type, and status
 * - Type-specific control panel (BLTouch, Cartographer, Beacon, etc.)
 * - Universal actions (accuracy test, z-offset calibration, bed mesh)
 *
 * ## Architecture:
 *
 * The overlay uses a swappable type-specific panel approach. On activation,
 * it checks the detected probe type from ProbeSensorManager and loads
 * the appropriate component XML into the `probe_type_panel` container.
 *
 * ## Usage:
 * ```cpp
 * ProbeOverlay& overlay = get_global_probe_overlay();
 * overlay.init_subjects();  // Once at startup
 * overlay.create(screen);   // Lazy create
 * overlay.show();           // Opens overlay
 * ```
 */
class ProbeOverlay : public OverlayBase {
  public:
    ProbeOverlay() = default;
    ~ProbeOverlay() override;

    //
    // === OverlayBase Interface ===
    //

    void init_subjects() override;
    lv_obj_t* create(lv_obj_t* parent) override;

    const char* get_name() const override {
        return "Probe";
    }

    void on_activate() override;
    void on_deactivate() override;
    void cleanup() override;

    //
    // === Public API ===
    //

    void show();
    void set_api(MoonrakerAPI* api);

    //
    // === Event Handlers (public for XML event_cb callbacks) ===
    //

    void handle_probe_accuracy();
    void handle_zoffset_cal();
    void handle_bed_mesh();

  private:
    // Subject manager for RAII cleanup
    SubjectManager subjects_;

    // Display subjects
    char probe_display_name_buf_[48] = {};
    lv_subject_t probe_display_name_{};
    char probe_type_label_buf_[64] = {};
    lv_subject_t probe_type_label_{};
    char probe_z_offset_display_buf_[32] = {};
    lv_subject_t probe_z_offset_display_{};

    // State subject for overlay mode
    lv_subject_t probe_overlay_state_{};

    // Accuracy test result subjects
    char probe_accuracy_result_buf_[128] = {};
    lv_subject_t probe_accuracy_result_{};
    lv_subject_t probe_accuracy_visible_{};

    // Widget/client references
    lv_obj_t* parent_screen_ = nullptr;
    MoonrakerAPI* api_ = nullptr;

    // Type-specific panel container
    lv_obj_t* type_panel_container_ = nullptr;

    // Load type-specific panel based on detected probe type
    void load_type_panel();

    // Update display subjects from ProbeSensorManager
    void update_display_subjects();
};

// Global instance accessor
ProbeOverlay& get_global_probe_overlay();

/**
 * @brief Register XML event callbacks for probe overlay
 *
 * Call once at startup before creating any probe_overlay XML.
 */
void ui_probe_overlay_register_callbacks();

/**
 * @brief Initialize row click callback for opening from Advanced panel
 *
 * Registers "on_probe_row_clicked" callback.
 */
void init_probe_row_handler();
