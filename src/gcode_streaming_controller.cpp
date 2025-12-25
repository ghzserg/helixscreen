// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_streaming_controller.h"

#include "memory_monitor.h"
#include "memory_utils.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <sstream>
#include <thread>

namespace helix {
namespace gcode {

// =============================================================================
// BackgroundGhostBuilder Implementation
// =============================================================================

BackgroundGhostBuilder::~BackgroundGhostBuilder() {
    cancel();
}

void BackgroundGhostBuilder::start(GCodeStreamingController* controller,
                                   RenderCallback render_callback) {
    // Cancel any existing build
    cancel();

    if (!controller || !controller->is_open()) {
        spdlog::warn("[GhostBuilder] Cannot start: controller not ready");
        return;
    }

    controller_ = controller;
    render_callback_ = std::move(render_callback);

    total_layers_.store(controller_->get_layer_count());
    current_layer_.store(0);
    complete_.store(false);
    cancelled_.store(false);
    running_.store(true);

    spdlog::info("[GhostBuilder] Starting background ghost build for {} layers",
                 total_layers_.load());

    worker_ = std::thread(&BackgroundGhostBuilder::worker_thread, this);
}

void BackgroundGhostBuilder::cancel() {
    // Signal cancellation
    cancelled_.store(true);

    // Always join if joinable - even if not running (thread may have completed)
    if (worker_.joinable()) {
        spdlog::debug("[GhostBuilder] Joining ghost build thread");
        worker_.join();
    }

    running_.store(false);
    cancelled_.store(false);
}

float BackgroundGhostBuilder::get_progress() const {
    size_t total = total_layers_.load();
    if (total == 0) {
        return complete_.load() ? 1.0f : 0.0f;
    }
    return static_cast<float>(current_layer_.load()) / static_cast<float>(total);
}

bool BackgroundGhostBuilder::is_complete() const {
    return complete_.load();
}

bool BackgroundGhostBuilder::is_running() const {
    return running_.load();
}

size_t BackgroundGhostBuilder::layers_rendered() const {
    return current_layer_.load();
}

size_t BackgroundGhostBuilder::total_layers() const {
    return total_layers_.load();
}

void BackgroundGhostBuilder::notify_user_request() {
    last_user_request_.store(std::chrono::steady_clock::now());
}

void BackgroundGhostBuilder::worker_thread() {
    spdlog::debug("[GhostBuilder] Worker thread started");

    size_t total = total_layers_.load();

    for (size_t i = 0; i < total && !cancelled_.load(); ++i) {
        // Yield to UI: pause if user recently navigated layers
        auto now = std::chrono::steady_clock::now();
        auto last_request = last_user_request_.load();
        while ((now - last_request) < YIELD_DURATION && !cancelled_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            now = std::chrono::steady_clock::now();
            last_request = last_user_request_.load();
        }

        if (cancelled_.load()) {
            break;
        }

        // Load layer segments
        const auto* segments = controller_->get_layer_segments(i);
        if (segments && render_callback_) {
            render_callback_(i, *segments);
        }

        current_layer_.store(i + 1);

        // Small yield between layers to avoid starving UI thread
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!cancelled_.load()) {
        complete_.store(true);
        spdlog::info("[GhostBuilder] Ghost build complete ({} layers)", current_layer_.load());
    } else {
        spdlog::debug("[GhostBuilder] Ghost build cancelled at layer {}/{}", current_layer_.load(),
                      total);
    }

    running_.store(false);
}

// Static member initialization
const LayerIndexStats GCodeStreamingController::empty_stats_{};

// =============================================================================
// Construction / Destruction
// =============================================================================

GCodeStreamingController::GCodeStreamingController()
    : cache_(GCodeLayerCache::DEFAULT_BUDGET_NORMAL) {
    // Select cache budget tier based on total system RAM
    auto mem = get_system_memory_info();
    size_t budget;
    const char* tier_name;

    if (mem.is_constrained_device()) {
        budget = GCodeLayerCache::DEFAULT_BUDGET_CONSTRAINED;
        tier_name = "constrained";
    } else if (mem.is_normal_device()) {
        budget = GCodeLayerCache::DEFAULT_BUDGET_NORMAL;
        tier_name = "normal";
    } else {
        budget = GCodeLayerCache::DEFAULT_BUDGET_GOOD;
        tier_name = "good";
    }

    cache_.set_memory_budget(budget);

    // Enable adaptive mode on constrained/normal devices (not desktop)
    if (!mem.is_good_device()) {
        cache_.set_adaptive_mode(true, 15, MIN_CACHE_BUDGET, budget);
    }

    spdlog::info("[StreamingController] {} device (total: {}MB, available: {}MB), "
                 "using {}MB cache budget{}",
                 tier_name, mem.total_mb(), mem.available_mb(), budget / (1024 * 1024),
                 mem.is_good_device() ? "" : " with adaptive mode");
}

GCodeStreamingController::GCodeStreamingController(size_t cache_budget_bytes)
    : cache_(std::max(cache_budget_bytes, MIN_CACHE_BUDGET)) {
    spdlog::debug("[StreamingController] Created with {:.1f}MB cache budget",
                  static_cast<double>(cache_budget_bytes) / (1024 * 1024));
}

GCodeStreamingController::~GCodeStreamingController() {
    // Wait for any async indexing to complete
    if (index_future_.valid()) {
        indexing_.store(false); // Signal cancellation
        try {
            index_future_.wait();
        } catch (...) {
            // Ignore exceptions during shutdown
        }
    }
}

// =============================================================================
// File Operations
// =============================================================================

bool GCodeStreamingController::open_file(const std::string& filepath) {
    close();

    spdlog::info("[StreamingController] Opening file: {}", filepath);

    // Create file data source
    auto source = std::make_unique<FileDataSource>(filepath);
    if (!source->is_valid()) {
        spdlog::error("[StreamingController] Failed to open file: {}", filepath);
        return false;
    }

    data_source_ = std::move(source);

    // Build index synchronously
    helix::MemoryMonitor::log_now("gcode_indexing_start");
    if (!build_index()) {
        spdlog::error("[StreamingController] Failed to build index for: {}", filepath);
        data_source_.reset();
        return false;
    }
    helix::MemoryMonitor::log_now("gcode_indexing_done");

    is_open_.store(true);
    spdlog::info("[StreamingController] Opened {} with {} layers", filepath,
                 index_.get_layer_count());

    return true;
}

void GCodeStreamingController::open_file_async(const std::string& filepath,
                                               std::function<void(bool)> on_complete) {
    close();

    spdlog::info("[StreamingController] Opening file async: {}", filepath);

    // Create file data source
    auto source = std::make_unique<FileDataSource>(filepath);
    if (!source->is_valid()) {
        spdlog::error("[StreamingController] Failed to open file: {}", filepath);
        if (on_complete) {
            on_complete(false);
        }
        return;
    }

    data_source_ = std::move(source);
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        index_complete_callback_ = on_complete;
    }
    indexing_.store(true);
    index_progress_.store(0.0f);

