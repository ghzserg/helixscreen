// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thermistor_widget.h"

#include "ui_event_safety.h"
#include "ui_icon.h"
#include "ui_temperature_utils.h"

#include "app_globals.h"
#include "config.h"
#include "observer_factory.h"
#include "panel_widget_config.h"
#include "panel_widget_manager.h"
#include "panel_widget_registry.h"
#include "printer_state.h"
#include "temperature_sensor_manager.h"

#include <spdlog/spdlog.h>

#include <cstring>

namespace {
const bool s_registered = [] {
    helix::register_widget_factory("thermistor", []() {
        auto& ps = get_printer_state();
        return std::make_unique<helix::ThermistorWidget>(ps);
    });
    return true;
}();
} // namespace

using namespace helix;
using helix::ui::temperature::centi_to_degrees;

// File-local helper: get the shared PanelWidgetConfig instance
static helix::PanelWidgetConfig& get_widget_config_ref() {
    static helix::PanelWidgetConfig config("home", *Config::get_instance());
    config.load();
    return config;
}

ThermistorWidget::ThermistorWidget(PrinterState& /*printer_state*/) {
    std::strcpy(temp_buffer_, "--\xC2\xB0"
                              "C"); // "--°C"
}

ThermistorWidget::~ThermistorWidget() {
    detach();
}

void ThermistorWidget::attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) {
    widget_obj_ = widget_obj;
    parent_screen_ = parent_screen;

    if (widget_obj_) {
        lv_obj_set_user_data(widget_obj_, this);
    }

    // Cache label pointers
    temp_label_ = lv_obj_find_by_name(widget_obj_, "thermistor_temp");
    name_label_ = lv_obj_find_by_name(widget_obj_, "thermistor_name");

    // Load saved sensor selection from config
    load_config();

    // If no sensor saved, auto-select first available
    if (selected_sensor_.empty()) {
        auto& tsm = helix::sensors::TemperatureSensorManager::instance();
        auto sensors = tsm.get_sensors_sorted();
        if (!sensors.empty()) {
            select_sensor(sensors.front().klipper_name);
        }
    } else {
        // Re-bind observer to saved sensor
        auto& tsm = helix::sensors::TemperatureSensorManager::instance();
        lv_subject_t* subject = tsm.get_temp_subject(selected_sensor_);
        if (subject) {
            temp_observer_ = helix::ui::observe_int_sync<ThermistorWidget>(
                subject, this,
                [](ThermistorWidget* self, int temp) { self->on_temp_changed(temp); });
        }
        update_display();
    }

    spdlog::debug("[ThermistorWidget] Attached (sensor: {})",
                  selected_sensor_.empty() ? "none" : selected_sensor_);
}

void ThermistorWidget::detach() {
    dismiss_sensor_picker();
    temp_observer_.reset();

    if (widget_obj_) {
        lv_obj_set_user_data(widget_obj_, nullptr);
        widget_obj_ = nullptr;
    }
    parent_screen_ = nullptr;
    temp_label_ = nullptr;
    name_label_ = nullptr;

    spdlog::debug("[ThermistorWidget] Detached");
}

void ThermistorWidget::handle_clicked() {
    spdlog::info("[ThermistorWidget] Clicked - showing sensor picker");
    show_sensor_picker();
}

void ThermistorWidget::select_sensor(const std::string& klipper_name) {
    if (klipper_name == selected_sensor_) {
        return;
    }

    // Reset existing observer
    temp_observer_.reset();

    selected_sensor_ = klipper_name;

    // Look up display name from sensor config
    auto& tsm = helix::sensors::TemperatureSensorManager::instance();
    auto sensors = tsm.get_sensors_sorted();
    display_name_ = klipper_name; // fallback
    for (const auto& s : sensors) {
        if (s.klipper_name == klipper_name) {
            display_name_ = s.display_name;
            break;
        }
    }

    // Subscribe to this sensor's temperature subject
    lv_subject_t* subject = tsm.get_temp_subject(klipper_name);
    if (subject) {
        temp_observer_ = helix::ui::observe_int_sync<ThermistorWidget>(
            subject, this, [](ThermistorWidget* self, int temp) { self->on_temp_changed(temp); });
    } else {
        spdlog::warn("[ThermistorWidget] No subject for sensor: {}", klipper_name);
    }

    update_display();
    save_config();

    spdlog::info("[ThermistorWidget] Selected sensor: {} ({})", display_name_, klipper_name);
}

