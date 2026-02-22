// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/debug_bundle_collector.h"

#include "ui_update_queue.h"

#include "app_globals.h"
#include "helix_version.h"
#include "hv/requests.h"
#include "moonraker_api.h"
#include "platform_capabilities.h"
#include "printer_state.h"
#include "system/crash_history.h"
#include "system/telemetry_manager.h"
#include "system/update_checker.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <regex>
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

    try {
        bundle["system"] = collect_system_info();
    } catch (const std::exception& e) {
        spdlog::warn("[DebugBundle] Failed to collect system info: {}", e.what());
        bundle["system"] = json{{"error", e.what()}};
    }

    try {
        bundle["printer"] = collect_printer_info();
    } catch (const std::exception& e) {
        spdlog::warn("[DebugBundle] Failed to collect printer info: {}", e.what());
        bundle["printer"] = json{{"error", e.what()}};
    }

    try {
        auto log_tail = collect_log_tail();
        if (!log_tail.empty()) {
            bundle["log_tail"] = log_tail;
        }
    } catch (const std::exception& e) {
        spdlog::warn("[DebugBundle] Failed to collect log tail: {}", e.what());
    }

    try {
        auto crash_txt = collect_crash_txt();
        if (!crash_txt.empty()) {
            bundle["crash_txt"] = crash_txt;
        }
    } catch (const std::exception& e) {
        spdlog::warn("[DebugBundle] Failed to collect crash.txt: {}", e.what());
    }

    // Crash data: report text, history, and device ID for R2 cross-referencing
    try {
        std::string config_dir = "config";
        const char* home = std::getenv("HOME");
        if (home && home[0] != '\0') {
            std::string home_config = std::string(home) + "/helixscreen/config";
            if (std::filesystem::exists(home_config)) {
                config_dir = home_config;
            }
        }

        auto crash_report = collect_crash_report_txt(config_dir);
        if (!crash_report.empty()) {
            bundle["crash_report"] = crash_report;
        }

        auto crash_history = CrashHistory::instance().to_json();
        if (!crash_history.empty()) {
            bundle["crash_history"] = crash_history;
        }

        auto device_id = collect_device_id(config_dir);
        if (!device_id.empty()) {
            bundle["device_id"] = device_id;
        }
    } catch (const std::exception& e) {
        spdlog::warn("[DebugBundle] Failed to collect crash data: {}", e.what());
    }

    try {
        bundle["settings"] = collect_sanitized_settings();
    } catch (const std::exception& e) {
        spdlog::warn("[DebugBundle] Failed to collect settings: {}", e.what());
        bundle["settings"] = json{{"error", e.what()}};
    }

    try {
        bundle["moonraker"] = collect_moonraker_info();
    } catch (const std::exception& e) {
        spdlog::warn("[DebugBundle] Failed to collect moonraker info: {}", e.what());
        bundle["moonraker"] = json{{"error", e.what()}};
    }

    if (options.include_klipper_logs) {
        try {
            auto klipper_log = collect_klipper_log_tail();
            if (!klipper_log.empty()) {
                bundle["klipper_log"] = klipper_log;
            }
        } catch (const std::exception& e) {
            spdlog::warn("[DebugBundle] Failed to collect klipper log: {}", e.what());
        }
    }

    if (options.include_moonraker_logs) {
        try {
            auto moonraker_log = collect_moonraker_log_tail();
            if (!moonraker_log.empty()) {
                bundle["moonraker_log"] = moonraker_log;
            }
        } catch (const std::exception& e) {
            spdlog::warn("[DebugBundle] Failed to collect moonraker log: {}", e.what());
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
    // Build search paths matching logging_init.cpp resolution order:
    // 1. /var/log/helix-screen.log (legacy)
    // 2. XDG_DATA_HOME/helix-screen/helix.log
    // 3. ~/.local/share/helix-screen/helix.log
    // 4. /tmp/helixscreen.log (settings default)
    std::vector<std::string> log_paths = {
        "/var/log/helix-screen.log",
    };

    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && xdg[0] != '\0') {
        log_paths.push_back(std::string(xdg) + "/helix-screen/helix.log");
    }
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        log_paths.push_back(std::string(home) + "/.local/share/helix-screen/helix.log");
    }
    log_paths.push_back("/tmp/helixscreen.log");

    return collect_log_tail_from_paths(log_paths, num_lines);
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

    static const std::vector<std::string> sensitive_patterns = {
        "token", "password", "secret", "key", "webhook", "credential", "auth", "bearer"};

    for (const auto& pattern : sensitive_patterns) {
        if (lower_key.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string DebugBundleCollector::sanitize_value(const std::string& value) {
    if (value.empty())
        return value;

    // Skip regex on very long strings to prevent ReDoS / performance issues
    if (value.size() > 4096) {
        return "[REDACTED_LONG_VALUE]";
    }

    // Check webhook URLs first (full replacement)
    if (value.find("discord.com/api/webhooks") != std::string::npos ||
        value.find("hooks.slack.com") != std::string::npos ||
        value.find("api.telegram.org/bot") != std::string::npos ||
        value.find("api.pushover.net") != std::string::npos ||
        value.find("ntfy.sh/") != std::string::npos ||
        value.find("maker.ifttt.com") != std::string::npos) {
        return "[REDACTED_WEBHOOK]";
    }

    try {
        // Check for long token-like strings (40+ chars of hex/base64/alphanum with prefix)
        static const std::regex token_re(
            R"(^(?:ghp_|gho_|glpat-|xoxb-|xoxp-)?[A-Za-z0-9+/=_-]{36,}$)");
        if (std::regex_match(value, token_re)) {
            return "[REDACTED_TOKEN]";
        }

        std::string result = value;

        // Redact URL credentials: ://user:pass@ -> ://[REDACTED_CREDENTIALS]@
        static const std::regex cred_url_re(R"(://[^@/\s]+:[^@/\s]+@)");
        result = std::regex_replace(result, cred_url_re, "://[REDACTED_CREDENTIALS]@");

        // Redact email addresses
        static const std::regex email_re(R"(\b[\w.+-]+@[\w-]+\.[\w.]+\b)");
        result = std::regex_replace(result, email_re, "[REDACTED_EMAIL]");

        // Redact MAC addresses (aa:bb:cc:dd:ee:ff or AA-BB-CC-DD-EE-FF)
        static const std::regex mac_re(R"(\b([0-9a-fA-F]{2}[:-]){5}[0-9a-fA-F]{2}\b)");
        result = std::regex_replace(result, mac_re, "[REDACTED_MAC]");

        return result;
    } catch (const std::exception& e) {
        spdlog::debug("[DebugBundle] sanitize_value regex failed: {}", e.what());
        return value; // Return unsanitized rather than crash
    }
}

json DebugBundleCollector::sanitize_json(const json& input, int depth) {
    if (depth > 32) {
        spdlog::debug("[DebugBundle] sanitize_json hit depth limit, passing through");
        return input;
    }

    if (input.is_object()) {
        json result = json::object();
        for (auto it = input.begin(); it != input.end(); ++it) {
            if (is_sensitive_key(it.key())) {
                result[it.key()] = "[REDACTED]";
            } else {
                result[it.key()] = sanitize_json(it.value(), depth + 1);
            }
        }
        return result;
    }

    if (input.is_array()) {
        json result = json::array();
        for (const auto& element : input) {
            result.push_back(sanitize_json(element, depth + 1));
        }
        return result;
    }

    // Sanitize string values for PII patterns
    if (input.is_string()) {
        return sanitize_value(input.get<std::string>());
    }

    // Non-string primitives pass through unchanged
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
// Moonraker REST collection
// =============================================================================

std::string DebugBundleCollector::get_moonraker_url() {
    auto* api = get_moonraker_api();
    if (!api)
        return {};
    return api->get_http_base_url();
}

json DebugBundleCollector::moonraker_get(const std::string& base_url, const std::string& endpoint,
                                         int timeout_sec) {
    if (base_url.empty()) {
        return json{{"error", "Moonraker not connected"}};
    }

    std::string url = base_url;
    if (!endpoint.empty() && endpoint[0] != '/')
        url += "/";
    url += endpoint;

    try {
        auto req = std::make_shared<HttpRequest>();
        req->method = HTTP_GET;
        req->url = url;
        req->timeout = timeout_sec;

        auto resp = requests::request(req);
        if (!resp) {
            return json{{"error", "No response from " + endpoint}};
        }

        int status = static_cast<int>(resp->status_code);
        if (status < 200 || status >= 300) {
            return json{{"error", "HTTP " + std::to_string(status) + " from " + endpoint}};
        }

        return json::parse(resp->body);
    } catch (const json::parse_error& e) {
        return json{{"error", "JSON parse error from " + endpoint + ": " + e.what()}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("Exception: ") + e.what()}};
    }
}

json DebugBundleCollector::collect_moonraker_info() {
    json mr;
    std::string base_url = get_moonraker_url();

    if (base_url.empty()) {
        spdlog::debug("[DebugBundle] Moonraker not connected, skipping moonraker info");
        mr["server_info"] = json{{"error", "Not connected"}};
        mr["printer_info"] = json{{"error", "Not connected"}};
        mr["system_info"] = json{{"error", "Not connected"}};
        mr["printer_state"] = json{{"error", "Not connected"}};
        mr["config"] = json{{"error", "Not connected"}};
        return mr;
    }

    spdlog::info("[DebugBundle] Collecting Moonraker info from {}", base_url);

    // Server info — version, components, klippy state
    try {
        mr["server_info"] = sanitize_json(moonraker_get(base_url, "/server/info"));
    } catch (const std::exception& e) {
        spdlog::debug("[DebugBundle] server_info collection failed: {}", e.what());
        mr["server_info"] = json{{"error", e.what()}};
    }

    // Printer info — hostname, klipper version, state
    try {
        mr["printer_info"] = sanitize_json(moonraker_get(base_url, "/printer/info"));
    } catch (const std::exception& e) {
        spdlog::debug("[DebugBundle] printer_info collection failed: {}", e.what());
        mr["printer_info"] = json{{"error", e.what()}};
    }

    // System info — OS, CPU, memory, network (heavy sanitization for MACs/IPs)
    try {
        mr["system_info"] = sanitize_json(moonraker_get(base_url, "/machine/system_info"));
    } catch (const std::exception& e) {
        spdlog::debug("[DebugBundle] system_info collection failed: {}", e.what());
        mr["system_info"] = json{{"error", e.what()}};
    }

    // Current printer state — temps, positions, fans, print progress
    try {
        mr["printer_state"] = sanitize_json(
            moonraker_get(base_url, "/printer/objects/query"
                                    "?heater_bed&extruder&print_stats&toolhead&motion_report"
                                    "&fan&display_status&virtual_sdcard"));
    } catch (const std::exception& e) {
        spdlog::debug("[DebugBundle] printer_state collection failed: {}", e.what());
        mr["printer_state"] = json{{"error", e.what()}};
    }

    // Full Moonraker config — heavily sanitized
    try {
        mr["config"] = sanitize_json(moonraker_get(base_url, "/server/config"));
    } catch (const std::exception& e) {
        spdlog::debug("[DebugBundle] config collection failed: {}", e.what());
        mr["config"] = json{{"error", e.what()}};
    }

    return mr;
}

// =============================================================================
// Klipper / Moonraker log tails (via HTTP Range for memory safety)
// =============================================================================

std::string DebugBundleCollector::fetch_log_tail(const std::string& base_url,
                                                 const std::string& endpoint, int num_lines,
                                                 int tail_bytes) {
    try {
        auto req = std::make_shared<HttpRequest>();
        req->method = HTTP_GET;
        req->url = base_url + endpoint;
        req->timeout = 15;

        // Request only the last chunk using HTTP Range
        // "bytes=-N" means "last N bytes of the file"
        req->headers["Range"] = "bytes=-" + std::to_string(tail_bytes);

        auto resp = requests::request(req);
        if (!resp) {
            spdlog::debug("[DebugBundle] No response fetching {}", endpoint);
            return {};
        }

        int status = static_cast<int>(resp->status_code);

        // 206 = partial content (range honored), 200 = full file (range not supported)
        if (status == 200) {
            // Server returned the full file -- check size before processing
            if (resp->body.size() > 5 * 1024 * 1024) {
                spdlog::warn("[DebugBundle] {} is too large ({} bytes), skipping", endpoint,
                             resp->body.size());
                return {};
            }
        } else if (status == 416) {
            // Range not satisfiable (file smaller than requested range) — retry without Range
            spdlog::debug("[DebugBundle] 416 for {}, retrying without Range header", endpoint);
            auto retry_req = std::make_shared<HttpRequest>();
            retry_req->method = HTTP_GET;
            retry_req->url = base_url + endpoint;
            retry_req->timeout = 15;
            resp = requests::request(retry_req);
            if (!resp)
                return {};
            status = static_cast<int>(resp->status_code);
            if (status < 200 || status >= 300)
                return {};
            // Small file — no size concern, fall through to line parsing
        } else if (status != 206) {
            spdlog::debug("[DebugBundle] HTTP {} fetching {}", status, endpoint);
            return {};
        }

        // Take last N lines from the response
        std::istringstream stream(resp->body);
        std::deque<std::string> lines;
        std::string line;

        // If we got a partial response (206), the first line is likely truncated -- skip it
        bool skip_first = (status == 206);

        while (std::getline(stream, line)) {
            if (skip_first) {
                skip_first = false;
                continue;
            }
            lines.push_back(std::move(line));
            if (static_cast<int>(lines.size()) > num_lines) {
                lines.pop_front();
            }
        }

        std::ostringstream result;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i > 0)
                result << '\n';
            result << sanitize_value(lines[i]);
        }

        spdlog::debug("[DebugBundle] Fetched {} lines from {} (HTTP {})", lines.size(), endpoint,
                      status);
        return result.str();
    } catch (const std::exception& e) {
        spdlog::debug("[DebugBundle] Exception fetching {}: {}", endpoint, e.what());
        return {};
    }
}

