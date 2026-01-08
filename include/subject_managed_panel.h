// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file subject_managed_panel.h
 * @brief RAII helper for automatic subject deinitialization in panels
 *
 * SubjectManager provides automatic cleanup for LVGL subjects registered with panels.
 * Panels register their subjects during init_subjects(), and the manager automatically
 * calls lv_subject_deinit() on all registered subjects when destroyed.
 *
 * @pattern RAII (Resource Acquisition Is Initialization)
 * @threading Main thread only (LVGL is not thread-safe)
 *
 * ## Usage Pattern:
 *
 * @code
 * class MyPanel : public PanelBase {
 * public:
 *     void init_subjects() override {
 *         if (subjects_initialized_) return;
 *
 *         // Initialize and register subjects with the manager
 *         lv_subject_init_int(&my_count_, 0);
 *         subjects_.register_subject(&my_count_);
 *
 *         lv_subject_init_string(&my_label_, label_buf_, nullptr, sizeof(label_buf_), "");
 *         subjects_.register_subject(&my_label_);
 *
 *         subjects_initialized_ = true;
 *     }
 *
 *     ~MyPanel() {
 *         // subjects_.deinit_all() called automatically, OR:
 *         subjects_.deinit_all();  // Explicit call if destructor does other work first
 *     }
 *
 * private:
 *     SubjectManager subjects_;  // RAII - auto-deinits on destruction
 *     lv_subject_t my_count_;
 *     lv_subject_t my_label_;
 *     char label_buf_[64];
 * };
 * @endcode
 *
 * ## Integration with UI_SUBJECT_INIT_AND_REGISTER_* Macros:
 *
 * The existing macros can be combined with SubjectManager:
 *
 * @code
 * UI_SUBJECT_INIT_AND_REGISTER_INT(my_count_, 0, "my_count");
 * subjects_.register_subject(&my_count_);
 * @endcode
 *
 * ## Thread Safety:
 *
 * SubjectManager is NOT thread-safe. All calls must happen on the main (LVGL) thread.
 * This matches LVGL's threading model.
 *
 * @see PanelBase for base class with subjects_initialized_ flag
 * @see OverlayBase for overlay base class with same pattern
 */

#pragma once

#include "lvgl/lvgl.h"
#include "subject_debug_registry.h"

#include <spdlog/spdlog.h>

#include <vector>

/**
 * @class SubjectManager
 * @brief RAII container for automatic LVGL subject cleanup
 *
 * Tracks registered lv_subject_t pointers and deinitializes them all in destructor.
 * Guards against double-deinit by clearing the list after deinitialization.
 */
class SubjectManager {
  public:
    /**
     * @brief Default constructor
     */
    SubjectManager() = default;

    /**
     * @brief Destructor - automatically deinitializes all registered subjects
     */
    ~SubjectManager() {
        deinit_all();
    }

    // Non-copyable (subject ownership is unique)
    SubjectManager(const SubjectManager&) = delete;
    SubjectManager& operator=(const SubjectManager&) = delete;

    // Movable (transfers subject ownership)
    SubjectManager(SubjectManager&& other) noexcept : subjects_(std::move(other.subjects_)) {
        other.subjects_.clear();
    }

    SubjectManager& operator=(SubjectManager&& other) noexcept {
        if (this != &other) {
            deinit_all();
            subjects_ = std::move(other.subjects_);
            other.subjects_.clear();
        }
        return *this;
    }

    /**
     * @brief Register a subject for automatic cleanup
     *
     * Call this after lv_subject_init_*() to ensure the subject is
     * deinitialized when this SubjectManager is destroyed.
     *
     * @param subject Pointer to initialized lv_subject_t (must not be null)
     *
     * @note Null pointers are safely ignored with a warning log
     * @note Duplicate registrations are ignored (no double-deinit)
     */
    void register_subject(lv_subject_t* subject) {
        if (!subject) {
            spdlog::warn("[SubjectManager] Attempted to register null subject");
            return;
        }

        // Check for duplicates
        for (const auto* s : subjects_) {
            if (s == subject) {
                spdlog::warn("[SubjectManager] Subject already registered, ignoring duplicate");
                return;
            }
        }

        subjects_.push_back(subject);
    }

    /**
     * @brief Deinitialize all registered subjects
     *
     * Called automatically by destructor. Can also be called manually
     * for explicit cleanup ordering (e.g., before lv_deinit()).
     *
     * Safe to call multiple times - subsequent calls are no-ops.
     *
     * @note Checks lv_is_initialized() to handle static destruction order safely
     * @note Subjects registered via lv_xml_register_subject() are NOT automatically
     *       unregistered from the XML system. This is safe because panels are
     *       destroyed via StaticPanelRegistry BEFORE lv_deinit() destroys the
     *       XML registry. Do not destroy panels after lv_deinit().
     */
    void deinit_all() {
        if (subjects_.empty()) {
            return;
        }

        // Check if LVGL is still initialized (static destruction order safety)
        if (!lv_is_initialized()) {
            spdlog::warn("[SubjectManager] LVGL not initialized, skipping {} subject deinits",
                         subjects_.size());
            subjects_.clear();
            return;
        }

        spdlog::debug("[SubjectManager] Deinitializing {} subjects", subjects_.size());

        for (auto* subject : subjects_) {
            if (subject) {
                lv_subject_deinit(subject);
            }
        }

        subjects_.clear();
    }

    /**
     * @brief Get count of registered subjects
     * @return Number of registered subjects
     */
    size_t count() const {
        return subjects_.size();
    }

    /**
     * @brief Check if any subjects are registered
     * @return true if at least one subject is registered
     */
    bool has_subjects() const {
        return !subjects_.empty();
    }

  private:
    std::vector<lv_subject_t*> subjects_;
};

/**
 * @brief Helper macro to init, register with XML system, AND register with SubjectManager
 *
 * Combines UI_SUBJECT_INIT_AND_REGISTER_INT with SubjectManager registration.
 *
 * @param subject lv_subject_t member variable
 * @param initial_value Initial integer value
 * @param xml_name String name for XML binding
 * @param manager SubjectManager instance
 */
#define UI_MANAGED_SUBJECT_INT(subject, initial_value, xml_name, manager)                          \
    do {                                                                                           \
        lv_subject_init_int(&(subject), (initial_value));                                          \
        lv_xml_register_subject(nullptr, (xml_name), &(subject));                                  \
        (manager).register_subject(&(subject));                                                    \
        SubjectDebugRegistry::instance().register_subject(                                         \
            &(subject), (xml_name), LV_SUBJECT_TYPE_INT, __FILE__, __LINE__);                      \
    } while (0)

/**
 * @brief Helper macro to init, register with XML system, AND register with SubjectManager
 *
 * Combines UI_SUBJECT_INIT_AND_REGISTER_STRING with SubjectManager registration.
 *
 * @param subject lv_subject_t member variable
 * @param buffer Character buffer for string storage
 * @param initial_value Initial string value
 * @param xml_name String name for XML binding
 * @param manager SubjectManager instance
 */
#define UI_MANAGED_SUBJECT_STRING(subject, buffer, initial_value, xml_name, manager)               \
    do {                                                                                           \
        lv_subject_init_string(&(subject), (buffer), nullptr, sizeof(buffer), (initial_value));    \
        lv_xml_register_subject(nullptr, (xml_name), &(subject));                                  \
        (manager).register_subject(&(subject));                                                    \
        SubjectDebugRegistry::instance().register_subject(                                         \
            &(subject), (xml_name), LV_SUBJECT_TYPE_STRING, __FILE__, __LINE__);                   \
    } while (0)
