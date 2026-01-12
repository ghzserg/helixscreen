// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file printer_state.cpp
 * @brief Reactive printer state model with LVGL subjects for all printer data
 *
 * @pattern Singleton with set_*() -> set_*_internal() for thread-safe updates
 * @threading Public setters called from WebSocket; internal setters run on main thread
 * @gotchas Static string buffers; init subjects before XML; temps in centidegrees
 *
 * @see moonraker_client.cpp, ui_update_queue.h
 */

#include "printer_state.h"

#include "ui_update_queue.h"

#include "async_helpers.h"
#include "capability_overrides.h"
#include "device_display_name.h"
#include "filament_sensor_manager.h"
#include "hardware_validator.h"
#include "lvgl.h"
#include "lvgl/src/display/lv_display_private.h" // For rendering_in_progress check
#include "lvgl_debug_invalidate.h"
#include "moonraker_client.h" // For ConnectionState enum
#include "runtime_config.h"
#include "unit_conversions.h"

#include <algorithm>
#include <cctype>
#include <cstring>

// ============================================================================
// PrintJobState Free Functions
// ============================================================================

PrintJobState parse_print_job_state(const char* state_str) {
    if (!state_str) {
        return PrintJobState::STANDBY;
    }

    if (std::strcmp(state_str, "standby") == 0) {
        return PrintJobState::STANDBY;
    } else if (std::strcmp(state_str, "printing") == 0) {
        return PrintJobState::PRINTING;
    } else if (std::strcmp(state_str, "paused") == 0) {
        return PrintJobState::PAUSED;
    } else if (std::strcmp(state_str, "complete") == 0) {
        return PrintJobState::COMPLETE;
    } else if (std::strcmp(state_str, "cancelled") == 0) {
        return PrintJobState::CANCELLED;
    } else if (std::strcmp(state_str, "error") == 0) {
        return PrintJobState::ERROR;
    }

    // Unknown state defaults to STANDBY
    spdlog::warn("[PrinterState] Unknown print state string: '{}', defaulting to STANDBY",
                 state_str);
    return PrintJobState::STANDBY;
}

const char* print_job_state_to_string(PrintJobState state) {
    switch (state) {
    case PrintJobState::STANDBY:
        return "Standby";
    case PrintJobState::PRINTING:
        return "Printing";
    case PrintJobState::PAUSED:
        return "Paused";
    case PrintJobState::COMPLETE:
        return "Complete";
    case PrintJobState::CANCELLED:
        return "Cancelled";
    case PrintJobState::ERROR:
        return "Error";
    default:
        return "Unknown";
    }
}

// ============================================================================
// PrinterState Implementation
// ============================================================================

PrinterState::PrinterState() {
    // Initialize string buffers
    std::memset(print_filename_buf_, 0, sizeof(print_filename_buf_));
    std::memset(print_state_buf_, 0, sizeof(print_state_buf_));
    std::memset(homed_axes_buf_, 0, sizeof(homed_axes_buf_));
    std::memset(printer_connection_message_buf_, 0, sizeof(printer_connection_message_buf_));
    std::memset(klipper_version_buf_, 0, sizeof(klipper_version_buf_));
    std::memset(moonraker_version_buf_, 0, sizeof(moonraker_version_buf_));
    std::memset(print_start_message_buf_, 0, sizeof(print_start_message_buf_));

    // Set default values
    std::strcpy(print_state_buf_, "standby");
    std::strcpy(homed_axes_buf_, "");
    std::strcpy(printer_connection_message_buf_, "Disconnected");
    std::strcpy(klipper_version_buf_, "—");
    std::strcpy(moonraker_version_buf_, "—");

    // Load user-configured capability overrides from helixconfig.json
    capability_overrides_.load_from_config();
}

PrinterState::~PrinterState() {}

void PrinterState::reset_for_testing() {
    if (!subjects_initialized_) {
        spdlog::debug(
            "[PrinterState] reset_for_testing: subjects not initialized, nothing to reset");
        return; // Nothing to reset
    }

    spdlog::info("[PrinterState] reset_for_testing: Deinitializing subjects to clear observers");

    // Reset temperature state component
    temperature_state_.reset_for_testing();

    // Use SubjectManager for automatic subject cleanup
    subjects_.deinit_all();

    // Deinit per-fan speed subjects (unique_ptr handles memory, we just need to deinit)
    // These are dynamically created so not tracked by SubjectManager
    for (auto& [name, subject_ptr] : fan_speed_subjects_) {
        if (subject_ptr) {
            lv_subject_deinit(subject_ptr.get());
        }
    }
    fan_speed_subjects_.clear();

    // Reset printer type and capabilities to initial empty state
    printer_type_.clear();
    print_start_capabilities_ = PrintStartCapabilities{};

    subjects_initialized_ = false;
}

