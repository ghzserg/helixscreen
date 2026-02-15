// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_spoolman_context_menu.h"
#include "ui_spoolman_edit_modal.h"

#include "overlay_base.h"
#include "spoolman_types.h" // For SpoolInfo
#include "subject_managed_panel.h"

#include <vector>

/**
 * @brief Panel display state for reactive visibility binding
 */
enum class SpoolmanPanelState : int32_t {
    LOADING = 0, ///< Showing loading spinner
    EMPTY = 1,   ///< Showing empty state (no spools)
    SPOOLS = 2   ///< Showing spool list
};

/**
 * @brief Spoolman filament inventory panel
 *
 * Displays all spools from Spoolman server with 3D visualization,
 * weight tracking, and low-stock warnings. Allows setting active spool
 * for filament usage tracking.
 *
 * Features:
 * - Scrollable list of spools with 3D canvas visualization
 * - Material, vendor, and weight display
 * - Low-stock warning (< 100g remaining)
 * - Click to set active spool
 * - Refresh button to reload from server
 *
 * Capability-gated: Only accessible when printer_has_spoolman=1
 */
class SpoolmanPanel : public OverlayBase {
  public:
    SpoolmanPanel();
    ~SpoolmanPanel() override;

    // === OverlayBase interface ===
    void init_subjects() override;
    void deinit_subjects();
    void register_callbacks() override;
    lv_obj_t* create(lv_obj_t* parent) override;
    const char* get_name() const override {
        return "Spoolman";
    }

    // === Lifecycle hooks ===
    void on_activate() override;
    void on_deactivate() override;

    // === Public API ===
    lv_obj_t* get_panel() const {
        return overlay_root_;
    }

    /**
     * @brief Refresh spool list from Spoolman server
     *
     * Fetches all spools via MoonrakerAPI and updates the UI.
     * Shows loading state during fetch, empty state if no spools.
     */
    void refresh_spools();

  private:
    // ========== UI Widget Pointers ==========
    lv_obj_t* spool_list_ = nullptr; // Still needed for populate_spool_list()

    // ========== Flags ==========
    bool callbacks_registered_ = false;

    // ========== State ==========
    std::vector<SpoolInfo> cached_spools_;
    int active_spool_id_ = -1;

    // ========== Subjects ==========
    SubjectManager subjects_;          ///< RAII subject manager
    lv_subject_t panel_state_subject_; ///< Panel display state (loading/empty/spools)
    lv_subject_t spool_count_subject_;
    char spool_count_buf_[32];

    // ========== Private Methods ==========
    void populate_spool_list();
    void update_row_visuals(lv_obj_t* row, const SpoolInfo& spool);
    void show_loading_state();
    void show_empty_state();
    void show_spool_list();
    void update_spool_count();

    void handle_spool_clicked(lv_obj_t* row);
    void handle_context_action(helix::ui::SpoolmanContextMenu::MenuAction action, int spool_id);
    void set_active_spool(int spool_id);
    void delete_spool(int spool_id);

    void show_edit_modal(int spool_id);

    // === Context Menu ===
    helix::ui::SpoolmanContextMenu context_menu_;
    helix::ui::SpoolEditModal edit_modal_;

    // ========== Static Event Callbacks ==========
    static void on_spool_row_clicked(lv_event_t* e);
    static void on_refresh_clicked(lv_event_t* e);
};

// ============================================================================
// Global Instance Accessors
// ============================================================================

/**
 * @brief Get global SpoolmanPanel instance
 * @return Reference to the singleton panel
 *
 * Creates the instance on first call. Used by static callbacks.
 */
SpoolmanPanel& get_global_spoolman_panel();
