// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_motion_api.h"

#include "ui_error_reporting.h"
#include "ui_notification.h"

#include "moonraker_client.h"
#include "moonraker_types.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

#include "hv/json.hpp"

// Local validation helpers (same as moonraker_api_internal.h but standalone)
namespace {

bool is_valid_axis(char axis) {
    char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(axis)));
    return upper == 'X' || upper == 'Y' || upper == 'Z' || upper == 'E';
}

bool is_safe_distance(double distance, const SafetyLimits& limits) {
    return distance >= limits.min_relative_distance_mm &&
           distance <= limits.max_relative_distance_mm;
}

bool is_safe_position(double position, const SafetyLimits& limits) {
    return position >= limits.min_absolute_position_mm &&
           position <= limits.max_absolute_position_mm;
}

bool is_safe_feedrate(double feedrate, const SafetyLimits& limits) {
    return feedrate >= limits.min_feedrate_mm_min && feedrate <= limits.max_feedrate_mm_min;
}

bool reject_non_finite(std::initializer_list<double> values, const char* method,
                       const MoonrakerMotionAPI::ErrorCallback& on_error) {
    for (double v : values) {
        if (std::isnan(v) || std::isinf(v)) {
            spdlog::warn("[Motion API] {}: Rejecting G-code generation: "
                         "invalid value (NaN/Inf)",
                         method);
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::VALIDATION_ERROR;
                err.message = "Parameter contains NaN or Inf value";
                err.method = method;
                on_error(err);
            }
            return true;
        }
    }
    return false;
}

// Annotate G-code with source comment for traceability
std::string annotate_gcode(const std::string& gcode) {
    constexpr const char* GCODE_SOURCE_COMMENT = " ; from helixscreen";

    std::string result;
    result.reserve(gcode.size() + 20 * std::count(gcode.begin(), gcode.end(), '\n') + 20);

    std::istringstream stream(gcode);
    std::string line;
    bool first = true;

    while (std::getline(stream, line)) {
        if (!first) {
            result += '\n';
        }
        first = false;

        if (!line.empty() && line.find_first_not_of(" \t\r") != std::string::npos) {
            result += line + GCODE_SOURCE_COMMENT;
        } else {
            result += line;
        }
    }

    return result;
}

} // namespace

// ============================================================================
// MoonrakerMotionAPI Implementation
// ============================================================================

MoonrakerMotionAPI::MoonrakerMotionAPI(helix::MoonrakerClient& client,
                                       const SafetyLimits& safety_limits)
    : client_(client), safety_limits_(safety_limits) {}

// ============================================================================
// Motion Control Operations
// ============================================================================

void MoonrakerMotionAPI::home_axes(const std::string& axes, SuccessCallback on_success,
                                   ErrorCallback on_error) {
    // Validate axes string (empty means all, or contains only XYZE)
    if (!axes.empty()) {
        for (char axis : axes) {
            if (!is_valid_axis(axis)) {
                NOTIFY_ERROR("Invalid axis '{}' in homing command. Must be X, Y, Z, or E.", axis);
                if (on_error) {
                    MoonrakerError err;
                    err.type = MoonrakerErrorType::VALIDATION_ERROR;
                    err.message = "Invalid axis character (must be X, Y, Z, or E)";
                    err.method = "home_axes";
                    on_error(err);
                }
                return;
            }
        }
    }

    std::string gcode = generate_home_gcode(axes);
    spdlog::info("[Motion API] Homing axes: {} (G-code: {})", axes.empty() ? "all" : axes, gcode);

    execute_gcode(gcode, on_success, on_error, HOMING_TIMEOUT_MS);
}

void MoonrakerMotionAPI::move_axis(char axis, double distance, double feedrate,
                                   SuccessCallback on_success, ErrorCallback on_error) {
    // Reject NaN/Inf before any G-code generation
    if (reject_non_finite({distance, feedrate}, "move_axis", on_error)) {
        return;
    }

    // Validate axis
    if (!is_valid_axis(axis)) {
        NOTIFY_ERROR("Invalid axis '{}'. Must be X, Y, Z, or E.", axis);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid axis: " + std::string(1, axis) + " (must be X, Y, Z, or E)";
            err.method = "move_axis";
            on_error(err);
        }
        return;
    }

    // Validate distance is within safety limits
    if (!is_safe_distance(distance, safety_limits_)) {
        NOTIFY_ERROR("Move distance {:.1f}mm is too large. Maximum: {:.1f}mm.", std::abs(distance),
                     safety_limits_.max_relative_distance_mm);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Distance " + std::to_string(distance) + "mm exceeds safety limits (" +
                          std::to_string(safety_limits_.min_relative_distance_mm) + "-" +
                          std::to_string(safety_limits_.max_relative_distance_mm) + "mm)";
            err.method = "move_axis";
            on_error(err);
        }
        return;
    }

    // Validate feedrate if specified (0 means use default, negative is invalid)
    if (feedrate != 0 && !is_safe_feedrate(feedrate, safety_limits_)) {
        NOTIFY_ERROR("Speed {:.0f}mm/min is too fast. Maximum: {:.0f}mm/min.", feedrate,
                     safety_limits_.max_feedrate_mm_min);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Feedrate " + std::to_string(feedrate) +
                          "mm/min exceeds safety limits (" +
                          std::to_string(safety_limits_.min_feedrate_mm_min) + "-" +
                          std::to_string(safety_limits_.max_feedrate_mm_min) + "mm/min)";
            err.method = "move_axis";
            on_error(err);
        }
        return;
    }

    std::string gcode = generate_move_gcode(axis, distance, feedrate);
    spdlog::info("[Motion API] Moving axis {} by {}mm (G-code: {})", axis, distance, gcode);

    execute_gcode(gcode, on_success, on_error);
}

