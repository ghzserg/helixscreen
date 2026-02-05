// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_settings.h"

#include "ui_ams_device_operations_overlay.h"
#include "ui_ams_spoolman_overlay.h"
#include "ui_emergency_stop.h"
#include "ui_event_safety.h"
#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_overlay_network_settings.h"
#include "ui_panel_memory_stats.h"
#include "ui_settings_display.h"
#include "ui_settings_hardware_health.h"
#include "ui_settings_machine_limits.h"
#include "ui_settings_macro_buttons.h"
#include "ui_settings_plugins.h"
#include "ui_settings_sensors.h"
#include "ui_severity_card.h"
#include "ui_toast.h"
#include "ui_touch_calibration_overlay.h"
#include "ui_update_queue.h"
#include "ui_utils.h"

#include "app_globals.h"
#include "config.h"
#include "filament_sensor_manager.h"
#include "format_utils.h"
#include "hardware_validator.h"
#include "helix_version.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "moonraker_api.h"
#include "moonraker_client.h"
#include "printer_state.h"
#include "runtime_config.h"
#include "settings_manager.h"
#include "sound_manager.h"
#include "standard_macros.h"
#include "static_panel_registry.h"
#include "theme_manager.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <memory>

// ============================================================================
// CONSTRUCTOR
// ============================================================================

SettingsPanel::SettingsPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    spdlog::trace("[{}] Constructor", get_name());
}