void PrinterState::init_subjects(bool register_xml) {
    // Detect LVGL reinitialization - display pointer changes when lv_init() called again
    // This happens in test suites where each test reinitializes LVGL but the PrinterState
    // singleton persists. Without this check, subjects would point to freed memory.
    lv_display_t* current_display = lv_display_get_default();

    if (subjects_initialized_) {
        if (current_display != cached_display_) {
            // LVGL was reinitialized - our subjects are now invalid
            spdlog::warn("[PrinterState] LVGL reinitialized (display changed), resetting subjects");
            reset_for_testing();
        } else {
            spdlog::debug("[PrinterState] Subjects already initialized, skipping");
            return;
        }
    }

    cached_display_ = current_display;

    spdlog::debug("[PrinterState] Initializing subjects (register_xml={})", register_xml);

    // Initialize temperature state component (extruder and bed temperatures)
    temperature_state_.init_subjects(register_xml);

    // Print progress subjects
    lv_subject_init_int(&print_progress_, 0);
    lv_subject_init_string(&print_filename_, print_filename_buf_, nullptr,
                           sizeof(print_filename_buf_), "");
    lv_subject_init_string(&print_state_, print_state_buf_, nullptr, sizeof(print_state_buf_),
                           "standby");
    lv_subject_init_int(&print_state_enum_, static_cast<int>(PrintJobState::STANDBY));
    lv_subject_init_int(&print_outcome_, static_cast<int>(PrintOutcome::NONE));
    lv_subject_init_int(&print_active_, 0);        // 0 when idle, 1 when PRINTING/PAUSED
    lv_subject_init_int(&print_show_progress_, 0); // 1 when active AND not in start phase
    lv_subject_init_string(&print_display_filename_, print_display_filename_buf_, nullptr,
                           sizeof(print_display_filename_buf_), "");
    lv_subject_init_string(&print_thumbnail_path_, print_thumbnail_path_buf_, nullptr,
                           sizeof(print_thumbnail_path_buf_), "");

    // Layer tracking subjects (from Moonraker print_stats.info)
    lv_subject_init_int(&print_layer_current_, 0);
    lv_subject_init_int(&print_layer_total_, 0);

    // Print time tracking subjects (in seconds)
    lv_subject_init_int(&print_duration_, 0);
    lv_subject_init_int(&print_time_left_, 0);

    // Print start progress subjects (for PRINT_START macro tracking)
    lv_subject_init_int(&print_start_phase_, static_cast<int>(PrintStartPhase::IDLE));
    lv_subject_init_string(&print_start_message_, print_start_message_buf_, nullptr,
                           sizeof(print_start_message_buf_), "");
    lv_subject_init_int(&print_start_progress_, 0);

    // Print workflow in-progress subject (1 while preparing/starting, 0 otherwise)
    lv_subject_init_int(&print_in_progress_, 0);

    // Motion subjects
    lv_subject_init_int(&position_x_, 0);
    lv_subject_init_int(&position_y_, 0);
    lv_subject_init_int(&position_z_, 0);
    lv_subject_init_string(&homed_axes_, homed_axes_buf_, nullptr, sizeof(homed_axes_buf_), "");

    // Speed/Flow subjects (percentages)
    lv_subject_init_int(&speed_factor_, 100);
    lv_subject_init_int(&flow_factor_, 100);
    lv_subject_init_int(&gcode_z_offset_, 0);         // Z-offset in microns from homing_origin[2]
    lv_subject_init_int(&pending_z_offset_delta_, 0); // Accumulated adjustment during print
    lv_subject_init_int(&fan_speed_, 0);
    lv_subject_init_int(&fans_version_, 0); // Multi-fan version for UI updates

    // Printer connection state subjects (Moonraker WebSocket)
    lv_subject_init_int(&printer_connection_state_, 0); // 0 = disconnected
    lv_subject_init_string(&printer_connection_message_, printer_connection_message_buf_, nullptr,
                           sizeof(printer_connection_message_buf_), "Disconnected");

    // Network connectivity subject (WiFi/Ethernet)
    // TODO: Get actual network status from EthernetManager/WiFiManager
    lv_subject_init_int(&network_status_,
                        2); // 0=DISCONNECTED, 1=CONNECTING, 2=CONNECTED (mock mode default)

    // Klipper firmware state subject (default to READY)
    lv_subject_init_int(&klippy_state_, static_cast<int>(KlippyState::READY));

    // Combined nav button enabled subject (connected AND klippy ready)
    // Starts disabled (0) - will be updated when connection/klippy state changes
    lv_subject_init_int(&nav_buttons_enabled_, 0);

    // LED state subject (0=off, 1=on, derived from LED color data)
    lv_subject_init_int(&led_state_, 0);

    // LED RGBW channel subjects (0-255 range)
    lv_subject_init_int(&led_r_, 0);
    lv_subject_init_int(&led_g_, 0);
    lv_subject_init_int(&led_b_, 0);
    lv_subject_init_int(&led_w_, 0);
    lv_subject_init_int(&led_brightness_, 0);

    // Excluded objects version subject (incremented when excluded_objects_ changes)
    lv_subject_init_int(&excluded_objects_version_, 0);

    // Printer capability subjects (all default to 0=not available)
    lv_subject_init_int(&printer_has_qgl_, 0);
    lv_subject_init_int(&printer_has_z_tilt_, 0);
    lv_subject_init_int(&printer_has_bed_mesh_, 0);
    lv_subject_init_int(&printer_has_nozzle_clean_, 0);
    lv_subject_init_int(&printer_has_probe_, 0);
    lv_subject_init_int(&printer_has_heater_bed_, 0);
    lv_subject_init_int(&printer_has_led_, 0);
    lv_subject_init_int(&printer_has_accelerometer_, 0);
    lv_subject_init_int(&printer_has_spoolman_, 0);
    lv_subject_init_int(&printer_has_speaker_, 0);
    lv_subject_init_int(&printer_has_timelapse_, 0);
    lv_subject_init_int(&printer_has_purge_line_, 0);
    lv_subject_init_int(&helix_plugin_installed_, -1); // -1=unknown, 0=not installed, 1=installed
    lv_subject_init_int(&phase_tracking_enabled_, -1); // -1=unknown, 0=disabled, 1=enabled
    lv_subject_init_int(&printer_has_firmware_retraction_, 0);
    lv_subject_init_int(&printer_bed_moves_, 0); // 0=gantry moves, 1=bed moves (cartesian)

    // Composite subjects for G-code modification option visibility
    // These are derived from helix_plugin_installed AND printer_has_* subjects
    lv_subject_init_int(&can_show_bed_mesh_, 0);
    lv_subject_init_int(&can_show_qgl_, 0);
    lv_subject_init_int(&can_show_z_tilt_, 0);
    lv_subject_init_int(&can_show_nozzle_clean_, 0);
    lv_subject_init_int(&can_show_purge_line_, 0);

    // Hardware validation subjects (for Hardware Health section in Settings)
    lv_subject_init_int(&hardware_has_issues_, 0);
    lv_subject_init_int(&hardware_issue_count_, 0);
    lv_subject_init_int(&hardware_max_severity_, 0);
    lv_subject_init_int(&hardware_validation_version_, 0);
    lv_subject_init_int(&hardware_critical_count_, 0);
    lv_subject_init_int(&hardware_warning_count_, 0);
    lv_subject_init_int(&hardware_info_count_, 0);
    lv_subject_init_int(&hardware_session_count_, 0);
    lv_subject_init_string(&hardware_status_title_, hardware_status_title_buf_, nullptr,
                           sizeof(hardware_status_title_buf_), "Healthy");
    lv_subject_init_string(&hardware_status_detail_, hardware_status_detail_buf_, nullptr,
                           sizeof(hardware_status_detail_buf_), "");
    lv_subject_init_string(&hardware_issues_label_, hardware_issues_label_buf_, nullptr,
                           sizeof(hardware_issues_label_buf_), "No Hardware Issues");

    // Firmware retraction settings (defaults: disabled)
    lv_subject_init_int(&retract_length_, 0);         // 0 = disabled
    lv_subject_init_int(&retract_speed_, 20);         // 20 mm/s default
    lv_subject_init_int(&unretract_extra_length_, 0); // 0mm extra
    lv_subject_init_int(&unretract_speed_, 10);       // 10 mm/s default

    // Manual probe subjects (for Z-offset calibration)
    lv_subject_init_int(&manual_probe_active_, 0);     // 0=inactive, 1=active
    lv_subject_init_int(&manual_probe_z_position_, 0); // Z position in microns

    // Motor enabled state (from idle_timeout.state - defaults to enabled/Ready)
    lv_subject_init_int(&motors_enabled_, 1); // 1=enabled (Ready/Printing), 0=disabled (Idle)

    // Version subjects (for About section)
    lv_subject_init_string(&klipper_version_, klipper_version_buf_, nullptr,
                           sizeof(klipper_version_buf_), "—");
    lv_subject_init_string(&moonraker_version_, moonraker_version_buf_, nullptr,
                           sizeof(moonraker_version_buf_), "—");

    // Register all subjects with SubjectManager for automatic cleanup
    // Note: Temperature subjects are managed by temperature_state_ component
    // Print progress subjects
    subjects_.register_subject(&print_progress_);
    subjects_.register_subject(&print_filename_);
    subjects_.register_subject(&print_state_);
    subjects_.register_subject(&print_state_enum_);
    subjects_.register_subject(&print_outcome_);
    subjects_.register_subject(&print_active_);
    subjects_.register_subject(&print_show_progress_);
    subjects_.register_subject(&print_display_filename_);
    subjects_.register_subject(&print_thumbnail_path_);
    // Layer tracking subjects
    subjects_.register_subject(&print_layer_current_);
    subjects_.register_subject(&print_layer_total_);
    // Print time subjects
    subjects_.register_subject(&print_duration_);
    subjects_.register_subject(&print_time_left_);
    // Print start progress subjects
    subjects_.register_subject(&print_start_phase_);
    subjects_.register_subject(&print_start_message_);
    subjects_.register_subject(&print_start_progress_);
    subjects_.register_subject(&print_in_progress_);
    // Motion subjects
    subjects_.register_subject(&position_x_);
    subjects_.register_subject(&position_y_);
    subjects_.register_subject(&position_z_);
    subjects_.register_subject(&homed_axes_);
    // Speed/Flow/Z-offset subjects
    subjects_.register_subject(&speed_factor_);
    subjects_.register_subject(&flow_factor_);
    subjects_.register_subject(&gcode_z_offset_);
    subjects_.register_subject(&pending_z_offset_delta_);
    subjects_.register_subject(&fan_speed_);
    subjects_.register_subject(&fans_version_);
    // Printer connection subjects
    subjects_.register_subject(&printer_connection_state_);
    subjects_.register_subject(&printer_connection_message_);
    subjects_.register_subject(&network_status_);
    subjects_.register_subject(&klippy_state_);
    subjects_.register_subject(&nav_buttons_enabled_);
    // LED subjects
    subjects_.register_subject(&led_state_);
    subjects_.register_subject(&led_r_);
    subjects_.register_subject(&led_g_);
    subjects_.register_subject(&led_b_);
    subjects_.register_subject(&led_w_);
    subjects_.register_subject(&led_brightness_);
    // Excluded objects
    subjects_.register_subject(&excluded_objects_version_);
    // Printer capability subjects
    subjects_.register_subject(&printer_has_qgl_);
    subjects_.register_subject(&printer_has_z_tilt_);
    subjects_.register_subject(&printer_has_bed_mesh_);
    subjects_.register_subject(&printer_has_nozzle_clean_);
    subjects_.register_subject(&printer_has_probe_);
    subjects_.register_subject(&printer_has_heater_bed_);
    subjects_.register_subject(&printer_has_led_);
    subjects_.register_subject(&printer_has_accelerometer_);
    subjects_.register_subject(&printer_has_spoolman_);
    subjects_.register_subject(&printer_has_speaker_);
    subjects_.register_subject(&printer_has_timelapse_);
    subjects_.register_subject(&printer_has_purge_line_);
    subjects_.register_subject(&helix_plugin_installed_);
    subjects_.register_subject(&phase_tracking_enabled_);
    subjects_.register_subject(&printer_has_firmware_retraction_);
    subjects_.register_subject(&printer_bed_moves_);
    // Composite subjects for G-code modification visibility
    subjects_.register_subject(&can_show_bed_mesh_);
    subjects_.register_subject(&can_show_qgl_);
    subjects_.register_subject(&can_show_z_tilt_);
    subjects_.register_subject(&can_show_nozzle_clean_);
    subjects_.register_subject(&can_show_purge_line_);
    // Hardware validation subjects
    subjects_.register_subject(&hardware_has_issues_);
    subjects_.register_subject(&hardware_issue_count_);
    subjects_.register_subject(&hardware_max_severity_);
    subjects_.register_subject(&hardware_validation_version_);
    subjects_.register_subject(&hardware_critical_count_);
    subjects_.register_subject(&hardware_warning_count_);
    subjects_.register_subject(&hardware_info_count_);
    subjects_.register_subject(&hardware_session_count_);
    subjects_.register_subject(&hardware_status_title_);
    subjects_.register_subject(&hardware_status_detail_);
    subjects_.register_subject(&hardware_issues_label_);
    // Firmware retraction subjects
    subjects_.register_subject(&retract_length_);
    subjects_.register_subject(&retract_speed_);
    subjects_.register_subject(&unretract_extra_length_);
    subjects_.register_subject(&unretract_speed_);
    // Manual probe subjects
    subjects_.register_subject(&manual_probe_active_);
    subjects_.register_subject(&manual_probe_z_position_);
    // Motor enabled state
    subjects_.register_subject(&motors_enabled_);
    // Version subjects
    subjects_.register_subject(&klipper_version_);
    subjects_.register_subject(&moonraker_version_);

    spdlog::debug("[PrinterState] Registered {} subjects with SubjectManager", subjects_.count());

    // Register all subjects with LVGL XML system (CRITICAL for XML bindings)
    // Note: Temperature subjects are registered by temperature_state_ component
    if (register_xml) {
        spdlog::debug("[PrinterState] Registering subjects with XML system");
        lv_xml_register_subject(NULL, "print_progress", &print_progress_);
        lv_xml_register_subject(NULL, "print_filename", &print_filename_);
        lv_xml_register_subject(NULL, "print_state", &print_state_);
        lv_xml_register_subject(NULL, "print_state_enum", &print_state_enum_);
        lv_xml_register_subject(NULL, "print_outcome", &print_outcome_);
        lv_xml_register_subject(NULL, "print_active", &print_active_);
        lv_xml_register_subject(NULL, "print_show_progress", &print_show_progress_);
        lv_xml_register_subject(NULL, "print_display_filename", &print_display_filename_);
        lv_xml_register_subject(NULL, "print_layer_current", &print_layer_current_);
        lv_xml_register_subject(NULL, "print_layer_total", &print_layer_total_);
        lv_xml_register_subject(NULL, "print_duration", &print_duration_);
        lv_xml_register_subject(NULL, "print_time_left", &print_time_left_);
        lv_xml_register_subject(NULL, "print_start_phase", &print_start_phase_);
        lv_xml_register_subject(NULL, "print_start_message", &print_start_message_);
        lv_xml_register_subject(NULL, "print_start_progress", &print_start_progress_);
        lv_xml_register_subject(NULL, "position_x", &position_x_);
        lv_xml_register_subject(NULL, "position_y", &position_y_);
        lv_xml_register_subject(NULL, "position_z", &position_z_);
        lv_xml_register_subject(NULL, "homed_axes", &homed_axes_);
        lv_xml_register_subject(NULL, "speed_factor", &speed_factor_);
        lv_xml_register_subject(NULL, "flow_factor", &flow_factor_);
        lv_xml_register_subject(NULL, "gcode_z_offset", &gcode_z_offset_);
        lv_xml_register_subject(NULL, "pending_z_offset_delta", &pending_z_offset_delta_);
        lv_xml_register_subject(NULL, "fan_speed", &fan_speed_);
        lv_xml_register_subject(NULL, "fans_version", &fans_version_);
        lv_xml_register_subject(NULL, "printer_connection_state", &printer_connection_state_);
        lv_xml_register_subject(NULL, "printer_connection_message", &printer_connection_message_);
        lv_xml_register_subject(NULL, "network_status", &network_status_);
        lv_xml_register_subject(NULL, "klippy_state", &klippy_state_);
        lv_xml_register_subject(NULL, "nav_buttons_enabled", &nav_buttons_enabled_);
        lv_xml_register_subject(NULL, "led_state", &led_state_);
        lv_xml_register_subject(NULL, "led_r", &led_r_);
        lv_xml_register_subject(NULL, "led_g", &led_g_);
        lv_xml_register_subject(NULL, "led_b", &led_b_);
        lv_xml_register_subject(NULL, "led_w", &led_w_);
        lv_xml_register_subject(NULL, "led_brightness", &led_brightness_);
        lv_xml_register_subject(NULL, "excluded_objects_version", &excluded_objects_version_);
        lv_xml_register_subject(NULL, "printer_has_qgl", &printer_has_qgl_);
        lv_xml_register_subject(NULL, "printer_has_z_tilt", &printer_has_z_tilt_);
        lv_xml_register_subject(NULL, "printer_has_bed_mesh", &printer_has_bed_mesh_);
        lv_xml_register_subject(NULL, "printer_has_nozzle_clean", &printer_has_nozzle_clean_);
        lv_xml_register_subject(NULL, "printer_has_probe", &printer_has_probe_);
        lv_xml_register_subject(NULL, "printer_has_heater_bed", &printer_has_heater_bed_);
        lv_xml_register_subject(NULL, "printer_has_led", &printer_has_led_);
        lv_xml_register_subject(NULL, "printer_has_accelerometer", &printer_has_accelerometer_);
        lv_xml_register_subject(NULL, "printer_has_spoolman", &printer_has_spoolman_);
        lv_xml_register_subject(NULL, "printer_has_speaker", &printer_has_speaker_);
        lv_xml_register_subject(NULL, "printer_has_timelapse", &printer_has_timelapse_);
        lv_xml_register_subject(NULL, "helix_plugin_installed", &helix_plugin_installed_);
        lv_xml_register_subject(NULL, "phase_tracking_enabled", &phase_tracking_enabled_);
        lv_xml_register_subject(NULL, "printer_has_firmware_retraction",
                                &printer_has_firmware_retraction_);
        lv_xml_register_subject(NULL, "printer_bed_moves", &printer_bed_moves_);
        lv_xml_register_subject(NULL, "can_show_bed_mesh", &can_show_bed_mesh_);
        lv_xml_register_subject(NULL, "can_show_qgl", &can_show_qgl_);
        lv_xml_register_subject(NULL, "can_show_z_tilt", &can_show_z_tilt_);
        lv_xml_register_subject(NULL, "can_show_nozzle_clean", &can_show_nozzle_clean_);
        lv_xml_register_subject(NULL, "can_show_purge_line", &can_show_purge_line_);
        lv_xml_register_subject(NULL, "hardware_has_issues", &hardware_has_issues_);
        lv_xml_register_subject(NULL, "hardware_issue_count", &hardware_issue_count_);
        lv_xml_register_subject(NULL, "hardware_max_severity", &hardware_max_severity_);
        lv_xml_register_subject(NULL, "hardware_validation_version", &hardware_validation_version_);
        lv_xml_register_subject(NULL, "hardware_critical_count", &hardware_critical_count_);
        lv_xml_register_subject(NULL, "hardware_warning_count", &hardware_warning_count_);
        lv_xml_register_subject(NULL, "hardware_info_count", &hardware_info_count_);
        lv_xml_register_subject(NULL, "hardware_session_count", &hardware_session_count_);
        lv_xml_register_subject(NULL, "hardware_status_title", &hardware_status_title_);
        lv_xml_register_subject(NULL, "hardware_status_detail", &hardware_status_detail_);
        lv_xml_register_subject(NULL, "hardware_issues_label", &hardware_issues_label_);
        lv_xml_register_subject(NULL, "retract_length", &retract_length_);
        lv_xml_register_subject(NULL, "retract_speed", &retract_speed_);
        lv_xml_register_subject(NULL, "unretract_extra_length", &unretract_extra_length_);
        lv_xml_register_subject(NULL, "unretract_speed", &unretract_speed_);
        lv_xml_register_subject(NULL, "manual_probe_active", &manual_probe_active_);
        lv_xml_register_subject(NULL, "manual_probe_z_position", &manual_probe_z_position_);
        lv_xml_register_subject(NULL, "motors_enabled", &motors_enabled_);
        lv_xml_register_subject(NULL, "klipper_version", &klipper_version_);
        lv_xml_register_subject(NULL, "moonraker_version", &moonraker_version_);
    } else {
        spdlog::debug("[PrinterState] Skipping XML registration (tests mode)");
    }

    subjects_initialized_ = true;
    spdlog::debug("[PrinterState] Subjects initialized and registered successfully");
}

