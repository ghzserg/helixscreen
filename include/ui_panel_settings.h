// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_panel_base.h"

#include "calibration_types.h" // For MachineLimits

/**
 * @file ui_panel_settings.h
 * @brief Settings panel - Scrolling list of app and printer settings
 *
 * A comprehensive settings panel with sections for Appearance, Printer,
 * Notifications, System, and About information.
 *
 * ## Key Features:
 * - Dark mode toggle with immediate theme switching
 * - Display sleep timeout configuration
 * - LED light control (via Moonraker)
 * - Sound and notification settings (placeholder)
 * - System info display (version, printer, Klipper)
 *
 * ## Architecture:
 * Uses SettingsManager for reactive data binding and persistence.
 * Toggle switches automatically sync with SettingsManager subjects.
 *
 * @see SettingsManager for data layer
 * @see PanelBase for base class documentation
 */
class SettingsPanel : public PanelBase {
  public:
    /**
     * @brief Construct SettingsPanel with injected dependencies
     *
     * @param printer_state Reference to PrinterState
     * @param api Pointer to MoonrakerAPI
     */
    SettingsPanel(PrinterState& printer_state, MoonrakerAPI* api);

    ~SettingsPanel() override;

    //
    // === PanelBase Implementation ===
    //

    /**
     * @brief Initialize SettingsManager subjects
     *
     * Must be called BEFORE XML creation to enable data binding.
     */
    void init_subjects() override;

    /**
     * @brief Deinitialize subjects for clean shutdown
     *
     * Calls lv_subject_deinit() on all local lv_subject_t members.
     * Must be called before lv_deinit() to prevent dangling observers.
     * Follows [L041] pattern for subject init/deinit symmetry.
     */
    void deinit_subjects();

    /**
     * @brief Setup the settings panel with event handlers and bindings
     *
     * Wires up toggle switches, dropdown, and action row click handlers.
     *
     * @param panel Root panel object from lv_xml_create()
     * @param parent_screen Parent screen (for overlay panel creation)
     */
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;

    const char* get_name() const override {
        return "Settings Panel";
    }
    const char* get_xml_component_name() const override {
        return "settings_panel";
    }

  private:
    //
    // === Widget References ===
    //

    // Toggle switches
    lv_obj_t* dark_mode_switch_ = nullptr;
    lv_obj_t* animations_switch_ = nullptr;
    lv_obj_t* gcode_3d_switch_ = nullptr;
    lv_obj_t* led_light_switch_ = nullptr;
    lv_obj_t* sounds_switch_ = nullptr;
    lv_obj_t* estop_confirm_switch_ = nullptr;

    // Dropdowns
    lv_obj_t* completion_alert_dropdown_ = nullptr;
    lv_obj_t* display_sleep_dropdown_ = nullptr;

    // Restart prompt dialog
    lv_obj_t* restart_prompt_dialog_ = nullptr;

    // Action rows (clickable)
    lv_obj_t* display_settings_row_ = nullptr;
    lv_obj_t* filament_sensors_row_ = nullptr;
    lv_obj_t* network_row_ = nullptr;
    lv_obj_t* factory_reset_row_ = nullptr;

    // Info rows (for dynamic updates)
    lv_obj_t* version_value_ = nullptr;
    lv_obj_t* printer_value_ = nullptr;
    lv_obj_t* klipper_value_ = nullptr;
    lv_obj_t* moonraker_value_ = nullptr;

    // Observers for reactive bindings (must be removed before labels are destroyed)
    lv_observer_t* klipper_version_observer_ = nullptr;
    lv_observer_t* moonraker_version_observer_ = nullptr;

    //
    // === Reactive Subjects ===
    //

    // Slider value subjects
    lv_subject_t brightness_value_subject_;

    // Info row subjects
    lv_subject_t version_value_subject_;
    lv_subject_t printer_value_subject_;

    // Static buffers for string subjects (required for lv_subject_init_string)
    char brightness_value_buf_[8]; // e.g., "75%"
    char version_value_buf_[32];   // e.g., "1.2.3"
    char printer_value_buf_[64];   // e.g., "Voron 2.4"

    // Lazily-created overlay panels
    lv_obj_t* display_settings_overlay_ = nullptr;
    lv_obj_t* filament_sensors_overlay_ = nullptr;
    lv_obj_t* macro_buttons_overlay_ = nullptr;
    lv_obj_t* hardware_health_overlay_ = nullptr;
    // Note: Bed mesh panel managed by get_global_bed_mesh_panel()
    // Note: Z-Offset calibration panel managed by get_global_zoffset_cal_panel()
    // Note: PID calibration panel managed by get_global_pid_cal_panel()
    // Note: factory_reset_dialog_ and theme_restart_dialog_ are public (for static callbacks)

    // Hardware save confirmation dialog state
    std::string pending_hardware_save_;
    lv_obj_t* hardware_save_dialog_ = nullptr;

