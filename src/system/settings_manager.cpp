// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings_manager.h"

#include "ui_toast_manager.h"

#include "app_globals.h"
#include "config.h"
#include "display_manager.h"
#include "led/led_controller.h"
#include "lv_i18n_translations.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "moonraker_client.h"
#include "printer_state.h"
#include "runtime_config.h"
#include "spdlog/spdlog.h"
#include "static_subject_registry.h"
#include "system/telemetry_manager.h"
#include "system/update_checker.h"
#include "theme_loader.h"
#include "theme_manager.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace helix;

// Display dim option values (seconds) - time before screen dims to lower brightness
// Index: 0=Never, 1=30sec, 2=1min, 3=2min, 4=5min
static const int DIM_OPTIONS[] = {0, 30, 60, 120, 300};
static const int DIM_OPTIONS_COUNT = sizeof(DIM_OPTIONS) / sizeof(DIM_OPTIONS[0]);
static const char* DIM_OPTIONS_TEXT = "Never\n30 seconds\n1 minute\n2 minutes\n5 minutes";

// Display sleep option values (seconds) - time before screen fully sleeps
// Index: 0=Never, 1=1min, 2=5min, 3=10min, 4=30min
static const int SLEEP_OPTIONS[] = {0, 60, 300, 600, 1800};
static const int SLEEP_OPTIONS_COUNT = sizeof(SLEEP_OPTIONS) / sizeof(SLEEP_OPTIONS[0]);
static const char* SLEEP_OPTIONS_TEXT = "Never\n1 minute\n5 minutes\n10 minutes\n30 minutes";

// Completion alert options (Off=0, Notification=1, Alert=2)
static const char* COMPLETION_ALERT_OPTIONS_TEXT = "Off\nNotification\nAlert";

// Bed mesh render mode options (Auto=0, 3D=1, 2D=2)
static const char* BED_MESH_RENDER_MODE_OPTIONS_TEXT = "Auto\n3D View\n2D Heatmap";
static const char* GCODE_RENDER_MODE_OPTIONS_TEXT = "Auto\n3D View\n2D Layers";

// Z movement style options (Auto=0, Bed Moves=1, Nozzle Moves=2)
static const char* Z_MOVEMENT_STYLE_OPTIONS_TEXT = "Auto\nBed Moves\nNozzle Moves";

// Helper: Validate a timeout value against allowed options, snapping to nearest valid value
template <size_t N>
static int validate_timeout_option(int value, const int (&options)[N], int default_value,
                                   const char* setting_name) {
    // Check if value is exactly one of the valid options
    for (size_t i = 0; i < N; ++i) {
        if (options[i] == value) {
            return value; // Valid
        }
    }

    // Invalid value - find the nearest valid option
    int nearest = default_value;
    int min_diff = std::abs(value - default_value);
    for (size_t i = 0; i < N; ++i) {
        int diff = std::abs(value - options[i]);
        if (diff < min_diff) {
            min_diff = diff;
            nearest = options[i];
        }
    }

    spdlog::warn("[SettingsManager] Invalid {} value {} - snapping to nearest valid: {}",
                 setting_name, value, nearest);
    return nearest;
}

// Time format options (12H=0, 24H=1)
static const char* TIME_FORMAT_OPTIONS_TEXT = "12 Hour\n24 Hour";

