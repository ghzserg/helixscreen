// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_error_reporting.h"
#include "ui_notification.h"

#include "json_utils.h"
#include "moonraker_api.h"
#include "moonraker_api_internal.h"
#include "shaper_csv_parser.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <memory>
#include <regex>
#include <set>
#include <sstream>

using namespace moonraker_internal;

// ============================================================================
// Domain Service Operations - Bed Mesh
// ============================================================================

void MoonrakerAPI::update_bed_mesh(const json& bed_mesh) {
    std::lock_guard<std::mutex> lock(bed_mesh_mutex_);

    spdlog::debug("[MoonrakerAPI] update_bed_mesh called with keys: {}", [&]() {
        std::string keys;
        for (auto it = bed_mesh.begin(); it != bed_mesh.end(); ++it) {
            if (!keys.empty())
                keys += ", ";
            keys += it.key();
        }
        return keys;
    }());

    // Parse active profile name
    if (bed_mesh.contains("profile_name") && !bed_mesh["profile_name"].is_null()) {
        active_bed_mesh_.name = bed_mesh["profile_name"].template get<std::string>();
    }

    // Parse probed_matrix (2D array of Z heights)
    if (bed_mesh.contains("probed_matrix") && bed_mesh["probed_matrix"].is_array()) {
        active_bed_mesh_.probed_matrix.clear();
        for (const auto& row : bed_mesh["probed_matrix"]) {
            if (row.is_array()) {
                std::vector<float> row_vec;
                for (const auto& val : row) {
                    if (val.is_number()) {
                        row_vec.push_back(val.template get<float>());
                    }
                }
                if (!row_vec.empty()) {
                    active_bed_mesh_.probed_matrix.push_back(row_vec);
                }
            }
        }

        // Update dimensions
        active_bed_mesh_.y_count = static_cast<int>(active_bed_mesh_.probed_matrix.size());
        active_bed_mesh_.x_count = active_bed_mesh_.probed_matrix.empty()
                                       ? 0
                                       : static_cast<int>(active_bed_mesh_.probed_matrix[0].size());
    }

    // Parse mesh bounds (check that elements are numbers, not null)
    if (bed_mesh.contains("mesh_min") && bed_mesh["mesh_min"].is_array() &&
        bed_mesh["mesh_min"].size() >= 2 && bed_mesh["mesh_min"][0].is_number() &&
        bed_mesh["mesh_min"][1].is_number()) {
        active_bed_mesh_.mesh_min[0] = bed_mesh["mesh_min"][0].template get<float>();
        active_bed_mesh_.mesh_min[1] = bed_mesh["mesh_min"][1].template get<float>();
    }

    if (bed_mesh.contains("mesh_max") && bed_mesh["mesh_max"].is_array() &&
        bed_mesh["mesh_max"].size() >= 2 && bed_mesh["mesh_max"][0].is_number() &&
        bed_mesh["mesh_max"][1].is_number()) {
        active_bed_mesh_.mesh_max[0] = bed_mesh["mesh_max"][0].template get<float>();
        active_bed_mesh_.mesh_max[1] = bed_mesh["mesh_max"][1].template get<float>();
    }

    // Parse available profiles and their mesh data
    if (bed_mesh.contains("profiles") && bed_mesh["profiles"].is_object()) {
        bed_mesh_profiles_.clear();
        stored_bed_mesh_profiles_.clear();

        spdlog::debug("[MoonrakerAPI] Parsing {} bed mesh profiles", bed_mesh["profiles"].size());

        for (auto& [profile_name, profile_data] : bed_mesh["profiles"].items()) {
            bed_mesh_profiles_.push_back(profile_name);

            // Parse and store mesh data for this profile (if available)
            if (profile_data.is_object()) {
                BedMeshProfile profile;
                profile.name = profile_name;

                // Parse points array (Moonraker calls it "points", not "probed_matrix")
                if (profile_data.contains("points") && profile_data["points"].is_array()) {
                    for (const auto& row : profile_data["points"]) {
                        if (row.is_array()) {
                            std::vector<float> row_vec;
                            for (const auto& val : row) {
                                if (val.is_number()) {
                                    row_vec.push_back(val.template get<float>());
                                }
                            }
                            if (!row_vec.empty()) {
                                profile.probed_matrix.push_back(row_vec);
                            }
                        }
                    }
                }

                // Parse mesh bounds
                if (profile_data.contains("mesh_params") &&
                    profile_data["mesh_params"].is_object()) {
                    const auto& params = profile_data["mesh_params"];
                    if (params.contains("min_x"))
                        profile.mesh_min[0] = params["min_x"].template get<float>();
                    if (params.contains("min_y"))
                        profile.mesh_min[1] = params["min_y"].template get<float>();
                    if (params.contains("max_x"))
                        profile.mesh_max[0] = params["max_x"].template get<float>();
                    if (params.contains("max_y"))
                        profile.mesh_max[1] = params["max_y"].template get<float>();
                    if (params.contains("x_count"))
                        profile.x_count = params["x_count"].template get<int>();
                    if (params.contains("y_count"))
                        profile.y_count = params["y_count"].template get<int>();
                }

                if (!profile.probed_matrix.empty()) {
                    stored_bed_mesh_profiles_[profile_name] = std::move(profile);
                }
            }
        }
    }

    // Parse algorithm from mesh_params (if available)
    if (bed_mesh.contains("mesh_params") && bed_mesh["mesh_params"].is_object()) {
        const json& params = bed_mesh["mesh_params"];
        if (params.contains("algo") && params["algo"].is_string()) {
            active_bed_mesh_.algo = params["algo"].template get<std::string>();
        }
    }

    if (active_bed_mesh_.probed_matrix.empty()) {
        spdlog::debug("[MoonrakerAPI] Bed mesh data cleared (no probed_matrix)");
    } else {
        spdlog::debug("[MoonrakerAPI] Bed mesh updated: profile='{}', size={}x{}, "
                      "profiles={}, algo='{}'",
                      active_bed_mesh_.name, active_bed_mesh_.x_count, active_bed_mesh_.y_count,
                      bed_mesh_profiles_.size(), active_bed_mesh_.algo);
    }
}

const BedMeshProfile* MoonrakerAPI::get_active_bed_mesh() const {
    std::lock_guard<std::mutex> lock(bed_mesh_mutex_);

    if (active_bed_mesh_.probed_matrix.empty()) {
        return nullptr;
    }
    return &active_bed_mesh_;
}

std::vector<std::string> MoonrakerAPI::get_bed_mesh_profiles() const {
    std::lock_guard<std::mutex> lock(bed_mesh_mutex_);
    return bed_mesh_profiles_;
}

bool MoonrakerAPI::has_bed_mesh() const {
    std::lock_guard<std::mutex> lock(bed_mesh_mutex_);
    return !active_bed_mesh_.probed_matrix.empty();
}

const BedMeshProfile* MoonrakerAPI::get_bed_mesh_profile(const std::string& profile_name) const {
    std::lock_guard<std::mutex> lock(bed_mesh_mutex_);

    // Check stored profiles first
    auto it = stored_bed_mesh_profiles_.find(profile_name);
    if (it != stored_bed_mesh_profiles_.end()) {
        return &it->second;
    }

    // Fall back to active mesh if name matches
    if (active_bed_mesh_.name == profile_name && !active_bed_mesh_.probed_matrix.empty()) {
        return &active_bed_mesh_;
    }

    return nullptr;
}

void MoonrakerAPI::get_excluded_objects(
    std::function<void(const std::set<std::string>&)> on_success, ErrorCallback on_error) {
    // Query exclude_object state from Klipper
    json params = {{"objects", json::object({{"exclude_object", nullptr}})}};

    client_.send_jsonrpc(
        "printer.objects.query", params,
        [on_success](json response) {
            std::set<std::string> excluded;

            try {
                if (response.contains("result") && response["result"].contains("status") &&
                    response["result"]["status"].contains("exclude_object")) {
                    const json& exclude_obj = response["result"]["status"]["exclude_object"];

                    // excluded_objects is an array of object names
                    if (exclude_obj.contains("excluded_objects") &&
                        exclude_obj["excluded_objects"].is_array()) {
                        for (const auto& obj : exclude_obj["excluded_objects"]) {
                            if (obj.is_string()) {
                                excluded.insert(obj.get<std::string>());
                            }
                        }
                    }
                }

                spdlog::debug("[Moonraker API] get_excluded_objects() -> {} objects",
                              excluded.size());
                if (on_success) {
                    on_success(excluded);
                }
            } catch (const std::exception& e) {
                spdlog::error("[Moonraker API] Failed to parse excluded objects: {}", e.what());
                if (on_success) {
                    on_success(std::set<std::string>{}); // Return empty set on error
                }
            }
        },
        on_error);
}

void MoonrakerAPI::get_available_objects(
    std::function<void(const std::vector<std::string>&)> on_success, ErrorCallback on_error) {
    // Query exclude_object state from Klipper
    json params = {{"objects", json::object({{"exclude_object", nullptr}})}};

    client_.send_jsonrpc(
        "printer.objects.query", params,
        [on_success](json response) {
            std::vector<std::string> objects;

            try {
                if (response.contains("result") && response["result"].contains("status") &&
                    response["result"]["status"].contains("exclude_object")) {
                    const json& exclude_obj = response["result"]["status"]["exclude_object"];

                    // objects is an array of {name, center, polygon} objects
                    if (exclude_obj.contains("objects") && exclude_obj["objects"].is_array()) {
                        for (const auto& obj : exclude_obj["objects"]) {
                            if (obj.is_object() && obj.contains("name") &&
                                obj["name"].is_string()) {
                                objects.push_back(obj["name"].get<std::string>());
                            }
                        }
                    }
                }

                spdlog::debug("[Moonraker API] get_available_objects() -> {} objects",
                              objects.size());
                if (on_success) {
                    on_success(objects);
                }
            } catch (const std::exception& e) {
                spdlog::error("[Moonraker API] Failed to parse available objects: {}", e.what());
                if (on_success) {
                    on_success(std::vector<std::string>{}); // Return empty vector on error
                }
            }
        },
        on_error);
}

// ============================================================================
// ADVANCED PANEL STUB IMPLEMENTATIONS
// ============================================================================
// These methods are placeholders for future implementation.
// NOTE: start_bed_mesh_calibrate is implemented after BedMeshProgressCollector class below.

