// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_mock.h"

#include "afc_defaults.h"
#include "filament_database.h"
#include "hh_defaults.h"
#include "runtime_config.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <thread>

// Sample filament colors for mock slots
namespace {
struct MockFilament {
    uint32_t color;
    const char* color_name;
    const char* material;
    const char* brand;
};

// Predefined sample filaments for visual testing
// Covers common material types: PLA, PETG, ABS, ASA, PA, TPU, and CF/GF variants
constexpr MockFilament SAMPLE_FILAMENTS[] = {
    {0xE53935, "Red", "PLA", "Polymaker"},      // Slot 0: Red PLA
    {0x1E88E5, "Blue", "PETG", "eSUN"},         // Slot 1: Blue PETG
    {0x43A047, "Green", "ABS", "Bambu"},        // Slot 2: Green ABS
    {0xFDD835, "Yellow", "ASA", "Polymaker"},   // Slot 3: Yellow ASA
    {0x424242, "Carbon", "PLA-CF", "Overture"}, // Slot 4: Carbon PLA-CF
    {0x8E24AA, "Purple", "PA-CF", "Bambu"},     // Slot 5: Purple PA-CF (Nylon)
    {0xFF6F00, "Orange", "TPU", "eSUN"},        // Slot 6: Orange TPU (Flexible)
    {0x90CAF9, "Sky Blue", "PETG-GF", "Prusa"}, // Slot 7: PETG-GF (Glass Filled)
};
constexpr int NUM_SAMPLE_FILAMENTS = sizeof(SAMPLE_FILAMENTS) / sizeof(SAMPLE_FILAMENTS[0]);

// Timing constants for realistic mode (milliseconds at 1x speed)
// These values simulate real AMS/MMU timing behavior
constexpr int HEATING_BASE_MS = 3000;            // 3 seconds to heat nozzle
constexpr int CUTTING_BASE_MS = 2000;            // 2 seconds for filament cut
constexpr int PURGING_BASE_MS = 3000;            // 3 seconds for purge after load
constexpr int CHECKING_BASE_MS = 1500;           // 1.5 seconds for recovery check
constexpr int SELECTING_BASE_MS = 1000;          // 1 second for slot/tool selection
constexpr int SEGMENT_ANIMATION_BASE_MS = 10000; // 10 seconds for full segment animation

// Variance factors (±percentage) for natural timing variation
constexpr float HEATING_VARIANCE = 0.3f;    // ±30%
constexpr float TIP_VARIANCE = 0.2f;        // ±20%
constexpr float LOADING_VARIANCE = 0.2f;    // ±20%
constexpr float PURGING_VARIANCE = 0.2f;    // ±20%
constexpr float CHECKING_VARIANCE = 0.2f;   // ±20% (for recovery)
constexpr float SELECTING_VARIANCE = 0.15f; // ±15%
} // namespace

AmsBackendMock::AmsBackendMock(int slot_count) {
    // Clamp slot count to reasonable range
    slot_count = std::clamp(slot_count, 1, 16);

    // Initialize system info
    system_info_.type = AmsType::HAPPY_HARE; // Mock as Happy Hare
    system_info_.type_name = "Happy Hare (Mock)";
    system_info_.version = "2.7.0-mock";
    system_info_.current_tool = -1;
    system_info_.current_slot = -1;
    system_info_.filament_loaded = false;
    system_info_.action = AmsAction::IDLE;
    system_info_.total_slots = slot_count;
    // Use shared AFC defaults for capabilities
    auto caps = helix::printer::afc_default_capabilities();
    system_info_.supports_endless_spool = caps.supports_endless_spool;
    system_info_.supports_tool_mapping = caps.supports_tool_mapping;
    system_info_.supports_bypass = caps.supports_bypass;
    system_info_.supports_purge = caps.supports_purge;
    system_info_.tip_method = caps.tip_method;
    system_info_.has_hardware_bypass_sensor = false; // Mock default: virtual toggle

    // Create single unit with all slots
    AmsUnit unit;
    unit.unit_index = 0;
    unit.name = "Mock MMU";
    unit.slot_count = slot_count;
    unit.first_slot_global_index = 0;
    unit.connected = true;
    unit.firmware_version = "mock-1.0";
    unit.has_encoder = true;
    unit.has_toolhead_sensor = true;
    unit.has_slot_sensors = true;

    // Initialize slots with sample filament data
    for (int i = 0; i < slot_count; ++i) {
        SlotInfo slot;
        slot.slot_index = i;
        slot.global_index = i;
        slot.status = SlotStatus::AVAILABLE;
        slot.mapped_tool = i; // Direct 1:1 mapping

        // Assign sample filament data (cycle through samples)
        const auto& sample = SAMPLE_FILAMENTS[i % NUM_SAMPLE_FILAMENTS];
        slot.color_rgb = sample.color;
        slot.color_name = sample.color_name;
        slot.material = sample.material;
        slot.brand = sample.brand;

        // Mock Spoolman data with dramatic fill level differences for demo
        // Use IDs 1-N to match mock Spoolman spools (1-18 in moonraker_api_mock.cpp)
        slot.spoolman_id = i + 1;
        slot.spool_name = std::string(sample.color_name) + " " + sample.material;
        slot.total_weight_g = 1000.0f;
        // Vary fill levels dramatically: 100%, 75%, 40%, 10% for clear visual difference
        static const float fill_levels[] = {1.0f, 0.75f, 0.40f, 0.10f, 0.90f, 0.50f, 0.25f, 0.05f};
        slot.remaining_weight_g = slot.total_weight_g * fill_levels[i % 8];

        // Temperature recommendations from filament database
        auto mat_info = filament::find_material(sample.material);
        if (mat_info) {
            slot.nozzle_temp_min = mat_info->nozzle_min;
            slot.nozzle_temp_max = mat_info->nozzle_max;
            slot.bed_temp = mat_info->bed_temp;
        } else {
            // Fallback to PLA defaults for unknown materials
            auto pla_info = filament::find_material("PLA");
            if (pla_info) {
                slot.nozzle_temp_min = pla_info->nozzle_min;
                slot.nozzle_temp_max = pla_info->nozzle_max;
                slot.bed_temp = pla_info->bed_temp;
            } else {
                // Ultimate fallback (should never happen - PLA is in database)
                slot.nozzle_temp_min = 190;
                slot.nozzle_temp_max = 220;
                slot.bed_temp = 60;
            }
        }

        unit.slots.push_back(slot);
    }

    system_info_.units.push_back(unit);

    // Initialize tool-to-slot mapping (1:1)
    system_info_.tool_to_slot_map.resize(slot_count);
    for (int i = 0; i < slot_count; ++i) {
        system_info_.tool_to_slot_map[i] = i;
    }

    // Start with slot 0 loaded for realistic demo appearance
    if (slot_count > 0) {
        auto* slot = system_info_.get_slot_global(0);
        if (slot) {
            slot->status = SlotStatus::LOADED;
        }
        system_info_.current_slot = 0;
        system_info_.current_tool = 0;
        system_info_.filament_loaded = true;
        filament_segment_ = PathSegment::NOZZLE; // Filament is fully loaded to nozzle
    }

    // Initialize endless spool configs for all slots (no backup by default)
    endless_spool_configs_.reserve(slot_count);
    for (int i = 0; i < slot_count; ++i) {
        helix::printer::EndlessSpoolConfig config;
        config.slot_index = i;
        config.backup_slot = -1; // No backup
        endless_spool_configs_.push_back(config);
    }

    // Make slot index 3 (4th slot) empty for realistic demo
    if (slot_count > 3) {
        auto* slot = system_info_.get_slot_global(3);
        if (slot) {
            slot->status = SlotStatus::EMPTY;
        }
    }

    // Initialize default device sections/actions for Happy Hare mode (constructor default)
    mock_device_sections_ = helix::printer::hh_default_sections();
    mock_device_actions_ = helix::printer::hh_default_actions();

    spdlog::debug("[AmsBackendMock] Created with {} slots", slot_count);
}

