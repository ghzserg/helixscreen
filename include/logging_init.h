// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <spdlog/spdlog.h>

#include <string>

namespace helix {
namespace logging {

/**
 * @brief Log destination targets
 *
 * On Linux, the system will auto-detect the best available target:
 * - Journal (systemd) if /run/systemd/journal/socket exists
 * - Syslog as fallback
 * - File as final fallback
 *
 * On macOS, only Console and File are available.
 */
enum class LogTarget {
    Auto,    ///< Detect best available (default)
    Journal, ///< systemd journal (Linux only)
    Syslog,  ///< Traditional syslog (Linux only)
    File,    ///< Rotating file log
    Console  ///< Console only (disable system logging)
};

/**
 * @brief Logging configuration
 */
struct LogConfig {
    spdlog::level::level_enum level = spdlog::level::warn;
    bool enable_console = true;         ///< Always show console output
    LogTarget target = LogTarget::Auto; ///< System log destination
    std::string file_path;              ///< Override file path (empty = auto)
};

/**
 * @brief Initialize minimal logging for early startup
 *
 * Sets up a basic console logger at WARN level. Call this FIRST in main()
 * before any log calls. The full init() can reconfigure later with user
 * preferences from CLI args and config files.
 */
void init_early();

/**
 * @brief Initialize logging subsystem
 *
 * Call once at startup before any log calls. Creates a multi-sink logger
 * that writes to both console (if enabled) and the selected system target.
 *
 * @param config Logging configuration
 */
void init(const LogConfig& config);

/**
 * @brief Parse log target from string
 *
 * @param str One of: "auto", "journal", "syslog", "file", "console"
 * @return Corresponding LogTarget enum value (Auto if unrecognized)
 */
LogTarget parse_log_target(const std::string& str);

/**
 * @brief Get string name for log target
 *
 * @param target LogTarget enum value
 * @return Human-readable name (e.g., "journal", "syslog")
 */
const char* log_target_name(LogTarget target);

/**
 * @brief Parse log level from string
 *
 * @param str One of: "trace", "debug", "info", "warn", "warning", "error", "critical", "off"
 * @param default_level Level to return if string is empty or unrecognized
 * @return Corresponding spdlog level enum
 */
spdlog::level::level_enum
parse_level(const std::string& str, spdlog::level::level_enum default_level = spdlog::level::warn);

/**
 * @brief Convert CLI verbosity count to log level
 *
 * Maps: 0 -> warn, 1 -> info, 2 -> debug, 3+ -> trace
 *
 * @param verbosity Number of -v flags (0 = none)
 * @return Corresponding spdlog level
 */
spdlog::level::level_enum verbosity_to_level(int verbosity);

/**
 * @brief Convert spdlog level to libhv level
 *
 * libhv levels: VERBOSE(0) < DEBUG(1) < INFO(2) < WARN(3) < ERROR(4) < FATAL(5) < SILENT(6)
 *
 * @param level spdlog log level
 * @return libhv log level integer
 */
int to_hv_level(spdlog::level::level_enum level);

/**
 * @brief Resolve log level with precedence: CLI > config > defaults
 *
 * @param cli_verbosity CLI -v flag count (0 = none)
 * @param config_level_str Log level from config file (empty = not set)
 * @param test_mode True if running in test mode (affects default)
 * @return Resolved log level
 */
spdlog::level::level_enum resolve_log_level(int cli_verbosity, const std::string& config_level_str,
                                            bool test_mode);

} // namespace logging
} // namespace helix
