/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 * Based on GuppyScreen WebSocket client implementation.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#include "moonraker_client.h"

using namespace hv;

MoonrakerClient::MoonrakerClient(EventLoopPtr loop)
    : WebSocketClient(loop)
    , request_id_(0)
    , was_connected_(false)
    , connection_state_(ConnectionState::DISCONNECTED)
    , connection_timeout_ms_(10000)      // Default 10 seconds
    , default_request_timeout_ms_(30000) // Default 30 seconds
    , keepalive_interval_ms_(10000)      // Default 10 seconds
    , reconnect_min_delay_ms_(200)       // Default 200ms
    , reconnect_max_delay_ms_(2000) {    // Default 2 seconds
}

MoonrakerClient::~MoonrakerClient() {
}

void MoonrakerClient::set_connection_state(ConnectionState new_state) {
  ConnectionState old_state = connection_state_.exchange(new_state);

  if (old_state != new_state) {
    const char* state_names[] = {"DISCONNECTED", "CONNECTING", "CONNECTED", "RECONNECTING", "FAILED"};
    spdlog::debug("Connection state: {} -> {}",
                  state_names[static_cast<int>(old_state)],
                  state_names[static_cast<int>(new_state)]);

    // Handle state-specific logic
    if (new_state == ConnectionState::RECONNECTING) {
      reconnect_attempts_++;
      if (max_reconnect_attempts_ > 0 && reconnect_attempts_ >= max_reconnect_attempts_) {
        spdlog::error("Max reconnect attempts ({}) exceeded", max_reconnect_attempts_);
        set_connection_state(ConnectionState::FAILED);
        return;
      }
    } else if (new_state == ConnectionState::CONNECTED) {
      reconnect_attempts_ = 0;  // Reset on successful connection
    }

    // Invoke state change callback if set
    if (state_change_callback_) {
      state_change_callback_(old_state, new_state);
    }
  }
}

int MoonrakerClient::connect(const char* url,
                               std::function<void()> on_connected,
                               std::function<void()> on_disconnected) {
  spdlog::debug("Moonraker WebSocket connecting to {}", url);
  set_connection_state(ConnectionState::CONNECTING);

  // Connection opened callback
  onopen = [this, on_connected, url]() {
    const HttpResponsePtr& resp = getHttpResponse();
    spdlog::info("Moonraker WebSocket connected to {}: {}", url, resp->body.c_str());
    was_connected_ = true;
    set_connection_state(ConnectionState::CONNECTED);
    on_connected();
  };

  // Message received callback
  onmessage = [this, on_connected, on_disconnected](const std::string& msg) {
    // Parse JSON message
    json j;
    try {
      j = json::parse(msg);
    } catch (const json::parse_error& e) {
      spdlog::error("JSON parse error: {}", e.what());
      return;
    }

    // Handle responses with request IDs (one-time callbacks)
    if (j.contains("id")) {
      uint32_t id = j["id"].get<uint32_t>();

      // Copy callbacks out before invoking to avoid deadlock
      std::function<void(json&)> success_cb;
      std::function<void(const MoonrakerError&)> error_cb;
      std::string method_name;
      bool has_error = false;
      MoonrakerError error;

      {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        auto it = pending_requests_.find(id);
        if (it != pending_requests_.end()) {
          PendingRequest& request = it->second;
          method_name = request.method;

          // Check for JSON-RPC error
          if (j.contains("error")) {
            has_error = true;
            error = MoonrakerError::from_json_rpc(j["error"], request.method);
            error_cb = request.error_callback;
          } else {
            success_cb = request.success_callback;
          }

          pending_requests_.erase(it);  // Remove before invoking callbacks
        }
      }  // Lock released here

      // Invoke callbacks outside the lock to avoid deadlock
      if (has_error) {
        spdlog::error("Request {} failed: {}", method_name, error.message);
        if (error_cb) {
          error_cb(error);
        }
      } else if (success_cb) {
        success_cb(j);
      }
    }

    // Handle notifications (no request ID)
    if (j.contains("method")) {
      std::string method = j["method"].get<std::string>();

      // Printer status updates (most common)
      if (method == "notify_status_update") {
        for (auto& cb : notify_callbacks_) {
          cb(j);
        }
      }
      // File list changes
      else if (method == "notify_filelist_changed") {
        for (auto& cb : notify_callbacks_) {
          cb(j);
        }
      }
      // Klippy disconnected from Moonraker
      else if (method == "notify_klippy_disconnected") {
        spdlog::warn("Klipper disconnected from Moonraker");
        on_disconnected();
      }
      // Klippy reconnected to Moonraker
      else if (method == "notify_klippy_ready") {
        spdlog::info("Klipper ready");
        on_connected();
      }

      // Method-specific persistent callbacks
      auto method_it = method_callbacks_.find(method);
      if (method_it != method_callbacks_.end()) {
        for (auto& [handler_name, cb] : method_it->second) {
          cb(j);
        }
      }
    }
  };

  // Connection closed callback
  onclose = [this, on_disconnected]() {
    ConnectionState current = connection_state_.load();

    // Cleanup all pending requests (invoke error callbacks)
    cleanup_pending_requests();

    if (was_connected_) {
      spdlog::warn("Moonraker WebSocket connection closed");
      was_connected_ = false;

      // Check if this is a reconnection scenario
      if (current != ConnectionState::FAILED) {
        set_connection_state(ConnectionState::RECONNECTING);
      }

      on_disconnected();
    } else {
      spdlog::debug("Moonraker WebSocket connection failed (printer not available)");

      // Initial connection failed
      if (current == ConnectionState::CONNECTING) {
        set_connection_state(ConnectionState::DISCONNECTED);
      }
      // Don't call on_disconnected() - we were never connected in the first place
    }
  };

  // WebSocket ping (keepalive) - use configured interval
  setPingInterval(keepalive_interval_ms_);

  // Automatic reconnection with exponential backoff - use configured values
  reconn_setting_t reconn;
  reconn_setting_init(&reconn);
  reconn.min_delay = reconnect_min_delay_ms_;
  reconn.max_delay = reconnect_max_delay_ms_;
  reconn.delay_policy = 2;    // Exponential backoff
  setReconnect(&reconn);

  // Connect
  http_headers headers;
  return open(url, headers);
}

