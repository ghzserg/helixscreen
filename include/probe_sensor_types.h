// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace helix::sensors {

/// @brief Role assigned to a probe sensor
enum class ProbeSensorRole {
    NONE = 0,    ///< Discovered but not assigned to a role
    Z_PROBE = 1, ///< Used as Z probe for bed leveling
};

/// @brief Type of probe sensor hardware
enum class ProbeSensorType {
    STANDARD = 1,       ///< Standard probe (Klipper "probe" section)
    BLTOUCH = 2,        ///< BLTouch probe
    SMART_EFFECTOR = 3, ///< Duet Smart Effector
    EDDY_CURRENT = 4,   ///< Eddy current probe (e.g., probe_eddy_current btt)
    CARTOGRAPHER = 5,   ///< Cartographer 3D scanning/contact probe
    BEACON = 6,         ///< Beacon eddy current probe
    TAP = 7,            ///< Voron Tap nozzle-contact probe
    KLICKY = 8,         ///< Klicky magnetic probe (macro-based)
};

/// @brief Configuration for a probe sensor
struct ProbeSensorConfig {
    std::string
        klipper_name; ///< Full Klipper name (e.g., "probe", "bltouch", "probe_eddy_current btt")
    std::string sensor_name; ///< Short display name (e.g., "probe", "bltouch", "btt")
    ProbeSensorType type = ProbeSensorType::STANDARD;
    ProbeSensorRole role = ProbeSensorRole::NONE;
    bool enabled = true;

    ProbeSensorConfig() = default;

    ProbeSensorConfig(std::string klipper_name_, std::string sensor_name_, ProbeSensorType type_)
        : klipper_name(std::move(klipper_name_)), sensor_name(std::move(sensor_name_)),
          type(type_) {}
};

/// @brief Runtime state for a probe sensor
struct ProbeSensorState {
    bool triggered = false;     ///< Current triggered state (from query, not regular status)
    float last_z_result = 0.0f; ///< Last Z probe result in mm
    float z_offset = 0.0f;      ///< Z offset in mm
    bool available = false;     ///< Sensor available in current config
};

/// @brief Convert role enum to config string
/// @param role The role to convert
/// @return Config-safe string for JSON storage
[[nodiscard]] inline std::string probe_role_to_string(ProbeSensorRole role) {
    switch (role) {
    case ProbeSensorRole::NONE:
        return "none";
    case ProbeSensorRole::Z_PROBE:
        return "z_probe";
    default:
        return "none";
    }
}

/// @brief Parse role string to enum
/// @param str The config string to parse
/// @return Parsed role, or NONE if unrecognized
[[nodiscard]] inline ProbeSensorRole probe_role_from_string(const std::string& str) {
    if (str == "z_probe")
        return ProbeSensorRole::Z_PROBE;
    return ProbeSensorRole::NONE;
}

/// @brief Convert role to display string
/// @param role The role to convert
/// @return Human-readable role name for UI display
[[nodiscard]] inline std::string probe_role_to_display_string(ProbeSensorRole role) {
    switch (role) {
    case ProbeSensorRole::NONE:
        return "Unassigned";
    case ProbeSensorRole::Z_PROBE:
        return "Z Probe";
    default:
        return "Unassigned";
    }
}

/// @brief Convert type enum to config string
/// @param type The type to convert
/// @return Config-safe string
[[nodiscard]] inline std::string probe_type_to_string(ProbeSensorType type) {
    switch (type) {
    case ProbeSensorType::STANDARD:
        return "standard";
    case ProbeSensorType::BLTOUCH:
        return "bltouch";
    case ProbeSensorType::SMART_EFFECTOR:
        return "smart_effector";
    case ProbeSensorType::EDDY_CURRENT:
        return "eddy_current";
    case ProbeSensorType::CARTOGRAPHER:
        return "cartographer";
    case ProbeSensorType::BEACON:
        return "beacon";
    case ProbeSensorType::TAP:
        return "tap";
    case ProbeSensorType::KLICKY:
        return "klicky";
    default:
        return "standard";
    }
}

/// @brief Parse type string to enum
/// @param str The config string to parse
/// @return Parsed type, defaults to STANDARD if unrecognized
[[nodiscard]] inline ProbeSensorType probe_type_from_string(const std::string& str) {
    if (str == "bltouch")
        return ProbeSensorType::BLTOUCH;
    if (str == "smart_effector")
        return ProbeSensorType::SMART_EFFECTOR;
    if (str == "eddy_current")
        return ProbeSensorType::EDDY_CURRENT;
    if (str == "cartographer")
        return ProbeSensorType::CARTOGRAPHER;
    if (str == "beacon")
        return ProbeSensorType::BEACON;
    if (str == "tap")
        return ProbeSensorType::TAP;
    if (str == "klicky")
        return ProbeSensorType::KLICKY;
    return ProbeSensorType::STANDARD;
}

} // namespace helix::sensors
