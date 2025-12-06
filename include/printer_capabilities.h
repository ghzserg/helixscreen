// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "hv/json.hpp"

using json = nlohmann::json;

/**
 * @brief Detected printer hardware and macro capabilities
 *
 * Populated from Klipper's printer.objects.list response during discovery.
 * Used to determine which pre-print options are available for the connected printer.
 *
 * Thread-safe for read access after initial population.
 *
 * @code
 * PrinterCapabilities caps;
 * caps.parse_objects(objects_array);
 *
 * if (caps.has_qgl()) {
 *     // Show QGL toggle in options
 * }
 * @endcode
 */
class PrinterCapabilities {
  public:
    PrinterCapabilities() = default;

    // Non-copyable, movable (contains unordered_set)
    PrinterCapabilities(const PrinterCapabilities&) = default;
    PrinterCapabilities& operator=(const PrinterCapabilities&) = default;
    PrinterCapabilities(PrinterCapabilities&&) = default;
    PrinterCapabilities& operator=(PrinterCapabilities&&) = default;
    ~PrinterCapabilities() = default;

    /**
     * @brief Parse Klipper objects from printer.objects.list response
     *
     * Extracts hardware capabilities (QGL, Z-tilt, bed mesh, chamber)
     * and available macros from the object list.
     *
     * @param objects JSON array of object names from printer.objects.list
     */
    void parse_objects(const json& objects);

    /**
     * @brief Reset all capabilities to undetected state
     */
    void clear();

    // ========================================================================
    // Hardware Capabilities
    // ========================================================================

    /**
     * @brief Check if printer has quad gantry leveling
     * @return true if quad_gantry_level object was detected
     */
    [[nodiscard]] bool has_qgl() const {
        return has_qgl_;
    }

    /**
     * @brief Check if printer has Z-tilt adjustment
     * @return true if z_tilt object was detected
     */
    [[nodiscard]] bool has_z_tilt() const {
        return has_z_tilt_;
    }

    /**
     * @brief Check if printer has bed mesh capability
     * @return true if bed_mesh object was detected
     */
    [[nodiscard]] bool has_bed_mesh() const {
        return has_bed_mesh_;
    }

    /**
     * @brief Check if printer has a chamber heater
     * @return true if heater_generic with "chamber" in name was detected
     */
    [[nodiscard]] bool has_chamber_heater() const {
        return has_chamber_heater_;
    }

    /**
     * @brief Check if printer has a chamber temperature sensor
     * @return true if temperature_sensor with "chamber" in name was detected
     */
    [[nodiscard]] bool has_chamber_sensor() const {
        return has_chamber_sensor_;
    }

    /**
     * @brief Check if printer has exclude_object support
     * @return true if exclude_object object was detected (Klipper config has [exclude_object])
     */
    [[nodiscard]] bool has_exclude_object() const {
        return has_exclude_object_;
    }

    /**
     * @brief Check if printer has a probe (for Z-offset calibration)
     * @return true if probe or bltouch object was detected
     */
    [[nodiscard]] bool has_probe() const {
        return has_probe_;
    }

    /**
     * @brief Check if printer has a heated bed
     * @return true if heater_bed object was detected
     */
    [[nodiscard]] bool has_heater_bed() const {
        return has_heater_bed_;
    }

    /**
     * @brief Check if printer has LED/light control
     * @return true if neopixel, led, or output_pin with light/led in name was detected
     */
    [[nodiscard]] bool has_led() const {
        return has_led_;
    }

    /**
     * @brief Check if printer has an accelerometer for input shaping
     * @return true if adxl345, lis2dw, mpu9250, or resonance_tester was detected
     */
    [[nodiscard]] bool has_accelerometer() const {
        return has_accelerometer_;
    }

    /**
     * @brief Check if printer has screws_tilt_adjust for manual bed leveling
     * @return true if screws_tilt_adjust object was detected
     */
    [[nodiscard]] bool has_screws_tilt() const {
        return has_screws_tilt_;
    }

    /**
     * @brief Check if Klippain Shake&Tune is installed
     *
     * Detects the AXES_SHAPER_CALIBRATION macro which is part of Klippain's
     * Shake&Tune plugin for enhanced input shaper calibration.
     *
     * @return true if AXES_SHAPER_CALIBRATION macro was detected
     */
    [[nodiscard]] bool has_klippain_shaketune() const {
        return has_klippain_shaketune_;
    }

    /**
     * @brief Check if printer has a speaker/buzzer for audio feedback
     *
     * Detects output_pin objects with beeper/buzzer/speaker in the name,
     * which are commonly used for M300 tone generation.
     *
     * @return true if speaker/buzzer output pin was detected
     */
    [[nodiscard]] bool has_speaker() const {
        return has_speaker_;
    }

