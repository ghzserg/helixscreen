// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_backend.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

/**
 * @file ams_backend_mock.h
 * @brief Mock AMS backend for development and testing
 *
 * Provides a simulated multi-filament system with configurable slots,
 * fake operation timing, and predictable state for UI development.
 *
 * Features:
 * - Configurable slot count (default 4)
 * - Simulated load/unload timing
 * - Pre-populated filament colors and materials
 * - Responds to all AmsBackend operations
 */
class AmsBackendMock : public AmsBackend {
  public:
    /**
     * @brief Construct mock backend with specified slot count
     * @param slot_count Number of simulated slots (1-16, default 4)
     */
    explicit AmsBackendMock(int slot_count = 4);

    ~AmsBackendMock() override;

    // Lifecycle
    AmsError start() override;
    void stop() override;
    [[nodiscard]] bool is_running() const override;

    // Events
    void set_event_callback(EventCallback callback) override;

    // State queries
    [[nodiscard]] AmsSystemInfo get_system_info() const override;
    [[nodiscard]] AmsType get_type() const override;
    [[nodiscard]] SlotInfo get_slot_info(int slot_index) const override;
    [[nodiscard]] AmsAction get_current_action() const override;
    [[nodiscard]] int get_current_tool() const override;
    [[nodiscard]] int get_current_slot() const override;
    [[nodiscard]] bool is_filament_loaded() const override;

    // Path visualization
    [[nodiscard]] PathTopology get_topology() const override;
    [[nodiscard]] PathSegment get_filament_segment() const override;
    [[nodiscard]] PathSegment get_slot_filament_segment(int slot_index) const override;
    [[nodiscard]] PathSegment infer_error_segment() const override;

    // Operations
    AmsError load_filament(int slot_index) override;
    AmsError unload_filament() override;
    AmsError select_slot(int slot_index) override;
    AmsError change_tool(int tool_number) override;

    // Recovery
    AmsError recover() override;
    AmsError reset() override;
    AmsError cancel() override;

    // Configuration
    AmsError set_slot_info(int slot_index, const SlotInfo& info) override;
    AmsError set_tool_mapping(int tool_number, int slot_index) override;

    // Bypass mode
    AmsError enable_bypass() override;
    AmsError disable_bypass() override;
    [[nodiscard]] bool is_bypass_active() const override;

    // Dryer
    [[nodiscard]] DryerInfo get_dryer_info() const override;
    AmsError start_drying(float temp_c, int duration_min, int fan_pct = -1) override;
    AmsError stop_drying() override;

    // Endless spool
    [[nodiscard]] helix::printer::EndlessSpoolCapabilities
    get_endless_spool_capabilities() const override;
    [[nodiscard]] std::vector<helix::printer::EndlessSpoolConfig>
    get_endless_spool_config() const override;
    AmsError set_endless_spool_backup(int slot_index, int backup_slot) override;

    // Tool mapping
    [[nodiscard]] helix::printer::ToolMappingCapabilities
    get_tool_mapping_capabilities() const override;
    [[nodiscard]] std::vector<int> get_tool_mapping() const override;

    // ========================================================================
    // Mock-specific methods (for testing)
    // ========================================================================

    /**
     * @brief Simulate an error condition
     * @param error The error to trigger
     */
    void simulate_error(AmsResult error);

    /**
     * @brief Set operation delay for simulated timing
     * @param delay_ms Delay in milliseconds (0 for instant)
     */
    void set_operation_delay(int delay_ms);

    /**
     * @brief Force a specific slot status (for testing)
     * @param slot_index Slot to modify
     * @param status New status
     */
    void force_slot_status(int slot_index, SlotStatus status);

    /**
     * @brief Set whether this mock simulates a hardware bypass sensor
     * @param has_sensor true=hardware sensor (auto-detect), false=virtual (manual toggle)
     *
     * When has_sensor is true:
     * - The bypass button should be disabled in the UI
     * - Bypass is controlled by the sensor, not user clicks
     */
    void set_has_hardware_bypass_sensor(bool has_sensor);

    /**
     * @brief Enable dryer simulation for this mock
     * @param enabled true to simulate a dryer, false to disable
     *
     * When enabled, the mock will:
     * - Report dryer_supported = true in get_dryer_info()
     * - Simulate temperature ramping and progress when drying
     * - Support start_drying() and stop_drying() commands
     */
    void set_dryer_enabled(bool enabled);

    /**
     * @brief Set dryer simulation speed multiplier
     * @param speed_x Speed multiplier (default 60 = 1 real second = 1 simulated minute)
     *
     * Can also be set via HELIX_MOCK_DRYER_SPEED environment variable.
     * Set to 1 for real-time, 60 for fast testing (4h = 4min), 3600 for instant.
     */
    void set_dryer_speed(int speed_x);

    /**
     * @brief Enable realistic multi-phase operation mode
     * @param enabled true for HEATING→LOADING→CHECKING sequences
     *
     * When enabled, operations show realistic phase progression:
     * - Load: HEATING → LOADING (segment animation) → CHECKING → IDLE
     * - Unload: HEATING → FORMING_TIP → UNLOADING (animation) → IDLE
     *
     * Can also be set via HELIX_MOCK_AMS_REALISTIC environment variable.
     * Timing respects --sim-speed flag with ±20-30% variance.
     */
    void set_realistic_mode(bool enabled);

    /**
     * @brief Check if realistic mode is enabled
     * @return true if multi-phase operations are simulated
     */
    [[nodiscard]] bool is_realistic_mode() const;

