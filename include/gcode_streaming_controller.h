// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gcode_data_source.h"
#include "gcode_layer_cache.h"
#include "gcode_layer_index.h"
#include "gcode_parser.h"
#include "gcode_streaming_config.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace helix {
namespace gcode {

// Forward declaration
class GCodeStreamingController;

/**
 * @brief Builds ghost preview progressively in background for streaming mode
 *
 * Since streaming mode loads layers on-demand with LRU eviction, we can't
 * guarantee all layers are cached simultaneously for ghost rendering.
 * This builder iterates through all layers in the background, rendering
 * each to the ghost buffer via a callback.
 *
 * Features:
 * - Progressive rendering: ghost fills in as layers are processed
 * - UI yielding: pauses when user navigates to avoid lag
 * - Cancellation: stops promptly on file change or destruction
 *
 * Usage:
 * @code
 *   BackgroundGhostBuilder builder;
 *   builder.start(&controller, [](size_t layer, const auto& segments) {
 *       // Render segments to ghost buffer using Bresenham
 *   });
 *   // Query progress: builder.get_progress()
 *   // Check completion: builder.is_complete()
 * @endcode
 */
class BackgroundGhostBuilder {
  public:
    /// Callback type for rendering a layer's segments
    using RenderCallback =
        std::function<void(size_t layer_index, const std::vector<ToolpathSegment>& segments)>;

    BackgroundGhostBuilder() = default;
    ~BackgroundGhostBuilder();

    // Non-copyable, non-moveable
    BackgroundGhostBuilder(const BackgroundGhostBuilder&) = delete;
    BackgroundGhostBuilder& operator=(const BackgroundGhostBuilder&) = delete;
    BackgroundGhostBuilder(BackgroundGhostBuilder&&) = delete;
    BackgroundGhostBuilder& operator=(BackgroundGhostBuilder&&) = delete;

    /**
     * @brief Start building ghost preview in background
     *
     * @param controller Streaming controller to load layers from
     * @param render_callback Called for each layer with its segments
     */
    void start(GCodeStreamingController* controller, RenderCallback render_callback);

    /**
     * @brief Cancel the background build
     *
     * Signals the worker thread to stop. Blocks until thread exits.
     */
    void cancel();

    /**
     * @brief Get build progress as fraction
     * @return Progress from 0.0 (starting) to 1.0 (complete)
     */
    float get_progress() const;

    /**
     * @brief Check if build has completed all layers
     * @return true if all layers have been rendered
     */
    bool is_complete() const;

    /**
     * @brief Check if build is currently running
     * @return true if worker thread is active
     */
    bool is_running() const;

    /**
     * @brief Get number of layers rendered so far
     * @return Count of layers processed
     */
    size_t layers_rendered() const;

    /**
     * @brief Get total number of layers to render
     * @return Total layer count
     */
    size_t total_layers() const;

    /**
     * @brief Signal that UI has a pending layer request
     *
     * Call this when user navigates layers. The ghost builder will
     * pause briefly to let the UI load complete first.
     */
    void notify_user_request();

  private:
    void worker_thread();

    GCodeStreamingController* controller_{nullptr};
    RenderCallback render_callback_;

    std::thread worker_;
    std::atomic<size_t> current_layer_{0};
    std::atomic<size_t> total_layers_{0};
    std::atomic<bool> complete_{false};
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> running_{false};

    // UI yielding: timestamp of last user navigation
    std::atomic<std::chrono::steady_clock::time_point> last_user_request_{};
    static constexpr std::chrono::milliseconds YIELD_DURATION{50};
};

/**
 * @brief Orchestrates streaming G-code loading for memory-constrained devices
 *
 * The streaming controller provides on-demand layer loading by coordinating:
 * - GCodeLayerIndex: Maps layer numbers to file byte offsets (~24 bytes/layer)
 * - GCodeDataSource: Reads byte ranges from file or network
 * - GCodeLayerCache: LRU cache for parsed segment data
 * - GCodeParser: Converts raw G-code bytes to ToolpathSegments
 *
 * This enables viewing 10MB+ G-code files on devices with limited RAM (e.g.,
 * AD5M with 47MB) by loading only the layers currently being viewed.
 *
 * Usage:
 * @code
 *   GCodeStreamingController controller;
 *   if (controller.open_file("model.gcode")) {
 *       // Get segments for layer 42 (loads if not cached)
 *       auto* segments = controller.get_layer_segments(42);
 *       if (segments) {
 *           for (const auto& seg : *segments) {
 *               // Render segment...
 *           }
 *       }
 *   }
 * @endcode
 *
 * Memory usage: Index (~24 bytes × layers) + Cache (configurable budget)
 */
class GCodeStreamingController {
  public:
    /// Default prefetch radius (layers around current view to preload)
    static constexpr size_t DEFAULT_PREFETCH_RADIUS = 3;

