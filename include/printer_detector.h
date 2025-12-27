// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <string>
#include <vector>

/**
 * @brief Printer auto-detection result with confidence and reasoning
 */
struct PrinterDetectionResult {
    std::string type_name; ///< Printer type name (e.g., "FlashForge AD5M Pro", "Voron 2.4")
    int confidence;        ///< Confidence score 0-100 (≥70 = high confidence, <70 = low confidence)
    std::string reason;    ///< Human-readable detection reasoning

    /**
     * @brief Check if detection succeeded
     * @return true if confidence > 0, false otherwise
     */
    bool detected() const {
        return confidence > 0;
    }
};

/**
 * @brief Build volume dimensions from bed_mesh configuration
 */
struct BuildVolume {
    float x_min = 0.0f;
    float x_max = 0.0f;
    float y_min = 0.0f;
    float y_max = 0.0f;
    float z_max = 0.0f; ///< Maximum Z height (if available)
};

/**
 * @brief A single PRINT_START parameter capability
 *
 * Maps a capability (e.g., "bed_leveling") to the native param name and values.
 */
struct PrintStartParamCapability {
    std::string param;         ///< Native param name (e.g., "FORCE_LEVELING")
    std::string skip_value;    ///< Value to skip/disable (e.g., "false")
    std::string enable_value;  ///< Value to enable/force (e.g., "true")
    std::string default_value; ///< Default value if param not specified
    std::string description;   ///< Human-readable description
};

/**
 * @brief PRINT_START capabilities for a printer
 *
 * Contains the macro name and all supported skip/control parameters.
 */
struct PrintStartCapabilities {
    std::string macro_name; ///< Macro name (e.g., "START_PRINT", "PRINT_START")
    std::map<std::string, PrintStartParamCapability>
        params; ///< Map of capability name to param info

    bool empty() const {
        return macro_name.empty() && params.empty();
    }

    bool has_capability(const std::string& name) const {
        return params.find(name) != params.end();
    }

    const PrintStartParamCapability* get_capability(const std::string& name) const {
        auto it = params.find(name);
        return (it != params.end()) ? &it->second : nullptr;
    }
};

/**
 * @brief Printer hardware discovery data
 *
 * Aggregates hardware information from Moonraker for detection analysis.
 */
struct PrinterHardwareData {
    std::vector<std::string> heaters{};         ///< Controllable heaters (extruders, bed, etc.)
    std::vector<std::string> sensors{};         ///< Read-only temperature sensors
    std::vector<std::string> fans{};            ///< All fan types
    std::vector<std::string> leds{};            ///< LED outputs
    std::string hostname{};                     ///< Printer hostname from printer.info
    std::vector<std::string> printer_objects{}; ///< Full list of Klipper objects from objects/list
    std::vector<std::string> steppers{}; ///< Stepper motor names (stepper_x, stepper_z, etc.)
    std::string kinematics{};            ///< Kinematics type (corexy, cartesian, delta, etc.)
    std::string mcu{};                   ///< Primary MCU chip type (e.g., "stm32h723xx", "rp2040")
    std::vector<std::string> mcu_list{}; ///< All MCU chips (primary + secondary, CAN toolheads)
    BuildVolume build_volume{};          ///< Build volume dimensions from bed_mesh
};

/**
 * @brief Printer auto-detection using hardware fingerprints
 *
 * Data-driven printer detection system that loads heuristics from JSON database.
 * Analyzes hardware discovery data to identify printer models based on
 * distinctive patterns found in real printers (FlashForge AD5M Pro, Voron V2, etc.).
 *
 * This class is completely independent of UI code and printer type lists.
 * It returns printer type names as strings, which the caller can map to their
 * own data structures (e.g., UI dropdowns, config values).
 *
 * Detection heuristics are defined in config/printer_database.json, allowing
 * new printer types to be added without recompilation.
 *
 * **Contract**: Returned type_name strings should match printer names in
 * PrinterTypes::PRINTER_TYPES_ROLLER for UI integration, but the detector
 * doesn't depend on that list and can be tested independently.
 */
class PrinterDetector {
  public:
    /**
     * @brief Detect printer type from hardware data
     *
     * Loads heuristics from config/printer_database.json and executes pattern matching
     * rules to identify printer model. Supports multiple heuristic types:
     * - sensor_match: Pattern matching on sensors array
     * - fan_match: Pattern matching on fans array
     * - hostname_match: Pattern matching on printer hostname
     * - fan_combo: Multiple fan patterns must all be present
     *
     * Returns the printer with highest confidence match, or empty result if
     * no distinctive fingerprints detected.
     *
     * @param hardware Hardware discovery data from Moonraker
     * @return Detection result with type name, confidence, and reasoning
     */
    static PrinterDetectionResult detect(const PrinterHardwareData& hardware);

    /**
     * @brief Get image filename for a printer type
     *
     * Looks up the image field from the printer database JSON.
     * Returns just the filename (e.g., "voron-24r2.png"), not the full path.
     *
     * @param printer_name Printer name (e.g., "Voron 2.4", "FlashForge Adventurer 5M")
     * @return Image filename if found, empty string if not found
     */
    static std::string get_image_for_printer(const std::string& printer_name);

    /**
     * @brief Get image filename for a printer by ID
     *
     * Looks up the image field from the printer database JSON using the printer ID.
     * Returns just the filename (e.g., "voron-24r2.png"), not the full path.
     *
     * @param printer_id Printer ID (e.g., "voron_2_4", "flashforge_adventurer_5m")
     * @return Image filename if found, empty string if not found
     */
    static std::string get_image_for_printer_id(const std::string& printer_id);

    /**
     * @brief Build roller options string from database
     *
     * Dynamically builds a newline-separated string of printer names suitable
     * for LVGL roller widget. Only includes entries with `show_in_roller: true`
     * (defaults to true if field is missing). Always appends "Custom/Other"
     * and "Unknown" at the end.
     *
     * The string is cached after first build for performance.
     *
     * @return Newline-separated printer names for lv_roller_set_options()
     */
    static const std::string& get_roller_options();

    /**
     * @brief Get list of printer names from database
     *
     * Returns a vector of all printer names that should appear in the roller.
     * Useful for index lookups and iteration.
     *
     * @return Vector of printer names (includes Custom/Other and Unknown)
     */
    static const std::vector<std::string>& get_roller_names();

    /**
     * @brief Find index of a printer name in the roller
     *
     * @param printer_name Name to search for
     * @return Index if found, or index of "Unknown" if not found
     */
    static int find_roller_index(const std::string& printer_name);

    /**
     * @brief Get printer name at roller index
     *
     * @param index Roller index (0-based)
     * @return Printer name, or "Unknown" if index out of bounds
     */
    static std::string get_roller_name_at(int index);

    /**
     * @brief Get the index of "Unknown" in the roller
     *
     * @return Index of the Unknown entry (last entry)
     */
    static int get_unknown_index();

    /**
     * @brief Get PRINT_START capabilities for a printer
     *
     * Looks up the print_start_capabilities field from the printer database JSON
     * for the specified printer. This contains native macro parameters that can
     * control pre-print operations (skip bed leveling, etc.) without file modification.
     *
     * @param printer_name Printer name (e.g., "FlashForge Adventurer 5M Pro")
     * @return Capabilities struct, or empty struct if not found
     */
    static PrintStartCapabilities get_print_start_capabilities(const std::string& printer_name);
};
