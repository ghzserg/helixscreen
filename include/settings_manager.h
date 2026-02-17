// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"
#include "subject_managed_panel.h"

#include <functional>
#include <memory>
#include <string>

namespace helix {
class MoonrakerClient;

/** @brief Print completion notification mode (Off=0, Notification=1, Alert=2) */
enum class CompletionAlertMode { OFF = 0, NOTIFICATION = 1, ALERT = 2 };

/** @brief Time display format (12-hour with AM/PM or 24-hour) */
enum class TimeFormat { HOUR_12 = 0, HOUR_24 = 1 };

/** @brief Z movement style override (Auto=detect from kinematics, or force) */
enum class ZMovementStyle { AUTO = 0, BED_MOVES = 1, NOZZLE_MOVES = 2 };

/**
 * @brief Application settings manager with reactive UI binding
 *
 * Coordinates persistence (Config), reactive subjects (lv_subject_t), immediate
 * effects (theme changes, Moonraker commands), and user preferences.
 *
 * Architecture:
 * ```
 * SettingsManager
 * ├── Persistence Layer (wraps Config)
 * │   └── JSON storage in helixconfig.json
 * ├── Reactive Layer (lv_subject_t)
 * │   └── UI automatically updates when settings change
 * ├── Effect Layer (immediate actions)
 * │   └── Apply dark mode, set LED, adjust sleep timer
 * └── Remote Layer (Moonraker API)
 *     └── LED commands, calibration triggers
 * ```
 *
 * Thread safety: Single-threaded, main LVGL thread only.
 *
 * Usage:
 * ```cpp
 * auto& settings = SettingsManager::instance();
 * settings.init_subjects();  // Before XML creation
 *
 * // Toggle dark mode (updates UI, applies theme, persists)
 * settings.set_dark_mode(true);
 *
 * // Get subject for XML binding
 * lv_subject_t* subject = settings.subject_dark_mode();
 * ```
 */
class SettingsManager {
  public:
    /**
     * @brief Get singleton instance
     * @return Reference to global SettingsManager
     */
    static SettingsManager& instance();

    // Prevent copying
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    /**
     * @brief Initialize LVGL subjects
     *
     * MUST be called BEFORE creating XML components that bind to settings subjects.
     * Loads initial values from Config and registers subjects with LVGL XML system.
     */
    void init_subjects();

    /**
     * @brief Deinitialize LVGL subjects
     *
     * Must be called before lv_deinit() to properly disconnect observers.
     * Called by StaticSubjectRegistry during application shutdown.
     */
    void deinit_subjects();

    /**
     * @brief Set Moonraker client reference for remote commands
     *
     * Required for LED control and other printer-dependent settings.
     * Call after MoonrakerClient is initialized.
     *
     * @param client Pointer to active MoonrakerClient (can be nullptr to disable)
     */
    void set_moonraker_client(MoonrakerClient* client);

    // =========================================================================
    // APPEARANCE SETTINGS
    // =========================================================================

    /**
     * @brief Get dark mode state
     * @return true if dark mode enabled
     */
    bool get_dark_mode() const;

    /**
     * @brief Set dark mode state
     *
     * Updates subject (UI reacts) and persists to Config.
     * Note: Theme change requires application restart to take effect.
     *
     * @param enabled true for dark mode, false for light mode
     */
    void set_dark_mode(bool enabled);

    /**
     * @brief Check if current theme supports dark mode toggle
     * @return true if theme supports both dark and light modes
     */
    bool is_dark_mode_available() const;

    /**
     * @brief Called when theme changes to update mode availability
     *
     * If theme is single-mode:
     * - Sets dark_mode_available subject to 0 (toggle disabled)
     * - Auto-switches to supported mode
     * - Updates dark_mode subject to match
     *
     * If theme is dual-mode:
     * - Sets dark_mode_available subject to 1 (toggle enabled)
     */
    void on_theme_changed();

    /** @brief Get current theme filename (without .json) */
    std::string get_theme_name() const;

    /** @brief Set theme by filename, marks restart pending */
    void set_theme_name(const std::string& name);

    /** @brief Get dropdown options string for discovered themes */
    std::string get_theme_options() const;

    /** @brief Get index of current theme in options list */
    int get_theme_index() const;

