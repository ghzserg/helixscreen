// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_settings.h"

#include "ui_emergency_stop.h"
#include "ui_event_safety.h"
#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_overlay_network_settings.h"
#include "ui_panel_memory_stats.h"
#include "ui_severity_card.h"
#include "ui_settings_plugins.h"
#include "ui_theme.h"
#include "ui_toast.h"

#include "app_globals.h"
#include "config.h"
#include "filament_sensor_manager.h"
#include "hardware_validator.h"
#include "helix_version.h"
#include "moonraker_client.h"
#include "printer_state.h"
#include "settings_manager.h"
#include "sound_manager.h"
#include "standard_macros.h"
#include "static_panel_registry.h"

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

    // Remove observers BEFORE labels are destroyed to prevent use-after-free
    // The subjects (in PrinterState) outlive this panel, so observers must be
    // explicitly removed or LVGL will try to update destroyed labels
    // Check lv_is_initialized() to handle static destruction order safely
    if (lv_is_initialized()) {
        if (klipper_version_observer_) {
            lv_observer_remove(klipper_version_observer_);
            klipper_version_observer_ = nullptr;
        }
        if (moonraker_version_observer_) {
            lv_observer_remove(moonraker_version_observer_);
            moonraker_version_observer_ = nullptr;
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

// ============================================================================
// MACRO BUTTONS OVERLAY STATIC CALLBACKS
// ============================================================================

// Helper: Get slot name from selected quick button dropdown index
// Index 0 = "(Empty)", then slot display names in order
static std::string quick_button_index_to_slot_name(int index) {
    if (index == 0) {
        return ""; // Empty - no slot assigned
    }
    // Map index-1 to StandardMacroSlot enum order
    const auto& slots = StandardMacros::instance().all();
    if (index - 1 < static_cast<int>(slots.size())) {
        return slots[index - 1].slot_name;
    }
    return "";
}

// Helper: Get selected macro name from standard macro dropdown
// The dropdown options are: "(Auto: X)" or "(Empty)", then sorted macro names
// user_data on dropdown contains the StandardMacroSlot as int
static std::string get_selected_macro_from_dropdown(lv_obj_t* dropdown) {
    char buf[64];
    lv_dropdown_get_selected_str(dropdown, buf, sizeof(buf));
    std::string selected(buf);

    // Check for special options
    if (selected.find("(Auto") == 0 || selected.find("(Empty)") == 0) {
        return ""; // Clear configured macro, use auto-detection
    }

    return selected; // Return the macro name
}

// Quick button 1 dropdown changed
static void on_quick_button_1_changed(lv_event_t* e) {
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int index = static_cast<int>(lv_dropdown_get_selected(dropdown));
    std::string slot_name = quick_button_index_to_slot_name(index);

    Config* config = Config::get_instance();
    if (config) {
        config->set<std::string>("/standard_macros/quick_button_1", slot_name);
        config->save();
    }

    spdlog::info("[SettingsPanel] Quick button 1 set to: {}",
                 slot_name.empty() ? "(empty)" : slot_name);
}

// Quick button 2 dropdown changed
static void on_quick_button_2_changed(lv_event_t* e) {
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int index = static_cast<int>(lv_dropdown_get_selected(dropdown));
    std::string slot_name = quick_button_index_to_slot_name(index);

    Config* config = Config::get_instance();
    if (config) {
        config->set<std::string>("/standard_macros/quick_button_2", slot_name);
        config->save();
    }

    spdlog::info("[SettingsPanel] Quick button 2 set to: {}",
                 slot_name.empty() ? "(empty)" : slot_name);
}

// Standard macro slot dropdown changed - template for all slots
static void handle_standard_macro_changed(StandardMacroSlot slot, lv_event_t* e) {
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    std::string macro = get_selected_macro_from_dropdown(dropdown);

    StandardMacros::instance().set_macro(slot, macro);

    const auto& info = StandardMacros::instance().get(slot);
    spdlog::info("[SettingsPanel] {} macro set to: {} (resolved: {})", info.display_name,
                 macro.empty() ? "(auto)" : macro, info.get_macro());
}

static void on_load_filament_changed(lv_event_t* e) {
    handle_standard_macro_changed(StandardMacroSlot::LoadFilament, e);
}

static void on_unload_filament_changed(lv_event_t* e) {
    handle_standard_macro_changed(StandardMacroSlot::UnloadFilament, e);
}

static void on_purge_changed(lv_event_t* e) {
    handle_standard_macro_changed(StandardMacroSlot::Purge, e);
}

static void on_pause_changed(lv_event_t* e) {
    handle_standard_macro_changed(StandardMacroSlot::Pause, e);
}

static void on_resume_changed(lv_event_t* e) {
    handle_standard_macro_changed(StandardMacroSlot::Resume, e);
}

static void on_cancel_changed(lv_event_t* e) {
    handle_standard_macro_changed(StandardMacroSlot::Cancel, e);
}

static void on_bed_level_changed(lv_event_t* e) {
    handle_standard_macro_changed(StandardMacroSlot::BedLevel, e);
}

static void on_clean_nozzle_changed(lv_event_t* e) {
    handle_standard_macro_changed(StandardMacroSlot::CleanNozzle, e);
}

static void on_heat_soak_changed(lv_event_t* e) {
    handle_standard_macro_changed(StandardMacroSlot::HeatSoak, e);
}

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
        ui_nav_go_back(); // Pop the modal
        lv_obj_delete(panel.theme_restart_dialog_);
        panel.theme_restart_dialog_ = nullptr;
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
        ui_nav_go_back(); // Pop the modal
        lv_obj_delete(panel.factory_reset_dialog_);
        panel.factory_reset_dialog_ = nullptr;
    }
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
    lv_xml_register_event_cb(nullptr, "on_time_format_changed", on_time_format_changed);
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
    lv_xml_register_event_cb(nullptr, "on_macro_buttons_clicked", on_macro_buttons_clicked);

    // Register macro buttons overlay dropdown callbacks
    // These are defined as static functions at the top of this file
    lv_xml_register_event_cb(nullptr, "on_quick_button_1_changed", on_quick_button_1_changed);
    lv_xml_register_event_cb(nullptr, "on_quick_button_2_changed", on_quick_button_2_changed);
    lv_xml_register_event_cb(nullptr, "on_load_filament_changed", on_load_filament_changed);
    lv_xml_register_event_cb(nullptr, "on_unload_filament_changed", on_unload_filament_changed);
    lv_xml_register_event_cb(nullptr, "on_purge_changed", on_purge_changed);
    lv_xml_register_event_cb(nullptr, "on_pause_changed", on_pause_changed);
    lv_xml_register_event_cb(nullptr, "on_resume_changed", on_resume_changed);
    lv_xml_register_event_cb(nullptr, "on_cancel_changed", on_cancel_changed);
    lv_xml_register_event_cb(nullptr, "on_bed_level_changed", on_bed_level_changed);
    lv_xml_register_event_cb(nullptr, "on_clean_nozzle_changed", on_clean_nozzle_changed);
    lv_xml_register_event_cb(nullptr, "on_heat_soak_changed", on_heat_soak_changed);

    lv_xml_register_event_cb(nullptr, "on_machine_limits_clicked", on_machine_limits_clicked);
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
    lv_xml_register_event_cb(nullptr, "on_brightness_changed", on_brightness_changed);

    // Note: BedMeshPanel subjects are initialized in main.cpp during startup

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void SettingsPanel::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[{}] Deinitializing subjects", get_name());

    // Deinit local subjects (3 string subjects)
    lv_subject_deinit(&brightness_value_subject_);
    lv_subject_deinit(&version_value_subject_);
    lv_subject_deinit(&printer_value_subject_);

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
        completion_alert_dropdown_ = lv_obj_find_by_name(completion_row, "dropdown");
        if (completion_alert_dropdown_) {
            // Set dropdown options (component doesn't support passing via XML attribute)
            lv_dropdown_set_options(completion_alert_dropdown_, "Off\nNotification\nAlert");
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

            // Initialize time format dropdown
            lv_obj_t* time_format_row =
                lv_obj_find_by_name(display_settings_overlay_, "row_time_format");
            lv_obj_t* time_format_dropdown =
                time_format_row ? lv_obj_find_by_name(time_format_row, "dropdown") : nullptr;
            if (time_format_dropdown) {
                // Set dropdown options
                lv_dropdown_set_options(time_format_dropdown,
                                        SettingsManager::get_time_format_options());
                // Set initial selection based on current setting
                auto current_format = SettingsManager::instance().get_time_format();
                lv_dropdown_set_selected(time_format_dropdown,
                                         static_cast<uint32_t>(current_format));
                spdlog::debug("[{}] Time format dropdown initialized to {} ({})", get_name(),
                              static_cast<int>(current_format),
                              current_format == TimeFormat::HOUR_12 ? "12H" : "24H");
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

void SettingsPanel::handle_macro_buttons_clicked() {
    spdlog::debug("[{}] Macro Buttons clicked - opening overlay", get_name());

    // Create macro buttons overlay on first access (lazy initialization)
    if (!macro_buttons_overlay_ && parent_screen_) {
        spdlog::debug("[{}] Creating macro buttons overlay...", get_name());

        // Create from XML - component name matches filename
        macro_buttons_overlay_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "macro_buttons_overlay", nullptr));
        if (!macro_buttons_overlay_) {
            spdlog::error("[{}] Failed to create macro buttons overlay from XML", get_name());
            return;
        }

        // Back button already wired via header_bar XML event_cb (on_header_back_clicked)

        // Initially hidden (dropdowns populated via populate_macro_dropdowns())
        lv_obj_add_flag(macro_buttons_overlay_, LV_OBJ_FLAG_HIDDEN);
        spdlog::info("[{}] Macro buttons overlay created", get_name());
    }

    // Populate dropdowns every time overlay is shown (handles printer reconnection)
    populate_macro_dropdowns();

    // Push overlay onto navigation history and show it
    if (macro_buttons_overlay_) {
        ui_nav_push_overlay(macro_buttons_overlay_);
    }
}

void SettingsPanel::populate_macro_dropdowns() {
    if (!macro_buttons_overlay_) {
        return;
    }

    spdlog::debug("[{}] Refreshing macro dropdowns...", get_name());

    // === Populate Quick Button Dropdowns ===
    // Options: "(Empty)", then slot display names
    std::string quick_button_options = "(Empty)";
    for (const auto& slot : StandardMacros::instance().all()) {
        quick_button_options += "\n" + slot.display_name;
    }

    // Get current quick button config
    Config* config = Config::get_instance();
    std::string qb1_slot =
        config ? config->get<std::string>("/standard_macros/quick_button_1", "clean_nozzle")
               : "clean_nozzle";
    std::string qb2_slot =
        config ? config->get<std::string>("/standard_macros/quick_button_2", "bed_level")
               : "bed_level";

    // Helper to find index for a slot name
    auto find_slot_index = [](const std::string& slot_name) -> int {
        if (slot_name.empty())
            return 0; // (Empty)
        const auto& slots = StandardMacros::instance().all();
        for (size_t i = 0; i < slots.size(); ++i) {
            if (slots[i].slot_name == slot_name) {
                return static_cast<int>(i) + 1; // +1 because 0 is "(Empty)"
            }
        }
        return 0;
    };

    // Quick Button 1
    lv_obj_t* qb1_row = lv_obj_find_by_name(macro_buttons_overlay_, "row_quick_button_1");
    lv_obj_t* qb1_dropdown = qb1_row ? lv_obj_find_by_name(qb1_row, "dropdown") : nullptr;
    if (qb1_dropdown) {
        lv_dropdown_set_options(qb1_dropdown, quick_button_options.c_str());
        lv_dropdown_set_selected(qb1_dropdown, find_slot_index(qb1_slot));
    }

    // Quick Button 2
    lv_obj_t* qb2_row = lv_obj_find_by_name(macro_buttons_overlay_, "row_quick_button_2");
    lv_obj_t* qb2_dropdown = qb2_row ? lv_obj_find_by_name(qb2_row, "dropdown") : nullptr;
    if (qb2_dropdown) {
        lv_dropdown_set_options(qb2_dropdown, quick_button_options.c_str());
        lv_dropdown_set_selected(qb2_dropdown, find_slot_index(qb2_slot));
    }

    // === Populate Standard Macro Dropdowns ===
    // Get sorted list of all printer macros from MoonrakerClient
    std::vector<std::string> printer_macros;
    MoonrakerClient* client = get_moonraker_client();
    if (client) {
        const auto& caps = client->capabilities();
        for (const auto& macro : caps.macros()) {
            printer_macros.push_back(macro);
        }
        std::sort(printer_macros.begin(), printer_macros.end());
    }

    // Row names matching XML
    const std::vector<std::pair<StandardMacroSlot, std::string>> slot_rows = {
        {StandardMacroSlot::LoadFilament, "row_load_filament"},
        {StandardMacroSlot::UnloadFilament, "row_unload_filament"},
        {StandardMacroSlot::Purge, "row_purge"},
        {StandardMacroSlot::Pause, "row_pause"},
        {StandardMacroSlot::Resume, "row_resume"},
        {StandardMacroSlot::Cancel, "row_cancel"},
        {StandardMacroSlot::BedLevel, "row_bed_level"},
        {StandardMacroSlot::CleanNozzle, "row_clean_nozzle"},
        {StandardMacroSlot::HeatSoak, "row_heat_soak"},
    };

    for (const auto& [slot, row_name] : slot_rows) {
        lv_obj_t* row = lv_obj_find_by_name(macro_buttons_overlay_, row_name.c_str());
        lv_obj_t* dropdown = row ? lv_obj_find_by_name(row, "dropdown") : nullptr;
        if (!dropdown)
            continue;

        const auto& info = StandardMacros::instance().get(slot);

        // Build options string - first option shows auto-detected or empty
        std::string options;
        if (!info.detected_macro.empty()) {
            options = "(Auto: " + info.detected_macro + ")";
        } else if (!info.fallback_macro.empty()) {
            options = "(Auto: " + info.fallback_macro + ")";
        } else {
            options = "(Empty)";
        }

        // Add all printer macros
        for (const auto& macro : printer_macros) {
            options += "\n" + macro;
        }

        lv_dropdown_set_options(dropdown, options.c_str());

        // Set selected value
        if (!info.configured_macro.empty()) {
            // Find the configured macro in the list
            int idx = 1; // Start after "(Auto/Empty)"
            for (const auto& macro : printer_macros) {
                if (macro == info.configured_macro) {
                    lv_dropdown_set_selected(dropdown, idx);
                    break;
                }
                ++idx;
            }
        } else {
            // Use auto (index 0)
            lv_dropdown_set_selected(dropdown, 0);
        }
    }

    spdlog::debug("[{}] Macro dropdowns refreshed ({} printer macros)", get_name(),
                  printer_macros.size());
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
        if (!klipper_name) {
            spdlog::error("[{}] Failed to allocate memory for sensor name: {}", get_name(),
                          sensor.klipper_name);
            continue;
        }
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

void SettingsPanel::handle_machine_limits_clicked() {
    spdlog::debug("[{}] Machine Limits clicked", get_name());
    ui_toast_show(ToastSeverity::INFO, "Machine Limits: Coming soon", 2000);
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

    // Hide the dialog
    if (factory_reset_dialog_) {
        ui_nav_go_back(); // Pop the modal
        lv_obj_delete(factory_reset_dialog_);
        factory_reset_dialog_ = nullptr;
    }

    // Show confirmation toast
    ui_toast_show(ToastSeverity::SUCCESS, "Settings reset to defaults", 2000);

    // TODO: In production, this would restart the application
    // or transition to the setup wizard. For now, just log.
    spdlog::info("[{}] Device should restart or show wizard now", get_name());
}

void SettingsPanel::handle_hardware_health_clicked() {
    spdlog::debug("[{}] Hardware Health clicked - opening overlay", get_name());

    // Create overlay on first access (lazy initialization)
    if (!hardware_health_overlay_ && parent_screen_) {
        spdlog::debug("[{}] Creating hardware health overlay...", get_name());

        hardware_health_overlay_ = static_cast<lv_obj_t*>(
            lv_xml_create(parent_screen_, "hardware_health_overlay", nullptr));

        if (hardware_health_overlay_) {
            lv_obj_add_flag(hardware_health_overlay_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Hardware health overlay created", get_name());
        } else {
            spdlog::error("[{}] Failed to create hardware health overlay", get_name());
            return;
        }
    }

    // Populate the issues lists before showing
    populate_hardware_issues();

    // Push overlay onto navigation history and show it
    if (hardware_health_overlay_) {
        ui_nav_push_overlay(hardware_health_overlay_);
    }
}

void SettingsPanel::populate_hardware_issues() {
    if (!hardware_health_overlay_) {
        return;
    }

    const auto& result = printer_state_.get_hardware_validation_result();

    // Helper to convert severity enum to string for XML attribute
    auto severity_to_string = [](HardwareIssueSeverity sev) -> const char* {
        switch (sev) {
        case HardwareIssueSeverity::CRITICAL:
            return "error";
        case HardwareIssueSeverity::WARNING:
            return "warning";
        case HardwareIssueSeverity::INFO:
        default:
            return "info";
        }
    };

    // Helper to populate a list with issues
    auto populate_list = [&](const char* list_name, const std::vector<HardwareIssue>& issues) {
        lv_obj_t* list = lv_obj_find_by_name(hardware_health_overlay_, list_name);
        if (!list) {
            spdlog::warn("[{}] Could not find list: {}", get_name(), list_name);
            return;
        }

        // Clear existing children
        lv_obj_clean(list);

        // Add issue rows
        for (const auto& issue : issues) {
            // Create row with severity attribute for colored left border
            const char* attrs[] = {"severity", severity_to_string(issue.severity), nullptr};
            lv_obj_t* row =
                static_cast<lv_obj_t*>(lv_xml_create(list, "hardware_issue_row", attrs));
            if (!row) {
                continue;
            }

            // Finalize severity_card to show correct icon
            ui_severity_card_finalize(row);

            // Set hardware name
            lv_obj_t* name_label = lv_obj_find_by_name(row, "hardware_name");
            if (name_label) {
                lv_label_set_text(name_label, issue.hardware_name.c_str());
            }

            // Set issue message
            lv_obj_t* message_label = lv_obj_find_by_name(row, "issue_message");
            if (message_label) {
                lv_label_set_text(message_label, issue.message.c_str());
            }

            // Configure action buttons for non-critical issues
            if (issue.severity != HardwareIssueSeverity::CRITICAL) {
                lv_obj_t* action_buttons = lv_obj_find_by_name(row, "action_buttons");
                lv_obj_t* ignore_btn = lv_obj_find_by_name(row, "ignore_btn");
                lv_obj_t* save_btn = lv_obj_find_by_name(row, "save_btn");

                if (action_buttons && ignore_btn) {
                    // Show button container
                    lv_obj_clear_flag(action_buttons, LV_OBJ_FLAG_HIDDEN);

                    // Show Save button only for INFO severity (newly discovered)
                    if (save_btn && issue.severity == HardwareIssueSeverity::INFO) {
                        lv_obj_clear_flag(save_btn, LV_OBJ_FLAG_HIDDEN);
                    }

                    // Store hardware name in row for callback (freed on row delete)
                    char* name_copy = strdup(issue.hardware_name.c_str());
                    lv_obj_set_user_data(row, name_copy);

                    // Add delete handler to free the strdup'd name
                    lv_obj_add_event_cb(
                        row,
                        [](lv_event_t* e) {
                            auto* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                            void* data = lv_obj_get_user_data(obj);
                            if (data) {
                                free(data);
                            }
                        },
                        LV_EVENT_DELETE, nullptr);

                    // Helper lambda for button click handlers
                    auto add_button_handler = [&](lv_obj_t* btn, bool is_ignore) {
                        lv_obj_add_event_cb(
                            btn,
                            [](lv_event_t* e) {
                                LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] hardware_action_clicked");
                                auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                                // Navigate up: btn -> action_buttons -> row
                                lv_obj_t* action_container = lv_obj_get_parent(btn);
                                lv_obj_t* row = lv_obj_get_parent(action_container);
                                const char* hw_name =
                                    static_cast<const char*>(lv_obj_get_user_data(row));
                                bool is_ignore = static_cast<bool>(
                                    reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));

                                if (hw_name) {
                                    get_global_settings_panel().handle_hardware_action(hw_name,
                                                                                       is_ignore);
                                }
                                LVGL_SAFE_EVENT_CB_END();
                            },
                            LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<uintptr_t>(is_ignore)));
                    };

                    // Wire up Ignore button (always visible for non-critical)
                    add_button_handler(ignore_btn, true);

                    // Wire up Save button (only for INFO severity)
                    if (save_btn && issue.severity == HardwareIssueSeverity::INFO) {
                        add_button_handler(save_btn, false);
                    }
                }
            }
        }
    };

    // Populate each section
    populate_list("critical_issues_list", result.critical_missing);
    populate_list("warning_issues_list", result.expected_missing);
    populate_list("info_issues_list", result.newly_discovered);
    populate_list("session_issues_list", result.changed_from_last_session);

    spdlog::debug("[{}] Populated hardware issues: {} critical, {} warning, {} info, {} session",
                  get_name(), result.critical_missing.size(), result.expected_missing.size(),
                  result.newly_discovered.size(), result.changed_from_last_session.size());
}

