// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "print_start_analyzer.h"

#include "moonraker_api.h"
#include "moonraker_types.h"
#include "operation_patterns.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <memory>
#include <regex>
#include <sstream>

namespace helix {

// ============================================================================
// Category Mapping (shared OperationCategory <-> PrintStartOpCategory)
// ============================================================================

namespace {

/**
 * @brief Map shared OperationCategory to PrintStartOpCategory
 */
PrintStartOpCategory to_print_start_category(OperationCategory cat) {
    switch (cat) {
    case OperationCategory::BED_LEVELING:
        return PrintStartOpCategory::BED_LEVELING;
    case OperationCategory::QGL:
        return PrintStartOpCategory::QGL;
    case OperationCategory::Z_TILT:
        return PrintStartOpCategory::Z_TILT;
    case OperationCategory::NOZZLE_CLEAN:
        return PrintStartOpCategory::NOZZLE_CLEAN;
    case OperationCategory::PURGE_LINE:
        return PrintStartOpCategory::PRIMING;
    case OperationCategory::SKEW_CORRECT:
        return PrintStartOpCategory::SKEW_CORRECT;
    case OperationCategory::HOMING:
        return PrintStartOpCategory::HOMING;
    case OperationCategory::CHAMBER_SOAK:
        return PrintStartOpCategory::CHAMBER_SOAK;
    default:
        return PrintStartOpCategory::UNKNOWN;
    }
}

/**
 * @brief Map PrintStartOpCategory to shared OperationCategory
 */
OperationCategory to_operation_category(PrintStartOpCategory cat) {
    switch (cat) {
    case PrintStartOpCategory::BED_LEVELING:
        return OperationCategory::BED_LEVELING;
    case PrintStartOpCategory::QGL:
        return OperationCategory::QGL;
    case PrintStartOpCategory::Z_TILT:
        return OperationCategory::Z_TILT;
    case PrintStartOpCategory::NOZZLE_CLEAN:
        return OperationCategory::NOZZLE_CLEAN;
    case PrintStartOpCategory::PRIMING:
        return OperationCategory::PURGE_LINE;
    case PrintStartOpCategory::SKEW_CORRECT:
        return OperationCategory::SKEW_CORRECT;
    case PrintStartOpCategory::HOMING:
        return OperationCategory::HOMING;
    case PrintStartOpCategory::CHAMBER_SOAK:
        return OperationCategory::CHAMBER_SOAK;
    default:
        return OperationCategory::UNKNOWN;
    }
}

} // anonymous namespace

// ============================================================================
// Category Helpers
// ============================================================================

const char* category_to_string(PrintStartOpCategory category) {
    switch (category) {
    case PrintStartOpCategory::BED_LEVELING:
        return "bed_leveling";
    case PrintStartOpCategory::QGL:
        return "qgl";
    case PrintStartOpCategory::Z_TILT:
        return "z_tilt";
    case PrintStartOpCategory::NOZZLE_CLEAN:
        return "nozzle_clean";
    case PrintStartOpCategory::PRIMING:
        return "priming";
    case PrintStartOpCategory::SKEW_CORRECT:
        return "skew_correct";
    case PrintStartOpCategory::HOMING:
        return "homing";
    case PrintStartOpCategory::CHAMBER_SOAK:
        return "chamber_soak";
    case PrintStartOpCategory::UNKNOWN:
    default:
        return "unknown";
    }
}

// ============================================================================
// PrintStartAnalysis Methods
// ============================================================================

bool PrintStartAnalysis::has_operation(PrintStartOpCategory category) const {
    return std::any_of(
        operations.begin(), operations.end(),
        [category](const PrintStartOperation& op) { return op.category == category; });
}

const PrintStartOperation* PrintStartAnalysis::get_operation(PrintStartOpCategory category) const {
    auto it =
        std::find_if(operations.begin(), operations.end(),
                     [category](const PrintStartOperation& op) { return op.category == category; });
    return (it != operations.end()) ? &(*it) : nullptr;
}

std::vector<const PrintStartOperation*> PrintStartAnalysis::get_uncontrollable_operations() const {
    std::vector<const PrintStartOperation*> result;
    for (const auto& op : operations) {
        if (!op.has_skip_param && op.category != PrintStartOpCategory::HOMING) {
            result.push_back(&op);
        }
    }
    return result;
}

std::string PrintStartAnalysis::summary() const {
    if (!found) {
        return "No print start macro found";
    }

    std::ostringstream ss;
    ss << macro_name << ": " << total_ops_count << " operations detected";
    if (controllable_count > 0) {
        ss << " (" << controllable_count << " controllable)";
    }

    if (!operations.empty()) {
        ss << " [";
        bool first = true;
        for (const auto& op : operations) {
            if (!first)
                ss << ", ";
            first = false;
            ss << op.name;
            if (op.has_skip_param) {
                ss << "(skip:" << op.skip_param_name << ")";
            }
        }
        ss << "]";
    }

    return ss.str();
}

// ============================================================================
// Operation Detection Patterns - Now using shared operation_patterns.h
// ============================================================================
// All patterns are defined in operation_patterns.h and accessed via
// OPERATION_KEYWORDS[] and get_skip_variations()

// ============================================================================
// PrintStartAnalyzer Implementation
// ============================================================================

namespace {

/**
 * @brief Helper struct to hold async search state across callbacks
 */
struct ConfigFileSearchState {
    MoonrakerAPI* api;
    std::vector<std::string> cfg_files;
    size_t current_index = 0;
    PrintStartAnalyzer::AnalysisCallback on_complete;
    PrintStartAnalyzer::ErrorCallback on_error;
};

/**
 * @brief Case-insensitive string search
 */
bool contains_ci(const std::string& haystack, const std::string& needle) {
    std::string hay_lower = haystack;
    std::string needle_lower = needle;
    std::transform(hay_lower.begin(), hay_lower.end(), hay_lower.begin(), ::tolower);
    std::transform(needle_lower.begin(), needle_lower.end(), needle_lower.begin(), ::tolower);
    return hay_lower.find(needle_lower) != std::string::npos;
}

/**
 * @brief Extract gcode content from a macro section in config file text
 */
std::string extract_gcode_from_section(const std::string& content, const std::string& section_start,
                                       size_t section_pos) {
    // Find the gcode: line
    std::string content_lower = content;
    std::transform(content_lower.begin(), content_lower.end(), content_lower.begin(), ::tolower);

    size_t gcode_pos = content_lower.find("gcode:", section_pos);
    if (gcode_pos == std::string::npos) {
        return "";
    }

    // Find end of this section (next [section] or EOF)
    size_t section_end = content.find("\n[", section_pos + section_start.size());
    if (section_end == std::string::npos) {
        section_end = content.size();
    }

    // Make sure gcode: is within this section
    if (gcode_pos >= section_end) {
        return "";
    }

    // Find start of gcode content (after "gcode:" and newline)
    size_t gcode_content_start = content.find('\n', gcode_pos);
    if (gcode_content_start == std::string::npos || gcode_content_start >= section_end) {
        return "";
    }
    gcode_content_start++; // Skip the newline

    return content.substr(gcode_content_start, section_end - gcode_content_start);
}

/**
 * @brief Recursively search config files for macro definition
 */
void search_next_file(std::shared_ptr<ConfigFileSearchState> state);

void search_next_file(std::shared_ptr<ConfigFileSearchState> state) {
    if (state->current_index >= state->cfg_files.size()) {
        // Searched all files, macro not found
        spdlog::info("[PrintStartAnalyzer] No PRINT_START macro found in any config file");
        PrintStartAnalysis result;
        result.found = false;
        if (state->on_complete) {
            state->on_complete(result);
        }
        return;
    }

    const std::string& filename = state->cfg_files[state->current_index];
    spdlog::debug("[PrintStartAnalyzer] Searching {} for macro...", filename);

    state->api->download_file(
        "config", filename,
        [state, filename](const std::string& content) {
            // Search for each macro name variant
            for (size_t i = 0; i < PrintStartAnalyzer::MACRO_NAMES_COUNT; ++i) {
                std::string section =
                    "[gcode_macro " + std::string(PrintStartAnalyzer::MACRO_NAMES[i]) + "]";

                if (contains_ci(content, section)) {
                    // Found the macro in this file!
                    std::string content_lower = content;
                    std::transform(content_lower.begin(), content_lower.end(),
                                   content_lower.begin(), ::tolower);
                    std::string section_lower = section;
                    std::transform(section_lower.begin(), section_lower.end(),
                                   section_lower.begin(), ::tolower);

                    size_t section_pos = content_lower.find(section_lower);
                    std::string gcode = extract_gcode_from_section(content, section, section_pos);

                    if (!gcode.empty()) {
                        spdlog::info("[PrintStartAnalyzer] Found macro '{}' in {} ({} chars)",
                                     PrintStartAnalyzer::MACRO_NAMES[i], filename, gcode.size());

                        PrintStartAnalysis result = PrintStartAnalyzer::parse_macro(
                            PrintStartAnalyzer::MACRO_NAMES[i], gcode);
                        result.found = true;
                        result.macro_name = PrintStartAnalyzer::MACRO_NAMES[i];
                        result.source_file = filename;

                        if (state->on_complete) {
                            state->on_complete(result);
                        }
                        return;
                    }
                }
            }

            // Not in this file, try next
            state->current_index++;
            search_next_file(state);
        },
        [state](const MoonrakerError& /* err */) {
            // Skip this file on download error, try next
            spdlog::debug("[PrintStartAnalyzer] Failed to download {}, skipping",
                          state->cfg_files[state->current_index]);
            state->current_index++;
            search_next_file(state);
        });
}

} // anonymous namespace

void PrintStartAnalyzer::analyze(MoonrakerAPI* api, AnalysisCallback on_complete,
                                 ErrorCallback on_error) {
    if (!api) {
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "API not initialized";
            on_error(err);
        }
        return;
    }

