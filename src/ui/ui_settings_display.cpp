// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_settings_display.cpp
 * @brief Implementation of DisplaySettingsOverlay
 */

#include "ui_settings_display.h"

#include "ui_event_safety.h"
#include "ui_modal.h"
#include "ui_nav_manager.h"
#include "ui_theme_editor_overlay.h"
#include "ui_toast_manager.h"
#include "ui_utils.h"

#include "format_utils.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "settings_manager.h"
#include "static_panel_registry.h"
#include "theme_manager.h"

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
        lv_subject_deinit(&theme_apply_disabled_subject_);
    }
    spdlog::trace("[{}] Destroyed", get_name());
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

    // Initialize theme Apply button disabled subject (1=disabled initially)
    lv_subject_init_int(&theme_apply_disabled_subject_, 1);
    lv_xml_register_subject(nullptr, "theme_apply_disabled", &theme_apply_disabled_subject_);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void DisplaySettingsOverlay::register_callbacks() {
    // Brightness slider callback
    lv_xml_register_event_cb(nullptr, "on_brightness_changed", on_brightness_changed);

    // Sleep while printing toggle
    lv_xml_register_event_cb(nullptr, "on_sleep_while_printing_changed",
                             on_sleep_while_printing_changed);

    // Theme explorer callbacks (primary panel)
    lv_xml_register_event_cb(nullptr, "on_theme_preset_changed", on_theme_preset_changed);
    lv_xml_register_event_cb(nullptr, "on_theme_settings_clicked", on_theme_settings_clicked);
    lv_xml_register_event_cb(nullptr, "on_preview_dark_mode_toggled", on_preview_dark_mode_toggled);
    lv_xml_register_event_cb(nullptr, "on_edit_colors_clicked", on_edit_colors_clicked);
    lv_xml_register_event_cb(nullptr, "on_preview_open_modal", on_preview_open_modal);

    // Apply button uses header_bar's action_button mechanism
    // The overlay_panel passes action_button_callback through, so we need to register it
    lv_xml_register_event_cb(nullptr, "on_apply_theme_clicked", on_apply_theme_clicked);

    spdlog::debug("[{}] Callbacks registered", get_name());
}

// ============================================================================
// UI CREATION
// ============================================================================

lv_obj_t* DisplaySettingsOverlay::create(lv_obj_t* parent) {
    if (overlay_root_) {
        spdlog::warn("[{}] create() called but overlay already exists", get_name());
        return overlay_root_;
    }

    spdlog::debug("[{}] Creating overlay...", get_name());

    // Create from XML component
    overlay_root_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent, "display_settings_overlay", nullptr));
    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    // Initially hidden until show() pushes it
    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    spdlog::info("[{}] Overlay created", get_name());
    return overlay_root_;
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
    if (!overlay_root_ && parent_screen_) {
        create(parent_screen_);
    }

    if (!overlay_root_) {
        spdlog::error("[{}] Cannot show - overlay not created", get_name());
        return;
    }

    // Register for lifecycle callbacks
    NavigationManager::instance().register_overlay_instance(overlay_root_, this);

    // Push onto navigation stack (on_activate will initialize dropdowns)
    ui_nav_push_overlay(overlay_root_);
}

// ============================================================================
// LIFECYCLE
// ============================================================================

void DisplaySettingsOverlay::on_activate() {
    OverlayBase::on_activate();

    // Initialize all widget values from SettingsManager
    init_brightness_controls();
    init_dim_dropdown();
    init_sleep_dropdown();
    init_sleep_while_printing_toggle();
    init_bed_mesh_dropdown();
    init_gcode_dropdown();
    init_time_format_dropdown();
}

void DisplaySettingsOverlay::on_deactivate() {
    OverlayBase::on_deactivate();
}

// ============================================================================
// INTERNAL METHODS
// ============================================================================