    /// Minimum cache budget (1MB)
    static constexpr size_t MIN_CACHE_BUDGET = 1 * 1024 * 1024;

    /**
     * @brief Construct controller with default settings
     *
     * Uses adaptive cache budget based on available system memory.
     */
    GCodeStreamingController();

    /**
     * @brief Construct controller with explicit cache budget
     * @param cache_budget_bytes Maximum memory for layer cache
     */
    explicit GCodeStreamingController(size_t cache_budget_bytes);

    ~GCodeStreamingController();

    // Non-copyable, non-moveable
    GCodeStreamingController(const GCodeStreamingController&) = delete;
    GCodeStreamingController& operator=(const GCodeStreamingController&) = delete;
    GCodeStreamingController(GCodeStreamingController&&) = delete;
    GCodeStreamingController& operator=(GCodeStreamingController&&) = delete;

    // =========================================================================
    // File Operations
    // =========================================================================

    /**
     * @brief Open a local G-code file for streaming
     *
     * Builds the layer index (single-pass scan) and prepares for streaming.
     * For large files, consider using open_file_async() to avoid blocking UI.
     *
     * @param filepath Path to G-code file
     * @return true if successful
     */
    bool open_file(const std::string& filepath);

    /**
     * @brief Open a local file asynchronously (background index building)
     *
     * Returns immediately. Use is_ready() to check when indexing is complete.
     * Progress can be monitored via get_index_progress().
     *
     * @param filepath Path to G-code file
     * @param on_complete Optional callback when indexing completes (bool success)
     */
    void open_file_async(const std::string& filepath,
                         std::function<void(bool)> on_complete = nullptr);

    /**
     * @brief Open a G-code file via Moonraker API
     *
     * Uses HTTP range requests for efficient streaming access.
     *
     * @param moonraker_url Base Moonraker URL (e.g., "http://192.168.1.100:7125")
     * @param gcode_path G-code file path on printer
     * @return true if successful
     */
    bool open_moonraker(const std::string& moonraker_url, const std::string& gcode_path);

    /**
     * @brief Open from an existing data source
     *
     * Takes ownership of the data source. Useful for custom sources.
     *
     * @param source Data source (ownership transferred)
     * @return true if successful
     */
    bool open_source(std::unique_ptr<GCodeDataSource> source);

    /**
     * @brief Close current file and release resources
     */
    void close();

    /**
     * @brief Check if a file is open and ready
     * @return true if ready for layer access
     */
    bool is_open() const;

    /**
     * @brief Check if async open is still in progress
     * @return true if indexing is running
     */
    bool is_indexing() const;

    /**
     * @brief Get indexing progress (0.0 to 1.0)
     * @return Progress fraction, or 1.0 if complete
     */
    float get_index_progress() const;

    /**
     * @brief Get source file/URL name
     * @return Source identifier
     */
    std::string get_source_name() const;

    // =========================================================================
    // Layer Access
    // =========================================================================

    /**
     * @brief Get parsed segments for a layer
     *
     * Returns cached data if available, otherwise loads from source.
     * Thread-safe but blocks if loading is needed.
     *
     * @param layer_index Zero-based layer index
     * @return Shared pointer to segment vector, or nullptr if layer doesn't exist.
     *         Data stays valid as long as the shared_ptr is held, even if the
     *         cache entry is evicted. This is critical for thread safety.
     *
     * @note For background loading, use request_layer() + is_layer_ready()
     */
    std::shared_ptr<const std::vector<ToolpathSegment>> get_layer_segments(size_t layer_index);

    /**
     * @brief Request a layer to be loaded (non-blocking)
     *
     * If layer is not cached, queues it for background loading.
     * Check is_layer_ready() or get_layer_segments() later.
     *
     * @param layer_index Zero-based layer index
     */
    void request_layer(size_t layer_index);

