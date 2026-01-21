// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_ams_settings_overlay.cpp
 * @brief Implementation of AmsSettingsOverlay
 */

#include "ui_ams_settings_overlay.h"

#include "ui_ams_behavior_overlay.h"
#include "ui_ams_endless_spool_overlay.h"
#include "ui_ams_maintenance_overlay.h"
#include "ui_ams_spoolman_overlay.h"
#include "ui_ams_tool_mapping_overlay.h"
#include "ui_event_safety.h"
#include "ui_nav_manager.h"

#include "ams_state.h"
#include "app_globals.h"
#include "moonraker_client.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <memory>

namespace helix::ui {

// ============================================================================
// SINGLETON ACCESSOR
// ============================================================================

static std::unique_ptr<AmsSettingsOverlay> g_ams_settings_overlay;

AmsSettingsOverlay& get_ams_settings_overlay() {
    if (!g_ams_settings_overlay) {
        g_ams_settings_overlay = std::make_unique<AmsSettingsOverlay>();
        StaticPanelRegistry::instance().register_destroy("AmsSettingsOverlay",
                                                         []() { g_ams_settings_overlay.reset(); });
    }
    return *g_ams_settings_overlay;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

AmsSettingsOverlay::AmsSettingsOverlay() {
    spdlog::debug("[{}] Created", get_name());
}

AmsSettingsOverlay::~AmsSettingsOverlay() {
    if (subjects_initialized_ && lv_is_initialized()) {
        lv_subject_deinit(&version_subject_);
        lv_subject_deinit(&slot_count_subject_);
        lv_subject_deinit(&connection_status_subject_);
    }
    spdlog::debug("[{}] Destroyed", get_name());
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void AmsSettingsOverlay::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    // Initialize version subject for label binding
    snprintf(version_buf_, sizeof(version_buf_), "");
    lv_subject_init_string(&version_subject_, version_buf_, nullptr, sizeof(version_buf_),
                           version_buf_);
    lv_xml_register_subject(nullptr, "ams_settings_version", &version_subject_);

    // Initialize slot count subject for label binding
    snprintf(slot_count_buf_, sizeof(slot_count_buf_), "");
    lv_subject_init_string(&slot_count_subject_, slot_count_buf_, nullptr, sizeof(slot_count_buf_),
                           slot_count_buf_);
    lv_xml_register_subject(nullptr, "ams_settings_slot_count", &slot_count_subject_);

    // Initialize connection status subject (0=disconnected, 1=connected)
    lv_subject_init_int(&connection_status_subject_, 0);
    lv_xml_register_subject(nullptr, "ams_settings_connection", &connection_status_subject_);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void AmsSettingsOverlay::register_callbacks() {
    // Register all navigation row callbacks
    lv_xml_register_event_cb(nullptr, "on_ams_settings_tool_mapping_clicked",
                             on_tool_mapping_clicked);
    lv_xml_register_event_cb(nullptr, "on_ams_settings_endless_spool_clicked",
                             on_endless_spool_clicked);
    lv_xml_register_event_cb(nullptr, "on_ams_settings_maintenance_clicked",
                             on_maintenance_clicked);
    lv_xml_register_event_cb(nullptr, "on_ams_settings_behavior_clicked", on_behavior_clicked);
    lv_xml_register_event_cb(nullptr, "on_ams_settings_calibration_clicked",
                             on_calibration_clicked);
    lv_xml_register_event_cb(nullptr, "on_ams_settings_speed_clicked", on_speed_settings_clicked);
    lv_xml_register_event_cb(nullptr, "on_ams_settings_spoolman_clicked", on_spoolman_clicked);

    spdlog::debug("[{}] Callbacks registered", get_name());
}

// ============================================================================
// UI CREATION
// ============================================================================

lv_obj_t* AmsSettingsOverlay::create(lv_obj_t* parent) {
    if (overlay_) {
        spdlog::warn("[{}] create() called but overlay already exists", get_name());
        return overlay_;
    }

    spdlog::debug("[{}] Creating overlay...", get_name());

    // Create from XML component
    overlay_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "ams_settings_panel", nullptr));
    if (!overlay_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    // Initially hidden until show() pushes it
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);

    spdlog::info("[{}] Overlay created", get_name());
    return overlay_;
}

void AmsSettingsOverlay::show(lv_obj_t* parent_screen) {
    spdlog::debug("[{}] show() called", get_name());

    parent_screen_ = parent_screen;

    // Ensure subjects and callbacks are initialized
    if (!subjects_initialized_) {
        init_subjects();
        register_callbacks();
    }

    // Lazy create overlay
    if (!overlay_ && parent_screen_) {
        create(parent_screen_);
    }

    if (!overlay_) {
        spdlog::error("[{}] Cannot show - overlay not created", get_name());
        return;
    }

    // Update status card from backend
    update_status_card();

    // Push onto navigation stack
    ui_nav_push_overlay(overlay_);
}

void AmsSettingsOverlay::update_status_card() {
    if (!overlay_) {
        return;
    }

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        snprintf(version_buf_, sizeof(version_buf_), "Not connected");
        lv_subject_copy_string(&version_subject_, version_buf_);

        snprintf(slot_count_buf_, sizeof(slot_count_buf_), "---");
        lv_subject_copy_string(&slot_count_subject_, slot_count_buf_);

        // Set disconnected status
        lv_subject_set_int(&connection_status_subject_, 0);
        return;
    }

