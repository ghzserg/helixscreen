// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "command_sequencer.h"

#include "moonraker_api.h"
#include "moonraker_client.h"
#include "printer_state.h"
#include "spdlog/spdlog.h"

#include <sstream>

namespace helix {
namespace gcode {

// ============================================================================
// Construction / Destruction
// ============================================================================

CommandSequencer::CommandSequencer(MoonrakerClient& client, MoonrakerAPI& api, PrinterState& state)
    : client_(client), api_(api), printer_state_(state) {
    // Suppress unused warnings for reserved fields
    (void)client_;
    (void)printer_state_;
    spdlog::debug("[CommandSequencer] Created");
}

CommandSequencer::~CommandSequencer() {
    if (is_running()) {
        spdlog::warn("[CommandSequencer] Destructor called while running - forcing cancellation");
        sequencer_state_.store(SequencerState::CANCELLED);
    }
}

// ============================================================================
// Queue Management
// ============================================================================

void CommandSequencer::add_operation(OperationType type, const OperationParams& params,
                                     const std::string& display_name,
                                     std::chrono::milliseconds timeout) {
    if (is_running()) {
        spdlog::warn("[CommandSequencer] Cannot add operation while running");
        return;
    }

    std::lock_guard<std::mutex> lock(queue_mutex_);

    QueuedOperation op;
    op.type = type;
    op.params = params;
    op.display_name = display_name;
    op.timeout = timeout;

    queue_.push(op);
    total_steps_.store(static_cast<int>(queue_.size()));

    spdlog::debug("[CommandSequencer] Added operation: {} (timeout={}ms)", display_name,
                  timeout.count());
}

void CommandSequencer::clear() {
    if (is_running()) {
        spdlog::warn("[CommandSequencer] Cannot clear queue while running");
        return;
    }

    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!queue_.empty()) {
        queue_.pop();
    }
    current_op_.reset();
    current_step_.store(0);
    total_steps_.store(0);
    sequencer_state_.store(SequencerState::IDLE);

    spdlog::debug("[CommandSequencer] Queue cleared");
}

size_t CommandSequencer::queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return queue_.size();
}

// ============================================================================
// Execution Control
// ============================================================================

bool CommandSequencer::start(ProgressCallback on_progress, CompletionCallback on_complete) {
    if (is_running()) {
        spdlog::warn("[CommandSequencer] Already running");
        return false;
    }

    // Check queue and set up initial state under lock
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);

        if (queue_.empty()) {
            spdlog::warn("[CommandSequencer] Cannot start with empty queue");
            return false;
        }

        // Store callbacks
        {
            std::lock_guard<std::mutex> cb_lock(callback_mutex_);
            on_progress_ = on_progress;
            on_complete_ = on_complete;
        }

        // Reset state
        current_step_.store(0);
        total_steps_.store(static_cast<int>(queue_.size()));
        cancel_requested_.store(false);
        sequencer_state_.store(SequencerState::RUNNING);

        spdlog::info("[CommandSequencer] Starting sequence with {} operations", queue_.size());
    }

    // Start first operation (lock released to avoid deadlock)
    execute_next();

    return true;
}

bool CommandSequencer::cancel() {
    if (!is_running()) {
        spdlog::debug("[CommandSequencer] Cancel called but not running");
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    bool escalate = false;

    if (cancel_requested_.load()) {
        // Check if within escalation window
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_cancel_time_);
        if (elapsed < ESCALATION_WINDOW) {
            escalate = true;
        }
    }

    cancel_requested_.store(true);
    last_cancel_time_ = now;

    if (escalate) {
        // Level 2: Emergency stop
        spdlog::warn("[CommandSequencer] EMERGENCY STOP (M112)");
        api_.emergency_stop([]() {}, [](const MoonrakerError&) {});
        sequencer_state_.store(SequencerState::CANCELLED);
        notify_complete(false, "Emergency stop triggered");
    } else {
        // Level 1: Graceful cancel
        spdlog::info("[CommandSequencer] Graceful cancel requested (CANCEL_PRINT)");
        sequencer_state_.store(SequencerState::CANCELLING);

        // Send CANCEL_PRINT and M400 (wait for moves to complete)
        api_.cancel_print(
            [this]() {
                spdlog::info("[CommandSequencer] Cancel command sent");
                // Wait for moves to complete
                api_.execute_gcode("M400", []() {}, [](const MoonrakerError&) {});
            },
            [this](const MoonrakerError& err) {
                spdlog::warn("[CommandSequencer] Cancel failed: {}", err.message);
                // Still mark as cancelled
                sequencer_state_.store(SequencerState::CANCELLED);
                notify_complete(false, "Cancelled (with error: " + err.message + ")");
            });
    }

    return true;
}

