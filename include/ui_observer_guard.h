// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_observer_guard.h
 * @brief RAII wrapper for LVGL observer cleanup
 *
 * @pattern Guard that removes observer on destruction; release() for pre-destroyed subjects
 * @threading Main thread only
 * @gotchas Checks lv_is_initialized() - safe during LVGL shutdown
 */

#pragma once

#include "lvgl/lvgl.h"

#include <utility>

/**
 * @brief RAII wrapper for LVGL observers - auto-removes on destruction
 */
class ObserverGuard {
  public:
    ObserverGuard() = default;

    ObserverGuard(lv_subject_t* subject, lv_observer_cb_t cb, void* user_data)
        : observer_(lv_subject_add_observer(subject, cb, user_data)) {}

    ~ObserverGuard() {
        reset();
    }

    ObserverGuard(ObserverGuard&& other) noexcept
        : observer_(std::exchange(other.observer_, nullptr)) {}

    ObserverGuard& operator=(ObserverGuard&& other) noexcept {
        if (this != &other) {
            reset();
            observer_ = std::exchange(other.observer_, nullptr);
        }
        return *this;
    }

    ObserverGuard(const ObserverGuard&) = delete;
    ObserverGuard& operator=(const ObserverGuard&) = delete;

    void reset() {
        if (observer_) {
            if (lv_is_initialized()) {
                lv_observer_remove(observer_);
            }
            // If LVGL is already torn down, just release — don't log,
            // as spdlog may also be destroyed during static cleanup
            observer_ = nullptr;
        }
    }

    /**
     * @brief Release ownership without calling lv_observer_remove()
     *
     * Use during shutdown when subjects may already be destroyed.
     * The observer will not be removed from the subject (it may already be gone).
     */
    void release() {
        observer_ = nullptr;
    }

    explicit operator bool() const {
        return observer_ != nullptr;
    }
    lv_observer_t* get() const {
        return observer_;
    }

  private:
    lv_observer_t* observer_ = nullptr;
};
