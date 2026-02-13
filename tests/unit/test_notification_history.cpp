// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_notification_history.cpp
 * @brief Unit tests for NotificationHistory circular buffer
 */

#include "ui_notification_history.h"
#include "ui_toast.h"

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

#include "../catch_amalgamated.hpp"

// Helper to create a test entry
static NotificationHistoryEntry make_entry(ToastSeverity severity, const char* message,
                                           bool was_modal = false) {
    NotificationHistoryEntry entry = {};
    entry.timestamp_ms = 1000; // Fixed timestamp for testing
    entry.severity = severity;
    entry.was_modal = was_modal;
    entry.was_read = false;
    strncpy(entry.message, message, sizeof(entry.message) - 1);
    return entry;
}

// ============================================================================
// Basic Operations Tests
// ============================================================================

TEST_CASE("NotificationHistory: Singleton returns same instance", "[ui][singleton]") {
    NotificationHistory& instance1 = NotificationHistory::instance();
    NotificationHistory& instance2 = NotificationHistory::instance();

    REQUIRE(&instance1 == &instance2);
}

TEST_CASE("NotificationHistory: Add and count entries", "[ui][basic]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    REQUIRE(history.count() == 0);

    history.add(make_entry(ToastSeverity::INFO, "Test message 1"));
    REQUIRE(history.count() == 1);

    history.add(make_entry(ToastSeverity::ERROR, "Test message 2"));
    REQUIRE(history.count() == 2);
}

TEST_CASE("NotificationHistory: Clear removes all entries", "[ui][basic]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    history.add(make_entry(ToastSeverity::INFO, "Message 1"));
    history.add(make_entry(ToastSeverity::WARNING, "Message 2"));
    history.add(make_entry(ToastSeverity::ERROR, "Message 3"));

    REQUIRE(history.count() == 3);

    history.clear();
    REQUIRE(history.count() == 0);
}

TEST_CASE("NotificationHistory: Get all returns entries newest first", "[ui][basic]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    history.add(make_entry(ToastSeverity::INFO, "First"));
    history.add(make_entry(ToastSeverity::WARNING, "Second"));
    history.add(make_entry(ToastSeverity::ERROR, "Third"));

    auto entries = history.get_all();
    REQUIRE(entries.size() == 3);
    REQUIRE(std::string(entries[0].message) == "Third"); // Newest first
    REQUIRE(std::string(entries[1].message) == "Second");
    REQUIRE(std::string(entries[2].message) == "First"); // Oldest last
}

// ============================================================================
// Circular Buffer Tests
// ============================================================================

TEST_CASE("NotificationHistory: Circular buffer caps at MAX_ENTRIES", "[ui][circular]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    // Add more than MAX_ENTRIES
    for (size_t i = 0; i < NotificationHistory::MAX_ENTRIES + 10; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Message %zu", i);
        history.add(make_entry(ToastSeverity::INFO, msg));
    }

    // Should cap at MAX_ENTRIES
    REQUIRE(history.count() == NotificationHistory::MAX_ENTRIES);
}

TEST_CASE("NotificationHistory: Circular buffer overwrites oldest entries", "[ui][circular]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    // Fill buffer completely
    for (size_t i = 0; i < NotificationHistory::MAX_ENTRIES; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Message %zu", i);
        history.add(make_entry(ToastSeverity::INFO, msg));
    }

    // Add one more - should overwrite the oldest (Message 0)
    history.add(make_entry(ToastSeverity::ERROR, "Newest message"));

    auto entries = history.get_all();
    REQUIRE(entries.size() == NotificationHistory::MAX_ENTRIES);

    // Newest should be first
    REQUIRE(std::string(entries[0].message) == "Newest message");

    // Oldest remaining should be "Message 1" (Message 0 was overwritten)
    REQUIRE(std::string(entries[NotificationHistory::MAX_ENTRIES - 1].message) == "Message 1");
}

// ============================================================================
// Unread Count Tests
// ============================================================================

