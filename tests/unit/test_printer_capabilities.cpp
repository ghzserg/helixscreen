// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 HelixScreen Contributors
 */

#include "printer_capabilities.h"

#include "../catch_amalgamated.hpp"

using json = nlohmann::json;

// ============================================================================
// Hardware Capability Detection Tests
// ============================================================================

TEST_CASE("PrinterCapabilities - Hardware detection", "[slow][printer]") {
    PrinterCapabilities caps;

    SECTION("Detects quad_gantry_level") {
        json objects = {"extruder", "heater_bed", "quad_gantry_level", "bed_mesh"};
        caps.parse_objects(objects);

        REQUIRE(caps.has_qgl());
        REQUIRE(caps.supports_leveling());
    }

    SECTION("Detects z_tilt") {
        json objects = {"extruder", "heater_bed", "z_tilt"};
        caps.parse_objects(objects);

        REQUIRE(caps.has_z_tilt());
        REQUIRE_FALSE(caps.has_qgl());
        REQUIRE(caps.supports_leveling());
    }

    SECTION("Detects bed_mesh") {
        json objects = {"extruder", "heater_bed", "bed_mesh"};
        caps.parse_objects(objects);

        REQUIRE(caps.has_bed_mesh());
        REQUIRE(caps.supports_leveling());
    }

    SECTION("Detects chamber heater") {
        json objects = {"extruder", "heater_bed", "heater_generic chamber"};
        caps.parse_objects(objects);

        REQUIRE(caps.has_chamber_heater());
        REQUIRE(caps.supports_chamber());
    }

    SECTION("Detects chamber heater with different naming") {
        SECTION("chamber_heater variant") {
            json objects = {"heater_generic chamber_heater"};
            caps.parse_objects(objects);
            REQUIRE(caps.has_chamber_heater());
        }

        SECTION("CHAMBER uppercase variant") {
            json objects = {"heater_generic CHAMBER"};
            caps.parse_objects(objects);
            REQUIRE(caps.has_chamber_heater());
        }

        SECTION("enclosure_chamber variant") {
            json objects = {"heater_generic enclosure_chamber"};
            caps.parse_objects(objects);
            REQUIRE(caps.has_chamber_heater());
        }
    }

    SECTION("Detects chamber sensor") {
        json objects = {"temperature_sensor chamber"};
        caps.parse_objects(objects);

        REQUIRE(caps.has_chamber_sensor());
        REQUIRE_FALSE(caps.has_chamber_heater());
        REQUIRE(caps.supports_chamber());
    }

    SECTION("Non-chamber heater doesn't trigger chamber detection") {
        json objects = {"heater_generic buildplate", "heater_generic exhaust"};
        caps.parse_objects(objects);

        REQUIRE_FALSE(caps.has_chamber_heater());
        REQUIRE_FALSE(caps.supports_chamber());
    }

    SECTION("Full Voron 2.4 printer") {
        json objects = {"extruder",
                        "heater_bed",
                        "quad_gantry_level",
                        "bed_mesh",
                        "heater_generic chamber",
                        "temperature_sensor chamber",
                        "gcode_macro PRINT_START",
                        "gcode_macro CLEAN_NOZZLE"};
        caps.parse_objects(objects);

        REQUIRE(caps.has_qgl());
        REQUIRE(caps.has_bed_mesh());
        REQUIRE(caps.has_chamber_heater());
        REQUIRE(caps.has_chamber_sensor());
        REQUIRE(caps.supports_leveling());
        REQUIRE(caps.supports_chamber());
    }

    SECTION("Simple Ender 3 printer") {
        json objects = {"extruder", "heater_bed", "bed_mesh", "gcode_macro START_PRINT"};
        caps.parse_objects(objects);

        REQUIRE_FALSE(caps.has_qgl());
        REQUIRE_FALSE(caps.has_z_tilt());
        REQUIRE(caps.has_bed_mesh());
        REQUIRE_FALSE(caps.has_chamber_heater());
        REQUIRE(caps.supports_leveling());
        REQUIRE_FALSE(caps.supports_chamber());
    }

    SECTION("No leveling capabilities") {
        json objects = {"extruder", "heater_bed"};
        caps.parse_objects(objects);

        REQUIRE_FALSE(caps.has_qgl());
        REQUIRE_FALSE(caps.has_z_tilt());
        REQUIRE_FALSE(caps.has_bed_mesh());
        REQUIRE_FALSE(caps.supports_leveling());
    }
}

