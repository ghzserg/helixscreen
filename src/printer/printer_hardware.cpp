// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "printer_hardware.h"

#include <spdlog/spdlog.h>

#include <algorithm>

PrinterHardware::PrinterHardware(const std::vector<std::string>& heaters,
                                 const std::vector<std::string>& sensors,
                                 const std::vector<std::string>& fans,
                                 const std::vector<std::string>& leds)
    : heaters_(heaters), sensors_(sensors), fans_(fans), leds_(leds) {}

// ============================================================================
// Static Helpers
// ============================================================================

bool PrinterHardware::has_exact(const std::vector<std::string>& vec, const std::string& name) {
    return std::find(vec.begin(), vec.end(), name) != vec.end();
}

std::string PrinterHardware::find_containing(const std::vector<std::string>& vec,
                                             const std::string& substring) {
    for (const auto& item : vec) {
        if (item.find(substring) != std::string::npos) {
            return item;
        }
    }
    return "";
}

std::string PrinterHardware::find_not_containing(const std::vector<std::string>& vec,
                                                 const std::vector<std::string>& avoid_substrings) {
    for (const auto& item : vec) {
        bool should_avoid = false;
        for (const auto& avoid : avoid_substrings) {
            if (item.find(avoid) != std::string::npos) {
                should_avoid = true;
                break;
            }
        }
        if (!should_avoid) {
            return item;
        }
    }
    return "";
}

// ============================================================================
// Heater Guessing
// ============================================================================

std::string PrinterHardware::guess_bed_heater() const {
    if (heaters_.empty()) {
        spdlog::debug("[PrinterHardware] guess_bed_heater() -> no heaters discovered");
        return "";
    }

    // Priority 1: Exact match for "heater_bed" - Klipper's canonical name
    if (has_exact(heaters_, "heater_bed")) {
        spdlog::debug("[PrinterHardware] guess_bed_heater() -> 'heater_bed'");
        return "heater_bed";
    }

    // Priority 2: Exact match for "heated_bed"
    if (has_exact(heaters_, "heated_bed")) {
        spdlog::debug("[PrinterHardware] guess_bed_heater() -> 'heated_bed'");
        return "heated_bed";
    }

    // Priority 3: Any heater containing "bed"
    std::string match = find_containing(heaters_, "bed");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_bed_heater() -> '{}'", match);
        return match;
    }

    spdlog::debug("[PrinterHardware] guess_bed_heater() -> no match found");
    return "";
}

std::string PrinterHardware::guess_hotend_heater() const {
    if (heaters_.empty()) {
        spdlog::debug("[PrinterHardware] guess_hotend_heater() -> no heaters discovered");
        return "";
    }

    // Priority 1: Exact match for "extruder" - Klipper's canonical [extruder] section
    if (has_exact(heaters_, "extruder")) {
        spdlog::debug("[PrinterHardware] guess_hotend_heater() -> 'extruder'");
        return "extruder";
    }

    // Priority 2: Exact match for "extruder0"
    if (has_exact(heaters_, "extruder0")) {
        spdlog::debug("[PrinterHardware] guess_hotend_heater() -> 'extruder0'");
        return "extruder0";
    }

    // Priority 3: Any heater containing "extruder"
    std::string match = find_containing(heaters_, "extruder");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_hotend_heater() -> '{}'", match);
        return match;
    }

    // Priority 4: Any heater containing "hotend"
    match = find_containing(heaters_, "hotend");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_hotend_heater() -> '{}'", match);
        return match;
    }

    // Priority 5: Any heater containing "e0"
    match = find_containing(heaters_, "e0");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_hotend_heater() -> '{}'", match);
        return match;
    }

    spdlog::debug("[PrinterHardware] guess_hotend_heater() -> no match found");
    return "";
}

// ============================================================================
// Sensor Guessing
// ============================================================================

std::string PrinterHardware::guess_bed_sensor() const {
    // First check heaters - heaters have built-in sensors
    std::string bed_heater = guess_bed_heater();
    if (!bed_heater.empty()) {
        spdlog::debug("[PrinterHardware] guess_bed_sensor() -> '{}' (from heater)", bed_heater);
        return bed_heater;
    }

    // Search sensors for bed-related names
    std::string match = find_containing(sensors_, "bed");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_bed_sensor() -> '{}'", match);
        return match;
    }

    spdlog::debug("[PrinterHardware] guess_bed_sensor() -> no match found");
    return "";
}

