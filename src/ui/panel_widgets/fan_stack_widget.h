// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "panel_widget.h"

#include <memory>
#include <string>
#include <vector>

class FanDial;

namespace helix {
class PrinterState;

/// Home widget displaying part, hotend, and auxiliary fan speeds in a compact stack.
/// Fan icons spin proportionally to fan speed when animations are enabled.
/// Clicking opens the fan control overlay.
/// Long-press toggles between stack and carousel display modes.
class FanStackWidget : public PanelWidget {
  public:
    explicit FanStackWidget(PrinterState& printer_state);
    ~FanStackWidget() override;

    void set_config(const nlohmann::json& config) override;
    std::string get_component_name() const override;
    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    void set_row_density(size_t widgets_in_row) override;
    const char* id() const override {
        return "fan_stack";
    }

    /// XML event callback — opens fan control overlay
    static void on_fan_stack_clicked(lv_event_t* e);

    // Long-press callbacks for toggling display mode
    static void fan_stack_long_press_cb(lv_event_t* e);
    static void fan_carousel_long_press_cb(lv_event_t* e);

  private:
    PrinterState& printer_state_;
    nlohmann::json config_;

    lv_obj_t* widget_obj_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;
    lv_obj_t* fan_control_panel_ = nullptr;

    // Labels, names, and icons for each fan row (stack mode)
    lv_obj_t* part_label_ = nullptr;
    lv_obj_t* hotend_label_ = nullptr;
    lv_obj_t* aux_label_ = nullptr;
    lv_obj_t* aux_row_ = nullptr;
    lv_obj_t* part_icon_ = nullptr;
    lv_obj_t* hotend_icon_ = nullptr;
    lv_obj_t* aux_icon_ = nullptr;

    // Per-fan observers
    ObserverGuard part_observer_;
    ObserverGuard hotend_observer_;
    ObserverGuard aux_observer_;

    // Version observer to detect fan discovery
    ObserverGuard version_observer_;

    // Animation settings observer
    ObserverGuard anim_settings_observer_;

    std::shared_ptr<bool> alive_ = std::make_shared<bool>(false);

    // Long-press click suppression flag
    bool long_pressed_ = false;

    // Resolved fan object names
    std::string part_fan_name_;
    std::string hotend_fan_name_;
    std::string aux_fan_name_;

    // Cached speeds for animation updates
    int part_speed_ = 0;
    int hotend_speed_ = 0;
    int aux_speed_ = 0;

    bool animations_enabled_ = false;

    // Carousel mode: owned FanDial instances
    std::vector<std::unique_ptr<FanDial>> fan_dials_;

    bool is_carousel_mode() const;
    void attach_stack(lv_obj_t* widget_obj);
    void attach_carousel(lv_obj_t* widget_obj);
    void toggle_display_mode();

    void handle_clicked();
    void bind_fans();
    void bind_carousel_fans();
    void update_label(lv_obj_t* label, int speed_pct);
    void update_fan_animation(lv_obj_t* icon, int speed_pct);
    void refresh_all_animations();

    static void carousel_dial_long_press_cb(lv_event_t* e);

    /// Stop any running spin animation on an icon
    static void stop_spin(lv_obj_t* icon);

    /// Start continuous spin animation scaled to fan speed
    static void start_spin(lv_obj_t* icon, int speed_pct);

    /// LVGL animation exec callback for rotation
    static void spin_anim_cb(void* var, int32_t value);
};

} // namespace helix
