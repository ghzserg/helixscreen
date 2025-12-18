// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_settings.h"

#include "ui_emergency_stop.h"
#include "ui_event_safety.h"
#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_panel_bed_mesh.h"
#include "ui_panel_calibration_pid.h"
#include "ui_panel_calibration_zoffset.h"
#include "ui_panel_memory_stats.h"
#include "ui_toast.h"

#include "app_globals.h"
#include "config.h"
#include "filament_sensor_manager.h"
#include "helix_version.h"
#include "moonraker_client.h"
#include "network_settings_overlay.h"
#include "printer_state.h"
#include "settings_manager.h"
#include "sound_manager.h"

#include <spdlog/spdlog.h>

#include <memory>

// Forward declarations for class-based API
class BedMeshPanel;
BedMeshPanel& get_global_bed_mesh_panel();
ZOffsetCalibrationPanel& get_global_zoffset_cal_panel();
PIDCalibrationPanel& get_global_pid_cal_panel();
MoonrakerClient* get_moonraker_client();

// ============================================================================
// CONSTRUCTOR
// ============================================================================

SettingsPanel::SettingsPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    spdlog::trace("[{}] Constructor", get_name());
}

// ============================================================================
// PANELBASE IMPLEMENTATION
// ============================================================================

// Static callback for XML event_cb (registered with lv_xml_register_event_cb)
static void on_completion_alert_dropdown_changed(lv_event_t* e) {
    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int index = static_cast<int>(lv_dropdown_get_selected(dropdown));
    auto mode = static_cast<CompletionAlertMode>(index);
    spdlog::info("[SettingsPanel] Completion alert changed: {} ({})", index,
                 index == 0 ? "Off" : (index == 1 ? "Notification" : "Alert"));
    SettingsManager::instance().set_completion_alert_mode(mode);
}

// Static callback for display sleep dropdown
static void on_display_sleep_dropdown_changed(lv_event_t* e) {
    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int index = static_cast<int>(lv_dropdown_get_selected(dropdown));
    int seconds = SettingsManager::index_to_sleep_seconds(index);
    spdlog::info("[SettingsPanel] Display sleep changed: index {} = {}s", index, seconds);
    SettingsManager::instance().set_display_sleep_sec(seconds);
}

// Static callback for bed mesh render mode dropdown
static void on_bed_mesh_mode_changed(lv_event_t* e) {
    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int mode = static_cast<int>(lv_dropdown_get_selected(dropdown));
    spdlog::info("[SettingsPanel] Bed mesh render mode changed: {} ({})", mode,
                 mode == 0 ? "Auto" : (mode == 1 ? "3D" : "2D"));
    SettingsManager::instance().set_bed_mesh_render_mode(mode);
}

// Static callback for G-code render mode dropdown
static void on_gcode_mode_changed(lv_event_t* e) {
    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int mode = static_cast<int>(lv_dropdown_get_selected(dropdown));
    spdlog::info("[SettingsPanel] G-code render mode changed: {} ({})", mode,
                 mode == 0 ? "Auto" : (mode == 1 ? "3D" : "2D Layers"));
    SettingsManager::instance().set_gcode_render_mode(mode);
}

// Static callback for version row long-press (memory debug toggle)
// This provides a touch-based way to enable memory debugging on Pi (no keyboard)
static void on_version_long_pressed(lv_event_t*) {
    MemoryStatsOverlay::instance().toggle();
    bool is_visible = MemoryStatsOverlay::instance().is_visible();
    ui_toast_show(ToastSeverity::SUCCESS, is_visible ? "Memory debug: ON" : "Memory debug: OFF",
                  1500);
    spdlog::info("[SettingsPanel] Memory debug toggled via long-press: {}",
                 is_visible ? "ON" : "OFF");
}

// Static callback for filament sensor master toggle (XML event_cb)
static void on_filament_master_toggle_changed(lv_event_t* e) {
    auto* toggle = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    auto& mgr = helix::FilamentSensorManager::instance();
    mgr.set_master_enabled(enabled);
    mgr.save_config();
    spdlog::info("[SettingsPanel] Filament sensor master enabled: {}", enabled ? "ON" : "OFF");
}

void SettingsPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // Initialize SettingsManager subjects (for reactive binding)
    SettingsManager::instance().init_subjects();

    // Initialize slider value subjects (for reactive binding)
    lv_subject_init_string(&brightness_value_subject_, brightness_value_buf_, nullptr,
                           sizeof(brightness_value_buf_), "100%");
    lv_xml_register_subject(nullptr, "brightness_value", &brightness_value_subject_);

    // Initialize info row subjects (for reactive binding)
    lv_subject_init_string(&version_value_subject_, version_value_buf_, nullptr,
                           sizeof(version_value_buf_), "—");
    lv_xml_register_subject(nullptr, "version_value", &version_value_subject_);

    lv_subject_init_string(&printer_value_subject_, printer_value_buf_, nullptr,
                           sizeof(printer_value_buf_), "—");
    lv_xml_register_subject(nullptr, "printer_value", &printer_value_subject_);

    // Register XML event callbacks for dropdowns (already in XML)
    lv_xml_register_event_cb(nullptr, "on_completion_alert_changed",
                             on_completion_alert_dropdown_changed);
    lv_xml_register_event_cb(nullptr, "on_display_sleep_changed",
                             on_display_sleep_dropdown_changed);
    lv_xml_register_event_cb(nullptr, "on_bed_mesh_mode_changed", on_bed_mesh_mode_changed);
    lv_xml_register_event_cb(nullptr, "on_gcode_mode_changed", on_gcode_mode_changed);
    lv_xml_register_event_cb(nullptr, "on_version_long_pressed", on_version_long_pressed);

    // Register XML event callbacks for toggle switches
    lv_xml_register_event_cb(nullptr, "on_dark_mode_changed", on_dark_mode_changed);
    lv_xml_register_event_cb(nullptr, "on_animations_changed", on_animations_changed);
    lv_xml_register_event_cb(nullptr, "on_gcode_3d_changed", on_gcode_3d_changed);
    lv_xml_register_event_cb(nullptr, "on_led_light_changed", on_led_light_changed);
    // Note: on_retraction_row_clicked is registered by RetractionSettingsOverlay
    lv_xml_register_event_cb(nullptr, "on_sounds_changed", on_sounds_changed);
    lv_xml_register_event_cb(nullptr, "on_estop_confirm_changed", on_estop_confirm_changed);

    // Register XML event callbacks for action rows
    lv_xml_register_event_cb(nullptr, "on_display_settings_clicked", on_display_settings_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_sensors_clicked", on_filament_sensors_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_master_toggle_changed",
                             on_filament_master_toggle_changed);
    lv_xml_register_event_cb(nullptr, "on_bed_mesh_clicked", on_bed_mesh_clicked);
    lv_xml_register_event_cb(nullptr, "on_z_offset_clicked", on_z_offset_clicked);
    lv_xml_register_event_cb(nullptr, "on_pid_tuning_clicked", on_pid_tuning_clicked);
    lv_xml_register_event_cb(nullptr, "on_network_clicked", on_network_clicked);
    lv_xml_register_event_cb(nullptr, "on_factory_reset_clicked", on_factory_reset_clicked);

    // Register XML event callbacks for modal dialogs and overlays
    lv_xml_register_event_cb(nullptr, "on_modal_primary_clicked", on_modal_primary_clicked);
    lv_xml_register_event_cb(nullptr, "on_modal_secondary_clicked", on_modal_secondary_clicked);
    lv_xml_register_event_cb(nullptr, "on_modal_backdrop_clicked", on_modal_backdrop_clicked);
    lv_xml_register_event_cb(nullptr, "on_restart_later_clicked", on_restart_later_clicked);
    lv_xml_register_event_cb(nullptr, "on_restart_now_clicked", on_restart_now_clicked);
    lv_xml_register_event_cb(nullptr, "on_header_back_clicked", on_header_back_clicked);
    lv_xml_register_event_cb(nullptr, "on_brightness_changed", on_brightness_changed);

    // Note: BedMeshPanel subjects are initialized in main.cpp during startup

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void SettingsPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    // Setup all handlers and bindings
    setup_toggle_handlers();
    setup_action_handlers();
    populate_info_rows();

    spdlog::info("[{}] Setup complete", get_name());
}

// ============================================================================
// SETUP HELPERS
// ============================================================================

