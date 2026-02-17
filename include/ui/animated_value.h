// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file animated_value.h
 * @brief Animates display values when underlying data subjects change
 *
 * AnimatedValue intercepts subject changes and smoothly transitions the
 * displayed value from old to new, providing premium "micro-animation" polish.
 *
 * Key features:
 * - Retarget pattern: mid-animation value changes chase the new target
 * - Threshold skipping: ignores tiny changes to prevent jitter
 * - Animation toggle: respects SettingsManager::get_animations_enabled()
 * - RAII cleanup: automatically stops animation on destruction
 *
 * @pattern Observer + Animation - Subjects update DATA, animations update DISPLAY
 *
 * Usage:
 * @code{.cpp}
 *   AnimatedValue<int> animated_temp_;
 *
 *   void setup() {
 *       animated_temp_.bind(
 *           printer_state_.get_active_extruder_temp_subject(),
 *           [this](int centi) {
 *               int deg = centi / 10;
 *               snprintf(buf_, sizeof(buf_), "%d°", deg);
 *               lv_label_set_text(label_, buf_);
 *           },
 *           {.duration_ms = 500, .threshold = 5}  // 0.5°C threshold
 *       );
 *   }
 * @endcode
 */

#pragma once

#include "ui_observer_guard.h"

#include "lvgl/lvgl.h"
#include "settings_manager.h"

#include <spdlog/spdlog.h>

#include <functional>

namespace helix::ui {

/**
 * @brief Configuration for animated value transitions
 */
struct AnimatedValueConfig {
    /// Animation duration in milliseconds
    uint32_t duration_ms = 300;

    /// Easing function (LVGL path callback)
    lv_anim_path_cb_t easing = lv_anim_path_ease_out;

    /// Skip animation if abs(new - current) < threshold
    int threshold = 0;
};

/**
 * @brief Animates a value when its underlying subject changes
 *
 * @tparam T Value type (must be convertible to/from int32_t for LVGL animation)
 */
template <typename T> class AnimatedValue {
  public:
    /// Callback invoked with the current display value during animation
    using DisplayCallback = std::function<void(T)>;

    AnimatedValue() = default;

    ~AnimatedValue() {
        unbind();
    }

    // Non-copyable (owns animation state)
    AnimatedValue(const AnimatedValue&) = delete;
    AnimatedValue& operator=(const AnimatedValue&) = delete;

    // Movable
    AnimatedValue(AnimatedValue&& other) noexcept
        : subject_(other.subject_), observer_(std::move(other.observer_)),
          display_callback_(std::move(other.display_callback_)), config_(other.config_),
          display_value_(other.display_value_), target_value_(other.target_value_),
          anim_running_(other.anim_running_), bound_(other.bound_) {
        other.subject_ = nullptr;
        other.anim_running_ = false;
        other.bound_ = false;
    }

    AnimatedValue& operator=(AnimatedValue&& other) noexcept {
        if (this != &other) {
            unbind();
            subject_ = other.subject_;
            observer_ = std::move(other.observer_);
            display_callback_ = std::move(other.display_callback_);
            config_ = other.config_;
            display_value_ = other.display_value_;
            target_value_ = other.target_value_;
            anim_running_ = other.anim_running_;
            bound_ = other.bound_;
            other.subject_ = nullptr;
            other.anim_running_ = false;
            other.bound_ = false;
        }
        return *this;
    }

    /**
     * @brief Bind to a subject and start observing changes
     *
     * @param subject LVGL subject to observe (must be int type)
     * @param on_display Callback invoked with display value during animation
     * @param config Animation configuration
     */
    void bind(lv_subject_t* subject, DisplayCallback on_display, AnimatedValueConfig config = {}) {
        if (!subject || !on_display) {
            return;
        }

        unbind();

        subject_ = subject;
        display_callback_ = std::move(on_display);
        config_ = config;
        bound_ = true;

        // Initialize to current subject value
        display_value_ = static_cast<T>(lv_subject_get_int(subject));
        target_value_ = display_value_;

        // Invoke callback with initial value
        display_callback_(display_value_);

        // Create observer for subject changes
        observer_ = ObserverGuard(subject, observer_callback, this);
    }

    /**
     * @brief Unbind from subject and stop animations
     */
    void unbind() {
        if (!bound_) {
            return;
        }

        stop_animation();
        observer_.reset();
        subject_ = nullptr;
        bound_ = false;
    }

    /**
     * @brief Get current display value (may be mid-animation)
     */
    T display_value() const {
        return display_value_;
    }

    /**
     * @brief Get target value (final value after animation completes)
     */
    T target_value() const {
        return target_value_;
    }

    /**
     * @brief Check if animation is currently running
     */
    bool is_animating() const {
        return anim_running_;
    }

    /**
     * @brief Check if bound to a subject
     */
    bool is_bound() const {
        return bound_;
    }

  private:
    lv_subject_t* subject_ = nullptr;
    ObserverGuard observer_;
    DisplayCallback display_callback_;
    AnimatedValueConfig config_;

    T display_value_ = T{};
    T target_value_ = T{};
    bool anim_running_ = false;
    bool bound_ = false;

    /**
     * @brief Handle subject value change
     *
     * If already animating toward a similar target, just update the target
     * without restarting. This prevents thrashing when updates come faster
     * than the animation can progress.
     */
    void on_subject_changed(T new_value) {
        // Check threshold from DISPLAY value (what user sees)
        T delta_from_display = new_value > display_value_ ? (new_value - display_value_)
                                                          : (display_value_ - new_value);

        spdlog::trace("[AnimatedValue] on_subject_changed: new={}, display={}, target={}, delta={}",
                      new_value, display_value_, target_value_, delta_from_display);

        // If animation is running, just update target - don't restart
        // This allows smooth "chasing" behavior where animation continues toward new target
        if (anim_running_) {
            target_value_ = new_value;
            spdlog::trace("[AnimatedValue] Animation running, updated target to {}", new_value);
            return;
        }

        if (delta_from_display < static_cast<T>(config_.threshold)) {
            // Change too small from current display - update silently
            target_value_ = new_value;
            spdlog::trace("[AnimatedValue] Below threshold, skipping animation");
            return;
        }

        target_value_ = new_value;

        // Check if animations are enabled
        if (!SettingsManager::instance().get_animations_enabled()) {
            // Instant update
            spdlog::trace("[AnimatedValue] Animations disabled, instant update");
            stop_animation();
            display_value_ = target_value_;
            display_callback_(display_value_);
            return;
        }

        // Skip animation for initial value (display is 0 or uninitialized)
        // This handles startup where many values arrive rapidly
        if (display_value_ == 0) {
            spdlog::trace("[AnimatedValue] Initial value, setting directly: {}", target_value_);
            display_value_ = target_value_;
            display_callback_(display_value_);
            return;
        }

        spdlog::trace("[AnimatedValue] Starting animation: {} -> {} ({}ms)", display_value_,
                      target_value_, config_.duration_ms);
        // Start animation from current display value to new target
        start_animation();
    }

    void start_animation() {
        // Stop existing animation if running
        if (anim_running_) {
            lv_anim_delete(this, anim_exec_cb);
            anim_running_ = false;
        }

        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, this);
        lv_anim_set_values(&anim, static_cast<int32_t>(display_value_),
                           static_cast<int32_t>(target_value_));
        lv_anim_set_duration(&anim, config_.duration_ms);
        lv_anim_set_path_cb(&anim, config_.easing);
        lv_anim_set_exec_cb(&anim, anim_exec_cb);
        lv_anim_set_completed_cb(&anim, anim_completed_cb);
        lv_anim_start(&anim);
        anim_running_ = true;
    }

