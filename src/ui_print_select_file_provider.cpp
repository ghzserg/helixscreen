// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_print_select_file_provider.h"

#include "ui_panel_print_select.h" // For PrintFileData
#include "ui_print_select_card_view.h"
#include "ui_update_queue.h"

#include "moonraker_api.h"
#include "print_file_data.h"
#include "thumbnail_cache.h"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <unordered_map>

namespace helix::ui {

// ============================================================================
// File Operations
// ============================================================================

bool PrintSelectFileProvider::is_ready() const {
    if (!api_) {
        return false;
    }

    ConnectionState state = api_->get_client().get_connection_state();
    return state == ConnectionState::CONNECTED;
}

void PrintSelectFileProvider::refresh_files(const std::string& current_path,
                                            const std::vector<PrintFileData>& existing_files) {
    if (!api_) {
        spdlog::warn("[FileProvider] Cannot refresh files: MoonrakerAPI not initialized");
        return;
    }

    // Check if WebSocket is actually connected
    if (!is_ready()) {
        spdlog::debug("[FileProvider] Cannot refresh files: not connected");
        return;
    }

    current_path_ = current_path;

    spdlog::debug("[FileProvider] Refreshing file list from Moonraker (path: '{}')...",
                  current_path.empty() ? "/" : current_path);

    // Build map of existing file data to preserve thumbnails/metadata
    std::unordered_map<std::string, PrintFileData> existing_data;
    for (const auto& file : existing_files) {
        existing_data[file.filename] = file;
    }

    auto* self = this;
    auto on_ready = on_files_ready_;
    auto on_err = on_error_;
    auto path_copy = current_path;

    // Request file list from current directory (non-recursive)
    api_->list_files(
        "gcodes", current_path, false,
        // Success callback
        [self, existing_data = std::move(existing_data), path_copy, on_ready,
         on_err](const std::vector<FileInfo>& files) {
            spdlog::debug("[FileProvider] Received {} items from Moonraker", files.size());

            std::vector<PrintFileData> file_list;

            // Add ".." parent directory entry if not at root
            if (!path_copy.empty()) {
                file_list.push_back(
                    PrintFileData::make_directory("..", self->FOLDER_UP_ICON, true));
            }

            // Convert FileInfo to PrintFileData, preserving existing data where available
            for (const auto& file : files) {
                // Skip .helix_temp directory (internal temp files for modified prints)
                if (file.filename == ".helix_temp" || file.filename.find(".helix_temp/") == 0) {
                    continue;
                }

                // Check if we have existing data for this file
                auto it = existing_data.find(file.filename);
                if (it != existing_data.end()) {
                    // Check if file was modified (e.g., re-uploaded with same name)
                    time_t new_modified = static_cast<time_t>(file.modified);
                    if (it->second.modified_timestamp == new_modified) {
                        // Same file - preserve existing data (thumbnail, metadata, fetched state)
                        // BUT: validate thumbnail still exists (may have been invalidated)
                        PrintFileData preserved = it->second;
                        if (!preserved.thumbnail_path.empty() &&
                            preserved.thumbnail_path.rfind("A:", 0) == 0 &&
                            preserved.thumbnail_path !=
                                PrintSelectCardView::get_default_thumbnail()) {
                            // Convert LVGL path to filesystem path and check existence
                            std::string fs_path = preserved.thumbnail_path.substr(2); // Strip "A:"
                            if (!std::filesystem::exists(fs_path)) {
                                spdlog::debug(
                                    "[FileProvider] Cached thumbnail missing, will re-fetch: {}",
                                    preserved.thumbnail_path);
                                preserved.thumbnail_path =
                                    PrintSelectCardView::get_default_thumbnail();
                                preserved.metadata_fetched = false; // Re-fetch to get thumbnail
                            }
                        }
                        file_list.push_back(preserved);
                        continue;
                    }
                    // File was modified - invalidate cached thumbnails and refetch
                    spdlog::info(
                        "[FileProvider] File modified, invalidating cache: {} (old: {}, new: {})",
                        file.filename, it->second.modified_timestamp, new_modified);
                    if (!it->second.original_thumbnail_url.empty()) {
                        get_thumbnail_cache().invalidate(it->second.original_thumbnail_url);
                    }
                    // Fall through to create fresh PrintFileData
                }

                // New file or modified file - create with factory methods
                if (file.is_dir) {
                    // Directory - use folder icon
                    file_list.push_back(PrintFileData::make_directory(
                        file.filename, PrintSelectCardView::FOLDER_ICON, false));
                } else {
                    // Only process .gcode files
                    if (file.filename.find(".gcode") == std::string::npos &&
                        file.filename.find(".g") == std::string::npos) {
                        continue;
                    }

                    file_list.push_back(PrintFileData::from_moonraker_file(
                        file, PrintSelectCardView::get_default_thumbnail()));
                }
            }

            // Count files vs directories for logging
            size_t dir_count = 0, file_count = 0;
            for (const auto& item : file_list) {
                if (item.is_dir)
                    dir_count++;
                else
                    file_count++;
            }
            spdlog::info("[FileProvider] File list updated: {} directories, {} G-code files",
                         dir_count, file_count);

            // Deliver results via callback (metadata_fetched is now in each file struct)
            if (on_ready) {
                on_ready(std::move(file_list));
            }
        },
        // Error callback
        [on_err](const MoonrakerError& error) {
            spdlog::error("[FileProvider] File list refresh error: {} ({})", error.message,
                          error.get_type_string());
            if (on_err) {
                on_err(error.message);
            }
        });
}

} // namespace helix::ui
