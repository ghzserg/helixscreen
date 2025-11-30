// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_temp_control.h"

#include "ui_component_keypad.h"
#include "ui_error_reporting.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_subject_registry.h"
#include "ui_temperature_utils.h"
#include "ui_theme.h"
#include "ui_utils.h"

#include "app_constants.h"
#include "moonraker_api.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstring>
#include <ctime>
#include <memory>

TempControlPanel::TempControlPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : printer_state_(printer_state), api_(api),
      nozzle_min_temp_(AppConstants::Temperature::DEFAULT_MIN_TEMP),
      nozzle_max_temp_(AppConstants::Temperature::DEFAULT_NOZZLE_MAX),
      bed_min_temp_(AppConstants::Temperature::DEFAULT_MIN_TEMP),
      bed_max_temp_(AppConstants::Temperature::DEFAULT_BED_MAX) {

    nozzle_config_ = {.type = HEATER_NOZZLE,
                      .name = "Nozzle",
                      .title = "Nozzle Temperature",
                      .color = lv_color_hex(0xFF4444),
                      .temp_range_max = 320.0f,
                      .y_axis_increment = 80,
                      .presets = {0, 210, 240, 250},
                      .keypad_range = {0.0f, 350.0f}};

    bed_config_ = {.type = HEATER_BED,
                   .name = "Bed",
                   .title = "Heatbed Temperature",
                   .color = lv_color_hex(0x00CED1),
                   .temp_range_max = 140.0f,
                   .y_axis_increment = 35,
                   .presets = {0, 60, 80, 100},
                   .keypad_range = {0.0f, 150.0f}};

    nozzle_current_buf_.fill('\0');
    nozzle_target_buf_.fill('\0');
    bed_current_buf_.fill('\0');
    bed_target_buf_.fill('\0');
    nozzle_display_buf_.fill('\0');
    bed_display_buf_.fill('\0');

    // Subscribe to PrinterState temperature subjects (ObserverGuard handles cleanup)
    nozzle_temp_observer_ = ObserverGuard(printer_state_.get_extruder_temp_subject(),
                                          nozzle_temp_observer_cb, this);
    nozzle_target_observer_ = ObserverGuard(printer_state_.get_extruder_target_subject(),
                                            nozzle_target_observer_cb, this);
    bed_temp_observer_ = ObserverGuard(printer_state_.get_bed_temp_subject(),
                                       bed_temp_observer_cb, this);
    bed_target_observer_ = ObserverGuard(printer_state_.get_bed_target_subject(),
                                         bed_target_observer_cb, this);

    spdlog::debug("[TempPanel] Constructed - subscribed to PrinterState temperature subjects");
}

void TempControlPanel::nozzle_temp_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<TempControlPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_nozzle_temp_changed(lv_subject_get_int(subject));
    }
}

void TempControlPanel::nozzle_target_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<TempControlPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_nozzle_target_changed(lv_subject_get_int(subject));
    }
}

void TempControlPanel::bed_temp_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<TempControlPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_bed_temp_changed(lv_subject_get_int(subject));
    }
}

void TempControlPanel::bed_target_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<TempControlPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_bed_target_changed(lv_subject_get_int(subject));
    }
}

void TempControlPanel::on_nozzle_temp_changed(int temp) {
    nozzle_current_ = temp;
    update_nozzle_display();

    // Guard: don't track graph points until subjects initialized
    if (!subjects_initialized_) {
        return;
    }

    // Track timestamp on first point
    if (nozzle_start_time_ms_ == 0) {
        nozzle_start_time_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
    }
    nozzle_point_count_++;

    // Update subject for reactive X-axis label visibility
    lv_subject_set_int(&nozzle_graph_points_subject_, nozzle_point_count_);

    // Push to graph if it exists
    if (nozzle_graph_ && nozzle_series_id_ >= 0) {
        ui_temp_graph_update_series(nozzle_graph_, nozzle_series_id_, static_cast<float>(temp));
        update_x_axis_labels(nozzle_x_labels_, nozzle_start_time_ms_, nozzle_point_count_);
        spdlog::trace("[TempPanel] Nozzle graph updated: {}°C (point #{})", temp, nozzle_point_count_);
    }
}