void SettingsPanel::handle_hardware_action(const char* hardware_name, bool is_ignore) {
    if (!hardware_name) {
        return;
    }

    Config* config = Config::get_instance();
    std::string hw_name(hardware_name);

    if (is_ignore) {
        // "Ignore" - Mark hardware as optional (no confirmation needed)
        HardwareValidator::set_hardware_optional(config, hw_name, true);
        ui_toast_show(ToastSeverity::SUCCESS, "Hardware marked as optional", 2000);
        spdlog::info("[{}] Marked hardware '{}' as optional", get_name(), hw_name);

        // Remove from cached validation result and refresh overlay
        printer_state_.remove_hardware_issue(hw_name);
        populate_hardware_issues();
    } else {
        // "Save" - Add to expected hardware (with confirmation)
        // Store name for confirmation callback
        pending_hardware_save_ = hw_name;

        // Static message buffer (safe since we close existing dialogs first)
        static char message_buf[256];
        snprintf(message_buf, sizeof(message_buf),
                 "Add '%s' to expected hardware?\n\nYou'll be notified if it's removed later.",
                 hw_name.c_str());

        // Close any existing dialog first
        if (hardware_save_dialog_) {
            ui_modal_hide(hardware_save_dialog_);
            hardware_save_dialog_ = nullptr;
        }

        // Show confirmation dialog
        hardware_save_dialog_ =
            ui_modal_show_confirmation("Save Hardware", message_buf, ModalSeverity::Info, "Save",
                                       on_hardware_save_confirm, on_hardware_save_cancel, this);
    }
}

