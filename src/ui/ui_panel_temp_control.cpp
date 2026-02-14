// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_temp_control.h"

#include "ui_component_keypad.h"
#include "ui_error_reporting.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_subject_registry.h"
#include "ui_temp_graph_scaling.h"
#include "ui_temperature_utils.h"
#include "ui_utils.h"

#include "app_constants.h"
#include "app_globals.h"
#include "filament_database.h"
#include "moonraker_api.h"
#include "observer_factory.h"
#include "printer_state.h"
#include "temperature_history_manager.h"
#include "theme_manager.h"
#include "tool_state.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <memory>

using helix::ui::observe_int_sync;
using helix::ui::temperature::centi_to_degrees_f;

TempControlPanel::TempControlPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : printer_state_(printer_state), api_(api),
      nozzle_min_temp_(AppConstants::Temperature::DEFAULT_MIN_TEMP),
      nozzle_max_temp_(AppConstants::Temperature::DEFAULT_NOZZLE_MAX),
      bed_min_temp_(AppConstants::Temperature::DEFAULT_MIN_TEMP),
      bed_max_temp_(AppConstants::Temperature::DEFAULT_BED_MAX) {
    // Get recommended temperatures from filament database
    auto pla_info = filament::find_material("PLA");
    auto petg_info = filament::find_material("PETG");
    auto abs_info = filament::find_material("ABS");

    // Nozzle presets: Off, PLA, PETG, ABS (using database midpoint recommendations)
    int nozzle_pla = pla_info ? pla_info->nozzle_recommended() : 210;
    int nozzle_petg = petg_info ? petg_info->nozzle_recommended() : 245;
    int nozzle_abs = abs_info ? abs_info->nozzle_recommended() : 255;

    // Bed presets: Off, PLA, PETG, ABS (using database recommendations)
    int bed_pla = pla_info ? pla_info->bed_temp : 60;
    int bed_petg = petg_info ? petg_info->bed_temp : 80;
    int bed_abs = abs_info ? abs_info->bed_temp : 100;

    nozzle_config_ = {.type = HEATER_NOZZLE,
                      .name = "Nozzle",
                      .title = "Nozzle Temperature",
                      .color = theme_manager_get_color("heating_color"),
                      .temp_range_max = 320.0f,
                      .y_axis_increment = 80,
                      .presets = {0, nozzle_pla, nozzle_petg, nozzle_abs},
                      .keypad_range = {0.0f, 350.0f}};

    bed_config_ = {.type = HEATER_BED,
                   .name = "Bed",
                   .title = "Heatbed Temperature",
                   .color = theme_manager_get_color("cooling_color"),
                   .temp_range_max = 140.0f,
                   .y_axis_increment = 35,
                   .presets = {0, bed_pla, bed_petg, bed_abs},
                   .keypad_range = {0.0f, 150.0f}};

    nozzle_current_buf_.fill('\0');
    nozzle_target_buf_.fill('\0');
    bed_current_buf_.fill('\0');
    bed_target_buf_.fill('\0');
    nozzle_display_buf_.fill('\0');
    bed_display_buf_.fill('\0');
    nozzle_status_buf_.fill('\0');
    bed_status_buf_.fill('\0');

    // Subscribe to temperature subjects with individual ObserverGuards.
    // Nozzle observers are separate so they can be rebound when switching
    // extruders in multi-extruder setups (bed observers stay constant).
    nozzle_temp_observer_ = observe_int_sync<TempControlPanel>(
        printer_state_.get_extruder_temp_subject(), this,
        [](TempControlPanel* self, int temp) { self->on_nozzle_temp_changed(temp); });
    nozzle_target_observer_ = observe_int_sync<TempControlPanel>(
        printer_state_.get_extruder_target_subject(), this,
        [](TempControlPanel* self, int target) { self->on_nozzle_target_changed(target); });
    bed_temp_observer_ = observe_int_sync<TempControlPanel>(
        printer_state_.get_bed_temp_subject(), this,
        [](TempControlPanel* self, int temp) { self->on_bed_temp_changed(temp); });
    bed_target_observer_ = observe_int_sync<TempControlPanel>(
        printer_state_.get_bed_target_subject(), this,
        [](TempControlPanel* self, int target) { self->on_bed_target_changed(target); });

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

TempControlPanel::~TempControlPanel() {
    deinit_subjects();
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

    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    // Guard: don't update live graph until subjects initialized
    if (!subjects_initialized_) {
        spdlog::debug("[TempPanel] SKIPPING graph update: subjects not initialized");
        return;
    }

    // DEBUG: Log that we passed subjects check (log every 40 calls = ~10sec)
    static int passed_init_count = 0;
    if (++passed_init_count % 40 == 0) {
        spdlog::trace("[TempPanel] PASSED subjects check #{}, throttle check: {} vs {} (diff={}ms)",
                      passed_init_count, now_ms, nozzle_last_graph_update_ms_,
                      now_ms - nozzle_last_graph_update_ms_);
    }

    // Throttle live graph updates to 1 Hz (graph has 1200 points for 20 minutes at 1 sample/sec)
    // Moonraker sends updates at ~4Hz, so skip updates that are too close together
    if (now_ms - nozzle_last_graph_update_ms_ < GRAPH_SAMPLE_INTERVAL_MS) {
        return; // Skip this update - too soon since last graph point
    }
    nozzle_last_graph_update_ms_ = now_ms;

    float temp_deg = centi_to_degrees_f(temp_centi);

    // Update all registered nozzle temperature graphs
    update_nozzle_graphs(temp_deg, now_ms);

    // Update mini graph Y-axis scaling (dynamic based on both temps)
    float bed_deg = centi_to_degrees_f(bed_current_);
    update_mini_graph_y_axis(temp_deg, bed_deg);
}

void TempControlPanel::update_nozzle_graphs(float temp_deg, int64_t now_ms) {
    // Log graph update state every 40 updates (~40 seconds)
    static int graph_update_count = 0;
    if (++graph_update_count % 40 == 0) {
        spdlog::trace("[TempPanel] Nozzle graphs update #{}: {:.1f}°C to {} graphs",
                      graph_update_count, temp_deg, nozzle_temp_graphs_.size());
    }

    // Update all registered graphs that care about nozzle temperature
    for (const auto& reg : nozzle_temp_graphs_) {
        if (reg.graph && reg.series_id >= 0) {
            ui_temp_graph_update_series_with_time(reg.graph, reg.series_id, temp_deg, now_ms);
        }
    }
}

void TempControlPanel::update_mini_graph_y_axis(float nozzle_deg, float bed_deg) {
    if (!mini_graph_) {
        return;
    }

    // Dynamic Y-axis scaling using extracted helper
    float new_y_max = calculate_mini_graph_y_max(mini_graph_y_max_, nozzle_deg, bed_deg);

    if (new_y_max != mini_graph_y_max_) {
        spdlog::debug("[TempPanel] Mini graph Y-axis {} to {}°C",
                      new_y_max > mini_graph_y_max_ ? "expanded" : "shrunk", new_y_max);
        mini_graph_y_max_ = new_y_max;
        ui_temp_graph_set_temp_range(mini_graph_, 0.0f, mini_graph_y_max_);
    }
}

void TempControlPanel::on_nozzle_target_changed(int target_centi) {
    nozzle_target_ = target_centi;
    update_nozzle_display();
    update_nozzle_status(); // Update status text and heating icon state

    float target_deg = centi_to_degrees_f(target_centi);
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

    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

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

    float temp_deg = centi_to_degrees_f(temp_centi);

    // Update all registered bed temperature graphs
    update_bed_graphs(temp_deg, now_ms);
}

void TempControlPanel::update_bed_graphs(float temp_deg, int64_t now_ms) {
    // Log graph update state every 40 updates (~40 seconds)
    static int graph_update_count = 0;
    if (++graph_update_count % 40 == 0) {
        spdlog::trace("[TempPanel] Bed graphs update #{}: {:.1f}°C to {} graphs",
                      graph_update_count, temp_deg, bed_temp_graphs_.size());
    }

    // Update all registered graphs that care about bed temperature
    for (const auto& reg : bed_temp_graphs_) {
        if (reg.graph && reg.series_id >= 0) {
            ui_temp_graph_update_series_with_time(reg.graph, reg.series_id, temp_deg, now_ms);
        }
    }
}

void TempControlPanel::on_bed_target_changed(int target_centi) {
    bed_target_ = target_centi;
    update_bed_display();
    update_bed_status(); // Update status text and heating icon state

    float target_deg = centi_to_degrees_f(target_centi);
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

    // nozzle_pending_ is in degrees (user-facing value from keypad/presets)
    int current_deg = centi_to_degrees_f(nozzle_current_);
    int target_deg = centi_to_degrees_f(nozzle_target_);

    // Show pending value if user has selected but not confirmed yet
    // Otherwise show actual target from Moonraker
    int display_target = (nozzle_pending_ >= 0) ? nozzle_pending_ : target_deg;

    if (nozzle_pending_ >= 0) {
        // Show pending with asterisk to indicate unsent
        if (nozzle_pending_ > 0) {
            snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(), "%d / %d*",
                     current_deg, nozzle_pending_);
        } else {
            snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(), "%d / —*",
                     current_deg);
        }
    } else if (display_target > 0) {
        snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(), "%d / %d", current_deg,
                 display_target);
    } else {
        snprintf(nozzle_display_buf_.data(), nozzle_display_buf_.size(), "%d / —", current_deg);
    }
    lv_subject_copy_string(&nozzle_display_subject_, nozzle_display_buf_.data());
}

