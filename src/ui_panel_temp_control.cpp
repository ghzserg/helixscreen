// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_temp_control.h"

#include "app_constants.h"
#include "moonraker_api.h"
#include "printer_state.h"
#include "ui_component_keypad.h"
#include "ui_error_reporting.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_subject_registry.h"
#include "ui_temperature_utils.h"
#include "ui_theme.h"
#include "ui_utils.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <memory>

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

TempControlPanel::TempControlPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : printer_state_(printer_state),
      api_(api),
      nozzle_min_temp_(AppConstants::Temperature::DEFAULT_MIN_TEMP),
      nozzle_max_temp_(AppConstants::Temperature::DEFAULT_NOZZLE_MAX),
      bed_min_temp_(AppConstants::Temperature::DEFAULT_MIN_TEMP),
      bed_max_temp_(AppConstants::Temperature::DEFAULT_BED_MAX) {

    // Initialize heater configurations
    nozzle_config_ = {
        .type = HEATER_NOZZLE,
        .name = "Nozzle",
        .title = "Nozzle Temperature",
        .color = lv_color_hex(0xFF4444), // Default red (loaded from XML later)
        .temp_range_max = 320.0f,
        .y_axis_increment = 80,
        .presets = {0, 210, 240, 250},
        .keypad_range = {0.0f, 350.0f}
    };

    bed_config_ = {
        .type = HEATER_BED,
        .name = "Bed",
        .title = "Heatbed Temperature",
        .color = lv_color_hex(0x00CED1), // Default cyan (loaded from XML later)
        .temp_range_max = 140.0f,
        .y_axis_increment = 35,
        .presets = {0, 60, 80, 100},
        .keypad_range = {0.0f, 150.0f}
    };

    // Zero-initialize string buffers
    nozzle_current_buf_.fill('\0');
    nozzle_target_buf_.fill('\0');
    bed_current_buf_.fill('\0');
    bed_target_buf_.fill('\0');
    nozzle_display_buf_.fill('\0');
    bed_display_buf_.fill('\0');

    // Subscribe to PrinterState temperature subjects
    // Pass 'this' as user_data so static callbacks can access instance
    nozzle_temp_observer_ = lv_subject_add_observer(
        printer_state_.get_extruder_temp_subject(),
        nozzle_temp_observer_cb, this);

    nozzle_target_observer_ = lv_subject_add_observer(
        printer_state_.get_extruder_target_subject(),
        nozzle_target_observer_cb, this);

    bed_temp_observer_ = lv_subject_add_observer(
        printer_state_.get_bed_temp_subject(),
        bed_temp_observer_cb, this);

    bed_target_observer_ = lv_subject_add_observer(
        printer_state_.get_bed_target_subject(),
        bed_target_observer_cb, this);

    spdlog::info("[TempPanel] Constructed - subscribed to PrinterState temperature subjects");
}

TempControlPanel::~TempControlPanel() {
    // RAII cleanup: remove all observers
    if (nozzle_temp_observer_) {
        lv_observer_remove(nozzle_temp_observer_);
        nozzle_temp_observer_ = nullptr;
    }
    if (nozzle_target_observer_) {
        lv_observer_remove(nozzle_target_observer_);
        nozzle_target_observer_ = nullptr;
    }
    if (bed_temp_observer_) {
        lv_observer_remove(bed_temp_observer_);
        bed_temp_observer_ = nullptr;
    }
    if (bed_target_observer_) {
        lv_observer_remove(bed_target_observer_);
        bed_target_observer_ = nullptr;
    }
}

