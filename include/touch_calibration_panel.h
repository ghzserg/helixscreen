// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 356C LLC

#pragma once

#include "touch_calibration.h"

#include <functional>
#include <lvgl.h>

namespace helix {

/**
 * @brief Touch calibration panel state machine
 *
 * Manages a 3-point touch calibration workflow:
 *
 * States:
 *   IDLE -> POINT_1 -> POINT_2 -> POINT_3 -> VERIFY -> COMPLETE
 *            |          |          |          |
 *            v          v          v          v
 *        (capture)  (capture)  (capture)  (accept/retry)
 *
 * Usage:
 * 1. Create panel and set screen size
 * 2. Set completion callback
 * 3. Call start() to begin calibration
 * 4. Display target at get_target_position(step)
 * 5. Call capture_point() when user touches screen
 * 6. In VERIFY state, display calibration and allow accept()/retry()
 * 7. Callback invoked with calibration data on accept() or nullptr on cancel()
 */
class TouchCalibrationPanel {
  public:
    /**
     * @brief Calibration state machine states
     */
    enum class State {
        IDLE,    ///< Not calibrating
        POINT_1, ///< Waiting for first calibration point
        POINT_2, ///< Waiting for second calibration point
        POINT_3, ///< Waiting for third calibration point
        VERIFY,  ///< Calibration computed, waiting for accept/retry
        COMPLETE ///< Calibration accepted
    };

    /**
     * @brief Callback invoked when calibration completes or is cancelled
     *
     * @param cal Pointer to calibration data if accepted, nullptr if cancelled
     */
    using CompletionCallback = std::function<void(const TouchCalibration* cal)>;

    /**
     * @brief Callback invoked when calibration fails (e.g., degenerate points)
     *
     * @param reason Human-readable failure reason
     */
    using FailureCallback = std::function<void(const char* reason)>;

    /**
     * @brief Callback invoked each second during verify countdown
     * @param seconds_remaining Seconds until timeout (15, 14, 13, ...)
     */
    using CountdownCallback = std::function<void(int seconds_remaining)>;

    /**
     * @brief Callback invoked when verify timeout expires without accept
     */
    using TimeoutCallback = std::function<void()>;

    TouchCalibrationPanel();
    ~TouchCalibrationPanel();

    // Non-copyable
    TouchCalibrationPanel(const TouchCalibrationPanel&) = delete;
    TouchCalibrationPanel& operator=(const TouchCalibrationPanel&) = delete;

    // Movable
    TouchCalibrationPanel(TouchCalibrationPanel&&) = default;
    TouchCalibrationPanel& operator=(TouchCalibrationPanel&&) = default;

    /**
     * @brief Set the completion callback
     * @param cb Callback to invoke when calibration completes or is cancelled
     */
    void set_completion_callback(CompletionCallback cb);

    /**
     * @brief Set the failure callback
     * @param cb Callback to invoke when calibration fails (degenerate points, etc.)
     */
    void set_failure_callback(FailureCallback cb);

    /**
     * @brief Set callback for countdown ticks during verify state
     */
    void set_countdown_callback(CountdownCallback cb);

    /**
     * @brief Set callback for timeout expiration
     */
    void set_timeout_callback(TimeoutCallback cb);

    /**
     * @brief Set verify timeout duration (default: 15 seconds)
     */
    void set_verify_timeout_seconds(int seconds);

    /**
     * @brief Set the screen dimensions for target position calculations
     * @param width Screen width in pixels
     * @param height Screen height in pixels
     */
    void set_screen_size(int width, int height);

    /**
     * @brief Start or restart calibration
     *
     * Transitions to POINT_1 state.
     */
    void start();

    /**
     * @brief Capture a raw touch point for the current calibration step
     * @param raw Raw touch coordinates from touch controller
     *
     * Only valid in POINT_1, POINT_2, or POINT_3 states.
     * Advances to next state after capture.
     */
    void capture_point(Point raw);

