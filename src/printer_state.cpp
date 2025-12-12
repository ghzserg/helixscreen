// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "printer_state.h"

#include "capability_overrides.h"
#include "printer_capabilities.h"
#include "runtime_config.h"

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
    // Deinitialize all subjects to clear observers
    lv_subject_deinit(&extruder_temp_);
    lv_subject_deinit(&extruder_target_);
    lv_subject_deinit(&bed_temp_);
    lv_subject_deinit(&bed_target_);
    lv_subject_deinit(&print_progress_);
    lv_subject_deinit(&print_filename_);
    lv_subject_deinit(&print_state_);
    lv_subject_deinit(&print_state_enum_);
    lv_subject_deinit(&print_layer_current_);
    lv_subject_deinit(&print_layer_total_);
    lv_subject_deinit(&position_x_);
    lv_subject_deinit(&position_y_);
    lv_subject_deinit(&position_z_);
    lv_subject_deinit(&homed_axes_);
    lv_subject_deinit(&speed_factor_);
    lv_subject_deinit(&flow_factor_);
    lv_subject_deinit(&gcode_z_offset_);
    lv_subject_deinit(&fan_speed_);
    lv_subject_deinit(&fans_version_);
    lv_subject_deinit(&printer_connection_state_);
    lv_subject_deinit(&printer_connection_message_);
    lv_subject_deinit(&network_status_);
    lv_subject_deinit(&klippy_state_);
    lv_subject_deinit(&led_state_);
    lv_subject_deinit(&excluded_objects_version_);
    lv_subject_deinit(&printer_has_qgl_);
    lv_subject_deinit(&printer_has_z_tilt_);
    lv_subject_deinit(&printer_has_bed_mesh_);
    lv_subject_deinit(&printer_has_nozzle_clean_);
    lv_subject_deinit(&printer_has_probe_);
    lv_subject_deinit(&printer_has_heater_bed_);
    lv_subject_deinit(&printer_has_led_);
    lv_subject_deinit(&printer_has_accelerometer_);
    lv_subject_deinit(&printer_has_spoolman_);
    lv_subject_deinit(&printer_has_speaker_);
    lv_subject_deinit(&printer_has_timelapse_);
    lv_subject_deinit(&printer_bed_moves_);
    lv_subject_deinit(&manual_probe_active_);
    lv_subject_deinit(&manual_probe_z_position_);
    lv_subject_deinit(&klipper_version_);
    lv_subject_deinit(&moonraker_version_);

    subjects_initialized_ = false;
}