    /** @brief Set theme by dropdown index */
    void set_theme_by_index(int index);

    /**
     * @brief Get display dim timeout in seconds
     * @return Dim timeout (0 = disabled)
     */
    int get_display_dim_sec() const;

    /**
     * @brief Set display dim timeout
     *
     * Updates subject, persists, and notifies DisplayManager immediately.
     * Screen dims to lower brightness after this timeout of inactivity.
     *
     * @param seconds Dim timeout (0 to disable)
     */
    void set_display_dim_sec(int seconds);

    /**
     * @brief Get display sleep timeout in seconds
     * @return Sleep timeout (0 = disabled)
     */
    int get_display_sleep_sec() const;

    /**
     * @brief Set display sleep timeout
     *
     * Updates subject and persists. Effect applied elsewhere (display driver).
     *
     * @param seconds Sleep timeout (0 to disable)
     */
    void set_display_sleep_sec(int seconds);

    /**
     * @brief Get display brightness (0-100)
     * @return Brightness percentage
     */
    int get_brightness() const;

    /**
     * @brief Set display brightness
     *
     * Updates subject and persists. Effect applied elsewhere (display driver).
     *
     * @param percent Brightness percentage (0-100, clamped to 10-100 minimum)
     */
    void set_brightness(int percent);

    /**
     * @brief Check if hardware backlight control is available
     *
     * Returns true if a hardware backend is active (Sysfs or Allwinner).
     * In test mode, returns true (simulated backlight for UI testing).
     * On desktop without hardware, returns false.
     *
     * @return true if brightness slider should be shown in UI
     */
    bool has_backlight_control() const;

    /**
     * @brief Get sleep while printing state
     * @return true if display is allowed to sleep during active prints
     */
    bool get_sleep_while_printing() const;

    /**
     * @brief Set sleep while printing state
     *
     * When enabled (default), the display can dim and sleep during active prints
     * based on the normal inactivity timeout. When disabled, the display stays
     * awake for the entire duration of a print.
     *
     * @param enabled true to allow sleep during prints, false to inhibit
     */
    void set_sleep_while_printing(bool enabled);

    /**
     * @brief Get animations enabled state
     * @return true if UI animations are enabled
     */
    bool get_animations_enabled() const;

    /**
     * @brief Set animations enabled state
     *
     * Controls whether delightful UI animations (toasts, modals, progress bars,
     * etc.) play or are instant. Useful for accessibility or performance.
     *
     * @param enabled true to enable animations, false for instant transitions
     */
    void set_animations_enabled(bool enabled);

    /**
     * @brief Get G-code 3D preview enabled state
     * @return true if 3D G-code rendering is enabled during print status
     */
    bool get_gcode_3d_enabled() const;

    /**
     * @brief Set G-code 3D preview enabled state
     *
     * When enabled, print status shows interactive 3D G-code preview.
     * When disabled, shows thumbnail only (saves memory on constrained devices).
     *
     * @param enabled true to enable 3D preview, false for thumbnail only
     */
    void set_gcode_3d_enabled(bool enabled);

    /**
     * @brief Get bed mesh render mode
     * @return Render mode (0=Auto, 1=3D, 2=2D heatmap)
     */
    int get_bed_mesh_render_mode() const;

    /**
     * @brief Set bed mesh render mode
     *
     * Controls how bed mesh visualization is rendered:
     * - Auto (0): System decides based on measured FPS
     * - 3D (1): Always use 3D perspective view
     * - 2D (2): Always use 2D heatmap (faster on slow hardware)
     *
     * @param mode Render mode (0=Auto, 1=3D, 2=2D)
     */
    void set_bed_mesh_render_mode(int mode);

    /** @brief Get dropdown options string "Auto\n3D View\n2D Heatmap" */
    static const char* get_bed_mesh_render_mode_options();

    /** @brief Get custom printer image ID (empty = auto-detect) */
    std::string get_printer_image() const;

    /** @brief Set custom printer image ID and persist. Empty = auto-detect. */
    void set_printer_image(const std::string& id);

    /**
     * @brief Get bed mesh zero plane visibility
     * @return true if translucent Z=0 reference plane should be shown in 3D view
     */
    bool get_bed_mesh_show_zero_plane() const;