/**
 * @brief Collector for PID_CALIBRATE gcode responses
 *
 * Klipper sends PID calibration results as console output via notify_gcode_response.
 * This class monitors for the result line containing pid_Kp, pid_Ki, pid_Kd values.
 *
 * Expected output format:
 *   PID parameters: pid_Kp=22.865 pid_Ki=1.292 pid_Kd=101.178
 *
 * Error handling:
 *   - "Unknown command" with "PID_CALIBRATE" - command not recognized
 *   - "Error"/"error"/"!! " - Klipper error messages
 *
 * Note: No timeout is implemented. Caller should implement UI-level timeout if needed.
 */
class PIDCalibrateCollector : public std::enable_shared_from_this<PIDCalibrateCollector> {
  public:
    using PIDCallback = std::function<void(float kp, float ki, float kd)>;
    using PIDProgressCallback = std::function<void(int sample, float tolerance)>;

    PIDCalibrateCollector(MoonrakerClient& client, PIDCallback on_success,
                          MoonrakerAPI::ErrorCallback on_error,
                          PIDProgressCallback on_progress = nullptr)
        : client_(client), on_success_(std::move(on_success)), on_error_(std::move(on_error)),
          on_progress_(std::move(on_progress)) {}

    ~PIDCalibrateCollector() {
        unregister();
    }

    void start() {
        static std::atomic<uint64_t> s_collector_id{0};
        handler_name_ = "pid_calibrate_collector_" + std::to_string(++s_collector_id);
        auto self = shared_from_this();
        client_.register_method_callback("notify_gcode_response", handler_name_,
                                         [self](const json& msg) { self->on_gcode_response(msg); });
        registered_.store(true);
        spdlog::debug("[PIDCalibrateCollector] Started (handler: {})", handler_name_);
    }

    void unregister() {
        bool was = registered_.exchange(false);
        if (was) {
            client_.unregister_method_callback("notify_gcode_response", handler_name_);
            spdlog::debug("[PIDCalibrateCollector] Unregistered");
        }
    }

    void mark_completed() {
        completed_.store(true);
    }

    void on_gcode_response(const json& msg) {
        if (completed_.load())
            return;
        if (!msg.contains("params") || !msg["params"].is_array() || msg["params"].empty())
            return;

        const std::string& line = msg["params"][0].get_ref<const std::string&>();
        spdlog::trace("[PIDCalibrateCollector] Received: {}", line);

        // Check for progress: "sample:1 pwm:0.5 asymmetry:0.2 tolerance:n/a"
        static const std::regex sample_regex(
            R"(sample:(\d+)\s+pwm:[\d.]+\s+asymmetry:[\d.]+\s+tolerance:(\S+))");
        std::smatch progress_match;
        if (std::regex_search(line, progress_match, sample_regex)) {
            int sample_num = std::stoi(progress_match[1].str());
            float tolerance_val = -1.0f;
            std::string tol_str = progress_match[2].str();
            if (tol_str != "n/a") {
                try {
                    tolerance_val = std::stof(tol_str);
                } catch (...) {
                }
            }
            spdlog::debug("[PIDCalibrateCollector] Progress: sample={} tolerance={}", sample_num,
                          tolerance_val);
            if (on_progress_)
                on_progress_(sample_num, tolerance_val);
            return;
        }

        // Check for PID result: "PID parameters: pid_Kp=22.865 pid_Ki=1.292 pid_Kd=101.178"
        static const std::regex pid_regex(R"(pid_Kp=([\d.]+)\s+pid_Ki=([\d.]+)\s+pid_Kd=([\d.]+))");
        std::smatch match;
        if (std::regex_search(line, match, pid_regex) && match.size() == 4) {
            float kp = std::stof(match[1].str());
            float ki = std::stof(match[2].str());
            float kd = std::stof(match[3].str());
            complete_success(kp, ki, kd);
            return;
        }

        // Check for unknown command error
        if (line.find("Unknown command") != std::string::npos &&
            line.find("PID_CALIBRATE") != std::string::npos) {
            complete_error("PID_CALIBRATE command not recognized. Check Klipper configuration.");
            return;
        }

        // Broader error detection
        if (line.find("Error") != std::string::npos || line.find("error") != std::string::npos ||
            line.rfind("!! ", 0) == 0) {
            complete_error(line);
            return;
        }
    }

  private:
    void complete_success(float kp, float ki, float kd) {
        if (completed_.exchange(true))
            return;
        spdlog::info("[PIDCalibrateCollector] PID result: Kp={:.3f} Ki={:.3f} Kd={:.3f}", kp, ki,
                     kd);
        unregister();
        if (on_success_)
            on_success_(kp, ki, kd);
    }

    void complete_error(const std::string& message) {
        if (completed_.exchange(true))
            return;
        spdlog::error("[PIDCalibrateCollector] Error: {}", message);
        unregister();
        if (on_error_) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::JSON_RPC_ERROR;
            err.message = message;
            err.method = "PID_CALIBRATE";
            on_error_(err);
        }
    }

    MoonrakerClient& client_;
    PIDCallback on_success_;
    MoonrakerAPI::ErrorCallback on_error_;
    PIDProgressCallback on_progress_;
    std::string handler_name_;
    std::atomic<bool> registered_{false};
    std::atomic<bool> completed_{false};
};

/**
 * @brief State machine for collecting SCREWS_TILT_CALCULATE responses
 *
 * Klipper sends screw tilt results as console output lines via notify_gcode_response.
 * This class collects and parses those lines until the sequence completes.
 *
 * Expected output format:
 *   // front_left (base) : x=-5.0, y=30.0, z=2.48750
 *   // front_right : x=155.0, y=30.0, z=2.36000 : adjust CW 01:15
 *   // rear_right : x=155.0, y=180.0, z=2.42500 : adjust CCW 00:30
 *   // rear_left : x=155.0, y=180.0, z=2.42500 : adjust CW 00:18
 *
 * Error handling:
 *   - "Unknown command" - screws_tilt_adjust not configured
 *   - "Error"/"error"/"!! " - Klipper error messages
 *   - "ok" without data - probing completed but no results parsed
 *
 * Note: No timeout is implemented. If connection drops mid-probing, the collector
 * will remain alive until the shared_ptr ref count drops (when MoonrakerClient
 * cleans up callbacks). Caller should implement UI-level timeout if needed.
 */
class ScrewsTiltCollector : public std::enable_shared_from_this<ScrewsTiltCollector> {
  public:
    ScrewsTiltCollector(MoonrakerClient& client, ScrewTiltCallback on_success,
                        MoonrakerAPI::ErrorCallback on_error)
        : client_(client), on_success_(std::move(on_success)), on_error_(std::move(on_error)) {}

    ~ScrewsTiltCollector() {
        // Ensure we always unregister callback
        unregister();
    }

    void start() {
        // Register for gcode_response notifications
        // Use atomic counter for unique handler names (safer than pointer address reuse)
        static std::atomic<uint64_t> s_collector_id{0};
        handler_name_ = "screws_tilt_collector_" + std::to_string(++s_collector_id);

        auto self = shared_from_this();
        client_.register_method_callback("notify_gcode_response", handler_name_,
                                         [self](const json& msg) { self->on_gcode_response(msg); });

        registered_.store(true);
        spdlog::debug("[ScrewsTiltCollector] Started collecting responses (handler: {})",
                      handler_name_);
    }

    void unregister() {
        bool was_registered = registered_.exchange(false);
        if (was_registered) {
            client_.unregister_method_callback("notify_gcode_response", handler_name_);
            spdlog::debug("[ScrewsTiltCollector] Unregistered callback");
        }
    }

    /**
     * @brief Mark as completed without invoking callbacks
     *
     * Used when the execute_gcode error path handles the error callback directly.
     */
    void mark_completed() {
        completed_.store(true);
    }

    void on_gcode_response(const json& msg) {
        // Check if already completed (prevent double-invocation)
        if (completed_.load()) {
            return;
        }

        // notify_gcode_response format: {"method": "notify_gcode_response", "params": ["line"]}
        if (!msg.contains("params") || !msg["params"].is_array() || msg["params"].empty()) {
            return;
        }

        const std::string& line = msg["params"][0].get_ref<const std::string&>();
        spdlog::trace("[ScrewsTiltCollector] Received: {}", line);

        // Check for unknown command error (screws_tilt_adjust not configured)
        if (line.find("Unknown command") != std::string::npos &&
            line.find("SCREWS_TILT_CALCULATE") != std::string::npos) {
            complete_error("SCREWS_TILT_CALCULATE requires [screws_tilt_adjust] in printer.cfg");
            return;
        }

        // Parse screw result lines that start with "//"
        if (line.rfind("//", 0) == 0) {
            parse_screw_line(line);
        }

        // Check for completion markers
        // Klipper prints "ok" when command completes
        if (line == "ok") {
            if (!results_.empty()) {
                complete_success();
            } else {
                complete_error("SCREWS_TILT_CALCULATE completed but no screw data received");
            }
            return;
        }

        // Broader error detection - catch Klipper errors
        if (line.find("Error") != std::string::npos || line.find("error") != std::string::npos ||
            line.rfind("!! ", 0) == 0) { // Emergency/critical errors start with "!! "
            complete_error(line);
        }
    }

  private:
    void parse_screw_line(const std::string& line) {
        // Format: "// screw_name (base) : x=X, y=Y, z=Z" for reference
        // Format: "// screw_name : x=X, y=Y, z=Z : adjust DIR TT:MM" for non-reference

        ScrewTiltResult result;

        // Find the screw name (after "//" and any whitespace, before first " :" or " (")
        size_t name_start = 2; // Skip "//"
        // Skip any whitespace after "//"
        while (name_start < line.length() && line[name_start] == ' ') {
            name_start++;
        }

        size_t name_end = line.find(" :");
        size_t base_pos = line.find(" (base)");

        if (base_pos != std::string::npos &&
            (name_end == std::string::npos || base_pos < name_end)) {
            // Reference screw with "(base)" marker
            result.screw_name = line.substr(name_start, base_pos - name_start);
            result.is_reference = true;
        } else if (name_end != std::string::npos) {
            result.screw_name = line.substr(name_start, name_end - name_start);
            result.is_reference = false;
        } else {
            // Can't parse - skip this line
            spdlog::debug("[ScrewsTiltCollector] Could not parse line: {}", line);
            return;
        }

        // Trim whitespace from screw name (leading and trailing)
        while (!result.screw_name.empty() && result.screw_name.front() == ' ') {
            result.screw_name.erase(0, 1);
        }
        while (!result.screw_name.empty() && result.screw_name.back() == ' ') {
            result.screw_name.pop_back();
        }

        // Parse x, y, z values
        // Look for "x=", "y=", "z="
        auto parse_float = [&line](const std::string& prefix) -> float {
            size_t pos = line.find(prefix);
            if (pos == std::string::npos) {
                return 0.0f;
            }
            pos += prefix.length();
            // Find end of number (next comma, space, or end of line)
            size_t end = line.find_first_of(", ", pos);
            if (end == std::string::npos) {
                end = line.length();
            }
            try {
                return std::stof(line.substr(pos, end - pos));
            } catch (...) {
                return 0.0f;
            }
        };

        result.x_pos = parse_float("x=");
        result.y_pos = parse_float("y=");
        result.z_height = parse_float("z=");

        // Parse adjustment for non-reference screws
        // Look for ": adjust CW 01:15" or ": adjust CCW 00:30"
        if (!result.is_reference) {
            size_t adjust_pos = line.find(": adjust ");
            if (adjust_pos != std::string::npos) {
                result.adjustment = line.substr(adjust_pos + 9); // Skip ": adjust "
                // Trim any trailing whitespace
                while (!result.adjustment.empty() &&
                       std::isspace(static_cast<unsigned char>(result.adjustment.back()))) {
                    result.adjustment.pop_back();
                }
            }
        }

        spdlog::debug("[ScrewsTiltCollector] Parsed: {} at ({:.1f}, {:.1f}) z={:.3f} {}",
                      result.screw_name, result.x_pos, result.y_pos, result.z_height,
                      result.is_reference ? "(reference)" : result.adjustment);

        results_.push_back(std::move(result));
    }

