// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "hv/json.hpp"

namespace helix::json_util {

/// Safely extract a string from a JSON field that may be null.
/// nlohmann .value("key", "") throws type_error.302 when the field is JSON null.
inline std::string safe_string(const nlohmann::json& j, const char* key,
                               const std::string& def = "") {
    if (!j.contains(key) || j[key].is_null()) {
        return def;
    }
    const auto& v = j[key];
    if (v.is_string()) {
        return v.get<std::string>();
    }
    return def;
}

/// Safely extract an int from a JSON field that may be number, string, or null.
inline int safe_int(const nlohmann::json& j, const char* key, int def = 0) {
    if (!j.contains(key) || j[key].is_null()) {
        return def;
    }
    const auto& v = j[key];
    if (v.is_number()) {
        return v.get<int>();
    }
    if (v.is_string()) {
        try {
            return std::stoi(v.get<std::string>());
        } catch (...) {
            return def;
        }
    }
    return def;
}

/// Safely extract a float from a JSON field that may be number, string, or null.
inline float safe_float(const nlohmann::json& j, const char* key, float def = 0.0f) {
    if (!j.contains(key) || j[key].is_null()) {
        return def;
    }
    const auto& v = j[key];
    if (v.is_number()) {
        return v.get<float>();
    }
    if (v.is_string()) {
        try {
            return std::stof(v.get<std::string>());
        } catch (...) {
            return def;
        }
    }
    return def;
}

/// Safely extract a double from a JSON field that may be number, string, or null.
inline double safe_double(const nlohmann::json& j, const char* key, double def = 0.0) {
    if (!j.contains(key) || j[key].is_null()) {
        return def;
    }
    const auto& v = j[key];
    if (v.is_number()) {
        return v.get<double>();
    }
    if (v.is_string()) {
        try {
            return std::stod(v.get<std::string>());
        } catch (...) {
            return def;
        }
    }
    return def;
}

} // namespace helix::json_util
