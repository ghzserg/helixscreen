// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings_manager.h"

#include "ui_theme.h"

#include "backlight_backend.h"
#include "config.h"
#include "moonraker_client.h"
#include "runtime_config.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cstring>

// Display sleep option values (seconds)
// Index: 0=Never, 1=1min, 2=5min, 3=10min, 4=30min
static const int SLEEP_OPTIONS[] = {0, 60, 300, 600, 1800};
static const int SLEEP_OPTIONS_COUNT = sizeof(SLEEP_OPTIONS) / sizeof(SLEEP_OPTIONS[0]);
static const char* SLEEP_OPTIONS_TEXT = "Never\n1 minute\n5 minutes\n10 minutes\n30 minutes";

// Completion alert options (Off=0, Notification=1, Alert=2)
static const char* COMPLETION_ALERT_OPTIONS_TEXT = "Off\nNotification\nAlert";

// Bed mesh render mode options (Auto=0, 3D=1, 2D=2)
static const char* BED_MESH_RENDER_MODE_OPTIONS_TEXT = "Auto\n3D View\n2D Heatmap";

SettingsManager& SettingsManager::instance() {
    static SettingsManager instance;
    return instance;
}

SettingsManager::SettingsManager() {
    spdlog::trace("[SettingsManager] Constructor");
}

