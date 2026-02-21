// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_timelapse_api.h"

#include "hv/requests.h"
#include "moonraker_client.h"
#include "spdlog/spdlog.h"

#include <iomanip>
#include <sstream>
#include <thread>

using namespace helix;

// ============================================================================
// Constructor
// ============================================================================

MoonrakerTimelapseAPI::MoonrakerTimelapseAPI(MoonrakerClient& client,
                                             const std::string& http_base_url)
    : client_(client), http_base_url_(http_base_url) {}

// ============================================================================
// Timelapse Operations (Moonraker-Timelapse Plugin)
// ============================================================================

void MoonrakerTimelapseAPI::get_timelapse_settings(TimelapseSettingsCallback on_success,
                                                   ErrorCallback on_error) {
    if (http_base_url_.empty()) {
        spdlog::error("[Timelapse API] HTTP base URL not configured for timelapse");
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::CONNECTION_LOST;
            err.message = "Not connected to Moonraker";
            err.method = "get_timelapse_settings";
            on_error(err);
        }
        return;
    }

    std::string url = http_base_url_ + "/machine/timelapse/settings";
    spdlog::debug("[Timelapse API] Fetching timelapse settings from: {}", url);

    std::thread([url, on_success, on_error]() {
        auto resp = requests::get(url.c_str());

        if (!resp) {
            spdlog::error("[Timelapse API] HTTP request failed for timelapse settings");
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::CONNECTION_LOST;
                err.message = "HTTP request failed";
                err.method = "get_timelapse_settings";
                on_error(err);
            }
            return;
        }

        if (resp->status_code != 200) {
            spdlog::error("[Timelapse API] Timelapse settings request failed: HTTP {}",
                          static_cast<int>(resp->status_code));
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::UNKNOWN;
                err.code = static_cast<int>(resp->status_code);
                err.message = "HTTP " + std::to_string(static_cast<int>(resp->status_code));
                err.method = "get_timelapse_settings";
                on_error(err);
            }
            return;
        }

        // Parse JSON response
        // Note: Moonraker-Timelapse returns settings directly as a flat object,
        // not wrapped in "result.settings" like standard Moonraker responses.
        try {
            json j = json::parse(resp->body);
            TimelapseSettings settings;

            // Moonraker-Timelapse returns the config dict directly
            settings.enabled = j.value("enabled", false);
            settings.mode = j.value("mode", "layermacro");
            settings.output_framerate = j.value("output_framerate", 30);
            settings.autorender = j.value("autorender", true);
            settings.park_retract_distance = j.value("park_retract_distance", 1);
            settings.park_extrude_speed = j.value("park_extrude_speed", 15.0);
            settings.hyperlapse_cycle = j.value("hyperlapse_cycle", 30);

            spdlog::info("[Timelapse API] Timelapse settings: enabled={}, mode={}, fps={}",
                         settings.enabled, settings.mode, settings.output_framerate);
            if (on_success) {
                on_success(settings);
            }
        } catch (const std::exception& e) {
            spdlog::error("[Timelapse API] Failed to parse timelapse settings: {}", e.what());
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::UNKNOWN;
                err.message = e.what();
                err.method = "get_timelapse_settings";
                on_error(err);
            }
        }
    }).detach();
}

void MoonrakerTimelapseAPI::set_timelapse_settings(const TimelapseSettings& settings,
                                                   SuccessCallback on_success,
                                                   ErrorCallback on_error) {
    if (http_base_url_.empty()) {
        spdlog::error("[Timelapse API] HTTP base URL not configured for timelapse");
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::CONNECTION_LOST;
            err.message = "Not connected to Moonraker";
            err.method = "set_timelapse_settings";
            on_error(err);
        }
        return;
    }

    // Validate mode parameter
    if (settings.mode != "layermacro" && settings.mode != "hyperlapse") {
        spdlog::error("[Timelapse API] Invalid timelapse mode: {}", settings.mode);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid timelapse mode (must be 'layermacro' or 'hyperlapse')";
            err.method = "set_timelapse_settings";
            on_error(err);
        }
        return;
    }

    // Validate framerate (reasonable bounds: 1-120 fps)
    if (settings.output_framerate < 1 || settings.output_framerate > 120) {
        spdlog::error("[Timelapse API] Invalid timelapse framerate: {}", settings.output_framerate);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid timelapse framerate (must be 1-120)";
            err.method = "set_timelapse_settings";
            on_error(err);
        }
        return;
    }

    // Build URL with query parameters (Moonraker-Timelapse uses query string)
    std::ostringstream url;
    url << http_base_url_ << "/machine/timelapse/settings?"
        << "enabled=" << (settings.enabled ? "True" : "False") << "&mode=" << settings.mode
        << "&output_framerate=" << settings.output_framerate
        << "&autorender=" << (settings.autorender ? "True" : "False")
        << "&park_retract_distance=" << settings.park_retract_distance
        << "&park_extrude_speed=" << std::fixed << std::setprecision(1)
        << settings.park_extrude_speed << "&hyperlapse_cycle=" << settings.hyperlapse_cycle;

    std::string url_str = url.str();
    spdlog::info("[Timelapse API] Setting timelapse: enabled={}, mode={}, fps={}", settings.enabled,
                 settings.mode, settings.output_framerate);
    spdlog::debug("[Timelapse API] Timelapse URL: {}", url_str);

    std::thread([url_str, on_success, on_error]() {
        auto resp = requests::post(url_str.c_str(), "");

        if (!resp) {
            spdlog::error("[Timelapse API] HTTP request failed for timelapse settings update");
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::CONNECTION_LOST;
                err.message = "HTTP request failed";
                err.method = "set_timelapse_settings";
                on_error(err);
            }
            return;
        }

        if (resp->status_code != 200) {
            spdlog::error("[Timelapse API] Timelapse settings update failed: HTTP {}",
                          static_cast<int>(resp->status_code));
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::UNKNOWN;
                err.code = static_cast<int>(resp->status_code);
                err.message = "HTTP " + std::to_string(static_cast<int>(resp->status_code));
                err.method = "set_timelapse_settings";
                on_error(err);
            }
            return;
        }

        spdlog::info("[Timelapse API] Timelapse settings updated successfully");
        if (on_success) {
            on_success();
        }
    }).detach();
}

