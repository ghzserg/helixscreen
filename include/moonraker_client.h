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

#ifndef MOONRAKER_CLIENT_H
#define MOONRAKER_CLIENT_H

#include "hv/WebSocketClient.h"
#include "hv/json.hpp"  // libhv's nlohmann json (via cpputil/)
#include "spdlog/spdlog.h"

#include <map>
#include <vector>
#include <atomic>
#include <functional>
#include <string>

using json = nlohmann::json;

/**
 * @brief WebSocket client for Moonraker API communication
 *
 * Implements JSON-RPC 2.0 protocol for Klipper/Moonraker integration.
 * Handles connection lifecycle, automatic reconnection, and message routing.
 */
class MoonrakerClient : public hv::WebSocketClient {
public:
  MoonrakerClient(hv::EventLoopPtr loop = nullptr);
  ~MoonrakerClient();

  /**
   * @brief Connect to Moonraker WebSocket server
   *
   * @param url WebSocket URL (e.g., "ws://127.0.0.1:7125/websocket")
   * @param on_connected Callback invoked when connection opens
   * @param on_disconnected Callback invoked when connection closes
   * @return 0 on success, non-zero on error
   */
  int connect(const char* url,
              std::function<void()> on_connected,
              std::function<void()> on_disconnected);

  /**
   * @brief Register callback for status update notifications
   *
   * Invoked when Moonraker sends "notify_status_update" messages
   * (triggered by printer.objects.subscribe subscriptions).
   *
   * @param cb Callback function receiving parsed JSON notification
   */
  void register_notify_update(std::function<void(json&)> cb);

  /**
   * @brief Register persistent callback for specific notification methods
   *
   * Unlike one-time request callbacks, these persist across multiple messages.
   * Useful for console output, prompt notifications, etc.
   *
   * @param method Notification method name (e.g., "notify_gcode_response")
   * @param handler_name Unique identifier for this handler (for debugging)
   * @param cb Callback function receiving parsed JSON notification
   */
  void register_method_callback(const std::string& method,
                                 const std::string& handler_name,
                                 std::function<void(json&)> cb);

  /**
   * @brief Send JSON-RPC request without parameters
   *
   * @param method RPC method name (e.g., "printer.info")
   * @return 0 on success, non-zero on error
   */
  int send_jsonrpc(const std::string& method);

  /**
   * @brief Send JSON-RPC request with parameters
   *
   * @param method RPC method name
   * @param params JSON parameters object
   * @return 0 on success, non-zero on error
   */
  int send_jsonrpc(const std::string& method, const json& params);

  /**
   * @brief Send JSON-RPC request with one-time response callback
   *
   * Callback is invoked once when response arrives, then removed.
   *
   * @param method RPC method name
   * @param params JSON parameters object
   * @param cb Callback function receiving response JSON
   * @return 0 on success, non-zero on error
   */
  int send_jsonrpc(const std::string& method,
                   const json& params,
                   std::function<void(json&)> cb);

  /**
   * @brief Send G-code script command
   *
   * Convenience wrapper for printer.gcode.script method.
   *
   * @param gcode G-code string (e.g., "G28", "M104 S210")
   * @return 0 on success, non-zero on error
   */
  int gcode_script(const std::string& gcode);

  /**
   * @brief Perform printer auto-discovery sequence
   *
   * Calls printer.objects.list → server.info → printer.info → printer.objects.subscribe
   * in sequence, parsing discovered objects and populating PrinterState.
   *
   * @param on_complete Callback invoked when discovery completes successfully
   */
  void discover_printer(std::function<void()> on_complete);

  /**
   * @brief Parse object list from printer.objects.list response
   *
   * Categorizes Klipper objects into typed arrays (extruders, heaters, sensors, fans).
   *
   * @param objects JSON array of object names
   */
  void parse_objects(const json& objects);

  /**
   * @brief Get discovered heaters (extruders, beds, generic heaters)
   */
  const std::vector<std::string>& get_heaters() const { return heaters_; }

  /**
   * @brief Get discovered read-only sensors
   */
  const std::vector<std::string>& get_sensors() const { return sensors_; }

  /**
   * @brief Get discovered fans
   */
  const std::vector<std::string>& get_fans() const { return fans_; }

private:
  // One-time callbacks keyed by request ID
  std::map<uint32_t, std::function<void(json&)>> callbacks_;

  // Persistent notify_status_update callbacks
  std::vector<std::function<void(json&)>> notify_callbacks_;

  // Persistent method-specific callbacks
  // method_name : { handler_name : callback }
  std::map<std::string, std::map<std::string, std::function<void(json&)>>> method_callbacks_;

  // Auto-incrementing JSON-RPC request ID
  std::atomic_uint64_t request_id_;

  // Auto-discovered printer objects
  std::vector<std::string> heaters_;   // Controllable heaters (extruders, bed, etc.)
  std::vector<std::string> sensors_;   // Read-only temperature sensors
  std::vector<std::string> fans_;      // All fan types
  std::vector<std::string> leds_;      // LED outputs
};

#endif // MOONRAKER_CLIENT_H
