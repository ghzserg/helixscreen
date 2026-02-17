// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_settings_about.cpp
 * @brief Implementation of AboutOverlay
 */

#include "ui_settings_about.h"

#include "ui_nav_manager.h"

#include "app_globals.h"
#include "moonraker_api.h"
#include "printer_hardware.h"
#include "printer_state.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <memory>

namespace helix::settings {

// ============================================================================
// SINGLETON ACCESSOR
// ============================================================================

static std::unique_ptr<AboutOverlay> g_about_overlay;

AboutOverlay& get_about_overlay() {
    if (!g_about_overlay) {
        g_about_overlay = std::make_unique<AboutOverlay>();
        StaticPanelRegistry::instance().register_destroy("AboutOverlay",
                                                         []() { g_about_overlay.reset(); });
    }
    return *g_about_overlay;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

AboutOverlay::AboutOverlay() {
    spdlog::debug("[{}] Created", get_name());
}

AboutOverlay::~AboutOverlay() {
    // Widget-bound observers are auto-removed by LVGL when widget tree is destroyed
    spdlog::trace("[{}] Destroyed", get_name());
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void AboutOverlay::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    // No local subjects needed - all subjects are globally registered:
    // version_value, update_version_text, update_status, update_channel,
    // show_beta_features, print_hours_value

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void AboutOverlay::register_callbacks() {
    // All callbacks used by this overlay are already globally registered:
    // on_version_clicked, on_check_updates_clicked, on_install_update_clicked,
    // on_update_channel_changed
    // They are static free functions in ui_panel_settings.cpp

    spdlog::debug("[{}] Callbacks registered (reusing global)", get_name());
}

// ============================================================================
// UI CREATION
// ============================================================================

lv_obj_t* AboutOverlay::create(lv_obj_t* parent) {
    if (overlay_root_) {
        spdlog::warn("[{}] create() called but overlay already exists", get_name());
        return overlay_root_;
    }

    spdlog::debug("[{}] Creating overlay...", get_name());

    overlay_root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "about_overlay", nullptr));
    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    // Initially hidden until show() pushes it
    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    spdlog::info("[{}] Overlay created", get_name());
    return overlay_root_;
}

void AboutOverlay::show(lv_obj_t* parent_screen) {
    spdlog::debug("[{}] show() called", get_name());

    parent_screen_ = parent_screen;

    // Ensure subjects and callbacks are initialized
    if (!subjects_initialized_) {
        init_subjects();
        register_callbacks();
    }

    // Lazy create overlay
    if (!overlay_root_ && parent_screen_) {
        create(parent_screen_);
    }

    if (!overlay_root_) {
        spdlog::error("[{}] Cannot show - overlay not created", get_name());
        return;
    }

    // Register for lifecycle callbacks
    NavigationManager::instance().register_overlay_instance(overlay_root_, this);

    // Push onto navigation stack (on_activate will bind subjects)
    NavigationManager::instance().push_overlay(overlay_root_);
}

// ============================================================================
// LIFECYCLE
// ============================================================================

void AboutOverlay::on_activate() {
    OverlayBase::on_activate();

    bind_version_subjects();
    populate_mcu_rows();
}

// ============================================================================
// INTERNAL METHODS
// ============================================================================

void AboutOverlay::bind_version_subjects() {
    if (!overlay_root_) {
        return;
    }

    auto& printer_state = get_printer_state();

    // === Klipper Version ===
    lv_obj_t* klipper_row = lv_obj_find_by_name(overlay_root_, "row_klipper");
    if (klipper_row) {
        lv_obj_t* klipper_value = lv_obj_find_by_name(klipper_row, "value");
        if (klipper_value && !klipper_version_observer_) {
            klipper_version_observer_ = lv_label_bind_text(
                klipper_value, printer_state.get_klipper_version_subject(), "%s");
            spdlog::trace("[{}]   Klipper version bound to subject", get_name());
        }
    }

    // === Moonraker Version ===
    lv_obj_t* moonraker_row = lv_obj_find_by_name(overlay_root_, "row_moonraker");
    if (moonraker_row) {
        lv_obj_t* moonraker_value = lv_obj_find_by_name(moonraker_row, "value");
        if (moonraker_value && !moonraker_version_observer_) {
            moonraker_version_observer_ = lv_label_bind_text(
                moonraker_value, printer_state.get_moonraker_version_subject(), "%s");
            spdlog::trace("[{}]   Moonraker version bound to subject", get_name());
        }
    }

    // === OS Version ===
    lv_obj_t* os_row = lv_obj_find_by_name(overlay_root_, "row_os");
    if (os_row) {
        lv_obj_t* os_value = lv_obj_find_by_name(os_row, "value");
        if (os_value && !os_version_observer_) {
            os_version_observer_ =
                lv_label_bind_text(os_value, printer_state.get_os_version_subject(), "%s");
            spdlog::trace("[{}]   OS version bound to subject", get_name());
        }
    }

    // === Check for Updates (reactive description) ===
    lv_obj_t* check_updates_row = lv_obj_find_by_name(overlay_root_, "row_check_updates");
    if (check_updates_row) {
        lv_obj_t* description = lv_obj_find_by_name(check_updates_row, "description");
        lv_subject_t* version_text = lv_xml_get_subject(nullptr, "update_version_text");
        if (description && version_text) {
            lv_label_bind_text(description, version_text, "%s");
            spdlog::trace("[{}]   Check for Updates description bound", get_name());
        }
    }
}

void AboutOverlay::populate_mcu_rows() {
    if (!overlay_root_) {
        return;
    }

    MoonrakerAPI* api = get_moonraker_api();
    if (!api) {
        return;
    }

    const auto& mcu_versions = api->hardware().mcu_versions();
    if (mcu_versions.empty()) {
        return;
    }

    // Find overlay_content to add MCU rows
    lv_obj_t* content = lv_obj_find_by_name(overlay_root_, "overlay_content");
    if (!content) {
        return;
    }

    // Check if MCU rows already added (avoid duplicates on re-activate)
    lv_obj_t* existing_mcu = lv_obj_find_by_name(content, "row_mcu_primary");
    if (existing_mcu) {
        return;
    }

    for (const auto& [mcu_name, mcu_version] : mcu_versions) {
        // Format label: "MCU" for primary, "MCU EBBCan" etc for secondaries
        std::string label = "MCU";
        if (mcu_name != "mcu") {
            label = "MCU" + mcu_name.substr(3);
        }

        // Unique name for duplicate detection
        std::string row_name = (mcu_name == "mcu") ? "row_mcu_primary" : "row_mcu_" + mcu_name;

        const char* attrs[] = {"name",      row_name.c_str(), "label", label.c_str(),
                               "label_tag", label.c_str(),    "icon",  "code_braces",
                               nullptr,     nullptr};
        lv_obj_t* row = static_cast<lv_obj_t*>(lv_xml_create(content, "setting_info_row", attrs));
        if (row) {
            lv_obj_t* value_label = lv_obj_find_by_name(row, "value");
            if (value_label) {
                std::string display_version = mcu_version;
                if (display_version.length() > 30) {
                    display_version = display_version.substr(0, 27) + "...";
                }
                lv_label_set_text(value_label, display_version.c_str());
            }

            spdlog::trace("[{}]   MCU row: {} = {}", get_name(), label, mcu_version);
        }
    }
}

} // namespace helix::settings
