// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_temp_control.h"

#include "ui_component_keypad.h"
#include "ui_error_reporting.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_subject_registry.h"
#include "ui_temp_graph_scaling.h"
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
                      .color = ui_theme_get_color("heating_color"),
                      .temp_range_max = 320.0f,
                      .y_axis_increment = 80,
                      .presets = {0, 210, 240, 250},
                      .keypad_range = {0.0f, 350.0f}};

    bed_config_ = {.type = HEATER_BED,
                   .name = "Bed",
                   .title = "Heatbed Temperature",
                   .color = ui_theme_get_color("cooling_color"),
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
    nozzle_status_buf_.fill('\0');
    bed_status_buf_.fill('\0');

    // Subscribe to PrinterState temperature subjects (ObserverGuard handles cleanup)
    nozzle_temp_observer_ =
        ObserverGuard(printer_state_.get_extruder_temp_subject(), nozzle_temp_observer_cb, this);
    nozzle_target_observer_ = ObserverGuard(printer_state_.get_extruder_target_subject(),
                                            nozzle_target_observer_cb, this);
    bed_temp_observer_ =
        ObserverGuard(printer_state_.get_bed_temp_subject(), bed_temp_observer_cb, this);
    bed_target_observer_ =
        ObserverGuard(printer_state_.get_bed_target_subject(), bed_target_observer_cb, this);

    // Register XML event callbacks in constructor (BEFORE any lv_xml_create calls)
    // These are global registrations that must exist when XML is parsed
    lv_xml_register_event_cb(nullptr, "on_nozzle_confirm_clicked", on_nozzle_confirm_clicked);
    lv_xml_register_event_cb(nullptr, "on_nozzle_preset_off_clicked", on_nozzle_preset_off_clicked);
    lv_xml_register_event_cb(nullptr, "on_nozzle_preset_pla_clicked", on_nozzle_preset_pla_clicked);
    lv_xml_register_event_cb(nullptr, "on_nozzle_preset_petg_clicked",
                             on_nozzle_preset_petg_clicked);
    lv_xml_register_event_cb(nullptr, "on_nozzle_preset_abs_clicked", on_nozzle_preset_abs_clicked);
    lv_xml_register_event_cb(nullptr, "on_nozzle_custom_clicked", on_nozzle_custom_clicked);
    lv_xml_register_event_cb(nullptr, "on_bed_confirm_clicked", on_bed_confirm_clicked);
    lv_xml_register_event_cb(nullptr, "on_bed_preset_off_clicked", on_bed_preset_off_clicked);
    lv_xml_register_event_cb(nullptr, "on_bed_preset_pla_clicked", on_bed_preset_pla_clicked);
    lv_xml_register_event_cb(nullptr, "on_bed_preset_petg_clicked", on_bed_preset_petg_clicked);
    lv_xml_register_event_cb(nullptr, "on_bed_preset_abs_clicked", on_bed_preset_abs_clicked);
    lv_xml_register_event_cb(nullptr, "on_bed_custom_clicked", on_bed_custom_clicked);

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

void TempControlPanel::on_nozzle_temp_changed(int temp_centi) {
    // Filter garbage data at the source: skip invalid temperature readings
    // Valid nozzle temps: -10°C to 400°C (centidegrees: -100 to 4000)
    // This prevents corrupt/uninitialized data from polluting the history buffer
    if (temp_centi <= 0 || temp_centi > 4000) {
        return; // Discard garbage/invalid temperature reading
    }

    nozzle_current_ = temp_centi;
    update_nozzle_display();
    update_nozzle_status(); // Update status text and heating icon state

    // Always store in history buffer (even at 4Hz) for later replay with downsampling
    // This ensures we capture ALL data from app start
    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    int write_idx = nozzle_history_count_ % TEMP_HISTORY_SIZE;
    nozzle_history_[write_idx] = {temp_centi, now_ms};
    nozzle_history_count_++;

    // Guard: don't update live graph until subjects initialized
    if (!subjects_initialized_) {
        return;
    }

    // Throttle live graph updates to 1 Hz (graph has 1200 points for 20 minutes at 1 sample/sec)
    // Moonraker sends updates at ~4Hz, so skip updates that are too close together
    if (now_ms - nozzle_last_graph_update_ms_ < GRAPH_SAMPLE_INTERVAL_MS) {
        return; // Skip this update - too soon since last graph point
    }
    nozzle_last_graph_update_ms_ = now_ms;

    // Push to graph if it exists (convert centidegrees to degrees with 0.1°C precision)
    // X-axis labels are rendered by the graph widget using the timestamp
    float temp_deg = static_cast<float>(temp_centi) / 10.0f;

    if (nozzle_graph_ && nozzle_series_id_ >= 0) {
        ui_temp_graph_update_series_with_time(nozzle_graph_, nozzle_series_id_, temp_deg, now_ms);
        spdlog::trace("[TempPanel] Nozzle graph updated: {:.1f}°C", temp_deg);
    }

    // Also update mini combined graph (filament panel)
    if (mini_graph_ && mini_nozzle_series_id_ >= 0) {
        // Dynamic Y-axis scaling using extracted helper
        float bed_deg = static_cast<float>(bed_current_) / 10.0f;
        float new_y_max = calculate_mini_graph_y_max(mini_graph_y_max_, temp_deg, bed_deg);

        if (new_y_max != mini_graph_y_max_) {
            spdlog::debug("[TempPanel] Mini graph Y-axis {} to {}°C",
                          new_y_max > mini_graph_y_max_ ? "expanded" : "shrunk", new_y_max);
            mini_graph_y_max_ = new_y_max;
            ui_temp_graph_set_temp_range(mini_graph_, 0.0f, mini_graph_y_max_);
        }

        ui_temp_graph_update_series_with_time(mini_graph_, mini_nozzle_series_id_, temp_deg,
                                              now_ms);
    }
}

void TempControlPanel::on_nozzle_target_changed(int target_centi) {
    nozzle_target_ = target_centi;
    update_nozzle_display();
    update_nozzle_status(); // Update status text and heating icon state

    // Update target line on graph (convert centidegrees to degrees)
    float target_deg = static_cast<float>(target_centi) / 10.0f;
    bool show_target = (target_centi > 0);

    if (nozzle_graph_ && nozzle_series_id_ >= 0) {
        ui_temp_graph_set_series_target(nozzle_graph_, nozzle_series_id_, target_deg, show_target);
        spdlog::trace("[TempPanel] Nozzle target line: {:.1f}°C (visible={})", target_deg,
                      show_target);
    }

    // Also update mini combined graph target line
    if (mini_graph_ && mini_nozzle_series_id_ >= 0) {
        ui_temp_graph_set_series_target(mini_graph_, mini_nozzle_series_id_, target_deg,
                                        show_target);
    }
}

void TempControlPanel::on_bed_temp_changed(int temp_centi) {
    // Filter garbage data at the source: skip invalid temperature readings
    // Valid bed temps: -10°C to 200°C (centidegrees: -100 to 2000)
    // This prevents corrupt/uninitialized data from polluting the history buffer
    if (temp_centi <= 0 || temp_centi > 2000) {
        return; // Discard garbage/invalid temperature reading
    }

    bed_current_ = temp_centi;
    update_bed_display();
    update_bed_status(); // Update status text and heating icon state

    // Always store in history buffer (even at 4Hz) for later replay with downsampling
    // This ensures we capture ALL data from app start
    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    int write_idx = bed_history_count_ % TEMP_HISTORY_SIZE;
    bed_history_[write_idx] = {temp_centi, now_ms};
    bed_history_count_++;

    // Guard: don't update live graph until subjects initialized
    if (!subjects_initialized_) {
        return;
    }

    // Throttle live graph updates to 1 Hz (graph has 1200 points for 20 minutes at 1 sample/sec)
    // Moonraker sends updates at ~4Hz, so skip updates that are too close together
    if (now_ms - bed_last_graph_update_ms_ < GRAPH_SAMPLE_INTERVAL_MS) {
        return; // Skip this update - too soon since last graph point
    }
    bed_last_graph_update_ms_ = now_ms;

    // Push to graph if it exists (convert centidegrees to degrees with 0.1°C precision)
    // X-axis labels are rendered by the graph widget using the timestamp
    float temp_deg = static_cast<float>(temp_centi) / 10.0f;

    if (bed_graph_ && bed_series_id_ >= 0) {
        ui_temp_graph_update_series_with_time(bed_graph_, bed_series_id_, temp_deg, now_ms);
        spdlog::trace("[TempPanel] Bed graph updated: {:.1f}°C", temp_deg);
    }

    // Also update mini combined graph (filament panel)
    if (mini_graph_ && mini_bed_series_id_ >= 0) {
        ui_temp_graph_update_series_with_time(mini_graph_, mini_bed_series_id_, temp_deg, now_ms);
    }
}

void TempControlPanel::on_bed_target_changed(int target_centi) {
    bed_target_ = target_centi;
    update_bed_display();
    update_bed_status(); // Update status text and heating icon state

    // Update target line on graph (convert centidegrees to degrees)
    float target_deg = static_cast<float>(target_centi) / 10.0f;
    bool show_target = (target_centi > 0);

    if (bed_graph_ && bed_series_id_ >= 0) {
        ui_temp_graph_set_series_target(bed_graph_, bed_series_id_, target_deg, show_target);
        spdlog::trace("[TempPanel] Bed target line: {:.1f}°C (visible={})", target_deg,
                      show_target);
    }

    // Also update mini combined graph target line
    if (mini_graph_ && mini_bed_series_id_ >= 0) {
        ui_temp_graph_set_series_target(mini_graph_, mini_bed_series_id_, target_deg, show_target);
    }
}

void TempControlPanel::update_nozzle_display() {
    // Guard: don't update subject if not initialized yet (observer fires during construction)
    if (!subjects_initialized_) {
        return;
    }

    // Convert from centidegrees to degrees for display
    // nozzle_current_ and nozzle_target_ are stored as centidegrees (×10)
    // nozzle_pending_ is in degrees (user-facing value from keypad/presets)
    int current_deg = nozzle_current_ / 10;
    int target_deg = nozzle_target_ / 10;

    // Show pending value if user has selected but not confirmed yet
    // Otherwise show actual target from Moonraker
    int display_target = (nozzle_pending_ >= 0) ? nozzle_pending_ : target_deg;

    if (nozzle_pending_ >= 0) {
        // Show pending with asterisk to indicate unsent
        if (nozzle_pending_ > 0) {
            snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(), "%d / %d*",
                     current_deg, nozzle_pending_);
        } else {
            snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(), "%d / --*",
                     current_deg);
        }
    } else if (display_target > 0) {
        snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(), "%d / %d", current_deg,
                 display_target);
    } else {
        snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(), "%d / --", current_deg);
    }
    lv_subject_copy_string(&nozzle_display_subject_, nozzle_display_buf_.data());
}

