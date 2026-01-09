// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "printer_detector.h"

#include "ui_error_reporting.h"

#include "print_start_analyzer.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_set>

#ifdef __APPLE__
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif

#include "hv/json.hpp"

using json = nlohmann::json;

// ============================================================================
// JSON Database Loader with User Extensions Support
// ============================================================================

namespace {

/**
 * @brief Extensible printer database with user override support
 *
 * Loads printer definitions from:
 * 1. Bundled database: config/printer_database.json
 * 2. User extensions: config/printer_database.d/ *.json (higher priority)
 *
 * User definitions can:
 * - Add new printers (unique ID)
 * - Override bundled printers (same ID replaces bundled)
 * - Disable bundled printers ("enabled": false)
 */
struct PrinterDatabase {
    json data;
    bool loaded = false;
    std::vector<std::string> loaded_files;
    std::vector<std::string> load_errors;
    int user_overrides = 0;
    int user_additions = 0;

    bool load() {
        if (loaded)
            return true;

        // Phase 1: Load bundled database
        try {
            std::ifstream file("config/printer_database.json");
            if (!file.is_open()) {
                NOTIFY_ERROR("Could not load printer database");
                LOG_ERROR_INTERNAL("[PrinterDetector] Failed to open config/printer_database.json");
                return false;
            }

            data = json::parse(file);
            loaded_files.push_back("config/printer_database.json");
            spdlog::info("[PrinterDetector] Loaded bundled printer database version {}",
                         data.value("version", "unknown"));
        } catch (const std::exception& e) {
            NOTIFY_ERROR("Printer database format error");
            LOG_ERROR_INTERNAL("[PrinterDetector] Failed to parse printer database: {}", e.what());
            return false;
        }

        // Phase 2: Merge user extensions from config/printer_database.d/
        merge_user_extensions();

        loaded = true;
        return true;
    }

    void reload() {
        loaded = false;
        loaded_files.clear();
        load_errors.clear();
        user_overrides = 0;
        user_additions = 0;
        data = json();
        load();
    }

  private:
    void merge_user_extensions() {
        const std::string extensions_dir = "config/printer_database.d";

        // Check if extensions directory exists
        if (!fs::exists(extensions_dir) || !fs::is_directory(extensions_dir)) {
            spdlog::debug("[PrinterDetector] No user extensions directory at {}", extensions_dir);
            return;
        }

        // Build index of bundled printers by ID for fast lookup
        std::map<std::string, size_t> bundled_index;
        if (data.contains("printers") && data["printers"].is_array()) {
            for (size_t i = 0; i < data["printers"].size(); ++i) {
                std::string id = data["printers"][i].value("id", "");
                if (!id.empty()) {
                    bundled_index[id] = i;
                }
            }
        }

        // Scan for JSON files in extensions directory
        std::vector<std::string> extension_files;
        try {
            for (const auto& entry : fs::directory_iterator(extensions_dir)) {
                if (entry.path().extension() == ".json") {
                    extension_files.push_back(entry.path().string());
                }
            }
        } catch (const std::exception& e) {
            load_errors.push_back(fmt::format("Failed to scan {}: {}", extensions_dir, e.what()));
            spdlog::warn("[PrinterDetector] {}", load_errors.back());
            return;
        }

        // Sort for consistent ordering
        std::sort(extension_files.begin(), extension_files.end());

        // Process each extension file
        for (const auto& file_path : extension_files) {
            merge_extension_file(file_path, bundled_index);
        }

        if (user_overrides > 0 || user_additions > 0) {
            spdlog::info("[PrinterDetector] User extensions: {} overrides, {} additions",
                         user_overrides, user_additions);
        }
    }