void TempControlPanel::on_nozzle_target_changed(int target) {
    nozzle_target_ = target;
    update_nozzle_display();

    // Update target line on graph
    if (nozzle_graph_ && nozzle_series_id_ >= 0) {
        bool show_target = (target > 0);
        ui_temp_graph_set_series_target(nozzle_graph_, nozzle_series_id_,
                                        static_cast<float>(target), show_target);
        spdlog::trace("[TempPanel] Nozzle target line: {}°C (visible={})", target, show_target);
    }
}

void TempControlPanel::on_bed_temp_changed(int temp) {
    bed_current_ = temp;
    update_bed_display();

    // Guard: don't track graph points until subjects initialized
    if (!subjects_initialized_) {
        return;
    }

    // Track timestamp on first point
    if (bed_start_time_ms_ == 0) {
        bed_start_time_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    }
    bed_point_count_++;

    // Update subject for reactive X-axis label visibility
    lv_subject_set_int(&bed_graph_points_subject_, bed_point_count_);

    // Push to graph if it exists
    if (bed_graph_ && bed_series_id_ >= 0) {
        ui_temp_graph_update_series(bed_graph_, bed_series_id_, static_cast<float>(temp));
        update_x_axis_labels(bed_x_labels_, bed_start_time_ms_, bed_point_count_);
        spdlog::trace("[TempPanel] Bed graph updated: {}°C (point #{})", temp, bed_point_count_);
    }
}

void TempControlPanel::on_bed_target_changed(int target) {
    bed_target_ = target;
    update_bed_display();

    // Update target line on graph
    if (bed_graph_ && bed_series_id_ >= 0) {
        bool show_target = (target > 0);
        ui_temp_graph_set_series_target(bed_graph_, bed_series_id_, static_cast<float>(target),
                                        show_target);
        spdlog::trace("[TempPanel] Bed target line: {}°C (visible={})", target, show_target);
    }
}

void TempControlPanel::update_nozzle_display() {
    // Guard: don't update subject if not initialized yet (observer fires during construction)
    if (!subjects_initialized_) {
        return;
    }

    // Show pending value if user has selected but not confirmed yet
    // Otherwise show actual target from Moonraker
    int display_target = (nozzle_pending_ >= 0) ? nozzle_pending_ : nozzle_target_;

    if (nozzle_pending_ >= 0) {
        // Show pending with asterisk to indicate unsent
        if (nozzle_pending_ > 0) {
            snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(), "%d / %d*",
                     nozzle_current_, nozzle_pending_);
        } else {
            snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(), "%d / --*",
                     nozzle_current_);
        }
    } else if (display_target > 0) {
        snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(), "%d / %d", nozzle_current_,
                 display_target);
    } else {
        snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(), "%d / --",
                 nozzle_current_);
    }
    lv_subject_copy_string(&nozzle_display_subject_, nozzle_display_buf_.data());
}

void TempControlPanel::update_bed_display() {
    // Guard: don't update subject if not initialized yet (observer fires during construction)
    if (!subjects_initialized_) {
        return;
    }

    // Show pending value if user has selected but not confirmed yet
    // Otherwise show actual target from Moonraker
    int display_target = (bed_pending_ >= 0) ? bed_pending_ : bed_target_;

    if (bed_pending_ >= 0) {
        // Show pending with asterisk to indicate unsent
        if (bed_pending_ > 0) {
            snprintf(bed_display_buf_.data(), bed_display_buf_.size(), "%d / %d*", bed_current_,
                     bed_pending_);
        } else {
            snprintf(bed_display_buf_.data(), bed_display_buf_.size(), "%d / --*", bed_current_);
        }
    } else if (display_target > 0) {
        snprintf(bed_display_buf_.data(), bed_display_buf_.size(), "%d / %d", bed_current_,
                 display_target);
    } else {
        snprintf(bed_display_buf_.data(), bed_display_buf_.size(), "%d / --", bed_current_);
    }

    spdlog::debug("[TempPanel] Bed display: '{}' (pending={}, target={}, current={})",
                  bed_display_buf_.data(), bed_pending_, bed_target_, bed_current_);

    lv_subject_copy_string(&bed_display_subject_, bed_display_buf_.data());
}

void TempControlPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[TempPanel] init_subjects() called twice - ignoring");
        return;
    }

    // Format initial strings
    snprintf(nozzle_current_buf_.data(), nozzle_current_buf_.size(), "%d°C", nozzle_current_);
    snprintf(nozzle_target_buf_.data(), nozzle_target_buf_.size(), "%d°C", nozzle_target_);
    snprintf(bed_current_buf_.data(), bed_current_buf_.size(), "%d°C", bed_current_);
    snprintf(bed_target_buf_.data(), bed_target_buf_.size(), "%d°C", bed_target_);
    snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(), "%d / %d°C", nozzle_current_,
             nozzle_target_);
    snprintf(bed_display_buf_.data(), bed_display_buf_.size(), "%d / %d°C", bed_current_,
             bed_target_);

    // Initialize and register subjects
    UI_SUBJECT_INIT_AND_REGISTER_STRING(nozzle_current_subject_, nozzle_current_buf_.data(),
                                        nozzle_current_buf_.data(), "nozzle_current_temp");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(nozzle_target_subject_, nozzle_target_buf_.data(),
                                        nozzle_target_buf_.data(), "nozzle_target_temp");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_current_subject_, bed_current_buf_.data(),
                                        bed_current_buf_.data(), "bed_current_temp");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_target_subject_, bed_target_buf_.data(),
                                        bed_target_buf_.data(), "bed_target_temp");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(nozzle_display_subject_, nozzle_display_buf_.data(),
                                        nozzle_display_buf_.data(), "nozzle_temp_display");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_display_subject_, bed_display_buf_.data(),
                                        bed_display_buf_.data(), "bed_temp_display");

    // Point count subjects for reactive X-axis label visibility
    // Labels become visible when count >= 60 (bound in XML with bind_flag_if_lt)
    UI_SUBJECT_INIT_AND_REGISTER_INT(nozzle_graph_points_subject_, 0, "nozzle_graph_points");
    UI_SUBJECT_INIT_AND_REGISTER_INT(bed_graph_points_subject_, 0, "bed_graph_points");

    subjects_initialized_ = true;
    spdlog::debug("[TempPanel] Subjects initialized: nozzle={}/{}°C, bed={}/{}°C", nozzle_current_,
                  nozzle_target_, bed_current_, bed_target_);
}

ui_temp_graph_t* TempControlPanel::create_temp_graph(lv_obj_t* chart_area,
                                                     const heater_config_t* config, int target_temp,
                                                     int* series_id_out) {
    if (!chart_area)
        return nullptr;

    ui_temp_graph_t* graph = ui_temp_graph_create(chart_area);
    if (!graph)
        return nullptr;

    lv_obj_t* chart = ui_temp_graph_get_chart(graph);
    lv_obj_set_size(chart, lv_pct(100), lv_pct(100));

    // Configure temperature range
    ui_temp_graph_set_temp_range(graph, 0.0f, config->temp_range_max);

    // Add series
    int series_id = ui_temp_graph_add_series(graph, config->name, config->color);
    if (series_id_out) {
        *series_id_out = series_id;
    }

    if (series_id >= 0) {
        // Set target temperature line (show if target > 0)
        bool show_target = (target_temp > 0);
        ui_temp_graph_set_series_target(graph, series_id, static_cast<float>(target_temp),
                                        show_target);

        // Graph starts empty - real-time data comes from PrinterState observers
        spdlog::debug("[TempPanel] {} graph created (awaiting live data)", config->name);
    }

    return graph;
}

void TempControlPanel::create_y_axis_labels(lv_obj_t* container, const heater_config_t* config) {
    if (!container)
        return;

    int num_labels = static_cast<int>(config->temp_range_max / config->y_axis_increment) + 1;

    // Create labels from top to bottom
    for (int i = num_labels - 1; i >= 0; i--) {
        int temp = i * config->y_axis_increment;
        lv_obj_t* label = lv_label_create(container);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d°", temp);
        lv_label_set_text(label, buf);
        lv_obj_set_style_text_font(label, UI_FONT_SMALL, 0);
    }
}