// Language options - codes and display names
// Order: en, de, fr, es, ru, pt, it, zh, ja (indices 0-8)
static const char* LANGUAGE_CODES[] = {"en", "de", "fr", "es", "ru", "pt", "it", "zh", "ja"};
static const int LANGUAGE_COUNT = sizeof(LANGUAGE_CODES) / sizeof(LANGUAGE_CODES[0]);
static const char* LANGUAGE_OPTIONS_TEXT =
    "English\nDeutsch\nFrançais\nEspañol\nРусский\nPortuguês\nItaliano\n中文\n日本語";

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

    // Get initial values from Config
    Config* config = Config::get_instance();

    // Dark mode (default: true = dark)
    bool dark_mode = config->get<bool>("/dark_mode", true);
    UI_MANAGED_SUBJECT_INT(dark_mode_subject_, dark_mode ? 1 : 0, "settings_dark_mode", subjects_);

    // Dark mode availability (depends on theme - updated in on_theme_changed())
    // Start with 1 (available) - will be corrected when theme is fully loaded
    UI_MANAGED_SUBJECT_INT(dark_mode_available_subject_, 1, "settings_dark_mode_available",
                           subjects_);

    // Theme index (derived from current theme name)
    int theme_index = get_theme_index();
    UI_MANAGED_SUBJECT_INT(theme_preset_subject_, theme_index, "settings_theme_preset", subjects_);

    // Display dim (default: 300 seconds = 5 minutes)
    // Validate against allowed options to catch corrupt config values
    int dim_sec = config->get<int>("/display/dim_sec", 300);
    dim_sec = validate_timeout_option(dim_sec, DIM_OPTIONS, 300, "dim_sec");
    UI_MANAGED_SUBJECT_INT(display_dim_subject_, dim_sec, "settings_display_dim", subjects_);

    // Sync validated dim timeout to DisplayManager (it reads config directly at init,
    // so we need to push the corrected value if validation changed it)
    if (auto* dm = DisplayManager::instance()) {
        dm->set_dim_timeout(dim_sec);
    }

    // Display sleep (default: 1800 seconds = 30 minutes)
    // Validate against allowed options to catch corrupt config values
    int sleep_sec = config->get<int>("/display/sleep_sec", 1800);
    sleep_sec = validate_timeout_option(sleep_sec, SLEEP_OPTIONS, 1800, "sleep_sec");
    UI_MANAGED_SUBJECT_INT(display_sleep_subject_, sleep_sec, "settings_display_sleep", subjects_);

    // Brightness: Read from config (DisplayManager handles hardware)
    int brightness = config->get<int>("/brightness", 50);
    brightness = std::clamp(brightness, 10, 100);
    UI_MANAGED_SUBJECT_INT(brightness_subject_, brightness, "settings_brightness", subjects_);
    spdlog::debug("[SettingsManager] Brightness initialized to {}%", brightness);

    // Has backlight control subject (for UI visibility) - check DisplayManager
    bool has_backlight = false;
    if (auto* dm = DisplayManager::instance()) {
        has_backlight = dm->has_backlight_control();
    }
    UI_MANAGED_SUBJECT_INT(has_backlight_subject_, has_backlight ? 1 : 0, "settings_has_backlight",
                           subjects_);

    // LED state (ephemeral, not persisted - start as off)
    UI_MANAGED_SUBJECT_INT(led_enabled_subject_, 0, "settings_led_enabled", subjects_);

    // Sounds (default: false)
    bool sounds = config->get<bool>("/sounds_enabled", false);
    UI_MANAGED_SUBJECT_INT(sounds_enabled_subject_, sounds ? 1 : 0, "settings_sounds_enabled",
                           subjects_);

    // UI sounds (default: true) — separate toggle for button taps, nav sounds
    bool ui_sounds = config->get<bool>("/ui_sounds_enabled", true);
    UI_MANAGED_SUBJECT_INT(ui_sounds_enabled_subject_, ui_sounds ? 1 : 0,
                           "settings_ui_sounds_enabled", subjects_);

    // Volume (0-100, default 80)
    int volume = std::clamp(config->get<int>("/sounds/volume", 80), 0, 100);
    UI_MANAGED_SUBJECT_INT(volume_subject_, volume, "settings_volume", subjects_);

    // Completion alert mode (default: NOTIFICATION=1, handles old bool migration)
    int completion_mode = config->get<int>("/completion_alert", 1);
    completion_mode = std::max(0, std::min(2, completion_mode));
    UI_MANAGED_SUBJECT_INT(completion_alert_subject_, completion_mode, "settings_completion_alert",
                           subjects_);

    // E-Stop confirmation (default: false = immediate action)
    bool estop_confirm = config->get<bool>("/safety/estop_require_confirmation", false);
    UI_MANAGED_SUBJECT_INT(estop_require_confirmation_subject_, estop_confirm ? 1 : 0,
                           "settings_estop_confirm", subjects_);

    // Cancel escalation (default: false = never escalate to e-stop)
    bool cancel_escalation = config->get<bool>("/safety/cancel_escalation_enabled", false);
    UI_MANAGED_SUBJECT_INT(cancel_escalation_enabled_subject_, cancel_escalation ? 1 : 0,
                           "settings_cancel_escalation_enabled", subjects_);

    // Cancel escalation timeout (default: 30s, stored as dropdown index 0-3 → 15/30/60/120s)
    int cancel_escalation_timeout =
        config->get<int>("/safety/cancel_escalation_timeout_seconds", 30);
    // Convert seconds to dropdown index: 15→0, 30→1, 60→2, 120→3
    int timeout_index = 1; // default 30s
    if (cancel_escalation_timeout <= 15)
        timeout_index = 0;
    else if (cancel_escalation_timeout <= 30)
        timeout_index = 1;
    else if (cancel_escalation_timeout <= 60)
        timeout_index = 2;
    else
        timeout_index = 3;
    UI_MANAGED_SUBJECT_INT(cancel_escalation_timeout_subject_, timeout_index,
                           "settings_cancel_escalation_timeout", subjects_);

    // Scroll throw (default: 25, range 5-50)
    int scroll_throw = config->get<int>("/input/scroll_throw", 25);
    scroll_throw = std::max(5, std::min(50, scroll_throw));
    UI_MANAGED_SUBJECT_INT(scroll_throw_subject_, scroll_throw, "settings_scroll_throw", subjects_);

    // Scroll limit (default: 10, range 1-20)
    int scroll_limit = config->get<int>("/input/scroll_limit", 10);
    scroll_limit = std::max(1, std::min(20, scroll_limit));
    UI_MANAGED_SUBJECT_INT(scroll_limit_subject_, scroll_limit, "settings_scroll_limit", subjects_);

    // Sleep while printing (default: true = allow sleep during prints)
    bool sleep_while_printing = config->get<bool>("/display/sleep_while_printing", true);
    UI_MANAGED_SUBJECT_INT(sleep_while_printing_subject_, sleep_while_printing ? 1 : 0,
                           "settings_sleep_while_printing", subjects_);

    // Animations enabled (default: true)
    bool animations = config->get<bool>("/display/animations_enabled", true);
    UI_MANAGED_SUBJECT_INT(animations_enabled_subject_, animations ? 1 : 0,
                           "settings_animations_enabled", subjects_);

    // G-code 3D preview enabled (default: true)
    bool gcode_3d = config->get<bool>("/display/gcode_3d_enabled", true);
    UI_MANAGED_SUBJECT_INT(gcode_3d_enabled_subject_, gcode_3d ? 1 : 0, "settings_gcode_3d_enabled",
                           subjects_);

    // Bed mesh render mode (default: 0 = Auto)
    int bed_mesh_mode = config->get<int>("/display/bed_mesh_render_mode", 0);
    bed_mesh_mode = std::clamp(bed_mesh_mode, 0, 2);
    UI_MANAGED_SUBJECT_INT(bed_mesh_render_mode_subject_, bed_mesh_mode,
                           "settings_bed_mesh_render_mode", subjects_);

    // G-code render mode (default: 0 = Auto)
    int gcode_mode = config->get<int>("/display/gcode_render_mode", 0);
    gcode_mode = std::clamp(gcode_mode, 0, 2);
    UI_MANAGED_SUBJECT_INT(gcode_render_mode_subject_, gcode_mode, "settings_gcode_render_mode",
                           subjects_);

    // Time format (default: 0 = 12-hour)
    int time_format = config->get<int>("/display/time_format", 0);
    time_format = std::clamp(time_format, 0, 1);
    UI_MANAGED_SUBJECT_INT(time_format_subject_, time_format, "settings_time_format", subjects_);

    // Language (default: "en" = English, index 0)
    std::string lang_code = config->get_language();
    int lang_index = language_code_to_index(lang_code);
    UI_MANAGED_SUBJECT_INT(language_subject_, lang_index, "settings_language", subjects_);
    spdlog::debug("[SettingsManager] Language initialized to {} (index {})", lang_code, lang_index);

    // Z movement style (default: 0 = Auto)
    int z_movement_style = config->get<int>("/printer/z_movement_style", 0);
    z_movement_style = std::clamp(z_movement_style, 0, 2);
    UI_MANAGED_SUBJECT_INT(z_movement_style_subject_, z_movement_style, "settings_z_movement_style",
                           subjects_);

    // Apply Z movement override to printer state (ensures non-Auto setting takes
    // effect even if set_kinematics() hasn't run yet, e.g. on reconnect)
    if (z_movement_style != 0) {
        get_printer_state().apply_effective_bed_moves();
    }

    // Update channel (default: 0 = Stable)
    int update_channel = config->get<int>("/update/channel", 0);
    update_channel = std::clamp(update_channel, 0, 2);
    UI_MANAGED_SUBJECT_INT(update_channel_subject_, update_channel, "update_channel", subjects_);

    // Telemetry (opt-in, default OFF)
    bool telemetry_enabled = config->get<bool>("/telemetry_enabled", false);
    UI_MANAGED_SUBJECT_INT(telemetry_enabled_subject_, telemetry_enabled ? 1 : 0,
                           "settings_telemetry_enabled", subjects_);
    spdlog::debug("[SettingsManager] telemetry_enabled: {}", telemetry_enabled);

    subjects_initialized_ = true;

    // Self-register cleanup — ensures deinit runs before lv_deinit()
    StaticSubjectRegistry::instance().register_deinit("SettingsManager",
                                                      [this]() { deinit_subjects(); });

    spdlog::debug("[SettingsManager] Subjects initialized: dark_mode={}, theme={}, "
                  "dim={}s, sleep={}s, sounds={}, ui_sounds={}, "
                  "completion_alert_mode={}, scroll_throw={}, scroll_limit={}, animations={}",
                  dark_mode, get_theme_name(), dim_sec, sleep_sec, sounds, ui_sounds,
                  completion_mode, scroll_throw, scroll_limit, animations);
}

