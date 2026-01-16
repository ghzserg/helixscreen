// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 356C LLC

/**
 * @file touch_calibration_panel.cpp
 * @brief Touch calibration panel state machine implementation
 */

#include "touch_calibration_panel.h"

#include <spdlog/spdlog.h>

namespace helix {

// Default screen dimensions used when invalid values are provided
constexpr int DEFAULT_SCREEN_WIDTH = 800;
constexpr int DEFAULT_SCREEN_HEIGHT = 480;

TouchCalibrationPanel::TouchCalibrationPanel() = default;

TouchCalibrationPanel::~TouchCalibrationPanel() = default;

void TouchCalibrationPanel::set_completion_callback(CompletionCallback cb) {
    callback_ = std::move(cb);
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
        } else {
            // Calibration failed (collinear/duplicate points) - restart from POINT_1
            spdlog::warn(
                "[TouchCalibrationPanel] Calibration failed (degenerate points), restarting");
            state_ = State::POINT_1;
            calibration_.valid = false;
        }
        break;
    default:
        // No-op in IDLE, VERIFY, COMPLETE states
        break;
    }
}

void TouchCalibrationPanel::accept() {
    if (state_ != State::VERIFY) {
        return;
    }

    state_ = State::COMPLETE;
    if (callback_) {
        callback_(&calibration_);
    }
}

void TouchCalibrationPanel::retry() {
    if (state_ != State::VERIFY) {
        return;
    }

    state_ = State::POINT_1;
    calibration_.valid = false;

    // Recalculate screen points using named constants
    screen_points_[0] = compute_target_position(0);
    screen_points_[1] = compute_target_position(1);
    screen_points_[2] = compute_target_position(2);
}

void TouchCalibrationPanel::cancel() {
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

} // namespace helix
