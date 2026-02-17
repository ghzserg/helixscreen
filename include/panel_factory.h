// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_nav_manager.h" // For UI_PANEL_COUNT

#include <array>

// Forward declarations
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace helix {

/**
 * @brief Factory for creating and wiring UI panels
 *
 * PanelFactory handles:
 * - Finding panels by name in the panel container
 * - Setting up panel observers and event handlers
 * - Creating overlay panels from XML
 * - Wiring panels together (e.g., print_select → print_status)
 *
 * Usage:
 *   PanelFactory factory;
 *   if (!factory.find_panels(panel_container)) { return error; }
 *   factory.setup_panels(screen);
 *   factory.create_overlays(screen);
 */
class PanelFactory {
  public:
    /// Panel names for lookup
    static constexpr const char* PANEL_NAMES[UI_PANEL_COUNT] = {
        "home_panel",     "print_select_panel", "controls_panel",
        "filament_panel", "settings_panel",     "advanced_panel"};

    /**
     * @brief Find all panels by name in the container
     * @param panel_container Container with panel children
     * @return true if all panels found, false if any missing
     */
    bool find_panels(lv_obj_t* panel_container);

    /**
     * @brief Set up all panel observers and event handlers
     * @param screen Root screen for overlays
     */
    void setup_panels(lv_obj_t* screen);

    /**
     * @brief Create print status overlay panel
     * @param screen Parent screen
     * @return true if created successfully
     */
    bool create_print_status_overlay(lv_obj_t* screen);

    /**
     * @brief Initialize numeric keypad modal
     * @param screen Parent screen
     */
    void init_keypad(lv_obj_t* screen);

    /**
     * @brief Get panel array for navigation system
     */
    lv_obj_t** panels() {
        return m_panels.data();
    }

    /**
     * @brief Get print status overlay panel
     */
    lv_obj_t* print_status_panel() const {
        return m_print_status_panel;
    }

    /**
     * @brief Create an overlay panel from XML
     * @param screen Parent screen
     * @param component_name XML component name
     * @param display_name Human-readable name for logging
     * @return Created object, or nullptr if failed
     */
    static lv_obj_t* create_overlay(lv_obj_t* screen, const char* component_name,
                                    const char* display_name);

  private:
    std::array<lv_obj_t*, UI_PANEL_COUNT> m_panels = {};
    lv_obj_t* m_print_status_panel = nullptr;
};

} // namespace helix
