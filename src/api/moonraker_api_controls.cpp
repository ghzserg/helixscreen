// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_error_reporting.h"
#include "ui_notification.h"

#include "app_globals.h"
#include "hv/requests.h"
#include "moonraker_api.h"
#include "moonraker_api_internal.h"
#include "printer_state.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>
#include <sstream>

using namespace moonraker_internal;

// ============================================================================
// Temperature Control Operations
// ============================================================================

void MoonrakerAPI::set_temperature(const std::string& heater, double temperature,
                                   SuccessCallback on_success, ErrorCallback on_error) {
    // Reject NaN/Inf before any G-code generation
    if (reject_non_finite({temperature}, "set_temperature", on_error)) {
        return;
    }

    // Validate heater name
    if (!is_safe_identifier(heater)) {
        NOTIFY_ERROR("Invalid heater name '{}'. Contains unsafe characters.", heater);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid heater name contains illegal characters";
            err.method = "set_temperature";
            on_error(err);
        }
        return;
    }

    // Validate temperature range
    if (!is_safe_temperature(temperature, safety_limits_)) {
        NOTIFY_ERROR("Temperature {:.0f}°C is out of range. Valid: {:.0f}°C to {:.0f}°C.",
                     temperature, safety_limits_.min_temperature_celsius,
                     safety_limits_.max_temperature_celsius);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message =
                "Temperature " + std::to_string(static_cast<int>(temperature)) +
                "°C exceeds safety limits (" +
                std::to_string(static_cast<int>(safety_limits_.min_temperature_celsius)) + "-" +
                std::to_string(static_cast<int>(safety_limits_.max_temperature_celsius)) + "°C)";
            err.method = "set_temperature";
            on_error(err);
        }
        return;
    }

    std::ostringstream gcode;
    gcode << "SET_HEATER_TEMPERATURE HEATER=" << heater << " TARGET=" << temperature;

    spdlog::info("[Moonraker API] Setting {} temperature to {}°C", heater, temperature);

    execute_gcode(gcode.str(), on_success, on_error);
}

void MoonrakerAPI::set_fan_speed(const std::string& fan, double speed, SuccessCallback on_success,
                                 ErrorCallback on_error) {
    // Reject NaN/Inf before any G-code generation
    if (reject_non_finite({speed}, "set_fan_speed", on_error)) {
        return;
    }

    // Validate fan name
    if (!is_safe_identifier(fan)) {
        NOTIFY_ERROR("Invalid fan name '{}'. Contains unsafe characters.", fan);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid fan name contains illegal characters";
            err.method = "set_fan_speed";
            on_error(err);
        }
        return;
    }

    // Validate speed percentage
    if (!is_safe_fan_speed(speed, safety_limits_)) {
        NOTIFY_ERROR("Fan speed {:.0f}% is out of range. Valid: {:.0f}% to {:.0f}%.", speed,
                     safety_limits_.min_fan_speed_percent, safety_limits_.max_fan_speed_percent);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message =
                "Fan speed " + std::to_string(static_cast<int>(speed)) +
                "% exceeds safety limits (" +
                std::to_string(static_cast<int>(safety_limits_.min_fan_speed_percent)) + "-" +
                std::to_string(static_cast<int>(safety_limits_.max_fan_speed_percent)) + "%)";
            err.method = "set_fan_speed";
            on_error(err);
        }
        return;
    }

    // Convert percentage to 0-255 range for M106 command
    int fan_value = static_cast<int>(speed * 255.0 / 100.0);

    std::ostringstream gcode;
    if (fan == "fan") {
        // Part cooling fan uses M106
        gcode << "M106 S" << fan_value;
    } else {
        // Generic fans use SET_FAN_SPEED with just the fan name (strip Klipper type prefix)
        // e.g., "fan_generic Fanm106" -> "Fanm106", "fan_generic chamber_fan" -> "chamber_fan"
        std::string fan_name = fan;
        size_t space_pos = fan_name.find(' ');
        if (space_pos != std::string::npos) {
            fan_name = fan_name.substr(space_pos + 1);
        }
        gcode << "SET_FAN_SPEED FAN=" << fan_name << " SPEED=" << (speed / 100.0);
    }

    spdlog::trace("[Moonraker API] Setting {} speed to {}%", fan, speed);

    execute_gcode(gcode.str(), on_success, on_error);
}