AmsBackendMock::~AmsBackendMock() {
    // Signal shutdown and wait for any running operation thread to finish
    // Using atomic flag - safe without mutex
    shutdown_requested_ = true;
    dryer_stop_requested_ = true;
    shutdown_cv_.notify_all();
    wait_for_operation_thread();

    // Stop dryer thread if running - use atomic exchange to prevent double-join
    if (dryer_thread_running_.exchange(false)) {
        if (dryer_thread_.joinable()) {
            dryer_thread_.join();
        }
    }

    // Don't call stop() - it would try to lock the mutex which may be invalid
    // during static destruction. The running_ flag doesn't matter at this point.
}

void AmsBackendMock::wait_for_operation_thread() {
    // Use atomic exchange to prevent double-join race condition
    // Only one caller can "win" the exchange and actually join
    if (operation_thread_running_.exchange(false)) {
        if (operation_thread_.joinable()) {
            operation_thread_.join();
        }
    }
}

AmsError AmsBackendMock::start() {
    bool should_emit = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (running_) {
            return AmsErrorHelper::success();
        }

        running_ = true;
        should_emit = true;
        spdlog::debug("[AmsBackendMock] Started");
    }

    // Emit initial state event OUTSIDE the lock to avoid deadlock
    // (emit_event also acquires mutex_ to safely copy the callback)
    if (should_emit) {
        emit_event(EVENT_STATE_CHANGED);
    }

    return AmsErrorHelper::success();
}

void AmsBackendMock::stop() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!running_) {
        return;
    }

    running_ = false;
    // Note: Don't log here - this may be called during static destruction
    // when spdlog's logger has already been destroyed (causes SIGSEGV)
}

bool AmsBackendMock::is_running() const {
    return running_;
}

void AmsBackendMock::set_event_callback(EventCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_callback_ = std::move(callback);
}

AmsSystemInfo AmsBackendMock::get_system_info() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_;
}

AmsType AmsBackendMock::get_type() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.type; // Returns TOOL_CHANGER or HAPPY_HARE based on mode
}

SlotInfo AmsBackendMock::get_slot_info(int slot_index) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto* slot = system_info_.get_slot_global(slot_index);
    if (slot) {
        return *slot;
    }

    // Return empty slot info for invalid index
    SlotInfo empty;
    empty.slot_index = -1;
    empty.global_index = -1;
    return empty;
}

AmsAction AmsBackendMock::get_current_action() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.action;
}

int AmsBackendMock::get_current_tool() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.current_tool;
}

int AmsBackendMock::get_current_slot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.current_slot;
}

bool AmsBackendMock::is_filament_loaded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.filament_loaded;
}

PathTopology AmsBackendMock::get_topology() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return topology_;
}

PathSegment AmsBackendMock::get_filament_segment() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return filament_segment_;
}

PathSegment AmsBackendMock::get_slot_filament_segment(int slot_index) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if this is the active slot - return the current filament segment
    if (slot_index == system_info_.current_slot && system_info_.filament_loaded) {
        return filament_segment_;
    }

    // For non-active slots, check if filament is installed at the slot
    // and return PREP segment (filament sitting at prep sensor)
    const SlotInfo* slot = system_info_.get_slot_global(slot_index);
    if (!slot) {
        return PathSegment::NONE;
    }

    // Slots with available filament show filament up to prep sensor
    if (slot->status == SlotStatus::AVAILABLE || slot->status == SlotStatus::FROM_BUFFER) {
        return PathSegment::PREP;
    }

    return PathSegment::NONE;
}

PathSegment AmsBackendMock::infer_error_segment() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_segment_;
}

AmsError AmsBackendMock::load_filament(int slot_index) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("Mock backend not started");
        }

        if (system_info_.action != AmsAction::IDLE) {
            return AmsErrorHelper::busy(ams_action_to_string(system_info_.action));
        }

        if (slot_index < 0 || slot_index >= system_info_.total_slots) {
            return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
        }

        auto* slot = system_info_.get_slot_global(slot_index);
        if (!slot || slot->status == SlotStatus::EMPTY) {
            return AmsErrorHelper::slot_not_available(slot_index);
        }

        // Start loading
        system_info_.action = AmsAction::LOADING;
        system_info_.operation_detail = "Loading from slot " + std::to_string(slot_index);
        filament_segment_ = PathSegment::SPOOL; // Start at spool
        spdlog::info("[AmsBackendMock] Loading from slot {}", slot_index);
    }

    emit_event(EVENT_STATE_CHANGED);
    schedule_completion(AmsAction::LOADING, EVENT_LOAD_COMPLETE, slot_index);

    return AmsErrorHelper::success();
}

AmsError AmsBackendMock::unload_filament() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("Mock backend not started");
        }

        if (system_info_.action != AmsAction::IDLE) {
            return AmsErrorHelper::busy(ams_action_to_string(system_info_.action));
        }

        if (!system_info_.filament_loaded) {
            return AmsError(AmsResult::WRONG_STATE, "No filament loaded", "No filament to unload",
                            "Load filament first");
        }

        // Start unloading
        system_info_.action = AmsAction::UNLOADING;
        system_info_.operation_detail = "Unloading filament";
        filament_segment_ = PathSegment::NOZZLE; // Start at nozzle (working backwards)
        spdlog::info("[AmsBackendMock] Unloading filament");
    }

    emit_event(EVENT_STATE_CHANGED);
    schedule_completion(AmsAction::UNLOADING, EVENT_UNLOAD_COMPLETE);

    return AmsErrorHelper::success();
}

AmsError AmsBackendMock::select_slot(int slot_index) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("Mock backend not started");
        }

        if (system_info_.action != AmsAction::IDLE) {
            return AmsErrorHelper::busy(ams_action_to_string(system_info_.action));
        }

        if (slot_index < 0 || slot_index >= system_info_.total_slots) {
            return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
        }

        // Immediate selection (no filament movement)
        system_info_.current_slot = slot_index;
        spdlog::info("[AmsBackendMock] Selected slot {}", slot_index);
    }

    emit_event(EVENT_STATE_CHANGED);
    return AmsErrorHelper::success();
}

AmsError AmsBackendMock::change_tool(int tool_number) {
    int target_slot = 0; // Captured inside lock, used after
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("Mock backend not started");
        }

        if (system_info_.action != AmsAction::IDLE) {
            return AmsErrorHelper::busy(ams_action_to_string(system_info_.action));
        }

        if (tool_number < 0 ||
            tool_number >= static_cast<int>(system_info_.tool_to_slot_map.size())) {
            return AmsError(AmsResult::INVALID_TOOL,
                            "Tool " + std::to_string(tool_number) + " out of range",
                            "Invalid tool number", "Select a valid tool");
        }

        // Start tool change (unload + load sequence)
        system_info_.action = AmsAction::UNLOADING; // Start with unload
        system_info_.operation_detail = "Tool change to T" + std::to_string(tool_number);
        target_slot = system_info_.tool_to_slot_map[tool_number]; // Capture while locked
        spdlog::info("[AmsBackendMock] Tool change to T{}", tool_number);
    }

    emit_event(EVENT_STATE_CHANGED);
    schedule_completion(AmsAction::LOADING, EVENT_TOOL_CHANGED, target_slot);

    return AmsErrorHelper::success();
}

AmsError AmsBackendMock::recover() {
    bool use_realistic;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("Mock backend not started");
        }

        use_realistic = realistic_mode_;

        if (!use_realistic) {
            // Simple mode: immediate recovery to IDLE
            system_info_.action = AmsAction::IDLE;
            system_info_.operation_detail.clear();
            error_segment_ = PathSegment::NONE;
            spdlog::info("[AmsBackendMock] Recovery complete (simple mode)");
        } else {
            // Realistic mode: schedule recovery sequence (ERROR → CHECKING → IDLE)
            spdlog::info("[AmsBackendMock] Starting recovery sequence (realistic mode)");
        }
    }

    if (!use_realistic) {
        emit_event(EVENT_STATE_CHANGED);
        return AmsErrorHelper::success();
    }

    // Realistic mode: run recovery sequence in background
    emit_event(EVENT_STATE_CHANGED);
    schedule_recovery_sequence();

    return AmsErrorHelper::success();
}

