// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_error_reporting.h"
#include "ui_notification.h"
#include "ui_utils.h"

#include "format_utils.h"
#include "hv/requests.h"
#include "moonraker_api.h"
#include "moonraker_api_internal.h"
#include "settings_manager.h"
#include "spdlog/spdlog.h"

#include <chrono>
#include <iomanip>
#include <sstream>

using namespace moonraker_internal;

namespace {

/**
 * @brief Format duration in seconds to human-readable string
 * @param seconds Duration in seconds
 * @return Formatted string like "2h 15m" or "45m" or "30s"
 */
std::string format_history_duration(double seconds) {
    return helix::fmt::duration(static_cast<int>(seconds));
}

/**
 * @brief Format Unix timestamp to human-readable date
 * @param timestamp Unix timestamp (seconds since epoch)
 * @return Formatted string like "Dec 1, 2:30 PM" (12H) or "Dec 1, 14:30" (24H)
 */
std::string format_history_date(double timestamp) {
    char buf[64];
    time_t t = static_cast<time_t>(timestamp);
    struct tm* timeinfo = localtime(&t);
    if (timeinfo) {
        // Format date part, then time part based on user preference
        TimeFormat format = SettingsManager::instance().get_time_format();
        if (format == TimeFormat::HOUR_12) {
            strftime(buf, sizeof(buf), "%b %d, %l:%M %p", timeinfo);
            // Trim double spaces from %l (space-padded hour)
            std::string result(buf);
            size_t pos;
            while ((pos = result.find("  ")) != std::string::npos) {
                result.erase(pos, 1);
            }
            return result;
        } else {
            strftime(buf, sizeof(buf), "%b %d, %H:%M", timeinfo);
        }
    } else {
        snprintf(buf, sizeof(buf), "Unknown");
    }
    return std::string(buf);
}

/**
 * @brief Format filament usage in mm to human-readable string
 * @param mm Filament length in millimeters
 * @return Formatted string like "12.5m" or "1.2km"
 */
std::string format_history_filament(double mm) {
    char buf[32];
    if (mm < 1000) {
        snprintf(buf, sizeof(buf), "%.0fmm", mm);
    } else if (mm < 1000000) {
        snprintf(buf, sizeof(buf), "%.1fm", mm / 1000.0);
    } else {
        snprintf(buf, sizeof(buf), "%.2fkm", mm / 1000000.0);
    }
    return std::string(buf);
}

/**
 * @brief Parse a single job from Moonraker history response
 */
PrintHistoryJob parse_history_job(const json& job_json) {
    PrintHistoryJob job;

    // String fields (use value() - safe for null since it returns default for missing)
    job.job_id = job_json.value("job_id", "");
    job.filename = job_json.value("filename", "");
    job.status = parse_job_status(job_json.value("status", "unknown"));

    // Numeric fields - use json_number_or() for null-safety
    // end_time is notably null for in-progress jobs
    job.start_time = json_number_or(job_json, "start_time", 0.0);
    job.end_time = json_number_or(job_json, "end_time", 0.0);
    job.print_duration = json_number_or(job_json, "print_duration", 0.0);
    job.total_duration = json_number_or(job_json, "total_duration", 0.0);
    job.filament_used = json_number_or(job_json, "filament_used", 0.0);

    // Boolean - value() is safe here since we check for existence first
    job.exists = job_json.value("exists", false);

    // Metadata (may be nested or null)
    if (job_json.contains("metadata") && job_json["metadata"].is_object()) {
        const auto& meta = job_json["metadata"];
        job.filament_type = meta.value("filament_type", "");
        job.layer_count = json_number_or(meta, "layer_count", 0u);
        job.layer_height = json_number_or(meta, "layer_height", 0.0);
        job.nozzle_temp = json_number_or(meta, "first_layer_extr_temp", 0.0);
        job.bed_temp = json_number_or(meta, "first_layer_bed_temp", 0.0);

        // Thumbnail path (first available)
        if (meta.contains("thumbnails") && meta["thumbnails"].is_array() &&
            !meta["thumbnails"].empty()) {
            job.thumbnail_path = meta["thumbnails"][0].value("relative_path", "");
        }

        // UUID and file size for precise history matching
        job.uuid = meta.value("uuid", "");
        job.size_bytes = json_number_or(meta, "size", static_cast<size_t>(0));
    }

    // Pre-format display strings
    job.duration_str = format_history_duration(job.print_duration);
    job.date_str = format_history_date(job.start_time);
    job.filament_str = format_history_filament(job.filament_used);

    return job;
}

} // anonymous namespace

