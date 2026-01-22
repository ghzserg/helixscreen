// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_settings_display.cpp
 * @brief Implementation of DisplaySettingsOverlay
 */

#include "ui_settings_display.h"

#include "ui_event_safety.h"
#include "ui_nav_manager.h"
#include "ui_theme_editor_overlay.h"
#include "ui_utils.h"

#include "settings_manager.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <memory>

namespace helix::settings {

// ============================================================================
// SINGLETON ACCESSOR
// ============================================================================

static std::unique_ptr<DisplaySettingsOverlay> g_display_settings_overlay;

DisplaySettingsOverlay& get_display_settings_overlay() {
    if (!g_display_settings_overlay) {
        g_display_settings_overlay = std::make_unique<DisplaySettingsOverlay>();
        StaticPanelRegistry::instance().register_destroy(
            "DisplaySettingsOverlay", []() { g_display_settings_overlay.reset(); });
    }
    return *g_display_settings_overlay;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

DisplaySettingsOverlay::DisplaySettingsOverlay() {
    spdlog::debug("[{}] Created", get_name());
}

DisplaySettingsOverlay::~DisplaySettingsOverlay() {
    if (subjects_initialized_ && lv_is_initialized()) {
        lv_subject_deinit(&brightness_value_subject_);
    }
    spdlog::debug("[{}] Destroyed", get_name());
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void DisplaySettingsOverlay::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    // Initialize brightness value subject for label binding
    // 5-arg form: (subject, buf, prev_buf, size, initial_value)
    snprintf(brightness_value_buf_, sizeof(brightness_value_buf_), "100%%");
    lv_subject_init_string(&brightness_value_subject_, brightness_value_buf_, nullptr,
                           sizeof(brightness_value_buf_), brightness_value_buf_);
    lv_xml_register_subject(nullptr, "brightness_value", &brightness_value_subject_);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void DisplaySettingsOverlay::register_callbacks() {
    // Brightness slider callback
    lv_xml_register_event_cb(nullptr, "on_brightness_changed", on_brightness_changed);
    lv_xml_register_event_cb(nullptr, "on_theme_preset_changed", on_theme_preset_changed);
    lv_xml_register_event_cb(nullptr, "on_theme_preview_clicked", on_theme_preview_clicked);
    lv_xml_register_event_cb(nullptr, "on_theme_settings_clicked", on_theme_settings_clicked);

    spdlog::debug("[{}] Callbacks registered", get_name());
}

// ============================================================================
// UI CREATION
// ============================================================================

lv_obj_t* DisplaySettingsOverlay::create(lv_obj_t* parent) {
    if (overlay_) {
        spdlog::warn("[{}] create() called but overlay already exists", get_name());
        return overlay_;
    }

    spdlog::debug("[{}] Creating overlay...", get_name());

    // Create from XML component
    overlay_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "display_settings_overlay", nullptr));
    if (!overlay_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    // Initially hidden until show() pushes it
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);

    spdlog::info("[{}] Overlay created", get_name());
    return overlay_;
}

void DisplaySettingsOverlay::show(lv_obj_t* parent_screen) {
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

    // Initialize all widget values from SettingsManager
    init_brightness_controls();
    init_sleep_dropdown();
    init_bed_mesh_dropdown();
    init_gcode_dropdown();
    init_time_format_dropdown();

    // Push onto navigation stack
    ui_nav_push_overlay(overlay_);
}

// ============================================================================
// INTERNAL METHODS
// ============================================================================

void DisplaySettingsOverlay::init_brightness_controls() {
    if (!overlay_)
        return;

    lv_obj_t* brightness_slider = lv_obj_find_by_name(overlay_, "brightness_slider");
    if (brightness_slider) {
        // Set initial value from settings
        int brightness = SettingsManager::instance().get_brightness();
        lv_slider_set_value(brightness_slider, brightness, LV_ANIM_OFF);

        // Update subject (label binding happens in XML)
        snprintf(brightness_value_buf_, sizeof(brightness_value_buf_), "%d%%", brightness);
        lv_subject_copy_string(&brightness_value_subject_, brightness_value_buf_);

        spdlog::debug("[{}] Brightness initialized to {}%", get_name(), brightness);
    }
}

void DisplaySettingsOverlay::init_sleep_dropdown() {
    if (!overlay_)
        return;

    lv_obj_t* sleep_row = lv_obj_find_by_name(overlay_, "row_display_sleep");
    lv_obj_t* sleep_dropdown = sleep_row ? lv_obj_find_by_name(sleep_row, "dropdown") : nullptr;
    if (sleep_dropdown) {
        // Set dropdown options
        lv_dropdown_set_options(sleep_dropdown,
                                "Never\n1 minute\n5 minutes\n10 minutes\n30 minutes");

        // Set initial selection based on current setting
        int current_sec = SettingsManager::instance().get_display_sleep_sec();
        int index = SettingsManager::sleep_seconds_to_index(current_sec);
        lv_dropdown_set_selected(sleep_dropdown, index);

        spdlog::debug("[{}] Sleep dropdown initialized to index {} ({}s)", get_name(), index,
                      current_sec);
    }
}

void DisplaySettingsOverlay::init_bed_mesh_dropdown() {
    if (!overlay_)
        return;

    lv_obj_t* bed_mesh_row = lv_obj_find_by_name(overlay_, "row_bed_mesh_mode");
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
                      current_mode, current_mode == 0 ? "Auto" : (current_mode == 1 ? "3D" : "2D"));
    }
}