void MoonrakerClient::register_notify_update(std::function<void(json&)> cb) {
  notify_callbacks_.push_back(cb);
}

void MoonrakerClient::register_method_callback(const std::string& method,
                                                const std::string& handler_name,
                                                std::function<void(json&)> cb) {
  auto it = method_callbacks_.find(method);
  if (it == method_callbacks_.end()) {
    spdlog::debug("Registering new method callback: {} (handler: {})",
                  method, handler_name);
    std::map<std::string, std::function<void(json&)>> handlers;
    handlers.insert({handler_name, cb});
    method_callbacks_.insert({method, handlers});
  } else {
    spdlog::debug("Adding handler to existing method {}: {}",
                  method, handler_name);
    it->second.insert({handler_name, cb});
  }
}

int MoonrakerClient::send_jsonrpc(const std::string& method) {
  json rpc;
  rpc["jsonrpc"] = "2.0";
  rpc["method"] = method;
  rpc["id"] = request_id_++;

  spdlog::debug("send_jsonrpc: {}", rpc.dump());
  return send(rpc.dump());
}

int MoonrakerClient::send_jsonrpc(const std::string& method, const json& params) {
  json rpc;
  rpc["jsonrpc"] = "2.0";
  rpc["method"] = method;

  // Only include params if not null or empty
  if (!params.is_null() && !params.empty()) {
    rpc["params"] = params;
  }

  rpc["id"] = request_id_++;

  spdlog::debug("send_jsonrpc: {}", rpc.dump());
  return send(rpc.dump());
}

int MoonrakerClient::send_jsonrpc(const std::string& method,
                                   const json& params,
                                   std::function<void(json&)> cb) {
  // Forward to new overload with null error callback
  return send_jsonrpc(method, params, cb, nullptr, 0);
}

