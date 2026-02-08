// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_file_modifier.h"

#include "app_globals.h"
#include "streaming_policy.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <random>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace helix {
namespace gcode {

// ============================================================================
// GCodeFileModifier implementation
// ============================================================================

void GCodeFileModifier::add_modification(Modification mod) {
    modifications_.push_back(std::move(mod));
}

void GCodeFileModifier::clear_modifications() {
    modifications_.clear();
}

void GCodeFileModifier::sort_modifications() {
    // Sort by line number descending (process from end to start)
    // This preserves line numbers for earlier modifications
    std::sort(
        modifications_.begin(), modifications_.end(),
        [](const Modification& a, const Modification& b) { return a.line_number > b.line_number; });
}

std::string GCodeFileModifier::comment_out_line(const std::string& line,
                                                const std::string& reason) {
    std::string result = "; ";
    result += line;
    if (!reason.empty()) {
        result += "  ; [HelixScreen: ";
        result += reason;
        result += "]";
    }
    return result;
}

void GCodeFileModifier::apply_single_modification(std::vector<std::string>& lines,
                                                  const Modification& mod,
                                                  ModificationResult& result) {
    // Line numbers are 1-indexed, vector is 0-indexed
    size_t idx = mod.line_number - 1;

    if (idx >= lines.size()) {
        spdlog::warn("[GCodeFileModifier] Line {} out of range (file has {} lines)",
                     mod.line_number, lines.size());
        return;
    }

    size_t end_idx = (mod.end_line_number > 0) ? mod.end_line_number - 1 : idx;
    if (end_idx >= lines.size()) {
        end_idx = lines.size() - 1;
    }

    switch (mod.type) {
    case ModificationType::COMMENT_OUT: {
        // Comment out from idx to end_idx (inclusive)
        for (size_t i = idx; i <= end_idx; ++i) {
            // Skip if already a comment
            if (!lines[i].empty() && lines[i][0] == ';') {
                continue;
            }
            lines[i] = comment_out_line(lines[i], mod.comment);
            result.lines_modified++;
        }
        spdlog::debug("[GCodeFileModifier] Commented out lines {}-{}", mod.line_number,
                      end_idx + 1);
        break;
    }

    case ModificationType::DELETE: {
        // Delete from idx to end_idx (inclusive)
        size_t count = end_idx - idx + 1;
        lines.erase(lines.begin() + static_cast<long>(idx),
                    lines.begin() + static_cast<long>(end_idx + 1));
        result.lines_removed += count;
        spdlog::debug("[GCodeFileModifier] Deleted {} lines starting at {}", count,
                      mod.line_number);
        break;
    }

    case ModificationType::INJECT_BEFORE: {
        // Split the gcode to inject into lines
        std::vector<std::string> new_lines;
        std::istringstream ss(mod.gcode);
        std::string line;
        while (std::getline(ss, line)) {
            new_lines.push_back(line);
        }

        // Insert before idx
        lines.insert(lines.begin() + static_cast<long>(idx), new_lines.begin(), new_lines.end());
        result.lines_added += new_lines.size();
        spdlog::debug("[GCodeFileModifier] Injected {} lines before line {}", new_lines.size(),
                      mod.line_number);
        break;
    }

    case ModificationType::INJECT_AFTER: {
        // Split the gcode to inject into lines
        std::vector<std::string> new_lines;
        std::istringstream ss(mod.gcode);
        std::string line;
        while (std::getline(ss, line)) {
            new_lines.push_back(line);
        }

        // Insert after idx (at idx+1)
        lines.insert(lines.begin() + static_cast<long>(idx + 1), new_lines.begin(),
                     new_lines.end());
        result.lines_added += new_lines.size();
        spdlog::debug("[GCodeFileModifier] Injected {} lines after line {}", new_lines.size(),
                      mod.line_number);
        break;
    }

    case ModificationType::REPLACE: {
        // Replace lines from idx to end_idx with new gcode
        size_t count = end_idx - idx + 1;

        // Split the replacement gcode into lines
        std::vector<std::string> new_lines;
        std::istringstream ss(mod.gcode);
        std::string line;
        while (std::getline(ss, line)) {
            new_lines.push_back(line);
        }

        // Erase old lines
        lines.erase(lines.begin() + static_cast<long>(idx),
                    lines.begin() + static_cast<long>(end_idx + 1));

        // Insert new lines
        lines.insert(lines.begin() + static_cast<long>(idx), new_lines.begin(), new_lines.end());

        result.lines_removed += count;
        result.lines_added += new_lines.size();
        result.lines_modified++;
        spdlog::debug("[GCodeFileModifier] Replaced {} lines at {} with {} lines", count,
                      mod.line_number, new_lines.size());
        break;
    }
    }
}

std::string GCodeFileModifier::apply_to_content(const std::string& content) {
    if (modifications_.empty()) {
        return content;
    }

    // Split content into lines
    std::vector<std::string> lines;
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    // Sort modifications by line number (descending)
    sort_modifications();

    // Apply each modification
    ModificationResult result;
    for (const auto& mod : modifications_) {
        apply_single_modification(lines, mod, result);
    }

    // Join lines back together
    std::ostringstream out;
    for (size_t i = 0; i < lines.size(); ++i) {
        out << lines[i];
        if (i + 1 < lines.size()) {
            out << '\n';
        }
    }

    return out.str();
}

ModificationResult GCodeFileModifier::apply(const std::filesystem::path& filepath) {
    // Check file size to decide between buffered and streaming modes
    std::error_code ec;
    auto file_size = std::filesystem::file_size(filepath, ec);
    if (ec) {
        ModificationResult result;
        result.success = false;
        result.error_message = "Failed to get file size: " + filepath.string();
        spdlog::error("[GCodeFileModifier] {}", result.error_message);
        return result;
    }

    // Use centralized policy for streaming decisions
    // This ensures consistent threshold behavior across all file operations
    if (helix::StreamingPolicy::instance().should_stream(file_size)) {
        spdlog::info("[GCodeFileModifier] File {} ({} MB) - streaming mode (threshold={}MB)",
                     filepath.filename().string(), file_size / (1024 * 1024),
                     helix::StreamingPolicy::instance().get_threshold_bytes() / (1024 * 1024));
        return apply_streaming(filepath);
    }

    return apply_buffered(filepath);
}

ModificationResult GCodeFileModifier::apply_buffered(const std::filesystem::path& filepath) {
    ModificationResult result;

    // Read original file
    std::ifstream infile(filepath);
    if (!infile.is_open()) {
        result.success = false;
        result.error_message = "Failed to open file: " + filepath.string();
        spdlog::error("[GCodeFileModifier] {}", result.error_message);
        return result;
    }

    // Read all lines
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(infile, line)) {
        lines.push_back(line);
        result.original_size += line.size() + 1; // +1 for newline
    }
    infile.close();

