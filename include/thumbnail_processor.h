// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>

// Forward declarations
class HThreadPool;

/**
 * @file thumbnail_processor.h
 * @brief Background thumbnail pre-scaling for optimal display performance
 *
 * This class addresses a critical performance issue on embedded displays:
 * LVGL scales large thumbnails (300x300) to display size (~140x150) every frame
 * when using inner_align="contain". On ARM devices without GPU (like AD5M),
 * this causes severe UI lag during scrolling.
 *
 * Solution: Pre-scale thumbnails once at download time, store as raw LVGL binary,
 * display at 1:1 with zero runtime scaling.
 *
 * @see docs/THUMBNAIL_OPTIMIZATION_PLAN.md for full architecture
 */

namespace helix {

/**
 * @brief Target dimensions and format for pre-scaled thumbnails
 *
 * Determined by display breakpoint and card layout. Thumbnails are scaled
 * to the smallest size that fully covers the target, preserving aspect ratio.
 */
struct ThumbnailTarget {
    int width = 160;  ///< Target width in pixels
    int height = 160; ///< Target height in pixels

    /**
     * @brief Color format for output
     *
     * ARGB8888 (0x10) is default - matches AD5M framebuffer format.
     * RGB565 (0x12) can be used for smaller files but loses alpha.
     */
    uint8_t color_format = 0x10; // LV_COLOR_FORMAT_ARGB8888

    bool operator==(const ThumbnailTarget& other) const {
        return width == other.width && height == other.height && color_format == other.color_format;
    }
};

/**
 * @brief Result of thumbnail processing operation
 */
struct ProcessResult {
    bool success = false;
    std::string output_path; ///< Path to .bin file (empty on failure)
    std::string error;       ///< Error message (empty on success)
    int output_width = 0;    ///< Actual output width (may differ due to aspect ratio)
    int output_height = 0;   ///< Actual output height
};

/**
 * @brief Callback types for async processing
 */
using ProcessSuccessCallback = std::function<void(const std::string& lvbin_path)>;
using ProcessErrorCallback = std::function<void(const std::string& error)>;

/**
 * @brief Background thumbnail processor with thread pool
 *
 * Decodes PNG thumbnails, resizes them to target dimensions, and writes
 * LVGL-native binary files (.bin) for zero-overhead display.
 *
 * Thread-safe: All public methods can be called from any thread.
 *
 * Example usage:
 * @code
 *   auto& processor = ThumbnailProcessor::instance();
 *
 *   // Check if already processed
 *   std::string cached = processor.get_if_processed("/path/thumb.png", target);
 *   if (!cached.empty()) {
 *       lv_image_set_src(img, cached.c_str());
 *       return;
 *   }
 *
 *   // Process in background
 *   processor.process_async(png_data, "/path/thumb.png", target,
 *       [](const std::string& path) { lv_image_set_src(img, path.c_str()); },
 *       [](const std::string& err) { spdlog::warn("Failed: {}", err); });
 * @endcode
 */
class ThumbnailProcessor {
  public:
    /**
     * @brief Get the singleton instance
     *
     * Creates the processor on first call with a 2-thread pool.
     */
    static ThumbnailProcessor& instance();

    // Non-copyable, non-movable (singleton)
    ThumbnailProcessor(const ThumbnailProcessor&) = delete;
    ThumbnailProcessor& operator=(const ThumbnailProcessor&) = delete;

    /**
     * @brief Process PNG data asynchronously
     *
     * Decodes the PNG, resizes to target dimensions, converts to LVGL format,
     * and writes to cache. Callbacks are invoked on worker thread.
     *
     * @param png_data Raw PNG file contents
     * @param source_path Original thumbnail path (used for cache key generation)
     * @param target Target dimensions and format
     * @param on_success Called with path to .bin file on success
     * @param on_error Called with error message on failure
     */
    void process_async(const std::vector<uint8_t>& png_data, const std::string& source_path,
                       const ThumbnailTarget& target, ProcessSuccessCallback on_success,
                       ProcessErrorCallback on_error);