void SettingsManager::init_subjects() {
    if (subjects_initialized_) {
        spdlog::debug("[SettingsManager] Subjects already initialized, skipping");
        return;
    }

    spdlog::debug("[SettingsManager] Initializing subjects");

    // Initialize backlight backend (auto-detects hardware)
    backlight_backend_ = BacklightBackend::create();
    spdlog::info("[SettingsManager] Backlight backend: {} (available: {})",
                 backlight_backend_->name(), backlight_backend_->is_available());

    // Get initial values from Config
    Config* config = Config::get_instance();

    // Dark mode (default: true = dark)
    bool dark_mode = config->get<bool>("/dark_mode", true);
    lv_subject_init_int(&dark_mode_subject_, dark_mode ? 1 : 0);

    // Display sleep (default: 1800 seconds = 30 minutes)
    int sleep_sec = config->get<int>("/display_sleep_sec", 1800);
    lv_subject_init_int(&display_sleep_subject_, sleep_sec);

    // Brightness: Read from hardware first, fall back to config
    // This ensures UI reflects actual display state on startup
    // NOTE: If hardware reports 0 (screen off), use config value - we always want to turn ON
    int brightness = backlight_backend_->get_brightness();
    bool brightness_from_hardware = (brightness > 0); // 0 means "off", treat as no value
    if (!brightness_from_hardware) {
        // Fall back to config value (or default 50%)
        brightness = config->get<int>("/brightness", 50);
    }
    brightness = std::clamp(brightness, 10, 100); // Always ensure minimum 10%
    lv_subject_init_int(&brightness_subject_, brightness);
    spdlog::info("[SettingsManager] Brightness initialized to {}% (from {})", brightness,
                 brightness_from_hardware ? "hardware" : "config");

    // Ensure backlight is ON at startup (may have been off from sleep or crash)
    if (backlight_backend_->is_available()) {
        backlight_backend_->set_brightness(brightness);
        spdlog::info("[SettingsManager] Backlight ON at {}%", brightness);
    }

    // Has backlight control subject (for UI visibility)
    lv_subject_init_int(&has_backlight_subject_, backlight_backend_->is_available() ? 1 : 0);

    // LED state (ephemeral, not persisted - start as off)
    lv_subject_init_int(&led_enabled_subject_, 0);

    // Sounds (default: true)
    bool sounds = config->get<bool>("/sounds_enabled", true);
    lv_subject_init_int(&sounds_enabled_subject_, sounds ? 1 : 0);

    // Completion alert mode (default: NOTIFICATION=1, handles old bool migration)
    int completion_mode = config->get<int>("/completion_alert", 1);
    completion_mode = std::max(0, std::min(2, completion_mode));
    lv_subject_init_int(&completion_alert_subject_, completion_mode);

    // E-Stop confirmation (default: false = immediate action)
    bool estop_confirm = config->get<bool>("/safety/estop_require_confirmation", false);
    lv_subject_init_int(&estop_require_confirmation_subject_, estop_confirm ? 1 : 0);

    // Scroll throw (default: 25, range 5-50)
    int scroll_throw = config->get<int>("/input/scroll_throw", 25);
    scroll_throw = std::max(5, std::min(50, scroll_throw));
    lv_subject_init_int(&scroll_throw_subject_, scroll_throw);

    // Scroll limit (default: 5, range 1-20)
    int scroll_limit = config->get<int>("/input/scroll_limit", 5);
    scroll_limit = std::max(1, std::min(20, scroll_limit));
    lv_subject_init_int(&scroll_limit_subject_, scroll_limit);

    // Animations enabled (default: true)
    bool animations = config->get<bool>("/animations_enabled", true);
    lv_subject_init_int(&animations_enabled_subject_, animations ? 1 : 0);

    // G-code 3D preview enabled (default: true)
    bool gcode_3d = config->get<bool>("/display/gcode_3d_enabled", true);
    lv_subject_init_int(&gcode_3d_enabled_subject_, gcode_3d ? 1 : 0);

    // Bed mesh render mode (default: 0 = Auto)
    int bed_mesh_mode = config->get<int>("/display/bed_mesh_render_mode", 0);
    bed_mesh_mode = std::clamp(bed_mesh_mode, 0, 2);
    lv_subject_init_int(&bed_mesh_render_mode_subject_, bed_mesh_mode);

    // Register subjects with LVGL XML system for data binding
    lv_xml_register_subject(nullptr, "settings_dark_mode", &dark_mode_subject_);
    lv_xml_register_subject(nullptr, "settings_display_sleep", &display_sleep_subject_);
    lv_xml_register_subject(nullptr, "settings_brightness", &brightness_subject_);
    lv_xml_register_subject(nullptr, "settings_has_backlight", &has_backlight_subject_);
    lv_xml_register_subject(nullptr, "settings_led_enabled", &led_enabled_subject_);
    lv_xml_register_subject(nullptr, "settings_sounds_enabled", &sounds_enabled_subject_);
    lv_xml_register_subject(nullptr, "settings_completion_alert", &completion_alert_subject_);
    lv_xml_register_subject(nullptr, "settings_estop_confirm",
                            &estop_require_confirmation_subject_);
    lv_xml_register_subject(nullptr, "settings_scroll_throw", &scroll_throw_subject_);
    lv_xml_register_subject(nullptr, "settings_scroll_limit", &scroll_limit_subject_);
    lv_xml_register_subject(nullptr, "settings_animations_enabled", &animations_enabled_subject_);
    lv_xml_register_subject(nullptr, "settings_gcode_3d_enabled", &gcode_3d_enabled_subject_);
    lv_xml_register_subject(nullptr, "settings_bed_mesh_render_mode",
                            &bed_mesh_render_mode_subject_);

    subjects_initialized_ = true;
    spdlog::info("[SettingsManager] Subjects initialized: dark_mode={}, sleep={}s, sounds={}, "
                 "completion_alert_mode={}, scroll_throw={}, scroll_limit={}, animations={}",
                 dark_mode, sleep_sec, sounds, completion_mode, scroll_throw, scroll_limit,
                 animations);
}

void SettingsManager::set_moonraker_client(MoonrakerClient* client) {
    moonraker_client_ = client;
    spdlog::debug("[SettingsManager] Moonraker client set: {}", client ? "connected" : "nullptr");
}

// =============================================================================
// APPEARANCE SETTINGS
// =============================================================================

bool SettingsManager::get_dark_mode() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&dark_mode_subject_)) != 0;
}

void SettingsManager::set_dark_mode(bool enabled) {
    spdlog::info("[SettingsManager] set_dark_mode({})", enabled);

    // 1. Update subject (UI reacts immediately via binding)
    lv_subject_set_int(&dark_mode_subject_, enabled ? 1 : 0);

    // 2. Persist to config (theme change requires restart to take effect)
    Config* config = Config::get_instance();
    config->set<bool>("/dark_mode", enabled);
    config->save();

    spdlog::debug("[SettingsManager] Dark mode {} saved (restart required)",
                  enabled ? "enabled" : "disabled");
}