void PrinterState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        spdlog::debug("Printer state subjects already initialized, skipping");
        return;
    }

    spdlog::debug("Initializing printer state subjects (register_xml={})", register_xml);

    // Temperature subjects (integer, degrees Celsius)
    lv_subject_init_int(&extruder_temp_, 0);
    lv_subject_init_int(&extruder_target_, 0);
    lv_subject_init_int(&bed_temp_, 0);
    lv_subject_init_int(&bed_target_, 0);

    // Print progress subjects
    lv_subject_init_int(&print_progress_, 0);
    lv_subject_init_string(&print_filename_, print_filename_buf_, nullptr,
                           sizeof(print_filename_buf_), "");
    lv_subject_init_string(&print_state_, print_state_buf_, nullptr, sizeof(print_state_buf_),
                           "standby");
    lv_subject_init_int(&print_state_enum_, static_cast<int>(PrintJobState::STANDBY));

    // Layer tracking subjects (from Moonraker print_stats.info)
    lv_subject_init_int(&print_layer_current_, 0);
    lv_subject_init_int(&print_layer_total_, 0);

    // Print time tracking subjects (in seconds)
    lv_subject_init_int(&print_duration_, 0);
    lv_subject_init_int(&print_time_left_, 0);

    // Motion subjects
    lv_subject_init_int(&position_x_, 0);
    lv_subject_init_int(&position_y_, 0);
    lv_subject_init_int(&position_z_, 0);
    lv_subject_init_string(&homed_axes_, homed_axes_buf_, nullptr, sizeof(homed_axes_buf_), "");

    // Speed/Flow subjects (percentages)
    lv_subject_init_int(&speed_factor_, 100);
    lv_subject_init_int(&flow_factor_, 100);
    lv_subject_init_int(&gcode_z_offset_, 0); // Z-offset in microns from homing_origin[2]
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

    // LED state subject (0=off, 1=on, derived from LED color data)
    lv_subject_init_int(&led_state_, 0);

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
    lv_subject_init_int(&printer_bed_moves_, 0); // 0=gantry moves, 1=bed moves (cartesian)

    // Manual probe subjects (for Z-offset calibration)
    lv_subject_init_int(&manual_probe_active_, 0);     // 0=inactive, 1=active
    lv_subject_init_int(&manual_probe_z_position_, 0); // Z position in microns

    // Version subjects (for About section)
    lv_subject_init_string(&klipper_version_, klipper_version_buf_, nullptr,
                           sizeof(klipper_version_buf_), "—");
    lv_subject_init_string(&moonraker_version_, moonraker_version_buf_, nullptr,
                           sizeof(moonraker_version_buf_), "—");

    // Register all subjects with LVGL XML system (CRITICAL for XML bindings)
    if (register_xml) {
        spdlog::debug("[PrinterState] Registering subjects with XML system");
        lv_xml_register_subject(NULL, "extruder_temp", &extruder_temp_);
        lv_xml_register_subject(NULL, "extruder_target", &extruder_target_);
        lv_xml_register_subject(NULL, "bed_temp", &bed_temp_);
        lv_xml_register_subject(NULL, "bed_target", &bed_target_);
        lv_xml_register_subject(NULL, "print_progress", &print_progress_);
        lv_xml_register_subject(NULL, "print_filename", &print_filename_);
        lv_xml_register_subject(NULL, "print_state", &print_state_);
        lv_xml_register_subject(NULL, "print_state_enum", &print_state_enum_);
        lv_xml_register_subject(NULL, "print_layer_current", &print_layer_current_);
        lv_xml_register_subject(NULL, "print_layer_total", &print_layer_total_);
        lv_xml_register_subject(NULL, "print_duration", &print_duration_);
        lv_xml_register_subject(NULL, "print_time_left", &print_time_left_);
        lv_xml_register_subject(NULL, "position_x", &position_x_);
        lv_xml_register_subject(NULL, "position_y", &position_y_);
        lv_xml_register_subject(NULL, "position_z", &position_z_);
        lv_xml_register_subject(NULL, "homed_axes", &homed_axes_);
        lv_xml_register_subject(NULL, "speed_factor", &speed_factor_);
        lv_xml_register_subject(NULL, "flow_factor", &flow_factor_);
        lv_xml_register_subject(NULL, "gcode_z_offset", &gcode_z_offset_);
        lv_xml_register_subject(NULL, "fan_speed", &fan_speed_);
        lv_xml_register_subject(NULL, "fans_version", &fans_version_);
        lv_xml_register_subject(NULL, "printer_connection_state", &printer_connection_state_);
        lv_xml_register_subject(NULL, "printer_connection_message", &printer_connection_message_);
        lv_xml_register_subject(NULL, "network_status", &network_status_);
        lv_xml_register_subject(NULL, "klippy_state", &klippy_state_);
        lv_xml_register_subject(NULL, "led_state", &led_state_);
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
        lv_xml_register_subject(NULL, "printer_bed_moves", &printer_bed_moves_);
        lv_xml_register_subject(NULL, "manual_probe_active", &manual_probe_active_);
        lv_xml_register_subject(NULL, "manual_probe_z_position", &manual_probe_z_position_);
        lv_xml_register_subject(NULL, "klipper_version", &klipper_version_);
        lv_xml_register_subject(NULL, "moonraker_version", &moonraker_version_);
    } else {
        spdlog::debug("[PrinterState] Skipping XML registration (tests mode)");
    }

    subjects_initialized_ = true;
    spdlog::debug("Printer state subjects initialized and registered successfully");
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
    auto params = notification["params"];
    if (params.is_array() && !params.empty()) {
        update_from_status(params[0]);
    }
}

