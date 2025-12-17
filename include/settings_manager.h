// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"

#include <functional>
#include <memory>
#include <string>

class BacklightBackend;
class MoonrakerClient;

/** @brief Print completion notification mode (Off=0, Notification=1, Alert=2) */
enum class CompletionAlertMode { OFF = 0, NOTIFICATION = 1, ALERT = 2 };

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
     * Updates subject and sends Moonraker command if client is connected.
     * Note: Does NOT persist - LED state is ephemeral.
     *
     * @param enabled true to turn on, false to turn off
     */
    void set_led_enabled(bool enabled);

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

    // =========================================================================
    // NOTIFICATION SETTINGS (placeholders for future hardware)
    // =========================================================================

    /**
     * @brief Get sound enabled state
     * @return true if sounds enabled
     */
    bool get_sounds_enabled() const;

    /**
     * @brief Set sound enabled state
     *
     * Placeholder - hardware support TBD. Updates subject and persists.
     *
     * @param enabled true to enable sounds
     */
    void set_sounds_enabled(bool enabled);

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

    /** @brief LED enabled subject (integer: 0=off, 1=on) */
    lv_subject_t* subject_led_enabled() {
        return &led_enabled_subject_;
    }

    /** @brief Sounds enabled subject (integer: 0=off, 1=on) */
    lv_subject_t* subject_sounds_enabled() {
        return &sounds_enabled_subject_;
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

    // =========================================================================
    // DISPLAY SLEEP MANAGEMENT
    // =========================================================================

    /**
     * @brief Check inactivity and trigger display sleep if timeout exceeded
     *
     * Call this from the main event loop. Uses LVGL's built-in inactivity
     * tracking (lv_display_get_inactive_time) and the configured sleep timeout.
     *
     * When sleep triggers: backlight dims to minimum (10%)
     * When touch detected: backlight restores to previous brightness
     */
    void check_display_sleep();

    /**
     * @brief Check if display is currently in sleep mode
     * @return true if display is sleeping (dimmed due to inactivity)
     */
    bool is_display_sleeping() const {
        return display_sleeping_;
    }

    /**
     * @brief Manually wake the display (e.g., on important notification)
     *
     * Restores brightness to saved level and resets inactivity timer.
     */
    void wake_display();

    /**
     * @brief Force display ON at startup
     *
     * Called early in app initialization to ensure display is visible regardless
     * of previous app's sleep state. Also disables OS console blanking on Linux.
     */
    void ensure_display_on();

    /**
     * @brief Restore display to usable state on shutdown
     *
     * Called during app cleanup to ensure display is awake before exiting.
     * Prevents next app from starting with a black screen.
     */
    void restore_display_on_shutdown();

  private:
    SettingsManager();
    ~SettingsManager() = default;

    // Apply immediate effects
    void send_led_command(bool enabled);

    // Backlight backend
    std::unique_ptr<BacklightBackend> backlight_backend_;

    // LVGL subjects
    lv_subject_t dark_mode_subject_;
    lv_subject_t display_sleep_subject_;
    lv_subject_t brightness_subject_;
    lv_subject_t has_backlight_subject_;
    lv_subject_t animations_enabled_subject_;
    lv_subject_t gcode_3d_enabled_subject_;
    lv_subject_t bed_mesh_render_mode_subject_;
    lv_subject_t led_enabled_subject_;
    lv_subject_t sounds_enabled_subject_;
    lv_subject_t completion_alert_subject_;
    lv_subject_t estop_require_confirmation_subject_;
    lv_subject_t scroll_throw_subject_;
    lv_subject_t scroll_limit_subject_;

    // External references
    MoonrakerClient* moonraker_client_ = nullptr;

    // State
    bool subjects_initialized_ = false;
    bool restart_pending_ = false;

    // Display sleep state
    bool display_sleeping_ = false;
};