// ============================================================================
// Power Device Control Operations
// ============================================================================

void MoonrakerAPI::get_power_devices(PowerDevicesCallback on_success, ErrorCallback on_error) {
    if (http_base_url_.empty()) {
        spdlog::error("[Moonraker API] HTTP base URL not configured for power devices");
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::CONNECTION_LOST;
            err.message = "Not connected to Moonraker";
            err.method = "get_power_devices";
            on_error(err);
        }
        return;
    }

    std::string url = http_base_url_ + "/machine/device_power/devices";
    spdlog::debug("[Moonraker API] Fetching power devices from: {}", url);

    std::thread([url, on_success, on_error]() {
        auto resp = requests::get(url.c_str());

        if (!resp) {
            spdlog::error("[Moonraker API] HTTP request failed for power devices");
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::CONNECTION_LOST;
                err.message = "HTTP request failed";
                err.method = "get_power_devices";
                on_error(err);
            }
            return;
        }

        if (resp->status_code != 200) {
            spdlog::error("[Moonraker API] Power devices request failed: HTTP {}",
                          static_cast<int>(resp->status_code));
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::UNKNOWN;
                err.code = static_cast<int>(resp->status_code);
                err.message = "HTTP " + std::to_string(static_cast<int>(resp->status_code));
                err.method = "get_power_devices";
                on_error(err);
            }
            return;
        }

        // Parse JSON response
        try {
            json j = json::parse(resp->body);
            std::vector<PowerDevice> devices;

            if (j.contains("result") && j["result"].contains("devices")) {
                for (const auto& [name, info] : j["result"]["devices"].items()) {
                    PowerDevice dev;
                    dev.device = name;
                    dev.type = info.value("type", "unknown");
                    dev.status = info.value("status", "off");
                    dev.locked_while_printing = info.value("locked_while_printing", false);
                    devices.push_back(dev);
                }
            }

            spdlog::info("[Moonraker API] Found {} power devices", devices.size());
            if (on_success) {
                on_success(devices);
            }
        } catch (const std::exception& e) {
            spdlog::error("[Moonraker API] Failed to parse power devices: {}", e.what());
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::UNKNOWN;
                err.message = e.what();
                err.method = "get_power_devices";
                on_error(err);
            }
        }
    }).detach();
}

void MoonrakerAPI::set_device_power(const std::string& device, const std::string& action,
                                    SuccessCallback on_success, ErrorCallback on_error) {
    // Validate device name
    if (!is_safe_identifier(device)) {
        spdlog::error("[Moonraker API] Invalid device name: {}", device);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid device name";
            err.method = "set_device_power";
            on_error(err);
        }
        return;
    }

    // Validate action
    if (action != "on" && action != "off" && action != "toggle") {
        spdlog::error("[Moonraker API] Invalid power action: {}", action);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid action (must be on, off, or toggle)";
            err.method = "set_device_power";
            on_error(err);
        }
        return;
    }

    if (http_base_url_.empty()) {
        spdlog::error("[Moonraker API] HTTP base URL not configured for power device control");
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::CONNECTION_LOST;
            err.message = "Not connected to Moonraker";
            err.method = "set_device_power";
            on_error(err);
        }
        return;
    }

    // Build URL with query params
    std::string url =
        http_base_url_ + "/machine/device_power/device?device=" + device + "&action=" + action;

    spdlog::info("[Moonraker API] Setting power device '{}' to '{}'", device, action);

    std::thread([url, device, action, on_success, on_error]() {
        auto resp = requests::post(url.c_str(), "");

        if (!resp) {
            spdlog::error("[Moonraker API] HTTP request failed for power device");
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::CONNECTION_LOST;
                err.message = "HTTP request failed";
                err.method = "set_device_power";
                on_error(err);
            }
            return;
        }

        if (resp->status_code != 200) {
            spdlog::error("[Moonraker API] Power device command failed: HTTP {}",
                          static_cast<int>(resp->status_code));
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::UNKNOWN;
                err.code = static_cast<int>(resp->status_code);
                err.message = "HTTP " + std::to_string(static_cast<int>(resp->status_code));
                err.method = "set_device_power";
                on_error(err);
            }
            return;
        }

        spdlog::info("[Moonraker API] Power device '{}' set to '{}' successfully", device, action);
        if (on_success) {
            on_success();
        }
    }).detach();
}