void DisplaySettingsOverlay::init_brightness_controls() {
    if (!overlay_root_)
        return;

    lv_obj_t* brightness_slider = lv_obj_find_by_name(overlay_root_, "brightness_slider");
    if (brightness_slider) {
        // Set initial value from settings
        int brightness = SettingsManager::instance().get_brightness();
        lv_slider_set_value(brightness_slider, brightness, LV_ANIM_OFF);

        // Update subject (label binding happens in XML)
        helix::format::format_percent(brightness, brightness_value_buf_,
                                      sizeof(brightness_value_buf_));
        lv_subject_copy_string(&brightness_value_subject_, brightness_value_buf_);

        spdlog::debug("[{}] Brightness initialized to {}%", get_name(), brightness);
    }
}

void DisplaySettingsOverlay::init_dim_dropdown() {
    if (!overlay_root_)
        return;

    lv_obj_t* dim_row = lv_obj_find_by_name(overlay_root_, "row_display_dim");
    lv_obj_t* dim_dropdown = dim_row ? lv_obj_find_by_name(dim_row, "dropdown") : nullptr;
    if (dim_dropdown) {
        // Set initial selection based on current setting (options set in XML)
        int current_sec = SettingsManager::instance().get_display_dim_sec();
        int index = SettingsManager::dim_seconds_to_index(current_sec);
        lv_dropdown_set_selected(dim_dropdown, index);

        spdlog::debug("[{}] Dim dropdown initialized to index {} ({}s)", get_name(), index,
                      current_sec);
    }
}

void DisplaySettingsOverlay::init_sleep_dropdown() {
    if (!overlay_root_)
        return;

    lv_obj_t* sleep_row = lv_obj_find_by_name(overlay_root_, "row_display_sleep");
    lv_obj_t* sleep_dropdown = sleep_row ? lv_obj_find_by_name(sleep_row, "dropdown") : nullptr;
    if (sleep_dropdown) {
        // Set initial selection based on current setting (options set in XML)
        int current_sec = SettingsManager::instance().get_display_sleep_sec();
        int index = SettingsManager::sleep_seconds_to_index(current_sec);
        lv_dropdown_set_selected(sleep_dropdown, index);

        spdlog::debug("[{}] Sleep dropdown initialized to index {} ({}s)", get_name(), index,
                      current_sec);
    }
}