// ============================================================================
// Macro Detection Tests
// ============================================================================

TEST_CASE("PrinterCapabilities - Macro detection", "[slow][printer]") {
    PrinterCapabilities caps;

    SECTION("Detects macros from gcode_macro prefix") {
        json objects = {"gcode_macro START_PRINT", "gcode_macro END_PRINT", "gcode_macro PAUSE"};
        caps.parse_objects(objects);

        REQUIRE(caps.macro_count() == 3);
        REQUIRE(caps.has_macro("START_PRINT"));
        REQUIRE(caps.has_macro("END_PRINT"));
        REQUIRE(caps.has_macro("PAUSE"));
    }

    SECTION("Macro lookup is case-insensitive") {
        json objects = {"gcode_macro CLEAN_NOZZLE"};
        caps.parse_objects(objects);

        REQUIRE(caps.has_macro("CLEAN_NOZZLE"));
        REQUIRE(caps.has_macro("clean_nozzle"));
        REQUIRE(caps.has_macro("Clean_Nozzle"));
    }

    SECTION("Detects HelixScreen macros") {
        json objects = {"gcode_macro HELIX_BED_LEVEL_IF_NEEDED", "gcode_macro HELIX_PREPARE",
                        "gcode_macro START_PRINT"};
        caps.parse_objects(objects);

        REQUIRE(caps.has_helix_macros());
        REQUIRE(caps.helix_macros().size() == 2);
        REQUIRE(caps.has_helix_macro("HELIX_BED_LEVEL_IF_NEEDED"));
        REQUIRE(caps.has_helix_macro("HELIX_PREPARE"));
        REQUIRE_FALSE(caps.has_helix_macro("START_PRINT"));
    }

    SECTION("Detects nozzle cleaning macro variants") {
        SECTION("CLEAN_NOZZLE") {
            json objects = {"gcode_macro CLEAN_NOZZLE"};
            caps.parse_objects(objects);
            REQUIRE(caps.has_nozzle_clean_macro());
            REQUIRE(caps.get_nozzle_clean_macro() == "CLEAN_NOZZLE");
        }

        SECTION("NOZZLE_WIPE") {
            json objects = {"gcode_macro NOZZLE_WIPE"};
            caps.parse_objects(objects);
            REQUIRE(caps.has_nozzle_clean_macro());
            REQUIRE(caps.get_nozzle_clean_macro() == "NOZZLE_WIPE");
        }

        SECTION("WIPE_NOZZLE") {
            json objects = {"gcode_macro WIPE_NOZZLE"};
            caps.parse_objects(objects);
            REQUIRE(caps.has_nozzle_clean_macro());
        }

        SECTION("PURGE_NOZZLE") {
            json objects = {"gcode_macro PURGE_NOZZLE"};
            caps.parse_objects(objects);
            REQUIRE(caps.has_nozzle_clean_macro());
        }
    }

    SECTION("Detects purge line macro variants") {
        SECTION("PURGE_LINE") {
            json objects = {"gcode_macro PURGE_LINE"};
            caps.parse_objects(objects);
            REQUIRE(caps.has_purge_line_macro());
            REQUIRE(caps.get_purge_line_macro() == "PURGE_LINE");
        }

        SECTION("PRIME_LINE") {
            json objects = {"gcode_macro PRIME_LINE"};
            caps.parse_objects(objects);
            REQUIRE(caps.has_purge_line_macro());
        }

        SECTION("INTRO_LINE") {
            json objects = {"gcode_macro INTRO_LINE"};
            caps.parse_objects(objects);
            REQUIRE(caps.has_purge_line_macro());
        }

        SECTION("LINE_PURGE") {
            json objects = {"gcode_macro LINE_PURGE"};
            caps.parse_objects(objects);
            REQUIRE(caps.has_purge_line_macro());
        }
    }

    SECTION("Detects heat soak macro variants") {
        SECTION("HEAT_SOAK") {
            json objects = {"gcode_macro HEAT_SOAK"};
            caps.parse_objects(objects);
            REQUIRE(caps.has_heat_soak_macro());
            REQUIRE(caps.get_heat_soak_macro() == "HEAT_SOAK");
        }

        SECTION("CHAMBER_SOAK") {
            json objects = {"gcode_macro CHAMBER_SOAK"};
            caps.parse_objects(objects);
            REQUIRE(caps.has_heat_soak_macro());
        }

        SECTION("BED_SOAK") {
            json objects = {"gcode_macro BED_SOAK"};
            caps.parse_objects(objects);
            REQUIRE(caps.has_heat_soak_macro());
        }
    }

    SECTION("First matching macro wins") {
        // If multiple cleaning macros exist, first one detected wins
        json objects = {"gcode_macro WIPE_NOZZLE", "gcode_macro CLEAN_NOZZLE"};
        caps.parse_objects(objects);

        REQUIRE(caps.has_nozzle_clean_macro());
        // WIPE_NOZZLE comes first alphabetically in the JSON array
        REQUIRE(caps.get_nozzle_clean_macro() == "WIPE_NOZZLE");
    }

    SECTION("No macros detected when none present") {
        json objects = {"extruder", "heater_bed"};
        caps.parse_objects(objects);

        REQUIRE(caps.macro_count() == 0);
        REQUIRE_FALSE(caps.has_nozzle_clean_macro());
        REQUIRE_FALSE(caps.has_purge_line_macro());
        REQUIRE_FALSE(caps.has_heat_soak_macro());
        REQUIRE_FALSE(caps.has_helix_macros());
    }
}