void TempControlPanel::update_bed_display() {
    // Guard: don't update subject if not initialized yet (observer fires during construction)
    if (!subjects_initialized_) {
        return;
    }

    // Convert from centidegrees to degrees for display
    // bed_current_ and bed_target_ are stored as centidegrees (×10)
    // bed_pending_ is in degrees (user-facing value from keypad/presets)
    int current_deg = bed_current_ / 10;
    int target_deg = bed_target_ / 10;

    // Show pending value if user has selected but not confirmed yet
    // Otherwise show actual target from Moonraker
    int display_target = (bed_pending_ >= 0) ? bed_pending_ : target_deg;

    if (bed_pending_ >= 0) {
        // Show pending with asterisk to indicate unsent
        if (bed_pending_ > 0) {
            snprintf(bed_display_buf_.data(), bed_display_buf_.size(), "%d / %d*", current_deg,
                     bed_pending_);
        } else {
            snprintf(bed_display_buf_.data(), bed_display_buf_.size(), "%d / --*", current_deg);
        }
    } else if (display_target > 0) {
        snprintf(bed_display_buf_.data(), bed_display_buf_.size(), "%d / %d", current_deg,
                 display_target);
    } else {
        snprintf(bed_display_buf_.data(), bed_display_buf_.size(), "%d / --", current_deg);
    }

    lv_subject_copy_string(&bed_display_subject_, bed_display_buf_.data());
}

