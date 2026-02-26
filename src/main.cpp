// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file main.cpp
 * @brief Application entry point
 *
 * This file is intentionally minimal. All application logic is implemented
 * in the Application class (src/application/application.cpp).
 *
 * @see Application
 */

#include "application.h"

#include <cstdio>
#include <cstdlib>
#include <exception>

// SDL2 redefines main → SDL_main via this header.
// On Android, the SDL Java activity loads libmain.so and calls SDL_main().
// Without this include, the symbol is missing and the app crashes on launch.
#ifdef HELIX_PLATFORM_ANDROID
#include <SDL.h>
#endif

// Log to stderr using only async-signal-safe-ish functions.
// spdlog may not be initialized yet or may be in a broken state.
static void log_fatal(const char* msg) {
    fprintf(stderr, "[FATAL] %s\n", msg);
    fflush(stderr);
}

// Called by std::terminate() — covers uncaught exceptions, joinable thread
// destruction, and other fatal C++ runtime errors. Logs what we can before
// the default terminate handler calls abort() (which triggers crash_handler).
static void terminate_handler() {
    // Check if there's a current exception we can inspect
    if (auto eptr = std::current_exception()) {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception& e) {
            fprintf(stderr, "[FATAL] Uncaught exception: %s\n", e.what());
            fflush(stderr);
        } catch (...) {
            log_fatal("Uncaught non-std::exception");
        }
    } else {
        log_fatal("std::terminate() called without active exception "
                  "(joinable thread destroyed? noexcept violation?)");
    }

    // Call abort() to trigger the crash_handler signal handler, which writes
    // crash.txt for telemetry. Do NOT call std::abort() which may skip our
    // signal handler on some platforms.
    abort();
}

int main(int argc, char** argv) {
    std::set_terminate(terminate_handler);

    try {
        Application app;
        return app.run(argc, argv);
    } catch (const std::exception& e) {
        fprintf(stderr, "[FATAL] Unhandled exception in Application: %s\n", e.what());
        fflush(stderr);
        return 1;
    } catch (...) {
        log_fatal("Unhandled non-std::exception in Application");
        return 1;
    }
}