void PrinterState::update_from_notification(const json& notification) {
    // Moonraker notifications have structure:
    // {"method": "notify_status_update", "params": [{...printer state...}, eventtime]}

    if (!notification.contains("method") || !notification.contains("params")) {
        return;
    }

    std::string method = notification["method"].get<std::string>();
    if (method != "notify_status_update") {
        return;
    }

    // Extract printer state from params[0] and delegate to update_from_status
    // CRITICAL: Defer to main thread via helix::async::invoke to avoid LVGL assertion
    // when subject updates trigger lv_obj_invalidate() during rendering
    auto params = notification["params"];
    if (params.is_array() && !params.empty()) {
        helix::async::invoke([this, state_json = params[0]]() {
            // Debug check: log if we're somehow in render phase (should never happen)
            if (lvgl_is_rendering()) {
                spdlog::error("[PrinterState] async status update running during render phase!");
                spdlog::error("[PrinterState] This should not happen - lv_async_call should run "
                              "between frames");
            }
            update_from_status(state_json);
        });
    }
}

void PrinterState::update_from_status(const json& state) {
    std::lock_guard<std::mutex> lock(state_mutex_);

    // Debug: Check if we're in render phase (this should never be true)
    LV_DEBUG_RENDER_STATE();

    // Delegate temperature updates to temperature state component
    temperature_state_.update_from_status(state);

    // Update print progress
    if (state.contains("virtual_sdcard")) {
        const auto& sdcard = state["virtual_sdcard"];

        if (sdcard.contains("progress") && sdcard["progress"].is_number()) {
            int progress_pct = helix::units::json_to_percent(sdcard, "progress");

            // Guard: Don't reset progress to 0 in terminal print states (Complete/Cancelled/Error)
            // This preserves the 100% display when a print finishes successfully
            auto current_state = static_cast<PrintJobState>(lv_subject_get_int(&print_state_enum_));
            bool is_terminal_state = (current_state == PrintJobState::COMPLETE ||
                                      current_state == PrintJobState::CANCELLED ||
                                      current_state == PrintJobState::ERROR);

            // Allow updates except: progress going backward in terminal state
            int current_progress = lv_subject_get_int(&print_progress_);
            if (!is_terminal_state || progress_pct >= current_progress) {
                lv_subject_set_int(&print_progress_, progress_pct);
            }
        }
    }

    // Update print state
    if (state.contains("print_stats")) {
        const auto& stats = state["print_stats"];

        if (stats.contains("state")) {
            std::string state_str = stats["state"].get<std::string>();
            // Update string subject (for UI display binding)
            lv_subject_copy_string(&print_state_, state_str.c_str());
            // Update enum subject (for type-safe logic)
            PrintJobState new_state = parse_print_job_state(state_str.c_str());
            auto current_state = static_cast<PrintJobState>(lv_subject_get_int(&print_state_enum_));
            auto current_outcome = static_cast<PrintOutcome>(lv_subject_get_int(&print_outcome_));

            // Update print_outcome based on state transitions:
            // - Set outcome when print reaches a terminal state (COMPLETE/CANCELLED/ERROR)
            // - Clear outcome when a NEW print starts (PRINTING from non-PAUSED)
            if (new_state != current_state) {
                // Entering a terminal state: record the outcome
                if (new_state == PrintJobState::COMPLETE) {
                    spdlog::info("[PrinterState] Print completed - setting outcome=COMPLETE");
                    lv_subject_set_int(&print_outcome_, static_cast<int>(PrintOutcome::COMPLETE));
                } else if (new_state == PrintJobState::CANCELLED) {
                    spdlog::info("[PrinterState] Print cancelled - setting outcome=CANCELLED");
                    lv_subject_set_int(&print_outcome_, static_cast<int>(PrintOutcome::CANCELLED));
                } else if (new_state == PrintJobState::ERROR) {
                    spdlog::info("[PrinterState] Print error - setting outcome=ERROR");
                    lv_subject_set_int(&print_outcome_, static_cast<int>(PrintOutcome::ERROR));
                }
                // Starting a NEW print: clear the previous outcome
                // (only when transitioning TO PRINTING from a non-PAUSED state)
                else if (new_state == PrintJobState::PRINTING &&
                         current_state != PrintJobState::PAUSED) {
                    if (current_outcome != PrintOutcome::NONE) {
                        spdlog::info("[PrinterState] New print starting - clearing outcome");
                        lv_subject_set_int(&print_outcome_, static_cast<int>(PrintOutcome::NONE));
                    }
                }
            }

            // Always update print_state_enum to reflect true Moonraker state
            // (print_outcome handles UI persistence for terminal states)
            if (new_state != current_state) {
                spdlog::info("[PrinterState] print_stats.state: '{}' -> enum {} (was {})",
                             state_str, static_cast<int>(new_state),
                             static_cast<int>(current_state));
                lv_subject_set_int(&print_state_enum_, static_cast<int>(new_state));
            }

            // Update print_active (1 when PRINTING/PAUSED, 0 otherwise)
            // This derived subject simplifies XML bindings for card visibility
            bool is_active =
                (new_state == PrintJobState::PRINTING || new_state == PrintJobState::PAUSED);
            int active_val = is_active ? 1 : 0;
            if (lv_subject_get_int(&print_active_) != active_val) {
                lv_subject_set_int(&print_active_, active_val);

                // Safety: When print becomes inactive, ensure print_start_phase is IDLE
                // This prevents "Preparing Print" from showing when print is finished
                if (!is_active) {
                    int phase = lv_subject_get_int(&print_start_phase_);
                    if (phase != static_cast<int>(PrintStartPhase::IDLE)) {
                        spdlog::warn("[PrinterState] Safety reset: print inactive but phase={}, "
                                     "resetting to IDLE",
                                     phase);
                        lv_subject_set_int(&print_start_phase_,
                                           static_cast<int>(PrintStartPhase::IDLE));
                        lv_subject_copy_string(&print_start_message_, "");
                        lv_subject_set_int(&print_start_progress_, 0);
                    }
                }
            }

            // Update combined subject for home panel progress card visibility
            update_print_show_progress();
        }

        if (stats.contains("filename")) {
            std::string filename = stats["filename"].get<std::string>();
            lv_subject_copy_string(&print_filename_, filename.c_str());
        }

        // Update layer info from print_stats.info (sent by Moonraker/mock client)
        // Note: Moonraker can send null values for layer fields when not available
        if (stats.contains("info") && stats["info"].is_object()) {
            const auto& info = stats["info"];

            if (info.contains("current_layer") && info["current_layer"].is_number()) {
                int current_layer = info["current_layer"].get<int>();
                lv_subject_set_int(&print_layer_current_, current_layer);
            }

            if (info.contains("total_layer") && info["total_layer"].is_number()) {
                int total_layer = info["total_layer"].get<int>();
                lv_subject_set_int(&print_layer_total_, total_layer);
            }
        }

        // Update print time tracking (elapsed and remaining)
        if (stats.contains("print_duration") && stats["print_duration"].is_number()) {
            int elapsed_seconds = static_cast<int>(stats["print_duration"].get<double>());
            lv_subject_set_int(&print_duration_, elapsed_seconds);
        }

        if (stats.contains("total_duration") && stats["total_duration"].is_number()) {
            // total_duration is the estimated total time, calculate remaining
            int total_seconds = static_cast<int>(stats["total_duration"].get<double>());
            int elapsed_seconds = lv_subject_get_int(&print_duration_);
            int remaining_seconds = std::max(0, total_seconds - elapsed_seconds);
            lv_subject_set_int(&print_time_left_, remaining_seconds);
        }
    }

    // Update toolhead position
    if (state.contains("toolhead")) {
        const auto& toolhead = state["toolhead"];

        if (toolhead.contains("position") && toolhead["position"].is_array()) {
            const auto& pos = toolhead["position"];
            // Note: Klipper can send null position values before homing or during errors
            if (pos.size() >= 3 && pos[0].is_number() && pos[1].is_number() && pos[2].is_number()) {
                lv_subject_set_int(&position_x_, static_cast<int>(pos[0].get<double>()));
                lv_subject_set_int(&position_y_, static_cast<int>(pos[1].get<double>()));
                lv_subject_set_int(&position_z_, static_cast<int>(pos[2].get<double>()));
            }
        }

        if (toolhead.contains("homed_axes") && toolhead["homed_axes"].is_string()) {
            std::string axes = toolhead["homed_axes"].get<std::string>();
            lv_subject_copy_string(&homed_axes_, axes.c_str());
            // Note: Derived homing subjects (xy_homed, z_homed, all_homed) are now
            // panel-local in ControlsPanel, which observes this homed_axes string.
        }

        // Extract kinematics type (determines if bed moves on Z or gantry moves)
        if (toolhead.contains("kinematics") && toolhead["kinematics"].is_string()) {
            std::string kin = toolhead["kinematics"].get<std::string>();
            set_kinematics(kin);
        }
    }

    // Update speed factor
    if (state.contains("gcode_move")) {
        const auto& gcode_move = state["gcode_move"];

        if (gcode_move.contains("speed_factor") && gcode_move["speed_factor"].is_number()) {
            int factor_pct = helix::units::json_to_percent(gcode_move, "speed_factor");
            lv_subject_set_int(&speed_factor_, factor_pct);
        }

        if (gcode_move.contains("extrude_factor") && gcode_move["extrude_factor"].is_number()) {
            int factor_pct = helix::units::json_to_percent(gcode_move, "extrude_factor");
            lv_subject_set_int(&flow_factor_, factor_pct);
        }

        // Parse Z-offset from homing_origin[2] (baby stepping / SET_GCODE_OFFSET Z=)
        if (gcode_move.contains("homing_origin") && gcode_move["homing_origin"].is_array()) {
            const auto& origin = gcode_move["homing_origin"];
            if (origin.size() >= 3 && origin[2].is_number()) {
                int z_microns = static_cast<int>(origin[2].get<double>() * 1000.0);
                lv_subject_set_int(&gcode_z_offset_, z_microns);
                spdlog::trace("[PrinterState] G-code Z-offset: {}µm", z_microns);
            }
        }
    }

    // Update fan speed
    if (state.contains("fan")) {
        const auto& fan = state["fan"];

        if (fan.contains("speed") && fan["speed"].is_number()) {
            int speed_pct = helix::units::json_to_percent(fan, "speed");
            lv_subject_set_int(&fan_speed_, speed_pct);

            // Also update multi-fan tracking
            double speed = fan["speed"].get<double>();
            update_fan_speed("fan", speed);
        }
    }

    // Check for other fan types in the status update
    // Moonraker sends fan objects as top-level keys: "heater_fan hotend_fan", "fan_generic xyz"
    for (const auto& [key, value] : state.items()) {
        // Skip non-fan objects
        if (key.rfind("heater_fan ", 0) == 0 || key.rfind("fan_generic ", 0) == 0 ||
            key.rfind("controller_fan ", 0) == 0) {
            if (value.is_object() && value.contains("speed") && value["speed"].is_number()) {
                double speed = value["speed"].get<double>();
                update_fan_speed(key, speed);
            }
        }
    }

    // Update LED state if we're tracking an LED
    // LED object names in Moonraker are like "neopixel chamber_light" or "led status_led"
    if (!tracked_led_name_.empty() && state.contains(tracked_led_name_)) {
        const auto& led = state[tracked_led_name_];

        if (led.contains("color_data") && led["color_data"].is_array() &&
            !led["color_data"].empty()) {
            // color_data is array of [R, G, B, W] arrays (one per LED in strip)
            // For on/off, we check if any color component of the first LED is > 0
            const auto& first_led = led["color_data"][0];
            if (first_led.is_array() && first_led.size() >= 3 && first_led[0].is_number() &&
                first_led[1].is_number() && first_led[2].is_number()) {
                double r = first_led[0].get<double>();
                double g = first_led[1].get<double>();
                double b = first_led[2].get<double>();
                double w = (first_led.size() >= 4 && first_led[3].is_number())
                               ? first_led[3].get<double>()
                               : 0.0;

                // Convert 0.0-1.0 range to 0-255 integer range (clamp for safety)
                int r_int = std::clamp(static_cast<int>(r * 255.0 + 0.5), 0, 255);
                int g_int = std::clamp(static_cast<int>(g * 255.0 + 0.5), 0, 255);
                int b_int = std::clamp(static_cast<int>(b * 255.0 + 0.5), 0, 255);
                int w_int = std::clamp(static_cast<int>(w * 255.0 + 0.5), 0, 255);

                // Compute brightness as max of RGBW channels (0-100%)
                int max_channel = std::max({r_int, g_int, b_int, w_int});
                int brightness = (max_channel * 100) / 255;

                // Update RGBW subjects
                lv_subject_set_int(&led_r_, r_int);
                lv_subject_set_int(&led_g_, g_int);
                lv_subject_set_int(&led_b_, b_int);
                lv_subject_set_int(&led_w_, w_int);
                lv_subject_set_int(&led_brightness_, brightness);

                // LED is "on" if any channel is non-zero
                bool is_on = (max_channel > 0);
                int new_state = is_on ? 1 : 0;

                int old_state = lv_subject_get_int(&led_state_);
                if (new_state != old_state) {
                    lv_subject_set_int(&led_state_, new_state);
                    spdlog::debug(
                        "[PrinterState] LED {} state: {} (R={} G={} B={} W={} brightness={}%)",
                        tracked_led_name_, is_on ? "ON" : "OFF", r_int, g_int, b_int, w_int,
                        brightness);
                }
            }
        }
    }

    // Update exclude_object state (for mid-print object exclusion)
    if (state.contains("exclude_object")) {
        const auto& eo = state["exclude_object"];

        if (eo.contains("excluded_objects") && eo["excluded_objects"].is_array()) {
            std::unordered_set<std::string> excluded;
            for (const auto& obj : eo["excluded_objects"]) {
                if (obj.is_string()) {
                    excluded.insert(obj.get<std::string>());
                }
            }
            // set_excluded_objects handles change detection and notification
            // Note: We're inside state_mutex_ lock, but set_excluded_objects only modifies
            // its own data and calls lv_subject_set_int which is safe
            set_excluded_objects(excluded);
        }
    }

    // Update klippy state from webhooks (for restart simulation)
    if (state.contains("webhooks")) {
        const auto& webhooks = state["webhooks"];
        if (webhooks.contains("state") && webhooks["state"].is_string()) {
            std::string klippy_state_str = webhooks["state"].get<std::string>();
            KlippyState new_state = KlippyState::READY; // default

            if (klippy_state_str == "ready") {
                new_state = KlippyState::READY;
            } else if (klippy_state_str == "startup") {
                new_state = KlippyState::STARTUP;
            } else if (klippy_state_str == "shutdown") {
                new_state = KlippyState::SHUTDOWN;
            } else if (klippy_state_str == "error") {
                new_state = KlippyState::ERROR;
            }

            lv_subject_set_int(&klippy_state_, static_cast<int>(new_state));
            spdlog::debug("[PrinterState] Klippy state from webhooks: {}", klippy_state_str);
        }
    }

    // Update manual probe state (for Z-offset calibration)
    // Klipper's manual_probe object is active during PROBE_CALIBRATE and Z_ENDSTOP_CALIBRATE
    if (state.contains("manual_probe")) {
        const auto& mp = state["manual_probe"];

        if (mp.contains("is_active") && mp["is_active"].is_boolean()) {
            bool is_active = mp["is_active"].get<bool>();
            int old_active = lv_subject_get_int(&manual_probe_active_);
            int new_active = is_active ? 1 : 0;

            if (old_active != new_active) {
                lv_subject_set_int(&manual_probe_active_, new_active);
                spdlog::info("[PrinterState] Manual probe active: {} -> {}", old_active != 0,
                             is_active);
            }
        }

        if (mp.contains("z_position") && mp["z_position"].is_number()) {
            // Store as microns (multiply by 1000) for integer subject with 0.001mm resolution
            double z_mm = mp["z_position"].get<double>();
            int z_microns = static_cast<int>(z_mm * 1000.0);
            lv_subject_set_int(&manual_probe_z_position_, z_microns);
            spdlog::trace("[PrinterState] Manual probe Z: {:.3f}mm", z_mm);
        }
    }

    // Update motor enabled state from idle_timeout
    // idle_timeout.state: "Ready" or "Printing" = motors enabled, "Idle" = motors disabled
    if (state.contains("idle_timeout")) {
        const auto& it = state["idle_timeout"];

        if (it.contains("state") && it["state"].is_string()) {
            std::string timeout_state = it["state"].get<std::string>();
            // Motors are enabled when state is "Ready" or "Printing", disabled when "Idle"
            int new_enabled = (timeout_state == "Ready" || timeout_state == "Printing") ? 1 : 0;
            int old_enabled = lv_subject_get_int(&motors_enabled_);

            if (old_enabled != new_enabled) {
                lv_subject_set_int(&motors_enabled_, new_enabled);
                spdlog::info("[PrinterState] Motors {}: idle_timeout.state='{}'",
                             new_enabled ? "enabled" : "disabled", timeout_state);
            }
        }
    }

    // Parse firmware_retraction settings (G10/G11 retraction parameters)
    if (state.contains("firmware_retraction")) {
        const auto& fr = state["firmware_retraction"];

        if (fr.contains("retract_length") && fr["retract_length"].is_number()) {
            // Store as centimillimeters (x100) to preserve 0.01mm precision
            int centimm = helix::units::json_to_centimm(fr, "retract_length");
            lv_subject_set_int(&retract_length_, centimm);
            spdlog::trace("[PrinterState] Retract length: {:.2f}mm",
                          helix::units::from_centimm(centimm));
        }

        if (fr.contains("retract_speed") && fr["retract_speed"].is_number()) {
            int speed = static_cast<int>(fr["retract_speed"].get<double>());
            lv_subject_set_int(&retract_speed_, speed);
            spdlog::trace("[PrinterState] Retract speed: {}mm/s", speed);
        }

        if (fr.contains("unretract_extra_length") && fr["unretract_extra_length"].is_number()) {
            int centimm = helix::units::json_to_centimm(fr, "unretract_extra_length");
            lv_subject_set_int(&unretract_extra_length_, centimm);
            spdlog::trace("[PrinterState] Unretract extra: {:.2f}mm",
                          helix::units::from_centimm(centimm));
        }

        if (fr.contains("unretract_speed") && fr["unretract_speed"].is_number()) {
            int speed = static_cast<int>(fr["unretract_speed"].get<double>());
            lv_subject_set_int(&unretract_speed_, speed);
            spdlog::trace("[PrinterState] Unretract speed: {}mm/s", speed);
        }
    }

    // Forward filament sensor updates to FilamentSensorManager
    // The manager handles all sensor types: filament_switch_sensor and filament_motion_sensor
    helix::FilamentSensorManager::instance().update_from_status(state);

    // Cache full state for complex queries
    json_state_.merge_patch(state);
}

