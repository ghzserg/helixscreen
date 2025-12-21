// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_moonraker_api_mock.cpp
 * @brief Unit tests for MoonrakerAPIMock - HTTP file transfer mocking
 *
 * Tests the mock API's ability to:
 * - Download files from test assets regardless of working directory
 * - Upload files (mock always succeeds)
 * - Handle missing files with proper error callbacks
 *
 * TDD: These tests are written BEFORE the implementation is complete.
 */

#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"

#include <atomic>
#include <filesystem>
#include <fstream>

#include "../catch_amalgamated.hpp"

// ============================================================================
// Test Fixture
// ============================================================================

class MoonrakerAPIMockTestFixture {
  public:
    MoonrakerAPIMockTestFixture() : client_(MoonrakerClientMock::PrinterType::VORON_24) {
        state_.init_subjects(false); // Don't register XML bindings in tests
        api_ = std::make_unique<MoonrakerAPIMock>(client_, state_);
    }

  protected:
    MoonrakerClientMock client_;
    PrinterState state_;
    std::unique_ptr<MoonrakerAPIMock> api_;
};

// ============================================================================
// download_file Tests
// ============================================================================

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock download_file finds existing test file",
                 "[mock][api][download]") {
    std::atomic<bool> success_called{false};
    std::atomic<bool> error_called{false};
    std::string downloaded_content;

    api_->download_file(
        "gcodes", "3DBenchy.gcode",
        [&](const std::string& content) {
            downloaded_content = content;
            success_called.store(true);
        },
        [&](const MoonrakerError&) { error_called.store(true); });

    REQUIRE(success_called.load());
    REQUIRE_FALSE(error_called.load());
    REQUIRE(downloaded_content.size() > 100); // Should have substantial content
    // Verify it looks like G-code
    REQUIRE(downloaded_content.find("G") != std::string::npos);
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock download_file returns FILE_NOT_FOUND for missing file",
                 "[mock][api][download]") {
    std::atomic<bool> success_called{false};
    std::atomic<bool> error_called{false};
    MoonrakerError captured_error;

    api_->download_file(
        "gcodes", "nonexistent_file_xyz123.gcode",
        [&](const std::string&) { success_called.store(true); },
        [&](const MoonrakerError& err) {
            captured_error = err;
            error_called.store(true);
        });

    REQUIRE_FALSE(success_called.load());
    REQUIRE(error_called.load());
    REQUIRE(captured_error.type == MoonrakerErrorType::FILE_NOT_FOUND);
    REQUIRE(captured_error.method == "download_file");
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock download_file strips directory from path",
                 "[mock][api][download]") {
    // Test that paths like "subdir/file.gcode" still find "file.gcode" in test assets
    std::atomic<bool> success_called{false};
    std::atomic<bool> error_called{false};

    api_->download_file(
        "gcodes", "some/nested/path/3DBenchy.gcode",
        [&](const std::string& content) {
            success_called.store(true);
            // Verify we got actual content
            REQUIRE(content.size() > 100);
        },
        [&](const MoonrakerError&) { error_called.store(true); });

    REQUIRE(success_called.load());
    REQUIRE_FALSE(error_called.load());
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock download_file works regardless of CWD",
                 "[mock][api][download]") {
    // This test verifies the fallback path search works
    // The implementation should try multiple paths:
    // - assets/test_gcodes/
    // - ../assets/test_gcodes/
    // - ../../assets/test_gcodes/

    std::atomic<bool> success_called{false};

    api_->download_file(
        "gcodes", "3DBenchy.gcode", [&](const std::string&) { success_called.store(true); },
        [&](const MoonrakerError& err) {
            // Log the error for debugging if this fails
            INFO("download_file error: " << err.message);
        });

    // Should succeed from project root or build/bin/
    REQUIRE(success_called.load());
}

