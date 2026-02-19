// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_mock.h"
#include "runtime_config.h"

#include <chrono>
#include <thread>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;
/**
 * @file test_ams_backend_mock_realistic.cpp
 * @brief Unit tests for AMS mock backend realistic mode functionality
 *
 * Tests the multi-phase operation mode where load/unload operations
 * progress through realistic phases (HEATING→LOADING→CHECKING etc.)
 * and integrate with the sim_speedup timing system.
 *
 * Note: These tests set RuntimeConfig::sim_speedup to 1000x so operations
 * complete quickly. Base timing constants are:
 * - HEATING: 3000ms -> 3ms at 1000x
 * - CUTTING: 2000ms -> 2ms at 1000x
 * - CHECKING: 1500ms -> 1.5ms at 1000x
 * - SEGMENT_ANIMATION: 5000ms -> 5ms at 1000x
 */

// RAII helper to set up fast timing and restore on exit
class FastTimingScope {
  public:
    FastTimingScope() {
        auto* config = get_runtime_config();
        original_speedup_ = config->sim_speedup;
        config->sim_speedup = 1000.0; // 1000x speedup for fast tests
    }
    ~FastTimingScope() {
        auto* config = get_runtime_config();
        config->sim_speedup = original_speedup_;
    }

  private:
    double original_speedup_ = 1.0;
};

TEST_CASE("AmsBackendMock realistic mode defaults", "[ams][mock][realistic]") {
    AmsBackendMock backend(4);

    SECTION("realistic mode is disabled by default") {
        REQUIRE_FALSE(backend.is_realistic_mode());
    }

    SECTION("can enable realistic mode") {
        backend.set_realistic_mode(true);
        REQUIRE(backend.is_realistic_mode());
    }

    SECTION("can disable realistic mode") {
        backend.set_realistic_mode(true);
        REQUIRE(backend.is_realistic_mode());
        backend.set_realistic_mode(false);
        REQUIRE_FALSE(backend.is_realistic_mode());
    }
}

TEST_CASE("AmsBackendMock realistic mode load operation phases",
          "[ams][mock][realistic][load][slow]") {
    FastTimingScope timing_guard; // RAII: 1000x speedup, auto-restored

    AmsBackendMock backend(4);
    backend.set_operation_delay(10); // Very fast for testing
    backend.set_realistic_mode(true);
    REQUIRE(backend.start());

    // Track action state changes
    std::vector<AmsAction> observed_actions;
    backend.set_event_callback([&](const std::string& event, const std::string&) {
        if (event == AmsBackend::EVENT_STATE_CHANGED) {
            auto action = backend.get_current_action();
            // Only record unique consecutive actions
            if (observed_actions.empty() || observed_actions.back() != action) {
                observed_actions.push_back(action);
            }
        }
    });

    SECTION("load shows HEATING then LOADING then IDLE sequence") {
        // Start with slot 1 (slot 0 is pre-loaded in mock)
        auto result = backend.unload_filament();
        REQUIRE(result);

        // Wait for unload to complete (with 1000x speedup: ~20ms total)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        observed_actions.clear();

        // Now do load
        result = backend.load_filament(1);
        REQUIRE(result);

        // Wait for operation to complete (with 1000x speedup: ~12ms total)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Verify phase sequence: HEATING → LOADING → IDLE
        // (CHECKING is only used in recovery, not normal load)
        REQUIRE(observed_actions.size() >= 2);

        bool found_heating = false;
        bool found_loading_after_heating = false;
        bool found_idle_after_loading = false;

        for (size_t i = 0; i < observed_actions.size(); ++i) {
            if (observed_actions[i] == AmsAction::HEATING) {
                found_heating = true;
            }
            if (found_heating && observed_actions[i] == AmsAction::LOADING) {
                found_loading_after_heating = true;
            }
            if (found_loading_after_heating && observed_actions[i] == AmsAction::IDLE) {
                found_idle_after_loading = true;
            }
        }

        CHECK(found_heating);
        CHECK(found_loading_after_heating);
        CHECK(found_idle_after_loading);
    }

    backend.stop();
}