json& PrinterState::get_json_state() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return json_state_;
}

void PrinterState::reset_for_new_print() {
    // Clear stale print PROGRESS data when starting a new print.
    // The preparing overlay covers the UI, so stale data isn't visible.
    // IMPORTANT: Do NOT clear print_filename_ or print_display_filename_ here!
    // Clearing filename triggers ActivePrintMediaManager to wipe the thumbnail we just set.
    // Filename is Moonraker's source of truth - it updates when the print actually starts.
    lv_subject_set_int(&print_progress_, 0);
    lv_subject_set_int(&print_layer_current_, 0);
    lv_subject_set_int(&print_duration_, 0);
    lv_subject_set_int(&print_time_left_, 0);
    spdlog::debug("[PrinterState] Reset print progress for new print");
}

// ============================================================================
// MULTI-FAN TRACKING
// ============================================================================

namespace {
/**
 * @brief Determine fan type from Moonraker object name
 */
FanType classify_fan_type(const std::string& object_name) {
    if (object_name == "fan") {
        return FanType::PART_COOLING;
    } else if (object_name.rfind("heater_fan ", 0) == 0) {
        return FanType::HEATER_FAN;
    } else if (object_name.rfind("controller_fan ", 0) == 0) {
        return FanType::CONTROLLER_FAN;
    } else {
        return FanType::GENERIC_FAN;
    }
}

/**
 * @brief Determine if fan is user-controllable
 *
 * Part cooling fans and generic fans can be controlled via SET_FAN_SPEED.
 * Heater fans and controller fans are auto-controlled by firmware.
 */
bool is_fan_controllable(FanType type) {
    return type == FanType::PART_COOLING || type == FanType::GENERIC_FAN;
}
} // namespace

