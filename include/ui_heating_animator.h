// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"

/**
 * @brief Animates heating icons with gradient color and pulse effects
 *
 * This class provides visual feedback for heating progress on temperature icons.
 * When heating is active, the icon:
 * - Displays a color gradient from cold (blue) → warm (amber) → hot (red)
 *   based on progress from ambient temperature to target
 * - Pulses (opacity oscillation) while actively heating
 * - Stops pulsing and shows solid hot color when at target temperature
 *
 * Usage:
 *   HeatingIconAnimator animator;
 *   animator.attach(icon_widget);
 *   // Call on every temperature update:
 *   animator.update(current_temp, target_temp);
 *   // Cleanup:
 *   animator.detach();
 *
 * State machine:
 *   OFF ──(target > 0)──► HEATING ──(current ≥ target-2)──► AT_TARGET
 *    ▲                        │                                  │
 *    └────(target = 0)────────┴──────────(target = 0)────────────┘
 */
class HeatingIconAnimator {
  public:
    /**
     * @brief Heating states
     */
    enum class State {
        OFF,      ///< Heater off (target = 0), secondary color, no animation
        HEATING,  ///< Actively heating, gradient color + pulse animation
        AT_TARGET ///< At target temperature, solid hot color, no pulse
    };

    HeatingIconAnimator() = default;
    ~HeatingIconAnimator();

    // Non-copyable (owns animation state)
    HeatingIconAnimator(const HeatingIconAnimator&) = delete;
    HeatingIconAnimator& operator=(const HeatingIconAnimator&) = delete;

    // Movable
    HeatingIconAnimator(HeatingIconAnimator&& other) noexcept;
    HeatingIconAnimator& operator=(HeatingIconAnimator&& other) noexcept;

    /**
     * @brief Attach animator to an icon widget
     *
     * @param icon LVGL icon widget (created by ui_icon)
     */
    void attach(lv_obj_t* icon);

    /**
     * @brief Detach from icon and cleanup animations
     */
    void detach();

    /**
     * @brief Update heating state based on current and target temperatures
     *
     * Call this whenever temperature readings change. The animator will:
     * - Capture ambient temperature when heating starts
     * - Calculate progress and update gradient color
     * - Start/stop pulse animation based on state transitions
     *
     * @param current_temp Current temperature in °C
     * @param target_temp Target temperature in °C (0 = heater off)
     */
    void update(int current_temp, int target_temp);

    /**
     * @brief Get current heating state
     */
    State get_state() const {
        return state_;
    }

    /**
     * @brief Check if animator is attached to an icon
     */
    bool is_attached() const {
        return icon_ != nullptr;
    }

  private:
    /// Temperature tolerance for "at target" detection (matches TempControlPanel)
    static constexpr int TEMP_TOLERANCE = 2;

    /// Pulse animation opacity range (80% to 100%)
    static constexpr lv_opa_t PULSE_OPA_MIN = 204; // ~80%
    static constexpr lv_opa_t PULSE_OPA_MAX = 255; // 100%

    /// Pulse animation duration (one direction)
    static constexpr uint32_t PULSE_DURATION_MS = 400;

    lv_obj_t* icon_ = nullptr;
    State state_ = State::OFF;

    int ambient_temp_ = 25; ///< Captured when heating starts
    int current_temp_ = 25;
    int target_temp_ = 0;

    lv_color_t current_color_; ///< Current gradient color
    lv_opa_t current_opacity_ = LV_OPA_COVER;

    bool pulse_active_ = false;

    /**
     * @brief Calculate gradient color based on heating progress
     *
     * Uses a two-segment gradient:
     * - 0-50%: cold (blue) → warm (amber)
     * - 50-100%: warm (amber) → hot (red)
     *
     * @param progress Heating progress 0.0 to 1.0
     * @return Interpolated color
     */
    lv_color_t calculate_gradient_color(float progress);

    /**
     * @brief Start pulse animation (opacity oscillation)
     */
    void start_pulse();

    /**
     * @brief Stop pulse animation
     */
    void stop_pulse();

    /**
     * @brief Apply current color and opacity to icon
     */
    void apply_color();

    /**
     * @brief Get secondary (off) color from theme
     */
    lv_color_t get_secondary_color();

    /**
     * @brief Animation callback for pulse effect
     */
    static void pulse_anim_cb(void* var, int32_t value);
};
