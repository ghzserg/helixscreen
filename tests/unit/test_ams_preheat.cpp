// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_preheat.cpp
 * @brief Unit tests for AMS preheat functionality
 *
 * Tests the auto-preheat feature for AMS filament loading:
 * 1. Temperature source priority logic (get_load_temp_for_slot)
 * 2. Load with preheat branching (handle_load_with_preheat)
 * 3. Pending load temperature monitoring (check_pending_load)
 * 4. Post-load cooling behavior (handle_load_complete)
 *
 * NOTE: These tests are designed to FAIL initially (test-first development).
 * The implementation does not exist yet - this follows TDD methodology.
 */

#include "ams_backend.h"
#include "ams_types.h"
#include "filament_database.h"

#include <optional>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;
// ============================================================================
// Test Constants - Define expected values for the preheat system
// ============================================================================

/// Default load preheat temperature when no other source available (°C)
constexpr int DEFAULT_LOAD_PREHEAT_TEMP = 220;

/// Temperature threshold - consider "hot enough" if within this many degrees
constexpr int TEMP_REACHED_THRESHOLD = 5;

// ============================================================================
// Mock/Test Helper Classes
// ============================================================================

/**
 * @brief Mock temperature provider for testing
 *
 * Simulates the nozzle temperature state without requiring full PrinterState.
 */
class MockTemperatureProvider {
  public:
    void set_nozzle_temp(int temp_c) {
        nozzle_temp_c_ = temp_c;
    }
    void set_nozzle_target(int target_c) {
        nozzle_target_c_ = target_c;
    }

    [[nodiscard]] int get_nozzle_temp() const {
        return nozzle_temp_c_;
    }
    [[nodiscard]] int get_nozzle_target() const {
        return nozzle_target_c_;
    }

    /**
     * @brief Check if nozzle is at or above target temperature
     * @param target Target temperature to check against
     * @param threshold How close is "close enough" (default 5°C)
     */
    [[nodiscard]] bool is_temp_reached(int target, int threshold = TEMP_REACHED_THRESHOLD) const {
        return nozzle_temp_c_ >= (target - threshold);
    }

  private:
    int nozzle_temp_c_ = 25;  // Room temperature default
    int nozzle_target_c_ = 0; // No target set
};

/**
 * @brief Mock AMS backend for preheat testing
 *
 * Extends AmsBackend interface with test helpers for preheat functionality.
 * Captures commands sent to verify correct behavior.
 */
class MockAmsBackendPreheat {
  public:
    explicit MockAmsBackendPreheat(int slot_count = 4) : slot_count_(slot_count) {
        // Initialize slots with default values
        for (int i = 0; i < slot_count; ++i) {
            SlotInfo slot;
            slot.slot_index = i;
            slot.global_index = i;
            slot.status = SlotStatus::AVAILABLE;
            slot.color_rgb = AMS_DEFAULT_SLOT_COLOR;
            slots_.push_back(slot);
        }
    }

    // ========================================================================
    // Slot Configuration (for test setup)
    // ========================================================================

    SlotInfo* get_slot(int index) {
        if (index < 0 || index >= static_cast<int>(slots_.size())) {
            return nullptr;
        }
        return &slots_[index];
    }

    const SlotInfo* get_slot(int index) const {
        if (index < 0 || index >= static_cast<int>(slots_.size())) {
            return nullptr;
        }
        return &slots_[index];
    }

    // ========================================================================
    // Backend Capability Flags
    // ========================================================================

    void set_supports_auto_heat(bool supports) {
        supports_auto_heat_ = supports;
    }

    [[nodiscard]] bool supports_auto_heat() const {
        return supports_auto_heat_;
    }

    // ========================================================================
    // Preheat State (the functionality being tested)
    // ========================================================================

    /// Slot index of pending load (-1 = no pending load)
    int pending_load_slot_ = -1;

    /// Target temperature for pending load
    int pending_load_target_temp_ = 0;

    /// Whether UI initiated the current heating (for post-load cooldown)
    bool ui_initiated_heat_ = false;

    // ========================================================================
    // Command Capture (for verification)
    // ========================================================================

    struct CapturedCommand {
        std::string type;
        int slot_index = -1;
        int temperature = 0;
    };

    std::vector<CapturedCommand> captured_commands;

    void clear_captured_commands() {
        captured_commands.clear();
    }

    void capture_load_filament(int slot_index) {
        captured_commands.push_back({"LOAD_FILAMENT", slot_index, 0});
    }

