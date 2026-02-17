// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file temperature_observer_bundle.h
 * @brief Bundle for managing common temperature subject observers (nozzle + bed)
 *
 * Encapsulates the repetitive pattern of subscribing to 4 temperature subjects
 * (active extruder temp/target, bed temp/target) that appears in 5+ panels.
 *
 * Reduces ~12-15 lines of boilerplate per panel to a single setup call.
 *
 * @pattern Observer Bundle - groups related observers for common use cases
 */

#pragma once

#include "ui_observer_guard.h"

#include "observer_factory.h"
#include "printer_state.h"

namespace helix::ui {

/**
 * @brief Bundle for temperature observers (nozzle + bed, current + target)
 *
 * Use when a panel needs to observe all 4 standard temperature subjects from PrinterState.
 * Supports two patterns:
 * 1. Sync observers with per-subject callbacks (UI thread only)
 * 2. Async observers for background thread updates with unified UI callback
 *
 * @tparam Panel The panel class type (must be pointer-safe)
 *
 * @code{.cpp}
 * // Pattern 1: Sync with individual handlers (simpler, UI thread only)
 * TemperatureObserverBundle<MyPanel> temp_observers_;
 *
 * temp_observers_.setup_sync(
 *     this,
 *     printer_state_,
 *     [](MyPanel* p, int v) { p->nozzle_current_ = v / 10; p->update_display(); },
 *     [](MyPanel* p, int v) { p->nozzle_target_ = v / 10; p->update_display(); },
 *     [](MyPanel* p, int v) { p->bed_current_ = v / 10; p->update_display(); },
 *     [](MyPanel* p, int v) { p->bed_target_ = v / 10; p->update_display(); }
 * );
 *
 * // Pattern 2: Async with caching + unified update (thread-safe)
 * temp_observers_.setup_async(
 *     this,
 *     printer_state_,
 *     [](MyPanel* p, int v) { p->cached_nozzle_ = v; },
 *     [](MyPanel* p, int v) { p->cached_nozzle_target_ = v; },
 *     [](MyPanel* p, int v) { p->cached_bed_ = v; },
 *     [](MyPanel* p, int v) { p->cached_bed_target_ = v; },
 *     [](MyPanel* p) { p->update_all_temp_displays(); }
 * );
 * @endcode
 */
template <typename Panel> class TemperatureObserverBundle {
  public:
    /**
     * @brief Setup synchronous temperature observers with individual callbacks
     *
     * Use when handlers run on UI thread and each temperature update needs
     * its own handler logic. Callbacks receive raw centidegree values.
     *
     * @param panel Panel instance (must outlive observers)
     * @param state PrinterState reference for temperature subjects
     * @param on_nozzle_temp Called when nozzle current temperature changes
     * @param on_nozzle_target Called when nozzle target temperature changes
     * @param on_bed_temp Called when bed current temperature changes
     * @param on_bed_target Called when bed target temperature changes
     */
    template <typename NozzleTempHandler, typename NozzleTargetHandler, typename BedTempHandler,
              typename BedTargetHandler>
    void setup_sync(Panel* panel, PrinterState& state, NozzleTempHandler&& on_nozzle_temp,
                    NozzleTargetHandler&& on_nozzle_target, BedTempHandler&& on_bed_temp,
                    BedTargetHandler&& on_bed_target) {
        clear();

        nozzle_temp_observer_ =
            observe_int_sync<Panel>(state.get_active_extruder_temp_subject(), panel,
                                    std::forward<NozzleTempHandler>(on_nozzle_temp));

        nozzle_target_observer_ =
            observe_int_sync<Panel>(state.get_active_extruder_target_subject(), panel,
                                    std::forward<NozzleTargetHandler>(on_nozzle_target));

        bed_temp_observer_ = observe_int_sync<Panel>(state.get_bed_temp_subject(), panel,
                                                     std::forward<BedTempHandler>(on_bed_temp));

        bed_target_observer_ = observe_int_sync<Panel>(
            state.get_bed_target_subject(), panel, std::forward<BedTargetHandler>(on_bed_target));
    }

    /**
     * @brief Setup async temperature observers with unified update callback
     *
     * Use when updates come from background threads and need thread-safe
     * caching followed by a single UI update. The value handlers cache
     * data directly, then update_handler is called via ui_async_call.
     *
     * @param panel Panel instance (must outlive observers)
     * @param state PrinterState reference for temperature subjects
     * @param cache_nozzle_temp Handler to cache nozzle temp (any thread)
     * @param cache_nozzle_target Handler to cache nozzle target (any thread)
     * @param cache_bed_temp Handler to cache bed temp (any thread)
     * @param cache_bed_target Handler to cache bed target (any thread)
     * @param update_handler Called on UI thread after any temp changes
     */
    template <typename CacheNozzleTemp, typename CacheNozzleTarget, typename CacheBedTemp,
              typename CacheBedTarget, typename UpdateHandler>
    void setup_async(Panel* panel, PrinterState& state, CacheNozzleTemp&& cache_nozzle_temp,
                     CacheNozzleTarget&& cache_nozzle_target, CacheBedTemp&& cache_bed_temp,
                     CacheBedTarget&& cache_bed_target, UpdateHandler&& update_handler) {
        clear();

        // Copy update_handler since it's used by all 4 observers
        // (forwarding an rvalue multiple times would move-from it)
        auto update_copy = update_handler;

        nozzle_temp_observer_ =
            observe_int_async<Panel>(state.get_active_extruder_temp_subject(), panel,
                                     std::forward<CacheNozzleTemp>(cache_nozzle_temp), update_copy);

        nozzle_target_observer_ = observe_int_async<Panel>(
            state.get_active_extruder_target_subject(), panel,
            std::forward<CacheNozzleTarget>(cache_nozzle_target), update_copy);

        bed_temp_observer_ =
            observe_int_async<Panel>(state.get_bed_temp_subject(), panel,
                                     std::forward<CacheBedTemp>(cache_bed_temp), update_copy);

        bed_target_observer_ = observe_int_async<Panel>(
            state.get_bed_target_subject(), panel, std::forward<CacheBedTarget>(cache_bed_target),
            std::move(update_copy));
    }

    /**
     * @brief Setup observers for a specific extruder (by Klipper name)
     *
     * Binds only nozzle temp/target observers to named extruder subjects.
     * Does not touch bed observers. Returns silently if subjects not found.
     *
     * @param panel Panel instance (must outlive observers)
     * @param state PrinterState reference for temperature subjects
     * @param extruder_name Klipper heater name (e.g. "extruder", "extruder1")
     * @param on_nozzle_temp Called when nozzle current temperature changes
     * @param on_nozzle_target Called when nozzle target temperature changes
     */
    template <typename NozzleTempHandler, typename NozzleTargetHandler>
    void setup_for_extruder(Panel* panel, PrinterState& state, const std::string& extruder_name,
                            NozzleTempHandler&& on_nozzle_temp,
                            NozzleTargetHandler&& on_nozzle_target) {
        clear(); // Release any existing observers before rebinding

        auto* temp_subj = state.get_extruder_temp_subject(extruder_name);
        auto* target_subj = state.get_extruder_target_subject(extruder_name);

        if (temp_subj) {
            nozzle_temp_observer_ = observe_int_sync<Panel>(
                temp_subj, panel, std::forward<NozzleTempHandler>(on_nozzle_temp));
        }
        if (target_subj) {
            nozzle_target_observer_ = observe_int_sync<Panel>(
                target_subj, panel, std::forward<NozzleTargetHandler>(on_nozzle_target));
        }
    }

    /**
     * @brief Clear all observers (automatic on destruction)
     *
     * Safe to call multiple times. Observers are released via RAII.
     */
    void clear() {
        nozzle_temp_observer_ = ObserverGuard();
        nozzle_target_observer_ = ObserverGuard();
        bed_temp_observer_ = ObserverGuard();
        bed_target_observer_ = ObserverGuard();
    }

    /**
     * @brief Check if bundle has active observers
     * @return true if any observer is set up
     */
    [[nodiscard]] bool is_active() const {
        return nozzle_temp_observer_ || nozzle_target_observer_ || bed_temp_observer_ ||
               bed_target_observer_;
    }

  private:
    ObserverGuard nozzle_temp_observer_;
    ObserverGuard nozzle_target_observer_;
    ObserverGuard bed_temp_observer_;
    ObserverGuard bed_target_observer_;
};

} // namespace helix::ui
