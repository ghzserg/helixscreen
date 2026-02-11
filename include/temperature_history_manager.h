// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "printer_state.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Single temperature sample with timestamp
 *
 * Uses centidegrees (x10) for precision without floating point.
 * Example: 2053 = 205.3°C
 */
struct TempSample {
    int temp_centi = 0;       ///< Temperature × 10 (e.g., 2053 = 205.3°C)
    int target_centi = 0;     ///< Target temperature × 10
    int64_t timestamp_ms = 0; ///< Unix timestamp in milliseconds
};

/**
 * @brief Heater type classification
 */
enum class HeaterType { EXTRUDER, BED, CHAMBER };

/**
 * @brief Manages temperature history collection for all heaters
 *
 * Collects temperature samples from PrinterState subjects at app startup,
 * stores 20 minutes of history (1200 samples @ 1Hz) per heater,
 * and provides observer notifications when new samples arrive.
 *
 * ## Thread Safety
 * - Data reads (get_samples, get_sample_count) are protected by mutex
 * - Writes are expected from the main thread via subject observers
 *
 * ## Usage Example
 *
 * ```cpp
 * // Create manager with PrinterState reference
 * TemperatureHistoryManager manager(printer_state);
 *
 * // Register observer for updates
 * TemperatureHistoryManager::HistoryCallback cb = [](const std::string& heater) {
 *     spdlog::info("New sample for {}", heater);
 * };
 * manager.add_observer(&cb);
 *
 * // Query history
 * auto samples = manager.get_samples("extruder");
 * auto recent = manager.get_samples_since("heater_bed", now_ms - 60000); // last minute
 * ```
 */
class TemperatureHistoryManager {
  public:
    static constexpr int HISTORY_SIZE = 1200;           ///< 20 minutes at 1Hz
    static constexpr int64_t SAMPLE_INTERVAL_MS = 1000; ///< 1 second minimum between samples
    static constexpr int64_t RECENT_SAMPLE_WINDOW_MS =
        100; ///< Window for retroactive target updates

    /**
     * @brief Construct TemperatureHistoryManager with PrinterState reference
     *
     * Pre-populates heater map with "extruder" and "heater_bed".
     * Subscribes to temperature subjects for automatic sample collection.
     *
     * @param printer_state Reference to PrinterState for subject subscription
     */
    explicit TemperatureHistoryManager(PrinterState& printer_state);

    /**
     * @brief Destructor - unsubscribes from subjects
     */
    ~TemperatureHistoryManager();

    // Non-copyable
    TemperatureHistoryManager(const TemperatureHistoryManager&) = delete;
    TemperatureHistoryManager& operator=(const TemperatureHistoryManager&) = delete;

    // ========================================================================
    // Data Access (thread-safe reads)
    // ========================================================================

    /**
     * @brief Get all samples for a heater
     *
     * Returns samples in chronological order (oldest first).
     *
     * @param heater_name Heater name (e.g., "extruder", "heater_bed")
     * @return Vector of samples, oldest first. Empty if heater unknown.
     */
    [[nodiscard]] std::vector<TempSample> get_samples(const std::string& heater_name) const;

    /**
     * @brief Get samples since a given timestamp
     *
     * Returns only samples with timestamp_ms > since_ms.
     *
     * @param heater_name Heater name
     * @param since_ms Unix timestamp in ms - return samples newer than this
     * @return Vector of samples since timestamp, oldest first
     */
    [[nodiscard]] std::vector<TempSample> get_samples_since(const std::string& heater_name,
                                                            int64_t since_ms) const;

    /**
     * @brief Get list of known heater names
     *
     * Returns at minimum "extruder" and "heater_bed".
     *
     * @return Vector of heater names
     */
    [[nodiscard]] std::vector<std::string> get_heater_names() const;

    /**
     * @brief Get number of samples stored for a heater
     *
     * @param heater_name Heater name
     * @return Sample count (0 to HISTORY_SIZE), 0 if heater unknown
     */
    [[nodiscard]] int get_sample_count(const std::string& heater_name) const;

    // ========================================================================
    // Observer Pattern
    // ========================================================================

    /**
     * @brief Callback type for history change notifications
     *
     * Called when new samples are stored. Parameter is the heater name.
     */
    using HistoryCallback = std::function<void(const std::string& heater_name)>;

    /**
     * @brief Register observer for history changes
     *
     * Callback is invoked when a sample is stored (not throttled).
     * Uses pointer-based registration for reliable removal.
     *
     * @param cb Pointer to callback function (caller owns memory)
     */
    void add_observer(HistoryCallback* cb);