    // Build index in background thread
    index_future_ = std::async(std::launch::async, [this, filepath]() {
        bool success = build_index();

        indexing_.store(false);
        index_progress_.store(1.0f);

        if (success) {
            is_open_.store(true);
            spdlog::info("[StreamingController] Async open complete: {} layers",
                         index_.get_layer_count());
        } else {
            spdlog::error("[StreamingController] Async indexing failed");
            data_source_.reset();
        }

        // Capture callback under lock to prevent race with close()
        // The callback may have been nullified if close() was called
        std::function<void(bool)> callback;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback = index_complete_callback_;
        }

        if (callback) {
            spdlog::debug("[StreamingController] Invoking completion callback (success={})",
                          success);
            callback(success);
            spdlog::debug("[StreamingController] Completion callback returned");
        } else {
            spdlog::debug("[StreamingController] No completion callback registered");
        }

        return success;
    });
}

bool GCodeStreamingController::open_moonraker(const std::string& moonraker_url,
                                              const std::string& gcode_path) {
    close();

    spdlog::info("[StreamingController] Opening via Moonraker: {} / {}", moonraker_url, gcode_path);

    auto source = std::make_unique<MoonrakerDataSource>(moonraker_url, gcode_path);
    if (!source->is_valid()) {
        spdlog::error("[StreamingController] Failed to connect to Moonraker");
        return false;
    }

    data_source_ = std::move(source);

    // For Moonraker, we may need to download the file if range requests aren't supported
    auto* moonraker_src = static_cast<MoonrakerDataSource*>(data_source_.get());
    if (!moonraker_src->supports_range_requests()) {
        spdlog::warn("[StreamingController] Moonraker doesn't support Range requests, "
                     "downloading to temp file");
        if (!moonraker_src->download_to_temp()) {
            spdlog::error("[StreamingController] Failed to download file");
            data_source_.reset();
            return false;
        }
    }

    if (!build_index()) {
        spdlog::error("[StreamingController] Failed to build index");
        data_source_.reset();
        return false;
    }

    is_open_.store(true);
    return true;
}

