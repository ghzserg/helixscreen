// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
struct PrintFileData;
class MoonrakerAPI;

namespace helix::ui {

/**
 * @file ui_print_select_file_provider.h
 * @brief Moonraker file data provider for print selection panel
 *
 * Handles fetching file lists and metadata from Moonraker API.
 * Uses callbacks to deliver data, keeping async handling clean.
 *
 * ## Key Features:
 * - Async file list fetching from Moonraker
 * - Lazy metadata loading for visible files only
 * - Thumbnail downloading and caching
 * - Thread-safe updates via LVGL async dispatch
 *
 * ## Usage:
 * @code
 * PrintSelectFileProvider provider;
 * provider.set_api(api);
 * provider.set_on_files_ready([](auto files) { ... });
 * provider.set_on_metadata_updated([](size_t idx, const PrintFileData& file) { ... });
 *
 * // Fetch file list (existing files preserved if unchanged):
 * provider.refresh_files("/subdir", existing_file_list);
 * @endcode
 */

/**
 * @brief Callback when file list is ready
 * @param files Vector of PrintFileData from Moonraker (each file has metadata_fetched field)
 */
using FilesReadyCallback = std::function<void(std::vector<PrintFileData>&& files)>;

/**
 * @brief Callback when a file's metadata is updated
 * @param index Index of updated file in list
 * @param file Updated file data
 */
using MetadataUpdatedCallback = std::function<void(size_t index, const PrintFileData& file)>;

/**
 * @brief Callback for file list refresh errors
 * @param error_message Error description
 */
using FileErrorCallback = std::function<void(const std::string& error_message)>;

/**
 * @brief Moonraker file data provider
 */
class PrintSelectFileProvider {
  public:
    PrintSelectFileProvider() = default;
    ~PrintSelectFileProvider() = default;

    // Non-copyable, movable
    PrintSelectFileProvider(const PrintSelectFileProvider&) = delete;
    PrintSelectFileProvider& operator=(const PrintSelectFileProvider&) = delete;
    PrintSelectFileProvider(PrintSelectFileProvider&&) noexcept = default;
    PrintSelectFileProvider& operator=(PrintSelectFileProvider&&) noexcept = default;

    // === Setup ===

    /**
     * @brief Set MoonrakerAPI dependency
     */
    void set_api(MoonrakerAPI* api) {
        api_ = api;
    }

    // === Callbacks ===

    /**
     * @brief Set callback for when file list is ready
     */
    void set_on_files_ready(FilesReadyCallback callback) {
        on_files_ready_ = std::move(callback);
    }

    /**
     * @brief Set callback for metadata updates
     */
    void set_on_metadata_updated(MetadataUpdatedCallback callback) {
        on_metadata_updated_ = std::move(callback);
    }

    /**
     * @brief Set callback for errors
     */
    void set_on_error(FileErrorCallback callback) {
        on_error_ = std::move(callback);
    }

    // === File Operations ===

    /**
     * @brief Refresh file list from Moonraker
     *
     * Fetches files from specified directory (non-recursive).
     * Results delivered via on_files_ready callback.
     * Existing files are preserved if unchanged (by modified timestamp).
     *
     * @param current_path Directory path relative to gcodes root (empty = root)
     * @param existing_files Existing file list to preserve metadata/thumbnails from
     */
    void refresh_files(const std::string& current_path,
                       const std::vector<PrintFileData>& existing_files = {});

    /**
     * @brief Check if API is connected and ready
     */
    [[nodiscard]] bool is_ready() const;

  private:
    // === Dependencies ===
    MoonrakerAPI* api_ = nullptr;

    // === Callbacks ===
    FilesReadyCallback on_files_ready_;
    MetadataUpdatedCallback on_metadata_updated_;
    FileErrorCallback on_error_;

    // === Internal State ===
    std::string current_path_; ///< Path for current refresh operation

    // === Constants ===
    static constexpr const char* FOLDER_UP_ICON = "A:assets/images/folder-up.png";
};

} // namespace helix::ui