    void complete_success() {
        if (completed_) {
            return;
        }
        completed_ = true;

        spdlog::info("[ScrewsTiltCollector] Complete with {} screws", results_.size());
        unregister();

        if (on_success_) {
            on_success_(results_);
        }
    }

    void complete_error(const std::string& message) {
        if (completed_) {
            return;
        }
        completed_ = true;

        spdlog::error("[ScrewsTiltCollector] Error: {}", message);
        unregister();

        if (on_error_) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::JSON_RPC_ERROR;
            err.message = message;
            err.method = "SCREWS_TILT_CALCULATE";
            on_error_(err);
        }
    }

    MoonrakerClient& client_;
    ScrewTiltCallback on_success_;
    MoonrakerAPI::ErrorCallback on_error_;
    std::string handler_name_;
    std::atomic<bool> registered_{false}; // Thread-safe: accessed from callback and destructor
    std::atomic<bool> completed_{false};  // Thread-safe: prevents double-callback invocation
    std::vector<ScrewTiltResult> results_;
};

/**
 * @brief State machine for collecting SHAPER_CALIBRATE responses
 *
 * Klipper sends input shaper results as console output lines via notify_gcode_response.
 * This class collects and parses those lines until the sequence completes.
 *
 * Expected output format:
 *   Testing frequency 5.00 Hz
 *   ...
 *   Testing frequency 100.00 Hz
 *   Wait for calculations..
 *   Fitted shaper 'zv' frequency = 35.8 Hz (vibrations = 22.7%, smoothing ~= 0.100)
 *   suggested max_accel <= 4000 mm/sec^2
 *   Fitted shaper 'mzv' frequency = 36.7 Hz (vibrations = 7.2%, smoothing ~= 0.140)
 *   suggested max_accel <= 5400 mm/sec^2
 *   ...
 *   Recommended shaper_type_x = mzv, shaper_freq_x = 36.7 Hz
 *   calibration data written to /tmp/calibration_data_x_*.csv
 */
class InputShaperCollector : public std::enable_shared_from_this<InputShaperCollector> {
  public:
    InputShaperCollector(MoonrakerClient& client, char axis, AdvancedProgressCallback on_progress,
                         InputShaperCallback on_success, MoonrakerAPI::ErrorCallback on_error)
        : client_(client), axis_(axis), on_progress_(std::move(on_progress)),
          on_success_(std::move(on_success)), on_error_(std::move(on_error)),
          last_activity_(std::chrono::steady_clock::now()) {}

    ~InputShaperCollector() {
        unregister();
    }

    void start() {
        static std::atomic<uint64_t> s_collector_id{0};
        handler_name_ = "input_shaper_collector_" + std::to_string(++s_collector_id);

        auto self = shared_from_this();
        client_.register_method_callback("notify_gcode_response", handler_name_,
                                         [self](const json& msg) { self->on_gcode_response(msg); });

        registered_.store(true);
        spdlog::debug(
            "[InputShaperCollector] Started collecting responses for axis {} (handler: {})", axis_,
            handler_name_);
    }

    void unregister() {
        bool was_registered = registered_.exchange(false);
        if (was_registered) {
            client_.unregister_method_callback("notify_gcode_response", handler_name_);
            spdlog::debug("[InputShaperCollector] Unregistered callback");
        }
    }

    void mark_completed() {
        completed_.store(true);
    }

    void on_gcode_response(const json& msg) {
        if (completed_.load()) {
            return;
        }

        if (!msg.contains("params") || !msg["params"].is_array() || msg["params"].empty()) {
            return;
        }

        const std::string& line = msg["params"][0].get_ref<const std::string&>();
        spdlog::trace("[InputShaperCollector] Received: {}", line);

        // Reset activity watchdog
        last_activity_ = std::chrono::steady_clock::now();

        // Check for unknown command error
        if (line.find("Unknown command") != std::string::npos &&
            line.find("SHAPER_CALIBRATE") != std::string::npos) {
            complete_error(
                "SHAPER_CALIBRATE requires [resonance_tester] and ADXL345 in printer.cfg");
            return;
        }

        // Parse frequency sweep lines: "Testing frequency 62.00 Hz"
        if (line.find("Testing frequency") != std::string::npos) {
            parse_sweep_line(line);
            return;
        }

        // Parse "Wait for calculations.." — transition to CALCULATING
        if (line.find("Wait for calculations") != std::string::npos) {
            if (collector_state_ != CollectorState::CALCULATING) {
                collector_state_ = CollectorState::CALCULATING;
                emit_progress(55, "Calculating results...");
            }
            return;
        }

        // Parse shaper fit lines
        if (line.find("Fitted shaper") != std::string::npos) {
            parse_shaper_line(line);
            return;
        }

        // Parse max_accel lines: "suggested max_accel <= 4000 mm/sec^2"
        if (line.find("suggested max_accel") != std::string::npos) {
            parse_max_accel_line(line);
            return;
        }

        // Parse recommendation line (try new format first, then old)
        // Don't complete yet — CSV path line follows immediately after
        if (line.find("Recommended shaper") != std::string::npos) {
            parse_recommendation(line);
            collector_state_ = CollectorState::COMPLETE;
            return;
        }

        // Parse CSV path: "calibration data written to /tmp/calibration_data_x_*.csv"
        if (line.find("calibration data written to") != std::string::npos) {
            parse_csv_path(line);
            complete_success();
            return;
        }

        // If we already have the recommendation but got a non-CSV line, complete now
        if (collector_state_ == CollectorState::COMPLETE) {
            complete_success();
            return;
        }

        // Error detection
        if (line.rfind("!! ", 0) == 0 || line.rfind("Error: ", 0) == 0 ||
            line.find("error:") != std::string::npos) {
            complete_error(line);
        }
    }

  private:
    enum class CollectorState { WAITING_FOR_OUTPUT, SWEEPING, CALCULATING, COMPLETE };

    void parse_sweep_line(const std::string& line) {
        static const std::regex freq_regex(R"(Testing frequency ([\d.]+) Hz)");
        std::smatch match;
        if (std::regex_search(line, match, freq_regex) && match.size() == 2) {
            try {
                float freq = std::stof(match[1].str());
                last_sweep_freq_ = freq;

                if (collector_state_ != CollectorState::SWEEPING) {
                    collector_state_ = CollectorState::SWEEPING;
                }

                // Progress: 3-55% range mapped from min_freq to max_freq
                float range = max_freq_ - min_freq_;
                float progress_frac = (range > 0) ? (freq - min_freq_) / range : 0.0f;
                int percent = 3 + static_cast<int>(progress_frac * 52.0f);
                percent = std::clamp(percent, 3, 55);

                char status[64];
                snprintf(status, sizeof(status), "Testing frequency %.0f Hz", freq);
                emit_progress(percent, status);
            } catch (const std::exception&) {
                // Ignore parse errors
            }
        }
    }

    void parse_shaper_line(const std::string& line) {
        static const std::regex shaper_regex(
            R"(Fitted shaper '(\w+)' frequency = ([\d.]+) Hz \(vibrations = ([\d.]+)%, smoothing ~= ([\d.]+)\))");

        std::smatch match;
        if (std::regex_search(line, match, shaper_regex) && match.size() == 5) {
            ShaperFitData fit;
            fit.type = match[1].str();
            try {
                fit.frequency = std::stof(match[2].str());
                fit.vibrations = std::stof(match[3].str());
                fit.smoothing = std::stof(match[4].str());
            } catch (const std::exception& e) {
                spdlog::warn("[InputShaperCollector] Failed to parse values: {}", e.what());
                return;
            }

            spdlog::debug("[InputShaperCollector] Parsed: {} @ {:.1f} Hz (vib: {:.1f}%)", fit.type,
                          fit.frequency, fit.vibrations);
            shaper_fits_.push_back(fit);

            // Emit progress in CALCULATING phase: 55-95% range, ~8% per shaper (5 shapers)
            int calc_progress = 55 + static_cast<int>(shaper_fits_.size()) * 8;
            calc_progress = std::min(calc_progress, 95);
            char status[64];
            snprintf(status, sizeof(status), "Fitted %s at %.1f Hz", fit.type.c_str(),
                     fit.frequency);
            emit_progress(calc_progress, status);
        }
    }

    void parse_max_accel_line(const std::string& line) {
        static const std::regex accel_regex(R"(suggested max_accel <= (\d+))");
        std::smatch match;
        if (std::regex_search(line, match, accel_regex) && match.size() == 2) {
            try {
                float max_accel = std::stof(match[1].str());
                // Attach to the most recently parsed shaper fit
                if (!shaper_fits_.empty()) {
                    shaper_fits_.back().max_accel = max_accel;
                    spdlog::debug("[InputShaperCollector] {} max_accel: {:.0f}",
                                  shaper_fits_.back().type, max_accel);
                }
            } catch (const std::exception&) {
                // Ignore parse errors
            }
        }
    }

