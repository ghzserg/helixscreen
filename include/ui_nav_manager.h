// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "lvgl/lvgl.h"

#include <functional>
#include <unordered_map>
#include <vector>

/// Callback type for overlay close notifications
using OverlayCloseCallback = std::function<void()>;

/**
 * @brief Navigation panel identifiers
 *
 * Order matches app_layout.xml panel children for index-based access.
 */
typedef enum {
    UI_PANEL_HOME,         ///< Panel 0: Home
    UI_PANEL_PRINT_SELECT, ///< Panel 1: Print Select (beneath Home)
    UI_PANEL_CONTROLS,     ///< Panel 2: Controls
    UI_PANEL_FILAMENT,     ///< Panel 3: Filament
    UI_PANEL_SETTINGS,     ///< Panel 4: Settings
    UI_PANEL_ADVANCED,     ///< Panel 5: Advanced
    UI_PANEL_COUNT         ///< Total number of panels
} ui_panel_id_t;

/**
 * @brief Singleton manager for navigation and panel management
 *
 * Manages the navigation system including:
 * - Panel switching via navbar buttons
 * - Overlay panel stack with slide animations
 * - Backdrop visibility for modal dimming
 * - Connection gating (redirect to home when disconnected)
 *
 * Uses RAII observer guards for automatic cleanup and LVGL subjects
 * for reactive XML bindings.
 *
 * Usage:
 *   NavigationManager::instance().init();  // Before XML creation
 *   // Create XML...
 *   NavigationManager::instance().wire_events(navbar);
 *   NavigationManager::instance().set_panels(panel_widgets);
 */
class NavigationManager {
  public:
    /**
     * @brief Get singleton instance
     * @return Reference to the NavigationManager singleton
     */
    static NavigationManager& instance();

    // Non-copyable, non-movable (singleton)
    NavigationManager(const NavigationManager&) = delete;
    NavigationManager& operator=(const NavigationManager&) = delete;
    NavigationManager(NavigationManager&&) = delete;
    NavigationManager& operator=(NavigationManager&&) = delete;

    /**
     * @brief Initialize navigation system with reactive subjects
     *
     * Sets up reactive subjects for icon colors and panel visibility.
     * MUST be called BEFORE creating navigation bar XML.
     */
    void init();

    /**
     * @brief Initialize overlay backdrop widget
     *
     * Creates a shared backdrop widget used by all overlay panels.
     * Should be called after screen is available.
     *
     * @param screen Screen to add backdrop to
     */
    void init_overlay_backdrop(lv_obj_t* screen);

    /**
     * @brief Set app_layout widget reference
     *
     * Stores reference to prevent hiding app_layout when dismissing
     * overlay panels.
     *
     * @param app_layout Main application layout widget
     */
    void set_app_layout(lv_obj_t* app_layout);

    /**
     * @brief Wire up event handlers to navigation bar widget
     *
     * Attaches click handlers to navbar icons for panel switching.
     * Call this after creating navigation_bar component from XML.
     *
     * @param navbar Navigation bar widget created from XML
     */
    void wire_events(lv_obj_t* navbar);

    /**
     * @brief Wire up status icons in navbar
     *
     * Applies responsive scaling and theming to status icons.
     *
     * @param navbar Navigation bar widget containing status icons
     */
    void wire_status_icons(lv_obj_t* navbar);

    /**
     * @brief Set active panel
     *
     * Updates active panel state and triggers reactive icon color updates.
     *
     * @param panel_id Panel identifier to activate
     */
    void set_active(ui_panel_id_t panel_id);

    /**
     * @brief Get current active panel
     * @return Currently active panel identifier
     */
    ui_panel_id_t get_active() const;

    /**
     * @brief Register panel widgets for show/hide management
     *
     * @param panels Array of panel widgets (size: UI_PANEL_COUNT)
     */
    void set_panels(lv_obj_t** panels);

    /**
     * @brief Push overlay panel onto navigation history stack
     *
     * Shows the overlay panel and pushes it onto history stack.
     *
     * @param overlay_panel Overlay panel widget to show
     */
    void push_overlay(lv_obj_t* overlay_panel);

    /**
     * @brief Register a callback to be called when an overlay is closed
     *
     * The callback is invoked when the overlay is popped from the stack
     * (via go_back or backdrop click). Useful for cleanup like freeing memory.
     *
     * @param overlay_panel The overlay panel to monitor
     * @param callback Function to call when the overlay closes
     */
    void register_overlay_close_callback(lv_obj_t* overlay_panel, OverlayCloseCallback callback);

    /**
     * @brief Remove a registered close callback for an overlay
     *
     * @param overlay_panel The overlay panel to stop monitoring
     */
    void unregister_overlay_close_callback(lv_obj_t* overlay_panel);