int SettingsManager::get_display_sleep_sec() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&display_sleep_subject_));
}

void SettingsManager::set_display_sleep_sec(int seconds) {
    spdlog::info("[SettingsManager] set_display_sleep_sec({})", seconds);

    // 1. Update subject
    lv_subject_set_int(&display_sleep_subject_, seconds);

    // 2. Persist
    Config* config = Config::get_instance();
    config->set<int>("/display_sleep_sec", seconds);
    config->save();

    // Note: Actual display sleep is handled by the display driver reading this value
    spdlog::debug("[SettingsManager] Display sleep set to {}s", seconds);
}

int SettingsManager::get_brightness() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&brightness_subject_));
}

void SettingsManager::set_brightness(int percent) {
    // Clamp to valid range (10-100, minimum 10% to prevent black screen)
    int clamped = std::clamp(percent, 10, 100);
    spdlog::info("[SettingsManager] set_brightness({})", clamped);

    // 1. Update subject (UI reflects change immediately)
    lv_subject_set_int(&brightness_subject_, clamped);

    // 2. Apply to hardware via backend
    if (backlight_backend_) {
        backlight_backend_->set_brightness(clamped);
    }

    // 3. Persist to config
    Config* config = Config::get_instance();
    config->set<int>("/brightness", clamped);
    config->save();
}

bool SettingsManager::has_backlight_control() const {
    return backlight_backend_ && backlight_backend_->is_available();
}

bool SettingsManager::get_animations_enabled() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&animations_enabled_subject_)) != 0;
}

void SettingsManager::set_animations_enabled(bool enabled) {
    spdlog::info("[SettingsManager] set_animations_enabled({})", enabled);

    // 1. Update subject (UI reacts, animations check this dynamically)
    lv_subject_set_int(&animations_enabled_subject_, enabled ? 1 : 0);

    // 2. Persist to config
    Config* config = Config::get_instance();
    config->set<bool>("/animations_enabled", enabled);
    config->save();
}

bool SettingsManager::get_gcode_3d_enabled() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&gcode_3d_enabled_subject_)) != 0;
}

void SettingsManager::set_gcode_3d_enabled(bool enabled) {
    spdlog::info("[SettingsManager] set_gcode_3d_enabled({})", enabled);

    // 1. Update subject (UI reacts)
    lv_subject_set_int(&gcode_3d_enabled_subject_, enabled ? 1 : 0);

    // 2. Persist to config
    Config* config = Config::get_instance();
    config->set<bool>("/display/gcode_3d_enabled", enabled);
    config->save();
}

int SettingsManager::get_bed_mesh_render_mode() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&bed_mesh_render_mode_subject_));
}

void SettingsManager::set_bed_mesh_render_mode(int mode) {
    // Clamp to valid range (0=Auto, 1=3D, 2=2D)
    int clamped = std::clamp(mode, 0, 2);
    spdlog::info("[SettingsManager] set_bed_mesh_render_mode({})", clamped);

    // 1. Update subject (UI reacts)
    lv_subject_set_int(&bed_mesh_render_mode_subject_, clamped);

    // 2. Persist to config
    Config* config = Config::get_instance();
    config->set<int>("/display/bed_mesh_render_mode", clamped);
    config->save();

    spdlog::debug("[SettingsManager] Bed mesh render mode set to {} ({})", clamped,
                  clamped == 0 ? "Auto" : (clamped == 1 ? "3D" : "2D"));
}

const char* SettingsManager::get_bed_mesh_render_mode_options() {
    return BED_MESH_RENDER_MODE_OPTIONS_TEXT;
}

// =============================================================================
// PRINTER SETTINGS
// =============================================================================

bool SettingsManager::get_led_enabled() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&led_enabled_subject_)) != 0;
}

void SettingsManager::set_led_enabled(bool enabled) {
    spdlog::info("[SettingsManager] set_led_enabled({})", enabled);

    // 1. Update subject (UI reacts)
    lv_subject_set_int(&led_enabled_subject_, enabled ? 1 : 0);

    // 2. Send command to printer (if connected)
    send_led_command(enabled);

    // Note: LED state is NOT persisted - it's ephemeral
}

