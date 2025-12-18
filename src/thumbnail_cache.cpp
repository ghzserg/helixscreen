// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnail_cache.h"

#include "config.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <vector>

// Global singleton using Meyer's Singleton pattern (thread-safe, no leak)
ThumbnailCache& get_thumbnail_cache() {
    static ThumbnailCache instance;
    return instance;
}

// Helper to calculate dynamic cache size based on available disk space
static size_t calculate_dynamic_max_size(size_t configured_max) {
    try {
        std::filesystem::space_info space = std::filesystem::space(ThumbnailCache::CACHE_DIR);
        size_t available = space.available;

        // Use 5% of available space
        size_t dynamic_size = static_cast<size_t>(available * ThumbnailCache::DEFAULT_DISK_PERCENT);

        // Clamp to min/configured_max bounds
        size_t clamped = std::clamp(dynamic_size, ThumbnailCache::MIN_CACHE_SIZE, configured_max);

        spdlog::info("[ThumbnailCache] Available disk: {} MB, cache limit: {} MB (max: {} MB)",
                     available / (1024 * 1024), clamped / (1024 * 1024),
                     configured_max / (1024 * 1024));

        return clamped;
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::warn("[ThumbnailCache] Failed to query disk space: {}, using minimum", e.what());
        return ThumbnailCache::MIN_CACHE_SIZE;
    }
}

ThumbnailCache::ThumbnailCache()
    : max_size_(MIN_CACHE_SIZE), disk_critical_(DEFAULT_DISK_CRITICAL), disk_low_(DEFAULT_DISK_LOW),
      configured_max_(DEFAULT_MAX_CACHE_SIZE) {
    ensure_cache_dir();
    load_config();
    // Now that directory exists and config is loaded, calculate dynamic size
    max_size_ = calculate_dynamic_max_size(configured_max_);
}

