// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file lvgl_assert_handler.h
 * @brief Custom LVGL assert handler with enhanced debugging context
 *
 * This handler logs assertion failures with:
 * - File/line/function info
 * - Stack traces (Linux/macOS)
 * - Optional C++ callback for spdlog integration
 * - LVGL display state context
 *
 * Continues execution after logging (does not halt).
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

// Stack trace support (glibc Linux / macOS only)
// musl libc (used by K1 series) doesn't have execinfo.h
#if defined(__APPLE__) || (defined(__linux__) && defined(__GLIBC__))
#include <execinfo.h>
#define HELIX_HAS_BACKTRACE 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback type for C++ assert handler extension
 *
 * Set this to a C++ function to get spdlog integration.
 * Called after the C handler logs to stderr/file.
 */
typedef void (*helix_assert_callback_t)(const char* file, int line, const char* func);

// Global callback pointer - defined in logging_init.cpp for main app,
// or in helix_splash.cpp (as NULL) for splash binary which doesn't link logging.
extern helix_assert_callback_t g_helix_assert_cpp_callback;

/**
 * @brief Print stack trace to file descriptor
 */
static inline void helix_print_backtrace(int fd) {
#ifdef HELIX_HAS_BACKTRACE
    void* callstack[32];
    int frames = backtrace(callstack, 32);
    if (frames > 0) {
        dprintf(fd, "\n=== Stack Trace (%d frames) ===\n", frames);
        backtrace_symbols_fd(callstack, frames, fd);
        dprintf(fd, "===================\n\n");
    }
#else
    (void)fd;
    dprintf(fd, "(Stack trace not available on this platform)\n");
#endif
}

/**
 * @brief Custom LVGL assert handler that logs and continues
 *
 * Called when LV_ASSERT fails. Logs detailed information including
 * stack trace and optionally calls C++ handler for spdlog integration.
 *
 * @param file Source file where assertion failed
 * @param line Line number where assertion failed
 * @param func Function name where assertion failed
 */
static inline void helix_lvgl_assert_handler(const char* file, int line, const char* func) {
    // Get timestamp
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    // Log to stderr (captured by syslog on embedded)
    fprintf(stderr,
            "\n"
            "╔══════════════════════════════════════════════════════════════╗\n"
            "║                    LVGL ASSERTION FAILED                     ║\n"
            "╠══════════════════════════════════════════════════════════════╣\n"
            "║ Time: %-54s ║\n"
            "║ File: %-54s ║\n"
            "║ Line: %-54d ║\n"
            "║ Func: %-54s ║\n"
            "╠══════════════════════════════════════════════════════════════╣\n"
            "║ Common causes:                                               ║\n"
            "║   1. lv_obj_invalidate() during render phase                 ║\n"
            "║   2. Subject observers triggering UI updates                 ║\n"
            "║   3. Async callbacks not using helix::ui::async_call()               ║\n"
            "║   4. NULL font/text/attributes in text operations            ║\n"
            "╚══════════════════════════════════════════════════════════════╝\n",
            time_buf, file, line, func);
    fflush(stderr);

    // Print stack trace to stderr
    helix_print_backtrace(STDERR_FILENO);

    // /tmp/helix_assert.log intentionally stays in /tmp - signal handler context
    // requires async-signal-safe functions only; can't use get_helix_cache_dir()
    FILE* log = fopen("/tmp/helix_assert.log", "a");
    if (log) {
        fprintf(log, "[%s] LVGL ASSERT at %s:%d in %s()\n", time_buf, file, line, func);
        fflush(log);
        // Write stack trace to log file too
        int log_fd = fileno(log);
        if (log_fd >= 0) {
            helix_print_backtrace(log_fd);
        }
        fclose(log);
    }

    // Call C++ callback if registered (for spdlog integration)
    if (g_helix_assert_cpp_callback) {
        g_helix_assert_cpp_callback(file, line, func);
    }
}

#ifdef __cplusplus
}
#endif