    /**
     * @brief Unregister observer
     *
     * @param cb Pointer to callback previously registered
     */
    void remove_observer(HistoryCallback* cb);

    /**
     * @brief Get cached target temperature for a heater
     * @param heater_name Heater name
     * @return Target in centidegrees
     */
    [[nodiscard]] int get_cached_target(const std::string& heater_name) const;

    /**
     * @brief Set cached target temperature for a heater
     * @param heater_name Heater name
     * @param target_centi Target in centidegrees
     */
    void set_cached_target(const std::string& heater_name, int target_centi);

    /**
     * @brief Update target in the most recent sample if stored recently
     *
     * Used when target is set after temp in the same update cycle.
     * Updates the most recent sample's target if it was stored within
     * RECENT_SAMPLE_WINDOW_MS milliseconds.
     *
     * @param heater_name Heater name
     * @param target_centi New target value
     */
    void update_recent_sample_target(const std::string& heater_name, int target_centi);

  private:
    friend class TemperatureHistoryManagerTestAccess;

    /**
     * @brief Per-heater circular buffer for temperature samples
     */
    struct HeaterHistory {
        std::array<TempSample, HISTORY_SIZE> samples{}; ///< Circular buffer
        int write_index = 0;        ///< Next write position (0 to HISTORY_SIZE-1)
        int count = 0;              ///< Number of samples stored (0 to HISTORY_SIZE)
        int64_t last_sample_ms = 0; ///< Timestamp of last stored sample (for throttling)
    };

    /**
     * @brief Add a sample to heater history (internal, must hold mutex)
     *
     * @param heater_name Heater name
     * @param temp_centi Temperature in centidegrees
     * @param target_centi Target in centidegrees
     * @param timestamp_ms Timestamp in milliseconds
     * @return true if sample was stored, false if throttled
     */
    bool add_sample_internal(const std::string& heater_name, int temp_centi, int target_centi,
                             int64_t timestamp_ms);

    /**
     * @brief Notify all registered observers
     *
     * @param heater_name Heater that received new sample
     */
    void notify_observers(const std::string& heater_name);

    /**
     * @brief Subscribe to PrinterState temperature subjects
     */
    void subscribe_to_subjects();

    /**
     * @brief Unsubscribe from PrinterState temperature subjects
     */
    void unsubscribe_from_subjects();

    /**
     * @brief Static callback for temperature observer notifications
     *
     * Called by LVGL when temperature subjects change. Implemented as static
     * member to access private ObserverContext struct.
     */
    static void temp_observer_callback(lv_observer_t* observer, lv_subject_t* subject);

    /**
     * @brief Static callback for target temperature observer notifications
     *
     * Called by LVGL when target subjects change. Updates cached target
     * and retroactively patches recent samples.
     */
    static void target_observer_callback(lv_observer_t* observer, lv_subject_t* subject);

    // Dependencies
    PrinterState& printer_state_;

    // Per-heater circular buffers
    std::unordered_map<std::string, HeaterHistory> heaters_;

    // Cached targets (updated by target subject observers)
    // Thread-safety note: These are only accessed from the main thread via LVGL
    // observer callbacks. No mutex protection needed as LVGL runs single-threaded.
    int cached_extruder_target_ = 0;
    int cached_bed_target_ = 0;

    // Thread safety
    mutable std::mutex mutex_;

    // Observers (stored as pointers for reliable removal)
    std::vector<HistoryCallback*> observers_;

    /**
     * @brief Context for tracking initial observer callback skip
     *
     * Implementation detail used by static observer callbacks in the .cpp file.
     * Contains the manager pointer, heater name, and a flag to skip the initial
     * callback that fires during observer registration.
     */
    struct ObserverContext {
        TemperatureHistoryManager* manager = nullptr;
        bool first_callback_skipped = false;
        std::string heater_name; ///< Which heater this context is for
    };

    // Observer contexts (for skipping initial callback and tracking heater name)
    std::unique_ptr<ObserverContext> extruder_temp_ctx_;
    std::unique_ptr<ObserverContext> bed_temp_ctx_;
    std::unique_ptr<ObserverContext> extruder_target_ctx_;
    std::unique_ptr<ObserverContext> bed_target_ctx_;

    // LVGL observer guards for automatic cleanup
    ObserverGuard extruder_temp_observer_;
    ObserverGuard bed_temp_observer_;
    ObserverGuard extruder_target_observer_;
    ObserverGuard bed_target_observer_;
};