void TempControlPanel::create_x_axis_labels(
    lv_obj_t* container, std::array<lv_obj_t*, X_AXIS_LABEL_COUNT>& labels) {
    if (!container)
        return;

    // Labels are now defined in XML with reactive visibility bindings.
    // Find them by name instead of creating programmatically.
    static const char* label_names[X_AXIS_LABEL_COUNT] = {"x_label_0", "x_label_1", "x_label_2",
                                                          "x_label_3", "x_label_4", "x_label_5"};

    for (int i = 0; i < X_AXIS_LABEL_COUNT; i++) {
        labels[i] = lv_obj_find_by_name(container, label_names[i]);
        if (!labels[i]) {
            spdlog::warn("[TempPanel] X-axis label '{}' not found in container", label_names[i]);
        }
    }
}

void TempControlPanel::update_x_axis_labels(
    std::array<lv_obj_t*, X_AXIS_LABEL_COUNT>& labels, int64_t start_time_ms, int point_count) {

    // Graph shows 300 points at 1-second intervals = 5 minutes
    constexpr int MAX_POINTS = 300;
    constexpr int64_t UPDATE_INTERVAL_MS = 1000;

    // Minimum points needed before updating labels (visibility controlled reactively via XML binding)
    constexpr int MIN_POINTS_FOR_LABELS = 60;

    // Don't update text until we have enough data (visibility is handled by XML binding)
    if (start_time_ms == 0 || point_count < MIN_POINTS_FOR_LABELS) {
        return;
    }

    // Calculate visible time span
    int visible_points = std::min(point_count, MAX_POINTS);
    int64_t visible_duration_ms = visible_points * UPDATE_INTERVAL_MS;

    // Current time (rightmost point)
    int64_t now_ms = start_time_ms + (point_count * UPDATE_INTERVAL_MS);

    // Oldest visible time (leftmost point)
    int64_t oldest_ms = now_ms - visible_duration_ms;

    // Interval between labels
    int64_t label_interval_ms = visible_duration_ms / (X_AXIS_LABEL_COUNT - 1);

    // Update label text (visibility controlled reactively by bind_flag_if_lt in XML)
    for (int i = 0; i < X_AXIS_LABEL_COUNT; i++) {
        if (!labels[i])
            continue;

        int64_t label_time_ms = oldest_ms + (i * label_interval_ms);
        time_t label_time = static_cast<time_t>(label_time_ms / 1000);

        struct tm* tm_info = localtime(&label_time);
        char buf[8];
        strftime(buf, sizeof(buf), "%H:%M", tm_info);
        lv_label_set_text(labels[i], buf);
    }
}

void TempControlPanel::nozzle_confirm_cb(lv_event_t* e) {
    auto* self = static_cast<TempControlPanel*>(lv_event_get_user_data(e));
    if (!self)
        return;

    // Use pending value if set, otherwise use current target (fallback, shouldn't happen)
    int target = (self->nozzle_pending_ >= 0) ? self->nozzle_pending_ : self->nozzle_target_;

    spdlog::debug("[TempPanel] Nozzle temperature confirmed: {}°C (pending={})", target,
                 self->nozzle_pending_);

    // Clear pending BEFORE navigation (since we're about to send the command)
    self->nozzle_pending_ = -1;

    if (self->api_) {
        self->api_->set_temperature(
            "extruder", static_cast<double>(target),
            [target]() {
                if (target == 0) {
                    NOTIFY_SUCCESS("Nozzle heater turned off");
                } else {
                    NOTIFY_SUCCESS("Nozzle target set to {}°C", target);
                }
            },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Failed to set nozzle temp: {}", error.user_message());
            });
    } else {
        NOTIFY_WARNING("Not connected to printer");
    }

    ui_nav_go_back();
}

