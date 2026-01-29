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
#include "ui_utils.h"

#include "format_utils.h"
#include "settings_manager.h"
#include "static_panel_registry.h"
#include "theme_core.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <memory>

namespace helix::settings {

// Forward declaration for use before definition
static void update_button_text_contrast(lv_obj_t* btn, lv_color_t text_light, lv_color_t text_dark);

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
        helix::fmt::format_percent(brightness, brightness_value_buf_,
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

    // Try direct lookup first (Theme Explorer uses this name)
    lv_obj_t* theme_preset_dropdown = lv_obj_find_by_name(root, "theme_preset_dropdown");

    // Fall back to nested row lookup (Theme Editor used this pattern)
    if (!theme_preset_dropdown) {
        lv_obj_t* theme_preset_row = lv_obj_find_by_name(root, "row_theme_preset");
        theme_preset_dropdown =
            theme_preset_row ? lv_obj_find_by_name(theme_preset_row, "dropdown") : nullptr;
    }

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

void DisplaySettingsOverlay::handle_brightness_changed(int value) {
    SettingsManager::instance().set_brightness(value);

    // Update subject (label binding happens in XML)
    helix::fmt::format_percent(value, brightness_value_buf_, sizeof(brightness_value_buf_));
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

    // Update Apply button state - enable if different from original
    if (theme_explorer_overlay_) {
        lv_obj_t* header = lv_obj_find_by_name(theme_explorer_overlay_, "overlay_header");
        lv_obj_t* action_btn = header ? lv_obj_find_by_name(header, "action_button") : nullptr;
        if (action_btn) {
            bool should_disable = (index == original_theme_index_);
            if (should_disable) {
                lv_obj_add_state(action_btn, LV_STATE_DISABLED);
            } else {
                lv_obj_remove_state(action_btn, LV_STATE_DISABLED);
            }
        }
    }

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
                // Revert preview to current theme on close
                theme_manager_revert_preview();
                lv_obj_safe_delete(theme_explorer_overlay_);
                // Clear cache so next open picks up filesystem changes
                cached_themes_.clear();
            });
    }

    // Initialize theme preset dropdown
    init_theme_preset_dropdown(theme_explorer_overlay_);

    // Cache the theme list to avoid re-parsing on every toggle/selection
    cached_themes_ = helix::discover_themes(helix::get_themes_directory());

    // Remember original theme for Apply button state and preview
    original_theme_index_ = SettingsManager::instance().get_theme_index();
    preview_theme_name_ = SettingsManager::instance().get_theme_name();

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

    // Initially disable Apply button (no changes yet) and set proper text color
    lv_obj_t* header = lv_obj_find_by_name(theme_explorer_overlay_, "overlay_header");
    lv_obj_t* action_btn = header ? lv_obj_find_by_name(header, "action_button") : nullptr;
    if (action_btn) {
        lv_obj_add_state(action_btn, LV_STATE_DISABLED);

        // Update button text color for disabled state
        const char* text_light_str = lv_xml_get_const(NULL, "text_light");
        const char* text_dark_str = lv_xml_get_const(NULL, "text_dark");
        if (text_light_str && text_dark_str) {
            lv_color_t text_light = theme_manager_parse_hex_color(text_light_str);
            lv_color_t text_dark = theme_manager_parse_hex_color(text_dark_str);
            update_button_text_contrast(action_btn, text_light, text_dark);
        }
    }

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
    SettingsManager::instance().set_theme_by_index(selected_index);

    // Update original index since theme is now applied
    original_theme_index_ = selected_index;

    // Disable Apply button since changes are now saved
    lv_obj_t* header = lv_obj_find_by_name(theme_explorer_overlay_, "overlay_header");
    lv_obj_t* action_btn = header ? lv_obj_find_by_name(header, "action_button") : nullptr;
    if (action_btn) {
        lv_obj_add_state(action_btn, LV_STATE_DISABLED);
    }

    // Show restart notice using info note toast (non-blocking)
    spdlog::info("[{}] Theme applied - index {}. Restart required for full effect.", get_name(),
                 selected_index);
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

void DisplaySettingsOverlay::on_preview_dark_mode_toggled(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[DisplaySettingsOverlay] on_preview_dark_mode_toggled");
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    bool is_dark = lv_obj_has_state(target, LV_STATE_CHECKED);

    get_display_settings_overlay().handle_preview_dark_mode_toggled(is_dark);
    LVGL_SAFE_EVENT_CB_END();
}