void TempControlPanel::send_nozzle_temperature(int target) {
    spdlog::debug("[TempPanel] Sending nozzle temperature: {}°C", target);

    if (!api_) {
        spdlog::warn("[TempPanel] Cannot set nozzle temp: no API connection");
        return;
    }

    api_->set_temperature(
        "extruder", static_cast<double>(target),
        []() {
            // No toast on success - immediate visual feedback is sufficient
        },
        [](const MoonrakerError& error) {
            NOTIFY_ERROR("Failed to set nozzle temp: {}", error.user_message());
        });
}

void TempControlPanel::send_bed_temperature(int target) {
    spdlog::debug("[TempPanel] Sending bed temperature: {}°C", target);

    if (!api_) {
        spdlog::warn("[TempPanel] Cannot set bed temp: no API connection");
        return;
    }

    api_->set_temperature(
        "heater_bed", static_cast<double>(target),
        []() {
            // No toast on success - immediate visual feedback is sufficient
        },
        [](const MoonrakerError& error) {
            NOTIFY_ERROR("Failed to set bed temp: {}", error.user_message());
        });
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
    // NOTE: Use _N variant with explicit size for std::array buffers (sizeof(.data()) = pointer
    // size)
    UI_SUBJECT_INIT_AND_REGISTER_STRING_N(nozzle_current_subject_, nozzle_current_buf_.data(),
                                          nozzle_current_buf_.size(), nozzle_current_buf_.data(),
                                          "nozzle_current_temp");
    UI_SUBJECT_INIT_AND_REGISTER_STRING_N(nozzle_target_subject_, nozzle_target_buf_.data(),
                                          nozzle_target_buf_.size(), nozzle_target_buf_.data(),
                                          "nozzle_target_temp");
    UI_SUBJECT_INIT_AND_REGISTER_STRING_N(bed_current_subject_, bed_current_buf_.data(),
                                          bed_current_buf_.size(), bed_current_buf_.data(),
                                          "bed_current_temp");
    UI_SUBJECT_INIT_AND_REGISTER_STRING_N(bed_target_subject_, bed_target_buf_.data(),
                                          bed_target_buf_.size(), bed_target_buf_.data(),
                                          "bed_target_temp");
    UI_SUBJECT_INIT_AND_REGISTER_STRING_N(nozzle_display_subject_, nozzle_display_buf_.data(),
                                          nozzle_display_buf_.size(), nozzle_display_buf_.data(),
                                          "nozzle_temp_display");
    UI_SUBJECT_INIT_AND_REGISTER_STRING_N(bed_display_subject_, bed_display_buf_.data(),
                                          bed_display_buf_.size(), bed_display_buf_.data(),
                                          "bed_temp_display");

    // Status text subjects (for reactive status messages like "Heating...", "Cooling down", "Idle")
    snprintf(nozzle_status_buf_.data(), nozzle_status_buf_.size(), "Idle");
    snprintf(bed_status_buf_.data(), bed_status_buf_.size(), "Idle");
    UI_SUBJECT_INIT_AND_REGISTER_STRING_N(nozzle_status_subject_, nozzle_status_buf_.data(),
                                          nozzle_status_buf_.size(), nozzle_status_buf_.data(),
                                          "nozzle_status");
    UI_SUBJECT_INIT_AND_REGISTER_STRING_N(bed_status_subject_, bed_status_buf_.data(),
                                          bed_status_buf_.size(), bed_status_buf_.data(),
                                          "bed_status");

    // Heating state subjects (0=off, 1=on) for reactive icon visibility in XML
    UI_SUBJECT_INIT_AND_REGISTER_INT(nozzle_heating_subject_, 0, "nozzle_heating");
    UI_SUBJECT_INIT_AND_REGISTER_INT(bed_heating_subject_, 0, "bed_heating");

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

void TempControlPanel::on_nozzle_confirm_clicked(lv_event_t* e) {
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
    }

    ui_nav_go_back();
}

