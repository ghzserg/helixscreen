// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temp_stack_widget.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_nav_manager.h"
#include "ui_panel_temp_control.h"

#include "app_globals.h"
#include "observer_factory.h"
#include "panel_widget_manager.h"
#include "panel_widget_registry.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

namespace {
const bool s_registered = [] {
    helix::register_widget_factory("temp_stack", []() {
        auto& ps = get_printer_state();
        auto* tcp = helix::PanelWidgetManager::instance().shared_resource<TempControlPanel>();
        return std::make_unique<helix::TempStackWidget>(ps, tcp);
    });
    return true;
}();
} // namespace

using namespace helix;

// Static instance pointer for callback dispatch (only one temp_stack widget at a time)
static TempStackWidget* s_active_instance = nullptr;

TempStackWidget::TempStackWidget(PrinterState& printer_state, TempControlPanel* temp_panel)
    : printer_state_(printer_state), temp_control_panel_(temp_panel) {}

TempStackWidget::~TempStackWidget() {
    detach();
}

void TempStackWidget::attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) {
    widget_obj_ = widget_obj;
    parent_screen_ = parent_screen;
    s_active_instance = this;

    using helix::ui::observe_int_sync;

    // Nozzle observers
    nozzle_temp_observer_ = observe_int_sync<TempStackWidget>(
        printer_state_.get_active_extruder_temp_subject(), this,
        [](TempStackWidget* self, int temp) { self->on_nozzle_temp_changed(temp); });
    nozzle_target_observer_ = observe_int_sync<TempStackWidget>(
        printer_state_.get_active_extruder_target_subject(), this,
        [](TempStackWidget* self, int target) { self->on_nozzle_target_changed(target); });

    // Bed observers
    bed_temp_observer_ = observe_int_sync<TempStackWidget>(
        printer_state_.get_bed_temp_subject(), this,
        [](TempStackWidget* self, int temp) { self->on_bed_temp_changed(temp); });
    bed_target_observer_ = observe_int_sync<TempStackWidget>(
        printer_state_.get_bed_target_subject(), this,
        [](TempStackWidget* self, int target) { self->on_bed_target_changed(target); });

    // Attach nozzle animator - look for the glyph inside the nozzle_icon component
    lv_obj_t* nozzle_icon = lv_obj_find_by_name(widget_obj_, "nozzle_icon_glyph");
    if (nozzle_icon) {
        nozzle_animator_.attach(nozzle_icon);
        cached_nozzle_temp_ = lv_subject_get_int(printer_state_.get_active_extruder_temp_subject());
        cached_nozzle_target_ =
            lv_subject_get_int(printer_state_.get_active_extruder_target_subject());
        nozzle_animator_.update(cached_nozzle_temp_, cached_nozzle_target_);
    }

    // Attach bed animator
    lv_obj_t* bed_icon = lv_obj_find_by_name(widget_obj_, "temp_stack_bed_icon_glyph");
    if (bed_icon) {
        bed_animator_.attach(bed_icon);
        cached_bed_temp_ = lv_subject_get_int(printer_state_.get_bed_temp_subject());
        cached_bed_target_ = lv_subject_get_int(printer_state_.get_bed_target_subject());
        bed_animator_.update(cached_bed_temp_, cached_bed_target_);
    }

    spdlog::debug("[TempStackWidget] Attached with {} animators",
                  (nozzle_icon ? 1 : 0) + (bed_icon ? 1 : 0));
}

void TempStackWidget::detach() {
    nozzle_animator_.detach();
    bed_animator_.detach();
    nozzle_temp_observer_.reset();
    nozzle_target_observer_.reset();
    bed_temp_observer_.reset();
    bed_target_observer_.reset();

    // Clean up lazily-created overlays (children of parent_screen_, not widget container)
    if (nozzle_temp_panel_) {
        NavigationManager::instance().unregister_overlay_instance(nozzle_temp_panel_);
        lv_obj_delete(nozzle_temp_panel_);
        nozzle_temp_panel_ = nullptr;
    }
    if (bed_temp_panel_) {
        NavigationManager::instance().unregister_overlay_instance(bed_temp_panel_);
        lv_obj_delete(bed_temp_panel_);
        bed_temp_panel_ = nullptr;
    }

    if (s_active_instance == this) {
        s_active_instance = nullptr;
    }

    widget_obj_ = nullptr;
    parent_screen_ = nullptr;

    spdlog::debug("[TempStackWidget] Detached");
}