    /**
     * @brief Get G-code render mode
     * @return Render mode (0=Auto, 1=3D View, 2=2D Layer View)
     */
    int get_gcode_render_mode() const;

    /**
     * @brief Set G-code render mode
     *
     * Controls how G-code visualization is rendered:
     * - Auto (0): System decides based on measured FPS
     * - 3D View (1): Always use TinyGL 3D ribbon view
     * - 2D Layers (2): Always use 2D orthographic layer view (fast on AD5M)
     *
     * @param mode Render mode (0=Auto, 1=3D, 2=2D)
     */
    void set_gcode_render_mode(int mode);

    /** @brief Get dropdown options string "Auto\n3D View\n2D Layers" */
    static const char* get_gcode_render_mode_options();

    /**
     * @brief Get time format setting
     * @return Current time format (12-hour or 24-hour)
     */
    TimeFormat get_time_format() const;

    /**
     * @brief Set time format
     *
     * Controls whether times are displayed as 12-hour (with AM/PM) or 24-hour format.
     * Affects temp graph, file dates, history timestamps, etc.
     *
     * @param format HOUR_12 for "2:30 PM" style, HOUR_24 for "14:30" style
     */
    void set_time_format(TimeFormat format);

    /** @brief Get dropdown options string "12 Hour\n24 Hour" */
    static const char* get_time_format_options();

    // =========================================================================
    // LANGUAGE SETTINGS
    // =========================================================================

    /**
     * @brief Get current language code
     * @return Language code (e.g., "en", "de", "fr", "es", "ru")
     */
    std::string get_language() const;

    /**
     * @brief Set language and apply translations
     *
     * Updates subject, calls lv_translation_set_language() for hot-reload,
     * and persists to Config. UI automatically updates via LVGL events.
     *
     * @param lang Language code (e.g., "en", "de", "fr", "es", "ru")
     */
    void set_language(const std::string& lang);

    /**
     * @brief Set language by dropdown index
     * @param index Index in language options dropdown (0=English, 1=German, etc.)
     */
    void set_language_by_index(int index);

    /**
     * @brief Get current language dropdown index
     * @return Index of current language in dropdown options
     */
    int get_language_index() const;

    /** @brief Get dropdown options string "English\nDeutsch\nFrançais\nEspañol\nРусский" */
    static const char* get_language_options();

    /** @brief Get language code for dropdown index */
    static std::string language_index_to_code(int index);

    /** @brief Get dropdown index for language code */
    static int language_code_to_index(const std::string& code);

    // =========================================================================
    // PRINTER SETTINGS
    // =========================================================================

    /**
     * @brief Get LED enabled state
     * @return true if LED is on
     */
    bool get_led_enabled() const;

    /**
     * @brief Set LED enabled state
     *
     * Updates subject, sends Moonraker command, and persists startup preference.
     * The LED state is saved as "LED on at start" preference.
     *
     * @param enabled true to turn on, false to turn off
     */
    void set_led_enabled(bool enabled);

    // =========================================================================
    // Z MOVEMENT STYLE (override auto-detected bed vs nozzle movement)
    // =========================================================================

    /** @brief Get Z movement style override (Auto/Bed Moves/Nozzle Moves) */
    ZMovementStyle get_z_movement_style() const;

    /** @brief Set Z movement style override and apply to printer state */
    void set_z_movement_style(ZMovementStyle style);

    /** @brief Get dropdown options string "Auto\nBed Moves\nNozzle Moves" */
    static const char* get_z_movement_style_options();

    /** @brief Z movement style subject (integer: 0=Auto, 1=Bed Moves, 2=Nozzle Moves) */
    lv_subject_t* subject_z_movement_style() {
        return &z_movement_style_subject_;
    }

    // =========================================================================
    // INPUT SETTINGS (require restart)
    // =========================================================================

    /**
     * @brief Get scroll throw (momentum decay rate)
     * @return Scroll throw value (1-99, higher = faster decay)
     */
    int get_scroll_throw() const;

    /**
     * @brief Set scroll throw (momentum decay rate)
     *
     * Persists to config. Requires restart to take effect.
     *
     * @param value Scroll throw (1-99)
     */
    void set_scroll_throw(int value);

    /**
     * @brief Get scroll limit (pixels before scrolling starts)
     * @return Scroll limit in pixels
     */
    int get_scroll_limit() const;