void SettingsManager::send_led_command(bool enabled) {
    if (!moonraker_client_) {
        spdlog::warn("[SettingsManager] Cannot send LED command - no Moonraker client");
        return;
    }

    // Use common LED pin name - this should be configurable in the future
    // Common names: caselight, chamber_light, led, status_led
    std::string gcode = enabled ? "SET_PIN PIN=caselight VALUE=1" : "SET_PIN PIN=caselight VALUE=0";

    // gcode_script returns request_id (>0) on success, -1 on failure
    int result = moonraker_client_->gcode_script(gcode);
    if (result > 0) {
        spdlog::debug("[SettingsManager] LED {} command sent (request_id={})",
                      enabled ? "on" : "off", result);
    } else {
        spdlog::warn(
            "[SettingsManager] Failed to send LED command - printer may not have caselight "
            "pin or not connected");
    }
}

// =============================================================================
// NOTIFICATION SETTINGS
// =============================================================================

bool SettingsManager::get_sounds_enabled() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&sounds_enabled_subject_)) != 0;
}

void SettingsManager::set_sounds_enabled(bool enabled) {
    spdlog::info("[SettingsManager] set_sounds_enabled({})", enabled);

    lv_subject_set_int(&sounds_enabled_subject_, enabled ? 1 : 0);

    Config* config = Config::get_instance();
    config->set<bool>("/sounds_enabled", enabled);
    config->save();

    // Note: Actual sound playback is a placeholder - hardware TBD
}

CompletionAlertMode SettingsManager::get_completion_alert_mode() const {
    int val = lv_subject_get_int(const_cast<lv_subject_t*>(&completion_alert_subject_));
    return static_cast<CompletionAlertMode>(std::max(0, std::min(2, val)));
}

void SettingsManager::set_completion_alert_mode(CompletionAlertMode mode) {
    int val = static_cast<int>(mode);
    spdlog::info("[SettingsManager] set_completion_alert_mode({})", val);
    lv_subject_set_int(&completion_alert_subject_, val);
    Config* config = Config::get_instance();
    config->set<int>("/completion_alert", val);
    config->save();
}

const char* SettingsManager::get_completion_alert_options() {
    return COMPLETION_ALERT_OPTIONS_TEXT;
}

// =============================================================================
// DISPLAY SLEEP OPTIONS
// =============================================================================

const char* SettingsManager::get_display_sleep_options() {
    return SLEEP_OPTIONS_TEXT;
}

int SettingsManager::sleep_seconds_to_index(int seconds) {
    for (int i = 0; i < SLEEP_OPTIONS_COUNT; i++) {
        if (SLEEP_OPTIONS[i] == seconds) {
            return i;
        }
    }
    // Default to "10 minutes" if not found
    return 3;
}

int SettingsManager::index_to_sleep_seconds(int index) {
    if (index >= 0 && index < SLEEP_OPTIONS_COUNT) {
        return SLEEP_OPTIONS[index];
    }
    return 600; // Default 10 minutes
}

// =============================================================================
// INPUT SETTINGS (require restart)
// =============================================================================

int SettingsManager::get_scroll_throw() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&scroll_throw_subject_));
}

void SettingsManager::set_scroll_throw(int value) {
    // Clamp to valid range (5-50)
    int clamped = std::max(5, std::min(50, value));
    spdlog::info("[SettingsManager] set_scroll_throw({})", clamped);

    // 1. Update subject
    lv_subject_set_int(&scroll_throw_subject_, clamped);

    // 2. Persist
    Config* config = Config::get_instance();
    config->set<int>("/input/scroll_throw", clamped);
    config->save();

    // 3. Mark restart needed (this setting only takes effect on startup)
    restart_pending_ = true;
    spdlog::debug("[SettingsManager] Scroll throw set to {} (restart required)", clamped);
}

int SettingsManager::get_scroll_limit() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&scroll_limit_subject_));
}

