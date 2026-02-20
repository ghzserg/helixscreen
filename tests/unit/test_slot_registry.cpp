// SPDX-License-Identifier: GPL-3.0-or-later
#include "slot_registry.h"

#include "../catch_amalgamated.hpp"

using namespace helix::printer;

TEST_CASE("SlotRegistry single-unit initialization", "[slot_registry][init]") {
    SlotRegistry reg;

    REQUIRE_FALSE(reg.is_initialized());
    REQUIRE(reg.slot_count() == 0);

    std::vector<std::string> names = {"lane0", "lane1", "lane2", "lane3"};
    reg.initialize("Turtle_1", names);

    SECTION("basic state") {
        REQUIRE(reg.is_initialized());
        REQUIRE(reg.slot_count() == 4);
        REQUIRE(reg.unit_count() == 1);
    }

    SECTION("slot access by index") {
        for (int i = 0; i < 4; ++i) {
            const auto* entry = reg.get(i);
            REQUIRE(entry != nullptr);
            REQUIRE(entry->global_index == i);
            REQUIRE(entry->unit_index == 0);
            REQUIRE(entry->backend_name == names[i]);
        }
    }

    SECTION("slot access by name") {
        REQUIRE(reg.index_of("lane2") == 2);
        REQUIRE(reg.name_of(3) == "lane3");
        REQUIRE(reg.index_of("nonexistent") == -1);
        REQUIRE(reg.name_of(99) == "");
        REQUIRE(reg.name_of(-1) == "");
    }

    SECTION("find_by_name") {
        const auto* entry = reg.find_by_name("lane1");
        REQUIRE(entry != nullptr);
        REQUIRE(entry->global_index == 1);
        REQUIRE(reg.find_by_name("nope") == nullptr);
    }

    SECTION("unit info") {
        const auto& u = reg.unit(0);
        REQUIRE(u.name == "Turtle_1");
        REQUIRE(u.first_slot == 0);
        REQUIRE(u.slot_count == 4);

        auto [first, end] = reg.unit_slot_range(0);
        REQUIRE(first == 0);
        REQUIRE(end == 4);
    }

    SECTION("unit_for_slot") {
        for (int i = 0; i < 4; ++i) {
            REQUIRE(reg.unit_for_slot(i) == 0);
        }
        REQUIRE(reg.unit_for_slot(-1) == -1);
        REQUIRE(reg.unit_for_slot(4) == -1);
    }

    SECTION("is_valid_index") {
        REQUIRE(reg.is_valid_index(0));
        REQUIRE(reg.is_valid_index(3));
        REQUIRE_FALSE(reg.is_valid_index(-1));
        REQUIRE_FALSE(reg.is_valid_index(4));
    }

    SECTION("default slot info") {
        const auto* entry = reg.get(0);
        REQUIRE(entry->info.global_index == 0);
        REQUIRE(entry->info.slot_index == 0); // unit-local
        REQUIRE(entry->info.mapped_tool == -1);
        REQUIRE(entry->info.status == SlotStatus::UNKNOWN);
    }
}

TEST_CASE("SlotRegistry multi-unit initialization", "[slot_registry][init]") {
    SlotRegistry reg;

    std::vector<std::pair<std::string, std::vector<std::string>>> units = {
        {"Turtle_1", {"lane0", "lane1", "lane2", "lane3"}},
        {"AMS_1", {"lane4", "lane5", "lane6", "lane7"}},
    };
    reg.initialize_units(units);

    REQUIRE(reg.slot_count() == 8);
    REQUIRE(reg.unit_count() == 2);

    SECTION("unit boundaries") {
        auto [f0, e0] = reg.unit_slot_range(0);
        REQUIRE(f0 == 0);
        REQUIRE(e0 == 4);
        auto [f1, e1] = reg.unit_slot_range(1);
        REQUIRE(f1 == 4);
        REQUIRE(e1 == 8);
    }

    SECTION("global index continuity") {
        for (int i = 0; i < 8; ++i) {
            REQUIRE(reg.get(i)->global_index == i);
        }
    }

    SECTION("unit-local indices") {
        REQUIRE(reg.get(0)->info.slot_index == 0);
        REQUIRE(reg.get(3)->info.slot_index == 3);
        REQUIRE(reg.get(4)->info.slot_index == 0); // first slot in unit 1
        REQUIRE(reg.get(7)->info.slot_index == 3);
    }

    SECTION("unit_for_slot across units") {
        REQUIRE(reg.unit_for_slot(0) == 0);
        REQUIRE(reg.unit_for_slot(3) == 0);
        REQUIRE(reg.unit_for_slot(4) == 1);
        REQUIRE(reg.unit_for_slot(7) == 1);
    }

    SECTION("name lookup across units") {
        REQUIRE(reg.index_of("lane4") == 4);
        REQUIRE(reg.name_of(7) == "lane7");
    }
}
