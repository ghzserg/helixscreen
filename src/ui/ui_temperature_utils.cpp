// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_temperature_utils.h"

#include "ui_theme.h"

#include "spdlog/spdlog.h"

#include <cstdio>

namespace helix {
namespace ui {
namespace temperature {

bool validate_and_clamp(int& temp, int min_temp, int max_temp, const char* context,
                        const char* temp_type) {
    if (temp < min_temp || temp > max_temp) {
        spdlog::warn("[{}] Invalid {} temperature {}°C (valid: {}-{}°C), clamping", context,
                     temp_type, temp, min_temp, max_temp);
        temp = (temp < min_temp) ? min_temp : max_temp;
        return false;
    }
    return true;
}

bool validate_and_clamp_pair(int& current, int& target, int min_temp, int max_temp,
                             const char* context) {
    bool current_valid = validate_and_clamp(current, min_temp, max_temp, context, "current");
    bool target_valid = validate_and_clamp(target, min_temp, max_temp, context, "target");
    return current_valid && target_valid;
}

bool is_extrusion_safe(int current_temp, int min_extrusion_temp) {
    return current_temp >= min_extrusion_temp;
}

const char* get_extrusion_safety_status(int current_temp, int min_extrusion_temp) {
    if (current_temp >= min_extrusion_temp) {
        return "Ready";
    }

    // Calculate how far below minimum we are
    static char status_buf[64];
    int deficit = min_extrusion_temp - current_temp;
    snprintf(status_buf, sizeof(status_buf), "Heating (%d°C below minimum)", deficit);
    return status_buf;
}

// ============================================================================
// Formatting Functions
// ============================================================================

char* format_temperature(int temp, char* buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%d°C", temp);
    return buffer;
}

char* format_temperature_pair(int current, int target, char* buffer, size_t buffer_size) {
    if (target == 0) {
        snprintf(buffer, buffer_size, "%d / --°C", current);
    } else {
        snprintf(buffer, buffer_size, "%d / %d°C", current, target);
    }
    return buffer;
}

char* format_temperature_f(float temp, char* buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%.1f°C", temp);
    return buffer;
}

char* format_temperature_pair_f(float current, float target, char* buffer, size_t buffer_size) {
    if (target == 0.0f) {
        snprintf(buffer, buffer_size, "%.1f / --°C", current);
    } else {
        snprintf(buffer, buffer_size, "%.1f / %.1f°C", current, target);
    }
    return buffer;
}

char* format_temperature_range(int min_temp, int max_temp, char* buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%d-%d°C", min_temp, max_temp);
    return buffer;
}

// ============================================================================
// Display Color Functions
// ============================================================================

lv_color_t get_heating_state_color(int current_deg, int target_deg, int tolerance) {
    if (target_deg == 0) {
        // OFF: Heater is disabled - GRAY
        return ui_theme_get_color("text_secondary");
    } else if (current_deg < target_deg - tolerance) {
        // HEATING: Actively heating up - RED
        return ui_theme_get_color("primary_color");
    } else if (current_deg > target_deg + tolerance) {
        // COOLING: Cooling down to target - BLUE
        return ui_theme_get_color("info_color");
    } else {
        // AT_TEMP: Within tolerance of target - GREEN
        return ui_theme_get_color("success_color");
    }
}

} // namespace temperature
} // namespace ui
} // namespace helix
