// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils/network_validation.h"

#include <algorithm>
#include <cctype>

// Trim leading and trailing whitespace from user input
static std::string trim(const std::string& s) {
    auto start =
        std::find_if_not(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); });
    auto end = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char c) {
                   return std::isspace(c);
               }).base();
    return (start < end) ? std::string(start, end) : std::string();
}

bool is_valid_ip_or_hostname(const std::string& host_raw) {
    std::string host = trim(host_raw);

    if (host.empty()) {
        return false;
    }

    // RFC 1035: Total hostname max 253 characters
    if (host.length() > 253) {
        return false;
    }

    // Determine if this looks like an IP address pattern (digits, dots, and possibly
    // invalid chars like '-' that would make it an invalid IP)
    bool has_letter = false;
    bool has_underscore = false;
    int dot_count = 0;
    for (char c : host) {
        if (std::isalpha(c)) {
            has_letter = true;
        } else if (c == '_') {
            has_underscore = true;
        } else if (c == '.') {
            dot_count++;
        }
    }

    // If it has no letters and no underscores, treat it as an IP address attempt
    // This prevents "192.168.-1.1" from being treated as a hostname
    bool is_ip_attempt = !has_letter && !has_underscore;

    // If it looks like an IP attempt, validate strictly as IPv4
    if (is_ip_attempt) {
        // Must have exactly 3 dots for valid IPv4
        if (dot_count != 3) {
            return false;
        }

        // Cannot start or end with dot
        if (host[0] == '.' || host.back() == '.') {
            return false;
        }

        // Parse and validate each octet
        size_t segment_start = 0;
        int octet_count = 0;
        for (size_t i = 0; i <= host.length(); i++) {
            if (i == host.length() || host[i] == '.') {
                std::string segment = host.substr(segment_start, i - segment_start);

                // Empty segment (e.g., "192..1.1")
                if (segment.empty()) {
                    return false;
                }

                // Check for invalid characters (only digits allowed in IP octets)
                for (char c : segment) {
                    if (!std::isdigit(c)) {
                        return false;
                    }
                }

                // Check octet range (0-255)
                if (segment.length() > 3) {
                    return false;
                }

                try {
                    int num = std::stoi(segment);
                    if (num < 0 || num > 255) {
                        return false;
                    }
                } catch (...) {
                    return false;
                }

                octet_count++;
                segment_start = i + 1;
            }
        }

        return octet_count == 4;
    }

    // Otherwise, validate as hostname
    // RFC 1035: Must start with alphanumeric
    if (!std::isalnum(static_cast<unsigned char>(host[0]))) {
        return false;
    }

    // Cannot end with hyphen or dot
    if (host.back() == '-' || host.back() == '.') {
        return false;
    }

    // Validate each label (segment between dots)
    size_t label_start = 0;
    for (size_t i = 0; i <= host.length(); i++) {
        if (i == host.length() || host[i] == '.') {
            size_t label_len = i - label_start;

            // RFC 1035: Each label max 63 characters, min 1 character
            if (label_len > 63 || label_len == 0) {
                return false;
            }

            // Label cannot start with hyphen
            if (host[label_start] == '-') {
                return false;
            }

            // Label cannot end with hyphen (check char before dot or end)
            if (i > 0 && host[i - 1] == '-') {
                return false;
            }

            label_start = i + 1;
        } else {
            char c = host[i];
            // Alphanumeric, hyphen, and underscore allowed
            // (underscores not RFC-compliant but common in internal networks)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
                return false;
            }
        }
    }

    return true;
}

std::string sanitize_port(const char* str) {
    std::string result;
    if (str) {
        for (const char* p = str; *p; ++p) {
            if (std::isdigit(static_cast<unsigned char>(*p))) {
                result += *p;
            }
        }
    }
    return result;
}

std::string sanitize_port(const std::string& str) {
    return sanitize_port(str.c_str());
}

bool is_valid_port(const std::string& port_str_raw) {
    std::string port_str = trim(port_str_raw);

    if (port_str.empty()) {
        return false;
    }

    // Check all characters are digits
    for (char c : port_str) {
        if (!std::isdigit(c)) {
            return false;
        }
    }

    // Reject leading zeros (could be confused with octal notation)
    // Exception: "0" alone is handled by range check below
    if (port_str.length() > 1 && port_str[0] == '0') {
        return false;
    }

    // Parse and validate range
    try {
        int port = std::stoi(port_str);
        return port > 0 && port <= 65535;
    } catch (...) {
        return false;
    }
}
