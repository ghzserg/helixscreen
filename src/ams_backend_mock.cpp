// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_mock.h"

#include <spdlog/spdlog.h>

#include <algorithm>
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
    system_info_.supports_endless_spool = true;
    system_info_.supports_spoolman = true;
    system_info_.supports_tool_mapping = true;
    system_info_.supports_bypass = true;
    system_info_.has_hardware_bypass_sensor = false; // Default: virtual (manual toggle)

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

        // Temperature recommendations based on material type
        std::string mat(sample.material);
        if (mat == "PLA" || mat == "PLA-CF") {
            slot.nozzle_temp_min = 190;
            slot.nozzle_temp_max = 220;
            slot.bed_temp = 60;
        } else if (mat == "PETG" || mat == "PETG-GF") {
            slot.nozzle_temp_min = 230;
            slot.nozzle_temp_max = 250;
            slot.bed_temp = 80;
        } else if (mat == "ABS") {
            slot.nozzle_temp_min = 240;
            slot.nozzle_temp_max = 260;
            slot.bed_temp = 100;
        } else if (mat == "ASA") {
            slot.nozzle_temp_min = 240;
            slot.nozzle_temp_max = 270;
            slot.bed_temp = 90;
        } else if (mat == "PA-CF" || mat == "PA" || mat == "PA-GF") {
            // Nylon-based materials need high temps
            slot.nozzle_temp_min = 260;
            slot.nozzle_temp_max = 290;
            slot.bed_temp = 85;
        } else if (mat == "TPU") {
            slot.nozzle_temp_min = 220;
            slot.nozzle_temp_max = 250;
            slot.bed_temp = 50;
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

    // Make slot index 3 (4th slot) empty for realistic demo
    if (slot_count > 3) {
        auto* slot = system_info_.get_slot_global(3);
        if (slot) {
            slot->status = SlotStatus::EMPTY;
        }
    }

    spdlog::debug("[AmsBackendMock] Created with {} slots", slot_count);
}

AmsBackendMock::~AmsBackendMock() {
    // Signal shutdown and wait for any running operation thread to finish
    // Using atomic flag - safe without mutex
    shutdown_requested_ = true;
    shutdown_cv_.notify_all();
    wait_for_operation_thread();
    // Don't call stop() - it would try to lock the mutex which may be invalid
    // during static destruction. The running_ flag doesn't matter at this point.
}

void AmsBackendMock::wait_for_operation_thread() {
    if (operation_thread_.joinable()) {
        operation_thread_.join();
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
        spdlog::info("[AmsBackendMock] Started");
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
    return AmsType::HAPPY_HARE; // Mock identifies as Happy Hare
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
        spdlog::info("[AmsBackendMock] Tool change to T{}", tool_number);
    }

    emit_event(EVENT_STATE_CHANGED);
    schedule_completion(AmsAction::LOADING, EVENT_TOOL_CHANGED,
                        system_info_.tool_to_slot_map[tool_number]);

    return AmsErrorHelper::success();
}

AmsError AmsBackendMock::recover() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("Mock backend not started");
        }

        // Reset to idle state
        system_info_.action = AmsAction::IDLE;
        system_info_.operation_detail.clear();
        error_segment_ = PathSegment::NONE; // Clear error location
        spdlog::info("[AmsBackendMock] Recovery complete");
    }

    emit_event(EVENT_STATE_CHANGED);
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

        spdlog::info("[AmsBackendMock] Updated slot {} info", slot_index);
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

void AmsBackendMock::set_has_hardware_bypass_sensor(bool has_sensor) {
    std::lock_guard<std::mutex> lock(mutex_);
    system_info_.has_hardware_bypass_sensor = has_sensor;
    spdlog::debug("[AmsBackendMock] Hardware bypass sensor set to {}", has_sensor);
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

    // Reset shutdown flag for new operation
    shutdown_requested_ = false;

    // Simulate operation delay in background thread with path segment progression
    operation_thread_ = std::thread([this, action, complete_event, slot_index]() {
        // Helper lambda for interruptible sleep
        auto interruptible_sleep = [this](int ms) -> bool {
            std::unique_lock<std::mutex> lock(shutdown_mutex_);
            return !shutdown_cv_.wait_for(lock, std::chrono::milliseconds(ms),
                                          [this] { return shutdown_requested_.load(); });
        };

        // Calculate delay per segment for smooth animation
        int segment_delay = operation_delay_ms_ / 6; // 6 segments to traverse

        if (action == AmsAction::LOADING) {
            // Progress through segments: SPOOL → PREP → LANE → HUB → OUTPUT → TOOLHEAD → NOZZLE
            const PathSegment load_sequence[] = {
                PathSegment::SPOOL,  PathSegment::PREP,     PathSegment::LANE,  PathSegment::HUB,
                PathSegment::OUTPUT, PathSegment::TOOLHEAD, PathSegment::NOZZLE};

            for (auto seg : load_sequence) {
                if (shutdown_requested_)
                    return; // Early exit on shutdown

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    filament_segment_ = seg;
                    system_info_.current_slot =
                        slot_index; // Set active slot early for visualization
                }
                emit_event(EVENT_STATE_CHANGED);
                if (!interruptible_sleep(segment_delay))
                    return; // Exit if shutdown signaled
            }

            // Final state
            {
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
        } else if (action == AmsAction::UNLOADING) {
            // Progress through segments in reverse: NOZZLE → TOOLHEAD → OUTPUT → HUB → LANE → PREP
            // → SPOOL → NONE
            const PathSegment unload_sequence[] = {
                PathSegment::NOZZLE, PathSegment::TOOLHEAD, PathSegment::OUTPUT, PathSegment::HUB,
                PathSegment::LANE,   PathSegment::PREP,     PathSegment::SPOOL,  PathSegment::NONE};

            for (auto seg : unload_sequence) {
                if (shutdown_requested_)
                    return; // Early exit on shutdown

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    filament_segment_ = seg;
                }
                emit_event(EVENT_STATE_CHANGED);
                if (!interruptible_sleep(segment_delay))
                    return; // Exit if shutdown signaled
            }

            // Final state
            {
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
        } else {
            // For other actions, just wait and complete
            if (!interruptible_sleep(operation_delay_ms_))
                return;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                system_info_.action = AmsAction::IDLE;
                system_info_.operation_detail.clear();
            }
        }

        if (shutdown_requested_)
            return; // Final check before emitting

        emit_event(complete_event, slot_index >= 0 ? std::to_string(slot_index) : "");
        emit_event(EVENT_STATE_CHANGED);
    });
}

// ============================================================================
// Factory method implementations (in ams_backend.cpp, but included here for mock)
// ============================================================================

std::unique_ptr<AmsBackend> AmsBackend::create_mock(int slot_count) {
    return std::make_unique<AmsBackendMock>(slot_count);
}
