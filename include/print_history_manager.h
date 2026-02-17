// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "print_history_data.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class MoonrakerAPI;
namespace helix {
class MoonrakerClient;
}

/**
 * @brief Per-filename aggregated print history stats
 *
 * Used by PrintSelectPanel to show status indicators:
 * - success_count: Number of completed prints (shows as "N ✓")
 * - failure_count: Number of failed/cancelled prints
 * - last_status: Status of most recent print (determines icon)
 */
struct PrintHistoryStats {
    int success_count = 0; ///< Count of COMPLETED jobs for this filename
    int failure_count = 0; ///< Count of CANCELLED + ERROR jobs
    PrintJobStatus last_status = PrintJobStatus::UNKNOWN; ///< Status of most recent job
    double last_print_time = 0.0;                         ///< Unix timestamp of most recent job
    std::string uuid;      ///< UUID from most recent job for this filename
    size_t size_bytes = 0; ///< Size from most recent job for this filename
};

namespace helix {
/// Observer callback when history data changes
using HistoryChangedCallback = std::function<void()>;
} // namespace helix

/**
 * @brief Centralized print history cache with observer notification
 *
 * PrintHistoryManager provides a single source of truth for print history,
 * serving both the History panels and PrintSelectPanel status indicators.
 *
 * ## Data Views
 *
 * Two views of the same cached data:
 * 1. **Raw jobs list** (`get_jobs()`) - For HistoryDashboardPanel, HistoryListPanel
 * 2. **Filename stats map** (`get_filename_stats()`) - For PrintSelectPanel status indicators
 *
 * ## Usage Example
 *
 * ```cpp
 * // In panel constructor
 * manager_->add_observer([this]() { on_history_changed(); });
 *
 * // In on_activate
 * if (!manager_->is_loaded()) {
 *     manager_->fetch();
 * } else {
 *     update_from_history();
 * }
 *
 * // In on_history_changed
 * update_from_history();
 * ```
 *
 * ## Cache Invalidation
 *
 * The manager subscribes to Moonraker's `notify_history_changed` notification
 * and automatically invalidates + re-fetches when a print completes.
 *
 * @see PrintHistoryStats for per-file aggregation structure
 * @see PrintHistoryJob for raw job data structure
 */
class PrintHistoryManager {
  public:
    /**
     * @brief Construct PrintHistoryManager with API and client references
     *
     * @param api MoonrakerAPI for fetching history
     * @param client helix::MoonrakerClient for notification subscription
     */
    PrintHistoryManager(MoonrakerAPI* api, helix::MoonrakerClient* client);

    ~PrintHistoryManager();

    // Non-copyable
    PrintHistoryManager(const PrintHistoryManager&) = delete;
    PrintHistoryManager& operator=(const PrintHistoryManager&) = delete;

    // ========================================================================
    // Data Access
    // ========================================================================

    /**
     * @brief Get raw jobs list (for History panels)
     * @return Reference to cached jobs vector
     */
    [[nodiscard]] const std::vector<PrintHistoryJob>& get_jobs() const {
        return cached_jobs_;
    }

    /**
     * @brief Get per-filename stats map (for PrintSelectPanel)
     * @return Reference to aggregated stats map (key = basename, no path)
     */
    [[nodiscard]] const std::unordered_map<std::string, PrintHistoryStats>&
    get_filename_stats() const {
        return filename_stats_;
    }

    /**
     * @brief Check if history data has been loaded
     * @return true if fetch has completed at least once
     */
    [[nodiscard]] bool is_loaded() const {
        return is_loaded_;
    }

    /**
     * @brief Get jobs filtered by start time
     *
     * Returns jobs where `start_time >= since`. Used by HistoryDashboardPanel
     * for time-based filtering (TODAY, WEEK, MONTH, etc.).
     *
     * @param since Unix timestamp threshold (jobs before this are excluded)
     * @return Vector of matching jobs (by value, allows filtering)
     */
    [[nodiscard]] std::vector<PrintHistoryJob> get_jobs_since(double since) const;

    // ========================================================================
    // Fetch / Refresh
    // ========================================================================

    /**
     * @brief Fetch history from Moonraker asynchronously
     *
     * Calls `get_history_list()` and populates both `cached_jobs_` and
     * `filename_stats_`. Notifies all observers when complete.
     *
     * Concurrent calls are ignored (only one fetch in progress at a time).
     *
     * @param limit Maximum number of jobs to fetch (default 500)
     */
    void fetch(int limit = 500);

    /**
     * @brief Mark cache as stale
     *
     * Clears `is_loaded_` flag. Does NOT clear cached data (allows
     * stale-while-revalidate pattern).
     */
    void invalidate();

    // ========================================================================
    // Observer Pattern
    // ========================================================================

    /**
     * @brief Register observer callback by pointer
     *
     * Callback is invoked (on main thread) when:
     * - fetch() completes successfully
     * - Cache is invalidated and re-fetched (via notify_history_changed)
     *
     * IMPORTANT: Pass the address of a member variable, not a temporary.
     * The pointer must remain valid until remove_observer() is called.
     *
     * @param cb Pointer to callback function (stored, not copied)
     */
    void add_observer(helix::HistoryChangedCallback* cb);

    /**
     * @brief Remove observer callback by pointer
     *
     * Removes the callback registered with add_observer(). Uses pointer
     * comparison, so this actually works (unlike std::function comparison).
     *
     * @param cb Pointer to callback to remove
     */
    void remove_observer(helix::HistoryChangedCallback* cb);

  private:
    /**
     * @brief Handle completed fetch (runs on main thread)
     */
    void on_history_fetched(std::vector<PrintHistoryJob>&& jobs);

    /**
     * @brief Build filename_stats_ from cached_jobs_
     *
     * Aggregates jobs by basename (strips path), counting successes/failures
     * and tracking the most recent job's status.
     */
    void build_filename_stats();

    /**
     * @brief Call all registered observers
     */
    void notify_observers();

    /**
     * @brief Subscribe to Moonraker's notify_history_changed
     *
     * Called in constructor. When notification fires, invalidates
     * cache and triggers re-fetch.
     */
    void subscribe_to_notifications();

    // Dependencies
    MoonrakerAPI* api_;
    helix::MoonrakerClient* client_;

    // Cached data
    std::vector<PrintHistoryJob> cached_jobs_;
    std::unordered_map<std::string, PrintHistoryStats> filename_stats_;

    // Observers (stored as pointers for reliable removal)
    std::vector<helix::HistoryChangedCallback*> observers_;

    // State
    bool is_loaded_ = false;
    bool is_fetching_ = false;

    /// Guard for async callback safety [L012]
    /// Prevents use-after-free when callbacks fire after destruction
    std::shared_ptr<bool> callback_guard_ = std::make_shared<bool>(true);
};
