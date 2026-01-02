// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "macro_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

namespace helix {

// ============================================================================
// File Loading Helpers
// ============================================================================

namespace {

/**
 * @brief Load macro content from config file
 * @return File content, or empty string if not found
 *
 * Tries multiple paths in order:
 * 1. config/helix_macros.cfg (relative to app)
 * 2. /opt/helixscreen/config/helix_macros.cfg (installed location)
 */
std::string load_macro_file() {
    const std::vector<std::string> paths = {
        "config/helix_macros.cfg",                  // Development/relative
        "/opt/helixscreen/config/helix_macros.cfg", // Linux installed
    };

    for (const auto& path : paths) {
        std::ifstream file(path);
        if (file) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            spdlog::debug("[MacroManager] Loaded macro file from {}", path);
            return buffer.str();
        }
    }

    spdlog::warn("[MacroManager] Could not find helix_macros.cfg in any expected location");
    return "";
}

/**
 * @brief Parse version from file header comment
 * @param content File content
 * @return Version string (e.g., "2.0.0"), or empty if not found
 *
 * Looks for pattern: # helix_macros v<version>
 */
std::string parse_file_version(const std::string& content) {
    static const std::regex version_pattern(R"(#\s*helix_macros\s+v(\d+\.\d+\.\d+))");
    std::smatch match;
    if (std::regex_search(content, match, version_pattern)) {
        return match[1].str();
    }
    return "";
}

/**
 * @brief Parse macro names from file content
 * @param content File content
 * @return Vector of macro names
 */
std::vector<std::string> parse_macro_names(const std::string& content) {
    std::vector<std::string> names;
    static const std::regex macro_pattern(R"(\[gcode_macro\s+(\w+)\])");

    auto it = std::sregex_iterator(content.begin(), content.end(), macro_pattern);
    auto end = std::sregex_iterator();

    for (; it != end; ++it) {
        std::string name = (*it)[1].str();
        // Skip internal state macros
        if (name[0] != '_') {
            names.push_back(name);
        }
    }

    return names;
}

} // anonymous namespace

// ============================================================================
// MacroManager Implementation
// ============================================================================

MacroManager::MacroManager(MoonrakerAPI& api, const PrinterCapabilities& capabilities)
    : api_(api), capabilities_(capabilities) {}

bool MacroManager::is_installed() const {
    return capabilities_.has_helix_macros();
}

MacroInstallStatus MacroManager::get_status() const {
    if (!capabilities_.has_helix_macros()) {
        return MacroInstallStatus::NOT_INSTALLED;
    }

    auto installed_version = parse_installed_version();
    if (!installed_version) {
        // Has macros but can't determine version - assume installed
        return MacroInstallStatus::INSTALLED;
    }

    // Compare against version from local file
    std::string local_version = get_version();
    if (local_version.empty()) {
        // Can't read local file - assume installed
        return MacroInstallStatus::INSTALLED;
    }

    if (*installed_version < local_version) {
        return MacroInstallStatus::OUTDATED;
    }

    return MacroInstallStatus::INSTALLED;
}

std::string MacroManager::get_installed_version() const {
    auto version = parse_installed_version();
    return version.value_or("");
}

bool MacroManager::update_available() const {
    return get_status() == MacroInstallStatus::OUTDATED;
}

void MacroManager::install(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[HelixMacroManager] Starting macro installation...");

    // Step 1: Upload macro file
    upload_macro_file(
        [this, on_success, on_error]() {
            spdlog::info("[HelixMacroManager] Macro file uploaded, adding include...");

            // Step 2: Add include to printer.cfg
            add_include_to_config(
                [this, on_success, on_error]() {
                    spdlog::info("[HelixMacroManager] Include added, restarting Klipper...");

                    // Step 3: Restart Klipper
                    restart_klipper(
                        [on_success]() {
                            spdlog::info("[HelixMacroManager] Installation complete!");
                            on_success();
                        },
                        on_error);
                },
                on_error);
        },
        on_error);
}

