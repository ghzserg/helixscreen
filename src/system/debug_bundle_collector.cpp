// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/debug_bundle_collector.h"

#include "ui_update_queue.h"

#include "app_globals.h"
#include "helix_version.h"
#include "hv/requests.h"
#include "platform_capabilities.h"
#include "printer_state.h"
#include "system/update_checker.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <fstream>
#include <sstream>
#include <thread>
#include <zlib.h>

using json = nlohmann::json;
namespace helix {

// =============================================================================
// Main collect
// =============================================================================

json DebugBundleCollector::collect(const BundleOptions& options) {
    json bundle;

    bundle["version"] = HELIX_VERSION;

    // ISO 8601 timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    char time_buf[64];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&time_t_now));
    bundle["timestamp"] = time_buf;

    bundle["system"] = collect_system_info();
    bundle["printer"] = collect_printer_info();

    auto log_tail = collect_log_tail();
    if (!log_tail.empty()) {
        bundle["log_tail"] = log_tail;
    }

    auto crash_txt = collect_crash_txt();
    if (!crash_txt.empty()) {
        bundle["crash_txt"] = crash_txt;
    }

    bundle["settings"] = collect_sanitized_settings();

    if (options.include_klipper_logs) {
        auto klipper_log = collect_klipper_log_tail();
        if (!klipper_log.empty()) {
            bundle["klipper_log"] = klipper_log;
        }
    }

    if (options.include_moonraker_logs) {
        auto moonraker_log = collect_moonraker_log_tail();
        if (!moonraker_log.empty()) {
            bundle["moonraker_log"] = moonraker_log;
        }
    }

    return bundle;
}

// =============================================================================
// System info
// =============================================================================

json DebugBundleCollector::collect_system_info() {
    json sys;

    sys["platform"] = UpdateChecker::get_platform_key();

    auto caps = PlatformCapabilities::detect();
    sys["total_ram_mb"] = caps.total_ram_mb;
    sys["cpu_cores"] = caps.cpu_cores;

    // Read uptime from /proc/uptime if available
    std::ifstream uptime_file("/proc/uptime");
    if (uptime_file.good()) {
        double uptime_sec = 0.0;
        uptime_file >> uptime_sec;
        sys["uptime_seconds"] = static_cast<int>(uptime_sec);
    }

    return sys;
}

// =============================================================================
// Printer info
// =============================================================================

json DebugBundleCollector::collect_printer_info() {
    json printer;

    try {
        auto& ps = get_printer_state();

        printer["model"] = ps.get_printer_type();

        // Get klipper version from the string subject
        auto* kv_subj = ps.get_klipper_version_subject();
        if (kv_subj) {
            const char* kv = lv_subject_get_string(kv_subj);
            if (kv && kv[0] != '\0') {
                printer["klipper_version"] = kv;
            }
        }

        // Connection state
        auto* conn_subj = ps.get_printer_connection_state_subject();
        if (conn_subj) {
            int state = lv_subject_get_int(conn_subj);
            const char* state_names[] = {"disconnected", "connecting", "connected", "reconnecting",
                                         "failed"};
            if (state >= 0 && state < 5) {
                printer["connection_state"] = state_names[state];
            }
        }

        // Klippy state
        auto* klippy_subj = ps.get_klippy_state_subject();
        if (klippy_subj) {
            int kstate = lv_subject_get_int(klippy_subj);
            const char* klippy_names[] = {"ready", "startup", "shutdown", "error"};
            if (kstate >= 0 && kstate < 4) {
                printer["klippy_state"] = klippy_names[kstate];
            }
        }
    } catch (const std::exception& e) {
        spdlog::debug("[DebugBundle] Failed to collect printer info: {}", e.what());
        printer["error"] = e.what();
    }

    return printer;
}

// =============================================================================
// Log tail (deque-based, same pattern as CrashReporter)
// =============================================================================

std::string DebugBundleCollector::collect_log_tail(int num_lines) {
    std::vector<std::string> log_paths = {
        "/var/log/helix-screen.log",
    };

    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && xdg[0] != '\0') {
        log_paths.push_back(std::string(xdg) + "/helix-screen/helix-screen.log");
    }
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        log_paths.push_back(std::string(home) + "/.local/share/helix-screen/helix-screen.log");
    }

    for (const auto& path : log_paths) {
        std::ifstream file(path);
        if (!file.good()) {
            continue;
        }

        std::deque<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(std::move(line));
            if (static_cast<int>(lines.size()) > num_lines) {
                lines.pop_front();
            }
        }

        if (lines.empty()) {
            return {};
        }

        std::ostringstream result;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i > 0) {
                result << '\n';
            }
            result << lines[i];
        }

        spdlog::debug("[DebugBundle] Read {} log lines from {}", lines.size(), path);
        return result.str();
    }

    spdlog::debug("[DebugBundle] No log file found for log tail");
    return {};
}

// =============================================================================
// Crash file
// =============================================================================

std::string DebugBundleCollector::collect_crash_txt() {
    // Try common config locations for crash.txt
    std::vector<std::string> crash_paths = {
        "config/crash.txt",
    };

    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        crash_paths.push_back(std::string(home) + "/helixscreen/config/crash.txt");
    }

    for (const auto& path : crash_paths) {
        std::ifstream file(path);
        if (!file.good()) {
            continue;
        }

        std::ostringstream content;
        content << file.rdbuf();
        std::string result = content.str();

        if (!result.empty()) {
            spdlog::debug("[DebugBundle] Read crash.txt from {}", path);
            return result;
        }
    }

    return {};
}

