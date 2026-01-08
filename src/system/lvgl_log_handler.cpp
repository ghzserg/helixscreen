// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lvgl_log_handler.h"

#include "runtime_config.h"
#include "subject_debug_registry.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <lvgl.h>
#include <string>
#include <string_view>

// Stack trace support (macOS and Linux with glibc)
#if defined(__APPLE__) || (defined(__linux__) && defined(__GLIBC__))
#define HAVE_STACK_TRACE 1
#include <cxxabi.h>   // For __cxa_demangle (symbol demangling)
#include <execinfo.h> // For backtrace() and backtrace_symbols()
#endif

namespace {

#ifdef HAVE_STACK_TRACE
/**
 * @brief Demangle a C++ symbol name
 * @param mangled The mangled symbol name
 * @return Demangled name, or original if demangling fails
 */
std::string demangle_symbol(const char* mangled) {
    // Extract the mangled name from the symbol string
    // Format on macOS: "N  path  address  symbol + offset"
    // Format on Linux: "path(symbol+offset) [address]" or "path [address]"

    std::string result(mangled);

#if defined(__APPLE__)
    // macOS format: "0   helix-screen  0x...  _ZN5helix... + 123"
    // Find the mangled symbol (starts with _ and contains letters)
    const char* last_space = nullptr;

    // Find the last occurrence of a space followed by an underscore
    for (const char* p = mangled; *p != '\0'; ++p) {
        if (*p == ' ' && *(p + 1) == '_') {
            last_space = p + 1;
        }
    }

    if (last_space != nullptr) {
        // Find the end of the symbol (space before " + ")
        const char* sym_end = last_space;
        while (*sym_end != '\0' && *sym_end != ' ') {
            ++sym_end;
        }

        std::string symbol(last_space, sym_end);

        int status = 0;
        char* demangled = abi::__cxa_demangle(symbol.c_str(), nullptr, nullptr, &status);
        if (status == 0 && demangled != nullptr) {
            // Replace the mangled symbol with demangled in the output
            size_t sym_pos = result.find(symbol);
            if (sym_pos != std::string::npos) {
                result.replace(sym_pos, symbol.length(), demangled);
            }
            free(demangled);
        }
    }
#elif defined(__linux__)
    // Linux format: "path(mangled+0x123) [0x...]"
    size_t paren_start = result.find('(');
    size_t plus_pos = result.find('+', paren_start);
    if (paren_start != std::string::npos && plus_pos != std::string::npos) {
        std::string symbol = result.substr(paren_start + 1, plus_pos - paren_start - 1);
        if (!symbol.empty()) {
            int status = 0;
            char* demangled = abi::__cxa_demangle(symbol.c_str(), nullptr, nullptr, &status);
            if (status == 0 && demangled != nullptr) {
                result.replace(paren_start + 1, symbol.length(), demangled);
                free(demangled);
            }
        }
    }
#endif

    return result;
}

/**
 * @brief Print a stack trace to help debug subject type mismatches
 *
 * Captures the current call stack and logs it using spdlog.
 * Attempts to demangle C++ symbol names for readability.
 */
void print_stack_trace() {
    constexpr int MAX_FRAMES = 32;
    void* callstack[MAX_FRAMES];

    int frames = backtrace(callstack, MAX_FRAMES);
    char** symbols = backtrace_symbols(callstack, frames);

    if (symbols == nullptr) {
        spdlog::warn("  Stack trace: (unable to capture)");
        return;
    }

    spdlog::warn("  Stack trace:");
    // Skip first 3 frames: print_stack_trace, lvgl_log_callback, and LVGL internal
    for (int i = 3; i < frames; ++i) {
        std::string demangled = demangle_symbol(symbols[i]);
        spdlog::warn("    #{} {}", i - 3, demangled);
    }

    free(symbols);
}
#endif // HAVE_STACK_TRACE

/**
 * @brief Parse a pointer from a hex string like "0x7f8a1234"
 * @param hex_str Hex string starting with "0x"
 * @return Parsed pointer value, or nullptr on failure
 */
void* parse_pointer(const char* hex_str) {
    if (hex_str == nullptr || hex_str[0] != '0' || (hex_str[1] != 'x' && hex_str[1] != 'X')) {
        return nullptr;
    }
    char* end = nullptr;
    uintptr_t addr = std::strtoull(hex_str, &end, 16);
    if (end == hex_str) {
        return nullptr;
    }
    return reinterpret_cast<void*>(addr);
}

/**
 * @brief Check if the log message is a subject type mismatch warning
 *
 * LVGL warns with: "Subject type is not X"
 * After patching LVGL, we may also see: "(ptr=0x..., type=N)"
 *
 * @param buf The log message buffer
 * @return true if this is a subject type mismatch warning
 */
bool is_subject_type_mismatch(const char* buf) {
    return std::strstr(buf, "Subject type is not") != nullptr;
}

/**
 * @brief Try to extract pointer from enhanced LVGL log message
 *
 * Looks for pattern: "(ptr=0x...," or "(ptr=0x...)"
 *
 * @param buf The log message buffer
 * @return Pointer if found, nullptr otherwise
 */
lv_subject_t* extract_subject_pointer(const char* buf) {
    // Look for "(ptr=0x"
    const char* ptr_start = std::strstr(buf, "(ptr=0x");
    if (ptr_start == nullptr) {
        return nullptr;
    }

    // Move past "(ptr="
    ptr_start += 5; // Skip "(ptr="

    return static_cast<lv_subject_t*>(parse_pointer(ptr_start));
}

/**
 * @brief Extract expected type from "Subject type is not X" message
 * @param buf The log message buffer
 * @return Expected type name or empty string
 */
std::string extract_expected_type(const char* buf) {
    const char* pattern = "Subject type is not ";
    const char* start = std::strstr(buf, pattern);
    if (start == nullptr) {
        return "";
    }

    start += std::strlen(pattern);

    // Find end of type name (next space, newline, or end)
    const char* end = start;
    while (*end != '\0' && *end != ' ' && *end != '\n' && *end != '\r' && *end != '(') {
        ++end;
    }

    return std::string(start, end);
}

/**
 * @brief Log enhanced subject debug info when available
 * @param ptr Subject pointer from the warning
 */
void log_subject_debug_info(lv_subject_t* ptr) {
    if (ptr == nullptr) {
        return;
    }

    auto* info = SubjectDebugRegistry::instance().lookup(ptr);
    if (info != nullptr) {
        spdlog::warn("  -> Subject: \"{}\" ({}) registered at {}:{}", info->name,
                     SubjectDebugRegistry::type_name(info->type), info->file, info->line);
    } else {
        spdlog::warn("  -> Subject at {} not found in debug registry", static_cast<void*>(ptr));
    }
}

/**
 * @brief LVGL log callback that routes to spdlog
 *
 * Called by LVGL for all log messages. Routes to appropriate spdlog level
 * and provides enhanced debugging for subject type mismatch warnings.
 *
 * @param level LVGL log level
 * @param buf Log message buffer (null-terminated)
 */
void lvgl_log_callback(lv_log_level_t level, const char* buf) {
    // Strip trailing newline if present (spdlog adds its own)
    std::string msg(buf);
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
        msg.pop_back();
    }