AmsError AmsBackendMock::reset() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("Mock backend not started");
        }

        if (system_info_.action != AmsAction::IDLE) {
            return AmsErrorHelper::busy(ams_action_to_string(system_info_.action));
        }

        system_info_.action = AmsAction::RESETTING;
        system_info_.operation_detail = "Resetting system";
        spdlog::info("[AmsBackendMock] Resetting");
    }

    emit_event(EVENT_STATE_CHANGED);

    // Use schedule_completion for thread-safe operation
    // RESETTING action will be handled by the "else" branch which just waits and completes
    schedule_completion(AmsAction::RESETTING, EVENT_STATE_CHANGED);

    return AmsErrorHelper::success();
}

AmsError AmsBackendMock::cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (system_info_.action == AmsAction::IDLE) {
            return AmsErrorHelper::success(); // Nothing to cancel
        }

        system_info_.action = AmsAction::IDLE;
        system_info_.operation_detail.clear();
        spdlog::info("[AmsBackendMock] Operation cancelled");
    }

    // Signal the operation thread to stop
    cancel_requested_ = true;
    shutdown_cv_.notify_all();

    emit_event(EVENT_STATE_CHANGED);
    return AmsErrorHelper::success();
}

AmsError AmsBackendMock::set_slot_info(int slot_index, const SlotInfo& info) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (slot_index < 0 || slot_index >= system_info_.total_slots) {
            return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
        }

        auto* slot = system_info_.get_slot_global(slot_index);
        if (!slot) {
            return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
        }

        // Update filament info
        slot->color_name = info.color_name;
        slot->color_rgb = info.color_rgb;
        slot->material = info.material;
        slot->brand = info.brand;
        slot->spoolman_id = info.spoolman_id;
        slot->spool_name = info.spool_name;
        slot->remaining_weight_g = info.remaining_weight_g;
        slot->total_weight_g = info.total_weight_g;
        slot->nozzle_temp_min = info.nozzle_temp_min;
        slot->nozzle_temp_max = info.nozzle_temp_max;
        slot->bed_temp = info.bed_temp;

        spdlog::trace("[AmsBackendMock] Updated slot {} info", slot_index);
    }

    // Emit event OUTSIDE the lock to avoid deadlock
    emit_event(EVENT_SLOT_CHANGED, std::to_string(slot_index));
    return AmsErrorHelper::success();
}

AmsError AmsBackendMock::set_tool_mapping(int tool_number, int slot_index) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (tool_number < 0 || tool_number >= static_cast<int>(system_info_.tool_to_slot_map.size())) {
        return AmsError(AmsResult::INVALID_TOOL,
                        "Tool " + std::to_string(tool_number) + " out of range",
                        "Invalid tool number", "");
    }

    if (slot_index < 0 || slot_index >= system_info_.total_slots) {
        return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
    }

    system_info_.tool_to_slot_map[tool_number] = slot_index;

    // Update slot's mapped_tool
    for (auto& unit : system_info_.units) {
        for (auto& slot : unit.slots) {
            if (slot.global_index == slot_index) {
                slot.mapped_tool = tool_number;
            }
        }
    }

    spdlog::info("[AmsBackendMock] Mapped T{} to slot {}", tool_number, slot_index);
    return AmsErrorHelper::success();
}

AmsError AmsBackendMock::enable_bypass() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("Mock backend not started");
        }

        if (!system_info_.supports_bypass) {
            return AmsError(AmsResult::WRONG_STATE, "Bypass not supported",
                            "This system does not support bypass mode", "");
        }

        if (system_info_.action != AmsAction::IDLE) {
            return AmsErrorHelper::busy(ams_action_to_string(system_info_.action));
        }

        // Enable bypass mode: current_slot = -2 indicates bypass
        system_info_.current_slot = -2;
        system_info_.filament_loaded = true;
        filament_segment_ = PathSegment::NOZZLE;
        spdlog::info("[AmsBackendMock] Bypass mode enabled");
    }

    emit_event(EVENT_STATE_CHANGED);
    return AmsErrorHelper::success();
}

AmsError AmsBackendMock::disable_bypass() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("Mock backend not started");
        }

        if (system_info_.current_slot != -2) {
            return AmsError(AmsResult::WRONG_STATE, "Bypass not active",
                            "Bypass mode is not currently active", "");
        }

        // Disable bypass mode
        system_info_.current_slot = -1;
        system_info_.filament_loaded = false;
        filament_segment_ = PathSegment::NONE;
        spdlog::info("[AmsBackendMock] Bypass mode disabled");
    }

    emit_event(EVENT_STATE_CHANGED);
    return AmsErrorHelper::success();
}

bool AmsBackendMock::is_bypass_active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.current_slot == -2;
}

void AmsBackendMock::simulate_error(AmsResult error) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        system_info_.action = AmsAction::ERROR;
        system_info_.operation_detail = ams_result_to_string(error);

        // Infer error segment based on error type
        if (error == AmsResult::FILAMENT_JAM || error == AmsResult::ENCODER_ERROR) {
            error_segment_ = PathSegment::HUB; // Jam typically in selector/hub
        } else if (error == AmsResult::SENSOR_ERROR || error == AmsResult::LOAD_FAILED) {
            error_segment_ = PathSegment::TOOLHEAD; // Detection issues at toolhead
        } else if (error == AmsResult::SLOT_BLOCKED || error == AmsResult::SLOT_NOT_AVAILABLE) {
            error_segment_ = PathSegment::PREP; // Slot issues at prep/entry
        } else {
            error_segment_ = filament_segment_; // Error at current position
        }
    }

    emit_event(EVENT_ERROR, ams_result_to_string(error));
    emit_event(EVENT_STATE_CHANGED);
}

void AmsBackendMock::simulate_pause() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        system_info_.action = AmsAction::PAUSED;
        system_info_.operation_detail = "User intervention required";
        spdlog::info("[AmsBackendMock] Simulated pause state");
    }

    emit_event(EVENT_STATE_CHANGED);
}

AmsError AmsBackendMock::resume() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("Mock backend not started");
        }

        // Already idle - no-op success
        if (system_info_.action == AmsAction::IDLE) {
            return AmsErrorHelper::success();
        }

        // Can only resume from PAUSED state
        if (system_info_.action != AmsAction::PAUSED) {
            return AmsError(AmsResult::WRONG_STATE, "Cannot resume - not in PAUSED state",
                            "System is " + std::string(ams_action_to_string(system_info_.action)),
                            "Wait for current operation to complete or use cancel");
        }

        // Resume to IDLE
        system_info_.action = AmsAction::IDLE;
        system_info_.operation_detail.clear();
        spdlog::info("[AmsBackendMock] Resumed from pause");
    }

    emit_event(EVENT_STATE_CHANGED);
    return AmsErrorHelper::success();
}

void AmsBackendMock::set_operation_delay(int delay_ms) {
    operation_delay_ms_ = std::max(0, delay_ms);
}

void AmsBackendMock::force_slot_status(int slot_index, SlotStatus status) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto* slot = system_info_.get_slot_global(slot_index);
    if (slot) {
        slot->status = status;
        spdlog::debug("[AmsBackendMock] Forced slot {} status to {}", slot_index,
                      slot_status_to_string(status));
    }
}

void AmsBackendMock::set_slot_error(int slot_index, std::optional<SlotError> error) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto* slot = system_info_.get_slot_global(slot_index);
    if (slot) {
        slot->error = std::move(error);
        spdlog::debug("[AmsBackendMock] Slot {} error {}", slot_index,
                      slot->error.has_value() ? slot->error->message : "cleared");
    }
}

void AmsBackendMock::set_slot_buffer_health(int slot_index, std::optional<BufferHealth> health) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto* slot = system_info_.get_slot_global(slot_index);
    if (slot) {
        slot->buffer_health = std::move(health);
        spdlog::debug("[AmsBackendMock] Slot {} buffer health {}", slot_index,
                      slot->buffer_health.has_value() ? "set" : "cleared");
    }
}

void AmsBackendMock::set_has_hardware_bypass_sensor(bool has_sensor) {
    std::lock_guard<std::mutex> lock(mutex_);
    system_info_.has_hardware_bypass_sensor = has_sensor;
    spdlog::debug("[AmsBackendMock] Hardware bypass sensor set to {}", has_sensor);
}

