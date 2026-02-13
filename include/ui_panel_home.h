// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_heating_animator.h"
#include "ui_observer_guard.h"
#include "ui_panel_base.h"
#include "ui_panel_print_status.h" // For RunoutGuidanceModal

#include "led/led_controller.h"
#include "subject_managed_panel.h"
#include "tips_manager.h"

#include <memory>
#include <vector>

// Forward declarations
class WiFiManager;
class EthernetManager;
class TempControlPanel;
enum class PrintJobState : int;

/**
 * @brief Home panel - Main dashboard showing printer status and quick actions
 *
 * Displays printer image, temperature, network status, light toggle, and
 * tip of the day with auto-rotation. Responsive sizing based on screen dimensions.
 *
 * @see TipsManager for tip of the day functionality
 */

// Network connection types
typedef enum { NETWORK_WIFI, NETWORK_ETHERNET, NETWORK_DISCONNECTED } network_type_t;

class HomePanel : public PanelBase {
  public:
    /**
     * @brief Construct HomePanel with injected dependencies
     * @param printer_state Reference to PrinterState
     * @param api Pointer to MoonrakerAPI (for light control)
     */
    HomePanel(PrinterState& printer_state, MoonrakerAPI* api);
    ~HomePanel() override;

    void init_subjects() override;
    void deinit_subjects();
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;
    void on_activate() override;
    void on_deactivate() override;
    const char* get_name() const override {
        return "Home Panel";
    }
    const char* get_xml_component_name() const override {
        return "home_panel";
    }

    /**
     * @brief Update status text and temperature display
     * @param status_text New status/tip text (nullptr to keep current)
     * @param temp Temperature in degrees Celsius
     */
    void update(const char* status_text, int temp);

    /** @brief Set network status display */
    void set_network(network_type_t type);

    /** @brief Set light state (on=gold, off=grey) */
    void set_light(bool is_on);

    bool get_light_state() const {
        return light_on_;
    }

    /**
     * @brief Reload printer image and LED visibility from config
     *
     * Called after wizard completion to update the home panel with
     * newly configured printer type and LED settings.
     */
    void reload_from_config();

    /// Re-check printer image setting and update the home panel image widget
    void refresh_printer_image();

    /**
     * @brief Trigger a deferred runout check (used after wizard completes)
     *
     * Resets the shown flag and re-checks runout condition.
     * This allows the modal to show after wizard if conditions are met.
     */
    void trigger_idle_runout_check();

    /**
     * @brief Set reference to TempControlPanel for temperature overlay
     *
     * Must be called before temp icon click handler can work.
     * @param temp_panel Pointer to TempControlPanel instance
     */
    void set_temp_control_panel(TempControlPanel* temp_panel);

  private:
    SubjectManager subjects_;
    TempControlPanel* temp_control_panel_ = nullptr;
    lv_subject_t status_subject_;
    lv_subject_t temp_subject_;
    lv_subject_t network_icon_state_; // Integer subject: 0-5 for conditional icon visibility
    lv_subject_t network_label_subject_;
    lv_subject_t printer_type_subject_;
    lv_subject_t printer_host_subject_;
    lv_subject_t printer_info_visible_;

    char status_buffer_[512];
    char temp_buffer_[32];
    char network_label_buffer_[32];
    char printer_type_buffer_[64];
    char printer_host_buffer_[64];

    bool light_on_ = false;
    network_type_t current_network_ = NETWORK_WIFI;
    PrintingTip current_tip_;
    PrintingTip pending_tip_; // Tip waiting to be displayed after fade-out
    // configured_leds_ removed - read LedController::selected_strips() lazily
    lv_timer_t* tip_rotation_timer_ = nullptr;
    lv_obj_t* tip_label_ = nullptr;                     // Cached for fade animation
    bool tip_animating_ = false;                        // Prevents overlapping animations
    lv_timer_t* signal_poll_timer_ = nullptr;           // Polls WiFi signal strength every 5s
    std::shared_ptr<WiFiManager> wifi_manager_;         // For signal strength queries
    std::unique_ptr<EthernetManager> ethernet_manager_; // For Ethernet status queries

