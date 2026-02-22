// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file crash_history.h
 * @brief Persistent crash submission history for debug bundles
 *
 * Maintains a local record of crashes that have been submitted via
 * CrashReporter (to crash worker) or TelemetryManager (batched events).
 * The debug bundle collector includes this history so that crash data
 * can be cross-referenced with R2 telemetry data server-side.
 *
 * File: config/crash_history.json (array, capped at MAX_ENTRIES, FIFO)
 */

#include <mutex>
#include <string>
#include <vector>

#include "hv/json.hpp"

namespace helix {

struct CrashHistoryEntry {
    std::string timestamp;       // ISO 8601 when crash occurred
    int signal = 0;              // Signal number (e.g. 11)
    std::string signal_name;     // e.g. "SIGSEGV"
    std::string app_version;     // HelixScreen version at crash time
    int uptime_sec = 0;          // App uptime before crash
    std::string fault_addr;      // Fault address (if available)
    std::string fault_code_name; // e.g. "SEGV_MAPERR" (if available)
    int github_issue = 0;        // GitHub issue number (from crash worker)
    std::string github_url;      // GitHub issue URL (from crash worker)
    std::string sent_via;        // "crash_reporter" or "telemetry"
};

class CrashHistory {
  public:
    static constexpr size_t MAX_ENTRIES = 20;

    /**
     * @brief Initialize with config directory path
     * @param config_dir Directory containing crash_history.json
     */
    void init(const std::string& config_dir);

    /**
     * @brief Reset state (for testing)
     */
    void shutdown();

    /**
     * @brief Add a crash entry to the history
     *
     * Appends the entry, drops oldest if at MAX_ENTRIES, and persists to disk.
     */
    void add_entry(const CrashHistoryEntry& entry);

    /**
     * @brief Get all entries (thread-safe copy)
     */
    std::vector<CrashHistoryEntry> get_entries() const;

    /**
     * @brief Get number of entries
     */
    size_t size() const;

    /**
     * @brief Get entries as JSON array (for debug bundle inclusion)
     */
    nlohmann::json to_json() const;

    /**
     * @brief Singleton access
     */
    static CrashHistory& instance();

  private:
    CrashHistory() = default;

    void load();
    void save() const;
    nlohmann::json entries_to_json() const; // Caller must hold mutex_
    std::string history_file_path() const;

    static CrashHistoryEntry entry_from_json(const nlohmann::json& j);
    static nlohmann::json entry_to_json(const CrashHistoryEntry& entry);

    mutable std::mutex mutex_;
    std::vector<CrashHistoryEntry> entries_;
    std::string config_dir_;
    bool initialized_ = false;
};

} // namespace helix