    spdlog::debug("[PrintStartAnalyzer] Listing config files to find macro location...");

    // List all files in config directory to find which one contains the macro
    api->list_files(
        "config", "", false,
        [api, on_complete, on_error](const std::vector<FileInfo>& files) {
            // Filter to .cfg files only
            std::vector<std::string> cfg_files;
            for (const auto& f : files) {
                if (!f.is_dir && f.filename.size() > 4 &&
                    f.filename.substr(f.filename.size() - 4) == ".cfg") {
                    cfg_files.push_back(f.filename);
                }
            }

            if (cfg_files.empty()) {
                spdlog::warn("[PrintStartAnalyzer] No .cfg files found in config directory");
                PrintStartAnalysis result;
                result.found = false;
                if (on_complete) {
                    on_complete(result);
                }
                return;
            }

            spdlog::debug("[PrintStartAnalyzer] Found {} config files to search", cfg_files.size());

            // Create shared state for async search
            auto state = std::make_shared<ConfigFileSearchState>();
            state->api = api;
            state->cfg_files = std::move(cfg_files);
            state->on_complete = on_complete;
            state->on_error = on_error;

            // Start searching files
            search_next_file(state);
        },
        on_error);
}

PrintStartAnalysis PrintStartAnalyzer::parse_macro(const std::string& macro_name,
                                                   const std::string& gcode) {
    PrintStartAnalysis result;
    result.found = true;
    result.macro_name = macro_name;
    result.raw_gcode = gcode;

    // Detect operations
    result.operations = detect_operations(gcode);
    result.total_ops_count = result.operations.size();

    // Check each operation for skip conditionals
    for (auto& op : result.operations) {
        std::string param_name;
        if (detect_skip_conditional(gcode, op.name, param_name)) {
            op.has_skip_param = true;
            op.skip_param_name = param_name;
            result.controllable_count++;
        }
    }

    result.is_controllable = (result.controllable_count > 0);

    // Extract known parameters
    result.known_params = extract_parameters(gcode);

    spdlog::debug("[PrintStartAnalyzer] Parsed {}: {} ops, {} controllable, {} params", macro_name,
                  result.total_ops_count, result.controllable_count, result.known_params.size());

    return result;
}

