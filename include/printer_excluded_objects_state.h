// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "subject_managed_panel.h"

#include <lvgl.h>
#include <string>
#include <unordered_set>
#include <vector>

namespace helix {

/**
 * @brief Manages excluded objects state for Klipper's EXCLUDE_OBJECT feature
 *
 * Tracks which objects have been excluded from the current print job.
 * Uses a version-based notification pattern since LVGL subjects don't
 * natively support set types.
 *
 * Extracted from PrinterState as part of god class decomposition.
 *
 * Usage pattern:
 * 1. Observer subscribes to excluded_objects_version_ subject
 * 2. When notified, observer calls get_excluded_objects() for updated set
 *
 * @note set_excluded_objects() only increments version if set actually changed
 */
class PrinterExcludedObjectsState {
  public:
    PrinterExcludedObjectsState() = default;
    ~PrinterExcludedObjectsState() = default;

    // Non-copyable
    PrinterExcludedObjectsState(const PrinterExcludedObjectsState&) = delete;
    PrinterExcludedObjectsState& operator=(const PrinterExcludedObjectsState&) = delete;

    /**
     * @brief Initialize excluded objects subjects
     * @param register_xml If true, register subjects with LVGL XML system
     */
    void init_subjects(bool register_xml = true);

    /**
     * @brief Deinitialize subjects (called by SubjectManager automatically)
     */
    void deinit_subjects();

    // ========================================================================
    // Setters
    // ========================================================================

    /**
     * @brief Update excluded objects from Moonraker status update
     *
     * Compares new set with current set and only updates if different.
     * Increments version subject to notify observers when set changes.
     *
     * @param objects Set of object names that are currently excluded
     */
    void set_excluded_objects(const std::unordered_set<std::string>& objects);

    /**
     * @brief Update defined objects from Klipper's exclude_object status
     *
     * Sets the full list of objects defined in the current print.
     * Only bumps version subject if the list actually changed.
     *
     * @param objects Vector of all object names from Klipper
     */
    void set_defined_objects(const std::vector<std::string>& objects);

    /**
     * @brief Update currently printing object name
     *
     * @param name Name of the object currently being printed, or empty if none
     */
    void set_current_object(const std::string& name);

    // ========================================================================
    // Subject accessors
    // ========================================================================

    /**
     * @brief Get excluded objects version subject
     *
     * This subject is incremented whenever the excluded objects list changes.
     * Observers should watch this subject and call get_excluded_objects() to
     * get the updated list when notified.
     *
     * @return Subject pointer (integer, incremented on each change)
     */
    lv_subject_t* get_excluded_objects_version_subject() {
        return &excluded_objects_version_;
    }

    /**
     * @brief Get defined objects version subject
     *
     * This subject is incremented whenever the defined objects list changes.
     * Observers should watch this subject and call get_defined_objects() to
     * get the updated list when notified.
     *
     * @return Subject pointer (integer, incremented on each change)
     */
    lv_subject_t* get_defined_objects_version_subject() {
        return &defined_objects_version_;
    }

    // ========================================================================
    // Query methods
    // ========================================================================

    /**
     * @brief Get the current set of excluded objects
     *
     * Returns object names that have been excluded from printing via Klipper's
     * EXCLUDE_OBJECT feature. Updated from Moonraker notify_status_update.
     *
     * @return Const reference to the set of excluded object names
     */
    const std::unordered_set<std::string>& get_excluded_objects() const {
        return excluded_objects_;
    }

    /**
     * @brief Get the list of all defined objects in the current print
     *
     * @return Const reference to the vector of defined object names
     */
    const std::vector<std::string>& get_defined_objects() const {
        return defined_objects_;
    }

    /**
     * @brief Get the name of the currently printing object
     *
     * @return Const reference to current object name, or empty string if none
     */
    const std::string& get_current_object() const {
        return current_object_;
    }

    /**
     * @brief Check if any objects are defined for exclude_object
     *
     * @return true if the print has defined objects available for exclusion
     */
    bool has_objects() const {
        return !defined_objects_.empty();
    }

  private:
    friend class PrinterExcludedObjectsStateTestAccess;

    SubjectManager subjects_;
    bool subjects_initialized_ = false;

    // Excluded objects version subject (incremented when excluded_objects_ changes)
    lv_subject_t excluded_objects_version_{};

    // Set of excluded object names (NOT a subject - sets aren't natively supported)
    std::unordered_set<std::string> excluded_objects_;

    // All defined object names from Klipper's exclude_object status
    std::vector<std::string> defined_objects_;

    // Currently printing object name (empty if none)
    std::string current_object_;

    // Version subject for defined objects list (incremented when list changes)
    lv_subject_t defined_objects_version_{};
};

} // namespace helix
