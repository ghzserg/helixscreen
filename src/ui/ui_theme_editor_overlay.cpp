// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_theme_editor_overlay.h"

#include "ui_ams_color_picker.h"
#include "ui_event_safety.h"
#include "ui_global_panel_helper.h"
#include "ui_keyboard_manager.h"
#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_theme.h"

#include "lvgl/src/xml/lv_xml.h"
#include "settings_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <sys/stat.h>

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================

DEFINE_GLOBAL_OVERLAY_STORAGE(ThemeEditorOverlay, g_theme_editor_overlay, get_theme_editor_overlay)

void init_theme_editor_overlay() {
    INIT_GLOBAL_OVERLAY(ThemeEditorOverlay, g_theme_editor_overlay);
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

ThemeEditorOverlay::ThemeEditorOverlay() {
    spdlog::debug("[{}] Constructor", get_name());
}

ThemeEditorOverlay::~ThemeEditorOverlay() {
    if (!lv_is_initialized()) {
        spdlog::debug("[ThemeEditorOverlay] Destroyed (LVGL already deinit)");
        return;
    }

    spdlog::debug("[ThemeEditorOverlay] Destroyed");
}

// ============================================================================
// OVERLAYBASE IMPLEMENTATION
// ============================================================================

void ThemeEditorOverlay::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // No local subjects needed for initial implementation

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

lv_obj_t* ThemeEditorOverlay::create(lv_obj_t* parent) {
    // Create overlay root from XML (uses theme_settings_overlay component)
    overlay_root_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent, "theme_settings_overlay", nullptr));
    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    // Find panel widget (content container)
    panel_ = lv_obj_find_by_name(overlay_root_, "overlay_content");
    if (!panel_) {
        spdlog::warn("[{}] Could not find overlay_content widget", get_name());
    }

    // Wire up custom back button handler for dirty state check
    // Exception to "NO lv_obj_add_event_cb" rule: Required for unsaved data protection
    // The default XML callback (on_header_back_clicked) is removed and replaced with ours
    lv_obj_t* header = lv_obj_find_by_name(overlay_root_, "overlay_header");
    if (header) {
        lv_obj_t* back_button = lv_obj_find_by_name(header, "back_button");
        if (back_button) {
            // Remove existing click handlers and add our custom one
            lv_obj_remove_event_cb(back_button, nullptr); // Remove all callbacks
            lv_obj_add_event_cb(back_button, on_back_clicked, LV_EVENT_CLICKED, nullptr);
            spdlog::debug("[{}] Wired custom back button handler for dirty state check",
                          get_name());
        }
    }

    // Find swatch widgets (swatch_0 through swatch_15)
    for (size_t i = 0; i < swatch_objects_.size(); ++i) {
        char swatch_name[16];
        std::snprintf(swatch_name, sizeof(swatch_name), "swatch_%zu", i);
        swatch_objects_[i] = lv_obj_find_by_name(overlay_root_, swatch_name);
        if (!swatch_objects_[i]) {
            spdlog::trace("[{}] Swatch '{}' not found (may be added later)", get_name(),
                          swatch_name);
        }
    }

    spdlog::debug("[{}] Created overlay", get_name());
    return overlay_root_;
}

void ThemeEditorOverlay::register_callbacks() {
    // Swatch click callback for color editing
    lv_xml_register_event_cb(nullptr, "on_theme_swatch_clicked", on_swatch_clicked);

    // Slider callbacks for property adjustments
    lv_xml_register_event_cb(nullptr, "on_border_radius_changed", on_border_radius_changed);
    lv_xml_register_event_cb(nullptr, "on_border_width_changed", on_border_width_changed);
    lv_xml_register_event_cb(nullptr, "on_border_opacity_changed", on_border_opacity_changed);
    lv_xml_register_event_cb(nullptr, "on_shadow_changed", on_shadow_changed);

    // Action button callbacks
    lv_xml_register_event_cb(nullptr, "on_theme_save_clicked", on_theme_save_clicked);
    lv_xml_register_event_cb(nullptr, "on_theme_save_as_clicked", on_theme_save_as_clicked);
    lv_xml_register_event_cb(nullptr, "on_theme_revert_clicked", on_theme_revert_clicked);

    // Custom back button callback to intercept close and check dirty state
    lv_xml_register_event_cb(nullptr, "on_theme_editor_back_clicked", on_back_clicked);

    // Save As dialog callbacks
    lv_xml_register_event_cb(nullptr, "on_theme_save_as_confirm", on_save_as_confirm);
    lv_xml_register_event_cb(nullptr, "on_theme_save_as_cancel", on_save_as_cancel);

    // Restart dialog callbacks
    lv_xml_register_event_cb(nullptr, "on_theme_restart_now", on_restart_now);
    lv_xml_register_event_cb(nullptr, "on_theme_restart_later", on_restart_later);

    spdlog::debug("[{}] Callbacks registered", get_name());
}