void AmsBackendMock::set_dryer_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    dryer_enabled_ = enabled;

    // Initialize dryer state
    dryer_state_.supported = enabled;
    dryer_state_.active = false;
    dryer_state_.allows_during_print = true;
    dryer_state_.current_temp_c = 25.0f; // Room temperature
    dryer_state_.target_temp_c = 0.0f;
    dryer_state_.duration_min = 0;
    dryer_state_.remaining_min = 0;
    dryer_state_.fan_pct = 0;
    dryer_state_.min_temp_c = 35.0f;
    dryer_state_.max_temp_c = 70.0f;
    dryer_state_.max_duration_min = 720;
    dryer_state_.supports_fan_control = true;

    // Check for environment variable override of speed
    const char* speed_env = std::getenv("HELIX_MOCK_DRYER_SPEED");
    if (speed_env) {
        dryer_speed_x_ = std::max(1, std::atoi(speed_env));
        spdlog::info("[AmsBackendMock] Dryer speed override: {}x", dryer_speed_x_);
    }

    spdlog::info("[AmsBackendMock] Dryer simulation {}", enabled ? "enabled" : "disabled");
}

void AmsBackendMock::set_dryer_speed(int speed_x) {
    std::lock_guard<std::mutex> lock(mutex_);
    dryer_speed_x_ = std::max(1, speed_x);
    spdlog::info("[AmsBackendMock] Dryer speed set to {}x", dryer_speed_x_);
}

DryerInfo AmsBackendMock::get_dryer_info() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dryer_state_;
}

AmsError AmsBackendMock::start_drying(float temp_c, int duration_min, int fan_pct) {
    spdlog::info("[AmsBackendMock] start_drying: {}°C for {}min, fan {}%", temp_c, duration_min,
                 fan_pct);

    int speed_x;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!dryer_enabled_) {
            return AmsError{AmsResult::NOT_SUPPORTED, "Dryer not available"};
        }

        // Stop any existing dryer thread
        dryer_stop_requested_ = true;
        speed_x = dryer_speed_x_;
    }

    // Wait for previous thread to finish using atomic exchange
    if (dryer_thread_running_.exchange(false)) {
        if (dryer_thread_.joinable()) {
            dryer_thread_.join();
        }
    }

    float start_temp;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dryer_stop_requested_ = false;

        // Set initial dryer state
        dryer_state_.active = true;
        dryer_state_.target_temp_c = temp_c;
        dryer_state_.duration_min = duration_min;
        dryer_state_.remaining_min = duration_min;
        dryer_state_.fan_pct = (fan_pct >= 0) ? fan_pct : 50;
        start_temp = dryer_state_.current_temp_c; // Use current temp as starting point
    }

    // Mark dryer thread as running BEFORE creating it
    dryer_thread_running_ = true;

    // Start simulation thread
    // speed_x: how many simulated seconds pass per real second
    // At default 60x: 1 real second = 1 simulated minute, so 4h completes in 4min
    dryer_thread_ = std::thread([this, temp_c, duration_min, speed_x, start_temp]() {
        float current_temp = start_temp;
        int total_sec = duration_min * 60; // Total simulated seconds
        int elapsed_sim_sec = 0;

        // Update interval: 100ms real time, but simulate speed_x/10 seconds
        // With speed_x=60: each 100ms tick = 6 simulated seconds
        const int tick_ms = 100;
        const int sim_sec_per_tick = std::max(1, speed_x / 10);

        // Temperature ramping: reach 95% of target in first 5 minutes (300 sim sec)
        // Then hold at target. Typical heater behavior.
        const int ramp_time_sec = 300; // 5 minutes to reach target

        spdlog::debug("[AmsBackendMock] Dryer starting: target={}°C, duration={}min, speed={}x",
                      temp_c, duration_min, speed_x);

        while (!dryer_stop_requested_ && elapsed_sim_sec < total_sec) {
            std::this_thread::sleep_for(std::chrono::milliseconds(tick_ms));
            elapsed_sim_sec += sim_sec_per_tick;

            // Temperature simulation: exponential approach to target
            // During ramp phase: aggressive heating
            // After ramp: maintain at target with small fluctuations
            float temp_diff = temp_c - current_temp;
            if (elapsed_sim_sec < ramp_time_sec) {
                // Ramp phase: approach target quickly (time constant ~60 sec)
                current_temp += temp_diff * 0.05f * sim_sec_per_tick;
            } else {
                // Holding phase: maintain with minor fluctuation
                current_temp = temp_c + (static_cast<float>(std::rand() % 100) / 100.0f - 0.5f);
            }

            // Clamp to valid range
            current_temp = std::max(25.0f, std::min(current_temp, temp_c + 1.0f));

            int remaining_min = std::max(0, (total_sec - elapsed_sim_sec) / 60);

            {
                std::lock_guard<std::mutex> lock(mutex_);
                dryer_state_.current_temp_c = current_temp;
                dryer_state_.remaining_min = remaining_min;
            }

            // Emit state change every tick for smooth UI updates
            emit_event(EVENT_STATE_CHANGED);
        }

        // Drying complete or stopped - simulate cool-down
        {
            std::lock_guard<std::mutex> lock(mutex_);
            dryer_state_.active = false;
            dryer_state_.target_temp_c = 0.0f;
            dryer_state_.remaining_min = 0;
            dryer_state_.fan_pct = 0;
            // Start cooling from current temp (not instant)
        }
        emit_event(EVENT_STATE_CHANGED);

        // Quick cool-down simulation (10 ticks)
        for (int i = 0; i < 10 && !dryer_stop_requested_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(tick_ms));
            float room_temp = 25.0f;
            current_temp = current_temp * 0.8f + room_temp * 0.2f; // Cool towards room temp
            {
                std::lock_guard<std::mutex> lock(mutex_);
                dryer_state_.current_temp_c = current_temp;
            }
            emit_event(EVENT_STATE_CHANGED);
        }

        // Final room temp (skip if shutting down)
        if (!dryer_stop_requested_) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                dryer_state_.current_temp_c = 25.0f;
            }
            emit_event(EVENT_STATE_CHANGED);
            spdlog::info("[AmsBackendMock] Drying complete/stopped, cooled to room temp");
        }
    });

    emit_event(EVENT_STATE_CHANGED);
    return AmsError{AmsResult::SUCCESS};
}

AmsError AmsBackendMock::stop_drying() {
    spdlog::info("[AmsBackendMock] stop_drying");

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!dryer_enabled_) {
            return AmsError{AmsResult::NOT_SUPPORTED, "Dryer not available"};
        }

        if (!dryer_state_.active) {
            return AmsError{AmsResult::SUCCESS}; // Already stopped
        }

        dryer_stop_requested_ = true;
    }

    // Wait for thread to finish using atomic exchange
    if (dryer_thread_running_.exchange(false)) {
        if (dryer_thread_.joinable()) {
            dryer_thread_.join();
        }
    }

    return AmsError{AmsResult::SUCCESS};
}

// ============================================================================
// Realistic mode implementation
// ============================================================================

void AmsBackendMock::set_realistic_mode(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    realistic_mode_ = enabled;
    spdlog::info("[AmsBackendMock] Realistic mode {}", enabled ? "enabled" : "disabled");
}

bool AmsBackendMock::is_realistic_mode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return realistic_mode_;
}

void AmsBackendMock::set_tool_changer_mode(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    tool_changer_mode_ = enabled;

    if (enabled) {
        // Configure mock as a tool changer
        system_info_.type = AmsType::TOOL_CHANGER;
        system_info_.type_name = "Tool Changer (Mock)";
        system_info_.supports_bypass = false; // Not applicable for tool changers

        // Use parallel topology (each slot → own toolhead)
        topology_ = PathTopology::PARALLEL;

        // Rename unit to reflect tool changer nature
        if (!system_info_.units.empty()) {
            system_info_.units[0].name = "Mock Tool Changer";
            // Slots retain their filament data - represents what's loaded in each toolhead
        }

        // Tool changers have no AMS-style device settings
        mock_device_sections_.clear();
        mock_device_actions_.clear();

        spdlog::info("[AmsBackendMock] Tool changer mode enabled ({} tools)",
                     system_info_.total_slots);
    } else {
        // Revert to filament system (Happy Hare)
        system_info_.type = AmsType::HAPPY_HARE;
        system_info_.type_name = "Happy Hare (Mock)";
        system_info_.supports_bypass = true;
        topology_ = PathTopology::HUB;

        if (!system_info_.units.empty()) {
            system_info_.units[0].name = "Mock MMU";
        }

        // Restore Happy Hare device sections and actions
        mock_device_sections_ = helix::printer::hh_default_sections();
        mock_device_actions_ = helix::printer::hh_default_actions();

        spdlog::info("[AmsBackendMock] Tool changer mode disabled, reverting to filament system");
    }
}