// ============================================================================
// HelixScreen Macro Detection Tests
// ============================================================================

TEST_CASE("PrinterCapabilities - Helix macro detection", "[slow][printer][config]") {
    PrinterCapabilities caps;

    SECTION("No Helix macros when only standard macros present") {
        json objects = {"gcode_macro START_PRINT", "gcode_macro END_PRINT", "bed_mesh"};
        caps.parse_objects(objects);

        REQUIRE_FALSE(caps.has_helix_macros());
        REQUIRE(caps.helix_macros().empty());
    }

    SECTION("Detects complete Helix macro set (v2.0+)") {
        // Core macros from helix_macros.cfg v2.0+
        json objects = {"gcode_macro HELIX_READY",
                        "gcode_macro HELIX_ENDED",
                        "gcode_macro HELIX_START_PRINT",
                        "gcode_macro HELIX_CLEAN_NOZZLE",
                        "gcode_macro HELIX_BED_LEVEL_IF_NEEDED",
                        "gcode_macro _HELIX_STATE"};
        caps.parse_objects(objects);

        REQUIRE(caps.has_helix_macros());
        REQUIRE(caps.helix_macros().size() == 5); // _HELIX_STATE is internal, not counted
        REQUIRE(caps.has_helix_macro("HELIX_READY"));
        REQUIRE(caps.has_helix_macro("HELIX_ENDED"));
        REQUIRE(caps.has_helix_macro("HELIX_START_PRINT"));
    }

    SECTION("Detects partial Helix macro install (legacy v1.x)") {
        // Only some Helix macros - older version or partial install
        json objects = {"gcode_macro HELIX_START_PRINT", "gcode_macro START_PRINT"};
        caps.parse_objects(objects);

        REQUIRE(caps.has_helix_macros());
        REQUIRE(caps.helix_macros().size() == 1);
        REQUIRE(caps.has_helix_macro("HELIX_START_PRINT"));
        REQUIRE_FALSE(caps.has_helix_macro("HELIX_READY"));
    }

    SECTION("Helix macro lookup is case-insensitive") {
        json objects = {"gcode_macro HELIX_READY"};
        caps.parse_objects(objects);

        REQUIRE(caps.has_helix_macro("HELIX_READY"));
        REQUIRE(caps.has_helix_macro("helix_ready"));
        REQUIRE(caps.has_helix_macro("Helix_Ready"));
    }

    SECTION("Distinguishes HELIX_ prefix from similar names") {
        json objects = {"gcode_macro HELIX_START_PRINT", // Valid Helix macro
                        "gcode_macro HELIXSCREEN_UTIL",  // Not a Helix macro (wrong prefix)
                        "gcode_macro MY_HELIX_MACRO"};   // Not a Helix macro (prefix not at start)
        caps.parse_objects(objects);

        REQUIRE(caps.helix_macros().size() == 1);
        REQUIRE(caps.has_helix_macro("HELIX_START_PRINT"));
    }

    SECTION("Mixed Helix and standard macros") {
        json objects = {"gcode_macro START_PRINT",  "gcode_macro HELIX_START_PRINT",
                        "gcode_macro END_PRINT",    "gcode_macro HELIX_VERSION",
                        "gcode_macro CLEAN_NOZZLE", "gcode_macro HELIX_CLEAN_NOZZLE"};
        caps.parse_objects(objects);

        REQUIRE(caps.macro_count() == 6);
        REQUIRE(caps.helix_macros().size() == 3);
        REQUIRE(caps.has_helix_macros());

        // Standard macros should also be detected
        REQUIRE(caps.has_macro("START_PRINT"));
        REQUIRE(caps.has_nozzle_clean_macro());
    }
}

