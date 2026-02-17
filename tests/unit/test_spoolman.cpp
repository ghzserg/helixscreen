// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"
#include "spoolman_types.h" // For SpoolInfo, VendorInfo, FilamentInfo

#include <set>

#include "../catch_amalgamated.hpp"

using namespace helix;
// ============================================================================
// SpoolInfo Struct Tests
// ============================================================================

TEST_CASE("SpoolInfo - remaining_percent calculation", "[filament]") {
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

TEST_CASE("SpoolInfo - is_low threshold detection", "[filament]") {
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

TEST_CASE("SpoolInfo - display_name formatting", "[filament]") {
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

TEST_CASE("SpoolInfo - default initialization", "[filament]") {
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

TEST_CASE("FilamentUsageRecord - default initialization", "[filament]") {
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
// VendorInfo Tests
// ============================================================================

TEST_CASE("VendorInfo - default initialization", "[filament]") {
    VendorInfo vendor;

    SECTION("All fields default correctly") {
        REQUIRE(vendor.id == 0);
        REQUIRE(vendor.name.empty());
        REQUIRE(vendor.url.empty());
    }
}

TEST_CASE("VendorInfo - display_name formatting", "[filament]") {
    VendorInfo vendor;

    SECTION("Name returns name") {
        vendor.name = "Hatchbox";
        REQUIRE(vendor.display_name() == "Hatchbox");
    }

    SECTION("Empty name returns Unknown Vendor") {
        REQUIRE(vendor.display_name() == "Unknown Vendor");
    }
}

// ============================================================================
// FilamentInfo Tests
// ============================================================================

TEST_CASE("FilamentInfo - default initialization", "[filament]") {
    FilamentInfo filament;

    SECTION("All numeric fields default correctly") {
        REQUIRE(filament.id == 0);
        REQUIRE(filament.vendor_id == 0);
        REQUIRE(filament.density == 0.0f);
        REQUIRE(filament.diameter == Catch::Approx(1.75f));
        REQUIRE(filament.weight == 0.0f);
        REQUIRE(filament.spool_weight == 0.0f);
        REQUIRE(filament.nozzle_temp_min == 0);
        REQUIRE(filament.nozzle_temp_max == 0);
        REQUIRE(filament.bed_temp_min == 0);
        REQUIRE(filament.bed_temp_max == 0);
    }

    SECTION("Strings default to empty") {
        REQUIRE(filament.vendor_name.empty());
        REQUIRE(filament.material.empty());
        REQUIRE(filament.color_name.empty());
        REQUIRE(filament.color_hex.empty());
    }

    SECTION("Diameter defaults to 1.75mm") {
        REQUIRE(filament.diameter == Catch::Approx(1.75f));
    }
}

TEST_CASE("FilamentInfo - display_name formatting", "[filament]") {
    FilamentInfo filament;

    SECTION("Full info formats correctly") {
        filament.vendor_name = "Polymaker";
        filament.material = "PLA";
        filament.color_name = "Jet Black";
        REQUIRE(filament.display_name() == "Polymaker PLA - Jet Black");
    }

    SECTION("No color omits dash") {
        filament.vendor_name = "eSUN";
        filament.material = "PETG";
        REQUIRE(filament.display_name() == "eSUN PETG");
    }

    SECTION("No vendor omits vendor") {
        filament.material = "ABS";
        filament.color_name = "Red";
        REQUIRE(filament.display_name() == "ABS - Red");
    }

    SECTION("Only material") {
        filament.material = "TPU";
        REQUIRE(filament.display_name() == "TPU");
    }

    SECTION("Empty returns Unknown Filament") {
        REQUIRE(filament.display_name() == "Unknown Filament");
    }
}

// ============================================================================
// MoonrakerAPIMock Spoolman Tests
// ============================================================================

TEST_CASE("MoonrakerAPIMock - get_spoolman_status", "[filament][mock]") {
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

TEST_CASE("MoonrakerAPIMock - get_spoolman_spools", "[filament][mock]") {
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

TEST_CASE("MoonrakerAPIMock - set_active_spool", "[filament][mock]") {
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
// MoonrakerAPIMock - Spoolman CRUD Tests
// ============================================================================

TEST_CASE("MoonrakerAPIMock - get_spoolman_vendors", "[filament][mock]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    SECTION("Returns vendor list derived from spools") {
        bool callback_called = false;
        api.get_spoolman_vendors(
            [&](const std::vector<VendorInfo>& vendors) {
                callback_called = true;
                // Should have multiple unique vendors from mock spools
                REQUIRE(vendors.size() > 0);
                // Each vendor should have a valid name
                for (const auto& v : vendors) {
                    REQUIRE(v.id > 0);
                    REQUIRE(!v.name.empty());
                }
            },
            [](const MoonrakerError&) { FAIL("Error callback should not be called"); });

        REQUIRE(callback_called);
    }

    SECTION("Vendors are deduplicated") {
        std::set<std::string> vendor_names;
        api.get_spoolman_vendors(
            [&](const std::vector<VendorInfo>& vendors) {
                for (const auto& v : vendors) {
                    REQUIRE(vendor_names.find(v.name) == vendor_names.end());
                    vendor_names.insert(v.name);
                }
            },
            [](const MoonrakerError&) {});
    }
}

TEST_CASE("MoonrakerAPIMock - get_spoolman_filaments", "[filament][mock]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    SECTION("Returns filament list") {
        bool callback_called = false;
        api.get_spoolman_filaments(
            [&](const std::vector<FilamentInfo>& filaments) {
                callback_called = true;
                REQUIRE(filaments.size() > 0);
                for (const auto& f : filaments) {
                    REQUIRE(f.id > 0);
                    REQUIRE(!f.material.empty());
                }
            },
            [](const MoonrakerError&) { FAIL("Error callback should not be called"); });

        REQUIRE(callback_called);
    }
}

TEST_CASE("MoonrakerAPIMock - create_spoolman_vendor", "[filament][mock]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    SECTION("Creates vendor and returns it") {
        nlohmann::json data;
        data["name"] = "Test Vendor";
        data["url"] = "https://example.com";

        bool callback_called = false;
        api.create_spoolman_vendor(
            data,
            [&](const VendorInfo& vendor) {
                callback_called = true;
                REQUIRE(vendor.id > 0);
                REQUIRE(vendor.name == "Test Vendor");
                REQUIRE(vendor.url == "https://example.com");
            },
            [](const MoonrakerError&) { FAIL("Error callback should not be called"); });

        REQUIRE(callback_called);
    }
}

TEST_CASE("MoonrakerAPIMock - create_spoolman_filament", "[filament][mock]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    SECTION("Creates filament and returns it") {
        nlohmann::json data;
        data["material"] = "PETG";
        data["name"] = "Ocean Blue";
        data["color_hex"] = "#0077B6";
        data["diameter"] = 1.75f;
        data["weight"] = 1000.0f;

        bool callback_called = false;
        api.create_spoolman_filament(
            data,
            [&](const FilamentInfo& filament) {
                callback_called = true;
                REQUIRE(filament.id > 0);
                REQUIRE(filament.material == "PETG");
                REQUIRE(filament.color_name == "Ocean Blue");
                REQUIRE(filament.color_hex == "#0077B6");
            },
            [](const MoonrakerError&) { FAIL("Error callback should not be called"); });

        REQUIRE(callback_called);
    }
}

TEST_CASE("MoonrakerAPIMock - create_spoolman_spool", "[filament][mock]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    SECTION("Creates spool and adds to list") {
        size_t initial_count = 0;
        api.get_spoolman_spools(
            [&](const std::vector<SpoolInfo>& spools) { initial_count = spools.size(); },
            [](const MoonrakerError&) {});

        nlohmann::json data;
        data["filament_id"] = 1;
        data["initial_weight"] = 800.0;
        data["spool_weight"] = 200.0;

        bool callback_called = false;
        api.create_spoolman_spool(
            data,
            [&](const SpoolInfo& spool) {
                callback_called = true;
                REQUIRE(spool.id > 0);
                REQUIRE(spool.initial_weight_g == Catch::Approx(800.0));
                REQUIRE(spool.spool_weight_g == Catch::Approx(200.0));
            },
            [](const MoonrakerError&) { FAIL("Error callback should not be called"); });

        REQUIRE(callback_called);

        // Verify spool count increased
        api.get_spoolman_spools(
            [&](const std::vector<SpoolInfo>& spools) {
                REQUIRE(spools.size() == initial_count + 1);
            },
            [](const MoonrakerError&) {});
    }
}

TEST_CASE("MoonrakerAPIMock - delete_spoolman_spool", "[filament][mock]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    SECTION("Deletes spool from list") {
        size_t initial_count = 0;
        api.get_spoolman_spools(
            [&](const std::vector<SpoolInfo>& spools) { initial_count = spools.size(); },
            [](const MoonrakerError&) {});

        REQUIRE(initial_count > 0);

        // Delete spool with ID 1
        bool callback_called = false;
        api.delete_spoolman_spool(
            1, [&]() { callback_called = true; },
            [](const MoonrakerError&) { FAIL("Error callback should not be called"); });

        REQUIRE(callback_called);

        // Verify spool count decreased
        api.get_spoolman_spools(
            [&](const std::vector<SpoolInfo>& spools) {
                REQUIRE(spools.size() == initial_count - 1);
                // Verify spool 1 is gone
                for (const auto& s : spools) {
                    REQUIRE(s.id != 1);
                }
            },
            [](const MoonrakerError&) {});
    }

    SECTION("Deleting non-existent spool still succeeds") {
        bool callback_called = false;
        api.delete_spoolman_spool(
            9999, [&]() { callback_called = true; }, [](const MoonrakerError&) {});

        REQUIRE(callback_called);
    }
}

TEST_CASE("MoonrakerAPIMock - update_spoolman_spool", "[filament][mock]") {
    MoonrakerClientMock client;
    PrinterState state;
    MoonrakerAPIMock api(client, state);

    SECTION("Updates remaining_weight field") {
        // Get initial weight of first spool
        double initial_weight = 0;
        api.get_spoolman_spools(
            [&initial_weight](const std::vector<SpoolInfo>& spools) {
                REQUIRE(!spools.empty());
                initial_weight = spools[0].remaining_weight_g;
            },
            [](const MoonrakerError&) { FAIL("Failed to get spools"); });

        // Update the spool
        nlohmann::json patch;
        patch["remaining_weight"] = 42.0;

        bool callback_called = false;
        int spool_id = 1; // First mock spool
        api.update_spoolman_spool(
            spool_id, patch, [&callback_called]() { callback_called = true; },
            [](const MoonrakerError&) { FAIL("Update should not fail"); });

        REQUIRE(callback_called);

        // Verify the weight was updated
        api.get_spoolman_spools(
            [spool_id](const std::vector<SpoolInfo>& spools) {
                for (const auto& s : spools) {
                    if (s.id == spool_id) {
                        REQUIRE(s.remaining_weight_g == Catch::Approx(42.0));
                        return;
                    }
                }
                FAIL("Spool not found after update");
            },
            [](const MoonrakerError&) { FAIL("Failed to get spools after update"); });
    }
}

TEST_CASE("SpoolInfo - new fields have defaults", "[filament]") {
    SpoolInfo spool;

    REQUIRE(spool.price == 0.0);
    REQUIRE(spool.lot_nr.empty());
    REQUIRE(spool.comment.empty());
}

// ============================================================================
// JSON Null Handling Tests (server.spoolman.status parsing)
// ============================================================================

TEST_CASE("Spoolman status - spool_id null handling", "[filament][parsing]") {
    // This test validates parsing of server.spoolman.status responses.
    // When no spool is active, Moonraker returns: {"spool_id": null}
    // Must use null-safe pattern: check contains() && !is_null() before get<int>()
    // Using json::value() with null throws type_error.302.

    SECTION("null spool_id should return default value (0)") {
        // Simulate Moonraker response when no spool is active
        auto response = nlohmann::json::parse(R"({
            "result": {
                "spoolman_connected": true,
                "spool_id": null
            }
        })");

        const auto& result = response["result"];
        bool connected = result.value("spoolman_connected", false);

        // Use null-safe pattern (matches moonraker_api_advanced.cpp:1195)
        int active_spool_id = 0;
        if (result.contains("spool_id") && !result["spool_id"].is_null()) {
            active_spool_id = result["spool_id"].get<int>();
        }

        REQUIRE(connected == true);
        REQUIRE(active_spool_id == 0); // null should fall back to default 0
    }

    SECTION("integer spool_id still works normally") {
        auto response = nlohmann::json::parse(R"({
            "result": {
                "spoolman_connected": true,
                "spool_id": 42
            }
        })");

        const auto& result = response["result"];
        int active_spool_id = result.value("spool_id", 0);

        REQUIRE(active_spool_id == 42);
    }

    SECTION("missing spool_id uses default") {
        auto response = nlohmann::json::parse(R"({
            "result": {
                "spoolman_connected": true
            }
        })");

        const auto& result = response["result"];
        int active_spool_id = result.value("spool_id", 0);

        REQUIRE(active_spool_id == 0);
    }
}