    spdlog::info("[GCodeFileModifier] Loaded {} lines ({} bytes) from {}", lines.size(),
                 result.original_size, filepath.filename().string());

    if (modifications_.empty()) {
        // No modifications - just copy to temp
        result.success = true;
        result.modified_path = generate_temp_path(filepath);

        std::ofstream outfile(result.modified_path);
        if (!outfile.is_open()) {
            result.success = false;
            result.error_message = "Failed to create temp file: " + result.modified_path;
            return result;
        }

        for (size_t i = 0; i < lines.size(); ++i) {
            outfile << lines[i];
            if (i + 1 < lines.size()) {
                outfile << '\n';
            }
        }
        result.modified_size = result.original_size;
        return result;
    }

    // Sort modifications by line number (descending)
    sort_modifications();

    spdlog::info("[GCodeFileModifier] Applying {} modifications (buffered mode)",
                 modifications_.size());

    // Apply each modification
    for (const auto& mod : modifications_) {
        apply_single_modification(lines, mod, result);
    }

    // Generate temp file path
    result.modified_path = generate_temp_path(filepath);

    // Write modified file
    std::ofstream outfile(result.modified_path);
    if (!outfile.is_open()) {
        result.success = false;
        result.error_message = "Failed to create temp file: " + result.modified_path;
        spdlog::error("[GCodeFileModifier] {}", result.error_message);
        return result;
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        outfile << lines[i];
        result.modified_size += lines[i].size();
        if (i + 1 < lines.size()) {
            outfile << '\n';
            result.modified_size++;
        }
    }
    outfile.close();

    result.success = true;
    spdlog::info("[GCodeFileModifier] Created modified file: {} ({} bytes, +{} -{} lines changed)",
                 result.modified_path, result.modified_size, result.lines_added,
                 result.lines_removed);

    return result;
}

std::unordered_map<size_t, Modification> GCodeFileModifier::build_streaming_lookup() const {
    std::unordered_map<size_t, Modification> lookup;

    for (const auto& mod : modifications_) {
        // For range modifications, we create entries for each line but note
        // that streaming mode has limitations with multi-line operations
        size_t end_line = (mod.end_line_number > 0) ? mod.end_line_number : mod.line_number;

        if (mod.end_line_number > 0 && mod.type != ModificationType::COMMENT_OUT &&
            mod.type != ModificationType::DELETE) {
            spdlog::warn("[GCodeFileModifier] Streaming mode: multi-line {} not fully supported, "
                         "processing line {} only",
                         static_cast<int>(mod.type), mod.line_number);
            end_line = mod.line_number;
        }

        for (size_t line = mod.line_number; line <= end_line; ++line) {
            // Create a modified copy for each line in the range
            Modification line_mod = mod;
            line_mod.line_number = line;
            line_mod.end_line_number = 0; // Reset to single-line for streaming
            lookup[line] = line_mod;
        }
    }

    return lookup;
}

