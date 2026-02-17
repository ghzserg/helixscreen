// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_settings_sound.cpp
 * @brief Implementation of SoundSettingsOverlay
 */

#include "ui_settings_sound.h"

#include "ui_event_safety.h"
#include "ui_nav_manager.h"

#include "format_utils.h"
#include "settings_manager.h"
#include "sound_manager.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <memory>

namespace helix::settings {

// ============================================================================
// SINGLETON ACCESSOR
// ============================================================================

static std::unique_ptr<SoundSettingsOverlay> g_sound_settings_overlay;

SoundSettingsOverlay& get_sound_settings_overlay() {
    if (!g_sound_settings_overlay) {
        g_sound_settings_overlay = std::make_unique<SoundSettingsOverlay>();
        StaticPanelRegistry::instance().register_destroy(
            "SoundSettingsOverlay", []() { g_sound_settings_overlay.reset(); });
    }
    return *g_sound_settings_overlay;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

SoundSettingsOverlay::SoundSettingsOverlay() {
    spdlog::debug("[{}] Created", get_name());
}

SoundSettingsOverlay::~SoundSettingsOverlay() {
    if (subjects_initialized_ && lv_is_initialized()) {
        lv_subject_deinit(&volume_value_subject_);
    }
    spdlog::trace("[{}] Destroyed", get_name());
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void SoundSettingsOverlay::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    // Initialize volume value subject for label binding
    snprintf(volume_value_buf_, sizeof(volume_value_buf_), "80%%");
    lv_subject_init_string(&volume_value_subject_, volume_value_buf_, nullptr,
                           sizeof(volume_value_buf_), volume_value_buf_);
    lv_xml_register_subject(nullptr, "volume_value", &volume_value_subject_);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void SoundSettingsOverlay::register_callbacks() {
    lv_xml_register_event_cb(nullptr, "on_sounds_changed", on_sounds_changed);
    lv_xml_register_event_cb(nullptr, "on_ui_sounds_changed", on_ui_sounds_changed);
    lv_xml_register_event_cb(nullptr, "on_volume_changed", on_volume_changed);
    lv_xml_register_event_cb(nullptr, "on_sound_theme_changed", on_sound_theme_changed);
    lv_xml_register_event_cb(nullptr, "on_test_beep", on_test_beep);

    spdlog::debug("[{}] Callbacks registered", get_name());
}

// ============================================================================
// UI CREATION
// ============================================================================

lv_obj_t* SoundSettingsOverlay::create(lv_obj_t* parent) {
    if (overlay_root_) {
        spdlog::warn("[{}] create() called but overlay already exists", get_name());
        return overlay_root_;
    }

    spdlog::debug("[{}] Creating overlay...", get_name());

    overlay_root_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent, "sound_settings_overlay", nullptr));
    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    // Initially hidden until show() pushes it
    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    spdlog::info("[{}] Overlay created", get_name());
    return overlay_root_;
}

void SoundSettingsOverlay::show(lv_obj_t* parent_screen) {
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

    // Push onto navigation stack (on_activate will initialize widgets)
    ui_nav_push_overlay(overlay_root_);
}

// ============================================================================
// LIFECYCLE
// ============================================================================

void SoundSettingsOverlay::on_activate() {
    OverlayBase::on_activate();

    init_sounds_toggle();
    init_volume_slider();
    init_sound_theme_dropdown();
}

void SoundSettingsOverlay::on_deactivate() {
    OverlayBase::on_deactivate();
}

// ============================================================================
// INTERNAL METHODS
// ============================================================================

void SoundSettingsOverlay::init_sounds_toggle() {
    if (!overlay_root_)
        return;

    lv_obj_t* sounds_row = lv_obj_find_by_name(overlay_root_, "row_sounds");
    if (sounds_row) {
        lv_obj_t* toggle = lv_obj_find_by_name(sounds_row, "toggle");
        if (toggle) {
            if (SettingsManager::instance().get_sounds_enabled()) {
                lv_obj_add_state(toggle, LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(toggle, LV_STATE_CHECKED);
            }
            spdlog::trace("[{}]   Sounds toggle", get_name());
        }
    }
}

void SoundSettingsOverlay::init_volume_slider() {
    if (!overlay_root_)
        return;

    lv_obj_t* volume_row = lv_obj_find_by_name(overlay_root_, "row_volume");
    if (!volume_row)
        return;

    lv_obj_t* slider = lv_obj_find_by_name(volume_row, "slider");
    if (slider) {
        int volume = SettingsManager::instance().get_volume();
        lv_slider_set_value(slider, volume, LV_ANIM_OFF);

        // Update volume value label
        helix::format::format_percent(volume, volume_value_buf_, sizeof(volume_value_buf_));
        lv_subject_copy_string(&volume_value_subject_, volume_value_buf_);

        // Play test beep on release so user hears the new volume level
        // (XML component only exposes value_changed callback, so we add released here)
        lv_obj_add_event_cb(slider, on_volume_released, LV_EVENT_RELEASED, nullptr);

        spdlog::debug("[{}] Volume slider initialized to {}%", get_name(), volume);
    }

    // Update value label widget directly (setting_slider_row has a value_label)
    lv_obj_t* value_label = lv_obj_find_by_name(volume_row, "value_label");
    if (value_label) {
        lv_label_set_text(value_label, volume_value_buf_);
    }
}

void SoundSettingsOverlay::init_sound_theme_dropdown() {
    if (!overlay_root_)
        return;

    lv_obj_t* theme_row = lv_obj_find_by_name(overlay_root_, "row_sound_theme");
    if (!theme_row)
        return;

    lv_obj_t* dropdown = lv_obj_find_by_name(theme_row, "dropdown");
    if (dropdown) {
        auto& settings = SettingsManager::instance();
        auto themes = SoundManager::instance().get_available_themes();
        std::string current_theme = settings.get_sound_theme();

        // Build newline-separated options string
        std::string options;
        int selected_index = 0;
        for (int i = 0; i < static_cast<int>(themes.size()); i++) {
            if (i > 0)
                options += "\n";
            options += themes[i];
            if (themes[i] == current_theme) {
                selected_index = i;
            }
        }

        if (!options.empty()) {
            lv_dropdown_set_options(dropdown, options.c_str());
            lv_dropdown_set_selected(dropdown, static_cast<uint32_t>(selected_index));
        }
        spdlog::trace("[{}]   Sound theme dropdown ({} themes, current={})", get_name(),
                      themes.size(), current_theme);
    }
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

void SoundSettingsOverlay::handle_sounds_changed(bool enabled) {
    spdlog::info("[{}] Sounds toggled: {}", get_name(), enabled ? "ON" : "OFF");
    SettingsManager::instance().set_sounds_enabled(enabled);

    // Play test beep when enabling sounds
    if (enabled) {
        SoundManager::instance().play_test_beep();
    }
}

void SoundSettingsOverlay::handle_ui_sounds_changed(bool enabled) {
    spdlog::info("[{}] UI Sounds toggled: {}", get_name(), enabled ? "ON" : "OFF");
    SettingsManager::instance().set_ui_sounds_enabled(enabled);
}

void SoundSettingsOverlay::handle_volume_changed(int value) {
    SettingsManager::instance().set_volume(value);

    // Update value label subject
    helix::format::format_percent(value, volume_value_buf_, sizeof(volume_value_buf_));
    lv_subject_copy_string(&volume_value_subject_, volume_value_buf_);

    // Update value label widget directly
    if (overlay_root_) {
        lv_obj_t* volume_row = lv_obj_find_by_name(overlay_root_, "row_volume");
        if (volume_row) {
            lv_obj_t* value_label = lv_obj_find_by_name(volume_row, "value_label");
            if (value_label) {
                lv_label_set_text(value_label, volume_value_buf_);
            }
        }
    }
}

void SoundSettingsOverlay::handle_sound_theme_changed(int index) {
    auto themes = SoundManager::instance().get_available_themes();
    if (index >= 0 && index < static_cast<int>(themes.size())) {
        const auto& theme_name = themes[index];
        spdlog::info("[{}] Sound theme changed: {} (index {})", get_name(), theme_name, index);
        SettingsManager::instance().set_sound_theme(theme_name);
        SoundManager::instance().set_theme(theme_name);
        SoundManager::instance().play("test_beep");
    } else {
        spdlog::warn("[{}] Sound theme index {} out of range ({})", get_name(), index,
                     themes.size());
    }
}

void SoundSettingsOverlay::handle_test_beep() {
    spdlog::info("[{}] Test beep requested", get_name());
    SoundManager::instance().play_test_beep();
}

// ============================================================================
// STATIC CALLBACKS
// ============================================================================

void SoundSettingsOverlay::on_sounds_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SoundSettingsOverlay] on_sounds_changed");
    auto* toggle = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    get_sound_settings_overlay().handle_sounds_changed(enabled);
    LVGL_SAFE_EVENT_CB_END();
}

void SoundSettingsOverlay::on_ui_sounds_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SoundSettingsOverlay] on_ui_sounds_changed");
    auto* toggle = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    get_sound_settings_overlay().handle_ui_sounds_changed(enabled);
    LVGL_SAFE_EVENT_CB_END();
}

void SoundSettingsOverlay::on_volume_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SoundSettingsOverlay] on_volume_changed");
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int value = lv_slider_get_value(slider);
    get_sound_settings_overlay().handle_volume_changed(value);
    LVGL_SAFE_EVENT_CB_END();
}

void SoundSettingsOverlay::on_volume_released(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SoundSettingsOverlay] on_volume_released");
    SoundManager::instance().play_test_beep();
    LVGL_SAFE_EVENT_CB_END();
}

void SoundSettingsOverlay::on_sound_theme_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SoundSettingsOverlay] on_sound_theme_changed");
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int index = static_cast<int>(lv_dropdown_get_selected(dropdown));
    get_sound_settings_overlay().handle_sound_theme_changed(index);
    LVGL_SAFE_EVENT_CB_END();
}

void SoundSettingsOverlay::on_test_beep(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SoundSettingsOverlay] on_test_beep");
    get_sound_settings_overlay().handle_test_beep();
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::settings