    void merge_extension_file(const std::string& file_path,
                              std::map<std::string, size_t>& bundled_index) {
        try {
            std::ifstream file(file_path);
            if (!file.is_open()) {
                load_errors.push_back(fmt::format("Could not open {}", file_path));
                spdlog::warn("[PrinterDetector] {}", load_errors.back());
                return;
            }

            json extension_data = json::parse(file);
            loaded_files.push_back(file_path);

            // Validate structure
            if (!extension_data.contains("printers") || !extension_data["printers"].is_array()) {
                load_errors.push_back(fmt::format("{}: missing 'printers' array", file_path));
                spdlog::warn("[PrinterDetector] {}", load_errors.back());
                return;
            }

            // Process each printer in the extension
            for (const auto& printer : extension_data["printers"]) {
                std::string id = printer.value("id", "");
                if (id.empty()) {
                    load_errors.push_back(fmt::format("{}: printer missing 'id' field", file_path));
                    spdlog::warn("[PrinterDetector] {}", load_errors.back());
                    continue;
                }

                // Check if printer is disabled
                bool enabled = printer.value("enabled", true);

                // Check if this overrides a bundled printer
                auto it = bundled_index.find(id);
                if (it != bundled_index.end()) {
                    // Override bundled printer
                    if (!enabled) {
                        // Mark as disabled (will be filtered out in roller)
                        data["printers"][it->second]["enabled"] = false;
                        spdlog::debug("[PrinterDetector] Disabled bundled printer '{}'", id);
                    } else {
                        // Replace bundled definition
                        data["printers"][it->second] = printer;
                        spdlog::debug("[PrinterDetector] User override for '{}'", id);
                    }
                    user_overrides++;
                } else {
                    // Add new printer
                    if (enabled) {
                        // Validate required fields for new printers
                        std::string name = printer.value("name", "");
                        if (name.empty()) {
                            load_errors.push_back(fmt::format(
                                "{}: printer '{}' missing 'name' field", file_path, id));
                            spdlog::warn("[PrinterDetector] {}", load_errors.back());
                            continue;
                        }

                        data["printers"].push_back(printer);
                        bundled_index[id] = data["printers"].size() - 1;
                        spdlog::debug("[PrinterDetector] Added user printer '{}'", name);
                        user_additions++;
                    }
                }
            }

            spdlog::debug("[PrinterDetector] Processed extension file: {}", file_path);

        } catch (const json::parse_error& e) {
            load_errors.push_back(fmt::format("{}: JSON parse error: {}", file_path, e.what()));
            spdlog::warn("[PrinterDetector] {}", load_errors.back());
        } catch (const std::exception& e) {
            load_errors.push_back(fmt::format("{}: {}", file_path, e.what()));
            spdlog::warn("[PrinterDetector] {}", load_errors.back());
        }
    }
};

PrinterDatabase g_database;
} // namespace

// ============================================================================
// Helper Functions
// ============================================================================