    /**
     * @brief Set scroll limit (pixels before scrolling starts)
     *
     * Persists to config. Requires restart to take effect.
     *
     * @param value Scroll limit in pixels
     */
    void set_scroll_limit(int value);

    /**
     * @brief Check if restart is pending due to settings changes
     * @return true if settings changed that require restart
     */
    bool is_restart_pending() const {
        return restart_pending_;
    }

    /**
     * @brief Clear restart pending flag
     */
    void clear_restart_pending() {
        restart_pending_ = false;
    }

    // =========================================================================
    // SAFETY SETTINGS
    // =========================================================================

    /**
     * @brief Get E-Stop confirmation requirement
     * @return true if confirmation dialog required before E-Stop
     */
    bool get_estop_require_confirmation() const;

    /**
     * @brief Set E-Stop confirmation requirement
     *
     * When enabled, the E-Stop button shows a confirmation dialog before
     * sending M112. When disabled (default), E-Stop executes immediately.
     *
     * @param require true to require confirmation, false for immediate action
     */
    void set_estop_require_confirmation(bool require);

    /**
     * @brief Get cancel escalation enabled state
     * @return true if cancel should escalate to e-stop on timeout
     */
    bool get_cancel_escalation_enabled() const;

    /**
     * @brief Set cancel escalation enabled state
     *
     * When enabled, a cancel that doesn't complete within the configured
     * timeout will escalate to M112 emergency stop. When disabled (default),
     * cancel waits indefinitely for the printer to respond.
     *
     * @param enabled true to enable escalation
     */
    void set_cancel_escalation_enabled(bool enabled);

    /**
     * @brief Get cancel escalation timeout in seconds
     * @return Timeout in seconds (15, 30, 60, or 120)
     */
    int get_cancel_escalation_timeout_seconds() const;

    /**
     * @brief Set cancel escalation timeout in seconds
     * @param seconds Timeout value (clamped to 15-120)
     */
    void set_cancel_escalation_timeout_seconds(int seconds);

    // =========================================================================
    // NOTIFICATION SETTINGS
    // =========================================================================

    /**
     * @brief Get sound enabled state (master switch)
     * @return true if sounds enabled
     */
    bool get_sounds_enabled() const;

    /**
     * @brief Set sound enabled state (master switch)
     *
     * Controls all sound playback. Updates subject and persists.
     *
     * @param enabled true to enable sounds
     */
    void set_sounds_enabled(bool enabled);

    /**
     * @brief Get master volume level (0-100)
     * @return Volume percentage (0=mute, 100=full)
     */
    int get_volume() const;

    /**
     * @brief Set master volume level
     *
     * Attenuates all sound output. Updates subject and persists.
     *
     * @param volume Volume percentage (0-100, clamped)
     */
    void set_volume(int volume);

    /**
     * @brief Get UI sounds enabled state
     * @return true if UI interaction sounds (taps, nav) enabled
     */
    bool get_ui_sounds_enabled() const;

    /**
     * @brief Set UI sounds enabled state
     *
     * Controls UI interaction sounds separately from event sounds.
     * Only affects button taps, navigation, toggles, dropdowns.
     * Print complete/error/alarm sounds are unaffected.
     *
     * @param enabled true to enable UI sounds
     */
    void set_ui_sounds_enabled(bool enabled);

    /**
     * @brief Get current sound theme name
     * @return Theme name (e.g., "default", "minimal")
     */
    std::string get_sound_theme() const;

    /**
     * @brief Set sound theme name
     *
     * Persists to config. SoundManager reloads the theme.
     *
     * @param name Theme name (corresponds to config/sounds/<name>.json)
     */
    void set_sound_theme(const std::string& name);

    /** @brief Get completion alert mode (Off/Notification/Alert) */
    CompletionAlertMode get_completion_alert_mode() const;

    /** @brief Set completion alert mode */
    void set_completion_alert_mode(CompletionAlertMode mode);

    /** @brief Get dropdown options string "Off\nNotification\nAlert" */
    static const char* get_completion_alert_options();

    // =========================================================================
    // SUBJECT ACCESSORS (for XML binding)
    // =========================================================================

    /** @brief Dark mode subject (integer: 0=light, 1=dark) */
    lv_subject_t* subject_dark_mode() {
        return &dark_mode_subject_;
    }

