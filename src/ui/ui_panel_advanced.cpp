// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_advanced.h"

#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_overlay_timelapse_install.h"
#include "ui_overlay_timelapse_settings.h"
#include "ui_panel_calibration_pid.h"
#include "ui_panel_console.h"
#include "ui_panel_macros.h"
#include "ui_panel_spoolman.h"
#include "ui_toast.h"

#include "app_globals.h"
#include "config.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "macro_modification_manager.h"
#include "moonraker_api.h"
#include "moonraker_client.h"
#include "moonraker_manager.h"
#include "printer_state.h"
#include "static_panel_registry.h"
#include "ui/ui_lazy_panel_helper.h"

#include <spdlog/spdlog.h>

// Global instance (singleton pattern matching SettingsPanel)
static std::unique_ptr<AdvancedPanel> g_advanced_panel;

AdvancedPanel& get_global_advanced_panel() {
    // Should be initialized by main.cpp before use
    if (!g_advanced_panel) {
        spdlog::error("[Advanced Panel] get_global_advanced_panel() called before initialization!");
        throw std::runtime_error("AdvancedPanel not initialized");
    }
    return *g_advanced_panel;
}

// Called by main.cpp to initialize the global instance
void init_global_advanced_panel(PrinterState& printer_state, MoonrakerAPI* api) {
    g_advanced_panel = std::make_unique<AdvancedPanel>(printer_state, api);
    StaticPanelRegistry::instance().register_destroy("AdvancedPanel",
                                                     []() { g_advanced_panel.reset(); });
}

// ============================================================================
// CONSTRUCTOR
// ============================================================================

AdvancedPanel::AdvancedPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    spdlog::trace("[{}] Constructor", get_name());
}

// ============================================================================
// PANELBASE IMPLEMENTATION
// ============================================================================

void AdvancedPanel::init_subjects() {
    // Register XML event callbacks (must be done BEFORE XML is created)
    lv_xml_register_event_cb(nullptr, "on_advanced_spoolman", on_spoolman_clicked);
    lv_xml_register_event_cb(nullptr, "on_advanced_macros", on_macros_clicked);
    lv_xml_register_event_cb(nullptr, "on_console_row_clicked", on_console_clicked);
    lv_xml_register_event_cb(nullptr, "on_history_row_clicked", on_history_clicked);
    lv_xml_register_event_cb(nullptr, "on_configure_print_start", on_configure_print_start_clicked);
    lv_xml_register_event_cb(nullptr, "on_helix_plugin_install_clicked",
                             on_helix_plugin_install_clicked);
    lv_xml_register_event_cb(nullptr, "on_helix_plugin_uninstall_clicked",
                             on_helix_plugin_uninstall_clicked);
    lv_xml_register_event_cb(nullptr, "on_phase_tracking_changed", on_phase_tracking_changed);
    lv_xml_register_event_cb(nullptr, "on_pid_tuning_clicked", on_pid_tuning_clicked);
    lv_xml_register_event_cb(nullptr, "on_timelapse_setup_clicked", on_timelapse_setup_clicked);

    // Note: Input shaping uses on_input_shaper_row_clicked registered by InputShaperPanel
    // Note: Restart row doesn't exist - restart buttons have their own callbacks in
    // ui_emergency_stop.cpp

    subjects_initialized_ = true;
    spdlog::trace("[{}] Event callbacks registered", get_name());
}

void AdvancedPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    // Event handlers are now declaratively bound via XML event_cb elements
    // No imperative lv_obj_add_event_cb() calls needed

    spdlog::debug("[{}] Setup complete", get_name());
}

void AdvancedPanel::on_activate() {
    spdlog::debug("[{}] Activated", get_name());
    // Note: Plugin detection now happens automatically in discovery flow (application.cpp)

    // Query phase tracking status if plugin is installed
    if (printer_state_.service_has_helix_plugin() && api_) {
        api_->get_phase_tracking_status(
            [this](bool enabled) {
                printer_state_.set_phase_tracking_enabled(enabled);
                spdlog::debug("[{}] Phase tracking status: {}", get_name(), enabled);
            },
            [this](const MoonrakerError& err) {
                spdlog::debug("[{}] Phase tracking status query failed: {}", get_name(),
                              err.message);
            });
    }
}