    // Get backend system info
    AmsSystemInfo info = backend->get_system_info();

    // Consider connected if backend type is valid AND has slot data
    // (type alone could be set before full initialization completes)
    bool is_connected = (info.type != AmsType::NONE && info.total_slots > 0);

    // Update version subject
    if (!info.version.empty()) {
        snprintf(version_buf_, sizeof(version_buf_), "v%s", info.version.c_str());
    } else {
        snprintf(version_buf_, sizeof(version_buf_), "");
    }
    lv_subject_copy_string(&version_subject_, version_buf_);

    // Update slot count subject
    snprintf(slot_count_buf_, sizeof(slot_count_buf_), "%d slots", info.total_slots);
    lv_subject_copy_string(&slot_count_subject_, slot_count_buf_);

    // Update connection status subject
    lv_subject_set_int(&connection_status_subject_, is_connected ? 1 : 0);

    // Update backend logo (same logic as AmsPanel)
    lv_obj_t* backend_logo = lv_obj_find_by_name(overlay_, "backend_logo");
    if (backend_logo) {
        if (!info.type_name.empty()) {
            const char* logo_path = AmsState::get_logo_path(info.type_name);
            if (logo_path) {
                lv_image_set_src(backend_logo, logo_path);
                lv_obj_remove_flag(backend_logo, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(backend_logo, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            lv_obj_add_flag(backend_logo, LV_OBJ_FLAG_HIDDEN);
        }
    }

    spdlog::debug("[{}] Status card updated: {} v{}, {} slots, connected={}", get_name(),
                  info.type_name, info.version, info.total_slots, is_connected);
}

// ============================================================================
// STATIC CALLBACKS
// ============================================================================

void AmsSettingsOverlay::on_tool_mapping_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsSettingsOverlay] on_tool_mapping_clicked");
    LV_UNUSED(e);

    auto& overlay = get_ams_tool_mapping_overlay();
    if (!overlay.are_subjects_initialized()) {
        overlay.init_subjects();
        overlay.register_callbacks();
    }
    overlay.show(get_ams_settings_overlay().get_parent_screen());

    LVGL_SAFE_EVENT_CB_END();
}

void AmsSettingsOverlay::on_endless_spool_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsSettingsOverlay] on_endless_spool_clicked");
    LV_UNUSED(e);

    auto& overlay = get_ams_endless_spool_overlay();
    if (!overlay.are_subjects_initialized()) {
        overlay.init_subjects();
        overlay.register_callbacks();
    }
    overlay.show(get_ams_settings_overlay().get_parent_screen());

    LVGL_SAFE_EVENT_CB_END();
}

void AmsSettingsOverlay::on_maintenance_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsSettingsOverlay] on_maintenance_clicked");
    LV_UNUSED(e);

    auto& overlay = get_ams_maintenance_overlay();
    if (!overlay.are_subjects_initialized()) {
        overlay.init_subjects();
        overlay.register_callbacks();
    }
    overlay.show(get_ams_settings_overlay().get_parent_screen());

    LVGL_SAFE_EVENT_CB_END();
}

void AmsSettingsOverlay::on_behavior_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsSettingsOverlay] on_behavior_clicked");
    LV_UNUSED(e);

    auto& overlay = get_ams_behavior_overlay();
    if (!overlay.are_subjects_initialized()) {
        overlay.init_subjects();
        overlay.register_callbacks();
    }
    overlay.show(get_ams_settings_overlay().get_parent_screen());

    LVGL_SAFE_EVENT_CB_END();
}

void AmsSettingsOverlay::on_calibration_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsSettingsOverlay] on_calibration_clicked");
    LV_UNUSED(e);
    spdlog::info("[AmsSettingsOverlay] Calibration clicked (not yet implemented)");
    // TODO: Push calibration sub-panel
    LVGL_SAFE_EVENT_CB_END();
}

void AmsSettingsOverlay::on_speed_settings_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsSettingsOverlay] on_speed_settings_clicked");
    LV_UNUSED(e);
    spdlog::info("[AmsSettingsOverlay] Speed Settings clicked (not yet implemented)");
    // TODO: Push speed settings sub-panel
    LVGL_SAFE_EVENT_CB_END();
}

void AmsSettingsOverlay::on_spoolman_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsSettingsOverlay] on_spoolman_clicked");
    LV_UNUSED(e);

    auto& overlay = get_ams_spoolman_overlay();
    if (!overlay.are_subjects_initialized()) {
        overlay.init_subjects();
        overlay.register_callbacks();
    }

    // Set MoonrakerClient for database access
    MoonrakerClient* client = get_moonraker_client();
    if (client) {
        overlay.set_client(client);
    }

    overlay.show(get_ams_settings_overlay().get_parent_screen());

    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::ui