bool GCodeStreamingController::open_source(std::unique_ptr<GCodeDataSource> source) {
    close();

    if (!source || !source->is_valid()) {
        spdlog::error("[StreamingController] Invalid data source");
        return false;
    }

    data_source_ = std::move(source);

    if (!build_index()) {
        spdlog::error("[StreamingController] Failed to build index from source");
        data_source_.reset();
        return false;
    }

    is_open_.store(true);
    return true;
}

void GCodeStreamingController::close() {
    // Wait for async operations
    if (index_future_.valid()) {
        indexing_.store(false);
        try {
            index_future_.wait();
        } catch (...) {
        }
    }

    // Clear callback under lock to prevent race with async invocation
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        index_complete_callback_ = nullptr;
    }

    cache_.clear();
    index_.clear();
    data_source_.reset();
    is_open_.store(false);

    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        metadata_extracted_ = false;
        header_metadata_.reset();
    }

    spdlog::debug("[StreamingController] Closed");
}

bool GCodeStreamingController::is_open() const {
    return is_open_.load() && !indexing_.load();
}

bool GCodeStreamingController::is_indexing() const {
    return indexing_.load();
}

float GCodeStreamingController::get_index_progress() const {
    if (!indexing_.load()) {
        return is_open_.load() ? 1.0f : 0.0f;
    }
    return index_progress_.load();
}

std::string GCodeStreamingController::get_source_name() const {
    if (data_source_) {
        return data_source_->source_name();
    }
    return "";
}

// =============================================================================
// Layer Access
// =============================================================================

const std::vector<ToolpathSegment>*
GCodeStreamingController::get_layer_segments(size_t layer_index) {
    if (!is_open() || layer_index >= index_.get_layer_count()) {
        return nullptr;
    }

    // Get from cache (loads if needed)
    auto result = cache_.get_or_load(layer_index, make_loader());

    if (result.load_failed) {
        spdlog::warn("[StreamingController] Failed to load layer {}", layer_index);
        return nullptr;
    }

    // Trigger prefetch for nearby layers
    prefetch_around(layer_index, prefetch_radius_);

    return result.segments;
}

void GCodeStreamingController::request_layer(size_t layer_index) {
    if (!is_open() || layer_index >= index_.get_layer_count()) {
        return;
    }

    // Just trigger the load (get_or_load handles caching)
    cache_.get_or_load(layer_index, make_loader());
}

bool GCodeStreamingController::is_layer_cached(size_t layer_index) const {
    return cache_.is_cached(layer_index);
}

void GCodeStreamingController::prefetch_around(size_t center_layer, size_t radius) {
    if (!is_open()) {
        return;
    }

    size_t layer_count = index_.get_layer_count();
    if (layer_count == 0) {
        return; // Nothing to prefetch
    }

    cache_.prefetch(center_layer, radius, make_loader(), layer_count - 1);
}

// =============================================================================
// Layer Information
// =============================================================================

size_t GCodeStreamingController::get_layer_count() const {
    return is_open_.load() ? index_.get_layer_count() : 0;
}

float GCodeStreamingController::get_layer_z(size_t layer_index) const {
    return index_.get_layer_z(layer_index);
}

int GCodeStreamingController::find_layer_at_z(float z) const {
    return index_.find_layer_at_z(z);
}

const LayerIndexStats& GCodeStreamingController::get_index_stats() const {
    if (index_.is_valid()) {
        return index_.get_stats();
    }
    return empty_stats_;
}

size_t GCodeStreamingController::get_file_size() const {
    return data_source_ ? data_source_->file_size() : 0;
}

// =============================================================================
// Cache Management
// =============================================================================

float GCodeStreamingController::get_cache_hit_rate() const {
    return cache_.hit_rate();
}

size_t GCodeStreamingController::get_cache_memory_usage() const {
    return cache_.memory_usage_bytes();
}

size_t GCodeStreamingController::get_cache_budget() const {
    return cache_.memory_budget_bytes();
}

void GCodeStreamingController::set_cache_budget(size_t budget_bytes) {
    cache_.set_memory_budget(std::max(budget_bytes, MIN_CACHE_BUDGET));
}

