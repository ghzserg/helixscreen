// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "moonraker_api.h"
#include "thumbnail_processor.h"

#include <filesystem>
#include <functional>
#include <string>

/**
 * @file thumbnail_cache.h
 * @brief Centralized thumbnail caching for print files and history
 *
 * ThumbnailCache provides a unified approach to downloading and caching
 * thumbnail images from Moonraker. It handles:
 * - Hash-based filename generation for cache files
 * - Cache directory creation
 * - Async download with callbacks
 * - LVGL-compatible path formatting ("A:" prefix)
 *
 * ## Usage Example
 * ```cpp
 * ThumbnailCache cache;
 *
 * // Check if already cached (sync)
 * std::string lvgl_path = cache.get_if_cached(relative_path);
 * if (!lvgl_path.empty()) {
 *     lv_image_set_src(img, lvgl_path.c_str());
 *     return;
 * }
 *
 * // Download async
 * cache.fetch(api_, relative_path,
 *     [this](const std::string& lvgl_path) {
 *         // Update UI on main thread
 *         lv_image_set_src(img, lvgl_path.c_str());
 *     },
 *     [](const std::string& error) {
 *         spdlog::warn("Thumbnail download failed: {}", error);
 *     });
 * ```
 *
 * @see MoonrakerAPI::download_thumbnail
 */
class ThumbnailCache {
  public:
    /// Cache directory for downloaded thumbnails
    static constexpr const char* CACHE_DIR = "/tmp/helix_thumbs";

    /// Minimum cache size (5 MB) - floor for very constrained systems
    static constexpr size_t MIN_CACHE_SIZE = 5 * 1024 * 1024;

    /// Default maximum cache size (20 MB) - conservative for AD5M, override via config
    static constexpr size_t DEFAULT_MAX_CACHE_SIZE = 20 * 1024 * 1024;

    /// Default percentage of available disk space to use for cache
    static constexpr double DEFAULT_DISK_PERCENT = 0.05; // 5%

    /// Default critical disk threshold (5 MB) - conservative for AD5M
    static constexpr size_t DEFAULT_DISK_CRITICAL = 5 * 1024 * 1024;

    /// Default low disk threshold (20 MB) - conservative for AD5M
    static constexpr size_t DEFAULT_DISK_LOW = 20 * 1024 * 1024;

    /// Callback for successful thumbnail fetch (receives LVGL-ready path with "A:" prefix)
    using SuccessCallback = std::function<void(const std::string& lvgl_path)>;

    /// Callback for failed thumbnail fetch (receives error message)
    using ErrorCallback = std::function<void(const std::string& error)>;

    /**
     * @brief Default constructor - auto-sizes based on available disk space
     *
     * Creates cache directory if it doesn't exist.
     * Cache size is calculated as:
     *   clamp(available_space * 5%, MIN_CACHE_SIZE, MAX_CACHE_SIZE)
     */
    ThumbnailCache();

    /**
     * @brief Constructor with explicit max size (for testing)
     *
     * @param max_size Maximum cache size in bytes
     */
    explicit ThumbnailCache(size_t max_size);

    /**
     * @brief Compute the local cache path for a relative Moonraker path
     *
     * Uses hash-based filename: `/tmp/helix_thumbs/{hash}.png`
     *
     * @param relative_path Moonraker relative path (e.g., ".thumbnails/file.png")
     * @return Local filesystem path for the cached file
     */
    [[nodiscard]] std::string get_cache_path(const std::string& relative_path) const;

    /**
     * @brief Get LVGL path if thumbnail is already cached
     *
     * Checks if the file exists locally without network request.
     * Useful for instant display when revisiting cached content.
     *
     * @param relative_path Moonraker relative path
     * @return LVGL-ready path ("A:/tmp/helix_thumbs/...") if cached, empty string otherwise
     */
    [[nodiscard]] std::string get_if_cached(const std::string& relative_path) const;

    /**
     * @brief Check if a path is already in LVGL format
     *
     * @param path Path to check
     * @return true if path starts with "A:" (already processed)
     */
    [[nodiscard]] static bool is_lvgl_path(const std::string& path);

    /**
     * @brief Convert a local filesystem path to LVGL format
     *
     * @param local_path Local filesystem path
     * @return LVGL-ready path with "A:" prefix
     */
    [[nodiscard]] static std::string to_lvgl_path(const std::string& local_path);

    /**
     * @brief Fetch thumbnail, downloading if not cached
     *
     * This is the main async entry point. It:
     * 1. Checks if already cached (returns immediately if so)
     * 2. Downloads from Moonraker if not cached
     * 3. Calls success callback with LVGL-ready path
     *
     * @param api MoonrakerAPI instance for downloading
     * @param relative_path Moonraker relative path (e.g., ".thumbnails/file.png")
     * @param on_success Called with LVGL path on success (may be called synchronously if cached)
     * @param on_error Called with error message on failure
     *
     * @note Callbacks may be invoked from background thread - use ui_async_call_safe for UI updates
     */
    void fetch(MoonrakerAPI* api, const std::string& relative_path, SuccessCallback on_success,
               ErrorCallback on_error);