void DisplaySettingsOverlay::init_gcode_dropdown() {
    if (!overlay_)
        return;

    // G-code mode row is hidden by default, but we still initialize it
    lv_obj_t* gcode_row = lv_obj_find_by_name(overlay_, "row_gcode_mode");
    lv_obj_t* gcode_dropdown = gcode_row ? lv_obj_find_by_name(gcode_row, "dropdown") : nullptr;
    if (gcode_dropdown) {
        // Set dropdown options
        lv_dropdown_set_options(gcode_dropdown, SettingsManager::get_gcode_render_mode_options());

        // Set initial selection based on current setting
        int current_mode = SettingsManager::instance().get_gcode_render_mode();
        lv_dropdown_set_selected(gcode_dropdown, current_mode);

        spdlog::debug("[{}] G-code mode dropdown initialized to {} ({})", get_name(), current_mode,
                      current_mode == 0 ? "Auto" : (current_mode == 1 ? "3D" : "2D Layers"));
    }
}

void DisplaySettingsOverlay::init_theme_preset_dropdown(lv_obj_t* root) {
    if (!root)
        return;

    lv_obj_t* theme_preset_row = lv_obj_find_by_name(root, "row_theme_preset");
    lv_obj_t* theme_preset_dropdown =
        theme_preset_row ? lv_obj_find_by_name(theme_preset_row, "dropdown") : nullptr;
    if (theme_preset_dropdown) {
        // Set dropdown options from discovered theme files
        std::string options = SettingsManager::instance().get_theme_options();
        lv_dropdown_set_options(theme_preset_dropdown, options.c_str());

        // Set initial selection based on current theme
        int current_index = SettingsManager::instance().get_theme_index();
        lv_dropdown_set_selected(theme_preset_dropdown, static_cast<uint32_t>(current_index));

        spdlog::debug("[{}] Theme dropdown initialized to index {} ({})", get_name(), current_index,
                      SettingsManager::instance().get_theme_name());
    }
}