std::string PrinterHardware::guess_hotend_sensor() const {
    // First check heaters - heaters have built-in sensors
    std::string hotend_heater = guess_hotend_heater();
    if (!hotend_heater.empty()) {
        spdlog::debug("[PrinterHardware] guess_hotend_sensor() -> '{}' (from heater)",
                      hotend_heater);
        return hotend_heater;
    }

    // Search sensors for extruder/hotend-related names
    std::string match = find_containing(sensors_, "extruder");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_hotend_sensor() -> '{}'", match);
        return match;
    }

    match = find_containing(sensors_, "hotend");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_hotend_sensor() -> '{}'", match);
        return match;
    }

    match = find_containing(sensors_, "e0");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_hotend_sensor() -> '{}'", match);
        return match;
    }

    spdlog::debug("[PrinterHardware] guess_hotend_sensor() -> no match found");
    return "";
}

// ============================================================================
// Fan Guessing
// ============================================================================

std::string PrinterHardware::guess_hotend_fan() const {
    if (fans_.empty()) {
        spdlog::debug("[PrinterHardware] guess_hotend_fan() -> no fans discovered");
        return "";
    }

    // Priority 1: Exact matches for common heater fan names
    if (has_exact(fans_, "heater_fan hotend_fan")) {
        spdlog::debug("[PrinterHardware] guess_hotend_fan() -> 'heater_fan hotend_fan' (exact)");
        return "heater_fan hotend_fan";
    }
    if (has_exact(fans_, "heater_fan heat_fan")) {
        spdlog::debug("[PrinterHardware] guess_hotend_fan() -> 'heater_fan heat_fan' (exact)");
        return "heater_fan heat_fan";
    }

    // Priority 2: Any fan containing "heater_fan" (Klipper's [heater_fan] section)
    std::string match = find_containing(fans_, "heater_fan");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_hotend_fan() -> '{}' (contains 'heater_fan')",
                      match);
        return match;
    }

    // Priority 3: Any fan containing "hotend_fan"
    match = find_containing(fans_, "hotend_fan");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_hotend_fan() -> '{}' (contains 'hotend_fan')",
                      match);
        return match;
    }

    // Priority 4: Any fan containing "heat_fan" or "heatbreak"
    match = find_containing(fans_, "heat_fan");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_hotend_fan() -> '{}' (contains 'heat_fan')", match);
        return match;
    }
    match = find_containing(fans_, "heatbreak");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_hotend_fan() -> '{}' (contains 'heatbreak')", match);
        return match;
    }

    // No match - hotend fan is required hardware, but not all printers expose it
    spdlog::debug("[PrinterHardware] guess_hotend_fan() -> no match found");
    return "";
}

std::string PrinterHardware::guess_part_cooling_fan() const {
    if (fans_.empty()) {
        spdlog::debug("[PrinterHardware] guess_part_cooling_fan() -> no fans discovered");
        return "";
    }

    // Priority 1: Exact match for "fan" - Klipper's canonical [fan] section
    // This is THE part cooling fan, controlled by M106/M107
    if (has_exact(fans_, "fan")) {
        spdlog::debug("[PrinterHardware] guess_part_cooling_fan() -> 'fan' (canonical)");
        return "fan";
    }

    // Priority 2: Any fan containing "M106" or "m106" - common naming for M106-controlled fans
    // (e.g., "fan_generic fanM106" on FlashForge printers)
    std::string match = find_containing(fans_, "M106");
    if (match.empty()) {
        match = find_containing(fans_, "m106");
    }
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_part_cooling_fan() -> '{}' (contains 'M106')",
                      match);
        return match;
    }

    // Priority 3: Any fan containing "part" (e.g., "fan_generic part_cooling")
    match = find_containing(fans_, "part");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_part_cooling_fan() -> '{}' (contains 'part')",
                      match);
        return match;
    }

    // Fallback: first fan in list (better than nothing)
    spdlog::debug("[PrinterHardware] guess_part_cooling_fan() -> '{}' (fallback)", fans_[0]);
    return fans_[0];
}