void GCodeStreamingController::set_adaptive_cache(bool enable) {
    if (enable) {
        // Use current budget as max (respects device tier set at construction)
        cache_.set_adaptive_mode(true, 15, MIN_CACHE_BUDGET, cache_.memory_budget_bytes());
    } else {
        cache_.set_adaptive_mode(false);
    }
}

void GCodeStreamingController::clear_cache() {
    cache_.clear();
}

void GCodeStreamingController::respond_to_memory_pressure() {
    cache_.respond_to_pressure(0.5f);
    spdlog::warn("[StreamingController] Responded to memory pressure, cache now at {:.1f}MB",
                 static_cast<double>(cache_.memory_usage_bytes()) / (1024 * 1024));
}

// =============================================================================
// Metadata Access
// =============================================================================

const GCodeHeaderMetadata* GCodeStreamingController::get_header_metadata() const {
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    return header_metadata_.get();
}

// =============================================================================
// Private Implementation
// =============================================================================

std::vector<ToolpathSegment> GCodeStreamingController::load_layer(size_t layer_index) {
    std::vector<ToolpathSegment> segments;

    if (!data_source_ || !index_.is_valid()) {
        return segments;
    }

    auto entry = index_.get_entry(layer_index);
    if (!entry.is_valid()) {
        spdlog::warn("[StreamingController] Invalid index entry for layer {}", layer_index);
        return segments;
    }

    // Read layer bytes from source
    auto bytes = data_source_->read_range(entry.file_offset, entry.byte_length);
    if (bytes.empty()) {
        spdlog::warn("[StreamingController] Failed to read bytes for layer {} "
                     "(offset={}, length={})",
                     layer_index, entry.file_offset, entry.byte_length);
        return segments;
    }

    // Parse the bytes line by line
    GCodeParser parser;
    std::istringstream stream(std::string(bytes.begin(), bytes.end()));
    std::string line;

    while (std::getline(stream, line)) {
        parser.parse_line(line);
    }

    // Get parsed result
    auto result = parser.finalize();

    // Extract metadata from first layer parsed (thread-safe)
    if (!result.layers.empty()) {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        if (!metadata_extracted_) {
            header_metadata_ = std::make_unique<GCodeHeaderMetadata>();
            header_metadata_->slicer = result.slicer_name;
            header_metadata_->filament_type = result.filament_type;
            header_metadata_->estimated_time_seconds = result.estimated_print_time_minutes * 60.0;
            header_metadata_->filament_used_mm = result.total_filament_mm;
            header_metadata_->layer_count = static_cast<uint32_t>(index_.get_layer_count());
            header_metadata_->tool_colors = result.tool_color_palette;
            metadata_extracted_ = true;
        }
    }

    // Collect all segments from all parsed layers
    // (usually just one layer, but parser may split on Z changes)
    for (const auto& layer : result.layers) {
        segments.insert(segments.end(), layer.segments.begin(), layer.segments.end());
    }

    spdlog::debug("[StreamingController] Loaded layer {} ({} segments, {} bytes)", layer_index,
                  segments.size(), bytes.size());

    return segments;
}

bool GCodeStreamingController::build_index() {
    if (!data_source_) {
        return false;
    }

    // For file sources, build index directly from file path
    auto* file_source = dynamic_cast<FileDataSource*>(data_source_.get());
    if (file_source) {
        return index_.build_from_file(file_source->filepath());
    }

    // For other sources (Moonraker with temp file fallback), check if we have a temp file
    auto* moonraker_source = dynamic_cast<MoonrakerDataSource*>(data_source_.get());
    if (moonraker_source && moonraker_source->is_using_temp_file()) {
        // Moonraker downloaded to temp file, build index from there
        // Note: We'd need to expose the temp file path - for now, read all and create memory source
        spdlog::warn("[StreamingController] Moonraker temp file indexing not yet implemented");
        return false;
    }

    // For memory sources or sources without file path, we need to read all bytes
    // and build index manually. This is less efficient but works for testing.
    auto* memory_source = dynamic_cast<MemoryDataSource*>(data_source_.get());
    if (memory_source) {
        // For memory sources, we need to write to a temp file for indexing
        // or implement a memory-based index builder
        // For now, just log and return false
        spdlog::warn("[StreamingController] Memory source requires file-based indexing");
        return false;
    }

    spdlog::error("[StreamingController] Unable to build index for source type");
    return false;
}

std::function<std::vector<ToolpathSegment>(size_t)> GCodeStreamingController::make_loader() {
    return [this](size_t layer_index) { return load_layer(layer_index); };
}

} // namespace gcode
} // namespace helix