    /** @brief Dark mode available subject (integer: 0=theme doesn't support toggle, 1=theme
     * supports both modes) */
    lv_subject_t* subject_dark_mode_available() {
        return &dark_mode_available_subject_;
    }

    /** @brief Theme preset subject (integer: preset index) */
    lv_subject_t* subject_theme_preset() {
        return &theme_preset_subject_;
    }

    /** @brief Display dim subject (integer: seconds, 0=disabled) */
    lv_subject_t* subject_display_dim() {
        return &display_dim_subject_;
    }

    /** @brief Display sleep subject (integer: seconds, 0=disabled) */
    lv_subject_t* subject_display_sleep() {
        return &display_sleep_subject_;
    }

    /** @brief Brightness subject (integer: 10-100 percent) */
    lv_subject_t* subject_brightness() {
        return &brightness_subject_;
    }

    /** @brief Has backlight control subject (integer: 0=no, 1=yes) */
    lv_subject_t* subject_has_backlight() {
        return &has_backlight_subject_;
    }

    /** @brief Sleep while printing subject (integer: 0=inhibit, 1=allow) */
    lv_subject_t* subject_sleep_while_printing() {
        return &sleep_while_printing_subject_;
    }

    /** @brief Animations enabled subject (integer: 0=off, 1=on) */
    lv_subject_t* subject_animations_enabled() {
        return &animations_enabled_subject_;
    }

    /** @brief G-code 3D preview subject (integer: 0=off, 1=on) */
    lv_subject_t* subject_gcode_3d_enabled() {
        return &gcode_3d_enabled_subject_;
    }

    /** @brief Bed mesh render mode subject (integer: 0=auto, 1=3D, 2=2D) */
    lv_subject_t* subject_bed_mesh_render_mode() {
        return &bed_mesh_render_mode_subject_;
    }

    /** @brief G-code render mode subject (integer: 0=auto, 1=3D, 2=2D) */
    lv_subject_t* subject_gcode_render_mode() {
        return &gcode_render_mode_subject_;
    }

    /** @brief Time format subject (integer: 0=12H, 1=24H) */
    lv_subject_t* subject_time_format() {
        return &time_format_subject_;
    }

    /** @brief Language subject (integer: index into language options) */
    lv_subject_t* subject_language() {
        return &language_subject_;
    }

    /** @brief LED enabled subject (integer: 0=off, 1=on) */
    lv_subject_t* subject_led_enabled() {
        return &led_enabled_subject_;
    }

    /** @brief Sounds enabled subject (integer: 0=off, 1=on) */
    lv_subject_t* subject_sounds_enabled() {
        return &sounds_enabled_subject_;
    }

    /** @brief UI sounds enabled subject (integer: 0=off, 1=on) */
    lv_subject_t* subject_ui_sounds_enabled() {
        return &ui_sounds_enabled_subject_;
    }

    /** @brief Volume subject (integer: 0-100 percent) */
    lv_subject_t* subject_volume() {
        return &volume_subject_;
    }

    /** @brief Completion alert subject (integer: 0=off, 1=on) */
    lv_subject_t* subject_completion_alert() {
        return &completion_alert_subject_;
    }

    /** @brief Scroll throw subject (integer: 1-99) */
    lv_subject_t* subject_scroll_throw() {
        return &scroll_throw_subject_;
    }

    /** @brief Scroll limit subject (integer: pixels) */
    lv_subject_t* subject_scroll_limit() {
        return &scroll_limit_subject_;
    }

    /** @brief E-Stop confirmation subject (integer: 0=immediate, 1=require confirm) */
    lv_subject_t* subject_estop_require_confirmation() {
        return &estop_require_confirmation_subject_;
    }

    /** @brief Cancel escalation enabled subject (integer: 0=disabled, 1=enabled) */
    lv_subject_t* subject_cancel_escalation_enabled() {
        return &cancel_escalation_enabled_subject_;
    }

    /** @brief Cancel escalation timeout subject (integer: dropdown index 0-3) */
    lv_subject_t* subject_cancel_escalation_timeout() {
        return &cancel_escalation_timeout_subject_;
    }

    /** @brief Update channel subject (integer: 0=Stable, 1=Beta, 2=Dev) */
    lv_subject_t* subject_update_channel() {
        return &update_channel_subject_;
    }