void PrinterState::init_fans(const std::vector<std::string>& fan_objects) {
    // Deinit existing per-fan subjects before clearing (unique_ptr handles memory)
    for (auto& [name, subject_ptr] : fan_speed_subjects_) {
        if (subject_ptr) {
            lv_subject_deinit(subject_ptr.get());
        }
    }
    fan_speed_subjects_.clear();

    fans_.clear();
    fans_.reserve(fan_objects.size());

    // Reserve map capacity to prevent rehashing during insertion
    // (unique_ptr makes this less critical, but still good practice)
    fan_speed_subjects_.reserve(fan_objects.size());

    for (const auto& obj_name : fan_objects) {
        FanInfo info;
        info.object_name = obj_name;
        info.display_name = helix::get_display_name(obj_name, helix::DeviceType::FAN);
        info.type = classify_fan_type(obj_name);
        info.is_controllable = is_fan_controllable(info.type);
        info.speed_percent = 0;

        spdlog::debug("[PrinterState] Registered fan: {} -> \"{}\" (type={}, controllable={})",
                      obj_name, info.display_name, static_cast<int>(info.type),
                      info.is_controllable);
        fans_.push_back(std::move(info));

        // Create per-fan speed subject for reactive UI updates (heap-allocated to survive rehash)
        auto subject_ptr = std::make_unique<lv_subject_t>();
        lv_subject_init_int(subject_ptr.get(), 0);
        fan_speed_subjects_.emplace(obj_name, std::move(subject_ptr));
        spdlog::debug("[PrinterState] Created speed subject for fan: {}", obj_name);
    }

    // Initialize and bump version to notify UI
    lv_subject_set_int(&fans_version_, lv_subject_get_int(&fans_version_) + 1);
    spdlog::info("[PrinterState] Initialized {} fans with {} speed subjects (version {})",
                 fans_.size(), fan_speed_subjects_.size(), lv_subject_get_int(&fans_version_));
}