    /**
     * @brief Check if printer supports any form of bed leveling
     * @return true if has QGL, Z-tilt, or bed mesh
     */
    [[nodiscard]] bool supports_leveling() const {
        return has_qgl_ || has_z_tilt_ || has_bed_mesh_;
    }

    /**
     * @brief Check if printer supports chamber temperature control/monitoring
     * @return true if has chamber heater or sensor
     */
    [[nodiscard]] bool supports_chamber() const {
        return has_chamber_heater_ || has_chamber_sensor_;
    }

    // ========================================================================
    // Macro Capabilities
    // ========================================================================

    /**
     * @brief Get all detected G-code macros
     * @return Set of macro names (without "gcode_macro " prefix)
     */
    [[nodiscard]] const std::unordered_set<std::string>& macros() const {
        return macros_;
    }

    /**
     * @brief Get detected HelixScreen helper macros
     * @return Set of HELIX_* macro names
     */
    [[nodiscard]] const std::unordered_set<std::string>& helix_macros() const {
        return helix_macros_;
    }

    /**
     * @brief Check if a specific macro exists
     * @param macro_name Macro name (case-insensitive)
     * @return true if macro was detected
     */
    [[nodiscard]] bool has_macro(const std::string& macro_name) const;

    /**
     * @brief Check if HelixScreen helper macros are installed
     * @return true if any HELIX_* macros were detected
     */
    [[nodiscard]] bool has_helix_macros() const {
        return !helix_macros_.empty();
    }

    /**
     * @brief Check if a specific HelixScreen helper macro exists
     * @param macro_name Full macro name (e.g., "HELIX_BED_LEVEL_IF_NEEDED")
     * @return true if macro was detected
     */
    [[nodiscard]] bool has_helix_macro(const std::string& macro_name) const;

    // ========================================================================
    // Common Macro Detection
    // ========================================================================

    /**
     * @brief Check if printer has a nozzle cleaning macro
     *
     * Looks for common names: CLEAN_NOZZLE, NOZZLE_WIPE, WIPE_NOZZLE, PURGE_NOZZLE
     *
     * @return true if any nozzle cleaning macro was detected
     */
    [[nodiscard]] bool has_nozzle_clean_macro() const;

    /**
     * @brief Check if printer has a purge line macro
     *
     * Looks for common names: PURGE_LINE, PRIME_LINE, INTRO_LINE
     *
     * @return true if any purge line macro was detected
     */
    [[nodiscard]] bool has_purge_line_macro() const;

    /**
     * @brief Check if printer has a heat soak macro
     *
     * Looks for common names: HEAT_SOAK, CHAMBER_SOAK, SOAK
     *
     * @return true if any heat soak macro was detected
     */
    [[nodiscard]] bool has_heat_soak_macro() const;

    /**
     * @brief Get the detected nozzle cleaning macro name
     * @return Macro name if found, empty string otherwise
     */
    [[nodiscard]] std::string get_nozzle_clean_macro() const;

    /**
     * @brief Get the detected purge line macro name
     * @return Macro name if found, empty string otherwise
     */
    [[nodiscard]] std::string get_purge_line_macro() const;

    /**
     * @brief Get the detected heat soak macro name
     * @return Macro name if found, empty string otherwise
     */
    [[nodiscard]] std::string get_heat_soak_macro() const;

    // ========================================================================
    // Statistics
    // ========================================================================

    /**
     * @brief Get total number of detected macros
     */
    [[nodiscard]] size_t macro_count() const {
        return macros_.size();
    }

    /**
     * @brief Get summary string for logging
     */
    [[nodiscard]] std::string summary() const;

  private:
    // Hardware capabilities
    bool has_qgl_ = false;
    bool has_z_tilt_ = false;
    bool has_bed_mesh_ = false;
    bool has_chamber_heater_ = false;
    bool has_chamber_sensor_ = false;
    bool has_exclude_object_ = false;
    bool has_probe_ = false;
    bool has_heater_bed_ = false;
    bool has_led_ = false;
    bool has_accelerometer_ = false;
    bool has_screws_tilt_ = false;
    bool has_klippain_shaketune_ = false;
    bool has_speaker_ = false;

    // Macro names (stored uppercase for case-insensitive matching)
    std::unordered_set<std::string> macros_;
    std::unordered_set<std::string> helix_macros_;

    // Detected common macros (cached for quick access)
    std::string nozzle_clean_macro_;
    std::string purge_line_macro_;
    std::string heat_soak_macro_;

    /**
     * @brief Convert string to uppercase for comparison
     */
    static std::string to_upper(const std::string& str);

    /**
     * @brief Check if name matches any of the patterns
     */
    static bool matches_any(const std::string& name, const std::vector<std::string>& patterns);
};
