// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "moonraker_client.h"
#include "preprint_predictor.h"
#include "print_start_profile.h"
#include "printer_state.h"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <string>

/**
 * @file print_start_collector.h
 * @brief Monitors G-code responses to detect PRINT_START macro phases
 *
 * Subscribes to Moonraker's notify_gcode_response to parse G-code output
 * during print initialization. Detects common PRINT_START phases like
 * homing, heating, QGL, bed mesh, and purging through pattern matching.
 *
 * ## Usage
 * The collector is started when a print begins and stopped when the print
 * transitions to actual printing (or is cancelled). Progress is reported
 * through helix::PrinterState subjects which XML can bind to directly.
 *
 * ## Pattern Detection
 * Uses best-effort regex matching on G-code responses. Not all macros will
 * output all phases - the progress calculation handles missing phases gracefully.
 *
 * @see helix::PrintStartPhase enum in printer_state.h
 */
class PrintStartCollector : public std::enable_shared_from_this<PrintStartCollector> {
  public:
    /**
     * @brief Construct a PrintStartCollector
     * @param client helix::MoonrakerClient for registering callbacks
     * @param state helix::PrinterState to update with phase progress
     */
    PrintStartCollector(helix::MoonrakerClient& client, helix::PrinterState& state);

    ~PrintStartCollector();

    // Non-copyable
    PrintStartCollector(const PrintStartCollector&) = delete;
    PrintStartCollector& operator=(const PrintStartCollector&) = delete;

    /**
     * @brief Start monitoring for PRINT_START phases
     *
     * Registers for notify_gcode_response notifications and begins
     * parsing G-code output for phase detection patterns.
     */
    void start();

    /**
     * @brief Stop monitoring
     *
     * Unregisters callback and resets state. Called when print
     * initialization completes or print is cancelled.
     */
    void stop();

    /**
     * @brief Check if collector is currently active
     */
    [[nodiscard]] bool is_active() const {
        return active_.load();
    }

    /**
     * @brief Reset detected phases (for new print)
     */
    void reset();

    /**
     * @brief Check fallback completion conditions
     *
     * Called by observers when layer count or progress changes.
     * Checks multiple fallback signals for printers that don't emit
     * layer markers in G-code responses (e.g., FlashForge AD5M).
     */
    void check_fallback_completion();

    /**
     * @brief Enable fallback detection after initial G-code response window
     *
     * Called shortly after start() to enable fallback signals.
     * Gives G-code response detection priority for the first few seconds.
     */
    void enable_fallbacks();

    /**
     * @brief Set the print start profile for pattern/signal matching
     *
     * Must be called before start(). Ignored if the collector is active.
     *
     * @param profile Profile to use, or nullptr to disable profile-based matching
     */
    void set_profile(std::shared_ptr<PrintStartProfile> profile);

    /**
     * @brief Get the predictor for reading predictions
     *
     * Thread-safe: predictor is loaded on start() and entries added on COMPLETE,
     * both under state_mutex_. Callers (LVGL timer) should use remaining_seconds()
     * which is const and safe to call from main thread.
     */
    [[nodiscard]] const helix::PreprintPredictor& predictor() const {
        return predictor_;
    }

    /**
     * @brief Get detected phases as int set (for predictor remaining calculation)
     *
     * Must be called under state_mutex_ or from main thread when collector stopped.
     */
    [[nodiscard]] std::set<int> get_completed_phase_ints() const;

    /**
     * @brief Get current phase as int
     */
    [[nodiscard]] int get_current_phase_int() const;

    /**
     * @brief Get elapsed seconds in current phase
     */
    [[nodiscard]] int get_current_phase_elapsed_seconds() const;

  private:
    /**
     * @brief Handle incoming G-code response
     */
    void on_gcode_response(const nlohmann::json& msg);

    /**
     * @brief Check line against phase patterns
     */
    void check_phase_patterns(const std::string& line);

    /**
     * @brief Check for HELIX:PHASE:* signals from plugin/macros
     *
     * These are definitive signals that take priority over regex detection.
     * Format: "HELIX:PHASE:STARTING", "HELIX:PHASE:HOMING", "HELIX:PHASE:COMPLETE", etc.
     *
     * @return true if a HELIX:PHASE signal was detected and handled
     */
    bool check_helix_phase_signal(const std::string& line);

    /**
     * @brief Update phase and recalculate progress (weighted mode)
     */
    void update_phase(helix::PrintStartPhase phase, const char* message);

    /**
     * @brief Update phase with explicit progress value (sequential mode)
     */
    void update_phase(helix::PrintStartPhase phase, const std::string& message, int progress);

    /**
     * @brief Calculate overall progress based on detected phases
     */
    int calculate_progress() const;

    /**
     * @brief Calculate progress (must be called with state_mutex_ held)
     */
    int calculate_progress_locked() const;

    /**
     * @brief Check for PRINT_START start marker
     */
    bool is_print_start_marker(const std::string& line) const;

    /**
     * @brief Check for print start completion (layer 1, etc.)
     */
    bool is_completion_marker(const std::string& line) const;

    // Dependencies
    helix::MoonrakerClient& client_;
    helix::PrinterState& state_;

    // Registration state
    std::string handler_name_;
    std::atomic<bool> active_{false};
    std::atomic<bool> registered_{false};

    // Thread safety: protects all non-atomic members below
    // WebSocket callbacks run on background thread, check_fallback_completion() runs on main thread
    mutable std::mutex state_mutex_;

    // Phase tracking (protected by state_mutex_)
    std::set<helix::PrintStartPhase> detected_phases_;
    helix::PrintStartPhase current_phase_ = helix::PrintStartPhase::IDLE;
    bool print_start_detected_ = false;
    int max_sequential_progress_ = 0; // Monotonic progress guard for sequential mode
    std::chrono::steady_clock::time_point printing_state_start_;

    // Profile for signal/pattern matching (set via set_profile() or loaded by start())
    std::shared_ptr<PrintStartProfile> profile_;

    // Universal patterns (not profile-specific)
    static const std::regex print_start_pattern_;
    static const std::regex completion_pattern_;

    // Fallback detection constants
    static constexpr auto FALLBACK_TIMEOUT = std::chrono::seconds(45);
    static constexpr int TEMP_TOLERANCE_DECIDEGREES = 50; // 5°C (temps stored as value * 10)

    // Fallback detection state (for printers without G-code layer markers)
    std::atomic<bool> fallbacks_enabled_{false};
    std::atomic<helix::SubscriptionId> macro_subscription_id_{0};

    // Phase timing for duration prediction (protected by state_mutex_)
    std::map<int, std::chrono::steady_clock::time_point> phase_enter_times_;
    helix::PreprintPredictor predictor_;

    // LVGL timer for periodic ETA updates (main thread only)
    lv_timer_t* eta_timer_ = nullptr;
    static constexpr uint32_t ETA_UPDATE_INTERVAL_MS = 5000;

    /**
     * @brief Update ETA display from timer callback (main thread)
     */
    void update_eta_display();

    /**
     * @brief Load prediction entries from helix::Config on start()
     */
    void load_prediction_history();

    /**
     * @brief Save current print's phase timings to prediction history
     *
     * Called on COMPLETE. Computes per-phase durations from timestamps,
     * adds entry to predictor, and persists to Config.
     */
    void save_prediction_entry();
};
