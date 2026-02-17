// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_ams_device_operations_overlay.h
 * @brief AMS Device Operations overlay with progressive disclosure
 *
 * This overlay shows Quick Actions at the top and a list of device
 * sections below. Tapping a section row pushes the detail overlay
 * (AmsDeviceSectionDetailOverlay) with that section's controls.
 *
 * @pattern Overlay (lazy init, singleton)
 * @threading Main thread only
 */

#pragma once

#include "ams_types.h"
#include "overlay_base.h"

#include <lvgl/lvgl.h>

#include <string>
#include <vector>

// Forward declarations
class AmsBackend;

namespace helix::ui {

/**
 * @class AmsDeviceOperationsOverlay
 * @brief Progressive disclosure overlay for AMS device operations
 *
 * Quick Actions card at top (Home/Recover/Abort/Bypass/Status),
 * then a list of section rows (icon + label + chevron). Tapping
 * a row pushes AmsDeviceSectionDetailOverlay with that section's controls.
 */
class AmsDeviceOperationsOverlay : public OverlayBase {
  public:
    /**
     * @brief Default constructor
     */
    AmsDeviceOperationsOverlay();

    /**
     * @brief Destructor
     */
    ~AmsDeviceOperationsOverlay() override;

    // Non-copyable
    AmsDeviceOperationsOverlay(const AmsDeviceOperationsOverlay&) = delete;
    AmsDeviceOperationsOverlay& operator=(const AmsDeviceOperationsOverlay&) = delete;

    //
    // === OverlayBase Interface ===
    //

    /**
     * @brief Initialize subjects for reactive binding
     */
    void init_subjects() override;

    /**
     * @brief Register event callbacks with lv_xml system
     *
     * Registers callbacks for:
     * - Home, Recover, Abort buttons
     * - Bypass toggle
     * - Dynamic action buttons
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
     * @return "Device Operations"
     */
    const char* get_name() const override {
        return "AMS Management";
    }

    //
    // === Public API ===
    //

    /**
     * @brief Show the overlay
     *
     * This method:
     * 1. Ensures overlay is created (lazy init)
     * 2. Queries backend for capabilities and actions
     * 3. Updates subjects and dynamic UI
     * 4. Pushes overlay onto navigation stack
     *
     * @param parent_screen The parent screen for overlay creation
     */
    void show(lv_obj_t* parent_screen);

    /**
     * @brief Refresh the overlay from backend
     *
     * Re-queries backend and updates all subjects and dynamic actions.
     */
    void refresh();

  private:
    //
    // === Internal Methods ===
    //

    /// Update subjects from backend state
    void update_from_backend();

    /// Populate section list rows from backend sections
    void populate_section_list();

    /// Create a single section row (icon + label + chevron)
    void create_section_row(lv_obj_t* parent, const helix::printer::DeviceSection& section);

    /// Convert AmsAction enum to human-readable string
    static const char* action_to_string(int action);

    //
    // === Static Callbacks ===
    //

    static void on_home_clicked(lv_event_t* e);
    static void on_recover_clicked(lv_event_t* e);
    static void on_abort_clicked(lv_event_t* e);
    static void on_bypass_toggled(lv_event_t* e);

    /// Callback for section row click — pushes detail overlay
    static void on_section_row_clicked(lv_event_t* e);

    //
    // === State ===
    //

    /// Alias for overlay_root_ to match existing pattern
    lv_obj_t*& overlay_ = overlay_root_;

    /// Container for section list rows
    lv_obj_t* section_list_container_ = nullptr;

    /// Subject for system info text (e.g. "System: AFC · v1.2.3")
    lv_subject_t system_info_subject_;

    /// Buffer for system info text
    char system_info_buf_[128] = {};

    /// Subject for status text display
    lv_subject_t status_subject_;

    /// Buffer for status text
    char status_buf_[128] = {};

    /// Subject for bypass support (0=not supported, 1=supported)
    lv_subject_t supports_bypass_subject_;

    /// Subject for bypass active state (0=inactive, 1=active)
    lv_subject_t bypass_active_subject_;

    /// Subject for hardware bypass sensor (0=virtual toggle, 1=hardware sensor)
    lv_subject_t hw_bypass_sensor_subject_;

    /// Subject for auto-heat support (0=not supported, 1=supported)
    lv_subject_t supports_auto_heat_subject_;

    /// Subject for backend presence (0=no backend, 1=has backend)
    lv_subject_t has_backend_subject_;

    /// Cached section metadata for row click dispatch
    std::vector<helix::printer::DeviceSection> cached_sections_;
};

/**
 * @brief Global instance accessor
 *
 * Creates the overlay on first access and registers it for cleanup
 * with StaticPanelRegistry.
 *
 * @return Reference to singleton AmsDeviceOperationsOverlay
 */
AmsDeviceOperationsOverlay& get_ams_device_operations_overlay();

} // namespace helix::ui
