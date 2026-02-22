// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_crash_history.cpp
 * @brief TDD tests for CrashHistory — persistent crash submission history
 *
 * Tests crash_history.json persistence, FIFO cap, JSON round-trip,
 * and thread safety.
 */

#include "system/crash_history.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ============================================================================
// Fixture: isolated temp directory with singleton reset
// ============================================================================

class CrashHistoryTestFixture {
  public:
    CrashHistoryTestFixture() {
        temp_dir_ = fs::temp_directory_path() /
                    ("helix_crash_history_test_" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(temp_dir_);

        auto& ch = helix::CrashHistory::instance();
        ch.shutdown();
        ch.init(temp_dir_.string());
    }

    ~CrashHistoryTestFixture() {
        helix::CrashHistory::instance().shutdown();
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    helix::CrashHistoryEntry make_entry(int signal = 11, const std::string& name = "SIGSEGV",
                                        const std::string& version = "0.10.12") {
        helix::CrashHistoryEntry entry;
        entry.timestamp = "2026-02-22T04:00:00Z";
        entry.signal = signal;
        entry.signal_name = name;
        entry.app_version = version;
        entry.uptime_sec = 3600;
        entry.fault_addr = "0x00000000";
        entry.fault_code_name = "SEGV_MAPERR";
        entry.github_issue = 142;
        entry.github_url = "https://github.com/prestonbrown/helixscreen/issues/142";
        entry.sent_via = "crash_reporter";
        return entry;
    }

    fs::path temp_dir_;
};

// ============================================================================
// Basic Operations [crash_history]
// ============================================================================

TEST_CASE_METHOD(CrashHistoryTestFixture, "CrashHistory: starts empty after init",
                 "[crash_history]") {
    auto& ch = helix::CrashHistory::instance();
    REQUIRE(ch.size() == 0);
    REQUIRE(ch.get_entries().empty());
}

TEST_CASE_METHOD(CrashHistoryTestFixture, "CrashHistory: add_entry increases size",
                 "[crash_history]") {
    auto& ch = helix::CrashHistory::instance();
    ch.add_entry(make_entry());
    REQUIRE(ch.size() == 1);
}

TEST_CASE_METHOD(CrashHistoryTestFixture, "CrashHistory: add_entry preserves all fields",
                 "[crash_history]") {
    auto& ch = helix::CrashHistory::instance();
    auto entry = make_entry(6, "SIGABRT", "1.0.0");
    entry.uptime_sec = 999;
    entry.fault_addr = "0xdeadbeef";
    entry.fault_code_name = "SEGV_ACCERR";
    entry.github_issue = 200;
    entry.github_url = "https://github.com/prestonbrown/helixscreen/issues/200";
    entry.sent_via = "telemetry";

    ch.add_entry(entry);
    auto entries = ch.get_entries();
    REQUIRE(entries.size() == 1);

    auto& e = entries[0];
    REQUIRE(e.timestamp == "2026-02-22T04:00:00Z");
    REQUIRE(e.signal == 6);
    REQUIRE(e.signal_name == "SIGABRT");
    REQUIRE(e.app_version == "1.0.0");
    REQUIRE(e.uptime_sec == 999);
    REQUIRE(e.fault_addr == "0xdeadbeef");
    REQUIRE(e.fault_code_name == "SEGV_ACCERR");
    REQUIRE(e.github_issue == 200);
    REQUIRE(e.github_url == "https://github.com/prestonbrown/helixscreen/issues/200");
    REQUIRE(e.sent_via == "telemetry");
}

TEST_CASE_METHOD(CrashHistoryTestFixture, "CrashHistory: multiple entries are ordered",
                 "[crash_history]") {
    auto& ch = helix::CrashHistory::instance();

    auto e1 = make_entry(11, "SIGSEGV", "0.10.10");
    e1.timestamp = "2026-02-20T00:00:00Z";
    auto e2 = make_entry(6, "SIGABRT", "0.10.11");
    e2.timestamp = "2026-02-21T00:00:00Z";

    ch.add_entry(e1);
    ch.add_entry(e2);

    auto entries = ch.get_entries();
    REQUIRE(entries.size() == 2);
    REQUIRE(entries[0].signal == 11);
    REQUIRE(entries[1].signal == 6);
}

// ============================================================================
// FIFO Cap [crash_history]
// ============================================================================

TEST_CASE_METHOD(CrashHistoryTestFixture, "CrashHistory: caps at MAX_ENTRIES", "[crash_history]") {
    auto& ch = helix::CrashHistory::instance();

    for (size_t i = 0; i < helix::CrashHistory::MAX_ENTRIES + 5; ++i) {
        auto entry = make_entry();
        entry.timestamp = "2026-02-22T" + std::to_string(i) + ":00:00Z";
        entry.github_issue = static_cast<int>(i);
        ch.add_entry(entry);
    }

    REQUIRE(ch.size() == helix::CrashHistory::MAX_ENTRIES);
}

TEST_CASE_METHOD(CrashHistoryTestFixture, "CrashHistory: FIFO drops oldest entries",
                 "[crash_history]") {
    auto& ch = helix::CrashHistory::instance();

    // Add MAX_ENTRIES + 1 entries
    for (size_t i = 0; i < helix::CrashHistory::MAX_ENTRIES + 1; ++i) {
        auto entry = make_entry();
        entry.github_issue = static_cast<int>(i);
        ch.add_entry(entry);
    }

    auto entries = ch.get_entries();
    REQUIRE(entries.size() == helix::CrashHistory::MAX_ENTRIES);
    // Oldest (issue 0) should be gone, issue 1 should be first
    REQUIRE(entries[0].github_issue == 1);
    // Newest should be last
    REQUIRE(entries.back().github_issue == static_cast<int>(helix::CrashHistory::MAX_ENTRIES));
}

// ============================================================================
// Persistence [crash_history]
// ============================================================================

TEST_CASE_METHOD(CrashHistoryTestFixture, "CrashHistory: persists to disk on add_entry",
                 "[crash_history]") {
    auto& ch = helix::CrashHistory::instance();
    ch.add_entry(make_entry());

    // Verify file exists
    REQUIRE(fs::exists(temp_dir_ / "crash_history.json"));

    // Verify it's valid JSON
    std::ifstream ifs((temp_dir_ / "crash_history.json").string());
    json j;
    REQUIRE_NOTHROW(j = json::parse(ifs));
    REQUIRE(j.is_array());
    REQUIRE(j.size() == 1);
}

TEST_CASE_METHOD(CrashHistoryTestFixture, "CrashHistory: survives re-init (load from disk)",
                 "[crash_history]") {
    auto& ch = helix::CrashHistory::instance();

    auto entry = make_entry(6, "SIGABRT", "0.10.5");
    entry.github_issue = 77;
    ch.add_entry(entry);
    REQUIRE(ch.size() == 1);

    // Re-init (simulates app restart)
    ch.shutdown();
    ch.init(temp_dir_.string());

    REQUIRE(ch.size() == 1);
    auto entries = ch.get_entries();
    REQUIRE(entries[0].signal == 6);
    REQUIRE(entries[0].signal_name == "SIGABRT");
    REQUIRE(entries[0].github_issue == 77);
}

TEST_CASE_METHOD(CrashHistoryTestFixture, "CrashHistory: handles missing file gracefully",
                 "[crash_history]") {
    // Delete file if it exists, re-init
    fs::remove(temp_dir_ / "crash_history.json");

    auto& ch = helix::CrashHistory::instance();
    ch.shutdown();
    ch.init(temp_dir_.string());

    REQUIRE(ch.size() == 0);
}

TEST_CASE_METHOD(CrashHistoryTestFixture, "CrashHistory: handles corrupt file gracefully",
                 "[crash_history]") {
    // Write invalid JSON
    std::ofstream ofs((temp_dir_ / "crash_history.json").string());
    ofs << "this is not json {{{";
    ofs.close();

    auto& ch = helix::CrashHistory::instance();
    ch.shutdown();
    ch.init(temp_dir_.string());

    // Should start empty, not crash
    REQUIRE(ch.size() == 0);
}

// ============================================================================
// JSON Serialization [crash_history]
// ============================================================================

TEST_CASE_METHOD(CrashHistoryTestFixture, "CrashHistory: to_json returns array",
                 "[crash_history]") {
    auto& ch = helix::CrashHistory::instance();
    ch.add_entry(make_entry());

    json j = ch.to_json();
    REQUIRE(j.is_array());
    REQUIRE(j.size() == 1);
}

TEST_CASE_METHOD(CrashHistoryTestFixture,
                 "CrashHistory: to_json includes all fields with correct values",
                 "[crash_history]") {
    auto& ch = helix::CrashHistory::instance();
    ch.add_entry(make_entry());

    json j = ch.to_json();
    auto& e = j[0];
    REQUIRE(e.contains("timestamp"));
    REQUIRE(e.contains("signal"));
    REQUIRE(e.contains("signal_name"));
    REQUIRE(e.contains("app_version"));
    REQUIRE(e.contains("uptime_sec"));
    REQUIRE(e.contains("github_issue"));
    REQUIRE(e.contains("github_url"));
    REQUIRE(e.contains("sent_via"));

    // Verify values match what was added (not just key presence)
    REQUIRE(e["signal"] == 11);
    REQUIRE(e["signal_name"] == "SIGSEGV");
    REQUIRE(e["app_version"] == "0.10.12");
    REQUIRE(e["github_issue"] == 142);
    REQUIRE(e["sent_via"] == "crash_reporter");
}

TEST_CASE_METHOD(CrashHistoryTestFixture, "CrashHistory: to_json returns empty array when empty",
                 "[crash_history]") {
    auto& ch = helix::CrashHistory::instance();
    json j = ch.to_json();
    REQUIRE(j.is_array());
    REQUIRE(j.empty());
}

TEST_CASE_METHOD(CrashHistoryTestFixture,
                 "CrashHistory: entry without github info has zero/empty fields",
                 "[crash_history]") {
    auto& ch = helix::CrashHistory::instance();

    helix::CrashHistoryEntry entry;
    entry.timestamp = "2026-02-22T00:00:00Z";
    entry.signal = 11;
    entry.signal_name = "SIGSEGV";
    entry.app_version = "0.10.12";
    entry.sent_via = "telemetry";
    // No github_issue, no github_url, no fault info

    ch.add_entry(entry);
    auto entries = ch.get_entries();
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].github_issue == 0);
    REQUIRE(entries[0].github_url.empty());
    REQUIRE(entries[0].fault_addr.empty());
}

// ============================================================================
// Safety [crash_history]
// ============================================================================

TEST_CASE_METHOD(CrashHistoryTestFixture, "CrashHistory: add_entry before init is safe",
                 "[crash_history]") {
    auto& ch = helix::CrashHistory::instance();
    ch.shutdown(); // ensure uninitialized

    // Should not crash, entry silently dropped
    ch.add_entry(make_entry());
    REQUIRE(ch.size() == 0);
}