// ============================================================================
// upload_file Tests
// ============================================================================

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture, "MoonrakerAPIMock upload_file always succeeds",
                 "[mock][api][upload]") {
    std::atomic<bool> success_called{false};
    std::atomic<bool> error_called{false};

    api_->upload_file(
        "gcodes", "test_upload.gcode", "G28\nG1 X100 Y100 F3000\n",
        [&]() { success_called.store(true); },
        [&](const MoonrakerError&) { error_called.store(true); });

    REQUIRE(success_called.load());
    REQUIRE_FALSE(error_called.load());
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock upload_file_with_name always succeeds", "[mock][api][upload]") {
    std::atomic<bool> success_called{false};
    std::atomic<bool> error_called{false};

    api_->upload_file_with_name(
        "gcodes", "subdir/test.gcode", "custom_filename.gcode", "G28\nM104 S200\n",
        [&]() { success_called.store(true); },
        [&](const MoonrakerError&) { error_called.store(true); });

    REQUIRE(success_called.load());
    REQUIRE_FALSE(error_called.load());
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture, "MoonrakerAPIMock upload_file handles large content",
                 "[mock][api][upload]") {
    std::atomic<bool> success_called{false};

    // Generate a large G-code content (simulate realistic file)
    std::string large_content;
    large_content.reserve(1024 * 100); // ~100KB
    for (int i = 0; i < 5000; i++) {
        large_content += "G1 X" + std::to_string(i % 200) + " Y" + std::to_string(i % 200) + " E" +
                         std::to_string(i * 0.1) + "\n";
    }

    api_->upload_file(
        "gcodes", "large_file.gcode", large_content, [&]() { success_called.store(true); },
        [&](const MoonrakerError&) {});

    REQUIRE(success_called.load());
}

// ============================================================================
// download_file_to_path Tests (Streaming Download)
// ============================================================================

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock download_file_to_path creates file at destination",
                 "[mock][api][download][streaming]") {
    std::atomic<bool> success_called{false};
    std::atomic<bool> error_called{false};
    std::string received_path;

    // Use a temp file for the destination
    std::string dest_path = "/tmp/helix_test_download_" +
                            std::to_string(std::hash<std::string>{}("3DBenchy.gcode")) + ".gcode";

    // Clean up any existing file
    std::remove(dest_path.c_str());

    api_->download_file_to_path(
        "gcodes", "3DBenchy.gcode", dest_path,
        [&](const std::string& path) {
            received_path = path;
            success_called.store(true);
        },
        [&](const MoonrakerError&) { error_called.store(true); });

    REQUIRE(success_called.load());
    REQUIRE_FALSE(error_called.load());
    REQUIRE(received_path == dest_path);

    // Verify file exists and has content
    REQUIRE(std::filesystem::exists(dest_path));
    REQUIRE(std::filesystem::file_size(dest_path) > 100);

    // Clean up
    std::remove(dest_path.c_str());
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock download_file_to_path file content matches source",
                 "[mock][api][download][streaming]") {
    std::atomic<bool> success_called{false};
    std::string dest_path = "/tmp/helix_test_download_content.gcode";

    // Clean up
    std::remove(dest_path.c_str());

    // First, get content via regular download_file
    std::string original_content;
    api_->download_file(
        "gcodes", "3DBenchy.gcode", [&](const std::string& content) { original_content = content; },
        [](const MoonrakerError&) {});

    REQUIRE(original_content.size() > 100);

    // Now download to path
    api_->download_file_to_path(
        "gcodes", "3DBenchy.gcode", dest_path,
        [&](const std::string&) { success_called.store(true); }, [](const MoonrakerError&) {});

    REQUIRE(success_called.load());

    // Read the downloaded file and compare
    std::ifstream file(dest_path, std::ios::binary);
    REQUIRE(file.good());
    std::ostringstream content;
    content << file.rdbuf();
    file.close();

    REQUIRE(content.str() == original_content);

    // Clean up
    std::remove(dest_path.c_str());
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock download_file_to_path returns FILE_NOT_FOUND for missing file",
                 "[mock][api][download][streaming]") {
    std::atomic<bool> success_called{false};
    std::atomic<bool> error_called{false};
    MoonrakerError captured_error;
    std::string dest_path = "/tmp/helix_test_download_missing.gcode";

    api_->download_file_to_path(
        "gcodes", "nonexistent_file_xyz123.gcode", dest_path,
        [&](const std::string&) { success_called.store(true); },
        [&](const MoonrakerError& err) {
            captured_error = err;
            error_called.store(true);
        });

    REQUIRE_FALSE(success_called.load());
    REQUIRE(error_called.load());
    REQUIRE(captured_error.type == MoonrakerErrorType::FILE_NOT_FOUND);
    REQUIRE(captured_error.method == "download_file_to_path");

    // Verify destination file was NOT created
    REQUIRE_FALSE(std::filesystem::exists(dest_path));
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock download_file_to_path strips directory from path",
                 "[mock][api][download][streaming]") {
    std::atomic<bool> success_called{false};
    std::string dest_path = "/tmp/helix_test_download_nested.gcode";

    // Clean up
    std::remove(dest_path.c_str());

    // Path with nested directories should still find the file
    api_->download_file_to_path(
        "gcodes", "some/nested/path/3DBenchy.gcode", dest_path,
        [&](const std::string&) { success_called.store(true); }, [](const MoonrakerError&) {});

    REQUIRE(success_called.load());
    REQUIRE(std::filesystem::exists(dest_path));
    REQUIRE(std::filesystem::file_size(dest_path) > 100);

    // Clean up
    std::remove(dest_path.c_str());
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock download_file handles null success callback",
                 "[mock][api][download]") {
    // Should not crash when success callback is null
    REQUIRE_NOTHROW(
        api_->download_file("gcodes", "3DBenchy.gcode", nullptr, [](const MoonrakerError&) {}));
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock download_file handles null error callback",
                 "[mock][api][download]") {
    // Should not crash when error callback is null (for missing file)
    REQUIRE_NOTHROW(
        api_->download_file("gcodes", "nonexistent.gcode", [](const std::string&) {}, nullptr));
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock upload_file handles null success callback",
                 "[mock][api][upload]") {
    // Should not crash when success callback is null
    REQUIRE_NOTHROW(
        api_->upload_file("gcodes", "test.gcode", "G28", nullptr, [](const MoonrakerError&) {}));
}