    // Light icon for dynamic brightness/color updates
    lv_obj_t* light_icon_ = nullptr;

    // Lazily-created overlay panels (owned by LVGL parent, not us)
    lv_obj_t* nozzle_temp_panel_ = nullptr;
    lv_obj_t* led_control_panel_ = nullptr;

    void update_tip_of_day();
    void start_tip_fade_transition(const PrintingTip& new_tip);
    void apply_pending_tip();         // Called when fade-out completes
    void detect_network_type();       // Detects WiFi vs Ethernet vs disconnected
    int compute_network_icon_state(); // Maps network type + signal → 0-5
    void update_network_icon_state(); // Updates the subject
    static void signal_poll_timer_cb(lv_timer_t* timer);

    void handle_light_toggle();
    void handle_light_long_press();
    void flash_light_icon();
    void ensure_led_observers();
    void handle_print_card_clicked();
    void handle_tip_text_clicked();
    void handle_tip_rotation_timer();
    void handle_temp_clicked();
    void handle_printer_status_clicked();
    void handle_network_clicked();
    void handle_printer_manager_clicked();
    void handle_ams_clicked();
    void on_extruder_temp_changed(int temp);
    void on_extruder_target_changed(int target);
    void on_led_state_changed(int state);
    void update_temp_icon_animation();
    void update_light_icon();

    static void light_toggle_cb(lv_event_t* e);
    static void light_long_press_cb(lv_event_t* e);
    static void print_card_clicked_cb(lv_event_t* e);
    static void tip_text_clicked_cb(lv_event_t* e);
    static void temp_clicked_cb(lv_event_t* e);
    static void printer_status_clicked_cb(lv_event_t* e);
    static void network_clicked_cb(lv_event_t* e);
    static void printer_manager_clicked_cb(lv_event_t* e);
    static void ams_clicked_cb(lv_event_t* e);
    static void tip_rotation_timer_cb(lv_timer_t* timer);

    ObserverGuard extruder_temp_observer_;
    ObserverGuard extruder_target_observer_;
    ObserverGuard led_state_observer_;
    ObserverGuard led_brightness_observer_;
    ObserverGuard ams_slot_count_observer_;
    ObserverGuard ams_bypass_observer_;
    ObserverGuard filament_sensor_count_observer_;

    // Computed subject: show filament status when sensors exist AND (no AMS OR bypass active)
    lv_subject_t show_filament_status_;

    // Print card observers (for showing progress during active print)
    ObserverGuard print_state_observer_;
    ObserverGuard print_progress_observer_;
    ObserverGuard print_time_left_observer_;
    ObserverGuard print_thumbnail_path_observer_; // Observes shared thumbnail from PrintStatusPanel

    // Filament runout observer and modal (shows when idle + runout detected)
    ObserverGuard filament_runout_observer_;
    RunoutGuidanceModal runout_modal_;
    bool runout_modal_shown_ = false; // Prevent repeated modals

    // Print card widgets (looked up after XML creation)
    lv_obj_t* print_card_thumb_ = nullptr;        // Idle state thumbnail
    lv_obj_t* print_card_active_thumb_ = nullptr; // Active print thumbnail
    lv_obj_t* print_card_label_ = nullptr;

    // Heating icon animator (gradient color + pulse while heating)
    HeatingIconAnimator temp_icon_animator_;
    int cached_extruder_temp_ = 25;
    int cached_extruder_target_ = 0;

    void update_ams_indicator(int slot_count);
    void update_filament_status_visibility();

    // Print card update methods
    void on_print_state_changed(PrintJobState state);
    void on_print_progress_or_time_changed();
    void on_print_thumbnail_path_changed(const char* path);
    void update_print_card_from_state();
    void update_print_card_label(int progress, int time_left_secs);
    void reset_print_card_to_idle();

    // Filament runout handling
    void check_and_show_idle_runout_modal();
    void show_idle_runout_modal();
};

// Global instance accessor (needed by main.cpp)
HomePanel& get_global_home_panel();
