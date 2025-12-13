// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_error_reporting.h"
#include "ui_notification.h"

#include "moonraker_api.h"
#include "moonraker_api_internal.h"
#include "spdlog/spdlog.h"

using namespace moonraker_internal;

// ============================================================================
// Job Control Operations
// ============================================================================

void MoonrakerAPI::start_print(const std::string& filename, SuccessCallback on_success,
                               ErrorCallback on_error) {
    // Validate filename path
    if (!is_safe_path(filename)) {
        NOTIFY_ERROR("Cannot start print. File '{}' has invalid path.", filename);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid filename contains directory traversal or illegal characters";
            err.method = "start_print";
            on_error(err);
        }
        return;
    }

    json params = {{"filename", filename}};

    spdlog::info("[Moonraker API] Starting print: {}", filename);

    client_.send_jsonrpc(
        "printer.print.start", params,
        [on_success](json) {
            spdlog::info("[Moonraker API] Print started successfully");
            on_success();
        },
        on_error);
}

void MoonrakerAPI::pause_print(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Pausing print");

    client_.send_jsonrpc(
        "printer.print.pause", json::object(),
        [on_success](json) {
            spdlog::info("[Moonraker API] Print paused successfully");
            on_success();
        },
        on_error);
}

void MoonrakerAPI::resume_print(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Resuming print");

    client_.send_jsonrpc(
        "printer.print.resume", json::object(),
        [on_success](json) {
            spdlog::info("[Moonraker API] Print resumed successfully");
            on_success();
        },
        on_error);
}

void MoonrakerAPI::cancel_print(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Canceling print");

    client_.send_jsonrpc(
        "printer.print.cancel", json::object(),
        [on_success](json) {
            spdlog::info("[Moonraker API] Print canceled successfully");
            on_success();
        },
        on_error);
}

// ============================================================================
// Motion Control Operations
// ============================================================================

// ============================================================================
// Query Operations
// ============================================================================

void MoonrakerAPI::is_printer_ready(BoolCallback on_result, ErrorCallback on_error) {
    client_.send_jsonrpc(
        "printer.info", json::object(),
        [on_result](json response) {
            bool ready = false;
            if (response.contains("result") && response["result"].contains("state")) {
                std::string state = response["result"]["state"].get<std::string>();
                ready = (state == "ready");
            }
            on_result(ready);
        },
        on_error);
}

void MoonrakerAPI::get_print_state(StringCallback on_result, ErrorCallback on_error) {
    json params = {{"objects", json::object({{"print_stats", nullptr}})}};

    client_.send_jsonrpc(
        "printer.objects.query", params,
        [on_result](json response) {
            std::string state = "unknown";
            if (response.contains("result") && response["result"].contains("status") &&
                response["result"]["status"].contains("print_stats") &&
                response["result"]["status"]["print_stats"].contains("state")) {
                state = response["result"]["status"]["print_stats"]["state"].get<std::string>();
            }
            on_result(state);
        },
        on_error);
}

// ============================================================================
// HelixPrint Plugin Operations
// ============================================================================

void MoonrakerAPI::check_helix_plugin(BoolCallback on_result, ErrorCallback on_error) {
    spdlog::debug("[Moonraker API] Checking for helix_print plugin");

    // Query the plugin's status endpoint
    // Note: This uses HTTP GET, not JSON-RPC
    // The plugin registers at /server/helix/status

    // For now, we'll try a JSON-RPC call that the plugin might expose
    // If that fails, we fall back to assuming no plugin
    // Use silent=true to suppress error toasts for this internal probe request
    client_.send_jsonrpc(
        "server.helix.status", json::object(),
        [this, on_result](json response) {
            // Plugin is available
            bool enabled = true;
            if (response.contains("result") && response["result"].contains("enabled")) {
                enabled = response["result"]["enabled"].get<bool>();
            }
            helix_plugin_available_ = enabled;
            helix_plugin_checked_ = true;
            spdlog::info("[Moonraker API] helix_print plugin detected (enabled={})", enabled);
            on_result(enabled);
        },
        [this, on_result, on_error](const MoonrakerError& err) {
            // Plugin not available (404 or method not found)
            helix_plugin_available_ = false;
            helix_plugin_checked_ = true;
            spdlog::debug("[Moonraker API] helix_print plugin not available: {}", err.message);
            // Don't treat this as an error - just means plugin isn't installed
            on_result(false);
        },
        0,     // timeout_ms: use default
        true); // silent: suppress RPC_ERROR events/toasts
}

void MoonrakerAPI::start_modified_print(const std::string& original_filename,
                                        const std::string& modified_content,
                                        const std::vector<std::string>& modifications,
                                        ModifiedPrintCallback on_success, ErrorCallback on_error) {
    // Validate filename path
    if (!is_safe_path(original_filename)) {
        NOTIFY_ERROR("Cannot start modified print. File '{}' has invalid path.", original_filename);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid filename contains directory traversal or illegal characters";
            err.method = "start_modified_print";
            on_error(err);
        }
        return;
    }

    // Build modifications array
    json mods_array = json::array();
    for (const auto& mod : modifications) {
        mods_array.push_back(mod);
    }

    json params = {{"original_filename", original_filename},
                   {"modified_content", modified_content},
                   {"modifications", mods_array},
                   {"copy_metadata", true}};

    spdlog::info("[Moonraker API] Starting modified print via helix_print plugin: {}",
                 original_filename);

    client_.send_jsonrpc(
        "server.helix.print_modified", params,
        [on_success, original_filename](json response) {
            ModifiedPrintResult result;
            if (response.contains("result")) {
                const auto& r = response["result"];
                result.original_filename =
                    r.value("original_filename", original_filename);
                result.print_filename = r.value("print_filename", "");
                result.temp_filename = r.value("temp_filename", "");
                result.status = r.value("status", "unknown");
            } else {
                result.original_filename = original_filename;
                result.status = "printing";
            }
            spdlog::info("[Moonraker API] Modified print started: {} -> {}",
                         result.original_filename, result.print_filename);
            on_success(result);
        },
        on_error);
}
