// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_error_reporting.h"
#include "ui_notification.h"

#include "moonraker_api.h"
#include "moonraker_api_internal.h"
#include "spdlog/spdlog.h"

#include <iomanip>
#include <memory>
#include <regex>
#include <set>
#include <sstream>

using namespace moonraker_internal;

// ============================================================================
// Domain Service Operations - Bed Mesh
// ============================================================================

const BedMeshProfile* MoonrakerAPI::get_active_bed_mesh() const {
    // Suppress deprecation warning - we're the migration target
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    const BedMeshProfile& mesh = client_.get_active_bed_mesh();
#pragma GCC diagnostic pop

    if (mesh.probed_matrix.empty()) {
        return nullptr;
    }
    return &mesh;
}

std::vector<std::string> MoonrakerAPI::get_bed_mesh_profiles() const {
    // Suppress deprecation warning - we're the migration target
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    return client_.get_bed_mesh_profiles();
#pragma GCC diagnostic pop
}

bool MoonrakerAPI::has_bed_mesh() const {
    // Suppress deprecation warning - we're the migration target
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    return client_.has_bed_mesh();
#pragma GCC diagnostic pop
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

void MoonrakerAPI::start_bed_mesh_calibrate(const std::string& /*profile_name*/,
                                            SuccessCallback /*on_success*/,
                                            ErrorCallback on_error) {
    spdlog::warn("[Moonraker API] start_bed_mesh_calibrate() not yet implemented");
    if (on_error) {
        MoonrakerError err;
        err.type = MoonrakerErrorType::UNKNOWN;
        err.message = "Bed mesh calibration not yet implemented";
        on_error(err);
    }
}

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
 *   Fitted shaper 'zv' frequency = 35.8 Hz (vibrations = 22.7%, smoothing ~= 0.100)
 *   Fitted shaper 'mzv' frequency = 36.7 Hz (vibrations = 7.2%, smoothing ~= 0.140)
 *   ...
 *   Recommended shaper is mzv @ 36.7 Hz
 */
class InputShaperCollector : public std::enable_shared_from_this<InputShaperCollector> {
  public:
    InputShaperCollector(MoonrakerClient& client, char axis, InputShaperCallback on_success,
                         MoonrakerAPI::ErrorCallback on_error)
        : client_(client), axis_(axis), on_success_(std::move(on_success)),
          on_error_(std::move(on_error)) {}

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

        // Check for unknown command error
        if (line.find("Unknown command") != std::string::npos &&
            line.find("SHAPER_CALIBRATE") != std::string::npos) {
            complete_error(
                "SHAPER_CALIBRATE requires [resonance_tester] and ADXL345 in printer.cfg");
            return;
        }

        // Parse shaper fit lines
        // Format: "Fitted shaper 'mzv' frequency = 36.7 Hz (vibrations = 7.2%, smoothing ~= 0.140)"
        if (line.find("Fitted shaper") != std::string::npos) {
            parse_shaper_line(line);
        }

        // Parse recommendation line
        // Format: "Recommended shaper is mzv @ 36.7 Hz"
        if (line.find("Recommended shaper") != std::string::npos) {
            parse_recommendation(line);
            // Recommendation marks completion
            complete_success();
            return;
        }

        // Error detection - be specific to avoid false positives
        if (line.rfind("!! ", 0) == 0 ||                // Klipper emergency errors
            line.rfind("Error: ", 0) == 0 ||            // Standard errors
            line.find("error:") != std::string::npos) { // Python traceback
            complete_error(line);
        }
    }

  private:
    void parse_shaper_line(const std::string& line) {
        // Static regex for performance
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
        }
    }

    void parse_recommendation(const std::string& line) {
        static const std::regex rec_regex(R"(Recommended shaper is (\w+) @ ([\d.]+) Hz)");

        std::smatch match;
        if (std::regex_search(line, match, rec_regex) && match.size() == 3) {
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

    void complete_success() {
        if (completed_.exchange(true)) {
            return; // Already completed
        }

        spdlog::info("[InputShaperCollector] Complete with {} shaper options", shaper_fits_.size());
        unregister();

        if (on_success_) {
            // Build the result
            InputShaperResult result;
            result.axis = axis_;
            result.shaper_type = recommended_type_;
            result.shaper_freq = recommended_freq_;

            // Find the recommended shaper's details
            for (const auto& fit : shaper_fits_) {
                if (fit.type == recommended_type_) {
                    result.smoothing = fit.smoothing;
                    result.vibrations = fit.vibrations;
                    break;
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
    };

    MoonrakerClient& client_;
    char axis_;
    InputShaperCallback on_success_;
    MoonrakerAPI::ErrorCallback on_error_;
    std::string handler_name_;
    std::atomic<bool> registered_{false};
    std::atomic<bool> completed_{false};

    std::vector<ShaperFitData> shaper_fits_;
    std::string recommended_type_;
    float recommended_freq_ = 0.0f;
};

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

void MoonrakerAPI::start_resonance_test(char axis, AdvancedProgressCallback /*on_progress*/,
                                        InputShaperCallback on_complete, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Starting SHAPER_CALIBRATE AXIS={}", axis);

    // Create collector to handle async response parsing
    auto collector = std::make_shared<InputShaperCollector>(client_, axis, on_complete, on_error);
    collector->start();

    // Send the G-code command
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
        });
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

// Helper to parse a Spoolman spool JSON object into SpoolInfo
static SpoolInfo parse_spool_info(const nlohmann::json& spool_json) {
    SpoolInfo info;

    info.id = spool_json.value("id", 0);
    info.remaining_weight_g = spool_json.value("remaining_weight", 0.0);
    info.initial_weight_g = spool_json.value("initial_weight", 0.0);
    info.spool_weight_g = spool_json.value("spool_weight", 0.0);

    // Length is in mm from Spoolman, convert to meters
    double remaining_length_mm = spool_json.value("remaining_length", 0.0);
    info.remaining_length_m = remaining_length_mm / 1000.0;

    // Parse nested filament object
    if (spool_json.contains("filament") && spool_json["filament"].is_object()) {
        const auto& filament = spool_json["filament"];

        info.material = filament.value("material", "");
        info.color_name = filament.value("name", "");
        info.color_hex = filament.value("color_hex", "");
        info.multi_color_hexes = filament.value("multi_color_hexes", "");

        // Temperature settings
        info.nozzle_temp_recommended = filament.value("settings_extruder_temp", 0);
        info.bed_temp_recommended = filament.value("settings_bed_temp", 0);

        // Nested vendor
        if (filament.contains("vendor") && filament["vendor"].is_object()) {
            info.vendor = filament["vendor"].value("name", "");
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
                active_spool_id = result.value("spool_id", 0);
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

void MoonrakerAPI::save_config(SuccessCallback /*on_success*/, ErrorCallback on_error) {
    spdlog::warn("[Moonraker API] save_config() not yet implemented");
    if (on_error) {
        MoonrakerError err;
        err.type = MoonrakerErrorType::UNKNOWN;
        err.message = "Save config not yet implemented";
        on_error(err);
    }
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
