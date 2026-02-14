// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file crash_reporter.h
 * @brief Standalone crash reporter — sends crash data to developer on next launch
 *
 * When HelixScreen crashes, crash_handler.cpp writes config/crash.txt with signal,
 * version, uptime, and backtrace. On next startup, CrashReporter detects this file,
 * collects additional context (platform, logs, hardware info), and offers the user
 * a dialog to send the report.
 *
 * Delivery priority:
 * 1. Auto-send via CF Worker at crash.helixscreen.org → GitHub issue
 * 2. QR code with pre-filled GitHub issue URL (if no network)
 * 3. File fallback to ~/helixscreen/crash_report.txt (always)
 *
 * Independent of TelemetryManager — works without telemetry opt-in.
 */

#include <string>
#include <vector>

#include "hv/json.hpp"

class CrashReporter {
  public:
    static CrashReporter& instance();

    /**
     * @brief Initialize crash reporter with config directory
     * @param config_dir Directory containing crash.txt (e.g., "config" or temp dir for tests)
     */
    void init(const std::string& config_dir);

    /**
     * @brief Reset state for clean re-initialization (used in tests)
     */
    void shutdown();

    /**
     * @brief Check if crash.txt exists from a previous crash
     */
    bool has_crash_report() const;

    /**
     * @brief Structured crash report with all collected context
     */
    struct CrashReport {
        // From crash.txt
        int signal = 0;
        std::string signal_name;
        std::string app_version;
        std::string timestamp;
        int uptime_sec = 0;
        std::vector<std::string> backtrace;

        // Fault info (Phase 2 - from siginfo_t)
        std::string fault_addr;
        int fault_code = 0;
        std::string fault_code_name;

        // Register state (Phase 2 - from ucontext_t)
        std::string reg_pc;
        std::string reg_sp;
        std::string reg_lr; // ARM only
        std::string reg_bp; // x86_64 only

        // Additional context (collected at startup)
        std::string platform;
        std::string printer_model;
        std::string klipper_version;
        std::string log_tail;
        std::string display_info;
        int ram_total_mb = 0;
        int cpu_cores = 0;
    };

    /**
     * @brief Collect crash data from crash.txt + system context
     * @return Populated CrashReport struct
     */
    CrashReport collect_report();

    /**
     * @brief Attempt to send crash report to CF Worker
     * @return true if report was sent successfully
     */
    bool try_auto_send(const CrashReport& report);

    /**
     * @brief Generate a pre-filled GitHub issue URL (for QR code)
     *
     * URL is truncated to stay under ~2000 chars for QR code compatibility.
     */
    std::string generate_github_url(const CrashReport& report);

    /**
     * @brief Save human-readable crash report to file
     * @return true if file was written successfully
     */
    bool save_to_file(const CrashReport& report);

    /**
     * @brief Delete crash.txt after handling (prevents re-processing)
     */
    void consume_crash_file();

    /**
     * @brief Convert crash report to JSON (for CF Worker POST)
     */
    nlohmann::json report_to_json(const CrashReport& report);

    /**
     * @brief Convert crash report to human-readable text
     */
    std::string report_to_text(const CrashReport& report);

    /**
     * @brief Read the last N lines from the log file
     * @param num_lines Number of lines to read from end of file
     * @return Last N lines as a string, empty if log not found
     */
    std::string get_log_tail(int num_lines = 50);

    /// Worker endpoint for auto-send
    static constexpr const char* CRASH_WORKER_URL = "https://crash.helixscreen.org/v1/report";

    /// Shared ingest API key (same as telemetry — write-only, not a true secret)
    static constexpr const char* INGEST_API_KEY = "hx-tel-v1-a7f3c9e2d1b84056";

    /// GitHub repo for issue URL generation
    static constexpr const char* GITHUB_REPO = "prestonbrown/helixscreen";

  private:
    CrashReporter() = default;
    ~CrashReporter() = default;

    CrashReporter(const CrashReporter&) = delete;
    CrashReporter& operator=(const CrashReporter&) = delete;

    std::string config_dir_;
    bool initialized_ = false;

    std::string crash_file_path() const;
    std::string report_file_path() const;
};
