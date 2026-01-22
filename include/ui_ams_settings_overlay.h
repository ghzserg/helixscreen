// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_ams_settings_overlay.h
 * @brief AMS Settings overlay - iOS Settings-style navigation panel
 *
 * This overlay provides access to AMS configuration:
 * - Tool Mapping
 * - Endless Spool settings
 * - Maintenance options
 * - Calibration
 * - Speed Settings
 * - Spoolman integration
 *
 * @pattern Overlay (lazy init, singleton)
 * @threading Main thread only
 */

#pragma once

#include "overlay_base.h"

#include <lvgl/lvgl.h>

#include <memory>

namespace helix::ui {

/**
 * @class AmsSettingsOverlay
 * @brief Overlay for AMS configuration settings
 *
 * This overlay provides an iOS Settings-style interface where tapping
 * a row slides to a sub-panel for detailed configuration.
 *
 * ## Usage:
 *
 * @code
 * auto& overlay = helix::ui::get_ams_settings_overlay();
 * if (!overlay.are_subjects_initialized()) {
 *     overlay.init_subjects();
 *     overlay.register_callbacks();
 * }
 * overlay.show(parent_screen);
 * @endcode
 */
class AmsSettingsOverlay : public OverlayBase {
  public:
    /**
     * @brief Default constructor
     */
    AmsSettingsOverlay();

    /**
     * @brief Destructor
     */
    ~AmsSettingsOverlay() override;

    // Non-copyable
    AmsSettingsOverlay(const AmsSettingsOverlay&) = delete;
    AmsSettingsOverlay& operator=(const AmsSettingsOverlay&) = delete;

    //
    // === OverlayBase Interface ===
    //

    /**
     * @brief Initialize subjects for reactive binding
     *
     * Registers subjects for:
     * - ams_settings_version: Backend version string
     * - ams_settings_slot_count: Slot count label
     */
    void init_subjects() override;

    /**
     * @brief Register event callbacks with lv_xml system
     *
     * Registers callbacks for all navigation row clicks.
     */
    void register_callbacks() override;

    /**
     * @brief Create the overlay UI (called lazily)
     *
     * @param parent Parent widget to attach overlay to (usually screen)
     * @return Root object of overlay, or nullptr on failure
     */
    lv_obj_t* create(lv_obj_t* parent) override;

    /**
     * @brief Get human-readable overlay name
     * @return "AMS Settings"
     */
    const char* get_name() const override {
        return "AMS Settings";
    }

    //
    // === Public API ===
    //

    /**
     * @brief Show the overlay
     *
     * This method:
     * 1. Ensures overlay is created (lazy init)
     * 2. Updates status card from backend
     * 3. Pushes overlay onto navigation stack
     *
     * @param parent_screen The parent screen for overlay creation
     */
    void show(lv_obj_t* parent_screen);

    /**
     * @brief Update the status card with backend info
     *
     * Updates backend logo, version, and connection status.
     */
    void update_status_card();

    /**
     * @brief Get the parent screen used for overlay creation
     *
     * Used by sub-panels to create their overlays on the same screen.
     *
     * @return Parent screen pointer, or nullptr if not yet shown
     */
    lv_obj_t* get_parent_screen() const {
        return parent_screen_;
    }

  private:
    //
    // === Static Callbacks for XML ===
    //

    static void on_tool_mapping_clicked(lv_event_t* e);
    static void on_endless_spool_clicked(lv_event_t* e);
    static void on_device_operations_clicked(lv_event_t* e);
    static void on_maintenance_clicked(lv_event_t* e);
    static void on_behavior_clicked(lv_event_t* e);
    static void on_calibration_clicked(lv_event_t* e);
    static void on_speed_settings_clicked(lv_event_t* e);
    static void on_spoolman_clicked(lv_event_t* e);

    //
    // === State ===
    //

    /// Alias for overlay_root_ to match existing pattern
    lv_obj_t*& overlay_ = overlay_root_;

    /// Subjects for reactive binding
    lv_subject_t version_subject_;
    char version_buf_[32];

    lv_subject_t slot_count_subject_;
    char slot_count_buf_[16];

    /// Connection status subject (0=disconnected, 1=connected)
    lv_subject_t connection_status_subject_;

    /// Navigation row summary subjects
    lv_subject_t tool_mapping_summary_subject_;
    char tool_mapping_summary_buf_[32];

    lv_subject_t endless_spool_summary_subject_;
    char endless_spool_summary_buf_[32];

    lv_subject_t device_operations_summary_subject_;
    char device_operations_summary_buf_[32];

    lv_subject_t maintenance_summary_subject_;
    char maintenance_summary_buf_[32];

    lv_subject_t behavior_summary_subject_;
    char behavior_summary_buf_[32];

    lv_subject_t calibration_summary_subject_;
    char calibration_summary_buf_[32];

    lv_subject_t speed_summary_subject_;
    char speed_summary_buf_[32];

    lv_subject_t spoolman_summary_subject_;
    char spoolman_summary_buf_[32];

    /**
     * @brief Update navigation row summaries from backend data
     */
    void update_nav_summaries();
};

/**
 * @brief Global instance accessor
 *
 * Creates the overlay on first access and registers it for cleanup
 * with StaticPanelRegistry.
 *
 * @return Reference to singleton AmsSettingsOverlay
 */
AmsSettingsOverlay& get_ams_settings_overlay();

} // namespace helix::ui