    void capture_set_heater_temp(int temp_c) {
        captured_commands.push_back({"SET_HEATER_TEMPERATURE", -1, temp_c});
    }

    [[nodiscard]] bool has_command(const std::string& type) const {
        for (const auto& cmd : captured_commands) {
            if (cmd.type == type)
                return true;
        }
        return false;
    }

    [[nodiscard]] bool has_load_command_for_slot(int slot) const {
        for (const auto& cmd : captured_commands) {
            if (cmd.type == "LOAD_FILAMENT" && cmd.slot_index == slot)
                return true;
        }
        return false;
    }

    [[nodiscard]] int get_heater_temp_command_value() const {
        for (const auto& cmd : captured_commands) {
            if (cmd.type == "SET_HEATER_TEMPERATURE")
                return cmd.temperature;
        }
        return -1; // Not found
    }

  private:
    int slot_count_;
    std::vector<SlotInfo> slots_;
    bool supports_auto_heat_ = false;
};

// ============================================================================
// Helper Functions Under Test (stubs - will fail until implemented)
// ============================================================================

/**
 * @brief Get the temperature to use for loading filament from a slot
 *
 * Priority order:
 * 1. SlotInfo::nozzle_temp_min (if > 0)
 * 2. FilamentDatabase lookup by material name
 * 3. DEFAULT_LOAD_PREHEAT_TEMP fallback (220°C)
 *
 * @param slot SlotInfo for the slot being loaded (may be nullptr)
 * @return Temperature in °C to heat to before loading
 */
inline int get_load_temp_for_slot(const SlotInfo* slot) {
    // Priority 1: Null slot returns default
    if (slot == nullptr) {
        return DEFAULT_LOAD_PREHEAT_TEMP;
    }

    // Priority 2: Slot has explicit nozzle_temp_min
    if (slot->nozzle_temp_min > 0) {
        return slot->nozzle_temp_min;
    }

    // Priority 3: Look up material in FilamentDatabase
    if (!slot->material.empty()) {
        auto mat_info = filament::find_material(slot->material);
        if (mat_info.has_value()) {
            // Use nozzle_min for loading - safer than recommended
            return mat_info->nozzle_min;
        }
    }

    // Priority 4: Fall back to default
    return DEFAULT_LOAD_PREHEAT_TEMP;
}

/**
 * @brief Handle load request with automatic preheat if needed
 *
 * Decision tree:
 * - If backend supports auto-heat: call load_filament directly
 * - If nozzle already at temp: call load_filament directly
 * - Otherwise: start heating, set pending_load_slot_, set ui_initiated_heat_
 *
 * @param backend Mock backend to operate on
 * @param temp_provider Temperature state provider
 * @param slot_index Slot to load from
 */
inline void handle_load_with_preheat(MockAmsBackendPreheat& backend,
                                     MockTemperatureProvider& temp_provider, int slot_index) {
    // If backend handles heating automatically, just load directly
    if (backend.supports_auto_heat()) {
        backend.capture_load_filament(slot_index);
        return;
    }

    // Get the target temperature for this slot
    const SlotInfo* slot = backend.get_slot(slot_index);
    int target_temp = get_load_temp_for_slot(slot);

    // If nozzle is already hot enough, load directly
    if (temp_provider.is_temp_reached(target_temp)) {
        backend.capture_load_filament(slot_index);
        return;
    }

    // Need to preheat first - start heating and set pending load
    backend.capture_set_heater_temp(target_temp);
    backend.pending_load_slot_ = slot_index;
    backend.pending_load_target_temp_ = target_temp;
    backend.ui_initiated_heat_ = true;
}

/**
 * @brief Check if pending load can proceed (temperature reached)
 *
 * If pending_load_slot_ >= 0 and temperature is reached:
 * - Call load_filament for the pending slot
 * - Clear pending_load_slot_ to -1
 *
 * @param backend Mock backend to check/update
 * @param temp_provider Temperature state provider
 */
inline void check_pending_load(MockAmsBackendPreheat& backend,
                               MockTemperatureProvider& temp_provider) {
    // No pending load
    if (backend.pending_load_slot_ < 0) {
        return;
    }

    // Check if temperature has been reached
    if (!temp_provider.is_temp_reached(backend.pending_load_target_temp_)) {
        return;
    }

    // Temperature reached - issue load command and clear pending state
    backend.capture_load_filament(backend.pending_load_slot_);
    backend.pending_load_slot_ = -1;
}

