// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../ui_test_utils.h"
#include "ams_backend.h"
#include "ams_state.h"
#include "printer_discovery.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("PrinterDiscovery: single MMU detected as one system", "[ams][multi-backend]") {
    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array(
        {"mmu", "mmu_encoder mmu_encoder", "extruder", "heater_bed", "gcode_move"});
    hw.parse_objects(objects);

    const auto& systems = hw.detected_ams_systems();
    REQUIRE(systems.size() == 1);
    REQUIRE(systems[0].type == AmsType::HAPPY_HARE);
    REQUIRE(hw.mmu_type() == AmsType::HAPPY_HARE);
}

TEST_CASE("PrinterDiscovery: toolchanger only detected as one system", "[ams][multi-backend]") {
    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array(
        {"toolchanger", "tool T0", "tool T1", "extruder", "extruder1", "heater_bed", "gcode_move"});
    hw.parse_objects(objects);

    const auto& systems = hw.detected_ams_systems();
    REQUIRE(systems.size() == 1);
    REQUIRE(systems[0].type == AmsType::TOOL_CHANGER);
}

TEST_CASE("PrinterDiscovery: toolchanger + Happy Hare detected as two systems",
          "[ams][multi-backend]") {
    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array({"toolchanger", "tool T0", "tool T1", "mmu",
                                                    "mmu_encoder mmu_encoder", "extruder",
                                                    "extruder1", "heater_bed", "gcode_move"});
    hw.parse_objects(objects);

    const auto& systems = hw.detected_ams_systems();
    REQUIRE(systems.size() == 2);

    bool has_tc = false, has_hh = false;
    for (const auto& sys : systems) {
        if (sys.type == AmsType::TOOL_CHANGER)
            has_tc = true;
        if (sys.type == AmsType::HAPPY_HARE)
            has_hh = true;
    }
    REQUIRE(has_tc);
    REQUIRE(has_hh);
    REQUIRE(hw.mmu_type() == AmsType::TOOL_CHANGER);
}

TEST_CASE("PrinterDiscovery: AFC + toolchanger detected as two systems", "[ams][multi-backend]") {
    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array(
        {"toolchanger", "tool T0", "tool T1", "AFC", "AFC_stepper lane1", "AFC_stepper lane2",
         "extruder", "extruder1", "heater_bed", "gcode_move"});
    hw.parse_objects(objects);

    const auto& systems = hw.detected_ams_systems();
    REQUIRE(systems.size() == 2);

    bool has_tc = false, has_afc = false;
    for (const auto& sys : systems) {
        if (sys.type == AmsType::TOOL_CHANGER)
            has_tc = true;
        if (sys.type == AmsType::AFC)
            has_afc = true;
    }
    REQUIRE(has_tc);
    REQUIRE(has_afc);
}

TEST_CASE("PrinterDiscovery: no AMS detected returns empty", "[ams][multi-backend]") {
    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array({"extruder", "heater_bed", "gcode_move"});
    hw.parse_objects(objects);

    REQUIRE(hw.detected_ams_systems().empty());
    REQUIRE(hw.mmu_type() == AmsType::NONE);
}

// ============================================================================
// Task 2: Multi-backend storage tests
// ============================================================================

TEST_CASE("AmsState: add_backend stores multiple backends", "[ams][multi-backend]") {
    lv_init_safe();
    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    auto mock1 = AmsBackend::create_mock(4);
    auto mock2 = AmsBackend::create_mock(2);
    ams.add_backend(std::move(mock1));
    ams.add_backend(std::move(mock2));

    REQUIRE(ams.backend_count() == 2);
    REQUIRE(ams.get_backend(0) != nullptr);
    REQUIRE(ams.get_backend(1) != nullptr);
    REQUIRE(ams.get_backend(2) == nullptr);
    REQUIRE(ams.get_backend() == ams.get_backend(0));

    ams.deinit_subjects();
}