void ThemeEditorOverlay::on_activate() {
    OverlayBase::on_activate();
    spdlog::debug("[{}] Activated", get_name());
}

void ThemeEditorOverlay::on_deactivate() {
    OverlayBase::on_deactivate();
    spdlog::debug("[{}] Deactivated", get_name());
}

void ThemeEditorOverlay::cleanup() {
    spdlog::debug("[{}] Cleanup", get_name());

    // Clean up color picker (may be showing a modal)
    if (color_picker_) {
        color_picker_.reset();
    }
    editing_color_index_ = -1;

    // Clean up discard confirmation dialog if showing
    if (discard_dialog_) {
        Modal::hide(discard_dialog_);
        discard_dialog_ = nullptr;
    }
    pending_discard_action_ = nullptr;

    // Clean up save as dialog if showing
    if (save_as_dialog_) {
        Modal::hide(save_as_dialog_);
        save_as_dialog_ = nullptr;
    }

    // Clean up restart dialog if showing
    if (restart_dialog_) {
        Modal::hide(restart_dialog_);
        restart_dialog_ = nullptr;
    }

    // Clear swatch references (widgets will be destroyed by LVGL)
    swatch_objects_.fill(nullptr);
    panel_ = nullptr;

    OverlayBase::cleanup();
}

// ============================================================================
// THEME EDITOR API
// ============================================================================

