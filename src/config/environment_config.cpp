// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "environment_config.h"

#include <cstdlib>
#include <cstring>

namespace helix::config {

std::optional<int> EnvironmentConfig::get_int(const char* name, int min, int max) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }

    char* endptr = nullptr;
    long parsed = strtol(value, &endptr, 10);

    // Check for conversion errors:
    // - endptr == value means no digits found
    // - *endptr != '\0' means trailing non-numeric characters
    if (endptr == value || *endptr != '\0') {
        return std::nullopt;
    }

    // Range validation
    if (parsed < min || parsed > max) {
        return std::nullopt;
    }

    return static_cast<int>(parsed);
}

std::optional<int> EnvironmentConfig::get_int_scaled(const char* name, int min, int max,
                                                     int divisor) {
    if (divisor <= 0) {
        return std::nullopt;
    }

    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }

    char* endptr = nullptr;
    long parsed = strtol(value, &endptr, 10);

    if (endptr == value || *endptr != '\0') {
        return std::nullopt;
    }

    // Ceiling division: (parsed + divisor - 1) / divisor
    long scaled = (parsed + divisor - 1) / divisor;

    // Range validation on scaled result
    if (scaled < min || scaled > max) {
        return std::nullopt;
    }

    return static_cast<int>(scaled);
}

bool EnvironmentConfig::get_bool(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && strcmp(value, "1") == 0;
}

bool EnvironmentConfig::exists(const char* name) {
    return std::getenv(name) != nullptr;
}

std::optional<std::string> EnvironmentConfig::get_string(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
}

// ============================================================================
// Application-specific helpers
// ============================================================================

std::optional<int> EnvironmentConfig::get_auto_quit_seconds() {
    // HELIX_AUTO_QUIT_MS: 100ms - 3600000ms (1 hour)
    // Validate raw ms range, then convert to seconds with ceiling
    auto ms = get_int("HELIX_AUTO_QUIT_MS", 100, 3600000);
    if (!ms) {
        return std::nullopt;
    }
    // Convert to seconds with ceiling: (ms + 999) / 1000
    return (*ms + 999) / 1000;
}

bool EnvironmentConfig::get_screenshot_enabled() {
    return get_bool("HELIX_AUTO_SCREENSHOT");
}

std::optional<int> EnvironmentConfig::get_mock_ams_gates() {
    return get_int("HELIX_AMS_GATES", 1, 16);
}

bool EnvironmentConfig::get_benchmark_mode() {
    return exists("HELIX_BENCHMARK");
}

std::optional<std::string> EnvironmentConfig::get_data_dir() {
    return get_string("HELIX_DATA_DIR");
}

} // namespace helix::config
