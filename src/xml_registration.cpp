// SPDX-License-Identifier: GPL-3.0-or-later

#include "xml_registration.h"

#include "ui_ams_current_tool.h"
#include "ui_fan_dial.h"
#include "ui_fonts.h"
#include "ui_gcode_viewer.h"
#include "ui_hsv_picker.h"
#include "ui_panel_settings.h"
#include "ui_spinner.h"
#include "ui_spool_canvas.h"
#include "ui_switch.h"
#include "ui_text.h"
#include "ui_text_input.h"
#include "ui_theme.h"

#include <spdlog/spdlog.h>

#include <lvgl.h>

namespace helix {

/**
 * No-op callback for optional event handlers in XML components.
 * When a component has an optional callback prop with default="",
 * LVGL tries to find a callback named "" which doesn't exist.
 * Registering this no-op callback silences those warnings.
 */
static void noop_event_callback(lv_event_t* /*e*/) {
    // Intentionally empty - used for optional callbacks that weren't provided
}

/**
 * No-op subject for optional subject bindings in XML components.
 * When a component has an optional subject prop with default="",
 * LVGL tries to find a subject named "" which doesn't exist.
 * Registering this no-op subject silences those warnings.
 */
static lv_subject_t s_noop_subject;
static bool s_noop_subject_initialized = false;

/**
 * Register responsive constants for color picker sizing based on screen dimensions
 * Call this BEFORE registering XML components that use the color picker
 */
static void register_color_picker_responsive_constants() {
    lv_display_t* display = lv_display_get_default();
    int32_t hor_res = lv_display_get_horizontal_resolution(display);
    int32_t ver_res = lv_display_get_vertical_resolution(display);
    int32_t greater_res = LV_MAX(hor_res, ver_res);

    // Preview swatch size and text height scale with screen
    const char* preview_size;
    const char* text_height;
    if (greater_res <= UI_BREAKPOINT_SMALL_MAX) {
        preview_size = "40";
        text_height = "52";
    } else if (greater_res <= UI_BREAKPOINT_MEDIUM_MAX) {
        preview_size = "48";
        text_height = "60";
    } else {
        preview_size = "56";
        text_height = "68";
    }

    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("globals");
    if (scope) {
        lv_xml_register_const(scope, "color_preview_size", preview_size);
        lv_xml_register_const(scope, "color_text_height", text_height);
        spdlog::debug(
            "[Color Picker] Registered color_preview_size={}, color_text_height={} for screen {}px",
            preview_size, text_height, greater_res);
    }
}

void register_xml_components() {
    spdlog::debug("[XML Registration] Registering XML components...");

    // Register responsive constants (AFTER globals, BEFORE components that use them)
    ui_switch_register_responsive_constants();
    register_color_picker_responsive_constants();

    // Register semantic text widgets (AFTER theme init, BEFORE components that use them)
    ui_text_init();
    ui_text_input_init(); // <text_input> with bind_text support
    ui_spinner_init();    // <spinner> with responsive sizing

    // Register no-op callback and subject for optional handlers in XML components
    // This silences warnings when components use callback/subject props with default=""
    lv_xml_register_event_cb(nullptr, "", noop_event_callback);
    lv_subject_init_int(&s_noop_subject, 0);
    lv_xml_register_subject(nullptr, "", &s_noop_subject);
    s_noop_subject_initialized = true;

    // Register custom widgets (BEFORE components that use them)
    ui_gcode_viewer_register();
    ui_spool_canvas_register(); // Needed by Spoolman panel (and AMS panel)
    ui_hsv_picker_register();   // HSV color picker for edit filament modal
    ui_ams_current_tool_init(); // AMS current tool indicator callbacks
    // NOTE: Other AMS widgets (ams_slot, filament_path_canvas) are
    // registered lazily in ui_panel_ams.cpp when the AMS panel is first accessed

    // Spoolman components (MUST be after spool_canvas registration)
    lv_xml_register_component_from_file("A:ui_xml/spoolman_spool_row.xml");
    lv_xml_register_component_from_file("A:ui_xml/spoolman_panel.xml");

    // Core UI components
    lv_xml_register_component_from_file("A:ui_xml/icon.xml");
    lv_xml_register_component_from_file("A:ui_xml/filament_sensor_indicator.xml");
    lv_xml_register_component_from_file("A:ui_xml/filament_sensor_row.xml");
    lv_xml_register_component_from_file("A:ui_xml/temp_display.xml");
    lv_xml_register_component_from_file("A:ui_xml/header_bar.xml");
    lv_xml_register_component_from_file("A:ui_xml/overlay_backdrop.xml");
    lv_xml_register_component_from_file("A:ui_xml/overlay_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/toast_notification.xml");
    // emergency_stop_button.xml removed - E-Stop buttons are now embedded in panels
    lv_xml_register_component_from_file("A:ui_xml/estop_confirmation_dialog.xml");
    lv_xml_register_component_from_file("A:ui_xml/klipper_recovery_dialog.xml");
    lv_xml_register_component_from_file("A:ui_xml/print_cancel_confirm_modal.xml");
    lv_xml_register_component_from_file("A:ui_xml/print_completion_modal.xml");
    lv_xml_register_component_from_file("A:ui_xml/save_z_offset_modal.xml");

    // Notification history
    lv_xml_register_component_from_file("A:ui_xml/notification_history_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/notification_history_item.xml");

    // Modal dialogs
    lv_xml_register_component_from_file("A:ui_xml/modal_dialog.xml");
    lv_xml_register_component_from_file("A:ui_xml/numeric_keypad_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/runout_guidance_modal.xml");
    lv_xml_register_component_from_file("A:ui_xml/plugin_install_modal.xml");
    lv_xml_register_component_from_file("A:ui_xml/macro_enhance_modal.xml");

    // Print file components
    lv_xml_register_component_from_file("A:ui_xml/print_file_card.xml");
    lv_xml_register_component_from_file("A:ui_xml/print_file_list_row.xml");
    lv_xml_register_component_from_file("A:ui_xml/print_file_detail.xml");

    // Main navigation and panels
    lv_xml_register_component_from_file("A:ui_xml/navigation_bar.xml");
    lv_xml_register_component_from_file("A:ui_xml/home_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/controls_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/motion_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/nozzle_temp_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/bed_temp_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/extrusion_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/fan_dial.xml");
    register_fan_dial_callbacks(); // Register FanDial event callbacks
    lv_xml_register_component_from_file("A:ui_xml/fan_status_card.xml");
    lv_xml_register_component_from_file("A:ui_xml/fan_control_overlay.xml");
    lv_xml_register_component_from_file("A:ui_xml/ams_current_tool.xml");
    lv_xml_register_component_from_file("A:ui_xml/print_status_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/print_tune_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/filament_panel.xml");

    // NOTE: AMS panel (ams_panel.xml) is registered lazily in ui_panel_ams.cpp

    // Feature parity panels
    lv_xml_register_component_from_file("A:ui_xml/macro_card.xml");
    lv_xml_register_component_from_file("A:ui_xml/macro_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/console_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/power_device_row.xml");
    lv_xml_register_component_from_file("A:ui_xml/power_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/screws_tilt_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/input_shaper_panel.xml");

    // Print history panels
    lv_xml_register_component_from_file("A:ui_xml/history_list_row.xml");
    lv_xml_register_component_from_file("A:ui_xml/history_list_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/history_detail_overlay.xml");
    lv_xml_register_component_from_file("A:ui_xml/history_dashboard_panel.xml");

    // Settings components (must be registered before settings_panel)
    lv_xml_register_component_from_file("A:ui_xml/setting_section_header.xml");
    lv_xml_register_component_from_file("A:ui_xml/setting_toggle_row.xml");
    lv_xml_register_component_from_file("A:ui_xml/setting_dropdown_row.xml");
    lv_xml_register_component_from_file("A:ui_xml/setting_action_row.xml");
    lv_xml_register_component_from_file("A:ui_xml/setting_info_row.xml");
    lv_xml_register_component_from_file("A:ui_xml/setting_slider_row.xml");
    register_settings_panel_callbacks(); // Register callbacks before XML parse [L013]
    lv_xml_register_component_from_file("A:ui_xml/settings_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/restart_prompt_dialog.xml");
    lv_xml_register_component_from_file("A:ui_xml/theme_restart_modal.xml");
    lv_xml_register_component_from_file("A:ui_xml/factory_reset_modal.xml");

    // Calibration panels (overlays launched from settings)
    lv_xml_register_component_from_file("A:ui_xml/calibration_zoffset_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/calibration_pid_panel.xml");

    // Bed mesh modals (must be registered before bed_mesh_panel which uses them)
    lv_xml_register_component_from_file("A:ui_xml/bed_mesh_calibrate_modal.xml");
    lv_xml_register_component_from_file("A:ui_xml/bed_mesh_rename_modal.xml");
    lv_xml_register_component_from_file("A:ui_xml/bed_mesh_save_config_modal.xml");
    lv_xml_register_component_from_file("A:ui_xml/bed_mesh_panel.xml");

    // Settings overlay panels
    lv_xml_register_component_from_file("A:ui_xml/display_settings_overlay.xml");
    lv_xml_register_component_from_file("A:ui_xml/filament_sensors_overlay.xml");
    lv_xml_register_component_from_file("A:ui_xml/macro_buttons_overlay.xml");
    lv_xml_register_component_from_file("A:ui_xml/hardware_issue_row.xml");
    lv_xml_register_component_from_file("A:ui_xml/hardware_health_overlay.xml");
    lv_xml_register_component_from_file("A:ui_xml/network_settings_overlay.xml");
    lv_xml_register_component_from_file("A:ui_xml/retraction_settings_overlay.xml");
    lv_xml_register_component_from_file("A:ui_xml/machine_limits_overlay.xml");
    lv_xml_register_component_from_file("A:ui_xml/timelapse_settings_overlay.xml");
    lv_xml_register_component_from_file("A:ui_xml/hidden_network_modal.xml");
    lv_xml_register_component_from_file("A:ui_xml/network_test_modal.xml");
    lv_xml_register_component_from_file("A:ui_xml/filament_preset_edit_modal.xml");
    lv_xml_register_component_from_file("A:ui_xml/wifi_network_item.xml");

    // Development tools
    lv_xml_register_component_from_file("A:ui_xml/memory_stats_overlay.xml");

    // Additional panels
    lv_xml_register_component_from_file("A:ui_xml/advanced_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/test_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/print_select_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/gcode_test_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/glyphs_panel.xml");

    // App layout
    lv_xml_register_component_from_file("A:ui_xml/app_layout.xml");

    // Wizard components
    lv_xml_register_component_from_file("A:ui_xml/wizard_header_bar.xml");
    lv_xml_register_component_from_file("A:ui_xml/wizard_container.xml");
    lv_xml_register_component_from_file("A:ui_xml/network_list_item.xml");
    lv_xml_register_component_from_file("A:ui_xml/wifi_password_modal.xml");
    lv_xml_register_component_from_file("A:ui_xml/wizard_wifi_setup.xml");
    lv_xml_register_component_from_file("A:ui_xml/wizard_connection.xml");
    lv_xml_register_component_from_file("A:ui_xml/wizard_printer_identify.xml");
    lv_xml_register_component_from_file("A:ui_xml/wizard_heater_select.xml");
    lv_xml_register_component_from_file("A:ui_xml/wizard_fan_select.xml");
    lv_xml_register_component_from_file("A:ui_xml/wizard_led_select.xml");
    lv_xml_register_component_from_file("A:ui_xml/wizard_filament_sensor_select.xml");
    lv_xml_register_component_from_file("A:ui_xml/wizard_summary.xml");

    spdlog::debug("[XML Registration] XML component registration complete");
}

void deinit_xml_subjects() {
    if (s_noop_subject_initialized) {
        lv_subject_deinit(&s_noop_subject);
        s_noop_subject_initialized = false;
        spdlog::debug("[XML Registration] No-op subject deinitialized");
    }
}

} // namespace helix