    void stop_animation() {
        if (anim_running_) {
            // Clear flag BEFORE lv_anim_delete — if the completion callback fires
            // synchronously during deletion, it will see anim_running_ == false and bail
            anim_running_ = false;
            lv_anim_delete(this, anim_exec_cb);
        }
    }

    /**
     * @brief Static observer callback (C-style for LVGL)
     */
    static void observer_callback(lv_observer_t* observer, lv_subject_t* subject) {
        auto* self = static_cast<AnimatedValue*>(lv_observer_get_user_data(observer));
        if (self) {
            T new_value = static_cast<T>(lv_subject_get_int(subject));
            self->on_subject_changed(new_value);
        }
    }

    /**
     * @brief Animation execution callback - called on each frame
     */
    static void anim_exec_cb(void* var, int32_t value) {
        auto* self = static_cast<AnimatedValue*>(var);
        if (self) {
            spdlog::trace("[AnimatedValue] anim_exec_cb: value={}", value);
            self->display_value_ = static_cast<T>(value);
            if (self->display_callback_) {
                self->display_callback_(self->display_value_);
            }
        }
    }

    /**
     * @brief Animation completion callback
     */
    static void anim_completed_cb(lv_anim_t* anim) {
        auto* self = static_cast<AnimatedValue*>(anim->var);
        if (!self || !self->anim_running_) {
            return; // Already stopped (e.g., stop_animation() triggered this callback)
        }
        self->anim_running_ = false;

        // Get current animation end value (what we animated TO)
        T anim_end = static_cast<T>(anim->end_value);

        // If target changed during animation, start new animation toward it
        if (self->target_value_ != anim_end) {
            self->display_value_ = anim_end; // Current position
            spdlog::trace("[AnimatedValue] Chaining animation: {} -> {}", self->display_value_,
                          self->target_value_);
            self->start_animation();
        } else {
            // Animation reached target
            self->display_value_ = self->target_value_;
            if (self->display_callback_) {
                self->display_callback_(self->display_value_);
            }
        }
    }
};

} // namespace helix::ui