std::string DebugBundleCollector::collect_klipper_log_tail(int num_lines) {
    std::string base_url = get_moonraker_url();
    if (base_url.empty())
        return {};
    return fetch_log_tail(base_url, "/server/files/klippy.log", num_lines);
}

std::string DebugBundleCollector::collect_moonraker_log_tail(int num_lines) {
    std::string base_url = get_moonraker_url();
    if (base_url.empty())
        return {};
    return fetch_log_tail(base_url, "/server/files/moonraker.log", num_lines);
}

// =============================================================================
// Crash report (human-readable, persists after crash.txt consumed)
// =============================================================================

std::string DebugBundleCollector::collect_crash_report_txt(const std::string& config_dir) {
    std::string path = config_dir + "/crash_report.txt";
    try {
        std::ifstream file(path);
        if (!file.good()) {
            return {};
        }

        std::ostringstream content;
        content << file.rdbuf();
        std::string result = content.str();

        if (!result.empty()) {
            spdlog::debug("[DebugBundle] Read crash_report.txt from {}", path);
        }
        return result;
    } catch (const std::exception& e) {
        spdlog::debug("[DebugBundle] Failed to read crash_report.txt: {}", e.what());
        return {};
    }
}

// =============================================================================
// Crash history (past crash submissions from crash_history.json)
// =============================================================================