// ============================================================================
// Slot-Spool Mapping Tests
// ============================================================================

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock slot-spool mapping is empty initially", "[mock][filament]") {
    // No spools should be assigned to slots initially
    REQUIRE(api_->get_spool_for_slot(0) == 0);
    REQUIRE(api_->get_spool_for_slot(1) == 0);
    REQUIRE(api_->get_spool_for_slot(7) == 0);
    REQUIRE_FALSE(api_->get_spool_info_for_slot(0).has_value());
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture, "MoonrakerAPIMock can assign spool to slot",
                 "[mock][filament]") {
    // Spool 1 exists in mock data (Polymaker Jet Black PLA)
    api_->assign_spool_to_slot(0, 1);

    REQUIRE(api_->get_spool_for_slot(0) == 1);

    auto spool_info = api_->get_spool_info_for_slot(0);
    REQUIRE(spool_info.has_value());
    REQUIRE(spool_info->id == 1);
    REQUIRE(spool_info->vendor == "Polymaker");
    REQUIRE(spool_info->material == "PLA");
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock can assign multiple spools to different slots",
                 "[mock][filament]") {
    api_->assign_spool_to_slot(0, 1); // Polymaker PLA
    api_->assign_spool_to_slot(1, 3); // Elegoo ASA
    api_->assign_spool_to_slot(2, 6); // Overture TPU

    REQUIRE(api_->get_spool_for_slot(0) == 1);
    REQUIRE(api_->get_spool_for_slot(1) == 3);
    REQUIRE(api_->get_spool_for_slot(2) == 6);
    REQUIRE(api_->get_spool_for_slot(3) == 0); // Not assigned

    auto slot0 = api_->get_spool_info_for_slot(0);
    auto slot1 = api_->get_spool_info_for_slot(1);
    auto slot2 = api_->get_spool_info_for_slot(2);

    REQUIRE(slot0->material == "PLA");
    REQUIRE(slot1->material == "ASA");
    REQUIRE(slot2->material == "TPU");
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture, "MoonrakerAPIMock can unassign spool from slot",
                 "[mock][filament]") {
    api_->assign_spool_to_slot(0, 1);
    REQUIRE(api_->get_spool_for_slot(0) == 1);

    api_->unassign_spool_from_slot(0);
    REQUIRE(api_->get_spool_for_slot(0) == 0);
    REQUIRE_FALSE(api_->get_spool_info_for_slot(0).has_value());
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock reassigning spool replaces previous", "[mock][filament]") {
    api_->assign_spool_to_slot(0, 1);
    REQUIRE(api_->get_spool_for_slot(0) == 1);

    api_->assign_spool_to_slot(0, 5); // Replace with different spool
    REQUIRE(api_->get_spool_for_slot(0) == 5);

    auto spool_info = api_->get_spool_info_for_slot(0);
    REQUIRE(spool_info->vendor == "Kingroon");
    REQUIRE(spool_info->material == "PETG");
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture, "MoonrakerAPIMock assigning spool_id 0 unassigns",
                 "[mock][filament]") {
    api_->assign_spool_to_slot(0, 1);
    REQUIRE(api_->get_spool_for_slot(0) == 1);

    api_->assign_spool_to_slot(0, 0); // Assign 0 = unassign
    REQUIRE(api_->get_spool_for_slot(0) == 0);
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock ignores assignment of nonexistent spool", "[mock][filament]") {
    api_->assign_spool_to_slot(0, 9999); // Doesn't exist
    REQUIRE(api_->get_spool_for_slot(0) == 0);
}