void SettingsManager::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::trace("[SettingsManager] Deinitializing subjects");

    // Use SubjectManager for RAII cleanup of all registered subjects
    subjects_.deinit_all();

    subjects_initialized_ = false;
    spdlog::trace("[SettingsManager] Subjects deinitialized");
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

    // Guard: Check if requested mode is supported
    if (enabled && !theme_manager_supports_dark_mode()) {
        spdlog::warn("[SettingsManager] Cannot enable dark mode - theme doesn't support it");
        return;
    }
    if (!enabled && !theme_manager_supports_light_mode()) {
        spdlog::warn("[SettingsManager] Cannot enable light mode - theme doesn't support it");
        return;
    }

    // 1. Update subject (UI reacts immediately via binding)
    lv_subject_set_int(&dark_mode_subject_, enabled ? 1 : 0);

    // 2. Persist to config (theme change requires restart to take effect)
    Config* config = Config::get_instance();
    config->set<bool>("/dark_mode", enabled);
    config->save();

    spdlog::debug("[SettingsManager] Dark mode {} saved (restart required)",
                  enabled ? "enabled" : "disabled");
}

bool SettingsManager::is_dark_mode_available() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&dark_mode_available_subject_)) != 0;
}

void SettingsManager::on_theme_changed() {
    // Check what modes the current theme supports
    bool supports_dark = theme_manager_supports_dark_mode();
    bool supports_light = theme_manager_supports_light_mode();

    if (supports_dark && supports_light) {
        // Dual-mode theme - enable toggle
        lv_subject_set_int(&dark_mode_available_subject_, 1);
        spdlog::trace("[SettingsManager] Theme supports both modes, toggle enabled");
    } else if (supports_dark) {
        // Dark-only theme - disable toggle, force dark mode
        lv_subject_set_int(&dark_mode_available_subject_, 0);
        if (!get_dark_mode()) {
            spdlog::info("[SettingsManager] Theme is dark-only, switching to dark mode");
            // Update subject without persisting (theme controls this)
            lv_subject_set_int(&dark_mode_subject_, 1);
        }
        spdlog::debug("[SettingsManager] Theme is dark-only, toggle disabled");
    } else if (supports_light) {
        // Light-only theme - disable toggle, force light mode
        lv_subject_set_int(&dark_mode_available_subject_, 0);
        if (get_dark_mode()) {
            spdlog::info("[SettingsManager] Theme is light-only, switching to light mode");
            // Update subject without persisting (theme controls this)
            lv_subject_set_int(&dark_mode_subject_, 0);
        }
        spdlog::debug("[SettingsManager] Theme is light-only, toggle disabled");
    } else {
        // Invalid theme (no palettes) - shouldn't happen, but handle gracefully
        spdlog::warn("[SettingsManager] Theme has no valid palettes");
        lv_subject_set_int(&dark_mode_available_subject_, 0);
    }
}