void TempControlPanel::update_bed_display() {
    // Guard: don't update subject if not initialized yet (observer fires during construction)
    if (!subjects_initialized_) {
        return;
    }

    // bed_pending_ is in degrees (user-facing value from keypad/presets)
    int current_deg = centi_to_degrees_f(bed_current_);
    int target_deg = centi_to_degrees_f(bed_target_);

    // Show pending value if user has selected but not confirmed yet
    // Otherwise show actual target from Moonraker
    int display_target = (bed_pending_ >= 0) ? bed_pending_ : target_deg;

    if (bed_pending_ >= 0) {
        // Show pending with asterisk to indicate unsent
        if (bed_pending_ > 0) {
            snprintf(bed_display_buf_.data(), bed_display_buf_.size(), "%d / %d*", current_deg,
                     bed_pending_);
        } else {
            snprintf(bed_display_buf_.data(), bed_display_buf_.size(), "%d / —*", current_deg);
        }
    } else if (display_target > 0) {
        snprintf(bed_display_buf_.data(), bed_display_buf_.size(), "%d / %d", current_deg,
                 display_target);
    } else {
        snprintf(bed_display_buf_.data(), bed_display_buf_.size(), "%d / —", current_deg);
    }

    lv_subject_copy_string(&bed_display_subject_, bed_display_buf_.data());
}

