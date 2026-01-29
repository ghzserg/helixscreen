// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2024-2025 Brad Barnett, 2025 Peter Welch Brown

/**
 * @file ui_lazy_panel_helper.h
 * @brief Template helper for lazy panel creation and navigation
 *
 * Reduces boilerplate code for lazy-initialized overlay panels that follow
 * the common pattern:
 * 1. Check if cached panel is null
 * 2. Get global panel instance
 * 3. Initialize subjects if needed
 * 4. Register callbacks
 * 5. Create panel from XML
 * 6. Register with NavigationManager
 * 7. Push overlay
 *
 * @see AdvancedPanel for usage example
 */

#pragma once

#include "ui_nav_manager.h"
#include "ui_toast.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

/**
 * @brief Lazy-create and push an overlay panel
 *
 * This template helper encapsulates the common pattern for lazy panel
 * initialization. It handles the full lifecycle:
 * - First access: initializes, creates, and registers the panel
 * - Subsequent access: reuses the cached panel
 * - Always pushes the overlay for navigation
 *
 * @tparam PanelType The panel class type (must have are_subjects_initialized(),
 *                   init_subjects(), register_callbacks(), create(), get_name())
 * @tparam Getter Callable that returns PanelType& (e.g., get_global_spoolman_panel)
 *
 * @param getter Function that returns the global panel instance reference
 * @param cached_panel Reference to the cached lv_obj_t* pointer
 * @param parent_screen Parent screen for overlay creation
 * @param panel_display_name Human-readable name for error messages
 * @param caller_name Name of the calling panel (for logging)
 *
 * @return true if overlay was pushed, false on failure
 *
 * Example:
 * @code
 * void AdvancedPanel::handle_spoolman_clicked() {
 *     lazy_create_and_push_overlay(
 *         get_global_spoolman_panel,
 *         spoolman_panel_,
 *         parent_screen_,
 *         "Spoolman",
 *         get_name()
 *     );
 * }
 * @endcode
 */
template <typename PanelType, typename Getter>
bool lazy_create_and_push_overlay(Getter getter, lv_obj_t*& cached_panel, lv_obj_t* parent_screen,
                                  const char* panel_display_name, const char* caller_name) {
    spdlog::debug("[{}] {} clicked - opening panel", caller_name, panel_display_name);

    // Create panel on first access (lazy initialization)
    if (!cached_panel && parent_screen) {
        PanelType& panel = getter();

        // Initialize subjects and callbacks if not already done
        if (!panel.are_subjects_initialized()) {
            panel.init_subjects();
        }
        panel.register_callbacks();

        // Create overlay UI
        cached_panel = panel.create(parent_screen);
        if (!cached_panel) {
            spdlog::error("[{}] Failed to create {} panel from XML", caller_name,
                          panel_display_name);
            ui_toast_show(ToastSeverity::ERROR,
                          (std::string("Failed to open ") + panel_display_name).c_str(), 2000);
            return false;
        }

        // Register with NavigationManager for lifecycle callbacks
        NavigationManager::instance().register_overlay_instance(cached_panel, &panel);
        spdlog::info("[{}] {} panel created", caller_name, panel_display_name);
    }

    // Push panel onto navigation history and show it
    if (cached_panel) {
        ui_nav_push_overlay(cached_panel);
        return true;
    }

    return false;
}

/**
 * @brief Simple lazy overlay creation and push
 *
 * A simpler version of lazy_create_and_push_overlay for overlays that don't
 * follow the full global-panel pattern. Use this when you have a custom
 * creation function that returns an lv_obj_t*.
 *
 * Pattern it replaces:
 * @code
 * if (!overlay_cache_) {
 *     overlay_cache_ = create_overlay(parent);
 *     if (!overlay_cache_) {
 *         spdlog::error("[Panel] Failed to create overlay");
 *         return;
 *     }
 * }
 * ui_nav_push_overlay(overlay_cache_);
 * @endcode
 *
 * @tparam CreateFunc Callable that takes (lv_obj_t* parent) and returns lv_obj_t*
 *
 * @param cache Reference to the cached lv_obj_t* pointer
 * @param create_func Function that creates the overlay, returns nullptr on failure
 * @param parent Parent screen for overlay creation
 * @param error_msg Error message for toast notification (default: "Failed to create overlay")
 *
 * @return true if overlay was pushed, false on failure
 *
 * Example:
 * @code
 * void MyPanel::handle_settings_clicked() {
 *     lazy_push_overlay(settings_overlay_, [this](lv_obj_t* p) {
 *         auto* overlay = static_cast<lv_obj_t*>(lv_xml_create(p, "settings_panel", nullptr));
 *         if (overlay) setup_settings(overlay);
 *         return overlay;
 *     }, parent_screen_, "Failed to load settings");
 * }
 * @endcode
 */
template <typename CreateFunc>
bool lazy_push_overlay(lv_obj_t*& cache, CreateFunc create_func, lv_obj_t* parent,
                       const char* error_msg = "Failed to create overlay") {
    if (!cache && parent) {
        cache = create_func(parent);
        if (!cache) {
            spdlog::error("{}", error_msg);
            ui_toast_show(ToastSeverity::ERROR, error_msg, 2000);
            return false;
        }
    }

    if (cache) {
        ui_nav_push_overlay(cache);
        return true;
    }

    return false;
}

} // namespace helix::ui
