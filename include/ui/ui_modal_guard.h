// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_modal.h" // For helix::ui::modal_hide

#include "lvgl.h"

namespace helix::ui {

/**
 * @brief RAII wrapper for modal dialog cleanup
 *
 * Automatically hides modal on destruction, eliminating manual cleanup boilerplate.
 * Similar to ObserverGuard pattern used elsewhere in codebase.
 *
 * @code{.cpp}
 * class MyPanel {
 *     ModalGuard warning_modal_;  // Auto-hides in destructor
 *
 *     void show_warning() {
 *         warning_modal_ = ModalGuard(create_my_modal());
 *     }
 * };
 * @endcode
 */
class ModalGuard {
  public:
    ModalGuard() = default;
    explicit ModalGuard(lv_obj_t* modal) : modal_(modal) {}

    ~ModalGuard() {
        hide();
    }

    // Non-copyable
    ModalGuard(const ModalGuard&) = delete;
    ModalGuard& operator=(const ModalGuard&) = delete;

    // Movable - transfers ownership
    ModalGuard(ModalGuard&& other) noexcept : modal_(other.modal_) {
        other.modal_ = nullptr;
    }

    ModalGuard& operator=(ModalGuard&& other) noexcept {
        if (this != &other) {
            hide(); // Hide current modal before taking new one
            modal_ = other.modal_;
            other.modal_ = nullptr;
        }
        return *this;
    }

    // Assign from raw pointer (hides previous modal)
    ModalGuard& operator=(lv_obj_t* modal) {
        hide();
        modal_ = modal;
        return *this;
    }

    void hide() {
        if (modal_) {
            modal_hide(modal_);
            modal_ = nullptr;
        }
    }

    lv_obj_t* release() {
        auto* ptr = modal_;
        modal_ = nullptr;
        return ptr;
    }

    lv_obj_t* get() const {
        return modal_;
    }
    explicit operator bool() const {
        return modal_ != nullptr;
    }

  private:
    lv_obj_t* modal_ = nullptr;
};

} // namespace helix::ui
