// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_fan.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_subject_registry.h"
#include "ui_theme.h"
#include "ui_utils.h"

#include "app_globals.h"
#include "moonraker_api.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <memory>

constexpr int FanPanel::PRESET_VALUES[4];

FanPanel::FanPanel(PrinterState& printer_state, MoonrakerAPI* api) : PanelBase(printer_state, api) {
    // Initialize buffer contents
    std::snprintf(part_speed_buf_, sizeof(part_speed_buf_), "0%%");
    std::snprintf(hotend_speed_buf_, sizeof(hotend_speed_buf_), "0%%");
}

void FanPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // Initialize subjects with default values
    UI_SUBJECT_INIT_AND_REGISTER_STRING(part_speed_display_subject_, part_speed_buf_,
                                        part_speed_buf_, "fan_part_speed_display");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(hotend_speed_display_subject_, hotend_speed_buf_,
                                        hotend_speed_buf_, "fan_hotend_speed_display");

    // Register XML event callbacks
    lv_xml_register_event_cb(nullptr, "on_fan_speed_slider_changed", on_slider_value_changed);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized: fan_part_speed_display, fan_hotend_speed_display",
                  get_name());
}

void FanPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    spdlog::info("[{}] Setting up event handlers...", get_name());

    // Use standard overlay panel setup (wires header, back button, handles responsive padding)
    ui_overlay_panel_setup_standard(panel_, parent_screen_, "overlay_header", "overlay_content");

    // Setup all controls
    setup_slider();
    setup_preset_buttons();
    register_fan_observer();

    // Initialize visual state
    update_speed_display();
    update_slider_value_label();

    spdlog::info("[{}] Setup complete!", get_name());
}

void FanPanel::setup_slider() {
    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_, "overlay_content");
    if (!overlay_content) {
        spdlog::error("[{}] overlay_content not found!", get_name());
        return;
    }

    // Find the slider (event handler registered via XML event_cb)
    fan_speed_slider_ = lv_obj_find_by_name(overlay_content, "fan_speed_slider");
    if (fan_speed_slider_) {
        spdlog::debug("[{}] Slider found (0-100%), event wired via XML", get_name());
    } else {
        spdlog::warn("[{}] fan_speed_slider not found", get_name());
    }

    // Find the value label
    fan_speed_value_label_ = lv_obj_find_by_name(overlay_content, "fan_speed_value");

    // Find the status message
    status_message_ = lv_obj_find_by_name(overlay_content, "status_message");
}

void FanPanel::setup_preset_buttons() {
    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_, "overlay_content");
    if (!overlay_content) {
        return;
    }

    const char* preset_names[] = {"preset_off", "preset_50", "preset_75", "preset_100"};

    for (int i = 0; i < 4; i++) {
        preset_buttons_[i] = lv_obj_find_by_name(overlay_content, preset_names[i]);
        if (preset_buttons_[i]) {
            // Pass 'this' as user_data for trampoline, store index in obj
            lv_obj_set_user_data(preset_buttons_[i],
                                 reinterpret_cast<void*>(static_cast<intptr_t>(i)));
            lv_obj_add_event_cb(preset_buttons_[i], on_preset_button_clicked, LV_EVENT_CLICKED,
                                this);
        }
    }

    spdlog::debug("[{}] Preset buttons wired (4 buttons)", get_name());
}

void FanPanel::register_fan_observer() {
    // Get fan speed subject from PrinterState
    lv_subject_t* fan_speed = printer_state_.get_fan_speed_subject();

    if (fan_speed) {
        // Use ObserverGuard for RAII cleanup
        fan_speed_observer_ = ObserverGuard(fan_speed, on_fan_speed_changed, this);
        spdlog::debug("[{}] Subscribed to fan_speed subject", get_name());
    } else {
        spdlog::warn("[{}] fan_speed subject not found - real-time updates unavailable",
                     get_name());
    }
}

