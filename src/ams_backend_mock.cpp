// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_mock.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <thread>

// Sample filament colors for mock gates
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
    {0xE53935, "Red", "PLA", "Polymaker"},      // Gate 0: Red PLA
    {0x1E88E5, "Blue", "PETG", "eSUN"},         // Gate 1: Blue PETG
    {0x43A047, "Green", "ABS", "Bambu"},        // Gate 2: Green ABS
    {0xFDD835, "Yellow", "ASA", "Polymaker"},   // Gate 3: Yellow ASA
    {0x424242, "Carbon", "PLA-CF", "Overture"}, // Gate 4: Carbon PLA-CF
    {0x8E24AA, "Purple", "PA-CF", "Bambu"},     // Gate 5: Purple PA-CF (Nylon)
    {0xFF6F00, "Orange", "TPU", "eSUN"},        // Gate 6: Orange TPU (Flexible)
    {0x90CAF9, "Sky Blue", "PETG-GF", "Prusa"}, // Gate 7: PETG-GF (Glass Filled)
};
constexpr int NUM_SAMPLE_FILAMENTS = sizeof(SAMPLE_FILAMENTS) / sizeof(SAMPLE_FILAMENTS[0]);
} // namespace

AmsBackendMock::AmsBackendMock(int gate_count) {
    // Clamp gate count to reasonable range
    gate_count = std::clamp(gate_count, 1, 16);

    // Initialize system info
    system_info_.type = AmsType::HAPPY_HARE; // Mock as Happy Hare
    system_info_.type_name = "Happy Hare (Mock)";
    system_info_.version = "2.7.0-mock";
    system_info_.current_tool = -1;
    system_info_.current_gate = -1;
    system_info_.filament_loaded = false;
    system_info_.action = AmsAction::IDLE;
    system_info_.total_gates = gate_count;
    system_info_.supports_endless_spool = true;
    system_info_.supports_spoolman = true;
    system_info_.supports_tool_mapping = true;
    system_info_.supports_bypass = true;
    system_info_.has_hardware_bypass_sensor = false; // Default: virtual (manual toggle)

    // Create single unit with all gates
    AmsUnit unit;
    unit.unit_index = 0;
    unit.name = "Mock MMU";
    unit.gate_count = gate_count;
    unit.first_gate_global_index = 0;
    unit.connected = true;
    unit.firmware_version = "mock-1.0";
    unit.has_encoder = true;
    unit.has_toolhead_sensor = true;
    unit.has_gate_sensors = true;

    // Initialize gates with sample filament data
    for (int i = 0; i < gate_count; ++i) {
        GateInfo gate;
        gate.gate_index = i;
        gate.global_index = i;
        gate.status = GateStatus::AVAILABLE;
        gate.mapped_tool = i; // Direct 1:1 mapping

        // Assign sample filament data (cycle through samples)
        const auto& sample = SAMPLE_FILAMENTS[i % NUM_SAMPLE_FILAMENTS];
        gate.color_rgb = sample.color;
        gate.color_name = sample.color_name;
        gate.material = sample.material;
        gate.brand = sample.brand;

        // Mock Spoolman data with dramatic fill level differences for demo
        gate.spoolman_id = 1000 + i;
        gate.spool_name = std::string(sample.color_name) + " " + sample.material;
        gate.total_weight_g = 1000.0f;
        // Vary fill levels dramatically: 100%, 75%, 40%, 10% for clear visual difference
        static const float fill_levels[] = {1.0f, 0.75f, 0.40f, 0.10f, 0.90f, 0.50f, 0.25f, 0.05f};
        gate.remaining_weight_g = gate.total_weight_g * fill_levels[i % 8];

        // Temperature recommendations based on material type
        std::string mat(sample.material);
        if (mat == "PLA" || mat == "PLA-CF") {
            gate.nozzle_temp_min = 190;
            gate.nozzle_temp_max = 220;
            gate.bed_temp = 60;
        } else if (mat == "PETG" || mat == "PETG-GF") {
            gate.nozzle_temp_min = 230;
            gate.nozzle_temp_max = 250;
            gate.bed_temp = 80;
        } else if (mat == "ABS") {
            gate.nozzle_temp_min = 240;
            gate.nozzle_temp_max = 260;
            gate.bed_temp = 100;
        } else if (mat == "ASA") {
            gate.nozzle_temp_min = 240;
            gate.nozzle_temp_max = 270;
            gate.bed_temp = 90;
        } else if (mat == "PA-CF" || mat == "PA" || mat == "PA-GF") {
            // Nylon-based materials need high temps
            gate.nozzle_temp_min = 260;
            gate.nozzle_temp_max = 290;
            gate.bed_temp = 85;
        } else if (mat == "TPU") {
            gate.nozzle_temp_min = 220;
            gate.nozzle_temp_max = 250;
            gate.bed_temp = 50;
        }

        unit.gates.push_back(gate);
    }

    system_info_.units.push_back(unit);

    // Initialize tool-to-gate mapping (1:1)
    system_info_.tool_to_gate_map.resize(gate_count);
    for (int i = 0; i < gate_count; ++i) {
        system_info_.tool_to_gate_map[i] = i;
    }

    // Start with gate 0 loaded for realistic demo appearance
    if (gate_count > 0) {
        auto* gate = system_info_.get_gate_global(0);
        if (gate) {
            gate->status = GateStatus::LOADED;
        }
        system_info_.current_gate = 0;
        system_info_.current_tool = 0;
        system_info_.filament_loaded = true;
    }

    // Make gate index 3 (4th slot) empty for realistic demo
    if (gate_count > 3) {
        auto* gate = system_info_.get_gate_global(3);
        if (gate) {
            gate->status = GateStatus::EMPTY;
        }
    }

    spdlog::debug("[AmsBackendMock] Created with {} gates", gate_count);
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

GateInfo AmsBackendMock::get_gate_info(int global_index) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto* gate = system_info_.get_gate_global(global_index);
    if (gate) {
        return *gate;
    }

    // Return empty gate info for invalid index
    GateInfo empty;
    empty.gate_index = -1;
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

int AmsBackendMock::get_current_gate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.current_gate;
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

PathSegment AmsBackendMock::infer_error_segment() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_segment_;
}

