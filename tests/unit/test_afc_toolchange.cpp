// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_afc.h"
#include "ams_types.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// Test helper — inherits from AmsBackendAfc to call protected parse methods
class AfcToolchangeTestHelper : public AmsBackendAfc {
  public:
    AfcToolchangeTestHelper() : AmsBackendAfc(nullptr, nullptr) {}

    void initialize_test_lanes(int count) {
        std::vector<std::string> names;
        for (int i = 0; i < count; ++i) {
            names.push_back("lane" + std::to_string(i + 1));
        }
        initialize_slots(names);
    }

    void feed_afc_state(const nlohmann::json& afc_data) {
        nlohmann::json notification;
        nlohmann::json params;
        params["AFC"] = afc_data;
        notification["params"] = nlohmann::json::array({params, 0.0});
        handle_status_update(notification);
    }

    const AmsSystemInfo& info() const {
        return system_info_;
    }
};

TEST_CASE("AFC toolchange fields in AmsSystemInfo default to safe values", "[afc][toolchange]") {
    AmsSystemInfo info;
    REQUIRE(info.current_toolchange == -1);
    REQUIRE(info.number_of_toolchanges == 0);
}

TEST_CASE("AFC backend parses toolchange fields from status update", "[afc][toolchange]") {
    AfcToolchangeTestHelper afc;
    afc.initialize_test_lanes(4);

    SECTION("both fields present") {
        afc.feed_afc_state(
            {{"current_toolchange", 2}, {"number_of_toolchanges", 5}, {"current_state", "Idle"}});
        REQUIRE(afc.info().current_toolchange == 2);
        REQUIRE(afc.info().number_of_toolchanges == 5);
    }

    SECTION("fields missing — keeps defaults") {
        afc.feed_afc_state({{"current_state", "Idle"}});
        REQUIRE(afc.info().current_toolchange == -1);
        REQUIRE(afc.info().number_of_toolchanges == 0);
    }

    SECTION("pre-first-swap state: current=-1, total=5") {
        afc.feed_afc_state(
            {{"current_toolchange", -1}, {"number_of_toolchanges", 5}, {"current_state", "Idle"}});
        REQUIRE(afc.info().current_toolchange == -1);
        REQUIRE(afc.info().number_of_toolchanges == 5);
    }

    SECTION("print complete resets to zero") {
        afc.feed_afc_state({{"current_toolchange", 4}, {"number_of_toolchanges", 5}});
        REQUIRE(afc.info().current_toolchange == 4);

        afc.feed_afc_state({{"current_toolchange", 0}, {"number_of_toolchanges", 0}});
        REQUIRE(afc.info().current_toolchange == 0);
        REQUIRE(afc.info().number_of_toolchanges == 0);
    }
}

#include "ams_backend_mock.h"

TEST_CASE("Mock backend supports toolchange simulation", "[afc][toolchange][mock]") {
    AmsBackendMock mock(4);

    SECTION("set_toolchange_progress updates system info") {
        mock.set_toolchange_progress(2, 5);
        auto info = mock.get_system_info();
        REQUIRE(info.current_toolchange == 2);
        REQUIRE(info.number_of_toolchanges == 5);
    }

    SECTION("defaults are -1 and 0") {
        auto info = mock.get_system_info();
        REQUIRE(info.current_toolchange == -1);
        REQUIRE(info.number_of_toolchanges == 0);
    }
}