void MoonrakerTimelapseAPI::set_timelapse_enabled(bool enabled, SuccessCallback on_success,
                                                  ErrorCallback on_error) {
    if (http_base_url_.empty()) {
        spdlog::error("[Timelapse API] HTTP base URL not configured for timelapse");
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::CONNECTION_LOST;
            err.message = "Not connected to Moonraker";
            err.method = "set_timelapse_enabled";
            on_error(err);
        }
        return;
    }

    // Simple update with just the enabled flag
    std::string url =
        http_base_url_ + "/machine/timelapse/settings?enabled=" + (enabled ? "True" : "False");

    spdlog::info("[Timelapse API] Setting timelapse enabled={}", enabled);

    std::thread([url, enabled, on_success, on_error]() {
        auto resp = requests::post(url.c_str(), "");

        if (!resp) {
            spdlog::error("[Timelapse API] HTTP request failed for timelapse enable");
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::CONNECTION_LOST;
                err.message = "HTTP request failed";
                err.method = "set_timelapse_enabled";
                on_error(err);
            }
            return;
        }

        if (resp->status_code != 200) {
            spdlog::error("[Timelapse API] Timelapse enable failed: HTTP {}",
                          static_cast<int>(resp->status_code));
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::UNKNOWN;
                err.code = static_cast<int>(resp->status_code);
                err.message = "HTTP " + std::to_string(static_cast<int>(resp->status_code));
                err.method = "set_timelapse_enabled";
                on_error(err);
            }
            return;
        }

        spdlog::info("[Timelapse API] Timelapse {} successfully", enabled ? "enabled" : "disabled");
        if (on_success) {
            on_success();
        }
    }).detach();
}

// ============================================================================
// Timelapse Render / Frame Operations
// ============================================================================

void MoonrakerTimelapseAPI::render_timelapse(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::debug("[Timelapse API] Triggering timelapse render");
    client_.send_jsonrpc(
        "machine.timelapse.render", json::object(),
        [on_success](json /*response*/) {
            if (on_success)
                on_success();
        },
        on_error);
}

void MoonrakerTimelapseAPI::save_timelapse_frames(SuccessCallback on_success,
                                                  ErrorCallback on_error) {
    spdlog::debug("[Timelapse API] Saving timelapse frames");
    client_.send_jsonrpc(
        "machine.timelapse.saveframes", json::object(),
        [on_success](json /*response*/) {
            if (on_success)
                on_success();
        },
        on_error);
}

void MoonrakerTimelapseAPI::get_last_frame_info(
    std::function<void(const LastFrameInfo&)> on_success, ErrorCallback on_error) {
    spdlog::debug("[Timelapse API] Getting last frame info");
    client_.send_jsonrpc(
        "machine.timelapse.lastframeinfo", json::object(),
        [on_success](json response) {
            LastFrameInfo info;
            const auto& result = response.contains("result") ? response["result"] : response;
            if (result.contains("count") && result["count"].is_number()) {
                info.frame_count = result["count"].get<int>();
            }
            if (result.contains("lastframefile") && result["lastframefile"].is_string()) {
                info.last_frame_file = result["lastframefile"].get<std::string>();
            }
            if (on_success)
                on_success(info);
        },
        on_error);
}

// ============================================================================
// Webcam Operations
// ============================================================================

void MoonrakerTimelapseAPI::get_webcam_list(WebcamListCallback on_success, ErrorCallback on_error) {
    spdlog::debug("[Timelapse API] get_webcam_list()");

    client_.send_jsonrpc(
        "server.webcams.list", json::object(),
        [on_success](json response) {
            std::vector<WebcamInfo> webcams;
            if (response.contains("result") && response["result"].contains("webcams")) {
                for (const auto& cam : response["result"]["webcams"]) {
                    WebcamInfo info;
                    info.name = cam.value("name", "");
                    info.service = cam.value("service", "");
                    info.snapshot_url = cam.value("snapshot_url", "");
                    info.stream_url = cam.value("stream_url", "");
                    info.uid = cam.value("uid", "");
                    info.enabled = cam.value("enabled", true);
                    if (info.enabled) {
                        webcams.push_back(std::move(info));
                    }
                }
            }
            spdlog::debug("[Timelapse API] Found {} enabled webcam(s)", webcams.size());
            if (on_success)
                on_success(webcams);
        },
        on_error);
}