void MoonrakerAPI::get_history_list(int limit, int start, double since, double before,
                                    HistoryListCallback on_success, ErrorCallback on_error) {
    json params = json::object();
    params["limit"] = limit;
    params["start"] = start;

    // Only add time filters if non-zero
    if (since > 0) {
        params["since"] = since;
    }
    if (before > 0) {
        params["before"] = before;
    }

    spdlog::debug("[Moonraker API] get_history_list(limit={}, start={}, since={}, before={})",
                  limit, start, since, before);

    client_.send_jsonrpc(
        "server.history.list", params,
        [on_success](json response) {
            std::vector<PrintHistoryJob> jobs;
            uint64_t total_count = 0;

            if (response.contains("result")) {
                const auto& result = response["result"];
                // Use null-safe access - count might be null in edge cases
                if (result.contains("count") && result["count"].is_number()) {
                    total_count = result["count"].get<uint64_t>();
                }

                if (result.contains("jobs") && result["jobs"].is_array()) {
                    for (const auto& job_json : result["jobs"]) {
                        jobs.push_back(parse_history_job(job_json));
                    }
                }
            }

            spdlog::debug("[Moonraker API] get_history_list returned {} jobs (total: {})",
                          jobs.size(), total_count);

            if (on_success) {
                on_success(jobs, total_count);
            }
        },
        on_error);
}

void MoonrakerAPI::get_history_totals(HistoryTotalsCallback on_success, ErrorCallback on_error) {
    spdlog::debug("[Moonraker API] get_history_totals()");

    client_.send_jsonrpc(
        "server.history.totals", json::object(),
        [on_success](json response) {
            PrintHistoryTotals totals;

            if (response.contains("result") && response["result"].contains("job_totals") &&
                response["result"]["job_totals"].is_object()) {
                const auto& jt = response["result"]["job_totals"];
                // Null-safe numeric access for all fields
                if (jt.contains("total_jobs") && jt["total_jobs"].is_number()) {
                    totals.total_jobs = jt["total_jobs"].get<uint64_t>();
                }
                if (jt.contains("total_time") && jt["total_time"].is_number()) {
                    totals.total_time = static_cast<uint64_t>(jt["total_time"].get<double>());
                }
                if (jt.contains("total_filament_used") && jt["total_filament_used"].is_number()) {
                    totals.total_filament_used = jt["total_filament_used"].get<double>();
                }
                if (jt.contains("longest_job") && jt["longest_job"].is_number()) {
                    totals.longest_job = jt["longest_job"].get<double>();
                }
                // Note: Moonraker doesn't provide breakdown counts (completed/cancelled/failed)
                // These must be calculated client-side from the job list if needed
            }

            spdlog::debug("[Moonraker API] get_history_totals: {} jobs, {}s total time",
                          totals.total_jobs, totals.total_time);

            if (on_success) {
                on_success(totals);
            }
        },
        on_error);
}

void MoonrakerAPI::delete_history_job(const std::string& job_id, SuccessCallback on_success,
                                      ErrorCallback on_error) {
    json params = json::object();
    params["uid"] = job_id;

    spdlog::debug("[Moonraker API] delete_history_job(uid={})", job_id);

    client_.send_jsonrpc(
        "server.history.delete_job", params,
        [on_success, job_id](json /*response*/) {
            spdlog::info("[Moonraker API] Deleted history job: {}", job_id);
            if (on_success) {
                on_success();
            }
        },
        on_error);
}

// ============================================================================
// Timelapse Operations (Moonraker-Timelapse Plugin)
// ============================================================================