void DisplaySettingsOverlay::on_preview_open_modal(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[DisplaySettingsOverlay] on_preview_open_modal");
    LV_UNUSED(e);

    // Show a sample modal with lorem ipsum
    ui_modal_show_confirmation(
        "Sample Dialog",
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod "
        "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
        "veniam, quis nostrud exercitation ullamco laboris.",
        ModalSeverity::Info, "OK", nullptr, nullptr, nullptr);

    // Apply preview palette to the newly created modal
    // (modal is created with global theme colors, need to update for preview)
    auto& overlay = get_display_settings_overlay();
    overlay.apply_preview_palette_to_screen_popups();

    LVGL_SAFE_EVENT_CB_END();
}

// Helper to update button label text with contrast-aware color
// text_light = dark text for light backgrounds (from light mode palette)
// text_dark = light text for dark backgrounds (from dark mode palette)
static void update_button_text_contrast(lv_obj_t* btn, lv_color_t text_light,
                                        lv_color_t text_dark) {
    if (!btn)
        return;

    // Get button's background color
    lv_color_t bg_color = lv_obj_get_style_bg_color(btn, LV_PART_MAIN);
    uint8_t lum = lv_color_luminance(bg_color);

    // Pick text color based on luminance (same threshold as text_button component)
    lv_color_t text_color = (lum > 140) ? text_light : text_dark;

    // Get text_subtle for disabled state (muted gray with readable contrast)
    const char* subtle_str = lv_xml_get_const(NULL, "text_subtle");
    bool btn_disabled = lv_obj_has_state(btn, LV_STATE_DISABLED);

    // Determine effective color: subtle for disabled (if available), otherwise contrast
    lv_color_t effective_color = text_color;
    if (btn_disabled && subtle_str) {
        effective_color = theme_manager_parse_hex_color(subtle_str);
    }

    // Update all label children in the button
    uint32_t count = lv_obj_get_child_count(btn);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t* child = lv_obj_get_child(btn, i);
        if (lv_obj_check_type(child, &lv_label_class)) {
            lv_obj_set_style_text_color(child, effective_color, LV_PART_MAIN);
        }
        // Also check nested containers (some buttons have container > label structure)
        uint32_t nested_count = lv_obj_get_child_count(child);
        for (uint32_t j = 0; j < nested_count; j++) {
            lv_obj_t* nested = lv_obj_get_child(child, j);
            if (lv_obj_check_type(nested, &lv_label_class)) {
                lv_obj_set_style_text_color(nested, effective_color, LV_PART_MAIN);
            }
        }
    }
}