// Move constructor
TempControlPanel::TempControlPanel(TempControlPanel&& other) noexcept
    : printer_state_(other.printer_state_),
      api_(other.api_),
      nozzle_temp_observer_(other.nozzle_temp_observer_),
      nozzle_target_observer_(other.nozzle_target_observer_),
      bed_temp_observer_(other.bed_temp_observer_),
      bed_target_observer_(other.bed_target_observer_),
      nozzle_current_(other.nozzle_current_),
      nozzle_target_(other.nozzle_target_),
      bed_current_(other.bed_current_),
      bed_target_(other.bed_target_),
      nozzle_pending_(other.nozzle_pending_),
      bed_pending_(other.bed_pending_),
      nozzle_min_temp_(other.nozzle_min_temp_),
      nozzle_max_temp_(other.nozzle_max_temp_),
      bed_min_temp_(other.bed_min_temp_),
      bed_max_temp_(other.bed_max_temp_),
      nozzle_panel_(other.nozzle_panel_),
      bed_panel_(other.bed_panel_),
      nozzle_graph_(other.nozzle_graph_),
      bed_graph_(other.bed_graph_),
      nozzle_series_id_(other.nozzle_series_id_),
      bed_series_id_(other.bed_series_id_),
      nozzle_config_(other.nozzle_config_),
      bed_config_(other.bed_config_),
      subjects_initialized_(other.subjects_initialized_) {

    // Null out source's observers so destructor doesn't double-free
    other.nozzle_temp_observer_ = nullptr;
    other.nozzle_target_observer_ = nullptr;
    other.bed_temp_observer_ = nullptr;
    other.bed_target_observer_ = nullptr;

    // Copy string buffers
    nozzle_current_buf_ = other.nozzle_current_buf_;
    nozzle_target_buf_ = other.nozzle_target_buf_;
    bed_current_buf_ = other.bed_current_buf_;
    bed_target_buf_ = other.bed_target_buf_;
    nozzle_display_buf_ = other.nozzle_display_buf_;
    bed_display_buf_ = other.bed_display_buf_;
}

// Move assignment
TempControlPanel& TempControlPanel::operator=(TempControlPanel&& other) noexcept {
    if (this != &other) {
        // Clean up our observers first
        if (nozzle_temp_observer_) lv_observer_remove(nozzle_temp_observer_);
        if (nozzle_target_observer_) lv_observer_remove(nozzle_target_observer_);
        if (bed_temp_observer_) lv_observer_remove(bed_temp_observer_);
        if (bed_target_observer_) lv_observer_remove(bed_target_observer_);

        // Move everything
        api_ = other.api_;
        nozzle_temp_observer_ = other.nozzle_temp_observer_;
        nozzle_target_observer_ = other.nozzle_target_observer_;
        bed_temp_observer_ = other.bed_temp_observer_;
        bed_target_observer_ = other.bed_target_observer_;
        nozzle_current_ = other.nozzle_current_;
        nozzle_target_ = other.nozzle_target_;
        bed_current_ = other.bed_current_;
        bed_target_ = other.bed_target_;
        nozzle_pending_ = other.nozzle_pending_;
        bed_pending_ = other.bed_pending_;
        nozzle_min_temp_ = other.nozzle_min_temp_;
        nozzle_max_temp_ = other.nozzle_max_temp_;
        bed_min_temp_ = other.bed_min_temp_;
        bed_max_temp_ = other.bed_max_temp_;
        nozzle_panel_ = other.nozzle_panel_;
        bed_panel_ = other.bed_panel_;
        nozzle_graph_ = other.nozzle_graph_;
        bed_graph_ = other.bed_graph_;
        nozzle_series_id_ = other.nozzle_series_id_;
        bed_series_id_ = other.bed_series_id_;
        nozzle_config_ = other.nozzle_config_;
        bed_config_ = other.bed_config_;
        subjects_initialized_ = other.subjects_initialized_;

        // Null out source's observers
        other.nozzle_temp_observer_ = nullptr;
        other.nozzle_target_observer_ = nullptr;
        other.bed_temp_observer_ = nullptr;
        other.bed_target_observer_ = nullptr;

        // Copy string buffers
        nozzle_current_buf_ = other.nozzle_current_buf_;
        nozzle_target_buf_ = other.nozzle_target_buf_;
        bed_current_buf_ = other.bed_current_buf_;
        bed_target_buf_ = other.bed_target_buf_;
        nozzle_display_buf_ = other.nozzle_display_buf_;
        bed_display_buf_ = other.bed_display_buf_;
    }
    return *this;
}

// ============================================================================
// OBSERVER CALLBACKS (static trampolines → instance methods)
// ============================================================================

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

// ============================================================================
// INSTANCE METHODS FOR TEMPERATURE CHANGES
// ============================================================================

