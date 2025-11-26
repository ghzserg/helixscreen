// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_panel_base.h"

#include "tips_manager.h"

/**
 * @file ui_panel_home.h
 * @brief Home panel - Main dashboard showing printer status and quick actions
 *
 * The home panel displays:
 * - Printer image and status
 * - Temperature display
 * - Network status (WiFi/Ethernet/Disconnected)
 * - Light toggle control
 * - Tip of the day with auto-rotation
 * - Quick access to print selection
 *
 * ## Reactive Subjects:
 * - `status_text` - Tip of the day title
 * - `temp_text` - Temperature display (e.g., "30 °C")
 * - `network_icon` - Network status icon (WiFi/Ethernet/slash)
 * - `network_label` - Network status text
 * - `network_color` - Network icon color (unused, theme handles)
 * - `light_icon_color` - Light bulb icon recolor
 *
 * ## Key Features:
 * - Tip rotation timer (60s auto-change)
 * - Modal dialog for full tip content
 * - Responsive sizing based on screen dimensions
 * - Theme-aware light icon colors
 *
 * ## Migration Notes:
 * Phase 4 panel - demonstrates timer lifecycle, observer RAII, and
 * lambda-to-trampoline conversion for dialog callbacks.
 *
 * @see PanelBase for base class documentation
 * @see TipsManager for tip of the day functionality
 */

// Network connection types
typedef enum { NETWORK_WIFI, NETWORK_ETHERNET, NETWORK_DISCONNECTED } network_type_t;

class HomePanel : public PanelBase {
  public:
    /**
     * @brief Construct HomePanel with injected dependencies
     *
     * @param printer_state Reference to PrinterState
     * @param api Pointer to MoonrakerAPI (for future light control)
     */
    HomePanel(PrinterState& printer_state, MoonrakerAPI* api);

    /**
     * @brief Destructor - cleans up timer and dialog
     */
    ~HomePanel() override;

    //
    // === PanelBase Implementation ===
    //

    /**
     * @brief Initialize home panel subjects for XML binding
     *
     * Registers: status_text, temp_text, network_icon, network_label,
     *            network_color, light_icon_color
     * Also registers XML event callbacks.
     */
    void init_subjects() override;

    /**
     * @brief Setup observers, timer, and responsive sizing
     *
     * - Finds named widgets for direct updates
     * - Sets up subject observers with RAII cleanup
     * - Creates tip rotation timer
     * - Applies responsive sizing based on screen dimensions
     *
     * @param panel Root panel object from lv_xml_create()
     * @param parent_screen Parent screen for navigation
     */
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;

    const char* get_name() const override { return "Home Panel"; }
    const char* get_xml_component_name() const override { return "home_panel"; }

    //
    // === Public API ===
    //

    /**
     * @brief Update status text and temperature display
     *
     * @param status_text New status/tip text (nullptr to keep current)
     * @param temp Temperature in degrees Celsius
     */
    void update(const char* status_text, int temp);

    /**
     * @brief Set network status display
     *
     * @param type Network type (WiFi, Ethernet, or Disconnected)
     */
    void set_network(network_type_t type);

    /**
     * @brief Set light state
     *
     * @param is_on true for on (gold color), false for off (grey)
     */
    void set_light(bool is_on);

    /**
     * @brief Get current light state
     * @return true if light is on
     */
    bool get_light_state() const { return light_on_; }

  private:
    //
    // === Subjects (owned by this panel) ===
    //

    lv_subject_t status_subject_;
    lv_subject_t temp_subject_;
    lv_subject_t network_icon_subject_;
    lv_subject_t network_label_subject_;
    lv_subject_t network_color_subject_;
    lv_subject_t light_icon_color_subject_;

    // Subject storage buffers
    char status_buffer_[512];  // Large for tip title + content
    char temp_buffer_[32];
    char network_icon_buffer_[8];
    char network_label_buffer_[32];
    char network_color_buffer_[16];

    //
    // === Instance State ===
    //

    bool light_on_ = false;
    network_type_t current_network_ = NETWORK_WIFI;
    PrintingTip current_tip_;
    lv_obj_t* tip_detail_dialog_ = nullptr;

    // Timer for tip rotation
    lv_timer_t* tip_rotation_timer_ = nullptr;

    // Theme-aware colors (cached)
    lv_color_t light_icon_on_color_;
    lv_color_t light_icon_off_color_;

    // Cached widget references (for direct updates)
    lv_obj_t* network_icon_label_ = nullptr;
    lv_obj_t* network_text_label_ = nullptr;
    lv_obj_t* light_icon_label_ = nullptr;

    //
    // === Private Helpers ===
    //

    void init_home_panel_colors();
    void update_tip_of_day();
    void setup_responsive_sizing();
    void close_tip_dialog();

    //
    // === Instance Handlers ===
    //

    void handle_light_toggle();
    void handle_print_card_clicked();
    void handle_tip_text_clicked();
    void handle_tip_dialog_close();
    void handle_tip_rotation_timer();

    // Observer instance methods
    void on_network_changed();
    void on_light_color_changed(lv_subject_t* subject);

    //
    // === Static Trampolines ===
    //

    static void light_toggle_cb(lv_event_t* e);
    static void print_card_clicked_cb(lv_event_t* e);
    static void tip_text_clicked_cb(lv_event_t* e);
    static void tip_dialog_close_cb(lv_event_t* e);
    static void tip_rotation_timer_cb(lv_timer_t* timer);
    static void network_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void light_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
};

// Global instance accessor (needed by main.cpp)
HomePanel& get_global_home_panel();
