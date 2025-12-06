// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "printer_capabilities.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <cctype>
#include <sstream>

// ============================================================================
// Parsing
// ============================================================================

void PrinterCapabilities::parse_objects(const json& objects) {
    clear();

    for (const auto& obj : objects) {
        std::string name = obj.template get<std::string>();
        std::string upper_name = to_upper(name);

        // Hardware detection
        if (name == "quad_gantry_level") {
            has_qgl_ = true;
            spdlog::debug("[PrinterCapabilities] Detected quad_gantry_level");
        } else if (name == "z_tilt") {
            has_z_tilt_ = true;
            spdlog::debug("[PrinterCapabilities] Detected z_tilt");
        } else if (name == "bed_mesh") {
            has_bed_mesh_ = true;
            spdlog::debug("[PrinterCapabilities] Detected bed_mesh");
        } else if (name == "exclude_object") {
            has_exclude_object_ = true;
            spdlog::debug("[PrinterCapabilities] Detected exclude_object");
        } else if (name == "probe" || name == "bltouch") {
            // Standard probe or BLTouch (includes Voron TAP which uses [probe])
            has_probe_ = true;
            spdlog::debug("[PrinterCapabilities] Detected probe: {}", name);
        } else if (name.rfind("probe_eddy_current ", 0) == 0) {
            // Eddy current probes: BTT Eddy, Beacon, Cartographer, etc.
            has_probe_ = true;
            spdlog::debug("[PrinterCapabilities] Detected eddy probe: {}", name);
        } else if (name == "heater_bed") {
            has_heater_bed_ = true;
            spdlog::debug("[PrinterCapabilities] Detected heater_bed");
        } else if (name == "screws_tilt_adjust") {
            has_screws_tilt_ = true;
            spdlog::debug("[PrinterCapabilities] Detected screws_tilt_adjust");
        }
        // Accelerometer detection for input shaping
        else if (name == "adxl345" || name.rfind("adxl345 ", 0) == 0 || name == "lis2dw" ||
                 name.rfind("lis2dw ", 0) == 0 || name == "mpu9250" ||
                 name.rfind("mpu9250 ", 0) == 0 || name == "resonance_tester") {
            has_accelerometer_ = true;
            spdlog::debug("[PrinterCapabilities] Detected accelerometer: {}", name);
        }
        // LED/light detection (neopixel, led, or output_pin with light/led in name)
        else if (name.rfind("neopixel ", 0) == 0 || name.rfind("led ", 0) == 0) {
            has_led_ = true;
            spdlog::debug("[PrinterCapabilities] Detected LED: {}", name);
        } else if (name.rfind("output_pin ", 0) == 0) {
            std::string pin_name = name.substr(11); // Remove "output_pin " prefix
            std::string upper_pin = to_upper(pin_name);
            if (upper_pin.find("LIGHT") != std::string::npos ||
                upper_pin.find("LED") != std::string::npos ||
                upper_pin.find("LAMP") != std::string::npos) {
                has_led_ = true;
                spdlog::debug("[PrinterCapabilities] Detected LED output pin: {}", name);
            }
            // Speaker/buzzer detection for M300 support
            if (upper_pin.find("BEEPER") != std::string::npos ||
                upper_pin.find("BUZZER") != std::string::npos ||
                upper_pin.find("SPEAKER") != std::string::npos) {
                has_speaker_ = true;
                spdlog::debug("[PrinterCapabilities] Detected speaker output pin: {}", name);
            }
        }
        // Chamber heater detection (heater_generic with "chamber" in name)
        else if (name.rfind("heater_generic ", 0) == 0) {
            std::string heater_name = name.substr(15); // Remove "heater_generic " prefix
            if (to_upper(heater_name).find("CHAMBER") != std::string::npos) {
                has_chamber_heater_ = true;
                spdlog::debug("[PrinterCapabilities] Detected chamber heater: {}", name);
            }
        }
        // Chamber sensor detection
        else if (name.rfind("temperature_sensor ", 0) == 0) {
            std::string sensor_name = name.substr(19); // Remove "temperature_sensor " prefix
            if (to_upper(sensor_name).find("CHAMBER") != std::string::npos) {
                has_chamber_sensor_ = true;
                spdlog::debug("[PrinterCapabilities] Detected chamber sensor: {}", name);
            }
        }
        // Macro detection
        else if (name.rfind("gcode_macro ", 0) == 0) {
            std::string macro_name = name.substr(12); // Remove "gcode_macro " prefix
            std::string upper_macro = to_upper(macro_name);

            macros_.insert(upper_macro);

            // Check for HelixScreen helper macros
            if (upper_macro.rfind("HELIX_", 0) == 0) {
                helix_macros_.insert(upper_macro);
                spdlog::debug("[PrinterCapabilities] Detected HelixScreen macro: {}", macro_name);
            }

            // Check for Klippain Shake&Tune
            if (upper_macro == "AXES_SHAPER_CALIBRATION") {
                has_klippain_shaketune_ = true;
                spdlog::debug("[PrinterCapabilities] Detected Klippain Shake&Tune");
            }

            // Check for common macro patterns and cache them
            if (nozzle_clean_macro_.empty()) {
                static const std::vector<std::string> nozzle_patterns = {
                    "CLEAN_NOZZLE", "NOZZLE_WIPE", "WIPE_NOZZLE", "PURGE_NOZZLE", "NOZZLE_CLEAN"};
                if (matches_any(upper_macro, nozzle_patterns)) {
                    nozzle_clean_macro_ = macro_name;
                    spdlog::debug("[PrinterCapabilities] Detected nozzle clean macro: {}",
                                  macro_name);
                }
            }

            if (purge_line_macro_.empty()) {
                static const std::vector<std::string> purge_patterns = {"PURGE_LINE", "PRIME_LINE",
                                                                        "INTRO_LINE", "LINE_PURGE"};
                if (matches_any(upper_macro, purge_patterns)) {
                    purge_line_macro_ = macro_name;
                    spdlog::debug("[PrinterCapabilities] Detected purge line macro: {}",
                                  macro_name);
                }
            }

            if (heat_soak_macro_.empty()) {
                static const std::vector<std::string> soak_patterns = {"HEAT_SOAK", "CHAMBER_SOAK",
                                                                       "SOAK", "BED_SOAK"};
                if (matches_any(upper_macro, soak_patterns)) {
                    heat_soak_macro_ = macro_name;
                    spdlog::debug("[PrinterCapabilities] Detected heat soak macro: {}", macro_name);
                }
            }
        }
    }

    spdlog::info("[PrinterCapabilities] {}", summary());
}