    // =========================================================================
    // TELEMETRY SETTINGS
    // =========================================================================

    /** @brief Get telemetry enabled state */
    bool get_telemetry_enabled() const;

    /** @brief Set telemetry enabled state (persists to config + notifies TelemetryManager) */
    void set_telemetry_enabled(bool enabled);

    /** @brief Telemetry enabled subject (integer: 0=off, 1=on) */
    lv_subject_t* subject_telemetry_enabled() {
        return &telemetry_enabled_subject_;
    }

    // =========================================================================
    // UPDATE SETTINGS
    // =========================================================================

    /** @brief Get current update channel (0=Stable, 1=Beta, 2=Dev) */
    int get_update_channel() const;

    /** @brief Set update channel, persist, and clear update cache */
    void set_update_channel(int channel);

    /** @brief Get dropdown options string "Stable\nBeta\nDev" */
    static const char* get_update_channel_options();

    // =========================================================================
    // DISPLAY DIM OPTIONS (for dropdown population)
    // =========================================================================

    /**
     * @brief Get display dim options for dropdown
     * @return Newline-separated string of options (e.g., "Never\n30 seconds\n1 minute")
     */
    static const char* get_display_dim_options();

    /**
     * @brief Get dropdown index for current dim seconds value
     * @param seconds Current dim timeout in seconds
     * @return Dropdown index (0-based)
     */
    static int dim_seconds_to_index(int seconds);

    /**
     * @brief Convert dropdown index to dim seconds
     * @param index Dropdown index (0-based)
     * @return Dim timeout in seconds
     */
    static int index_to_dim_seconds(int index);

    // =========================================================================
    // DISPLAY SLEEP OPTIONS (for dropdown population)
    // =========================================================================

    /**
     * @brief Get display sleep options for dropdown
     * @return Newline-separated string of options (e.g., "Never\n1 minute\n5 minutes")
     */
    static const char* get_display_sleep_options();

    /**
     * @brief Get dropdown index for current sleep seconds value
     * @param seconds Current sleep timeout in seconds
     * @return Dropdown index (0-based)
     */
    static int sleep_seconds_to_index(int seconds);

    /**
     * @brief Convert dropdown index to sleep seconds
     * @param index Dropdown index (0-based)
     * @return Sleep timeout in seconds
     */
    static int index_to_sleep_seconds(int index);

  private:
    SettingsManager();
    ~SettingsManager() = default;

    // Subject manager for RAII cleanup
    SubjectManager subjects_;

    // LVGL subjects
    lv_subject_t dark_mode_subject_;
    lv_subject_t dark_mode_available_subject_;
    lv_subject_t theme_preset_subject_;
    lv_subject_t display_dim_subject_;
    lv_subject_t display_sleep_subject_;
    lv_subject_t brightness_subject_;
    lv_subject_t has_backlight_subject_;
    lv_subject_t sleep_while_printing_subject_;
    lv_subject_t animations_enabled_subject_;
    lv_subject_t gcode_3d_enabled_subject_;
    lv_subject_t bed_mesh_render_mode_subject_;
    lv_subject_t gcode_render_mode_subject_;
    lv_subject_t time_format_subject_;
    lv_subject_t language_subject_;
    lv_subject_t led_enabled_subject_;
    lv_subject_t sounds_enabled_subject_;
    lv_subject_t ui_sounds_enabled_subject_;
    lv_subject_t volume_subject_;
    lv_subject_t completion_alert_subject_;
    lv_subject_t estop_require_confirmation_subject_;
    lv_subject_t cancel_escalation_enabled_subject_;
    lv_subject_t cancel_escalation_timeout_subject_;
    lv_subject_t scroll_throw_subject_;
    lv_subject_t scroll_limit_subject_;
    lv_subject_t update_channel_subject_;
    lv_subject_t z_movement_style_subject_;
    lv_subject_t telemetry_enabled_subject_;

    // External references
    MoonrakerClient* moonraker_client_ = nullptr;

    // State
    bool subjects_initialized_ = false;
    bool restart_pending_ = false;
};

} // namespace helix
using helix::CompletionAlertMode;
using helix::SettingsManager;
using helix::TimeFormat;
using helix::ZMovementStyle;
