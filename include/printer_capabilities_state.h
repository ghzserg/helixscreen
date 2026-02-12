// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "capability_overrides.h"
#include "printer_discovery.h"
#include "subject_managed_panel.h"

#include <lvgl.h>

namespace helix {

/**
 * @brief Manages printer capability subjects for UI feature visibility
 *
 * Tracks hardware capabilities (probe, heater bed, LED, accelerometer, etc.)
 * and feature availability (spoolman, timelapse, firmware retraction, etc.)
 * Provides 17 subjects for reactive UI updates based on printer capabilities.
 * Extracted from PrinterState as part of god class decomposition.
 *
 * @note Capability values are set from hardware discovery on connect, with
 *       user overrides applied from CapabilityOverrides. Some capabilities
 *       (spoolman, purge_line, bed_moves) are updated asynchronously.
 */
class PrinterCapabilitiesState {
  public:
    PrinterCapabilitiesState() = default;
    ~PrinterCapabilitiesState() = default;

    // Non-copyable
    PrinterCapabilitiesState(const PrinterCapabilitiesState&) = delete;
    PrinterCapabilitiesState& operator=(const PrinterCapabilitiesState&) = delete;

    /**
     * @brief Initialize capability subjects
     * @param register_xml If true, register subjects with LVGL XML system
     */
    void init_subjects(bool register_xml = true);

    /**
     * @brief Deinitialize subjects (called by SubjectManager automatically)
     */
    void deinit_subjects();

    // ========================================================================
    // Hardware update methods
    // ========================================================================

    /**
     * @brief Update capabilities from hardware discovery with overrides applied
     *
     * Called from PrinterState::set_hardware_internal() when hardware is detected.
     * Uses effective values from capability_overrides (auto-detect + user overrides).
     *
     * @param hardware Auto-detected hardware capabilities
     * @param overrides Capability override layer with effective values
     */
    void set_hardware(const PrinterDiscovery& hardware, const CapabilityOverrides& overrides);

    /**
     * @brief Set spoolman availability (async update from Moonraker query)
     *
     * Thread-safe: Uses helix::async::invoke() for main-thread execution.
     *
     * @param available True if spoolman is available
     */
    void set_spoolman_available(bool available);

    /**
     * @brief Set purge line capability (from printer type database)
     *
     * Called when printer type is set to update has_purge_line based on
     * printer-specific capabilities.
     *
     * @param has_purge_line True if printer has purge/priming capability
     */
    void set_purge_line(bool has_purge_line);

    /**
     * @brief Set webcam availability (async update from Moonraker query)
     *
     * Thread-safe: Uses helix::async::invoke() for main-thread execution.
     *
     * @param available True if at least one enabled webcam is configured
     */
    void set_webcam_available(bool available);

    /**
     * @brief Set timelapse plugin availability (async update)
     *
     * Thread-safe: Uses helix::async::invoke() for main-thread execution.
     *
     * @param available True if moonraker-timelapse plugin is installed and responding
     */
    void set_timelapse_available(bool available);

    /**
     * @brief Set bed moves on Z axis (from kinematics detection)
     *
     * @param bed_moves True if bed moves on Z (corexy), false if gantry moves (cartesian/delta)
     */
    void set_bed_moves(bool bed_moves);

    /**
     * @brief Set stepper_z position_endstop value (for non-probe printers)
     *
     * Stores the configured position_endstop from stepper_z in Klipper's
     * configfile.settings. Used as the "saved z-offset" reference for
     * endstop-based printers during Z-offset calibration.
     *
     * @param microns position_endstop in microns (e.g., 235000 for 235.0mm)
     */
    void set_stepper_z_endstop_microns(int microns) {
        stepper_z_endstop_microns_ = microns;
    }

    /**
     * @brief Get stepper_z position_endstop value in microns
     *
     * @return position_endstop in microns, or 0 if not set
     */
    int get_stepper_z_endstop_microns() const {
        return stepper_z_endstop_microns_;
    }

    // ========================================================================
    // Subject accessors (17 subjects)
    // ========================================================================

