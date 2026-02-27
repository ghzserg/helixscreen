// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "moonraker_client_mock.h"

#include <functional>
#include <string>
#include <unordered_map>

/**
 * @file moonraker_client_mock_internal.h
 * @brief Internal types and handler registry for MoonrakerClientMock
 *
 * This header defines the method handler function type and registration
 * functions for domain-specific mock handlers. It's used internally by
 * the mock implementation modules and should not be included by external code.
 */

namespace mock_internal {

// ============================================================================
// Mock Printer Configuration Constants
// ============================================================================

// Bed dimensions (mm)
constexpr double MOCK_BED_X_MIN = 0.0;
constexpr double MOCK_BED_X_MAX = 250.0;
constexpr double MOCK_BED_Y_MIN = 0.0;
constexpr double MOCK_BED_Y_MAX = 250.0;
constexpr double MOCK_BED_Z_MAX = 300.0;

// Probe margins - typical probes can't reach bed edges
constexpr double MOCK_PROBE_MARGIN = 15.0;

// Derived mesh bounds (bed size minus probe margins)
constexpr double MOCK_MESH_X_MIN = MOCK_BED_X_MIN + MOCK_PROBE_MARGIN;
constexpr double MOCK_MESH_X_MAX = MOCK_BED_X_MAX - MOCK_PROBE_MARGIN;
constexpr double MOCK_MESH_Y_MIN = MOCK_BED_Y_MIN + MOCK_PROBE_MARGIN;
constexpr double MOCK_MESH_Y_MAX = MOCK_BED_Y_MAX - MOCK_PROBE_MARGIN;

/**
 * @brief Type for method handler functions
 *
 * Handlers process a specific JSON-RPC method call and invoke either
 * the success or error callback.
 *
 * @param self Pointer to MoonrakerClientMock instance
 * @param params JSON parameters from the RPC call
 * @param success_cb Success callback to invoke with result
 * @param error_cb Error callback to invoke on failure
 * @return true if the handler recognized and processed the method, false otherwise
 */
using MethodHandler = std::function<bool(MoonrakerClientMock* self, const json& params,
                                         std::function<void(json)> success_cb,
                                         std::function<void(const MoonrakerError&)> error_cb)>;

/**
 * @brief Register file-related method handlers
 *
 * Registers handlers for:
 * - server.files.list
 * - server.files.metadata
 * - server.files.delete
 * - server.files.move
 * - server.files.copy
 * - server.files.post_directory
 * - server.files.delete_directory
 *
 * @param registry Map to register handlers into
 */
void register_file_handlers(std::unordered_map<std::string, MethodHandler>& registry);

/**
 * @brief Register print control method handlers
 *
 * Registers handlers for:
 * - printer.print.start
 * - printer.print.pause
 * - printer.print.resume
 * - printer.print.cancel
 * - printer.gcode.script
 *
 * @param registry Map to register handlers into
 */
void register_print_handlers(std::unordered_map<std::string, MethodHandler>& registry);

/**
 * @brief Register object query method handlers
 *
 * Registers handlers for:
 * - printer.objects.list
 * - printer.objects.query
 *
 * @param registry Map to register handlers into
 */
void register_object_handlers(std::unordered_map<std::string, MethodHandler>& registry);

/**
 * @brief Register history method handlers
 *
 * Registers handlers for:
 * - server.history.list
 * - server.history.totals
 * - server.history.delete_job
 *
 * @param registry Map to register handlers into
 */
void register_history_handlers(std::unordered_map<std::string, MethodHandler>& registry);

/**
 * @brief Register server method handlers
 *
 * Registers handlers for:
 * - server.connection.identify
 * - server.info
 * - printer.info
 *
 * @param registry Map to register handlers into
 */
void register_server_handlers(std::unordered_map<std::string, MethodHandler>& registry);

} // namespace mock_internal