    void parse_recommendation(const std::string& line) {
        // Try new Klipper format first: "Recommended shaper_type_x = mzv, shaper_freq_x = 53.8 Hz"
        static const std::regex rec_new(
            R"(Recommended shaper_type_\w+ = (\w+), shaper_freq_\w+ = ([\d.]+) Hz)");
        // Legacy format: "Recommended shaper is mzv @ 36.7 Hz"
        static const std::regex rec_old(R"(Recommended shaper is (\w+) @ ([\d.]+) Hz)");

        std::smatch match;
        bool matched = std::regex_search(line, match, rec_new);
        if (!matched) {
            matched = std::regex_search(line, match, rec_old);
        }

        if (matched && match.size() == 3) {
            recommended_type_ = match[1].str();
            try {
                recommended_freq_ = std::stof(match[2].str());
            } catch (const std::exception&) {
                recommended_freq_ = 0.0f;
            }
            spdlog::info("[InputShaperCollector] Recommendation: {} @ {:.1f} Hz", recommended_type_,
                         recommended_freq_);
        }
    }

    void parse_csv_path(const std::string& line) {
        static const std::regex csv_regex(R"(calibration data written to (\S+\.csv))");
        std::smatch match;
        if (std::regex_search(line, match, csv_regex) && match.size() == 2) {
            csv_path_ = match[1].str();
            spdlog::info("[InputShaperCollector] CSV path: {}", csv_path_);
        }
    }

    void emit_progress(int percent, const std::string& status) {
        if (on_progress_) {
            on_progress_(percent);
        }
        spdlog::trace("[InputShaperCollector] Progress: {}% - {}", percent, status);
    }

    void complete_success() {
        if (completed_.exchange(true)) {
            return;
        }

        spdlog::info("[InputShaperCollector] Complete with {} shaper options", shaper_fits_.size());
        unregister();

        // Emit 100% progress
        emit_progress(100, "Complete");

        if (on_success_) {
            InputShaperResult result;
            result.axis = axis_;
            result.shaper_type = recommended_type_;
            result.shaper_freq = recommended_freq_;
            result.csv_path = csv_path_;

            // Find recommended shaper's details and populate all_shapers
            for (const auto& fit : shaper_fits_) {
                if (fit.type == recommended_type_) {
                    result.smoothing = fit.smoothing;
                    result.vibrations = fit.vibrations;
                    result.max_accel = fit.max_accel;
                }

                ShaperOption option;
                option.type = fit.type;
                option.frequency = fit.frequency;
                option.vibrations = fit.vibrations;
                option.smoothing = fit.smoothing;
                option.max_accel = fit.max_accel;
                result.all_shapers.push_back(option);
            }

            // Parse frequency response data from calibration CSV
            if (!result.csv_path.empty()) {
                auto csv_data = helix::calibration::parse_shaper_csv(result.csv_path, axis_);
                if (!csv_data.frequencies.empty()) {
                    result.freq_response.reserve(csv_data.frequencies.size());
                    for (size_t i = 0; i < csv_data.frequencies.size(); ++i) {
                        result.freq_response.emplace_back(
                            csv_data.frequencies[i],
                            i < csv_data.raw_psd.size() ? csv_data.raw_psd[i] : 0.0f);
                    }
                    result.shaper_curves = std::move(csv_data.shaper_curves);
                    spdlog::debug(
                        "[InputShaperCollector] parsed {} freq bins, {} shaper curves from CSV",
                        result.freq_response.size(), result.shaper_curves.size());
                }
            }

            on_success_(result);
        }
    }

    void complete_error(const std::string& message) {
        if (completed_.exchange(true)) {
            return;
        }

        spdlog::error("[InputShaperCollector] Error: {}", message);
        unregister();

        if (on_error_) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::JSON_RPC_ERROR;
            err.message = message;
            err.method = "SHAPER_CALIBRATE";
            on_error_(err);
        }
    }

    // Internal struct for collecting fits before building final result
    struct ShaperFitData {
        std::string type;
        float frequency = 0.0f;
        float vibrations = 0.0f;
        float smoothing = 0.0f;
        float max_accel = 0.0f;
    };

    MoonrakerClient& client_;
    char axis_;
    AdvancedProgressCallback on_progress_;
    InputShaperCallback on_success_;
    MoonrakerAPI::ErrorCallback on_error_;
    std::string handler_name_;
    std::atomic<bool> registered_{false};
    std::atomic<bool> completed_{false};

    CollectorState collector_state_ = CollectorState::WAITING_FOR_OUTPUT;
    float min_freq_ = 5.0f;
    float max_freq_ = 100.0f;
    float last_sweep_freq_ = 0.0f;
    std::string csv_path_;
    std::chrono::steady_clock::time_point last_activity_;

    std::vector<ShaperFitData> shaper_fits_;
    std::string recommended_type_;
    float recommended_freq_ = 0.0f;
};

/**
 * @brief State machine for collecting MEASURE_AXES_NOISE responses
 *
 * Klipper sends noise measurement results as console output lines via notify_gcode_response.
 * This class collects and parses those lines to extract the noise level.
 *
 * Expected output format:
 *   "Axes noise for xy-axis accelerometer: 57.956 (x), 103.543 (y), 45.396 (z)"
 *
 * Error handling:
 *   - "Unknown command" - MEASURE_AXES_NOISE not available (no accelerometer)
 *   - "Error"/"error"/"!! " - Klipper error messages
 */
class NoiseCheckCollector : public std::enable_shared_from_this<NoiseCheckCollector> {
  public:
    NoiseCheckCollector(MoonrakerClient& client, MoonrakerAPI::NoiseCheckCallback on_success,
                        MoonrakerAPI::ErrorCallback on_error)
        : client_(client), on_success_(std::move(on_success)), on_error_(std::move(on_error)) {}

    ~NoiseCheckCollector() {
        unregister();
    }

    void start() {
        static std::atomic<uint64_t> s_collector_id{0};
        handler_name_ = "noise_check_collector_" + std::to_string(++s_collector_id);

        auto self = shared_from_this();
        client_.register_method_callback("notify_gcode_response", handler_name_,
                                         [self](const json& msg) { self->on_gcode_response(msg); });

        registered_.store(true);
        spdlog::debug("[NoiseCheckCollector] Started collecting responses (handler: {})",
                      handler_name_);
    }

    void unregister() {
        bool was_registered = registered_.exchange(false);
        if (was_registered) {
            client_.unregister_method_callback("notify_gcode_response", handler_name_);
            spdlog::debug("[NoiseCheckCollector] Unregistered callback");
        }
    }

    void mark_completed() {
        completed_.store(true);
    }

    void on_gcode_response(const json& msg) {
        if (completed_.load()) {
            return;
        }

        if (!msg.contains("params") || !msg["params"].is_array() || msg["params"].empty()) {
            return;
        }

        const std::string& line = msg["params"][0].get_ref<const std::string&>();
        spdlog::trace("[NoiseCheckCollector] Received: {}", line);

        // Check for unknown command error (no accelerometer configured)
        if (line.find("Unknown command") != std::string::npos &&
            line.find("MEASURE_AXES_NOISE") != std::string::npos) {
            complete_error("MEASURE_AXES_NOISE requires [adxl345] accelerometer in printer.cfg");
            return;
        }

        // Parse noise level line: "Axes noise for xy-axis accelerometer: 57.956 (x), ..."
        if (line.find("Axes noise") != std::string::npos) {
            parse_noise_line(line);
            return;
        }

        // Error detection
        if (line.rfind("!! ", 0) == 0 ||                // Emergency errors
            line.rfind("Error:", 0) == 0 ||             // Standard errors
            line.find("error:") != std::string::npos) { // Python traceback
            complete_error(line);
        }
    }

  private:
    void parse_noise_line(const std::string& line) {
        // Klipper output format:
        // "Axes noise for xy-axis accelerometer: 57.956 (x), 103.543 (y), 45.396 (z)"
        static const std::regex noise_regex(
            R"(Axes noise.*:\s*([\d.]+)\s*\(x\),\s*([\d.]+)\s*\(y\),\s*([\d.]+)\s*\(z\))");

        std::smatch match;
        if (std::regex_search(line, match, noise_regex) && match.size() == 4) {
            try {
                float noise_x = std::stof(match[1].str());
                float noise_y = std::stof(match[2].str());
                float noise_z = std::stof(match[3].str());

                spdlog::info("[NoiseCheckCollector] Noise: x={:.2f}, y={:.2f}, z={:.2f}", noise_x,
                             noise_y, noise_z);

                // Zero reading on X or Y means accelerometer isn't working on that axis
                constexpr float MIN_NOISE = 0.001f;
                if (noise_x < MIN_NOISE || noise_y < MIN_NOISE) {
                    std::string dead_axes;
                    if (noise_x < MIN_NOISE)
                        dead_axes += "X";
                    if (noise_y < MIN_NOISE) {
                        if (!dead_axes.empty())
                            dead_axes += " and ";
                        dead_axes += "Y";
                    }
                    complete_error("Accelerometer reading zero on " + dead_axes +
                                   " axis — check wiring and axes_map configuration");
                    return;
                }

                // Report max of x,y as the overall noise level
                float noise = std::max(noise_x, noise_y);
                complete_success(noise);
            } catch (const std::exception& e) {
                spdlog::warn("[NoiseCheckCollector] Failed to parse noise value: {}", e.what());
                complete_error("Failed to parse noise measurement");
            }
        }
    }

    void complete_success(float noise_level) {
        if (completed_.exchange(true)) {
            return;
        }

        spdlog::info("[NoiseCheckCollector] Complete with noise level: {:.6f}", noise_level);
        unregister();

        if (on_success_) {
            on_success_(noise_level);
        }
    }

    void complete_error(const std::string& message) {
        if (completed_.exchange(true)) {
            return;
        }

        spdlog::error("[NoiseCheckCollector] Error: {}", message);
        unregister();

        if (on_error_) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::JSON_RPC_ERROR;
            err.message = message;
            err.method = "MEASURE_AXES_NOISE";
            on_error_(err);
        }
    }

    MoonrakerClient& client_;
    MoonrakerAPI::NoiseCheckCallback on_success_;
    MoonrakerAPI::ErrorCallback on_error_;
    std::string handler_name_;
    std::atomic<bool> registered_{false};
    std::atomic<bool> completed_{false};
};

/**
 * @brief State machine for collecting BED_MESH_CALIBRATE progress
 *
 * Klipper sends probing progress as console output lines via notify_gcode_response.
 * This class collects and parses those lines to provide real-time progress updates.
 *
 * Expected output formats:
 *   Probing point 5/25
 *   Probe point 5 of 25
 *
 * Completion markers:
 *   "Mesh Bed Leveling Complete"
 *   "Mesh bed leveling complete"
 *
 * Error handling:
 *   - "!! " prefix - Klipper emergency/critical errors
 *   - "Error:" prefix - Standard Klipper errors
 *   - "error:" in line - Python traceback errors
 */