std::string PrinterHardware::guess_chamber_fan() const {
    if (fans_.empty()) {
        spdlog::debug("[PrinterHardware] guess_chamber_fan() -> no fans discovered");
        return "";
    }

    // Priority 1: Exact match for "chamber_fan"
    if (has_exact(fans_, "chamber_fan")) {
        spdlog::debug("[PrinterHardware] guess_chamber_fan() -> 'chamber_fan' (exact)");
        return "chamber_fan";
    }

    // Priority 2: Substring priority chain
    // "chamber" - chamber air circulation
    std::string match = find_containing(fans_, "chamber");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_chamber_fan() -> '{}' (contains 'chamber')", match);
        return match;
    }

    // "nevermore" - popular Klipper recirculating filter
    match = find_containing(fans_, "nevermore");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_chamber_fan() -> '{}' (contains 'nevermore')",
                      match);
        return match;
    }

    // "bed_fans" - BTT Pi naming convention
    match = find_containing(fans_, "bed_fans");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_chamber_fan() -> '{}' (contains 'bed_fans')", match);
        return match;
    }

    // "filter" - air filtration
    match = find_containing(fans_, "filter");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_chamber_fan() -> '{}' (contains 'filter')", match);
        return match;
    }

    // No match - chamber fan is optional hardware
    spdlog::debug("[PrinterHardware] guess_chamber_fan() -> no match found (optional)");
    return "";
}

std::string PrinterHardware::guess_exhaust_fan() const {
    if (fans_.empty()) {
        spdlog::debug("[PrinterHardware] guess_exhaust_fan() -> no fans discovered");
        return "";
    }

    // Priority 1: Exact match for "exhaust_fan"
    if (has_exact(fans_, "exhaust_fan")) {
        spdlog::debug("[PrinterHardware] guess_exhaust_fan() -> 'exhaust_fan' (exact)");
        return "exhaust_fan";
    }

    // Priority 2: Substring priority chain
    // "exhaust" - direct exhaust
    std::string match = find_containing(fans_, "exhaust");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_exhaust_fan() -> '{}' (contains 'exhaust')", match);
        return match;
    }

    // "external" - external/outside venting
    match = find_containing(fans_, "external");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_exhaust_fan() -> '{}' (contains 'external')", match);
        return match;
    }

    // "vent" - ventilation
    match = find_containing(fans_, "vent");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_exhaust_fan() -> '{}' (contains 'vent')", match);
        return match;
    }

    // No match - exhaust fan is optional hardware
    spdlog::debug("[PrinterHardware] guess_exhaust_fan() -> no match found (optional)");
    return "";
}

// ============================================================================
// LED Guessing
// ============================================================================

std::string PrinterHardware::guess_main_led_strip() const {
    if (leds_.empty()) {
        spdlog::debug("[PrinterHardware] guess_main_led_strip() -> no LEDs discovered");
        return "";
    }

    // Priority 1: LEDs containing "case" (most common for case lighting)
    std::string match = find_containing(leds_, "case");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_main_led_strip() -> '{}' (contains 'case')", match);
        return match;
    }

    // Priority 2: LEDs containing "chamber"
    match = find_containing(leds_, "chamber");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_main_led_strip() -> '{}' (contains 'chamber')",
                      match);
        return match;
    }

    // Priority 3: LEDs containing "light" (general lighting)
    match = find_containing(leds_, "light");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_main_led_strip() -> '{}' (contains 'light')", match);
        return match;
    }

    // Priority 4: Any LED that's NOT a specialty indicator
    // Avoid: indicator, status, corner (these are typically status LEDs, not room lighting)
    match = find_not_containing(leds_,
                                {"indicator", "status", "corner", "Indicator", "Status", "Corner"});
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_main_led_strip() -> '{}' (non-indicator)", match);
        return match;
    }

    // Fallback: first LED in list
    spdlog::debug("[PrinterHardware] guess_main_led_strip() -> '{}' (fallback)", leds_[0]);
    return leds_[0];
}

// ============================================================================
// Filament Sensor Guessing
// ============================================================================