void FanPanel::update_speed_display() {
    std::snprintf(part_speed_buf_, sizeof(part_speed_buf_), "%d%%", part_fan_speed_);
    lv_subject_copy_string(&part_speed_display_subject_, part_speed_buf_);

    std::snprintf(hotend_speed_buf_, sizeof(hotend_speed_buf_), "%d%%", hotend_fan_speed_);
    lv_subject_copy_string(&hotend_speed_display_subject_, hotend_speed_buf_);
}

void FanPanel::update_slider_value_label() {
    if (fan_speed_value_label_) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d%%", slider_value_);
        lv_label_set_text(fan_speed_value_label_, buf);
    }
}

void FanPanel::send_fan_speed(int speed) {
    if (!api_) {
        spdlog::warn("[{}] No MoonrakerAPI available - fan command not sent", get_name());
        return;
    }

    // Clamp speed to valid range
    speed = (speed < 0) ? 0 : (speed > 100) ? 100 : speed;

    spdlog::info("[{}] Setting part cooling fan to {}%", get_name(), speed);

    // Use "fan" for the main part cooling fan (uses M106 internally)
    api_->set_fan_speed(
        "fan", static_cast<double>(speed),
        [this, speed]() {
            spdlog::debug("[{}] Fan speed set successfully to {}%", get_name(), speed);
            if (status_message_) {
                lv_label_set_text(status_message_, "");
            }
        },
        [this](const MoonrakerError& err) {
            spdlog::error("[{}] Failed to set fan speed: {}", get_name(), err.message);
            if (status_message_) {
                lv_label_set_text(status_message_, "Failed to set fan speed");
            }
        });
}

void FanPanel::handle_preset_button(int preset_index) {
    if (preset_index < 0 || preset_index >= 4) {
        return;
    }

    int speed = PRESET_VALUES[preset_index];
    slider_value_ = speed;

    // Update slider position
    if (fan_speed_slider_) {
        lv_slider_set_value(fan_speed_slider_, speed, LV_ANIM_ON);
    }

    update_slider_value_label();
    send_fan_speed(speed);

    spdlog::debug("[{}] Preset selected: {}%", get_name(), speed);
}

void FanPanel::handle_slider_changed(int value) {
    slider_value_ = value;
    update_slider_value_label();
    send_fan_speed(value);

    spdlog::debug("[{}] Slider changed to {}%", get_name(), value);
}

void FanPanel::on_preset_button_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FanPanel] on_preset_button_clicked");
    auto* self = static_cast<FanPanel*>(lv_event_get_user_data(e));
    if (self) {
        lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
        int preset_index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn)));
        self->handle_preset_button(preset_index);
    }
    LVGL_SAFE_EVENT_CB_END();
}

void FanPanel::on_slider_value_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FanPanel] on_slider_value_changed");
    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int value = lv_slider_get_value(slider);
    get_global_fan_panel().handle_slider_changed(value);
    LVGL_SAFE_EVENT_CB_END();
}

void FanPanel::on_fan_speed_changed(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<FanPanel*>(lv_observer_get_user_data(observer));
    if (!self) {
        return;
    }

    // Get the new fan speed value (percentage 0-100)
    int new_speed = lv_subject_get_int(subject);

    spdlog::debug("[{}] Fan speed update from subject: {}%", self->get_name(), new_speed);

    // Update local state and refresh display
    self->part_fan_speed_ = new_speed;
    self->update_speed_display();

    // Also update slider if it doesn't match (external change)
    if (self->slider_value_ != new_speed && self->fan_speed_slider_) {
        self->slider_value_ = new_speed;
        lv_slider_set_value(self->fan_speed_slider_, new_speed, LV_ANIM_OFF);
        self->update_slider_value_label();
    }
}

void FanPanel::set_fan_speeds(int part_speed, int hotend_speed) {
    part_fan_speed_ = part_speed;
    hotend_fan_speed_ = hotend_speed;
    update_speed_display();
}

// Global instance accessor
static std::unique_ptr<FanPanel> g_fan_panel;

FanPanel& get_global_fan_panel() {
    if (!g_fan_panel) {
        g_fan_panel = std::make_unique<FanPanel>(get_printer_state(), nullptr);
    }
    return *g_fan_panel;
}