std::string SettingsManager::get_theme_name() const {
    // Use the actual active theme (which respects HELIX_THEME env override)
    const auto& active = theme_manager_get_active_theme();
    if (!active.filename.empty()) {
        // Return the filename to match dropdown option matching
        return active.filename;
    }
    // Fallback to config if theme_manager not initialized yet
    Config* config = Config::get_instance();
    return config ? config->get<std::string>("/display/theme", helix::DEFAULT_THEME)
                  : helix::DEFAULT_THEME;
}

void SettingsManager::set_theme_name(const std::string& name) {
    spdlog::info("[SettingsManager] set_theme_name({})", name);

    Config* config = Config::get_instance();
    config->set<std::string>("/display/theme", name);
    config->save();
}

std::string SettingsManager::get_theme_options() const {
    auto themes = helix::discover_themes(helix::get_themes_directory());

    std::string options;
    for (size_t i = 0; i < themes.size(); ++i) {
        if (i > 0)
            options += "\n";
        options += themes[i].display_name;
    }
    return options;
}

int SettingsManager::get_theme_index() const {
    std::string current = get_theme_name();
    auto themes = helix::discover_themes(helix::get_themes_directory());

    for (size_t i = 0; i < themes.size(); ++i) {
        if (themes[i].filename == current) {
            return static_cast<int>(i);
        }
    }
    return 0; // Default to first theme
}