std::string PrintStartAnalyzer::get_suggested_skip_param(const std::string& op_name) {
    // Use shared pattern registry
    const auto* kw = find_keyword(op_name);
    if (kw) {
        return kw->skip_param;
    }
    // Default: SKIP_ + operation name
    return "SKIP_" + op_name;
}

PrintStartOpCategory PrintStartAnalyzer::categorize_operation(const std::string& command) {
    // Extract just the command name (before any parameters)
    std::string cmd = command;
    auto space_pos = cmd.find(' ');
    if (space_pos != std::string::npos) {
        cmd = cmd.substr(0, space_pos);
    }

    // Use shared pattern registry
    const auto* kw = find_keyword(cmd);
    if (kw) {
        return to_print_start_category(kw->category);
    }

    return PrintStartOpCategory::UNKNOWN;
}

// ============================================================================
// Parsing Helpers
// ============================================================================

std::vector<PrintStartOperation> PrintStartAnalyzer::detect_operations(const std::string& gcode) {
    std::vector<PrintStartOperation> operations;

    // Split into lines and process each
    std::istringstream stream(gcode);
    std::string line;
    size_t line_num = 0;

    while (std::getline(stream, line)) {
        ++line_num;

        // Skip empty lines and comments
        auto first_non_space = line.find_first_not_of(" \t");
        if (first_non_space == std::string::npos)
            continue;
        if (line[first_non_space] == '#' || line[first_non_space] == ';')
            continue;

        // Skip Jinja2 control statements ({% ... %})
        if (line.find("{%") != std::string::npos)
            continue;

        // Extract the command (first word on the line, excluding Jinja2 expressions)
        std::string trimmed = line.substr(first_non_space);

        // Skip lines that are just Jinja2 expressions
        if (trimmed[0] == '{')
            continue;

        // Get the command name
        auto end_of_cmd = trimmed.find_first_of(" \t{");
        std::string cmd =
            (end_of_cmd != std::string::npos) ? trimmed.substr(0, end_of_cmd) : trimmed;

        // Check against shared pattern registry
        const auto* kw = find_keyword(cmd);
        if (kw) {
            PrintStartOperation op;
            op.name = kw->keyword;
            op.category = to_print_start_category(kw->category);
            op.line_number = line_num;

            // Avoid duplicates (same operation appearing multiple times)
            bool duplicate = std::any_of(
                operations.begin(), operations.end(),
                [&op](const PrintStartOperation& existing) { return existing.name == op.name; });

            if (!duplicate) {
                operations.push_back(op);
                spdlog::trace("[PrintStartAnalyzer] Detected {} at line {}", op.name, line_num);
            }
        }
    }

    return operations;
}