void TempControlPanel::bed_confirm_cb(lv_event_t* e) {
    auto* self = static_cast<TempControlPanel*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("[TempPanel] bed_confirm_cb: self is null!");
        return;
    }

    // Use pending value if set, otherwise use current target (fallback, shouldn't happen)
    int target = (self->bed_pending_ >= 0) ? self->bed_pending_ : self->bed_target_;

    spdlog::debug("[TempPanel] Bed temperature confirmed: {}°C (pending={}, api_={})", target,
                 self->bed_pending_, self->api_ ? "valid" : "NULL");

    // Clear pending BEFORE navigation (since we're about to send the command)
    self->bed_pending_ = -1;

    if (self->api_) {
        spdlog::debug("[TempPanel] Calling api_->set_temperature(heater_bed, {})", target);
        self->api_->set_temperature(
            "heater_bed", static_cast<double>(target),
            [target]() {
                spdlog::debug("[TempPanel] set_temperature SUCCESS for bed: {}°C", target);
                if (target == 0) {
                    NOTIFY_SUCCESS("Bed heater turned off");
                } else {
                    NOTIFY_SUCCESS("Bed target set to {}°C", target);
                }
            },
            [](const MoonrakerError& error) {
                spdlog::error("[TempPanel] set_temperature FAILED: {}", error.message);
                NOTIFY_ERROR("Failed to set bed temp: {}", error.user_message());
            });
    } else {
        spdlog::warn("[TempPanel] api_ is null - not connected");
        NOTIFY_WARNING("Not connected to printer");
    }

    ui_nav_go_back();
}

// Struct to pass context to preset button callback
struct PresetCallbackData {
    TempControlPanel* panel;
    heater_type_t type;
    int temp;
};

void TempControlPanel::preset_button_cb(lv_event_t* e) {
    auto* data = static_cast<PresetCallbackData*>(lv_event_get_user_data(e));
    if (!data || !data->panel)
        return;

    if (data->type == HEATER_NOZZLE) {
        data->panel->nozzle_pending_ = data->temp;
        data->panel->update_nozzle_display();
    } else {
        data->panel->bed_pending_ = data->temp;
        data->panel->update_bed_display();
    }

    spdlog::debug("[TempPanel] {} pending selection: {}°C (not sent yet)",
                  data->type == HEATER_NOZZLE ? "Nozzle" : "Bed", data->temp);
}

// Struct for keypad callback
struct KeypadCallbackData {
    TempControlPanel* panel;
    heater_type_t type;
};

void TempControlPanel::keypad_value_cb(float value, void* user_data) {
    auto* data = static_cast<KeypadCallbackData*>(user_data);
    if (!data || !data->panel)
        return;

    int temp = static_cast<int>(value);
    if (data->type == HEATER_NOZZLE) {
        data->panel->nozzle_pending_ = temp;
        data->panel->update_nozzle_display();
    } else {
        data->panel->bed_pending_ = temp;
        data->panel->update_bed_display();
    }

    spdlog::debug("[TempPanel] {} pending selection: {}°C via keypad (not sent yet)",
                  data->type == HEATER_NOZZLE ? "Nozzle" : "Bed", temp);
}

void TempControlPanel::custom_button_cb(lv_event_t* e) {
    auto* data = static_cast<KeypadCallbackData*>(lv_event_get_user_data(e));
    if (!data || !data->panel)
        return;

    const heater_config_t& config =
        (data->type == HEATER_NOZZLE) ? data->panel->nozzle_config_ : data->panel->bed_config_;

    int current_target =
        (data->type == HEATER_NOZZLE) ? data->panel->nozzle_target_ : data->panel->bed_target_;

    ui_keypad_config_t keypad_config = {
        .initial_value = static_cast<float>(current_target),
        .min_value = config.keypad_range.min,
        .max_value = config.keypad_range.max,
        .title_label = (data->type == HEATER_NOZZLE) ? "Nozzle Temp" : "Heat Bed Temp",
        .unit_label = "°C",
        .allow_decimal = false,
        .allow_negative = false,
        .callback = keypad_value_cb,
        .user_data = data};

    ui_keypad_show(&keypad_config);
}

// Static storage for callback data (needed because LVGL holds raw pointers)
// These persist for the lifetime of the application
static PresetCallbackData nozzle_preset_data[4];
static PresetCallbackData bed_preset_data[4];
static KeypadCallbackData nozzle_keypad_data;
static KeypadCallbackData bed_keypad_data;