ModificationResult GCodeFileModifier::apply_streaming(const std::filesystem::path& filepath) {
    ModificationResult result;

    // Open input file
    std::ifstream infile(filepath);
    if (!infile.is_open()) {
        result.success = false;
        result.error_message = "Failed to open file: " + filepath.string();
        spdlog::error("[GCodeFileModifier] {}", result.error_message);
        return result;
    }

    // Generate output path
    result.modified_path = generate_temp_path(filepath);

    // Open output file
    std::ofstream outfile(result.modified_path);
    if (!outfile.is_open()) {
        result.success = false;
        result.error_message = "Failed to create temp file: " + result.modified_path;
        spdlog::error("[GCodeFileModifier] {}", result.error_message);
        return result;
    }

    // Build lookup map for O(1) per-line checks
    auto lookup = build_streaming_lookup();

    spdlog::info("[GCodeFileModifier] Processing file in streaming mode ({} modifications)",
                 modifications_.size());

    // Process line by line
    std::string line;
    size_t line_number = 0;
    bool first_line = true;

    while (std::getline(infile, line)) {
        line_number++;
        result.original_size += line.size() + 1;

        // Check if this line has a modification
        auto it = lookup.find(line_number);
        if (it != lookup.end()) {
            const auto& mod = it->second;

            switch (mod.type) {
            case ModificationType::COMMENT_OUT: {
                // Skip if already a comment
                if (!line.empty() && line[0] == ';') {
                    if (!first_line)
                        outfile << '\n';
                    outfile << line;
                } else {
                    std::string commented = comment_out_line(line, mod.comment);
                    if (!first_line)
                        outfile << '\n';
                    outfile << commented;
                    result.lines_modified++;
                }
                result.modified_size += line.size() + (first_line ? 0 : 1);
                first_line = false;
                break;
            }

            case ModificationType::DELETE: {
                // Simply skip the line (don't write it)
                result.lines_removed++;
                // Note: we don't add to modified_size for deleted lines
                break;
            }

            case ModificationType::INJECT_BEFORE: {
                // Write injected content first
                std::istringstream ss(mod.gcode);
                std::string inject_line;
                while (std::getline(ss, inject_line)) {
                    if (!first_line)
                        outfile << '\n';
                    outfile << inject_line;
                    result.modified_size += inject_line.size() + (first_line ? 0 : 1);
                    result.lines_added++;
                    first_line = false;
                }
                // Then write the original line
                if (!first_line)
                    outfile << '\n';
                outfile << line;
                result.modified_size += line.size() + 1;
                first_line = false;
                break;
            }

            case ModificationType::INJECT_AFTER: {
                // Write original line first
                if (!first_line)
                    outfile << '\n';
                outfile << line;
                result.modified_size += line.size() + (first_line ? 0 : 1);
                first_line = false;
                // Then write injected content
                std::istringstream ss(mod.gcode);
                std::string inject_line;
                while (std::getline(ss, inject_line)) {
                    outfile << '\n';
                    outfile << inject_line;
                    result.modified_size += inject_line.size() + 1;
                    result.lines_added++;
                }
                break;
            }

            case ModificationType::REPLACE: {
                // Write replacement content instead of original
                std::istringstream ss(mod.gcode);
                std::string replace_line;
                bool first_replace = true;
                while (std::getline(ss, replace_line)) {
                    if (!first_line && !first_replace)
                        outfile << '\n';
                    if (!first_line && first_replace)
                        outfile << '\n';
                    outfile << replace_line;
                    result.modified_size += replace_line.size() + (first_line ? 0 : 1);
                    first_replace = false;
                    first_line = false;
                }
                result.lines_modified++;
                break;
            }
            }
        } else {
            // No modification - write line as-is
            if (!first_line)
                outfile << '\n';
            outfile << line;
            result.modified_size += line.size() + (first_line ? 0 : 1);
            first_line = false;
        }
    }

    infile.close();
    outfile.close();

    result.success = true;
    spdlog::info("[GCodeFileModifier] Streaming complete: {} ({} bytes, +{} -{} lines)",
                 result.modified_path, result.modified_size, result.lines_added,
                 result.lines_removed);

    return result;
}

bool GCodeFileModifier::disable_operation(const DetectedOperation& op) {
    switch (op.embedding) {
    case OperationEmbedding::DIRECT_COMMAND:
    case OperationEmbedding::MACRO_CALL:
        // Comment out the line containing the operation
        add_modification(
            Modification::comment_out(op.line_number, "Disabled " + op.display_name()));
        spdlog::debug("[GCodeFileModifier] Will disable {} at line {}", op.display_name(),
                      op.line_number);
        return true;

    case OperationEmbedding::MACRO_PARAMETER:
        // Need to modify the parameter, not comment out the whole line
        return disable_macro_parameter(op);

    case OperationEmbedding::NOT_FOUND:
        // Nothing to disable
        return false;
    }

    return false;
}

