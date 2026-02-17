// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_backend.h"
#include "moonraker_types.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

// Forward declarations
class MoonrakerAPI;
namespace helix {
class MoonrakerClient;
}
using helix::MoonrakerClient;

/**
 * @file ams_backend_valgace.h
 * @brief ValgACE (AnyCubic ACE Pro) backend implementation
 *
 * Implements the AmsBackend interface for AnyCubic ACE Pro systems
 * using the ValgACE Klipper driver. Unlike Happy Hare and AFC which
 * use Moonraker's WebSocket subscriptions, ValgACE exposes a REST API
 * that must be polled for state updates.
 *
 * ValgACE REST Endpoints:
 * - GET /server/ace/info      - System information (model, version, slots)
 * - GET /server/ace/status    - Current state (dryer, loaded slot, action)
 * - GET /server/ace/slots     - Slot information (colors, materials, status)
 *
 * G-code Commands:
 * - ACE_CHANGE_TOOL TOOL={n}  - Load filament from slot n (-1 to unload)
 * - ACE_START_DRYING TEMP={t} DURATION={m}  - Start drying
 * - ACE_STOP_DRYING           - Stop drying
 *
 * Thread Model:
 * - Polling thread runs at ~500ms interval when running_
 * - State is cached under mutex protection
 * - Callbacks invoked on polling thread (consider posting to main thread)
 */
class AmsBackendValgACE : public AmsBackend {
  public:
    /**
     * @brief Construct ValgACE backend
     *
     * @param api Pointer to MoonrakerAPI (for REST calls and G-code)
     * @param client Pointer to MoonrakerClient (for connection state)
     *
     * @note Both pointers must remain valid for the lifetime of this backend.
     */
    AmsBackendValgACE(MoonrakerAPI* api, MoonrakerClient* client);

    ~AmsBackendValgACE() override;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    AmsError start() override;
    void stop() override;
    [[nodiscard]] bool is_running() const override;

    // ========================================================================
    // Events
    // ========================================================================

    void set_event_callback(EventCallback callback) override;

    // ========================================================================
    // State Queries
    // ========================================================================

    [[nodiscard]] AmsSystemInfo get_system_info() const override;
    [[nodiscard]] AmsType get_type() const override;
    [[nodiscard]] SlotInfo get_slot_info(int slot_index) const override;
    [[nodiscard]] AmsAction get_current_action() const override;
    [[nodiscard]] int get_current_tool() const override;
    [[nodiscard]] int get_current_slot() const override;
    [[nodiscard]] bool is_filament_loaded() const override;

    // ========================================================================
    // Path Visualization
    // ========================================================================

    [[nodiscard]] PathTopology get_topology() const override;
    [[nodiscard]] PathSegment get_filament_segment() const override;
    [[nodiscard]] PathSegment get_slot_filament_segment(int slot_index) const override;
    [[nodiscard]] PathSegment infer_error_segment() const override;

    // ========================================================================
    // Filament Operations
    // ========================================================================

    AmsError load_filament(int slot_index) override;
    AmsError unload_filament() override;
    AmsError select_slot(int slot_index) override;
    AmsError change_tool(int tool_number) override;

    // ========================================================================
    // Recovery Operations
    // ========================================================================

    AmsError recover() override;
    AmsError reset() override;
    AmsError cancel() override;

    // ========================================================================
    // Configuration
    // ========================================================================

    AmsError set_slot_info(int slot_index, const SlotInfo& info) override;
    AmsError set_tool_mapping(int tool_number, int slot_index) override;

    // ValgACE has fixed 1:1 mapping (tools ARE slots), not configurable
    [[nodiscard]] helix::printer::ToolMappingCapabilities
    get_tool_mapping_capabilities() const override;
    [[nodiscard]] std::vector<int> get_tool_mapping() const override;

    // ========================================================================
    // Bypass Mode (not supported on ACE Pro)
    // ========================================================================

    AmsError enable_bypass() override;
    AmsError disable_bypass() override;
    [[nodiscard]] bool is_bypass_active() const override;

    // ========================================================================
    // Dryer Control (ValgACE has built-in dryer)
    // ========================================================================