void SettingsManager::set_theme_by_index(int index) {
    auto themes = helix::discover_themes(helix::get_themes_directory());

    if (index >= 0 && index < static_cast<int>(themes.size())) {
        set_theme_name(themes[index].filename);

        // Update subject so UI reflects the change
        lv_subject_set_int(&theme_preset_subject_, index);
    }
}

int SettingsManager::get_display_sleep_sec() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&display_sleep_subject_));
}

void SettingsManager::set_display_sleep_sec(int seconds) {
    spdlog::info("[SettingsManager] set_display_sleep_sec({})", seconds);

    // Ensure sleep timeout >= dim timeout (unless sleep is disabled with 0)
    // It's nonsensical to sleep before dimming
    if (seconds > 0) {
        int dim_sec = get_display_dim_sec();
        if (dim_sec > 0 && seconds < dim_sec) {
            spdlog::info("[SettingsManager] Clamping sleep {}s to dim {}s", seconds, dim_sec);
            seconds = dim_sec;
            ToastManager::instance().show(ToastSeverity::INFO,
                                          "Sleep adjusted to match dim timeout", 2000);
        }
    }

    // 1. Update subject
    lv_subject_set_int(&display_sleep_subject_, seconds);

    // 2. Persist
    Config* config = Config::get_instance();
    config->set<int>("/display/sleep_sec", seconds);
    config->save();

    // Note: Actual display sleep is handled by the display driver reading this value
    spdlog::debug("[SettingsManager] Display sleep set to {}s", seconds);
}

int SettingsManager::get_display_dim_sec() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&display_dim_subject_));
}

void SettingsManager::set_display_dim_sec(int seconds) {
    spdlog::info("[SettingsManager] set_display_dim_sec({})", seconds);

    // 1. Update subject
    lv_subject_set_int(&display_dim_subject_, seconds);

    // 2. Persist
    Config* config = Config::get_instance();
    config->set<int>("/display/dim_sec", seconds);
    config->save();

    // 3. Notify DisplayManager to reload dim setting
    DisplayManager* dm = DisplayManager::instance();
    if (dm) {
        dm->set_dim_timeout(seconds);
    }

    // 4. If dim is now > sleep, bump sleep up to match (unless sleep is disabled)
    int sleep_sec = get_display_sleep_sec();
    if (seconds > 0 && sleep_sec > 0 && sleep_sec < seconds) {
        spdlog::info("[SettingsManager] Bumping sleep {}s up to match dim {}s", sleep_sec, seconds);
        // Update directly to avoid recursion through set_display_sleep_sec
        lv_subject_set_int(&display_sleep_subject_, seconds);
        Config* cfg = Config::get_instance();
        cfg->set<int>("/display/sleep_sec", seconds);
        cfg->save();
        ToastManager::instance().show(ToastSeverity::INFO, lv_tr("Sleep timeout adjusted"), 2000);
    }

    spdlog::debug("[SettingsManager] Display dim set to {}s", seconds);
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

    // 2. Apply to hardware via DisplayManager
    if (auto* dm = DisplayManager::instance()) {
        dm->set_backlight_brightness(clamped);
    }

    // 3. Persist to config
    Config* config = Config::get_instance();
    config->set<int>("/brightness", clamped);
    config->save();
}