void TempControlPanel::on_bed_confirm_clicked(lv_event_t* e) {
    auto* self = static_cast<TempControlPanel*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("[TempPanel] on_bed_confirm_clicked: self is null!");
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
    }

    ui_nav_go_back();
}

// Nozzle preset button callbacks - immediately send temperature to printer
// NOTE: XML event callbacks use current_target (where callback is attached), not target (where
// click originated)
void TempControlPanel::on_nozzle_preset_off_clicked(lv_event_t* e) {
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* self = static_cast<TempControlPanel*>(lv_obj_get_user_data(btn));
    if (!self)
        return;
    int target = self->nozzle_config_.presets.off;
    spdlog::debug("[TempPanel] Nozzle preset Off clicked: setting to {}°C", target);
    self->send_nozzle_temperature(target);
}

void TempControlPanel::on_nozzle_preset_pla_clicked(lv_event_t* e) {
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* self = static_cast<TempControlPanel*>(lv_obj_get_user_data(btn));
    if (!self)
        return;
    int target = self->nozzle_config_.presets.pla;
    spdlog::debug("[TempPanel] Nozzle preset PLA clicked: setting to {}°C", target);
    self->send_nozzle_temperature(target);
}

void TempControlPanel::on_nozzle_preset_petg_clicked(lv_event_t* e) {
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* self = static_cast<TempControlPanel*>(lv_obj_get_user_data(btn));
    if (!self)
        return;
    int target = self->nozzle_config_.presets.petg;
    spdlog::debug("[TempPanel] Nozzle preset PETG clicked: setting to {}°C", target);
    self->send_nozzle_temperature(target);
}

void TempControlPanel::on_nozzle_preset_abs_clicked(lv_event_t* e) {
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* self = static_cast<TempControlPanel*>(lv_obj_get_user_data(btn));
    if (!self)
        return;
    int target = self->nozzle_config_.presets.abs;
    spdlog::debug("[TempPanel] Nozzle preset ABS clicked: setting to {}°C", target);
    self->send_nozzle_temperature(target);
}

// Bed preset button callbacks (send immediately like nozzle presets)
void TempControlPanel::on_bed_preset_off_clicked(lv_event_t* e) {
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* self = static_cast<TempControlPanel*>(lv_obj_get_user_data(btn));
    if (!self)
        return;
    int target = self->bed_config_.presets.off;
    spdlog::debug("[TempPanel] Bed preset OFF clicked: setting to {}°C", target);
    self->send_bed_temperature(target);
}

void TempControlPanel::on_bed_preset_pla_clicked(lv_event_t* e) {
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* self = static_cast<TempControlPanel*>(lv_obj_get_user_data(btn));
    if (!self)
        return;
    int target = self->bed_config_.presets.pla;
    spdlog::debug("[TempPanel] Bed preset PLA clicked: setting to {}°C", target);
    self->send_bed_temperature(target);
}

void TempControlPanel::on_bed_preset_petg_clicked(lv_event_t* e) {
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* self = static_cast<TempControlPanel*>(lv_obj_get_user_data(btn));
    if (!self)
        return;
    int target = self->bed_config_.presets.petg;
    spdlog::debug("[TempPanel] Bed preset PETG clicked: setting to {}°C", target);
    self->send_bed_temperature(target);
}

void TempControlPanel::on_bed_preset_abs_clicked(lv_event_t* e) {
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* self = static_cast<TempControlPanel*>(lv_obj_get_user_data(btn));
    if (!self)
        return;
    int target = self->bed_config_.presets.abs;
    spdlog::debug("[TempPanel] Bed preset ABS clicked: setting to {}°C", target);
    self->send_bed_temperature(target);
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
        spdlog::debug("[TempPanel] Nozzle custom temperature: {}°C via keypad", temp);
        data->panel->send_nozzle_temperature(temp);
    } else {
        spdlog::debug("[TempPanel] Bed custom temperature: {}°C via keypad", temp);
        data->panel->send_bed_temperature(temp);
    }
}

// Static storage for keypad callback data (needed because LVGL holds raw pointers)
static KeypadCallbackData nozzle_keypad_data;
static KeypadCallbackData bed_keypad_data;

void TempControlPanel::on_nozzle_custom_clicked(lv_event_t* e) {
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* self = static_cast<TempControlPanel*>(lv_obj_get_user_data(btn));
    if (!self)
        return;

    nozzle_keypad_data = {self, HEATER_NOZZLE};

    ui_keypad_config_t keypad_config = {.initial_value = static_cast<float>(self->nozzle_target_),
                                        .min_value = self->nozzle_config_.keypad_range.min,
                                        .max_value = self->nozzle_config_.keypad_range.max,
                                        .title_label = "Nozzle Temp",
                                        .unit_label = "°C",
                                        .allow_decimal = false,
                                        .allow_negative = false,
                                        .callback = keypad_value_cb,
                                        .user_data = &nozzle_keypad_data};

    ui_keypad_show(&keypad_config);
}

void TempControlPanel::on_bed_custom_clicked(lv_event_t* e) {
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* self = static_cast<TempControlPanel*>(lv_obj_get_user_data(btn));
    if (!self)
        return;

    bed_keypad_data = {self, HEATER_BED};

    ui_keypad_config_t keypad_config = {.initial_value = static_cast<float>(self->bed_target_),
                                        .min_value = self->bed_config_.keypad_range.min,
                                        .max_value = self->bed_config_.keypad_range.max,
                                        .title_label = "Heat Bed Temp",
                                        .unit_label = "°C",
                                        .allow_decimal = false,
                                        .allow_negative = false,
                                        .callback = keypad_value_cb,
                                        .user_data = &bed_keypad_data};

    ui_keypad_show(&keypad_config);
}

