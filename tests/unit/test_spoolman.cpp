// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"
#include "spoolman_types.h" // For SpoolInfo

#include "../catch_amalgamated.hpp"

// ============================================================================
// SpoolInfo Struct Tests
// ============================================================================

TEST_CASE("SpoolInfo - remaining_percent calculation", "[spoolman]") {
    SpoolInfo spool;

    SECTION("Full spool returns 100%") {
        spool.initial_weight_g = 1000.0;
        spool.remaining_weight_g = 1000.0;
        REQUIRE(spool.remaining_percent() == Catch::Approx(100.0));
    }

    SECTION("Half spool returns 50%") {
        spool.initial_weight_g = 1000.0;
        spool.remaining_weight_g = 500.0;
        REQUIRE(spool.remaining_percent() == Catch::Approx(50.0));
    }

    SECTION("Empty spool returns 0%") {
        spool.initial_weight_g = 1000.0;
        spool.remaining_weight_g = 0.0;
        REQUIRE(spool.remaining_percent() == Catch::Approx(0.0));
    }

    SECTION("Partial spool calculates correctly") {
        spool.initial_weight_g = 1000.0;
        spool.remaining_weight_g = 850.0;
        REQUIRE(spool.remaining_percent() == Catch::Approx(85.0));
    }

    SECTION("Non-standard spool weight works") {
        spool.initial_weight_g = 750.0; // 750g spool
        spool.remaining_weight_g = 500.0;
        REQUIRE(spool.remaining_percent() == Catch::Approx(66.666666).margin(0.001));
    }

    SECTION("Zero initial weight returns 0% (avoids division by zero)") {
        spool.initial_weight_g = 0.0;
        spool.remaining_weight_g = 100.0;
        REQUIRE(spool.remaining_percent() == Catch::Approx(0.0));
    }

    SECTION("Negative initial weight returns 0%") {
        spool.initial_weight_g = -100.0;
        spool.remaining_weight_g = 50.0;
        REQUIRE(spool.remaining_percent() == Catch::Approx(0.0));
    }
}

TEST_CASE("SpoolInfo - is_low threshold detection", "[spoolman]") {
    SpoolInfo spool;

    SECTION("Default threshold is 100g") {
        spool.remaining_weight_g = 99.0;
        REQUIRE(spool.is_low() == true);

        spool.remaining_weight_g = 100.0;
        REQUIRE(spool.is_low() == false);

        spool.remaining_weight_g = 101.0;
        REQUIRE(spool.is_low() == false);
    }

    SECTION("Custom threshold works") {
        spool.remaining_weight_g = 200.0;
        REQUIRE(spool.is_low(250.0) == true);
        REQUIRE(spool.is_low(200.0) == false);
        REQUIRE(spool.is_low(150.0) == false);
    }

    SECTION("Empty spool is always low") {
        spool.remaining_weight_g = 0.0;
        REQUIRE(spool.is_low() == true);
        REQUIRE(spool.is_low(0.0) == false); // Edge case: threshold 0
    }

    SECTION("Very low threshold") {
        spool.remaining_weight_g = 5.0;
        REQUIRE(spool.is_low(10.0) == true);
        REQUIRE(spool.is_low(5.0) == false);
        REQUIRE(spool.is_low(1.0) == false);
    }
}

TEST_CASE("SpoolInfo - display_name formatting", "[spoolman]") {
    SpoolInfo spool;

    SECTION("Full info formats correctly") {
        spool.vendor = "Polymaker";
        spool.material = "PLA";
        spool.color_name = "Jet Black";
        REQUIRE(spool.display_name() == "Polymaker PLA - Jet Black");
    }

    SECTION("No color_name omits dash") {
        spool.vendor = "eSUN";
        spool.material = "PETG";
        spool.color_name = "";
        REQUIRE(spool.display_name() == "eSUN PETG");
    }

    SECTION("No vendor omits vendor") {
        spool.vendor = "";
        spool.material = "ABS";
        spool.color_name = "Red";
        REQUIRE(spool.display_name() == "ABS - Red");
    }

    SECTION("Only material") {
        spool.vendor = "";
        spool.material = "TPU";
        spool.color_name = "";
        REQUIRE(spool.display_name() == "TPU");
    }

    SECTION("Empty info returns 'Unknown Spool'") {
        spool.vendor = "";
        spool.material = "";
        spool.color_name = "";
        REQUIRE(spool.display_name() == "Unknown Spool");
    }

    SECTION("Only color returns color with dash") {
        spool.vendor = "";
        spool.material = "";
        spool.color_name = "Blue";
        REQUIRE(spool.display_name() == " - Blue");
    }

    SECTION("Complex color names preserved") {
        spool.vendor = "Eryone";
        spool.material = "Silk PLA";
        spool.color_name = "Gold/Silver/Copper Tri-Color";
        REQUIRE(spool.display_name() == "Eryone Silk PLA - Gold/Silver/Copper Tri-Color");
    }
}