bool SettingsManager::has_backlight_control() const {
    if (auto* dm = DisplayManager::instance()) {
        return dm->has_backlight_control();
    }
    return false;
}

bool SettingsManager::get_sleep_while_printing() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&sleep_while_printing_subject_)) != 0;
}

void SettingsManager::set_sleep_while_printing(bool enabled) {
    spdlog::info("[SettingsManager] set_sleep_while_printing({})", enabled);

    // 1. Update subject (UI reacts)
    lv_subject_set_int(&sleep_while_printing_subject_, enabled ? 1 : 0);

    // 2. Persist to config
    Config* config = Config::get_instance();
    config->set<bool>("/display/sleep_while_printing", enabled);
    config->save();
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
    config->set<bool>("/display/animations_enabled", enabled);
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

bool SettingsManager::get_bed_mesh_show_zero_plane() const {
    Config* config = Config::get_instance();
    return config->get<bool>("/display/bed_mesh_show_zero_plane", true);
}

std::string SettingsManager::get_printer_image() const {
    Config* config = Config::get_instance();
    if (!config)
        return "";
    return config->get<std::string>("/display/printer_image", "");
}

void SettingsManager::set_printer_image(const std::string& id) {
    Config* config = Config::get_instance();
    if (!config)
        return;
    config->set<std::string>("/display/printer_image", id);
    config->save();
    spdlog::info("[SettingsManager] Printer image set to: '{}'", id.empty() ? "(auto-detect)" : id);
}

int SettingsManager::get_gcode_render_mode() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&gcode_render_mode_subject_));
}

void SettingsManager::set_gcode_render_mode(int mode) {
    // Clamp to valid range (0=Auto, 1=3D, 2=2D)
    int clamped = std::clamp(mode, 0, 2);
    spdlog::info("[SettingsManager] set_gcode_render_mode({})", clamped);

    // 1. Update subject (UI reacts)
    lv_subject_set_int(&gcode_render_mode_subject_, clamped);

    // 2. Persist to config
    Config* config = Config::get_instance();
    config->set<int>("/display/gcode_render_mode", clamped);
    config->save();

    spdlog::debug("[SettingsManager] G-code render mode set to {} ({})", clamped,
                  clamped == 0 ? "Auto" : (clamped == 1 ? "3D" : "2D"));
}

const char* SettingsManager::get_gcode_render_mode_options() {
    return GCODE_RENDER_MODE_OPTIONS_TEXT;
}

TimeFormat SettingsManager::get_time_format() const {
    int val = lv_subject_get_int(const_cast<lv_subject_t*>(&time_format_subject_));
    return static_cast<TimeFormat>(std::clamp(val, 0, 1));
}

void SettingsManager::set_time_format(TimeFormat format) {
    int val = static_cast<int>(format);
    spdlog::info("[SettingsManager] set_time_format({})", val == 0 ? "12H" : "24H");

    // 1. Update subject (UI reacts)
    lv_subject_set_int(&time_format_subject_, val);

    // 2. Persist to config
    Config* config = Config::get_instance();
    config->set<int>("/display/time_format", val);
    config->save();
}

const char* SettingsManager::get_time_format_options() {
    return TIME_FORMAT_OPTIONS_TEXT;
}

// =============================================================================
// LANGUAGE SETTINGS
// =============================================================================

std::string SettingsManager::get_language() const {
    int index = lv_subject_get_int(const_cast<lv_subject_t*>(&language_subject_));
    return language_index_to_code(index);
}

void SettingsManager::set_language(const std::string& lang) {
    int index = language_code_to_index(lang);
    spdlog::info("[SettingsManager] set_language({}) -> index {}", lang, index);

    // 1. Update subject (UI reacts)
    lv_subject_set_int(&language_subject_, index);

    // 2. Call LVGL translation API for hot-reload
    // This sends LV_EVENT_TRANSLATION_LANGUAGE_CHANGED to all widgets
    lv_translation_set_language(lang.c_str());

    // 3. Sync lv_i18n system (for plural forms and runtime lookups)
    int i18n_result = lv_i18n_set_locale(lang.c_str());
    if (i18n_result != 0) {
        spdlog::warn("[SettingsManager] Failed to set lv_i18n locale to '{}'", lang);
    }

    // 4. Persist to config
    Config* config = Config::get_instance();
    config->set_language(lang);
    config->save();
}