// ============================================================================
// filter_spools Tests
// ============================================================================

static std::vector<SpoolInfo> make_filter_test_spools() {
    std::vector<SpoolInfo> spools;

    SpoolInfo s1;
    s1.id = 1;
    s1.vendor = "Polymaker";
    s1.material = "PLA";
    s1.color_name = "Jet Black";
    spools.push_back(s1);

    SpoolInfo s2;
    s2.id = 2;
    s2.vendor = "eSUN";
    s2.material = "PETG";
    s2.color_name = "Blue";
    spools.push_back(s2);

    SpoolInfo s3;
    s3.id = 3;
    s3.vendor = "Polymaker";
    s3.material = "ASA";
    s3.color_name = "Red";
    spools.push_back(s3);

    SpoolInfo s4;
    s4.id = 42;
    s4.vendor = "Hatchbox";
    s4.material = "PLA";
    s4.color_name = "White";
    spools.push_back(s4);

    return spools;
}

TEST_CASE("filter_spools - empty query returns all", "[filament][filter]") {
    auto spools = make_filter_test_spools();
    auto result = filter_spools(spools, "");
    REQUIRE(result.size() == spools.size());
}

TEST_CASE("filter_spools - whitespace-only query returns all", "[filament][filter]") {
    auto spools = make_filter_test_spools();
    auto result = filter_spools(spools, "   ");
    REQUIRE(result.size() == spools.size());
}