// ============================================================================
// Filament Consumption Tests
// ============================================================================

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock consume_filament decrements active spool weight",
                 "[mock][filament]") {
    // Get initial weight of active spool (spool 1 by default)
    auto& spools = api_->get_mock_spools();
    float initial_weight = spools[0].remaining_weight_g; // Spool 1 is first

    api_->consume_filament(50.0f); // Consume 50 grams

    REQUIRE(spools[0].remaining_weight_g == Catch::Approx(initial_weight - 50.0f));
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock consume_filament uses slot's assigned spool",
                 "[mock][filament]") {
    // Assign spool 5 (Kingroon PETG, 1000g) to slot 2
    api_->assign_spool_to_slot(2, 5);

    auto& spools = api_->get_mock_spools();
    SpoolInfo* spool5 = nullptr;
    for (auto& s : spools) {
        if (s.id == 5) {
            spool5 = &s;
            break;
        }
    }
    REQUIRE(spool5 != nullptr);

    float initial_weight = spool5->remaining_weight_g;
    REQUIRE(initial_weight == Catch::Approx(1000.0f));

    api_->consume_filament(75.0f, 2); // Consume from slot 2

    REQUIRE(spool5->remaining_weight_g == Catch::Approx(925.0f));
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock consume_filament doesn't go negative", "[mock][filament]") {
    // Spool 4 has only 100g remaining
    api_->set_active_spool(4, []() {}, nullptr);

    auto& spools = api_->get_mock_spools();
    SpoolInfo* spool4 = nullptr;
    for (auto& s : spools) {
        if (s.id == 4) {
            spool4 = &s;
            break;
        }
    }
    REQUIRE(spool4 != nullptr);

    api_->consume_filament(200.0f); // Try to consume more than available

    REQUIRE(spool4->remaining_weight_g == 0.0f); // Should clamp to 0
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock consume_filament updates remaining length", "[mock][filament]") {
    auto& spools = api_->get_mock_spools();
    float initial_length = spools[0].remaining_length_m;

    api_->consume_filament(100.0f); // Consume 10% of a 1kg spool

    // Length should decrease proportionally
    REQUIRE(spools[0].remaining_length_m < initial_length);
}

TEST_CASE_METHOD(MoonrakerAPIMockTestFixture,
                 "MoonrakerAPIMock set_active_spool updates is_active flag", "[mock][filament]") {
    auto& spools = api_->get_mock_spools();

    // Initially spool 1 is active
    REQUIRE(spools[0].is_active == true);
    REQUIRE(spools[1].is_active == false);

    api_->set_active_spool(2, []() {}, nullptr);

    // Now spool 2 is active, spool 1 is not
    REQUIRE(spools[0].is_active == false);
    REQUIRE(spools[1].is_active == true);
}