class BedMeshProgressCollector : public std::enable_shared_from_this<BedMeshProgressCollector> {
  public:
    using ProgressCallback = std::function<void(int current, int total)>;

    BedMeshProgressCollector(MoonrakerClient& client, ProgressCallback on_progress,
                             MoonrakerAPI::SuccessCallback on_complete,
                             MoonrakerAPI::ErrorCallback on_error)
        : client_(client), on_progress_(std::move(on_progress)),
          on_complete_(std::move(on_complete)), on_error_(std::move(on_error)) {}

    ~BedMeshProgressCollector() {
        unregister();
    }

    void start() {
        static std::atomic<uint64_t> s_collector_id{0};
        handler_name_ = "bed_mesh_collector_" + std::to_string(++s_collector_id);

        auto self = shared_from_this();
        client_.register_method_callback("notify_gcode_response", handler_name_,
                                         [self](const json& msg) { self->on_gcode_response(msg); });

        registered_.store(true);
        spdlog::debug("[BedMeshProgressCollector] Started collecting responses (handler: {})",
                      handler_name_);
    }

    void unregister() {
        bool was_registered = registered_.exchange(false);
        if (was_registered) {
            client_.unregister_method_callback("notify_gcode_response", handler_name_);
            spdlog::debug("[BedMeshProgressCollector] Unregistered callback");
        }
    }

    void mark_completed() {
        completed_.store(true);
    }

    void on_gcode_response(const json& msg) {
        if (completed_.load()) {
            return;
        }

        // notify_gcode_response format: {"method": "notify_gcode_response", "params": ["line"]}
        if (!msg.contains("params") || !msg["params"].is_array() || msg["params"].empty()) {
            return;
        }

        const std::string& line = msg["params"][0].get_ref<const std::string&>();
        spdlog::trace("[BedMeshProgressCollector] Received: {}", line);

        // Check for errors first
        if (line.rfind("!! ", 0) == 0 ||                // Emergency errors
            line.rfind("Error:", 0) == 0 ||             // Standard errors
            line.find("error:") != std::string::npos) { // Python traceback
            complete_error(line);
            return;
        }

        // Check for unknown command error
        if (line.find("Unknown command") != std::string::npos &&
            line.find("BED_MESH_CALIBRATE") != std::string::npos) {
            complete_error("BED_MESH_CALIBRATE requires [bed_mesh] in printer.cfg");
            return;
        }

        // Try to parse probe progress
        parse_probe_line(line);

        // Check for completion markers
        if (line.find("Mesh Bed Leveling Complete") != std::string::npos ||
            line.find("Mesh bed leveling complete") != std::string::npos) {
            complete_success();
            return;
        }
    }

  private:
    void parse_probe_line(const std::string& line) {
        // Static regex for performance - handles both formats:
        // "Probing point 5/25" and "Probe point 5 of 25"
        static const std::regex probe_regex(
            R"(Prob(?:ing point|e point) (\d+)[/\s]+(?:of\s+)?(\d+))");

        std::smatch match;
        if (std::regex_search(line, match, probe_regex) && match.size() == 3) {
            try {
                int current = std::stoi(match[1].str());
                int total = std::stoi(match[2].str());

                // Update tracked values
                current_probe_ = current;
                total_probes_ = total;

                spdlog::debug("[BedMeshProgressCollector] Progress: {}/{}", current, total);

                // Invoke progress callback
                if (on_progress_) {
                    on_progress_(current, total);
                }
            } catch (const std::exception& e) {
                spdlog::warn("[BedMeshProgressCollector] Failed to parse values: {}", e.what());
            }
        }
    }

    void complete_success() {
        if (completed_.exchange(true)) {
            return; // Already completed
        }

        spdlog::info("[BedMeshProgressCollector] Complete ({}/{} probes)", current_probe_,
                     total_probes_);
        unregister();

        if (on_complete_) {
            on_complete_();
        }
    }

    void complete_error(const std::string& message) {
        if (completed_.exchange(true)) {
            return;
        }

        spdlog::error("[BedMeshProgressCollector] Error: {}", message);
        unregister();

        if (on_error_) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::JSON_RPC_ERROR;
            err.message = message;
            err.method = "BED_MESH_CALIBRATE";
            on_error_(err);
        }
    }

    MoonrakerClient& client_;
    ProgressCallback on_progress_;
    MoonrakerAPI::SuccessCallback on_complete_;
    MoonrakerAPI::ErrorCallback on_error_;
    std::string handler_name_;
    std::atomic<bool> registered_{false};
    std::atomic<bool> completed_{false};

    int current_probe_ = 0;
    int total_probes_ = 0;
};

void MoonrakerAPI::start_bed_mesh_calibrate(BedMeshProgressCallback on_progress,
                                            SuccessCallback on_complete, ErrorCallback on_error) {
    spdlog::info("[MoonrakerAPI] Starting bed mesh calibration with progress tracking");

    // Create collector to track progress
    auto collector = std::make_shared<BedMeshProgressCollector>(client_, std::move(on_progress),
                                                                std::move(on_complete), on_error);

    collector->start();

    // Execute the calibration command
    // Note: No PROFILE= parameter - user will name the mesh after completion
    execute_gcode(
        "BED_MESH_CALIBRATE",
        [collector]() {
            // Command accepted - collector will handle completion via gcode_response
            spdlog::debug("[MoonrakerAPI] BED_MESH_CALIBRATE command accepted");
        },
        [collector, on_error](const MoonrakerError& err) {
            spdlog::error("[MoonrakerAPI] BED_MESH_CALIBRATE failed: {}", err.message);
            collector->mark_completed(); // Stop listening
            if (on_error) {
                on_error(err);
            }
        });
}

void MoonrakerAPI::calculate_screws_tilt(ScrewTiltCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Starting SCREWS_TILT_CALCULATE");

    // Create a collector to handle async response parsing
    // The collector will self-destruct when complete via shared_ptr ref counting
    auto collector = std::make_shared<ScrewsTiltCollector>(client_, on_success, on_error);
    collector->start();

    // Send the G-code command
    // The command will trigger probing, and results come back via notify_gcode_response
    execute_gcode(
        "SCREWS_TILT_CALCULATE",
        []() {
            // Command was accepted by Klipper - actual results come via gcode_response
            spdlog::debug("[Moonraker API] SCREWS_TILT_CALCULATE command accepted");
        },
        [collector, on_error](const MoonrakerError& err) {
            // Failed to send command - mark collector completed to prevent double-callback
            spdlog::error("[Moonraker API] Failed to send SCREWS_TILT_CALCULATE: {}", err.message);
            collector->mark_completed(); // Prevent collector from calling on_error again
            collector->unregister();
            if (on_error) {
                on_error(err);
            }
        });
}

void MoonrakerAPI::run_qgl(SuccessCallback /*on_success*/, ErrorCallback on_error) {
    spdlog::warn("[Moonraker API] run_qgl() not yet implemented");
    if (on_error) {
        MoonrakerError err;
        err.type = MoonrakerErrorType::UNKNOWN;
        err.message = "QGL not yet implemented";
        on_error(err);
    }
}

void MoonrakerAPI::run_z_tilt_adjust(SuccessCallback /*on_success*/, ErrorCallback on_error) {
    spdlog::warn("[Moonraker API] run_z_tilt_adjust() not yet implemented");
    if (on_error) {
        MoonrakerError err;
        err.type = MoonrakerErrorType::UNKNOWN;
        err.message = "Z-tilt adjust not yet implemented";
        on_error(err);
    }
}

void MoonrakerAPI::start_resonance_test(char axis, AdvancedProgressCallback on_progress,
                                        InputShaperCallback on_complete, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Starting SHAPER_CALIBRATE AXIS={}", axis);

    // Create collector to handle async response parsing
    auto collector =
        std::make_shared<InputShaperCollector>(client_, axis, on_progress, on_complete, on_error);
    collector->start();

    // Send the G-code command
    // SHAPER_CALIBRATE sweeps 5-100 Hz (~95s) then calculates best shapers (~30-60s)
    static constexpr uint32_t SHAPER_CALIBRATE_TIMEOUT_MS = 5 * 60 * 1000;
    std::string cmd = "SHAPER_CALIBRATE AXIS=";
    cmd += axis;

    execute_gcode(
        cmd, []() { spdlog::debug("[Moonraker API] SHAPER_CALIBRATE command accepted"); },
        [collector, on_error](const MoonrakerError& err) {
            spdlog::error("[Moonraker API] Failed to send SHAPER_CALIBRATE: {}", err.message);
            collector->mark_completed();
            collector->unregister();
            if (on_error) {
                on_error(err);
            }
        },
        SHAPER_CALIBRATE_TIMEOUT_MS);
}

void MoonrakerAPI::start_klippain_shaper_calibration(const std::string& /*axis*/,
                                                     SuccessCallback /*on_success*/,
                                                     ErrorCallback on_error) {
    spdlog::warn("[Moonraker API] start_klippain_shaper_calibration() not yet implemented");
    if (on_error) {
        MoonrakerError err;
        err.type = MoonrakerErrorType::UNKNOWN;
        err.message = "Klippain Shake&Tune not yet implemented";
        on_error(err);
    }
}

void MoonrakerAPI::set_input_shaper(char axis, const std::string& shaper_type, double frequency,
                                    SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Setting input shaper: {}={} @ {:.1f} Hz", axis, shaper_type,
                 frequency);

    // Build SET_INPUT_SHAPER command
    std::ostringstream cmd;
    cmd << "SET_INPUT_SHAPER SHAPER_FREQ_" << axis << "=" << frequency << " SHAPER_TYPE_" << axis
        << "=" << shaper_type;

    execute_gcode(cmd.str(), on_success, on_error);
}

void MoonrakerAPI::measure_axes_noise(NoiseCheckCallback on_complete, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Starting MEASURE_AXES_NOISE");

    // Create collector to handle async response parsing
    auto collector = std::make_shared<NoiseCheckCollector>(client_, on_complete, on_error);
    collector->start();

    // Send the G-code command
    execute_gcode(
        "MEASURE_AXES_NOISE",
        []() { spdlog::debug("[Moonraker API] MEASURE_AXES_NOISE command accepted"); },
        [collector, on_error](const MoonrakerError& err) {
            spdlog::error("[Moonraker API] Failed to send MEASURE_AXES_NOISE: {}", err.message);
            collector->mark_completed();
            collector->unregister();
            if (on_error) {
                on_error(err);
            }
        });
}