    /**
     * @brief Process PNG data synchronously
     *
     * Blocks until processing is complete. Prefer process_async() for UI code.
     *
     * @param png_data Raw PNG file contents
     * @param source_path Original thumbnail path (used for cache key generation)
     * @param target Target dimensions and format
     * @return ProcessResult with success/failure and output path
     */
    ProcessResult process_sync(const std::vector<uint8_t>& png_data, const std::string& source_path,
                               const ThumbnailTarget& target);

    /**
     * @brief Check if a pre-scaled version exists in cache
     *
     * Fast synchronous lookup - does not trigger processing.
     *
     * @param source_path Original thumbnail path
     * @param target Target dimensions and format
     * @return LVGL path (A:/...) to .bin if cached, empty string otherwise
     */
    std::string get_if_processed(const std::string& source_path,
                                 const ThumbnailTarget& target) const;

    /**
     * @brief Get optimal thumbnail target for current display
     *
     * Returns target dimensions based on display breakpoint:
     * - SMALL (<=480px): 120x120
     * - MEDIUM (<=800px): 160x160
     * - LARGE (>800px): 220x220
     *
     * @return ThumbnailTarget for current display configuration
     */
    static ThumbnailTarget get_target_for_display();

    /**
     * @brief Get the cache directory path
     * @return Path to thumbnail cache directory (e.g., /tmp/helix_thumbs)
     */
    std::string get_cache_dir() const {
        return cache_dir_;
    }

    /**
     * @brief Set the cache directory path
     *
     * Must be called before any processing. Creates directory if needed.
     *
     * @param path Directory path for cached .bin files
     */
    void set_cache_dir(const std::string& path);

    /**
     * @brief Clear all cached pre-scaled thumbnails
     *
     * Removes all .bin files from cache directory.
     * Thread-safe but may block briefly.
     */
    void clear_cache();

    /**
     * @brief Get number of pending processing tasks
     */
    size_t pending_tasks() const;

    /**
     * @brief Wait for all pending tasks to complete
     *
     * Useful for testing or graceful shutdown.
     */
    void wait_for_completion();

    /**
     * @brief Shutdown the processor
     *
     * Stops the thread pool and waits for pending tasks.
     * Called automatically on destruction.
     */
    void shutdown();

  private:
    ThumbnailProcessor();
    ~ThumbnailProcessor();

    /**
     * @brief Generate cache filename for a source/target combination
     *
     * Format: {hash}_{w}x{h}_{format}.bin
     * Example: a1b2c3d4_160x160_ARGB8888.bin
     */
    std::string generate_cache_filename(const std::string& source_path,
                                        const ThumbnailTarget& target) const;

    /**
     * @brief Core processing implementation
     *
     * 1. Decode PNG with stb_image
     * 2. Calculate output dimensions (preserve aspect, cover target)
     * 3. Resize with stb_image_resize (high-quality Mitchell filter)
     * 4. Convert to ARGB8888 if needed
     * 5. Write LVGL binary header + pixel data
     *
     * @param cache_dir Cache directory path (passed explicitly for thread safety)
     */
    ProcessResult do_process(const std::vector<uint8_t>& png_data, const std::string& source_path,
                             const ThumbnailTarget& target, const std::string& cache_dir);

    /**
     * @brief Write LVGL binary file
     *
     * Format: 12-byte lv_image_header_t followed by raw pixel data
     */
    bool write_lvbin(const std::string& path, int width, int height, uint8_t color_format,
                     const uint8_t* pixel_data, size_t data_size);

    std::unique_ptr<HThreadPool> thread_pool_;
    std::string cache_dir_;
    mutable std::mutex mutex_;
    bool shutdown_ = false;
};

} // namespace helix