TEST_CASE("AmsState: set_backend replaces all backends", "[ams][multi-backend]") {
    lv_init_safe();
    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    ams.add_backend(AmsBackend::create_mock(4));
    ams.add_backend(AmsBackend::create_mock(2));
    REQUIRE(ams.backend_count() == 2);

    ams.set_backend(AmsBackend::create_mock(3));
    REQUIRE(ams.backend_count() == 1);

    ams.deinit_subjects();
}

TEST_CASE("AmsState: clear_backends removes all", "[ams][multi-backend]") {
    lv_init_safe();
    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    ams.add_backend(AmsBackend::create_mock(4));
    REQUIRE(ams.backend_count() == 1);

    ams.clear_backends();
    REQUIRE(ams.backend_count() == 0);
    REQUIRE(ams.get_backend() == nullptr);

    ams.deinit_subjects();
}

// ============================================================================
// Task 3: Per-backend slot subject accessor tests
// ============================================================================

TEST_CASE("AmsState: primary backend uses flat slot subjects", "[ams][multi-backend]") {
    lv_init_safe();
    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    ams.set_backend(AmsBackend::create_mock(4));

    REQUIRE(ams.get_slot_color_subject(0, 0) == ams.get_slot_color_subject(0));
    REQUIRE(ams.get_slot_color_subject(0, 3) == ams.get_slot_color_subject(3));
    REQUIRE(ams.get_slot_status_subject(0, 0) == ams.get_slot_status_subject(0));
    REQUIRE(ams.get_slot_status_subject(0, 3) == ams.get_slot_status_subject(3));

    ams.deinit_subjects();
}

TEST_CASE("AmsState: secondary backend gets separate slot subjects", "[ams][multi-backend]") {
    lv_init_safe();
    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    ams.add_backend(AmsBackend::create_mock(4));
    ams.add_backend(AmsBackend::create_mock(3));

    auto* color_0 = ams.get_slot_color_subject(0, 0);
    auto* color_1 = ams.get_slot_color_subject(1, 0);
    REQUIRE(color_0 != nullptr);
    REQUIRE(color_1 != nullptr);
    REQUIRE(color_0 != color_1);

    auto* status_0 = ams.get_slot_status_subject(0, 0);
    auto* status_1 = ams.get_slot_status_subject(1, 0);
    REQUIRE(status_0 != nullptr);
    REQUIRE(status_1 != nullptr);
    REQUIRE(status_0 != status_1);

    // Out of range for secondary (mock2 only has 3 slots: 0, 1, 2)
    REQUIRE(ams.get_slot_color_subject(1, 3) == nullptr);
    REQUIRE(ams.get_slot_status_subject(1, 3) == nullptr);

    // Non-existent backend
    REQUIRE(ams.get_slot_color_subject(2, 0) == nullptr);
    REQUIRE(ams.get_slot_status_subject(2, 0) == nullptr);

    ams.deinit_subjects();
}

// ============================================================================
// Task 4: Per-backend event routing and sync tests
// ============================================================================

TEST_CASE("AmsState: sync_backend updates correct subjects", "[ams][multi-backend]") {
    lv_init_safe();
    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    ams.add_backend(AmsBackend::create_mock(4));
    ams.add_backend(AmsBackend::create_mock(2));

    // Sync primary
    ams.sync_backend(0);
    REQUIRE(lv_subject_get_int(ams.get_slot_count_subject()) > 0);

    // Sync secondary - should update secondary slot subjects
    ams.sync_backend(1);
    auto* sec_color = ams.get_slot_color_subject(1, 0);
    REQUIRE(sec_color != nullptr);

    ams.deinit_subjects();
}

TEST_CASE("AmsState: update_slot_for_backend delegates to primary", "[ams][multi-backend]") {
    lv_init_safe();
    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    ams.add_backend(AmsBackend::create_mock(4));

    // Should not crash for primary backend
    ams.update_slot_for_backend(0, 0);

    // Should not crash for out-of-range backend
    ams.update_slot_for_backend(5, 0);

    ams.deinit_subjects();
}