void SettingsPanel::setup_toggle_handlers() {
    auto& settings = SettingsManager::instance();

    // === Dark Mode Toggle ===
    // Event handler wired via XML <event_cb>, just set initial state here
    lv_obj_t* dark_mode_row = lv_obj_find_by_name(panel_, "row_dark_mode");
    if (dark_mode_row) {
        dark_mode_switch_ = lv_obj_find_by_name(dark_mode_row, "toggle");
        if (dark_mode_switch_) {
            // Set initial state from SettingsManager
            if (settings.get_dark_mode()) {
                lv_obj_add_state(dark_mode_switch_, LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(dark_mode_switch_, LV_STATE_CHECKED);
            }
            spdlog::debug("[{}]   ✓ Dark mode toggle", get_name());
        }
    }

    // === Animations Toggle ===
    // Event handler wired via XML <event_cb>, just set initial state here
    lv_obj_t* animations_row = lv_obj_find_by_name(panel_, "row_animations");
    if (animations_row) {
        animations_switch_ = lv_obj_find_by_name(animations_row, "toggle");
        if (animations_switch_) {
            // Set initial state from SettingsManager
            if (settings.get_animations_enabled()) {
                lv_obj_add_state(animations_switch_, LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(animations_switch_, LV_STATE_CHECKED);
            }
            spdlog::debug("[{}]   ✓ Animations toggle", get_name());
        }
    }

    // === LED Light Toggle ===
    // Event handler wired via XML <event_cb>, just set initial state here
    lv_obj_t* led_light_row = lv_obj_find_by_name(panel_, "row_led_light");
    if (led_light_row) {
        led_light_switch_ = lv_obj_find_by_name(led_light_row, "toggle");
        if (led_light_switch_) {
            // LED state from SettingsManager (ephemeral, starts off)
            if (settings.get_led_enabled()) {
                lv_obj_add_state(led_light_switch_, LV_STATE_CHECKED);
            }
            spdlog::debug("[{}]   ✓ LED light toggle", get_name());
        }
    }

    // === Sounds Toggle ===
    // Event handler wired via XML <event_cb>, just set initial state here
    lv_obj_t* sounds_row = lv_obj_find_by_name(panel_, "row_sounds");
    if (sounds_row) {
        sounds_switch_ = lv_obj_find_by_name(sounds_row, "toggle");
        if (sounds_switch_) {
            if (settings.get_sounds_enabled()) {
                lv_obj_add_state(sounds_switch_, LV_STATE_CHECKED);
            }
            spdlog::debug("[{}]   ✓ Sounds toggle", get_name());
        }
    }

    // === Completion Alert Dropdown ===
    // Event handler wired via XML <event_cb>, just set initial value here
    lv_obj_t* completion_row = lv_obj_find_by_name(panel_, "row_completion_alert");
    if (completion_row) {
        completion_alert_dropdown_ =
            lv_obj_find_by_name(completion_row, "completion_alert_dropdown");
        if (completion_alert_dropdown_) {
            auto mode = settings.get_completion_alert_mode();
            lv_dropdown_set_selected(completion_alert_dropdown_, static_cast<uint32_t>(mode));
            spdlog::debug("[{}]   ✓ Completion alert dropdown (mode={})", get_name(),
                          static_cast<int>(mode));
        }
    }

    // === E-Stop Confirmation Toggle ===
    // Event handler wired via XML <event_cb>, just set initial state here
    lv_obj_t* estop_confirm_row = lv_obj_find_by_name(panel_, "row_estop_confirm");
    if (estop_confirm_row) {
        estop_confirm_switch_ = lv_obj_find_by_name(estop_confirm_row, "toggle");
        if (estop_confirm_switch_) {
            if (settings.get_estop_require_confirmation()) {
                lv_obj_add_state(estop_confirm_switch_, LV_STATE_CHECKED);
            }
            spdlog::debug("[{}]   ✓ E-Stop confirmation toggle", get_name());
        }
    }
}

void SettingsPanel::setup_action_handlers() {
    // All action row event handlers are wired via XML <event_cb>
    // Just cache the row references for potential future use

    // === Display Settings Row ===
    display_settings_row_ = lv_obj_find_by_name(panel_, "row_display_settings");
    if (display_settings_row_) {
        spdlog::debug("[{}]   ✓ Display settings action row", get_name());
    }

    // === Filament Sensors Row ===
    filament_sensors_row_ = lv_obj_find_by_name(panel_, "row_filament_sensors");
    if (filament_sensors_row_) {
        spdlog::debug("[{}]   ✓ Filament sensors action row", get_name());
    }

    // === Bed Mesh Row ===
    bed_mesh_row_ = lv_obj_find_by_name(panel_, "row_bed_mesh");
    if (bed_mesh_row_) {
        spdlog::debug("[{}]   ✓ Bed mesh action row", get_name());
    }

    // === Z-Offset Row ===
    z_offset_row_ = lv_obj_find_by_name(panel_, "row_z_offset");
    if (z_offset_row_) {
        spdlog::debug("[{}]   ✓ Z-offset action row", get_name());
    }

    // === PID Tuning Row ===
    pid_tuning_row_ = lv_obj_find_by_name(panel_, "row_pid_tuning");
    if (pid_tuning_row_) {
        spdlog::debug("[{}]   ✓ PID tuning action row", get_name());
    }

    // === Network Row ===
    network_row_ = lv_obj_find_by_name(panel_, "row_network");
    if (network_row_) {
        spdlog::debug("[{}]   ✓ Network action row", get_name());
    }

    // === Factory Reset Row ===
    factory_reset_row_ = lv_obj_find_by_name(panel_, "row_factory_reset");
    if (factory_reset_row_) {
        spdlog::debug("[{}]   ✓ Factory reset action row", get_name());
    }
}

void SettingsPanel::populate_info_rows() {
    // === Version ===
    lv_obj_t* version_row = lv_obj_find_by_name(panel_, "row_version");
    if (version_row) {
        version_value_ = lv_obj_find_by_name(version_row, "value");
        if (version_value_) {
            // Update subject (label binding happens in XML)
            lv_subject_copy_string(&version_value_subject_, helix_version());
            spdlog::debug("[{}]   ✓ Version: {}", get_name(), helix_version());
        }
    }

    // === Printer Name (from PrinterState or Config) ===
    lv_obj_t* printer_row = lv_obj_find_by_name(panel_, "row_printer");
    if (printer_row) {
        printer_value_ = lv_obj_find_by_name(printer_row, "value");
        if (printer_value_) {
            // Try to get printer name from config
            Config* config = Config::get_instance();
            std::string printer_name =
                config->get<std::string>(config->df() + "printer_name", "Unknown");
            // Update subject (label binding happens in XML)
            lv_subject_copy_string(&printer_value_subject_, printer_name.c_str());
            spdlog::debug("[{}]   ✓ Printer: {}", get_name(), printer_name);
        }
    }

    // === Klipper Version (reactive binding from PrinterState) ===
    lv_obj_t* klipper_row = lv_obj_find_by_name(panel_, "row_klipper");
    if (klipper_row) {
        klipper_value_ = lv_obj_find_by_name(klipper_row, "value");
        if (klipper_value_) {
            // Bind to reactive subject - updates automatically after discovery
            lv_label_bind_text(klipper_value_, printer_state_.get_klipper_version_subject(), "%s");
            spdlog::debug("[{}]   ✓ Klipper version bound to subject", get_name());
        }
    }

    // === Moonraker Version (reactive binding from PrinterState) ===
    lv_obj_t* moonraker_row = lv_obj_find_by_name(panel_, "row_moonraker");
    if (moonraker_row) {
        moonraker_value_ = lv_obj_find_by_name(moonraker_row, "value");
        if (moonraker_value_) {
            // Bind to reactive subject - updates automatically after discovery
            lv_label_bind_text(moonraker_value_, printer_state_.get_moonraker_version_subject(),
                               "%s");
            spdlog::debug("[{}]   ✓ Moonraker version bound to subject", get_name());
        }
    }
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

void SettingsPanel::handle_dark_mode_changed(bool enabled) {
    spdlog::info("[{}] Dark mode toggled: {}", get_name(), enabled ? "ON" : "OFF");

    // Save the setting to config (will be applied on next app launch)
    SettingsManager::instance().set_dark_mode(enabled);

    // Show dialog informing user that restart is required
    show_theme_restart_dialog();
}

void SettingsPanel::handle_animations_changed(bool enabled) {
    spdlog::info("[{}] Animations toggled: {}", get_name(), enabled ? "ON" : "OFF");
    SettingsManager::instance().set_animations_enabled(enabled);
}

void SettingsPanel::handle_gcode_3d_changed(bool enabled) {
    spdlog::info("[{}] G-code 3D preview toggled: {}", get_name(), enabled ? "ON" : "OFF");
    SettingsManager::instance().set_gcode_3d_enabled(enabled);
}

void SettingsPanel::show_theme_restart_dialog() {
    // Create dialog on first use (lazy initialization)
    if (!theme_restart_dialog_ && parent_screen_) {
        spdlog::debug("[{}] Creating theme restart dialog...", get_name());

        // Configure modal_dialog subjects BEFORE creation
        // INFO severity (0) = blue info icon, show both buttons
        // "Later" = dismiss (restart later), "Restart" = restart now
        ui_modal_configure(UI_MODAL_SEVERITY_INFO, true, "Restart", "Later");

        // Create modal_dialog with title/message props
        const char* attrs[] = {"title", "Theme Changed", "message",
                               "Restart the app to apply the new theme.", nullptr};
        theme_restart_dialog_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "modal_dialog", attrs));

        if (theme_restart_dialog_) {
            // Set user_data on buttons so callbacks can identify which dialog this is
            lv_obj_t* restart_btn = lv_obj_find_by_name(theme_restart_dialog_, "btn_primary");
            if (restart_btn) {
                lv_obj_set_user_data(restart_btn, theme_restart_dialog_);
            }

            lv_obj_t* ok_btn = lv_obj_find_by_name(theme_restart_dialog_, "btn_secondary");
            if (ok_btn) {
                lv_obj_set_user_data(ok_btn, theme_restart_dialog_);
            }

            // Start hidden
            lv_obj_add_flag(theme_restart_dialog_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Theme restart dialog created", get_name());
        } else {
            spdlog::error("[{}] Failed to create theme restart dialog", get_name());
            return;
        }
    }

    // Show the dialog
    if (theme_restart_dialog_) {
        lv_obj_remove_flag(theme_restart_dialog_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(theme_restart_dialog_);
    }
}

void SettingsPanel::handle_display_sleep_changed(int index) {
    int seconds = SettingsManager::index_to_sleep_seconds(index);
    spdlog::info("[{}] Display sleep changed: index {} = {}s", get_name(), index, seconds);
    SettingsManager::instance().set_display_sleep_sec(seconds);
}

void SettingsPanel::handle_led_light_changed(bool enabled) {
    spdlog::info("[{}] LED light toggled: {}", get_name(), enabled ? "ON" : "OFF");
    SettingsManager::instance().set_led_enabled(enabled);
}

void SettingsPanel::handle_sounds_changed(bool enabled) {
    spdlog::info("[{}] Sounds toggled: {}", get_name(), enabled ? "ON" : "OFF");
    SettingsManager::instance().set_sounds_enabled(enabled);

    // Play test beep when enabling sounds
    if (enabled) {
        SoundManager::instance().play_test_beep();
    }
}

void SettingsPanel::handle_estop_confirm_changed(bool enabled) {
    spdlog::info("[{}] E-Stop confirmation toggled: {}", get_name(), enabled ? "ON" : "OFF");
    SettingsManager::instance().set_estop_require_confirmation(enabled);
    // Update EmergencyStopOverlay immediately
    EmergencyStopOverlay::instance().set_require_confirmation(enabled);
}

void SettingsPanel::show_restart_prompt() {
    // Only show once per session - check if dialog already exists and is visible
    if (restart_prompt_dialog_ && !lv_obj_has_flag(restart_prompt_dialog_, LV_OBJ_FLAG_HIDDEN)) {
        return; // Already showing
    }

    // Create restart prompt dialog on first access
    if (!restart_prompt_dialog_ && parent_screen_) {
        restart_prompt_dialog_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "restart_prompt_dialog", nullptr));
        if (restart_prompt_dialog_) {
            // Event handlers are already wired via XML event_cb elements
            // No need to wire up buttons here - callbacks registered in init_subjects()

            // Initially hidden
            lv_obj_add_flag(restart_prompt_dialog_, LV_OBJ_FLAG_HIDDEN);
            spdlog::debug("[{}] Restart prompt dialog created", get_name());
        }
    }

    // Show the dialog
    if (restart_prompt_dialog_) {
        lv_obj_remove_flag(restart_prompt_dialog_, LV_OBJ_FLAG_HIDDEN);
        // Clear pending flag so we don't show again until next change
        SettingsManager::instance().clear_restart_pending();
    }
}

void SettingsPanel::handle_display_settings_clicked() {
    spdlog::debug("[{}] Display Settings clicked - opening overlay", get_name());

    // Create display settings overlay on first access (lazy initialization)
    if (!display_settings_overlay_ && parent_screen_) {
        spdlog::debug("[{}] Creating display settings overlay...", get_name());

        // Create from XML - component name matches filename
        display_settings_overlay_ = static_cast<lv_obj_t*>(
            lv_xml_create(parent_screen_, "display_settings_overlay", nullptr));
        if (display_settings_overlay_) {
            // Back button event handler already wired via header_bar XML event_cb
            // Brightness slider event handler already wired via XML event_cb

            // Wire up brightness slider initial state
            lv_obj_t* brightness_slider =
                lv_obj_find_by_name(display_settings_overlay_, "brightness_slider");
            lv_obj_t* brightness_label =
                lv_obj_find_by_name(display_settings_overlay_, "brightness_value_label");
            if (brightness_slider && brightness_label) {
                // Set initial value from settings
                int brightness = SettingsManager::instance().get_brightness();
                lv_slider_set_value(brightness_slider, brightness, LV_ANIM_OFF);
                // Update subject (label binding happens in XML)
                snprintf(brightness_value_buf_, sizeof(brightness_value_buf_), "%d%%", brightness);
                lv_subject_copy_string(&brightness_value_subject_, brightness_value_buf_);
                // Event handler already wired via XML event_cb
            }

            // Initialize sleep timeout dropdown (inside setting_dropdown_row)
            lv_obj_t* sleep_row =
                lv_obj_find_by_name(display_settings_overlay_, "row_display_sleep");
            lv_obj_t* sleep_dropdown =
                sleep_row ? lv_obj_find_by_name(sleep_row, "dropdown") : nullptr;
            if (sleep_dropdown) {
                // Set dropdown options
                lv_dropdown_set_options(sleep_dropdown,
                                        "Never\n1 minute\n5 minutes\n10 minutes\n30 minutes");
                // Set initial selection based on current setting
                int current_sec = SettingsManager::instance().get_display_sleep_sec();
                int index = SettingsManager::sleep_seconds_to_index(current_sec);
                lv_dropdown_set_selected(sleep_dropdown, index);
                spdlog::debug("[{}] Sleep dropdown initialized to index {} ({}s)", get_name(),
                              index, current_sec);
            }

            // Initialize bed mesh render mode dropdown (inside setting_dropdown_row)
            lv_obj_t* bed_mesh_row =
                lv_obj_find_by_name(display_settings_overlay_, "row_bed_mesh_mode");
            lv_obj_t* bed_mesh_dropdown =
                bed_mesh_row ? lv_obj_find_by_name(bed_mesh_row, "dropdown") : nullptr;
            if (bed_mesh_dropdown) {
                // Set dropdown options
                lv_dropdown_set_options(bed_mesh_dropdown,
                                        SettingsManager::get_bed_mesh_render_mode_options());
                // Set initial selection based on current setting
                int current_mode = SettingsManager::instance().get_bed_mesh_render_mode();
                lv_dropdown_set_selected(bed_mesh_dropdown, current_mode);
                spdlog::debug("[{}] Bed mesh mode dropdown initialized to {} ({})", get_name(),
                              current_mode,
                              current_mode == 0 ? "Auto" : (current_mode == 1 ? "3D" : "2D"));
            }

            // Initialize G-code render mode dropdown (inside setting_dropdown_row, currently
            // hidden)
            lv_obj_t* gcode_row = lv_obj_find_by_name(display_settings_overlay_, "row_gcode_mode");
            lv_obj_t* gcode_dropdown =
                gcode_row ? lv_obj_find_by_name(gcode_row, "dropdown") : nullptr;
            if (gcode_dropdown) {
                // Set dropdown options
                lv_dropdown_set_options(gcode_dropdown,
                                        SettingsManager::get_gcode_render_mode_options());
                // Set initial selection based on current setting
                int current_mode = SettingsManager::instance().get_gcode_render_mode();
                lv_dropdown_set_selected(gcode_dropdown, current_mode);
                spdlog::debug(
                    "[{}] G-code mode dropdown initialized to {} ({})", get_name(), current_mode,
                    current_mode == 0 ? "Auto" : (current_mode == 1 ? "3D" : "2D Layers"));
            }

            // Initially hidden
            lv_obj_add_flag(display_settings_overlay_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Display settings overlay created", get_name());
        } else {
            spdlog::error("[{}] Failed to create display settings overlay from XML", get_name());
            return;
        }
    }

    // Push overlay onto navigation history and show it
    if (display_settings_overlay_) {
        ui_nav_push_overlay(display_settings_overlay_);
    }
}

void SettingsPanel::handle_filament_sensors_clicked() {
    spdlog::debug("[{}] Filament Sensors clicked - opening overlay", get_name());

    // Create filament sensors overlay on first access (lazy initialization)
    if (!filament_sensors_overlay_ && parent_screen_) {
        spdlog::debug("[{}] Creating filament sensors overlay...", get_name());

        // Create from XML - component name matches filename
        filament_sensors_overlay_ = static_cast<lv_obj_t*>(
            lv_xml_create(parent_screen_, "filament_sensors_overlay", nullptr));
        if (filament_sensors_overlay_) {
            // Back button already wired via header_bar XML event_cb (on_header_back_clicked)

            // Note: Master toggle state and events are handled declaratively via XML:
            // - Initial state via bind_state_if_eq to filament_master_enabled subject
            // - Value changes via event_cb to on_filament_master_toggle_changed

            // Update sensor count label
            lv_obj_t* count_label =
                lv_obj_find_by_name(filament_sensors_overlay_, "sensor_count_label");
            if (count_label) {
                auto& mgr = helix::FilamentSensorManager::instance();
                lv_label_set_text_fmt(count_label, "(%zu)", mgr.sensor_count());
            }

            // Populate sensor list
            populate_sensor_list();

            // Initially hidden
            lv_obj_add_flag(filament_sensors_overlay_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Filament sensors overlay created", get_name());
        } else {
            spdlog::error("[{}] Failed to create filament sensors overlay from XML", get_name());
            return;
        }
    }

    // Push overlay onto navigation history and show it
    if (filament_sensors_overlay_) {
        ui_nav_push_overlay(filament_sensors_overlay_);
    }
}

void SettingsPanel::populate_sensor_list() {
    if (!filament_sensors_overlay_) {
        return;
    }

    lv_obj_t* sensors_list = lv_obj_find_by_name(filament_sensors_overlay_, "sensors_list");
    if (!sensors_list) {
        spdlog::error("[{}] Could not find sensors_list container", get_name());
        return;
    }

    // Get discovered sensors
    auto& mgr = helix::FilamentSensorManager::instance();
    auto sensors = mgr.get_sensors();

    spdlog::debug("[{}] Populating sensor list with {} sensors", get_name(), sensors.size());

    // NOTE: Placeholder visibility is handled by XML binding to filament_sensor_count subject
    // via <lv_obj-bind_flag_if_ne subject="filament_sensor_count" flag="hidden" ref_value="0"/>

    // Create a row for each sensor
    for (const auto& sensor : sensors) {
        // Create sensor row from XML component
        const char* attrs[] = {
            "sensor_name", sensor.sensor_name.c_str(), "sensor_type",
            sensor.type == helix::FilamentSensorType::MOTION ? "motion" : "switch", nullptr};
        auto* row =
            static_cast<lv_obj_t*>(lv_xml_create(sensors_list, "filament_sensor_row", attrs));
        if (!row) {
            spdlog::error("[{}] Failed to create sensor row for {}", get_name(),
                          sensor.sensor_name);
            continue;
        }

        // Store klipper_name as user data for callbacks
        // Note: We need to allocate this because sensor goes out of scope
        char* klipper_name = static_cast<char*>(lv_malloc(sensor.klipper_name.size() + 1));
        strcpy(klipper_name, sensor.klipper_name.c_str());
        lv_obj_set_user_data(row, klipper_name);

        // Register cleanup to free allocated string when row is deleted
        // (LV_EVENT_DELETE is acceptable exception to "no lv_obj_add_event_cb" rule)
        lv_obj_add_event_cb(
            row,
            [](lv_event_t* e) {
                lv_obj_t* obj = lv_event_get_target_obj(e);
                char* data = static_cast<char*>(lv_obj_get_user_data(obj));
                if (data) {
                    lv_free(data);
                }
            },
            LV_EVENT_DELETE, nullptr);

        // Wire up role dropdown
        lv_obj_t* role_dropdown = lv_obj_find_by_name(row, "role_dropdown");
        if (role_dropdown) {
            // Set options with proper newline separators (XML can't do this)
            lv_dropdown_set_options(role_dropdown, "None\nRunout\nToolhead\nEntry");

            // Set current role
            lv_dropdown_set_selected(role_dropdown, static_cast<uint32_t>(sensor.role));

            // Store klipper_name reference for callback
            lv_obj_set_user_data(role_dropdown, klipper_name);

            // Wire up value change
            lv_obj_add_event_cb(
                role_dropdown,
                [](lv_event_t* e) {
                    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                    auto* klipper_name_ptr =
                        static_cast<const char*>(lv_obj_get_user_data(dropdown));
                    if (!klipper_name_ptr)
                        return;

                    int index = static_cast<int>(lv_dropdown_get_selected(dropdown));
                    auto role = static_cast<helix::FilamentSensorRole>(index);

                    auto& mgr = helix::FilamentSensorManager::instance();
                    mgr.set_sensor_role(klipper_name_ptr, role);
                    mgr.save_config();
                    spdlog::info("[SettingsPanel] Sensor {} role changed to {}", klipper_name_ptr,
                                 helix::role_to_config_string(role));
                },
                LV_EVENT_VALUE_CHANGED, nullptr);
        }

        // Wire up enable toggle
        lv_obj_t* enable_toggle = lv_obj_find_by_name(row, "enable_toggle");
        if (enable_toggle) {
            // Set current state
            if (sensor.enabled) {
                lv_obj_add_state(enable_toggle, LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(enable_toggle, LV_STATE_CHECKED);
            }

            // Store klipper_name reference for callback
            lv_obj_set_user_data(enable_toggle, klipper_name);

            // Wire up value change
            lv_obj_add_event_cb(
                enable_toggle,
                [](lv_event_t* e) {
                    auto* toggle = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                    auto* klipper_name_ptr = static_cast<const char*>(lv_obj_get_user_data(toggle));
                    if (!klipper_name_ptr)
                        return;

                    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);

                    auto& mgr = helix::FilamentSensorManager::instance();
                    mgr.set_sensor_enabled(klipper_name_ptr, enabled);
                    mgr.save_config();
                    spdlog::info("[SettingsPanel] Sensor {} enabled: {}", klipper_name_ptr,
                                 enabled ? "ON" : "OFF");
                },
                LV_EVENT_VALUE_CHANGED, nullptr);
        }

        spdlog::debug("[{}]   ✓ Created row for sensor: {}", get_name(), sensor.sensor_name);
    }
}

void SettingsPanel::handle_bed_mesh_clicked() {
    spdlog::debug("[{}] Bed Mesh clicked - opening visualization", get_name());

    // Create bed mesh panel on first access (lazy initialization)
    if (!bed_mesh_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating bed mesh visualization panel...", get_name());

        // Create from XML
        bed_mesh_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "bed_mesh_panel", nullptr));
        if (bed_mesh_panel_) {
            // Setup event handlers and renderer (class-based API)
            get_global_bed_mesh_panel().setup(bed_mesh_panel_, parent_screen_);

            // Initially hidden
            lv_obj_add_flag(bed_mesh_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Bed mesh visualization panel created", get_name());
        } else {
            spdlog::error("[{}] Failed to create bed mesh panel from XML", get_name());
            return;
        }
    }

    // Push bed mesh panel onto navigation history and show it
    if (bed_mesh_panel_) {
        ui_nav_push_overlay(bed_mesh_panel_);
    }
}

void SettingsPanel::handle_z_offset_clicked() {
    spdlog::debug("[{}] Z-Offset clicked - opening calibration panel", get_name());

    // Create Z-Offset calibration panel on first access (lazy initialization)
    if (!zoffset_cal_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating Z-Offset calibration panel...", get_name());

        // Create from XML
        zoffset_cal_panel_ = static_cast<lv_obj_t*>(
            lv_xml_create(parent_screen_, "calibration_zoffset_panel", nullptr));
        if (zoffset_cal_panel_) {
            // Setup event handlers (class-based API)
            MoonrakerClient* client = get_moonraker_client();
            get_global_zoffset_cal_panel().setup(zoffset_cal_panel_, parent_screen_, client);

            // Initially hidden
            lv_obj_add_flag(zoffset_cal_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Z-Offset calibration panel created", get_name());
        } else {
            spdlog::error("[{}] Failed to create Z-Offset panel from XML", get_name());
            return;
        }
    }

    // Push Z-Offset panel onto navigation history and show it
    if (zoffset_cal_panel_) {
        ui_nav_push_overlay(zoffset_cal_panel_);
    }
}

void SettingsPanel::handle_pid_tuning_clicked() {
    spdlog::debug("[{}] PID Tuning clicked - opening calibration panel", get_name());

    // Create PID calibration panel on first access (lazy initialization)
    if (!pid_cal_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating PID calibration panel...", get_name());

        // Create from XML
        pid_cal_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "calibration_pid_panel", nullptr));
        if (pid_cal_panel_) {
            // Setup event handlers (class-based API)
            MoonrakerClient* client = get_moonraker_client();
            get_global_pid_cal_panel().setup(pid_cal_panel_, parent_screen_, client);

            // Initially hidden
            lv_obj_add_flag(pid_cal_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] PID calibration panel created", get_name());
        } else {
            spdlog::error("[{}] Failed to create PID panel from XML", get_name());
            return;
        }
    }

    // Push PID panel onto navigation history and show it
    if (pid_cal_panel_) {
        ui_nav_push_overlay(pid_cal_panel_);
    }
}

void SettingsPanel::handle_network_clicked() {
    spdlog::debug("[{}] Network Settings clicked", get_name());

    auto& overlay = get_network_settings_overlay();

    if (!overlay.is_created()) {
        overlay.init_subjects();
        overlay.register_callbacks();
        overlay.create(parent_screen_);
    }

    overlay.show();
}

void SettingsPanel::handle_factory_reset_clicked() {
    spdlog::debug("[{}] Factory Reset clicked - showing confirmation dialog", get_name());

    // Create modal config
    ui_modal_config_t config = {.position = {.use_alignment = true, .alignment = LV_ALIGN_CENTER},
                                .backdrop_opa = 180,
                                .keyboard = nullptr,
                                .persistent = false,
                                .on_close = nullptr};

    // Factory reset is a destructive action - use ERROR severity with confirm/cancel
    const char* attrs[] = {"title", "Factory Reset", "message",
                           "This will reset all device and Klipper configurations to defaults. "
                           "This action cannot be undone.",
                           nullptr};

    ui_modal_configure(UI_MODAL_SEVERITY_ERROR, true, "Reset", "Cancel");
    factory_reset_dialog_ = ui_modal_show("modal_dialog", &config, attrs);

    if (!factory_reset_dialog_) {
        spdlog::error("[{}] Failed to create factory reset dialog", get_name());
        return;
    }

    // Set user_data on buttons so callbacks can identify which dialog this is
    lv_obj_t* cancel_btn = lv_obj_find_by_name(factory_reset_dialog_, "btn_secondary");
    if (cancel_btn) {
        lv_obj_set_user_data(cancel_btn, factory_reset_dialog_);
    }

    lv_obj_t* reset_btn = lv_obj_find_by_name(factory_reset_dialog_, "btn_primary");
    if (reset_btn) {
        lv_obj_set_user_data(reset_btn, factory_reset_dialog_);
    }
    // Event handlers already wired via modal_dialog XML event_cb elements

    spdlog::info("[{}] Factory reset dialog shown", get_name());
}

// ============================================================================
// STATIC TRAMPOLINES (XML event_cb pattern - use global singleton)
// ============================================================================

void SettingsPanel::on_dark_mode_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_dark_mode_changed");
    auto* toggle = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    get_global_settings_panel().handle_dark_mode_changed(enabled);
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_animations_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_animations_changed");
    auto* toggle = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    get_global_settings_panel().handle_animations_changed(enabled);
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_gcode_3d_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_gcode_3d_changed");
    auto* toggle = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    get_global_settings_panel().handle_gcode_3d_changed(enabled);
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_display_sleep_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_display_sleep_changed");
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int index = static_cast<int>(lv_dropdown_get_selected(dropdown));
    get_global_settings_panel().handle_display_sleep_changed(index);
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_led_light_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_led_light_changed");
    auto* toggle = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    get_global_settings_panel().handle_led_light_changed(enabled);
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_sounds_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_sounds_changed");
    auto* toggle = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    get_global_settings_panel().handle_sounds_changed(enabled);
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_estop_confirm_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_estop_confirm_changed");
    auto* toggle = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    get_global_settings_panel().handle_estop_confirm_changed(enabled);
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_display_settings_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_display_settings_clicked");
    get_global_settings_panel().handle_display_settings_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_filament_sensors_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_filament_sensors_clicked");
    get_global_settings_panel().handle_filament_sensors_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_bed_mesh_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_bed_mesh_clicked");
    get_global_settings_panel().handle_bed_mesh_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_z_offset_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_z_offset_clicked");
    get_global_settings_panel().handle_z_offset_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_pid_tuning_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_pid_tuning_clicked");
    get_global_settings_panel().handle_pid_tuning_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_network_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_network_clicked");
    get_global_settings_panel().handle_network_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_factory_reset_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_factory_reset_clicked");
    get_global_settings_panel().handle_factory_reset_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// STATIC TRAMPOLINES - MODAL DIALOGS AND OVERLAYS
// ============================================================================

// Generic modal dialog callbacks - these are registered globally and can be used
// by any modal_dialog instance. The specific action is determined by which dialog
// created the button and what user_data was attached to it.
void SettingsPanel::on_modal_primary_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_modal_primary_clicked");
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* dialog = static_cast<lv_obj_t*>(lv_obj_get_user_data(btn));

    // Check if this is the theme restart dialog
    auto& panel = get_global_settings_panel();
    if (dialog == panel.theme_restart_dialog_) {
        spdlog::info("[SettingsPanel] User requested app restart for theme change");
        // Fork new process with modified args: removes --dark/--light,
        // forces -p settings. Theme is read from saved config.
        app_request_restart_for_theme();
    }
    // Check if this is the factory reset dialog
    else if (dialog == panel.factory_reset_dialog_) {
        spdlog::warn("[SettingsPanel] Factory reset confirmed - resetting config!");

        // Get config instance and reset
        Config* config = Config::get_instance();
        if (config) {
            config->reset_to_defaults();
            config->save();
            spdlog::info("[SettingsPanel] Config reset to defaults");
        }

        // Hide the dialog
        if (dialog) {
            ui_modal_hide(dialog);
        }

        // TODO: In production, this would restart the application
        // or transition to the setup wizard. For now, just log.
        spdlog::info("[SettingsPanel] Device should restart or show wizard now");
    }
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_modal_secondary_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_modal_secondary_clicked");
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* dialog = static_cast<lv_obj_t*>(lv_obj_get_user_data(btn));

    // Check if this is the theme restart dialog
    auto& panel = get_global_settings_panel();
    if (dialog == panel.theme_restart_dialog_) {
        if (dialog) {
            lv_obj_add_flag(dialog, LV_OBJ_FLAG_HIDDEN);
        }
        spdlog::debug("[SettingsPanel] Theme restart dialog dismissed (will restart later)");
    }
    // Check if this is the factory reset dialog
    else if (dialog == panel.factory_reset_dialog_) {
        if (dialog) {
            ui_modal_hide(dialog);
        }
        spdlog::debug("[SettingsPanel] Factory reset cancelled");
    }
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_modal_backdrop_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_modal_backdrop_clicked");
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* original = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // Only dismiss if clicking the backdrop itself (not a child element)
    auto& panel = get_global_settings_panel();
    if (target == original && target == panel.theme_restart_dialog_) {
        lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
        spdlog::debug("[SettingsPanel] Theme restart dialog dismissed (backdrop)");
    }
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_restart_later_clicked(lv_event_t* /* e */) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_restart_later_clicked");
    auto& panel = get_global_settings_panel();
    if (panel.restart_prompt_dialog_) {
        lv_obj_add_flag(panel.restart_prompt_dialog_, LV_OBJ_FLAG_HIDDEN);
    }
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_restart_now_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_restart_now_clicked");
    spdlog::info("[SettingsPanel] User requested restart");
    // Exit the application - user will restart manually
    // In a real embedded system, this would trigger a system restart
    exit(0);
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_header_back_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_header_back_clicked");
    ui_nav_go_back();
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_brightness_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_brightness_changed");
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int value = lv_slider_get_value(slider);
    SettingsManager::instance().set_brightness(value);

    // Update subject (label binding happens in XML)
    auto& panel_ref = get_global_settings_panel();
    snprintf(panel_ref.brightness_value_buf_, sizeof(panel_ref.brightness_value_buf_), "%d%%",
             value);
    lv_subject_copy_string(&panel_ref.brightness_value_subject_, panel_ref.brightness_value_buf_);
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================

static std::unique_ptr<SettingsPanel> g_settings_panel;

SettingsPanel& get_global_settings_panel() {
    if (!g_settings_panel) {
        g_settings_panel = std::make_unique<SettingsPanel>(get_printer_state(), nullptr);
    }
    return *g_settings_panel;
}