void SettingsManager::set_language_by_index(int index) {
    std::string code = language_index_to_code(index);
    set_language(code);
}

int SettingsManager::get_language_index() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&language_subject_));
}

const char* SettingsManager::get_language_options() {
    return LANGUAGE_OPTIONS_TEXT;
}

std::string SettingsManager::language_index_to_code(int index) {
    if (index < 0 || index >= LANGUAGE_COUNT) {
        return "en"; // Default to English
    }
    return LANGUAGE_CODES[index];
}

int SettingsManager::language_code_to_index(const std::string& code) {
    for (int i = 0; i < LANGUAGE_COUNT; ++i) {
        if (code == LANGUAGE_CODES[i]) {
            return i;
        }
    }
    return 0; // Default to English (index 0)
}

// =============================================================================
// PRINTER SETTINGS
// =============================================================================

bool SettingsManager::get_led_enabled() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&led_enabled_subject_)) != 0;
}

void SettingsManager::set_led_enabled(bool enabled) {
    spdlog::info("[SettingsManager] set_led_enabled({})", enabled);

    // 1. Delegate to LedController for actual hardware control
    helix::led::LedController::instance().toggle_all(enabled);

    // 2. Update subject (UI reacts)
    lv_subject_set_int(&led_enabled_subject_, enabled ? 1 : 0);

    // 3. Persist startup preference via LedController
    helix::led::LedController::instance().set_led_on_at_start(enabled);
    helix::led::LedController::instance().save_config();
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
}

bool SettingsManager::get_ui_sounds_enabled() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&ui_sounds_enabled_subject_)) != 0;
}

void SettingsManager::set_ui_sounds_enabled(bool enabled) {
    spdlog::info("[SettingsManager] set_ui_sounds_enabled({})", enabled);

    lv_subject_set_int(&ui_sounds_enabled_subject_, enabled ? 1 : 0);

    Config* config = Config::get_instance();
    config->set<bool>("/ui_sounds_enabled", enabled);
    config->save();
}

int SettingsManager::get_volume() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&volume_subject_));
}

void SettingsManager::set_volume(int volume) {
    volume = std::clamp(volume, 0, 100);
    spdlog::info("[SettingsManager] set_volume({})", volume);

    lv_subject_set_int(&volume_subject_, volume);

    Config* config = Config::get_instance();
    if (config) {
        config->set<int>("/sounds/volume", volume);
        config->save();
    }
}

std::string SettingsManager::get_sound_theme() const {
    Config* config = Config::get_instance();
    return config->get<std::string>("/sound_theme", "default");
}

void SettingsManager::set_sound_theme(const std::string& name) {
    spdlog::info("[SettingsManager] set_sound_theme('{}')", name);

    Config* config = Config::get_instance();
    config->set<std::string>("/sound_theme", name);
    config->save();
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
// DISPLAY DIM OPTIONS
// =============================================================================

const char* SettingsManager::get_display_dim_options() {
    return DIM_OPTIONS_TEXT;
}

int SettingsManager::dim_seconds_to_index(int seconds) {
    for (int i = 0; i < DIM_OPTIONS_COUNT; i++) {
        if (DIM_OPTIONS[i] == seconds) {
            return i;
        }
    }
    // Default to "5 minutes" if not found
    return 4;
}

int SettingsManager::index_to_dim_seconds(int index) {
    if (index >= 0 && index < DIM_OPTIONS_COUNT) {
        return DIM_OPTIONS[index];
    }
    return 300; // Default 5 minutes
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

bool SettingsManager::get_cancel_escalation_enabled() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&cancel_escalation_enabled_subject_)) != 0;
}

void SettingsManager::set_cancel_escalation_enabled(bool enabled) {
    spdlog::info("[SettingsManager] set_cancel_escalation_enabled({})", enabled);

    // 1. Update subject (UI reacts)
    lv_subject_set_int(&cancel_escalation_enabled_subject_, enabled ? 1 : 0);

    // 2. Persist
    Config* config = Config::get_instance();
    config->set<bool>("/safety/cancel_escalation_enabled", enabled);
    config->save();

    spdlog::debug("[SettingsManager] Cancel escalation {} and saved",
                  enabled ? "enabled" : "disabled");
}

