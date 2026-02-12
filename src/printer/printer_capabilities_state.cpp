// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file printer_capabilities_state.cpp
 * @brief Printer capabilities state management extracted from PrinterState
 *
 * Manages capability subjects that control UI feature visibility based on
 * hardware detection and user overrides. Extracted from PrinterState as
 * part of god class decomposition.
 */

#include "printer_capabilities_state.h"

#include "async_helpers.h"
#include "config.h"
#include "state/subject_macros.h"

#include <spdlog/spdlog.h>

namespace helix {

void PrinterCapabilitiesState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        spdlog::debug("[PrinterCapabilitiesState] Subjects already initialized, skipping");
        return;
    }

    spdlog::trace("[PrinterCapabilitiesState] Initializing subjects (register_xml={})",
                  register_xml);

    // Printer capability subjects (all default to 0=not available)
    INIT_SUBJECT_INT(printer_has_qgl, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(printer_has_z_tilt, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(printer_has_bed_mesh, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(printer_has_nozzle_clean, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(printer_has_probe, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(printer_has_heater_bed, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(printer_has_led, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(printer_has_accelerometer, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(printer_has_spoolman, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(printer_has_speaker, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(printer_has_timelapse, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(printer_has_purge_line, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(printer_has_firmware_retraction, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(printer_bed_moves, 0, subjects_, register_xml); // 0=gantry moves, 1=bed moves
    INIT_SUBJECT_INT(printer_has_chamber_sensor, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(printer_has_screws_tilt, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(printer_has_webcam, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(printer_has_extra_fans, 0, subjects_, register_xml);

    subjects_initialized_ = true;
    spdlog::trace("[PrinterCapabilitiesState] Subjects initialized successfully");
}

void PrinterCapabilitiesState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[PrinterCapabilitiesState] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void PrinterCapabilitiesState::set_hardware(const PrinterDiscovery& hardware,
                                            const CapabilityOverrides& overrides) {
    // Update subjects using effective values (auto-detect + user overrides)
    // This allows users to force-enable features that weren't detected
    // (e.g., heat soak macro without chamber heater) or force-disable
    // features they don't want to see in the UI.
    lv_subject_set_int(&printer_has_qgl_, overrides.has_qgl() ? 1 : 0);
    lv_subject_set_int(&printer_has_z_tilt_, overrides.has_z_tilt() ? 1 : 0);
    lv_subject_set_int(&printer_has_bed_mesh_, overrides.has_bed_mesh() ? 1 : 0);
    lv_subject_set_int(&printer_has_nozzle_clean_, overrides.has_nozzle_clean() ? 1 : 0);

    // Hardware capabilities (no user override support yet - set directly from detection)
    lv_subject_set_int(&printer_has_probe_, hardware.has_probe() ? 1 : 0);
    lv_subject_set_int(&printer_has_heater_bed_, hardware.has_heater_bed() ? 1 : 0);
    lv_subject_set_int(&printer_has_led_, hardware.has_led() ? 1 : 0);
    lv_subject_set_int(&printer_has_accelerometer_, hardware.has_accelerometer() ? 1 : 0);

    // Speaker capability (for M300 audio feedback)
    lv_subject_set_int(&printer_has_speaker_, hardware.has_speaker() ? 1 : 0);

    // Timelapse capability (Moonraker-Timelapse plugin) — gated behind beta features
    {
        bool timelapse_detected = hardware.has_timelapse();
        bool beta = Config::get_instance() && Config::get_instance()->is_beta_features_enabled();
        lv_subject_set_int(&printer_has_timelapse_, (timelapse_detected && beta) ? 1 : 0);
        if (timelapse_detected && !beta) {
            spdlog::debug(
                "[PrinterCapabilitiesState] Timelapse detected but beta features disabled");
        }
    }

    // Firmware retraction capability (for G10/G11 retraction settings)
    lv_subject_set_int(&printer_has_firmware_retraction_,
                       hardware.has_firmware_retraction() ? 1 : 0);

    // Chamber temperature sensor capability
    lv_subject_set_int(&printer_has_chamber_sensor_, hardware.has_chamber_sensor() ? 1 : 0);

    // Screws tilt adjust capability
    lv_subject_set_int(&printer_has_screws_tilt_, hardware.has_screws_tilt() ? 1 : 0);

    // Spoolman requires async check - default to 0, updated separately via set_spoolman_available()

    spdlog::debug("[PrinterCapabilitiesState] Hardware set: probe={}, heater_bed={}, LED={}, "
                  "accelerometer={}, speaker={}, timelapse={}, fw_retraction={}, chamber_sensor={}",
                  hardware.has_probe(), hardware.has_heater_bed(), hardware.has_led(),
                  hardware.has_accelerometer(), hardware.has_speaker(), hardware.has_timelapse(),
                  hardware.has_firmware_retraction(), hardware.has_chamber_sensor());
    spdlog::debug("[PrinterCapabilitiesState] Hardware set (with overrides): {}",
                  overrides.summary());
}

void PrinterCapabilitiesState::set_spoolman_available(bool available) {
    // Thread-safe: Use helix::async::invoke to update LVGL subject from any thread
    helix::async::invoke([this, available]() {
        lv_subject_set_int(&printer_has_spoolman_, available ? 1 : 0);
        spdlog::debug("[PrinterCapabilitiesState] Spoolman availability set: {}", available);
    });
}

void PrinterCapabilitiesState::set_webcam_available(bool available) {
    // Thread-safe: Use helix::async::invoke to update LVGL subject from any thread
    helix::async::invoke([this, available]() {
        lv_subject_set_int(&printer_has_webcam_, available ? 1 : 0);
        spdlog::debug("[PrinterCapabilitiesState] Webcam availability set: {}", available);
    });
}

void PrinterCapabilitiesState::set_timelapse_available(bool available) {
    // Thread-safe: Use helix::async::invoke to update LVGL subject from any thread
    helix::async::invoke([this, available]() {
        // Gate behind beta features — timelapse is still in beta
        bool beta = Config::get_instance() && Config::get_instance()->is_beta_features_enabled();
        bool effective = available && beta;
        lv_subject_set_int(&printer_has_timelapse_, effective ? 1 : 0);
        spdlog::debug("[PrinterCapabilitiesState] Timelapse availability set: {} (beta={})",
                      available, beta);
    });
}

void PrinterCapabilitiesState::set_purge_line(bool has_purge_line) {
    lv_subject_set_int(&printer_has_purge_line_, has_purge_line ? 1 : 0);
    spdlog::debug("[PrinterCapabilitiesState] Purge line capability set: {}", has_purge_line);
}

void PrinterCapabilitiesState::set_bed_moves(bool bed_moves) {
    int new_value = bed_moves ? 1 : 0;
    // Only log when value actually changes (this gets called frequently from status updates)
    if (lv_subject_get_int(&printer_bed_moves_) != new_value) {
        lv_subject_set_int(&printer_bed_moves_, new_value);
        spdlog::info("[PrinterCapabilitiesState] Bed moves on Z: {}", bed_moves);
    }
}

} // namespace helix