void ThemeEditorOverlay::load_theme(const std::string& filename) {
    std::string themes_dir = helix::get_themes_directory();
    std::string filepath = themes_dir + "/" + filename + ".json";

    helix::ThemeData loaded = helix::load_theme_from_file(filepath);
    if (!loaded.is_valid()) {
        spdlog::error("[{}] Failed to load theme from '{}'", get_name(), filepath);
        return;
    }

    // Store both copies - editing and original for revert
    editing_theme_ = loaded;
    original_theme_ = loaded;

    // Clear dirty state since we just loaded
    clear_dirty();

    // Update visual swatches
    update_swatch_colors();

    // Update property sliders
    update_property_sliders();

    spdlog::info("[{}] Loaded theme '{}' for editing", get_name(), loaded.name);
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void ThemeEditorOverlay::setup_callbacks() {
    // Will be implemented in subsequent tasks
}

void ThemeEditorOverlay::update_swatch_colors() {
    for (size_t i = 0; i < swatch_objects_.size(); ++i) {
        if (!swatch_objects_[i]) {
            continue;
        }

        // Get color from editing theme
        const std::string& color_hex = editing_theme_.colors.at(i);
        if (color_hex.empty()) {
            continue;
        }

        // Parse hex color and apply to swatch background
        lv_color_t color = ui_theme_parse_hex_color(color_hex.c_str());
        lv_obj_set_style_bg_color(swatch_objects_[i], color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(swatch_objects_[i], LV_OPA_COVER, LV_PART_MAIN);

        spdlog::trace("[{}] Set swatch {} to {}", get_name(), i, color_hex);
    }
}

void ThemeEditorOverlay::update_property_sliders() {
    if (!overlay_root_) {
        return;
    }

    // Update border radius slider
    lv_obj_t* radius_row = lv_obj_find_by_name(overlay_root_, "row_border_radius");
    lv_obj_t* radius_slider = radius_row ? lv_obj_find_by_name(radius_row, "slider") : nullptr;
    if (radius_slider) {
        lv_slider_set_value(radius_slider, editing_theme_.properties.border_radius, LV_ANIM_OFF);
    }

    // Update border width slider
    lv_obj_t* width_row = lv_obj_find_by_name(overlay_root_, "row_border_width");
    lv_obj_t* width_slider = width_row ? lv_obj_find_by_name(width_row, "slider") : nullptr;
    if (width_slider) {
        lv_slider_set_value(width_slider, editing_theme_.properties.border_width, LV_ANIM_OFF);
    }

    // Update border opacity slider
    lv_obj_t* opacity_row = lv_obj_find_by_name(overlay_root_, "row_border_opacity");
    lv_obj_t* opacity_slider = opacity_row ? lv_obj_find_by_name(opacity_row, "slider") : nullptr;
    if (opacity_slider) {
        lv_slider_set_value(opacity_slider, editing_theme_.properties.border_opacity, LV_ANIM_OFF);
    }

    // Update shadow intensity slider
    lv_obj_t* shadow_row = lv_obj_find_by_name(overlay_root_, "row_shadow_intensity");
    lv_obj_t* shadow_slider = shadow_row ? lv_obj_find_by_name(shadow_row, "slider") : nullptr;
    if (shadow_slider) {
        lv_slider_set_value(shadow_slider, editing_theme_.properties.shadow_intensity, LV_ANIM_OFF);
    }

    spdlog::debug("[{}] Property sliders updated: border_radius={}, border_width={}, "
                  "border_opacity={}, shadow_intensity={}",
                  get_name(), editing_theme_.properties.border_radius,
                  editing_theme_.properties.border_width, editing_theme_.properties.border_opacity,
                  editing_theme_.properties.shadow_intensity);
}

void ThemeEditorOverlay::mark_dirty() {
    if (!dirty_) {
        dirty_ = true;
        update_title_dirty_indicator();
        spdlog::debug("[{}] Theme marked as dirty (unsaved changes)", get_name());
    }
}

void ThemeEditorOverlay::clear_dirty() {
    if (dirty_) {
        dirty_ = false;
        update_title_dirty_indicator();
        spdlog::trace("[{}] Dirty state cleared", get_name());
    }
}

void ThemeEditorOverlay::update_title_dirty_indicator() {
    if (!overlay_root_) {
        return;
    }

    // Find the header bar and its title label
    lv_obj_t* header = lv_obj_find_by_name(overlay_root_, "overlay_header");
    if (!header) {
        spdlog::trace("[{}] Could not find overlay_header for title update", get_name());
        return;
    }

    lv_obj_t* title_label = lv_obj_find_by_name(header, "header_title");
    if (!title_label) {
        spdlog::trace("[{}] Could not find header_title for title update", get_name());
        return;
    }

    // Update title text with dirty indicator
    if (dirty_) {
        lv_label_set_text(title_label, "Theme Colors *");
    } else {
        lv_label_set_text(title_label, "Theme Colors");
    }
}

// ============================================================================
// STATIC CALLBACKS - Slider Property Changes
// ============================================================================

void ThemeEditorOverlay::on_border_radius_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThemeEditorOverlay] on_border_radius_changed");
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int value = lv_slider_get_value(slider);
    get_theme_editor_overlay().handle_border_radius_changed(value);
    LVGL_SAFE_EVENT_CB_END();
}

void ThemeEditorOverlay::on_border_width_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThemeEditorOverlay] on_border_width_changed");
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int value = lv_slider_get_value(slider);
    get_theme_editor_overlay().handle_border_width_changed(value);
    LVGL_SAFE_EVENT_CB_END();
}

void ThemeEditorOverlay::on_border_opacity_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThemeEditorOverlay] on_border_opacity_changed");
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int value = lv_slider_get_value(slider);
    get_theme_editor_overlay().handle_border_opacity_changed(value);
    LVGL_SAFE_EVENT_CB_END();
}

void ThemeEditorOverlay::on_shadow_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThemeEditorOverlay] on_shadow_changed");
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int value = lv_slider_get_value(slider);
    get_theme_editor_overlay().handle_shadow_intensity_changed(value);
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// STATIC CALLBACKS - Action Buttons
// ============================================================================

void ThemeEditorOverlay::on_theme_save_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThemeEditorOverlay] on_theme_save_clicked");
    static_cast<void>(lv_event_get_current_target(e));
    get_theme_editor_overlay().handle_save_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void ThemeEditorOverlay::on_theme_save_as_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThemeEditorOverlay] on_theme_save_as_clicked");
    static_cast<void>(lv_event_get_current_target(e));
    get_theme_editor_overlay().handle_save_as_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void ThemeEditorOverlay::on_theme_revert_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThemeEditorOverlay] on_theme_revert_clicked");
    static_cast<void>(lv_event_get_current_target(e));
    get_theme_editor_overlay().handle_revert_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// CALLBACK STUBS (to be implemented in tasks 6.4-6.6)
