// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_ams_settings_overlay.cpp
 * @brief Implementation of AmsSettingsOverlay
 */

#include "ui_ams_settings_overlay.h"

#include "ui_ams_behavior_overlay.h"
#include "ui_ams_device_actions_overlay.h"
#include "ui_ams_device_operations_overlay.h"
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
        lv_subject_deinit(&tool_mapping_summary_subject_);
        lv_subject_deinit(&endless_spool_summary_subject_);
        lv_subject_deinit(&device_operations_summary_subject_);
        lv_subject_deinit(&maintenance_summary_subject_);
        lv_subject_deinit(&behavior_summary_subject_);
        lv_subject_deinit(&calibration_summary_subject_);
        lv_subject_deinit(&speed_summary_subject_);
        lv_subject_deinit(&spoolman_summary_subject_);
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

    // Initialize navigation row summary subjects
    snprintf(tool_mapping_summary_buf_, sizeof(tool_mapping_summary_buf_), "");
    lv_subject_init_string(&tool_mapping_summary_subject_, tool_mapping_summary_buf_, nullptr,
                           sizeof(tool_mapping_summary_buf_), tool_mapping_summary_buf_);
    lv_xml_register_subject(nullptr, "ams_settings_tool_mapping_summary",
                            &tool_mapping_summary_subject_);

    snprintf(endless_spool_summary_buf_, sizeof(endless_spool_summary_buf_), "");
    lv_subject_init_string(&endless_spool_summary_subject_, endless_spool_summary_buf_, nullptr,
                           sizeof(endless_spool_summary_buf_), endless_spool_summary_buf_);
    lv_xml_register_subject(nullptr, "ams_settings_endless_spool_summary",
                            &endless_spool_summary_subject_);

    snprintf(device_operations_summary_buf_, sizeof(device_operations_summary_buf_), "");
    lv_subject_init_string(&device_operations_summary_subject_, device_operations_summary_buf_,
                           nullptr, sizeof(device_operations_summary_buf_),
                           device_operations_summary_buf_);
    lv_xml_register_subject(nullptr, "ams_settings_device_operations_summary",
                            &device_operations_summary_subject_);

    snprintf(maintenance_summary_buf_, sizeof(maintenance_summary_buf_), "");
    lv_subject_init_string(&maintenance_summary_subject_, maintenance_summary_buf_, nullptr,
                           sizeof(maintenance_summary_buf_), maintenance_summary_buf_);
    lv_xml_register_subject(nullptr, "ams_settings_maintenance_summary",
                            &maintenance_summary_subject_);

    snprintf(behavior_summary_buf_, sizeof(behavior_summary_buf_), "");
    lv_subject_init_string(&behavior_summary_subject_, behavior_summary_buf_, nullptr,
                           sizeof(behavior_summary_buf_), behavior_summary_buf_);
    lv_xml_register_subject(nullptr, "ams_settings_behavior_summary", &behavior_summary_subject_);

    snprintf(calibration_summary_buf_, sizeof(calibration_summary_buf_), "");
    lv_subject_init_string(&calibration_summary_subject_, calibration_summary_buf_, nullptr,
                           sizeof(calibration_summary_buf_), calibration_summary_buf_);
    lv_xml_register_subject(nullptr, "ams_settings_calibration_summary",
                            &calibration_summary_subject_);

    snprintf(speed_summary_buf_, sizeof(speed_summary_buf_), "");
    lv_subject_init_string(&speed_summary_subject_, speed_summary_buf_, nullptr,
                           sizeof(speed_summary_buf_), speed_summary_buf_);
    lv_xml_register_subject(nullptr, "ams_settings_speed_summary", &speed_summary_subject_);

    snprintf(spoolman_summary_buf_, sizeof(spoolman_summary_buf_), "");
    lv_subject_init_string(&spoolman_summary_subject_, spoolman_summary_buf_, nullptr,
                           sizeof(spoolman_summary_buf_), spoolman_summary_buf_);
    lv_xml_register_subject(nullptr, "ams_settings_spoolman_summary", &spoolman_summary_subject_);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void AmsSettingsOverlay::register_callbacks() {
    // Register all navigation row callbacks
    lv_xml_register_event_cb(nullptr, "on_ams_settings_tool_mapping_clicked",
                             on_tool_mapping_clicked);
    lv_xml_register_event_cb(nullptr, "on_ams_settings_endless_spool_clicked",
                             on_endless_spool_clicked);
    lv_xml_register_event_cb(nullptr, "on_ams_settings_device_operations_clicked",
                             on_device_operations_clicked);
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

    // Update navigation row summaries
    update_nav_summaries();

    // Register with NavigationManager for lifecycle callbacks
    NavigationManager::instance().register_overlay_instance(overlay_, this);

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

    // Update version subject (hide "unknown" placeholder)
    if (!info.version.empty() && info.version != "unknown") {
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

void AmsSettingsOverlay::update_nav_summaries() {
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        // Clear all summaries when no backend
        snprintf(tool_mapping_summary_buf_, sizeof(tool_mapping_summary_buf_), "");
        lv_subject_copy_string(&tool_mapping_summary_subject_, tool_mapping_summary_buf_);

        snprintf(endless_spool_summary_buf_, sizeof(endless_spool_summary_buf_), "");
        lv_subject_copy_string(&endless_spool_summary_subject_, endless_spool_summary_buf_);

        snprintf(device_operations_summary_buf_, sizeof(device_operations_summary_buf_), "");
        lv_subject_copy_string(&device_operations_summary_subject_, device_operations_summary_buf_);

        snprintf(maintenance_summary_buf_, sizeof(maintenance_summary_buf_), "");
        lv_subject_copy_string(&maintenance_summary_subject_, maintenance_summary_buf_);

        snprintf(behavior_summary_buf_, sizeof(behavior_summary_buf_), "");
        lv_subject_copy_string(&behavior_summary_subject_, behavior_summary_buf_);

        snprintf(calibration_summary_buf_, sizeof(calibration_summary_buf_), "");
        lv_subject_copy_string(&calibration_summary_subject_, calibration_summary_buf_);

        snprintf(speed_summary_buf_, sizeof(speed_summary_buf_), "");
        lv_subject_copy_string(&speed_summary_subject_, speed_summary_buf_);

        snprintf(spoolman_summary_buf_, sizeof(spoolman_summary_buf_), "");
        lv_subject_copy_string(&spoolman_summary_subject_, spoolman_summary_buf_);
        return;
    }

    // Tool Mapping summary: show tool count if supported
    auto tool_mapping_caps = backend->get_tool_mapping_capabilities();
    if (tool_mapping_caps.supported) {
        auto mapping = backend->get_tool_mapping();
        int tool_count = static_cast<int>(mapping.size());
        snprintf(tool_mapping_summary_buf_, sizeof(tool_mapping_summary_buf_), "%d tool%s",
                 tool_count, tool_count == 1 ? "" : "s");
    } else {
        snprintf(tool_mapping_summary_buf_, sizeof(tool_mapping_summary_buf_), "");
    }
    lv_subject_copy_string(&tool_mapping_summary_subject_, tool_mapping_summary_buf_);

    // Endless Spool summary: count pairs with backups configured
    auto es_caps = backend->get_endless_spool_capabilities();
    if (es_caps.supported) {
        auto configs = backend->get_endless_spool_config();
        int pair_count = 0;
        for (const auto& config : configs) {
            if (config.backup_slot >= 0) {
                pair_count++;
            }
        }
        if (pair_count > 0) {
            snprintf(endless_spool_summary_buf_, sizeof(endless_spool_summary_buf_), "%d pair%s",
                     pair_count, pair_count == 1 ? "" : "s");
        } else {
            snprintf(endless_spool_summary_buf_, sizeof(endless_spool_summary_buf_), "None");
        }
    } else {
        snprintf(endless_spool_summary_buf_, sizeof(endless_spool_summary_buf_), "");
    }
    lv_subject_copy_string(&endless_spool_summary_subject_, endless_spool_summary_buf_);

    // Device Operations summary: count total actions (calibration + speed)
    auto actions = backend->get_device_actions();
    int device_ops_count = 0;
    for (const auto& action : actions) {
        if (action.section == "calibration" || action.section == "speed") {
            device_ops_count++;
        }
    }
    if (device_ops_count > 0) {
        snprintf(device_operations_summary_buf_, sizeof(device_operations_summary_buf_),
                 "%d action%s", device_ops_count, device_ops_count == 1 ? "" : "s");
    } else {
        snprintf(device_operations_summary_buf_, sizeof(device_operations_summary_buf_), "");
    }
    lv_subject_copy_string(&device_operations_summary_subject_, device_operations_summary_buf_);

    // Maintenance summary: leave empty for now
    snprintf(maintenance_summary_buf_, sizeof(maintenance_summary_buf_), "");
    lv_subject_copy_string(&maintenance_summary_subject_, maintenance_summary_buf_);

    // Behavior summary: leave empty for now
    snprintf(behavior_summary_buf_, sizeof(behavior_summary_buf_), "");
    lv_subject_copy_string(&behavior_summary_subject_, behavior_summary_buf_);

    // Calibration summary: count actions in calibration section
    int calibration_count = 0;
    for (const auto& action : actions) {
        if (action.section == "calibration") {
            calibration_count++;
        }
    }
    if (calibration_count > 0) {
        snprintf(calibration_summary_buf_, sizeof(calibration_summary_buf_), "%d action%s",
                 calibration_count, calibration_count == 1 ? "" : "s");
    } else {
        snprintf(calibration_summary_buf_, sizeof(calibration_summary_buf_), "");
    }
    lv_subject_copy_string(&calibration_summary_subject_, calibration_summary_buf_);

    // Speed Settings summary: count actions in speed section
    int speed_count = 0;
    for (const auto& action : actions) {
        if (action.section == "speed") {
            speed_count++;
        }
    }
    if (speed_count > 0) {
        snprintf(speed_summary_buf_, sizeof(speed_summary_buf_), "%d setting%s", speed_count,
                 speed_count == 1 ? "" : "s");
    } else {
        snprintf(speed_summary_buf_, sizeof(speed_summary_buf_), "");
    }
    lv_subject_copy_string(&speed_summary_subject_, speed_summary_buf_);

    // Spoolman summary: leave empty for now
    snprintf(spoolman_summary_buf_, sizeof(spoolman_summary_buf_), "");
    lv_subject_copy_string(&spoolman_summary_subject_, spoolman_summary_buf_);

    spdlog::debug("[{}] Navigation summaries updated", get_name());
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

void AmsSettingsOverlay::on_device_operations_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsSettingsOverlay] on_device_operations_clicked");
    LV_UNUSED(e);

    auto& overlay = get_ams_device_operations_overlay();
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

    auto& overlay = get_ams_device_actions_overlay();
    if (!overlay.are_subjects_initialized()) {
        overlay.init_subjects();
        overlay.register_callbacks();
    }
    overlay.set_filter("calibration");
    overlay.show(get_ams_settings_overlay().get_parent_screen());

    LVGL_SAFE_EVENT_CB_END();
}

void AmsSettingsOverlay::on_speed_settings_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsSettingsOverlay] on_speed_settings_clicked");
    LV_UNUSED(e);

    auto& overlay = get_ams_device_actions_overlay();
    if (!overlay.are_subjects_initialized()) {
        overlay.init_subjects();
        overlay.register_callbacks();
    }
    overlay.set_filter("speed");
    overlay.show(get_ams_settings_overlay().get_parent_screen());

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
