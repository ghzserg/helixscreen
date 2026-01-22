// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "filament_database.h"

#include <lvgl.h>
#include <vector>

namespace helix::ui {

/**
 * @file ui_ams_dryer_card.h
 * @brief Dryer card and presets modal for AMS panel
 *
 * Manages the filament dryer card UI including:
 * - Progress bar visualization
 * - Presets modal (PLA, PETG, ABS temperatures)
 * - Start/stop controls
 *
 * State is managed via AmsState subjects for reactive UI updates.
 *
 * ## Usage:
 * @code
 * helix::ui::AmsDryerCard dryer;
 * dryer.setup(panel);  // panel contains dryer_card widget
 * @endcode
 */
class AmsDryerCard {
  public:
    AmsDryerCard();
    ~AmsDryerCard();

    // Non-copyable
    AmsDryerCard(const AmsDryerCard&) = delete;
    AmsDryerCard& operator=(const AmsDryerCard&) = delete;

    // Movable
    AmsDryerCard(AmsDryerCard&& other) noexcept;
    AmsDryerCard& operator=(AmsDryerCard&& other) noexcept;

    /**
     * @brief Set up dryer card within panel
     * @param panel Panel containing dryer_card widget
     * @return true if dryer card was found and set up
     *
     * Finds dryer_card widget, sets up progress observer,
     * and creates presets modal on top layer.
     */
    bool setup(lv_obj_t* panel);

    /**
     * @brief Clean up dryer card resources
     *
     * Removes observers and deletes modal. Call before panel destruction.
     */
    void cleanup();

    /**
     * @brief Check if dryer card is set up
     */
    [[nodiscard]] bool is_setup() const {
        return dryer_card_ != nullptr;
    }

    // === Actions ===

    /**
     * @brief Start drying with specified parameters
     * @param temp_c Target temperature in Celsius
     * @param duration_min Duration in minutes
     * @param fan_pct Fan speed percentage (0-100)
     */
    void start_drying(float temp_c, int duration_min, int fan_pct);

    /**
     * @brief Stop drying
     */
    void stop_drying();

    /**
     * @brief Apply preset and optionally start if already running
     * @param temp_c Preset temperature
     * @param duration_min Preset duration
     */
    void apply_preset(int temp_c, int duration_min);

    /**
     * @brief Register XML callbacks statically (before XML parsing)
     *
     * Call this before registering ams_dryer_card.xml to ensure
     * callbacks exist when the XML parser encounters <event_cb> elements.
     */
    static void register_callbacks_static();

  private:
    // === Widget References ===
    lv_obj_t* dryer_card_ = nullptr;
    lv_obj_t* dryer_modal_ = nullptr;
    lv_obj_t* progress_fill_ = nullptr;

    // === Observers ===
    ObserverGuard progress_observer_;

    // === Static Callback Registration ===
    static void register_callbacks();
    static bool callbacks_registered_;

    // === Static Callbacks ===
    static void on_open_modal_cb(lv_event_t* e);
    static void on_close_modal_cb(lv_event_t* e);
    static void on_preset_changed_cb(lv_event_t* e);
    static void on_stop_cb(lv_event_t* e);
    static void on_temp_minus_cb(lv_event_t* e);
    static void on_temp_plus_cb(lv_event_t* e);
    static void on_duration_minus_cb(lv_event_t* e);
    static void on_duration_plus_cb(lv_event_t* e);
    static void on_power_toggled_cb(lv_event_t* e);

    /**
     * @brief Find dryer card instance from event target
     */
    static AmsDryerCard* get_instance_from_event(lv_event_t* e);

    /**
     * @brief Populate preset dropdown with data from filament database
     */
    void populate_preset_dropdown();

    // === Cached Presets ===
    std::vector<filament::DryingPreset> cached_presets_;
};

} // namespace helix::ui
