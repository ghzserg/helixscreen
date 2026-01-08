// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file subject_debug_registry.h
 * @brief Debug registry for LVGL subjects
 *
 * Maps subject pointers to metadata (name, type, file, line) for debugging.
 * Useful for tracing subject updates and diagnosing binding issues.
 *
 * Usage:
 *   SubjectDebugRegistry::instance().register_subject(&subject, "name", type, __FILE__, __LINE__);
 *   auto* info = SubjectDebugRegistry::instance().lookup(&subject);
 */

#pragma once

#include <lvgl.h>
#include <mutex>
#include <string>
#include <unordered_map>

/**
 * @brief Debug information for a registered subject
 */
struct SubjectDebugInfo {
    std::string name;       ///< Human-readable name for the subject
    lv_subject_type_t type; ///< LVGL subject type
    std::string file;       ///< Source file where subject was registered
    int line;               ///< Line number where subject was registered
};

/**
 * @brief Registry mapping LVGL subject pointers to debug metadata
 *
 * Singleton that stores debug information for subjects. Useful for debugging
 * subject binding issues and tracing value updates.
 */
class SubjectDebugRegistry {
  public:
    /**
     * @brief Get the singleton instance
     * @return Reference to the global registry
     */
    static SubjectDebugRegistry& instance();

    /**
     * @brief Register a subject with debug metadata
     * @param subject Pointer to the LVGL subject
     * @param name Human-readable name for the subject
     * @param type LVGL subject type
     * @param file Source file (__FILE__)
     * @param line Line number (__LINE__)
     *
     * Re-registering the same pointer updates the existing entry.
     */
    void register_subject(lv_subject_t* subject, const std::string& name, lv_subject_type_t type,
                          const std::string& file, int line);

    /**
     * @brief Look up debug info for a subject
     * @param subject Pointer to the LVGL subject
     * @return Pointer to debug info, or nullptr if not registered
     */
    const SubjectDebugInfo* lookup(lv_subject_t* subject);

    /**
     * @brief Get human-readable name for a subject type
     * @param type LVGL subject type
     * @return String representation (e.g., "INT", "STRING", "POINTER")
     */
    static std::string type_name(lv_subject_type_t type);

    /**
     * @brief Log all registered subjects
     *
     * Dumps all registered subjects to the log at DEBUG level.
     */
    void dump_all_subjects();

    /**
     * @brief Clear all registrations
     *
     * Primarily for testing. Removes all registered subjects.
     */
    void clear();

  private:
    SubjectDebugRegistry() = default;
    ~SubjectDebugRegistry() = default;

    SubjectDebugRegistry(const SubjectDebugRegistry&) = delete;
    SubjectDebugRegistry& operator=(const SubjectDebugRegistry&) = delete;

    std::unordered_map<lv_subject_t*, SubjectDebugInfo> subjects_;
    mutable std::mutex mutex_; ///< Protects subjects_ map
};