// =============================================================================
// Sanitized settings
// =============================================================================

bool DebugBundleCollector::is_sensitive_key(const std::string& key) {
    // Case-insensitive substring match for sensitive patterns
    std::string lower_key = key;
    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    static const std::vector<std::string> sensitive_patterns = {"token", "password", "secret",
                                                                "key"};

    for (const auto& pattern : sensitive_patterns) {
        if (lower_key.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

json DebugBundleCollector::sanitize_json(const json& input) {
    if (input.is_object()) {
        json result = json::object();
        for (auto it = input.begin(); it != input.end(); ++it) {
            if (is_sensitive_key(it.key())) {
                result[it.key()] = "[REDACTED]";
            } else {
                result[it.key()] = sanitize_json(it.value());
            }
        }
        return result;
    }

    if (input.is_array()) {
        json result = json::array();
        for (const auto& element : input) {
            result.push_back(sanitize_json(element));
        }
        return result;
    }

    // Primitives pass through unchanged
    return input;
}

json DebugBundleCollector::collect_sanitized_settings() {
    // Try common config locations for settings.json
    std::vector<std::string> settings_paths = {
        "config/helixconfig.json",
    };

    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        settings_paths.push_back(std::string(home) + "/helixscreen/config/helixconfig.json");
    }

    for (const auto& path : settings_paths) {
        std::ifstream file(path);
        if (!file.good()) {
            continue;
        }

        try {
            json settings = json::parse(file);
            spdlog::debug("[DebugBundle] Read settings from {}", path);
            return sanitize_json(settings);
        } catch (const json::parse_error& e) {
            spdlog::debug("[DebugBundle] Failed to parse settings from {}: {}", path, e.what());
        }
    }

    return json::object();
}

// =============================================================================
// Klipper / Moonraker log tails (stub - Moonraker HTTP fetch TBD)
// =============================================================================

std::string DebugBundleCollector::collect_klipper_log_tail(int /*num_lines*/) {
    // TODO: Fetch from Moonraker HTTP API: GET /server/files/klippy.log
    return {};
}

std::string DebugBundleCollector::collect_moonraker_log_tail(int /*num_lines*/) {
    // TODO: Fetch from Moonraker HTTP API: GET /server/files/moonraker.log
    return {};
}

// =============================================================================
// Gzip compression
// =============================================================================

std::vector<uint8_t> DebugBundleCollector::gzip_compress(const std::string& data) {
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        spdlog::error("[DebugBundle] deflateInit2 failed");
        return {};
    }

    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());

    std::vector<uint8_t> output;
    output.resize(deflateBound(&zs, zs.avail_in));

    zs.next_out = output.data();
    zs.avail_out = static_cast<uInt>(output.size());

    int ret = deflate(&zs, Z_FINISH);
    if (ret != Z_STREAM_END) {
        spdlog::error("[DebugBundle] deflate failed with code: {}", ret);
        deflateEnd(&zs);
        return {};
    }

    output.resize(zs.total_out);
    deflateEnd(&zs);
    return output;
}

// =============================================================================
// Async upload
// =============================================================================

void DebugBundleCollector::upload_async(const BundleOptions& options, ResultCallback callback) {
    // Detached thread for non-blocking upload
    std::thread([options, callback = std::move(callback)]() {
        BundleResult result;

        try {
            spdlog::info("[DebugBundle] Collecting debug bundle...");
            json bundle = collect(options);
            std::string json_str = bundle.dump();

            spdlog::info("[DebugBundle] Compressing {} bytes...", json_str.size());
            auto compressed = gzip_compress(json_str);

            if (compressed.empty()) {
                result.error_message = "Compression failed";
                helix::ui::queue_update([callback, result]() { callback(result); });
                return;
            }

            spdlog::info("[DebugBundle] Uploading {} bytes (compressed from {})...",
                         compressed.size(), json_str.size());

            auto req = std::make_shared<HttpRequest>();
            req->method = HTTP_POST;
            req->url = WORKER_URL;
            req->timeout = 30;
            req->headers["Content-Type"] = "application/json";
            req->headers["Content-Encoding"] = "gzip";
            req->headers["User-Agent"] = std::string("HelixScreen/") + HELIX_VERSION;
            req->headers["X-API-Key"] = INGEST_API_KEY;
            req->body.assign(reinterpret_cast<const char*>(compressed.data()), compressed.size());

            auto resp = requests::request(req);
            int status = resp ? static_cast<int>(resp->status_code) : 0;

            if (resp && status >= 200 && status < 300) {
                // Parse share_code from response
                try {
                    json resp_json = json::parse(resp->body);
                    if (resp_json.contains("share_code")) {
                        result.share_code = resp_json["share_code"].get<std::string>();
                    }
                } catch (const json::parse_error&) {
                    // Response might not be JSON, but upload succeeded
                }
                result.success = true;
                spdlog::info("[DebugBundle] Upload successful (HTTP {}), share_code: {}", status,
                             result.share_code);
            } else {
                result.error_message = "HTTP " + std::to_string(status) +
                                       (resp ? ": " + resp->body.substr(0, 200) : ": no response");
                spdlog::warn("[DebugBundle] Upload failed: {}", result.error_message);
            }
        } catch (const std::exception& e) {
            result.error_message = std::string("Exception: ") + e.what();
            spdlog::error("[DebugBundle] Upload exception: {}", e.what());
        }

        helix::ui::queue_update([callback, result]() { callback(result); });
    }).detach();
}

} // namespace helix