void MoonrakerAPI::get_timelapse_settings(TimelapseSettingsCallback on_success,
                                          ErrorCallback on_error) {
    if (http_base_url_.empty()) {
        spdlog::error("[Moonraker API] HTTP base URL not configured for timelapse");
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
    spdlog::debug("[Moonraker API] Fetching timelapse settings from: {}", url);

    launch_http_thread([url, on_success, on_error]() {
        auto resp = requests::get(url.c_str());

        if (!resp) {
            spdlog::error("[Moonraker API] HTTP request failed for timelapse settings");
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
            spdlog::error("[Moonraker API] Timelapse settings request failed: HTTP {}",
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

            spdlog::info("[Moonraker API] Timelapse settings: enabled={}, mode={}, fps={}",
                         settings.enabled, settings.mode, settings.output_framerate);
            if (on_success) {
                on_success(settings);
            }
        } catch (const std::exception& e) {
            spdlog::error("[Moonraker API] Failed to parse timelapse settings: {}", e.what());
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::UNKNOWN;
                err.message = e.what();
                err.method = "get_timelapse_settings";
                on_error(err);
            }
        }
    });
}

void MoonrakerAPI::set_timelapse_settings(const TimelapseSettings& settings,
                                          SuccessCallback on_success, ErrorCallback on_error) {
    if (http_base_url_.empty()) {
        spdlog::error("[Moonraker API] HTTP base URL not configured for timelapse");
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
        spdlog::error("[Moonraker API] Invalid timelapse mode: {}", settings.mode);
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
        spdlog::error("[Moonraker API] Invalid timelapse framerate: {}", settings.output_framerate);
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
    spdlog::info("[Moonraker API] Setting timelapse: enabled={}, mode={}, fps={}", settings.enabled,
                 settings.mode, settings.output_framerate);
    spdlog::debug("[Moonraker API] Timelapse URL: {}", url_str);

    launch_http_thread([url_str, on_success, on_error]() {
        auto resp = requests::post(url_str.c_str(), "");

        if (!resp) {
            spdlog::error("[Moonraker API] HTTP request failed for timelapse settings update");
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
            spdlog::error("[Moonraker API] Timelapse settings update failed: HTTP {}",
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

        spdlog::info("[Moonraker API] Timelapse settings updated successfully");
        if (on_success) {
            on_success();
        }
    });
}

void MoonrakerAPI::set_timelapse_enabled(bool enabled, SuccessCallback on_success,
                                         ErrorCallback on_error) {
    if (http_base_url_.empty()) {
        spdlog::error("[Moonraker API] HTTP base URL not configured for timelapse");
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

    spdlog::info("[Moonraker API] Setting timelapse enabled={}", enabled);

    launch_http_thread([url, enabled, on_success, on_error]() {
        auto resp = requests::post(url.c_str(), "");

        if (!resp) {
            spdlog::error("[Moonraker API] HTTP request failed for timelapse enable");
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
            spdlog::error("[Moonraker API] Timelapse enable failed: HTTP {}",
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

        spdlog::info("[Moonraker API] Timelapse {} successfully", enabled ? "enabled" : "disabled");
        if (on_success) {
            on_success();
        }
    });
}

// ============================================================================
// Timelapse Render / Frame Operations
// ============================================================================

void MoonrakerAPI::render_timelapse(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::debug("[MoonrakerAPI] Triggering timelapse render");
    client_.send_jsonrpc(
        "machine.timelapse.render", json::object(),
        [on_success](json /*response*/) {
            if (on_success)
                on_success();
        },
        on_error);
}

void MoonrakerAPI::save_timelapse_frames(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::debug("[MoonrakerAPI] Saving timelapse frames");
    client_.send_jsonrpc(
        "machine.timelapse.saveframes", json::object(),
        [on_success](json /*response*/) {
            if (on_success)
                on_success();
        },
        on_error);
}

void MoonrakerAPI::get_last_frame_info(std::function<void(const LastFrameInfo&)> on_success,
                                       ErrorCallback on_error) {
    spdlog::debug("[MoonrakerAPI] Getting last frame info");
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

void MoonrakerAPI::get_webcam_list(WebcamListCallback on_success, ErrorCallback on_error) {
    spdlog::debug("[Moonraker API] get_webcam_list()");

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
            spdlog::debug("[Moonraker API] Found {} enabled webcam(s)", webcams.size());
            if (on_success)
                on_success(webcams);
        },
        on_error);
}