void MoonrakerMotionAPI::move_to_position(char axis, double position, double feedrate,
                                          SuccessCallback on_success, ErrorCallback on_error) {
    // Reject NaN/Inf before any G-code generation
    if (reject_non_finite({position, feedrate}, "move_to_position", on_error)) {
        return;
    }

    // Validate axis
    if (!is_valid_axis(axis)) {
        NOTIFY_ERROR("Invalid axis '{}'. Must be X, Y, Z, or E.", axis);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid axis character (must be X, Y, Z, or E)";
            err.method = "move_to_position";
            on_error(err);
        }
        return;
    }

    // Validate position is within safety limits
    if (!is_safe_position(position, safety_limits_)) {
        NOTIFY_ERROR("Position {:.1f}mm is out of range. Valid: {:.1f}mm to {:.1f}mm.", position,
                     safety_limits_.min_absolute_position_mm,
                     safety_limits_.max_absolute_position_mm);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Position " + std::to_string(position) + "mm exceeds safety limits (" +
                          std::to_string(safety_limits_.min_absolute_position_mm) + "-" +
                          std::to_string(safety_limits_.max_absolute_position_mm) + "mm)";
            err.method = "move_to_position";
            on_error(err);
        }
        return;
    }

    // Validate feedrate if specified (0 means use default, negative is invalid)
    if (feedrate != 0 && !is_safe_feedrate(feedrate, safety_limits_)) {
        NOTIFY_ERROR("Speed {:.0f}mm/min is too fast. Maximum: {:.0f}mm/min.", feedrate,
                     safety_limits_.max_feedrate_mm_min);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Feedrate " + std::to_string(feedrate) +
                          "mm/min exceeds safety limits (" +
                          std::to_string(safety_limits_.min_feedrate_mm_min) + "-" +
                          std::to_string(safety_limits_.max_feedrate_mm_min) + "mm/min)";
            err.method = "move_to_position";
            on_error(err);
        }
        return;
    }

    std::string gcode = generate_absolute_move_gcode(axis, position, feedrate);
    spdlog::info("[Motion API] Moving axis {} to {}mm (G-code: {})", axis, position, gcode);

    execute_gcode(gcode, on_success, on_error);
}

// ============================================================================
// G-code Generation Helpers
// ============================================================================

std::string MoonrakerMotionAPI::generate_home_gcode(const std::string& axes) {
    if (axes.empty()) {
        return "G28"; // Home all axes
    } else {
        std::ostringstream gcode;
        gcode << "G28";
        for (char axis : axes) {
            gcode << " " << static_cast<char>(std::toupper(axis));
        }
        return gcode.str();
    }
}

std::string MoonrakerMotionAPI::generate_move_gcode(char axis, double distance, double feedrate) {
    if (std::isnan(distance) || std::isinf(distance) || std::isnan(feedrate) ||
        std::isinf(feedrate)) {
        spdlog::warn("[Motion API] generate_move_gcode: Rejecting G-code generation: "
                     "invalid value (NaN/Inf)");
        return "";
    }

    std::ostringstream gcode;
    gcode << "G91\n"; // Relative positioning
    gcode << "G0 " << static_cast<char>(std::toupper(axis)) << distance;
    if (feedrate > 0) {
        gcode << " F" << feedrate;
    }
    gcode << "\nG90"; // Back to absolute positioning
    return gcode.str();
}

std::string MoonrakerMotionAPI::generate_absolute_move_gcode(char axis, double position,
                                                             double feedrate) {
    if (std::isnan(position) || std::isinf(position) || std::isnan(feedrate) ||
        std::isinf(feedrate)) {
        spdlog::warn("[Motion API] generate_absolute_move_gcode: Rejecting G-code generation: "
                     "invalid value (NaN/Inf)");
        return "";
    }

    std::ostringstream gcode;
    gcode << "G90\n"; // Absolute positioning
    gcode << "G0 " << static_cast<char>(std::toupper(axis)) << position;
    if (feedrate > 0) {
        gcode << " F" << feedrate;
    }
    return gcode.str();
}

// ============================================================================
// G-code Execution
// ============================================================================

void MoonrakerMotionAPI::execute_gcode(const std::string& gcode, SuccessCallback on_success,
                                       ErrorCallback on_error, uint32_t timeout_ms) {
    std::string annotated = annotate_gcode(gcode);
    json params = {{"script", annotated}};

    spdlog::trace("[Motion API] Executing G-code: {}", annotated);

    // Guard: only wrap on_success in lambda if non-null, otherwise pass nullptr.
    std::function<void(json)> success_wrapper;
    if (on_success) {
        success_wrapper = [on_success](json) { on_success(); };
    }
    client_.send_jsonrpc("printer.gcode.script", params, std::move(success_wrapper), on_error,
                         timeout_ms);
}
