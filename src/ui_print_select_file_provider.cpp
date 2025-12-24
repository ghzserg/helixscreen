// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_print_select_file_provider.h"

#include "ui_panel_print_select.h" // For PrintFileData
#include "ui_print_select_card_view.h"
#include "ui_update_queue.h"
#include "ui_utils.h" // For format_* helpers

#include "moonraker_api.h"
#include "thumbnail_cache.h"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <unordered_map>
#include <unordered_set>

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
                                            const std::vector<PrintFileData>& existing_files,
                                            const std::vector<bool>& existing_fetched) {
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
    std::unordered_set<std::string> already_fetched;

    for (size_t i = 0; i < existing_files.size(); ++i) {
        const auto& file = existing_files[i];
        existing_data[file.filename] = file;
        if (i < existing_fetched.size() && existing_fetched[i]) {
            already_fetched.insert(file.filename);
        }
    }

    auto* self = this;
    auto on_ready = on_files_ready_;
    auto on_err = on_error_;
    auto path_copy = current_path;

    // Request file list from current directory (non-recursive)
    api_->list_files(
        "gcodes", current_path, false,
        // Success callback
        [self, existing_data = std::move(existing_data),
         already_fetched = std::move(already_fetched), path_copy, on_ready,
         on_err](const std::vector<FileInfo>& files) {
            spdlog::debug("[FileProvider] Received {} items from Moonraker", files.size());

            std::vector<PrintFileData> file_list;
            std::vector<bool> metadata_fetched;

            // Add ".." parent directory entry if not at root
            if (!path_copy.empty()) {
                PrintFileData parent_dir;
                parent_dir.filename = "..";
                parent_dir.is_dir = true;
                parent_dir.thumbnail_path = self->FOLDER_UP_ICON;
                parent_dir.size_str = "Go up";
                parent_dir.print_time_str = "";
                parent_dir.filament_str = "";
                parent_dir.modified_str = "";
                file_list.push_back(std::move(parent_dir));
                metadata_fetched.push_back(true); // Parent dir doesn't need metadata
            }

            // Convert FileInfo to PrintFileData, preserving existing data where available
            for (const auto& file : files) {
                // Check if we have existing data for this file
                auto it = existing_data.find(file.filename);
                if (it != existing_data.end()) {
                    // Check if file was modified (e.g., re-uploaded with same name)
                    time_t new_modified = static_cast<time_t>(file.modified);
                    if (it->second.modified_timestamp == new_modified) {
                        // Same file - preserve existing data (thumbnail, metadata already loaded)
                        file_list.push_back(it->second);
                        metadata_fetched.push_back(already_fetched.count(file.filename) > 0);
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

                // New file or modified file - create with placeholder data
                PrintFileData data;
                data.filename = file.filename;
                data.is_dir = file.is_dir;
                data.file_size_bytes = file.size;
                data.modified_timestamp = static_cast<time_t>(file.modified);

                if (file.is_dir) {
                    // Directory - use folder icon
                    data.thumbnail_path = PrintSelectCardView::FOLDER_ICON;
                    data.print_time_minutes = 0;
                    data.filament_grams = 0.0f;
                    data.size_str = "Folder";
                    data.print_time_str = "";
                    data.filament_str = "";
                    metadata_fetched.push_back(true); // Directories don't need metadata
                } else {
                    // Only process .gcode files
                    if (file.filename.find(".gcode") == std::string::npos &&
                        file.filename.find(".g") == std::string::npos) {
                        continue;
                    }

                    data.thumbnail_path = PrintSelectCardView::DEFAULT_THUMB;
                    data.print_time_minutes = 0;
                    data.filament_grams = 0.0f;
                    data.size_str = format_file_size(data.file_size_bytes);
                    data.print_time_str = format_print_time(data.print_time_minutes);
                    data.filament_str = format_filament_weight(data.filament_grams);
                    metadata_fetched.push_back(false); // Needs metadata fetch
                }

                data.modified_str = format_modified_date(data.modified_timestamp);
                file_list.push_back(std::move(data));
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

            // Deliver results via callback
            if (on_ready) {
                on_ready(std::move(file_list), std::move(metadata_fetched));
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

void PrintSelectFileProvider::fetch_metadata_range(std::vector<PrintFileData>& files,
                                                   std::vector<bool>& metadata_fetched,
                                                   size_t start, size_t end) {
    if (!api_) {
        return;
    }

    // Clamp range to file list bounds
    start = std::min(start, files.size());
    end = std::min(end, files.size());

    if (start >= end) {
        return;
    }

    // Ensure tracking vector is properly sized
    if (metadata_fetched.size() != files.size()) {
        metadata_fetched.resize(files.size(), false);
    }

    auto* self = this;
    auto on_updated = on_metadata_updated_;
    size_t fetch_count = 0;

    // Fetch metadata for files in range only (not directories, not already fetched)
    for (size_t i = start; i < end; i++) {
        if (files[i].is_dir)
            continue; // Skip directories

        if (metadata_fetched[i])
            continue; // Already fetched or in flight

        // Mark as fetched immediately to prevent duplicate requests
        metadata_fetched[i] = true;
        fetch_count++;

        const std::string filename = files[i].filename;

        // Define success handler as a named lambda so it can be reused for metascan fallback
        auto on_metadata_success = [self, i, filename, on_updated](const FileMetadata& metadata) {
            // Extract all values on background thread (safe - metadata is const ref)
            int print_time_minutes = static_cast<int>(metadata.estimated_time / 60.0);
            float filament_grams = static_cast<float>(metadata.filament_weight_total);
            std::string filament_type = metadata.filament_type;
            std::vector<std::string> filament_colors = metadata.filament_colors;
            std::string thumb_path = metadata.get_largest_thumbnail();
            uint32_t layer_count = metadata.layer_count;

            // Format strings on background thread
            std::string print_time_str = format_print_time(print_time_minutes);
            std::string filament_str = format_filament_weight(filament_grams);
            std::string layer_count_str = layer_count > 0 ? std::to_string(layer_count) : "--";

            // Check if thumbnail is a local file
            bool thumb_is_local = !thumb_path.empty() && std::filesystem::exists(thumb_path);

            // Prepare cache path for remote thumbnails
            std::string cache_file;
            if (!thumb_path.empty() && !thumb_is_local) {
                // Use ThumbnailCache's centralized cache path
                cache_file = get_thumbnail_cache().get_cache_path(thumb_path);
            }

            // Dispatch update to callback
            // NOTE: We capture api directly (not through provider) to avoid use-after-free
            // if the provider is destroyed while thumbnail download is in flight.
            // MoonrakerAPI is a long-lived singleton that outlives the provider.
            struct MetadataUpdate {
                MoonrakerAPI* api; // Captured directly, not through provider
                MetadataUpdatedCallback on_updated;
                size_t index;
                std::string filename;
                int print_time_minutes;
                float filament_grams;
                std::string filament_type;
                std::vector<std::string> filament_colors;
                std::string print_time_str;
                std::string filament_str;
                uint32_t layer_count;
                std::string layer_count_str;
                std::string thumb_path;
                std::string cache_file;
                bool thumb_is_local;
            };

            ui_queue_update<MetadataUpdate>(
                std::make_unique<MetadataUpdate>(MetadataUpdate{
                    self->api_, on_updated, i, filename, print_time_minutes, filament_grams,
                    filament_type, filament_colors, print_time_str, filament_str, layer_count,
                    layer_count_str, thumb_path, cache_file, thumb_is_local}),
                [](MetadataUpdate* d) {
                    // Create updated file data
                    PrintFileData updated;
                    updated.filename = d->filename;
                    updated.print_time_minutes = d->print_time_minutes;
                    updated.filament_grams = d->filament_grams;
                    updated.filament_type = d->filament_type;
                    updated.filament_colors = d->filament_colors;
                    updated.print_time_str = d->print_time_str;
                    updated.filament_str = d->filament_str;
                    updated.layer_count = d->layer_count;
                    updated.layer_count_str = d->layer_count_str;

                    // Handle thumbnail
                    if (!d->thumb_path.empty()) {
                        if (d->thumb_is_local) {
                            // Local file exists - use directly (mock mode)
                            updated.thumbnail_path = "A:" + d->thumb_path;
                            spdlog::trace("[FileProvider] Using local thumbnail for {}: {}",
                                          d->filename, updated.thumbnail_path);

                            // Deliver update
                            if (d->on_updated) {
                                d->on_updated(d->index, updated);
                            }
                        } else if (d->api) {
                            // Remote path - download from Moonraker
                            spdlog::trace("[FileProvider] Downloading thumbnail for {}: {} -> {}",
                                          d->filename, d->thumb_path, d->cache_file);

                            size_t file_idx = d->index;
                            std::string filename_copy = d->filename;
                            auto on_updated_copy = d->on_updated;

                            d->api->download_thumbnail(
                                d->thumb_path, d->cache_file,
                                // Success callback
                                [file_idx, filename_copy,
                                 on_updated_copy](const std::string& local_path) {
                                    struct ThumbUpdate {
                                        size_t index;
                                        std::string filename;
                                        std::string local_path;
                                        MetadataUpdatedCallback on_updated;
                                    };
                                    ui_queue_update<ThumbUpdate>(
                                        std::make_unique<ThumbUpdate>(ThumbUpdate{
                                            file_idx, filename_copy, local_path, on_updated_copy}),
                                        [](ThumbUpdate* t) {
                                            PrintFileData thumb_update;
                                            thumb_update.filename = t->filename;
                                            thumb_update.thumbnail_path = "A:" + t->local_path;
                                            spdlog::debug("[FileProvider] Thumbnail cached for "
                                                          "{}: {}",
                                                          t->filename, thumb_update.thumbnail_path);
                                            if (t->on_updated) {
                                                t->on_updated(t->index, thumb_update);
                                            }
                                        });
                                },
                                // Error callback
                                [filename_copy](const MoonrakerError& error) {
                                    spdlog::warn("[FileProvider] Failed to download thumbnail "
                                                 "for {}: {}",
                                                 filename_copy, error.message);
                                });
                        }
                    } else {
                        // No thumbnail, just deliver metadata update
                        if (d->on_updated) {
                            d->on_updated(d->index, updated);
                        }
                    }
                });
        };

        // Request metadata with silent=true (no toast on error)
        // On error, fall back to metascan which can parse metadata from the file directly
        api_->get_file_metadata(
            filename, on_metadata_success,
            // Metadata error callback - fallback to metascan
            [self, filename, on_metadata_success](const MoonrakerError& error) {
                spdlog::debug("[FileProvider] Metadata not indexed for {} ({}), trying metascan...",
                              filename, error.message);

                // Metascan can extract metadata directly from the G-code file
                if (self->api_) {
                    self->api_->metascan_file(filename, on_metadata_success,
                                              [filename](const MoonrakerError& scan_error) {
                                                  // Silent fail - UI will show "--" for missing
                                                  // metadata
                                                  spdlog::debug(
                                                      "[FileProvider] Metascan failed for {}: {}",
                                                      filename, scan_error.message);
                                              });
                }
            },
            true // silent - don't trigger RPC_ERROR event/toast
        );
    }

    if (fetch_count > 0) {
        spdlog::debug("[FileProvider] fetch_metadata_range({}, {}): started {} metadata requests",
                      start, end, fetch_count);
    }
}

} // namespace helix::ui
