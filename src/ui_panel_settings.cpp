// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_settings.h"

#include "app_globals.h"
#include "printer_state.h"
#include "ui_event_safety.h"
#include "ui_nav.h"
#include "ui_panel_bed_mesh.h"

#include <spdlog/spdlog.h>

#include <memory>

// Forward declarations for class-based API
class BedMeshPanel;
BedMeshPanel& get_global_bed_mesh_panel();

// ============================================================================
// CONSTRUCTOR
// ============================================================================

SettingsPanel::SettingsPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // Dependencies passed for interface consistency
    // Currently only bed mesh uses these indirectly
}

// ============================================================================
// PANELBASE IMPLEMENTATION
// ============================================================================

void SettingsPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // Initialize bed mesh panel subjects (may be lazily created later)
    get_global_bed_mesh_panel().init_subjects();

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

    // Wire up card click handlers
    setup_card_handlers();

    spdlog::info("[{}] Setup complete", get_name());
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void SettingsPanel::setup_card_handlers() {
    // Find launcher card objects by name
    lv_obj_t* card_network = lv_obj_find_by_name(panel_, "card_network");
    lv_obj_t* card_display = lv_obj_find_by_name(panel_, "card_display");
    lv_obj_t* card_bed_mesh = lv_obj_find_by_name(panel_, "card_bed_mesh");
    lv_obj_t* card_z_offset = lv_obj_find_by_name(panel_, "card_z_offset");
    lv_obj_t* card_printer_info = lv_obj_find_by_name(panel_, "card_printer_info");
    lv_obj_t* card_about = lv_obj_find_by_name(panel_, "card_about");

    // Verify all cards found
    if (!card_network || !card_display || !card_bed_mesh || !card_z_offset || !card_printer_info ||
        !card_about) {
        spdlog::error("[{}] Failed to find all launcher cards", get_name());
        return;
    }

    // Wire click event handlers - pass 'this' as user_data for trampolines
    lv_obj_add_event_cb(card_network, on_network_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(card_display, on_display_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(card_bed_mesh, on_bed_mesh_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(card_z_offset, on_z_offset_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(card_printer_info, on_printer_info_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(card_about, on_about_clicked, LV_EVENT_CLICKED, this);

    // Make only active cards clickable (bed_mesh is the only active one for now)
    lv_obj_add_flag(card_bed_mesh, LV_OBJ_FLAG_CLICKABLE);
    // Others are disabled placeholders - no clickable flag

    spdlog::debug("[{}] Card handlers wired", get_name());
}

// ============================================================================
// CARD CLICK HANDLERS
// ============================================================================

void SettingsPanel::handle_network_clicked() {
    spdlog::debug("[{}] Network card clicked (placeholder)", get_name());
    // TODO: Open network settings panel
}

void SettingsPanel::handle_display_clicked() {
    spdlog::debug("[{}] Display card clicked (placeholder)", get_name());
    // TODO: Open display settings panel
}

void SettingsPanel::handle_bed_mesh_clicked() {
    spdlog::debug("[{}] Bed Mesh card clicked - opening visualization", get_name());

    // Create bed mesh panel on first access (lazy initialization)
    if (!bed_mesh_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating bed mesh visualization panel...", get_name());

        // Create from XML
        bed_mesh_panel_ = static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "bed_mesh_panel", nullptr));
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
    spdlog::debug("[{}] Z-Offset card clicked (placeholder)", get_name());
    // TODO: Open Z-offset adjustment panel
}

void SettingsPanel::handle_printer_info_clicked() {
    spdlog::debug("[{}] Printer Info card clicked (placeholder)", get_name());
    // TODO: Open printer info panel
}

void SettingsPanel::handle_about_clicked() {
    spdlog::debug("[{}] About card clicked (placeholder)", get_name());
    // TODO: Open about panel
}

// ============================================================================
// STATIC TRAMPOLINES
// ============================================================================

void SettingsPanel::on_network_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_network_clicked");
    auto* self = static_cast<SettingsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_network_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_display_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_display_clicked");
    auto* self = static_cast<SettingsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_display_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_bed_mesh_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_bed_mesh_clicked");
    auto* self = static_cast<SettingsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_bed_mesh_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_z_offset_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_z_offset_clicked");
    auto* self = static_cast<SettingsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_z_offset_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_printer_info_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_printer_info_clicked");
    auto* self = static_cast<SettingsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_printer_info_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void SettingsPanel::on_about_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SettingsPanel] on_about_clicked");
    auto* self = static_cast<SettingsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_about_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// GLOBAL INSTANCE (needed by main.cpp)
// ============================================================================

static std::unique_ptr<SettingsPanel> g_settings_panel;

SettingsPanel& get_global_settings_panel() {
    if (!g_settings_panel) {
        g_settings_panel = std::make_unique<SettingsPanel>(get_printer_state(), nullptr);
    }
    return *g_settings_panel;
}