TEST_CASE("AmsBackendMock realistic mode unload operation phases",
          "[ams][mock][realistic][unload][slow]") {
    FastTimingScope timing_guard; // RAII: 1000x speedup, auto-restored

    AmsBackendMock backend(4);
    backend.set_operation_delay(10); // Very fast for testing
    backend.set_realistic_mode(true);
    REQUIRE(backend.start());

    // Track action state changes
    std::vector<AmsAction> observed_actions;
    backend.set_event_callback([&](const std::string& event, const std::string&) {
        if (event == AmsBackend::EVENT_STATE_CHANGED) {
            auto action = backend.get_current_action();
            if (observed_actions.empty() || observed_actions.back() != action) {
                observed_actions.push_back(action);
            }
        }
    });

    SECTION("unload shows HEATING then CUTTING then UNLOADING sequence") {
        // Slot 0 is pre-loaded, so we can unload directly
        auto result = backend.unload_filament();
        REQUIRE(result);

        // Wait for operation to complete (with 1000x speedup: ~15ms total)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Verify phase sequence
        REQUIRE(observed_actions.size() >= 3);

        // Find the HEATING -> CUTTING -> UNLOADING sequence
        bool found_heating = false;
        bool found_cutting = false;
        bool found_unloading = false;

        for (size_t i = 0; i < observed_actions.size(); ++i) {
            if (observed_actions[i] == AmsAction::HEATING) {
                found_heating = true;
            }
            if (found_heating && observed_actions[i] == AmsAction::CUTTING) {
                found_cutting = true;
            }
            if (found_cutting && observed_actions[i] == AmsAction::UNLOADING) {
                found_unloading = true;
            }
        }

        CHECK(found_heating);
        CHECK(found_cutting);
        CHECK(found_unloading);
    }

    backend.stop();
}

TEST_CASE("AmsBackendMock simple mode skips extra phases", "[ams][mock][realistic][simple]") {
    FastTimingScope timing_guard; // RAII: 1000x speedup, auto-restored

    AmsBackendMock backend(4);
    backend.set_operation_delay(10); // Very fast for testing
    // Realistic mode is OFF by default
    REQUIRE_FALSE(backend.is_realistic_mode());
    REQUIRE(backend.start());

    // Track action state changes
    std::vector<AmsAction> observed_actions;
    backend.set_event_callback([&](const std::string& event, const std::string&) {
        if (event == AmsBackend::EVENT_STATE_CHANGED) {
            auto action = backend.get_current_action();
            if (observed_actions.empty() || observed_actions.back() != action) {
                observed_actions.push_back(action);
            }
        }
    });

    SECTION("unload in simple mode shows only UNLOADING") {
        auto result = backend.unload_filament();
        REQUIRE(result);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Should NOT see HEATING or CUTTING in simple mode
        bool found_heating = false;
        bool found_cutting = false;
        bool found_unloading = false;

        for (const auto& action : observed_actions) {
            if (action == AmsAction::HEATING)
                found_heating = true;
            if (action == AmsAction::CUTTING)
                found_cutting = true;
            if (action == AmsAction::UNLOADING)
                found_unloading = true;
        }

        CHECK_FALSE(found_heating);
        CHECK_FALSE(found_cutting);
        CHECK(found_unloading);
    }

    backend.stop();
}

