// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_retraction_settings.h"

#include "ui_nav.h"

#include "lvgl/src/xml/lv_xml.h"
#include "runtime_config.h"

#include <spdlog/spdlog.h>

// Global instance and panel
static std::unique_ptr<RetractionSettingsOverlay> g_retraction_settings;
static lv_obj_t* g_retraction_settings_panel = nullptr;

// Forward declaration for row click callback (from settings panel)
static void on_retraction_row_clicked(lv_event_t* e);

RetractionSettingsOverlay& get_global_retraction_settings() {
    if (!g_retraction_settings) {
        spdlog::error(
            "[Retraction Settings] get_global_retraction_settings() called before initialization!");
        throw std::runtime_error("RetractionSettingsOverlay not initialized");
    }
    return *g_retraction_settings;
}

void init_global_retraction_settings(PrinterState& printer_state, MoonrakerClient* client) {
    if (g_retraction_settings) {
        spdlog::warn(
            "[Retraction Settings] RetractionSettingsOverlay already initialized, skipping");
        return;
    }
    g_retraction_settings = std::make_unique<RetractionSettingsOverlay>(printer_state, client);
    spdlog::debug("[Retraction Settings] RetractionSettingsOverlay initialized");
}

RetractionSettingsOverlay::RetractionSettingsOverlay(PrinterState& printer_state,
                                                     MoonrakerClient* client)
    : PanelBase(printer_state, nullptr), client_(client) {
    spdlog::debug("[Retraction Settings] Constructor");
}

RetractionSettingsOverlay::~RetractionSettingsOverlay() {
    // [L010] No spdlog in destructors - may crash during static destruction

    // LVGL may already be destroyed during static destruction
    if (!lv_is_initialized()) {
        return;
    }

    lv_subject_deinit(&retract_length_display_);
    lv_subject_deinit(&retract_speed_display_);
    lv_subject_deinit(&unretract_extra_display_);
    lv_subject_deinit(&unretract_speed_display_);
}

void RetractionSettingsOverlay::init_subjects() {
    // Initialize display label subjects
    snprintf(retract_length_buf_, sizeof(retract_length_buf_), "0.0mm");
    lv_subject_init_string(&retract_length_display_, retract_length_buf_, nullptr,
                           sizeof(retract_length_buf_), "0.0mm");
    lv_xml_register_subject(nullptr, "retract_length_display", &retract_length_display_);

    snprintf(retract_speed_buf_, sizeof(retract_speed_buf_), "35mm/s");
    lv_subject_init_string(&retract_speed_display_, retract_speed_buf_, nullptr,
                           sizeof(retract_speed_buf_), "35mm/s");
    lv_xml_register_subject(nullptr, "retract_speed_display", &retract_speed_display_);

    snprintf(unretract_extra_buf_, sizeof(unretract_extra_buf_), "0.0mm");
    lv_subject_init_string(&unretract_extra_display_, unretract_extra_buf_, nullptr,
                           sizeof(unretract_extra_buf_), "0.0mm");
    lv_xml_register_subject(nullptr, "unretract_extra_display", &unretract_extra_display_);

    snprintf(unretract_speed_buf_, sizeof(unretract_speed_buf_), "35mm/s");
    lv_subject_init_string(&unretract_speed_display_, unretract_speed_buf_, nullptr,
                           sizeof(unretract_speed_buf_), "35mm/s");
    lv_xml_register_subject(nullptr, "unretract_speed_display", &unretract_speed_display_);

    // Register the row click callback for opening this overlay from settings panel
    spdlog::info("[RetractionSettings] Registering on_retraction_row_clicked callback");
    lv_xml_register_event_cb(nullptr, "on_retraction_row_clicked", on_retraction_row_clicked);

    // Register slider/toggle callbacks
    lv_xml_register_event_cb(nullptr, "on_retraction_enabled_changed", on_enabled_changed);
    lv_xml_register_event_cb(nullptr, "on_retract_length_changed", on_retract_length_changed);
    lv_xml_register_event_cb(nullptr, "on_retract_speed_changed", on_retract_speed_changed);
    lv_xml_register_event_cb(nullptr, "on_unretract_extra_changed", on_unretract_extra_changed);
    lv_xml_register_event_cb(nullptr, "on_unretract_speed_changed", on_unretract_speed_changed);

    spdlog::debug("[{}] init_subjects() - registered callbacks", get_name());
}