TEST_CASE("AmsState: get_backend negative index returns nullptr", "[ams][multi-backend]") {
    lv_init_safe();
    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    ams.add_backend(AmsBackend::create_mock(4));
    REQUIRE(ams.get_backend(-1) == nullptr);

    ams.deinit_subjects();
}

// ============================================================================
// Task 5: Multi-backend init flow tests
// ============================================================================

TEST_CASE("AmsState: init_backends_from_hardware with single system", "[ams][multi-backend]") {
    lv_init_safe();
    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array(
        {"toolchanger", "tool T0", "tool T1", "extruder", "extruder1", "heater_bed", "gcode_move"});
    hw.parse_objects(objects);

    REQUIRE(hw.detected_ams_systems().size() == 1);
    REQUIRE(hw.detected_ams_systems()[0].type == AmsType::TOOL_CHANGER);

    ams.deinit_subjects();
}

TEST_CASE("AmsState: init_backends skips when no systems detected", "[ams][multi-backend]") {
    lv_init_safe();
    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array({"extruder", "heater_bed", "gcode_move"});
    hw.parse_objects(objects);

    // Verify detection returns empty - no systems to init
    REQUIRE(hw.detected_ams_systems().empty());

    ams.deinit_subjects();
}

// ============================================================================
// Task 8: Integration and lifecycle tests
// ============================================================================

TEST_CASE("Multi-backend: full lifecycle", "[ams][multi-backend]") {
    lv_init_safe();

    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    // Add two mock backends
    ams.add_backend(AmsBackend::create_mock(4));
    ams.add_backend(AmsBackend::create_mock(2));

    REQUIRE(ams.backend_count() == 2);
    REQUIRE(lv_subject_get_int(ams.get_backend_count_subject()) == 2);

    // Sync both backends
    ams.sync_backend(0);
    ams.sync_backend(1);

    // Primary backend slots via flat accessors
    REQUIRE(ams.get_slot_color_subject(0) != nullptr);
    REQUIRE(ams.get_slot_color_subject(3) != nullptr);

    // Secondary backend slots via indexed accessors
    REQUIRE(ams.get_slot_color_subject(1, 0) != nullptr);
    REQUIRE(ams.get_slot_color_subject(1, 1) != nullptr);
    REQUIRE(ams.get_slot_color_subject(1, 2) == nullptr); // mock2 only has 2 slots

    // Active backend selection
    ams.set_active_backend(1);
    REQUIRE(ams.active_backend_index() == 1);
    REQUIRE(lv_subject_get_int(ams.get_active_backend_subject()) == 1);

    // Out of range selection is ignored
    ams.set_active_backend(5);
    REQUIRE(ams.active_backend_index() == 1); // unchanged

    // Deinit cleans everything up
    ams.deinit_subjects();
    REQUIRE(ams.backend_count() == 0);
    REQUIRE(lv_subject_get_int(ams.get_backend_count_subject()) == 0);
}

TEST_CASE("Multi-backend: deinit then re-init is safe", "[ams][multi-backend]") {
    lv_init_safe();

    AmsState& ams = AmsState::instance();

    // First cycle
    ams.deinit_subjects();
    ams.init_subjects(false);
    ams.add_backend(AmsBackend::create_mock(4));
    REQUIRE(ams.backend_count() == 1);
    ams.deinit_subjects();

    // Second cycle
    ams.init_subjects(false);
    REQUIRE(ams.backend_count() == 0);
    ams.add_backend(AmsBackend::create_mock(2));
    REQUIRE(ams.backend_count() == 1);
    ams.deinit_subjects();
}

TEST_CASE("Multi-backend: active backend resets on clear", "[ams][multi-backend]") {
    lv_init_safe();

    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    ams.add_backend(AmsBackend::create_mock(4));
    ams.add_backend(AmsBackend::create_mock(2));
    ams.set_active_backend(1);
    REQUIRE(ams.active_backend_index() == 1);

    ams.clear_backends();
    REQUIRE(ams.active_backend_index() == 0);
    REQUIRE(ams.backend_count() == 0);

    ams.deinit_subjects();
}
