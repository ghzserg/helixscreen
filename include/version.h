// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2025 HelixScreen

#pragma once

/**
 * @file version.h
 * @brief Semantic version parsing and constraint checking
 *
 * Provides utilities for parsing semantic version strings and checking
 * version constraints. Used by the plugin system to verify plugin
 * compatibility with the running HelixScreen version.
 *
 * Supports operators: >=, >, =, <, <=
 *
 * @example
 * // Parse a version string
 * auto [major, minor, patch] = parse_version("2.1.0");
 *
 * // Check if constraint is satisfied
 * bool ok = check_version_constraint(">=2.0.0", "2.1.0"); // true
 */

#include <optional>
#include <string>
#include <tuple>

namespace helix::version {

/**
 * @brief Semantic version components
 */
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    bool operator==(const Version& other) const {
        return major == other.major && minor == other.minor && patch == other.patch;
    }

    bool operator<(const Version& other) const {
        if (major != other.major)
            return major < other.major;
        if (minor != other.minor)
            return minor < other.minor;
        return patch < other.patch;
    }

    bool operator>(const Version& other) const {
        return other < *this;
    }

    bool operator<=(const Version& other) const {
        return !(other < *this);
    }

    bool operator>=(const Version& other) const {
        return !(*this < other);
    }

    bool operator!=(const Version& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Parse a semantic version string
 *
 * Accepts formats:
 * - "1" -> 1.0.0
 * - "1.2" -> 1.2.0
 * - "1.2.3" -> 1.2.3
 * - "1.2.3-beta" -> 1.2.3 (pre-release suffix ignored)
 * - "1.2.3+build" -> 1.2.3 (build metadata ignored)
 *
 * @param version_str Version string to parse
 * @return Parsed version, or nullopt if invalid
 */
std::optional<Version> parse_version(const std::string& version_str);

/**
 * @brief Check if a version satisfies a constraint
 *
 * Constraint format: [operator]version
 * - ">=2.0.0" - version must be >= 2.0.0
 * - ">1.0.0" - version must be > 1.0.0
 * - "=2.0.0" or "2.0.0" - version must equal 2.0.0
 * - "<3.0.0" - version must be < 3.0.0
 * - "<=2.5.0" - version must be <= 2.5.0
 *
 * @param constraint Version constraint string
 * @param version Version string to check
 * @return true if version satisfies constraint, false otherwise
 */
bool check_version_constraint(const std::string& constraint, const std::string& version);

/**
 * @brief Convert Version to string
 *
 * @param v Version to convert
 * @return String representation (e.g., "1.2.3")
 */
std::string to_string(const Version& v);

} // namespace helix::version