/**
 * @brief Handle load completion - turn off heater if UI initiated it
 *
 * If ui_initiated_heat_ is true:
 * - Send SET_HEATER_TEMPERATURE TARGET=0 to turn off
 * - Clear ui_initiated_heat_ flag
 *
 * @param backend Mock backend to operate on
 */
inline void handle_load_complete(MockAmsBackendPreheat& backend) {
    // Only turn off heater if we initiated the heating
    if (!backend.ui_initiated_heat_) {
        return;
    }

    // Turn off heater and clear flag
    backend.capture_set_heater_temp(0);
    backend.ui_initiated_heat_ = false;
}

// ============================================================================
// Test Cases: get_load_temp_for_slot() - Temperature Priority Logic
// ============================================================================

TEST_CASE("get_load_temp_for_slot: slot has nozzle_temp_min set", "[ams][preheat][temp]") {
    SlotInfo slot;
    slot.nozzle_temp_min = 200;
    slot.material = "PLA"; // Has material, but nozzle_temp_min should take priority

    int temp = get_load_temp_for_slot(&slot);

    // Priority 1: nozzle_temp_min from slot should be used
    REQUIRE(temp == 200);
}

TEST_CASE("get_load_temp_for_slot: slot has material but no temp - uses FilamentDatabase",
          "[ams][preheat][temp]") {
    SlotInfo slot;
    slot.nozzle_temp_min = 0; // Not set
    slot.material = "PLA";

    int temp = get_load_temp_for_slot(&slot);

    // Priority 2: Should lookup PLA in FilamentDatabase
    // PLA in database has nozzle_min=190, nozzle_max=220, recommended=(190+220)/2=205
    // Implementation should use nozzle_min (190) or recommended (205)
    auto mat_info = filament::find_material("PLA");
    REQUIRE(mat_info.has_value());

    // Accept either nozzle_min or recommended - implementation choice
    bool valid_temp = (temp == mat_info->nozzle_min) || (temp == mat_info->nozzle_recommended());
    REQUIRE(valid_temp);
}

TEST_CASE("get_load_temp_for_slot: slot has PETG material", "[ams][preheat][temp]") {
    SlotInfo slot;
    slot.nozzle_temp_min = 0;
    slot.material = "PETG";

    int temp = get_load_temp_for_slot(&slot);

    // PETG in database: nozzle_min=230, nozzle_max=260, recommended=245
    auto mat_info = filament::find_material("PETG");
    REQUIRE(mat_info.has_value());

    bool valid_temp = (temp == mat_info->nozzle_min) || (temp == mat_info->nozzle_recommended());
    REQUIRE(valid_temp);
}

TEST_CASE("get_load_temp_for_slot: unknown material falls back to default",
          "[ams][preheat][temp]") {
    SlotInfo slot;
    slot.nozzle_temp_min = 0;
    slot.material = "UnknownMaterial123";

    int temp = get_load_temp_for_slot(&slot);

    // Priority 3: Unknown material should fall back to default
    REQUIRE(temp == DEFAULT_LOAD_PREHEAT_TEMP);
}

TEST_CASE("get_load_temp_for_slot: empty material falls back to default", "[ams][preheat][temp]") {
    SlotInfo slot;
    slot.nozzle_temp_min = 0;
    slot.material = "";

    int temp = get_load_temp_for_slot(&slot);

    // No temp, no material -> default
    REQUIRE(temp == DEFAULT_LOAD_PREHEAT_TEMP);
}

TEST_CASE("get_load_temp_for_slot: null slot returns default", "[ams][preheat][temp]") {
    int temp = get_load_temp_for_slot(nullptr);

    // Null slot should return default safely
    REQUIRE(temp == DEFAULT_LOAD_PREHEAT_TEMP);
}

TEST_CASE("get_load_temp_for_slot: case-insensitive material lookup", "[ams][preheat][temp]") {
    SlotInfo slot;
    slot.nozzle_temp_min = 0;
    slot.material = "pla"; // lowercase

    int temp = get_load_temp_for_slot(&slot);

    // Should find "PLA" despite lowercase input
    auto mat_info = filament::find_material("PLA");
    REQUIRE(mat_info.has_value());

    bool valid_temp = (temp == mat_info->nozzle_min) || (temp == mat_info->nozzle_recommended());
    REQUIRE(valid_temp);
}

// ============================================================================
// Test Cases: handle_load_with_preheat() - Branching Logic
// ============================================================================

