// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnail_cache.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <functional>
#include <vector>

// Global singleton using Meyer's Singleton pattern (thread-safe, no leak)
ThumbnailCache& get_thumbnail_cache() {
    static ThumbnailCache instance;
    return instance;
}

// Helper to calculate dynamic cache size based on available disk space
static size_t calculate_dynamic_max_size() {
    try {
        std::filesystem::space_info space = std::filesystem::space(ThumbnailCache::CACHE_DIR);
        size_t available = space.available;

        // Use 5% of available space
        size_t dynamic_size = static_cast<size_t>(available * ThumbnailCache::DEFAULT_DISK_PERCENT);

        // Clamp to min/max bounds
        size_t clamped = std::clamp(dynamic_size, ThumbnailCache::MIN_CACHE_SIZE,
                                    ThumbnailCache::MAX_CACHE_SIZE);

        spdlog::info("[ThumbnailCache] Available disk: {} MB, cache limit: {} MB",
                     available / (1024 * 1024), clamped / (1024 * 1024));

        return clamped;
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::warn("[ThumbnailCache] Failed to query disk space: {}, using minimum", e.what());
        return ThumbnailCache::MIN_CACHE_SIZE;
    }
}

ThumbnailCache::ThumbnailCache() : max_size_(MIN_CACHE_SIZE) {
    ensure_cache_dir();
    // Now that directory exists, we can query disk space
    max_size_ = calculate_dynamic_max_size();
}

ThumbnailCache::ThumbnailCache(size_t max_size) : max_size_(max_size) {
    ensure_cache_dir();
    spdlog::debug("[ThumbnailCache] Using explicit max size: {} MB", max_size_ / (1024 * 1024));
}

void ThumbnailCache::ensure_cache_dir() const {
    try {
        std::filesystem::create_directories(CACHE_DIR);
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::warn("[ThumbnailCache] Failed to create cache directory {}: {}", CACHE_DIR,
                     e.what());
    }
}

std::string ThumbnailCache::compute_hash(const std::string& path) {
    std::hash<std::string> hasher;
    return std::to_string(hasher(path));
}

std::string ThumbnailCache::get_cache_path(const std::string& relative_path) const {
    return std::string(CACHE_DIR) + "/" + compute_hash(relative_path) + ".png";
}

bool ThumbnailCache::is_lvgl_path(const std::string& path) {
    return path.size() >= 2 && path[0] == 'A' && path[1] == ':';
}

std::string ThumbnailCache::to_lvgl_path(const std::string& local_path) {
    if (is_lvgl_path(local_path)) {
        return local_path; // Already in LVGL format
    }
    return "A:" + local_path;
}

std::string ThumbnailCache::get_if_cached(const std::string& relative_path) const {
    if (relative_path.empty()) {
        return "";
    }

    // If already an LVGL path, check if the file exists
    if (is_lvgl_path(relative_path)) {
        std::string local_path = relative_path.substr(2); // Remove "A:" prefix
        if (std::filesystem::exists(local_path)) {
            return relative_path;
        }
        return "";
    }

    // Check if cached locally
    std::string cache_path = get_cache_path(relative_path);
    if (std::filesystem::exists(cache_path)) {
        spdlog::trace("[ThumbnailCache] Cache hit for {}", relative_path);
        return to_lvgl_path(cache_path);
    }

    return "";
}

void ThumbnailCache::set_max_size(size_t max_size) {
    max_size_ = max_size;
    evict_if_needed();
}