// ============================================================================
// System Control Operations
// ============================================================================

namespace {
// Annotate G-code with source comment for traceability
// Handles multi-line G-code by adding comment to each line
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

        // Only add comment to non-empty lines
        if (!line.empty() && line.find_first_not_of(" \t\r") != std::string::npos) {
            result += line + GCODE_SOURCE_COMMENT;
        } else {
            result += line;
        }
    }

    return result;
}
} // namespace

void MoonrakerAPI::execute_gcode(const std::string& gcode, SuccessCallback on_success,
                                 ErrorCallback on_error, uint32_t timeout_ms, bool silent) {
    std::string annotated = annotate_gcode(gcode);
    json params = {{"script", annotated}};

    spdlog::trace("[Moonraker API] Executing G-code: {}", annotated);

    // Guard: only wrap on_success in lambda if non-null, otherwise pass nullptr.
    // A lambda wrapping a null std::function would bypass send_jsonrpc's null check
    // and throw bad_function_call when invoked.
    std::function<void(json)> success_wrapper;
    if (on_success) {
        success_wrapper = [on_success](json) { on_success(); };
    }
    client_.send_jsonrpc("printer.gcode.script", params, std::move(success_wrapper), on_error,
                         timeout_ms, silent);
}

bool MoonrakerAPI::is_safe_gcode_param(const std::string& str) {
    return moonraker_internal::is_safe_identifier(str);
}

// ============================================================================
// Object Exclusion Operations
// ============================================================================

void MoonrakerAPI::exclude_object(const std::string& object_name, SuccessCallback on_success,
                                  ErrorCallback on_error) {
    // Validate object name to prevent G-code injection
    if (!is_safe_identifier(object_name)) {
        NOTIFY_ERROR("Invalid object name '{}'. Contains unsafe characters.", object_name);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid object name contains illegal characters";
            err.method = "exclude_object";
            on_error(err);
        }
        return;
    }

    std::ostringstream gcode;
    gcode << "EXCLUDE_OBJECT NAME=" << object_name;

    spdlog::info("[Moonraker API] Excluding object: {}", object_name);

    execute_gcode(gcode.str(), on_success, on_error);
}

void MoonrakerAPI::emergency_stop(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::warn("[Moonraker API] Emergency stop requested!");

    client_.send_jsonrpc(
        "printer.emergency_stop", json::object(),
        [on_success](json) {
            spdlog::info("[Moonraker API] Emergency stop executed");
            on_success();
        },
        on_error);
}

void MoonrakerAPI::restart_firmware(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Restarting firmware");

    client_.send_jsonrpc(
        "printer.firmware_restart", json::object(),
        [on_success](json) {
            spdlog::info("[Moonraker API] Firmware restart initiated");
            on_success();
        },
        on_error);
}

void MoonrakerAPI::restart_klipper(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Restarting Klipper");

    client_.send_jsonrpc(
        "printer.restart", json::object(),
        [on_success](json) {
            spdlog::info("[Moonraker API] Klipper restart initiated");
            on_success();
        },
        on_error);
}

