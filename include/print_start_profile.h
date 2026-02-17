// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "printer_state.h"

#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @file print_start_profile.h
 * @brief JSON-driven print start signal matching profiles
 *
 * Profiles define how to detect PRINT_START phases for specific printer firmware.
 * Each profile contains signal format mappings (exact prefix matching) and
 * regex response patterns, loaded from JSON config files.
 *
 * @see config/print_start_profiles/default.json - Generic patterns for unknown printers
 * @see config/print_start_profiles/forge_x.json - FlashForge AD5M Forge-X mod
 */
class PrintStartProfile {
  public:
    /**
     * @brief Result of a signal or pattern match
     */
    struct MatchResult {
        helix::PrintStartPhase phase;
        std::string message;
        int progress; // 0-100, only meaningful in sequential mode
    };

    /**
     * @brief A single signal format mapping (exact prefix + value lookup)
     */
    struct SignalFormat {
        std::string prefix;
        std::unordered_map<std::string, MatchResult> mappings;
    };

    /**
     * @brief A regex response pattern
     */
    struct ResponsePattern {
        std::regex pattern;
        helix::PrintStartPhase phase;
        std::string message_template; // supports $1, $2 capture group substitution
        int weight;                   // only used in weighted mode
    };

    /**
     * @brief Progress calculation mode
     */
    enum class ProgressMode {
        WEIGHTED,  ///< Sum weights of detected phases (default, handles missing phases)
        SEQUENTIAL ///< Each signal maps to specific progress % (for known firmware)
    };

    // =========================================================================
    // Factory Methods
    // =========================================================================

    /**
     * @brief Load a named profile from config/print_start_profiles/{name}.json
     *
     * Falls back to default profile if the named profile can't be loaded.
     *
     * @param profile_name Profile name (without .json extension)
     * @return Loaded profile, or default profile on error
     */
    static std::shared_ptr<PrintStartProfile> load(const std::string& profile_name);

    /**
     * @brief Load the default profile
     *
     * Loads from config/print_start_profiles/default.json.
     * If that file is missing, returns a built-in fallback with the same patterns
     * currently hardcoded in PrintStartCollector.
     *
     * @return Default profile
     */
    static std::shared_ptr<PrintStartProfile> load_default();

    // =========================================================================
    // Matching Methods (called by PrintStartCollector)
    // =========================================================================

    /**
     * @brief Try to match a line against signal format mappings
     *
     * Checks line against each signal format's prefix. If prefix matches,
     * looks up the remainder in the mappings table.
     *
     * @param line G-code response line
     * @param[out] result Match result (phase, message, progress)
     * @return true if matched
     */
    bool try_match_signal(const std::string& line, MatchResult& result) const;

    /**
     * @brief Try to match a line against response patterns (regex)
     *
     * Runs regex search against each response pattern. Supports $1, $2
     * capture group substitution in message templates.
     *
     * @param line G-code response line
     * @param[out] result Match result (phase, message, weight in progress field)
     * @return true if matched
     */
    bool try_match_pattern(const std::string& line, MatchResult& result) const;

    // =========================================================================
    // Progress Calculation
    // =========================================================================

    /**
     * @brief Get the progress mode for this profile
     */
    ProgressMode progress_mode() const {
        return progress_mode_;
    }

    /**
     * @brief Get phase weight for weighted progress calculation
     *
     * Returns the weight assigned to a phase, or 0 if not defined.
     */
    int get_phase_weight(helix::PrintStartPhase phase) const;

    // =========================================================================
    // Accessors
    // =========================================================================

    const std::string& name() const {
        return name_;
    }
    const std::string& description() const {
        return description_;
    }
    bool has_signal_formats() const {
        return !signal_formats_.empty();
    }

  private:
    std::string name_;
    std::string description_;
    ProgressMode progress_mode_ = ProgressMode::WEIGHTED;
    std::vector<SignalFormat> signal_formats_;
    std::vector<ResponsePattern> response_patterns_;
    std::unordered_map<helix::PrintStartPhase, int> phase_weights_;

    /**
     * @brief Parse a JSON object into this profile
     * @return true on success
     */
    bool parse_json(const nlohmann::json& j, const std::string& source_path);

    /**
     * @brief Convert phase name string to helix::PrintStartPhase enum
     */
    static helix::PrintStartPhase parse_phase_name(const std::string& name);

    /**
     * @brief Substitute regex capture groups ($1, $2, ...) in a template
     */
    static std::string substitute_captures(const std::string& tmpl, const std::smatch& match);
};
