// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class MoonrakerClient;

/**
 * @brief Audio feedback manager for printer sounds
 *
 * Handles M300 G-code playback for UI feedback sounds.
 * Respects SettingsManager::get_sounds_enabled() setting.
 *
 * ## Supported Sounds:
 * - Test beep: Short confirmation tone when enabling sounds
 * - Print complete: Multi-tone melody (future)
 * - Error alert: Attention-grabbing tone (future)
 *
 * ## Usage:
 * @code
 * auto& sound = SoundManager::instance();
 * sound.set_moonraker_client(client);
 *
 * if (sound.is_available()) {
 *     sound.play_test_beep();
 * }
 * @endcode
 */
class SoundManager {
  public:
    /**
     * @brief Get singleton instance
     * @return Reference to the global SoundManager instance
     */
    static SoundManager& instance();

    // Prevent copying
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;

    /**
     * @brief Set Moonraker client for G-code execution
     * @param client Pointer to MoonrakerClient (can be nullptr)
     */
    void set_moonraker_client(MoonrakerClient* client);

    /**
     * @brief Check if sound playback is available
     *
     * Returns true if:
     * - MoonrakerClient is connected
     * - Sound is enabled in SettingsManager
     * - (In test mode, always returns true for UI testing)
     *
     * @return true if sounds can be played
     */
    [[nodiscard]] bool is_available() const;

    /**
     * @brief Play a short test beep
     *
     * Used when enabling sounds in settings to confirm hardware works.
     * Plays a 1000Hz tone for 100ms.
     */
    void play_test_beep();

    /**
     * @brief Play print complete melody
     *
     * Plays a short celebratory tune when a print finishes.
     * Only plays if sounds are enabled and printer has speaker.
     */
    void play_print_complete();

    /**
     * @brief Play error alert tone
     *
     * Plays attention-grabbing beeps for errors.
     */
    void play_error_alert();

  private:
    SoundManager() = default;
    ~SoundManager() = default;

    /**
     * @brief Send M300 G-code command
     * @param frequency Frequency in Hz (100-10000 typical)
     * @param duration Duration in milliseconds
     * @return true if command was sent
     */
    bool send_m300(int frequency, int duration);

    MoonrakerClient* client_ = nullptr;
};
