// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UI_OBSERVER_GUARD_H
#define UI_OBSERVER_GUARD_H

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

    ~ObserverGuard() { reset(); }

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
            lv_observer_remove(observer_);
            observer_ = nullptr;
        }
    }

    explicit operator bool() const { return observer_ != nullptr; }
    lv_observer_t* get() const { return observer_; }

private:
    lv_observer_t* observer_ = nullptr;
};

#endif // UI_OBSERVER_GUARD_H