    [[nodiscard]] DryerInfo get_dryer_info() const override;
    AmsError start_drying(float temp_c, int duration_min, int fan_pct = -1) override;
    AmsError stop_drying() override;
    AmsError update_drying(float temp_c = -1, int duration_min = -1, int fan_pct = -1) override;
    [[nodiscard]] std::vector<DryingPreset> get_drying_presets() const override;

    // ========================================================================
    // Device Actions (stub - not yet exposed)
    // ========================================================================

    [[nodiscard]] std::vector<helix::printer::DeviceSection> get_device_sections() const override;
    [[nodiscard]] std::vector<helix::printer::DeviceAction> get_device_actions() const override;
    AmsError execute_device_action(const std::string& action_id,
                                   const std::any& value = {}) override;

  protected:
    // ========================================================================
    // Response Parsing (protected for unit testing)
    // ========================================================================

    /**
     * @brief Parse /server/ace/info response
     * @param data JSON response data
     */
    void parse_info_response(const nlohmann::json& data);

    /**
     * @brief Parse /server/ace/status response
     * @param data JSON response data
     * @return true if state changed (emit event)
     */
    bool parse_status_response(const nlohmann::json& data);

    /**
     * @brief Parse /server/ace/slots response
     * @param data JSON response data
     * @return true if state changed (emit event)
     */
    bool parse_slots_response(const nlohmann::json& data);

  private:
    // ========================================================================
    // Polling Thread
    // ========================================================================

    /**
     * @brief Main polling loop (runs in background thread)
     *
     * Polls /server/ace/status and /server/ace/slots at regular intervals.
     * Updates cached state and emits events on changes.
     */
    void polling_thread_func();

    /**
     * @brief Poll system info (called once on start)
     */
    void poll_info();

    /**
     * @brief Poll current status (dryer, action, loaded slot)
     */
    void poll_status();

    /**
     * @brief Poll slot information (colors, materials, status)
     */
    void poll_slots();

    // ========================================================================
    // Helpers
    // ========================================================================

    /**
     * @brief Emit event to registered callback
     * @param event Event name (EVENT_* constants)
     * @param data Event data (JSON string or empty)
     */
    void emit_event(const std::string& event, const std::string& data = "");

    /**
     * @brief Execute G-code command via MoonrakerAPI
     * @param gcode G-code command string
     * @return AmsError indicating if command was queued
     */
    AmsError execute_gcode(const std::string& gcode);

    /**
     * @brief Check preconditions for operations
     * @return AmsError (SUCCESS if ready, error otherwise)
     */
    AmsError check_preconditions() const;

    /**
     * @brief Validate slot index is in range
     * @param slot_index Slot to validate
     * @return AmsError (SUCCESS if valid)
     */
    AmsError validate_slot_index(int slot_index) const;

    /**
     * @brief Interruptible sleep for polling interval
     * @param ms Milliseconds to sleep
     * @return false if interrupted (stop requested)
     */
    bool interruptible_sleep(int ms);

    // ========================================================================
    // Members
    // ========================================================================

    // Dependencies
    MoonrakerAPI* api_;       ///< For REST calls and G-code
    MoonrakerClient* client_; ///< For connection state checks

    // Threading
    std::thread polling_thread_;              ///< Background polling thread
    std::atomic<bool> running_{false};        ///< Is backend running?
    std::atomic<bool> stop_requested_{false}; ///< Signal thread to exit
    std::condition_variable stop_cv_;         ///< For interruptible sleep
    mutable std::mutex stop_mutex_;           ///< Protects stop_cv_ wait

    // State (protected by state_mutex_)
    mutable std::mutex state_mutex_;        ///< Protects cached state
    AmsSystemInfo system_info_;             ///< Cached system state
    DryerInfo dryer_info_;                  ///< Cached dryer state
    std::atomic<bool> info_fetched_{false}; ///< Have we got /server/ace/info yet?

    // Callback lifetime management
    std::shared_ptr<std::atomic<bool>> alive_; ///< Weak flag for callback safety

    // Events
    EventCallback event_callback_;      ///< Registered event handler
    mutable std::mutex callback_mutex_; ///< Protects callback access

    // Configuration
    static constexpr int POLL_INTERVAL_MS = 500; ///< Polling interval
};
