// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "logging_init.h"

#include "../catch_amalgamated.hpp"

using namespace helix::logging;

// ============================================================================
// parse_level() tests
// ============================================================================

TEST_CASE("parse_level: valid level strings", "[logging][config]") {
    SECTION("trace") {
        REQUIRE(parse_level("trace") == spdlog::level::trace);
    }

    SECTION("debug") {
        REQUIRE(parse_level("debug") == spdlog::level::debug);
    }

    SECTION("info") {
        REQUIRE(parse_level("info") == spdlog::level::info);
    }

    SECTION("warn") {
        REQUIRE(parse_level("warn") == spdlog::level::warn);
    }

    SECTION("warning (alias)") {
        REQUIRE(parse_level("warning") == spdlog::level::warn);
    }

    SECTION("error") {
        REQUIRE(parse_level("error") == spdlog::level::err);
    }

    SECTION("critical") {
        REQUIRE(parse_level("critical") == spdlog::level::critical);
    }

    SECTION("off") {
        REQUIRE(parse_level("off") == spdlog::level::off);
    }
}

TEST_CASE("parse_level: returns default for invalid input", "[logging][config]") {
    SECTION("empty string") {
        REQUIRE(parse_level("", spdlog::level::warn) == spdlog::level::warn);
        REQUIRE(parse_level("", spdlog::level::debug) == spdlog::level::debug);
    }

    SECTION("unrecognized string") {
        REQUIRE(parse_level("verbose", spdlog::level::warn) == spdlog::level::warn);
        REQUIRE(parse_level("TRACE", spdlog::level::info) == spdlog::level::info); // case sensitive
    }
}

// ============================================================================
// verbosity_to_level() tests
// ============================================================================

TEST_CASE("verbosity_to_level: CLI verbosity flags", "[logging][config]") {
    SECTION("-v (1) = info") {
        REQUIRE(verbosity_to_level(1) == spdlog::level::info);
    }

    SECTION("-vv (2) = debug") {
        REQUIRE(verbosity_to_level(2) == spdlog::level::debug);
    }

    SECTION("-vvv (3+) = trace") {
        REQUIRE(verbosity_to_level(3) == spdlog::level::trace);
        REQUIRE(verbosity_to_level(4) == spdlog::level::trace);
        REQUIRE(verbosity_to_level(10) == spdlog::level::trace);
    }

    SECTION("0 = warn (no verbosity flags)") {
        REQUIRE(verbosity_to_level(0) == spdlog::level::warn);
    }

    SECTION("negative = warn") {
        REQUIRE(verbosity_to_level(-1) == spdlog::level::warn);
    }
}

// ============================================================================
// to_hv_level() tests
// ============================================================================

TEST_CASE("to_hv_level: spdlog to libhv level mapping", "[logging][config]") {
    // libhv levels: VERBOSE(0) < DEBUG(1) < INFO(2) < WARN(3) < ERROR(4) < FATAL(5) < SILENT(6)

    SECTION("trace maps to DEBUG (libhv has no trace)") {
        REQUIRE(to_hv_level(spdlog::level::trace) == 1); // LOG_LEVEL_DEBUG
    }

    SECTION("debug maps to DEBUG") {
        REQUIRE(to_hv_level(spdlog::level::debug) == 1); // LOG_LEVEL_DEBUG
    }

    SECTION("info maps to INFO") {
        REQUIRE(to_hv_level(spdlog::level::info) == 2); // LOG_LEVEL_INFO
    }

    SECTION("warn maps to WARN") {
        REQUIRE(to_hv_level(spdlog::level::warn) == 3); // LOG_LEVEL_WARN
    }

    SECTION("error maps to ERROR") {
        REQUIRE(to_hv_level(spdlog::level::err) == 4); // LOG_LEVEL_ERROR
    }

    SECTION("critical maps to FATAL") {
        REQUIRE(to_hv_level(spdlog::level::critical) == 5); // LOG_LEVEL_FATAL
    }

    SECTION("off maps to SILENT") {
        REQUIRE(to_hv_level(spdlog::level::off) == 6); // LOG_LEVEL_SILENT
    }
}

// ============================================================================
// resolve_log_level() tests
// ============================================================================

TEST_CASE("resolve_log_level: precedence rules", "[logging][config]") {
    SECTION("CLI verbosity takes precedence over config") {
        // CLI says -vv (debug), config says "error"
        auto level = resolve_log_level(2, "error", false);
        REQUIRE(level == spdlog::level::debug);
    }

    SECTION("config file used when no CLI verbosity") {
        auto level = resolve_log_level(0, "trace", false);
        REQUIRE(level == spdlog::level::trace);
    }

    SECTION("test_mode defaults to debug when no CLI or config") {
        auto level = resolve_log_level(0, "", true);
        REQUIRE(level == spdlog::level::debug);
    }

    SECTION("production defaults to warn when no CLI or config") {
        auto level = resolve_log_level(0, "", false);
        REQUIRE(level == spdlog::level::warn);
    }

    SECTION("CLI verbosity beats test_mode default") {
        // CLI says -v (info), test mode would default to debug
        auto level = resolve_log_level(1, "", true);
        REQUIRE(level == spdlog::level::info);
    }

    SECTION("config beats test_mode default") {
        // Config says warn, test mode would default to debug
        auto level = resolve_log_level(0, "warn", true);
        REQUIRE(level == spdlog::level::warn);
    }
}

// ============================================================================
// Existing parse_log_target() tests (ensure we didn't break it)
// ============================================================================

TEST_CASE("parse_log_target: valid targets", "[logging][config]") {
    REQUIRE(parse_log_target("auto") == LogTarget::Auto);
    REQUIRE(parse_log_target("journal") == LogTarget::Journal);
    REQUIRE(parse_log_target("syslog") == LogTarget::Syslog);
    REQUIRE(parse_log_target("file") == LogTarget::File);
    REQUIRE(parse_log_target("console") == LogTarget::Console);
}

TEST_CASE("parse_log_target: defaults to Auto for unknown", "[logging][config]") {
    REQUIRE(parse_log_target("unknown") == LogTarget::Auto);
    REQUIRE(parse_log_target("") == LogTarget::Auto);
    REQUIRE(parse_log_target("CONSOLE") == LogTarget::Auto); // case sensitive
}

TEST_CASE("log_target_name: round-trip", "[logging][config]") {
    REQUIRE(std::string(log_target_name(LogTarget::Auto)) == "auto");
    REQUIRE(std::string(log_target_name(LogTarget::Journal)) == "journal");
    REQUIRE(std::string(log_target_name(LogTarget::Syslog)) == "syslog");
    REQUIRE(std::string(log_target_name(LogTarget::File)) == "file");
    REQUIRE(std::string(log_target_name(LogTarget::Console)) == "console");
}