TEST_CASE("handle_load_with_preheat: backend supports auto-heat - loads directly",
          "[ams][preheat][load]") {
    MockAmsBackendPreheat backend(4);
    MockTemperatureProvider temp_provider;

    // Backend handles heating automatically (like AFC)
    backend.set_supports_auto_heat(true);
    temp_provider.set_nozzle_temp(25); // Cold nozzle

    handle_load_with_preheat(backend, temp_provider, 0);

    // Should call load_filament directly without UI heating
    REQUIRE(backend.has_load_command_for_slot(0));
    REQUIRE_FALSE(backend.ui_initiated_heat_);
    REQUIRE(backend.pending_load_slot_ == -1);
}

TEST_CASE("handle_load_with_preheat: nozzle already hot - loads directly", "[ams][preheat][load]") {
    MockAmsBackendPreheat backend(4);
    MockTemperatureProvider temp_provider;

    backend.set_supports_auto_heat(false);

    // Set slot with PLA (needs ~200°C)
    auto* slot = backend.get_slot(0);
    REQUIRE(slot != nullptr);
    slot->nozzle_temp_min = 200;

    // Nozzle is already hot enough
    temp_provider.set_nozzle_temp(205);

    handle_load_with_preheat(backend, temp_provider, 0);

    // Should call load_filament directly since already hot
    REQUIRE(backend.has_load_command_for_slot(0));
    REQUIRE_FALSE(backend.ui_initiated_heat_);
    REQUIRE(backend.pending_load_slot_ == -1);
}

TEST_CASE("handle_load_with_preheat: nozzle cold - starts heating and sets pending",
          "[ams][preheat][load]") {
    MockAmsBackendPreheat backend(4);
    MockTemperatureProvider temp_provider;

    backend.set_supports_auto_heat(false);

    // Set slot with PLA (needs ~200°C)
    auto* slot = backend.get_slot(0);
    REQUIRE(slot != nullptr);
    slot->nozzle_temp_min = 200;

    // Nozzle is cold
    temp_provider.set_nozzle_temp(25);

    handle_load_with_preheat(backend, temp_provider, 0);

    // Should NOT call load_filament yet
    REQUIRE_FALSE(backend.has_load_command_for_slot(0));

    // Should send heater command
    REQUIRE(backend.has_command("SET_HEATER_TEMPERATURE"));
    REQUIRE(backend.get_heater_temp_command_value() == 200);

    // Should set pending state
    REQUIRE(backend.pending_load_slot_ == 0);
    REQUIRE(backend.pending_load_target_temp_ == 200);
    REQUIRE(backend.ui_initiated_heat_);
}

TEST_CASE("handle_load_with_preheat: uses FilamentDatabase temp when slot has no temp set",
          "[ams][preheat][load]") {
    MockAmsBackendPreheat backend(4);
    MockTemperatureProvider temp_provider;

    backend.set_supports_auto_heat(false);

    // Set slot with PETG but no explicit temp
    auto* slot = backend.get_slot(1);
    REQUIRE(slot != nullptr);
    slot->nozzle_temp_min = 0; // Not set
    slot->material = "PETG";

    temp_provider.set_nozzle_temp(25); // Cold

    handle_load_with_preheat(backend, temp_provider, 1);

    // Should use PETG temp from database
    auto mat_info = filament::find_material("PETG");
    REQUIRE(mat_info.has_value());

    // Check heater was set to appropriate PETG temp
    int heater_temp = backend.get_heater_temp_command_value();
    bool valid_temp =
        (heater_temp == mat_info->nozzle_min) || (heater_temp == mat_info->nozzle_recommended());
    REQUIRE(valid_temp);
}

TEST_CASE("handle_load_with_preheat: temp within threshold is considered hot enough",
          "[ams][preheat][load]") {
    MockAmsBackendPreheat backend(4);
    MockTemperatureProvider temp_provider;

    backend.set_supports_auto_heat(false);

    auto* slot = backend.get_slot(0);
    REQUIRE(slot != nullptr);
    slot->nozzle_temp_min = 200;

    // Nozzle is 3°C below target (within TEMP_REACHED_THRESHOLD of 5)
    temp_provider.set_nozzle_temp(197);

    handle_load_with_preheat(backend, temp_provider, 0);

    // Should load directly since within threshold
    REQUIRE(backend.has_load_command_for_slot(0));
    REQUIRE_FALSE(backend.ui_initiated_heat_);
}

// ============================================================================
// Test Cases: check_pending_load() - Temperature Monitoring
// ============================================================================