bool AmsBackendMock::is_tool_changer_mode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tool_changer_mode_;
}

void AmsBackendMock::set_afc_mode(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    afc_mode_ = enabled;

    if (enabled) {
        // Disable conflicting mode
        tool_changer_mode_ = false;

        // Configure system info for AFC Box Turtle
        system_info_.type = AmsType::AFC;
        system_info_.type_name = "AFC (Mock)";
        system_info_.version = "1.0.32-mock";
        system_info_.total_slots = 4;

        // Use shared AFC defaults for capabilities
        auto afc_caps = helix::printer::afc_default_capabilities();
        system_info_.supports_endless_spool = afc_caps.supports_endless_spool;
        system_info_.supports_tool_mapping = afc_caps.supports_tool_mapping;
        system_info_.supports_bypass = afc_caps.supports_bypass;
        system_info_.supports_purge = afc_caps.supports_purge;
        system_info_.tip_method = afc_caps.tip_method;
        system_info_.has_hardware_bypass_sensor = false;

        // HUB topology, single unit
        topology_ = PathTopology::HUB;

        // Build single Box Turtle unit with 4 lanes
        system_info_.units.clear();
        AmsUnit unit;
        unit.unit_index = 0;
        unit.name = "Box Turtle (Mock)";
        unit.slot_count = 4;
        unit.first_slot_global_index = 0;
        unit.connected = true;
        unit.firmware_version = "1.0.32-mock";
        unit.has_encoder = false;
        unit.has_toolhead_sensor = true;
        unit.has_slot_sensors = true;

        // Lane 1: ASA, loaded
        {
            SlotInfo slot;
            slot.slot_index = 0;
            slot.global_index = 0;
            slot.material = "ASA";
            slot.color_rgb = 0x000000;
            slot.color_name = "Black";
            slot.status = SlotStatus::LOADED;
            slot.mapped_tool = 0;
            slot.total_weight_g = 1000.0f;
            slot.remaining_weight_g = 750.0f;
            auto mat_info = filament::find_material("ASA");
            if (mat_info) {
                slot.nozzle_temp_min = mat_info->nozzle_min;
                slot.nozzle_temp_max = mat_info->nozzle_max;
                slot.bed_temp = mat_info->bed_temp;
            }
            unit.slots.push_back(slot);
        }

        // Lane 2: PLA, loaded
        {
            SlotInfo slot;
            slot.slot_index = 1;
            slot.global_index = 1;
            slot.material = "PLA";
            slot.color_rgb = 0xFF0000;
            slot.color_name = "Red";
            slot.status = SlotStatus::AVAILABLE;
            slot.mapped_tool = 1;
            slot.total_weight_g = 1000.0f;
            slot.remaining_weight_g = 900.0f;
            auto mat_info = filament::find_material("PLA");
            if (mat_info) {
                slot.nozzle_temp_min = mat_info->nozzle_min;
                slot.nozzle_temp_max = mat_info->nozzle_max;
                slot.bed_temp = mat_info->bed_temp;
            }
            unit.slots.push_back(slot);
        }

        // Lane 3: PETG, loaded
        {
            SlotInfo slot;
            slot.slot_index = 2;
            slot.global_index = 2;
            slot.material = "PETG";
            slot.color_rgb = 0x00FF00;
            slot.color_name = "Green";
            slot.status = SlotStatus::AVAILABLE;
            slot.mapped_tool = 2;
            slot.total_weight_g = 1000.0f;
            slot.remaining_weight_g = 500.0f;
            auto mat_info = filament::find_material("PETG");
            if (mat_info) {
                slot.nozzle_temp_min = mat_info->nozzle_min;
                slot.nozzle_temp_max = mat_info->nozzle_max;
                slot.bed_temp = mat_info->bed_temp;
            }
            unit.slots.push_back(slot);
        }

        // Lane 4: error state (for testing error visualization)
        {
            SlotInfo slot;
            slot.slot_index = 3;
            slot.global_index = 3;
            slot.material = "TPU";
            slot.color_rgb = 0xFF6600;
            slot.color_name = "Orange";
            slot.status = SlotStatus::AVAILABLE;
            slot.mapped_tool = 3;
            slot.total_weight_g = 1000.0f;
            slot.remaining_weight_g = 200.0f;
            SlotError err;
            err.message = "Lane 4 load failed";
            err.severity = SlotError::ERROR;
            slot.error = err;
            unit.slots.push_back(slot);
        }

        // Pre-populate buffer health for all lanes (AFC has TurtleNeck buffers)
        for (auto& slot : unit.slots) {
            BufferHealth health;
            health.fault_detection_enabled = true;
            health.state = "Advancing";
            // Lane 3 (index 2): approaching fault (warning visualization)
            if (slot.slot_index == 2) {
                health.distance_to_fault = 12.5f;
                // Also add a WARNING-level error for the approaching fault
                SlotError warn;
                warn.message = "Buffer fault approaching (12.5mm)";
                warn.severity = SlotError::WARNING;
                slot.error = warn;
            }
            slot.buffer_health = health;
        }

        system_info_.units.push_back(unit);

        // Tool-to-slot mapping: T0->lane1, T1->lane2, T2->lane3, T3->lane4
        system_info_.tool_to_slot_map = {0, 1, 2, 3};

        // Start with lane 0 (ASA) loaded
        system_info_.current_slot = 0;
        system_info_.current_tool = 0;
        system_info_.filament_loaded = true;
        filament_segment_ = PathSegment::NOZZLE;

        // Reinitialize endless spool configs for 4 slots
        endless_spool_configs_.clear();
        endless_spool_configs_.reserve(4);
        for (int i = 0; i < 4; ++i) {
            helix::printer::EndlessSpoolConfig config;
            config.slot_index = i;
            config.backup_slot = -1;
            endless_spool_configs_.push_back(config);
        }

        // AFC device sections and actions — use all defaults
        mock_device_sections_ = helix::printer::afc_default_sections();
        mock_device_actions_ = helix::printer::afc_default_actions();

        // Disable save_restart in mock mode (no real Klipper to restart)
        for (auto& action : mock_device_actions_) {
            if (action.id == "save_restart") {
                action.enabled = false;
                action.disable_reason = "Not available in mock mode";
            }
        }

        spdlog::info("[AmsBackendMock] AFC mode enabled (4-lane Box Turtle)");
    } else {
        // Revert to Happy Hare defaults
        afc_mode_ = false;
        system_info_.type = AmsType::HAPPY_HARE;
        system_info_.type_name = "Happy Hare (Mock)";
        system_info_.version = "2.7.0-mock";
        system_info_.supports_bypass = true;
        topology_ = PathTopology::HUB;

        if (!system_info_.units.empty()) {
            system_info_.units[0].name = "Mock MMU";
        }

        // Restore Happy Hare device sections and actions
        mock_device_sections_ = helix::printer::hh_default_sections();
        mock_device_actions_ = helix::printer::hh_default_actions();

        spdlog::info("[AmsBackendMock] AFC mode disabled, reverting to Happy Hare");
    }
}

bool AmsBackendMock::is_afc_mode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return afc_mode_;
}