void MoonrakerAPI::get_input_shaper_config(InputShaperConfigCallback on_success,
                                           ErrorCallback on_error) {
    spdlog::debug("[Moonraker API] Querying input shaper configuration");

    // Query configfile to get saved input_shaper settings from printer.cfg
    // (the input_shaper runtime object is empty — config lives in configfile)
    json params = {{"objects", json::object({{"configfile", json::array({"config"})}})}};

    client_.send_jsonrpc(
        "printer.objects.query", params,
        [on_success, on_error](json response) {
            try {
                InputShaperConfig config;

                if (response.contains("result") && response["result"].contains("status") &&
                    response["result"]["status"].contains("configfile") &&
                    response["result"]["status"]["configfile"].contains("config") &&
                    response["result"]["status"]["configfile"]["config"].contains("input_shaper")) {
                    const auto& shaper =
                        response["result"]["status"]["configfile"]["config"]["input_shaper"];

                    config.shaper_type_x = shaper.value("shaper_type_x", "");
                    config.shaper_type_y = shaper.value("shaper_type_y", "");

                    // configfile returns frequencies as strings
                    if (shaper.contains("shaper_freq_x")) {
                        auto& val = shaper["shaper_freq_x"];
                        config.shaper_freq_x =
                            val.is_string() ? std::stof(val.get<std::string>()) : val.get<float>();
                    }
                    if (shaper.contains("shaper_freq_y")) {
                        auto& val = shaper["shaper_freq_y"];
                        config.shaper_freq_y =
                            val.is_string() ? std::stof(val.get<std::string>()) : val.get<float>();
                    }
                    if (shaper.contains("damping_ratio_x")) {
                        auto& val = shaper["damping_ratio_x"];
                        config.damping_ratio_x =
                            val.is_string() ? std::stof(val.get<std::string>()) : val.get<float>();
                    }
                    if (shaper.contains("damping_ratio_y")) {
                        auto& val = shaper["damping_ratio_y"];
                        config.damping_ratio_y =
                            val.is_string() ? std::stof(val.get<std::string>()) : val.get<float>();
                    }

                    // Input shaper is configured if at least one axis has a type set
                    config.is_configured =
                        !config.shaper_type_x.empty() || !config.shaper_type_y.empty();

                    spdlog::info(
                        "[Moonraker API] Input shaper config: X={}@{:.1f}Hz, Y={}@{:.1f}Hz",
                        config.shaper_type_x, config.shaper_freq_x, config.shaper_type_y,
                        config.shaper_freq_y);
                } else {
                    spdlog::debug(
                        "[Moonraker API] Input shaper section not found in printer config");
                    config.is_configured = false;
                }

                if (on_success) {
                    on_success(config);
                }
            } catch (const std::exception& e) {
                spdlog::error("[Moonraker API] Failed to parse input shaper config: {}", e.what());
                if (on_error) {
                    MoonrakerError err;
                    err.type = MoonrakerErrorType::UNKNOWN;
                    err.message = std::string("Failed to parse input shaper config: ") + e.what();
                    on_error(err);
                }
            }
        },
        on_error);
}

// Aliases for json_utils.h helpers
using helix::json::safe_float;
using helix::json::safe_int;
using helix::json::safe_string;

// Helper to parse a Spoolman spool JSON object into SpoolInfo
static SpoolInfo parse_spool_info(const nlohmann::json& spool_json) {
    SpoolInfo info;

    info.id = spool_json.value("id", 0);
    info.remaining_weight_g = spool_json.value("remaining_weight", 0.0);
    info.initial_weight_g = spool_json.value("initial_weight", 0.0);
    info.spool_weight_g = spool_json.value("spool_weight", 0.0);
    info.price = spool_json.value("price", 0.0);
    info.lot_nr = safe_string(spool_json, "lot_nr");
    info.comment = safe_string(spool_json, "comment");

    // Length is in mm from Spoolman, convert to meters
    double remaining_length_mm = spool_json.value("remaining_length", 0.0);
    info.remaining_length_m = remaining_length_mm / 1000.0;

    // Parse nested filament object
    if (spool_json.contains("filament") && spool_json["filament"].is_object()) {
        const auto& filament = spool_json["filament"];

        info.filament_id = filament.value("id", 0);
        info.material = safe_string(filament, "material");
        info.color_name = safe_string(filament, "name");
        info.color_hex = safe_string(filament, "color_hex");
        info.multi_color_hexes = safe_string(filament, "multi_color_hexes");

        // Temperature settings
        info.nozzle_temp_recommended = filament.value("settings_extruder_temp", 0);
        info.bed_temp_recommended = filament.value("settings_bed_temp", 0);

        // Nested vendor
        if (filament.contains("vendor") && filament["vendor"].is_object()) {
            info.vendor = safe_string(filament["vendor"], "name");
        }
    }

    return info;
}

void MoonrakerAPI::get_spoolman_status(std::function<void(bool, int)> on_success,
                                       ErrorCallback on_error) {
    spdlog::debug("[Moonraker API] get_spoolman_status()");

    client_.send_jsonrpc(
        "server.spoolman.status", json::object(),
        [on_success](json response) {
            bool connected = false;
            int active_spool_id = 0;

            if (response.contains("result")) {
                const auto& result = response["result"];
                connected = result.value("spoolman_connected", false);
                if (result.contains("spool_id") && !result["spool_id"].is_null()) {
                    active_spool_id = result["spool_id"].get<int>();
                }
            }

            spdlog::debug("[Moonraker API] Spoolman status: connected={}, active_spool={}",
                          connected, active_spool_id);

            if (on_success) {
                on_success(connected, active_spool_id);
            }
        },
        on_error);
}

void MoonrakerAPI::get_spoolman_spools(SpoolListCallback on_success, ErrorCallback on_error) {
    spdlog::debug("[Moonraker API] get_spoolman_spools()");

    // Use Moonraker's Spoolman proxy to GET /v1/spool
    json params;
    params["request_method"] = "GET";
    params["path"] = "/v1/spool";

    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success](json response) {
            std::vector<SpoolInfo> spools;

            // The proxy returns the Spoolman response in "result"
            if (response.contains("result") && response["result"].is_array()) {
                for (const auto& spool_json : response["result"]) {
                    spools.push_back(parse_spool_info(spool_json));
                }
            }

            spdlog::debug("[Moonraker API] Got {} spools from Spoolman", spools.size());

            if (on_success) {
                on_success(spools);
            }
        },
        on_error);
}

void MoonrakerAPI::get_spoolman_spool(int spool_id, SpoolCallback on_success,
                                      ErrorCallback on_error) {
    spdlog::debug("[Moonraker API] get_spoolman_spool({})", spool_id);

    // Use Moonraker's Spoolman proxy to GET /v1/spool/{id}
    json params;
    params["request_method"] = "GET";
    params["path"] = "/v1/spool/" + std::to_string(spool_id);

    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success, spool_id](json response) {
            if (response.contains("result") && response["result"].is_object()) {
                SpoolInfo spool = parse_spool_info(response["result"]);
                spdlog::debug("[Moonraker API] Got spool {}: {} {}", spool_id, spool.vendor,
                              spool.material);
                if (on_success) {
                    on_success(spool);
                }
            } else {
                spdlog::debug("[Moonraker API] Spool {} not found", spool_id);
                if (on_success) {
                    on_success(std::nullopt);
                }
            }
        },
        on_error);
}

void MoonrakerAPI::set_active_spool(int spool_id, SuccessCallback on_success,
                                    ErrorCallback on_error) {
    spdlog::info("[Moonraker API] set_active_spool({})", spool_id);

    // POST to server.spoolman.post_spool_id
    json params;
    params["spool_id"] = spool_id;

    client_.send_jsonrpc(
        "server.spoolman.post_spool_id", params,
        [on_success, spool_id](json /*response*/) {
            spdlog::debug("[Moonraker API] Active spool set to {}", spool_id);
            if (on_success) {
                on_success();
            }
        },
        on_error);
}

void MoonrakerAPI::get_spool_usage_history(
    int /*spool_id*/, std::function<void(const std::vector<FilamentUsageRecord>&)> /*on_success*/,
    ErrorCallback on_error) {
    spdlog::warn("[Moonraker API] get_spool_usage_history() not yet implemented");
    if (on_error) {
        MoonrakerError err;
        err.type = MoonrakerErrorType::UNKNOWN;
        err.message = "Spoolman usage history not yet implemented";
        on_error(err);
    }
}

void MoonrakerAPI::update_spoolman_spool_weight(int spool_id, double remaining_weight_g,
                                                SuccessCallback on_success,
                                                ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Updating spool {} remaining weight to {:.1f}g", spool_id,
                 remaining_weight_g);

    // Build the Spoolman proxy request
    // Method: server.spoolman.proxy
    // Params: { request_method: "PATCH", path: "/v1/spool/{id}", body: {...} }
    nlohmann::json body;
    body["remaining_weight"] = remaining_weight_g;

    nlohmann::json params;
    params["request_method"] = "PATCH";
    params["path"] = "/v1/spool/" + std::to_string(spool_id);
    params["body"] = body;

    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success, spool_id](json /*response*/) {
            spdlog::debug("[Moonraker API] Spool {} weight updated successfully", spool_id);
            if (on_success) {
                on_success();
            }
        },
        on_error);
}

void MoonrakerAPI::update_spoolman_spool(int spool_id, const nlohmann::json& spool_data,
                                         SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Updating spool {} with {} fields", spool_id, spool_data.size());

    nlohmann::json params;
    params["request_method"] = "PATCH";
    params["path"] = "/v1/spool/" + std::to_string(spool_id);
    params["body"] = spool_data;

    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success, spool_id](json /*response*/) {
            spdlog::debug("[Moonraker API] Spool {} updated successfully", spool_id);
            if (on_success) {
                on_success();
            }
        },
        on_error);
}

void MoonrakerAPI::update_spoolman_filament(int filament_id, const nlohmann::json& filament_data,
                                            SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Updating filament {} with {} fields", filament_id,
                 filament_data.size());

    nlohmann::json params;
    params["request_method"] = "PATCH";
    params["path"] = "/v1/filament/" + std::to_string(filament_id);
    params["body"] = filament_data;

    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success, filament_id](json /*response*/) {
            spdlog::debug("[Moonraker API] Filament {} updated successfully", filament_id);
            if (on_success) {
                on_success();
            }
        },
        on_error);
}

void MoonrakerAPI::update_spoolman_filament_color(int filament_id, const std::string& color_hex,
                                                  SuccessCallback on_success,
                                                  ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Updating filament {} color to {}", filament_id, color_hex);

    // Build the Spoolman proxy request for filament update
    nlohmann::json body;
    body["color_hex"] = color_hex;

    nlohmann::json params;
    params["request_method"] = "PATCH";
    params["path"] = "/v1/filament/" + std::to_string(filament_id);
    params["body"] = body;

    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success, filament_id, color_hex](json /*response*/) {
            spdlog::debug("[Moonraker API] Filament {} color updated to {}", filament_id,
                          color_hex);
            if (on_success) {
                on_success();
            }
        },
        on_error);
}

