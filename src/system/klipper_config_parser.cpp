// SPDX-License-Identifier: GPL-3.0-or-later

#include "klipper_config_parser.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>

std::string KlipperConfigParser::trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r");
    if (start == std::string::npos)
        return "";
    auto end = s.find_last_not_of(" \t\r");
    return s.substr(start, end - start + 1);
}

bool KlipperConfigParser::parse(const std::string& content) {
    lines_.clear();
    section_map_.clear();
    section_order_.clear();
    modified_ = false;

    if (content.empty()) {
        return true;
    }

    // Split content into lines
    std::istringstream stream(content);
    std::string line_str;
    std::vector<std::string> raw_lines;
    while (std::getline(stream, line_str)) {
        raw_lines.push_back(line_str);
    }

    // If content ends with newline, getline won't produce a trailing empty entry,
    // which is fine since we re-add newlines in serialize.

    std::string current_section;
    size_t current_kv_idx = std::string::npos; // Index of current key-value line for continuations

    for (size_t i = 0; i < raw_lines.size(); ++i) {
        const auto& raw = raw_lines[i];
        Line line;
        line.raw = raw;

        std::string trimmed = trim(raw);

        if (trimmed.empty()) {
            line.type = Line::BLANK;
            current_kv_idx = std::string::npos;
            lines_.push_back(std::move(line));
            continue;
        }

        if (trimmed[0] == '#') {
            line.type = Line::COMMENT;
            current_kv_idx = std::string::npos;
            lines_.push_back(std::move(line));
            continue;
        }

        if (trimmed[0] == '[' && trimmed.back() == ']') {
            line.type = Line::SECTION_HEADER;
            line.section_name = trimmed.substr(1, trimmed.size() - 2);
            current_section = line.section_name;
            current_kv_idx = std::string::npos;

            if (section_map_.find(current_section) == section_map_.end()) {
                section_map_[current_section] = {};
                section_order_.push_back(current_section);
            }
            lines_.push_back(std::move(line));
            continue;
        }

        // Check if this is a continuation line (starts with whitespace)
        if ((raw[0] == ' ' || raw[0] == '\t') && current_kv_idx != std::string::npos) {
            line.type = Line::CONTINUATION;
            lines_.push_back(std::move(line));
            lines_[current_kv_idx].continuation_indices.push_back(lines_.size() - 1);
            continue;
        }

        // Must be a key-value line. Find separator (first `:` or `=`)
        // Klipper uses ": " or " = " but we need to handle both
        line.type = Line::KEY_VALUE;

        // Find the separator - prefer ": " first, then " = ", then bare ":" or "="
        size_t colon_pos = raw.find(": ");
        size_t equals_pos = raw.find(" = ");
        size_t sep_pos = std::string::npos;

        if (colon_pos != std::string::npos &&
            (equals_pos == std::string::npos || colon_pos <= equals_pos)) {
            sep_pos = colon_pos;
            line.separator = ':';
            line.separator_ws = ": ";
            line.key = trim(raw.substr(0, sep_pos));
            line.value = trim(raw.substr(sep_pos + 2));
        } else if (equals_pos != std::string::npos) {
            sep_pos = equals_pos;
            line.separator = '=';
            line.separator_ws = " = ";
            line.key = trim(raw.substr(0, sep_pos));
            line.value = trim(raw.substr(sep_pos + 3));
        } else {
            // Try bare separators
            colon_pos = raw.find(':');
            equals_pos = raw.find('=');

            if (colon_pos != std::string::npos &&
                (equals_pos == std::string::npos || colon_pos <= equals_pos)) {
                sep_pos = colon_pos;
                line.separator = ':';
                line.separator_ws = ":";
                line.key = trim(raw.substr(0, sep_pos));
                line.value = trim(raw.substr(sep_pos + 1));
            } else if (equals_pos != std::string::npos) {
                sep_pos = equals_pos;
                line.separator = '=';
                line.separator_ws = "=";
                line.key = trim(raw.substr(0, sep_pos));
                line.value = trim(raw.substr(sep_pos + 1));
            } else {
                // No separator found - treat as comment/unknown
                spdlog::warn("KlipperConfigParser: unrecognized line: '{}'", raw);
                line.type = Line::COMMENT;
                lines_.push_back(std::move(line));
                continue;
            }
        }

        current_kv_idx = lines_.size();
        if (!current_section.empty()) {
            section_map_[current_section][line.key] = current_kv_idx;
        }
        lines_.push_back(std::move(line));
    }

    return true;
}

std::string KlipperConfigParser::get_multiline_value(size_t key_line_idx) const {
    const auto& kv_line = lines_[key_line_idx];
    if (kv_line.continuation_indices.empty()) {
        return kv_line.value;
    }

    // Multi-line: first line value (may be empty for "gcode:") plus continuation lines
    std::string result;
    if (!kv_line.value.empty()) {
        result = kv_line.value;
    }
    for (size_t ci : kv_line.continuation_indices) {
        if (!result.empty()) {
            result += '\n';
        }
        result += trim(lines_[ci].raw);
    }
    return result;
}

std::string KlipperConfigParser::get(const std::string& section, const std::string& key,
                                     const std::string& default_val) const {
    auto sec_it = section_map_.find(section);
    if (sec_it == section_map_.end())
        return default_val;
    auto key_it = sec_it->second.find(key);
    if (key_it == sec_it->second.end())
        return default_val;
    return get_multiline_value(key_it->second);
}

