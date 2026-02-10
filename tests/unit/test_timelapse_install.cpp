// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_overlay_timelapse_install.h"

#include <string>

#include "../catch_amalgamated.hpp"

// ============================================================================
// has_timelapse_section
// ============================================================================

TEST_CASE("has_timelapse_section: detects existing section", "[timelapse][config]") {
    std::string config = "[printer]\nkinematics: corexy\n\n[timelapse]\n\n[update_manager]\n";
    REQUIRE(TimelapseInstallOverlay::has_timelapse_section(config) == true);
}

TEST_CASE("has_timelapse_section: returns false when missing", "[timelapse][config]") {
    std::string config = "[printer]\nkinematics: corexy\n\n[heater_bed]\n";
    REQUIRE(TimelapseInstallOverlay::has_timelapse_section(config) == false);
}

TEST_CASE("has_timelapse_section: returns false for empty string", "[timelapse][config]") {
    REQUIRE(TimelapseInstallOverlay::has_timelapse_section("") == false);
}

TEST_CASE("has_timelapse_section: ignores commented-out lines", "[timelapse][config]") {
    std::string config = "[printer]\n# [timelapse]\n[heater_bed]\n";
    REQUIRE(TimelapseInstallOverlay::has_timelapse_section(config) == false);
}

TEST_CASE("has_timelapse_section: ignores indented comment lines", "[timelapse][config]") {
    std::string config = "[printer]\n  # [timelapse]\n[heater_bed]\n";
    REQUIRE(TimelapseInstallOverlay::has_timelapse_section(config) == false);
}

TEST_CASE("has_timelapse_section: handles surrounding sections", "[timelapse][config]") {
    std::string config = "[server]\nhost: 0.0.0.0\nport: 7125\n\n"
                         "[timelapse]\n\n"
                         "[update_manager]\n";
    REQUIRE(TimelapseInstallOverlay::has_timelapse_section(config) == true);
}

TEST_CASE("has_timelapse_section: does not match similar section names", "[timelapse][config]") {
    std::string config = "[printer]\n[timelapse_settings]\n[heater_bed]\n";
    REQUIRE(TimelapseInstallOverlay::has_timelapse_section(config) == false);
}

TEST_CASE("has_timelapse_section: does not match update_manager timelapse", "[timelapse][config]") {
    std::string config = "[printer]\n[update_manager timelapse]\ntype: git_repo\n";
    REQUIRE(TimelapseInstallOverlay::has_timelapse_section(config) == false);
}

TEST_CASE("has_timelapse_section: matches with leading whitespace", "[timelapse][config]") {
    std::string config = "[printer]\n  [timelapse]\n";
    REQUIRE(TimelapseInstallOverlay::has_timelapse_section(config) == true);
}

TEST_CASE("has_timelapse_section: matches with leading tab", "[timelapse][config]") {
    std::string config = "[printer]\n\t[timelapse]\n";
    REQUIRE(TimelapseInstallOverlay::has_timelapse_section(config) == true);
}

TEST_CASE("has_timelapse_section: section at very start of file", "[timelapse][config]") {
    std::string config = "[timelapse]\nsome_option: value\n";
    REQUIRE(TimelapseInstallOverlay::has_timelapse_section(config) == true);
}

TEST_CASE("has_timelapse_section: section at end without trailing newline", "[timelapse][config]") {
    std::string config = "[printer]\n[timelapse]";
    REQUIRE(TimelapseInstallOverlay::has_timelapse_section(config) == true);
}

TEST_CASE("has_timelapse_section: handles trailing spaces", "[timelapse][config]") {
    std::string config = "[printer]\n[timelapse]   \n";
    REQUIRE(TimelapseInstallOverlay::has_timelapse_section(config) == true);
}

TEST_CASE("has_timelapse_section: handles Windows line endings", "[timelapse][config]") {
    std::string config = "[printer]\r\n[timelapse]\r\n";
    REQUIRE(TimelapseInstallOverlay::has_timelapse_section(config) == true);
}

// ============================================================================
// append_timelapse_config
// ============================================================================

TEST_CASE("append_timelapse_config: adds timelapse sections", "[timelapse][config]") {
    std::string config = "[printer]\nkinematics: corexy\n";
    std::string result = TimelapseInstallOverlay::append_timelapse_config(config);

    REQUIRE(result.find("[timelapse]") != std::string::npos);
    REQUIRE(result.find("[update_manager timelapse]") != std::string::npos);
    REQUIRE(result.find("type: git_repo") != std::string::npos);
    REQUIRE(result.find("primary_branch: main") != std::string::npos);
    REQUIRE(result.find("path: ~/moonraker-timelapse") != std::string::npos);
    REQUIRE(result.find("origin: https://github.com/mainsail-crew/moonraker-timelapse.git") !=
            std::string::npos);
    REQUIRE(result.find("managed_services: klipper moonraker") != std::string::npos);
}

TEST_CASE("append_timelapse_config: preserves existing content", "[timelapse][config]") {
    std::string config = "[printer]\nkinematics: corexy\nmax_velocity: 300\n";
    std::string result = TimelapseInstallOverlay::append_timelapse_config(config);

    REQUIRE(result.find("[printer]") != std::string::npos);
    REQUIRE(result.find("kinematics: corexy") != std::string::npos);
    REQUIRE(result.find("max_velocity: 300") != std::string::npos);

    // Original content appears before the appended sections
    auto printer_pos = result.find("[printer]");
    auto timelapse_pos = result.find("[timelapse]");
    REQUIRE(printer_pos < timelapse_pos);
}

TEST_CASE("append_timelapse_config: handles config without trailing newline",
          "[timelapse][config]") {
    std::string config = "[printer]\nkinematics: corexy";
    std::string result = TimelapseInstallOverlay::append_timelapse_config(config);

    // Should still produce valid config with sections
    REQUIRE(result.find("[timelapse]") != std::string::npos);
    REQUIRE(result.find("[update_manager timelapse]") != std::string::npos);

    // The original content should still be there
    REQUIRE(result.find("[printer]") != std::string::npos);
    REQUIRE(result.find("kinematics: corexy") != std::string::npos);
}

TEST_CASE("append_timelapse_config: handles empty config", "[timelapse][config]") {
    std::string result = TimelapseInstallOverlay::append_timelapse_config("");

    REQUIRE(result.find("[timelapse]") != std::string::npos);
    REQUIRE(result.find("[update_manager timelapse]") != std::string::npos);
    REQUIRE(result.find("type: git_repo") != std::string::npos);
}

TEST_CASE("append_timelapse_config: includes HelixScreen comment", "[timelapse][config]") {
    std::string config = "[printer]\n";
    std::string result = TimelapseInstallOverlay::append_timelapse_config(config);

    REQUIRE(result.find("# Timelapse - added by HelixScreen") != std::string::npos);
}

TEST_CASE("append_timelapse_config: result is valid with has_timelapse_section",
          "[timelapse][config]") {
    std::string config = "[printer]\nkinematics: corexy\n";

    // Original should not have timelapse section
    REQUIRE(TimelapseInstallOverlay::has_timelapse_section(config) == false);

    // After appending, it should
    std::string result = TimelapseInstallOverlay::append_timelapse_config(config);
    REQUIRE(TimelapseInstallOverlay::has_timelapse_section(result) == true);
}
