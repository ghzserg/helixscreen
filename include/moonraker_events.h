// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>

/**
 * @brief Event types emitted by MoonrakerClient
 *
 * These events replace direct UI notification calls, allowing the transport
 * layer to remain decoupled from the UI layer.
 */
enum class MoonrakerEventType {
    CONNECTION_FAILED,   ///< Max reconnect attempts exceeded
    CONNECTION_LOST,     ///< WebSocket connection closed unexpectedly
    RECONNECTING,        ///< Attempting to reconnect
    RECONNECTED,         ///< Successfully reconnected after disconnect
    MESSAGE_OVERSIZED,   ///< Received message exceeds size limit
    RPC_ERROR,           ///< JSON-RPC request failed
    KLIPPY_DISCONNECTED, ///< Klipper firmware disconnected from Moonraker
    KLIPPY_READY,        ///< Klipper firmware ready
    DISCOVERY_FAILED,    ///< Printer discovery failed
    REQUEST_TIMEOUT      ///< JSON-RPC request timed out
};

/**
 * @brief Event structure passed to event handlers
 */
struct MoonrakerEvent {
    MoonrakerEventType type;
    std::string message; ///< Human-readable message
    std::string details; ///< Additional details (optional)
    bool is_error;       ///< true for errors, false for warnings/info
};

namespace helix {
/**
 * @brief Callback type for event handlers
 */
using MoonrakerEventCallback = std::function<void(const MoonrakerEvent&)>;
} // namespace helix