// ============================================================================
// State Queries
// ============================================================================

std::string CommandSequencer::current_operation_name() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (current_op_.has_value()) {
        return current_op_->display_name;
    }
    return "";
}

// ============================================================================
// State Update Processing
// ============================================================================

void CommandSequencer::process_state_update(const json& notification) {
    if (!is_running() && sequencer_state_.load() != SequencerState::WAITING) {
        return;
    }

    // Extract status from notification
    // Moonraker sends: {"jsonrpc": "2.0", "method": "notify_status_update", "params": [status,
    // timestamp]}
    json status;
    if (notification.contains("params") && notification["params"].is_array() &&
        !notification["params"].empty()) {
        status = notification["params"][0];
    } else if (notification.is_object() && !notification.contains("jsonrpc")) {
        // Direct status object (for testing)
        status = notification;
    } else {
        return;
    }

    simulate_state_update(status);
}

void CommandSequencer::simulate_state_update(const json& status) {
    if (sequencer_state_.load() != SequencerState::WAITING) {
        return;
    }

    bool should_execute_next = false;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);

        if (!current_op_.has_value()) {
            return;
        }

        // Check timeout
        auto now = std::chrono::steady_clock::now();
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - operation_start_time_);
        if (elapsed > current_timeout_) {
            spdlog::warn("[CommandSequencer] Operation '{}' timed out after {}ms",
                         current_op_->display_name, elapsed.count());
            handle_timeout();
            return;
        }

        // Check completion
        if (check_operation_complete(current_op_.value(), status)) {
            spdlog::info("[CommandSequencer] Operation '{}' completed", current_op_->display_name);
            // Mark for next execution but don't call execute_next while holding lock
            current_op_.reset();
            sequencer_state_.store(SequencerState::RUNNING);
            should_execute_next = true;
        }
    }

    // Execute next operation outside the lock to avoid deadlock
    if (should_execute_next) {
        execute_next();
    }
}

// ============================================================================
// Completion Conditions
// ============================================================================

CompletionCondition CommandSequencer::get_completion_condition(OperationType type) {
    CompletionCondition cond;

    switch (type) {
    case OperationType::HOMING:
        cond.object_name = "toolhead";
        cond.field_path = "homed_axes";
        cond.check = [](const json& val) {
            if (!val.is_string())
                return false;
            std::string axes = val.get<std::string>();
            // Check for xyz (case-insensitive)
            return axes.find('x') != std::string::npos && axes.find('y') != std::string::npos &&
                   axes.find('z') != std::string::npos;
        };
        break;

    case OperationType::QGL:
        cond.object_name = "quad_gantry_level";
        cond.field_path = "applied";
        cond.check = [](const json& val) { return val.is_boolean() && val.get<bool>(); };
        break;

    case OperationType::Z_TILT:
        cond.object_name = "z_tilt";
        cond.field_path = "applied";
        cond.check = [](const json& val) { return val.is_boolean() && val.get<bool>(); };
        break;

    case OperationType::BED_LEVELING:
        cond.object_name = "bed_mesh";
        cond.field_path = "profile_name";
        cond.check = [](const json& val) {
            // Complete when profile_name is non-empty (mesh loaded)
            return val.is_string() && !val.get<std::string>().empty();
        };
        break;

    case OperationType::NOZZLE_CLEAN:
    case OperationType::PURGE_LINE:
    case OperationType::CHAMBER_SOAK:
    case OperationType::SKEW_CORRECT:
        // These complete when idle_timeout returns to "Ready"
        cond.object_name = "idle_timeout";
        cond.field_path = "state";
        cond.check = [](const json& val) {
            return val.is_string() && val.get<std::string>() == "Ready";
        };
        break;

    case OperationType::START_PRINT:
        // Print started when print_stats.state changes to "printing"
        cond.object_name = "print_stats";
        cond.field_path = "state";
        cond.check = [](const json& val) {
            return val.is_string() && val.get<std::string>() == "printing";
        };
        break;
    }

    return cond;
}

