// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "logging_init.h"

#include "lvgl_assert_handler.h"
#include "lvgl_log_handler.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>
#include <filesystem>
#include <lvgl.h>
#include <src/display/lv_display_private.h>
#include <vector>

// Define the global callback pointer for LVGL assert handler
helix_assert_callback_t g_helix_assert_cpp_callback = nullptr;

#ifdef __linux__
#ifdef HELIX_HAS_SYSTEMD
#include <spdlog/sinks/systemd_sink.h>
#endif
#include <spdlog/sinks/syslog_sink.h>
#endif

namespace helix {
namespace logging {

namespace {

/// Check if a path is writable (for file logging location selection)
bool is_path_writable(const std::string& path) {
    // Check parent directory for new files, or file itself if exists
    std::filesystem::path p(path);
    std::filesystem::path dir = p.parent_path();

    if (dir.empty()) {
        dir = ".";
    }

    // Check if directory exists and is writable
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        return false;
    }

    // Try to determine write permission
    auto perms = std::filesystem::status(dir, ec).permissions();
    if (ec) {
        return false;
    }

    // Check owner write permission (simplified check)
    return (perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none;
}

/// Get XDG_DATA_HOME or default ~/.local/share
std::string get_xdg_data_home() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && xdg[0] != '\0') {
        return xdg;
    }

    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::string(home) + "/.local/share";
    }

    return "/tmp"; // Last resort fallback
}

/// Resolve log file path with fallback logic
std::string resolve_log_file_path(const std::string& override_path) {
    if (!override_path.empty()) {
        return override_path;
    }

    // Try /var/log first (requires permissions, typical for system services)
    const std::string var_log = "/var/log/helix-screen.log";
    if (is_path_writable(var_log)) {
        return var_log;
    }

    // Fallback to user directory
    std::string user_dir = get_xdg_data_home() + "/helix-screen";
    std::error_code ec;
    std::filesystem::create_directories(user_dir, ec);

    return user_dir + "/helix.log";
}

/// Detect best available logging target at runtime
LogTarget detect_best_target() {
#ifdef __linux__
#ifdef HELIX_HAS_SYSTEMD
    // Check for systemd journal socket
    std::error_code ec;
    if (std::filesystem::exists("/run/systemd/journal/socket", ec)) {
        return LogTarget::Journal;
    }
#endif
    // Syslog is always available on Linux
    return LogTarget::Syslog;
#else
    // macOS/other: console only by default
    return LogTarget::Console;
#endif
}

/// Add system sink based on target
void add_system_sink(std::vector<spdlog::sink_ptr>& sinks, LogTarget target,
                     const std::string& file_path) {
    switch (target) {
#ifdef __linux__
#ifdef HELIX_HAS_SYSTEMD
    case LogTarget::Journal:
        sinks.push_back(std::make_shared<spdlog::sinks::systemd_sink_mt>("helix-screen"));
        break;
#endif
    case LogTarget::Syslog:
        sinks.push_back(std::make_shared<spdlog::sinks::syslog_sink_mt>("helix-screen", LOG_PID,
                                                                        LOG_USER, false));
        break;
#endif
    case LogTarget::File: {
        std::string path = resolve_log_file_path(file_path);
        // 5MB max size, 3 rotated files
        sinks.push_back(
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path, 5 * 1024 * 1024, 3));
        break;
    }
    case LogTarget::Console:
    case LogTarget::Auto:
        // Console-only or auto (which would have been resolved already)
        // No additional sink needed
        break;
#ifdef __linux__
    default:
        // Handle Journal case when HELIX_HAS_SYSTEMD is not defined
        // Fall back to syslog
        if (target == LogTarget::Journal) {
            sinks.push_back(std::make_shared<spdlog::sinks::syslog_sink_mt>("helix-screen", LOG_PID,
                                                                            LOG_USER, false));
        }
        break;
#else
    default:
        break;
#endif
    }
}

/// C++ assert callback that logs via spdlog and dumps backtrace
void lvgl_assert_spdlog_callback(const char* file, int line, const char* func) {
    // Log via spdlog for consistent logging across all outputs
    spdlog::critical("╔═══════════════════════════════════════════════════════════╗");
    spdlog::critical("║              LVGL ASSERTION FAILED                        ║");
    spdlog::critical("╠═══════════════════════════════════════════════════════════╣");
    spdlog::critical("║ File: {}", file);
    spdlog::critical("║ Line: {}", line);
    spdlog::critical("║ Func: {}()", func);

    // Log LVGL display state if available
    lv_display_t* disp = lv_display_get_default();
    if (disp) {
        spdlog::critical("║ Display rendering_in_progress: {}",
                         disp->rendering_in_progress ? "YES (!!)" : "no");
    } else {
        spdlog::critical("║ Display: not initialized");
    }
    spdlog::critical("╚═══════════════════════════════════════════════════════════╝");

    // Dump recent log messages that led up to this assertion
    spdlog::critical("=== Recent log messages (backtrace) ===");
    spdlog::dump_backtrace();
}

} // namespace

void init_early() {
    // Create minimal console-only logger at WARN level
    // This allows early startup code to log without crashing
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("helix", console);
    logger->set_level(spdlog::level::warn);
    spdlog::set_default_logger(logger);
}

void init(const LogConfig& config) {
    std::vector<spdlog::sink_ptr> sinks;

    // Console sink (always, unless explicitly disabled)
    if (config.enable_console) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    // Resolve auto-detection
    LogTarget effective_target =
        (config.target == LogTarget::Auto) ? detect_best_target() : config.target;

    // Add system sink
    add_system_sink(sinks, effective_target, config.file_path);

    // Create logger with all sinks
    auto logger = std::make_shared<spdlog::logger>("helix", sinks.begin(), sinks.end());
    logger->set_level(config.level);

    // Set as default logger
    spdlog::set_default_logger(logger);

    // Enable backtrace buffer to capture recent log messages before an assertion
    // These get dumped when spdlog::dump_backtrace() is called in the assert handler
    spdlog::enable_backtrace(32);

    // Register C++ callback for LVGL assert handler
    // This provides spdlog integration and LVGL state context
    g_helix_assert_cpp_callback = lvgl_assert_spdlog_callback;

    // NOTE: LVGL log handler is registered separately AFTER lv_init()
    // because lv_init() resets the global state and clears any callbacks.
    // See Application::init_display() which calls register_lvgl_log_handler().

    // Log what we configured (at debug level so it's not noisy)
    spdlog::debug("[Logging] Initialized: target={}, console={}, backtrace=32 messages",
                  log_target_name(effective_target), config.enable_console ? "yes" : "no");
}

LogTarget parse_log_target(const std::string& str) {
    if (str == "journal")
        return LogTarget::Journal;
    if (str == "syslog")
        return LogTarget::Syslog;
    if (str == "file")
        return LogTarget::File;
    if (str == "console")
        return LogTarget::Console;
    return LogTarget::Auto; // Default for "auto" or unrecognized
}

const char* log_target_name(LogTarget target) {
    switch (target) {
    case LogTarget::Auto:
        return "auto";
    case LogTarget::Journal:
        return "journal";
    case LogTarget::Syslog:
        return "syslog";
    case LogTarget::File:
        return "file";
    case LogTarget::Console:
        return "console";
    }
    return "unknown";
}

} // namespace logging
} // namespace helix