void PrinterState::update_fan_speed(const std::string& object_name, double speed) {
    int speed_pct = helix::units::to_percent(speed);

    for (auto& fan : fans_) {
        if (fan.object_name == object_name) {
            if (fan.speed_percent != speed_pct) {
                fan.speed_percent = speed_pct;

                // Fire per-fan subject for reactive UI updates
                auto it = fan_speed_subjects_.find(object_name);
                if (it != fan_speed_subjects_.end() && it->second) {
                    lv_subject_set_int(it->second.get(), speed_pct);
                    spdlog::trace("[PrinterState] Fan {} speed updated to {}%", object_name,
                                  speed_pct);
                }
            }
            return;
        }
    }
    // Fan not in list - this is normal during initial status before discovery
}

lv_subject_t* PrinterState::get_fan_speed_subject(const std::string& object_name) {
    auto it = fan_speed_subjects_.find(object_name);
    if (it != fan_speed_subjects_.end() && it->second) {
        return it->second.get();
    }
    return nullptr;
}

void PrinterState::set_printer_connection_state(int state, const char* message) {
    // Thread-safe wrapper: defer LVGL subject updates to main thread
    std::string msg = message ? message : "";
    helix::async::invoke(
        [this, state, msg]() { set_printer_connection_state_internal(state, msg.c_str()); });
}

void PrinterState::set_printer_connection_state_internal(int state, const char* message) {
    // Called from main thread via ui_async_call
    spdlog::info("[PrinterState] Printer connection state changed: {} - {}", state, message);

    // Track if we've ever successfully connected
    if (state == static_cast<int>(ConnectionState::CONNECTED) && !was_ever_connected_) {
        was_ever_connected_ = true;
        spdlog::debug("[PrinterState] First successful connection - was_ever_connected_ = true");
    }

    spdlog::trace("[PrinterState] Setting printer_connection_state_ subject (at {}) to value {}",
                  (void*)&printer_connection_state_, state);
    lv_subject_set_int(&printer_connection_state_, state);
    spdlog::trace("[PrinterState] Subject value now: {}",
                  lv_subject_get_int(&printer_connection_state_));
    lv_subject_copy_string(&printer_connection_message_, message);
    update_nav_buttons_enabled();
    spdlog::trace(
        "[PrinterState] Printer connection state update complete, observers should be notified");
}

void PrinterState::set_network_status(int status) {
    spdlog::debug("[PrinterState] Network status changed: {}", status);
    lv_subject_set_int(&network_status_, status);
}

void PrinterState::set_klippy_state(KlippyState state) {
    // Thread-safe wrapper: defer LVGL subject updates to main thread
    helix::async::call_method(this, &PrinterState::set_klippy_state_internal, state);
}

void PrinterState::set_klippy_state_sync(KlippyState state) {
    // Direct call for main-thread use (testing, or when already on main thread)
    set_klippy_state_internal(state);
}

void PrinterState::set_klippy_state_internal(KlippyState state) {
    const char* state_names[] = {"READY", "STARTUP", "SHUTDOWN", "ERROR"};
    int state_int = static_cast<int>(state);
    spdlog::info("[PrinterState] Klippy state changed: {} ({})", state_names[state_int], state_int);
    lv_subject_set_int(&klippy_state_, state_int);
    update_nav_buttons_enabled();
}

void PrinterState::update_nav_buttons_enabled() {
    // Compute combined state: enabled when connected AND klippy ready
    int connection = lv_subject_get_int(&printer_connection_state_);
    int klippy = lv_subject_get_int(&klippy_state_);
    bool connected = (connection == static_cast<int>(ConnectionState::CONNECTED));
    bool klippy_ready = (klippy == static_cast<int>(KlippyState::READY));
    int enabled = (connected && klippy_ready) ? 1 : 0;

    // Only update if changed to avoid unnecessary observer notifications
    if (lv_subject_get_int(&nav_buttons_enabled_) != enabled) {
        spdlog::debug("[PrinterState] nav_buttons_enabled: {} (connected={}, klippy_ready={})",
                      enabled, connected, klippy_ready);
        lv_subject_set_int(&nav_buttons_enabled_, enabled);
    }
}

void PrinterState::set_print_in_progress(bool in_progress) {
    // Thread-safe wrapper: defer LVGL subject updates to main thread
    helix::async::invoke([this, in_progress]() { set_print_in_progress_internal(in_progress); });
}

void PrinterState::set_print_in_progress_internal(bool in_progress) {
    int new_value = in_progress ? 1 : 0;
    if (lv_subject_get_int(&print_in_progress_) != new_value) {
        spdlog::debug("[PrinterState] Print in progress: {}", in_progress);
        lv_subject_set_int(&print_in_progress_, new_value);
    }
}

void PrinterState::set_tracked_led(const std::string& led_name) {
    tracked_led_name_ = led_name;
    if (!led_name.empty()) {
        spdlog::info("[PrinterState] Tracking LED: {}", led_name);
    } else {
        spdlog::debug("[PrinterState] LED tracking disabled");
    }
}

void PrinterState::set_hardware(const helix::PrinterHardwareDiscovery& hardware) {
    // Thread-safe wrapper: defer LVGL subject updates to main thread
    helix::async::call_method_ref(this, &PrinterState::set_hardware_internal, hardware);
}

void PrinterState::set_hardware_internal(const helix::PrinterHardwareDiscovery& hardware) {
    // Pass auto-detected hardware to the override layer
    capability_overrides_.set_hardware(hardware);

    // Update subjects using effective values (auto-detect + user overrides)
    // This allows users to force-enable features that weren't detected
    // (e.g., heat soak macro without chamber heater) or force-disable
    // features they don't want to see in the UI.
    lv_subject_set_int(&printer_has_qgl_, capability_overrides_.has_qgl() ? 1 : 0);
    lv_subject_set_int(&printer_has_z_tilt_, capability_overrides_.has_z_tilt() ? 1 : 0);
    lv_subject_set_int(&printer_has_bed_mesh_, capability_overrides_.has_bed_mesh() ? 1 : 0);
    lv_subject_set_int(&printer_has_nozzle_clean_,
                       capability_overrides_.has_nozzle_clean() ? 1 : 0);

    // Hardware capabilities (no user override support yet - set directly from detection)
    lv_subject_set_int(&printer_has_probe_, hardware.has_probe() ? 1 : 0);
    lv_subject_set_int(&printer_has_heater_bed_, hardware.has_heater_bed() ? 1 : 0);
    lv_subject_set_int(&printer_has_led_, hardware.has_led() ? 1 : 0);
    lv_subject_set_int(&printer_has_accelerometer_, hardware.has_accelerometer() ? 1 : 0);

    // Speaker capability (for M300 audio feedback)
    lv_subject_set_int(&printer_has_speaker_, hardware.has_speaker() ? 1 : 0);

    // Timelapse capability (Moonraker-Timelapse plugin)
    lv_subject_set_int(&printer_has_timelapse_, hardware.has_timelapse() ? 1 : 0);

    // Firmware retraction capability (for G10/G11 retraction settings)
    lv_subject_set_int(&printer_has_firmware_retraction_,
                       hardware.has_firmware_retraction() ? 1 : 0);

    // Spoolman requires async check - default to 0, updated separately

    spdlog::info("[PrinterState] Hardware set: probe={}, heater_bed={}, LED={}, "
                 "accelerometer={}, speaker={}, timelapse={}, fw_retraction={}",
                 hardware.has_probe(), hardware.has_heater_bed(), hardware.has_led(),
                 hardware.has_accelerometer(), hardware.has_speaker(), hardware.has_timelapse(),
                 hardware.has_firmware_retraction());
    spdlog::info("[PrinterState] Hardware set (with overrides): {}",
                 capability_overrides_.summary());

    // Update composite subjects for G-code modification options
    // (visibility depends on both plugin status and capability)
    update_gcode_modification_visibility();
}