void ThermistorWidget::on_temp_changed(int centidegrees) {
    int deg = centi_to_degrees(centidegrees);
    helix::ui::temperature::format_temperature(deg, temp_buffer_, sizeof(temp_buffer_));

    if (temp_label_) {
        lv_label_set_text(temp_label_, temp_buffer_);
    }

    spdlog::trace("[ThermistorWidget] {} = {}°C", display_name_, deg);
}

void ThermistorWidget::update_display() {
    if (temp_label_) {
        if (selected_sensor_.empty()) {
            lv_label_set_text(temp_label_, "--\xC2\xB0"
                                           "C");
        } else {
            // Read current value from subject
            auto& tsm = helix::sensors::TemperatureSensorManager::instance();
            lv_subject_t* subject = tsm.get_temp_subject(selected_sensor_);
            if (subject) {
                int centi = lv_subject_get_int(subject);
                int deg = centi_to_degrees(centi);
                helix::ui::temperature::format_temperature(deg, temp_buffer_, sizeof(temp_buffer_));
                lv_label_set_text(temp_label_, temp_buffer_);
            } else {
                lv_label_set_text(temp_label_, "--\xC2\xB0"
                                               "C");
            }
        }
    }

    if (name_label_) {
        if (selected_sensor_.empty()) {
            lv_label_set_text(name_label_, "Select sensor");
        } else {
            lv_label_set_text(name_label_, display_name_.c_str());
        }
    }
}

void ThermistorWidget::load_config() {
    auto& wc = get_widget_config_ref();
    auto config = wc.get_widget_config("thermistor");
    if (config.contains("sensor") && config["sensor"].is_string()) {
        selected_sensor_ = config["sensor"].get<std::string>();

        // Resolve display name
        auto& tsm = helix::sensors::TemperatureSensorManager::instance();
        auto sensors = tsm.get_sensors_sorted();
        display_name_ = selected_sensor_;
        for (const auto& s : sensors) {
            if (s.klipper_name == selected_sensor_) {
                display_name_ = s.display_name;
                break;
            }
        }

        spdlog::debug("[ThermistorWidget] Loaded config: sensor={}", selected_sensor_);
    }
}

void ThermistorWidget::save_config() {
    if (selected_sensor_.empty()) {
        return;
    }
    nlohmann::json config;
    config["sensor"] = selected_sensor_;

    auto& wc = get_widget_config_ref();
    wc.set_widget_config("thermistor", config);

    spdlog::debug("[ThermistorWidget] Saved config: sensor={}", selected_sensor_);
}