void AmsBackendMock::set_multi_unit_mode(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    multi_unit_mode_ = enabled;

    if (enabled) {
        // Disable conflicting modes
        tool_changer_mode_ = false;

        // Configure as AFC with 2 Box Turtle units
        system_info_.type = AmsType::AFC;
        system_info_.type_name = "AFC (Mock Multi-Unit)";
        system_info_.version = "1.0.32-mock";
        system_info_.supports_bypass = true;
        system_info_.supports_endless_spool = true;
        system_info_.supports_tool_mapping = true;
        system_info_.has_hardware_bypass_sensor = false;
        system_info_.tip_method = TipMethod::CUT;
        system_info_.supports_purge = true;
        topology_ = PathTopology::HUB;

        system_info_.units.clear();
        system_info_.total_slots = 6; // 4 (Box Turtle) + 2 (Night Owl)

        // Unit 0: "Box Turtle 1" - 4 slots
        {
            AmsUnit unit;
            unit.unit_index = 0;
            unit.name = "Box Turtle 1";
            unit.slot_count = 4;
            unit.first_slot_global_index = 0;
            unit.connected = true;
            unit.firmware_version = "1.0.32-mock";
            unit.has_encoder = false;
            unit.has_toolhead_sensor = true;
            unit.has_slot_sensors = true;
            unit.has_hub_sensor = true;
            unit.hub_sensor_triggered = true; // Active unit has filament at hub

            const struct {
                uint32_t color;
                const char* name;
                const char* material;
                SlotStatus status;
            } slots[] = {
                {0x000000, "Black", "ASA", SlotStatus::LOADED},
                {0xFF0000, "Red", "PLA", SlotStatus::AVAILABLE},
                {0x00FF00, "Green", "PETG", SlotStatus::AVAILABLE},
                {0xFFFFFF, "White", "PLA", SlotStatus::EMPTY},
            };

            for (int i = 0; i < 4; i++) {
                SlotInfo slot;
                slot.slot_index = i;
                slot.global_index = i;
                slot.material = slots[i].material;
                slot.color_rgb = slots[i].color;
                slot.color_name = slots[i].name;
                slot.status = slots[i].status;
                slot.mapped_tool = i;
                slot.spoolman_id = 100 + i;
                slot.total_weight_g = 1000.0f;
                slot.remaining_weight_g = (i == 3) ? 0.0f : (1000.0f - i * 250.0f);
                auto mat_info = filament::find_material(slots[i].material);
                if (mat_info) {
                    slot.nozzle_temp_min = mat_info->nozzle_min;
                    slot.nozzle_temp_max = mat_info->nozzle_max;
                    slot.bed_temp = mat_info->bed_temp;
                }
                unit.slots.push_back(slot);
            }
            system_info_.units.push_back(unit);
        }

        // Unit 1: "Night Owl" - 2 slots (Night Owls are 2-lane units)
        {
            AmsUnit unit;
            unit.unit_index = 1;
            unit.name = "Night Owl";
            unit.slot_count = 2;
            unit.first_slot_global_index = 4;
            unit.connected = true;
            unit.firmware_version = "2.1.0-mock";
            unit.has_encoder = true;
            unit.has_toolhead_sensor = true;
            unit.has_slot_sensors = true;
            unit.has_hub_sensor = true;
            unit.hub_sensor_triggered = false; // Inactive unit, no filament at hub

            const struct {
                uint32_t color;
                const char* name;
                const char* material;
                SlotStatus status;
            } slots[] = {
                {0x1E88E5, "Blue", "PETG", SlotStatus::AVAILABLE},
                {0xFDD835, "Yellow", "ABS", SlotStatus::AVAILABLE},
            };

            for (int i = 0; i < 2; i++) {
                SlotInfo slot;
                slot.slot_index = i;
                slot.global_index = 4 + i;
                slot.material = slots[i].material;
                slot.color_rgb = slots[i].color;
                slot.color_name = slots[i].name;
                slot.status = slots[i].status;
                slot.mapped_tool = 4 + i;
                slot.spoolman_id = 200 + i;
                slot.total_weight_g = 1000.0f;
                slot.remaining_weight_g = 1000.0f - i * 200.0f;
                auto mat_info = filament::find_material(slots[i].material);
                if (mat_info) {
                    slot.nozzle_temp_min = mat_info->nozzle_min;
                    slot.nozzle_temp_max = mat_info->nozzle_max;
                    slot.bed_temp = mat_info->bed_temp;
                }
                unit.slots.push_back(slot);
            }
            system_info_.units.push_back(unit);
        }

        // Tool-to-slot mapping: T0-T5 (6 tools for 6 slots)
        system_info_.tool_to_slot_map = {0, 1, 2, 3, 4, 5};

        // Start with slot 0 loaded
        system_info_.current_slot = 0;
        system_info_.current_tool = 0;
        system_info_.filament_loaded = true;
        filament_segment_ = PathSegment::NOZZLE;

        // Reinitialize endless spool configs for 6 slots
        endless_spool_configs_.clear();
        endless_spool_configs_.reserve(6);
        for (int i = 0; i < 6; ++i) {
            helix::printer::EndlessSpoolConfig config;
            config.slot_index = i;
            config.backup_slot = -1;
            endless_spool_configs_.push_back(config);
        }

        spdlog::info("[AmsBackendMock] Multi-unit mode: Box Turtle (4) + Night Owl (2) = 6 slots");
    } else {
        multi_unit_mode_ = false;
        // Revert to Happy Hare defaults
        system_info_.type = AmsType::HAPPY_HARE;
        system_info_.type_name = "Happy Hare (Mock)";
        system_info_.version = "2.7.0-mock";
        system_info_.supports_bypass = true;
        topology_ = PathTopology::HUB;

        if (!system_info_.units.empty()) {
            system_info_.units[0].name = "Mock MMU";
        }

        spdlog::info("[AmsBackendMock] Multi-unit mode disabled");
    }
}

bool AmsBackendMock::is_multi_unit_mode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return multi_unit_mode_;
}

int AmsBackendMock::get_effective_delay_ms(int base_ms, float variance) const {
    double speedup = get_runtime_config()->sim_speedup;
    if (speedup <= 0)
        speedup = 1.0;

    int effective = static_cast<int>(base_ms / speedup);

    // Apply variance if non-zero
    if (variance > 0.0f && effective > 0) {
        // Random factor between (1-variance) and (1+variance)
        float random_factor = static_cast<float>(std::rand() % 1000) / 1000.0f; // 0.0-0.999
        float factor = 1.0f + (random_factor - 0.5f) * 2.0f * variance;
        effective = static_cast<int>(effective * factor);
    }

    return std::max(1, effective); // At least 1ms
}

void AmsBackendMock::set_action(AmsAction action, const std::string& detail) {
    std::lock_guard<std::mutex> lock(mutex_);
    system_info_.action = action;
    system_info_.operation_detail = detail;
}

void AmsBackendMock::run_load_segment_animation(int slot_index,
                                                InterruptibleSleep interruptible_sleep) {
    // Calculate per-segment delay
    int total_animation_ms =
        realistic_mode_ ? get_effective_delay_ms(SEGMENT_ANIMATION_BASE_MS, LOADING_VARIANCE)
                        : get_effective_delay_ms(operation_delay_ms_);
    int segment_delay = total_animation_ms / 6;

    const PathSegment load_sequence[] = {
        PathSegment::SPOOL,  PathSegment::PREP,     PathSegment::LANE,  PathSegment::HUB,
        PathSegment::OUTPUT, PathSegment::TOOLHEAD, PathSegment::NOZZLE};

    for (auto seg : load_sequence) {
        if (shutdown_requested_ || cancel_requested_)
            return;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            filament_segment_ = seg;
            system_info_.current_slot = slot_index; // Set active slot early for visualization
        }
        emit_event(EVENT_STATE_CHANGED);
        if (!interruptible_sleep(segment_delay))
            return;
    }
}

void AmsBackendMock::run_unload_segment_animation(InterruptibleSleep interruptible_sleep) {
    int total_animation_ms =
        realistic_mode_ ? get_effective_delay_ms(SEGMENT_ANIMATION_BASE_MS, LOADING_VARIANCE)
                        : get_effective_delay_ms(operation_delay_ms_);
    int segment_delay = total_animation_ms / 6;

    const PathSegment unload_sequence[] = {
        PathSegment::NOZZLE, PathSegment::TOOLHEAD, PathSegment::OUTPUT, PathSegment::HUB,
        PathSegment::LANE,   PathSegment::PREP,     PathSegment::SPOOL,  PathSegment::NONE};

    for (auto seg : unload_sequence) {
        if (shutdown_requested_ || cancel_requested_)
            return;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            filament_segment_ = seg;
        }
        emit_event(EVENT_STATE_CHANGED);
        if (!interruptible_sleep(segment_delay))
            return;
    }
}

