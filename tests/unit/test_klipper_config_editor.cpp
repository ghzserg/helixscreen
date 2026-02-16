// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "klipper_config_editor.h"

#include "../catch_amalgamated.hpp"

using namespace helix::system;

TEST_CASE("KlipperConfigEditor - section parsing", "[config][parser]") {
    KlipperConfigEditor editor;

    SECTION("Finds simple section") {
        std::string content = "[printer]\nkinematics: corexy\n\n[probe]\npin: PA1\nz_offset: 1.5\n";
        auto result = editor.parse_structure(content);
        REQUIRE(result.sections.count("probe") == 1);
        REQUIRE(result.sections["probe"].line_start > 0);
    }

    SECTION("Handles section with space in name") {
        std::string content = "[bed_mesh default]\nversion: 1\n";
        auto result = editor.parse_structure(content);
        REQUIRE(result.sections.count("bed_mesh default") == 1);
    }

    SECTION("Finds key within section") {
        std::string content = "[probe]\npin: PA1\nz_offset: 1.5\nsamples: 3\n";
        auto result = editor.parse_structure(content);
        auto key = result.find_key("probe", "z_offset");
        REQUIRE(key.has_value());
        REQUIRE(key->value == "1.5");
    }

    SECTION("Handles both : and = delimiters") {
        std::string content = "[probe]\npin: PA1\nz_offset = 1.5\n";
        auto result = editor.parse_structure(content);
        auto key1 = result.find_key("probe", "pin");
        auto key2 = result.find_key("probe", "z_offset");
        REQUIRE(key1->delimiter == ":");
        REQUIRE(key2->delimiter == "=");
    }

    SECTION("Skips multi-line values correctly") {
        std::string content =
            "[gcode_macro START]\ngcode:\n    G28\n    G1 Z10\n\n[probe]\npin: PA1\n";
        auto result = editor.parse_structure(content);
        auto key = result.find_key("probe", "pin");
        REQUIRE(key.has_value());
        REQUIRE(key->value == "PA1");
    }

    SECTION("Identifies SAVE_CONFIG boundary") {
        std::string content = "[probe]\npin: PA1\n\n"
                              "#*# <---------------------- SAVE_CONFIG ---------------------->\n"
                              "#*# DO NOT EDIT THIS BLOCK OR BELOW.\n"
                              "#*#\n"
                              "#*# [probe]\n"
                              "#*# z_offset = 1.234\n";
        auto result = editor.parse_structure(content);
        REQUIRE(result.save_config_line > 0);
    }

    SECTION("Preserves comments - not treated as keys") {
        std::string content = "# My config\n[probe]\n# Z offset\nz_offset: 1.5\n";
        auto result = editor.parse_structure(content);
        auto key = result.find_key("probe", "z_offset");
        REQUIRE(key.has_value());
        // Should only have z_offset as a key, not comments
        REQUIRE(result.sections["probe"].keys.size() == 1);
    }

    SECTION("Detects include directives") {
        std::string content =
            "[include hardware/*.cfg]\n[include macros.cfg]\n[printer]\nkinematics: corexy\n";
        auto result = editor.parse_structure(content);
        REQUIRE(result.includes.size() == 2);
        REQUIRE(result.includes[0] == "hardware/*.cfg");
        REQUIRE(result.includes[1] == "macros.cfg");
    }

    SECTION("Option names are lowercased") {
        std::string content = "[probe]\nZ_Offset: 1.5\n";
        auto result = editor.parse_structure(content);
        auto key = result.find_key("probe", "z_offset");
        REQUIRE(key.has_value());
    }

    SECTION("Handles empty file") {
        auto result = editor.parse_structure("");
        REQUIRE(result.sections.empty());
        REQUIRE(result.includes.empty());
    }

    SECTION("Handles file with only comments") {
        auto result = editor.parse_structure("# Just a comment\n; Another\n");
        REQUIRE(result.sections.empty());
    }

    SECTION("Multi-line value with empty lines preserved") {
        std::string content =
            "[gcode_macro M]\ngcode:\n    G28\n\n    G1 Z10\n\n[probe]\npin: PA1\n";
        auto result = editor.parse_structure(content);
        // The gcode macro's multi-line value spans across the empty line
        auto gcode_key = result.find_key("gcode_macro M", "gcode");
        REQUIRE(gcode_key.has_value());
        REQUIRE(gcode_key->is_multiline);
        // probe section should still be found after the multi-line value
        REQUIRE(result.sections.count("probe") == 1);
    }

    SECTION("Section line ranges are correct") {
        std::string content =
            "[printer]\nkinematics: corexy\nmax_velocity: 300\n\n[probe]\npin: PA1\n";
        auto result = editor.parse_structure(content);
        auto& printer = result.sections["printer"];
        auto& probe = result.sections["probe"];
        REQUIRE(printer.line_start < probe.line_start);
        REQUIRE(printer.line_end < probe.line_start);
    }
}

