// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sound_sequencer.h"
#include "sound_theme.h"

#include <memory>
#include <string>
#include <vector>

namespace helix {
class MoonrakerClient;
}

namespace helix {

/**
 * @brief Audio feedback manager using the synth engine
 *
 * Plays named sounds from JSON themes through a backend-agnostic sequencer.
 * Detects the best available backend (M300/Moonraker for now, SDL/PWM later).
 *
 * Respects SettingsManager toggles:
 * - sounds_enabled: master switch for all sounds
 * - ui_sounds_enabled: separate toggle for UI interaction sounds (button taps, nav)
 *
 * ## Usage:
 * @code
 * auto& sound = SoundManager::instance();
 * sound.set_moonraker_client(client);
 * sound.initialize();
 *
 * sound.play("button_tap");
 * sound.play("print_complete", SoundPriority::EVENT);
 * @endcode
 */
class SoundManager {
  public:
    static SoundManager& instance();

    // Prevent copying
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;

    /// Set Moonraker client for M300 backend
    void set_moonraker_client(MoonrakerClient* client);

    /// Auto-detect backend, load theme, start sequencer
    void initialize();

    /// Stop sequencer, cleanup
    void shutdown();

    /// Play a named sound from the current theme (UI priority)
    void play(const std::string& sound_name);

    /// Play a named sound with explicit priority
    void play(const std::string& sound_name, SoundPriority priority);

    /// Backward compatibility: calls play("test_beep")
    void play_test_beep();

    /// Backward compatibility: calls play("print_complete", EVENT)
    void play_print_complete();

    /// Backward compatibility: calls play("error_alert", EVENT)
    void play_error_alert();

    /// Set active theme by name (loads from config/sounds/<name>.json)
    void set_theme(const std::string& theme_name);

    /// Get current theme name
    std::string get_current_theme() const;

    /// Scan config/sounds/ for available .json theme files
    std::vector<std::string> get_available_themes() const;

    /// Check if sound playback is available (backend exists + sounds enabled)
    [[nodiscard]] bool is_available() const;

  private:
    SoundManager() = default;
    ~SoundManager() = default;

    /// Detect best available backend
    std::shared_ptr<SoundBackend> create_backend();

    /// Load theme JSON from config/sounds/
    void load_theme(const std::string& theme_name);

    /// Check if a sound name is a UI sound (affected by ui_sounds_enabled)
    static bool is_ui_sound(const std::string& name);

    MoonrakerClient* client_ = nullptr;
    std::unique_ptr<SoundSequencer> sequencer_;
    std::shared_ptr<SoundBackend> backend_;
    SoundTheme current_theme_;
    std::string theme_name_ = "default";
    bool initialized_ = false;
};

} // namespace helix