// ============================================================================
// Spoolman CRUD Operations
// ============================================================================

static VendorInfo parse_vendor_info(const nlohmann::json& vendor_json) {
    VendorInfo info;
    info.id = vendor_json.value("id", 0);
    info.name = safe_string(vendor_json, "name");
    info.url = safe_string(vendor_json, "url");
    return info;
}

static FilamentInfo parse_filament_info(const nlohmann::json& filament_json) {
    FilamentInfo info;
    info.id = safe_int(filament_json, "id", 0);
    info.material = safe_string(filament_json, "material");
    info.color_name = safe_string(filament_json, "name");
    info.color_hex = safe_string(filament_json, "color_hex");
    info.density = safe_float(filament_json, "density", 0.0f);
    info.diameter = safe_float(filament_json, "diameter", 1.75f);
    info.weight = safe_float(filament_json, "weight", 0.0f);
    info.spool_weight = safe_float(filament_json, "spool_weight", 0.0f);
    info.nozzle_temp_min = safe_int(filament_json, "settings_extruder_temp_min", 0);
    info.nozzle_temp_max = safe_int(filament_json, "settings_extruder_temp_max", 0);
    info.bed_temp_min = safe_int(filament_json, "settings_bed_temp_min", 0);
    info.bed_temp_max = safe_int(filament_json, "settings_bed_temp_max", 0);

    // Extract vendor_id from top-level field (always present in Spoolman response)
    info.vendor_id = safe_int(filament_json, "vendor_id", 0);

    // Nested vendor object (may override vendor_id, adds vendor_name)
    if (filament_json.contains("vendor") && filament_json["vendor"].is_object()) {
        info.vendor_id = filament_json["vendor"].value("id", info.vendor_id);
        info.vendor_name = safe_string(filament_json["vendor"], "name");
    }

    return info;
}

void MoonrakerAPI::get_spoolman_vendors(VendorListCallback on_success, ErrorCallback on_error) {
    spdlog::debug("[Moonraker API] get_spoolman_vendors()");

    json params;
    params["request_method"] = "GET";
    params["path"] = "/v1/vendor";

    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success](json response) {
            std::vector<VendorInfo> vendors;

            if (response.contains("result") && response["result"].is_array()) {
                for (const auto& vendor_json : response["result"]) {
                    vendors.push_back(parse_vendor_info(vendor_json));
                }
            }

            spdlog::debug("[Moonraker API] Got {} vendors from Spoolman", vendors.size());

            if (on_success) {
                on_success(vendors);
            }
        },
        on_error);
}

void MoonrakerAPI::get_spoolman_filaments(FilamentListCallback on_success, ErrorCallback on_error) {
    spdlog::debug("[Moonraker API] get_spoolman_filaments()");

    json params;
    params["request_method"] = "GET";
    params["path"] = "/v1/filament";

    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success](json response) {
            std::vector<FilamentInfo> filaments;

            if (response.contains("result") && response["result"].is_array()) {
                for (const auto& filament_json : response["result"]) {
                    filaments.push_back(parse_filament_info(filament_json));
                }
            }

            spdlog::debug("[Moonraker API] Got {} filaments from Spoolman", filaments.size());

            if (on_success) {
                on_success(filaments);
            }
        },
        on_error);
}

void MoonrakerAPI::create_spoolman_vendor(const nlohmann::json& vendor_data,
                                          VendorCreateCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Creating Spoolman vendor: {}",
                 vendor_data.value("name", "unknown"));

    json params;
    params["request_method"] = "POST";
    params["path"] = "/v1/vendor";
    params["body"] = vendor_data;

    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success, on_error](json response) {
            if (response.contains("result") && response["result"].is_object()) {
                VendorInfo vendor = parse_vendor_info(response["result"]);
                spdlog::debug("[Moonraker API] Created vendor {}: {}", vendor.id, vendor.name);
                if (on_success) {
                    on_success(vendor);
                }
            } else {
                spdlog::error("[Moonraker API] create_spoolman_vendor: unexpected response format");
                if (on_error) {
                    on_error(MoonrakerError{MoonrakerErrorType::UNKNOWN, 0,
                                            "Unexpected response format",
                                            "create_spoolman_vendor"});
                }
            }
        },
        on_error);
}

void MoonrakerAPI::create_spoolman_filament(const nlohmann::json& filament_data,
                                            FilamentCreateCallback on_success,
                                            ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Creating Spoolman filament: {} {}",
                 filament_data.value("material", "?"), filament_data.value("name", "?"));

    json params;
    params["request_method"] = "POST";
    params["path"] = "/v1/filament";
    params["body"] = filament_data;

    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success, on_error](json response) {
            if (response.contains("result") && response["result"].is_object()) {
                FilamentInfo filament = parse_filament_info(response["result"]);
                spdlog::debug("[Moonraker API] Created filament {}: {}", filament.id,
                              filament.display_name());
                if (on_success) {
                    on_success(filament);
                }
            } else {
                spdlog::error(
                    "[Moonraker API] create_spoolman_filament: unexpected response format");
                if (on_error) {
                    on_error(MoonrakerError{MoonrakerErrorType::UNKNOWN, 0,
                                            "Unexpected response format",
                                            "create_spoolman_filament"});
                }
            }
        },
        on_error);
}

void MoonrakerAPI::create_spoolman_spool(const nlohmann::json& spool_data,
                                         SpoolCreateCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Creating Spoolman spool");

    json params;
    params["request_method"] = "POST";
    params["path"] = "/v1/spool";
    params["body"] = spool_data;

    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success, on_error](json response) {
            if (response.contains("result") && response["result"].is_object()) {
                SpoolInfo spool = parse_spool_info(response["result"]);
                spdlog::debug("[Moonraker API] Created spool {}: {}", spool.id,
                              spool.display_name());
                if (on_success) {
                    on_success(spool);
                }
            } else {
                spdlog::error("[Moonraker API] create_spoolman_spool: unexpected response format");
                if (on_error) {
                    on_error(MoonrakerError{MoonrakerErrorType::UNKNOWN, 0,
                                            "Unexpected response format", "create_spoolman_spool"});
                }
            }
        },
        on_error);
}

void MoonrakerAPI::delete_spoolman_spool(int spool_id, SuccessCallback on_success,
                                         ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Deleting Spoolman spool {}", spool_id);

    json params;
    params["request_method"] = "DELETE";
    params["path"] = "/v1/spool/" + std::to_string(spool_id);

    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success, spool_id](json /*response*/) {
            spdlog::debug("[Moonraker API] Spool {} deleted successfully", spool_id);
            if (on_success) {
                on_success();
            }
        },
        on_error);
}

void MoonrakerAPI::get_spoolman_external_vendors(VendorListCallback on_success,
                                                 ErrorCallback on_error) {
    spdlog::debug("[Moonraker API] get_spoolman_external_vendors()");

    json params;
    params["request_method"] = "GET";
    params["path"] = "/v1/external/vendor";

    // Silent: /v1/external/ endpoints require SpoolmanDB integration which
    // is not available on all Spoolman versions (e.g. v0.22.x)
    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success](json response) {
            std::vector<VendorInfo> vendors;

            if (response.contains("result") && response["result"].is_array()) {
                for (const auto& vendor_json : response["result"]) {
                    vendors.push_back(parse_vendor_info(vendor_json));
                }
            }

            spdlog::debug("[Moonraker API] Got {} external vendors from SpoolmanDB",
                          vendors.size());

            if (on_success) {
                on_success(vendors);
            }
        },
        on_error, 0, /*silent=*/true);
}

void MoonrakerAPI::get_spoolman_external_filaments(const std::string& vendor_name,
                                                   FilamentListCallback on_success,
                                                   ErrorCallback on_error) {
    spdlog::debug("[Moonraker API] get_spoolman_external_filaments(vendor={})", vendor_name);

    // URL-encode the vendor name
    std::string encoded;
    for (char c : vendor_name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += "%20";
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
            encoded += buf;
        }
    }

    json params;
    params["request_method"] = "GET";
    params["path"] = "/v1/external/filament?vendor_name=" + encoded;

    // Silent: /v1/external/ endpoints require SpoolmanDB integration which
    // is not available on all Spoolman versions (e.g. v0.22.x)
    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success, vendor_name](json response) {
            std::vector<FilamentInfo> filaments;

            if (response.contains("result") && response["result"].is_array()) {
                for (const auto& filament_json : response["result"]) {
                    filaments.push_back(parse_filament_info(filament_json));
                }
            }

            spdlog::debug("[Moonraker API] Got {} external filaments for vendor '{}'",
                          filaments.size(), vendor_name);

            if (on_success) {
                on_success(filaments);
            }
        },
        on_error, 0, /*silent=*/true);
}

void MoonrakerAPI::get_spoolman_filaments(int vendor_id, FilamentListCallback on_success,
                                          ErrorCallback on_error) {
    spdlog::debug("[Moonraker API] get_spoolman_filaments(vendor_id={})", vendor_id);

    json params;
    params["request_method"] = "GET";
    params["path"] = "/v1/filament?vendor.id=" + std::to_string(vendor_id);

    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success, vendor_id](json response) {
            std::vector<FilamentInfo> filaments;

            if (response.contains("result") && response["result"].is_array()) {
                for (const auto& filament_json : response["result"]) {
                    filaments.push_back(parse_filament_info(filament_json));
                }
            }

            spdlog::debug("[Moonraker API] Got {} filaments for vendor_id {}", filaments.size(),
                          vendor_id);

            if (on_success) {
                on_success(filaments);
            }
        },
        on_error);
}

void MoonrakerAPI::delete_spoolman_vendor(int vendor_id, SuccessCallback on_success,
                                          ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Deleting Spoolman vendor {}", vendor_id);

    json params;
    params["request_method"] = "DELETE";
    params["path"] = "/v1/vendor/" + std::to_string(vendor_id);

    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success, vendor_id](json /*response*/) {
            spdlog::debug("[Moonraker API] Vendor {} deleted successfully", vendor_id);
            if (on_success) {
                on_success();
            }
        },
        on_error);
}