void PrinterState::update_from_status(const json& state) {
    std::lock_guard<std::mutex> lock(state_mutex_);

    // Update extruder temperature (stored as centidegrees for 0.1°C resolution)
    if (state.contains("extruder")) {
        const auto& extruder = state["extruder"];

        if (extruder.contains("temperature")) {
            int temp_centi = static_cast<int>(extruder["temperature"].get<double>() * 10.0);
            lv_subject_set_int(&extruder_temp_, temp_centi);
            // Always notify for temp graphing even when value unchanged
            lv_subject_notify(&extruder_temp_);
        }

        if (extruder.contains("target")) {
            int target_centi = static_cast<int>(extruder["target"].get<double>() * 10.0);
            lv_subject_set_int(&extruder_target_, target_centi);
        }
    }

    // Update bed temperature (stored as centidegrees for 0.1°C resolution)
    if (state.contains("heater_bed")) {
        const auto& bed = state["heater_bed"];

        if (bed.contains("temperature")) {
            int temp_centi = static_cast<int>(bed["temperature"].get<double>() * 10.0);
            lv_subject_set_int(&bed_temp_, temp_centi);
            // Always notify for temp graphing even when value unchanged
            lv_subject_notify(&bed_temp_);
            spdlog::trace("[PrinterState] Bed temp: {}.{}°C", temp_centi / 10, temp_centi % 10);
        }

        if (bed.contains("target")) {
            int target_centi = static_cast<int>(bed["target"].get<double>() * 10.0);
            lv_subject_set_int(&bed_target_, target_centi);
            spdlog::trace("[PrinterState] Bed target: {}.{}°C", target_centi / 10,
                          target_centi % 10);
        }
    }

    // Update print progress
    if (state.contains("virtual_sdcard")) {
        const auto& sdcard = state["virtual_sdcard"];

        if (sdcard.contains("progress")) {
            double progress = sdcard["progress"].get<double>();
            int progress_pct = static_cast<int>(progress * 100.0);
            lv_subject_set_int(&print_progress_, progress_pct);
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
            lv_subject_set_int(&print_state_enum_, static_cast<int>(new_state));
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
            if (pos.size() >= 3) {
                lv_subject_set_int(&position_x_, static_cast<int>(pos[0].get<double>()));
                lv_subject_set_int(&position_y_, static_cast<int>(pos[1].get<double>()));
                lv_subject_set_int(&position_z_, static_cast<int>(pos[2].get<double>()));
            }
        }

        if (toolhead.contains("homed_axes")) {
            std::string axes = toolhead["homed_axes"].get<std::string>();
            lv_subject_copy_string(&homed_axes_, axes.c_str());
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

        if (gcode_move.contains("speed_factor")) {
            double factor = gcode_move["speed_factor"].get<double>();
            int factor_pct = static_cast<int>(factor * 100.0);
            lv_subject_set_int(&speed_factor_, factor_pct);
        }

        if (gcode_move.contains("extrude_factor")) {
            double factor = gcode_move["extrude_factor"].get<double>();
            int factor_pct = static_cast<int>(factor * 100.0);
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

        if (fan.contains("speed")) {
            double speed = fan["speed"].get<double>();
            int speed_pct = static_cast<int>(speed * 100.0);
            lv_subject_set_int(&fan_speed_, speed_pct);

            // Also update multi-fan tracking
            update_fan_speed("fan", speed);
        }
    }

    // Check for other fan types in the status update
    // Moonraker sends fan objects as top-level keys: "heater_fan hotend_fan", "fan_generic xyz"
    for (const auto& [key, value] : state.items()) {
        // Skip non-fan objects
        if (key.rfind("heater_fan ", 0) == 0 || key.rfind("fan_generic ", 0) == 0 ||
            key.rfind("controller_fan ", 0) == 0) {
            if (value.is_object() && value.contains("speed")) {
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
            if (first_led.is_array() && first_led.size() >= 3) {
                double r = first_led[0].get<double>();
                double g = first_led[1].get<double>();
                double b = first_led[2].get<double>();
                double w = (first_led.size() >= 4) ? first_led[3].get<double>() : 0.0;

                // LED is "on" if any color component is non-zero
                bool is_on = (r > 0.001 || g > 0.001 || b > 0.001 || w > 0.001);
                int new_state = is_on ? 1 : 0;

                int old_state = lv_subject_get_int(&led_state_);
                if (new_state != old_state) {
                    lv_subject_set_int(&led_state_, new_state);
                    spdlog::debug(
                        "[PrinterState] LED {} state: {} (R={:.2f} G={:.2f} B={:.2f} W={:.2f})",
                        tracked_led_name_, is_on ? "ON" : "OFF", r, g, b, w);
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
        if (webhooks.contains("state")) {
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

        if (mp.contains("is_active")) {
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

    // Cache full state for complex queries
    json_state_.merge_patch(state);
}

json& PrinterState::get_json_state() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return json_state_;
}

// ============================================================================
// MULTI-FAN TRACKING
// ============================================================================

namespace {
/**
 * @brief Convert Moonraker fan object name to human-readable display name
 *
 * Examples:
 * - "fan" -> "Part Cooling"
 * - "heater_fan hotend_fan" -> "Hotend Fan"
 * - "fan_generic nevermore" -> "Nevermore"
 * - "controller_fan electronics_fan" -> "Electronics Fan"
 */
std::string fan_object_to_display_name(const std::string& object_name) {
    // Part cooling fan is just "fan"
    if (object_name == "fan") {
        return "Part Cooling";
    }

    // Extract the suffix after the type prefix
    // "heater_fan hotend_fan" -> "hotend_fan" -> "Hotend Fan"
    std::string suffix;
    size_t space_pos = object_name.find(' ');
    if (space_pos != std::string::npos && space_pos + 1 < object_name.length()) {
        suffix = object_name.substr(space_pos + 1);
    } else {
        suffix = object_name;
    }

    // Convert snake_case to Title Case
    std::string display;
    bool capitalize_next = true;
    for (char c : suffix) {
        if (c == '_') {
            display += ' ';
            capitalize_next = true;
        } else if (capitalize_next) {
            display += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            capitalize_next = false;
        } else {
            display += c;
        }
    }
    return display;
}

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
    fans_.clear();
    fans_.reserve(fan_objects.size());

    for (const auto& obj_name : fan_objects) {
        FanInfo info;
        info.object_name = obj_name;
        info.display_name = fan_object_to_display_name(obj_name);
        info.type = classify_fan_type(obj_name);
        info.is_controllable = is_fan_controllable(info.type);
        info.speed_percent = 0;

        spdlog::debug("[PrinterState] Registered fan: {} -> \"{}\" (type={}, controllable={})",
                      obj_name, info.display_name, static_cast<int>(info.type),
                      info.is_controllable);
        fans_.push_back(std::move(info));
    }

    // Initialize and bump version to notify UI
    lv_subject_set_int(&fans_version_, lv_subject_get_int(&fans_version_) + 1);
    spdlog::info("[PrinterState] Initialized {} fans (version {})", fans_.size(),
                 lv_subject_get_int(&fans_version_));
}

void PrinterState::update_fan_speed(const std::string& object_name, double speed) {
    int speed_pct = static_cast<int>(speed * 100.0);

    for (auto& fan : fans_) {
        if (fan.object_name == object_name) {
            if (fan.speed_percent != speed_pct) {
                fan.speed_percent = speed_pct;
                // Bump version to notify UI of speed change
                lv_subject_set_int(&fans_version_, lv_subject_get_int(&fans_version_) + 1);
            }
            return;
        }
    }
    // Fan not in list - this is normal during initial status before discovery
}

void PrinterState::set_printer_connection_state(int state, const char* message) {
    spdlog::info("[PrinterState] Printer connection state changed: {} - {}", state, message);

    // Track if we've ever successfully connected (state 2 = CONNECTED)
    if (state == 2 && !was_ever_connected_) {
        was_ever_connected_ = true;
        spdlog::debug("[PrinterState] First successful connection - was_ever_connected_ = true");
    }

    spdlog::debug("[PrinterState] Setting printer_connection_state_ subject (at {}) to value {}",
                  (void*)&printer_connection_state_, state);
    lv_subject_set_int(&printer_connection_state_, state);
    spdlog::debug("[PrinterState] Subject value now: {}",
                  lv_subject_get_int(&printer_connection_state_));
    lv_subject_copy_string(&printer_connection_message_, message);
    spdlog::debug(
        "[PrinterState] Printer connection state update complete, observers should be notified");
}

void PrinterState::set_network_status(int status) {
    spdlog::debug("[PrinterState] Network status changed: {}", status);
    lv_subject_set_int(&network_status_, status);
}

void PrinterState::set_klippy_state(KlippyState state) {
    const char* state_names[] = {"READY", "STARTUP", "SHUTDOWN", "ERROR"};
    int state_int = static_cast<int>(state);
    spdlog::info("[PrinterState] Klippy state changed: {} ({})", state_names[state_int], state_int);
    lv_subject_set_int(&klippy_state_, state_int);
}

void PrinterState::set_tracked_led(const std::string& led_name) {
    tracked_led_name_ = led_name;
    if (!led_name.empty()) {
        spdlog::info("[PrinterState] Tracking LED: {}", led_name);
    } else {
        spdlog::debug("[PrinterState] LED tracking disabled");
    }
}

void PrinterState::set_printer_capabilities(const PrinterCapabilities& caps) {
    // Pass auto-detected capabilities to the override layer
    capability_overrides_.set_printer_capabilities(caps);

    // Update subjects using effective values (auto-detect + user overrides)
    // This allows users to force-enable features that weren't detected
    // (e.g., heat soak macro without chamber heater) or force-disable
    // features they don't want to see in the UI.
    lv_subject_set_int(&printer_has_qgl_, capability_overrides_.has_qgl() ? 1 : 0);
    lv_subject_set_int(&printer_has_z_tilt_, capability_overrides_.has_z_tilt() ? 1 : 0);
    lv_subject_set_int(&printer_has_bed_mesh_, capability_overrides_.has_bed_leveling() ? 1 : 0);
    lv_subject_set_int(&printer_has_nozzle_clean_,
                       capability_overrides_.has_nozzle_clean() ? 1 : 0);

    // Hardware capabilities (no user override support yet - set directly from detection)
    lv_subject_set_int(&printer_has_probe_, caps.has_probe() ? 1 : 0);
    lv_subject_set_int(&printer_has_heater_bed_, caps.has_heater_bed() ? 1 : 0);
    lv_subject_set_int(&printer_has_led_, caps.has_led() ? 1 : 0);
    lv_subject_set_int(&printer_has_accelerometer_, caps.has_accelerometer() ? 1 : 0);

    // Speaker capability (with test mode override - always show in test mode for UI testing)
    bool has_speaker = caps.has_speaker() || get_runtime_config().is_test_mode();
    lv_subject_set_int(&printer_has_speaker_, has_speaker ? 1 : 0);

    // Timelapse capability (Moonraker-Timelapse plugin)
    lv_subject_set_int(&printer_has_timelapse_, caps.has_timelapse() ? 1 : 0);

    // Spoolman requires async check - default to 0, updated separately
    // TODO: Add set_spoolman_available() method when Spoolman API is implemented

    spdlog::info("[PrinterState] Capabilities set: probe={}, heater_bed={}, LED={}, "
                 "accelerometer={}, speaker={}, timelapse={}",
                 caps.has_probe(), caps.has_heater_bed(), caps.has_led(), caps.has_accelerometer(),
                 has_speaker, caps.has_timelapse());
    spdlog::info("[PrinterState] Capabilities set (with overrides): {}",
                 capability_overrides_.summary());
}

void PrinterState::set_klipper_version(const std::string& version) {
    lv_subject_copy_string(&klipper_version_, version.c_str());
    spdlog::debug("[PrinterState] Klipper version set: {}", version);
}

void PrinterState::set_moonraker_version(const std::string& version) {
    lv_subject_copy_string(&moonraker_version_, version.c_str());
    spdlog::debug("[PrinterState] Moonraker version set: {}", version);
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
    // - CoreXY/CoreXZ: bed typically moves on Z axis (Voron 2.4, RatRig, etc.)
    // - Cartesian: gantry typically moves on Z axis (Ender 3, Prusa MK3, etc.)
    // - Delta: effector moves on Z, bed is stationary
    //
    // Note: This is a heuristic. Some CoreXY printers (Voron Trident) have gantry-Z.
    // For perfect accuracy, we'd need to parse stepper_z configuration.
    bool bed_moves_z = (kinematics.find("corexy") != std::string::npos ||
                        kinematics.find("corexz") != std::string::npos ||
                        kinematics.find("hybrid_corexy") != std::string::npos);
    int new_value = bed_moves_z ? 1 : 0;

    // Only log when value actually changes (this gets called frequently from status updates)
    if (lv_subject_get_int(&printer_bed_moves_) != new_value) {
        lv_subject_set_int(&printer_bed_moves_, new_value);
        spdlog::info("[PrinterState] Kinematics: {} -> bed_moves_z={}", kinematics, bed_moves_z);
    }
}