void TempControlPanel::setup_nozzle_panel(lv_obj_t* panel, lv_obj_t* parent_screen) {
    nozzle_panel_ = panel;

    // NOTE: Event callbacks are registered in constructor (before any lv_xml_create calls)

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

    // Set user_data on all buttons for event callbacks
    lv_obj_t* overlay_header = lv_obj_find_by_name(panel, "overlay_header");
    if (overlay_header) {
        lv_obj_t* action_button = lv_obj_find_by_name(overlay_header, "action_button");
        if (action_button) {
            lv_obj_set_user_data(action_button, this);
        }
    }

    lv_obj_t* preset_off = lv_obj_find_by_name(overlay_content, "preset_off");
    lv_obj_t* preset_pla = lv_obj_find_by_name(overlay_content, "preset_pla");
    lv_obj_t* preset_petg = lv_obj_find_by_name(overlay_content, "preset_petg");
    lv_obj_t* preset_abs = lv_obj_find_by_name(overlay_content, "preset_abs");
    lv_obj_t* btn_custom = lv_obj_find_by_name(overlay_content, "btn_custom");

    if (preset_off)
        lv_obj_set_user_data(preset_off, this);
    if (preset_pla)
        lv_obj_set_user_data(preset_pla, this);
    if (preset_petg)
        lv_obj_set_user_data(preset_petg, this);
    if (preset_abs)
        lv_obj_set_user_data(preset_abs, this);
    if (btn_custom)
        lv_obj_set_user_data(btn_custom, this);

    // Load theme-aware graph color
    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("nozzle_temp_panel");
    if (scope) {
        bool use_dark_mode = ui_theme_is_dark_mode();
        const char* color_str = lv_xml_get_const(scope, use_dark_mode ? "temp_graph_nozzle_dark"
                                                                      : "temp_graph_nozzle_light");
        if (color_str) {
            nozzle_config_.color = ui_theme_parse_hex_color(color_str);
            spdlog::debug("[TempPanel] Nozzle graph color: {} ({})", color_str,
                          use_dark_mode ? "dark" : "light");
        }
    }

    spdlog::debug("[TempPanel] Setting up nozzle panel...");

    // Create temperature graph
    lv_obj_t* chart_area = lv_obj_find_by_name(overlay_content, "chart_area");
    if (chart_area) {
        nozzle_graph_ =
            create_temp_graph(chart_area, &nozzle_config_, nozzle_target_, &nozzle_series_id_);

        // Configure Y-axis labels (rendered directly on graph canvas)
        if (nozzle_graph_) {
            ui_temp_graph_set_y_axis(nozzle_graph_,
                                     static_cast<float>(nozzle_config_.y_axis_increment), true);
        }
    }

    // Replay buffered temperature history to graph (shows data from app start)
    replay_nozzle_history_to_graph();

    // Attach heating icon animator (simplified to single icon, color controlled programmatically)
    lv_obj_t* heater_icon = lv_obj_find_by_name(panel, "heater_icon");
    if (heater_icon) {
        nozzle_animator_.attach(heater_icon);
        // Initialize with current state
        nozzle_animator_.update(nozzle_current_, nozzle_target_);
        spdlog::debug("[TempPanel] Nozzle heating animator attached");
    }

    spdlog::debug("[TempPanel] Nozzle panel setup complete!");
}

void TempControlPanel::setup_bed_panel(lv_obj_t* panel, lv_obj_t* parent_screen) {
    bed_panel_ = panel;

    // NOTE: Event callbacks are registered in constructor (before any lv_xml_create calls)

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

    // Set user_data on all buttons for event callbacks
    lv_obj_t* overlay_header = lv_obj_find_by_name(panel, "overlay_header");
    if (overlay_header) {
        lv_obj_t* action_button = lv_obj_find_by_name(overlay_header, "action_button");
        if (action_button) {
            lv_obj_set_user_data(action_button, this);
        }
    }

    lv_obj_t* preset_off = lv_obj_find_by_name(overlay_content, "preset_off");
    lv_obj_t* preset_pla = lv_obj_find_by_name(overlay_content, "preset_pla");
    lv_obj_t* preset_petg = lv_obj_find_by_name(overlay_content, "preset_petg");
    lv_obj_t* preset_abs = lv_obj_find_by_name(overlay_content, "preset_abs");
    lv_obj_t* btn_custom = lv_obj_find_by_name(overlay_content, "btn_custom");

    if (preset_off)
        lv_obj_set_user_data(preset_off, this);
    if (preset_pla)
        lv_obj_set_user_data(preset_pla, this);
    if (preset_petg)
        lv_obj_set_user_data(preset_petg, this);
    if (preset_abs)
        lv_obj_set_user_data(preset_abs, this);
    if (btn_custom)
        lv_obj_set_user_data(btn_custom, this);

    // Load theme-aware graph color
    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("bed_temp_panel");
    if (scope) {
        bool use_dark_mode = ui_theme_is_dark_mode();
        const char* color_str =
            lv_xml_get_const(scope, use_dark_mode ? "temp_graph_bed_dark" : "temp_graph_bed_light");
        if (color_str) {
            bed_config_.color = ui_theme_parse_hex_color(color_str);
            spdlog::debug("[TempPanel] Bed graph color: {} ({})", color_str,
                          use_dark_mode ? "dark" : "light");
        }
    }

    spdlog::debug("[TempPanel] Setting up bed panel...");

    // Create temperature graph
    lv_obj_t* chart_area = lv_obj_find_by_name(overlay_content, "chart_area");
    if (chart_area) {
        bed_graph_ = create_temp_graph(chart_area, &bed_config_, bed_target_, &bed_series_id_);

        // Configure Y-axis labels (rendered directly on graph canvas)
        if (bed_graph_) {
            ui_temp_graph_set_y_axis(bed_graph_, static_cast<float>(bed_config_.y_axis_increment),
                                     true);
        }
    }

    // Replay buffered temperature history to graph (shows data from app start)
    replay_bed_history_to_graph();

    // Attach heating icon animator to the bed icon container
    // The bed uses composite icons (heat_wave + train_flatbed), we animate the container
    lv_obj_t* bed_icon = lv_obj_find_by_name(panel, "bed_icon");
    if (bed_icon) {
        bed_animator_.attach(bed_icon);
        // Initialize with current state
        bed_animator_.update(bed_current_, bed_target_);
        spdlog::debug("[TempPanel] Bed heating animator attached");
    }

    spdlog::debug("[TempPanel] Bed panel setup complete!");
}