// ============================================================================
// Private Implementation
// ============================================================================

void CommandSequencer::execute_next() {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    if (queue_.empty()) {
        spdlog::info("[CommandSequencer] All operations complete");
        sequencer_state_.store(SequencerState::COMPLETED);
        notify_complete(true, "");
        return;
    }

    if (cancel_requested_.load()) {
        spdlog::info("[CommandSequencer] Cancel requested, stopping sequence");
        sequencer_state_.store(SequencerState::CANCELLED);
        notify_complete(false, "Cancelled by user");
        return;
    }

    // Get next operation
    current_op_ = queue_.front();
    queue_.pop();
    current_step_.fetch_add(1);

    spdlog::info("[CommandSequencer] Executing step {}/{}: {}", current_step_.load(),
                 total_steps_.load(), current_op_->display_name);

    // Record start time for timeout
    operation_start_time_ = std::chrono::steady_clock::now();
    current_timeout_ = current_op_->timeout;

    // Notify progress
    notify_progress();

    // Send command
    send_operation_command(current_op_.value());

    // Transition to waiting for completion
    sequencer_state_.store(SequencerState::WAITING);
}

void CommandSequencer::send_operation_command(const QueuedOperation& op) {
    // START_PRINT is special - use the Moonraker API's start_print method
    if (op.type == OperationType::START_PRINT) {
        if (op.params.filename.empty()) {
            handle_failure("START_PRINT missing filename");
            return;
        }

        spdlog::debug("[CommandSequencer] Starting print via API: {}", op.params.filename);

        api_.start_print(
            op.params.filename,
            [name = op.display_name]() {
                spdlog::debug("[CommandSequencer] Print start command sent for '{}'", name);
                // Note: This just means the command was sent, not that print started
            },
            [this, name = op.display_name](const MoonrakerError& err) {
                spdlog::error("[CommandSequencer] Print start failed for '{}': {}", name,
                              err.message);
                handle_failure("Print start error: " + err.message);
            });
        return;
    }

    // All other operations use G-code
    std::string gcode = generate_gcode(op);

    spdlog::debug("[CommandSequencer] Sending G-code: {}", gcode);

    api_.execute_gcode(
        gcode,
        [name = op.display_name]() {
            spdlog::debug("[CommandSequencer] G-code sent for '{}'", name);
            // Note: This just means the command was sent, not that it completed
        },
        [this, name = op.display_name](const MoonrakerError& err) {
            spdlog::error("[CommandSequencer] G-code failed for '{}': {}", name, err.message);
            handle_failure("G-code error: " + err.message);
        });
}

bool CommandSequencer::check_operation_complete(const QueuedOperation& op, const json& status) {
    auto cond = get_completion_condition(op.type);

    if (cond.object_name.empty()) {
        // No completion condition defined - assume instant completion
        return true;
    }

    // Look for the object in status
    if (!status.contains(cond.object_name)) {
        return false;
    }

    const json& obj = status[cond.object_name];

    // Navigate to field
    if (!obj.contains(cond.field_path)) {
        return false;
    }

    return cond.check(obj[cond.field_path]);
}

void CommandSequencer::handle_timeout() {
    std::string op_name = current_op_.has_value() ? current_op_->display_name : "unknown";
    spdlog::error("[CommandSequencer] Operation '{}' timed out", op_name);

    sequencer_state_.store(SequencerState::FAILED);
    notify_complete(false, "Timeout waiting for: " + op_name);
}

