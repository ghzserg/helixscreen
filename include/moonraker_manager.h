// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "json_fwd.h"
#include "runtime_config.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <queue>

// Forward declarations
namespace helix {
class Config;
}
namespace helix {
class MoonrakerClient;
}
class MoonrakerAPI;
namespace helix {
class PrinterState;
}
class PrintStartCollector;

// Need full enum definition for inline helper function
#include "printer_state.h"

namespace helix {
class MacroModificationManager;
}

/**
 * @brief Manages Moonraker client and API lifecycle
 *
 * MoonrakerManager handles:
 * - Creating mock or real helix::MoonrakerClient based on RuntimeConfig
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
    bool init(const RuntimeConfig& runtime_config, helix::Config* config);

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
    helix::MoonrakerClient* client() const {
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

    /**
     * @brief Determine if print start collector should be started
     *
     * Helper function for testing mid-print detection logic.
     * Returns true if collector should start based on state transition and progress.
     *
     * @param prev_state Previous print job state
     * @param new_state New print job state
     * @param current_progress Current print progress percentage (0-100)
     * @return true if collector should start, false otherwise
     */
    static inline bool should_start_print_collector(helix::PrintJobState prev_state,
                                                    helix::PrintJobState new_state,
                                                    int current_progress) {
        // Only start on TRANSITION to PRINTING from non-printing state
        bool was_not_printing = (prev_state != helix::PrintJobState::PRINTING &&
                                 prev_state != helix::PrintJobState::PAUSED);
        bool is_now_printing = (new_state == helix::PrintJobState::PRINTING);

        if (!was_not_printing || !is_now_printing) {
            return false; // Not a transition to printing
        }

        // Mid-print detection: only relevant when prev_state is STANDBY.
        // STANDBY is the initial state at app boot - if we transition STANDBY → PRINTING
        // with progress > 0, the app started while a print was already running.
        // From COMPLETE/CANCELLED/ERROR → PRINTING, progress is stale from the
        // previous print and should be ignored - this is always a fresh print.
        if (prev_state == helix::PrintJobState::STANDBY && current_progress > 0) {
            return false; // App joined mid-print, skip collector
        }
        return true;
    }

    /**
     * @brief Initialize macro analysis manager
     *
     * Creates the manager for PRINT_START macro analysis and wizard.
     * Call after init() but before connect().
     */
    void init_macro_analysis(helix::Config* config);

    /**
     * @brief Get macro modification manager
     * @return Pointer to manager, or nullptr if not initialized
     */
    helix::MacroModificationManager* macro_analysis() const;

  private:
    // Initialization helpers
    void create_client(const RuntimeConfig& runtime_config);
    void configure_timeouts(helix::Config* config);
    void register_callbacks();
    void create_api(const RuntimeConfig& runtime_config);

    // Owned resources
    std::unique_ptr<helix::MoonrakerClient> m_client;
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

    // Macro modification manager (PRINT_START wizard integration)
    std::unique_ptr<helix::MacroModificationManager> m_macro_analysis;

    // Destruction flag for async callback safety [L012]
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);

    // Startup time for suppressing initial notifications (Klipper ready toast)
    std::chrono::steady_clock::time_point m_startup_time;

    bool m_initialized = false;
};
