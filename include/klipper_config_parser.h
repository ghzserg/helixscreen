// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Parser for Klipper's INI-like config format.
 *
 * Handles Klipper-specific quirks: colon and equals separators, multi-line
 * gcode values, prefixed section names (e.g. [gcode_macro NAME]), comment
 * preservation, and format-preserving roundtrip serialization.
 */
class KlipperConfigParser {
  public:
    /// Parse config from string content. Returns true on success.
    bool parse(const std::string& content);

    /// Get a string value from section/key, or default_val if not found.
    std::string get(const std::string& section, const std::string& key,
                    const std::string& default_val = "") const;

    /// Get a boolean value. Recognizes True/False, true/false, yes/no, 1/0.
    bool get_bool(const std::string& section, const std::string& key,
                  bool default_val = false) const;

    /// Get a float value, or default_val if not found or not parseable.
    float get_float(const std::string& section, const std::string& key,
                    float default_val = 0.0f) const;

    /// Get an integer value, or default_val if not found or not parseable.
    int get_int(const std::string& section, const std::string& key, int default_val = 0) const;

    /// Set a value in memory. Preserves original separator style for existing keys.
    void set(const std::string& section, const std::string& key, const std::string& value);

    /// Check if a section exists.
    bool has_section(const std::string& section) const;

    /// Get all section names in order of appearance.
    std::vector<std::string> get_sections() const;

    /// Get all sections whose name starts with prefix (e.g. "AFC_stepper").
    /// Matches "prefix" exactly or "prefix " followed by anything.
    std::vector<std::string> get_sections_matching(const std::string& prefix) const;

    /// Get all keys in a section, in order of appearance.
    std::vector<std::string> get_keys(const std::string& section) const;

    /// Serialize back to string, preserving comments, blank lines, and formatting.
    std::string serialize() const;

    /// Returns true if any set() call has been made since parse().
    bool is_modified() const;

  private:
    /// Represents one line of the config file, preserving original text.
    struct Line {
        enum Type { COMMENT, BLANK, SECTION_HEADER, KEY_VALUE, CONTINUATION };
        Type type;
        std::string raw; // Original line text (without trailing newline)

        // Populated for SECTION_HEADER
        std::string section_name;

        // Populated for KEY_VALUE
        std::string key;
        std::string value;        // Trimmed value (first line only for multi-line)
        char separator = ':';     // ':' or '='
        std::string separator_ws; // Whitespace around separator for exact reproduction

        // For multi-line values: indices of continuation lines in lines_
        std::vector<size_t> continuation_indices;
    };

    std::vector<Line> lines_;
    // section_name -> (key -> line index in lines_)
    std::unordered_map<std::string, std::unordered_map<std::string, size_t>> section_map_;
    // Ordered list of section names
    std::vector<std::string> section_order_;
    bool modified_ = false;

    static std::string trim(const std::string& s);
    std::string get_multiline_value(size_t key_line_idx) const;
    void rebuild_indices_after_insert(size_t inserted_idx, const std::string& section,
                                      const std::string& key);
};