    /**
     * @brief Enable tool changer simulation mode
     * @param enabled true to simulate a tool changer, false for filament system
     *
     * When enabled, the mock will:
     * - Report type as TOOL_CHANGER instead of HAPPY_HARE
     * - Use PARALLEL path topology (each slot → own toolhead)
     * - Disable bypass mode (not applicable for tool changers)
     * - Label slots as "T0", "T1", etc.
     *
     * Can also be set via HELIX_MOCK_AMS_TYPE=toolchanger environment variable.
     */
    void set_tool_changer_mode(bool enabled);

    /**
     * @brief Check if tool changer mode is enabled
     * @return true if simulating a tool changer
     */
    [[nodiscard]] bool is_tool_changer_mode() const;

    /**
     * @brief Set whether endless spool is supported
     * @param supported true to enable endless spool support
     *
     * When disabled, get_endless_spool_capabilities() returns supported=false.
     */
    void set_endless_spool_supported(bool supported);

    /**
     * @brief Set whether endless spool configuration is editable
     * @param editable true for AFC-style (editable), false for Happy Hare-style (read-only)
     *
     * When editable=false, set_endless_spool_backup() returns NOT_SUPPORTED.
     */
    void set_endless_spool_editable(bool editable);

  private:
    /**
     * @brief Initialize mock state with sample data
     */
    void init_mock_data();

    /**
     * @brief Emit event to registered callback
     * @param event Event name
     * @param data Event data (JSON or empty)
     */
    void emit_event(const std::string& event, const std::string& data = "");

    /**
     * @brief Simulate async operation completion
     * @param action Action being performed
     * @param complete_event Event to emit on completion
     * @param slot_index Slot involved (-1 if N/A)
     */
    void schedule_completion(AmsAction action, const std::string& complete_event,
                             int slot_index = -1);

    /**
     * @brief Wait for any active operation thread to complete
     */
    void wait_for_operation_thread();

    // Realistic mode helpers (multi-phase operations)
    using InterruptibleSleep = std::function<bool(int)>;

    /**
     * @brief Get delay with speedup and optional variance applied
     * @param base_ms Base delay in milliseconds (at 1x speed)
     * @param variance Variance factor (0.2 = ±20%, 0 = no variance)
     * @return Effective delay considering RuntimeConfig::sim_speedup
     */
    int get_effective_delay_ms(int base_ms, float variance = 0.0f) const;

    /**
     * @brief Update action state with thread safety
     * @param action New action state
     * @param detail Operation detail string
     */
    void set_action(AmsAction action, const std::string& detail);

    /**
     * @brief Execute load operation with optional multi-phase sequence
     * @param slot_index Slot being loaded from
     * @param interruptible_sleep Sleep function that respects shutdown
     */
    void execute_load_operation(int slot_index, InterruptibleSleep interruptible_sleep);

    /**
     * @brief Execute unload operation with optional multi-phase sequence
     * @param interruptible_sleep Sleep function that respects shutdown
     */
    void execute_unload_operation(InterruptibleSleep interruptible_sleep);

    /**
     * @brief Animate filament through load path segments
     * @param slot_index Slot being loaded from
     * @param interruptible_sleep Sleep function that respects shutdown
     */
    void run_load_segment_animation(int slot_index, InterruptibleSleep interruptible_sleep);

    /**
     * @brief Animate filament through unload path segments (reverse)
     * @param interruptible_sleep Sleep function that respects shutdown
     */
    void run_unload_segment_animation(InterruptibleSleep interruptible_sleep);

    /**
     * @brief Finalize state after successful load
     * @param slot_index Slot that was loaded
     */
    void finalize_load_state(int slot_index);

    /**
     * @brief Finalize state after successful unload
     */
    void finalize_unload_state();

    mutable std::mutex mutex_;         ///< Protects state access
    std::atomic<bool> running_{false}; ///< Backend running state
    EventCallback event_callback_;     ///< Registered event handler

    AmsSystemInfo system_info_;    ///< Simulated system state
    int operation_delay_ms_ = 500; ///< Simulated operation delay
    bool realistic_mode_ = false;  ///< Enable multi-phase operations (HEATING→LOADING→CHECKING)

    // Path visualization state
    PathTopology topology_ = PathTopology::HUB;        ///< Simulated topology (default hub for AFC)
    PathSegment filament_segment_ = PathSegment::NONE; ///< Current filament position
    PathSegment error_segment_ = PathSegment::NONE;    ///< Error location (if any)

    // Thread-safe shutdown support
    std::thread operation_thread_;                      ///< Current operation thread (if any)
    std::atomic<bool> operation_thread_running_{false}; ///< Guards against double-join
    std::atomic<bool> shutdown_requested_{false};       ///< Signal thread to exit
    std::atomic<bool> cancel_requested_{false};         ///< Signal operation cancellation
    std::condition_variable shutdown_cv_;               ///< For interruptible sleep
    mutable std::mutex shutdown_mutex_;                 ///< Protects shutdown_cv_ wait

    // Dryer simulation state
    bool dryer_enabled_ = false;                    ///< Whether dryer is simulated
    DryerInfo dryer_state_;                         ///< Current dryer state
    std::thread dryer_thread_;                      ///< Background thread for dryer simulation
    std::atomic<bool> dryer_thread_running_{false}; ///< Guards against double-join
    std::atomic<bool> dryer_stop_requested_{false}; ///< Signal dryer thread to stop
    int dryer_speed_x_ = 60; ///< Speed multiplier (60 = 1 real sec = 1 sim min)

    // Tool changer mode (alternative to filament system simulation)
    bool tool_changer_mode_ = false; ///< Simulate tool changer instead of filament system

    // Endless spool simulation state
    bool endless_spool_supported_ = true; ///< Whether endless spool is supported
    bool endless_spool_editable_ = true;  ///< Whether config is editable (AFC) vs read-only (HH)
    std::vector<helix::printer::EndlessSpoolConfig>
        endless_spool_configs_; ///< Per-slot backup config
};