static constexpr int ESCALATION_TIMEOUT_VALUES[] = {15, 30, 60, 120};

int SettingsManager::get_cancel_escalation_timeout_seconds() const {
    int index = lv_subject_get_int(const_cast<lv_subject_t*>(&cancel_escalation_timeout_subject_));
    index = std::max(0, std::min(3, index));
    return ESCALATION_TIMEOUT_VALUES[index];
}

void SettingsManager::set_cancel_escalation_timeout_seconds(int seconds) {
    spdlog::info("[SettingsManager] set_cancel_escalation_timeout_seconds({})", seconds);

    // Convert seconds to dropdown index
    int index = 1; // default 30s
    if (seconds <= 15)
        index = 0;
    else if (seconds <= 30)
        index = 1;
    else if (seconds <= 60)
        index = 2;
    else
        index = 3;

    // 1. Update subject (UI reacts via dropdown binding)
    lv_subject_set_int(&cancel_escalation_timeout_subject_, index);

    // 2. Persist actual seconds value
    Config* config = Config::get_instance();
    config->set<int>("/safety/cancel_escalation_timeout_seconds", ESCALATION_TIMEOUT_VALUES[index]);
    config->save();

    spdlog::debug("[SettingsManager] Cancel escalation timeout set to {}s (index {}) and saved",
                  ESCALATION_TIMEOUT_VALUES[index], index);
}

// =============================================================================
// UPDATE SETTINGS
// =============================================================================

int SettingsManager::get_update_channel() const {
    return lv_subject_get_int(&const_cast<SettingsManager*>(this)->update_channel_subject_);
}

void SettingsManager::set_update_channel(int channel) {
    int clamped = std::clamp(channel, 0, 2);
    spdlog::info("[SettingsManager] set_update_channel({})",
                 clamped == 0 ? "Stable" : (clamped == 1 ? "Beta" : "Dev"));

    // 1. Update subject (UI reacts)
    lv_subject_set_int(&update_channel_subject_, clamped);

    // 2. Persist to config
    Config* config = Config::get_instance();
    config->set<int>("/update/channel", clamped);
    config->save();

    // 3. Clear update checker cache (force re-check on new channel)
    UpdateChecker::instance().clear_cache();
}

const char* SettingsManager::get_update_channel_options() {
    return "Stable\nBeta\nDev";
}

// =============================================================================
// TELEMETRY SETTINGS
// =============================================================================

bool SettingsManager::get_telemetry_enabled() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&telemetry_enabled_subject_)) != 0;
}

void SettingsManager::set_telemetry_enabled(bool enabled) {
    spdlog::info("[SettingsManager] set_telemetry_enabled({})", enabled);

    // Update subject (UI reacts)
    lv_subject_set_int(&telemetry_enabled_subject_, enabled ? 1 : 0);

    // Persist to config
    Config* config = Config::get_instance();
    config->set<bool>("/telemetry_enabled", enabled);
    config->save();

    // Apply to TelemetryManager
    TelemetryManager::instance().set_enabled(enabled);
}

// =============================================================================
// Z MOVEMENT STYLE
// =============================================================================

ZMovementStyle SettingsManager::get_z_movement_style() const {
    int val = lv_subject_get_int(const_cast<lv_subject_t*>(&z_movement_style_subject_));
    return static_cast<ZMovementStyle>(std::clamp(val, 0, 2));
}

void SettingsManager::set_z_movement_style(ZMovementStyle style) {
    int val = static_cast<int>(style);
    val = std::clamp(val, 0, 2);
    spdlog::info("[SettingsManager] set_z_movement_style({})",
                 val == 0 ? "Auto" : (val == 1 ? "Bed Moves" : "Nozzle Moves"));

    // 1. Update subject (UI reacts)
    lv_subject_set_int(&z_movement_style_subject_, val);

    // 2. Persist to config
    Config* config = Config::get_instance();
    config->set<int>("/printer/z_movement_style", val);
    config->save();

    // 3. Apply override to printer state
    get_printer_state().apply_effective_bed_moves();
}

const char* SettingsManager::get_z_movement_style_options() {
    return Z_MOVEMENT_STYLE_OPTIONS_TEXT;
}