TEST_CASE("KlipperConfigEditor - value editing", "[config][editor]") {
    KlipperConfigEditor editor;

    SECTION("set_value replaces existing value") {
        std::string content = "[probe]\npin: PA1\nz_offset: 1.5\nsamples: 3\n";
        auto result = editor.set_value(content, "probe", "samples", "5");
        REQUIRE(result.has_value());
        REQUIRE(result->find("samples: 5") != std::string::npos);
        // Other values unchanged
        REQUIRE(result->find("pin: PA1") != std::string::npos);
        REQUIRE(result->find("z_offset: 1.5") != std::string::npos);
    }

    SECTION("set_value preserves delimiter style") {
        std::string content = "[probe]\nz_offset = 1.5\n";
        auto result = editor.set_value(content, "probe", "z_offset", "2.0");
        REQUIRE(result.has_value());
        REQUIRE(result->find("z_offset = 2.0") != std::string::npos);
    }

    SECTION("set_value preserves comments") {
        std::string content = "[probe]\n# Important comment\nz_offset: 1.5\n";
        auto result = editor.set_value(content, "probe", "z_offset", "2.0");
        REQUIRE(result.has_value());
        REQUIRE(result->find("# Important comment") != std::string::npos);
    }

    SECTION("set_value returns nullopt for missing key") {
        std::string content = "[probe]\npin: PA1\n";
        auto result = editor.set_value(content, "probe", "samples", "5");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("set_value returns nullopt for missing section") {
        std::string content = "[printer]\nkinematics: corexy\n";
        auto result = editor.set_value(content, "probe", "pin", "PA1");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("add_key adds to end of section") {
        std::string content = "[probe]\npin: PA1\nz_offset: 1.5\n\n[printer]\nkinematics: corexy\n";
        auto result = editor.add_key(content, "probe", "samples", "3");
        REQUIRE(result.has_value());
        REQUIRE(result->find("samples: 3") != std::string::npos);
        // Should be in [probe] section, before [printer]
        auto samples_pos = result->find("samples: 3");
        auto printer_pos = result->find("[printer]");
        REQUIRE(samples_pos < printer_pos);
    }

    SECTION("add_key returns nullopt for missing section") {
        std::string content = "[printer]\nkinematics: corexy\n";
        auto result = editor.add_key(content, "probe", "pin", "PA1");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("add_key respects custom delimiter") {
        std::string content = "[probe]\npin = PA1\n";
        auto result = editor.add_key(content, "probe", "samples", "3", " = ");
        REQUIRE(result.has_value());
        REQUIRE(result->find("samples = 3") != std::string::npos);
    }

    SECTION("remove_key comments out the line") {
        std::string content = "[probe]\npin: PA1\nsamples: 3\nz_offset: 1.5\n";
        auto result = editor.remove_key(content, "probe", "samples");
        REQUIRE(result.has_value());
        REQUIRE(result->find("#samples: 3") != std::string::npos);
        // Other keys untouched
        REQUIRE(result->find("pin: PA1") != std::string::npos);
        REQUIRE(result->find("z_offset: 1.5") != std::string::npos);
    }

    SECTION("remove_key returns nullopt for missing key") {
        std::string content = "[probe]\npin: PA1\n";
        auto result = editor.remove_key(content, "probe", "nonexistent");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("set_value handles value with spaces") {
        std::string content = "[probe]\nsamples_result: median\n";
        auto result = editor.set_value(content, "probe", "samples_result", "average");
        REQUIRE(result.has_value());
        REQUIRE(result->find("samples_result: average") != std::string::npos);
    }
}

TEST_CASE("KlipperConfigEditor - include resolution", "[config][includes]") {
    KlipperConfigEditor editor;

    SECTION("Resolves simple include") {
        std::map<std::string, std::string> files;
        files["printer.cfg"] = "[include hardware.cfg]\n[printer]\nkinematics: corexy\n";
        files["hardware.cfg"] = "[probe]\npin: PA1\nz_offset: 1.5\n";

        auto result = editor.resolve_includes(files, "printer.cfg");
        REQUIRE(result.count("probe") == 1);
        REQUIRE(result["probe"].file_path == "hardware.cfg");
        REQUIRE(result.count("printer") == 1);
        REQUIRE(result["printer"].file_path == "printer.cfg");
    }

    SECTION("Resolves nested includes") {
        std::map<std::string, std::string> files;
        files["printer.cfg"] = "[include hardware/main.cfg]\n[printer]\nkinematics: corexy\n";
        files["hardware/main.cfg"] = "[include probe.cfg]\n[stepper_x]\nstep_pin: PA1\n";
        files["hardware/probe.cfg"] = "[probe]\npin: PB6\n";

        auto result = editor.resolve_includes(files, "printer.cfg");
        REQUIRE(result.count("probe") == 1);
        REQUIRE(result["probe"].file_path == "hardware/probe.cfg");
        REQUIRE(result.count("stepper_x") == 1);
        REQUIRE(result["stepper_x"].file_path == "hardware/main.cfg");
    }

    SECTION("Detects circular includes without infinite loop") {
        std::map<std::string, std::string> files;
        files["a.cfg"] = "[include b.cfg]\n[section_a]\nkey: val\n";
        files["b.cfg"] = "[include a.cfg]\n[section_b]\nkey: val\n";

        auto result = editor.resolve_includes(files, "a.cfg");
        REQUIRE(result.count("section_a") == 1);
        REQUIRE(result.count("section_b") == 1);
    }

    SECTION("Caps recursion depth at max_depth") {
        std::map<std::string, std::string> files;
        files["l0.cfg"] = "[include l1.cfg]\n[s0]\nk: v\n";
        files["l1.cfg"] = "[include l2.cfg]\n[s1]\nk: v\n";
        files["l2.cfg"] = "[include l3.cfg]\n[s2]\nk: v\n";
        files["l3.cfg"] = "[include l4.cfg]\n[s3]\nk: v\n";
        files["l4.cfg"] = "[include l5.cfg]\n[s4]\nk: v\n";
        files["l5.cfg"] = "[include l6.cfg]\n[s5]\nk: v\n";
        files["l6.cfg"] = "[deep]\nk: v\n";

        // With max_depth=5, l6.cfg should NOT be reached
        auto result = editor.resolve_includes(files, "l0.cfg", 5);
        REQUIRE(result.count("s0") == 1);
        REQUIRE(result.count("s5") == 1);
        REQUIRE(result.count("deep") == 0);
    }

    SECTION("Handles missing included file gracefully") {
        std::map<std::string, std::string> files;
        files["printer.cfg"] = "[include nonexistent.cfg]\n[printer]\nkinematics: corexy\n";

        auto result = editor.resolve_includes(files, "printer.cfg");
        REQUIRE(result.count("printer") == 1);
    }

    SECTION("Resolves relative paths from including file directory") {
        std::map<std::string, std::string> files;
        files["printer.cfg"] = "[include hardware/sensors.cfg]\n";
        files["hardware/sensors.cfg"] = "[include probe.cfg]\n";
        files["hardware/probe.cfg"] = "[probe]\npin: PA1\n";

        auto result = editor.resolve_includes(files, "printer.cfg");
        REQUIRE(result.count("probe") == 1);
        REQUIRE(result["probe"].file_path == "hardware/probe.cfg");
    }

    SECTION("Resolves glob patterns") {
        std::map<std::string, std::string> files;
        files["printer.cfg"] = "[include macros/*.cfg]\n[printer]\nkinematics: corexy\n";
        files["macros/start.cfg"] = "[gcode_macro START]\ngcode:\n    G28\n";
        files["macros/end.cfg"] = "[gcode_macro END]\ngcode:\n    M84\n";

        auto result = editor.resolve_includes(files, "printer.cfg");
        REQUIRE(result.count("gcode_macro START") == 1);
        REQUIRE(result.count("gcode_macro END") == 1);
    }

    SECTION("Last section wins for duplicates") {
        std::map<std::string, std::string> files;
        files["printer.cfg"] = "[include override.cfg]\n[probe]\npin: PA1\n";
        files["override.cfg"] = "[probe]\npin: PB6\n";

        auto result = editor.resolve_includes(files, "printer.cfg");
        REQUIRE(result.count("probe") == 1);
        // printer.cfg is processed after its includes, so its [probe] wins
        REQUIRE(result["probe"].file_path == "printer.cfg");
    }
}