TEST_CASE("AmsBackendMock realistic mode completes to IDLE",
          "[ams][mock][realistic][completion][slow]") {
    FastTimingScope timing_guard; // RAII: 1000x speedup, auto-restored

    AmsBackendMock backend(4);
    backend.set_operation_delay(10);
    backend.set_realistic_mode(true);
    REQUIRE(backend.start());

    SECTION("load completes to IDLE state") {
        // Unload first
        backend.unload_filament();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Load
        backend.load_filament(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto action = backend.get_current_action();
        REQUIRE(action == AmsAction::IDLE);

        auto info = backend.get_system_info();
        REQUIRE(info.filament_loaded == true);
        REQUIRE(info.current_slot == 1);
    }

    SECTION("unload completes to IDLE state") {
        backend.unload_filament();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto action = backend.get_current_action();
        REQUIRE(action == AmsAction::IDLE);

        auto info = backend.get_system_info();
        REQUIRE(info.filament_loaded == false);
        REQUIRE(info.current_slot == -1);
    }

    backend.stop();
}

TEST_CASE("AmsBackendMock realistic mode can be cancelled",
          "[ams][mock][realistic][cancel][slow]") {
    FastTimingScope timing_guard; // RAII: 1000x speedup, auto-restored

    AmsBackendMock backend(4);
    backend.set_operation_delay(100); // Slower to give time to cancel
    backend.set_realistic_mode(true);
    REQUIRE(backend.start());

    SECTION("cancel during heating phase") {
        backend.unload_filament();

        // Give it a moment to start
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        // Cancel mid-operation
        auto cancel_result = backend.cancel();
        REQUIRE(cancel_result);

        auto action = backend.get_current_action();
        REQUIRE(action == AmsAction::IDLE);
    }

    backend.stop();
}

// ============================================================================
// Phase 5: Mock Loading State Machine - SELECTING, PAUSED, Recovery
// ============================================================================

TEST_CASE("AmsBackendMock tool change shows SELECTING phase",
          "[ams][mock][realistic][selecting][slow]") {
    FastTimingScope timing_guard; // RAII: 1000x speedup, auto-restored

    AmsBackendMock backend(4);
    backend.set_operation_delay(10);
    backend.set_realistic_mode(true);
    REQUIRE(backend.start());

    // Track action state changes
    std::vector<AmsAction> observed_actions;
    backend.set_event_callback([&](const std::string& event, const std::string&) {
        if (event == AmsBackend::EVENT_STATE_CHANGED) {
            auto action = backend.get_current_action();
            if (observed_actions.empty() || observed_actions.back() != action) {
                observed_actions.push_back(action);
            }
        }
    });

    SECTION("tool change includes SELECTING between unload and load") {
        // Perform a tool change from T0 to T1
        auto result = backend.change_tool(1);
        REQUIRE(result);

        // Wait for operation to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Verify SELECTING phase appears between UNLOADING and LOADING phases
        bool found_unloading = false;
        bool found_selecting_after_unload = false;
        bool found_loading_after_selecting = false;

        for (size_t i = 0; i < observed_actions.size(); ++i) {
            if (observed_actions[i] == AmsAction::UNLOADING) {
                found_unloading = true;
            }
            if (found_unloading && observed_actions[i] == AmsAction::SELECTING) {
                found_selecting_after_unload = true;
            }
            if (found_selecting_after_unload && observed_actions[i] == AmsAction::LOADING) {
                found_loading_after_selecting = true;
            }
        }

        CHECK(found_unloading);
        CHECK(found_selecting_after_unload);
        CHECK(found_loading_after_selecting);
    }

    backend.stop();
}

TEST_CASE("AmsBackendMock PAUSED state handling", "[ams][mock][realistic][paused][slow]") {
    FastTimingScope timing_guard; // RAII: 1000x speedup, auto-restored

    AmsBackendMock backend(4);
    backend.set_operation_delay(10);
    backend.set_realistic_mode(true);
    REQUIRE(backend.start());

    SECTION("simulate_pause sets PAUSED state") {
        // Simulate a pause condition (e.g., user intervention required)
        backend.simulate_pause();

        auto action = backend.get_current_action();
        REQUIRE(action == AmsAction::PAUSED);
    }

    SECTION("resume from PAUSED returns to IDLE") {
        backend.simulate_pause();
        REQUIRE(backend.get_current_action() == AmsAction::PAUSED);

        // Resume should return to IDLE
        auto result = backend.resume();
        REQUIRE(result);

        auto action = backend.get_current_action();
        REQUIRE(action == AmsAction::IDLE);
    }

    SECTION("resume when not paused returns success") {
        // Should be a no-op when not paused
        REQUIRE(backend.get_current_action() == AmsAction::IDLE);

        auto result = backend.resume();
        REQUIRE(result);
        REQUIRE(backend.get_current_action() == AmsAction::IDLE);
    }

    backend.stop();
}

TEST_CASE("AmsBackendMock error recovery sequence", "[ams][mock][realistic][recovery][slow]") {
    FastTimingScope timing_guard; // RAII: 1000x speedup, auto-restored

    AmsBackendMock backend(4);
    backend.set_operation_delay(10);
    backend.set_realistic_mode(true);
    REQUIRE(backend.start());

    // Track action state changes
    std::vector<AmsAction> observed_actions;
    backend.set_event_callback([&](const std::string& event, const std::string&) {
        if (event == AmsBackend::EVENT_STATE_CHANGED) {
            auto action = backend.get_current_action();
            if (observed_actions.empty() || observed_actions.back() != action) {
                observed_actions.push_back(action);
            }
        }
    });

    SECTION("recover from ERROR goes through CHECKING to IDLE") {
        // Put system in error state
        backend.simulate_error(AmsResult::FILAMENT_JAM);
        REQUIRE(backend.get_current_action() == AmsAction::ERROR);
        observed_actions.clear();

        // Trigger recovery
        auto result = backend.recover();
        REQUIRE(result);

        // Wait for recovery sequence to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Verify recovery sequence: ERROR → CHECKING → IDLE
        bool found_checking = false;
        bool found_idle_after_checking = false;

        for (size_t i = 0; i < observed_actions.size(); ++i) {
            if (observed_actions[i] == AmsAction::CHECKING) {
                found_checking = true;
            }
            if (found_checking && observed_actions[i] == AmsAction::IDLE) {
                found_idle_after_checking = true;
            }
        }

        CHECK(found_checking);
        CHECK(found_idle_after_checking);

        // Final state should be IDLE
        REQUIRE(backend.get_current_action() == AmsAction::IDLE);
    }

    SECTION("recover clears error segment") {
        backend.simulate_error(AmsResult::FILAMENT_JAM);
        REQUIRE(backend.infer_error_segment() != PathSegment::NONE);

        backend.recover();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        REQUIRE(backend.infer_error_segment() == PathSegment::NONE);
    }

    backend.stop();
}

// ============================================================================
// Mock data consistency tests
// ============================================================================

TEST_CASE("Mock backend: slots have valid Spoolman IDs and filament data",
          "[ams][mock][slot-data]") {
    auto backend = AmsBackend::create_mock(4);
    backend->start();

    for (int i = 0; i < 4; ++i) {
        auto slot = backend->get_slot_info(i);
        CAPTURE(i);
        // Each slot should have a spoolman_id matching its 1-based index
        REQUIRE(slot.spoolman_id == i + 1);
        // Should have non-empty filament data
        REQUIRE_FALSE(slot.material.empty());
        REQUIRE_FALSE(slot.brand.empty());
        REQUIRE_FALSE(slot.color_name.empty());
        REQUIRE(slot.color_rgb != 0);
        REQUIRE(slot.total_weight_g > 0);
        REQUIRE(slot.remaining_weight_g > 0);
    }

    backend->stop();
}

TEST_CASE("Mock backend: slot data matches first N Spoolman mock spools",
          "[ams][mock][slot-data]") {
    // Verifies AMS mock slots and Spoolman mock spools use consistent data.
    // If either mock changes independently, this test catches the drift.
    auto backend = AmsBackend::create_mock(4);
    backend->start();

    struct Expected {
        const char* brand;
        const char* material;
        const char* color_name;
    };
    const Expected expected[] = {
        {"Polymaker", "PLA", "Jet Black"},
        {"eSUN", "Silk PLA", "Silk Blue"},
        {"Elegoo", "ASA", "Pop Blue"},
        {"Flashforge", "ABS", "Fire Engine Red"},
    };

    for (int i = 0; i < 4; ++i) {
        auto slot = backend->get_slot_info(i);
        CAPTURE(i, slot.brand, slot.material, slot.color_name);
        REQUIRE(slot.brand == expected[i].brand);
        REQUIRE(slot.material == expected[i].material);
        REQUIRE(slot.color_name == expected[i].color_name);
    }

    backend->stop();
}