void RetractionSettingsOverlay::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    PanelBase::setup(panel, parent_screen);

    spdlog::debug("[{}] setup() - finding widgets", get_name());

    // Find widgets
    enable_switch_ = lv_obj_find_by_name(panel_, "retraction_enabled_switch");
    retract_length_slider_ = lv_obj_find_by_name(panel_, "retract_length_slider");
    retract_speed_slider_ = lv_obj_find_by_name(panel_, "retract_speed_slider");
    unretract_extra_slider_ = lv_obj_find_by_name(panel_, "unretract_extra_slider");
    unretract_speed_slider_ = lv_obj_find_by_name(panel_, "unretract_speed_slider");

    spdlog::debug("[{}] Widgets found: enable={} length={} speed={} extra={} uspeed={}", get_name(),
                  enable_switch_ != nullptr, retract_length_slider_ != nullptr,
                  retract_speed_slider_ != nullptr, unretract_extra_slider_ != nullptr,
                  unretract_speed_slider_ != nullptr);
}

void RetractionSettingsOverlay::on_activate() {
    spdlog::debug("[{}] on_activate() - syncing from printer state", get_name());
    sync_from_printer_state();
}

void RetractionSettingsOverlay::on_deactivate() {
    spdlog::debug("[{}] on_deactivate()", get_name());
}

void RetractionSettingsOverlay::sync_from_printer_state() {
    syncing_from_state_ = true;

    // Get subjects from global registry (registered by PrinterState)
    lv_subject_t* length_subj = lv_xml_get_subject(nullptr, "retract_length");
    lv_subject_t* speed_subj = lv_xml_get_subject(nullptr, "retract_speed");
    lv_subject_t* extra_subj = lv_xml_get_subject(nullptr, "unretract_extra_length");
    lv_subject_t* uspeed_subj = lv_xml_get_subject(nullptr, "unretract_speed");

    if (!length_subj || !speed_subj || !extra_subj || !uspeed_subj) {
        spdlog::error("[{}] Required subjects not registered - cannot sync!", get_name());
        syncing_from_state_ = false;
        return;
    }

    // Get values (centimm for lengths)
    int retract_length_centimm = lv_subject_get_int(length_subj);
    int retract_speed = lv_subject_get_int(speed_subj);
    int unretract_extra_centimm = lv_subject_get_int(extra_subj);
    int unretract_speed = lv_subject_get_int(uspeed_subj);

    spdlog::debug("[{}] Syncing: length={}centimm speed={} extra={}centimm uspeed={}", get_name(),
                  retract_length_centimm, retract_speed, unretract_extra_centimm, unretract_speed);

    // Update enable switch (enabled if retract_length > 0)
    if (enable_switch_) {
        if (retract_length_centimm > 0) {
            lv_obj_add_state(enable_switch_, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(enable_switch_, LV_STATE_CHECKED);
        }
    }

    // Update sliders (length sliders are in centimm to match subject)
    if (retract_length_slider_) {
        lv_slider_set_value(retract_length_slider_, retract_length_centimm, LV_ANIM_OFF);
    }
    if (retract_speed_slider_) {
        lv_slider_set_value(retract_speed_slider_, retract_speed, LV_ANIM_OFF);
    }
    if (unretract_extra_slider_) {
        lv_slider_set_value(unretract_extra_slider_, unretract_extra_centimm, LV_ANIM_OFF);
    }
    if (unretract_speed_slider_) {
        lv_slider_set_value(unretract_speed_slider_, unretract_speed, LV_ANIM_OFF);
    }

    update_display_labels();
    syncing_from_state_ = false;
}

void RetractionSettingsOverlay::update_display_labels() {
    if (retract_length_slider_) {
        int centimm = lv_slider_get_value(retract_length_slider_);
        snprintf(retract_length_buf_, sizeof(retract_length_buf_), "%.1fmm", centimm / 100.0);
        lv_subject_copy_string(&retract_length_display_, retract_length_buf_);
    }

    if (retract_speed_slider_) {
        int speed = lv_slider_get_value(retract_speed_slider_);
        snprintf(retract_speed_buf_, sizeof(retract_speed_buf_), "%dmm/s", speed);
        lv_subject_copy_string(&retract_speed_display_, retract_speed_buf_);
    }

    if (unretract_extra_slider_) {
        int centimm = lv_slider_get_value(unretract_extra_slider_);
        snprintf(unretract_extra_buf_, sizeof(unretract_extra_buf_), "%.1fmm", centimm / 100.0);
        lv_subject_copy_string(&unretract_extra_display_, unretract_extra_buf_);
    }

    if (unretract_speed_slider_) {
        int speed = lv_slider_get_value(unretract_speed_slider_);
        snprintf(unretract_speed_buf_, sizeof(unretract_speed_buf_), "%dmm/s", speed);
        lv_subject_copy_string(&unretract_speed_display_, unretract_speed_buf_);
    }
}

void RetractionSettingsOverlay::send_retraction_settings() {
    if (!client_) {
        spdlog::debug("[{}] No client, skipping G-code send", get_name());
        return;
    }

    // Get current slider values
    int length_centimm = retract_length_slider_ ? lv_slider_get_value(retract_length_slider_) : 0;
    int retract_speed = retract_speed_slider_ ? lv_slider_get_value(retract_speed_slider_) : 35;
    int extra_centimm = unretract_extra_slider_ ? lv_slider_get_value(unretract_extra_slider_) : 0;
    int unretract_spd = unretract_speed_slider_ ? lv_slider_get_value(unretract_speed_slider_) : 35;

    // Convert centimm to mm
    double length_mm = length_centimm / 100.0;
    double extra_mm = extra_centimm / 100.0;

    // Build G-code command
    char gcode[128];
    snprintf(gcode, sizeof(gcode),
             "SET_RETRACTION RETRACT_LENGTH=%.2f RETRACT_SPEED=%d "
             "UNRETRACT_EXTRA_LENGTH=%.2f UNRETRACT_SPEED=%d",
             length_mm, retract_speed, extra_mm, unretract_spd);

    spdlog::info("[{}] Sending: {}", get_name(), gcode);
    client_->gcode_script(gcode);
}

// =============================================================================
// EVENT HANDLERS
// =============================================================================

void RetractionSettingsOverlay::on_enabled_changed(lv_event_t* e) {
    auto* sw = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);

    spdlog::debug("[Retraction Settings] Enable toggled: {}", enabled);

    auto& overlay = get_global_retraction_settings();
    if (overlay.syncing_from_state_) {
        return;
    }

    if (enabled) {
        // Enable with current slider values
        overlay.send_retraction_settings();
    } else {
        // Disable by setting retract length to 0
        if (overlay.client_) {
            overlay.client_->gcode_script("SET_RETRACTION RETRACT_LENGTH=0");
        }
    }
}