bool GCodeFileModifier::disable_macro_parameter(const DetectedOperation& op) {
    if (op.embedding != OperationEmbedding::MACRO_PARAMETER) {
        return false;
    }

    if (op.param_name.empty() || op.raw_line.empty()) {
        spdlog::warn("[GCodeFileModifier] Cannot disable macro parameter: missing param name or "
                     "raw line");
        return false;
    }

    // Build regex pattern to find PARAM_NAME=value (case-insensitive)
    // Replace the value with 0 or FALSE
    std::string pattern = op.param_name + R"(=\S+)";
    std::regex re(pattern, std::regex_constants::icase);

    // Determine replacement value
    std::string replacement = op.param_name + "=0";

    // Check if original value was boolean-like
    std::string upper_value = op.param_value;
    std::transform(upper_value.begin(), upper_value.end(), upper_value.begin(), ::toupper);
    if (upper_value == "TRUE" || upper_value == "YES") {
        replacement = op.param_name + "=FALSE";
    }

    // Build the modified line
    std::string modified_line = std::regex_replace(op.raw_line, re, replacement);

    // Add a replacement modification
    add_modification(
        Modification::replace(op.line_number, modified_line, "Disabled " + op.param_name));

    spdlog::debug("[GCodeFileModifier] Will replace {} param at line {} with value 0/FALSE",
                  op.param_name, op.line_number);

    return true;
}

void GCodeFileModifier::disable_operations(const ScanResult& scan_result,
                                           const std::vector<OperationType>& types_to_disable) {
    for (OperationType type : types_to_disable) {
        auto ops = scan_result.get_operations(type);
        for (const auto& op : ops) {
            disable_operation(op);
        }
    }
}

bool GCodeFileModifier::add_print_start_skip_params(
    const ScanResult& scan_result,
    const std::vector<std::pair<std::string, std::string>>& skip_params) {
    if (!scan_result.print_start.found || skip_params.empty()) {
        spdlog::debug("[GCodeFileModifier] Cannot add skip params: PRINT_START {} found, {} params",
                      scan_result.print_start.found ? "" : "NOT", skip_params.size());
        return false;
    }

    // Build the modified line with skip params appended
    std::string modified_line = scan_result.print_start.with_skip_params(skip_params);

    // Create a REPLACE modification for the PRINT_START line
    add_modification(Modification::replace(scan_result.print_start.line_number, modified_line,
                                           "HelixScreen: Added skip parameters"));

    spdlog::info("[GCodeFileModifier] Adding skip params to {} at line {}: {}",
                 scan_result.print_start.macro_name, scan_result.print_start.line_number,
                 modified_line.substr(0, 80));

    return true;
}

std::string GCodeFileModifier::generate_temp_path(const std::filesystem::path& original_path) {
    // Generate unique temp file path in persistent cache directory
    // Format: <cache_dir>/mod_XXXXXX_filename.gcode

    std::string cache_dir = get_helix_cache_dir("gcode_mod");
    if (cache_dir.empty()) {
        spdlog::error("[GCodeFileModifier] No writable cache directory");
        return "";
    }

    std::string filename = original_path.filename().string();

    // Generate random suffix
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);
    int suffix = dis(gen);

    std::ostringstream path;
    path << cache_dir << "/mod_" << suffix << "_" << filename;

    return path.str();
}

size_t GCodeFileModifier::cleanup_temp_files(int max_age_seconds) {
    size_t deleted = 0;

    std::string cache_dir = get_helix_cache_dir("gcode_mod");
    if (cache_dir.empty()) {
        return 0;
    }

    try {
        auto now = std::chrono::system_clock::now();

        for (const auto& entry : std::filesystem::directory_iterator(cache_dir)) {
            if (!std::filesystem::is_regular_file(entry.path())) {
                continue;
            }

            std::string name = entry.path().filename().string();
            if (name.rfind("mod_", 0) != 0) {
                continue; // Not our file
            }

            // Check file age
            auto ftime = std::filesystem::last_write_time(entry.path());
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() +
                std::chrono::system_clock::now());

            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - sctp).count();

            if (age > max_age_seconds) {
                std::filesystem::remove(entry.path());
                deleted++;
                spdlog::debug("[GCodeFileModifier] Cleaned up old temp file: {}", name);
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("[GCodeFileModifier] Error cleaning up temp files: {}", e.what());
    }

    if (deleted > 0) {
        spdlog::info("[GCodeFileModifier] Cleaned up {} temp files", deleted);
    }

    return deleted;
}

} // namespace gcode
} // namespace helix