void TempControlPanel::on_nozzle_temp_changed(int temp) {
    nozzle_current_ = temp;
    update_nozzle_display();

    // Push to graph if it exists
    if (nozzle_graph_ && nozzle_series_id_ >= 0) {
        ui_temp_graph_update_series(nozzle_graph_, nozzle_series_id_, static_cast<float>(temp));
        spdlog::trace("[TempPanel] Nozzle graph updated: {}°C", temp);
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

    // Push to graph if it exists
    if (bed_graph_ && bed_series_id_ >= 0) {
        ui_temp_graph_update_series(bed_graph_, bed_series_id_, static_cast<float>(temp));
        spdlog::trace("[TempPanel] Bed graph updated: {}°C", temp);
    }
}

void TempControlPanel::on_bed_target_changed(int target) {
    bed_target_ = target;
    update_bed_display();

    // Update target line on graph
    if (bed_graph_ && bed_series_id_ >= 0) {
        bool show_target = (target > 0);
        ui_temp_graph_set_series_target(bed_graph_, bed_series_id_,
                                        static_cast<float>(target), show_target);
        spdlog::trace("[TempPanel] Bed target line: {}°C (visible={})", target, show_target);
    }
}

// ============================================================================
// DISPLAY UPDATE HELPERS
// ============================================================================

void TempControlPanel::update_nozzle_display() {
    // Show pending value if user has selected but not confirmed yet
    // Otherwise show actual target from Moonraker
    int display_target = (nozzle_pending_ >= 0) ? nozzle_pending_ : nozzle_target_;

    if (nozzle_pending_ >= 0) {
        // Show pending with asterisk to indicate unsent
        if (nozzle_pending_ > 0) {
            snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(),
                     "%d / %d*", nozzle_current_, nozzle_pending_);
        } else {
            snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(),
                     "%d / --*", nozzle_current_);
        }
    } else if (display_target > 0) {
        snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(),
                 "%d / %d", nozzle_current_, display_target);
    } else {
        snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(),
                 "%d / --", nozzle_current_);
    }
    lv_subject_copy_string(&nozzle_display_subject_, nozzle_display_buf_.data());
}

void TempControlPanel::update_bed_display() {
    // Show pending value if user has selected but not confirmed yet
    // Otherwise show actual target from Moonraker
    int display_target = (bed_pending_ >= 0) ? bed_pending_ : bed_target_;

    if (bed_pending_ >= 0) {
        // Show pending with asterisk to indicate unsent
        if (bed_pending_ > 0) {
            snprintf(bed_display_buf_.data(), bed_display_buf_.size(),
                     "%d / %d*", bed_current_, bed_pending_);
        } else {
            snprintf(bed_display_buf_.data(), bed_display_buf_.size(),
                     "%d / --*", bed_current_);
        }
    } else if (display_target > 0) {
        snprintf(bed_display_buf_.data(), bed_display_buf_.size(),
                 "%d / %d", bed_current_, display_target);
    } else {
        snprintf(bed_display_buf_.data(), bed_display_buf_.size(),
                 "%d / --", bed_current_);
    }

    spdlog::debug("[TempPanel] Bed display: '{}' (pending={}, target={}, current={})",
                  bed_display_buf_.data(), bed_pending_, bed_target_, bed_current_);

    lv_subject_copy_string(&bed_display_subject_, bed_display_buf_.data());
}

// ============================================================================
// SUBJECT INITIALIZATION
// ============================================================================

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
    snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(),
             "%d / %d°C", nozzle_current_, nozzle_target_);
    snprintf(bed_display_buf_.data(), bed_display_buf_.size(),
             "%d / %d°C", bed_current_, bed_target_);

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

    subjects_initialized_ = true;
    spdlog::debug("[TempPanel] Subjects initialized: nozzle={}/{}°C, bed={}/{}°C",
                  nozzle_current_, nozzle_target_, bed_current_, bed_target_);
}

// ============================================================================
// GRAPH CREATION
// ============================================================================

ui_temp_graph_t* TempControlPanel::create_temp_graph(lv_obj_t* chart_area,
                                                     const heater_config_t* config,
                                                     int target_temp,
                                                     int* series_id_out) {
    if (!chart_area) return nullptr;

    ui_temp_graph_t* graph = ui_temp_graph_create(chart_area);
    if (!graph) return nullptr;

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
        ui_temp_graph_set_series_target(graph, series_id, static_cast<float>(target_temp), show_target);

        // Graph starts empty - real-time data comes from PrinterState observers
        spdlog::debug("[TempPanel] {} graph created (awaiting live data)", config->name);
    }

    return graph;
}