void TempControlPanel::send_nozzle_temperature(int target) {
    spdlog::debug("[TempPanel] Sending nozzle temperature: {}°C to {}", target,
                  active_extruder_name_);

    if (!api_) {
        spdlog::warn("[TempPanel] Cannot set nozzle temp: no API connection");
        return;
    }

    api_->set_temperature(
        active_extruder_name_, static_cast<double>(target),
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

    // Initialize and register subjects using SubjectManager for RAII cleanup
    // NOTE: Use _N variant with explicit size for std::array buffers (sizeof(.data()) = pointer
    // size)
    UI_MANAGED_SUBJECT_STRING_N(nozzle_current_subject_, nozzle_current_buf_.data(),
                                nozzle_current_buf_.size(), nozzle_current_buf_.data(),
                                "nozzle_current_temp", subjects_);
    UI_MANAGED_SUBJECT_STRING_N(nozzle_target_subject_, nozzle_target_buf_.data(),
                                nozzle_target_buf_.size(), nozzle_target_buf_.data(),
                                "nozzle_target_temp", subjects_);
    UI_MANAGED_SUBJECT_STRING_N(bed_current_subject_, bed_current_buf_.data(),
                                bed_current_buf_.size(), bed_current_buf_.data(),
                                "bed_current_temp", subjects_);
    UI_MANAGED_SUBJECT_STRING_N(bed_target_subject_, bed_target_buf_.data(), bed_target_buf_.size(),
                                bed_target_buf_.data(), "bed_target_temp", subjects_);
    UI_MANAGED_SUBJECT_STRING_N(nozzle_display_subject_, nozzle_display_buf_.data(),
                                nozzle_display_buf_.size(), nozzle_display_buf_.data(),
                                "nozzle_temp_display", subjects_);
    UI_MANAGED_SUBJECT_STRING_N(bed_display_subject_, bed_display_buf_.data(),
                                bed_display_buf_.size(), bed_display_buf_.data(),
                                "bed_temp_display", subjects_);

    // Status text subjects (for reactive status messages like "Heating...", "Cooling down", "Idle")
    UI_MANAGED_SUBJECT_STRING_N(nozzle_status_subject_, nozzle_status_buf_.data(),
                                nozzle_status_buf_.size(), "Idle", "nozzle_status", subjects_);
    UI_MANAGED_SUBJECT_STRING_N(bed_status_subject_, bed_status_buf_.data(), bed_status_buf_.size(),
                                "Idle", "bed_status", subjects_);

    // Heating state subjects (0=off, 1=on) for reactive icon visibility in XML
    UI_MANAGED_SUBJECT_INT(nozzle_heating_subject_, 0, "nozzle_heating", subjects_);
    UI_MANAGED_SUBJECT_INT(bed_heating_subject_, 0, "bed_heating", subjects_);

    subjects_initialized_ = true;
    spdlog::debug("[TempPanel] Subjects initialized: nozzle={}/{}°C, bed={}/{}°C", nozzle_current_,
                  nozzle_target_, bed_current_, bed_target_);
}

void TempControlPanel::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    // Deinitialize all subjects via SubjectManager (RAII pattern)
    subjects_.deinit_all();

    subjects_initialized_ = false;
    spdlog::debug("[TempPanel] Subjects deinitialized");
}