    /**
     * @brief Check if a layer is cached and ready
     * @param layer_index Zero-based layer index
     * @return true if layer can be accessed immediately
     */
    bool is_layer_cached(size_t layer_index) const;

    /**
     * @brief Prefetch layers around current view
     *
     * Loads layers in range [center - radius, center + radius].
     * Called automatically by get_layer_segments() but can be
     * called explicitly for more control.
     *
     * @param center_layer Center layer index
     * @param radius Number of layers on each side (default: 3)
     */
    void prefetch_around(size_t center_layer, size_t radius = DEFAULT_PREFETCH_RADIUS);

    // =========================================================================
    // Layer Information
    // =========================================================================

    /**
     * @brief Get total number of layers
     * @return Layer count, or 0 if not open
     */
    size_t get_layer_count() const;

    /**
     * @brief Get Z height for a layer
     * @param layer_index Zero-based layer index
     * @return Z height in mm, or 0.0 if invalid
     */
    float get_layer_z(size_t layer_index) const;

    /**
     * @brief Find layer closest to Z height
     * @param z Z coordinate
     * @return Layer index, or -1 if no layers
     */
    int find_layer_at_z(float z) const;

    /**
     * @brief Get layer index statistics
     * @return Statistics from index building
     */
    const LayerIndexStats& get_index_stats() const;

    /**
     * @brief Get file size
     * @return File size in bytes, or 0 if not open
     */
    size_t get_file_size() const;

    // =========================================================================
    // Cache Management
    // =========================================================================

    /**
     * @brief Get cache hit rate
     * @return Hit rate as fraction [0.0, 1.0]
     */
    float get_cache_hit_rate() const;

    /**
     * @brief Get current cache memory usage
     * @return Bytes used
     */
    size_t get_cache_memory_usage() const;

    /**
     * @brief Get cache memory budget
     * @return Maximum bytes allowed
     */
    size_t get_cache_budget() const;

    /**
     * @brief Set new cache budget
     * @param budget_bytes New budget in bytes
     */
    void set_cache_budget(size_t budget_bytes);

    /**
     * @brief Enable adaptive cache budget based on system memory
     * @param enable true to enable
     */
    void set_adaptive_cache(bool enable);

    /**
     * @brief Clear the layer cache
     */
    void clear_cache();

    /**
     * @brief Trigger memory pressure response
     *
     * Call when system memory is low. Reduces cache and evicts entries.
     */
    void respond_to_memory_pressure();

    // =========================================================================
    // Metadata Access
    // =========================================================================

    /**
     * @brief Get header metadata (slicer info, print time, etc.)
     *
     * Only populated after first layer is parsed.
     *
     * @return Pointer to metadata, or nullptr if not available
     */
    const GCodeHeaderMetadata* get_header_metadata() const;

  private:
    /**
     * @brief Load a layer from source and parse to segments
     * @param layer_index Layer to load
     * @return Parsed segments
     */
    std::vector<ToolpathSegment> load_layer(size_t layer_index);

    /**
     * @brief Build index from current data source
     * @return true if successful
     */
    bool build_index();

    /**
     * @brief Create loader function for cache
     * @return Loader lambda
     */
    std::function<std::vector<ToolpathSegment>(size_t)> make_loader();

    // Components (order matters for destruction)
    std::unique_ptr<GCodeDataSource> data_source_;
    GCodeLayerIndex index_;
    GCodeLayerCache cache_;

    // Async indexing
    std::future<bool> index_future_;
    std::atomic<bool> indexing_{false};
    std::atomic<float> index_progress_{0.0f};
    mutable std::mutex callback_mutex_; // Protects index_complete_callback_
    std::function<void(bool)> index_complete_callback_;

    // Metadata (populated lazily)
    mutable std::mutex metadata_mutex_;
    std::unique_ptr<GCodeHeaderMetadata> header_metadata_;
    bool metadata_extracted_{false};

    // State
    std::atomic<bool> is_open_{false};
    size_t prefetch_radius_{DEFAULT_PREFETCH_RADIUS};

    // Empty stats for when not open
    static const LayerIndexStats empty_stats_;
};

} // namespace gcode
} // namespace helix