bool KlipperConfigParser::get_bool(const std::string& section, const std::string& key,
                                   bool default_val) const {
    std::string val = get(section, key, "");
    if (val.empty())
        return default_val;
    // Lowercase for comparison
    std::string lower = val;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "true" || lower == "yes" || lower == "1")
        return true;
    if (lower == "false" || lower == "no" || lower == "0")
        return false;
    return default_val;
}

float KlipperConfigParser::get_float(const std::string& section, const std::string& key,
                                     float default_val) const {
    std::string val = get(section, key, "");
    if (val.empty())
        return default_val;
    try {
        return std::stof(val);
    } catch (...) {
        return default_val;
    }
}

int KlipperConfigParser::get_int(const std::string& section, const std::string& key,
                                 int default_val) const {
    std::string val = get(section, key, "");
    if (val.empty())
        return default_val;
    try {
        return std::stoi(val);
    } catch (...) {
        return default_val;
    }
}

void KlipperConfigParser::set(const std::string& section, const std::string& key,
                              const std::string& value) {
    modified_ = true;

    auto sec_it = section_map_.find(section);
    if (sec_it == section_map_.end()) {
        spdlog::warn("KlipperConfigParser: set() on nonexistent section '{}'", section);
        return;
    }

    auto key_it = sec_it->second.find(key);
    if (key_it != sec_it->second.end()) {
        // Update existing key-value line
        auto& line = lines_[key_it->second];
        line.value = value;
        // Rebuild raw line preserving separator style
        line.raw = line.key + line.separator_ws + value;
        // Clear continuations (set replaces multi-line with single value)
        line.continuation_indices.clear();
    } else {
        // Append new key to the section. Find last line belonging to this section.
        // Walk from the section header line to find where to insert.
        size_t insert_after = std::string::npos;
        bool in_section = false;
        for (size_t i = 0; i < lines_.size(); ++i) {
            if (lines_[i].type == Line::SECTION_HEADER && lines_[i].section_name == section) {
                in_section = true;
                insert_after = i;
                continue;
            }
            if (in_section) {
                if (lines_[i].type == Line::SECTION_HEADER) {
                    break; // Next section starts
                }
                if (lines_[i].type == Line::KEY_VALUE || lines_[i].type == Line::CONTINUATION) {
                    insert_after = i;
                }
            }
        }

        if (insert_after == std::string::npos)
            return;

        Line new_line;
        new_line.type = Line::KEY_VALUE;
        new_line.key = key;
        new_line.value = value;
        new_line.separator = ':';
        new_line.separator_ws = ": ";
        new_line.raw = key + ": " + value;

        // Insert after the last key in this section
        size_t new_idx = insert_after + 1;
        lines_.insert(lines_.begin() + static_cast<long>(new_idx), std::move(new_line));

        // Rebuild section_map_ since indices shifted
        rebuild_indices_after_insert(new_idx, section, key);
    }
}

void KlipperConfigParser::rebuild_indices_after_insert(size_t inserted_idx,
                                                       const std::string& section,
                                                       const std::string& key) {
    // All indices >= inserted_idx need to be incremented (except the new one)
    for (auto& [sec_name, keys] : section_map_) {
        for (auto& [k, idx] : keys) {
            if (idx >= inserted_idx) {
                idx++;
            }
        }
    }
    // Also fix continuation indices
    for (auto& line : lines_) {
        for (auto& ci : line.continuation_indices) {
            if (ci >= inserted_idx) {
                ci++;
            }
        }
    }
    // Now register the new key
    section_map_[section][key] = inserted_idx;
}

bool KlipperConfigParser::has_section(const std::string& section) const {
    return section_map_.find(section) != section_map_.end();
}

std::vector<std::string> KlipperConfigParser::get_sections() const {
    return section_order_;
}

std::vector<std::string>
KlipperConfigParser::get_sections_matching(const std::string& prefix) const {
    std::vector<std::string> result;
    for (const auto& name : section_order_) {
        if (name == prefix ||
            (name.size() > prefix.size() && name.substr(0, prefix.size()) == prefix &&
             name[prefix.size()] == ' ')) {
            result.push_back(name);
        }
    }
    return result;
}

std::vector<std::string> KlipperConfigParser::get_keys(const std::string& section) const {
    auto sec_it = section_map_.find(section);
    if (sec_it == section_map_.end())
        return {};

    // Return keys in order of appearance
    std::vector<std::pair<size_t, std::string>> indexed_keys;
    for (const auto& [key, idx] : sec_it->second) {
        indexed_keys.emplace_back(idx, key);
    }
    std::sort(indexed_keys.begin(), indexed_keys.end());

    std::vector<std::string> result;
    result.reserve(indexed_keys.size());
    for (const auto& [idx, key] : indexed_keys) {
        result.push_back(key);
    }
    return result;
}

std::string KlipperConfigParser::serialize() const {
    if (lines_.empty())
        return "";

    std::string result;
    for (size_t i = 0; i < lines_.size(); ++i) {
        result += lines_[i].raw;
        result += '\n';
    }
    return result;
}

bool KlipperConfigParser::is_modified() const {
    return modified_;
}
