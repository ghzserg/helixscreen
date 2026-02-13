// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_moonraker_mock_bed_mesh.cpp
 * @brief Unit tests for MoonrakerClientMock bed mesh profile storage
 *
 * Tests verify that:
 * - Initial mesh generation creates distinct profiles
 * - Profile LOAD restores actual mesh data (not just name)
 * - Profile SAVE stores current mesh data
 * - BED_MESH_CALIBRATE generates new random mesh
 * - Profile REMOVE deletes stored data
 */

#include "../../include/moonraker_client_mock.h"

#include <cmath>

#include "../catch_amalgamated.hpp"

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Calculate Z range (max - min) of a mesh
 */
static float calculate_mesh_z_range(const BedMeshProfile& mesh) {
    if (mesh.probed_matrix.empty()) {
        return 0.0f;
    }

    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();

    for (const auto& row : mesh.probed_matrix) {
        for (float z : row) {
            min_z = std::min(min_z, z);
            max_z = std::max(max_z, z);
        }
    }

    return max_z - min_z;
}

/**
 * @brief Check if two meshes have the same data (within tolerance)
 */
static bool meshes_equal(const BedMeshProfile& a, const BedMeshProfile& b,
                         float tolerance = 0.001f) {
    if (a.x_count != b.x_count || a.y_count != b.y_count) {
        return false;
    }
    if (a.probed_matrix.size() != b.probed_matrix.size()) {
        return false;
    }

    for (size_t row = 0; row < a.probed_matrix.size(); row++) {
        if (a.probed_matrix[row].size() != b.probed_matrix[row].size()) {
            return false;
        }
        for (size_t col = 0; col < a.probed_matrix[row].size(); col++) {
            if (std::fabs(a.probed_matrix[row][col] - b.probed_matrix[row][col]) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

// ============================================================================
// Initial Mesh Generation Tests
// ============================================================================

TEST_CASE("Mock bed mesh initial generation", "[bed_mesh][mock]") {
    MoonrakerClientMock mock(MoonrakerClientMock::PrinterType::VORON_24);

    SECTION("generates mesh on construction") {
        REQUIRE(mock.has_bed_mesh());
        const auto& mesh = mock.get_active_bed_mesh();
        REQUIRE(!mesh.probed_matrix.empty());
        REQUIRE(mesh.x_count == 7);
        REQUIRE(mesh.y_count == 7);
    }

    SECTION("default profile has realistic Z range") {
        const auto& mesh = mock.get_active_bed_mesh();
        float z_range = calculate_mesh_z_range(mesh);

        // Realistic bed mesh: 0.1 - 0.5mm range
        REQUIRE(z_range > 0.1f);
        REQUIRE(z_range < 0.6f);
    }

    SECTION("creates both default and adaptive profiles") {
        const auto& profiles = mock.get_bed_mesh_profiles();
        REQUIRE(profiles.size() >= 2);

        bool has_default = false;
        bool has_adaptive = false;
        for (const auto& name : profiles) {
            if (name == "default")
                has_default = true;
            if (name == "adaptive")
                has_adaptive = true;
        }
        REQUIRE(has_default);
        REQUIRE(has_adaptive);
    }

    SECTION("default and adaptive have different mesh data") {
        // Load default
        mock.gcode_script("BED_MESH_PROFILE LOAD=default");
        BedMeshProfile default_mesh = mock.get_active_bed_mesh();

        // Load adaptive
        mock.gcode_script("BED_MESH_PROFILE LOAD=adaptive");
        BedMeshProfile adaptive_mesh = mock.get_active_bed_mesh();

        // They should be different (not just renamed)
        REQUIRE(!meshes_equal(default_mesh, adaptive_mesh));
    }

    SECTION("mesh bounds use probe margins") {
        const auto& mesh = mock.get_active_bed_mesh();

        // Should have margins from bed edges (15mm default)
        REQUIRE(mesh.mesh_min[0] > 0.0f);
        REQUIRE(mesh.mesh_min[1] > 0.0f);
        REQUIRE(mesh.mesh_max[0] < 250.0f); // Mock bed is 250mm
        REQUIRE(mesh.mesh_max[1] < 250.0f);
    }
}

// ============================================================================
// Profile Load/Save Tests
// ============================================================================

TEST_CASE("Mock bed mesh profile storage", "[bed_mesh][mock]") {
    MoonrakerClientMock mock(MoonrakerClientMock::PrinterType::VORON_24);

    SECTION("LOAD restores actual mesh data") {
        // Capture default mesh
        mock.gcode_script("BED_MESH_PROFILE LOAD=default");
        BedMeshProfile default_mesh = mock.get_active_bed_mesh();

        // Load adaptive (different data)
        mock.gcode_script("BED_MESH_PROFILE LOAD=adaptive");
        REQUIRE(mock.get_active_bed_mesh().name == "adaptive");
        REQUIRE(!meshes_equal(mock.get_active_bed_mesh(), default_mesh));

        // Load default again - should restore original data
        mock.gcode_script("BED_MESH_PROFILE LOAD=default");
        REQUIRE(mock.get_active_bed_mesh().name == "default");
        REQUIRE(meshes_equal(mock.get_active_bed_mesh(), default_mesh));
    }

    SECTION("SAVE stores current mesh data") {
        // Generate a calibration (new random mesh)
        mock.gcode_script("BED_MESH_CALIBRATE PROFILE=test_save");
        BedMeshProfile calibrated = mock.get_active_bed_mesh();

        // Load a different profile
        mock.gcode_script("BED_MESH_PROFILE LOAD=default");
        REQUIRE(mock.get_active_bed_mesh().name == "default");

        // Load our saved profile - should restore the calibrated data
        mock.gcode_script("BED_MESH_PROFILE LOAD=test_save");
        REQUIRE(mock.get_active_bed_mesh().name == "test_save");
        REQUIRE(meshes_equal(mock.get_active_bed_mesh(), calibrated));
    }

    SECTION("REMOVE deletes profile") {
        // Verify profile exists
        auto profiles_before = mock.get_bed_mesh_profiles();
        REQUIRE(std::find(profiles_before.begin(), profiles_before.end(), "default") !=
                profiles_before.end());

        // Remove it
        mock.gcode_script("BED_MESH_PROFILE REMOVE=default");

        // Verify it's gone
        auto profiles_after = mock.get_bed_mesh_profiles();
        REQUIRE(std::find(profiles_after.begin(), profiles_after.end(), "default") ==
                profiles_after.end());
    }
}

// ============================================================================
// Calibration Tests
// ============================================================================

TEST_CASE("Mock bed mesh calibration", "[bed_mesh][mock]") {
    MoonrakerClientMock mock(MoonrakerClientMock::PrinterType::VORON_24);

    SECTION("BED_MESH_CALIBRATE generates new mesh") {
        BedMeshProfile before = mock.get_active_bed_mesh();

        mock.gcode_script("BED_MESH_CALIBRATE PROFILE=calibrated");

        // Should have different data (randomized)
        REQUIRE(mock.get_active_bed_mesh().name == "calibrated");
        // Compare element-wise - more robust than float sum comparison
        REQUIRE(!meshes_equal(mock.get_active_bed_mesh(), before));
    }

    SECTION("calibration generates realistic mesh") {
        mock.gcode_script("BED_MESH_CALIBRATE PROFILE=realistic_test");

        const auto& mesh = mock.get_active_bed_mesh();
        float z_range = calculate_mesh_z_range(mesh);

        // Should have realistic Z variation
        // With dome amp 0.15-0.35, center shift ±0.5, tilt ±0.08, noise ±0.03,
        // worst-case z_range can reach ~0.9mm
        REQUIRE(z_range > 0.05f);
        REQUIRE(z_range < 1.0f);
    }

    SECTION("multiple calibrations produce different results") {
        std::vector<BedMeshProfile> meshes;

        // Run 5 calibrations
        for (int i = 0; i < 5; i++) {
            mock.gcode_script("BED_MESH_CALIBRATE PROFILE=multi_" + std::to_string(i));
            meshes.push_back(mock.get_active_bed_mesh());
        }

        // Each mesh should differ from the previous one
        for (size_t i = 1; i < meshes.size(); i++) {
            REQUIRE(!meshes_equal(meshes[i], meshes[i - 1]));
        }
    }

    SECTION("calibration adds profile to list") {
        auto profiles_before = mock.get_bed_mesh_profiles();
        size_t count_before = profiles_before.size();

        mock.gcode_script("BED_MESH_CALIBRATE PROFILE=new_profile");

        auto profiles_after = mock.get_bed_mesh_profiles();
        REQUIRE(profiles_after.size() == count_before + 1);
        REQUIRE(std::find(profiles_after.begin(), profiles_after.end(), "new_profile") !=
                profiles_after.end());
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("Mock bed mesh edge cases", "[bed_mesh][mock]") {
    MoonrakerClientMock mock(MoonrakerClientMock::PrinterType::VORON_24);

    SECTION("loading non-existent profile does nothing") {
        std::string original_name = mock.get_active_bed_mesh().name;
        BedMeshProfile original = mock.get_active_bed_mesh();

        mock.gcode_script("BED_MESH_PROFILE LOAD=nonexistent");

        // Should be unchanged
        REQUIRE(mock.get_active_bed_mesh().name == original_name);
        REQUIRE(meshes_equal(mock.get_active_bed_mesh(), original));
    }

    SECTION("removing non-existent profile does nothing") {
        auto profiles_before = mock.get_bed_mesh_profiles();

        mock.gcode_script("BED_MESH_PROFILE REMOVE=nonexistent");

        auto profiles_after = mock.get_bed_mesh_profiles();
        REQUIRE(profiles_before.size() == profiles_after.size());
    }

    SECTION("BED_MESH_CLEAR clears active mesh") {
        REQUIRE(mock.has_bed_mesh());

        mock.gcode_script("BED_MESH_CLEAR");

        REQUIRE(!mock.has_bed_mesh());
    }
}
