// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file printer_excluded_objects_state.cpp
 * @brief Excluded objects state management extracted from PrinterState
 *
 * Manages the set of objects excluded from printing via Klipper's EXCLUDE_OBJECT
 * feature. Uses version-based notification since LVGL subjects don't support sets.
 *
 * Extracted from PrinterState as part of god class decomposition.
 */

#include "printer_excluded_objects_state.h"

#include "state/subject_macros.h"

#include <spdlog/spdlog.h>

namespace helix {

void PrinterExcludedObjectsState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        spdlog::debug("[PrinterExcludedObjectsState] Subjects already initialized, skipping");
        return;
    }

    spdlog::trace("[PrinterExcludedObjectsState] Initializing subjects (register_xml={})",
                  register_xml);

    // Initialize version subject to 0 (no changes yet)
    INIT_SUBJECT_INT(excluded_objects_version, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(defined_objects_version, 0, subjects_, register_xml);

    subjects_initialized_ = true;
    spdlog::trace("[PrinterExcludedObjectsState] Subjects initialized successfully");
}

void PrinterExcludedObjectsState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::trace("[PrinterExcludedObjectsState] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void PrinterExcludedObjectsState::set_excluded_objects(
    const std::unordered_set<std::string>& objects) {
    // Only update if the set actually changed
    if (excluded_objects_ != objects) {
        excluded_objects_ = objects;

        // Increment version to notify observers
        int version = lv_subject_get_int(&excluded_objects_version_);
        lv_subject_set_int(&excluded_objects_version_, version + 1);

        spdlog::debug("[PrinterExcludedObjectsState] Excluded objects updated: {} objects "
                      "(version {})",
                      excluded_objects_.size(), version + 1);
    }
}

void PrinterExcludedObjectsState::set_defined_objects(const std::vector<std::string>& objects) {
    // Only update if the list actually changed
    if (defined_objects_ != objects) {
        defined_objects_ = objects;

        // Increment version to notify observers
        int version = lv_subject_get_int(&defined_objects_version_);
        lv_subject_set_int(&defined_objects_version_, version + 1);

        spdlog::debug("[PrinterExcludedObjectsState] Defined objects updated: {} objects "
                      "(version {})",
                      defined_objects_.size(), version + 1);
    }
}

void PrinterExcludedObjectsState::set_current_object(const std::string& name) {
    if (current_object_ != name) {
        current_object_ = name;

        // Bump excluded version to notify overlay observers of current object change
        int version = lv_subject_get_int(&excluded_objects_version_);
        lv_subject_set_int(&excluded_objects_version_, version + 1);

        spdlog::debug("[PrinterExcludedObjectsState] Current object: '{}' (version {})",
                      current_object_, version + 1);
    }
}

} // namespace helix