void DisplaySettingsOverlay::init_sleep_while_printing_toggle() {
    if (!overlay_root_)
        return;

    lv_obj_t* row = lv_obj_find_by_name(overlay_root_, "row_sleep_while_printing");
    if (!row)
        return;

    lv_obj_t* toggle = lv_obj_find_by_name(row, "toggle");
    if (toggle) {
        if (SettingsManager::instance().get_sleep_while_printing()) {
            lv_obj_add_state(toggle, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(toggle, LV_STATE_CHECKED);
        }
        spdlog::trace("[{}]   ✓ Sleep while printing toggle", get_name());
    }
}

void DisplaySettingsOverlay::init_bed_mesh_dropdown() {
    if (!overlay_root_)
        return;

    lv_obj_t* bed_mesh_row = lv_obj_find_by_name(overlay_root_, "row_bed_mesh_mode");
    lv_obj_t* bed_mesh_dropdown =
        bed_mesh_row ? lv_obj_find_by_name(bed_mesh_row, "dropdown") : nullptr;
    if (bed_mesh_dropdown) {
        // Set initial selection based on current setting (options set in XML)
        int current_mode = SettingsManager::instance().get_bed_mesh_render_mode();
        lv_dropdown_set_selected(bed_mesh_dropdown, current_mode);

        spdlog::debug("[{}] Bed mesh mode dropdown initialized to {} ({})", get_name(),
                      current_mode, current_mode == 0 ? "Auto" : (current_mode == 1 ? "3D" : "2D"));
    }
}

void DisplaySettingsOverlay::init_gcode_dropdown() {
    if (!overlay_root_)
        return;

    // G-code mode row is hidden by default, but we still initialize it
    lv_obj_t* gcode_row = lv_obj_find_by_name(overlay_root_, "row_gcode_mode");
    lv_obj_t* gcode_dropdown = gcode_row ? lv_obj_find_by_name(gcode_row, "dropdown") : nullptr;
    if (gcode_dropdown) {
        // Set initial selection based on current setting (options set in XML)
        int current_mode = SettingsManager::instance().get_gcode_render_mode();
        lv_dropdown_set_selected(gcode_dropdown, current_mode);

        spdlog::debug("[{}] G-code mode dropdown initialized to {} ({})", get_name(), current_mode,
                      current_mode == 0 ? "Auto" : (current_mode == 1 ? "3D" : "2D Layers"));
    }
}

void DisplaySettingsOverlay::init_theme_preset_dropdown(lv_obj_t* root) {
    if (!root)
        return;

    lv_obj_t* theme_preset_dropdown = lv_obj_find_by_name(root, "theme_preset_dropdown");
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
    if (!overlay_root_)
        return;

    lv_obj_t* time_format_row = lv_obj_find_by_name(overlay_root_, "row_time_format");
    lv_obj_t* time_format_dropdown =
        time_format_row ? lv_obj_find_by_name(time_format_row, "dropdown") : nullptr;
    if (time_format_dropdown) {
        // Set initial selection based on current setting (options set in XML)
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

void DisplaySettingsOverlay::handle_sleep_while_printing_changed(bool enabled) {
    spdlog::info("[{}] Sleep while printing toggled: {}", get_name(), enabled ? "ON" : "OFF");
    SettingsManager::instance().set_sleep_while_printing(enabled);
}

void DisplaySettingsOverlay::handle_brightness_changed(int value) {
    SettingsManager::instance().set_brightness(value);

    // Update subject (label binding happens in XML)
    helix::format::format_percent(value, brightness_value_buf_, sizeof(brightness_value_buf_));
    lv_subject_copy_string(&brightness_value_subject_, brightness_value_buf_);
}

void DisplaySettingsOverlay::handle_theme_preset_changed(int index) {
    // If called from Theme Explorer, preview the theme locally
    if (theme_explorer_overlay_ && lv_obj_is_visible(theme_explorer_overlay_)) {
        handle_explorer_theme_changed(index);
        return;
    }

    // Otherwise fall back to global theme change (legacy behavior)
    SettingsManager::instance().set_theme_by_index(index);

    spdlog::info("[{}] Theme changed to index {} ({})", get_name(), index,
                 SettingsManager::instance().get_theme_name());
}

void DisplaySettingsOverlay::handle_explorer_theme_changed(int index) {
    // Preview selected theme without saving globally
    // Use cached theme list (populated when explorer opens)
    if (index < 0 || index >= static_cast<int>(cached_themes_.size())) {
        spdlog::error("[{}] Invalid theme index {}", get_name(), index);
        return;
    }

    std::string theme_name = cached_themes_[index].filename;
    helix::ThemeData theme = helix::load_theme_from_file(theme_name);

    if (!theme.is_valid()) {
        spdlog::error("[{}] Failed to load theme '{}' for preview", get_name(), theme_name);
        return;
    }

    // Store for passing to editor
    preview_theme_name_ = theme_name;

    // Check theme's mode support and update toggle accordingly
    bool supports_dark = theme.supports_dark();
    bool supports_light = theme.supports_light();

    if (theme_explorer_overlay_) {
        lv_obj_t* dark_toggle =
            lv_obj_find_by_name(theme_explorer_overlay_, "preview_dark_mode_toggle");
        lv_obj_t* toggle_container =
            lv_obj_find_by_name(theme_explorer_overlay_, "dark_mode_toggle_container");

        if (dark_toggle) {
            if (supports_dark && supports_light) {
                // Dual-mode theme - enable toggle
                lv_obj_remove_state(dark_toggle, LV_STATE_DISABLED);
                if (toggle_container) {
                    lv_obj_remove_flag(toggle_container, LV_OBJ_FLAG_HIDDEN);
                }
                spdlog::debug("[{}] Theme '{}' supports both modes, toggle enabled", get_name(),
                              theme_name);
            } else if (supports_dark) {
                // Dark-only theme - disable toggle, force to dark
                lv_obj_add_state(dark_toggle, LV_STATE_DISABLED);
                lv_obj_add_state(dark_toggle, LV_STATE_CHECKED);
                preview_is_dark_ = true;
                if (toggle_container) {
                    lv_obj_remove_flag(toggle_container, LV_OBJ_FLAG_HIDDEN);
                }
                spdlog::debug("[{}] Theme '{}' is dark-only, forcing dark mode", get_name(),
                              theme_name);
            } else if (supports_light) {
                // Light-only theme - disable toggle, force to light
                lv_obj_add_state(dark_toggle, LV_STATE_DISABLED);
                lv_obj_remove_state(dark_toggle, LV_STATE_CHECKED);
                preview_is_dark_ = false;
                if (toggle_container) {
                    lv_obj_remove_flag(toggle_container, LV_OBJ_FLAG_HIDDEN);
                }
                spdlog::debug("[{}] Theme '{}' is light-only, forcing light mode", get_name(),
                              theme_name);
            }
        }
    }

    // Preview the theme with the (possibly forced) dark mode setting
    theme_manager_preview(theme);

    // Update Apply button state reactively - disabled when unchanged from original
    lv_subject_set_int(&theme_apply_disabled_subject_, index == original_theme_index_ ? 1 : 0);

    // Update all preview widget colors (reuse dark mode toggle logic)
    handle_preview_dark_mode_toggled(preview_is_dark_);

    spdlog::debug("[{}] Explorer preview: theme '{}' (index {})", get_name(), theme_name, index);
}

void DisplaySettingsOverlay::handle_theme_settings_clicked() {
    // Primary entry point: Opens Theme Explorer first (not editor)
    if (!parent_screen_) {
        spdlog::warn("[{}] Theme settings clicked without parent screen", get_name());
        return;
    }

    if (!theme_explorer_overlay_) {
        spdlog::debug("[{}] Creating theme explorer overlay...", get_name());
        theme_explorer_overlay_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "theme_preview_overlay", nullptr));
        if (!theme_explorer_overlay_) {
            spdlog::error("[{}] Failed to create theme explorer overlay", get_name());
            return;
        }

        lv_obj_add_flag(theme_explorer_overlay_, LV_OBJ_FLAG_HIDDEN);

        // Register with nullptr - this overlay has no lifecycle object but we
        // register to suppress the "pushed without lifecycle registration" warning
        NavigationManager::instance().register_overlay_instance(theme_explorer_overlay_, nullptr);
        NavigationManager::instance().register_overlay_close_callback(
            theme_explorer_overlay_, [this]() {
                // Revert to the theme that was active when explorer opened
                theme_manager_apply_theme(original_theme_, theme_manager_is_dark_mode());
                helix::ui::safe_delete(theme_explorer_overlay_);
                // Clear cache so next open picks up filesystem changes
                cached_themes_.clear();
            });
    }

    // Initialize theme preset dropdown
    init_theme_preset_dropdown(theme_explorer_overlay_);

    // Cache the theme list to avoid re-parsing on every toggle/selection
    cached_themes_ = helix::discover_themes(helix::get_themes_directory());

    // Remember original theme for Apply button state and revert on close
    original_theme_index_ = SettingsManager::instance().get_theme_index();
    preview_theme_name_ = SettingsManager::instance().get_theme_name();
    original_theme_ = theme_manager_get_active_theme();

    // Initialize dark mode toggle to current global state
    preview_is_dark_ = theme_manager_is_dark_mode();
    lv_obj_t* dark_toggle =
        lv_obj_find_by_name(theme_explorer_overlay_, "preview_dark_mode_toggle");
    if (dark_toggle) {
        if (preview_is_dark_) {
            lv_obj_add_state(dark_toggle, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(dark_toggle, LV_STATE_CHECKED);
        }

        // Set toggle enabled/disabled based on current theme's mode support
        bool supports_dark = theme_manager_supports_dark_mode();
        bool supports_light = theme_manager_supports_light_mode();
        if (supports_dark && supports_light) {
            lv_obj_remove_state(dark_toggle, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(dark_toggle, LV_STATE_DISABLED);
        }
    }

    // Initially disable Apply button (no changes yet) - reactive via subject
    lv_subject_set_int(&theme_apply_disabled_subject_, 1);

    ui_nav_push_overlay(theme_explorer_overlay_);
}

void DisplaySettingsOverlay::handle_apply_theme_clicked() {
    // Apply the currently selected (previewed) theme globally
    lv_obj_t* dropdown = theme_explorer_overlay_
                             ? lv_obj_find_by_name(theme_explorer_overlay_, "theme_preset_dropdown")
                             : nullptr;
    if (!dropdown) {
        spdlog::warn("[{}] Apply clicked but dropdown not found", get_name());
        return;
    }

    int selected_index = lv_dropdown_get_selected(dropdown);

    // Persist theme selection
    SettingsManager::instance().set_theme_by_index(selected_index);
    std::string theme_name = SettingsManager::instance().get_theme_name();

    // Commit the previewed theme as the new active theme.
    // Preview already called theme_manager_preview(), so apply it permanently.
    theme_manager_apply_theme(theme_manager_get_active_theme(), theme_manager_is_dark_mode());

    // Update original theme so the close callback won't revert
    original_theme_index_ = selected_index;
    original_theme_ = theme_manager_get_active_theme();

    spdlog::info("[{}] Theme '{}' applied (index {})", get_name(), theme_name, selected_index);

    // Get display name for toast (use cached theme list if available)
    std::string display_name = theme_name;
    if (selected_index >= 0 && selected_index < static_cast<int>(cached_themes_.size())) {
        display_name = cached_themes_[selected_index].display_name;
    }
    std::string toast_msg = "Theme set to " + display_name;
    ToastManager::instance().show(ToastSeverity::SUCCESS, toast_msg.c_str());

    // Close the theme explorer overlay
    ui_nav_go_back();
}

void DisplaySettingsOverlay::handle_edit_colors_clicked() {
    // Open Theme Colors Editor (secondary panel)
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
        // Load currently previewed theme for editing (or fallback to saved theme)
        std::string theme_name = !preview_theme_name_.empty()
                                     ? preview_theme_name_
                                     : SettingsManager::instance().get_theme_name();
        // Pass the preview mode so editor shows correct palette (dark or light)
        get_theme_editor_overlay().set_editing_dark_mode(preview_is_dark_);
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

void DisplaySettingsOverlay::on_sleep_while_printing_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[DisplaySettingsOverlay] on_sleep_while_printing_changed");
    auto* toggle = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    get_display_settings_overlay().handle_sleep_while_printing_changed(enabled);
    LVGL_SAFE_EVENT_CB_END();
}

void DisplaySettingsOverlay::on_theme_preset_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[DisplaySettingsOverlay] on_theme_preset_changed");
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int index = lv_dropdown_get_selected(dropdown);
    get_display_settings_overlay().handle_theme_preset_changed(index);
    LVGL_SAFE_EVENT_CB_END();
}

void DisplaySettingsOverlay::on_theme_settings_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[DisplaySettingsOverlay] on_theme_settings_clicked");
    static_cast<void>(lv_event_get_current_target(e));
    get_display_settings_overlay().handle_theme_settings_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void DisplaySettingsOverlay::on_apply_theme_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[DisplaySettingsOverlay] on_apply_theme_clicked");
    static_cast<void>(lv_event_get_current_target(e));
    get_display_settings_overlay().handle_apply_theme_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void DisplaySettingsOverlay::on_edit_colors_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[DisplaySettingsOverlay] on_edit_colors_clicked");
    static_cast<void>(lv_event_get_current_target(e));
    get_display_settings_overlay().handle_edit_colors_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void DisplaySettingsOverlay::on_preview_open_modal(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[DisplaySettingsOverlay] on_preview_open_modal");
    LV_UNUSED(e);

    // Show a sample modal with lorem ipsum (not translatable - intentional lorem ipsum)
    helix::ui::modal_show_confirmation(
        lv_tr("Sample Dialog"),
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod "
        "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
        "veniam, quis nostrud exercitation ullamco laboris.",
        ModalSeverity::Info, lv_tr("OK"), nullptr, nullptr, nullptr);

    // Apply preview palette to the newly created modal
    // (modal is created with global theme colors, need to update for preview)
    auto& overlay = get_display_settings_overlay();
    overlay.apply_preview_palette_to_screen_popups();

    LVGL_SAFE_EVENT_CB_END();
}

void DisplaySettingsOverlay::on_preview_dark_mode_toggled(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[DisplaySettingsOverlay] on_preview_dark_mode_toggled");
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    bool is_dark = lv_obj_has_state(target, LV_STATE_CHECKED);

    get_display_settings_overlay().handle_preview_dark_mode_toggled(is_dark);
    LVGL_SAFE_EVENT_CB_END();
}

void DisplaySettingsOverlay::handle_preview_dark_mode_toggled(bool is_dark) {
    preview_is_dark_ = is_dark;

    if (!theme_explorer_overlay_) {
        return;
    }

    lv_obj_t* dropdown = lv_obj_find_by_name(theme_explorer_overlay_, "theme_preset_dropdown");
    if (!dropdown) {
        return;
    }

    int selected_index = lv_dropdown_get_selected(dropdown);
    std::string themes_dir = helix::get_themes_directory();
    auto themes = helix::discover_themes(themes_dir);

    if (selected_index < 0 || selected_index >= static_cast<int>(themes.size())) {
        return;
    }

    // Pass just the theme name - load_theme_from_file() handles path resolution
    helix::ThemeData theme = helix::load_theme_from_file(cached_themes_[selected_index].filename);

    if (!theme.is_valid()) {
        return;
    }

    // Use the new overload with explicit dark mode
    theme_manager_preview(theme, is_dark);

    spdlog::debug("[DisplaySettingsOverlay] Preview dark mode toggled to {}",
                  is_dark ? "dark" : "light");
}

void DisplaySettingsOverlay::apply_preview_palette_to_screen_popups() {
    if (!theme_explorer_overlay_ || cached_themes_.empty()) {
        return;
    }

    // Get currently selected theme from dropdown
    lv_obj_t* dropdown = lv_obj_find_by_name(theme_explorer_overlay_, "theme_preset_dropdown");
    if (!dropdown) {
        return;
    }

    uint32_t selected_index = lv_dropdown_get_selected(dropdown);
    if (selected_index >= cached_themes_.size()) {
        return;
    }

    // Load theme data
    helix::ThemeData theme = helix::load_theme_from_file(cached_themes_[selected_index].filename);
    if (!theme.is_valid()) {
        return;
    }

    // Select palette based on preview mode
    const helix::ModePalette* palette = nullptr;
    if (preview_is_dark_ && theme.supports_dark()) {
        palette = &theme.dark;
    } else if (!preview_is_dark_ && theme.supports_light()) {
        palette = &theme.light;
    } else {
        palette = theme.supports_dark() ? &theme.dark : &theme.light;
    }

    // Apply to screen-level popups (modals, dropdown lists)
    theme_apply_palette_to_screen_dropdowns(*palette);

    // Apply border_radius to sample modal dialog (if visible)
    // The modal_dialog component uses ui_dialog which reads border_radius at creation,
    // so we need to override it here for preview
    lv_obj_t* modal_dialog = lv_obj_find_by_name(lv_screen_active(), "modal_dialog");
    if (modal_dialog) {
        lv_obj_set_style_radius(modal_dialog, theme.properties.border_radius, LV_PART_MAIN);
    }
}

void DisplaySettingsOverlay::show_theme_preview(lv_obj_t* parent_screen) {
    // Store parent screen for overlay creation
    parent_screen_ = parent_screen;

    // Register callbacks (idempotent - safe to call multiple times)
    register_callbacks();

    // handle_theme_settings_clicked() creates, initializes, and pushes the overlay
    handle_theme_settings_clicked();
}

} // namespace helix::settings
