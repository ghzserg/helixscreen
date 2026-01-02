// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2025 HelixScreen

#include "version.h"

#include "spdlog/spdlog.h"

#include <charconv>
#include <cstring>

namespace helix::version {

std::optional<Version> parse_version(const std::string& version_str) {
    if (version_str.empty()) {
        return std::nullopt;
    }

    // Skip leading 'v' or 'V' if present (e.g., "v1.2.3")
    const char* start = version_str.c_str();
    if (*start == 'v' || *start == 'V') {
        start++;
    }

    Version v{0, 0, 0};
    int* components[] = {&v.major, &v.minor, &v.patch};
    int component_idx = 0;

    const char* end = version_str.c_str() + version_str.length();

    while (start < end && component_idx < 3) {
        // Skip any leading whitespace
        while (start < end && (*start == ' ' || *start == '\t')) {
            start++;
        }

        if (start >= end) {
            break;
        }

        // Stop at pre-release (-) or build metadata (+)
        if (*start == '-' || *start == '+') {
            break;
        }

        // Parse the number
        int value = 0;
        auto [ptr, ec] = std::from_chars(start, end, value);

        if (ec != std::errc{}) {
            // Failed to parse - if we got at least major, that's ok
            if (component_idx == 0) {
                return std::nullopt;
            }
            break;
        }

        if (value < 0) {
            return std::nullopt; // Negative versions not allowed
        }

        *components[component_idx] = value;
        component_idx++;
        start = ptr;

        // Skip the dot separator
        if (start < end && *start == '.') {
            start++;
        } else if (start < end && *start != '-' && *start != '+' && *start != '\0') {
            // Invalid character
            break;
        }
    }

    // Must have at least major version
    if (component_idx == 0) {
        return std::nullopt;
    }

    return v;
}

bool check_version_constraint(const std::string& constraint, const std::string& version) {
    if (constraint.empty()) {
        // Empty constraint matches anything
        return true;
    }

    auto current = parse_version(version);
    if (!current) {
        spdlog::warn("[version] Failed to parse version: {}", version);
        return false;
    }

    // Parse operator and required version from constraint
    const char* c = constraint.c_str();

    // Skip leading whitespace
    while (*c == ' ' || *c == '\t') {
        c++;
    }

    enum class Op { EQ, GT, GE, LT, LE };
    Op op = Op::EQ;

    if (c[0] == '>' && c[1] == '=') {
        op = Op::GE;
        c += 2;
    } else if (c[0] == '<' && c[1] == '=') {
        op = Op::LE;
        c += 2;
    } else if (c[0] == '>') {
        op = Op::GT;
        c += 1;
    } else if (c[0] == '<') {
        op = Op::LT;
        c += 1;
    } else if (c[0] == '=') {
        op = Op::EQ;
        c += 1;
    }
    // else: no operator means equality

    // Skip whitespace after operator
    while (*c == ' ' || *c == '\t') {
        c++;
    }

    auto required = parse_version(c);
    if (!required) {
        spdlog::warn("[version] Failed to parse constraint version: {}", constraint);
        return false;
    }

    spdlog::debug("[version] Checking {} against constraint {} (op={}, required={}.{}.{})", version,
                  constraint, static_cast<int>(op), required->major, required->minor,
                  required->patch);

    switch (op) {
    case Op::EQ:
        return *current == *required;
    case Op::GT:
        return *current > *required;
    case Op::GE:
        return *current >= *required;
    case Op::LT:
        return *current < *required;
    case Op::LE:
        return *current <= *required;
    }

    return false;
}

std::string to_string(const Version& v) {
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

} // namespace helix::version
