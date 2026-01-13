// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file printer_composite_visibility_state.cpp
 * @brief Composite visibility state management extracted from PrinterState
 *
 * Manages derived visibility subjects that combine plugin installation status
 * with printer capabilities. Used to control visibility of pre-print G-code
 * modification options in the UI.
 *
 * Extracted from PrinterState as part of god class decomposition.
 */

#include "printer_composite_visibility_state.h"

#include <spdlog/spdlog.h>

namespace helix {

void PrinterCompositeVisibilityState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        spdlog::debug("[PrinterCompositeVisibilityState] Subjects already initialized, skipping");
        return;
    }

    spdlog::debug("[PrinterCompositeVisibilityState] Initializing subjects (register_xml={})",
                  register_xml);

    // Composite visibility subjects - all initialize to 0 (hidden by default)
    // These are derived from helix_plugin_installed AND printer_has_* subjects
    lv_subject_init_int(&can_show_bed_mesh_, 0);
    lv_subject_init_int(&can_show_qgl_, 0);
    lv_subject_init_int(&can_show_z_tilt_, 0);
    lv_subject_init_int(&can_show_nozzle_clean_, 0);
    lv_subject_init_int(&can_show_purge_line_, 0);

    // Register with SubjectManager for automatic cleanup
    subjects_.register_subject(&can_show_bed_mesh_);
    subjects_.register_subject(&can_show_qgl_);
    subjects_.register_subject(&can_show_z_tilt_);
    subjects_.register_subject(&can_show_nozzle_clean_);
    subjects_.register_subject(&can_show_purge_line_);

    // Register with LVGL XML system for XML bindings
    if (register_xml) {
        spdlog::debug("[PrinterCompositeVisibilityState] Registering subjects with XML system");
        lv_xml_register_subject(NULL, "can_show_bed_mesh", &can_show_bed_mesh_);
        lv_xml_register_subject(NULL, "can_show_qgl", &can_show_qgl_);
        lv_xml_register_subject(NULL, "can_show_z_tilt", &can_show_z_tilt_);
        lv_xml_register_subject(NULL, "can_show_nozzle_clean", &can_show_nozzle_clean_);
        lv_xml_register_subject(NULL, "can_show_purge_line", &can_show_purge_line_);
    } else {
        spdlog::debug("[PrinterCompositeVisibilityState] Skipping XML registration (tests mode)");
    }

    subjects_initialized_ = true;
    spdlog::debug("[PrinterCompositeVisibilityState] Subjects initialized successfully");
}

void PrinterCompositeVisibilityState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[PrinterCompositeVisibilityState] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void PrinterCompositeVisibilityState::reset_for_testing() {
    if (!subjects_initialized_) {
        spdlog::debug("[PrinterCompositeVisibilityState] reset_for_testing: subjects not "
                      "initialized, nothing to reset");
        return;
    }

    spdlog::info("[PrinterCompositeVisibilityState] reset_for_testing: Deinitializing subjects to "
                 "clear observers");

    // Use SubjectManager for automatic subject cleanup
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void PrinterCompositeVisibilityState::update_visibility(
    bool plugin_installed, const PrinterCapabilitiesState& capabilities) {
    // Recalculate composite subjects: can_show_X = helix_plugin_installed && printer_has_X
    // These control visibility of pre-print G-code modification options in the UI
    // Only update subjects when the value changes to avoid spurious observer notifications

    auto update_if_changed = [](lv_subject_t* subject, int new_value) {
        if (lv_subject_get_int(subject) != new_value) {
            lv_subject_set_int(subject, new_value);
        }
    };

    // Read capability values from capabilities state and combine with plugin status
    update_if_changed(
        &can_show_bed_mesh_,
        (plugin_installed && lv_subject_get_int(capabilities.get_printer_has_bed_mesh_subject()))
            ? 1
            : 0);

    update_if_changed(
        &can_show_qgl_,
        (plugin_installed && lv_subject_get_int(capabilities.get_printer_has_qgl_subject())) ? 1
                                                                                             : 0);

    update_if_changed(
        &can_show_z_tilt_,
        (plugin_installed && lv_subject_get_int(capabilities.get_printer_has_z_tilt_subject()))
            ? 1
            : 0);

    update_if_changed(&can_show_nozzle_clean_,
                      (plugin_installed &&
                       lv_subject_get_int(capabilities.get_printer_has_nozzle_clean_subject()))
                          ? 1
                          : 0);

    update_if_changed(
        &can_show_purge_line_,
        (plugin_installed && lv_subject_get_int(capabilities.get_printer_has_purge_line_subject()))
            ? 1
            : 0);

    spdlog::debug("[PrinterCompositeVisibilityState] Visibility updated: bed_mesh={}, qgl={}, "
                  "z_tilt={}, nozzle_clean={}, purge_line={} (plugin={})",
                  lv_subject_get_int(&can_show_bed_mesh_), lv_subject_get_int(&can_show_qgl_),
                  lv_subject_get_int(&can_show_z_tilt_),
                  lv_subject_get_int(&can_show_nozzle_clean_),
                  lv_subject_get_int(&can_show_purge_line_), plugin_installed);
}

} // namespace helix