void ThumbnailCache::evict_if_needed() {
    size_t current_size = get_cache_size();
    if (current_size <= max_size_) {
        return;
    }

    spdlog::debug("[ThumbnailCache] Cache size {} MB exceeds limit {} MB, evicting oldest files",
                  current_size / (1024 * 1024), max_size_ / (1024 * 1024));

    // Collect files with their modification times
    struct CacheEntry {
        std::filesystem::path path;
        std::filesystem::file_time_type mtime;
        size_t size;
    };
    std::vector<CacheEntry> entries;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(CACHE_DIR)) {
            if (entry.is_regular_file()) {
                entries.push_back({entry.path(), entry.last_write_time(), entry.file_size()});
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::warn("[ThumbnailCache] Error scanning cache for eviction: {}", e.what());
        return;
    }

    // Sort by modification time (oldest first)
    std::sort(entries.begin(), entries.end(),
              [](const CacheEntry& a, const CacheEntry& b) { return a.mtime < b.mtime; });

    // Remove oldest files until under limit
    size_t evicted_count = 0;
    size_t evicted_bytes = 0;
    for (const auto& entry : entries) {
        if (current_size <= max_size_) {
            break;
        }

        try {
            std::filesystem::remove(entry.path);
            current_size -= entry.size;
            evicted_bytes += entry.size;
            ++evicted_count;
        } catch (const std::filesystem::filesystem_error& e) {
            spdlog::warn("[ThumbnailCache] Failed to evict {}: {}", entry.path.string(), e.what());
        }
    }

    if (evicted_count > 0) {
        spdlog::info("[ThumbnailCache] Evicted {} files ({} KB) to stay under limit", evicted_count,
                     evicted_bytes / 1024);
    }
}

void ThumbnailCache::fetch(MoonrakerAPI* api, const std::string& relative_path,
                           SuccessCallback on_success, ErrorCallback on_error) {
    if (relative_path.empty()) {
        if (on_error) {
            on_error("Empty thumbnail path");
        }
        return;
    }

    // If already an LVGL path, validate and return immediately
    if (is_lvgl_path(relative_path)) {
        std::string local_path = relative_path.substr(2);
        if (std::filesystem::exists(local_path)) {
            spdlog::trace("[ThumbnailCache] Already LVGL path: {}", relative_path);
            if (on_success) {
                on_success(relative_path);
            }
        } else if (on_error) {
            on_error("LVGL path file not found: " + local_path);
        }
        return;
    }

    // Check local filesystem first (might be a local file path in mock mode)
    if (std::filesystem::exists(relative_path)) {
        spdlog::trace("[ThumbnailCache] Local file exists: {}", relative_path);
        if (on_success) {
            on_success(to_lvgl_path(relative_path));
        }
        return;
    }

    // Check cache
    std::string cached = get_if_cached(relative_path);
    if (!cached.empty()) {
        if (on_success) {
            on_success(cached);
        }
        return;
    }

    // Need to download
    if (!api) {
        if (on_error) {
            on_error("No API available for thumbnail download");
        }
        return;
    }

    // Evict old files before downloading new one
    evict_if_needed();

    std::string cache_path = get_cache_path(relative_path);
    spdlog::trace("[ThumbnailCache] Downloading {} -> {}", relative_path, cache_path);

    api->download_thumbnail(
        relative_path, cache_path,
        // Success callback
        [this, on_success, relative_path](const std::string& local_path) {
            spdlog::trace("[ThumbnailCache] Downloaded {} to {}", relative_path, local_path);
            // Check if we need eviction after download
            evict_if_needed();
            if (on_success) {
                on_success(to_lvgl_path(local_path));
            }
        },
        // Error callback
        [on_error, relative_path](const MoonrakerError& error) {
            spdlog::warn("[ThumbnailCache] Failed to download {}: {}", relative_path,
                         error.message);
            if (on_error) {
                on_error(error.message);
            }
        });
}

size_t ThumbnailCache::clear_cache() {
    size_t count = 0;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(CACHE_DIR)) {
            if (entry.is_regular_file()) {
                std::filesystem::remove(entry.path());
                ++count;
            }
        }
        spdlog::info("[ThumbnailCache] Cleared {} cached thumbnails", count);
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::warn("[ThumbnailCache] Error clearing cache: {}", e.what());
    }
    return count;
}

size_t ThumbnailCache::get_cache_size() const {
    size_t total = 0;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(CACHE_DIR)) {
            if (entry.is_regular_file()) {
                total += entry.file_size();
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::warn("[ThumbnailCache] Error calculating cache size: {}", e.what());
    }
    return total;
}