void TempControlPanel::update_nozzle_status() {
    if (!subjects_initialized_) {
        return;
    }

    // Thresholds in centidegrees (×10) - values are stored as centidegrees
    constexpr int NOZZLE_COOLING_THRESHOLD_CENTI =
        400;                                 // 40°C - above this when off = "cooling down"
    constexpr int TEMP_TOLERANCE_CENTI = 20; // 2°C - within this of target = "at target"

    // Convert to degrees for display
    int current_deg = nozzle_current_ / 10;
    int target_deg = nozzle_target_ / 10;

    if (nozzle_target_ > 0 && nozzle_current_ < nozzle_target_ - TEMP_TOLERANCE_CENTI) {
        // Actively heating
        snprintf(nozzle_status_buf_.data(), nozzle_status_buf_.size(), "Heating to %d°C...",
                 target_deg);
    } else if (nozzle_target_ > 0 && nozzle_current_ >= nozzle_target_ - TEMP_TOLERANCE_CENTI) {
        // At target temperature
        snprintf(nozzle_status_buf_.data(), nozzle_status_buf_.size(), "At target temperature");
    } else if (nozzle_target_ == 0 && nozzle_current_ > NOZZLE_COOLING_THRESHOLD_CENTI) {
        // Cooling down (heater off but still hot)
        snprintf(nozzle_status_buf_.data(), nozzle_status_buf_.size(), "Cooling down (%d°C)",
                 current_deg);
    } else {
        // Idle (heater off and cool)
        snprintf(nozzle_status_buf_.data(), nozzle_status_buf_.size(), "Idle");
    }

    lv_subject_copy_string(&nozzle_status_subject_, nozzle_status_buf_.data());

    // Update heating state for reactive icon visibility (0=off, 1=on)
    int heating_state = (nozzle_target_ > 0) ? 1 : 0;
    lv_subject_set_int(&nozzle_heating_subject_, heating_state);

    // Update heating icon animator (gradient color + pulse animation, in centidegrees)
    nozzle_animator_.update(nozzle_current_, nozzle_target_);

    spdlog::trace("[TempPanel] Nozzle status: '{}' (heating={})", nozzle_status_buf_.data(),
                  heating_state);
}

void TempControlPanel::update_bed_status() {
    if (!subjects_initialized_) {
        return;
    }

    // Thresholds in centidegrees (×10) - values are stored as centidegrees
    constexpr int BED_COOLING_THRESHOLD_CENTI = 350; // 35°C - above this when off = "cooling down"
    constexpr int TEMP_TOLERANCE_CENTI = 20;         // 2°C - within this of target = "at target"

    // Convert to degrees for display
    int current_deg = bed_current_ / 10;
    int target_deg = bed_target_ / 10;

    if (bed_target_ > 0 && bed_current_ < bed_target_ - TEMP_TOLERANCE_CENTI) {
        // Actively heating
        snprintf(bed_status_buf_.data(), bed_status_buf_.size(), "Heating to %d°C...", target_deg);
    } else if (bed_target_ > 0 && bed_current_ >= bed_target_ - TEMP_TOLERANCE_CENTI) {
        // At target temperature
        snprintf(bed_status_buf_.data(), bed_status_buf_.size(), "At target temperature");
    } else if (bed_target_ == 0 && bed_current_ > BED_COOLING_THRESHOLD_CENTI) {
        // Cooling down (heater off but still hot)
        snprintf(bed_status_buf_.data(), bed_status_buf_.size(), "Cooling down (%d°C)",
                 current_deg);
    } else {
        // Idle (heater off and cool)
        snprintf(bed_status_buf_.data(), bed_status_buf_.size(), "Idle");
    }

    lv_subject_copy_string(&bed_status_subject_, bed_status_buf_.data());

    // Update heating state for reactive icon visibility (0=off, 1=on)
    int heating_state = (bed_target_ > 0) ? 1 : 0;
    lv_subject_set_int(&bed_heating_subject_, heating_state);

    // Update heating icon animator (gradient color + pulse animation, in centidegrees)
    bed_animator_.update(bed_current_, bed_target_);

    spdlog::trace("[TempPanel] Bed status: '{}' (heating={})", bed_status_buf_.data(),
                  heating_state);
}