void PrinterCapabilities::clear() {
    has_qgl_ = false;
    has_z_tilt_ = false;
    has_bed_mesh_ = false;
    has_chamber_heater_ = false;
    has_chamber_sensor_ = false;
    has_exclude_object_ = false;
    has_probe_ = false;
    has_heater_bed_ = false;
    has_led_ = false;
    has_accelerometer_ = false;
    has_screws_tilt_ = false;
    has_klippain_shaketune_ = false;
    has_speaker_ = false;
    macros_.clear();
    helix_macros_.clear();
    nozzle_clean_macro_.clear();
    purge_line_macro_.clear();
    heat_soak_macro_.clear();
}

// ============================================================================
// Macro Queries
// ============================================================================

bool PrinterCapabilities::has_macro(const std::string& macro_name) const {
    return macros_.count(to_upper(macro_name)) > 0;
}

bool PrinterCapabilities::has_helix_macro(const std::string& macro_name) const {
    return helix_macros_.count(to_upper(macro_name)) > 0;
}

bool PrinterCapabilities::has_nozzle_clean_macro() const {
    return !nozzle_clean_macro_.empty();
}

bool PrinterCapabilities::has_purge_line_macro() const {
    return !purge_line_macro_.empty();
}

bool PrinterCapabilities::has_heat_soak_macro() const {
    return !heat_soak_macro_.empty();
}

std::string PrinterCapabilities::get_nozzle_clean_macro() const {
    return nozzle_clean_macro_;
}

std::string PrinterCapabilities::get_purge_line_macro() const {
    return purge_line_macro_;
}

std::string PrinterCapabilities::get_heat_soak_macro() const {
    return heat_soak_macro_;
}

// ============================================================================
// Summary
// ============================================================================

std::string PrinterCapabilities::summary() const {
    std::ostringstream ss;
    ss << "Capabilities: ";

    std::vector<std::string> caps;

    if (has_qgl_)
        caps.push_back("QGL");
    if (has_z_tilt_)
        caps.push_back("Z-tilt");
    if (has_bed_mesh_)
        caps.push_back("bed_mesh");
    if (has_chamber_heater_)
        caps.push_back("chamber_heater");
    if (has_chamber_sensor_)
        caps.push_back("chamber_sensor");
    if (has_exclude_object_)
        caps.push_back("exclude_object");
    if (has_probe_)
        caps.push_back("probe");
    if (has_heater_bed_)
        caps.push_back("heater_bed");
    if (has_led_)
        caps.push_back("LED");
    if (has_accelerometer_)
        caps.push_back("accelerometer");
    if (has_screws_tilt_)
        caps.push_back("screws_tilt");
    if (has_klippain_shaketune_)
        caps.push_back("Klippain");
    if (has_speaker_)
        caps.push_back("speaker");

    if (caps.empty()) {
        ss << "none";
    } else {
        for (size_t i = 0; i < caps.size(); ++i) {
            if (i > 0)
                ss << ", ";
            ss << caps[i];
        }
    }

    ss << " | " << macros_.size() << " macros";
    if (!helix_macros_.empty()) {
        ss << " (" << helix_macros_.size() << " HELIX_*)";
    }

    return ss.str();
}

// ============================================================================
// Helpers
// ============================================================================

std::string PrinterCapabilities::to_upper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

bool PrinterCapabilities::matches_any(const std::string& name,
                                      const std::vector<std::string>& patterns) {
    for (const auto& pattern : patterns) {
        if (name == pattern) {
            return true;
        }
    }
    return false;
}