void SettingsManager::set_scroll_limit(int value) {
    // Clamp to valid range (1-20)
    int clamped = std::max(1, std::min(20, value));
    spdlog::info("[SettingsManager] set_scroll_limit({})", clamped);

    // 1. Update subject
    lv_subject_set_int(&scroll_limit_subject_, clamped);

    // 2. Persist
    Config* config = Config::get_instance();
    config->set<int>("/input/scroll_limit", clamped);
    config->save();

    // 3. Mark restart needed (this setting only takes effect on startup)
    restart_pending_ = true;
    spdlog::debug("[SettingsManager] Scroll limit set to {} (restart required)", clamped);
}

// =============================================================================
// SAFETY SETTINGS
// =============================================================================

bool SettingsManager::get_estop_require_confirmation() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&estop_require_confirmation_subject_)) != 0;
}

void SettingsManager::set_estop_require_confirmation(bool require) {
    spdlog::info("[SettingsManager] set_estop_require_confirmation({})", require);

    // 1. Update subject (UI reacts)
    lv_subject_set_int(&estop_require_confirmation_subject_, require ? 1 : 0);

    // 2. Persist
    Config* config = Config::get_instance();
    config->set<bool>("/safety/estop_require_confirmation", require);
    config->save();

    spdlog::debug("[SettingsManager] E-Stop confirmation {} and saved",
                  require ? "enabled" : "disabled");
}

// =============================================================================
// DISPLAY SLEEP MANAGEMENT
// =============================================================================

void SettingsManager::check_display_sleep() {
    // Get configured sleep timeout (0 = disabled)
    int sleep_timeout_sec = get_display_sleep_sec();
    if (sleep_timeout_sec == 0) {
        // Sleep disabled - ensure we're awake
        if (display_sleeping_) {
            wake_display();
        }
        return;
    }

    // Get LVGL inactivity time (milliseconds since last touch/input)
    uint32_t inactive_ms = lv_display_get_inactive_time(nullptr);
    uint32_t timeout_ms = static_cast<uint32_t>(sleep_timeout_sec) * 1000U;

    if (display_sleeping_) {
        // Currently sleeping - check if user touched screen (activity detected)
        // LVGL resets inactivity time on any input event
        if (inactive_ms < 500) { // Touch detected (< 500ms since activity)
            wake_display();
        }
    } else {
        // Currently awake - check if we should sleep
        if (inactive_ms >= timeout_ms) {
            display_sleeping_ = true;

            // Turn backlight OFF (0%) - don't use set_brightness() to avoid persisting
            if (backlight_backend_) {
                backlight_backend_->set_brightness(0);
            }
            spdlog::info("[DisplaySleep] Display sleeping (backlight off) after {}s inactivity",
                         sleep_timeout_sec);
        }
    }
}

void SettingsManager::wake_display() {
    if (!display_sleeping_) {
        return; // Already awake
    }

    display_sleeping_ = false;

    // Restore configured brightness from config file (source of truth)
    Config* config = Config::get_instance();
    int brightness = config->get<int>("/brightness", 50);
    brightness = std::clamp(brightness, 10, 100);

    if (backlight_backend_) {
        backlight_backend_->set_brightness(brightness);
    }
    spdlog::info("[DisplaySleep] Display woken, brightness restored to {}% (from config)",
                 brightness);
}

void SettingsManager::ensure_display_on() {
    // Force display awake at startup regardless of previous state
    display_sleeping_ = false;

    // Get configured brightness (or default to 50%)
    Config* config = Config::get_instance();
    int brightness = config->get<int>("/brightness", 50);
    brightness = std::clamp(brightness, 10, 100);

    // Apply to hardware - this ensures display is visible
    if (backlight_backend_) {
        backlight_backend_->set_brightness(brightness);
    }
    spdlog::info("[Display] Startup: forcing display ON at {}% brightness", brightness);

    // Note: We control display sleep via backlight brightness only.
    // Linux VT console blanking (TIOCLINUX) is not used - we're on DRM/KMS.
}

void SettingsManager::restore_display_on_shutdown() {
    // Ensure display is awake before exiting so next app doesn't start with black screen
    Config* config = Config::get_instance();
    int brightness = config->get<int>("/brightness", 50);
    brightness = std::clamp(brightness, 10, 100);

    if (backlight_backend_) {
        backlight_backend_->set_brightness(brightness);
    }
    display_sleeping_ = false;
    spdlog::info("[Display] Shutdown: restoring display to {}% brightness", brightness);
}
