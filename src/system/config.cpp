// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"

#include "ui_error_reporting.h"

#include <fstream>
#include <iomanip>
#include <sys/stat.h>
#ifdef __APPLE__
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif

Config* Config::instance{NULL};

Config::Config() {}

Config* Config::get_instance() {
    if (instance == NULL) {
        instance = new Config();
    }
    return instance;
}

void Config::init(const std::string& config_path) {
    path = config_path;
    struct stat buffer;

    // Migration: Check for legacy config at old location (helixconfig.json in app root)
    // If new location doesn't exist but old location does, migrate it
    if (stat(config_path.c_str(), &buffer) != 0) {
        // New config doesn't exist - check for legacy locations
        const std::vector<std::string> legacy_paths = {
            "helixconfig.json",                  // Old location (app root)
            "/opt/helixscreen/helixconfig.json", // Legacy embedded install
        };

        for (const auto& legacy_path : legacy_paths) {
            if (stat(legacy_path.c_str(), &buffer) == 0) {
                spdlog::info("[Config] Found legacy config at {}, migrating to {}", legacy_path,
                             config_path);

                // Ensure config/ directory exists
                fs::path config_dir = fs::path(config_path).parent_path();
                if (!config_dir.empty() && !fs::exists(config_dir)) {
                    fs::create_directories(config_dir);
                }

                // Copy legacy config to new location, then remove old file
                try {
                    fs::copy_file(legacy_path, config_path);
                    // Remove legacy file to avoid confusion
                    fs::remove(legacy_path);
                    spdlog::info("[Config] Migration complete: {} -> {} (old file removed)",
                                 legacy_path, config_path);
                } catch (const fs::filesystem_error& e) {
                    spdlog::warn("[Config] Migration failed: {}", e.what());
                    // Fall through to create default config
                }
                break;
            }
        }
    }

    // Default sensor configuration for auto-discovery fallback
    json sensors_conf = {{{"id", "extruder"},
                          {"display_name", "Extruder"},
                          {"controllable", true},
                          {"color", "red"}},
                         {{"id", "heater_bed"},
                          {"display_name", "Bed"},
                          {"controllable", true},
                          {"color", "purple"}}};

    // Default macro configuration
    // Supports both string format (backward compat) and object format {label, gcode}
    json default_macros_conf = {
        {"load_filament", {{"label", "Load"}, {"gcode", "LOAD_FILAMENT"}}},
        {"unload_filament", {{"label", "Unload"}, {"gcode", "UNLOAD_FILAMENT"}}},
        {"macro_1", {{"label", "Clean Nozzle"}, {"gcode", "HELIX_CLEAN_NOZZLE"}}},
        {"macro_2", {{"label", "Bed Level"}, {"gcode", "HELIX_BED_LEVEL_IF_NEEDED"}}},
        {"cooldown", "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=0\nSET_HEATER_TEMPERATURE "
                     "HEATER=heater_bed TARGET=0"}};

    if (stat(config_path.c_str(), &buffer) == 0) {
        // Load existing config
        spdlog::info("[Config] Loading config from {}", config_path);
        data = json::parse(std::fstream(config_path));
    } else {
        // Create default config
        spdlog::info("[Config] Creating default config at {}", config_path);
        data = {{"log_path", "/tmp/helixscreen.log"},
                {"display_sleep_sec", 600},
                {"display_rotate", 0},
                {"dark_mode", true}, // Theme preference: true=dark, false=light
                {"default_printer", "default_printer"},
                {"gcode_viewer",
                 {{"shading_model", "phong"}, {"tube_sides", 4}}},      // G-code viewer settings
                {"input", {{"scroll_throw", 25}, {"scroll_limit", 5}}}, // Scroll momentum settings
                {"printers",
                 {{"default_printer",
                   {{"moonraker_api_key", false},
                    {"moonraker_host", "127.0.0.1"},
                    {"moonraker_port", 7125},
                    {"log_level", "debug"},
                    {"monitored_sensors", json::array()}, // Empty - will be auto-populated
                    {"fans", json::array()},              // Empty - will be auto-populated
                    {"default_macros", default_macros_conf}}}}}};
    }

    // Store config path in data for reference
    data["config_path"] = config_path;

    // Set default printer path prefix
    auto df_name = data["/default_printer"_json_pointer];
    if (!df_name.is_null()) {
        default_printer = "/printers/" + df_name.template get<std::string>() + "/";

        // Ensure monitored_sensors exists (even if empty for auto-discovery)
        auto& monitored_sensors = data[json::json_pointer(df() + "monitored_sensors")];
        if (monitored_sensors.is_null()) {
            data[json::json_pointer(df() + "monitored_sensors")] = json::array();
        }

        // Ensure fans exists (even if empty for auto-discovery)
        auto& fans = data[json::json_pointer(df() + "fans")];
        if (fans.is_null()) {
            data[json::json_pointer(df() + "fans")] = json::array();
        }

        // Ensure default_macros exists
        auto& default_macros = data[json::json_pointer(df() + "default_macros")];
        if (default_macros.is_null()) {
            data[json::json_pointer(df() + "default_macros")] = default_macros_conf;
        }

        // Ensure log_level exists
        auto& ll = data[json::json_pointer(df() + "log_level")];
        if (ll.is_null()) {
            data[json::json_pointer(df() + "log_level")] = "debug";
        }
    }

    // Ensure display_rotate exists
    auto& rotate = data["/display_rotate"_json_pointer];
    if (rotate.is_null()) {
        data["/display_rotate"_json_pointer] = 0; // LV_DISP_ROT_0
    }

    // Ensure display_sleep_sec exists
    auto& display_sleep = data["/display_sleep_sec"_json_pointer];
    if (display_sleep.is_null()) {
        data["/display_sleep_sec"_json_pointer] = 600;
    }

    // Save updated config with any new defaults
    std::ofstream o(config_path);
    o << std::setw(2) << data << std::endl;

    spdlog::debug("[Config] initialized: moonraker={}:{}",
                  get<std::string>(df() + "moonraker_host"), get<int>(df() + "moonraker_port"));
}