AmsError AmsBackendMock::load_filament(int gate_index) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("Mock backend not started");
        }

        if (system_info_.action != AmsAction::IDLE) {
            return AmsErrorHelper::busy(ams_action_to_string(system_info_.action));
        }

        if (gate_index < 0 || gate_index >= system_info_.total_gates) {
            return AmsErrorHelper::invalid_gate(gate_index, system_info_.total_gates - 1);
        }

        auto* gate = system_info_.get_gate_global(gate_index);
        if (!gate || gate->status == GateStatus::EMPTY) {
            return AmsErrorHelper::gate_not_available(gate_index);
        }

        // Start loading
        system_info_.action = AmsAction::LOADING;
        system_info_.operation_detail = "Loading from gate " + std::to_string(gate_index);
        filament_segment_ = PathSegment::SPOOL; // Start at spool
        spdlog::info("[AmsBackendMock] Loading from gate {}", gate_index);
    }

    emit_event(EVENT_STATE_CHANGED);
    schedule_completion(AmsAction::LOADING, EVENT_LOAD_COMPLETE, gate_index);

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

AmsError AmsBackendMock::select_gate(int gate_index) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            return AmsErrorHelper::not_connected("Mock backend not started");
        }

        if (system_info_.action != AmsAction::IDLE) {
            return AmsErrorHelper::busy(ams_action_to_string(system_info_.action));
        }

        if (gate_index < 0 || gate_index >= system_info_.total_gates) {
            return AmsErrorHelper::invalid_gate(gate_index, system_info_.total_gates - 1);
        }

        // Immediate selection (no filament movement)
        system_info_.current_gate = gate_index;
        spdlog::info("[AmsBackendMock] Selected gate {}", gate_index);
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
            tool_number >= static_cast<int>(system_info_.tool_to_gate_map.size())) {
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
                        system_info_.tool_to_gate_map[tool_number]);

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