TEST_CASE("NotificationHistory: Unread count tracks unread entries", "[ui][unread]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    REQUIRE(history.get_unread_count() == 0);

    history.add(make_entry(ToastSeverity::INFO, "Unread 1"));
    REQUIRE(history.get_unread_count() == 1);

    history.add(make_entry(ToastSeverity::WARNING, "Unread 2"));
    REQUIRE(history.get_unread_count() == 2);

    history.add(make_entry(ToastSeverity::ERROR, "Unread 3"));
    REQUIRE(history.get_unread_count() == 3);
}

TEST_CASE("NotificationHistory: Mark all read clears unread count", "[ui][unread]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    history.add(make_entry(ToastSeverity::INFO, "Message 1"));
    history.add(make_entry(ToastSeverity::WARNING, "Message 2"));
    history.add(make_entry(ToastSeverity::ERROR, "Message 3"));

    REQUIRE(history.get_unread_count() == 3);

    history.mark_all_read();
    REQUIRE(history.get_unread_count() == 0);
}

TEST_CASE("NotificationHistory: New entries after mark_all_read are unread", "[ui][unread]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    history.add(make_entry(ToastSeverity::INFO, "Old message"));
    history.mark_all_read();
    REQUIRE(history.get_unread_count() == 0);

    history.add(make_entry(ToastSeverity::ERROR, "New message"));
    REQUIRE(history.get_unread_count() == 1);
}

// ============================================================================
// Severity Priority Tests
// ============================================================================

TEST_CASE("NotificationHistory: Get highest unread severity", "[ui][severity]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    // No entries - should return INFO as default
    REQUIRE(history.get_highest_unread_severity() == ToastSeverity::INFO);

    // Add INFO only
    history.add(make_entry(ToastSeverity::INFO, "Info message"));
    REQUIRE(history.get_highest_unread_severity() == ToastSeverity::INFO);

    // Add WARNING - should now be highest
    history.add(make_entry(ToastSeverity::WARNING, "Warning message"));
    REQUIRE(history.get_highest_unread_severity() == ToastSeverity::WARNING);

    // Add ERROR - should now be highest
    history.add(make_entry(ToastSeverity::ERROR, "Error message"));
    REQUIRE(history.get_highest_unread_severity() == ToastSeverity::ERROR);

    // Mark all as read
    history.mark_all_read();
    REQUIRE(history.get_highest_unread_severity() == ToastSeverity::INFO); // No unread = INFO

    // Add just WARNING
    history.add(make_entry(ToastSeverity::WARNING, "New warning"));
    REQUIRE(history.get_highest_unread_severity() == ToastSeverity::WARNING);
}

// ============================================================================
// Filter Tests
// ============================================================================

TEST_CASE("NotificationHistory: Filter by severity", "[ui][filter]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    history.add(make_entry(ToastSeverity::INFO, "Info 1"));
    history.add(make_entry(ToastSeverity::WARNING, "Warning 1"));
    history.add(make_entry(ToastSeverity::ERROR, "Error 1"));
    history.add(make_entry(ToastSeverity::INFO, "Info 2"));
    history.add(make_entry(ToastSeverity::SUCCESS, "Success 1"));

    // Filter by ERROR
    auto errors = history.get_filtered(static_cast<int>(ToastSeverity::ERROR));
    REQUIRE(errors.size() == 1);
    REQUIRE(errors[0].severity == ToastSeverity::ERROR);

    // Filter by INFO
    auto infos = history.get_filtered(static_cast<int>(ToastSeverity::INFO));
    REQUIRE(infos.size() == 2);

    // Filter by WARNING
    auto warnings = history.get_filtered(static_cast<int>(ToastSeverity::WARNING));
    REQUIRE(warnings.size() == 1);

    // Get all (-1)
    auto all = history.get_filtered(-1);
    REQUIRE(all.size() == 5);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_CASE("NotificationHistory: Thread-safe concurrent adds", "[ui][thread]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    const int num_threads = 4;
    const int entries_per_thread = 25;

    std::vector<std::thread> threads;

    // Launch multiple threads that add entries concurrently
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([t, entries_per_thread]() {
            NotificationHistory& history = NotificationHistory::instance();
            for (int i = 0; i < entries_per_thread; i++) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Thread %d Message %d", t, i);
                history.add(make_entry(ToastSeverity::INFO, msg));
            }
        });
    }

    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }

    // Should have all entries (unless we hit MAX_ENTRIES)
    size_t expected = std::min(static_cast<size_t>(num_threads * entries_per_thread),
                               NotificationHistory::MAX_ENTRIES);
    REQUIRE(history.count() == expected);
}