    /**
     * @brief Navigate back to previous panel
     *
     * @return true if navigation occurred, false if history empty
     */
    bool go_back();

    /**
     * @brief Check if a panel is in the overlay stack
     *
     * Used to determine if a specific panel (like PrintStatusPanel) is currently
     * visible as an overlay.
     *
     * @param panel Panel widget to check for
     * @return true if panel is in the overlay stack
     */
    bool is_panel_in_stack(lv_obj_t* panel) const;

  private:
    // Private constructor for singleton
    NavigationManager() = default;
    ~NavigationManager() = default;

    // Panel ID to name mapping for E-Stop visibility
    static const char* panel_id_to_name(ui_panel_id_t id);

    // Check if panel requires Moonraker connection
    static bool panel_requires_connection(ui_panel_id_t panel);

    // Check if printer is connected
    bool is_printer_connected() const;

    // Clear overlay stack (used during connection loss)
    void clear_overlay_stack();

    // Animation helpers
    void overlay_animate_slide_in(lv_obj_t* panel);
    void overlay_animate_slide_out(lv_obj_t* panel);
    static void overlay_slide_out_complete_cb(lv_anim_t* anim);

    // Observer callbacks
    static void active_panel_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void connection_state_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    // Event callbacks
    static void backdrop_click_event_cb(lv_event_t* e);
    static void nav_button_clicked_cb(lv_event_t* event);

    // Active panel tracking
    lv_subject_t active_panel_subject_{};
    ui_panel_id_t active_panel_ = UI_PANEL_HOME;

    // Panel widget tracking for show/hide
    lv_obj_t* panel_widgets_[UI_PANEL_COUNT] = {nullptr};

    // App layout widget reference
    lv_obj_t* app_layout_widget_ = nullptr;

    // Panel stack: tracks ALL visible panels in z-order
    std::vector<lv_obj_t*> panel_stack_;

    // Overlay close callbacks (called when overlay is popped from stack)
    std::unordered_map<lv_obj_t*, OverlayCloseCallback> overlay_close_callbacks_;

    // Shared overlay backdrop widget
    lv_obj_t* overlay_backdrop_ = nullptr;

    // Navbar widget reference (for z-order management)
    lv_obj_t* navbar_widget_ = nullptr;

    // RAII observer guards
    ObserverGuard active_panel_observer_;
    ObserverGuard connection_state_observer_;

    // Track previous connection state for detecting disconnection
    int previous_connection_state_ = -1;

    // Animation constants
    static constexpr uint32_t OVERLAY_ANIM_DURATION_MS = 200;
    static constexpr int32_t OVERLAY_SLIDE_OFFSET = 400;

    bool subjects_initialized_ = false;
};

// ============================================================================
// LEGACY API (forwards to NavigationManager for backward compatibility)
// ============================================================================

/**
 * @brief Initialize navigation system
 * @deprecated Use NavigationManager::instance().init() instead
 */
void ui_nav_init();

/**
 * @brief Initialize overlay backdrop
 * @deprecated Use NavigationManager::instance().init_overlay_backdrop() instead
 */
void ui_nav_init_overlay_backdrop(lv_obj_t* screen);

/**
 * @brief Set app_layout widget
 * @deprecated Use NavigationManager::instance().set_app_layout() instead
 */
void ui_nav_set_app_layout(lv_obj_t* app_layout);

/**
 * @brief Wire event handlers
 * @deprecated Use NavigationManager::instance().wire_events() instead
 */
void ui_nav_wire_events(lv_obj_t* navbar);

/**
 * @brief Wire status icons
 * @deprecated Use NavigationManager::instance().wire_status_icons() instead
 */
void ui_nav_wire_status_icons(lv_obj_t* navbar);

/**
 * @brief Set active panel
 * @deprecated Use NavigationManager::instance().set_active() instead
 */
void ui_nav_set_active(ui_panel_id_t panel_id);

/**
 * @brief Get active panel
 * @deprecated Use NavigationManager::instance().get_active() instead
 */
ui_panel_id_t ui_nav_get_active();

/**
 * @brief Register panel widgets
 * @deprecated Use NavigationManager::instance().set_panels() instead
 */
void ui_nav_set_panels(lv_obj_t** panels);

/**
 * @brief Push overlay panel
 * @deprecated Use NavigationManager::instance().push_overlay() instead
 */
void ui_nav_push_overlay(lv_obj_t* overlay_panel);

/**
 * @brief Navigate back
 * @deprecated Use NavigationManager::instance().go_back() instead
 */
bool ui_nav_go_back();