AmsError AmsBackendMock::set_gate_info(int gate_index, const GateInfo& info) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (gate_index < 0 || gate_index >= system_info_.total_gates) {
            return AmsErrorHelper::invalid_gate(gate_index, system_info_.total_gates - 1);
        }

        auto* gate = system_info_.get_gate_global(gate_index);
        if (!gate) {
            return AmsErrorHelper::invalid_gate(gate_index, system_info_.total_gates - 1);
        }

        // Update filament info
        gate->color_name = info.color_name;
        gate->color_rgb = info.color_rgb;
        gate->material = info.material;
        gate->brand = info.brand;
        gate->spoolman_id = info.spoolman_id;
        gate->spool_name = info.spool_name;
        gate->remaining_weight_g = info.remaining_weight_g;
        gate->total_weight_g = info.total_weight_g;
        gate->nozzle_temp_min = info.nozzle_temp_min;
        gate->nozzle_temp_max = info.nozzle_temp_max;
        gate->bed_temp = info.bed_temp;

        spdlog::info("[AmsBackendMock] Updated gate {} info", gate_index);
    }

    // Emit event OUTSIDE the lock to avoid deadlock
    emit_event(EVENT_GATE_CHANGED, std::to_string(gate_index));
    return AmsErrorHelper::success();
}

AmsError AmsBackendMock::set_tool_mapping(int tool_number, int gate_index) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (tool_number < 0 || tool_number >= static_cast<int>(system_info_.tool_to_gate_map.size())) {
        return AmsError(AmsResult::INVALID_TOOL,
                        "Tool " + std::to_string(tool_number) + " out of range",
                        "Invalid tool number", "");
    }

    if (gate_index < 0 || gate_index >= system_info_.total_gates) {
        return AmsErrorHelper::invalid_gate(gate_index, system_info_.total_gates - 1);
    }

    system_info_.tool_to_gate_map[tool_number] = gate_index;

    // Update gate's mapped_tool
    for (auto& unit : system_info_.units) {
        for (auto& gate : unit.gates) {
            if (gate.global_index == gate_index) {
                gate.mapped_tool = tool_number;
            }
        }
    }

    spdlog::info("[AmsBackendMock] Mapped T{} to gate {}", tool_number, gate_index);
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

        // Enable bypass mode: current_gate = -2 indicates bypass
        system_info_.current_gate = -2;
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

        if (system_info_.current_gate != -2) {
            return AmsError(AmsResult::WRONG_STATE, "Bypass not active",
                            "Bypass mode is not currently active", "");
        }

        // Disable bypass mode
        system_info_.current_gate = -1;
        system_info_.filament_loaded = false;
        filament_segment_ = PathSegment::NONE;
        spdlog::info("[AmsBackendMock] Bypass mode disabled");
    }

    emit_event(EVENT_STATE_CHANGED);
    return AmsErrorHelper::success();
}

bool AmsBackendMock::is_bypass_active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.current_gate == -2;
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
        } else if (error == AmsResult::GATE_BLOCKED || error == AmsResult::GATE_NOT_AVAILABLE) {
            error_segment_ = PathSegment::PREP; // Gate issues at prep/entry
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

void AmsBackendMock::force_gate_status(int gate_index, GateStatus status) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto* gate = system_info_.get_gate_global(gate_index);
    if (gate) {
        gate->status = status;
        spdlog::debug("[AmsBackendMock] Forced gate {} status to {}", gate_index,
                      gate_status_to_string(status));
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
                                         int gate_index) {
    // Wait for any previous operation to complete first
    wait_for_operation_thread();

    // Reset shutdown flag for new operation
    shutdown_requested_ = false;

    // Simulate operation delay in background thread with path segment progression
    operation_thread_ = std::thread([this, action, complete_event, gate_index]() {
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
                    system_info_.current_gate =
                        gate_index; // Set active gate early for visualization
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
                if (gate_index >= 0) {
                    system_info_.current_gate = gate_index;
                    system_info_.current_tool = gate_index;
                    auto* gate = system_info_.get_gate_global(gate_index);
                    if (gate) {
                        gate->status = GateStatus::LOADED;
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
                if (system_info_.current_gate >= 0) {
                    auto* gate = system_info_.get_gate_global(system_info_.current_gate);
                    if (gate) {
                        gate->status = GateStatus::AVAILABLE;
                    }
                }
                system_info_.filament_loaded = false;
                system_info_.current_gate = -1;
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

        emit_event(complete_event, gate_index >= 0 ? std::to_string(gate_index) : "");
        emit_event(EVENT_STATE_CHANGED);
    });
}

// ============================================================================
// Factory method implementations (in ams_backend.cpp, but included here for mock)
// ============================================================================

std::unique_ptr<AmsBackend> AmsBackend::create_mock(int gate_count) {
    return std::make_unique<AmsBackendMock>(gate_count);
}