    /**
     * @brief Fetch thumbnail with pre-scaling optimization
     *
     * Similar to fetch(), but produces pre-scaled LVGL binary files (.bin) for
     * optimal display performance. Pre-scaled thumbnails render instantly without
     * runtime scaling overhead.
     *
     * Flow:
     * 1. Check for pre-scaled .bin (instant return if found)
     * 2. Check for cached PNG (queue for background pre-scaling)
     * 3. Download PNG if needed, then pre-scale
     * 4. Return .bin path on success
     *
     * @param api MoonrakerAPI instance for downloading
     * @param relative_path Moonraker relative path (e.g., ".thumbnails/file.png")
     * @param target Target dimensions for pre-scaling (from
     * ThumbnailProcessor::get_target_for_display())
     * @param on_success Called with LVGL path to .bin on success
     * @param on_error Called with error message on failure
     *
     * @note Falls back to PNG on pre-scaling failure - display still works, just slower
     * @see docs/THUMBNAIL_OPTIMIZATION_PLAN.md
     */
    void fetch_optimized(MoonrakerAPI* api, const std::string& relative_path,
                         const helix::ThumbnailTarget& target, SuccessCallback on_success,
                         ErrorCallback on_error);

    /**
     * @brief Check if a pre-scaled version exists in cache
     *
     * Fast synchronous lookup for pre-scaled .bin files.
     * Does not trigger download or processing.
     *
     * @param relative_path Moonraker relative path
     * @param target Target dimensions to check for
     * @return LVGL path (A:/...) to .bin if cached, empty string otherwise
     */
    [[nodiscard]] std::string get_if_optimized(const std::string& relative_path,
                                               const helix::ThumbnailTarget& target) const;

    /**
     * @brief Clear all cached thumbnails
     *
     * Removes all files from the cache directory.
     * Useful for testing or manual cache invalidation.
     *
     * @return Number of files removed
     */
    size_t clear_cache();

    /**
     * @brief Get the total size of cached thumbnails
     *
     * @return Total size in bytes
     */
    [[nodiscard]] size_t get_cache_size() const;

    /**
     * @brief Get the maximum cache size
     *
     * @return Maximum cache size in bytes
     */
    [[nodiscard]] size_t get_max_size() const {
        return max_size_;
    }

    /**
     * @brief Set maximum cache size
     *
     * If new size is smaller than current cache, eviction will occur.
     *
     * @param max_size New maximum size in bytes
     */
    void set_max_size(size_t max_size);

    /**
     * @brief Disk pressure levels for adaptive cache management
     */
    enum class DiskPressure {
        Normal,  ///< Plenty of space - normal caching behavior
        Low,     ///< Below DISK_LOW_THRESHOLD - evict aggressively
        Critical ///< Below DISK_CRITICAL_THRESHOLD - skip caching entirely
    };

    /**
     * @brief Check current disk pressure level
     *
     * Queries available disk space and returns appropriate pressure level.
     * Used to adapt caching behavior to real-time disk conditions.
     *
     * @return Current disk pressure level
     */
    [[nodiscard]] DiskPressure get_disk_pressure() const;

    /**
     * @brief Get available disk space in bytes
     *
     * @return Available bytes, or 0 on error
     */
    [[nodiscard]] size_t get_available_disk_space() const;

    /**
     * @brief Check if caching is currently allowed
     *
     * Returns false when disk is critically low to prevent filling up the filesystem.
     *
     * @return true if caching is allowed, false if disk is critical
     */
    [[nodiscard]] bool is_caching_allowed() const;

  private:
    size_t max_size_;       ///< Maximum cache size before LRU eviction
    size_t disk_critical_;  ///< Stop caching below this available space
    size_t disk_low_;       ///< Evict aggressively below this available space
    size_t configured_max_; ///< Max size from config (before dynamic sizing)

    /**
     * @brief Load cache settings from helixconfig.json
     *
     * Reads cache/thumbnail_max_mb, cache/disk_critical_mb, cache/disk_low_mb.
     * Falls back to defaults if config not available.
     */
    void load_config();

    /**
     * @brief Ensure cache directory exists
     */
    void ensure_cache_dir() const;

    /**
     * @brief Compute hash for a path string
     *
     * @param path Path to hash
     * @return Hash value as string
     */
    [[nodiscard]] static std::string compute_hash(const std::string& path);

    /**
     * @brief Evict oldest files if cache exceeds max size
     *
     * Uses file modification time (mtime) as LRU approximation.
     * Removes oldest files until cache is under max_size_.
     */
    void evict_if_needed();

    /**
     * @brief Process PNG and invoke callback with result
     *
     * Helper for fetch_optimized(). Reads PNG, queues for pre-scaling,
     * and invokes callback with .bin path on success or PNG fallback on error.
     *
     * @param png_lvgl_path LVGL path to the cached PNG
     * @param source_path Original Moonraker relative path (for cache key)
     * @param target Target dimensions for pre-scaling
     * @param on_success Success callback
     * @param on_error Error callback (not currently used - fallback to PNG instead)
     */
    void process_and_callback(const std::string& png_lvgl_path, const std::string& source_path,
                              const helix::ThumbnailTarget& target, SuccessCallback on_success,
                              ErrorCallback on_error);
};

/**
 * @brief Global singleton accessor
 *
 * Provides a single shared cache instance for the application.
 *
 * @return Reference to the global ThumbnailCache
 */
ThumbnailCache& get_thumbnail_cache();