TEST_CASE("check_pending_load: no pending load - does nothing", "[ams][preheat][pending]") {
    MockAmsBackendPreheat backend(4);
    MockTemperatureProvider temp_provider;

    backend.pending_load_slot_ = -1; // No pending load
    temp_provider.set_nozzle_temp(200);

    check_pending_load(backend, temp_provider);

    // Should not issue any commands
    REQUIRE(backend.captured_commands.empty());
}

TEST_CASE("check_pending_load: pending but temp not reached - does nothing",
          "[ams][preheat][pending]") {
    MockAmsBackendPreheat backend(4);
    MockTemperatureProvider temp_provider;

    backend.pending_load_slot_ = 0;
    backend.pending_load_target_temp_ = 200;
    temp_provider.set_nozzle_temp(150); // Way below target

    check_pending_load(backend, temp_provider);

    // Should not issue load command yet
    REQUIRE_FALSE(backend.has_load_command_for_slot(0));
    // Pending state should remain
    REQUIRE(backend.pending_load_slot_ == 0);
}

TEST_CASE("check_pending_load: temp reached - issues load and clears pending",
          "[ams][preheat][pending]") {
    MockAmsBackendPreheat backend(4);
    MockTemperatureProvider temp_provider;

    backend.pending_load_slot_ = 0;
    backend.pending_load_target_temp_ = 200;
    backend.ui_initiated_heat_ = true;
    temp_provider.set_nozzle_temp(200); // At target

    check_pending_load(backend, temp_provider);

    // Should issue load command
    REQUIRE(backend.has_load_command_for_slot(0));
    // Pending state should be cleared
    REQUIRE(backend.pending_load_slot_ == -1);
    // ui_initiated_heat_ should remain (cleared on load COMPLETE, not here)
    REQUIRE(backend.ui_initiated_heat_);
}

TEST_CASE("check_pending_load: temp within threshold triggers load", "[ams][preheat][pending]") {
    MockAmsBackendPreheat backend(4);
    MockTemperatureProvider temp_provider;

    backend.pending_load_slot_ = 2;
    backend.pending_load_target_temp_ = 200;
    temp_provider.set_nozzle_temp(196); // 4°C below, within threshold

    check_pending_load(backend, temp_provider);

    // Should trigger load since within TEMP_REACHED_THRESHOLD
    REQUIRE(backend.has_load_command_for_slot(2));
    REQUIRE(backend.pending_load_slot_ == -1);
}

TEST_CASE("check_pending_load: temp just outside threshold - waits", "[ams][preheat][pending]") {
    MockAmsBackendPreheat backend(4);
    MockTemperatureProvider temp_provider;

    backend.pending_load_slot_ = 1;
    backend.pending_load_target_temp_ = 200;
    temp_provider.set_nozzle_temp(194); // 6°C below, outside threshold

    check_pending_load(backend, temp_provider);

    // Should NOT trigger load
    REQUIRE_FALSE(backend.has_command("LOAD_FILAMENT"));
    REQUIRE(backend.pending_load_slot_ == 1);
}

// ============================================================================
// Test Cases: handle_load_complete() - Post-Load Cooling
// ============================================================================

TEST_CASE("handle_load_complete: ui_initiated_heat true - turns off heater",
          "[ams][preheat][complete]") {
    MockAmsBackendPreheat backend(4);

    backend.ui_initiated_heat_ = true;

    handle_load_complete(backend);

    // Should send heater off command
    REQUIRE(backend.has_command("SET_HEATER_TEMPERATURE"));
    REQUIRE(backend.get_heater_temp_command_value() == 0);

    // Flag should be cleared
    REQUIRE_FALSE(backend.ui_initiated_heat_);
}

TEST_CASE("handle_load_complete: ui_initiated_heat false - does nothing",
          "[ams][preheat][complete]") {
    MockAmsBackendPreheat backend(4);

    backend.ui_initiated_heat_ = false;

    handle_load_complete(backend);

    // Should NOT send any heater command
    REQUIRE_FALSE(backend.has_command("SET_HEATER_TEMPERATURE"));
    // Flag should remain false
    REQUIRE_FALSE(backend.ui_initiated_heat_);
}