void MacroManager::update(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[HelixMacroManager] Starting macro update...");

    // Just upload the new file and restart
    upload_macro_file([this, on_success, on_error]() { restart_klipper(on_success, on_error); },
                      on_error);
}

void MacroManager::uninstall(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[HelixMacroManager] Starting macro uninstall...");

    // Step 1: Remove include from printer.cfg
    remove_include_from_config(
        [this, on_success, on_error]() {
            // Step 2: Delete macro file
            delete_macro_file(
                [this, on_success, on_error]() {
                    // Step 3: Restart Klipper
                    restart_klipper(on_success, on_error);
                },
                on_error);
        },
        on_error);
}

std::string MacroManager::get_macro_content() {
    return load_macro_file();
}

std::string MacroManager::get_version() {
    std::string content = load_macro_file();
    return parse_file_version(content);
}

std::vector<std::string> MacroManager::get_macro_names() {
    std::string content = load_macro_file();
    if (content.empty()) {
        return {};
    }
    return parse_macro_names(content);
}

// ============================================================================
// Private Implementation
// ============================================================================

void MacroManager::upload_macro_file(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[HelixMacroManager] Uploading {} to printer config directory",
                 HELIX_MACROS_FILENAME);

    // Get the macro content to upload
    std::string content = get_macro_content();

    spdlog::debug("[HelixMacroManager] Macro content size: {} bytes", content.size());

    // Upload to config root (not gcodes)
    // The path is "" because we upload directly to the config directory
    api_.upload_file_with_name(
        "config", "", HELIX_MACROS_FILENAME, content,
        // Upload success
        [on_success]() {
            spdlog::info("[HelixMacroManager] Successfully uploaded {}", HELIX_MACROS_FILENAME);
            if (on_success) {
                on_success();
            }
        },
        // Upload error
        [on_error](const MoonrakerError& err) {
            spdlog::error("[HelixMacroManager] Failed to upload {}: {}", HELIX_MACROS_FILENAME,
                          err.message);
            if (on_error) {
                on_error(err);
            }
        });
}

void MacroManager::add_include_to_config(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[HelixMacroManager] Adding include line to printer.cfg");

    // Download printer.cfg
    api_.download_file(
        "config", "printer.cfg",
        // Download success
        [this, on_success, on_error](const std::string& content) {
            // Check if include line already exists
            std::string include_line = "[include " + std::string(HELIX_MACROS_FILENAME) + "]";
            if (content.find(include_line) != std::string::npos) {
                spdlog::info("[HelixMacroManager] Include line already present in printer.cfg");
                if (on_success) {
                    on_success();
                }
                return;
            }

            // Find the best place to insert the include line
            // Strategy: Insert after the last existing [include ...] line, or at the very top
            std::string modified_content;
            std::istringstream input(content);
            std::string line;
            size_t last_include_end = 0;
            size_t current_pos = 0;

            // First pass: find the position after the last [include] line
            while (std::getline(input, line)) {
                current_pos += line.length() + 1; // +1 for newline
                // Check for [include ...] pattern (case-insensitive for robustness)
                std::string lower_line = line;
                std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (lower_line.find("[include ") == 0 || lower_line.find("[include\t") == 0) {
                    last_include_end = current_pos;
                }
            }

            // Second pass: insert at the right position
            if (last_include_end > 0) {
                // Insert after last include line
                modified_content = content.substr(0, last_include_end) + include_line + "\n" +
                                   content.substr(last_include_end);
                spdlog::debug("[HelixMacroManager] Inserted after existing includes at pos {}",
                              last_include_end);
            } else {
                // No existing includes - add at the very beginning
                modified_content = include_line + "\n" + content;
                spdlog::debug("[HelixMacroManager] Inserted at beginning of file");
            }

            // Upload modified printer.cfg
            api_.upload_file_with_name(
                "config", "", "printer.cfg", modified_content,
                // Upload success
                [on_success]() {
                    spdlog::info("[HelixMacroManager] Successfully added include to printer.cfg");
                    if (on_success) {
                        on_success();
                    }
                },
                // Upload error
                [on_error](const MoonrakerError& err) {
                    spdlog::error("[HelixMacroManager] Failed to upload modified printer.cfg: {}",
                                  err.message);
                    if (on_error) {
                        on_error(err);
                    }
                });
        },
        // Download error
        [on_error](const MoonrakerError& err) {
            spdlog::error("[HelixMacroManager] Failed to download printer.cfg: {}", err.message);
            if (on_error) {
                on_error(err);
            }
        });
}