void SettingsPanel::on_hardware_save_confirm(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_hardware_save_confirm");
    auto* self = static_cast<SettingsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_hardware_save_confirm();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_hardware_save_cancel(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_hardware_save_cancel");
    auto* self = static_cast<SettingsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_hardware_save_cancel();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::handle_hardware_save_confirm() {
    // Close dialog first
    if (hardware_save_dialog_) {
        ui_modal_hide(hardware_save_dialog_);
        hardware_save_dialog_ = nullptr;
    }

    Config* cfg = Config::get_instance();

    // Add to expected hardware list
    HardwareValidator::add_expected_hardware(cfg, pending_hardware_save_);
    ui_toast_show(ToastSeverity::SUCCESS, "Hardware saved to config", 2000);
    spdlog::info("[{}] Added hardware '{}' to expected list", get_name(), pending_hardware_save_);

    // Remove from cached validation result and refresh overlay
    printer_state_.remove_hardware_issue(pending_hardware_save_);
    populate_hardware_issues();
    pending_hardware_save_.clear();
}

void SettingsPanel::handle_hardware_save_cancel() {
    // Close dialog
    if (hardware_save_dialog_) {
        ui_modal_hide(hardware_save_dialog_);
        hardware_save_dialog_ = nullptr;
    }

    pending_hardware_save_.clear();
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
    lv_xml_register_event_cb(nullptr, "on_factory_reset_clicked",
                             SettingsPanel::on_factory_reset_clicked);
    lv_xml_register_event_cb(nullptr, "on_hardware_health_clicked",
                             SettingsPanel::on_hardware_health_clicked);
}
