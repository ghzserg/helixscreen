// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "printer_capabilities_state.h"
#include "subject_managed_panel.h"

#include <lvgl.h>

namespace helix {

/**
 * @brief Manages composite visibility subjects for G-code modification options
 *
 * These subjects combine helix_plugin_installed with individual printer capabilities
 * to control visibility of pre-print option rows in the UI. An option is shown only
 * when BOTH: plugin is installed AND printer has the capability.
 *
 * Composite visibility subjects (5 total):
 * - can_show_bed_mesh: helix_plugin_installed && printer_has_bed_mesh
 * - can_show_qgl: helix_plugin_installed && printer_has_qgl
 * - can_show_z_tilt: helix_plugin_installed && printer_has_z_tilt
 * - can_show_nozzle_clean: helix_plugin_installed && printer_has_nozzle_clean
 * - can_show_purge_line: helix_plugin_installed && printer_has_purge_line
 *
 * Extracted from PrinterState as part of god class decomposition.
 *
 * @note Update triggers:
 *   - Hardware discovery (set_hardware_internal)
 *   - Plugin status changes (set_helix_plugin_installed)
 *   - Printer type changes (set_printer_type_internal)
 */
class PrinterCompositeVisibilityState {
  public:
    PrinterCompositeVisibilityState() = default;
    ~PrinterCompositeVisibilityState() = default;

    // Non-copyable
    PrinterCompositeVisibilityState(const PrinterCompositeVisibilityState&) = delete;
    PrinterCompositeVisibilityState& operator=(const PrinterCompositeVisibilityState&) = delete;

    /**
     * @brief Initialize composite visibility subjects
     * @param register_xml If true, register subjects with LVGL XML system
     */
    void init_subjects(bool register_xml = true);

    /**
     * @brief Deinitialize subjects (called by SubjectManager automatically)
     */
    void deinit_subjects();

    // ========================================================================
    // Update method
    // ========================================================================

    /**
     * @brief Recalculate all composite visibility subjects
     *
     * Computes can_show_X = plugin_installed && printer_has_X for all five
     * composite subjects. Only updates subjects when the computed value differs
     * from the current value to avoid spurious observer notifications.
     *
     * Called by PrinterState when:
     * - Hardware is discovered (set_hardware_internal)
     * - Plugin status changes (set_helix_plugin_installed)
     * - Printer type changes (set_printer_type_internal)
     *
     * @param plugin_installed True if HelixPrint plugin is installed
     * @param capabilities Reference to capabilities state for has_* queries
     */
    void update_visibility(bool plugin_installed, const PrinterCapabilitiesState& capabilities);

    // ========================================================================
    // Subject accessors
    // ========================================================================

    /**
     * @brief Get visibility subject for bed mesh row
     * @return 1 when bed mesh option should be visible, 0 otherwise
     */
    lv_subject_t* get_can_show_bed_mesh_subject() {
        return &can_show_bed_mesh_;
    }

    /**
     * @brief Get visibility subject for QGL row
     * @return 1 when QGL option should be visible, 0 otherwise
     */
    lv_subject_t* get_can_show_qgl_subject() {
        return &can_show_qgl_;
    }

    /**
     * @brief Get visibility subject for Z-tilt row
     * @return 1 when Z-tilt option should be visible, 0 otherwise
     */
    lv_subject_t* get_can_show_z_tilt_subject() {
        return &can_show_z_tilt_;
    }

    /**
     * @brief Get visibility subject for nozzle clean row
     * @return 1 when nozzle clean option should be visible, 0 otherwise
     */
    lv_subject_t* get_can_show_nozzle_clean_subject() {
        return &can_show_nozzle_clean_;
    }

    /**
     * @brief Get visibility subject for purge line row
     * @return 1 when purge line option should be visible, 0 otherwise
     */
    lv_subject_t* get_can_show_purge_line_subject() {
        return &can_show_purge_line_;
    }

  private:
    friend class PrinterCompositeVisibilityStateTestAccess;

    SubjectManager subjects_;
    bool subjects_initialized_ = false;

    // Composite visibility subjects (all integer: 0=hidden, 1=visible)
    lv_subject_t can_show_bed_mesh_{};     // helix_plugin_installed && printer_has_bed_mesh
    lv_subject_t can_show_qgl_{};          // helix_plugin_installed && printer_has_qgl
    lv_subject_t can_show_z_tilt_{};       // helix_plugin_installed && printer_has_z_tilt
    lv_subject_t can_show_nozzle_clean_{}; // helix_plugin_installed && printer_has_nozzle_clean
    lv_subject_t can_show_purge_line_{};   // helix_plugin_installed && printer_has_purge_line
};

} // namespace helix
