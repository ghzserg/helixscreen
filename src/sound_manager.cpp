// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sound_manager.h"

#include "moonraker_client.h"
#include "runtime_config.h"
#include "settings_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>

SoundManager& SoundManager::instance() {
    static SoundManager instance;
    return instance;
}

void SoundManager::set_moonraker_client(MoonrakerClient* client) {
    client_ = client;
    spdlog::debug("[SoundManager] Moonraker client set: {}", client ? "connected" : "nullptr");
}

bool SoundManager::is_available() const {
    // In test mode, sounds are always "available" for UI testing
    // (the actual M300 won't do anything without a real printer)
    if (get_runtime_config().is_test_mode()) {
        return true;
    }

    // Need Moonraker client and sounds must be enabled
    return client_ != nullptr && SettingsManager::instance().get_sounds_enabled();
}

void SoundManager::play_test_beep() {
    if (!SettingsManager::instance().get_sounds_enabled()) {
        spdlog::debug("[SoundManager] Test beep skipped - sounds disabled");
        return;
    }

    spdlog::info("[SoundManager] Playing test beep");

    // Simple 1000Hz beep for 100ms
    send_m300(1000, 100);
}

void SoundManager::play_print_complete() {
    if (!SettingsManager::instance().get_sounds_enabled()) {
        return;
    }

    spdlog::info("[SoundManager] Playing print complete melody");

    // Simple celebratory tune: C5 - E5 - G5 - C6
    // Each note 150ms with small gaps
    // Note: This sends multiple G-codes quickly; Klipper queues them
    send_m300(523, 150);  // C5
    send_m300(659, 150);  // E5
    send_m300(784, 150);  // G5
    send_m300(1047, 300); // C6 (longer)
}

void SoundManager::play_error_alert() {
    if (!SettingsManager::instance().get_sounds_enabled()) {
        return;
    }

    spdlog::info("[SoundManager] Playing error alert");

    // Attention-grabbing: two short high beeps
    send_m300(2000, 100);
    send_m300(2000, 100);
}

bool SoundManager::send_m300(int frequency, int duration) {
    if (!client_) {
        spdlog::debug("[SoundManager] Cannot send M300 - no Moonraker client");
        return false;
    }

    // Clamp values to reasonable ranges
    frequency = std::max(100, std::min(10000, frequency));
    duration = std::max(10, std::min(5000, duration));

    // M300 format: S=frequency (Hz), P=duration (ms)
    std::string gcode = "M300 S" + std::to_string(frequency) + " P" + std::to_string(duration);

    int result = client_->gcode_script(gcode);
    if (result == 0) {
        spdlog::debug("[SoundManager] M300 sent: {} Hz, {} ms", frequency, duration);
        return true;
    } else {
        spdlog::warn("[SoundManager] Failed to send M300 command (result={})", result);
        return false;
    }
}
