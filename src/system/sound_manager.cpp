// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sound_manager.h"

#include "m300_sound_backend.h"
#include "moonraker_client.h"
#include "pwm_sound_backend.h"
#include "runtime_config.h"
#include "settings_manager.h"
#include "sound_backend.h"
#include "sound_sequencer.h"
#include "sound_theme.h"

#ifdef HELIX_DISPLAY_SDL
#include "sdl_sound_backend.h"
#endif

#include <spdlog/spdlog.h>

#include <algorithm>
#include <dirent.h>
#include <string>

using namespace helix;

// ============================================================================
// SoundManager singleton
// ============================================================================

SoundManager& SoundManager::instance() {
    static SoundManager instance;
    return instance;
}

void SoundManager::set_moonraker_client(MoonrakerClient* client) {
    client_ = client;
    spdlog::debug("[SoundManager] Moonraker client set: {}", client ? "connected" : "nullptr");
}

void SoundManager::initialize() {
    if (initialized_) {
        spdlog::debug("[SoundManager] Already initialized");
        return;
    }

    // Create the best available backend
    backend_ = create_backend();
    if (!backend_) {
        spdlog::info("[SoundManager] No sound backend available, sounds disabled");
        return;
    }

    // Load the configured theme
    theme_name_ = SettingsManager::instance().get_sound_theme();
    load_theme(theme_name_);

    // Create and start the sequencer
    sequencer_ = std::make_unique<SoundSequencer>(backend_);
    sequencer_->start();

    initialized_ = true;
    spdlog::info("[SoundManager] Initialized with theme '{}', backend ready", theme_name_);
}

void SoundManager::shutdown() {
    if (!initialized_)
        return;

    if (sequencer_) {
        sequencer_->shutdown();
        sequencer_.reset();
    }

    backend_.reset();
    initialized_ = false;

    spdlog::info("[SoundManager] Shutdown complete");
}

void SoundManager::play(const std::string& sound_name) {
    play(sound_name, SoundPriority::UI);
}

void SoundManager::play(const std::string& sound_name, SoundPriority priority) {
    // Master switch
    if (!SettingsManager::instance().get_sounds_enabled()) {
        spdlog::trace("[SoundManager] play('{}') skipped - sounds disabled", sound_name);
        return;
    }

    // UI sounds have their own toggle
    if (is_ui_sound(sound_name) && !SettingsManager::instance().get_ui_sounds_enabled()) {
        spdlog::trace("[SoundManager] play('{}') skipped - UI sounds disabled", sound_name);
        return;
    }

    if (!sequencer_ || !backend_) {
        spdlog::debug("[SoundManager] play('{}') skipped - no sequencer/backend", sound_name);
        return;
    }

    // Look up sound in current theme
    auto it = current_theme_.sounds.find(sound_name);
    if (it == current_theme_.sounds.end()) {
        spdlog::debug("[SoundManager] play('{}') - sound not in theme '{}'", sound_name,
                      theme_name_);
        return;
    }

    sequencer_->play(it->second, priority);
    spdlog::debug("[SoundManager] play('{}', priority={})", sound_name, static_cast<int>(priority));
}

void SoundManager::play_test_beep() {
    play("test_beep");
}

void SoundManager::play_print_complete() {
    play("print_complete", SoundPriority::EVENT);
}

void SoundManager::play_error_alert() {
    play("error_alert", SoundPriority::EVENT);
}

void SoundManager::set_theme(const std::string& name) {
    theme_name_ = name;
    load_theme(name);
    spdlog::info("[SoundManager] Theme changed to '{}'", name);
}

std::string SoundManager::get_current_theme() const {
    return theme_name_;
}

std::vector<std::string> SoundManager::get_available_themes() const {
    std::vector<std::string> themes;

    DIR* dir = opendir("config/sounds");
    if (!dir) {
        spdlog::debug("[SoundManager] Could not open config/sounds/");
        return themes;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        // Match *.json files
        if (filename.size() > 5 && filename.substr(filename.size() - 5) == ".json") {
            // Strip .json extension to get theme name
            themes.push_back(filename.substr(0, filename.size() - 5));
        }
    }
    closedir(dir);

    std::sort(themes.begin(), themes.end());
    return themes;
}

bool SoundManager::is_available() const {
    return initialized_ && backend_ != nullptr && SettingsManager::instance().get_sounds_enabled();
}

std::shared_ptr<SoundBackend> SoundManager::create_backend() {
    // Auto-detection order:
    // 1. SDL audio available (desktop build) -> SDLBackend
    // 2. /sys/class/pwm/pwmchip0 exists -> PWMBackend
    // 3. Moonraker connected -> M300Backend
    // 4. None -> sounds disabled

#ifdef HELIX_DISPLAY_SDL
    auto sdl_backend = std::make_shared<SDLSoundBackend>();
    if (sdl_backend->initialize()) {
        spdlog::info("[SoundManager] Using SDL audio backend");
        return sdl_backend;
    }
    spdlog::warn("[SoundManager] SDL audio init failed, falling back");
#endif

    // Try PWM sysfs backend (AD5M buzzer)
    auto pwm_backend = std::make_shared<PWMSoundBackend>();
    if (pwm_backend->initialize()) {
        spdlog::info("[SoundManager] Using PWM sysfs backend ({})", pwm_backend->channel_path());
        return pwm_backend;
    }
    spdlog::debug("[SoundManager] PWM sysfs not available, falling back");

    if (client_) {
        spdlog::debug("[SoundManager] Creating M300 backend via Moonraker");
        auto* client = client_;
        return std::make_shared<M300SoundBackend>(
            [client](const std::string& gcode) { return client->gcode_script(gcode); });
    }

    spdlog::debug("[SoundManager] No backend available");
    return nullptr;
}

void SoundManager::load_theme(const std::string& name) {
    std::string path = "config/sounds/" + name + ".json";
    auto theme = SoundThemeParser::load_from_file(path);

    if (theme) {
        current_theme_ = std::move(*theme);
        spdlog::info("[SoundManager] Loaded theme '{}' ({} sounds)", name,
                     current_theme_.sounds.size());
    } else {
        spdlog::warn("[SoundManager] Failed to load theme '{}', keeping current", name);
        // If no theme is loaded at all, try default as fallback
        if (current_theme_.sounds.empty() && name != "default") {
            spdlog::info("[SoundManager] Attempting fallback to 'default' theme");
            auto fallback = SoundThemeParser::load_from_file("config/sounds/default.json");
            if (fallback) {
                current_theme_ = std::move(*fallback);
                theme_name_ = "default";
            }
        }
    }
}

bool SoundManager::is_ui_sound(const std::string& name) {
    // UI interaction sounds — affected by ui_sounds_enabled toggle
    return name == "button_tap" || name == "toggle_on" || name == "toggle_off" ||
           name == "nav_forward" || name == "nav_back" || name == "dropdown_open";
}