void ThermistorWidget::show_sensor_picker() {
    if (picker_backdrop_ || !parent_screen_) {
        return;
    }

    auto& tsm = helix::sensors::TemperatureSensorManager::instance();
    auto sensors = tsm.get_sensors_sorted();
    if (sensors.empty()) {
        spdlog::warn("[ThermistorWidget] No sensors available for picker");
        return;
    }

    // Create picker from XML
    picker_backdrop_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "thermistor_sensor_picker", nullptr));
    if (!picker_backdrop_) {
        spdlog::error("[ThermistorWidget] Failed to create sensor picker from XML");
        return;
    }

    // Find the sensor_list container
    lv_obj_t* sensor_list = lv_obj_find_by_name(picker_backdrop_, "sensor_list");
    if (!sensor_list) {
        spdlog::error("[ThermistorWidget] sensor_list not found in picker XML");
        lv_obj_delete(picker_backdrop_);
        picker_backdrop_ = nullptr;
        return;
    }

    // Populate sensor rows
    for (const auto& sensor : sensors) {
        bool is_selected = (sensor.klipper_name == selected_sensor_);

        // Create a row button for each sensor
        lv_obj_t* row = lv_obj_create(sensor_list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_set_style_pad_gap(row, 4, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        // Highlight selected row
        if (is_selected) {
            lv_obj_set_style_bg_opa(row, 30, 0);
        } else {
            lv_obj_set_style_bg_opa(row, 0, 0);
        }

        // Sensor display name
        lv_obj_t* name = lv_label_create(row);
        lv_label_set_text(name, sensor.display_name.c_str());
        lv_obj_set_flex_grow(name, 1);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_font(name, lv_font_get_default(), 0);

        // Current temperature
        auto state = tsm.get_sensor_state(sensor.klipper_name);
        char temp_buf[16];
        if (state && state->available) {
            helix::ui::temperature::format_temperature(static_cast<int>(state->temperature),
                                                       temp_buf, sizeof(temp_buf));
        } else {
            std::strcpy(temp_buf, "--\xC2\xB0"
                                  "C");
        }
        lv_obj_t* temp = lv_label_create(row);
        lv_label_set_text(temp, temp_buf);
        lv_obj_set_style_text_font(temp, lv_font_get_default(), 0);
        lv_obj_set_style_text_opa(temp, 180, 0);

        // Store klipper_name as user_data for click handler
        // Allocate string on heap; cleaned up when picker is dismissed (lv_obj_delete)
        auto* klipper_name_copy = new std::string(sensor.klipper_name);
        lv_obj_set_user_data(row, klipper_name_copy);

        lv_obj_add_event_cb(
            row,
            [](lv_event_t* e) {
                LVGL_SAFE_EVENT_CB_BEGIN("[ThermistorWidget] sensor_row_cb");
                auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                auto* name_ptr = static_cast<std::string*>(lv_obj_get_user_data(target));
                if (!name_ptr)
                    return;

                // Walk up to find the backdrop, which has the ThermistorWidget pointer
                // The picker_backdrop's parent screen has the widget via
                // get_global_home_panel But simpler: use static active instance
                // pattern Actually, just use a static pointer set before showing
                // picker
                if (ThermistorWidget::s_active_picker_) {
                    std::string sensor_name = *name_ptr;
                    ThermistorWidget::s_active_picker_->select_sensor(sensor_name);
                    ThermistorWidget::s_active_picker_->dismiss_sensor_picker();
                }
                LVGL_SAFE_EVENT_CB_END();
            },
            LV_EVENT_CLICKED, nullptr);
    }

    s_active_picker_ = this;

    // Position the context menu card near the widget
    lv_obj_t* card = lv_obj_find_by_name(picker_backdrop_, "context_menu");
    if (card && widget_obj_) {
        // Get widget position on screen
        lv_point_t widget_pos;
        lv_obj_get_coords(widget_obj_, (lv_area_t*)&widget_pos);

        int screen_w = lv_obj_get_width(parent_screen_);
        int screen_h = lv_obj_get_height(parent_screen_);

        // Get actual screen coordinates of the widget
        lv_area_t widget_area;
        lv_obj_get_coords(widget_obj_, &widget_area);

        // Position card below the widget, centered horizontally
        int card_w = 200;
        int card_x = (widget_area.x1 + widget_area.x2) / 2 - card_w / 2;
        int card_y = widget_area.y2 + 4;

        // Clamp to screen bounds
        if (card_x < 4)
            card_x = 4;
        if (card_x + card_w > screen_w - 4)
            card_x = screen_w - card_w - 4;
        if (card_y + 200 > screen_h) {
            // Show above widget instead
            card_y = widget_area.y1 - 200 - 4;
            if (card_y < 4)
                card_y = 4;
        }

        lv_obj_set_pos(card, card_x, card_y);
    }

    spdlog::debug("[ThermistorWidget] Sensor picker shown with {} sensors", sensors.size());
}

void ThermistorWidget::dismiss_sensor_picker() {
    if (!picker_backdrop_) {
        return;
    }

    // Clean up heap-allocated klipper_name strings
    lv_obj_t* sensor_list = lv_obj_find_by_name(picker_backdrop_, "sensor_list");
    if (sensor_list) {
        uint32_t count = lv_obj_get_child_count(sensor_list);
        for (uint32_t i = 0; i < count; ++i) {
            lv_obj_t* row = lv_obj_get_child(sensor_list, i);
            auto* name_ptr = static_cast<std::string*>(lv_obj_get_user_data(row));
            delete name_ptr;
            lv_obj_set_user_data(row, nullptr);
        }
    }

    lv_obj_delete(picker_backdrop_);
    picker_backdrop_ = nullptr;
    s_active_picker_ = nullptr;

    spdlog::debug("[ThermistorWidget] Sensor picker dismissed");
}

// Static callbacks
void ThermistorWidget::thermistor_clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThermistorWidget] thermistor_clicked_cb");
    auto* widget = panel_widget_from_event<ThermistorWidget>(e);
    if (widget) {
        widget->handle_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ThermistorWidget::thermistor_picker_backdrop_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ThermistorWidget] thermistor_picker_backdrop_cb");
    (void)e;
    if (s_active_picker_) {
        s_active_picker_->dismiss_sensor_picker();
    }
    LVGL_SAFE_EVENT_CB_END();
}

// Static instance for picker callbacks
ThermistorWidget* ThermistorWidget::s_active_picker_ = nullptr;