void TempControlPanel::setup_preset_buttons(lv_obj_t* panel, heater_type_t type) {
    const char* preset_names[] = {"preset_off", "preset_pla", "preset_petg", "preset_abs"};
    const heater_config_t& config = (type == HEATER_NOZZLE) ? nozzle_config_ : bed_config_;
    PresetCallbackData* preset_data =
        (type == HEATER_NOZZLE) ? nozzle_preset_data : bed_preset_data;

    int presets[] = {config.presets.off, config.presets.pla, config.presets.petg,
                     config.presets.abs};

    for (int i = 0; i < 4; i++) {
        lv_obj_t* btn = lv_obj_find_by_name(panel, preset_names[i]);
        if (btn) {
            preset_data[i] = {this, type, presets[i]};
            lv_obj_add_event_cb(btn, preset_button_cb, LV_EVENT_CLICKED, &preset_data[i]);
        }
    }
}

void TempControlPanel::setup_custom_button(lv_obj_t* panel, heater_type_t type) {
    lv_obj_t* btn = lv_obj_find_by_name(panel, "btn_custom");
    if (btn) {
        KeypadCallbackData* data = (type == HEATER_NOZZLE) ? &nozzle_keypad_data : &bed_keypad_data;
        *data = {this, type};
        lv_obj_add_event_cb(btn, custom_button_cb, LV_EVENT_CLICKED, data);
    }
}

void TempControlPanel::setup_confirm_button(lv_obj_t* header, heater_type_t type) {
    lv_obj_t* right_button = lv_obj_find_by_name(header, "right_button");
    if (right_button) {
        lv_event_cb_t cb = (type == HEATER_NOZZLE) ? nozzle_confirm_cb : bed_confirm_cb;
        lv_obj_add_event_cb(right_button, cb, LV_EVENT_CLICKED, this);
        spdlog::debug("[TempPanel] {} confirm button wired",
                      type == HEATER_NOZZLE ? "Nozzle" : "Bed");
    }
}

void TempControlPanel::setup_nozzle_panel(lv_obj_t* panel, lv_obj_t* parent_screen) {
    nozzle_panel_ = panel;

    // Read current values from PrinterState (observers only fire on changes, not initial state)
    nozzle_current_ = lv_subject_get_int(printer_state_.get_extruder_temp_subject());
    nozzle_target_ = lv_subject_get_int(printer_state_.get_extruder_target_subject());
    spdlog::debug("[TempPanel] Nozzle initial state from PrinterState: current={}°C, target={}°C",
                 nozzle_current_, nozzle_target_);

    // Update display with initial values
    update_nozzle_display();

    // Use standard overlay panel setup
    ui_overlay_panel_setup_standard(panel, parent_screen, "overlay_header", "overlay_content");

    lv_obj_t* overlay_content = lv_obj_find_by_name(panel, "overlay_content");
    if (!overlay_content) {
        spdlog::error("[TempPanel] Nozzle: overlay_content not found!");
        return;
    }

    // Load theme-aware graph color
    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("nozzle_temp_panel");
    if (scope) {
        bool use_dark_mode = ui_theme_is_dark_mode();
        const char* color_str = lv_xml_get_const(scope, use_dark_mode ? "temp_graph_nozzle_dark"
                                                                      : "temp_graph_nozzle_light");
        if (color_str) {
            nozzle_config_.color = ui_theme_parse_color(color_str);
            spdlog::debug("[TempPanel] Nozzle graph color: {} ({})", color_str,
                          use_dark_mode ? "dark" : "light");
        }
    }

    spdlog::debug("[TempPanel] Setting up nozzle panel...");

    // Create Y-axis labels
    lv_obj_t* y_axis_labels = lv_obj_find_by_name(overlay_content, "y_axis_labels");
    if (y_axis_labels) {
        create_y_axis_labels(y_axis_labels, &nozzle_config_);
    }

    // Create temperature graph
    lv_obj_t* chart_area = lv_obj_find_by_name(overlay_content, "chart_area");
    if (chart_area) {
        nozzle_graph_ =
            create_temp_graph(chart_area, &nozzle_config_, nozzle_target_, &nozzle_series_id_);
    }

    // Create X-axis time labels
    lv_obj_t* x_axis_labels = lv_obj_find_by_name(overlay_content, "x_axis_labels");
    if (x_axis_labels) {
        create_x_axis_labels(x_axis_labels, nozzle_x_labels_);
    }

    // Wire up confirm button
    lv_obj_t* header = lv_obj_find_by_name(panel, "overlay_header");
    if (header) {
        setup_confirm_button(header, HEATER_NOZZLE);
    }

    // Wire up preset and custom buttons
    setup_preset_buttons(overlay_content, HEATER_NOZZLE);
    setup_custom_button(overlay_content, HEATER_NOZZLE);

    spdlog::debug("[TempPanel] Nozzle panel setup complete!");
}