    /**
     * @brief Add a raw touch sample to the current capture buffer
     *
     * Collects multiple samples per calibration point. When SAMPLES_REQUIRED
     * samples have been collected, filters out ADC-saturated values, computes
     * the median, and advances the state machine via capture_point().
     *
     * @param raw Raw touch coordinates from touch controller
     */
    void add_sample(Point raw);

    /**
     * @brief Accept the computed calibration
     *
     * Only valid in VERIFY state.
     * Transitions to COMPLETE and invokes callback with calibration data.
     */
    void accept();

    /**
     * @brief Retry calibration from the beginning
     *
     * Only valid in VERIFY state.
     * Transitions back to POINT_1 and clears captured points.
     */
    void retry();

    /**
     * @brief Cancel calibration
     *
     * Returns to IDLE state and invokes callback with nullptr.
     */
    void cancel();

    /**
     * @brief Get current state
     * @return Current state machine state
     */
    State get_state() const;

    /**
     * @brief Get target position for a calibration step
     * @param step Step number (0, 1, or 2)
     * @return Screen coordinates where target should be displayed
     *
     * Returns (0, 0) for out-of-range step values.
     *
     * Default target positions (for 800x480 screen):
     *   Step 0: (120, 86)  - 15% from left, 18% from top
     *   Step 1: (400, 408) - center X, 85% from top
     *   Step 2: (680, 86)  - 85% from left, 18% from top
     */
    Point get_target_position(int step) const;

    /**
     * @brief Get computed calibration data
     * @return Pointer to calibration if in VERIFY/COMPLETE state, nullptr otherwise
     */
    const TouchCalibration* get_calibration() const;

  private:
    State state_ = State::IDLE;
    int screen_width_ = 800;
    int screen_height_ = 480;
    CompletionCallback callback_;
    FailureCallback failure_callback_;
    CountdownCallback countdown_callback_;
    TimeoutCallback timeout_callback_;
    int verify_timeout_seconds_ = 15;
    int countdown_remaining_ = 0;
    lv_timer_t* countdown_timer_ = nullptr;

    Point screen_points_[3]; ///< Target screen positions
    Point touch_points_[3];  ///< Captured raw touch positions
    TouchCalibration calibration_;

    // Multi-sample filtering
    static constexpr int SAMPLES_REQUIRED = 7;
    static constexpr int MIN_VALID_SAMPLES = 3;

    struct RawSample {
        int x = 0;
        int y = 0;
    };
    RawSample sample_buffer_[SAMPLES_REQUIRED]{};
    int sample_count_ = 0;

    /// Check if a sample has ADC-saturated values
    static bool is_saturated_sample(const Point& sample);

    /// Compute median from valid samples in the buffer
    bool compute_median_point(Point& out);

    /// Reset sample buffer for new point capture
    void reset_samples();

    /// Calculate target position for a given step using screen dimensions
    Point compute_target_position(int step) const;

    /// Start countdown timer when entering VERIFY state
    void start_countdown_timer();

    /// Stop countdown timer
    void stop_countdown_timer();

    /// Timer callback - static member
    static void countdown_timer_cb(lv_timer_t* timer);

    // Calibration target positions as screen ratios
    // These form a well-distributed triangle for accurate affine transform
    // Y ratios pushed to 18%-85% for maximum spread within wizard content area
    // (Content area is ~16%-87% of screen height, between header and footer)
    static constexpr float TARGET_0_X_RATIO = 0.15f; ///< 15% from left edge
    static constexpr float TARGET_0_Y_RATIO = 0.18f; ///< 18% from top (near content top)
    static constexpr float TARGET_1_X_RATIO = 0.50f; ///< Center X
    static constexpr float TARGET_1_Y_RATIO = 0.85f; ///< 85% from top (near content bottom)
    static constexpr float TARGET_2_X_RATIO = 0.85f; ///< 85% from left
    static constexpr float TARGET_2_Y_RATIO = 0.18f; ///< 18% from top (near content top)
};

} // namespace helix