void MoonrakerAPI::restart_moonraker(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Restarting Moonraker");

    client_.send_jsonrpc(
        "server.restart", json::object(),
        [on_success](json) {
            spdlog::info("[Moonraker API] Moonraker restart initiated");
            if (on_success)
                on_success();
        },
        on_error);
}

void MoonrakerAPI::machine_shutdown(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Shutting down host machine");

    client_.send_jsonrpc(
        "machine.shutdown", json::object(),
        [on_success](json) {
            spdlog::info("[Moonraker API] Host shutdown initiated");
            if (on_success)
                on_success();
        },
        on_error);
}

void MoonrakerAPI::machine_reboot(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Rebooting host machine");

    client_.send_jsonrpc(
        "machine.reboot", json::object(),
        [on_success](json) {
            spdlog::info("[Moonraker API] Host reboot initiated");
            if (on_success)
                on_success();
        },
        on_error);
}

// ============================================================================
// Safety Limits Configuration
// ============================================================================

void MoonrakerAPI::update_safety_limits_from_printer(SuccessCallback on_success,
                                                     ErrorCallback on_error) {
    // Only update if limits haven't been explicitly set
    if (limits_explicitly_set_) {
        spdlog::debug("[Moonraker API] Safety limits explicitly configured, skipping Moonraker "
                      "auto-detection");
        if (on_success) {
            on_success();
        }
        return;
    }

    // Query printer configuration for safety limits
    json params = {{"objects", json::object({{"configfile", json::array({"settings"})}})}};

    client_.send_jsonrpc(
        "printer.objects.query", params,
        [this, on_success](json response) {
            try {
                if (!response.contains("result") || !response["result"].contains("status") ||
                    !response["result"]["status"].contains("configfile") ||
                    !response["result"]["status"]["configfile"].contains("settings")) {
                    spdlog::warn("[Moonraker API] Printer configuration not available, using "
                                 "default safety limits");
                    if (on_success) {
                        on_success();
                    }
                    return;
                }

                const json& settings = response["result"]["status"]["configfile"]["settings"];
                bool updated = false;

                // Extract max_velocity from printer settings
                if (settings.contains("printer") && settings["printer"].contains("max_velocity")) {
                    double max_velocity_mm_s = settings["printer"]["max_velocity"].get<double>();
                    safety_limits_.max_feedrate_mm_min = max_velocity_mm_s * 60.0;
                    updated = true;
                    spdlog::debug(
                        "[Moonraker API] Updated max_feedrate from printer config: {} mm/min",
                        safety_limits_.max_feedrate_mm_min);
                }

                // Extract axis limits from stepper configurations
                // Also populate build_volume from stepper_x/y for accurate bed dimensions
                BuildVolume build_vol = hardware().build_volume();
                bool build_volume_updated = false;

                for (const auto& stepper : {"stepper_x", "stepper_y", "stepper_z"}) {
                    if (settings.contains(stepper)) {
                        if (settings[stepper].contains("position_max")) {
                            double pos_max = settings[stepper]["position_max"].get<double>();
                            // Use the largest axis max as absolute position limit
                            if (pos_max > safety_limits_.max_absolute_position_mm) {
                                safety_limits_.max_absolute_position_mm = pos_max;
                                updated = true;
                            }
                            // Update build_volume for X/Y axes
                            if (std::string(stepper) == "stepper_x") {
                                build_vol.x_max = static_cast<float>(pos_max);
                                build_volume_updated = true;
                            } else if (std::string(stepper) == "stepper_y") {
                                build_vol.y_max = static_cast<float>(pos_max);
                                build_volume_updated = true;
                            } else if (std::string(stepper) == "stepper_z") {
                                build_vol.z_max = static_cast<float>(pos_max);
                                build_volume_updated = true;
                            }
                        }
                        if (settings[stepper].contains("position_min")) {
                            double pos_min = settings[stepper]["position_min"].get<double>();
                            // Use the smallest (most negative) axis min as absolute position limit
                            if (pos_min < safety_limits_.min_absolute_position_mm) {
                                safety_limits_.min_absolute_position_mm = pos_min;
                                updated = true;
                            }
                            // Update build_volume for X/Y axes
                            if (std::string(stepper) == "stepper_x") {
                                build_vol.x_min = static_cast<float>(pos_min);
                                build_volume_updated = true;
                            } else if (std::string(stepper) == "stepper_y") {
                                build_vol.y_min = static_cast<float>(pos_min);
                                build_volume_updated = true;
                            }
                        }
                    }
                }

                // Update build_volume if we found stepper configs
                if (build_volume_updated) {
                    hardware().set_build_volume(build_vol);
                    notify_build_volume_changed();
                    spdlog::debug("[Moonraker API] Build volume from stepper config: "
                                  "X[{:.0f},{:.0f}] Y[{:.0f},{:.0f}] Z[0,{:.0f}]",
                                  build_vol.x_min, build_vol.x_max, build_vol.y_min,
                                  build_vol.y_max, build_vol.z_max);
                }

                // Extract stepper_z position_endstop for non-probe Z-offset reference
                if (settings.contains("stepper_z") &&
                    settings["stepper_z"].contains("position_endstop")) {
                    double endstop = settings["stepper_z"]["position_endstop"].get<double>();
                    int microns = static_cast<int>(endstop * 1000.0);
                    get_printer_state().set_stepper_z_endstop_microns(microns);
                    spdlog::debug(
                        "[Moonraker API] stepper_z position_endstop: {:.3f}mm ({} microns)",
                        endstop, microns);
                }

                // Extract temperature limits from heater configurations
                for (const auto& [key, value] : settings.items()) {
                    if ((key.find("extruder") != std::string::npos ||
                         key.find("heater_") != std::string::npos) &&
                        value.is_object()) {
                        if (value.contains("max_temp")) {
                            double max_temp = value["max_temp"].get<double>();
                            // Use the highest heater max_temp as temperature limit
                            if (max_temp > safety_limits_.max_temperature_celsius) {
                                safety_limits_.max_temperature_celsius = max_temp;
                                updated = true;
                            }
                        }
                        if (value.contains("min_temp")) {
                            double min_temp = value["min_temp"].get<double>();
                            // Use the lowest heater min_temp as temperature limit
                            if (min_temp < safety_limits_.min_temperature_celsius) {
                                safety_limits_.min_temperature_celsius = min_temp;
                                updated = true;
                            }
                        }
                        // Extract min_extrude_temp from extruder (not heater_bed)
                        if (key == "extruder" && value.contains("min_extrude_temp")) {
                            double min_extrude = value["min_extrude_temp"].get<double>();
                            safety_limits_.min_extrude_temp_celsius = min_extrude;
                            updated = true;
                            spdlog::debug("[Moonraker API] min_extrude_temp from config: {}°C",
                                          min_extrude);
                        }
                    }
                }

                if (updated) {
                    spdlog::debug(
                        "[Moonraker API] Updated safety limits from printer configuration:");
                    spdlog::debug("[Moonraker API]   Temperature: {} to {}°C",
                                  safety_limits_.min_temperature_celsius,
                                  safety_limits_.max_temperature_celsius);
                    spdlog::debug("[Moonraker API]   Position: {} to {}mm",
                                  safety_limits_.min_absolute_position_mm,
                                  safety_limits_.max_absolute_position_mm);
                    spdlog::debug("[Moonraker API]   Feedrate: {} to {} mm/min",
                                  safety_limits_.min_feedrate_mm_min,
                                  safety_limits_.max_feedrate_mm_min);
                } else {
                    spdlog::debug("[Moonraker API] No safety limit overrides found in printer "
                                  "config, using defaults");
                }

                if (on_success) {
                    on_success();
                }
            } catch (const std::exception& e) {
                LOG_ERROR_INTERNAL("Failed to parse printer configuration for safety limits: {}",
                                   e.what());
                if (on_success) {
                    on_success(); // Continue with defaults on parse error
                }
            }
        },
        on_error);
}
