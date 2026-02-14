// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_crash_handler.cpp
 * @brief Unit tests for crash handler - signal-safe crash file writing and parsing
 *
 * Tests the crash file parsing, JSON event creation, file lifecycle,
 * and TelemetryManager integration. Does NOT trigger real signals --
 * tests only the file-based parsing and event creation logic.
 *
 * Written TDD-style - tests WILL FAIL if crash_handler is removed.
 */

#include "system/crash_handler.h"
#include "system/telemetry_manager.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ============================================================================
// Helper: create a temporary directory for test isolation
// ============================================================================

class CrashTestFixture {
  public:
    CrashTestFixture() {
        temp_dir_ = fs::temp_directory_path() /
                    ("helix_crash_test_" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(temp_dir_);
        crash_path_ = (temp_dir_ / "crash.txt").string();
    }

    ~CrashTestFixture() {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    [[nodiscard]] std::string crash_path() const {
        return crash_path_;
    }
    [[nodiscard]] fs::path temp_dir() const {
        return temp_dir_;
    }

    /// Write a mock crash file with the given content
    void write_crash_file(const std::string& content) const {
        std::ofstream ofs(crash_path_);
        ofs << content;
        ofs.close();
    }

    /// Write a realistic crash file matching the signal handler's output format
    void write_realistic_crash_file() const {
        write_crash_file("signal:11\n"
                         "name:SIGSEGV\n"
                         "version:0.9.6\n"
                         "timestamp:1707350400\n"
                         "uptime:3600\n"
                         "bt:0x0040abcd\n"
                         "bt:0x0040ef01\n"
                         "bt:0x00401234\n");
    }

  private:
    fs::path temp_dir_;
    std::string crash_path_;
};

// ============================================================================
// Crash File Detection [telemetry][crash]
// ============================================================================

TEST_CASE_METHOD(CrashTestFixture, "Crash: has_crash_file returns false when no file exists",
                 "[telemetry][crash]") {
    REQUIRE_FALSE(crash_handler::has_crash_file(crash_path()));
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: has_crash_file returns true when file exists",
                 "[telemetry][crash]") {
    write_realistic_crash_file();
    REQUIRE(crash_handler::has_crash_file(crash_path()));
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: has_crash_file returns false for empty file",
                 "[telemetry][crash]") {
    write_crash_file("");
    REQUIRE_FALSE(crash_handler::has_crash_file(crash_path()));
}

// ============================================================================
// Crash File Format Parsing [telemetry][crash]
// ============================================================================

TEST_CASE_METHOD(CrashTestFixture, "Crash: parse crash file extracts signal number",
                 "[telemetry][crash]") {
    write_realistic_crash_file();
    auto result = crash_handler::read_crash_file(crash_path());

    REQUIRE_FALSE(result.is_null());
    REQUIRE(result.contains("signal"));
    REQUIRE(result["signal"] == 11);
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: parse crash file extracts signal name",
                 "[telemetry][crash]") {
    write_realistic_crash_file();
    auto result = crash_handler::read_crash_file(crash_path());

    REQUIRE_FALSE(result.is_null());
    REQUIRE(result.contains("signal_name"));
    REQUIRE(result["signal_name"] == "SIGSEGV");
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: parse crash file extracts version",
                 "[telemetry][crash]") {
    write_realistic_crash_file();
    auto result = crash_handler::read_crash_file(crash_path());

    REQUIRE_FALSE(result.is_null());
    REQUIRE(result.contains("app_version"));
    REQUIRE(result["app_version"] == "0.9.6");
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: parse crash file converts timestamp to ISO 8601",
                 "[telemetry][crash]") {
    write_realistic_crash_file();
    auto result = crash_handler::read_crash_file(crash_path());

    REQUIRE_FALSE(result.is_null());
    REQUIRE(result.contains("timestamp"));
    // 1707350400 = 2024-02-08T00:00:00Z
    std::string ts = result["timestamp"];
    REQUIRE(ts.find('T') != std::string::npos);
    REQUIRE(ts.find('Z') != std::string::npos);
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: parse crash file extracts uptime",
                 "[telemetry][crash]") {
    write_realistic_crash_file();
    auto result = crash_handler::read_crash_file(crash_path());

    REQUIRE_FALSE(result.is_null());
    REQUIRE(result.contains("uptime_sec"));
    REQUIRE(result["uptime_sec"] == 3600);
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: parse crash file extracts backtrace entries",
                 "[telemetry][crash]") {
    write_realistic_crash_file();
    auto result = crash_handler::read_crash_file(crash_path());

    REQUIRE_FALSE(result.is_null());
    REQUIRE(result.contains("backtrace"));
    REQUIRE(result["backtrace"].is_array());
    REQUIRE(result["backtrace"].size() == 3);
    REQUIRE(result["backtrace"][0] == "0x0040abcd");
    REQUIRE(result["backtrace"][1] == "0x0040ef01");
    REQUIRE(result["backtrace"][2] == "0x00401234");
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: parse crash file with no backtrace omits field",
                 "[telemetry][crash]") {
    write_crash_file("signal:6\nname:SIGABRT\nversion:1.0.0\ntimestamp:1707350400\nuptime:100\n");
    auto result = crash_handler::read_crash_file(crash_path());

    REQUIRE_FALSE(result.is_null());
    REQUIRE(result["signal"] == 6);
    REQUIRE(result["signal_name"] == "SIGABRT");
    // No backtrace entries should mean no backtrace field
    REQUIRE_FALSE(result.contains("backtrace"));
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: parse returns null for missing file",
                 "[telemetry][crash]") {
    auto result = crash_handler::read_crash_file(crash_path());
    REQUIRE(result.is_null());
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: parse returns null for file missing required fields",
                 "[telemetry][crash]") {
    // File with only version, no signal info
    write_crash_file("version:1.0.0\nuptime:100\n");
    auto result = crash_handler::read_crash_file(crash_path());
    REQUIRE(result.is_null());
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: parse handles all signal types", "[telemetry][crash]") {
    struct SignalCase {
        int number;
        const char* name;
    };

    SignalCase signals[] = {
        {11, "SIGSEGV"},
        {6, "SIGABRT"},
        {7, "SIGBUS"},
        {8, "SIGFPE"},
    };

    for (const auto& sig : signals) {
        std::string content = "signal:" + std::to_string(sig.number) +
                              "\n"
                              "name:" +
                              sig.name +
                              "\n"
                              "version:1.0.0\n"
                              "timestamp:1707350400\n"
                              "uptime:0\n";
        write_crash_file(content);

        auto result = crash_handler::read_crash_file(crash_path());
        REQUIRE_FALSE(result.is_null());
        REQUIRE(result["signal"] == sig.number);
        REQUIRE(result["signal_name"] == sig.name);
    }
}

// ============================================================================
// Crash File Cleanup [telemetry][crash]
// ============================================================================

TEST_CASE_METHOD(CrashTestFixture, "Crash: remove_crash_file deletes the file",
                 "[telemetry][crash]") {
    write_realistic_crash_file();
    REQUIRE(crash_handler::has_crash_file(crash_path()));

    crash_handler::remove_crash_file(crash_path());
    REQUIRE_FALSE(crash_handler::has_crash_file(crash_path()));
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: remove_crash_file is safe for non-existent file",
                 "[telemetry][crash]") {
    // Should not throw or crash
    crash_handler::remove_crash_file(crash_path());
    REQUIRE_FALSE(crash_handler::has_crash_file(crash_path()));
}

// ============================================================================
// Install / Uninstall (no real signals) [telemetry][crash]
// ============================================================================

TEST_CASE_METHOD(CrashTestFixture, "Crash: install and uninstall do not crash",
                 "[telemetry][crash]") {
    // Just verify install/uninstall is safe (no actual signal triggering)
    crash_handler::install(crash_path());
    crash_handler::uninstall();

    // Double uninstall should be safe
    crash_handler::uninstall();
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: install with long path does not crash",
                 "[telemetry][crash]") {
    // Test with a path longer than typical but within buffer limits
    std::string long_path = temp_dir().string() + "/" + std::string(200, 'a') + "/crash.txt";
    crash_handler::install(long_path);
    crash_handler::uninstall();
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: install with very long path truncates safely",
                 "[telemetry][crash]") {
    // Path longer than MAX_PATH_LEN (512) -- should truncate, not crash
    std::string very_long_path = "/" + std::string(600, 'x') + "/crash.txt";
    crash_handler::install(very_long_path);
    crash_handler::uninstall();
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: double install is idempotent", "[telemetry][crash]") {
    crash_handler::install(crash_path());
    crash_handler::install(crash_path()); // Should be safe
    crash_handler::uninstall();
}

// ============================================================================
// TelemetryManager Integration [telemetry][crash]
// ============================================================================

TEST_CASE_METHOD(CrashTestFixture, "Crash: TelemetryManager enqueues crash event from file",
                 "[telemetry][crash]") {
    // Write a crash file in the temp directory that TelemetryManager will use
    std::string tm_crash_path = (temp_dir() / "crash.txt").string();
    {
        std::ofstream ofs(tm_crash_path);
        ofs << "signal:11\nname:SIGSEGV\nversion:0.9.6\ntimestamp:1707350400\nuptime:3600\n"
            << "bt:0x0040abcd\nbt:0x0040ef01\n";
    }

    // Enable telemetry via config file (crash events respect opt-in)
    {
        std::ofstream ofs((temp_dir() / "telemetry_config.json").string());
        ofs << R"({"enabled": true})";
    }

    // Initialize TelemetryManager with the temp dir (init calls check_previous_crash)
    auto& tm = TelemetryManager::instance();
    tm.shutdown();
    tm.init(temp_dir().string());

    // Should have enqueued the crash event
    REQUIRE(tm.queue_size() >= 1);

    auto snapshot = tm.get_queue_snapshot();
    // Find the crash event
    bool found_crash = false;
    for (const auto& event : snapshot) {
        if (event.contains("event") && event["event"] == "crash") {
            found_crash = true;

            // Verify crash event schema
            REQUIRE(event.contains("schema_version"));
            REQUIRE(event["schema_version"] == TelemetryManager::SCHEMA_VERSION);
            REQUIRE(event.contains("device_id"));
            REQUIRE(event["device_id"].is_string());
            REQUIRE(event.contains("timestamp"));
            REQUIRE(event.contains("signal"));
            REQUIRE(event["signal"] == 11);
            REQUIRE(event.contains("signal_name"));
            REQUIRE(event["signal_name"] == "SIGSEGV");
            REQUIRE(event.contains("app_version"));
            REQUIRE(event["app_version"] == "0.9.6");
            REQUIRE(event.contains("uptime_sec"));
            REQUIRE(event["uptime_sec"] == 3600);
            REQUIRE(event.contains("backtrace"));
            REQUIRE(event["backtrace"].size() == 2);
            break;
        }
    }
    REQUIRE(found_crash);

    // Crash file is intentionally NOT deleted by TelemetryManager —
    // CrashReporter owns the lifecycle and removes it after user interaction
    REQUIRE(crash_handler::has_crash_file(tm_crash_path));

    // Clean up for other tests
    fs::remove(tm_crash_path);
    tm.shutdown();
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: TelemetryManager ignores absent crash file",
                 "[telemetry][crash]") {
    // Initialize without any crash file
    auto& tm = TelemetryManager::instance();
    tm.shutdown();
    tm.init(temp_dir().string());

    // No crash events should be enqueued
    auto snapshot = tm.get_queue_snapshot();
    for (const auto& event : snapshot) {
        REQUIRE_FALSE(event["event"] == "crash");
    }

    tm.shutdown();
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: when disabled, crash event is not enqueued",
                 "[telemetry][crash]") {
    // Write a crash file
    {
        std::ofstream ofs((temp_dir() / "crash.txt").string());
        ofs << "signal:11\nname:SIGSEGV\nversion:0.9.6\ntimestamp:1707350400\nuptime:3600\n";
    }

    // No telemetry config = disabled by default
    auto& tm = TelemetryManager::instance();
    tm.shutdown();
    tm.init(temp_dir().string());

    // No crash events should be enqueued
    auto snapshot = tm.get_queue_snapshot();
    for (const auto& event : snapshot) {
        REQUIRE_FALSE(event["event"] == "crash");
    }

    // Crash file is intentionally NOT deleted by TelemetryManager —
    // CrashReporter owns the lifecycle and removes it after user interaction
    REQUIRE(crash_handler::has_crash_file((temp_dir() / "crash.txt").string()));

    // Clean up for other tests
    fs::remove(temp_dir() / "crash.txt");
    tm.shutdown();
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: crash event has correct device_id format",
                 "[telemetry][crash]") {
    // Write crash file
    {
        std::ofstream ofs((temp_dir() / "crash.txt").string());
        ofs << "signal:6\nname:SIGABRT\nversion:1.0.0\ntimestamp:1707350400\nuptime:0\n";
    }

    // Enable telemetry so crash event is enqueued
    {
        std::ofstream ofs((temp_dir() / "telemetry_config.json").string());
        ofs << R"({"enabled": true})";
    }

    auto& tm = TelemetryManager::instance();
    tm.shutdown();
    tm.init(temp_dir().string());

    auto snapshot = tm.get_queue_snapshot();
    bool found = false;
    for (const auto& event : snapshot) {
        if (event.contains("event") && event["event"] == "crash") {
            found = true;
            std::string device_id = event["device_id"];
            // Device ID should be a 64-character hex hash (SHA-256)
            REQUIRE(device_id.size() == 64);
            for (char c : device_id) {
                bool valid_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                REQUIRE(valid_hex);
            }
            break;
        }
    }
    REQUIRE(found);

    tm.shutdown();
}

// ============================================================================
// Phase 2: Fault Info & Register State Parsing [telemetry][crash]
// ============================================================================

TEST_CASE_METHOD(CrashTestFixture, "Crash: parse crash file extracts fault_addr",
                 "[telemetry][crash]") {
    write_crash_file("signal:11\nname:SIGSEGV\nversion:0.9.18\ntimestamp:1707350400\nuptime:3174\n"
                     "fault_addr:0x00000000\nfault_code:1\nfault_code_name:SEGV_MAPERR\n"
                     "reg_pc:0x00920bac\nreg_sp:0xbe8ff420\nreg_lr:0x0091a3c0\n"
                     "bt:0x920bac\nbt:0xf7101290\n");
    auto result = crash_handler::read_crash_file(crash_path());
    REQUIRE_FALSE(result.is_null());
    REQUIRE(result.contains("fault_addr"));
    REQUIRE(result["fault_addr"] == "0x00000000");
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: parse crash file extracts fault_code and name",
                 "[telemetry][crash]") {
    write_crash_file("signal:11\nname:SIGSEGV\nversion:0.9.18\ntimestamp:1707350400\nuptime:3174\n"
                     "fault_addr:0x00000000\nfault_code:1\nfault_code_name:SEGV_MAPERR\n"
                     "bt:0x920bac\n");
    auto result = crash_handler::read_crash_file(crash_path());
    REQUIRE_FALSE(result.is_null());
    REQUIRE(result.contains("fault_code"));
    REQUIRE(result["fault_code"] == 1);
    REQUIRE(result.contains("fault_code_name"));
    REQUIRE(result["fault_code_name"] == "SEGV_MAPERR");
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: parse crash file extracts register state",
                 "[telemetry][crash]") {
    write_crash_file("signal:11\nname:SIGSEGV\nversion:0.9.18\ntimestamp:1707350400\nuptime:3174\n"
                     "fault_addr:0xdeadbeef\nfault_code:2\nfault_code_name:SEGV_ACCERR\n"
                     "reg_pc:0x00920bac\nreg_sp:0xbe8ff420\nreg_lr:0x0091a3c0\n"
                     "bt:0x920bac\n");
    auto result = crash_handler::read_crash_file(crash_path());
    REQUIRE_FALSE(result.is_null());
    REQUIRE(result.contains("reg_pc"));
    REQUIRE(result["reg_pc"] == "0x00920bac");
    REQUIRE(result.contains("reg_sp"));
    REQUIRE(result["reg_sp"] == "0xbe8ff420");
    REQUIRE(result.contains("reg_lr"));
    REQUIRE(result["reg_lr"] == "0x0091a3c0");
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: parse crash file extracts reg_bp for x86_64",
                 "[telemetry][crash]") {
    write_crash_file("signal:11\nname:SIGSEGV\nversion:0.9.18\ntimestamp:1707350400\nuptime:3174\n"
                     "fault_addr:0x00000000\nfault_code:1\nfault_code_name:SEGV_MAPERR\n"
                     "reg_pc:0x00400abc\nreg_sp:0x7ffd12345678\nreg_bp:0x7ffd12345690\n"
                     "bt:0x400abc\n");
    auto result = crash_handler::read_crash_file(crash_path());
    REQUIRE_FALSE(result.is_null());
    REQUIRE(result.contains("reg_bp"));
    REQUIRE(result["reg_bp"] == "0x7ffd12345690");
    // Should NOT have reg_lr when reg_bp is present
    REQUIRE_FALSE(result.contains("reg_lr"));
}

TEST_CASE_METHOD(CrashTestFixture,
                 "Crash: parse old format crash file without fault/register fields",
                 "[telemetry][crash]") {
    // This is the existing format - should continue working
    write_realistic_crash_file();
    auto result = crash_handler::read_crash_file(crash_path());
    REQUIRE_FALSE(result.is_null());
    REQUIRE(result["signal"] == 11);
    REQUIRE(result["signal_name"] == "SIGSEGV");
    // New fields should be absent, not error
    REQUIRE_FALSE(result.contains("fault_addr"));
    REQUIRE_FALSE(result.contains("fault_code"));
    REQUIRE_FALSE(result.contains("fault_code_name"));
    REQUIRE_FALSE(result.contains("reg_pc"));
    REQUIRE_FALSE(result.contains("reg_sp"));
    REQUIRE_FALSE(result.contains("reg_lr"));
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: parse crash file with partial fault fields",
                 "[telemetry][crash]") {
    // Only fault_addr, no fault_code or registers
    write_crash_file("signal:11\nname:SIGSEGV\nversion:0.9.18\ntimestamp:1707350400\nuptime:100\n"
                     "fault_addr:0x00000000\n"
                     "bt:0x920bac\n");
    auto result = crash_handler::read_crash_file(crash_path());
    REQUIRE_FALSE(result.is_null());
    REQUIRE(result.contains("fault_addr"));
    REQUIRE(result["fault_addr"] == "0x00000000");
    REQUIRE_FALSE(result.contains("fault_code"));
    REQUIRE_FALSE(result.contains("reg_pc"));
}

TEST_CASE_METHOD(CrashTestFixture,
                 "Crash: write_mock_crash_file includes fault and register fields",
                 "[telemetry][crash]") {
    crash_handler::write_mock_crash_file(crash_path());
    auto result = crash_handler::read_crash_file(crash_path());
    REQUIRE_FALSE(result.is_null());
    // Mock file should include fault info
    REQUIRE(result.contains("fault_addr"));
    REQUIRE(result.contains("fault_code"));
    REQUIRE(result.contains("fault_code_name"));
    // Mock file should include at least PC and SP registers
    REQUIRE(result.contains("reg_pc"));
    REQUIRE(result.contains("reg_sp"));
}

TEST_CASE_METHOD(CrashTestFixture, "Crash: TelemetryManager crash event includes fault fields",
                 "[telemetry][crash]") {
    std::string tm_crash_path = (temp_dir() / "crash.txt").string();
    {
        std::ofstream ofs(tm_crash_path);
        ofs << "signal:11\nname:SIGSEGV\nversion:0.9.18\ntimestamp:1707350400\nuptime:3174\n"
            << "fault_addr:0x00000000\nfault_code:1\nfault_code_name:SEGV_MAPERR\n"
            << "reg_pc:0x00920bac\nreg_sp:0xbe8ff420\nreg_lr:0x0091a3c0\n"
            << "bt:0x920bac\nbt:0xf7101290\n";
    }
    {
        std::ofstream ofs((temp_dir() / "telemetry_config.json").string());
        ofs << R"({"enabled": true})";
    }

    auto& tm = TelemetryManager::instance();
    tm.shutdown();
    tm.init(temp_dir().string());

    auto snapshot = tm.get_queue_snapshot();
    bool found = false;
    for (const auto& event : snapshot) {
        if (event.contains("event") && event["event"] == "crash") {
            found = true;
            REQUIRE(event.contains("fault_addr"));
            REQUIRE(event["fault_addr"] == "0x00000000");
            REQUIRE(event.contains("fault_code"));
            REQUIRE(event["fault_code"] == 1);
            REQUIRE(event.contains("fault_code_name"));
            REQUIRE(event["fault_code_name"] == "SEGV_MAPERR");
            break;
        }
    }
    REQUIRE(found);

    fs::remove(tm_crash_path);
    tm.shutdown();
}
