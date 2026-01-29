// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_settings_display.h
 * @brief Display Settings overlay - brightness, sleep timeout, render modes
 *
 * This overlay allows users to configure:
 * - Dark mode toggle
 * - Theme colors action (preset, preview)
 * - Screen brightness (when hardware backlight available)
 * - Display sleep timeout
 * - Bed mesh render mode (Auto/3D/2D)
 * - Time format (12H/24H)
 *
 * @pattern Overlay (lazy init)
 * @threading Main thread only
 *
 * @see SettingsManager for persistence
 */

#pragma once

#include "lvgl/lvgl.h"
#include "overlay_base.h"
#include "theme_loader.h"

#include <string>
#include <vector>

namespace helix::settings {

/**
 * @class DisplaySettingsOverlay
 * @brief Overlay for configuring display-related settings
 *
 * This overlay provides controls for:
     * - Theme colors action row

 * - Brightness slider with percentage label
 * - Sleep timeout dropdown (Never/1m/5m/10m/30m)
 * - Bed mesh and G-code render mode dropdowns
 * - Time format selection (12H/24H)
 *
 * ## Usage:
 *
 * @code
 * auto& overlay = helix::settings::get_display_settings_overlay();
 * overlay.show(parent_screen);  // Creates overlay if needed, initializes widgets, shows
 * @endcode
 */
class DisplaySettingsOverlay : public OverlayBase {
  public:
    /**
     * @brief Default constructor
     */
    DisplaySettingsOverlay();

    /**
     * @brief Destructor
     */
    ~DisplaySettingsOverlay() override;

    //
    // === OverlayBase Interface ===
    //

    /**
     * @brief Initialize subjects for reactive binding
     *
     * Must be called before overlay creation.
     */
    void init_subjects() override;

    /**
     * @brief Register event callbacks with lv_xml system
     *
     * Registers callbacks for brightness slider and dropdown changes.
     */
    void register_callbacks() override;

    /**
     * @brief Get human-readable overlay name
     * @return "Display Settings"
     */
    const char* get_name() const override {
        return "Display Settings";
    }

    /**
     * @brief Called when overlay becomes visible
     *
     * Initializes dropdown values from settings.
     */
    void on_activate() override;

    /**
     * @brief Called when overlay is being hidden
     */
    void on_deactivate() override;

    //
    // === UI Creation ===
    //

    /**
     * @brief Create the overlay UI (called lazily)
     *
     * @param parent Parent widget to attach overlay to (usually screen)
     * @return Root object of overlay, or nullptr on failure
     */
    lv_obj_t* create(lv_obj_t* parent) override;

    /**
     * @brief Show the overlay
     *
     * This method:
     * 1. Ensures overlay is created
     * 2. Initializes all widget values from SettingsManager
     * 3. Pushes overlay onto navigation stack
     *
     * @param parent_screen The parent screen for overlay creation
     */
    void show(lv_obj_t* parent_screen);

    //
    // === Accessors ===
    //

    /**
     * @brief Check if overlay has been created
     * @return true if create() was called successfully
     */
    bool is_created() const {
        return overlay_root_ != nullptr;
    }

    //
    // === Event Handlers (public for static callbacks) ===
    //

    /**
     * @brief Handle brightness slider change
     * @param value New brightness value (10-100)
     */
    void handle_brightness_changed(int value);

    /**
     * @brief Handle theme preset dropdown change
     * @param index Selected preset index
     */
    void handle_theme_preset_changed(int index);

    /**
     * @brief Handle theme explorer opening (now primary entry point)
     *
     * Opens Theme Explorer panel where users can browse themes before editing.
     */
    void handle_theme_settings_clicked();

    /**
     * @brief Handle Apply button click in Theme Explorer
     *
     * Applies selected theme globally and shows restart notice.
     */
    void handle_apply_theme_clicked();

    /**
     * @brief Handle Edit Colors button click in Theme Explorer
     *
     * Opens Theme Colors Editor for detailed editing.
     */
    void handle_edit_colors_clicked();

    /**
     * @brief Handle theme preset dropdown change in Theme Explorer
     * @param index Selected preset index
     */
    void handle_explorer_theme_changed(int index);

    /**
     * @brief Handle preview dark mode toggle in Theme Explorer
     * @param is_dark Whether dark mode is selected
     *
     * Updates preview colors locally without modifying global theme.
     */
    void handle_preview_dark_mode_toggled(bool is_dark);

    /**
     * @brief Apply current preview palette to screen-level popups (modals, dropdowns)
     *
     * Called after creating a modal from within the theme preview to ensure
     * it uses preview colors instead of global theme colors.
     */
    void apply_preview_palette_to_screen_popups();

  private:
    //
    // === Internal Methods ===
    //

    /**
     * @brief Initialize brightness slider and label
     */
    void init_brightness_controls();

    /**
     * @brief Initialize dim timeout dropdown
     */
    void init_dim_dropdown();

    /**
     * @brief Initialize sleep timeout dropdown
     */
    void init_sleep_dropdown();

    /**
     * @brief Initialize bed mesh mode dropdown
     */
    void init_bed_mesh_dropdown();

    /**
     * @brief Initialize G-code mode dropdown
     */
    void init_gcode_dropdown();

    /**
     * @brief Initialize theme preset dropdown
     */
    void init_theme_preset_dropdown(lv_obj_t* root);

    /**
     * @brief Initialize time format dropdown
     */
    void init_time_format_dropdown();

    //
    // === State ===
    //

    /// Theme Editor overlay (secondary - for detailed color editing)
    lv_obj_t* theme_settings_overlay_{nullptr};
    /// Theme Explorer overlay (primary - for browsing and selecting themes)
    lv_obj_t* theme_explorer_overlay_{nullptr};

    /// Tracks original theme index for Apply button state
    int original_theme_index_{-1};
    /// Current preview dark mode state
    bool preview_is_dark_{true};
    /// Currently previewed theme name (for passing to editor)
    std::string preview_theme_name_;
    /// Cached theme list (populated when explorer opens, avoids re-parsing on every toggle)
    std::vector<helix::ThemeInfo> cached_themes_;

    /// Subject for brightness value label binding
    lv_subject_t brightness_value_subject_;
    char brightness_value_buf_[8]; // e.g., "100%"

    /// Subject for theme Apply button disabled state (1=disabled, 0=enabled)
    lv_subject_t theme_apply_disabled_subject_;

    //
    // === Static Callbacks ===
    //

    static void on_brightness_changed(lv_event_t* e);
    static void on_theme_preset_changed(lv_event_t* e);
    static void on_theme_settings_clicked(lv_event_t* e);
    static void on_apply_theme_clicked(lv_event_t* e);
    static void on_edit_colors_clicked(lv_event_t* e);
    static void on_preview_dark_mode_toggled(lv_event_t* e);
    static void on_preview_open_modal(lv_event_t* e);

  public:
    /**
     * @brief Show theme preview overlay directly (for CLI access)
     *
     * Registers callbacks and creates the theme preview overlay
     * without showing the parent display settings overlay.
     *
     * @param parent_screen Screen to create overlay on
     */
    void show_theme_preview(lv_obj_t* parent_screen);
};

/**
 * @brief Global instance accessor
 *
 * Creates the overlay on first access and registers it for cleanup
 * with StaticPanelRegistry.
 *
 * @return Reference to singleton DisplaySettingsOverlay
 */
DisplaySettingsOverlay& get_display_settings_overlay();

} // namespace helix::settings