std::string& Config::df() {
    return default_printer;
}

std::string Config::get_path() {
    return path;
}

json& Config::get_json(const std::string& json_path) {
    return data[json::json_pointer(json_path)];
}

bool Config::save() {
    spdlog::debug("[Config] Saving config to {}", path);

    try {
        std::ofstream o(path);
        if (!o.is_open()) {
            NOTIFY_ERROR("Could not save configuration file");
            LOG_ERROR_INTERNAL("Failed to open config file for writing: {}", path);
            return false;
        }

        o << std::setw(2) << data << std::endl;

        if (!o.good()) {
            NOTIFY_ERROR("Error writing configuration file");
            LOG_ERROR_INTERNAL("Error writing to config file: {}", path);
            return false;
        }

        o.close();
        spdlog::debug("[Config] saved successfully to {}", path);
        return true;

    } catch (const std::exception& e) {
        NOTIFY_ERROR("Failed to save configuration: {}", e.what());
        LOG_ERROR_INTERNAL("Exception while saving config to {}: {}", path, e.what());
        return false;
    }
}

bool Config::is_wizard_required() {
    // Check explicit wizard completion flag
    // IMPORTANT: Use contains() first to avoid creating null entries via operator[]
    json::json_pointer ptr("/wizard_completed");

    if (data.contains(ptr)) {
        auto& wizard_completed = data[ptr];
        if (wizard_completed.is_boolean()) {
            bool is_completed = wizard_completed.get<bool>();
            spdlog::trace("[Config] Wizard completed flag = {}", is_completed);
            return !is_completed; // Wizard required if flag is false
        }
        // Key exists but wrong type - treat as not set
        spdlog::warn("[Config] wizard_completed has invalid type, treating as unset");
    }

    // No flag set - wizard has never been run
    spdlog::debug("[Config] No wizard_completed flag found, wizard required");
    return true;
}

void Config::reset_to_defaults() {
    spdlog::info("[Config] Resetting configuration to factory defaults");

    // Default macro configuration (same as init())
    // Supports both string format (backward compat) and object format {label, gcode}
    json default_macros_conf = {
        {"load_filament", {{"label", "Load"}, {"gcode", "LOAD_FILAMENT"}}},
        {"unload_filament", {{"label", "Unload"}, {"gcode", "UNLOAD_FILAMENT"}}},
        {"macro_1", {{"label", "Clean Nozzle"}, {"gcode", "HELIX_CLEAN_NOZZLE"}}},
        {"macro_2", {{"label", "Bed Level"}, {"gcode", "HELIX_BED_LEVEL_IF_NEEDED"}}},
        {"cooldown", "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=0\nSET_HEATER_TEMPERATURE "
                     "HEATER=heater_bed TARGET=0"}};

    // Reset to default configuration structure
    // Keep the config_path and log_path, reset everything else
    std::string config_path_backup = path;

    data = {{"log_path", "/tmp/helixscreen.log"},
            {"config_path", config_path_backup},
            {"display_sleep_sec", 600},
            {"display_rotate", 0},
            {"brightness", 50},
            {"dark_mode", true},
            {"sounds_enabled", true},
            {"completion_alert", true},
            {"wizard_completed", false}, // Force wizard to run again
            {"default_printer", "default_printer"},
            {"gcode_viewer", {{"shading_model", "phong"}, {"tube_sides", 4}}},
            {"input", {{"scroll_throw", 25}, {"scroll_limit", 5}}},
            {"printers",
             {{"default_printer",
               {{"moonraker_api_key", false},
                {"moonraker_host", ""}, // Cleared - requires reconfiguration
                {"moonraker_port", 7125},
                {"log_level", "debug"},
                {"monitored_sensors", json::array()},
                {"fans", json::array()},
                {"default_macros", default_macros_conf}}}}}};

    // Update default printer path prefix
    default_printer = "/printers/default_printer/";

    spdlog::info("[Config] Configuration reset to defaults. Wizard will run on next startup.");
}

MacroConfig Config::get_macro(const std::string& key, const MacroConfig& default_val) {
    try {
        std::string path = df() + "default_macros/" + key;
        json::json_pointer ptr(path);

        if (!data.contains(ptr)) {
            spdlog::trace("[Config] Macro '{}' not found, using default", key);
            return default_val;
        }

        const auto& val = data[ptr];

        // Handle string format (backward compatibility): use as both label and gcode
        if (val.is_string()) {
            std::string macro = val.get<std::string>();
            spdlog::trace("[Config] Macro '{}' is string format: '{}'", key, macro);
            return {macro, macro};
        }

        // Handle object format: {label, gcode}
        if (val.is_object()) {
            MacroConfig result;
            result.label = val.value("label", default_val.label);
            result.gcode = val.value("gcode", default_val.gcode);
            spdlog::trace("[Config] Macro '{}': label='{}', gcode='{}'", key, result.label,
                          result.gcode);
            return result;
        }

        spdlog::warn("[Config] Macro '{}' has unexpected type, using default", key);
        return default_val;

    } catch (const std::exception& e) {
        spdlog::warn("[Config] Error reading macro '{}': {}", key, e.what());
        return default_val;
    }
}