// ============================================================================

void ThemeEditorOverlay::on_swatch_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThemeEditorOverlay] on_swatch_clicked");

    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    if (target) {
        // Determine which swatch was clicked by checking against our stored references
        auto& overlay = get_theme_editor_overlay();
        bool found = false;
        for (size_t i = 0; i < overlay.swatch_objects_.size(); ++i) {
            if (overlay.swatch_objects_[i] == target) {
                overlay.handle_swatch_click(static_cast<int>(i));
                found = true;
                break;
            }
        }
        if (!found) {
            spdlog::warn("[ThemeEditorOverlay] on_swatch_clicked: unknown swatch target");
        }
    } else {
        spdlog::warn("[ThemeEditorOverlay] on_swatch_clicked: no target");
    }

    LVGL_SAFE_EVENT_CB_END();
}

void ThemeEditorOverlay::on_slider_changed(lv_event_t* /* e */) {
    // Generic slider handler - individual property handlers are used instead
}

void ThemeEditorOverlay::on_close_requested(lv_event_t* /* e */) {
    // Delegate to on_back_clicked for consistent dirty state handling
    get_theme_editor_overlay().handle_back_clicked();
}

void ThemeEditorOverlay::on_back_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThemeEditorOverlay] on_back_clicked");
    static_cast<void>(lv_event_get_current_target(e));
    get_theme_editor_overlay().handle_back_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void ThemeEditorOverlay::on_discard_confirm(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThemeEditorOverlay] on_discard_confirm");
    static_cast<void>(lv_event_get_current_target(e));

    auto& overlay = get_theme_editor_overlay();

    // Hide the dialog first
    if (overlay.discard_dialog_) {
        Modal::hide(overlay.discard_dialog_);
        overlay.discard_dialog_ = nullptr;
    }

    // Execute the pending discard action
    if (overlay.pending_discard_action_) {
        auto action = std::move(overlay.pending_discard_action_);
        overlay.pending_discard_action_ = nullptr;
        action();
    }

    LVGL_SAFE_EVENT_CB_END();
}

void ThemeEditorOverlay::on_discard_cancel(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThemeEditorOverlay] on_discard_cancel");
    static_cast<void>(lv_event_get_current_target(e));

    auto& overlay = get_theme_editor_overlay();

    // Just hide the dialog, don't execute the discard action
    if (overlay.discard_dialog_) {
        Modal::hide(overlay.discard_dialog_);
        overlay.discard_dialog_ = nullptr;
    }

    overlay.pending_discard_action_ = nullptr;
    spdlog::debug("[ThemeEditorOverlay] Discard cancelled by user");

    LVGL_SAFE_EVENT_CB_END();
}

void ThemeEditorOverlay::handle_back_clicked() {
    if (dirty_) {
        // Show confirmation before closing
        show_discard_confirmation([]() { ui_nav_go_back(); });
    } else {
        // Not dirty, close immediately
        ui_nav_go_back();
    }
}

// ============================================================================
// INSTANCE HANDLERS - Slider Property Changes
// ============================================================================

void ThemeEditorOverlay::handle_border_radius_changed(int value) {
    editing_theme_.properties.border_radius = value;
    mark_dirty();
    ui_theme_preview(editing_theme_);
    spdlog::debug("[{}] Border radius changed to {}", get_name(), value);
}

void ThemeEditorOverlay::handle_border_width_changed(int value) {
    editing_theme_.properties.border_width = value;
    mark_dirty();
    ui_theme_preview(editing_theme_);
    spdlog::debug("[{}] Border width changed to {}", get_name(), value);
}

void ThemeEditorOverlay::handle_border_opacity_changed(int value) {
    editing_theme_.properties.border_opacity = value;
    mark_dirty();
    ui_theme_preview(editing_theme_);
    spdlog::debug("[{}] Border opacity changed to {}", get_name(), value);
}

void ThemeEditorOverlay::handle_shadow_intensity_changed(int value) {
    editing_theme_.properties.shadow_intensity = value;
    mark_dirty();
    ui_theme_preview(editing_theme_);
    spdlog::debug("[{}] Shadow intensity changed to {}", get_name(), value);
}

// ============================================================================
// INSTANCE HANDLERS - Action Buttons
// ============================================================================

