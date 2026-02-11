// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "format_utils.h"

#include <algorithm>
#include <cstdio>

namespace helix::fmt {

// =============================================================================
// Percentage Formatting
// =============================================================================

char* format_percent(int percent, char* buf, size_t size) {
    std::snprintf(buf, size, "%d%%", percent);
    return buf;
}

char* format_percent_or_unavailable(int percent, bool available, char* buf, size_t size) {
    if (available) {
        return format_percent(percent, buf, size);
    }
    std::snprintf(buf, size, "%s", UNAVAILABLE);
    return buf;
}

char* format_percent_float(double percent, int decimals, char* buf, size_t size) {
    std::snprintf(buf, size, "%.*f%%", decimals, percent);
    return buf;
}

char* format_humidity(int humidity_x10, char* buf, size_t size) {
    // Divide by 10 to get whole percent (truncate, don't round)
    return format_percent(humidity_x10 / 10, buf, size);
}

// =============================================================================
// Distance/Length Formatting
// =============================================================================

char* format_distance_mm(double mm, int precision, char* buf, size_t size) {
    std::snprintf(buf, size, "%.*f mm", precision, mm);
    return buf;
}

char* format_diameter_mm(float mm, char* buf, size_t size) {
    std::snprintf(buf, size, "%.2f mm", static_cast<double>(mm));
    return buf;
}

// =============================================================================
// Speed Formatting
// =============================================================================

char* format_speed_mm_s(double speed, char* buf, size_t size) {
    std::snprintf(buf, size, "%.0f mm/s", speed);
    return buf;
}

char* format_speed_mm_min(double speed, char* buf, size_t size) {
    std::snprintf(buf, size, "%.0f mm/min", speed);
    return buf;
}

// =============================================================================
// Acceleration Formatting
// =============================================================================

char* format_accel_mm_s2(double accel, char* buf, size_t size) {
    std::snprintf(buf, size, "%.0f mm/s²", accel);
    return buf;
}

// =============================================================================
// Frequency Formatting
// =============================================================================

char* format_frequency_hz(double hz, char* buf, size_t size) {
    std::snprintf(buf, size, "%.1f Hz", hz);
    return buf;
}

// =============================================================================
// Temperature Formatting
// =============================================================================

char* format_temp(int temp_c, char* buf, size_t size) {
    std::snprintf(buf, size, "%d°C", temp_c);
    return buf;
}

char* format_temp_pair(int current_c, int target_c, char* buf, size_t size) {
    if (target_c == 0) {
        std::snprintf(buf, size, "%d / %s°C", current_c, UNAVAILABLE);
    } else {
        std::snprintf(buf, size, "%d / %d°C", current_c, target_c);
    }
    return buf;
}

char* format_temp_range(int min_c, int max_c, char* buf, size_t size) {
    std::snprintf(buf, size, "%d-%d°C", min_c, max_c);
    return buf;
}

// =============================================================================
// Duration Formatting
// =============================================================================

std::string duration(int total_seconds) {
    // Handle negative or zero
    if (total_seconds <= 0) {
        return "0s";
    }

    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int seconds = total_seconds % 60;

    char buf[32];

    if (hours == 0 && minutes == 0) {
        // Under 1 minute: show seconds
        std::snprintf(buf, sizeof(buf), "%ds", seconds);
    } else if (hours == 0) {
        // Under 1 hour: show minutes only
        std::snprintf(buf, sizeof(buf), "%dm", minutes);
    } else if (minutes == 0) {
        // Exact hours
        std::snprintf(buf, sizeof(buf), "%dh", hours);
    } else {
        // Hours and minutes
        std::snprintf(buf, sizeof(buf), "%dh %dm", hours, minutes);
    }

    return std::string(buf);
}

std::string duration_remaining(int total_seconds) {
    // Handle negative or zero
    if (total_seconds <= 0) {
        return "0 min left";
    }

    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int seconds = total_seconds % 60;

    char buf[32];

    if (hours > 0) {
        // Show as H:MM format for longer durations
        std::snprintf(buf, sizeof(buf), "%d:%02d left", hours, minutes);
    } else if (total_seconds < 300) {
        // Under 5 minutes: show minutes:seconds for precision
        if (minutes > 0) {
            std::snprintf(buf, sizeof(buf), "%d:%02d left", minutes, seconds);
        } else {
            std::snprintf(buf, sizeof(buf), "0:%02d left", seconds);
        }
    } else {
        // Show as "X min left" for shorter durations
        std::snprintf(buf, sizeof(buf), "%d min left", minutes > 0 ? minutes : 1);
    }

    return std::string(buf);
}

std::string duration_from_minutes(int total_minutes) {
    // Handle negative or zero
    if (total_minutes <= 0) {
        return "0 min";
    }

    int hours = total_minutes / 60;
    int minutes = total_minutes % 60;

    char buf[32];

    if (hours == 0) {
        // Under 1 hour: show minutes
        std::snprintf(buf, sizeof(buf), "%d min", minutes);
    } else if (minutes == 0) {
        // Exact hours
        std::snprintf(buf, sizeof(buf), "%dh", hours);
    } else {
        // Hours and minutes
        std::snprintf(buf, sizeof(buf), "%dh %dm", hours, minutes);
    }

    return std::string(buf);
}

size_t duration_to_buffer(char* buf, size_t buf_size, int total_seconds) {
    if (buf == nullptr || buf_size == 0) {
        return 0;
    }

    // Handle negative or zero
    if (total_seconds <= 0) {
        return static_cast<size_t>(std::snprintf(buf, buf_size, "0s"));
    }

    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int seconds = total_seconds % 60;

    int written = 0;

    if (hours == 0 && minutes == 0) {
        // Under 1 minute: show seconds
        written = std::snprintf(buf, buf_size, "%ds", seconds);
    } else if (hours == 0) {
        // Under 1 hour: show minutes only
        written = std::snprintf(buf, buf_size, "%dm", minutes);
    } else if (minutes == 0) {
        // Exact hours
        written = std::snprintf(buf, buf_size, "%dh", hours);
    } else {
        // Hours and minutes
        written = std::snprintf(buf, buf_size, "%dh %dm", hours, minutes);
    }

    return written > 0 ? static_cast<size_t>(written) : 0;
}

std::string duration_padded(int total_seconds) {
    // Handle negative or zero
    if (total_seconds <= 0) {
        return "0s";
    }

    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int seconds = total_seconds % 60;

    char buf[32];

    if (hours == 0 && total_seconds < 300) {
        // Under 5 minutes: show minutes and seconds for precision
        if (minutes > 0) {
            std::snprintf(buf, sizeof(buf), "%dm %02ds", minutes, seconds);
        } else {
            std::snprintf(buf, sizeof(buf), "%ds", seconds);
        }
    } else if (hours == 0) {
        // Under 1 hour: show minutes only
        std::snprintf(buf, sizeof(buf), "%dm", minutes);
    } else {
        // Hours and zero-padded minutes
        std::snprintf(buf, sizeof(buf), "%dh %02dm", hours, minutes);
    }

    return std::string(buf);
}

std::string format_filament_length(double mm) {
    char buf[32];
    if (mm < 1000) {
        std::snprintf(buf, sizeof(buf), "%.0fmm", mm);
    } else if (mm < 1000000) {
        std::snprintf(buf, sizeof(buf), "%.1fm", mm / 1000.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2fkm", mm / 1000000.0);
    }
    return std::string(buf);
}

HeaterDisplayResult heater_display(int current_centi, int target_centi) {
    HeaterDisplayResult result;

    // Convert centi-degrees to degrees (integer division is fine for display)
    int current_deg = current_centi / 100;
    int target_deg = target_centi / 100;

    // Format temperature string
    char buf[32];
    if (target_centi > 0) {
        std::snprintf(buf, sizeof(buf), "%d / %d°C", current_deg, target_deg);
    } else {
        std::snprintf(buf, sizeof(buf), "%d°C", current_deg);
    }
    result.temp = buf;

    // Calculate percentage (clamped to 0-100)
    if (target_centi <= 0) {
        result.pct = 0;
    } else {
        int pct = (current_centi * 100) / target_centi;
        result.pct = std::clamp(pct, 0, 100);
    }

    // Determine status using same tolerance logic as get_heating_state_color()
    constexpr int tolerance_deg = 2;
    if (target_centi <= 0) {
        result.status = "Off";
    } else if (current_deg < target_deg - tolerance_deg) {
        result.status = "Heating...";
    } else if (current_deg > target_deg + tolerance_deg) {
        result.status = "Cooling";
    } else {
        result.status = "Ready";
    }

    return result;
}

} // namespace helix::fmt
