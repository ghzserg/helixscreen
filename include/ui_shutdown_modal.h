// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_modal.h"

#include <spdlog/spdlog.h>

#include <functional>

/**
 * @file ui_shutdown_modal.h
 * @brief Confirmation dialog for shutdown/reboot with pending state spinner.
 *
 * Uses modal_button_row for Shutdown|Reboot, X button for dismiss.
 * State transitions driven by shutdown_pending subject (static, shared
 * across all instances since only one modal is visible at a time).
 * XML bind_flag_if_not_eq bindings handle all visibility.
 */
class ShutdownModal : public Modal {
  public:
    using ActionCallback = std::function<void()>;

    ShutdownModal() {
        init_subject();
    }

    const char* get_name() const override {
        return "Shutdown";
    }
    const char* component_name() const override {
        return "shutdown_modal";
    }

    void set_callbacks(ActionCallback on_shutdown, ActionCallback on_reboot) {
        on_shutdown_cb_ = std::move(on_shutdown);
        on_reboot_cb_ = std::move(on_reboot);
    }

  protected:
    void on_show() override {
        lv_subject_set_int(&pending_subject_, 0);

        wire_cancel_button("btn_close");
        wire_ok_button("btn_primary");
        wire_tertiary_button("btn_secondary");
    }

    void on_ok() override {
        spdlog::info("[ShutdownModal] Shutdown confirmed");
        lv_subject_set_int(&pending_subject_, 1);
        if (on_shutdown_cb_)
            on_shutdown_cb_();
    }

    void on_tertiary() override {
        spdlog::info("[ShutdownModal] Reboot confirmed");
        lv_subject_set_int(&pending_subject_, 2);
        if (on_reboot_cb_)
            on_reboot_cb_();
    }

  private:
    ActionCallback on_shutdown_cb_;
    ActionCallback on_reboot_cb_;

    // Static subject shared across all instances — only one modal visible at a time
    static inline lv_subject_t pending_subject_{};
    static inline bool subject_initialized_ = false;

    static void init_subject() {
        if (subject_initialized_)
            return;
        subject_initialized_ = true;

        lv_subject_init_int(&pending_subject_, 0);

        // Register into component scope so XML bindings can find it
        auto* scope = lv_xml_component_get_scope("shutdown_modal");
        if (scope) {
            lv_xml_register_subject(scope, "shutdown_pending", &pending_subject_);
        } else {
            spdlog::warn("[ShutdownModal] Component scope not found — "
                         "ensure shutdown_modal.xml is registered first");
        }
    }
};