TEST_CASE("NotificationHistory: Thread-safe concurrent read/write", "[ui][thread]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    std::atomic<bool> running{true};
    std::atomic<int> read_count{0};

    // Writer thread
    std::thread writer([&running]() {
        NotificationHistory& history = NotificationHistory::instance();
        int i = 0;
        while (running) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Write %d", i++);
            history.add(make_entry(ToastSeverity::INFO, msg));
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    // Reader threads
    std::vector<std::thread> readers;
    for (int r = 0; r < 3; r++) {
        readers.emplace_back([&running, &read_count]() {
            NotificationHistory& history = NotificationHistory::instance();
            while (running) {
                auto entries = history.get_all();
                read_count++;
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        });
    }

    // Let it run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    running = false;

    // Wait for threads
    writer.join();
    for (auto& r : readers) {
        r.join();
    }

    // Should have completed multiple reads without crashes
    REQUIRE(read_count > 0);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("NotificationHistory: Message truncation", "[ui][edge]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    // Create a very long message (longer than 256 bytes)
    std::string long_message(300, 'X');
    NotificationHistoryEntry entry = make_entry(ToastSeverity::INFO, long_message.c_str());

    history.add(entry);

    auto entries = history.get_all();
    REQUIRE(entries.size() == 1);
    // Message should be truncated to fit in the buffer
    REQUIRE(strlen(entries[0].message) < 256);
}

TEST_CASE("NotificationHistory: Empty title and message handling", "[ui][edge]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    NotificationHistoryEntry entry = {};
    entry.timestamp_ms = 1000;
    entry.severity = ToastSeverity::INFO;
    entry.was_modal = false;
    entry.was_read = false;
    // Leave title and message empty

    history.add(entry);
    REQUIRE(history.count() == 1);

    auto entries = history.get_all();
    REQUIRE(entries[0].title[0] == '\0');
    REQUIRE(entries[0].message[0] == '\0');
}

// ============================================================================
// Action Field Tests
// ============================================================================

TEST_CASE("NotificationHistory: Action field stored and retrieved", "[ui][action]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    NotificationHistoryEntry entry = {};
    entry.timestamp_ms = 1000;
    entry.severity = ToastSeverity::INFO;
    entry.was_modal = false;
    entry.was_read = false;
    strncpy(entry.title, "Update Available", sizeof(entry.title) - 1);
    strncpy(entry.message, "Version 1.0 available", sizeof(entry.message) - 1);
    strncpy(entry.action, "show_update_modal", sizeof(entry.action) - 1);

    history.add(entry);

    auto entries = history.get_all();
    REQUIRE(entries.size() == 1);
    REQUIRE(std::string(entries[0].action) == "show_update_modal");
    REQUIRE(std::string(entries[0].title) == "Update Available");
}

TEST_CASE("NotificationHistory: Empty action field by default", "[ui][action]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    NotificationHistoryEntry entry = {};
    entry.timestamp_ms = 1000;
    entry.severity = ToastSeverity::INFO;
    entry.was_modal = false;
    entry.was_read = false;
    strncpy(entry.message, "Normal notification", sizeof(entry.message) - 1);

    history.add(entry);

    auto entries = history.get_all();
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].action[0] == '\0');
}
