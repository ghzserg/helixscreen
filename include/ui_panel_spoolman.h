// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_panel_base.h"

#include "advanced_panel_types.h" // For SpoolInfo

#include <vector>

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
class SpoolmanPanel : public PanelBase {
  public:
    SpoolmanPanel(PrinterState& printer_state, MoonrakerAPI* api);
    ~SpoolmanPanel() override = default;

    void init_subjects() override;
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;

    const char* get_name() const override {
        return "Spoolman";
    }
    const char* get_xml_component_name() const override {
        return "spoolman_panel";
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
    lv_obj_t* spool_list_ = nullptr;
    lv_obj_t* empty_state_ = nullptr;
    lv_obj_t* loading_state_ = nullptr;
    lv_obj_t* spool_count_text_ = nullptr;

    // ========== State ==========
    std::vector<SpoolInfo> cached_spools_;
    int active_spool_id_ = -1;

    // ========== Private Methods ==========
    void populate_spool_list();
    void update_row_visuals(lv_obj_t* row, const SpoolInfo& spool);
    void show_loading_state();
    void show_empty_state();
    void show_spool_list();
    void update_spool_count();

    void handle_spool_clicked(lv_obj_t* row);
    void set_active_spool(int spool_id);

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
 * @throws std::runtime_error if not initialized
 */
SpoolmanPanel& get_global_spoolman_panel();

/**
 * @brief Initialize global SpoolmanPanel instance
 * @param printer_state Reference to printer state
 * @param api Pointer to MoonrakerAPI
 *
 * Called by main.cpp during startup.
 */
void init_global_spoolman_panel(PrinterState& printer_state, MoonrakerAPI* api);