    // Machine Limits overlay
    lv_obj_t* machine_limits_overlay_ = nullptr;
    MachineLimits current_limits_;  ///< Live values from sliders
    MachineLimits original_limits_; ///< Values when overlay opened

    // Subjects for value display (bind_text in XML)
    lv_subject_t max_velocity_display_subject_;
    char max_velocity_display_buf_[16];
    lv_subject_t max_accel_display_subject_;
    char max_accel_display_buf_[16];
    lv_subject_t accel_to_decel_display_subject_;
    char accel_to_decel_display_buf_[16];
    lv_subject_t square_corner_velocity_display_subject_;
    char square_corner_velocity_display_buf_[16];
    bool machine_limits_subjects_initialized_ = false;

    //
    // === Setup Helpers ===
    //

    void setup_toggle_handlers();
    void setup_dropdown();
    void setup_action_handlers();
    void populate_info_rows();
    void show_restart_prompt();

    //
    // === Event Handlers ===
    //

    void handle_dark_mode_changed(bool enabled);
    void handle_animations_changed(bool enabled);
    void handle_gcode_3d_changed(bool enabled);
    void handle_display_sleep_changed(int index);
    void handle_led_light_changed(bool enabled);
    void handle_sounds_changed(bool enabled);
    void handle_estop_confirm_changed(bool enabled);

    void handle_display_settings_clicked();
    void handle_filament_sensors_clicked();
    void handle_macro_buttons_clicked();
    void handle_machine_limits_clicked();
    void handle_network_clicked();
    void handle_factory_reset_clicked();
    void handle_plugins_clicked();
    void show_theme_restart_dialog();
    void populate_sensor_list();
    void populate_macro_dropdowns();
    void populate_hardware_issues();

  public:
    // Called by static modal callbacks - performs actual reset after confirmation
    void perform_factory_reset();

    // Called by toast action to navigate and open overlay
    void handle_hardware_health_clicked();

    // Called by hardware issue row action buttons
    // is_ignore: true="Ignore" (mark optional), false="Save" (add to config with confirmation)
    void handle_hardware_action(const char* hardware_name, bool is_ignore);

    // Dialog pointers accessible to static callbacks
    lv_obj_t* theme_restart_dialog_ = nullptr;
    lv_obj_t* factory_reset_dialog_ = nullptr;

  public:
    //
    // === XML Callbacks (public for global registration) ===
    // These are registered before settings_panel.xml is parsed [L013]
    //
    static void on_animations_changed(lv_event_t* e);
    static void on_gcode_3d_changed(lv_event_t* e);
    static void on_led_light_changed(lv_event_t* e);
    static void on_sounds_changed(lv_event_t* e);
    static void on_estop_confirm_changed(lv_event_t* e);
    static void on_display_settings_clicked(lv_event_t* e);
    static void on_filament_sensors_clicked(lv_event_t* e);
    static void on_macro_buttons_clicked(lv_event_t* e);
    static void on_machine_limits_clicked(lv_event_t* e);
    static void on_network_clicked(lv_event_t* e);
    static void on_factory_reset_clicked(lv_event_t* e);
    static void on_hardware_health_clicked(lv_event_t* e);
    static void on_plugins_clicked(lv_event_t* e);

  private:
    //
    // === Static Trampolines (private - only used internally) ===
    //
    static void on_dark_mode_changed(lv_event_t* e);
    static void on_display_sleep_changed(lv_event_t* e);

    // Static callbacks for overlays
    static void on_restart_later_clicked(lv_event_t* e);
    static void on_restart_now_clicked(lv_event_t* e);
    static void on_header_back_clicked(lv_event_t* e);
    static void on_brightness_changed(lv_event_t* e);

    // Static callbacks for hardware save confirmation dialog
    static void on_hardware_save_confirm(lv_event_t* e);
    static void on_hardware_save_cancel(lv_event_t* e);

    // Static callbacks for machine limits overlay
    static void on_max_velocity_changed(lv_event_t* e);
    static void on_max_accel_changed(lv_event_t* e);
    static void on_accel_to_decel_changed(lv_event_t* e);
    static void on_square_corner_velocity_changed(lv_event_t* e);
    static void on_limits_reset(lv_event_t* e);
    static void on_limits_apply(lv_event_t* e);

    // Instance methods called by static callbacks
    void handle_hardware_save_confirm();
    void handle_hardware_save_cancel();

    // Machine limits instance handlers
    void handle_max_velocity_changed(int value);
    void handle_max_accel_changed(int value);
    void handle_accel_to_decel_changed(int value);
    void handle_square_corner_velocity_changed(int value);
    void handle_limits_reset();
    void handle_limits_apply();
    void update_limits_display();
    void update_limits_sliders();
};

// Global instance accessor (needed by main.cpp)
SettingsPanel& get_global_settings_panel();

// Register SettingsPanel callbacks for XML parsing (call before settings_panel.xml registration)
// This ensures callbacks exist when LVGL parses the XML component [L013]
void register_settings_panel_callbacks();
