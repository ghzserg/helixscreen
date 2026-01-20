// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_advanced.h"

#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_overlay_timelapse_settings.h"
#include "ui_panel_calibration_pid.h"
#include "ui_panel_console.h"
#include "ui_panel_macros.h"
#include "ui_panel_spoolman.h"
#include "ui_toast.h"
#include "ui_update_queue.h"

#include "app_globals.h"
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
    lv_xml_register_event_cb(nullptr, "on_restart_helix_clicked", on_restart_helix_clicked);
    lv_xml_register_event_cb(nullptr, "on_pid_tuning_clicked", on_pid_tuning_clicked);

    // Note: Input shaping uses on_input_shaper_row_clicked registered by InputShaperPanel
    // Note: Restart row doesn't exist - restart buttons have their own callbacks in
    // ui_emergency_stop.cpp

    subjects_initialized_ = true;
    spdlog::debug("[{}] Event callbacks registered", get_name());
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

    spdlog::info("[{}] Setup complete", get_name());
}

void AdvancedPanel::on_activate() {
    spdlog::debug("[{}] Activated", get_name());
    // Note: Plugin detection now happens automatically in discovery flow (application.cpp)

    // Query phase tracking status if plugin is installed
    if (printer_state_.service_has_helix_plugin() && api_) {
        api_->get_client().send_jsonrpc(
            "server.helix.phase_tracking.status", json::object(),
            [this](json response) {
                if (response.contains("result")) {
                    bool enabled = response["result"].value("enabled", false);
                    printer_state_.set_phase_tracking_enabled(enabled);
                    spdlog::debug("[{}] Phase tracking status: {}", get_name(), enabled);
                }
            },
            [this](const MoonrakerError& err) {
                spdlog::debug("[{}] Phase tracking status query failed: {}", get_name(),
                              err.message);
            },
            0,     // timeout_ms: use default
            true); // silent: suppress RPC_ERROR events/toasts
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
        ui_toast_show(ToastSeverity::ERROR, "Not connected to printer", 2000);
        return;
    }

    helix::MacroModificationManager* macro_mgr = mgr->macro_analysis();
    if (!macro_mgr) {
        spdlog::error("[{}] No MacroModificationManager available", get_name());
        ui_toast_show(ToastSeverity::ERROR, "Macro analysis not initialized", 2000);
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
        overlay.set_client(get_moonraker_client());
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

void AdvancedPanel::on_restart_helix_clicked(lv_event_t* /*e*/) {
    get_global_advanced_panel().handle_restart_helix_clicked();
}

void AdvancedPanel::on_pid_tuning_clicked(lv_event_t* /*e*/) {
    get_global_advanced_panel().handle_pid_tuning_clicked();
}

// ============================================================================
// HELIXPRINT PLUGIN HANDLERS
// ============================================================================

void AdvancedPanel::handle_helix_plugin_install_clicked() {
    spdlog::debug("[{}] HelixPrint Plugin Install clicked", get_name());

    // Double-check plugin isn't already installed (defensive)
    if (printer_state_.service_has_helix_plugin()) {
        spdlog::info("[{}] Plugin already installed", get_name());
        ui_toast_show(ToastSeverity::INFO, "Plugin already installed", 2000);
        return;
    }

    // Update installer's websocket URL for local/remote detection
    if (api_) {
        plugin_installer_.set_websocket_url(api_->get_client().get_last_url());
    }

    // Show the install modal
    plugin_install_modal_.set_installer(&plugin_installer_);
    plugin_install_modal_.set_on_install_complete([this](bool success) {
        if (success) {
            printer_state_.set_helix_plugin_installed(true);
            ui_toast_show(ToastSeverity::SUCCESS, "Plugin installed successfully", 2000);
        }
    });
    plugin_install_modal_.show(lv_screen_active());
}

void AdvancedPanel::handle_helix_plugin_uninstall_clicked() {
    spdlog::debug("[{}] HelixPrint Plugin Uninstall clicked", get_name());
    // TODO: Implement uninstall functionality
    ui_toast_show(ToastSeverity::INFO, "Uninstall: Coming soon", 2000);
}

void AdvancedPanel::handle_phase_tracking_changed(bool enabled) {
    spdlog::info("[{}] Phase tracking toggle: {}", get_name(), enabled);

    if (!api_) {
        ui_toast_show(ToastSeverity::ERROR, "Not connected to printer", 2000);
        return;
    }

    // Call the plugin API to enable/disable phase tracking via JSON-RPC
    std::string method =
        enabled ? "server.helix.phase_tracking.enable" : "server.helix.phase_tracking.disable";

    api_->get_client().send_jsonrpc(
        method, json::object(),
        [this, enabled](json response) {
            // Check response for success
            if (response.contains("result")) {
                bool success = response["result"].value("success", false);
                if (success) {
                    printer_state_.set_phase_tracking_enabled(enabled);
                    ui_toast_show(ToastSeverity::SUCCESS,
                                  enabled ? "Phase tracking enabled" : "Phase tracking disabled",
                                  2000);
                    return;
                }

                // Check for error message
                if (response["result"].contains("error")) {
                    std::string error = response["result"]["error"].get<std::string>();
                    spdlog::warn("[{}] Phase tracking API error: {}", get_name(), error);
                    ui_toast_show(ToastSeverity::WARNING, error.c_str(), 3000);
                    // Revert the toggle state
                    printer_state_.set_phase_tracking_enabled(!enabled);
                    return;
                }
            }

            // Fallback: assume success if we got a response without error
            printer_state_.set_phase_tracking_enabled(enabled);
            ui_toast_show(ToastSeverity::INFO,
                          enabled ? "Phase tracking enabled" : "Phase tracking disabled", 2000);
        },
        [this, enabled](const MoonrakerError& err) {
            spdlog::error("[{}] Phase tracking API call failed: {}", get_name(), err.message);
            ui_toast_show(ToastSeverity::ERROR, "Failed to update phase tracking", 2000);
            // Revert the toggle state
            printer_state_.set_phase_tracking_enabled(!enabled);
        });
}

// ============================================================================
// RESTART HANDLER
// ============================================================================

void AdvancedPanel::handle_restart_helix_clicked() {
    spdlog::info("[{}] Restart HelixScreen requested", get_name());
    ui_toast_show(ToastSeverity::INFO, "Restarting HelixScreen...", 1500);

    // Schedule restart after a brief delay to let toast display
    // Uses fork/exec pattern from app_globals - works on both systemd and standalone
    ui_async_call(
        [](void*) {
            spdlog::info("[Advanced Panel] Initiating restart...");
            app_request_restart();
        },
        nullptr);
}