void AmsBackendMock::finalize_load_state(int slot_index) {
    std::lock_guard<std::mutex> lock(mutex_);
    system_info_.filament_loaded = true;
    filament_segment_ = PathSegment::NOZZLE;
    if (slot_index >= 0) {
        system_info_.current_slot = slot_index;
        system_info_.current_tool = slot_index;
        auto* slot = system_info_.get_slot_global(slot_index);
        if (slot) {
            slot->status = SlotStatus::LOADED;
        }
    }
    system_info_.action = AmsAction::IDLE;
    system_info_.operation_detail.clear();
}

void AmsBackendMock::finalize_unload_state() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (system_info_.current_slot >= 0) {
        auto* slot = system_info_.get_slot_global(system_info_.current_slot);
        if (slot) {
            slot->status = SlotStatus::AVAILABLE;
        }
    }
    system_info_.filament_loaded = false;
    system_info_.current_slot = -1;
    filament_segment_ = PathSegment::NONE;
    system_info_.action = AmsAction::IDLE;
    system_info_.operation_detail.clear();
}

void AmsBackendMock::execute_load_operation(int slot_index,
                                            InterruptibleSleep interruptible_sleep) {
    if (realistic_mode_) {
        // Phase 1: HEATING
        spdlog::debug("[AmsBackendMock] Load phase: HEATING");
        set_action(AmsAction::HEATING, "Heating nozzle for load");
        emit_event(EVENT_STATE_CHANGED);
        if (!interruptible_sleep(get_effective_delay_ms(HEATING_BASE_MS, HEATING_VARIANCE)))
            return;
        if (shutdown_requested_ || cancel_requested_)
            return;

        // Phase 2: LOADING with segment animation
        spdlog::debug("[AmsBackendMock] Load phase: LOADING (segment animation)");
        set_action(AmsAction::LOADING, "Loading from slot " + std::to_string(slot_index));
        emit_event(EVENT_STATE_CHANGED);
    }

    // Segment animation (same for both modes)
    run_load_segment_animation(slot_index, interruptible_sleep);
    if (shutdown_requested_ || cancel_requested_)
        return;

    if (realistic_mode_ && system_info_.supports_purge) {
        // Phase 3: PURGING (only if AMS supports it)
        spdlog::debug("[AmsBackendMock] Load phase: PURGING");
        set_action(AmsAction::PURGING, "Purging filament");
        emit_event(EVENT_STATE_CHANGED);
        if (!interruptible_sleep(get_effective_delay_ms(PURGING_BASE_MS, PURGING_VARIANCE)))
            return;
        if (shutdown_requested_ || cancel_requested_)
            return;
    }

    // Finalize
    finalize_load_state(slot_index);
}

void AmsBackendMock::execute_unload_operation(InterruptibleSleep interruptible_sleep) {
    if (realistic_mode_) {
        // Phase 1: HEATING (shorter - just for clean cut)
        spdlog::debug("[AmsBackendMock] Unload phase: HEATING");
        set_action(AmsAction::HEATING, "Heating for cut");
        emit_event(EVENT_STATE_CHANGED);
        if (!interruptible_sleep(get_effective_delay_ms(HEATING_BASE_MS / 2, HEATING_VARIANCE)))
            return;
        if (shutdown_requested_ || cancel_requested_)
            return;

        // Phase 2: CUTTING
        spdlog::debug("[AmsBackendMock] Unload phase: CUTTING");
        set_action(AmsAction::CUTTING, "Cutting filament");
        emit_event(EVENT_STATE_CHANGED);
        if (!interruptible_sleep(get_effective_delay_ms(CUTTING_BASE_MS, TIP_VARIANCE)))
            return;
        if (shutdown_requested_ || cancel_requested_)
            return;

        // Phase 3: UNLOADING with segment animation
        spdlog::debug("[AmsBackendMock] Unload phase: UNLOADING (segment animation)");
        set_action(AmsAction::UNLOADING, "Retracting filament");
        emit_event(EVENT_STATE_CHANGED);
    }

    // Reverse segment animation
    run_unload_segment_animation(interruptible_sleep);
    if (shutdown_requested_ || cancel_requested_)
        return;

    // Finalize
    finalize_unload_state();
}

void AmsBackendMock::execute_tool_change_operation(int target_slot,
                                                   InterruptibleSleep interruptible_sleep) {
    // Phase 1: Unload current filament
    execute_unload_operation(interruptible_sleep);
    if (shutdown_requested_ || cancel_requested_)
        return;

    // Phase 2: SELECTING (only in realistic mode)
    if (realistic_mode_) {
        spdlog::debug("[AmsBackendMock] Tool change phase: SELECTING slot {}", target_slot);
        set_action(AmsAction::SELECTING, "Selecting slot " + std::to_string(target_slot));
        emit_event(EVENT_STATE_CHANGED);
        if (!interruptible_sleep(get_effective_delay_ms(SELECTING_BASE_MS, SELECTING_VARIANCE)))
            return;
        if (shutdown_requested_ || cancel_requested_)
            return;
    }

    // Phase 3: Load new filament
    execute_load_operation(target_slot, interruptible_sleep);
}

void AmsBackendMock::schedule_recovery_sequence() {
    // Wait for any previous operation to complete first
    wait_for_operation_thread();

    // Reset flags for new operation
    shutdown_requested_ = false;
    cancel_requested_ = false;

    // Mark thread as running BEFORE creating it (for safe shutdown)
    operation_thread_running_ = true;

    // Run recovery sequence in background thread
    operation_thread_ = std::thread([this]() {
        // Helper lambda for interruptible sleep
        InterruptibleSleep interruptible_sleep = [this](int ms) -> bool {
            std::unique_lock<std::mutex> lock(shutdown_mutex_);
            return !shutdown_cv_.wait_for(lock, std::chrono::milliseconds(ms), [this] {
                return shutdown_requested_.load() || cancel_requested_.load();
            });
        };

        // Phase 1: CHECKING (verify system state after error)
        spdlog::debug("[AmsBackendMock] Recovery phase: CHECKING");
        set_action(AmsAction::CHECKING, "Checking system state");
        emit_event(EVENT_STATE_CHANGED);
        if (!interruptible_sleep(get_effective_delay_ms(CHECKING_BASE_MS, CHECKING_VARIANCE)))
            return;
        if (shutdown_requested_ || cancel_requested_)
            return;

        // Phase 2: Return to IDLE
        {
            std::lock_guard<std::mutex> lock(mutex_);
            system_info_.action = AmsAction::IDLE;
            system_info_.operation_detail.clear();
            error_segment_ = PathSegment::NONE; // Clear error location
        }
        emit_event(EVENT_STATE_CHANGED);
        spdlog::info("[AmsBackendMock] Recovery complete (realistic mode)");
    });
}

void AmsBackendMock::emit_event(const std::string& event, const std::string& data) {
    EventCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = event_callback_;
    }

    if (cb) {
        cb(event, data);
    }
}

void AmsBackendMock::schedule_completion(AmsAction action, const std::string& complete_event,
                                         int slot_index) {
    // Wait for any previous operation to complete first
    wait_for_operation_thread();

    // Reset flags for new operation
    shutdown_requested_ = false;
    cancel_requested_ = false;

    // Mark thread as running BEFORE creating it (for safe shutdown)
    operation_thread_running_ = true;

    // Check if this is a tool change (complete_event signals intent)
    bool is_tool_change = (complete_event == EVENT_TOOL_CHANGED);

    // Simulate operation delay in background thread with path segment progression
    operation_thread_ = std::thread([this, action, complete_event, slot_index, is_tool_change]() {
        // Helper lambda for interruptible sleep (returns false if cancelled/shutdown)
        InterruptibleSleep interruptible_sleep = [this](int ms) -> bool {
            std::unique_lock<std::mutex> lock(shutdown_mutex_);
            return !shutdown_cv_.wait_for(lock, std::chrono::milliseconds(ms), [this] {
                return shutdown_requested_.load() || cancel_requested_.load();
            });
        };

        if (is_tool_change) {
            // Tool change uses special operation with SELECTING phase
            execute_tool_change_operation(slot_index, interruptible_sleep);
        } else if (action == AmsAction::LOADING) {
            // Use phase executor (handles both realistic and simple modes)
            execute_load_operation(slot_index, interruptible_sleep);
        } else if (action == AmsAction::UNLOADING) {
            // Use phase executor (handles both realistic and simple modes)
            execute_unload_operation(interruptible_sleep);
        } else {
            // For other actions, just wait and complete
            if (!interruptible_sleep(get_effective_delay_ms(operation_delay_ms_)))
                return;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                system_info_.action = AmsAction::IDLE;
                system_info_.operation_detail.clear();
            }
        }

        if (shutdown_requested_ || cancel_requested_)
            return; // Final check before emitting

        emit_event(complete_event, slot_index >= 0 ? std::to_string(slot_index) : "");
        emit_event(EVENT_STATE_CHANGED);
    });
}