    // Route to appropriate spdlog level
    switch (level) {
    case LV_LOG_LEVEL_TRACE:
        spdlog::trace("[LVGL] {}", msg);
        break;
    case LV_LOG_LEVEL_INFO:
        spdlog::info("[LVGL] {}", msg);
        break;
    case LV_LOG_LEVEL_WARN:
        spdlog::warn("[LVGL] {}", msg);
        break;
    case LV_LOG_LEVEL_ERROR:
        spdlog::error("[LVGL] {}", msg);
        break;
    case LV_LOG_LEVEL_USER:
        spdlog::info("[LVGL:USER] {}", msg);
        break;
    case LV_LOG_LEVEL_NONE:
    default:
        // Should not reach here, but log anyway
        spdlog::debug("[LVGL] {}", msg);
        break;
    }

    // Enhanced subject debugging for type mismatch warnings
    if (is_subject_type_mismatch(buf)) {
        std::string expected_type = extract_expected_type(buf);
        if (!expected_type.empty()) {
            spdlog::warn("  -> Expected type: {}", expected_type);
        }

        // Try to extract and lookup subject pointer
        lv_subject_t* ptr = extract_subject_pointer(buf);
        log_subject_debug_info(ptr);

#ifdef HAVE_STACK_TRACE
        // Print stack trace to help identify the code path that triggered the mismatch
        if (RuntimeConfig::debug_subjects()) {
            print_stack_trace();
        }
#endif
    }
}

} // namespace

namespace helix {
namespace logging {

void register_lvgl_log_handler() {
    lv_log_register_print_cb(lvgl_log_callback);
    spdlog::debug("[Logging] Registered custom LVGL log handler");
}

} // namespace logging
} // namespace helix