void ThemeEditorOverlay::handle_save_clicked() {
    if (!editing_theme_.is_valid()) {
        spdlog::error("[{}] Cannot save - editing theme is invalid", get_name());
        return;
    }

    // Build filepath from theme filename
    std::string themes_dir = helix::get_themes_directory();
    std::string filepath = themes_dir + "/" + editing_theme_.filename + ".json";

    if (helix::save_theme_to_file(editing_theme_, filepath)) {
        clear_dirty();
        original_theme_ = editing_theme_;
        spdlog::info("[{}] Theme '{}' saved to '{}'", get_name(), editing_theme_.name, filepath);

        // Show restart dialog (theme changes require restart to take full effect)
        show_restart_dialog();
    } else {
        spdlog::error("[{}] Failed to save theme to '{}'", get_name(), filepath);
    }
}

void ThemeEditorOverlay::handle_save_as_clicked() {
    // Show save as dialog to get new filename
    show_save_as_dialog();
}

void ThemeEditorOverlay::handle_revert_clicked() {
    // If dirty, show confirmation before reverting
    if (dirty_) {
        show_discard_confirmation([this]() {
            // Restore original theme
            editing_theme_ = original_theme_;
            clear_dirty();

            // Update UI to reflect reverted values
            update_swatch_colors();
            update_property_sliders();

            // Preview the original theme
            ui_theme_preview(editing_theme_);

            spdlog::info("[{}] Theme reverted to original state", get_name());
        });
    } else {
        // Not dirty, no confirmation needed
        spdlog::debug("[{}] No changes to revert", get_name());
    }
}

// ============================================================================
// STUBS (to be implemented in future tasks)
// ============================================================================

void ThemeEditorOverlay::handle_swatch_click(int palette_index) {
    if (palette_index < 0 || palette_index >= static_cast<int>(swatch_objects_.size())) {
        spdlog::warn("[{}] handle_swatch_click: invalid index {}", get_name(), palette_index);
        return;
    }

    spdlog::debug("[{}] Swatch {} clicked, opening color picker", get_name(), palette_index);
    show_color_picker(palette_index);
}

void ThemeEditorOverlay::handle_slider_change(const char* /* slider_name */, int /* value */) {
    // Generic handler - individual property handlers are used instead
}

void ThemeEditorOverlay::show_color_picker(int palette_index) {
    if (palette_index < 0 ||
        palette_index >= static_cast<int>(editing_theme_.colors.color_names().size())) {
        spdlog::error("[{}] Invalid palette index {} for color picker", get_name(), palette_index);
        return;
    }

    // Store which color we're editing
    editing_color_index_ = palette_index;

    // Get current color hex from the editing theme
    const std::string& current_hex = editing_theme_.colors.at(palette_index);
    uint32_t current_rgb = 0x808080; // Default gray if parsing fails

    // Parse hex color (handle both "#RRGGBB" and "RRGGBB" formats)
    if (!current_hex.empty()) {
        const char* hex_ptr = current_hex.c_str();
        if (hex_ptr[0] == '#') {
            hex_ptr++;
        }
        current_rgb = static_cast<uint32_t>(std::strtoul(hex_ptr, nullptr, 16));
    }

    // Create color picker if not already created
    if (!color_picker_) {
        color_picker_ = std::make_unique<helix::ui::AmsColorPicker>();
    }

    // Set callback to handle color selection
    color_picker_->set_color_callback([this](uint32_t color_rgb,
                                             const std::string& /* color_name */) {
        if (editing_color_index_ < 0 ||
            editing_color_index_ >= static_cast<int>(editing_theme_.colors.color_names().size())) {
            spdlog::warn("[{}] Color picker callback: invalid editing_color_index_ {}", get_name(),
                         editing_color_index_);
            return;
        }

        // Format color as hex string
        char hex_buf[8];
        std::snprintf(hex_buf, sizeof(hex_buf), "#%06X", color_rgb);

        // Update the editing theme color
        editing_theme_.colors.at(editing_color_index_) = hex_buf;

        // Update the swatch visual if it exists
        if (editing_color_index_ < static_cast<int>(swatch_objects_.size()) &&
            swatch_objects_[editing_color_index_]) {
            lv_color_t lv_color = lv_color_hex(color_rgb);
            lv_obj_set_style_bg_color(swatch_objects_[editing_color_index_], lv_color,
                                      LV_PART_MAIN);
        }

        // Mark dirty and preview
        mark_dirty();
        ui_theme_preview(editing_theme_);

        spdlog::info("[{}] Color {} updated to {}", get_name(), editing_color_index_, hex_buf);

        // Reset editing index
        editing_color_index_ = -1;
    });

    // Show the color picker with current color
    lv_obj_t* screen = lv_screen_active();
    if (!color_picker_->show_with_color(screen, current_rgb)) {
        spdlog::error("[{}] Failed to show color picker", get_name());
        editing_color_index_ = -1;
    }
}

