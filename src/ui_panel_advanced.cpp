// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_advanced.h"

#include "ui_nav.h"
#include "ui_panel_spoolman.h"
#include "ui_timelapse_settings.h"
#include "ui_toast.h"

#include "moonraker_api.h"
#include "moonraker_client.h"
#include "printer_capabilities.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

// Forward declarations
MoonrakerClient* get_moonraker_client();

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
    lv_xml_register_event_cb(nullptr, "on_advanced_machine_limits", on_machine_limits_clicked);
    lv_xml_register_event_cb(nullptr, "on_advanced_spoolman", on_spoolman_clicked);
    lv_xml_register_event_cb(nullptr, "on_advanced_macros", on_macros_clicked);

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
}

// ============================================================================
// NAVIGATION HANDLERS
// ============================================================================

void AdvancedPanel::handle_machine_limits_clicked() {
    spdlog::debug("[{}] Machine Limits clicked", get_name());
    ui_toast_show(ToastSeverity::INFO, "Machine Limits: Coming soon", 2000);
}

void AdvancedPanel::handle_spoolman_clicked() {
    spdlog::debug("[{}] Spoolman clicked - opening panel", get_name());

    // Create Spoolman panel on first access (lazy initialization)
    if (!spoolman_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating Spoolman panel...", get_name());

        // Create from XML
        spoolman_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "spoolman_panel", nullptr));
        if (spoolman_panel_) {
            // Setup event handlers and data loading
            get_global_spoolman_panel().setup(spoolman_panel_, parent_screen_);

            // Initially hidden
            lv_obj_add_flag(spoolman_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Spoolman panel created", get_name());
        } else {
            spdlog::error("[{}] Failed to create Spoolman panel from XML", get_name());
            ui_toast_show(ToastSeverity::ERROR, "Failed to open Spoolman", 2000);
            return;
        }
    }

    // Push Spoolman panel onto navigation history and show it
    if (spoolman_panel_) {
        ui_nav_push_overlay(spoolman_panel_);
    }
}

void AdvancedPanel::handle_macros_clicked() {
    spdlog::debug("[{}] Macros clicked", get_name());

    MoonrakerClient* client = get_moonraker_client();
    if (client) {
        size_t macro_count = client->capabilities().macro_count();
        std::string msg = "Macros: " + std::to_string(macro_count) + " available";
        ui_toast_show(ToastSeverity::INFO, msg.c_str(), 2000);
    }
}

// ============================================================================
// STATIC EVENT CALLBACKS (registered via lv_xml_register_event_cb)
// ============================================================================

void AdvancedPanel::on_machine_limits_clicked(lv_event_t* /*e*/) {
    get_global_advanced_panel().handle_machine_limits_clicked();
}

void AdvancedPanel::on_spoolman_clicked(lv_event_t* /*e*/) {
    get_global_advanced_panel().handle_spoolman_clicked();
}

void AdvancedPanel::on_macros_clicked(lv_event_t* /*e*/) {
    get_global_advanced_panel().handle_macros_clicked();
}