void TempStackWidget::on_nozzle_temp_changed(int temp_centi) {
    cached_nozzle_temp_ = temp_centi;
    nozzle_animator_.update(cached_nozzle_temp_, cached_nozzle_target_);
}

void TempStackWidget::on_nozzle_target_changed(int target_centi) {
    cached_nozzle_target_ = target_centi;
    nozzle_animator_.update(cached_nozzle_temp_, cached_nozzle_target_);
}

void TempStackWidget::on_bed_temp_changed(int temp_centi) {
    cached_bed_temp_ = temp_centi;
    bed_animator_.update(cached_bed_temp_, cached_bed_target_);
}

void TempStackWidget::on_bed_target_changed(int target_centi) {
    cached_bed_target_ = target_centi;
    bed_animator_.update(cached_bed_temp_, cached_bed_target_);
}

void TempStackWidget::handle_nozzle_clicked() {
    spdlog::info("[TempStackWidget] Nozzle clicked - opening nozzle temp panel");

    if (!temp_control_panel_) {
        spdlog::error("[TempStackWidget] TempControlPanel not initialized");
        NOTIFY_ERROR("Temperature panel not available");
        return;
    }

    if (!nozzle_temp_panel_ && parent_screen_) {
        nozzle_temp_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "nozzle_temp_panel", nullptr));
        if (nozzle_temp_panel_) {
            temp_control_panel_->setup_nozzle_panel(nozzle_temp_panel_, parent_screen_);
            NavigationManager::instance().register_overlay_instance(
                nozzle_temp_panel_, temp_control_panel_->get_nozzle_lifecycle());
            lv_obj_add_flag(nozzle_temp_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[TempStackWidget] Nozzle temp panel created");
        } else {
            spdlog::error("[TempStackWidget] Failed to create nozzle temp panel");
            NOTIFY_ERROR("Failed to load temperature panel");
            return;
        }
    }

    if (nozzle_temp_panel_) {
        NavigationManager::instance().push_overlay(nozzle_temp_panel_);
    }
}

void TempStackWidget::handle_bed_clicked() {
    spdlog::info("[TempStackWidget] Bed clicked - opening bed temp panel");

    if (!temp_control_panel_) {
        spdlog::error("[TempStackWidget] TempControlPanel not initialized");
        NOTIFY_ERROR("Temperature panel not available");
        return;
    }

    if (!bed_temp_panel_ && parent_screen_) {
        bed_temp_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "bed_temp_panel", nullptr));
        if (bed_temp_panel_) {
            temp_control_panel_->setup_bed_panel(bed_temp_panel_, parent_screen_);
            NavigationManager::instance().register_overlay_instance(
                bed_temp_panel_, temp_control_panel_->get_bed_lifecycle());
            lv_obj_add_flag(bed_temp_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[TempStackWidget] Bed temp panel created");
        } else {
            spdlog::error("[TempStackWidget] Failed to create bed temp panel");
            NOTIFY_ERROR("Failed to load temperature panel");
            return;
        }
    }

    if (bed_temp_panel_) {
        NavigationManager::instance().push_overlay(bed_temp_panel_);
    }
}

void TempStackWidget::temp_stack_nozzle_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[TempStackWidget] temp_stack_nozzle_cb");
    (void)e;
    if (s_active_instance) {
        s_active_instance->handle_nozzle_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void TempStackWidget::temp_stack_bed_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[TempStackWidget] temp_stack_bed_cb");
    (void)e;
    if (s_active_instance) {
        s_active_instance->handle_bed_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}
