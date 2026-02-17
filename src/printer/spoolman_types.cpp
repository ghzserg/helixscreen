// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "spoolman_types.h"

#include <algorithm>
#include <cctype>
#include <sstream>

// ============================================================================
// Spool Filtering
// ============================================================================

std::vector<SpoolInfo> filter_spools(const std::vector<SpoolInfo>& spools,
                                     const std::string& query) {
    // Empty or whitespace-only query returns all spools.
    // The stream >> term loop skips whitespace, so terms will be empty for whitespace-only input.
    if (query.empty()) {
        return spools;
    }

    // Split query into lowercase terms (space-separated)
    std::vector<std::string> terms;
    std::istringstream stream(query);
    std::string term;
    while (stream >> term) {
        std::transform(term.begin(), term.end(), term.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        terms.push_back(std::move(term));
    }

    if (terms.empty()) {
        return spools;
    }

    std::vector<SpoolInfo> result;
    result.reserve(spools.size());

    for (const auto& spool : spools) {
        // Build searchable text: "#ID vendor material color_name"
        std::string searchable = "#" + std::to_string(spool.id) + " " + spool.vendor + " " +
                                 spool.material + " " + spool.color_name;

        // Lowercase the searchable text
        std::transform(searchable.begin(), searchable.end(), searchable.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        // All terms must match (AND logic)
        bool all_match =
            std::all_of(terms.begin(), terms.end(), [&searchable](const std::string& t) {
                return searchable.find(t) != std::string::npos;
            });

        if (all_match) {
            result.push_back(spool);
        }
    }

    return result;
}
