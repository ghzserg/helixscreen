// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 356C LLC

/**
 * @file touch_calibration_panel.cpp
 * @brief Touch calibration panel state machine implementation
 */

#include "touch_calibration_panel.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <vector>

namespace helix {

// Default screen dimensions used when invalid values are provided
constexpr int DEFAULT_SCREEN_WIDTH = 800;
constexpr int DEFAULT_SCREEN_HEIGHT = 480;

TouchCalibrationPanel::TouchCalibrationPanel() = default;

TouchCalibrationPanel::~TouchCalibrationPanel() {
    stop_countdown_timer();
}

void TouchCalibrationPanel::set_completion_callback(CompletionCallback cb) {
    callback_ = std::move(cb);
}

void TouchCalibrationPanel::set_failure_callback(FailureCallback cb) {
    failure_callback_ = std::move(cb);
}

void TouchCalibrationPanel::set_countdown_callback(CountdownCallback cb) {
    countdown_callback_ = std::move(cb);
}

void TouchCalibrationPanel::set_timeout_callback(TimeoutCallback cb) {
    timeout_callback_ = std::move(cb);
}

void TouchCalibrationPanel::set_verify_timeout_seconds(int seconds) {
    verify_timeout_seconds_ = seconds;
}

void TouchCalibrationPanel::set_screen_size(int width, int height) {
    // Validate screen dimensions - reject zero or negative values
    if (width <= 0 || height <= 0) {
        spdlog::warn("[TouchCalibrationPanel] Invalid screen size {}x{}, using defaults {}x{}",
                     width, height, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT);
        screen_width_ = DEFAULT_SCREEN_WIDTH;
        screen_height_ = DEFAULT_SCREEN_HEIGHT;
        return;
    }
    screen_width_ = width;
    screen_height_ = height;
}

Point TouchCalibrationPanel::compute_target_position(int step) const {
    switch (step) {
    case 0:
        return {static_cast<int>(screen_width_ * TARGET_0_X_RATIO),
                static_cast<int>(screen_height_ * TARGET_0_Y_RATIO)};
    case 1:
        return {static_cast<int>(screen_width_ * TARGET_1_X_RATIO),
                static_cast<int>(screen_height_ * TARGET_1_Y_RATIO)};
    case 2:
        return {static_cast<int>(screen_width_ * TARGET_2_X_RATIO),
                static_cast<int>(screen_height_ * TARGET_2_Y_RATIO)};
    default:
        return Point{0, 0};
    }
}

void TouchCalibrationPanel::start() {
    state_ = State::POINT_1;
    calibration_.valid = false;
    reset_samples();

    // Calculate screen target positions using named constants
    screen_points_[0] = compute_target_position(0);
    screen_points_[1] = compute_target_position(1);
    screen_points_[2] = compute_target_position(2);
}

void TouchCalibrationPanel::capture_point(Point raw) {
    switch (state_) {
    case State::POINT_1:
        touch_points_[0] = raw;
        state_ = State::POINT_2;
        break;
    case State::POINT_2:
        touch_points_[1] = raw;
        state_ = State::POINT_3;
        break;
    case State::POINT_3:
        touch_points_[2] = raw;
        // Compute calibration and check for degenerate case (collinear points)
        if (compute_calibration(screen_points_, touch_points_, calibration_)) {
            state_ = State::VERIFY;
            start_countdown_timer();
        } else {
            // Calibration failed (collinear/duplicate points) - restart from POINT_1
            spdlog::warn(
                "[TouchCalibrationPanel] Calibration failed (degenerate points), restarting");
            state_ = State::POINT_1;
            calibration_.valid = false;
            // Notify caller about the failure
            if (failure_callback_) {
                failure_callback_("Touch points too close together. Please try again.");
            }
        }
        break;
    default:
        // No-op in IDLE, VERIFY, COMPLETE states
        break;
    }
}

bool TouchCalibrationPanel::is_saturated_sample(const Point& sample) {
    return sample.x == 4095 || sample.y == 4095 || sample.x == 65535 || sample.y == 65535;
}

void TouchCalibrationPanel::reset_samples() {
    sample_count_ = 0;
}

bool TouchCalibrationPanel::compute_median_point(Point& out) {
    std::vector<int> valid_x, valid_y;
    for (int i = 0; i < sample_count_; i++) {
        Point p{sample_buffer_[i].x, sample_buffer_[i].y};
        if (!is_saturated_sample(p)) {
            valid_x.push_back(p.x);
            valid_y.push_back(p.y);
        }
    }

    if (static_cast<int>(valid_x.size()) < MIN_VALID_SAMPLES) {
        spdlog::warn("[TouchCalibrationPanel] Only {}/{} valid samples (need {})", valid_x.size(),
                     sample_count_, MIN_VALID_SAMPLES);
        return false;
    }

    std::sort(valid_x.begin(), valid_x.end());
    std::sort(valid_y.begin(), valid_y.end());
    size_t mid = valid_x.size() / 2;
    out.x = valid_x[mid];
    out.y = valid_y[mid];

    spdlog::debug("[TouchCalibrationPanel] Median from {}/{} valid samples: ({}, {})",
                  valid_x.size(), sample_count_, out.x, out.y);
    return true;
}

void TouchCalibrationPanel::add_sample(Point raw) {
    if (state_ != State::POINT_1 && state_ != State::POINT_2 && state_ != State::POINT_3) {
        return;
    }

    if (sample_count_ < SAMPLES_REQUIRED) {
        sample_buffer_[sample_count_] = {raw.x, raw.y};
        sample_count_++;
    }

    if (sample_count_ >= SAMPLES_REQUIRED) {
        Point median;
        if (compute_median_point(median)) {
            capture_point(median);
        } else {
            if (failure_callback_) {
                failure_callback_("Touch was noisy, please tap again.");
            }
        }
        reset_samples();
    }
}

void TouchCalibrationPanel::accept() {
    stop_countdown_timer();

    if (state_ != State::VERIFY) {
        return;
    }

    state_ = State::COMPLETE;
    if (callback_) {
        callback_(&calibration_);
    }
}

void TouchCalibrationPanel::retry() {
    stop_countdown_timer();

    if (state_ != State::VERIFY) {
        return;
    }

    state_ = State::POINT_1;
    calibration_.valid = false;
    reset_samples();

    // Recalculate screen points using named constants
    screen_points_[0] = compute_target_position(0);
    screen_points_[1] = compute_target_position(1);
    screen_points_[2] = compute_target_position(2);
}

void TouchCalibrationPanel::cancel() {
    stop_countdown_timer();

    state_ = State::IDLE;
    calibration_.valid = false;
    if (callback_) {
        callback_(nullptr);
    }
}

TouchCalibrationPanel::State TouchCalibrationPanel::get_state() const {
    return state_;
}

Point TouchCalibrationPanel::get_target_position(int step) const {
    if (step < 0 || step > 2) {
        return Point{0, 0};
    }
    // Delegate to private helper that uses named constants
    return compute_target_position(step);
}

const TouchCalibration* TouchCalibrationPanel::get_calibration() const {
    if ((state_ == State::VERIFY || state_ == State::COMPLETE) && calibration_.valid) {
        return &calibration_;
    }
    return nullptr;
}

void TouchCalibrationPanel::start_countdown_timer() {
    countdown_remaining_ = verify_timeout_seconds_;
    countdown_timer_ = lv_timer_create(countdown_timer_cb, 1000, this);
    spdlog::debug("[TouchCalibrationPanel] Started countdown timer: {} seconds",
                  countdown_remaining_);

    // Immediately notify with initial value
    if (countdown_callback_) {
        countdown_callback_(countdown_remaining_);
    }
}

void TouchCalibrationPanel::stop_countdown_timer() {
    if (countdown_timer_ != nullptr) {
        lv_timer_delete(countdown_timer_);
        countdown_timer_ = nullptr;
        spdlog::debug("[TouchCalibrationPanel] Stopped countdown timer");
    }
}

void TouchCalibrationPanel::countdown_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<TouchCalibrationPanel*>(lv_timer_get_user_data(timer));
    self->countdown_remaining_--;
    spdlog::debug("[TouchCalibrationPanel] Countdown tick: {} seconds remaining",
                  self->countdown_remaining_);

    if (self->countdown_remaining_ > 0) {
        if (self->countdown_callback_) {
            self->countdown_callback_(self->countdown_remaining_);
        }
    } else {
        spdlog::debug("[TouchCalibrationPanel] Countdown expired, invoking timeout callback");
        if (self->timeout_callback_) {
            self->timeout_callback_();
        }
        self->stop_countdown_timer();
    }
}

} // namespace helix