TEST_CASE("SpoolInfo - default initialization", "[spoolman]") {
    SpoolInfo spool;

    SECTION("All numeric fields default to 0") {
        REQUIRE(spool.id == 0);
        REQUIRE(spool.remaining_weight_g == 0.0);
        REQUIRE(spool.remaining_length_m == 0.0);
        REQUIRE(spool.spool_weight_g == 0.0);
        REQUIRE(spool.initial_weight_g == 0.0);
        REQUIRE(spool.nozzle_temp_min == 0);
        REQUIRE(spool.nozzle_temp_max == 0);
        REQUIRE(spool.nozzle_temp_recommended == 0);
        REQUIRE(spool.bed_temp_min == 0);
        REQUIRE(spool.bed_temp_max == 0);
        REQUIRE(spool.bed_temp_recommended == 0);
    }

    SECTION("Strings default to empty") {
        REQUIRE(spool.vendor.empty());
        REQUIRE(spool.material.empty());
        REQUIRE(spool.color_name.empty());
        REQUIRE(spool.color_hex.empty());
    }

    SECTION("is_active defaults to false") {
        REQUIRE(spool.is_active == false);
    }
}

// ============================================================================
// FilamentUsageRecord Tests
// ============================================================================

TEST_CASE("FilamentUsageRecord - default initialization", "[spoolman]") {
    FilamentUsageRecord record;

    SECTION("All fields default correctly") {
        REQUIRE(record.spool_id == 0);
        REQUIRE(record.used_weight_g == 0.0);
        REQUIRE(record.used_length_m == 0.0);
        REQUIRE(record.print_filename.empty());
        REQUIRE(record.timestamp == 0.0);
    }
}

// ============================================================================
// MoonrakerAPIMock Spoolman Tests
// ============================================================================

TEST_CASE("MoonrakerAPIMock - get_spoolman_status", "[spoolman][mock]") {
    // Create mock client and state
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    SECTION("Returns connected by default") {
        bool callback_called = false;
        api.get_spoolman_status(
            [&](bool connected, int active_spool_id) {
                callback_called = true;
                REQUIRE(connected == true);
                REQUIRE(active_spool_id == 1); // Default active spool
            },
            [](const MoonrakerError&) { FAIL("Error callback should not be called"); });

        REQUIRE(callback_called);
    }

    SECTION("Can be disabled") {
        api.set_mock_spoolman_enabled(false);

        bool callback_called = false;
        api.get_spoolman_status(
            [&](bool connected, int /*active_spool_id*/) {
                callback_called = true;
                REQUIRE(connected == false);
                // active_spool_id still returns the cached value
            },
            [](const MoonrakerError&) {});

        REQUIRE(callback_called);
    }
}

