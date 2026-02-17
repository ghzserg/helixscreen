// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "print_history_manager.h"

#include "ui_update_queue.h"

#include "moonraker_api.h"
#include "moonraker_client.h"

#include <spdlog/spdlog.h>

#include <algorithm>

using namespace helix;

// ============================================================================
// Construction / Destruction
// ============================================================================

PrintHistoryManager::PrintHistoryManager(MoonrakerAPI* api, MoonrakerClient* client)
    : api_(api), client_(client) {
    spdlog::debug("[HistoryManager] Created");
    subscribe_to_notifications();
}

PrintHistoryManager::~PrintHistoryManager() {
    // Unregister notification callback
    if (client_) {
        client_->unregister_method_callback("notify_history_changed", "PrintHistoryManager");
    }
}

// ============================================================================
// Fetch / Refresh
// ============================================================================

void PrintHistoryManager::fetch(int limit) {
    if (is_fetching_) {
        spdlog::debug("[HistoryManager] Fetch already in progress, ignoring");
        return;
    }

    if (!api_) {
        spdlog::warn("[HistoryManager] No API available, cannot fetch");
        return;
    }

    is_fetching_ = true;
    spdlog::debug("[HistoryManager] Fetching history (limit={})", limit);

    // Capture weak_ptr for async callback safety [L012]
    std::weak_ptr<bool> weak_guard = callback_guard_;

    api_->get_history_list(
        limit, 0, 0.0, 0.0, // limit, start, since, before
        [this, weak_guard](const std::vector<PrintHistoryJob>& jobs, uint64_t /*total*/) {
            // Copy jobs since callback param is const ref
            std::vector<PrintHistoryJob> jobs_copy = jobs;

            // Dispatch to main thread with guard check
            helix::ui::queue_update([this, weak_guard, jobs = std::move(jobs_copy)]() mutable {
                if (!weak_guard.lock()) {
                    return; // Object destroyed, abort
                }
                on_history_fetched(std::move(jobs));
            });
        },
        [this, weak_guard](const MoonrakerError& error) {
            spdlog::warn("[HistoryManager] Failed to fetch history: {}", error.message);
            // Dispatch to main thread with guard check
            helix::ui::queue_update([this, weak_guard]() {
                if (!weak_guard.lock()) {
                    return; // Object destroyed, abort
                }
                is_fetching_ = false;
            });
        });
}

void PrintHistoryManager::invalidate() {
    spdlog::debug("[HistoryManager] Cache invalidated");
    is_loaded_ = false;
}

// ============================================================================
// Observer Pattern
// ============================================================================

void PrintHistoryManager::add_observer(HistoryChangedCallback* cb) {
    if (cb && *cb) {
        observers_.push_back(cb);
        spdlog::debug("[HistoryManager] Added observer (total: {})", observers_.size());
    }
}

void PrintHistoryManager::remove_observer(HistoryChangedCallback* cb) {
    if (!cb) {
        return;
    }

    auto it = std::find(observers_.begin(), observers_.end(), cb);
    if (it != observers_.end()) {
        observers_.erase(it);
        spdlog::debug("[HistoryManager] Removed observer (remaining: {})", observers_.size());
    }
}

// ============================================================================
// Private Implementation
// ============================================================================

void PrintHistoryManager::on_history_fetched(std::vector<PrintHistoryJob>&& jobs) {
    spdlog::debug("[HistoryManager] Fetched {} jobs", jobs.size());

    cached_jobs_ = std::move(jobs);
    build_filename_stats();

    is_loaded_ = true;
    is_fetching_ = false;

    notify_observers();
}

void PrintHistoryManager::build_filename_stats() {
    filename_stats_.clear();

    for (const auto& job : cached_jobs_) {
        // Strip path from filename to get basename
        std::string basename = job.filename;
        auto slash_pos = basename.rfind('/');
        if (slash_pos != std::string::npos) {
            basename = basename.substr(slash_pos + 1);
        }

        if (basename.empty()) {
            continue;
        }

        auto& stats = filename_stats_[basename];

        // Count successes and failures
        if (job.status == PrintJobStatus::COMPLETED) {
            stats.success_count++;
        } else if (job.status == PrintJobStatus::CANCELLED || job.status == PrintJobStatus::ERROR) {
            stats.failure_count++;
        }

        // Track most recent job for this filename
        if (job.start_time > stats.last_print_time) {
            stats.last_print_time = job.start_time;
            stats.last_status = job.status;
            stats.uuid = job.uuid;
            stats.size_bytes = job.size_bytes;
        }
    }

    spdlog::debug("[HistoryManager] Built stats for {} unique filenames", filename_stats_.size());
}

std::vector<PrintHistoryJob> PrintHistoryManager::get_jobs_since(double since) const {
    std::vector<PrintHistoryJob> filtered;
    filtered.reserve(cached_jobs_.size()); // Avoid reallocation

    for (const auto& job : cached_jobs_) {
        if (job.start_time >= since) {
            filtered.push_back(job);
        }
    }

    return filtered;
}

void PrintHistoryManager::notify_observers() {
    for (auto* cb : observers_) {
        if (cb && *cb) {
            (*cb)();
        }
    }
}

void PrintHistoryManager::subscribe_to_notifications() {
    if (!client_) {
        return;
    }

    // Capture weak_ptr for async callback safety [L012]
    std::weak_ptr<bool> weak_guard = callback_guard_;

    client_->register_method_callback("notify_history_changed", "PrintHistoryManager",
                                      [this, weak_guard](const nlohmann::json& /*data*/) {
                                          spdlog::debug(
                                              "[HistoryManager] Received notify_history_changed");

                                          // Dispatch to main thread with guard check
                                          helix::ui::queue_update([this, weak_guard]() {
                                              if (!weak_guard.lock()) {
                                                  return; // Object destroyed, abort
                                              }
                                              invalidate();
                                              fetch();
                                          });
                                      });
}