void TempControlPanel::create_y_axis_labels(lv_obj_t* container, const heater_config_t* config) {
    if (!container) return;

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

// ============================================================================
// EVENT HANDLERS (static trampolines)
// ============================================================================

void TempControlPanel::nozzle_confirm_cb(lv_event_t* e) {
    auto* self = static_cast<TempControlPanel*>(lv_event_get_user_data(e));
    if (!self) return;

    // Use pending value if set, otherwise use current target (fallback, shouldn't happen)
    int target = (self->nozzle_pending_ >= 0) ? self->nozzle_pending_ : self->nozzle_target_;

    spdlog::info("[TempPanel] Nozzle temperature confirmed: {}°C (pending={})",
                 target, self->nozzle_pending_);

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

    spdlog::info("[TempPanel] Bed temperature confirmed: {}°C (pending={}, api_={})",
                 target, self->bed_pending_, self->api_ ? "valid" : "NULL");

    // Clear pending BEFORE navigation (since we're about to send the command)
    self->bed_pending_ = -1;

    if (self->api_) {
        spdlog::info("[TempPanel] Calling api_->set_temperature(heater_bed, {})", target);
        self->api_->set_temperature(
            "heater_bed", static_cast<double>(target),
            [target]() {
                spdlog::info("[TempPanel] set_temperature SUCCESS for bed: {}°C", target);
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
    if (!data || !data->panel) return;

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
    if (!data || !data->panel) return;

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
    if (!data || !data->panel) return;

    const heater_config_t& config = (data->type == HEATER_NOZZLE)
        ? data->panel->nozzle_config_
        : data->panel->bed_config_;

    int current_target = (data->type == HEATER_NOZZLE)
        ? data->panel->nozzle_target_
        : data->panel->bed_target_;

    ui_keypad_config_t keypad_config = {
        .initial_value = static_cast<float>(current_target),
        .min_value = config.keypad_range.min,
        .max_value = config.keypad_range.max,
        .title_label = (data->type == HEATER_NOZZLE) ? "Nozzle Temp" : "Heat Bed Temp",
        .unit_label = "°C",
        .allow_decimal = false,
        .allow_negative = false,
        .callback = keypad_value_cb,
        .user_data = data
    };

    ui_keypad_show(&keypad_config);
}

// ============================================================================
// BUTTON SETUP HELPERS
// ============================================================================

// Static storage for callback data (needed because LVGL holds raw pointers)
// These persist for the lifetime of the application
static PresetCallbackData nozzle_preset_data[4];
static PresetCallbackData bed_preset_data[4];
static KeypadCallbackData nozzle_keypad_data;
static KeypadCallbackData bed_keypad_data;

void TempControlPanel::setup_preset_buttons(lv_obj_t* panel, heater_type_t type) {
    const char* preset_names[] = {"preset_off", "preset_pla", "preset_petg", "preset_abs"};
    const heater_config_t& config = (type == HEATER_NOZZLE) ? nozzle_config_ : bed_config_;
    PresetCallbackData* preset_data = (type == HEATER_NOZZLE) ? nozzle_preset_data : bed_preset_data;

    int presets[] = {config.presets.off, config.presets.pla, config.presets.petg, config.presets.abs};

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
        spdlog::debug("[TempPanel] {} confirm button wired", type == HEATER_NOZZLE ? "Nozzle" : "Bed");
    }
}

// ============================================================================
// PANEL SETUP
// ============================================================================

void TempControlPanel::setup_nozzle_panel(lv_obj_t* panel, lv_obj_t* parent_screen) {
    nozzle_panel_ = panel;

    // Read current values from PrinterState (observers only fire on changes, not initial state)
    nozzle_current_ = lv_subject_get_int(printer_state_.get_extruder_temp_subject());
    nozzle_target_ = lv_subject_get_int(printer_state_.get_extruder_target_subject());
    spdlog::info("[TempPanel] Nozzle initial state from PrinterState: current={}°C, target={}°C",
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
        const char* color_str = lv_xml_get_const(scope, use_dark_mode
            ? "temp_graph_nozzle_dark" : "temp_graph_nozzle_light");
        if (color_str) {
            nozzle_config_.color = ui_theme_parse_color(color_str);
            spdlog::debug("[TempPanel] Nozzle graph color: {} ({})",
                          color_str, use_dark_mode ? "dark" : "light");
        }
    }

    spdlog::info("[TempPanel] Setting up nozzle panel...");

    // Create Y-axis labels
    lv_obj_t* y_axis_labels = lv_obj_find_by_name(overlay_content, "y_axis_labels");
    if (y_axis_labels) {
        create_y_axis_labels(y_axis_labels, &nozzle_config_);
    }

    // Create temperature graph
    lv_obj_t* chart_area = lv_obj_find_by_name(overlay_content, "chart_area");
    if (chart_area) {
        nozzle_graph_ = create_temp_graph(chart_area, &nozzle_config_,
                                          nozzle_target_, &nozzle_series_id_);
    }

    // Wire up confirm button
    lv_obj_t* header = lv_obj_find_by_name(panel, "overlay_header");
    if (header) {
        setup_confirm_button(header, HEATER_NOZZLE);
    }

    // Wire up preset and custom buttons
    setup_preset_buttons(overlay_content, HEATER_NOZZLE);
    setup_custom_button(overlay_content, HEATER_NOZZLE);

    spdlog::info("[TempPanel] Nozzle panel setup complete!");
}

void TempControlPanel::setup_bed_panel(lv_obj_t* panel, lv_obj_t* parent_screen) {
    bed_panel_ = panel;

    // Read current values from PrinterState (observers only fire on changes, not initial state)
    bed_current_ = lv_subject_get_int(printer_state_.get_bed_temp_subject());
    bed_target_ = lv_subject_get_int(printer_state_.get_bed_target_subject());
    spdlog::info("[TempPanel] Bed initial state from PrinterState: current={}°C, target={}°C",
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
        const char* color_str = lv_xml_get_const(scope, use_dark_mode
            ? "temp_graph_bed_dark" : "temp_graph_bed_light");
        if (color_str) {
            bed_config_.color = ui_theme_parse_color(color_str);
            spdlog::debug("[TempPanel] Bed graph color: {} ({})",
                          color_str, use_dark_mode ? "dark" : "light");
        }
    }

    spdlog::info("[TempPanel] Setting up bed panel...");

    // Create Y-axis labels
    lv_obj_t* y_axis_labels = lv_obj_find_by_name(overlay_content, "y_axis_labels");
    if (y_axis_labels) {
        create_y_axis_labels(y_axis_labels, &bed_config_);
    }

    // Create temperature graph
    lv_obj_t* chart_area = lv_obj_find_by_name(overlay_content, "chart_area");
    if (chart_area) {
        bed_graph_ = create_temp_graph(chart_area, &bed_config_,
                                       bed_target_, &bed_series_id_);
    }

    // Wire up confirm button
    lv_obj_t* header = lv_obj_find_by_name(panel, "overlay_header");
    if (header) {
        setup_confirm_button(header, HEATER_BED);
    }

    // Wire up preset and custom buttons
    setup_preset_buttons(overlay_content, HEATER_BED);
    setup_custom_button(overlay_content, HEATER_BED);

    spdlog::info("[TempPanel] Bed panel setup complete!");
}

// ============================================================================
// PUBLIC API
// ============================================================================

void TempControlPanel::set_nozzle(int current, int target) {
    UITemperatureUtils::validate_and_clamp_pair(current, target,
        nozzle_min_temp_, nozzle_max_temp_, "TempPanel/Nozzle");

    nozzle_current_ = current;
    nozzle_target_ = target;
    update_nozzle_display();
}

void TempControlPanel::set_bed(int current, int target) {
    UITemperatureUtils::validate_and_clamp_pair(current, target,
        bed_min_temp_, bed_max_temp_, "TempPanel/Bed");

    bed_current_ = current;
    bed_target_ = target;
    update_bed_display();
}

void TempControlPanel::set_nozzle_limits(int min_temp, int max_temp) {
    nozzle_min_temp_ = min_temp;
    nozzle_max_temp_ = max_temp;
    spdlog::info("[TempPanel] Nozzle limits updated: {}-{}°C", min_temp, max_temp);
}

void TempControlPanel::set_bed_limits(int min_temp, int max_temp) {
    bed_min_temp_ = min_temp;
    bed_max_temp_ = max_temp;
    spdlog::info("[TempPanel] Bed limits updated: {}-{}°C", min_temp, max_temp);
}