namespace {
// Case-insensitive substring search
bool has_pattern(const std::vector<std::string>& objects, const std::string& pattern) {
    std::string pattern_lower = pattern;
    std::transform(pattern_lower.begin(), pattern_lower.end(), pattern_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return std::any_of(objects.begin(), objects.end(), [&pattern_lower](const std::string& obj) {
        std::string obj_lower = obj;
        std::transform(obj_lower.begin(), obj_lower.end(), obj_lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return obj_lower.find(pattern_lower) != std::string::npos;
    });
}

// Check if all patterns in array are present
bool has_all_patterns(const std::vector<std::string>& objects, const json& patterns) {
    for (const auto& pattern : patterns) {
        if (!has_pattern(objects, pattern.get<std::string>())) {
            return false;
        }
    }
    return true;
}

// Get field data from hardware based on field name
// Returns a vector by value for string fields, reference for vector fields
std::vector<std::string> get_field_data(const PrinterHardwareData& hardware,
                                        const std::string& field) {
    if (field == "sensors")
        return hardware.sensors;
    if (field == "fans")
        return hardware.fans;
    if (field == "heaters")
        return hardware.heaters;
    if (field == "leds")
        return hardware.leds;
    if (field == "printer_objects")
        return hardware.printer_objects;
    if (field == "steppers")
        return hardware.steppers;
    if (field == "hostname")
        return {hardware.hostname};
    if (field == "kinematics")
        return {hardware.kinematics};
    if (field == "mcu")
        return {hardware.mcu};

    // Unknown field - return empty vector
    return {};
}

// Count Z steppers in the steppers list
int count_z_steppers(const std::vector<std::string>& steppers) {
    int count = 0;
    for (const auto& stepper : steppers) {
        std::string stepper_lower = stepper;
        std::transform(stepper_lower.begin(), stepper_lower.end(), stepper_lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        // Match stepper_z, stepper_z1, stepper_z2, stepper_z3 patterns
        if (stepper_lower.find("stepper_z") == 0) {
            count++;
        }
    }
    return count;
}

// Check if build volume is within specified range
bool check_build_volume_range(const BuildVolume& volume, const json& heuristic) {
    // Get the dimensions we need to check
    float x_size = volume.x_max - volume.x_min;
    float y_size = volume.y_max - volume.y_min;

    // If no volume data, can't match
    if (x_size <= 0 || y_size <= 0) {
        return false;
    }

    // Check X range
    if (heuristic.contains("min_x")) {
        float min_x = heuristic["min_x"].get<float>();
        if (x_size < min_x)
            return false;
    }
    if (heuristic.contains("max_x")) {
        float max_x = heuristic["max_x"].get<float>();
        if (x_size > max_x)
            return false;
    }

    // Check Y range
    if (heuristic.contains("min_y")) {
        float min_y = heuristic["min_y"].get<float>();
        if (y_size < min_y)
            return false;
    }
    if (heuristic.contains("max_y")) {
        float max_y = heuristic["max_y"].get<float>();
        if (y_size > max_y)
            return false;
    }

    return true;
}
} // namespace

// ============================================================================
// Heuristic Execution Engine
// ============================================================================

namespace {
// Execute a single heuristic and return confidence (0 = no match)
int execute_heuristic(const json& heuristic, const PrinterHardwareData& hardware) {
    std::string type = heuristic.value("type", "");
    std::string field = heuristic.value("field", "");
    int confidence = heuristic.value("confidence", 0);

    auto field_data = get_field_data(hardware, field);

    if (type == "sensor_match" || type == "fan_match" || type == "hostname_match" ||
        type == "led_match") {
        // Simple pattern matching in specified field
        std::string pattern = heuristic.value("pattern", "");
        if (has_pattern(field_data, pattern)) {
            spdlog::debug("[PrinterDetector] Matched {} pattern '{}' (confidence: {})", type,
                          pattern, confidence);
            return confidence;
        }
    } else if (type == "fan_combo") {
        // Multiple patterns must all be present
        if (heuristic.contains("patterns") && heuristic["patterns"].is_array()) {
            if (has_all_patterns(field_data, heuristic["patterns"])) {
                spdlog::debug("[PrinterDetector] Matched fan combo (confidence: {})", confidence);
                return confidence;
            }
        }
    } else if (type == "kinematics_match") {
        // Match against printer kinematics type (corexy, cartesian, delta, etc.)
        std::string pattern = heuristic.value("pattern", "");
        if (!hardware.kinematics.empty()) {
            std::string kinematics_lower = hardware.kinematics;
            std::transform(kinematics_lower.begin(), kinematics_lower.end(),
                           kinematics_lower.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            std::string pattern_lower = pattern;
            std::transform(pattern_lower.begin(), pattern_lower.end(), pattern_lower.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            if (kinematics_lower.find(pattern_lower) != std::string::npos) {
                spdlog::debug("[PrinterDetector] Matched kinematics '{}' (confidence: {})", pattern,
                              confidence);
                return confidence;
            }
        }
    } else if (type == "object_exists") {
        // Check if a Klipper object exists in the printer_objects list
        std::string pattern = heuristic.value("pattern", "");
        if (has_pattern(hardware.printer_objects, pattern)) {
            spdlog::debug("[PrinterDetector] Found object '{}' (confidence: {})", pattern,
                          confidence);
            return confidence;
        }
    } else if (type == "stepper_count") {
        // Count Z steppers and match against pattern (z_count_1, z_count_2, z_count_3, z_count_4)
        std::string pattern = heuristic.value("pattern", "");
        int z_count = count_z_steppers(hardware.steppers);

        // Also check for delta steppers (stepper_a, stepper_b, stepper_c)
        if (pattern == "stepper_a") {
            // Delta printer detection via stepper naming
            if (has_pattern(hardware.steppers, "stepper_a")) {
                spdlog::debug("[PrinterDetector] Found delta stepper pattern (confidence: {})",
                              confidence);
                return confidence;
            }
        } else {
            // Parse expected count from pattern (z_count_N)
            int expected_count = 0;
            if (pattern == "z_count_1")
                expected_count = 1;
            else if (pattern == "z_count_2")
                expected_count = 2;
            else if (pattern == "z_count_3")
                expected_count = 3;
            else if (pattern == "z_count_4")
                expected_count = 4;

            if (expected_count > 0 && z_count == expected_count) {
                spdlog::debug("[PrinterDetector] Matched {} Z steppers (confidence: {})", z_count,
                              confidence);
                return confidence;
            }
        }
    } else if (type == "build_volume_range") {
        // Check if build volume is within specified range
        if (check_build_volume_range(hardware.build_volume, heuristic)) {
            spdlog::debug("[PrinterDetector] Matched build volume range (confidence: {})",
                          confidence);
            return confidence;
        }
    } else if (type == "mcu_match") {
        // Match against MCU chip type
        std::string pattern = heuristic.value("pattern", "");
        if (!hardware.mcu.empty()) {
            std::string mcu_lower = hardware.mcu;
            std::transform(mcu_lower.begin(), mcu_lower.end(), mcu_lower.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            std::string pattern_lower = pattern;
            std::transform(pattern_lower.begin(), pattern_lower.end(), pattern_lower.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            if (mcu_lower.find(pattern_lower) != std::string::npos) {
                spdlog::debug("[PrinterDetector] Matched MCU '{}' (confidence: {})", pattern,
                              confidence);
                return confidence;
            }
        }
    } else if (type == "macro_match") {
        // Match against G-code macro names in printer_objects
        // G-code macros appear as "gcode_macro <NAME>" in the objects list
        std::string pattern = heuristic.value("pattern", "");
        std::string pattern_lower = pattern;
        std::transform(pattern_lower.begin(), pattern_lower.end(), pattern_lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        for (const auto& obj : hardware.printer_objects) {
            // Check if object is a G-code macro
            if (obj.rfind("gcode_macro ", 0) == 0) {
                // Extract macro name (everything after "gcode_macro ")
                std::string macro_name = obj.substr(12);
                std::string macro_lower = macro_name;
                std::transform(macro_lower.begin(), macro_lower.end(), macro_lower.begin(),
                               [](unsigned char c) { return std::tolower(c); });

                if (macro_lower.find(pattern_lower) != std::string::npos) {
                    spdlog::debug("[PrinterDetector] Matched macro '{}' (confidence: {})",
                                  macro_name, confidence);
                    return confidence;
                }
            }
        }
    } else {
        spdlog::warn("[PrinterDetector] Unknown heuristic type: {}", type);
    }

    return 0; // No match
}

// Execute all heuristics for a printer and return best confidence + reason
PrinterDetectionResult execute_printer_heuristics(const json& printer,
                                                  const PrinterHardwareData& hardware) {
    std::string printer_id = printer.value("id", "");
    std::string printer_name = printer.value("name", "");

    if (!printer.contains("heuristics") || !printer["heuristics"].is_array()) {
        return {"", 0, ""};
    }

    PrinterDetectionResult best_result{"", 0, ""};

    // Try all heuristics for this printer
    for (const auto& heuristic : printer["heuristics"]) {
        int confidence = execute_heuristic(heuristic, hardware);
        if (confidence > best_result.confidence) {
            best_result.type_name = printer_name;
            best_result.confidence = confidence;
            best_result.reason = heuristic.value("reason", "");
        }
    }

    return best_result;
}
} // namespace

// ============================================================================
// Main Detection Entry Point
// ============================================================================

PrinterDetectionResult PrinterDetector::detect(const PrinterHardwareData& hardware) {
    try {
        // Verbose debug output for troubleshooting detection issues
        spdlog::info("[PrinterDetector] Running detection with {} sensors, {} fans, hostname '{}'",
                     hardware.sensors.size(), hardware.fans.size(), hardware.hostname);
        spdlog::info("[PrinterDetector]   printer_objects: {}, steppers: {}, kinematics: '{}'",
                     hardware.printer_objects.size(), hardware.steppers.size(),
                     hardware.kinematics);

        // Load database if not already loaded
        if (!g_database.load()) {
            LOG_ERROR_INTERNAL("[PrinterDetector] Cannot perform detection without database");
            return {"", 0, "Failed to load printer database"};
        }

        // Iterate through all printers in database and find best match
        PrinterDetectionResult best_match{"", 0, "No distinctive hardware detected"};

        if (!g_database.data.contains("printers") || !g_database.data["printers"].is_array()) {
            NOTIFY_ERROR("Printer database is corrupt");
            LOG_ERROR_INTERNAL(
                "[PrinterDetector] Invalid database format: missing 'printers' array");
            return {"", 0, "Invalid printer database format"};
        }

        for (const auto& printer : g_database.data["printers"]) {
            PrinterDetectionResult result = execute_printer_heuristics(printer, hardware);

            // Log all matches for debugging (not just best)
            if (result.confidence > 0) {
                spdlog::info("[PrinterDetector] Candidate: '{}' scored {}% via: {}",
                             result.type_name, result.confidence, result.reason);
            }

            if (result.confidence > best_match.confidence) {
                best_match = result;
            }
        }

        if (best_match.confidence > 0) {
            spdlog::info("[PrinterDetector] Detection complete: {} (confidence: {}, reason: {})",
                         best_match.type_name, best_match.confidence, best_match.reason);
        } else {
            spdlog::debug("[PrinterDetector] No distinctive fingerprints detected");
        }

        return best_match;
    } catch (const std::exception& e) {
        spdlog::error("[PrinterDetector] Exception during detection: {}", e.what());
        return {"", 0, std::string("Detection error: ") + e.what()};
    }
}

// ============================================================================
// Image Lookup Functions
// ============================================================================

std::string PrinterDetector::get_image_for_printer(const std::string& printer_name) {
    // Load database if not already loaded
    if (!g_database.load()) {
        spdlog::warn("[PrinterDetector] Cannot lookup image without database");
        return "";
    }

    if (!g_database.data.contains("printers") || !g_database.data["printers"].is_array()) {
        return "";
    }

    // Case-insensitive search by printer name
    std::string name_lower = printer_name;
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    for (const auto& printer : g_database.data["printers"]) {
        std::string db_name = printer.value("name", "");
        std::string db_name_lower = db_name;
        std::transform(db_name_lower.begin(), db_name_lower.end(), db_name_lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (db_name_lower == name_lower) {
            std::string image = printer.value("image", "");
            if (!image.empty()) {
                spdlog::debug("[PrinterDetector] Found image '{}' for printer '{}'", image,
                              printer_name);
            }
            return image;
        }
    }

    spdlog::debug("[PrinterDetector] No image found for printer '{}'", printer_name);
    return "";
}

std::string PrinterDetector::get_image_for_printer_id(const std::string& printer_id) {
    // Load database if not already loaded
    if (!g_database.load()) {
        spdlog::warn("[PrinterDetector] Cannot lookup image without database");
        return "";
    }

    if (!g_database.data.contains("printers") || !g_database.data["printers"].is_array()) {
        return "";
    }

    // Case-insensitive search by printer ID
    std::string id_lower = printer_id;
    std::transform(id_lower.begin(), id_lower.end(), id_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    for (const auto& printer : g_database.data["printers"]) {
        std::string db_id = printer.value("id", "");
        std::string db_id_lower = db_id;
        std::transform(db_id_lower.begin(), db_id_lower.end(), db_id_lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (db_id_lower == id_lower) {
            std::string image = printer.value("image", "");
            if (!image.empty()) {
                spdlog::debug("[PrinterDetector] Found image '{}' for printer ID '{}'", image,
                              printer_id);
            }
            return image;
        }
    }

    spdlog::debug("[PrinterDetector] No image found for printer ID '{}'", printer_id);
    return "";
}

// ============================================================================
// Dynamic Roller Builder
// ============================================================================

namespace {
// Cached roller data - built once and reused
struct RollerCache {
    std::string options;            // Newline-separated string for lv_roller_set_options()
    std::vector<std::string> names; // Vector of names for index lookups
    bool built = false;

    void reset() {
        options.clear();
        names.clear();
        built = false;
    }

    void build() {
        if (built)
            return;

        // Load database if not already loaded
        if (!g_database.load()) {
            spdlog::warn("[PrinterDetector] Cannot build roller without database");
            // Fallback to just Custom/Other and Unknown
            names = {"Custom/Other", "Unknown"};
            options = "Custom/Other\nUnknown";
            built = true;
            return;
        }

        if (!g_database.data.contains("printers") || !g_database.data["printers"].is_array()) {
            names = {"Custom/Other", "Unknown"};
            options = "Custom/Other\nUnknown";
            built = true;
            return;
        }

        // Collect all printer names that should appear in roller
        for (const auto& printer : g_database.data["printers"]) {
            // Check enabled flag (defaults to true if missing) - allows user to hide bundled
            bool enabled = printer.value("enabled", true);
            if (!enabled) {
                continue;
            }

            // Check show_in_roller flag (defaults to true if missing)
            bool show = printer.value("show_in_roller", true);
            if (!show) {
                continue;
            }

            std::string name = printer.value("name", "");
            if (!name.empty()) {
                names.push_back(name);
            }
        }

        // Sort alphabetically for consistent ordering
        std::sort(names.begin(), names.end());

        // Always append Custom/Other and Unknown at the end
        names.push_back("Custom/Other");
        names.push_back("Unknown");

        // Build newline-separated string for LVGL roller
        for (size_t i = 0; i < names.size(); ++i) {
            options += names[i];
            if (i < names.size() - 1) {
                options += "\n";
            }
        }

        spdlog::info("[PrinterDetector] Built roller with {} printer types", names.size());
        built = true;
    }
};

RollerCache g_roller_cache;
} // namespace

const std::string& PrinterDetector::get_roller_options() {
    g_roller_cache.build();
    return g_roller_cache.options;
}

const std::vector<std::string>& PrinterDetector::get_roller_names() {
    g_roller_cache.build();
    return g_roller_cache.names;
}

int PrinterDetector::find_roller_index(const std::string& printer_name) {
    g_roller_cache.build();

    // Case-insensitive search
    std::string name_lower = printer_name;
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    for (size_t i = 0; i < g_roller_cache.names.size(); ++i) {
        std::string cached_lower = g_roller_cache.names[i];
        std::transform(cached_lower.begin(), cached_lower.end(), cached_lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (cached_lower == name_lower) {
            return static_cast<int>(i);
        }
    }

    // Return Unknown index if not found
    return get_unknown_index();
}

std::string PrinterDetector::get_roller_name_at(int index) {
    g_roller_cache.build();

    if (index < 0 || static_cast<size_t>(index) >= g_roller_cache.names.size()) {
        return "Unknown";
    }

    return g_roller_cache.names[static_cast<size_t>(index)];
}

int PrinterDetector::get_unknown_index() {
    g_roller_cache.build();

    // Unknown is always the last entry
    if (g_roller_cache.names.empty()) {
        return 0;
    }
    return static_cast<int>(g_roller_cache.names.size() - 1);
}

// ============================================================================
// Print Start Capabilities Lookup
// ============================================================================

namespace {

/**
 * @brief Get set of valid capability keys from PrintStartOpCategory enum
 *
 * These keys must match what category_to_string() returns.
 */
const std::unordered_set<std::string>& get_valid_capability_keys() {
    static const std::unordered_set<std::string> keys = {
        helix::category_to_string(helix::PrintStartOpCategory::BED_MESH),
        helix::category_to_string(helix::PrintStartOpCategory::QGL),
        helix::category_to_string(helix::PrintStartOpCategory::Z_TILT),
        helix::category_to_string(helix::PrintStartOpCategory::NOZZLE_CLEAN),
        helix::category_to_string(helix::PrintStartOpCategory::PURGE_LINE),
        helix::category_to_string(helix::PrintStartOpCategory::SKEW_CORRECT),
        helix::category_to_string(helix::PrintStartOpCategory::CHAMBER_SOAK),
        // HOMING and UNKNOWN intentionally excluded - they shouldn't have capabilities
    };
    return keys;
}

/**
 * @brief Check if a capability key is recognized
 */
bool is_valid_capability_key(const std::string& key) {
    return get_valid_capability_keys().count(key) > 0;
}

} // namespace

PrintStartCapabilities
PrinterDetector::get_print_start_capabilities(const std::string& printer_name) {
    PrintStartCapabilities result;

    // Load database if not already loaded
    if (!g_database.load()) {
        spdlog::warn("[PrinterDetector] Cannot lookup capabilities without database");
        return result;
    }

    if (!g_database.data.contains("printers") || !g_database.data["printers"].is_array()) {
        return result;
    }

    // Case-insensitive search by printer name
    std::string name_lower = printer_name;
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    for (const auto& printer : g_database.data["printers"]) {
        std::string db_name = printer.value("name", "");
        std::string db_name_lower = db_name;
        std::transform(db_name_lower.begin(), db_name_lower.end(), db_name_lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (db_name_lower == name_lower) {
            // Found matching printer - check for capabilities
            if (!printer.contains("print_start_capabilities")) {
                spdlog::debug("[PrinterDetector] Printer '{}' has no print_start_capabilities",
                              printer_name);
                return result;
            }

            const auto& caps = printer["print_start_capabilities"];
            result.macro_name = caps.value("macro_name", "");

            if (caps.contains("params") && caps["params"].is_object()) {
                for (const auto& [key, value] : caps["params"].items()) {
                    // Validate capability key
                    if (!is_valid_capability_key(key)) {
                        spdlog::warn("[PrinterDetector] Unknown capability key '{}' for printer "
                                     "'{}' - will be ignored during matching",
                                     key, printer_name);
                    }

                    PrintStartParamCapability param;
                    param.param = value.value("param", "");
                    param.skip_value = value.value("skip_value", "");
                    param.enable_value = value.value("enable_value", "");
                    param.default_value = value.value("default_value", "");
                    param.description = value.value("description", "");

                    // Validate required fields
                    if (param.param.empty()) {
                        spdlog::warn("[PrinterDetector] Capability '{}' for printer '{}' has empty "
                                     "'param' field - entry will be skipped",
                                     key, printer_name);
                        continue;
                    }

                    result.params[key] = param;
                }
            }

            spdlog::info("[PrinterDetector] Found {} capabilities for '{}' (macro: {})",
                         result.params.size(), printer_name, result.macro_name);
            return result;
        }
    }

    spdlog::debug("[PrinterDetector] No capabilities found for printer '{}'", printer_name);
    return result;
}

// ============================================================================
// Reload and Status Functions
// ============================================================================

void PrinterDetector::reload() {
    spdlog::info("[PrinterDetector] Reloading printer database and extensions");
    g_roller_cache.reset();
    g_database.reload();
}

PrinterDetector::LoadStatus PrinterDetector::get_load_status() {
    // Ensure database is loaded
    g_database.load();

    LoadStatus status;
    status.loaded = g_database.loaded;
    status.total_printers = 0;
    status.user_overrides = g_database.user_overrides;
    status.user_additions = g_database.user_additions;
    status.loaded_files = g_database.loaded_files;
    status.load_errors = g_database.load_errors;

    // Count enabled printers
    if (g_database.data.contains("printers") && g_database.data["printers"].is_array()) {
        for (const auto& printer : g_database.data["printers"]) {
            if (printer.value("enabled", true)) {
                status.total_printers++;
            }
        }
    }

    return status;
}