    /// 1 if printer has quad_gantry_level
    lv_subject_t* get_printer_has_qgl_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_qgl_);
    }

    /// 1 if printer has z_tilt_adjust
    lv_subject_t* get_printer_has_z_tilt_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_z_tilt_);
    }

    /// 1 if printer has bed_mesh calibration
    lv_subject_t* get_printer_has_bed_mesh_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_bed_mesh_);
    }

    /// 1 if printer has nozzle clean macro
    lv_subject_t* get_printer_has_nozzle_clean_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_nozzle_clean_);
    }

    /// 1 if printer has probe or bltouch
    lv_subject_t* get_printer_has_probe_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_probe_);
    }

    /// 1 if printer has heated bed
    lv_subject_t* get_printer_has_heater_bed_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_heater_bed_);
    }

    /// 1 if printer has controllable LED
    lv_subject_t* get_printer_has_led_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_led_);
    }

    /// 1 if printer has accelerometer for input shaping
    lv_subject_t* get_printer_has_accelerometer_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_accelerometer_);
    }

    /// 1 if spoolman filament manager is available
    lv_subject_t* get_printer_has_spoolman_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_spoolman_);
    }

    /// 1 if printer has speaker for M300 audio
    lv_subject_t* get_printer_has_speaker_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_speaker_);
    }

    /// 1 if moonraker-timelapse plugin is installed
    lv_subject_t* get_printer_has_timelapse_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_timelapse_);
    }

    /// 1 if printer has purge/priming capability
    lv_subject_t* get_printer_has_purge_line_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_purge_line_);
    }

    /// 1 if printer has firmware retraction (G10/G11)
    lv_subject_t* get_printer_has_firmware_retraction_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_firmware_retraction_);
    }

    /// 1 if bed moves on Z axis, 0 if gantry moves
    lv_subject_t* get_printer_bed_moves_subject() const {
        return const_cast<lv_subject_t*>(&printer_bed_moves_);
    }

    /// 1 if printer has chamber temperature sensor
    lv_subject_t* get_printer_has_chamber_sensor_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_chamber_sensor_);
    }

    /// 1 if printer has screws_tilt_adjust
    lv_subject_t* get_printer_has_screws_tilt_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_screws_tilt_);
    }

    /// 1 if printer has an enabled webcam configured
    lv_subject_t* get_printer_has_webcam_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_webcam_);
    }

    /// 1 if printer has controllable fans beyond part cooling (generic fans, exhaust, etc.)
    lv_subject_t* get_printer_has_extra_fans_subject() const {
        return const_cast<lv_subject_t*>(&printer_has_extra_fans_);
    }

    // ========================================================================
    // Convenience methods
    // ========================================================================

    /**
     * @brief Check if printer has a probe
     * @return true if [probe] or [bltouch] section exists in Klipper config
     */
    bool has_probe() const {
        // Cast away const for lv_subject_get_int which doesn't modify the subject
        return lv_subject_get_int(const_cast<lv_subject_t*>(&printer_has_probe_)) != 0;
    }

  private:
    friend class PrinterCapabilitiesStateTestAccess;

    SubjectManager subjects_;
    bool subjects_initialized_ = false;

    /// stepper_z position_endstop from configfile.settings (microns)
    int stepper_z_endstop_microns_ = 0;

    // Printer capability subjects (all integer: 0=no, 1=yes)
    lv_subject_t printer_has_qgl_{};                 // quad_gantry_level
    lv_subject_t printer_has_z_tilt_{};              // z_tilt_adjust
    lv_subject_t printer_has_bed_mesh_{};            // bed_mesh calibration
    lv_subject_t printer_has_nozzle_clean_{};        // nozzle clean macro
    lv_subject_t printer_has_probe_{};               // probe or bltouch
    lv_subject_t printer_has_heater_bed_{};          // heated bed
    lv_subject_t printer_has_led_{};                 // controllable LED
    lv_subject_t printer_has_accelerometer_{};       // accelerometer for input shaping
    lv_subject_t printer_has_spoolman_{};            // spoolman filament manager
    lv_subject_t printer_has_speaker_{};             // speaker for M300
    lv_subject_t printer_has_timelapse_{};           // moonraker-timelapse plugin
    lv_subject_t printer_has_purge_line_{};          // purge/priming capability
    lv_subject_t printer_has_firmware_retraction_{}; // firmware retraction (G10/G11)
    lv_subject_t printer_bed_moves_{};               // 0=gantry moves on Z, 1=bed moves on Z
    lv_subject_t printer_has_chamber_sensor_{};      // chamber temperature sensor
    lv_subject_t printer_has_screws_tilt_{};         // screws_tilt_adjust
    lv_subject_t printer_has_webcam_{};              // enabled webcam configured
    lv_subject_t printer_has_extra_fans_{};          // extra controllable fans beyond part cooling
};

} // namespace helix