std::string PrinterHardware::guess_runout_sensor(const std::vector<std::string>& filament_sensors) {
    if (filament_sensors.empty()) {
        spdlog::debug("[PrinterHardware] guess_runout_sensor() -> no sensors provided");
        return "";
    }

    // Priority 1: Exact match for canonical names
    if (has_exact(filament_sensors, "runout_sensor")) {
        spdlog::debug("[PrinterHardware] guess_runout_sensor() -> 'runout_sensor' (exact)");
        return "runout_sensor";
    }
    if (has_exact(filament_sensors, "filament_runout")) {
        spdlog::debug("[PrinterHardware] guess_runout_sensor() -> 'filament_runout' (exact)");
        return "filament_runout";
    }

    // Priority 2: Contains "runout"
    std::string match = find_containing(filament_sensors, "runout");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_runout_sensor() -> '{}' (contains 'runout')", match);
        return match;
    }

    // Priority 3: Contains "tool_start" (AFC pattern - filament at toolhead entry)
    match = find_containing(filament_sensors, "tool_start");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_runout_sensor() -> '{}' (contains 'tool_start')",
                      match);
        return match;
    }

    // Priority 4: Contains "filament" (generic)
    match = find_containing(filament_sensors, "filament");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_runout_sensor() -> '{}' (contains 'filament')",
                      match);
        return match;
    }

    // Priority 5: Contains "switch" or "motion" (sensor type keywords)
    match = find_containing(filament_sensors, "switch");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_runout_sensor() -> '{}' (contains 'switch')", match);
        return match;
    }
    match = find_containing(filament_sensors, "motion");
    if (!match.empty()) {
        spdlog::debug("[PrinterHardware] guess_runout_sensor() -> '{}' (contains 'motion')", match);
        return match;
    }

    // No match found
    spdlog::debug("[PrinterHardware] guess_runout_sensor() -> no match found");
    return "";
}

// ============================================================================
// AMS Sensor Detection
// ============================================================================

bool PrinterHardware::is_ams_sensor(const std::string& sensor_name) {
    // Convert to lowercase for case-insensitive matching
    std::string lower_name = sensor_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

    // AFC (Armored Turtle Filament Changer) patterns
    if (lower_name.find("lane") != std::string::npos)
        return true;
    if (lower_name.find("afc") != std::string::npos)
        return true;
    if (lower_name.find("slot") != std::string::npos)
        return true;
    if (lower_name.find("bypass") != std::string::npos)
        return true;
    if (lower_name.find("quiet") != std::string::npos)
        return true;

    // ERCF (Enraged Rabbit Carrot Feeder) patterns
    if (lower_name.find("ercf") != std::string::npos)
        return true;
    if (lower_name.find("gate") != std::string::npos)
        return true;

    // MMU2/MMU3 (Prusa Multi-Material Unit) patterns
    if (lower_name.find("mmu") != std::string::npos)
        return true;

    // TradRack patterns
    if (lower_name.find("trad") != std::string::npos)
        return true;

    // BoxTurtle patterns
    if (lower_name.find("turtle") != std::string::npos)
        return true;
    if (lower_name.find("box") != std::string::npos &&
        lower_name.find("filament") != std::string::npos)
        return true;

    // Happy Hare patterns
    if (lower_name.find("happy") != std::string::npos)
        return true;
    if (lower_name.find("hare") != std::string::npos)
        return true;

    // Generic multi-material patterns (numbered sensors)
    // Match patterns like "filament_0", "filament_1", "unit_0", "channel_1"
    if (lower_name.find("unit") != std::string::npos)
        return true;
    if (lower_name.find("channel") != std::string::npos)
        return true;
    if (lower_name.find("buffer") != std::string::npos)
        return true;
    if (lower_name.find("hub") != std::string::npos)
        return true;

    // Check for numbered filament sensors (e.g., filament_0, fil_sensor_2)
    // These typically indicate multi-material setups
    for (char c = '0'; c <= '9'; c++) {
        std::string pattern = std::string("filament_") + c;
        if (lower_name.find(pattern) != std::string::npos)
            return true;
        pattern = std::string("fil_") + c;
        if (lower_name.find(pattern) != std::string::npos)
            return true;
    }

    return false;
}
