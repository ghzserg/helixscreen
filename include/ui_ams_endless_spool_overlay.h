// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_ams_endless_spool_overlay.h
 * @brief AMS Endless Spool sub-panel overlay
 *
 * This overlay allows users to view and configure endless spool settings:
 * - View backup slot mappings for each slot
 * - Configure backup slots (if backend supports editing)
 *
 * Backend capabilities vary:
 * - AFC: Fully editable, per-slot backup configuration via SET_RUNOUT
 * - Happy Hare: Read-only, group-based (configured via mmu_vars.cfg)
 *
 * @pattern Overlay (lazy init, singleton)
 * @threading Main thread only
 */

#pragma once

#include "overlay_base.h"

#include <lvgl/lvgl.h>

#include <memory>
#include <vector>

namespace helix::ui {

/// Maximum number of slot rows to pre-allocate (most systems have 4-8 slots)
constexpr int MAX_ENDLESS_SPOOL_SLOTS = 8;

/**
 * @class AmsEndlessSpoolOverlay
 * @brief Overlay for viewing and configuring endless spool settings
 *
 * This overlay displays the backup slot configuration for each slot.
 * When a slot runs out of filament, the backup slot will be used automatically.
 *
 * ## Usage:
 *
 * @code
 * auto& overlay = helix::ui::get_ams_endless_spool_overlay();
 * if (!overlay.are_subjects_initialized()) {
 *     overlay.init_subjects();
 *     overlay.register_callbacks();
 * }
 * overlay.show(parent_screen);
 * @endcode
 */
class AmsEndlessSpoolOverlay : public OverlayBase {
  public:
    /**
     * @brief Default constructor
     */
    AmsEndlessSpoolOverlay();

    /**
     * @brief Destructor
     */
    ~AmsEndlessSpoolOverlay() override;

    // Non-copyable
    AmsEndlessSpoolOverlay(const AmsEndlessSpoolOverlay&) = delete;
    AmsEndlessSpoolOverlay& operator=(const AmsEndlessSpoolOverlay&) = delete;

    //
    // === OverlayBase Interface ===
    //

    /**
     * @brief Initialize subjects for reactive binding
     *
     * Registers subjects for:
     * - ams_endless_spool_supported: Whether endless spool is available (0/1)
     * - ams_endless_spool_editable: Whether config can be modified (0/1)
     * - ams_endless_spool_description: Description text for info card
     * - ams_endless_spool_editable_text: Hint text about edit mode
     */
    void init_subjects() override;

    /**
     * @brief Register event callbacks with lv_xml system
     *
     * Registers callbacks for backup slot dropdown changes.
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
     * @return "Endless Spool"
     */
    const char* get_name() const override {
        return "Endless Spool";
    }

    //
    // === Public API ===
    //

    /**
     * @brief Show the overlay
     *
     * This method:
     * 1. Ensures overlay is created (lazy init)
     * 2. Queries backend for endless spool capabilities
     * 3. Loads current configuration
     * 4. Updates UI with current backup mappings
     * 5. Pushes overlay onto navigation stack
     *
     * @param parent_screen The parent screen for overlay creation
     */
    void show(lv_obj_t* parent_screen);

    /**
     * @brief Refresh configuration from backend
     *
     * Re-loads current values from the backend and updates UI.
     */
    void refresh();

    /**
     * @brief Get the current slot count
     * @return Number of slots displayed in the overlay
     */
    [[nodiscard]] int get_slot_count() const;

  private:
    //
    // === Internal Methods ===
    //

    /**
     * @brief Create slot row UI for a given slot
     *
     * Creates a card with slot label and dropdown (if editable) or
     * read-only label showing the backup slot.
     *
     * @param parent Container to add row to
     * @param slot_index Slot index (0-based)
     * @param backup_slot Current backup slot (-1 = none)
     * @param total_slots Total number of slots in system
     * @param editable Whether backup can be changed
     * @return Created row widget
     */
    lv_obj_t* create_slot_row(lv_obj_t* parent, int slot_index, int backup_slot, int total_slots,
                              bool editable);

    /**
     * @brief Build dropdown options string for backup selection
     *
     * Creates options like "None\nSlot 0\nSlot 1\n..." excluding the source slot.
     *
     * @param slot_index Source slot to exclude from options
     * @param total_slots Total number of slots
     * @return Newline-separated options string
     */
    std::string build_dropdown_options(int slot_index, int total_slots);

    /**
     * @brief Convert backup slot to dropdown index
     *
     * Maps backup_slot to dropdown index, accounting for "None" option
     * and excluded source slot.
     *
     * @param backup_slot Backup slot (-1 = none)
     * @param slot_index Source slot (excluded from dropdown)
     * @param total_slots Total number of slots for bounds checking
     * @return Dropdown index
     */
    int backup_slot_to_dropdown_index(int backup_slot, int slot_index, int total_slots);

    /**
     * @brief Convert dropdown index to backup slot
     *
     * Maps dropdown selection back to actual slot index.
     *
     * @param dropdown_index Selected dropdown index
     * @param slot_index Source slot (excluded from dropdown)
     * @param total_slots Total number of slots for iteration bounds
     * @return Backup slot index (-1 = none)
     */
    int dropdown_index_to_backup_slot(int dropdown_index, int slot_index, int total_slots);

    /**
     * @brief Update all slot rows from current configuration
     */
    void update_slot_rows();

    /**
     * @brief Clear all dynamically created slot rows
     */
    void clear_slot_rows();

    //
    // === Static Callbacks ===
    //

    /**
     * @brief Callback for backup dropdown change
     *
     * Called when user changes the backup slot selection.
     * Saves setting via backend set_endless_spool_backup().
     */
    static void on_backup_changed(lv_event_t* e);

    //
    // === State ===
    //

    /// Alias for overlay_root_ to match existing pattern
    lv_obj_t*& overlay_ = overlay_root_;

    /// Container for slot rows
    lv_obj_t* slot_container_ = nullptr;

    /// Subject for supported flag (0=not supported, 1=supported)
    lv_subject_t supported_subject_;

    /// Subject for editable flag (0=read-only, 1=editable)
    lv_subject_t editable_subject_;

    /// Subject for description text
    lv_subject_t description_subject_;
    char description_buf_[128];

    /// Subject for editable hint text
    lv_subject_t editable_text_subject_;
    char editable_text_buf_[64];

    /// Cached total slot count for dropdown building
    int total_slots_ = 0;

    /// Track created dropdown widgets for event handling
    std::vector<lv_obj_t*> dropdown_widgets_;
};

/**
 * @brief Global instance accessor
 *
 * Creates the overlay on first access and registers it for cleanup
 * with StaticPanelRegistry.
 *
 * @return Reference to singleton AmsEndlessSpoolOverlay
 */
AmsEndlessSpoolOverlay& get_ams_endless_spool_overlay();

} // namespace helix::ui