SettingsPanel::~SettingsPanel() {
    // Applying [L041]: deinit_subjects() as first line in destructor
    deinit_subjects();

    // Widget-bound observers (from lv_label_bind_text) are automatically removed
    // by LVGL when the bound widget is deleted. We avoid manual lv_observer_remove()
    // because the observers may already be removed (stale pointers) if the widget
    // tree was cleaned up. The subjects (in PrinterState) outlive this panel and
    // will handle any remaining observer cleanup via lv_subject_deinit().
    if (lv_is_initialized()) {
        klipper_version_observer_ = nullptr;
        moonraker_version_observer_ = nullptr;

        // Unregister overlay callbacks to prevent dangling 'this' in callbacks
        auto& nav = NavigationManager::instance();
        if (factory_reset_dialog_) {
            nav.unregister_overlay_close_callback(factory_reset_dialog_);
        }
        if (theme_restart_dialog_) {
            nav.unregister_overlay_close_callback(theme_restart_dialog_);
        }
    }
    // Note: Don't log here - spdlog may be destroyed during static destruction
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

// Static callback for display dim dropdown
static void on_display_dim_dropdown_changed(lv_event_t* e) {
    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int index = static_cast<int>(lv_dropdown_get_selected(dropdown));
    int seconds = SettingsManager::index_to_dim_seconds(index);
    spdlog::info("[SettingsPanel] Display dim changed: index {} = {}s", index, seconds);
    SettingsManager::instance().set_display_dim_sec(seconds);
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

// Static callback for time format dropdown
static void on_time_format_changed(lv_event_t* e) {
    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int index = static_cast<int>(lv_dropdown_get_selected(dropdown));
    auto format = static_cast<TimeFormat>(index);
    spdlog::info("[SettingsPanel] Time format changed: {} ({})", index,
                 index == 0 ? "12 Hour" : "24 Hour");
    SettingsManager::instance().set_time_format(format);
}

// Static callback for language dropdown
static void on_language_changed(lv_event_t* e) {
    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int index = static_cast<int>(lv_dropdown_get_selected(dropdown));
    std::string lang_code = SettingsManager::language_index_to_code(index);
    spdlog::info("[SettingsPanel] Language changed: index {} ({})", index, lang_code);
    SettingsManager::instance().set_language_by_index(index);
}

// Static callback for version row tap (memory debug toggle via 7-tap secret)
// This provides a touch-based way to enable memory debugging on Pi (no keyboard)
// Like Android's "tap build number 7 times" to enable developer mode
static constexpr int kSecretTapCount = 7;
static constexpr uint32_t kSecretTapTimeoutMs = 2000; // Reset counter after 2s of no taps

static void on_version_clicked(lv_event_t*) {
    static int tap_count = 0;
    static uint32_t last_tap_time = 0;

    uint32_t now = lv_tick_get();

    // Reset counter if too much time has passed
    if (now - last_tap_time > kSecretTapTimeoutMs) {
        tap_count = 0;
    }
    last_tap_time = now;
    tap_count++;

    int remaining = kSecretTapCount - tap_count;

    if (remaining > 0 && remaining <= 3) {
        // Show countdown when close
        ui_toast_show(ToastSeverity::INFO,
                      fmt::format("{} more tap{} to toggle memory debug", remaining,
                                  remaining == 1 ? "" : "s")
                          .c_str(),
                      1000);
    } else if (remaining == 0) {
        // Toggle memory debug
        MemoryStatsOverlay::instance().toggle();
        bool is_visible = MemoryStatsOverlay::instance().is_visible();
        ui_toast_show(ToastSeverity::SUCCESS,
                      is_visible ? lv_tr("Memory debug: ON") : lv_tr("Memory debug: OFF"), 1500);
        spdlog::info("[SettingsPanel] Memory debug toggled via 7-tap secret: {}",
                     is_visible ? "ON" : "OFF");
        tap_count = 0; // Reset for next time
    }
}

// Note: Sensors overlay callbacks are now in SensorSettingsOverlay class
// See ui_settings_sensors.cpp
// Note: Macro Buttons overlay callbacks are now in MacroButtonsOverlay class
// See ui_settings_macro_buttons.cpp

// ============================================================================
// MODAL DIALOG STATIC CALLBACKS (XML event_cb)
// ============================================================================

static void on_theme_restart_confirm(lv_event_t* e) {
    (void)e;
    spdlog::info("[SettingsPanel] User confirmed theme restart");
    app_request_restart_for_theme();
}

static void on_theme_restart_dismiss(lv_event_t* e) {
    (void)e;
    spdlog::info("[SettingsPanel] User dismissed theme restart");
    auto& panel = get_global_settings_panel();
    if (panel.theme_restart_dialog_) {
        ui_nav_go_back(); // Animation + callback will handle cleanup
    }
}

static void on_factory_reset_confirm(lv_event_t* e) {
    (void)e;
    spdlog::info("[SettingsPanel] User confirmed factory reset");
    auto& panel = get_global_settings_panel();
    panel.perform_factory_reset();
}

static void on_factory_reset_cancel(lv_event_t* e) {
    (void)e;
    spdlog::info("[SettingsPanel] User cancelled factory reset");
    auto& panel = get_global_settings_panel();
    if (panel.factory_reset_dialog_) {
        ui_nav_go_back(); // Animation + callback will handle cleanup
    }
}

void SettingsPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // Initialize SettingsManager subjects (for reactive binding)
    SettingsManager::instance().init_subjects();

    // Note: LED config loading moved to MoonrakerManager::create_api() for centralized init

    // Note: brightness_value subject is now managed by DisplaySettingsOverlay
    // See ui_settings_display.cpp

    // Initialize info row subjects (for reactive binding)
    UI_MANAGED_SUBJECT_STRING(version_value_subject_, version_value_buf_, "—", "version_value",
                              subjects_);

    UI_MANAGED_SUBJECT_STRING(printer_value_subject_, printer_value_buf_, "—", "printer_value",
                              subjects_);

    UI_MANAGED_SUBJECT_STRING(printer_host_value_subject_, printer_host_value_buf_, "—",
                              "printer_host_value", subjects_);

    UI_MANAGED_SUBJECT_STRING(print_hours_value_subject_, print_hours_value_buf_, "—",
                              "print_hours_value", subjects_);

    // Initialize visibility subjects (controls which settings are shown)
    // Touch calibration: show on touch displays (non-SDL) OR in test mode (for testing on desktop)
#ifdef HELIX_DISPLAY_SDL
    bool show_touch_cal = get_runtime_config()->is_test_mode();
#else
    bool show_touch_cal = true;
#endif
    lv_subject_init_int(&show_touch_calibration_subject_, show_touch_cal ? 1 : 0);
    subjects_.register_subject(&show_touch_calibration_subject_);
    lv_xml_register_subject(nullptr, "show_touch_calibration", &show_touch_calibration_subject_);

    // Touch calibration status - show "Calibrated" or "Not calibrated" in row description
    Config* config = Config::get_instance();
    bool is_calibrated = config && config->get<bool>("/input/calibration/valid", false);
    const char* status_text = is_calibrated ? "Calibrated" : "Not calibrated";
    UI_MANAGED_SUBJECT_STRING(touch_cal_status_subject_, touch_cal_status_buf_, status_text,
                              "touch_cal_status", subjects_);

    // Register XML event callbacks for dropdowns (already in XML)
    lv_xml_register_event_cb(nullptr, "on_completion_alert_changed",
                             on_completion_alert_dropdown_changed);
    lv_xml_register_event_cb(nullptr, "on_display_dim_changed", on_display_dim_dropdown_changed);
    lv_xml_register_event_cb(nullptr, "on_display_sleep_changed",
                             on_display_sleep_dropdown_changed);
    lv_xml_register_event_cb(nullptr, "on_bed_mesh_mode_changed", on_bed_mesh_mode_changed);
    lv_xml_register_event_cb(nullptr, "on_gcode_mode_changed", on_gcode_mode_changed);
    lv_xml_register_event_cb(nullptr, "on_time_format_changed", on_time_format_changed);
    lv_xml_register_event_cb(nullptr, "on_language_changed", on_language_changed);
    lv_xml_register_event_cb(nullptr, "on_version_clicked", on_version_clicked);

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

    // Note: Sensors overlay callbacks are now handled by SensorSettingsOverlay
    // See ui_settings_sensors.h
    helix::settings::get_sensor_settings_overlay().register_callbacks();

    // Note: Display Settings overlay callbacks are now handled by DisplaySettingsOverlay
    // See ui_settings_display.h

    lv_xml_register_event_cb(nullptr, "on_ams_settings_clicked", on_ams_settings_clicked);

    // Note: AMS Settings now goes directly to Device Operations overlay
    // See ui_ams_device_operations_overlay.h

    lv_xml_register_event_cb(nullptr, "on_spoolman_settings_clicked", on_spoolman_settings_clicked);

    // Note: Spoolman overlay for weight sync settings
    // See ui_ams_spoolman_overlay.h

    lv_xml_register_event_cb(nullptr, "on_macro_buttons_clicked", on_macro_buttons_clicked);

    // Note: Macro Buttons overlay callbacks are now handled by MacroButtonsOverlay
    // See ui_settings_macro_buttons.h

    lv_xml_register_event_cb(nullptr, "on_machine_limits_clicked", on_machine_limits_clicked);

    // Note: Machine limits overlay callbacks and subjects are now handled by MachineLimitsOverlay
    // See ui_settings_machine_limits.h

    lv_xml_register_event_cb(nullptr, "on_network_clicked", on_network_clicked);
    lv_xml_register_event_cb(nullptr, "on_factory_reset_clicked", on_factory_reset_clicked);
    lv_xml_register_event_cb(nullptr, "on_hardware_health_clicked", on_hardware_health_clicked);
    lv_xml_register_event_cb(nullptr, "on_plugins_clicked", on_plugins_clicked);

    // Register XML event callbacks for overlays
    lv_xml_register_event_cb(nullptr, "on_restart_later_clicked", on_restart_later_clicked);
    lv_xml_register_event_cb(nullptr, "on_restart_now_clicked", on_restart_now_clicked);

    // Register modal dialog callbacks (self-contained XML components)
    lv_xml_register_event_cb(nullptr, "on_theme_restart_confirm", on_theme_restart_confirm);
    lv_xml_register_event_cb(nullptr, "on_theme_restart_dismiss", on_theme_restart_dismiss);
    lv_xml_register_event_cb(nullptr, "on_factory_reset_confirm", on_factory_reset_confirm);
    lv_xml_register_event_cb(nullptr, "on_factory_reset_cancel", on_factory_reset_cancel);
    lv_xml_register_event_cb(nullptr, "on_header_back_clicked", on_header_back_clicked);
    // Note: on_brightness_changed is now handled by DisplaySettingsOverlay

    // Note: BedMeshPanel subjects are initialized in main.cpp during startup

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void SettingsPanel::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[{}] Deinitializing subjects", get_name());

    // Deinit all subjects via SubjectManager (handles 7 string subjects)
    subjects_.deinit_all();

    subjects_initialized_ = false;
    spdlog::debug("[{}] Subjects deinitialized", get_name());
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
    // Event handler wired via XML <event_cb>, sync toggle with actual printer LED state
    lv_obj_t* led_light_row = lv_obj_find_by_name(panel_, "row_led_light");
    if (led_light_row) {
        led_light_switch_ = lv_obj_find_by_name(led_light_row, "toggle");
        if (led_light_switch_) {
            // Sync toggle with actual printer LED state via observer
            // Applying [L020]: Use ObserverGuard for cleanup
            led_state_observer_ = ObserverGuard(
                printer_state_.get_led_state_subject(),
                [](lv_observer_t* obs, lv_subject_t* subj) {
                    auto* self = static_cast<SettingsPanel*>(lv_observer_get_user_data(obs));
                    if (self && self->led_light_switch_) {
                        bool on = lv_subject_get_int(subj) != 0;
                        if (on) {
                            lv_obj_add_state(self->led_light_switch_, LV_STATE_CHECKED);
                        } else {
                            lv_obj_remove_state(self->led_light_switch_, LV_STATE_CHECKED);
                        }
                    }
                },
                this);
            spdlog::debug("[{}]   ✓ LED light toggle (observing printer state)", get_name());
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
    // Event handler wired via XML <event_cb>, just set initial value here (options set in XML)
    lv_obj_t* completion_row = lv_obj_find_by_name(panel_, "row_completion_alert");
    if (completion_row) {
        completion_alert_dropdown_ = lv_obj_find_by_name(completion_row, "dropdown");
        if (completion_alert_dropdown_) {
            auto mode = settings.get_completion_alert_mode();
            lv_dropdown_set_selected(completion_alert_dropdown_, static_cast<uint32_t>(mode));
            spdlog::debug("[{}]   ✓ Completion alert dropdown (mode={})", get_name(),
                          static_cast<int>(mode));
        }
    }

    // === Language Dropdown ===
    // Event handler wired via XML <event_cb>, options populated from SettingsManager
    lv_obj_t* language_row = lv_obj_find_by_name(panel_, "row_language");
    if (language_row) {
        language_dropdown_ = lv_obj_find_by_name(language_row, "dropdown");
        if (language_dropdown_) {
            lv_dropdown_set_options(language_dropdown_, SettingsManager::get_language_options());
            int lang_index = settings.get_language_index();
            lv_dropdown_set_selected(language_dropdown_, static_cast<uint32_t>(lang_index));
            spdlog::debug("[{}]   ✓ Language dropdown (index={})", get_name(), lang_index);
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

    // === Hardware Health Row (reactive label binding) ===
    lv_obj_t* hardware_health_row = lv_obj_find_by_name(panel_, "row_hardware_health");
    if (hardware_health_row) {
        lv_obj_t* label = lv_obj_find_by_name(hardware_health_row, "label");
        if (label) {
            // Bind to subject with %s format (string passthrough)
            lv_label_bind_text(label, get_printer_state().get_hardware_issues_label_subject(),
                               "%s");
            spdlog::debug("[{}]   ✓ Hardware health row with reactive label", get_name());
        }
    }

    // === Touch Calibration Row (reactive description binding) ===
    lv_obj_t* touch_cal_row = lv_obj_find_by_name(panel_, "row_touch_calibration");
    if (touch_cal_row) {
        lv_obj_t* description = lv_obj_find_by_name(touch_cal_row, "description");
        if (description) {
            // Bind to subject for "Calibrated" / "Not calibrated" status
            lv_label_bind_text(description, &touch_cal_status_subject_, "%s");
            spdlog::debug("[{}]   ✓ Touch calibration row with reactive description", get_name());
        }
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

    // === Printer Host (from config - shows IP/hostname:port) ===
    lv_obj_t* host_row = lv_obj_find_by_name(panel_, "row_printer_host");
    if (host_row) {
        lv_obj_t* host_value = lv_obj_find_by_name(host_row, "value");
        if (host_value) {
            Config* config = Config::get_instance();
            std::string host = config->get<std::string>(config->df() + "moonraker_host", "");
            int port = config->get<int>(config->df() + "moonraker_port", 7125);

            if (!host.empty()) {
                std::string host_display = host + ":" + std::to_string(port);
                lv_subject_copy_string(&printer_host_value_subject_, host_display.c_str());
                spdlog::debug("[{}]   ✓ Printer Host: {}", get_name(), host_display);
            }
        }
    }

    // === Klipper Version (reactive binding from PrinterState) ===
    lv_obj_t* klipper_row = lv_obj_find_by_name(panel_, "row_klipper");
    if (klipper_row) {
        klipper_value_ = lv_obj_find_by_name(klipper_row, "value");
        if (klipper_value_) {
            // Bind to reactive subject - updates automatically after discovery
            // Track observer for cleanup in destructor
            klipper_version_observer_ = lv_label_bind_text(
                klipper_value_, printer_state_.get_klipper_version_subject(), "%s");
            spdlog::debug("[{}]   ✓ Klipper version bound to subject", get_name());
        }
    }

    // === Moonraker Version (reactive binding from PrinterState) ===
    lv_obj_t* moonraker_row = lv_obj_find_by_name(panel_, "row_moonraker");
    if (moonraker_row) {
        moonraker_value_ = lv_obj_find_by_name(moonraker_row, "value");
        if (moonraker_value_) {
            // Bind to reactive subject - updates automatically after discovery
            // Track observer for cleanup in destructor
            moonraker_version_observer_ = lv_label_bind_text(
                moonraker_value_, printer_state_.get_moonraker_version_subject(), "%s");
            spdlog::debug("[{}]   ✓ Moonraker version bound to subject", get_name());
        }
    }

    // === Total Print Hours (from Moonraker history totals) ===
    if (api_) {
        api_->get_history_totals(
            [this](const PrintHistoryTotals& totals) {
                // Format total_time (seconds) as hours
                std::string formatted = helix::fmt::duration(static_cast<int>(totals.total_time));
                ui_queue_update([this, formatted]() {
                    if (subjects_initialized_) {
                        lv_subject_copy_string(&print_hours_value_subject_, formatted.c_str());
                        spdlog::debug("[{}]   ✓ Print hours: {}", get_name(), formatted);
                    }
                });
            },
            [this](const MoonrakerError& err) {
                spdlog::warn("[{}] Failed to fetch print hours: {}", get_name(), err.message);
            });
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

        // Create self-contained theme_restart_modal component
        // Callbacks are already wired via XML event_cb elements
        theme_restart_dialog_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "theme_restart_modal", nullptr));

        if (theme_restart_dialog_) {
            // Start hidden
            lv_obj_add_flag(theme_restart_dialog_, LV_OBJ_FLAG_HIDDEN);

            // Register close callback to delete dialog when animation completes
            NavigationManager::instance().register_overlay_close_callback(
                theme_restart_dialog_, [this]() {
                    lv_obj_safe_delete(theme_restart_dialog_);
                    theme_restart_dialog_ = nullptr;
                });

            spdlog::info("[{}] Theme restart dialog created", get_name());
        } else {
            spdlog::error("[{}] Failed to create theme restart dialog", get_name());
            return;
        }
    }

    // Show the dialog via navigation stack
    if (theme_restart_dialog_) {
        ui_nav_push_overlay(theme_restart_dialog_);
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
    spdlog::debug("[{}] Display Settings clicked - delegating to DisplaySettingsOverlay",
                  get_name());

    auto& overlay = helix::settings::get_display_settings_overlay();
    overlay.show(parent_screen_);
}

void SettingsPanel::handle_filament_sensors_clicked() {
    spdlog::debug("[{}] Sensors clicked - delegating to SensorSettingsOverlay", get_name());

    auto& overlay = helix::settings::get_sensor_settings_overlay();
    overlay.show(parent_screen_);
}

void SettingsPanel::handle_ams_settings_clicked() {
    spdlog::debug("[{}] AMS Settings clicked - opening Device Operations", get_name());

    auto& overlay = helix::ui::get_ams_device_operations_overlay();
    if (!overlay.are_subjects_initialized()) {
        overlay.init_subjects();
        overlay.register_callbacks();
    }
    overlay.show(parent_screen_);
}

void SettingsPanel::handle_spoolman_settings_clicked() {
    spdlog::debug("[{}] Spoolman Settings clicked - opening Spoolman overlay", get_name());

    auto& overlay = helix::ui::get_ams_spoolman_overlay();
    if (!overlay.are_subjects_initialized()) {
        overlay.init_subjects();
        overlay.register_callbacks();
    }
    MoonrakerClient* client = get_moonraker_client();
    if (client) {
        overlay.set_client(client);
    }
    overlay.show(parent_screen_);
}

void SettingsPanel::handle_macro_buttons_clicked() {
    spdlog::debug("[{}] Macro Buttons clicked - delegating to MacroButtonsOverlay", get_name());

    auto& overlay = helix::settings::get_macro_buttons_overlay();
    overlay.show(parent_screen_);
}

// Note: populate_macro_dropdowns() moved to MacroButtonsOverlay::populate_dropdowns()
// See ui_settings_macro_buttons.cpp
// Note: populate_sensor_list() moved to SensorSettingsOverlay::populate_switch_sensors()
// See ui_settings_sensors.cpp

void SettingsPanel::handle_machine_limits_clicked() {
    spdlog::debug("[{}] Machine Limits clicked - delegating to MachineLimitsOverlay", get_name());

    auto& overlay = helix::settings::get_machine_limits_overlay();
    overlay.set_api(api_);
    overlay.show(parent_screen_);
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

void SettingsPanel::handle_touch_calibration_clicked() {
    spdlog::debug("[{}] Touch Calibration clicked", get_name());

    auto& overlay = helix::ui::get_touch_calibration_overlay();

    if (!overlay.is_created()) {
        overlay.init_subjects();
        overlay.register_callbacks();
        overlay.create(parent_screen_);
    }

    // Auto-start: skip IDLE state since user explicitly chose to recalibrate
    overlay.set_auto_start(true);
    overlay.show([this](bool success) {
        if (success) {
            // Update status to "Calibrated" when calibration completes successfully
            lv_subject_copy_string(&touch_cal_status_subject_, "Calibrated");
            spdlog::info("[{}] Touch calibration completed - updated status", get_name());
        }
    });
}

void SettingsPanel::handle_factory_reset_clicked() {
    spdlog::debug("[{}] Factory Reset clicked - showing confirmation dialog", get_name());

    // Create dialog on first use (lazy initialization)
    if (!factory_reset_dialog_ && parent_screen_) {
        spdlog::debug("[{}] Creating factory reset dialog...", get_name());

        // Create self-contained factory_reset_modal component
        // Callbacks are already wired via XML event_cb elements
        factory_reset_dialog_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "factory_reset_modal", nullptr));

        if (factory_reset_dialog_) {
            // Start hidden
            lv_obj_add_flag(factory_reset_dialog_, LV_OBJ_FLAG_HIDDEN);

            // Register close callback to delete dialog when animation completes
            NavigationManager::instance().register_overlay_close_callback(
                factory_reset_dialog_, [this]() {
                    lv_obj_safe_delete(factory_reset_dialog_);
                    factory_reset_dialog_ = nullptr;
                });

            spdlog::info("[{}] Factory reset dialog created", get_name());
        } else {
            spdlog::error("[{}] Failed to create factory reset dialog", get_name());
            return;
        }
    }

    // Show the dialog via navigation stack
    if (factory_reset_dialog_) {
        ui_nav_push_overlay(factory_reset_dialog_);
    }
}

void SettingsPanel::handle_plugins_clicked() {
    spdlog::debug("[{}] Plugins clicked - opening overlay", get_name());

    auto& overlay = get_settings_plugins_overlay();

    if (!overlay.are_subjects_initialized()) {
        overlay.init_subjects();
        overlay.register_callbacks();
        overlay.create(parent_screen_);
    }

    // Show the overlay via navigation stack
    if (overlay.get_root()) {
        NavigationManager::instance().register_overlay_instance(overlay.get_root(), &overlay);
        ui_nav_push_overlay(overlay.get_root());
    }
}

void SettingsPanel::perform_factory_reset() {
    spdlog::warn("[{}] Performing factory reset - resetting config!", get_name());

    // Get config instance and reset
    Config* config = Config::get_instance();
    if (config) {
        config->reset_to_defaults();
        config->save();
        spdlog::info("[{}] Config reset to defaults", get_name());
    }

    // Hide the dialog - animation + callback will handle cleanup
    if (factory_reset_dialog_) {
        ui_nav_go_back();
    }

    // Show confirmation toast
    ui_toast_show(ToastSeverity::SUCCESS, lv_tr("Settings reset to defaults"), 2000);

    // TODO: In production, this would restart the application
    // or transition to the setup wizard. For now, just log.
    spdlog::info("[{}] Device should restart or show wizard now", get_name());
}

void SettingsPanel::handle_hardware_health_clicked() {
    spdlog::debug("[{}] Hardware Health clicked - delegating to HardwareHealthOverlay", get_name());

    auto& overlay = helix::settings::get_hardware_health_overlay();
    overlay.set_printer_state(&printer_state_);
    overlay.show(parent_screen_);
}

// Note: populate_hardware_issues() moved to HardwareHealthOverlay
// See ui_settings_hardware_health.cpp

// Note: handle_hardware_action() and related methods moved to HardwareHealthOverlay
// See ui_settings_hardware_health.cpp

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

void SettingsPanel::on_ams_settings_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_ams_settings_clicked");
    get_global_settings_panel().handle_ams_settings_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_spoolman_settings_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_spoolman_settings_clicked");
    get_global_settings_panel().handle_spoolman_settings_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_macro_buttons_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_macro_buttons_clicked");
    get_global_settings_panel().handle_macro_buttons_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_machine_limits_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_machine_limits_clicked");
    get_global_settings_panel().handle_machine_limits_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_network_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_network_clicked");
    get_global_settings_panel().handle_network_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_touch_calibration_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_touch_calibration_clicked");
    get_global_settings_panel().handle_touch_calibration_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_factory_reset_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_factory_reset_clicked");
    get_global_settings_panel().handle_factory_reset_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_hardware_health_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_hardware_health_clicked");
    get_global_settings_panel().handle_hardware_health_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_plugins_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_plugins_clicked");
    get_global_settings_panel().handle_plugins_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// STATIC TRAMPOLINES - OVERLAYS
// ============================================================================

// Note: Machine limits overlay callbacks are now in MachineLimitsOverlay class
// See ui_settings_machine_limits.cpp

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

// Note: on_brightness_changed is now handled by DisplaySettingsOverlay
// See ui_settings_display.cpp

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================

static std::unique_ptr<SettingsPanel> g_settings_panel;

SettingsPanel& get_global_settings_panel() {
    if (!g_settings_panel) {
        g_settings_panel = std::make_unique<SettingsPanel>(get_printer_state(), nullptr);
        StaticPanelRegistry::instance().register_destroy("SettingsPanel",
                                                         []() { g_settings_panel.reset(); });
    }
    return *g_settings_panel;
}

// Register callbacks BEFORE settings_panel.xml registration per [L013]
void register_settings_panel_callbacks() {
    spdlog::debug("[SettingsPanel] Registering XML callbacks for settings_panel.xml");

    // Toggle callbacks used in settings_panel.xml
    lv_xml_register_event_cb(nullptr, "on_animations_changed",
                             SettingsPanel::on_animations_changed);
    lv_xml_register_event_cb(nullptr, "on_gcode_3d_changed", SettingsPanel::on_gcode_3d_changed);
    lv_xml_register_event_cb(nullptr, "on_led_light_changed", SettingsPanel::on_led_light_changed);
    lv_xml_register_event_cb(nullptr, "on_sounds_changed", SettingsPanel::on_sounds_changed);
    lv_xml_register_event_cb(nullptr, "on_estop_confirm_changed",
                             SettingsPanel::on_estop_confirm_changed);

    // Action row callbacks used in settings_panel.xml
    lv_xml_register_event_cb(nullptr, "on_display_settings_clicked",
                             SettingsPanel::on_display_settings_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_sensors_clicked",
                             SettingsPanel::on_filament_sensors_clicked);
    lv_xml_register_event_cb(nullptr, "on_macro_buttons_clicked",
                             SettingsPanel::on_macro_buttons_clicked);
    lv_xml_register_event_cb(nullptr, "on_machine_limits_clicked",
                             SettingsPanel::on_machine_limits_clicked);
    lv_xml_register_event_cb(nullptr, "on_network_clicked", SettingsPanel::on_network_clicked);
    lv_xml_register_event_cb(nullptr, "on_touch_calibration_clicked",
                             SettingsPanel::on_touch_calibration_clicked);
    lv_xml_register_event_cb(nullptr, "on_factory_reset_clicked",
                             SettingsPanel::on_factory_reset_clicked);
    lv_xml_register_event_cb(nullptr, "on_hardware_health_clicked",
                             SettingsPanel::on_hardware_health_clicked);
}