TEST_CASE("handle_load_complete: clears ui_initiated_heat after turning off heater",
          "[ams][preheat][complete]") {
    MockAmsBackendPreheat backend(4);

    // Simulate state after UI-initiated load completes
    backend.ui_initiated_heat_ = true;
    backend.pending_load_slot_ = -1; // Already cleared when load started

    handle_load_complete(backend);

    // After completion, flag should be cleared for next operation
    REQUIRE_FALSE(backend.ui_initiated_heat_);

    // Call again - should not send another heater command
    backend.clear_captured_commands();
    handle_load_complete(backend);
    REQUIRE_FALSE(backend.has_command("SET_HEATER_TEMPERATURE"));
}

// ============================================================================
// Integration Test Cases
// ============================================================================

TEST_CASE("Preheat flow: cold start -> heat -> load -> cooldown", "[ams][preheat][integration]") {
    MockAmsBackendPreheat backend(4);
    MockTemperatureProvider temp_provider;

    backend.set_supports_auto_heat(false);

    // Configure slot 0 with PLA
    auto* slot = backend.get_slot(0);
    REQUIRE(slot != nullptr);
    slot->nozzle_temp_min = 200;
    slot->material = "PLA";

    // Step 1: Initial load request with cold nozzle
    temp_provider.set_nozzle_temp(25);
    handle_load_with_preheat(backend, temp_provider, 0);

    REQUIRE_FALSE(backend.has_load_command_for_slot(0));
    REQUIRE(backend.pending_load_slot_ == 0);
    REQUIRE(backend.ui_initiated_heat_);
    REQUIRE(backend.get_heater_temp_command_value() == 200);

    // Step 2: Temperature rising but not there yet
    backend.clear_captured_commands();
    temp_provider.set_nozzle_temp(150);
    check_pending_load(backend, temp_provider);

    REQUIRE_FALSE(backend.has_load_command_for_slot(0));
    REQUIRE(backend.pending_load_slot_ == 0); // Still pending

    // Step 3: Temperature reached
    backend.clear_captured_commands();
    temp_provider.set_nozzle_temp(200);
    check_pending_load(backend, temp_provider);

    REQUIRE(backend.has_load_command_for_slot(0));
    REQUIRE(backend.pending_load_slot_ == -1); // Cleared
    REQUIRE(backend.ui_initiated_heat_);       // Still set until complete

    // Step 4: Load completes - turn off heater
    backend.clear_captured_commands();
    handle_load_complete(backend);

    REQUIRE(backend.has_command("SET_HEATER_TEMPERATURE"));
    REQUIRE(backend.get_heater_temp_command_value() == 0);
    REQUIRE_FALSE(backend.ui_initiated_heat_);
}

TEST_CASE("Preheat flow: already hot skips heating phase", "[ams][preheat][integration]") {
    MockAmsBackendPreheat backend(4);
    MockTemperatureProvider temp_provider;

    backend.set_supports_auto_heat(false);

    auto* slot = backend.get_slot(0);
    REQUIRE(slot != nullptr);
    slot->nozzle_temp_min = 200;

    // Already printing something, nozzle is hot
    temp_provider.set_nozzle_temp(210);

    handle_load_with_preheat(backend, temp_provider, 0);

    // Should load immediately
    REQUIRE(backend.has_load_command_for_slot(0));
    REQUIRE_FALSE(backend.ui_initiated_heat_);
    REQUIRE(backend.pending_load_slot_ == -1);

    // Load completes - should NOT turn off heater (wasn't UI-initiated)
    backend.clear_captured_commands();
    handle_load_complete(backend);

    REQUIRE_FALSE(backend.has_command("SET_HEATER_TEMPERATURE"));
}

TEST_CASE("Preheat flow: auto-heat backend skips all UI heating", "[ams][preheat][integration]") {
    MockAmsBackendPreheat backend(4);
    MockTemperatureProvider temp_provider;

    // Backend like AFC that handles heating internally
    backend.set_supports_auto_heat(true);

    auto* slot = backend.get_slot(0);
    REQUIRE(slot != nullptr);
    slot->nozzle_temp_min = 200;

    temp_provider.set_nozzle_temp(25); // Cold

    handle_load_with_preheat(backend, temp_provider, 0);

    // Should load immediately, backend handles heating
    REQUIRE(backend.has_load_command_for_slot(0));
    REQUIRE_FALSE(backend.ui_initiated_heat_);
    REQUIRE_FALSE(backend.has_command("SET_HEATER_TEMPERATURE"));

    // Load completes - nothing to turn off
    backend.clear_captured_commands();
    handle_load_complete(backend);

    REQUIRE_FALSE(backend.has_command("SET_HEATER_TEMPERATURE"));
}