void CommandSequencer::handle_failure(const std::string& error) {
    spdlog::error("[CommandSequencer] Operation failed: {}", error);

    sequencer_state_.store(SequencerState::FAILED);
    notify_complete(false, error);
}

void CommandSequencer::notify_progress() {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (on_progress_) {
        std::string name = current_op_.has_value() ? current_op_->display_name : "";
        int step = current_step_.load();
        int total = total_steps_.load();
        float progress =
            total > 0 ? static_cast<float>(step - 1) / static_cast<float>(total) : 0.0f;

        on_progress_(name, step, total, progress);
    }
}

void CommandSequencer::notify_complete(bool success, const std::string& error) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (on_complete_) {
        on_complete_(success, error);
        on_complete_ = nullptr; // One-shot callback
    }
}

std::string CommandSequencer::generate_gcode(const QueuedOperation& op) const {
    std::ostringstream gcode;

    switch (op.type) {
    case OperationType::HOMING:
        gcode << "G28";
        break;

    case OperationType::QGL:
        gcode << "QUAD_GANTRY_LEVEL";
        break;

    case OperationType::Z_TILT:
        gcode << "Z_TILT_ADJUST";
        break;

    case OperationType::BED_LEVELING:
        if (!op.params.profile_name.empty()) {
            gcode << "BED_MESH_PROFILE LOAD=" << op.params.profile_name;
        } else {
            gcode << "BED_MESH_CALIBRATE";
        }
        break;

    case OperationType::NOZZLE_CLEAN:
        // Try common macro names - caller should specify via params
        if (op.params.extra.count("macro")) {
            gcode << op.params.extra.at("macro");
        } else {
            gcode << "CLEAN_NOZZLE";
        }
        break;

    case OperationType::PURGE_LINE:
        if (op.params.extra.count("macro")) {
            gcode << op.params.extra.at("macro");
        } else {
            gcode << "PURGE_LINE";
        }
        break;

    case OperationType::CHAMBER_SOAK:
        if (op.params.extra.count("macro")) {
            gcode << op.params.extra.at("macro");
        } else {
            gcode << "HEAT_SOAK";
        }
        if (op.params.temperature > 0) {
            gcode << " TEMP=" << static_cast<int>(op.params.temperature);
        }
        if (op.params.duration_minutes > 0) {
            gcode << " DURATION=" << op.params.duration_minutes;
        }
        break;

    case OperationType::SKEW_CORRECT:
        if (op.params.extra.count("macro")) {
            gcode << op.params.extra.at("macro");
        } else if (op.params.extra.count("profile")) {
            gcode << "SKEW_PROFILE LOAD=" << op.params.extra.at("profile");
        } else {
            gcode << "SKEW_PROFILE";
        }
        break;

    case OperationType::START_PRINT:
        // START_PRINT is special - uses Moonraker API, not G-code
        // This generates the SDCARD_PRINT_FILE command as fallback
        if (!op.params.filename.empty()) {
            gcode << "SDCARD_PRINT_FILE FILENAME=" << op.params.filename;
        }
        break;
    }

    // Add any extra parameters
    for (const auto& kv : op.params.extra) {
        if (kv.first != "macro") { // Already handled
            gcode << " " << kv.first << "=" << kv.second;
        }
    }

    return gcode.str();
}

// ============================================================================
// Utility Functions
// ============================================================================

std::string sequencer_state_name(SequencerState state) {
    switch (state) {
    case SequencerState::IDLE:
        return "idle";
    case SequencerState::RUNNING:
        return "running";
    case SequencerState::WAITING:
        return "waiting";
    case SequencerState::CANCELLING:
        return "cancelling";
    case SequencerState::CANCELLED:
        return "cancelled";
    case SequencerState::COMPLETED:
        return "completed";
    case SequencerState::FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

} // namespace gcode
} // namespace helix