// ============================================================================
// Endless spool implementation
// ============================================================================

void AmsBackendMock::set_endless_spool_supported(bool supported) {
    std::lock_guard<std::mutex> lock(mutex_);
    endless_spool_supported_ = supported;
    // Update system_info to reflect the new capability
    system_info_.supports_endless_spool = supported;
    spdlog::debug("[AmsBackendMock] Endless spool supported set to {}", supported);
}

void AmsBackendMock::set_endless_spool_editable(bool editable) {
    std::lock_guard<std::mutex> lock(mutex_);
    endless_spool_editable_ = editable;
    spdlog::debug("[AmsBackendMock] Endless spool editable set to {}", editable);
}

helix::printer::EndlessSpoolCapabilities AmsBackendMock::get_endless_spool_capabilities() const {
    std::lock_guard<std::mutex> lock(mutex_);

    helix::printer::EndlessSpoolCapabilities caps;
    caps.supported = endless_spool_supported_;
    caps.editable = endless_spool_supported_ && endless_spool_editable_;
    if (caps.supported) {
        caps.description =
            caps.editable ? "Per-slot backup (AFC-style)" : "Group-based (Happy Hare-style)";
    }
    return caps;
}

std::vector<helix::printer::EndlessSpoolConfig> AmsBackendMock::get_endless_spool_config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return endless_spool_configs_;
}

AmsError AmsBackendMock::set_endless_spool_backup(int slot_index, int backup_slot) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if endless spool is supported
    if (!endless_spool_supported_) {
        return AmsErrorHelper::not_supported("Endless spool");
    }

    // Check if editable
    if (!endless_spool_editable_) {
        return AmsErrorHelper::not_supported("Endless spool configuration");
    }

    // Validate slot_index
    if (slot_index < 0 || slot_index >= system_info_.total_slots) {
        return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
    }

    // Cannot set slot as its own backup
    if (backup_slot == slot_index) {
        return AmsError(AmsResult::INVALID_SLOT,
                        "Cannot set slot " + std::to_string(slot_index) + " as its own backup",
                        "Invalid backup configuration", "Select a different slot as backup");
    }

    // Validate backup_slot (must be -1 or valid slot index)
    if (backup_slot != -1 && (backup_slot < 0 || backup_slot >= system_info_.total_slots)) {
        return AmsErrorHelper::invalid_slot(backup_slot, system_info_.total_slots - 1);
    }

    // Update the config
    if (slot_index < static_cast<int>(endless_spool_configs_.size())) {
        endless_spool_configs_[slot_index].backup_slot = backup_slot;
        spdlog::info("[AmsBackendMock] Set slot {} backup to {}", slot_index, backup_slot);
    }

    return AmsErrorHelper::success();
}

// ============================================================================
// Tool mapping implementation
// ============================================================================

helix::printer::ToolMappingCapabilities AmsBackendMock::get_tool_mapping_capabilities() const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Tool changers don't support tool mapping (tools ARE slots)
    if (tool_changer_mode_) {
        return {false, false, ""};
    }

    // Filament systems support editable tool mapping
    return {true, true, "Mock tool-to-slot mapping"};
}

std::vector<int> AmsBackendMock::get_tool_mapping() const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Tool changers don't support tool mapping (tools ARE slots)
    if (tool_changer_mode_) {
        return {};
    }

    return system_info_.tool_to_slot_map;
}

// ============================================================================
// Factory method implementations (in ams_backend.cpp, but included here for mock)
// ============================================================================

// ============================================================================
// Device actions implementation
// ============================================================================

std::vector<helix::printer::DeviceSection> AmsBackendMock::get_device_sections() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mock_device_sections_;
}

std::vector<helix::printer::DeviceAction> AmsBackendMock::get_device_actions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mock_device_actions_;
}

AmsError AmsBackendMock::execute_device_action(const std::string& action_id,
                                               const std::any& value) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Store for test verification
    last_action_id_ = action_id;
    last_action_value_ = value;

    // Mock calibration wizard: simulate AFC_CALIBRATION action_prompt sequence
    if (action_id == "calibration_wizard") {
        // Copy callback before releasing lock — callback may chain into
        // UI code that queries backend state (would deadlock if lock held)
        auto cb = gcode_response_callback_;
        if (!cb) {
            spdlog::warn("[AMS Mock] Calibration wizard: no gcode response callback, skipping");
            return AmsErrorHelper::success();
        }

        spdlog::info("[AMS Mock] Simulating AFC calibration wizard action_prompt");
        lock.unlock();

        // Simulate a multi-step calibration prompt (matches real AFC behavior)
        cb("// action:prompt_begin AFC Calibration");
        cb("// action:prompt_text Lane calibration measures bowden tube length");
        cb("// action:prompt_text for accurate filament loading distances.");
        cb("// action:prompt_text ");
        cb("// action:prompt_text Select a lane to calibrate, or calibrate all lanes.");
        cb("// action:prompt_button_group_start");
        cb("// action:prompt_button Lane 1|RESPOND msg=\"AFC_CALIBRATION LANE=lane1\"|primary");
        cb("// action:prompt_button Lane 2|RESPOND msg=\"AFC_CALIBRATION LANE=lane2\"|primary");
        cb("// action:prompt_button Lane 3|RESPOND msg=\"AFC_CALIBRATION LANE=lane3\"|primary");
        cb("// action:prompt_button Lane 4|RESPOND msg=\"AFC_CALIBRATION LANE=lane4\"|primary");
        cb("// action:prompt_button_group_end");
        cb("// action:prompt_button Calibrate All|RESPOND msg=\"AFC_CALIBRATION ALL=1\"|secondary");
        cb("// action:prompt_footer_button Cancel|RESPOND msg=\"AFC_CALIBRATION CANCEL=1\"|error");
        cb("// action:prompt_show");

        return AmsErrorHelper::success();
    }

    // Find the action to verify it exists
    for (const auto& action : mock_device_actions_) {
        if (action.id == action_id) {
            if (!action.enabled) {
                return AmsErrorHelper::not_supported(action.disable_reason);
            }
            spdlog::info("[AMS Mock] Executed device action: {} with value type: {}", action_id,
                         value.has_value() ? value.type().name() : "none");
            return AmsErrorHelper::success();
        }
    }

    return AmsErrorHelper::not_supported("Unknown action: " + action_id);
}

void AmsBackendMock::set_device_sections(std::vector<helix::printer::DeviceSection> sections) {
    std::lock_guard<std::mutex> lock(mutex_);
    mock_device_sections_ = std::move(sections);
}

void AmsBackendMock::set_device_actions(std::vector<helix::printer::DeviceAction> actions) {
    std::lock_guard<std::mutex> lock(mutex_);
    mock_device_actions_ = std::move(actions);
}

std::pair<std::string, std::any> AmsBackendMock::get_last_executed_action() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {last_action_id_, last_action_value_};
}

void AmsBackendMock::clear_last_executed_action() {
    std::lock_guard<std::mutex> lock(mutex_);
    last_action_id_.clear();
    last_action_value_.reset();
}

void AmsBackendMock::set_gcode_response_callback(std::function<void(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    gcode_response_callback_ = std::move(callback);
    spdlog::debug("[AMS Mock] Gcode response callback {}",
                  gcode_response_callback_ ? "set" : "cleared");
}

// ============================================================================
// Factory method implementations (in ams_backend.cpp, but included here for mock)
// ============================================================================

std::unique_ptr<AmsBackend> AmsBackend::create_mock(int slot_count) {
    return std::make_unique<AmsBackendMock>(slot_count);
}