void PrinterState::set_klipper_version(const std::string& version) {
    // Thread-safe wrapper: defer LVGL subject updates to main thread
    helix::async::call_method_ref(this, &PrinterState::set_klipper_version_internal, version);
}

void PrinterState::set_klipper_version_internal(const std::string& version) {
    lv_subject_copy_string(&klipper_version_, version.c_str());
    spdlog::debug("[PrinterState] Klipper version set: {}", version);
}

void PrinterState::set_moonraker_version(const std::string& version) {
    // Thread-safe wrapper: defer LVGL subject updates to main thread
    helix::async::call_method_ref(this, &PrinterState::set_moonraker_version_internal, version);
}

void PrinterState::set_moonraker_version_internal(const std::string& version) {
    lv_subject_copy_string(&moonraker_version_, version.c_str());
    spdlog::debug("[PrinterState] Moonraker version set: {}", version);
}

void PrinterState::set_spoolman_available(bool available) {
    // Thread-safe: Use helix::async::invoke to update LVGL subject from any thread
    helix::async::invoke([this, available]() {
        lv_subject_set_int(&printer_has_spoolman_, available ? 1 : 0);
        spdlog::info("[PrinterState] Spoolman availability set: {}", available);
    });
}

void PrinterState::set_helix_plugin_installed(bool installed) {
    // Thread-safe: Use helix::async::invoke to update LVGL subject from any thread
    helix::async::invoke([this, installed]() {
        lv_subject_set_int(&helix_plugin_installed_, installed ? 1 : 0);
        spdlog::info("[PrinterState] HelixPrint plugin installed: {}", installed);

        // Update composite subjects for G-code modification options
        update_gcode_modification_visibility();
    });
}

bool PrinterState::service_has_helix_plugin() const {
    // Note: lv_subject_get_int is thread-safe (atomic read)
    // Tri-state: -1=unknown, 0=not installed, 1=installed
    return lv_subject_get_int(const_cast<lv_subject_t*>(&helix_plugin_installed_)) == 1;
}

void PrinterState::set_phase_tracking_enabled(bool enabled) {
    // Thread-safe: Use helix::async::invoke to update LVGL subject from any thread
    helix::async::invoke([this, enabled]() {
        lv_subject_set_int(&phase_tracking_enabled_, enabled ? 1 : 0);
        spdlog::info("[PrinterState] Phase tracking enabled: {}", enabled);
    });
}

bool PrinterState::is_phase_tracking_enabled() const {
    // Note: lv_subject_get_int is thread-safe (atomic read)
    // Tri-state: -1=unknown, 0=disabled, 1=enabled
    return lv_subject_get_int(const_cast<lv_subject_t*>(&phase_tracking_enabled_)) == 1;
}

void PrinterState::update_gcode_modification_visibility() {
    // Recalculate composite subjects: can_show_X = helix_plugin_installed && printer_has_X
    // These control visibility of pre-print G-code modification options in the UI
    // Tri-state: -1=unknown, 0=not installed, 1=installed (only 1 counts as "has plugin")
    bool plugin = lv_subject_get_int(&helix_plugin_installed_) == 1;

    auto update_if_changed = [](lv_subject_t* subject, int new_value) {
        if (lv_subject_get_int(subject) != new_value) {
            lv_subject_set_int(subject, new_value);
        }
    };

    update_if_changed(&can_show_bed_mesh_,
                      (plugin && lv_subject_get_int(&printer_has_bed_mesh_)) ? 1 : 0);
    update_if_changed(&can_show_qgl_, (plugin && lv_subject_get_int(&printer_has_qgl_)) ? 1 : 0);
    update_if_changed(&can_show_z_tilt_,
                      (plugin && lv_subject_get_int(&printer_has_z_tilt_)) ? 1 : 0);
    update_if_changed(&can_show_nozzle_clean_,
                      (plugin && lv_subject_get_int(&printer_has_nozzle_clean_)) ? 1 : 0);
    update_if_changed(&can_show_purge_line_,
                      (plugin && lv_subject_get_int(&printer_has_purge_line_)) ? 1 : 0);

    spdlog::debug("[PrinterState] G-code modification visibility updated: bed_mesh={}, qgl={}, "
                  "z_tilt={}, nozzle_clean={}, purge_line={} (plugin={})",
                  lv_subject_get_int(&can_show_bed_mesh_), lv_subject_get_int(&can_show_qgl_),
                  lv_subject_get_int(&can_show_z_tilt_),
                  lv_subject_get_int(&can_show_nozzle_clean_),
                  lv_subject_get_int(&can_show_purge_line_), plugin);
}

void PrinterState::update_print_show_progress() {
    // Combined subject for home panel progress card visibility
    // Show progress card only when: print is active AND not in print start phase
    bool is_active = lv_subject_get_int(&print_active_) != 0;
    bool is_starting =
        lv_subject_get_int(&print_start_phase_) != static_cast<int>(PrintStartPhase::IDLE);
    int new_value = (is_active && !is_starting) ? 1 : 0;

    if (lv_subject_get_int(&print_show_progress_) != new_value) {
        lv_subject_set_int(&print_show_progress_, new_value);
        spdlog::debug("[PrinterState] print_show_progress updated: {} (active={}, starting={})",
                      new_value, is_active, is_starting);
    }
}

void PrinterState::set_excluded_objects(const std::unordered_set<std::string>& objects) {
    // Only update if the set actually changed
    if (excluded_objects_ != objects) {
        excluded_objects_ = objects;

        // Increment version to notify observers
        int version = lv_subject_get_int(&excluded_objects_version_);
        lv_subject_set_int(&excluded_objects_version_, version + 1);

        spdlog::debug("[PrinterState] Excluded objects updated: {} objects (version {})",
                      excluded_objects_.size(), version + 1);
    }
}

PrintJobState PrinterState::get_print_job_state() const {
    // Note: lv_subject_get_int is thread-safe (atomic read)
    return static_cast<PrintJobState>(
        lv_subject_get_int(const_cast<lv_subject_t*>(&print_state_enum_)));
}

bool PrinterState::can_start_new_print() const {
    // Check if a print workflow is already in progress (UI state)
    // This prevents double-tap issues during long G-code modification workflows
    if (is_print_in_progress()) {
        return false;
    }

    // Check printer's physical state
    PrintJobState state = get_print_job_state();
    // A new print can be started when printer is idle or previous print finished
    switch (state) {
    case PrintJobState::STANDBY:
    case PrintJobState::COMPLETE:
    case PrintJobState::CANCELLED:
    case PrintJobState::ERROR:
        return true;
    case PrintJobState::PRINTING:
    case PrintJobState::PAUSED:
        return false;
    default:
        // Unknown state - be conservative
        return false;
    }
}

void PrinterState::set_kinematics(const std::string& kinematics) {
    // Determine if the bed moves on Z based on kinematics type:
    // - Cartesian: bed typically moves on Z axis (Ender 3, Prusa MK3, etc.)
    // - CoreXY/CoreXZ: gantry typically moves on Z axis (Voron 2.4, RatRig, etc.)
    // - Delta: effector moves on Z, bed is stationary
    //
    // Note: This is a heuristic. Some CoreXY printers (Voron Trident) have gantry-Z.
    // For perfect accuracy, we'd need to parse stepper_z configuration.
    bool bed_moves_z = (kinematics.find("cartesian") != std::string::npos);
    int new_value = bed_moves_z ? 1 : 0;

    // Only log when value actually changes (this gets called frequently from status updates)
    if (lv_subject_get_int(&printer_bed_moves_) != new_value) {
        lv_subject_set_int(&printer_bed_moves_, new_value);
        spdlog::info("[PrinterState] Kinematics: {} -> bed_moves_z={}", kinematics, bed_moves_z);
    }
}

// ============================================================================
// PENDING Z-OFFSET DELTA TRACKING
// ============================================================================

void PrinterState::add_pending_z_offset_delta(int delta_microns) {
    int current = lv_subject_get_int(&pending_z_offset_delta_);
    int new_value = current + delta_microns;
    lv_subject_set_int(&pending_z_offset_delta_, new_value);
    spdlog::debug("[PrinterState] Pending Z-offset delta: {:+}µm (total: {:+}µm)", delta_microns,
                  new_value);
}

int PrinterState::get_pending_z_offset_delta() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&pending_z_offset_delta_));
}

bool PrinterState::has_pending_z_offset_adjustment() const {
    return get_pending_z_offset_delta() != 0;
}

void PrinterState::clear_pending_z_offset_delta() {
    if (has_pending_z_offset_adjustment()) {
        spdlog::info("[PrinterState] Clearing pending Z-offset delta");
        lv_subject_set_int(&pending_z_offset_delta_, 0);
    }
}

