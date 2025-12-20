// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2025 HelixScreen

#pragma once

#include "ui_observer_guard.h"

#include "runtime_config.h"

#include <queue>

#include "hv/json.hpp"

// Forward declarations
class Config;
class MoonrakerClient;
class MoonrakerAPI;
class PrinterState;
class PrintStartCollector;

using json = nlohmann::json;

/**
 * @brief Manages Moonraker client and API lifecycle
 *
 * MoonrakerManager handles:
 * - Creating mock or real MoonrakerClient based on RuntimeConfig
 * - Creating mock or real MoonrakerAPI based on RuntimeConfig
 * - Thread-safe notification queue for WebSocket → main thread handoff
 * - Connection state change handling
 * - Timeout processing
 * - API injection to panels
 *
 * Thread Safety:
 * Moonraker callbacks run on libhv's event loop thread. LVGL is single-threaded.
 * This class queues notifications for processing on the main thread.
 *
 * Usage:
 *   MoonrakerManager mgr;
 *   mgr.init(runtime_config, app_config);
 *   mgr.connect(url);
 *   // In main loop:
 *   mgr.process_notifications();
 *   mgr.process_timeouts();
 */
class MoonrakerManager {
  public:
    MoonrakerManager();
    ~MoonrakerManager();

    // Non-copyable, non-movable (owns resources, manages threads)
    MoonrakerManager(const MoonrakerManager&) = delete;
    MoonrakerManager& operator=(const MoonrakerManager&) = delete;
    MoonrakerManager(MoonrakerManager&&) = delete;
    MoonrakerManager& operator=(MoonrakerManager&&) = delete;

    /**
     * @brief Initialize Moonraker client and API
     * @param runtime_config Runtime configuration for mock modes
     * @param config Application config for timeouts
     * @return true if initialization succeeded
     */
    bool init(const RuntimeConfig& runtime_config, Config* config);

    /**
     * @brief Shutdown and cleanup
     */
    void shutdown();

    /**
     * @brief Check if manager is initialized
     */
    bool is_initialized() const {
        return m_initialized;
    }

    /**
     * @brief Connect to Moonraker server
     * @param websocket_url WebSocket URL (e.g., "ws://192.168.1.100:7125/websocket")
     * @param http_base_url HTTP base URL (e.g., "http://192.168.1.100:7125")
     * @return 0 on success, non-zero on failure
     */
    int connect(const std::string& websocket_url, const std::string& http_base_url);

    /**
     * @brief Process queued notifications on main thread
     *
     * Must be called from the main thread (LVGL thread).
     * Processes all queued Moonraker notifications and connection state changes.
     */
    void process_notifications();

    /**
     * @brief Process client timeouts
     *
     * Should be called periodically (e.g., every 100ms) to check for
     * request timeouts and trigger reconnection if needed.
     */
    void process_timeouts();

    /**
     * @brief Get the Moonraker client
     */
    MoonrakerClient* client() const {
        return m_client.get();
    }

    /**
     * @brief Get the Moonraker API
     */
    MoonrakerAPI* api() const {
        return m_api.get();
    }

    /**
     * @brief Get number of pending notifications in queue
     */
    size_t pending_notification_count() const;

    /**
     * @brief Initialize print start collector after connection
     *
     * Sets up observers to monitor print startup phases.
     * Call after successful connect().
     */
    void init_print_start_collector();

  private:
    // Initialization helpers
    void create_client(const RuntimeConfig& runtime_config);
    void configure_timeouts(Config* config);
    void register_callbacks();
    void create_api(const RuntimeConfig& runtime_config);

    // Owned resources
    std::unique_ptr<MoonrakerClient> m_client;
    std::unique_ptr<MoonrakerAPI> m_api;

    // Thread-safe notification queue
    std::queue<nlohmann::json> m_notification_queue;
    mutable std::mutex m_notification_mutex;

    // Print start collector (monitors PRINT_START macro progress)
    std::shared_ptr<PrintStartCollector> m_print_start_collector;
    ObserverGuard m_print_start_observer;
    ObserverGuard m_print_start_phase_observer;
    ObserverGuard m_print_layer_fallback_observer;
    ObserverGuard m_print_progress_fallback_observer;

    bool m_initialized = false;
};