TEST_CASE("MoonrakerAPIMock - get_spoolman_spools", "[spoolman][mock]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    SECTION("Returns non-empty spool list") {
        bool callback_called = false;
        api.get_spoolman_spools(
            [&](const std::vector<SpoolInfo>& spools) {
                callback_called = true;
                REQUIRE(spools.size() == 19); // Mock has 19 spools
            },
            [](const MoonrakerError&) { FAIL("Error callback should not be called"); });

        REQUIRE(callback_called);
    }

    SECTION("First spool is active by default") {
        api.get_spoolman_spools(
            [&](const std::vector<SpoolInfo>& spools) {
                REQUIRE(spools.size() > 0);
                REQUIRE(spools[0].is_active == true);

                // All other spools should not be active
                for (size_t i = 1; i < spools.size(); ++i) {
                    REQUIRE(spools[i].is_active == false);
                }
            },
            [](const MoonrakerError&) {});
    }

    SECTION("Spools have valid data") {
        api.get_spoolman_spools(
            [&](const std::vector<SpoolInfo>& spools) {
                for (const auto& spool : spools) {
                    // Each spool should have basic info
                    REQUIRE(spool.id > 0);
                    REQUIRE(!spool.vendor.empty());
                    REQUIRE(!spool.material.empty());
                    REQUIRE(spool.initial_weight_g > 0);
                    REQUIRE(spool.remaining_weight_g >= 0);
                    REQUIRE(spool.remaining_weight_g <= spool.initial_weight_g);
                }
            },
            [](const MoonrakerError&) {});
    }

    SECTION("Has diverse materials") {
        std::set<std::string> materials;
        api.get_spoolman_spools(
            [&](const std::vector<SpoolInfo>& spools) {
                for (const auto& spool : spools) {
                    materials.insert(spool.material);
                }
            },
            [](const MoonrakerError&) {});

        // Should have at least 5 different materials
        REQUIRE(materials.size() >= 5);
    }
}

TEST_CASE("MoonrakerAPIMock - set_active_spool", "[spoolman][mock]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    SECTION("Changes active spool") {
        bool success_called = false;
        api.set_active_spool(
            5, [&]() { success_called = true; },
            [](const MoonrakerError&) { FAIL("Error should not be called"); });

        REQUIRE(success_called);

        // Verify the change via get_spoolman_status
        api.get_spoolman_status(
            [](bool /*connected*/, int active_spool_id) { REQUIRE(active_spool_id == 5); },
            [](const MoonrakerError&) {});
    }

    SECTION("Updates is_active flag on spools") {
        // Set spool 3 as active
        api.set_active_spool(3, []() {}, [](const MoonrakerError&) {});

        // Verify spool 3 has is_active=true, others false
        api.get_spoolman_spools(
            [](const std::vector<SpoolInfo>& spools) {
                for (const auto& spool : spools) {
                    if (spool.id == 3) {
                        REQUIRE(spool.is_active == true);
                    } else {
                        REQUIRE(spool.is_active == false);
                    }
                }
            },
            [](const MoonrakerError&) {});
    }

    SECTION("Setting non-existent spool ID still succeeds") {
        // Mock doesn't validate IDs - that's the server's job
        bool success_called = false;
        api.set_active_spool(9999, [&]() { success_called = true; }, [](const MoonrakerError&) {});

        REQUIRE(success_called);
    }
}

// ============================================================================
// Integration-style Tests
// ============================================================================

TEST_CASE("SpoolInfo - realistic spool scenarios", "[spoolman][integration]") {
    SECTION("Typical PLA spool usage") {
        SpoolInfo spool;
        spool.vendor = "Polymaker";
        spool.material = "PLA";
        spool.color_name = "Jet Black";
        spool.color_hex = "1A1A2E";
        spool.initial_weight_g = 1000.0;
        spool.remaining_weight_g = 850.0;
        spool.nozzle_temp_recommended = 210;
        spool.bed_temp_recommended = 60;

        REQUIRE(spool.remaining_percent() == Catch::Approx(85.0));
        REQUIRE(spool.is_low() == false);
        REQUIRE(spool.is_low(900.0) == true); // Custom threshold
        REQUIRE(spool.display_name() == "Polymaker PLA - Jet Black");
    }

    SECTION("Nearly empty ASA spool") {
        SpoolInfo spool;
        spool.vendor = "Flashforge";
        spool.material = "ASA";
        spool.color_name = "Fire Engine Red";
        spool.initial_weight_g = 1000.0;
        spool.remaining_weight_g = 50.0;

        REQUIRE(spool.remaining_percent() == Catch::Approx(5.0));
        REQUIRE(spool.is_low() == true);
        REQUIRE(spool.is_low(50.0) == false);
    }

    SECTION("Engineering filament with 750g spool") {
        SpoolInfo spool;
        spool.vendor = "Polymaker";
        spool.material = "PC";
        spool.color_name = "PolyMax PC Grey";
        spool.initial_weight_g = 750.0;
        spool.remaining_weight_g = 500.0;
        spool.nozzle_temp_recommended = 270;
        spool.bed_temp_recommended = 100;

        REQUIRE(spool.remaining_percent() == Catch::Approx(66.666666).margin(0.001));
        REQUIRE(spool.is_low() == false);
    }
}