// ============================================================================
// LIFECYCLE HOOKS
// ============================================================================

void NozzleTempPanelLifecycle::on_activate() {
    if (panel_) {
        panel_->on_nozzle_panel_activate();
    }
}

void NozzleTempPanelLifecycle::on_deactivate() {
    if (panel_) {
        panel_->on_nozzle_panel_deactivate();
    }
}

void BedTempPanelLifecycle::on_activate() {
    if (panel_) {
        panel_->on_bed_panel_activate();
    }
}

void BedTempPanelLifecycle::on_deactivate() {
    if (panel_) {
        panel_->on_bed_panel_deactivate();
    }
}

NozzleTempPanelLifecycle* TempControlPanel::get_nozzle_lifecycle() {
    return &nozzle_lifecycle_;
}

BedTempPanelLifecycle* TempControlPanel::get_bed_lifecycle() {
    return &bed_lifecycle_;
}

void TempControlPanel::on_nozzle_panel_activate() {
    spdlog::debug("[TempPanel] Nozzle panel activated");

    // Refresh display with current values
    update_nozzle_display();
    update_nozzle_status();

    // Replay history to graph if it exists
    if (nozzle_graph_) {
        replay_nozzle_history_to_graph();
    }
}

void TempControlPanel::on_nozzle_panel_deactivate() {
    spdlog::debug("[TempPanel] Nozzle panel deactivated");

    // Clear pending selection when panel is closed
    nozzle_pending_ = -1;
}

void TempControlPanel::on_bed_panel_activate() {
    spdlog::debug("[TempPanel] Bed panel activated");

    // Refresh display with current values
    update_bed_display();
    update_bed_status();

    // Replay history to graph if it exists
    if (bed_graph_) {
        replay_bed_history_to_graph();
    }
}