void RetractionSettingsOverlay::on_retract_length_changed(lv_event_t* /*e*/) {
    auto& overlay = get_global_retraction_settings();
    overlay.update_display_labels();

    if (overlay.syncing_from_state_) {
        return;
    }
    overlay.send_retraction_settings();
}

void RetractionSettingsOverlay::on_retract_speed_changed(lv_event_t* /*e*/) {
    auto& overlay = get_global_retraction_settings();
    overlay.update_display_labels();

    if (overlay.syncing_from_state_) {
        return;
    }
    overlay.send_retraction_settings();
}

void RetractionSettingsOverlay::on_unretract_extra_changed(lv_event_t* /*e*/) {
    auto& overlay = get_global_retraction_settings();
    overlay.update_display_labels();

    if (overlay.syncing_from_state_) {
        return;
    }
    overlay.send_retraction_settings();
}

void RetractionSettingsOverlay::on_unretract_speed_changed(lv_event_t* /*e*/) {
    auto& overlay = get_global_retraction_settings();
    overlay.update_display_labels();

    if (overlay.syncing_from_state_) {
        return;
    }
    overlay.send_retraction_settings();
}

// =============================================================================
// ROW CLICK CALLBACK (from settings panel)
// =============================================================================

static void on_retraction_row_clicked(lv_event_t* /*e*/) {
    spdlog::info("[Retraction Settings] Row clicked, opening overlay");

    // Create overlay if not exists
    if (!g_retraction_settings_panel) {
        spdlog::info("[Retraction Settings] Creating overlay from XML...");
        g_retraction_settings_panel = static_cast<lv_obj_t*>(
            lv_xml_create(lv_screen_active(), "retraction_settings_overlay", nullptr));
        if (g_retraction_settings_panel) {
            spdlog::info("[Retraction Settings] Overlay created successfully, calling setup()");
            get_global_retraction_settings().setup(g_retraction_settings_panel, lv_screen_active());
        } else {
            spdlog::error("[Retraction Settings] FAILED to create overlay from XML!");
            return;
        }
    }

    if (g_retraction_settings_panel) {
        spdlog::info("[Retraction Settings] Calling ui_nav_push_overlay...");
        ui_nav_push_overlay(g_retraction_settings_panel);
        get_global_retraction_settings().on_activate();
        spdlog::info("[Retraction Settings] Overlay should be visible now");
    } else {
        spdlog::error("[Retraction Settings] Panel is NULL!");
    }
}
