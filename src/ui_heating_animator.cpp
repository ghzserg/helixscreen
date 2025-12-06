// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_heating_animator.h"

#include "ui_icon.h"
#include "ui_theme.h"

#include <spdlog/spdlog.h>

#include <algorithm>

// Gradient colors (cold → warm → hot)
// These match the tokens we'll add to globals.xml
static constexpr uint32_t COLOR_COLD = 0x2196F3; // Blue
static constexpr uint32_t COLOR_WARM = 0xFFC107; // Amber
static constexpr uint32_t COLOR_HOT = 0xFF4444;  // Red

HeatingIconAnimator::~HeatingIconAnimator() {
    // Don't cleanup LVGL resources in destructor - may be called during static destruction
    // when LVGL is already torn down. LVGL will clean up animations on its own shutdown.
    // Just reset our state.
    pulse_active_ = false;
    icon_ = nullptr;
}

HeatingIconAnimator::HeatingIconAnimator(HeatingIconAnimator&& other) noexcept
    : icon_(other.icon_), state_(other.state_), ambient_temp_(other.ambient_temp_),
      current_temp_(other.current_temp_), target_temp_(other.target_temp_),
      current_color_(other.current_color_), current_opacity_(other.current_opacity_),
      pulse_active_(other.pulse_active_) {
    other.icon_ = nullptr;
    other.pulse_active_ = false;
}

HeatingIconAnimator& HeatingIconAnimator::operator=(HeatingIconAnimator&& other) noexcept {
    if (this != &other) {
        detach();
        icon_ = other.icon_;
        state_ = other.state_;
        ambient_temp_ = other.ambient_temp_;
        current_temp_ = other.current_temp_;
        target_temp_ = other.target_temp_;
        current_color_ = other.current_color_;
        current_opacity_ = other.current_opacity_;
        pulse_active_ = other.pulse_active_;
        other.icon_ = nullptr;
        other.pulse_active_ = false;
    }
    return *this;
}

void HeatingIconAnimator::attach(lv_obj_t* icon) {
    if (icon_ != nullptr) {
        detach();
    }
    icon_ = icon;
    state_ = State::OFF;
    current_color_ = get_secondary_color();
    current_opacity_ = LV_OPA_COVER;
    apply_color();
    spdlog::debug("[HeatingIconAnimator] Attached to icon");
}

void HeatingIconAnimator::detach() {
    if (icon_ == nullptr) {
        return;
    }
    stop_pulse();
    icon_ = nullptr;
    spdlog::debug("[HeatingIconAnimator] Detached");
}

void HeatingIconAnimator::update(int current_temp, int target_temp) {
    if (icon_ == nullptr) {
        return;
    }

    current_temp_ = current_temp;
    target_temp_ = target_temp;

    // Determine new state based on temperatures
    State new_state;
    if (target_temp <= 0) {
        new_state = State::OFF;
    } else if (current_temp >= target_temp - TEMP_TOLERANCE) {
        new_state = State::AT_TARGET;
    } else {
        new_state = State::HEATING;
    }

    // Handle state transitions
    if (new_state != state_) {
        State old_state = state_;
        state_ = new_state;

        switch (new_state) {
        case State::OFF:
            // Heater turned off - stop pulse, show secondary color
            stop_pulse();
            current_color_ = get_secondary_color();
            current_opacity_ = LV_OPA_COVER;
            spdlog::debug("[HeatingIconAnimator] State: OFF");
            break;

        case State::HEATING:
            // Starting to heat - capture ambient and start pulse
            if (old_state == State::OFF) {
                // Fresh heating start - capture current temp as ambient
                ambient_temp_ = current_temp;
                spdlog::debug("[HeatingIconAnimator] Captured ambient: {}°C, target: {}°C",
                              ambient_temp_, target_temp);
            }
            // Start or continue pulse animation
            if (!pulse_active_) {
                start_pulse();
            }
            spdlog::debug("[HeatingIconAnimator] State: HEATING");
            break;

        case State::AT_TARGET:
            // Reached target - stop pulse, solid hot color
            stop_pulse();
            current_color_ = lv_color_hex(COLOR_HOT);
            current_opacity_ = LV_OPA_COVER;
            spdlog::debug("[HeatingIconAnimator] State: AT_TARGET");
            break;
        }
    }

    // Update gradient color if heating
    if (state_ == State::HEATING) {
        // Calculate progress: (current - ambient) / (target - ambient)
        float progress = 0.0f;
        int range = target_temp_ - ambient_temp_;
        if (range > 0) {
            progress =
                static_cast<float>(current_temp_ - ambient_temp_) / static_cast<float>(range);
            progress = std::clamp(progress, 0.0f, 1.0f);
        }
        current_color_ = calculate_gradient_color(progress);
    }

    apply_color();
}

lv_color_t HeatingIconAnimator::calculate_gradient_color(float progress) {
    // Two-segment gradient:
    // 0.0 - 0.5: cold (blue) → warm (amber)
    // 0.5 - 1.0: warm (amber) → hot (red)

    lv_color_t cold = lv_color_hex(COLOR_COLD);
    lv_color_t warm = lv_color_hex(COLOR_WARM);
    lv_color_t hot = lv_color_hex(COLOR_HOT);

    if (progress < 0.5f) {
        // Cold → Warm
        // lv_color_mix: mix ratio is how much of c1 to use (0=c2, 255=c1)
        uint8_t ratio = static_cast<uint8_t>(progress * 2.0f * 255.0f);
        return lv_color_mix(warm, cold, ratio);
    } else {
        // Warm → Hot
        uint8_t ratio = static_cast<uint8_t>((progress - 0.5f) * 2.0f * 255.0f);
        return lv_color_mix(hot, warm, ratio);
    }
}

void HeatingIconAnimator::start_pulse() {
    if (icon_ == nullptr || pulse_active_) {
        return;
    }

    pulse_active_ = true;

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, this);
    lv_anim_set_values(&anim, PULSE_OPA_MIN, PULSE_OPA_MAX);
    lv_anim_set_duration(&anim, PULSE_DURATION_MS);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_playback_duration(&anim, PULSE_DURATION_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&anim, pulse_anim_cb);
    lv_anim_start(&anim);

    spdlog::debug("[HeatingIconAnimator] Pulse animation started");
}

void HeatingIconAnimator::stop_pulse() {
    if (!pulse_active_) {
        return;
    }

    pulse_active_ = false;
    lv_anim_delete(this, pulse_anim_cb);
    current_opacity_ = LV_OPA_COVER;

    spdlog::debug("[HeatingIconAnimator] Pulse animation stopped");
}

void HeatingIconAnimator::apply_color() {
    if (icon_ == nullptr) {
        return;
    }

    // Try to set color on the attached widget directly (for single icons)
    ui_icon_set_color(icon_, current_color_, current_opacity_);

    // Also iterate through children (for composite icons like bed's heat_wave + train_flatbed)
    uint32_t child_count = lv_obj_get_child_count(icon_);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(icon_, static_cast<int32_t>(i));
        if (child) {
            ui_icon_set_color(child, current_color_, current_opacity_);
        }
    }
}

lv_color_t HeatingIconAnimator::get_secondary_color() {
    // Use theme's text_secondary color for "off" state
    return ui_theme_parse_color("#ADAEB8");
}

void HeatingIconAnimator::pulse_anim_cb(void* var, int32_t value) {
    auto* animator = static_cast<HeatingIconAnimator*>(var);
    if (animator && animator->icon_) {
        animator->current_opacity_ = static_cast<lv_opa_t>(value);
        animator->apply_color();
    }
}