void MoonrakerAPI::delete_spoolman_filament(int filament_id, SuccessCallback on_success,
                                            ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Deleting Spoolman filament {}", filament_id);

    json params;
    params["request_method"] = "DELETE";
    params["path"] = "/v1/filament/" + std::to_string(filament_id);

    client_.send_jsonrpc(
        "server.spoolman.proxy", params,
        [on_success, filament_id](json /*response*/) {
            spdlog::debug("[Moonraker API] Filament {} deleted successfully", filament_id);
            if (on_success) {
                on_success();
            }
        },
        on_error);
}

void MoonrakerAPI::get_machine_limits(MachineLimitsCallback on_success, ErrorCallback on_error) {
    spdlog::debug("[Moonraker API] Querying machine limits from toolhead");

    // Query toolhead object for current velocity/acceleration limits
    json params = {{"objects", {{"toolhead", nullptr}}}};

    client_.send_jsonrpc(
        "printer.objects.query", params,
        [on_success, on_error](json response) {
            try {
                if (!response.contains("result") || !response["result"].contains("status") ||
                    !response["result"]["status"].contains("toolhead")) {
                    spdlog::warn("[Moonraker API] Toolhead object not available in response");
                    if (on_error) {
                        MoonrakerError err;
                        err.type = MoonrakerErrorType::UNKNOWN;
                        err.message = "Toolhead object not available";
                        on_error(err);
                    }
                    return;
                }

                const auto& toolhead = response["result"]["status"]["toolhead"];
                MachineLimits limits;

                // Extract limits with safe defaults
                limits.max_velocity = toolhead.value("max_velocity", 0.0);
                limits.max_accel = toolhead.value("max_accel", 0.0);
                limits.max_accel_to_decel = toolhead.value("max_accel_to_decel", 0.0);
                limits.square_corner_velocity = toolhead.value("square_corner_velocity", 0.0);
                limits.max_z_velocity = toolhead.value("max_z_velocity", 0.0);
                limits.max_z_accel = toolhead.value("max_z_accel", 0.0);

                spdlog::info("[Moonraker API] Machine limits: vel={:.0f} accel={:.0f} "
                             "accel_to_decel={:.0f} scv={:.1f} z_vel={:.0f} z_accel={:.0f}",
                             limits.max_velocity, limits.max_accel, limits.max_accel_to_decel,
                             limits.square_corner_velocity, limits.max_z_velocity,
                             limits.max_z_accel);

                if (on_success) {
                    on_success(limits);
                }
            } catch (const std::exception& e) {
                spdlog::error("[Moonraker API] Failed to parse machine limits: {}", e.what());
                if (on_error) {
                    MoonrakerError err;
                    err.type = MoonrakerErrorType::UNKNOWN;
                    err.message = std::string("Failed to parse machine limits: ") + e.what();
                    on_error(err);
                }
            }
        },
        on_error);
}

void MoonrakerAPI::set_machine_limits(const MachineLimits& limits, SuccessCallback on_success,
                                      ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Setting machine limits");

    // Warn about Z limits that cannot be set at runtime
    if (limits.max_z_velocity > 0 || limits.max_z_accel > 0) {
        spdlog::warn("[Moonraker API] max_z_velocity and max_z_accel cannot be set "
                     "via SET_VELOCITY_LIMIT - they require config changes");
    }

    // Build SET_VELOCITY_LIMIT command with only non-zero parameters
    // Use fixed precision to avoid floating point representation issues
    std::ostringstream cmd;
    cmd << std::fixed << std::setprecision(1);
    cmd << "SET_VELOCITY_LIMIT";

    bool has_params = false;

    if (limits.max_velocity > 0) {
        cmd << " VELOCITY=" << limits.max_velocity;
        has_params = true;
    }
    if (limits.max_accel > 0) {
        cmd << " ACCEL=" << limits.max_accel;
        has_params = true;
    }
    if (limits.max_accel_to_decel > 0) {
        cmd << " ACCEL_TO_DECEL=" << limits.max_accel_to_decel;
        has_params = true;
    }
    if (limits.square_corner_velocity > 0) {
        cmd << " SQUARE_CORNER_VELOCITY=" << limits.square_corner_velocity;
        has_params = true;
    }

    if (!has_params) {
        spdlog::warn("[Moonraker API] set_machine_limits called with no valid parameters");
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "No valid machine limit parameters provided";
            on_error(err);
        }
        return;
    }

    spdlog::debug("[Moonraker API] Executing: {}", cmd.str());
    execute_gcode(cmd.str(), on_success, on_error);
}

void MoonrakerAPI::save_config(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[MoonrakerAPI] Sending SAVE_CONFIG");
    execute_gcode("SAVE_CONFIG", std::move(on_success), std::move(on_error));
}

void MoonrakerAPI::execute_macro(const std::string& name,
                                 const std::map<std::string, std::string>& params,
                                 SuccessCallback on_success, ErrorCallback on_error) {
    // Validate macro name - only allow alphanumeric, underscore (standard Klipper macro names)
    if (name.empty()) {
        spdlog::error("[Moonraker API] execute_macro() called with empty name");
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Macro name cannot be empty";
            err.method = "execute_macro";
            on_error(err);
        }
        return;
    }

    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            spdlog::error("[Moonraker API] Invalid macro name '{}' contains illegal character '{}'",
                          name, c);
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::VALIDATION_ERROR;
                err.message = "Macro name contains illegal characters";
                err.method = "execute_macro";
                on_error(err);
            }
            return;
        }
    }

    // Build G-code: MACRO_NAME KEY1=value1 KEY2=value2
    std::ostringstream gcode;
    gcode << name;

    for (const auto& [key, value] : params) {
        // Validate param key - only alphanumeric and underscore
        bool key_valid = !key.empty();
        for (char c : key) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
                key_valid = false;
                break;
            }
        }
        if (!key_valid) {
            spdlog::warn("[Moonraker API] Skipping invalid param key '{}'", key);
            continue;
        }

        // Validate param value - reject dangerous characters that could enable G-code injection
        // Allow: alphanumeric, underscore, hyphen, dot, space (for human-readable values)
        bool value_valid = true;
        for (char c : value) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.' &&
                c != ' ') {
                value_valid = false;
                break;
            }
        }
        if (!value_valid) {
            spdlog::warn("[Moonraker API] Skipping param with unsafe value: {}={}", key, value);
            continue;
        }

        // Safe to include - quote if it has spaces
        if (value.find(' ') != std::string::npos) {
            gcode << " " << key << "=\"" << value << "\"";
        } else {
            gcode << " " << key << "=" << value;
        }
    }

    std::string gcode_str = gcode.str();
    spdlog::debug("[Moonraker API] Executing macro: {}", gcode_str);

    execute_gcode(gcode_str, std::move(on_success), std::move(on_error));
}

std::vector<MacroInfo> MoonrakerAPI::get_user_macros(bool /*include_system*/) const {
    spdlog::warn("[Moonraker API] get_user_macros() not yet implemented");
    return {};
}

// ============================================================================
// Advanced Panel Operations - PID Calibration
// ============================================================================

void MoonrakerAPI::get_heater_pid_values(const std::string& heater,
                                         MoonrakerAPI::PIDCalibrateCallback on_complete,
                                         MoonrakerAPI::ErrorCallback on_error) {
    json params = {{"objects", json::object({{"configfile", json::array({"settings"})}})}};

    client_.send_jsonrpc(
        "printer.objects.query", params,
        [heater, on_complete, on_error](json response) {
            try {
                if (!response.contains("result") || !response["result"].contains("status") ||
                    !response["result"]["status"].contains("configfile") ||
                    !response["result"]["status"]["configfile"].contains("settings")) {
                    spdlog::debug("[Moonraker API] configfile.settings not available in response");
                    if (on_error) {
                        on_error(MoonrakerError{MoonrakerErrorType::UNKNOWN, 0,
                                                "configfile.settings not available",
                                                "get_pid_values"});
                    }
                    return;
                }

                const json& settings = response["result"]["status"]["configfile"]["settings"];

                if (!settings.contains(heater)) {
                    if (on_error) {
                        on_error(MoonrakerError{MoonrakerErrorType::UNKNOWN, 0,
                                                "Heater '" + heater + "' not in config",
                                                "get_pid_values"});
                    }
                    return;
                }

                const json& h = settings[heater];
                if (h.contains("pid_kp") && h.contains("pid_ki") && h.contains("pid_kd")) {
                    float kp = h["pid_kp"].get<float>();
                    float ki = h["pid_ki"].get<float>();
                    float kd = h["pid_kd"].get<float>();
                    spdlog::debug(
                        "[Moonraker API] Fetched PID values for {}: Kp={:.3f} Ki={:.3f} Kd={:.3f}",
                        heater, kp, ki, kd);
                    if (on_complete) {
                        on_complete(kp, ki, kd);
                    }
                } else {
                    if (on_error) {
                        on_error(MoonrakerError{MoonrakerErrorType::UNKNOWN, 0,
                                                "No PID values for heater '" + heater + "'",
                                                "get_pid_values"});
                    }
                }
            } catch (const std::exception& ex) {
                spdlog::warn("[Moonraker API] Error parsing PID values: {}", ex.what());
                if (on_error) {
                    on_error(MoonrakerError{MoonrakerErrorType::UNKNOWN, 0,
                                            std::string("Parse error: ") + ex.what(),
                                            "get_pid_values"});
                }
            }
        },
        [on_error](const MoonrakerError& err) {
            spdlog::debug("[Moonraker API] Failed to fetch PID values: {}", err.message);
            if (on_error) {
                on_error(err);
            }
        });
}

void MoonrakerAPI::start_pid_calibrate(const std::string& heater, int target_temp,
                                       MoonrakerAPI::PIDCalibrateCallback on_complete,
                                       ErrorCallback on_error, PIDProgressCallback on_progress) {
    spdlog::info("[MoonrakerAPI] Starting PID calibration for {} at {}°C", heater, target_temp);

    auto collector = std::make_shared<PIDCalibrateCollector>(client_, std::move(on_complete),
                                                             on_error, std::move(on_progress));
    collector->start();

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "PID_CALIBRATE HEATER=%s TARGET=%d", heater.c_str(), target_temp);

    // PID calibration takes 3-10 minutes — use 15 min timeout
    static constexpr uint32_t PID_TIMEOUT_MS = 15 * 60 * 1000;

    // silent=true: PID errors are handled by the collector and UI panel, not global toast
    execute_gcode(
        cmd, nullptr,
        [collector, on_error](const MoonrakerError& err) {
            spdlog::error("[MoonrakerAPI] Failed to send PID_CALIBRATE: {}", err.message);
            collector->mark_completed();
            collector->unregister();
            if (on_error)
                on_error(err);
        },
        PID_TIMEOUT_MS, true);
}

// ============================================================================