int MoonrakerClient::send_jsonrpc(const std::string& method,
                                   const json& params,
                                   std::function<void(json&)> success_cb,
                                   std::function<void(const MoonrakerError&)> error_cb,
                                   uint32_t timeout_ms) {
  uint32_t id = request_id_;

  // Create pending request
  PendingRequest request;
  request.id = id;
  request.method = method;
  request.success_callback = success_cb;
  request.error_callback = error_cb;
  request.timestamp = std::chrono::steady_clock::now();
  request.timeout_ms = (timeout_ms > 0) ? timeout_ms : default_request_timeout_ms_;

  // Register request
  {
    std::lock_guard<std::mutex> lock(requests_mutex_);
    auto it = pending_requests_.find(id);
    if (it != pending_requests_.end()) {
      spdlog::warn("Request ID {} already has a registered callback", id);
      return -1;
    }
    pending_requests_.insert({id, request});
  }

  // Send the request
  return send_jsonrpc(method, params);
}

int MoonrakerClient::gcode_script(const std::string& gcode) {
  json params = {{"script", gcode}};
  return send_jsonrpc("printer.gcode.script", params);
}

void MoonrakerClient::discover_printer(std::function<void()> on_complete) {
  spdlog::info("Starting printer auto-discovery");

  // Step 1: Query available printer objects (no params required)
  send_jsonrpc("printer.objects.list", json(), [this, on_complete](json& response) {
    // Debug: Log raw response
    spdlog::debug("printer.objects.list response: {}", response.dump());

    // Validate response
    if (!response.contains("result") || !response["result"].contains("objects")) {
      spdlog::error("printer.objects.list failed: invalid response");
      if (response.contains("error")) {
        spdlog::error("  Error details: {}", response["error"].dump());
      }
      return;
    }

    // Parse discovered objects into typed arrays
    const json& objects = response["result"]["objects"];
    parse_objects(objects);

    // Step 2: Get server information
    send_jsonrpc("server.info", {}, [this, on_complete](json& info_response) {
      if (info_response.contains("result")) {
        const json& result = info_response["result"];
        std::string klippy_version = result.value("klippy_version", "unknown");
        std::string moonraker_version = result.value("moonraker_version", "unknown");

        spdlog::info("Moonraker version: {}", moonraker_version);
        spdlog::info("Klippy version: {}", klippy_version);

        if (result.contains("components")) {
          std::vector<std::string> components = result["components"].get<std::vector<std::string>>();
          spdlog::debug("Server components: {}", json(components).dump());
        }
      }

      // Step 3: Get printer information
      send_jsonrpc("printer.info", {}, [this, on_complete](json& printer_response) {
        if (printer_response.contains("result")) {
          const json& result = printer_response["result"];
          std::string hostname = result.value("hostname", "unknown");
          std::string software_version = result.value("software_version", "unknown");
          std::string state_message = result.value("state_message", "");

          spdlog::info("Printer hostname: {}", hostname);
          spdlog::info("Klipper software version: {}", software_version);
          if (!state_message.empty()) {
            spdlog::info("Printer state: {}", state_message);
          }
        }

        // Step 4: Subscribe to all discovered objects + core objects
        json subscription_objects;

        // Core non-optional objects
        subscription_objects["print_stats"] = nullptr;
        subscription_objects["virtual_sdcard"] = nullptr;
        subscription_objects["toolhead"] = nullptr;
        subscription_objects["gcode_move"] = nullptr;
        subscription_objects["motion_report"] = nullptr;
        subscription_objects["system_stats"] = nullptr;

        // All discovered heaters (extruders, beds, generic heaters)
        for (const auto& heater : heaters_) {
          subscription_objects[heater] = nullptr;
        }

        // All discovered sensors
        for (const auto& sensor : sensors_) {
          subscription_objects[sensor] = nullptr;
        }

        // All discovered fans
        for (const auto& fan : fans_) {
          subscription_objects[fan] = nullptr;
        }

        // All discovered LEDs
        for (const auto& led : leds_) {
          subscription_objects[led] = nullptr;
        }

        json subscribe_params = {{"objects", subscription_objects}};

        send_jsonrpc("printer.objects.subscribe", subscribe_params,
                     [on_complete, subscription_objects](json& sub_response) {
          if (sub_response.contains("result")) {
            spdlog::info("Subscription complete: {} objects subscribed",
                         subscription_objects.size());
          } else if (sub_response.contains("error")) {
            spdlog::error("Subscription failed: {}",
                          sub_response["error"].dump());
          }

          // Discovery complete
          on_complete();
        });
      });
    });
  });
}

