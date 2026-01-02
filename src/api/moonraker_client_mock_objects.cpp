// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_client_mock_internal.h"

#include <spdlog/spdlog.h>

namespace mock_internal {

void register_object_handlers(std::unordered_map<std::string, MethodHandler>& registry) {
    // printer.objects.query - Query printer object state
    registry["printer.objects.query"] =
        [](MoonrakerClientMock* self, const json& params, std::function<void(json)> success_cb,
           std::function<void(const MoonrakerError&)> error_cb) -> bool {
        (void)error_cb;
        json status_obj = json::object();

        // Check what objects are being queried
        if (params.contains("objects")) {
            auto& objects = params["objects"];

            // webhooks state (for is_printer_ready)
            if (objects.contains("webhooks")) {
                auto klippy = self->get_klippy_state();
                std::string state_str = "ready";
                switch (klippy) {
                case MoonrakerClientMock::KlippyState::STARTUP:
                    state_str = "startup";
                    break;
                case MoonrakerClientMock::KlippyState::SHUTDOWN:
                    state_str = "shutdown";
                    break;
                case MoonrakerClientMock::KlippyState::ERROR:
                    state_str = "error";
                    break;
                default:
                    break;
                }
                status_obj["webhooks"] = {{"state", state_str}};
            }

            // print_stats (for get_print_state)
            if (objects.contains("print_stats")) {
                // Use get_print_phase to derive print state string
                auto phase = self->get_print_phase();
                std::string state_str = "standby";
                switch (phase) {
                case MoonrakerClientMock::MockPrintPhase::IDLE:
                    state_str = "standby";
                    break;
                case MoonrakerClientMock::MockPrintPhase::PREHEAT:
                case MoonrakerClientMock::MockPrintPhase::PRINTING:
                    state_str = "printing";
                    break;
                case MoonrakerClientMock::MockPrintPhase::PAUSED:
                    state_str = "paused";
                    break;
                case MoonrakerClientMock::MockPrintPhase::COMPLETE:
                    state_str = "complete";
                    break;
                case MoonrakerClientMock::MockPrintPhase::CANCELLED:
                    state_str = "cancelled";
                    break;
                case MoonrakerClientMock::MockPrintPhase::ERROR:
                    state_str = "error";
                    break;
                }
                status_obj["print_stats"] = {{"state", state_str}};
            }

            // configfile.settings (for update_safety_limits_from_printer)
            if (objects.contains("configfile")) {
                status_obj["configfile"] = {
                    {"settings",
                     {{"printer", {{"max_velocity", 500.0}, {"max_accel", 10000.0}}},
                      {"stepper_x", {{"position_min", 0.0}, {"position_max", 250.0}}},
                      {"stepper_y", {{"position_min", 0.0}, {"position_max", 250.0}}},
                      {"stepper_z", {{"position_min", 0.0}, {"position_max", 300.0}}},
                      {"extruder",
                       {{"min_temp", 0.0}, {"max_temp", 300.0}, {"min_extrude_temp", 170.0}}},
                      {"heater_bed", {{"min_temp", 0.0}, {"max_temp", 120.0}}}}}};
            }

            // toolhead (for get_machine_limits)
            if (objects.contains("toolhead")) {
                status_obj["toolhead"] = {{"max_velocity", 500.0},
                                          {"max_accel", 10000.0},
                                          {"max_accel_to_decel", 5000.0},
                                          {"square_corner_velocity", 5.0},
                                          {"max_z_velocity", 40.0},
                                          {"max_z_accel", 1000.0},
                                          {"position", {0.0, 0.0, 0.0, 0.0}},
                                          {"homed_axes", "xyz"}};
            }

            // idle_timeout (for motors_enabled state)
            if (objects.contains("idle_timeout")) {
                // Derive state from print phase and motor state:
                // - "Idle" when motors disabled (via M84)
                // - "Printing" when active print in progress
                // - "Ready" when motors enabled but not printing
                std::string idle_state;
                if (!self->are_motors_enabled()) {
                    idle_state = "Idle";
                } else {
                    auto phase = self->get_print_phase();
                    idle_state = (phase == MoonrakerClientMock::MockPrintPhase::PRINTING ||
                                  phase == MoonrakerClientMock::MockPrintPhase::PREHEAT)
                                     ? "Printing"
                                     : "Ready";
                }
                status_obj["idle_timeout"] = {{"state", idle_state}};
            }
        }

        if (success_cb) {
            json response = {{"result", {{"status", status_obj}}}};
            success_cb(response);
        }
        return true;
    };
}

} // namespace mock_internal