bool PrintStartAnalyzer::detect_skip_conditional(const std::string& gcode,
                                                 const std::string& op_name,
                                                 std::string& out_param_name) {
    // Get the category to know which skip param variations to look for
    PrintStartOpCategory category = categorize_operation(op_name);
    if (category == PrintStartOpCategory::UNKNOWN) {
        return false;
    }

    // Get skip param variations from shared registry
    OperationCategory shared_cat = to_operation_category(category);
    const auto& variations = get_skip_variations(shared_cat);
    if (variations.empty()) {
        return false;
    }

    // Look for patterns like:
    //   {% if SKIP_BED_MESH == 0 %}
    //   {% if params.SKIP_BED_MESH|default(0)|int == 0 %}
    //   {% if not SKIP_BED_MESH %}
    //
    // We check if any skip parameter variation appears near the operation

    // First, find the operation in the gcode
    auto op_pos = gcode.find(op_name);
    if (op_pos == std::string::npos) {
        return false;
    }

    // Look backwards from the operation for an {% if ... %} block
    // Search up to 500 characters before the operation
    size_t search_start = (op_pos > 500) ? op_pos - 500 : 0;
    std::string context = gcode.substr(search_start, op_pos - search_start);

    // Check for each skip param variation
    for (const auto& param : variations) {
        // Case-insensitive search
        std::string param_lower = param;
        std::transform(param_lower.begin(), param_lower.end(), param_lower.begin(), ::tolower);

        std::string context_lower = context;
        std::transform(context_lower.begin(), context_lower.end(), context_lower.begin(),
                       ::tolower);

        // Look for the param in an if statement
        if (context_lower.find(param_lower) != std::string::npos) {
            // Verify it's in an if statement context
            // Look for patterns like: {% if ... param ...
            std::regex if_pattern(R"(\{%\s*if\s+.*)" + param_lower + R"(.*%\})", std::regex::icase);
            if (std::regex_search(context, if_pattern)) {
                out_param_name = param;
                spdlog::trace("[PrintStartAnalyzer] {} is controlled by {}", op_name, param);
                return true;
            }

            // Also check for variable assignment: {% set X = params.SKIP_...
            std::regex set_pattern(R"(\{%\s*set\s+\w+\s*=\s*params\.)" + param_lower,
                                   std::regex::icase);
            if (std::regex_search(context, set_pattern)) {
                out_param_name = param;
                spdlog::trace("[PrintStartAnalyzer] {} is controlled by params.{}", op_name, param);
                return true;
            }
        }
    }

    return false;
}

std::vector<std::string> PrintStartAnalyzer::extract_parameters(const std::string& gcode) {
    std::vector<std::string> params;

    // Look for patterns like:
    //   params.BED
    //   params.EXTRUDER|default(...)
    //   {% set BED = params.BED|default(60) %}

    std::regex params_pattern(R"(params\.([A-Z_][A-Z0-9_]*))", std::regex::icase);

    std::smatch match;
    std::string::const_iterator search_start = gcode.cbegin();

    while (std::regex_search(search_start, gcode.cend(), match, params_pattern)) {
        std::string param = match[1].str();
        // Convert to uppercase
        std::transform(param.begin(), param.end(), param.begin(), ::toupper);

        // Avoid duplicates
        if (std::find(params.begin(), params.end(), param) == params.end()) {
            params.push_back(param);
        }

        search_start = match.suffix().first;
    }

    return params;
}

} // namespace helix