json DebugBundleCollector::collect_crash_history(const std::string& config_dir) {
    std::string path = config_dir + "/crash_history.json";
    try {
        std::ifstream file(path);
        if (!file.good()) {
            return json::array();
        }

        json arr = json::parse(file);
        if (!arr.is_array()) {
            spdlog::warn("[DebugBundle] crash_history.json is not an array");
            return json::array();
        }

        spdlog::debug("[DebugBundle] Read {} crash history entries from {}", arr.size(), path);
        return arr;
    } catch (const json::parse_error& e) {
        spdlog::warn("[DebugBundle] Failed to parse crash_history.json: {}", e.what());
        return json::array();
    } catch (const std::exception& e) {
        spdlog::debug("[DebugBundle] Failed to read crash_history.json: {}", e.what());
        return json::array();
    }
}

// =============================================================================
// Device ID (double-hashed for R2 cross-referencing)
// =============================================================================

std::string DebugBundleCollector::collect_device_id(const std::string& config_dir) {
    std::string path = config_dir + "/telemetry_device.json";
    try {
        std::ifstream file(path);
        if (!file.good()) {
            return {};
        }

        json data = json::parse(file);
        if (!data.contains("uuid") || !data["uuid"].is_string() || !data.contains("salt") ||
            !data["salt"].is_string()) {
            spdlog::debug("[DebugBundle] telemetry_device.json missing uuid/salt");
            return {};
        }

        std::string uuid = data["uuid"].get<std::string>();
        std::string salt = data["salt"].get<std::string>();

        // Use the same double-hash as TelemetryManager for consistency
        return TelemetryManager::hash_device_id(uuid, salt);
    } catch (const std::exception& e) {
        spdlog::debug("[DebugBundle] Failed to read device ID: {}", e.what());
        return {};
    }
}

// =============================================================================
// Log tail from explicit paths (testable, used by collect_log_tail)
// =============================================================================

std::string DebugBundleCollector::collect_log_tail_from_paths(const std::vector<std::string>& paths,
                                                              int num_lines) {
    for (const auto& path : paths) {
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
            continue;
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