void MacroManager::remove_include_from_config(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[HelixMacroManager] Removing include line from printer.cfg");

    // Download printer.cfg
    api_.download_file(
        "config", "printer.cfg",
        // Download success
        [this, on_success, on_error](const std::string& content) {
            std::string include_line = "[include " + std::string(HELIX_MACROS_FILENAME) + "]";

            // Check if include line exists
            size_t pos = content.find(include_line);
            if (pos == std::string::npos) {
                spdlog::info("[HelixMacroManager] Include line not found in printer.cfg");
                if (on_success) {
                    on_success();
                }
                return;
            }

            // Find the full line to remove (including newline)
            size_t line_start = pos;
            size_t line_end = content.find('\n', pos);
            if (line_end == std::string::npos) {
                line_end = content.length();
            } else {
                line_end++; // Include the newline
            }

            // Build modified content without the include line
            std::string modified_content = content.substr(0, line_start) + content.substr(line_end);

            spdlog::debug("[HelixMacroManager] Removed include line at pos {}-{}", line_start,
                          line_end);

            // Upload modified printer.cfg
            api_.upload_file_with_name(
                "config", "", "printer.cfg", modified_content,
                // Upload success
                [on_success]() {
                    spdlog::info(
                        "[HelixMacroManager] Successfully removed include from printer.cfg");
                    if (on_success) {
                        on_success();
                    }
                },
                // Upload error
                [on_error](const MoonrakerError& err) {
                    spdlog::error("[HelixMacroManager] Failed to upload modified printer.cfg: {}",
                                  err.message);
                    if (on_error) {
                        on_error(err);
                    }
                });
        },
        // Download error
        [on_error](const MoonrakerError& err) {
            spdlog::error("[HelixMacroManager] Failed to download printer.cfg: {}", err.message);
            if (on_error) {
                on_error(err);
            }
        });
}

void MacroManager::delete_macro_file(SuccessCallback on_success, ErrorCallback on_error) {
    // Use MoonrakerAPI to delete the file
    api_.delete_file(std::string("config/") + HELIX_MACROS_FILENAME, on_success,
                     [on_success, on_error](const MoonrakerError& err) {
                         // File might not exist - that's OK for uninstall
                         if (err.type == MoonrakerErrorType::FILE_NOT_FOUND) {
                             spdlog::debug("[HelixMacroManager] Macro file already deleted");
                             on_success(); // Continue with success path
                         } else {
                             on_error(err);
                         }
                     });
}

void MacroManager::restart_klipper(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[HelixMacroManager] Requesting Klipper restart...");
    api_.restart_klipper(on_success, on_error);
}

std::optional<std::string> MacroManager::parse_installed_version() const {
    // Check if HELIX_READY macro exists (indicates v2.0+ macros)
    if (capabilities_.has_helix_macro("HELIX_READY")) {
        // TODO: Query the actual version from _HELIX_STATE or similar via Moonraker
        // For now, assume 2.0.0 if HELIX_READY exists
        return "2.0.0";
    }

    // Check for legacy v1.x macros
    if (capabilities_.has_helix_macro("HELIX_START_PRINT")) {
        return "1.0.0";
    }

    return std::nullopt;
}

} // namespace helix