void TempControlPanel::on_bed_panel_deactivate() {
    spdlog::debug("[TempPanel] Bed panel deactivated");

    // Clear pending selection when panel is closed
    bed_pending_ = -1;
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
            self->active_extruder_name_, static_cast<double>(target),
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
        bool use_dark_mode = theme_manager_is_dark_mode();
        const char* color_str = lv_xml_get_const(scope, use_dark_mode ? "temp_graph_nozzle_dark"
                                                                      : "temp_graph_nozzle_light");
        if (color_str) {
            nozzle_config_.color = theme_manager_parse_hex_color(color_str);
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
            // Register graph for unified temperature updates
            nozzle_temp_graphs_.push_back({nozzle_graph_, nozzle_series_id_});
            spdlog::debug("[TempPanel] Registered nozzle_graph_ for temp updates");
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

    // Multi-extruder: show segmented control if multiple extruders discovered
    if (printer_state_.extruder_count() > 1) {
        rebuild_extruder_segments();
    }

    // Watch for late extruder discovery (e.g., reconnection) to rebuild selector
    extruder_version_observer_ = observe_int_sync<TempControlPanel>(
        printer_state_.get_extruder_version_subject(), this,
        [](TempControlPanel* self, int /*version*/) {
            spdlog::debug("[TempPanel] Extruder list changed, rebuilding selector");
            self->rebuild_extruder_segments();
        });

    // When active tool changes, auto-switch to that tool's extruder
    auto& tool_state = helix::ToolState::instance();
    if (tool_state.tool_count() > 1) {
        active_tool_observer_ =
            observe_int_sync<TempControlPanel>(tool_state.get_active_tool_subject(), this,
                                               [](TempControlPanel* self, int /*tool_idx*/) {
                                                   auto& ts = helix::ToolState::instance();
                                                   const auto* tool = ts.active_tool();
                                                   if (tool && tool->extruder_name) {
                                                       self->select_extruder(*tool->extruder_name);
                                                   }
                                               });
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
        bool use_dark_mode = theme_manager_is_dark_mode();
        const char* color_str =
            lv_xml_get_const(scope, use_dark_mode ? "temp_graph_bed_dark" : "temp_graph_bed_light");
        if (color_str) {
            bed_config_.color = theme_manager_parse_hex_color(color_str);
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
            // Register graph for unified temperature updates
            bed_temp_graphs_.push_back({bed_graph_, bed_series_id_});
            spdlog::debug("[TempPanel] Registered bed_graph_ for temp updates");
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

    // Convert target to degrees for display (current temp shown separately in UI)
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
        snprintf(nozzle_status_buf_.data(), nozzle_status_buf_.size(), "Cooling down");
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

    // Convert target to degrees for display (current temp shown separately in UI)
    int target_deg = bed_target_ / 10;

    if (bed_target_ > 0 && bed_current_ < bed_target_ - TEMP_TOLERANCE_CENTI) {
        // Actively heating
        snprintf(bed_status_buf_.data(), bed_status_buf_.size(), "Heating to %d°C...", target_deg);
    } else if (bed_target_ > 0 && bed_current_ >= bed_target_ - TEMP_TOLERANCE_CENTI) {
        // At target temperature
        snprintf(bed_status_buf_.data(), bed_status_buf_.size(), "At target temperature");
    } else if (bed_target_ == 0 && bed_current_ > BED_COOLING_THRESHOLD_CENTI) {
        // Cooling down (heater off but still hot)
        snprintf(bed_status_buf_.data(), bed_status_buf_.size(), "Cooling down");
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

// ============================================================================
// MULTI-EXTRUDER SUPPORT
// ============================================================================

void TempControlPanel::select_extruder(const std::string& name) {
    if (name == active_extruder_name_) {
        return;
    }

    spdlog::info("[TempPanel] Switching extruder: {} -> {}", active_extruder_name_, name);
    active_extruder_name_ = name;

    // Rebind nozzle observers to the selected extruder's subjects
    auto* temp_subj = printer_state_.get_extruder_temp_subject(name);
    auto* target_subj = printer_state_.get_extruder_target_subject(name);

    if (temp_subj) {
        nozzle_temp_observer_ = observe_int_sync<TempControlPanel>(
            temp_subj, this,
            [](TempControlPanel* self, int temp) { self->on_nozzle_temp_changed(temp); });
        // Read initial value from the new subject
        nozzle_current_ = lv_subject_get_int(temp_subj);
    }
    if (target_subj) {
        nozzle_target_observer_ = observe_int_sync<TempControlPanel>(
            target_subj, this,
            [](TempControlPanel* self, int target) { self->on_nozzle_target_changed(target); });
        // Read initial value from the new subject
        nozzle_target_ = lv_subject_get_int(target_subj);
    }

    // Clear pending selection, refresh display
    nozzle_pending_ = -1;
    update_nozzle_display();
    update_nozzle_status();

    // Replay graph history for the newly selected extruder
    if (nozzle_graph_ && nozzle_series_id_ >= 0) {
        // Clear existing graph data before replaying
        ui_temp_graph_clear_series(nozzle_graph_, nozzle_series_id_);
        replay_nozzle_history_to_graph();
    }

    // Update button states in the selector
    rebuild_extruder_segments();
}

void TempControlPanel::rebuild_extruder_segments() {
    auto* selector = lv_obj_find_by_name(nozzle_panel_, "extruder_selector");
    if (!selector) {
        return;
    }

    int count = printer_state_.extruder_count();
    if (count <= 1) {
        lv_obj_add_flag(selector, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(selector, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(selector); // Remove previous buttons

    // Build sorted extruder list for deterministic button order
    const auto& extruders = printer_state_.temperature_state().extruders();
    std::vector<std::string> names;
    names.reserve(extruders.size());
    for (const auto& [ext_name, info] : extruders) {
        names.push_back(ext_name);
    }
    std::sort(names.begin(), names.end());

    // Reset active extruder if it no longer exists (e.g., reconnect to different printer)
    if (extruders.find(active_extruder_name_) == extruders.end() && !names.empty()) {
        select_extruder(names.front()); // This calls rebuild_extruder_segments() again
        return;                         // Inner call already populated the selector
    }

    // Configure selector as a horizontal button row
    lv_obj_set_flex_flow(selector, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(selector, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(selector, 8, 0);

    for (const auto& ext_name : names) {
        const auto& info = extruders.at(ext_name);
        lv_obj_t* btn = lv_button_create(selector);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_height(btn, LV_SIZE_CONTENT);

        // Style: checked state for the active extruder
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
        if (ext_name == active_extruder_name_) {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(btn, LV_STATE_CHECKED);
        }

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, info.display_name.c_str());
        lv_obj_center(label);

        // Store TempControlPanel pointer as user data on the button.
        // The extruder name is recoverable from the button's position/label.
        lv_obj_set_user_data(btn, this);

        // Dynamically created buttons use C++ event callbacks (exception to
        // the "no lv_obj_add_event_cb" rule -- same pattern as FanDial)
        lv_obj_add_event_cb(
            btn,
            [](lv_event_t* e) {
                auto* self = static_cast<TempControlPanel*>(lv_event_get_user_data(e));
                if (!self) {
                    return;
                }
                lv_obj_t* clicked_btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
                // Find which extruder was clicked by reading the label text
                lv_obj_t* lbl = lv_obj_get_child(clicked_btn, 0);
                if (!lbl) {
                    return;
                }
                const char* display_text = lv_label_get_text(lbl);

                // Match display name back to klipper name
                const auto& exts = self->printer_state_.temperature_state().extruders();
                for (const auto& [kname, kinfo] : exts) {
                    if (kinfo.display_name == display_text) {
                        self->select_extruder(kname);
                        return;
                    }
                }
                spdlog::warn("[TempPanel] Could not find extruder for display name '{}'",
                             display_text);
            },
            LV_EVENT_CLICKED, this);
    }

    spdlog::debug("[TempPanel] Rebuilt extruder selector with {} buttons", names.size());
}

void TempControlPanel::replay_history_from_manager(ui_temp_graph_t* graph, int series_id,
                                                   const std::string& heater_name) {
    auto* mgr = get_temperature_history_manager();
    if (mgr == nullptr || graph == nullptr || series_id < 0) {
        return;
    }

    auto samples = mgr->get_samples(heater_name);
    if (samples.empty()) {
        spdlog::debug("[TempPanel] No history samples from manager for {}", heater_name);
        return;
    }

    int replayed = 0;
    for (const auto& sample : samples) {
        // Convert centidegrees to degrees for graph
        float temp = static_cast<float>(sample.temp_centi) / 10.0f;
        ui_temp_graph_update_series_with_time(graph, series_id, temp, sample.timestamp_ms);
        replayed++;
    }

    spdlog::info("[TempPanel] Replayed {} {} samples from history manager", replayed, heater_name);
}

void TempControlPanel::replay_nozzle_history_to_graph() {
    if (!nozzle_graph_ || nozzle_series_id_ < 0) {
        return;
    }

    // Use TemperatureHistoryManager for history data (active extruder)
    replay_history_from_manager(nozzle_graph_, nozzle_series_id_, active_extruder_name_);
}

void TempControlPanel::replay_bed_history_to_graph() {
    if (!bed_graph_ || bed_series_id_ < 0) {
        return;
    }

    // Use TemperatureHistoryManager for history data
    replay_history_from_manager(bed_graph_, bed_series_id_, "heater_bed");
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

    // Use smallest font for axis labels (cramped space on filament panel)
    ui_temp_graph_set_axis_size(mini_graph_, "xs");

    // Add bed series FIRST (renders underneath) - cyan/cooling color
    mini_bed_series_id_ = ui_temp_graph_add_series(mini_graph_, "Bed", bed_config_.color);
    if (mini_bed_series_id_ >= 0) {
        // Subtle gradient so it doesn't dominate
        ui_temp_graph_set_series_gradient(mini_graph_, mini_bed_series_id_, LV_OPA_0, LV_OPA_10);
        // Register for unified bed temperature updates
        bed_temp_graphs_.push_back({mini_graph_, mini_bed_series_id_});
    }

    // Add nozzle series SECOND (renders on top) - red/heating color
    mini_nozzle_series_id_ = ui_temp_graph_add_series(mini_graph_, "Nozzle", nozzle_config_.color);
    if (mini_nozzle_series_id_ >= 0) {
        // More visible gradient for the primary temp (filament loading)
        ui_temp_graph_set_series_gradient(mini_graph_, mini_nozzle_series_id_, LV_OPA_0, LV_OPA_20);
        // Register for unified nozzle temperature updates
        nozzle_temp_graphs_.push_back({mini_graph_, mini_nozzle_series_id_});
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

    spdlog::debug("[TempPanel] Mini combined graph created with {} point capacity, "
                  "registered {} nozzle graphs, {} bed graphs",
                  MINI_GRAPH_POINTS, nozzle_temp_graphs_.size(), bed_temp_graphs_.size());
}

void TempControlPanel::register_heater_graph(ui_temp_graph_t* graph, int series_id,
                                             const std::string& heater) {
    // Match any extruder name (extruder, extruder1, etc.) to nozzle graphs
    if (heater.rfind("extruder", 0) == 0) {
        nozzle_temp_graphs_.push_back({graph, series_id});
    } else if (heater == "heater_bed") {
        bed_temp_graphs_.push_back({graph, series_id});
    }
    spdlog::debug("[TempPanel] Registered external graph for {}", heater);
}

void TempControlPanel::unregister_heater_graph(ui_temp_graph_t* graph) {
    auto remove_from = [graph](std::vector<RegisteredGraph>& vec) {
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [graph](const RegisteredGraph& rg) { return rg.graph == graph; }),
                  vec.end());
    };
    remove_from(nozzle_temp_graphs_);
    remove_from(bed_temp_graphs_);
    spdlog::debug("[TempPanel] Unregistered external graph");
}

void TempControlPanel::replay_history_to_mini_graph() {
    if (!mini_graph_) {
        return;
    }

    auto* mgr = get_temperature_history_manager();
    if (mgr == nullptr) {
        spdlog::debug("[TempPanel] Mini graph: no history manager available");
        return;
    }

    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    // Only replay last 5 minutes (300 seconds)
    int64_t cutoff_ms = now_ms - (MINI_GRAPH_POINTS * 1000);

    // Helper lambda to replay samples from manager
    auto replay_heater = [&](const std::string& heater_name, int series_id) {
        if (series_id < 0) {
            return;
        }

        auto samples = mgr->get_samples_since(heater_name, cutoff_ms);
        if (samples.empty()) {
            return;
        }

        int64_t last_graphed_time = 0;
        int replayed = 0;

        for (const auto& sample : samples) {
            // Throttle to GRAPH_SAMPLE_INTERVAL_MS
            if (last_graphed_time > 0 &&
                (sample.timestamp_ms - last_graphed_time) < GRAPH_SAMPLE_INTERVAL_MS) {
                continue;
            }

            float temp_deg = centi_to_degrees_f(sample.temp_centi);
            ui_temp_graph_update_series_with_time(mini_graph_, series_id, temp_deg,
                                                  sample.timestamp_ms);
            last_graphed_time = sample.timestamp_ms;
            replayed++;
        }

        if (replayed > 0) {
            spdlog::debug("[TempPanel] Mini graph: replayed {} {} samples", replayed, heater_name);
        }
    };

    // Replay both heaters (use active extruder for nozzle)
    replay_heater(active_extruder_name_, mini_nozzle_series_id_);
    replay_heater("heater_bed", mini_bed_series_id_);
}