ThumbnailCache::ThumbnailCache(size_t max_size)
    : max_size_(max_size), disk_critical_(DEFAULT_DISK_CRITICAL), disk_low_(DEFAULT_DISK_LOW),
      configured_max_(max_size) {
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

void ThumbnailCache::load_config() {
    Config* config = Config::get_instance();
    if (!config) {
        spdlog::debug("[ThumbnailCache] Config not available, using defaults");
        return;
    }

    // Read cache settings from config (values are in MB, convert to bytes)
    int max_mb = config->get<int>("/cache/thumbnail_max_mb",
                                  static_cast<int>(DEFAULT_MAX_CACHE_SIZE / (1024 * 1024)));
    int critical_mb = config->get<int>("/cache/disk_critical_mb",
                                       static_cast<int>(DEFAULT_DISK_CRITICAL / (1024 * 1024)));
    int low_mb =
        config->get<int>("/cache/disk_low_mb", static_cast<int>(DEFAULT_DISK_LOW / (1024 * 1024)));

    // Convert to bytes and store
    configured_max_ = static_cast<size_t>(max_mb) * 1024 * 1024;
    disk_critical_ = static_cast<size_t>(critical_mb) * 1024 * 1024;
    disk_low_ = static_cast<size_t>(low_mb) * 1024 * 1024;

    // Sanity check: critical should be less than low
    if (disk_critical_ >= disk_low_) {
        spdlog::warn("[ThumbnailCache] disk_critical_mb ({}) >= disk_low_mb ({}), adjusting",
                     critical_mb, low_mb);
        disk_critical_ = disk_low_ / 2;
    }

    spdlog::info("[ThumbnailCache] Config loaded: max={} MB, critical={} MB, low={} MB",
                 configured_max_ / (1024 * 1024), disk_critical_ / (1024 * 1024),
                 disk_low_ / (1024 * 1024));
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

size_t ThumbnailCache::get_available_disk_space() const {
    try {
        std::filesystem::space_info space = std::filesystem::space(CACHE_DIR);
        return space.available;
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::warn("[ThumbnailCache] Failed to query disk space: {}", e.what());
        return 0;
    }
}

ThumbnailCache::DiskPressure ThumbnailCache::get_disk_pressure() const {
    size_t available = get_available_disk_space();

    if (available < disk_critical_) {
        return DiskPressure::Critical;
    } else if (available < disk_low_) {
        return DiskPressure::Low;
    }
    return DiskPressure::Normal;
}

bool ThumbnailCache::is_caching_allowed() const {
    return get_disk_pressure() != DiskPressure::Critical;
}

void ThumbnailCache::evict_if_needed() {
    size_t current_size = get_cache_size();
    DiskPressure pressure = get_disk_pressure();

    // Determine effective limit based on disk pressure
    size_t effective_limit = max_size_;
    const char* reason = nullptr;

    if (pressure == DiskPressure::Critical) {
        // Critical: evict everything possible to free disk space
        effective_limit = 0;
        reason = "disk critically low";
    } else if (pressure == DiskPressure::Low) {
        // Low: reduce cache to half of normal limit
        effective_limit = max_size_ / 2;
        reason = "disk space low";
    }

    if (current_size <= effective_limit) {
        return;
    }

    if (reason) {
        spdlog::warn("[ThumbnailCache] {} (available: {} MB), reducing cache from {} MB to {} MB",
                     reason, get_available_disk_space() / (1024 * 1024),
                     current_size / (1024 * 1024), effective_limit / (1024 * 1024));
    } else {
        spdlog::debug(
            "[ThumbnailCache] Cache size {} MB exceeds limit {} MB, evicting oldest files",
            current_size / (1024 * 1024), effective_limit / (1024 * 1024));
    }

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
        if (current_size <= effective_limit) {
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

    // Check disk pressure before downloading
    if (!is_caching_allowed()) {
        spdlog::warn("[ThumbnailCache] Disk critically low, skipping download of {}",
                     relative_path);
        if (on_error) {
            on_error("Disk space critically low - caching disabled");
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

// ============================================================================
// Optimized Thumbnail Fetching (Pre-scaling)
// ============================================================================

std::string ThumbnailCache::get_if_optimized(const std::string& relative_path,
                                             const helix::ThumbnailTarget& target) const {
    if (relative_path.empty()) {
        return "";
    }

    // Check for pre-scaled .bin via ThumbnailProcessor
    return helix::ThumbnailProcessor::instance().get_if_processed(relative_path, target);
}

void ThumbnailCache::fetch_optimized(MoonrakerAPI* api, const std::string& relative_path,
                                     const helix::ThumbnailTarget& target,
                                     SuccessCallback on_success, ErrorCallback on_error) {
    if (relative_path.empty()) {
        if (on_error) {
            on_error("Empty thumbnail path");
        }
        return;
    }

    // Step 1: Check for pre-scaled .bin (instant return)
    std::string optimized = get_if_optimized(relative_path, target);
    if (!optimized.empty()) {
        spdlog::trace("[ThumbnailCache] Pre-scaled cache hit: {}", optimized);
        if (on_success) {
            on_success(optimized);
        }
        return;
    }

    // Step 2: Check for cached PNG
    std::string cached_png = get_if_cached(relative_path);
    if (!cached_png.empty()) {
        // PNG exists, queue for pre-scaling
        spdlog::trace("[ThumbnailCache] PNG cached, queuing pre-scale: {}", relative_path);
        process_and_callback(cached_png, relative_path, target, on_success, on_error);
        return;
    }

    // Step 3: Download PNG, then pre-scale
    if (!api) {
        if (on_error) {
            on_error("No API available for thumbnail download");
        }
        return;
    }

    // Check disk pressure before downloading
    if (!is_caching_allowed()) {
        spdlog::warn("[ThumbnailCache] Disk critically low, skipping optimized fetch of {}",
                     relative_path);
        if (on_error) {
            on_error("Disk space critically low - caching disabled");
        }
        return;
    }

    evict_if_needed();

    std::string cache_path = get_cache_path(relative_path);
    spdlog::trace("[ThumbnailCache] Downloading for optimization: {} -> {}", relative_path,
                  cache_path);

    // Capture target and callbacks for the download completion handler
    api->download_thumbnail(
        relative_path, cache_path,
        // Success callback - PNG downloaded, now pre-scale it
        [this, on_success, on_error, relative_path, target](const std::string& local_path) {
            spdlog::trace("[ThumbnailCache] Downloaded, now pre-scaling: {}", local_path);
            evict_if_needed();

            // Process the downloaded PNG
            std::string lvgl_path = to_lvgl_path(local_path);
            process_and_callback(lvgl_path, relative_path, target, on_success, on_error);
        },
        // Error callback - download failed
        [on_error, relative_path](const MoonrakerError& error) {
            spdlog::warn("[ThumbnailCache] Optimized fetch failed for {}: {}", relative_path,
                         error.message);
            if (on_error) {
                on_error(error.message);
            }
        });
}

void ThumbnailCache::process_and_callback(const std::string& png_lvgl_path,
                                          const std::string& source_path,
                                          const helix::ThumbnailTarget& target,
                                          SuccessCallback on_success, ErrorCallback on_error) {
    // Read PNG file into memory
    std::string local_path = png_lvgl_path;
    if (is_lvgl_path(local_path)) {
        local_path = local_path.substr(2); // Remove "A:" prefix
    }

    std::ifstream file(local_path, std::ios::binary | std::ios::ate);
    if (!file) {
        spdlog::warn("[ThumbnailCache] Cannot read PNG for processing: {}", local_path);
        // Fallback: return PNG path (still works, just not optimized)
        if (on_success) {
            on_success(png_lvgl_path);
        }
        return;
    }

    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> png_data(size);
    if (!file.read(reinterpret_cast<char*>(png_data.data()), size)) {
        spdlog::warn("[ThumbnailCache] Failed to read PNG data: {}", local_path);
        // Fallback: return PNG path
        if (on_success) {
            on_success(png_lvgl_path);
        }
        return;
    }
    file.close();

    // Queue for background processing
    helix::ThumbnailProcessor::instance().process_async(
        png_data, source_path, target,
        // Success - return optimized path
        [on_success](const std::string& lvbin_path) {
            spdlog::debug("[ThumbnailCache] Pre-scaling complete: {}", lvbin_path);
            if (on_success) {
                on_success(lvbin_path);
            }
        },
        // Error - fallback to PNG
        [on_success, png_lvgl_path](const std::string& error) {
            spdlog::warn("[ThumbnailCache] Pre-scaling failed ({}), using PNG fallback", error);
            // Fallback: return PNG path (still works, just slower)
            if (on_success) {
                on_success(png_lvgl_path);
            }
        });
}
