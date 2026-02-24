// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_heating_animator.h"
#include "ui_observer_guard.h"

#include "panel_widget.h"

#include <memory>

class TempControlPanel;

namespace helix {
class PrinterState;

class TempStackWidget : public PanelWidget {
  public:
    TempStackWidget(PrinterState& printer_state, TempControlPanel* temp_panel);
    ~TempStackWidget() override;

    void set_config(const nlohmann::json& config) override;
    std::string get_component_name() const override;
    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    const char* id() const override {
        return "temp_stack";
    }

  private:
    PrinterState& printer_state_;
    TempControlPanel* temp_control_panel_;
    nlohmann::json config_;

    lv_obj_t* widget_obj_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;

    // Lazy overlay panels
    lv_obj_t* nozzle_temp_panel_ = nullptr;
    lv_obj_t* bed_temp_panel_ = nullptr;
    lv_obj_t* chamber_temp_panel_ = nullptr;

    // Heating icon animators
    HeatingIconAnimator nozzle_animator_;
    HeatingIconAnimator bed_animator_;

    // Cached temps (centidegrees)
    int cached_nozzle_temp_ = 25;
    int cached_nozzle_target_ = 0;
    int cached_bed_temp_ = 25;
    int cached_bed_target_ = 0;

    std::shared_ptr<bool> alive_ = std::make_shared<bool>(false);

    // Long-press click suppression flag
    bool long_pressed_ = false;

    // Observers
    ObserverGuard nozzle_temp_observer_;
    ObserverGuard nozzle_target_observer_;
    ObserverGuard bed_temp_observer_;
    ObserverGuard bed_target_observer_;

    bool is_carousel_mode() const;
    void attach_stack(lv_obj_t* widget_obj);
    void attach_carousel(lv_obj_t* widget_obj);
    void toggle_display_mode();

    void on_nozzle_temp_changed(int temp_centi);
    void on_nozzle_target_changed(int target_centi);
    void on_bed_temp_changed(int temp_centi);
    void on_bed_target_changed(int target_centi);

    void handle_nozzle_clicked();
    void handle_bed_clicked();
    void handle_chamber_clicked();

  public:
    // Public for early XML callback registration (before attach)
    static void temp_stack_nozzle_cb(lv_event_t* e);
    static void temp_stack_bed_cb(lv_event_t* e);
    static void temp_stack_chamber_cb(lv_event_t* e);

    // Long-press callbacks for toggling display mode
    static void temp_stack_long_press_cb(lv_event_t* e);
    static void temp_carousel_long_press_cb(lv_event_t* e);

    // Carousel page click callback
    static void temp_carousel_page_cb(lv_event_t* e);
};

} // namespace helix