void TempControlPanel::set_nozzle(int current, int target) {
    helix::ui::temperature::validate_and_clamp_pair(current, target, nozzle_min_temp_,
                                                    nozzle_max_temp_, "TempPanel/Nozzle");

    nozzle_current_ = current;
    nozzle_target_ = target;
    update_nozzle_display();
}

void TempControlPanel::set_bed(int current, int target) {
    helix::ui::temperature::validate_and_clamp_pair(current, target, bed_min_temp_, bed_max_temp_,
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

void TempControlPanel::replay_nozzle_history_to_graph() {
    if (!nozzle_graph_ || nozzle_series_id_ < 0 || nozzle_history_count_ == 0) {
        return;
    }

    // Determine how many samples are available (up to TEMP_HISTORY_SIZE)
    int samples_available = std::min(nozzle_history_count_, TEMP_HISTORY_SIZE);

    // Find the oldest sample index
    int start_idx;
    if (nozzle_history_count_ <= TEMP_HISTORY_SIZE) {
        // Buffer hasn't wrapped yet - start from 0
        start_idx = 0;
    } else {
        // Buffer has wrapped - oldest is at current write position
        start_idx = nozzle_history_count_ % TEMP_HISTORY_SIZE;
    }

    // Replay samples at 1Hz (downsample if data came in faster)
    // History may have been stored at 4Hz during startup, but graph expects 1Hz
    // Only graph samples that are at least GRAPH_SAMPLE_INTERVAL_MS apart
    int replayed = 0;
    int64_t last_graphed_time = 0;

    for (int i = 0; i < samples_available; i++) {
        int idx = (start_idx + i) % TEMP_HISTORY_SIZE;
        int temp_centi = nozzle_history_[idx].temp;
        int64_t sample_time = nozzle_history_[idx].timestamp_ms;

        if (temp_centi == 0) {
            continue; // Skip uninitialized/zero entries
        }

        // Downsample: only graph if enough time has passed since last point
        if (last_graphed_time > 0 && (sample_time - last_graphed_time) < GRAPH_SAMPLE_INTERVAL_MS) {
            continue; // Skip - too close to previous point
        }

        float temp_deg = static_cast<float>(temp_centi) / 10.0f;
        ui_temp_graph_update_series_with_time(nozzle_graph_, nozzle_series_id_, temp_deg,
                                              sample_time);
        last_graphed_time = sample_time;
        replayed++;
    }

    if (replayed > 0) {
        spdlog::info("[TempPanel] Replayed {} nozzle temp samples to graph (from {} available)",
                     replayed, samples_available);
    }
}

void TempControlPanel::replay_bed_history_to_graph() {
    if (!bed_graph_ || bed_series_id_ < 0 || bed_history_count_ == 0) {
        return;
    }

    // Determine how many samples are available (up to TEMP_HISTORY_SIZE)
    int samples_available = std::min(bed_history_count_, TEMP_HISTORY_SIZE);

    // Find the oldest sample index
    int start_idx;
    if (bed_history_count_ <= TEMP_HISTORY_SIZE) {
        // Buffer hasn't wrapped yet - start from 0
        start_idx = 0;
    } else {
        // Buffer has wrapped - oldest is at current write position
        start_idx = bed_history_count_ % TEMP_HISTORY_SIZE;
    }

    // Replay samples at 1Hz (downsample if data came in faster)
    // History may have been stored at 4Hz during startup, but graph expects 1Hz
    // Only graph samples that are at least GRAPH_SAMPLE_INTERVAL_MS apart
    int replayed = 0;
    int64_t last_graphed_time = 0;

    for (int i = 0; i < samples_available; i++) {
        int idx = (start_idx + i) % TEMP_HISTORY_SIZE;
        int temp_centi = bed_history_[idx].temp;
        int64_t sample_time = bed_history_[idx].timestamp_ms;

        if (temp_centi == 0) {
            continue; // Skip uninitialized/zero entries
        }

        // Downsample: only graph if enough time has passed since last point
        if (last_graphed_time > 0 && (sample_time - last_graphed_time) < GRAPH_SAMPLE_INTERVAL_MS) {
            continue; // Skip - too close to previous point
        }

        float temp_deg = static_cast<float>(temp_centi) / 10.0f;
        ui_temp_graph_update_series_with_time(bed_graph_, bed_series_id_, temp_deg, sample_time);
        last_graphed_time = sample_time;
        replayed++;
    }

    if (replayed > 0) {
        spdlog::info("[TempPanel] Replayed {} bed temp samples to graph (from {} available)",
                     replayed, samples_available);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Mini Combined Graph (for FilamentPanel)
// ─────────────────────────────────────────────────────────────────────────────

// 5-minute window at 1Hz = 300 points
static constexpr int MINI_GRAPH_POINTS = 300;

void TempControlPanel::setup_mini_combined_graph(lv_obj_t* container) {
    if (!container) {
        spdlog::warn("[TempPanel] setup_mini_combined_graph: null container");
        return;
    }

    // Create the graph widget
    mini_graph_ = ui_temp_graph_create(container);
    if (!mini_graph_) {
        spdlog::error("[TempPanel] Failed to create mini combined graph");
        return;
    }

    // Size it to fill the container
    lv_obj_t* chart = ui_temp_graph_get_chart(mini_graph_);
    lv_obj_set_size(chart, lv_pct(100), lv_pct(100));

    // Configure for combined view:
    // Use 0-150°C for better visibility of typical temps (room temp through bed range)
    // High nozzle temps (>150°C) will clip at top, but that's acceptable for this mini view
    // since the full temp panels show the complete range
    ui_temp_graph_set_temp_range(mini_graph_, 0.0f, 150.0f);
    ui_temp_graph_set_point_count(mini_graph_, MINI_GRAPH_POINTS);

    // Configure Y-axis with 50°C increments for readability
    ui_temp_graph_set_y_axis(mini_graph_, 50.0f, true);

    // Add bed series FIRST (renders underneath) - cyan/cooling color
    mini_bed_series_id_ = ui_temp_graph_add_series(mini_graph_, "Bed", bed_config_.color);
    if (mini_bed_series_id_ >= 0) {
        // Subtle gradient so it doesn't dominate
        ui_temp_graph_set_series_gradient(mini_graph_, mini_bed_series_id_, LV_OPA_0, LV_OPA_10);
    }

    // Add nozzle series SECOND (renders on top) - red/heating color
    mini_nozzle_series_id_ = ui_temp_graph_add_series(mini_graph_, "Nozzle", nozzle_config_.color);
    if (mini_nozzle_series_id_ >= 0) {
        // More visible gradient for the primary temp (filament loading)
        ui_temp_graph_set_series_gradient(mini_graph_, mini_nozzle_series_id_, LV_OPA_0, LV_OPA_20);
    }

    // Replay last 5 minutes of history to each series
    replay_history_to_mini_graph();

    // Set target lines if targets are set
    if (mini_nozzle_series_id_ >= 0 && nozzle_target_ > 0) {
        float target_deg = static_cast<float>(nozzle_target_) / 10.0f;
        ui_temp_graph_set_series_target(mini_graph_, mini_nozzle_series_id_, target_deg, true);
    }
    if (mini_bed_series_id_ >= 0 && bed_target_ > 0) {
        float target_deg = static_cast<float>(bed_target_) / 10.0f;
        ui_temp_graph_set_series_target(mini_graph_, mini_bed_series_id_, target_deg, true);
    }

    spdlog::info("[TempPanel] Mini combined graph created with {} point capacity",
                 MINI_GRAPH_POINTS);
}

void TempControlPanel::replay_history_to_mini_graph() {
    if (!mini_graph_) {
        return;
    }

    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    // Only replay last 5 minutes (300 seconds)
    int64_t cutoff_ms = now_ms - (MINI_GRAPH_POINTS * 1000);

    // Replay nozzle history
    if (mini_nozzle_series_id_ >= 0 && nozzle_history_count_ > 0) {
        int samples_available = std::min(nozzle_history_count_, TEMP_HISTORY_SIZE);
        int start_idx = (nozzle_history_count_ <= TEMP_HISTORY_SIZE)
                            ? 0
                            : (nozzle_history_count_ % TEMP_HISTORY_SIZE);

        int64_t last_graphed_time = 0;
        int replayed = 0;

        for (int i = 0; i < samples_available; i++) {
            int idx = (start_idx + i) % TEMP_HISTORY_SIZE;
            int temp_centi = nozzle_history_[idx].temp;
            int64_t sample_time = nozzle_history_[idx].timestamp_ms;

            if (temp_centi == 0 || sample_time < cutoff_ms) {
                continue;
            }

            if (last_graphed_time > 0 &&
                (sample_time - last_graphed_time) < GRAPH_SAMPLE_INTERVAL_MS) {
                continue;
            }

            float temp_deg = static_cast<float>(temp_centi) / 10.0f;
            ui_temp_graph_update_series_with_time(mini_graph_, mini_nozzle_series_id_, temp_deg,
                                                  sample_time);
            last_graphed_time = sample_time;
            replayed++;
        }

        if (replayed > 0) {
            spdlog::debug("[TempPanel] Mini graph: replayed {} nozzle samples", replayed);
        }
    }

    // Replay bed history
    if (mini_bed_series_id_ >= 0 && bed_history_count_ > 0) {
        int samples_available = std::min(bed_history_count_, TEMP_HISTORY_SIZE);
        int start_idx = (bed_history_count_ <= TEMP_HISTORY_SIZE)
                            ? 0
                            : (bed_history_count_ % TEMP_HISTORY_SIZE);

        int64_t last_graphed_time = 0;
        int replayed = 0;

        for (int i = 0; i < samples_available; i++) {
            int idx = (start_idx + i) % TEMP_HISTORY_SIZE;
            int temp_centi = bed_history_[idx].temp;
            int64_t sample_time = bed_history_[idx].timestamp_ms;

            if (temp_centi == 0 || sample_time < cutoff_ms) {
                continue;
            }

            if (last_graphed_time > 0 &&
                (sample_time - last_graphed_time) < GRAPH_SAMPLE_INTERVAL_MS) {
                continue;
            }

            float temp_deg = static_cast<float>(temp_centi) / 10.0f;
            ui_temp_graph_update_series_with_time(mini_graph_, mini_bed_series_id_, temp_deg,
                                                  sample_time);
            last_graphed_time = sample_time;
            replayed++;
        }

        if (replayed > 0) {
            spdlog::debug("[TempPanel] Mini graph: replayed {} bed samples", replayed);
        }
    }
}