// ============================================================================
// PRINT START PROGRESS TRACKING
// ============================================================================

bool PrinterState::is_in_print_start() const {
    int phase = lv_subject_get_int(const_cast<lv_subject_t*>(&print_start_phase_));
    return phase != static_cast<int>(PrintStartPhase::IDLE);
}

void PrinterState::set_print_start_state(PrintStartPhase phase, const char* message, int progress) {
    spdlog::debug("[PrinterState] Print start: phase={}, message='{}', progress={}%",
                  static_cast<int>(phase), message ? message : "", progress);

    // CRITICAL: Defer to main thread via helix::async::invoke to avoid LVGL assertion
    // when subject updates trigger lv_obj_invalidate() during rendering.
    // This is called from WebSocket callbacks (background thread).
    std::string msg = message ? message : "";
    int clamped_progress = std::clamp(progress, 0, 100);
    int old_phase = lv_subject_get_int(&print_start_phase_);
    helix::async::invoke([this, phase, old_phase, msg, clamped_progress]() {
        // Reset print progress when transitioning from IDLE to a preparing phase
        if (old_phase == static_cast<int>(PrintStartPhase::IDLE) &&
            phase != PrintStartPhase::IDLE) {
            reset_for_new_print();
        }
        lv_subject_set_int(&print_start_phase_, static_cast<int>(phase));
        if (!msg.empty()) {
            lv_subject_copy_string(&print_start_message_, msg.c_str());
        }
        lv_subject_set_int(&print_start_progress_, clamped_progress);
        update_print_show_progress();
    });
}

void PrinterState::reset_print_start_state() {
    // CRITICAL: Defer to main thread via helix::async::invoke
    helix::async::invoke([this]() {
        int phase = lv_subject_get_int(&print_start_phase_);
        if (phase != static_cast<int>(PrintStartPhase::IDLE)) {
            spdlog::info("[PrinterState] Resetting print start state to IDLE");
            lv_subject_set_int(&print_start_phase_, static_cast<int>(PrintStartPhase::IDLE));
            lv_subject_copy_string(&print_start_message_, "");
            lv_subject_set_int(&print_start_progress_, 0);
            update_print_show_progress();
        }
    });
}

void PrinterState::set_print_thumbnail_path(const std::string& path) {
    // Thumbnail path is set from PrintStatusPanel's main-thread callback,
    // so we can safely update the subject directly without ui_async_call.
    if (path.empty()) {
        spdlog::debug("[PrinterState] Clearing print thumbnail path");
    } else {
        spdlog::debug("[PrinterState] Setting print thumbnail path: {}", path);
    }
    lv_subject_copy_string(&print_thumbnail_path_, path.c_str());
}

void PrinterState::set_print_display_filename(const std::string& name) {
    // Display filename is set from PrintStatusPanel's main-thread callback.
    spdlog::debug("[PrinterState] Setting print display filename: {}", name);
    lv_subject_copy_string(&print_display_filename_, name.c_str());
}

// ============================================================================
// HARDWARE VALIDATION
// ============================================================================

void PrinterState::set_hardware_validation_result(const HardwareValidationResult& result) {
    // Store the full result for UI access
    hardware_validation_result_ = result;

    // Update summary subjects
    lv_subject_set_int(&hardware_has_issues_, result.has_issues() ? 1 : 0);
    lv_subject_set_int(&hardware_issue_count_, static_cast<int>(result.total_issue_count()));
    lv_subject_set_int(&hardware_max_severity_, static_cast<int>(result.max_severity()));

    // Update category counts
    lv_subject_set_int(&hardware_critical_count_, static_cast<int>(result.critical_missing.size()));
    lv_subject_set_int(&hardware_warning_count_, static_cast<int>(result.expected_missing.size()));
    lv_subject_set_int(&hardware_info_count_, static_cast<int>(result.newly_discovered.size()));
    lv_subject_set_int(&hardware_session_count_,
                       static_cast<int>(result.changed_from_last_session.size()));

    // Update status text
    if (!result.has_issues()) {
        snprintf(hardware_status_title_buf_, sizeof(hardware_status_title_buf_), "All Healthy");
        snprintf(hardware_status_detail_buf_, sizeof(hardware_status_detail_buf_),
                 "All configured hardware detected");
    } else {
        size_t total = result.total_issue_count();
        snprintf(hardware_status_title_buf_, sizeof(hardware_status_title_buf_),
                 "%zu Issue%s Detected", total, total == 1 ? "" : "s");

        // Build detail string
        std::string detail;
        if (!result.critical_missing.empty()) {
            detail += std::to_string(result.critical_missing.size()) + " critical";
        }
        if (!result.expected_missing.empty()) {
            if (!detail.empty())
                detail += ", ";
            detail += std::to_string(result.expected_missing.size()) + " missing";
        }
        if (!result.newly_discovered.empty()) {
            if (!detail.empty())
                detail += ", ";
            detail += std::to_string(result.newly_discovered.size()) + " new";
        }
        if (!result.changed_from_last_session.empty()) {
            if (!detail.empty())
                detail += ", ";
            detail += std::to_string(result.changed_from_last_session.size()) + " changed";
        }
        snprintf(hardware_status_detail_buf_, sizeof(hardware_status_detail_buf_), "%s",
                 detail.c_str());
    }
    lv_subject_copy_string(&hardware_status_title_, hardware_status_title_buf_);
    lv_subject_copy_string(&hardware_status_detail_, hardware_status_detail_buf_);

    // Update issues label for settings panel ("1 Hardware Issue" / "5 Hardware Issues")
    size_t total = result.total_issue_count();
    if (total == 0) {
        snprintf(hardware_issues_label_buf_, sizeof(hardware_issues_label_buf_),
                 "No Hardware Issues");
    } else if (total == 1) {
        snprintf(hardware_issues_label_buf_, sizeof(hardware_issues_label_buf_),
                 "1 Hardware Issue");
    } else {
        snprintf(hardware_issues_label_buf_, sizeof(hardware_issues_label_buf_),
                 "%zu Hardware Issues", total);
    }
    lv_subject_copy_string(&hardware_issues_label_, hardware_issues_label_buf_);

    // Increment version to notify UI observers
    int version = lv_subject_get_int(&hardware_validation_version_);
    lv_subject_set_int(&hardware_validation_version_, version + 1);

    spdlog::debug("[PrinterState] Hardware validation updated: {} issues, max_severity={}",
                  result.total_issue_count(), static_cast<int>(result.max_severity()));
}

const HardwareValidationResult& PrinterState::get_hardware_validation_result() const {
    return hardware_validation_result_;
}

void PrinterState::remove_hardware_issue(const std::string& hardware_name) {
    // Helper lambda to remove an issue from a vector by hardware_name
    auto remove_by_name = [&hardware_name](std::vector<HardwareIssue>& issues) {
        issues.erase(std::remove_if(issues.begin(), issues.end(),
                                    [&hardware_name](const HardwareIssue& issue) {
                                        return issue.hardware_name == hardware_name;
                                    }),
                     issues.end());
    };

    // Remove from all issue lists
    remove_by_name(hardware_validation_result_.critical_missing);
    remove_by_name(hardware_validation_result_.expected_missing);
    remove_by_name(hardware_validation_result_.newly_discovered);
    remove_by_name(hardware_validation_result_.changed_from_last_session);

    // Re-apply the updated result to refresh all subjects
    set_hardware_validation_result(hardware_validation_result_);

    spdlog::debug("[PrinterState] Removed hardware issue: {}", hardware_name);
}

void PrinterState::set_print_outcome(PrintOutcome outcome) {
    lv_subject_set_int(&print_outcome_, static_cast<int>(outcome));
    spdlog::info("[PrinterState] Print outcome set to: {}", static_cast<int>(outcome));
}

// ============================================================================
// PRINTER TYPE AND PRINT START CAPABILITIES
// ============================================================================

void PrinterState::set_printer_type(const std::string& type) {
    // Thread-safe wrapper: defer updates to main thread
    helix::async::call_method_ref(this, &PrinterState::set_printer_type_internal, type);
}

void PrinterState::set_printer_type_sync(const std::string& type) {
    // Direct call for main-thread use (testing, or when already on main thread)
    set_printer_type_internal(type);
}

void PrinterState::set_printer_type_internal(const std::string& type) {
    printer_type_ = type;
    print_start_capabilities_ = PrinterDetector::get_print_start_capabilities(type);

    // Update printer_has_purge_line_ based on capabilities database
    // "priming" is the capability key for purge/prime line in the database
    bool has_priming = print_start_capabilities_.get_capability("priming") != nullptr;
    lv_subject_set_int(&printer_has_purge_line_, has_priming ? 1 : 0);

    // Recalculate composite visibility subjects
    update_gcode_modification_visibility();

    spdlog::info("[PrinterState] Printer type set to: '{}' (capabilities: {}, priming={})", type,
                 print_start_capabilities_.empty() ? "none" : print_start_capabilities_.macro_name,
                 has_priming);
}

const std::string& PrinterState::get_printer_type() const {
    return printer_type_;
}

const PrintStartCapabilities& PrinterState::get_print_start_capabilities() const {
    return print_start_capabilities_;
}