void ThemeEditorOverlay::show_save_as_dialog() {
    // Close existing dialog if any
    if (save_as_dialog_) {
        Modal::hide(save_as_dialog_);
        save_as_dialog_ = nullptr;
    }

    // Show save as modal
    save_as_dialog_ = ui_modal_show("theme_save_as_modal");
    if (!save_as_dialog_) {
        spdlog::error("[{}] Failed to show Save As dialog", get_name());
        return;
    }

    // Find and configure the textarea
    lv_obj_t* input = lv_obj_find_by_name(save_as_dialog_, "theme_name_input");
    if (input) {
        // Pre-fill with current theme name as suggestion
        std::string suggested_name = editing_theme_.name + " Copy";
        lv_textarea_set_text(input, suggested_name.c_str());
        lv_textarea_set_cursor_pos(input, LV_TEXTAREA_CURSOR_LAST);

        // Register with keyboard manager for on-screen keyboard
        ui_modal_register_keyboard(save_as_dialog_, input);
    }

    spdlog::debug("[{}] Showing Save As dialog", get_name());
}

void ThemeEditorOverlay::show_restart_dialog() {
    // Close existing dialog if any
    if (restart_dialog_) {
        Modal::hide(restart_dialog_);
        restart_dialog_ = nullptr;
    }

    // Show restart prompt using confirmation dialog
    restart_dialog_ = ui_modal_show_confirmation(
        "Theme Saved", "Theme changes require an app restart to fully apply. Restart now?",
        ModalSeverity::Info, "Restart Now", on_restart_now, on_restart_later, nullptr);

    if (!restart_dialog_) {
        spdlog::error("[{}] Failed to show restart dialog", get_name());
    }

    spdlog::debug("[{}] Showing restart dialog", get_name());
}

void ThemeEditorOverlay::show_discard_confirmation(std::function<void()> on_discard) {
    // Store the action to execute if user confirms discard
    pending_discard_action_ = std::move(on_discard);

    // Show confirmation dialog using modal system
    discard_dialog_ = ui_modal_show_confirmation(
        "Discard Changes?", "You have unsaved changes. Discard them?", ModalSeverity::Warning,
        "Discard", on_discard_confirm, on_discard_cancel, nullptr);

    if (!discard_dialog_) {
        spdlog::error("[{}] Failed to show discard confirmation dialog", get_name());
        pending_discard_action_ = nullptr;
    }
}

// ============================================================================
// SAVE AS DIALOG CALLBACKS
// ============================================================================

void ThemeEditorOverlay::on_save_as_confirm(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThemeEditorOverlay] on_save_as_confirm");
    static_cast<void>(lv_event_get_current_target(e));
    get_theme_editor_overlay().handle_save_as_confirm();
    LVGL_SAFE_EVENT_CB_END();
}

void ThemeEditorOverlay::on_save_as_cancel(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThemeEditorOverlay] on_save_as_cancel");
    static_cast<void>(lv_event_get_current_target(e));

    auto& overlay = get_theme_editor_overlay();
    if (overlay.save_as_dialog_) {
        Modal::hide(overlay.save_as_dialog_);
        overlay.save_as_dialog_ = nullptr;
    }

    spdlog::debug("[ThemeEditorOverlay] Save As cancelled");
    LVGL_SAFE_EVENT_CB_END();
}