void MoonrakerClient::parse_objects(const json& objects) {
  heaters_.clear();
  sensors_.clear();
  fans_.clear();
  leds_.clear();

  for (const auto& obj : objects) {
    std::string name = obj.template get<std::string>();

    // Extruders (controllable heaters)
    // Match "extruder", "extruder1", etc., but NOT "extruder_stepper"
    if (name.rfind("extruder", 0) == 0 && name.rfind("extruder_stepper", 0) != 0) {
      heaters_.push_back(name);
    }
    // Heated bed
    else if (name == "heater_bed") {
      heaters_.push_back(name);
    }
    // Generic heaters (e.g., "heater_generic chamber")
    else if (name.rfind("heater_generic ", 0) == 0) {
      heaters_.push_back(name);
    }
    // Read-only temperature sensors
    else if (name.rfind("temperature_sensor ", 0) == 0) {
      sensors_.push_back(name);
    }
    // Temperature-controlled fans (also act as sensors)
    else if (name.rfind("temperature_fan ", 0) == 0) {
      sensors_.push_back(name);
      fans_.push_back(name);  // Also add to fans for control
    }
    // Part cooling fan
    else if (name == "fan") {
      fans_.push_back(name);
    }
    // Heater fans (e.g., "heater_fan hotend_fan")
    else if (name.rfind("heater_fan ", 0) == 0) {
      fans_.push_back(name);
    }
    // Generic fans
    else if (name.rfind("fan_generic ", 0) == 0) {
      fans_.push_back(name);
    }
    // Controller fans
    else if (name.rfind("controller_fan ", 0) == 0) {
      fans_.push_back(name);
    }
    // Output pins (can be used as fans)
    else if (name.rfind("output_pin ", 0) == 0) {
      fans_.push_back(name);
    }
    // LED outputs
    else if (name.rfind("led ", 0) == 0 ||
             name.rfind("neopixel ", 0) == 0 ||
             name.rfind("dotstar ", 0) == 0) {
      leds_.push_back(name);
    }
  }

  spdlog::info("Discovered: {} heaters, {} sensors, {} fans, {} LEDs",
               heaters_.size(), sensors_.size(), fans_.size(), leds_.size());

  // Debug output of discovered objects
  if (!heaters_.empty()) {
    spdlog::debug("Heaters: {}", json(heaters_).dump());
  }
  if (!sensors_.empty()) {
    spdlog::debug("Sensors: {}", json(sensors_).dump());
  }
  if (!fans_.empty()) {
    spdlog::debug("Fans: {}", json(fans_).dump());
  }
  if (!leds_.empty()) {
    spdlog::debug("LEDs: {}", json(leds_).dump());
  }
}

void MoonrakerClient::check_request_timeouts() {
  std::lock_guard<std::mutex> lock(requests_mutex_);

  std::vector<uint32_t> timed_out_ids;

  // Find timed out requests
  for (auto& [id, request] : pending_requests_) {
    if (request.is_timed_out()) {
      timed_out_ids.push_back(id);

      spdlog::warn("Request {} ({}) timed out after {}ms",
                   id, request.method, request.get_elapsed_ms());

      // Invoke error callback if set
      if (request.error_callback) {
        MoonrakerError error = MoonrakerError::timeout(request.method, request.timeout_ms);
        request.error_callback(error);
      }
    }
  }

  // Remove timed out requests
  for (uint32_t id : timed_out_ids) {
    pending_requests_.erase(id);
  }
}

void MoonrakerClient::cleanup_pending_requests() {
  std::lock_guard<std::mutex> lock(requests_mutex_);

  if (!pending_requests_.empty()) {
    spdlog::debug("Cleaning up {} pending requests due to disconnect",
                  pending_requests_.size());

    // Invoke error callbacks for all pending requests
    for (auto& [id, request] : pending_requests_) {
      if (request.error_callback) {
        MoonrakerError error = MoonrakerError::connection_lost(request.method);
        request.error_callback(error);
      }
    }

    pending_requests_.clear();
  }
}