TEST_CASE("filter_spools - single term matches material", "[filament][filter]") {
    auto spools = make_filter_test_spools();
    auto result = filter_spools(spools, "PLA");
    REQUIRE(result.size() == 2);
    REQUIRE(result[0].id == 1);
    REQUIRE(result[1].id == 42);
}

TEST_CASE("filter_spools - single term matches vendor", "[filament][filter]") {
    auto spools = make_filter_test_spools();
    auto result = filter_spools(spools, "polymaker");
    REQUIRE(result.size() == 2);
    REQUIRE(result[0].id == 1);
    REQUIRE(result[1].id == 3);
}

TEST_CASE("filter_spools - multi-term AND matching", "[filament][filter]") {
    auto spools = make_filter_test_spools();
    auto result = filter_spools(spools, "polymaker pla");
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].id == 1);
}

TEST_CASE("filter_spools - case insensitive", "[filament][filter]") {
    auto spools = make_filter_test_spools();
    auto result = filter_spools(spools, "ESUN petg");
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].id == 2);
}

TEST_CASE("filter_spools - spool ID search with #", "[filament][filter]") {
    auto spools = make_filter_test_spools();
    auto result = filter_spools(spools, "#42");
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].id == 42);
}

TEST_CASE("filter_spools - spool ID search without #", "[filament][filter]") {
    auto spools = make_filter_test_spools();
    // "42" matches spool #42's searchable text which contains "#42"
    auto result = filter_spools(spools, "42");
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].id == 42);
}