// ============================================================================
// NAVIGATION HANDLERS
// ============================================================================

void AdvancedPanel::handle_spoolman_clicked() {
    helix::ui::lazy_create_and_push_overlay<SpoolmanPanel>(
        get_global_spoolman_panel, spoolman_panel_, parent_screen_, "Spoolman", get_name());
}

void AdvancedPanel::handle_macros_clicked() {
    helix::ui::lazy_create_and_push_overlay<MacrosPanel>(get_global_macros_panel, macros_panel_,
                                                         parent_screen_, "Macros", get_name());
}

void AdvancedPanel::handle_console_clicked() {
    helix::ui::lazy_create_and_push_overlay<ConsolePanel>(get_global_console_panel, console_panel_,
                                                          parent_screen_, "Console", get_name());
}

void AdvancedPanel::handle_history_clicked() {
    helix::ui::lazy_create_and_push_overlay<HistoryDashboardPanel>(
        get_global_history_dashboard_panel, history_dashboard_panel_, parent_screen_,
        "Print History", get_name());
}

void AdvancedPanel::handle_configure_print_start_clicked() {
    spdlog::debug("[{}] Configure PRINT_START clicked", get_name());

    MoonrakerManager* mgr = get_moonraker_manager();
    if (!mgr) {
        spdlog::error("[{}] No MoonrakerManager available", get_name());
        ui_toast_show(ToastSeverity::ERROR, lv_tr("Not connected to printer"), 2000);
        return;
    }

    helix::MacroModificationManager* macro_mgr = mgr->macro_analysis();
    if (!macro_mgr) {
        spdlog::error("[{}] No MacroModificationManager available", get_name());
        ui_toast_show(ToastSeverity::ERROR, lv_tr("Macro analysis not initialized"), 2000);
        return;
    }

    // Launch wizard (handles its own analysis and UI)
    macro_mgr->analyze_and_launch_wizard();
}

void AdvancedPanel::handle_pid_tuning_clicked() {
    spdlog::debug("[{}] PID Tuning clicked - opening calibration panel", get_name());

    auto& overlay = get_global_pid_cal_panel();

    if (!overlay.get_root()) {
        overlay.init_subjects();
        overlay.set_api(get_moonraker_api());
        overlay.create(parent_screen_);
    }

    overlay.show();
}

// ============================================================================
// STATIC EVENT CALLBACKS (registered via lv_xml_register_event_cb)
// ============================================================================

void AdvancedPanel::on_spoolman_clicked(lv_event_t* /*e*/) {
    get_global_advanced_panel().handle_spoolman_clicked();
}

void AdvancedPanel::on_macros_clicked(lv_event_t* /*e*/) {
    get_global_advanced_panel().handle_macros_clicked();
}

void AdvancedPanel::on_console_clicked(lv_event_t* /*e*/) {
    get_global_advanced_panel().handle_console_clicked();
}

void AdvancedPanel::on_history_clicked(lv_event_t* /*e*/) {
    get_global_advanced_panel().handle_history_clicked();
}

void AdvancedPanel::on_configure_print_start_clicked(lv_event_t* /*e*/) {
    get_global_advanced_panel().handle_configure_print_start_clicked();
}

void AdvancedPanel::on_helix_plugin_install_clicked(lv_event_t* /*e*/) {
    get_global_advanced_panel().handle_helix_plugin_install_clicked();
}

void AdvancedPanel::on_helix_plugin_uninstall_clicked(lv_event_t* /*e*/) {
    get_global_advanced_panel().handle_helix_plugin_uninstall_clicked();
}