void TempControlPanel::setup_bed_panel(lv_obj_t* panel, lv_obj_t* parent_screen) {
    bed_panel_ = panel;

    // Read current values from PrinterState (observers only fire on changes, not initial state)
    bed_current_ = lv_subject_get_int(printer_state_.get_bed_temp_subject());
    bed_target_ = lv_subject_get_int(printer_state_.get_bed_target_subject());
    spdlog::debug("[TempPanel] Bed initial state from PrinterState: current={}°C, target={}°C",
                 bed_current_, bed_target_);

    // Update display with initial values
    update_bed_display();

    // Use standard overlay panel setup
    ui_overlay_panel_setup_standard(panel, parent_screen, "overlay_header", "overlay_content");

    lv_obj_t* overlay_content = lv_obj_find_by_name(panel, "overlay_content");
    if (!overlay_content) {
        spdlog::error("[TempPanel] Bed: overlay_content not found!");
        return;
    }

    // Load theme-aware graph color
    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("bed_temp_panel");
    if (scope) {
        bool use_dark_mode = ui_theme_is_dark_mode();
        const char* color_str =
            lv_xml_get_const(scope, use_dark_mode ? "temp_graph_bed_dark" : "temp_graph_bed_light");
        if (color_str) {
            bed_config_.color = ui_theme_parse_color(color_str);
            spdlog::debug("[TempPanel] Bed graph color: {} ({})", color_str,
                          use_dark_mode ? "dark" : "light");
        }
    }

    spdlog::debug("[TempPanel] Setting up bed panel...");

    // Create Y-axis labels
    lv_obj_t* y_axis_labels = lv_obj_find_by_name(overlay_content, "y_axis_labels");
    if (y_axis_labels) {
        create_y_axis_labels(y_axis_labels, &bed_config_);
    }

    // Create temperature graph
    lv_obj_t* chart_area = lv_obj_find_by_name(overlay_content, "chart_area");
    if (chart_area) {
        bed_graph_ = create_temp_graph(chart_area, &bed_config_, bed_target_, &bed_series_id_);
    }

    // Create X-axis time labels
    lv_obj_t* x_axis_labels = lv_obj_find_by_name(overlay_content, "x_axis_labels");
    if (x_axis_labels) {
        create_x_axis_labels(x_axis_labels, bed_x_labels_);
    }

    // Wire up confirm button
    lv_obj_t* header = lv_obj_find_by_name(panel, "overlay_header");
    if (header) {
        setup_confirm_button(header, HEATER_BED);
    }

    // Wire up preset and custom buttons
    setup_preset_buttons(overlay_content, HEATER_BED);
    setup_custom_button(overlay_content, HEATER_BED);

    spdlog::debug("[TempPanel] Bed panel setup complete!");
}

void TempControlPanel::set_nozzle(int current, int target) {
    UITemperatureUtils::validate_and_clamp_pair(current, target, nozzle_min_temp_, nozzle_max_temp_,
                                                "TempPanel/Nozzle");

    nozzle_current_ = current;
    nozzle_target_ = target;
    update_nozzle_display();
}

void TempControlPanel::set_bed(int current, int target) {
    UITemperatureUtils::validate_and_clamp_pair(current, target, bed_min_temp_, bed_max_temp_,
                                                "TempPanel/Bed");

    bed_current_ = current;
    bed_target_ = target;
    update_bed_display();
}

void TempControlPanel::set_nozzle_limits(int min_temp, int max_temp) {
    nozzle_min_temp_ = min_temp;
    nozzle_max_temp_ = max_temp;
    spdlog::debug("[TempPanel] Nozzle limits updated: {}-{}°C", min_temp, max_temp);
}

void TempControlPanel::set_bed_limits(int min_temp, int max_temp) {
    bed_min_temp_ = min_temp;
    bed_max_temp_ = max_temp;
    spdlog::debug("[TempPanel] Bed limits updated: {}-{}°C", min_temp, max_temp);
}
