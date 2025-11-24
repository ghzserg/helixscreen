// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_notification_history.h"
#include <spdlog/spdlog.h>
#include <hv/json.hpp>
#include <fstream>
#include <algorithm>

using json = nlohmann::json;

NotificationHistory& NotificationHistory::instance() {
    static NotificationHistory instance;
    return instance;
}

void NotificationHistory::add(const NotificationHistoryEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Reserve space if needed
    if (entries_.empty()) {
        entries_.reserve(MAX_ENTRIES);
    }

    // Add entry to circular buffer
    if (entries_.size() < MAX_ENTRIES) {
        // Buffer not full yet - just append
        entries_.push_back(entry);
        head_index_ = entries_.size();
    } else {
        // Buffer is full - overwrite oldest entry
        if (!buffer_full_) {
            buffer_full_ = true;
        }
        entries_[head_index_] = entry;
        head_index_ = (head_index_ + 1) % MAX_ENTRIES;
    }

    spdlog::trace("Added notification to history: severity={}, message='{}'",
                  static_cast<int>(entry.severity), entry.message);
}

std::vector<NotificationHistoryEntry> NotificationHistory::get_all() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (entries_.empty()) {
        return {};
    }

    std::vector<NotificationHistoryEntry> result;
    result.reserve(entries_.size());

    if (!buffer_full_) {
        // Buffer not full - entries are in order, just reverse
        result.assign(entries_.rbegin(), entries_.rend());
    } else {
        // Buffer is full - reconstruct newest-first order
        // head_index_ points to oldest, so start from head_index_-1 (newest)
        size_t idx = (head_index_ == 0) ? MAX_ENTRIES - 1 : head_index_ - 1;
        for (size_t i = 0; i < entries_.size(); i++) {
            result.push_back(entries_[idx]);
            idx = (idx == 0) ? MAX_ENTRIES - 1 : idx - 1;
        }
    }

    return result;
}

std::vector<NotificationHistoryEntry> NotificationHistory::get_filtered(int severity) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto all_entries = get_all();

    if (severity < 0) {
        return all_entries;
    }

    std::vector<NotificationHistoryEntry> result;
    std::copy_if(all_entries.begin(), all_entries.end(),
                 std::back_inserter(result),
                 [severity](const NotificationHistoryEntry& e) {
                     return static_cast<int>(e.severity) == severity;
                 });

    return result;
}

size_t NotificationHistory::get_unread_count() const {
    std::lock_guard<std::mutex> lock(mutex_);

    return std::count_if(entries_.begin(), entries_.end(),
                        [](const NotificationHistoryEntry& e) {
                            return !e.was_read;
                        });
}

void NotificationHistory::mark_all_read() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& entry : entries_) {
        entry.was_read = true;
    }

    spdlog::debug("Marked all {} notifications as read", entries_.size());
}

void NotificationHistory::clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    entries_.clear();
    head_index_ = 0;
    buffer_full_ = false;

    spdlog::debug("Cleared notification history");
}

size_t NotificationHistory::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

bool NotificationHistory::save_to_disk(const char* path) const {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        json j;
        j["version"] = 1;
        j["entries"] = json::array();

        // Get entries in newest-first order
        auto all_entries = get_all();

        // Limit to last 50 entries to keep file size reasonable
        size_t save_count = std::min(all_entries.size(), size_t(50));

        for (size_t i = 0; i < save_count; i++) {
            const auto& entry = all_entries[i];

            // Convert severity to string
            std::string severity_str;
            switch (entry.severity) {
                case ToastSeverity::INFO:    severity_str = "INFO"; break;
                case ToastSeverity::SUCCESS: severity_str = "SUCCESS"; break;
                case ToastSeverity::WARNING: severity_str = "WARNING"; break;
                case ToastSeverity::ERROR:   severity_str = "ERROR"; break;
                default:                     severity_str = "UNKNOWN"; break;
            }

            json entry_json;
            entry_json["timestamp"] = entry.timestamp_ms;
            entry_json["severity"] = severity_str;
            entry_json["title"] = entry.title;
            entry_json["message"] = entry.message;
            entry_json["was_modal"] = entry.was_modal;
            entry_json["was_read"] = entry.was_read;

            j["entries"].push_back(entry_json);
        }

        // Write to file
        std::ofstream file(path);
        if (!file.is_open()) {
            spdlog::error("Failed to open notification history file for writing: {}", path);
            return false;
        }

        file << j.dump(2);  // Pretty-print with 2-space indent
        file.close();

        spdlog::info("Saved {} notification entries to {}", save_count, path);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("Failed to save notification history: {}", e.what());
        return false;
    }
}

bool NotificationHistory::load_from_disk(const char* path) {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            spdlog::debug("No notification history file found at {}", path);
            return false;
        }

        json j;
        file >> j;
        file.close();

        // Check version
        int version = j.value("version", 0);
        if (version != 1) {
            spdlog::warn("Unsupported notification history version: {}", version);
            return false;
        }

        // Clear existing entries
        entries_.clear();
        head_index_ = 0;
        buffer_full_ = false;

        // Load entries
        if (j.contains("entries") && j["entries"].is_array()) {
            for (const auto& entry_json : j["entries"]) {
                NotificationHistoryEntry entry = {};

                entry.timestamp_ms = entry_json.value("timestamp", uint64_t(0));
                entry.was_modal = entry_json.value("was_modal", false);
                entry.was_read = entry_json.value("was_read", false);

                // Parse severity
                std::string severity_str = entry_json.value("severity", "INFO");
                if (severity_str == "INFO") {
                    entry.severity = ToastSeverity::INFO;
                } else if (severity_str == "SUCCESS") {
                    entry.severity = ToastSeverity::SUCCESS;
                } else if (severity_str == "WARNING") {
                    entry.severity = ToastSeverity::WARNING;
                } else if (severity_str == "ERROR") {
                    entry.severity = ToastSeverity::ERROR;
                } else {
                    entry.severity = ToastSeverity::INFO;
                }

                // Copy strings
                std::string title = entry_json.value("title", "");
                std::string message = entry_json.value("message", "");

                strncpy(entry.title, title.c_str(), sizeof(entry.title) - 1);
                entry.title[sizeof(entry.title) - 1] = '\0';

                strncpy(entry.message, message.c_str(), sizeof(entry.message) - 1);
                entry.message[sizeof(entry.message) - 1] = '\0';

                entries_.push_back(entry);
            }

            head_index_ = entries_.size();
            if (entries_.size() >= MAX_ENTRIES) {
                buffer_full_ = true;
                head_index_ = 0;
            }

            spdlog::info("Loaded {} notification entries from {}", entries_.size(), path);
            return true;
        }

        return false;

    } catch (const std::exception& e) {
        spdlog::error("Failed to load notification history: {}", e.what());
        return false;
    }
}