void ThemeEditorOverlay::handle_save_as_confirm() {
    if (!save_as_dialog_) {
        spdlog::error("[{}] handle_save_as_confirm: no dialog", get_name());
        return;
    }

    // Get theme name from input field
    lv_obj_t* input = lv_obj_find_by_name(save_as_dialog_, "theme_name_input");
    if (!input) {
        spdlog::error("[{}] Could not find theme_name_input", get_name());
        return;
    }

    const char* raw_name = lv_textarea_get_text(input);
    if (!raw_name || std::strlen(raw_name) == 0) {
        // Show error in status field
        lv_obj_t* status = lv_obj_find_by_name(save_as_dialog_, "save_as_status");
        if (status) {
            lv_label_set_text(status, "Please enter a theme name");
            lv_obj_remove_flag(status, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    std::string theme_name(raw_name);
    std::string themes_dir = helix::get_themes_directory();

    // Sanitize and generate unique filename
    std::string base_filename = sanitize_filename(theme_name);
    if (base_filename.empty()) {
        base_filename = "custom_theme";
    }
    std::string unique_filename = generate_unique_filename(base_filename, themes_dir);

    // Update theme data with new name and filename
    editing_theme_.name = theme_name;
    editing_theme_.filename = unique_filename;

    // Save to new file
    std::string filepath = themes_dir + "/" + unique_filename + ".json";
    if (!helix::save_theme_to_file(editing_theme_, filepath)) {
        spdlog::error("[{}] Failed to save theme to '{}'", get_name(), filepath);
        lv_obj_t* status = lv_obj_find_by_name(save_as_dialog_, "save_as_status");
        if (status) {
            lv_label_set_text(status, "Failed to save theme file");
            lv_obj_remove_flag(status, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    // Update config to use new theme
    SettingsManager::instance().set_theme_name(unique_filename);

    // Clear dirty state
    clear_dirty();
    original_theme_ = editing_theme_;

    spdlog::info("[{}] Theme saved as '{}' (file: {}.json)", get_name(), theme_name,
                 unique_filename);

    // Hide save as dialog
    Modal::hide(save_as_dialog_);
    save_as_dialog_ = nullptr;

    // Show restart dialog
    show_restart_dialog();
}

// ============================================================================
// RESTART DIALOG CALLBACKS
// ============================================================================

void ThemeEditorOverlay::on_restart_now(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThemeEditorOverlay] on_restart_now");
    static_cast<void>(lv_event_get_current_target(e));

    auto& overlay = get_theme_editor_overlay();
    if (overlay.restart_dialog_) {
        Modal::hide(overlay.restart_dialog_);
        overlay.restart_dialog_ = nullptr;
    }

    spdlog::info("[ThemeEditorOverlay] User requested restart - exiting application");

    // Exit the application to trigger restart (supervisor will restart it)
    std::exit(0);

    LVGL_SAFE_EVENT_CB_END();
}

void ThemeEditorOverlay::on_restart_later(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThemeEditorOverlay] on_restart_later");
    static_cast<void>(lv_event_get_current_target(e));

    auto& overlay = get_theme_editor_overlay();
    if (overlay.restart_dialog_) {
        Modal::hide(overlay.restart_dialog_);
        overlay.restart_dialog_ = nullptr;
    }

    spdlog::info("[ThemeEditorOverlay] User chose to restart later");
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// FILENAME HELPERS
// ============================================================================

std::string ThemeEditorOverlay::sanitize_filename(const std::string& name) {
    std::string result;
    result.reserve(name.size());

    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            // Keep alphanumeric characters, convert to lowercase
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (c == ' ' || c == '-' || c == '_') {
            // Replace spaces and dashes with underscores
            if (!result.empty() && result.back() != '_') {
                result += '_';
            }
        }
        // Skip all other characters (punctuation, special chars, etc.)
    }

    // Trim trailing underscores
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }

    // Limit length
    constexpr size_t MAX_FILENAME_LEN = 32;
    if (result.size() > MAX_FILENAME_LEN) {
        result.resize(MAX_FILENAME_LEN);
        // Trim trailing underscore from truncation
        while (!result.empty() && result.back() == '_') {
            result.pop_back();
        }
    }

    return result;
}

std::string ThemeEditorOverlay::generate_unique_filename(const std::string& base_name,
                                                         const std::string& themes_dir) {
    struct stat st{};

    // Check if base name is available
    std::string candidate = base_name;
    std::string filepath = themes_dir + "/" + candidate + ".json";

    if (stat(filepath.c_str(), &st) != 0) {
        return candidate;
    }

    // Append numbers until we find an available name
    for (int i = 2; i < 100; ++i) {
        candidate = base_name + "_" + std::to_string(i);
        filepath = themes_dir + "/" + candidate + ".json";

        if (stat(filepath.c_str(), &st) != 0) {
            return candidate;
        }
    }

    // Fallback: use timestamp
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();

    return base_name + "_" + std::to_string(seconds);
}
