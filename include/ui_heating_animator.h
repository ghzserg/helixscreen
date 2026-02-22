// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

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
 *   // Call on every temperature update (temperatures in centidegrees):
 *   animator.update(current_temp_centi, target_temp_centi);
 *   // Cleanup:
 *   animator.detach();
 *
 * State machine:
 *   OFF ──(target > 0)──► HEATING ──(current ≥ target-20)──► AT_TARGET
 *    ▲                        │                                  │
 *    └────(target = 0)────────┴──────────(target = 0)────────────┘
 *
 * Note: 20 centidegrees = 2°C tolerance
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
     * @param current_temp Current temperature in centidegrees (31.5°C = 315)
     * @param target_temp Target temperature in centidegrees (0 = heater off)
     */
    void update(int current_temp, int target_temp);

    /**
     * @brief Refresh colors from theme (call after theme toggle)
     *
     * Re-fetches color values from the current theme and re-applies them.
     * This ensures the icon color updates when switching between light/dark mode.
     */
    void refresh_theme();

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
    /// Temperature tolerance for "at target" detection in centidegrees (2°C = 20)
    static constexpr int TEMP_TOLERANCE = 20;

    /// Pulse animation opacity range (80% to 100%)
    static constexpr lv_opa_t PULSE_OPA_MIN = 204; // ~80%
    static constexpr lv_opa_t PULSE_OPA_MAX = 255; // 100%

    /// Pulse animation duration (one direction)
    static constexpr uint32_t PULSE_DURATION_MS = 400;

    lv_obj_t* icon_ = nullptr;
    State state_ = State::OFF;

    int ambient_temp_ = 250; ///< Captured when heating starts (centidegrees)
    int current_temp_ = 250; ///< Current temperature (centidegrees)
    int target_temp_ = 0;    ///< Target temperature (centidegrees)

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
     * @brief RAII observer for theme/dark mode changes
     * ObserverGuard auto-removes the observer on destruction/reset.
     */
    ObserverGuard theme_observer_;

    /**
     * @brief Static callback for theme change observer
     */
    static void theme_change_cb(lv_observer_t* observer, lv_subject_t* subject);

    /**
     * @brief Animation callback for pulse effect
     */
    static void pulse_anim_cb(void* var, int32_t value);
};
