// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 HelixScreen Contributors

#include "ui_temperature_utils.h"
#include "spdlog/spdlog.h"
#include <cstdio>

namespace UITemperatureUtils {

bool validate_and_clamp(int& temp, int min_temp, int max_temp,
                       const char* context, const char* temp_type) {
    if (temp < min_temp || temp > max_temp) {
        spdlog::warn("[{}] Invalid {} temperature {}°C (valid: {}-{}°C), clamping",
                     context, temp_type, temp, min_temp, max_temp);
        temp = (temp < min_temp) ? min_temp : max_temp;
        return false;
    }
    return true;
}

bool validate_and_clamp_pair(int& current, int& target,
                             int min_temp, int max_temp,
                             const char* context) {
    bool current_valid = validate_and_clamp(current, min_temp, max_temp,
                                           context, "current");
    bool target_valid = validate_and_clamp(target, min_temp, max_temp,
                                          context, "target");
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
    snprintf(status_buf, sizeof(status_buf),
             "Heating (%d°C below minimum)", deficit);
    return status_buf;
}

} // namespace UITemperatureUtils