void AdvancedPanel::on_phase_tracking_changed(lv_event_t* e) {
    // Toggle switch callback - get the new state
    lv_obj_t* toggle = static_cast<lv_obj_t*>(lv_event_get_target(e));
    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    get_global_advanced_panel().handle_phase_tracking_changed(enabled);
}

void AdvancedPanel::on_pid_tuning_clicked(lv_event_t* /*e*/) {
    get_global_advanced_panel().handle_pid_tuning_clicked();
}

void AdvancedPanel::on_timelapse_setup_clicked(lv_event_t* /*e*/) {
    get_global_advanced_panel().handle_timelapse_setup_clicked();
}

// ============================================================================
// TIMELAPSE SETUP HANDLER
// ============================================================================

void AdvancedPanel::handle_timelapse_setup_clicked() {
    spdlog::info("[{}] Timelapse setup clicked", get_name());
    open_timelapse_install();
}

// ============================================================================
// HELIXPRINT PLUGIN HANDLERS
// ============================================================================

void AdvancedPanel::handle_helix_plugin_install_clicked() {
    spdlog::debug("[{}] HelixPrint Plugin Install clicked", get_name());

    // Gate plugin install behind beta_features flag
    Config* config = Config::get_instance();
    if (config && !config->is_beta_features_enabled()) {
        spdlog::debug("[{}] Beta features disabled, ignoring plugin install", get_name());
        return;
    }

    // Double-check plugin isn't already installed (defensive)
    if (printer_state_.service_has_helix_plugin()) {
        spdlog::info("[{}] Plugin already installed", get_name());
        ui_toast_show(ToastSeverity::INFO, lv_tr("Plugin already installed"), 2000);
        return;
    }

    // Update installer's websocket URL for local/remote detection
    if (api_) {
        plugin_installer_.set_websocket_url(api_->get_websocket_url());
    }

    // Show the install modal
    plugin_install_modal_.set_installer(&plugin_installer_);
    plugin_install_modal_.set_on_install_complete([this](bool success) {
        if (success) {
            printer_state_.set_helix_plugin_installed(true);
            ui_toast_show(ToastSeverity::SUCCESS, lv_tr("Plugin installed successfully"), 2000);
        }
    });
    plugin_install_modal_.show(lv_screen_active());
}

void AdvancedPanel::handle_helix_plugin_uninstall_clicked() {
    spdlog::debug("[{}] HelixPrint Plugin Uninstall clicked", get_name());
    // TODO: Implement uninstall functionality
    ui_toast_show(ToastSeverity::INFO, lv_tr("Uninstall: Coming soon"), 2000);
}

void AdvancedPanel::handle_phase_tracking_changed(bool enabled) {
    spdlog::info("[{}] Phase tracking toggle: {}", get_name(), enabled);

    if (!api_) {
        ui_toast_show(ToastSeverity::ERROR, lv_tr("Not connected to printer"), 2000);
        return;
    }

    // Call the plugin API to enable/disable phase tracking
    api_->set_phase_tracking_enabled(
        enabled,
        [this, enabled](bool success) {
            if (success) {
                printer_state_.set_phase_tracking_enabled(enabled);
                ui_toast_show(ToastSeverity::SUCCESS,
                              enabled ? lv_tr("Phase tracking enabled")
                                      : lv_tr("Phase tracking disabled"),
                              2000);
            } else {
                spdlog::warn("[{}] Phase tracking API returned success=false", get_name());
                ui_toast_show(ToastSeverity::WARNING, lv_tr("Phase tracking update failed"), 3000);
                // Revert the toggle state
                printer_state_.set_phase_tracking_enabled(!enabled);
            }
        },
        [this, enabled](const MoonrakerError& err) {
            spdlog::error("[{}] Phase tracking API call failed: {}", get_name(), err.message);
            ui_toast_show(ToastSeverity::ERROR, lv_tr("Failed to update phase tracking"), 2000);
            // Revert the toggle state
            printer_state_.set_phase_tracking_enabled(!enabled);
        });
}