void DisplaySettingsOverlay::handle_preview_dark_mode_toggled(bool is_dark) {
    // Update local state
    preview_is_dark_ = is_dark;

    // Get currently previewed theme (from dropdown selection)
    if (!theme_explorer_overlay_) {
        return;
    }

    lv_obj_t* dropdown = lv_obj_find_by_name(theme_explorer_overlay_, "theme_preset_dropdown");
    if (!dropdown) {
        return;
    }

    int selected_index = lv_dropdown_get_selected(dropdown);

    // Use cached theme list (populated when explorer opens)
    if (selected_index < 0 || selected_index >= static_cast<int>(cached_themes_.size())) {
        return;
    }

    // Pass just the theme name - load_theme_from_file() handles path resolution
    helix::ThemeData theme = helix::load_theme_from_file(cached_themes_[selected_index].filename);

    if (!theme.is_valid()) {
        return;
    }

    // Select palette based on mode toggle (fall back if mode not supported)
    const helix::ModePalette* palette = nullptr;
    if (is_dark && theme.supports_dark()) {
        palette = &theme.dark;
    } else if (!is_dark && theme.supports_light()) {
        palette = &theme.light;
    } else {
        // Fall back to whatever is available
        palette = theme.supports_dark() ? &theme.dark : &theme.light;
    }

    // Button text contrast colors - we need BOTH palettes for contrast calculation
    // text_light = dark text for light backgrounds (from light palette)
    // text_dark = light text for dark backgrounds (from dark palette)
    lv_color_t text_primary = theme_manager_parse_hex_color(palette->text.c_str());
    lv_color_t text_light = theme.supports_light()
                                ? theme_manager_parse_hex_color(theme.light.text.c_str())
                                : text_primary;
    lv_color_t text_dark = theme.supports_dark()
                               ? theme_manager_parse_hex_color(theme.dark.text.c_str())
                               : text_primary;

    // Apply palette to entire widget tree (handles labels, switches, sliders, dropdowns, etc.)
    theme_apply_palette_to_tree(theme_explorer_overlay_, *palette, text_light, text_dark);

    // Style any open dropdown lists (they're screen-level popups, not in overlay tree)
    theme_apply_palette_to_screen_dropdowns(*palette);

    // Parse accent colors for specific named buttons
    lv_color_t primary = theme_manager_parse_hex_color(palette->primary.c_str());
    lv_color_t secondary = theme_manager_parse_hex_color(palette->secondary.c_str());
    lv_color_t tertiary = theme_manager_parse_hex_color(palette->tertiary.c_str());
    lv_color_t warning = theme_manager_parse_hex_color(palette->warning.c_str());
    lv_color_t danger = theme_manager_parse_hex_color(palette->danger.c_str());
    lv_color_t app_bg = theme_manager_parse_hex_color(palette->app_bg.c_str());

    // Update overlay root background (tree walker doesn't know this is the root)
    lv_obj_set_style_bg_color(theme_explorer_overlay_, app_bg, LV_PART_MAIN);

    // Update specific accent-colored buttons (bg set by name, text contrast automatic)
    struct ButtonColorMapping {
        const char* name;
        lv_color_t color;
    };
    ButtonColorMapping button_mappings[] = {
        {"example_btn_primary", primary},   {"example_btn_secondary", secondary},
        {"example_btn_tertiary", tertiary}, {"example_btn_warning", warning},
        {"example_btn_danger", danger},
    };

    for (const auto& mapping : button_mappings) {
        lv_obj_t* btn = lv_obj_find_by_name(theme_explorer_overlay_, mapping.name);
        if (btn) {
            lv_obj_set_style_bg_color(btn, mapping.color, LV_PART_MAIN);
            // Refresh text contrast after changing background
            theme_apply_palette_to_widget(btn, *palette, text_light, text_dark);
        }
    }

    // Update header action buttons (Edit=secondary, Apply=primary)
    lv_obj_t* header = lv_obj_find_by_name(theme_explorer_overlay_, "overlay_header");
    if (header) {
        lv_obj_t* edit_btn = lv_obj_find_by_name(header, "action_button_2");
        if (edit_btn) {
            lv_obj_set_style_bg_color(edit_btn, secondary, LV_PART_MAIN);
            theme_apply_palette_to_widget(edit_btn, *palette, text_light, text_dark);
        }
        lv_obj_t* apply_btn = lv_obj_find_by_name(header, "action_button");
        if (apply_btn) {
            lv_obj_set_style_bg_color(apply_btn, primary, LV_PART_MAIN);
            theme_apply_palette_to_widget(apply_btn, *palette, text_light, text_dark);
        }
        // Back button icon - ensure transparent background
        lv_obj_t* back_btn = lv_obj_find_by_name(header, "back_button");
        if (back_btn) {
            lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, LV_PART_MAIN);
        }
    }

    // Update status icons with semantic colors
    lv_obj_t* status_label =
        lv_obj_find_by_name(theme_explorer_overlay_, "preview_label_status_icons");
    if (status_label) {
        lv_color_t info_color = theme_manager_parse_hex_color(palette->info.c_str());
        lv_color_t success_color = theme_manager_parse_hex_color(palette->success.c_str());
        lv_color_t warning_color = theme_manager_parse_hex_color(palette->warning.c_str());
        lv_color_t danger_color = theme_manager_parse_hex_color(palette->danger.c_str());

        // Icons are siblings before this label in the same row
        lv_obj_t* row = lv_obj_get_parent(status_label);
        if (row) {
            uint32_t child_count = lv_obj_get_child_count(row);
            // Icons are first 4 children: info, success, warning, error
            if (child_count >= 4) {
                lv_obj_set_style_text_color(lv_obj_get_child(row, 0), info_color, LV_PART_MAIN);
                lv_obj_set_style_text_color(lv_obj_get_child(row, 1), success_color, LV_PART_MAIN);
                lv_obj_set_style_text_color(lv_obj_get_child(row, 2), warning_color, LV_PART_MAIN);
                lv_obj_set_style_text_color(lv_obj_get_child(row, 3), danger_color, LV_PART_MAIN);
            }
        }
    }

    spdlog::debug("[DisplaySettingsOverlay] Preview dark mode toggled to {} (local only)",
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
