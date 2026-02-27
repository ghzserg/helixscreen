// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_shutdown_modal.h"

#include "panel_widget.h"

#include <memory>

class MoonrakerAPI;

namespace helix {

class ShutdownWidget : public PanelWidget {
  public:
    explicit ShutdownWidget(MoonrakerAPI* api);
    ~ShutdownWidget() override;

    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    const char* id() const override {
        return "shutdown";
    }

    // XML event callback (public for early registration)
    static void shutdown_clicked_cb(lv_event_t* e);

  private:
    MoonrakerAPI* api_;

    lv_obj_t* widget_obj_ = nullptr;
    lv_obj_t* shutdown_btn_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;

    ShutdownModal shutdown_modal_;

    // Shared flag for async callback safety
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(false);

    void handle_click();
    void execute_shutdown();
    void execute_reboot();
};

void register_shutdown_widget();

} // namespace helix
