// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef __HELIX_CONFIG_H__
#define __HELIX_CONFIG_H__

#include "hv/json.hpp"
#include "spdlog/spdlog.h"

#include <string>

using json = nlohmann::json;

class Config {
private:
  static Config *instance;
  std::string path;

protected:
  json data;
  std::string default_printer;

public:
  Config();
  Config(Config &o) = delete;
  void operator=(const Config &) = delete;

  // Initialize config from file path
  void init(const std::string &config_path);

  // Template get/set with JSON pointer syntax
  template<typename T> T get(const std::string &json_ptr) {
    return data[json::json_pointer(json_ptr)].template get<T>();
  };

  template<typename T> T set(const std::string &json_ptr, T v) {
    return data[json::json_pointer(json_ptr)] = v;
  };

  // Get JSON sub-object
  json &get_json(const std::string &json_path);

  // Save current config to file
  void save();

  // Get default printer path prefix (e.g., "/printers/default_printer/")
  std::string& df();

  // Get config file path
  std::string get_path();

  // Singleton accessor
  static Config *get_instance();
};

#endif // __HELIX_CONFIG_H__