TEST_CASE("filter_spools - color name search", "[filament][filter]") {
    auto spools = make_filter_test_spools();
    auto result = filter_spools(spools, "blue");
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].id == 2);
}

TEST_CASE("filter_spools - no matches returns empty", "[filament][filter]") {
    auto spools = make_filter_test_spools();
    auto result = filter_spools(spools, "nonexistent");
    REQUIRE(result.empty());
}

TEST_CASE("filter_spools - empty spool list returns empty", "[filament][filter]") {
    std::vector<SpoolInfo> empty;
    auto result = filter_spools(empty, "PLA");
    REQUIRE(result.empty());
}

// ============================================================================
// MoonrakerAPIMock - Filament Persistence & Patching Tests
// ============================================================================

TEST_CASE("Mock persists created filaments", "[spoolman][mock]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    // Create a filament
    nlohmann::json filament_data;
    filament_data["material"] = "PETG";
    filament_data["name"] = "Blue";
    filament_data["color_hex"] = "#0000FF";
    filament_data["vendor_id"] = 1;

    FilamentInfo created;
    api.create_spoolman_filament(
        filament_data, [&](const FilamentInfo& f) { created = f; }, nullptr);
    REQUIRE(created.id > 0);

    // Verify it appears in subsequent filament list
    std::vector<FilamentInfo> filaments;
    api.get_spoolman_filaments([&](const std::vector<FilamentInfo>& list) { filaments = list; },
                               nullptr);

    bool found = false;
    for (const auto& f : filaments) {
        if (f.id == created.id) {
            found = true;
            REQUIRE(f.material == "PETG");
            REQUIRE(f.color_name == "Blue");
        }
    }
    REQUIRE(found);
}

TEST_CASE("Mock update_spoolman_spool supports filament_id patch", "[spoolman][mock]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    auto& spools = api.get_mock_spools();
    int spool_id = spools[0].id;
    int original_filament_id = spools[0].filament_id;

    nlohmann::json patch;
    patch["filament_id"] = 999;

    bool success = false;
    api.update_spoolman_spool(spool_id, patch, [&]() { success = true; }, nullptr);

    REQUIRE(success);
    REQUIRE(spools[0].filament_id == 999);
    REQUIRE(spools[0].filament_id != original_filament_id);
}

TEST_CASE("SpoolInfo - realistic spool scenarios", "[filament][integration]") {
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
