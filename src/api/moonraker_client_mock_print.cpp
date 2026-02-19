// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_update_queue.h"

#include "app_globals.h"
#include "moonraker_client_mock_internal.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <lvgl.h>

using namespace helix;

namespace mock_internal {

void register_print_handlers(std::unordered_map<std::string, MethodHandler>& registry) {
    // printer.gcode.script - Execute G-code script
    // Like real Moonraker, returns error for out-of-range moves and other gcode failures
    registry["printer.gcode.script"] =
        [](MoonrakerClientMock* self, const json& params, std::function<void(json)> success_cb,
           std::function<void(const MoonrakerError&)> error_cb) -> bool {
        std::string script;
        if (params.contains("script")) {
            script = params["script"].get<std::string>();
        }
        int result = self->gcode_script(script); // Process G-code (updates LED state, etc.)
        if (result != 0) {
            // G-code execution failed (e.g., out-of-range move)
            // Return error like real Moonraker does
            if (error_cb) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::JSON_RPC_ERROR;
                err.message = self->get_last_gcode_error();
                err.method = "printer.gcode.script";
                error_cb(err);
            }
        } else if (success_cb) {
            success_cb(json::object()); // Return empty success response
        }
        return true;
    };

    // printer.print.start - Start a print job
    registry["printer.print.start"] =
        [](MoonrakerClientMock* self, const json& params, std::function<void(json)> success_cb,
           std::function<void(const MoonrakerError&)> error_cb) -> bool {
        std::string filename;
        if (params.contains("filename")) {
            filename = params["filename"].get<std::string>();
        }
        if (!filename.empty()) {
            if (self->start_print_internal(filename)) {
                if (success_cb) {
                    success_cb(json::object());
                }
            } else if (error_cb) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::VALIDATION_ERROR;
                err.message = "Failed to start print";
                err.method = "printer.print.start";
                error_cb(err);
            }
        } else if (error_cb) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Missing filename parameter";
            err.method = "printer.print.start";
            error_cb(err);
        }
        return true;
    };

    // printer.print.pause - Pause current print
    registry["printer.print.pause"] =
        [](MoonrakerClientMock* self, const json& params, std::function<void(json)> success_cb,
           std::function<void(const MoonrakerError&)> error_cb) -> bool {
        (void)params;
        if (self->pause_print_internal()) {
            if (success_cb) {
                success_cb(json::object());
            }
        } else if (error_cb) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Cannot pause - not currently printing";
            err.method = "printer.print.pause";
            error_cb(err);
        }
        return true;
    };

    // printer.print.resume - Resume paused print
    registry["printer.print.resume"] =
        [](MoonrakerClientMock* self, const json& params, std::function<void(json)> success_cb,
           std::function<void(const MoonrakerError&)> error_cb) -> bool {
        (void)params;
        if (self->resume_print_internal()) {
            if (success_cb) {
                success_cb(json::object());
            }
        } else if (error_cb) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Cannot resume - not currently paused";
            err.method = "printer.print.resume";
            error_cb(err);
        }
        return true;
    };

    // printer.print.cancel - Cancel current print
    registry["printer.print.cancel"] =
        [](MoonrakerClientMock* self, const json& params, std::function<void(json)> success_cb,
           std::function<void(const MoonrakerError&)> error_cb) -> bool {
        (void)params;
        if (self->cancel_print_internal()) {
            if (success_cb) {
                success_cb(json::object());
            }
        } else if (error_cb) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Cannot cancel - no active print";
            err.method = "printer.print.cancel";
            error_cb(err);
        }
        return true;
    };

    // printer.emergency_stop - Execute emergency stop (M112)
    registry["printer.emergency_stop"] =
        [](MoonrakerClientMock* self, [[maybe_unused]] const json& params,
           std::function<void(json)> success_cb,
           [[maybe_unused]] std::function<void(const MoonrakerError&)> error_cb) -> bool {
        self->emergency_stop_internal();

        if (success_cb) {
            success_cb(json::object());
        }
        return true;
    };

    // printer.firmware_restart - Restart firmware (MCU reset)
    registry["printer.firmware_restart"] =
        []([[maybe_unused]] MoonrakerClientMock* self, [[maybe_unused]] const json& params,
           std::function<void(json)> success_cb,
           [[maybe_unused]] std::function<void(const MoonrakerError&)> error_cb) -> bool {
        spdlog::info("[MoonrakerClientMock] Firmware restart initiated");

        // Simulate restart: briefly go SHUTDOWN, then READY after 1 second
        helix::ui::async_call(
            [](void*) {
                get_printer_state().set_klippy_state_sync(KlippyState::SHUTDOWN);

                // Schedule recovery to READY after 1 second
                static lv_timer_t* timer = nullptr;
                if (timer) {
                    lv_timer_delete(timer);
                }
                timer = lv_timer_create(
                    [](lv_timer_t* t) {
                        spdlog::info("[MoonrakerClientMock] Firmware restart complete - READY");
                        get_printer_state().set_klippy_state_sync(KlippyState::READY);
                        lv_timer_delete(t);
                        timer = nullptr;
                    },
                    1000, nullptr);
                lv_timer_set_repeat_count(timer, 1);
            },
            nullptr);

        if (success_cb) {
            success_cb(json::object());
        }
        return true;
    };

    // printer.restart - Restart Klipper (soft restart)
    registry["printer.restart"] =
        []([[maybe_unused]] MoonrakerClientMock* self, [[maybe_unused]] const json& params,
           std::function<void(json)> success_cb,
           [[maybe_unused]] std::function<void(const MoonrakerError&)> error_cb) -> bool {
        spdlog::info("[MoonrakerClientMock] Klipper restart initiated");

        // Simulate restart: briefly go SHUTDOWN, then READY after 500ms
        helix::ui::async_call(
            [](void*) {
                get_printer_state().set_klippy_state_sync(KlippyState::SHUTDOWN);

                // Schedule recovery to READY after 500ms (faster than firmware restart)
                static lv_timer_t* timer = nullptr;
                if (timer) {
                    lv_timer_delete(timer);
                }
                timer = lv_timer_create(
                    [](lv_timer_t* t) {
                        spdlog::info("[MoonrakerClientMock] Klipper restart complete - READY");
                        get_printer_state().set_klippy_state_sync(KlippyState::READY);
                        lv_timer_delete(t);
                        timer = nullptr;
                    },
                    500, nullptr);
                lv_timer_set_repeat_count(timer, 1);
            },
            nullptr);

        if (success_cb) {
            success_cb(json::object());
        }
        return true;
    };
}

} // namespace mock_internal