void DisplaySettingsOverlay::init_time_format_dropdown() {
    if (!overlay_)
        return;

    lv_obj_t* time_format_row = lv_obj_find_by_name(overlay_, "row_time_format");
    lv_obj_t* time_format_dropdown =
        time_format_row ? lv_obj_find_by_name(time_format_row, "dropdown") : nullptr;
    if (time_format_dropdown) {
        // Set dropdown options
        lv_dropdown_set_options(time_format_dropdown, SettingsManager::get_time_format_options());

        // Set initial selection based on current setting
        auto current_format = SettingsManager::instance().get_time_format();
        lv_dropdown_set_selected(time_format_dropdown, static_cast<uint32_t>(current_format));

        spdlog::debug("[{}] Time format dropdown initialized to {} ({})", get_name(),
                      static_cast<int>(current_format),
                      current_format == TimeFormat::HOUR_12 ? "12H" : "24H");
    }
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

void DisplaySettingsOverlay::handle_brightness_changed(int value) {
    SettingsManager::instance().set_brightness(value);

    // Update subject (label binding happens in XML)
    snprintf(brightness_value_buf_, sizeof(brightness_value_buf_), "%d%%", value);
    lv_subject_copy_string(&brightness_value_subject_, brightness_value_buf_);
}

void DisplaySettingsOverlay::handle_theme_preset_changed(int index) {
    SettingsManager::instance().set_theme_by_index(index);

    spdlog::info("[{}] Theme changed to index {} ({})", get_name(), index,
                 SettingsManager::instance().get_theme_name());
}

void DisplaySettingsOverlay::handle_theme_preview_clicked() {
    if (!parent_screen_) {
        spdlog::warn("[{}] Theme preview clicked without parent screen", get_name());
        return;
    }

    if (!theme_preview_overlay_) {
        spdlog::debug("[{}] Creating theme preview overlay...", get_name());
        theme_preview_overlay_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "theme_preview_overlay", nullptr));
        if (!theme_preview_overlay_) {
            spdlog::error("[{}] Failed to create theme preview overlay", get_name());
            return;
        }

        lv_obj_add_flag(theme_preview_overlay_, LV_OBJ_FLAG_HIDDEN);
        NavigationManager::instance().register_overlay_close_callback(
            theme_preview_overlay_, [this]() { lv_obj_safe_delete(theme_preview_overlay_); });
    }

    ui_nav_push_overlay(theme_preview_overlay_);
}

void DisplaySettingsOverlay::handle_theme_settings_clicked() {
    if (!parent_screen_) {
        spdlog::warn("[{}] Theme settings clicked without parent screen", get_name());
        return;
    }

    // Create theme editor overlay on first access (lazy initialization)
    if (!theme_settings_overlay_) {
        spdlog::debug("[{}] Creating theme editor overlay...", get_name());
        auto& overlay = get_theme_editor_overlay();

        // Initialize subjects and callbacks if not already done
        if (!overlay.are_subjects_initialized()) {
            overlay.init_subjects();
        }
        overlay.register_callbacks();

        // Create overlay UI
        theme_settings_overlay_ = overlay.create(parent_screen_);
        if (!theme_settings_overlay_) {
            spdlog::error("[{}] Failed to create theme editor overlay", get_name());
            return;
        }

        // Register with NavigationManager for lifecycle callbacks
        NavigationManager::instance().register_overlay_instance(theme_settings_overlay_, &overlay);
    }

    if (theme_settings_overlay_) {
        // Load current theme for editing
        auto theme_name = SettingsManager::instance().get_theme_name();
        get_theme_editor_overlay().load_theme(theme_name);
        ui_nav_push_overlay(theme_settings_overlay_);
    }
}

// ============================================================================
// STATIC CALLBACKS
// ============================================================================

void DisplaySettingsOverlay::on_brightness_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[DisplaySettingsOverlay] on_brightness_changed");
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int value = lv_slider_get_value(slider);
    get_display_settings_overlay().handle_brightness_changed(value);
    LVGL_SAFE_EVENT_CB_END();
}

void DisplaySettingsOverlay::on_theme_preset_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[DisplaySettingsOverlay] on_theme_preset_changed");
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int index = lv_dropdown_get_selected(dropdown);
    get_display_settings_overlay().handle_theme_preset_changed(index);
    LVGL_SAFE_EVENT_CB_END();
}

void DisplaySettingsOverlay::on_theme_preview_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[DisplaySettingsOverlay] on_theme_preview_clicked");
    static_cast<void>(lv_event_get_current_target(e));
    get_display_settings_overlay().handle_theme_preview_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void DisplaySettingsOverlay::on_theme_settings_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[DisplaySettingsOverlay] on_theme_settings_clicked");
    static_cast<void>(lv_event_get_current_target(e));
    get_display_settings_overlay().handle_theme_settings_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::settings
