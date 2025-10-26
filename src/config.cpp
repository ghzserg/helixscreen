// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"

#include <sys/stat.h>
#include <fstream>
#include <iomanip>
#ifdef __APPLE__
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif

Config *Config::instance{NULL};

Config::Config() {
}

Config *Config::get_instance() {
  if (instance == NULL) {
    instance = new Config();
  }
  return instance;
}

void Config::init(const std::string &config_path) {
  path = config_path;
  struct stat buffer;

  // Default sensor configuration for auto-discovery fallback
  json sensors_conf = {
    {
      {"id", "extruder"},
      {"display_name", "Extruder"},
      {"controllable", true},
      {"color", "red"}
    },
    {
      {"id", "heater_bed"},
      {"display_name", "Bed"},
      {"controllable", true},
      {"color", "purple"}
    }
  };

  // Default macro configuration
  json default_macros_conf = {
    {"load_filament", "LOAD_FILAMENT"},
    {"unload_filament", "UNLOAD_FILAMENT"},
    {"cooldown", "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=0\nSET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=0"}
  };

  if (stat(config_path.c_str(), &buffer) == 0) {
    // Load existing config
    spdlog::info("Loading config from {}", config_path);
    data = json::parse(std::fstream(config_path));
  } else {
    // Create default config
    spdlog::info("Creating default config at {}", config_path);
    data = {
      {"log_path", "/tmp/helixscreen.log"},
      {"display_sleep_sec", 600},
      {"display_rotate", 0},
      {"default_printer", "default_printer"},
      {"printers", {
        {"default_printer", {
          {"moonraker_api_key", false},
          {"moonraker_host", "127.0.0.1"},
          {"moonraker_port", 7125},
          {"log_level", "debug"},
          {"monitored_sensors", json::array()},  // Empty - will be auto-populated
          {"fans", json::array()},                // Empty - will be auto-populated
          {"default_macros", default_macros_conf}
        }}
      }}
    };
  }

  // Store config path in data for reference
  data["config_path"] = config_path;

  // Set default printer path prefix
  auto df_name = data["/default_printer"_json_pointer];
  if (!df_name.is_null()) {
    default_printer = "/printers/" + df_name.template get<std::string>() + "/";

    // Ensure monitored_sensors exists (even if empty for auto-discovery)
    auto &monitored_sensors = data[json::json_pointer(df() + "monitored_sensors")];
    if (monitored_sensors.is_null()) {
      data[json::json_pointer(df() + "monitored_sensors")] = json::array();
    }

    // Ensure fans exists (even if empty for auto-discovery)
    auto &fans = data[json::json_pointer(df() + "fans")];
    if (fans.is_null()) {
      data[json::json_pointer(df() + "fans")] = json::array();
    }

    // Ensure default_macros exists
    auto &default_macros = data[json::json_pointer(df() + "default_macros")];
    if (default_macros.is_null()) {
      data[json::json_pointer(df() + "default_macros")] = default_macros_conf;
    }

    // Ensure log_level exists
    auto &ll = data[json::json_pointer(df() + "log_level")];
    if (ll.is_null()) {
      data[json::json_pointer(df() + "log_level")] = "debug";
    }
  }

  // Ensure display_rotate exists
  auto &rotate = data["/display_rotate"_json_pointer];
  if (rotate.is_null()) {
    data["/display_rotate"_json_pointer] = 0;  // LV_DISP_ROT_0
  }

  // Ensure display_sleep_sec exists
  auto &display_sleep = data["/display_sleep_sec"_json_pointer];
  if (display_sleep.is_null()) {
    data["/display_sleep_sec"_json_pointer] = 600;
  }

  // Save updated config with any new defaults
  std::ofstream o(config_path);
  o << std::setw(2) << data << std::endl;

  spdlog::info("Config initialized: moonraker={}:{}",
               get<std::string>(df() + "moonraker_host"),
               get<int>(df() + "moonraker_port"));
}

std::string& Config::df() {
  return default_printer;
}

std::string Config::get_path() {
  return path;
}

json &Config::get_json(const std::string &json_path) {
  return data[json::json_pointer(json_path)];
}

void Config::save() {
  spdlog::info("Saving config to {}", path);
  std::ofstream o(path);
  o << std::setw(2) << data << std::endl;
}