// ============================================================================
// Clear and Reset Tests
// ============================================================================

TEST_CASE("PrinterCapabilities - Clear", "[slow][printer]") {
    PrinterCapabilities caps;

    // First parse some capabilities
    json objects = {"quad_gantry_level", "bed_mesh", "gcode_macro CLEAN_NOZZLE",
                    "gcode_macro HELIX_PREPARE"};
    caps.parse_objects(objects);

    REQUIRE(caps.has_qgl());
    REQUIRE(caps.macro_count() == 2);
    REQUIRE(caps.has_helix_macros());

    // Now clear
    caps.clear();

    REQUIRE_FALSE(caps.has_qgl());
    REQUIRE_FALSE(caps.has_bed_mesh());
    REQUIRE(caps.macro_count() == 0);
    REQUIRE_FALSE(caps.has_helix_macros());
    REQUIRE_FALSE(caps.has_nozzle_clean_macro());
}

TEST_CASE("PrinterCapabilities - Re-parse replaces old data", "[slow][printer]") {
    PrinterCapabilities caps;

    // First parse
    json objects1 = {"quad_gantry_level", "gcode_macro MACRO_A"};
    caps.parse_objects(objects1);

    REQUIRE(caps.has_qgl());
    REQUIRE(caps.has_macro("MACRO_A"));
    REQUIRE_FALSE(caps.has_z_tilt());

    // Second parse with different data
    json objects2 = {"z_tilt", "gcode_macro MACRO_B"};
    caps.parse_objects(objects2);

    REQUIRE_FALSE(caps.has_qgl()); // No longer present
    REQUIRE(caps.has_z_tilt());    // Now present
    REQUIRE_FALSE(caps.has_macro("MACRO_A"));
    REQUIRE(caps.has_macro("MACRO_B"));
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("PrinterCapabilities - Edge cases", "[slow][printer]") {
    PrinterCapabilities caps;

    SECTION("Empty objects array") {
        json objects = json::array();
        caps.parse_objects(objects);

        REQUIRE_FALSE(caps.has_qgl());
        REQUIRE(caps.macro_count() == 0);
    }

    SECTION("Ignores non-macro gcode_ objects") {
        json objects = {"gcode_move", "gcode_shell_command my_script"};
        caps.parse_objects(objects);

        REQUIRE(caps.macro_count() == 0);
    }

    SECTION("Handles objects with spaces in names") {
        json objects = {"gcode_macro MY MACRO NAME", "heater_generic my chamber heater"};
        caps.parse_objects(objects);

        REQUIRE(caps.has_macro("MY MACRO NAME"));
        REQUIRE(caps.has_chamber_heater());
    }

    SECTION("Handles empty macro name") {
        json objects = {"gcode_macro "}; // Just "gcode_macro " with trailing space
        caps.parse_objects(objects);

        // Should handle gracefully (empty string macro)
        REQUIRE(caps.macro_count() == 1);
    }
}

// ============================================================================
// Summary Output Tests
// ============================================================================

TEST_CASE("PrinterCapabilities - Summary", "[slow][printer]") {
    PrinterCapabilities caps;

    SECTION("Summary includes all detected capabilities") {
        json objects = {"quad_gantry_level", "bed_mesh", "heater_generic chamber",
                        "gcode_macro START_PRINT", "gcode_macro HELIX_PREPARE"};
        caps.parse_objects(objects);

        std::string summary = caps.summary();

        REQUIRE(summary.find("QGL") != std::string::npos);
        REQUIRE(summary.find("bed_mesh") != std::string::npos);
        REQUIRE(summary.find("chamber_heater") != std::string::npos);
        REQUIRE(summary.find("2 macros") != std::string::npos);
        REQUIRE(summary.find("1 HELIX_*") != std::string::npos);
    }

    SECTION("Summary shows 'none' when no capabilities") {
        json objects = {"extruder"};
        caps.parse_objects(objects);

        std::string summary = caps.summary();

        REQUIRE(summary.find("none") != std::string::npos);
        REQUIRE(summary.find("0 macros") != std::string::npos);
    }
}

// ============================================================================
// Real-world Printer Configurations
// ============================================================================

TEST_CASE("PrinterCapabilities - Real printer configs", "[slow][printer]") {
    PrinterCapabilities caps;

    SECTION("Voron 2.4 with full configuration") {
        json objects = {
            "configfile",
            "mcu",
            "mcu EBBCan",
            "stepper_x",
            "stepper_y",
            "stepper_z",
            "stepper_z1",
            "stepper_z2",
            "stepper_z3",
            "extruder",
            "heater_bed",
            "heater_generic chamber",
            "temperature_sensor chamber",
            "temperature_sensor raspberry_pi",
            "temperature_sensor mcu_temp",
            "fan",
            "heater_fan hotend_fan",
            "controller_fan controller_fan",
            "fan_generic exhaust_fan",
            "neopixel status_led",
            "probe",
            "quad_gantry_level",
            "bed_mesh",
            "gcode_macro PRINT_START",
            "gcode_macro PRINT_END",
            "gcode_macro CLEAN_NOZZLE",
            "gcode_macro PURGE_LINE",
            "gcode_macro HEAT_SOAK",
            "gcode_macro G32",
            "gcode_macro CANCEL_PRINT",
            "gcode_macro PAUSE",
            "gcode_macro RESUME",
        };
        caps.parse_objects(objects);

        REQUIRE(caps.has_qgl());
        REQUIRE(caps.has_bed_mesh());
        REQUIRE(caps.has_chamber_heater());
        REQUIRE(caps.has_chamber_sensor());
        REQUIRE(caps.has_nozzle_clean_macro());
        REQUIRE(caps.has_purge_line_macro());
        REQUIRE(caps.has_heat_soak_macro());
        REQUIRE(caps.macro_count() == 9);
    }

    SECTION("Voron Trident with z_tilt") {
        json objects = {"extruder",
                        "heater_bed",
                        "z_tilt",
                        "bed_mesh",
                        "gcode_macro Z_TILT_ADJUST_WRAPPER",
                        "gcode_macro PRINT_START"};
        caps.parse_objects(objects);

        REQUIRE_FALSE(caps.has_qgl());
        REQUIRE(caps.has_z_tilt());
        REQUIRE(caps.has_bed_mesh());
        REQUIRE(caps.supports_leveling());
    }

    SECTION("Prusa MK3 style with no leveling objects") {
        // Some printers use G29 for mesh without declaring bed_mesh object
        json objects = {"extruder", "heater_bed", "fan", "gcode_macro M600"};
        caps.parse_objects(objects);

        REQUIRE_FALSE(caps.has_bed_mesh());
        REQUIRE_FALSE(caps.supports_leveling());
    }
}
