// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_controls_extrusion.h"

#include "app_constants.h"
#include "app_globals.h"
#include "printer_state.h"
#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_subject_registry.h"
#include "ui_temperature_utils.h"
#include "ui_theme.h"
#include "ui_utils.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <memory>

// Static constexpr member definition (C++14 compatibility)
constexpr int ExtrusionPanel::AMOUNT_VALUES[4];

// ============================================================================
// CONSTRUCTOR
// ============================================================================

ExtrusionPanel::ExtrusionPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // Initialize buffer contents
    std::snprintf(temp_status_buf_, sizeof(temp_status_buf_), "%d / %d°C", nozzle_current_,
                  nozzle_target_);
    std::snprintf(warning_temps_buf_, sizeof(warning_temps_buf_), "Current: %d°C\nTarget: %d°C",
                  nozzle_current_, nozzle_target_);
}

// ============================================================================
// PANELBASE IMPLEMENTATION
// ============================================================================

void ExtrusionPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // Initialize subjects with default values
    UI_SUBJECT_INIT_AND_REGISTER_STRING(temp_status_subject_, temp_status_buf_, temp_status_buf_,
                                        "extrusion_temp_status");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(warning_temps_subject_, warning_temps_buf_,
                                        warning_temps_buf_, "extrusion_warning_temps");

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized: temp_status, warning_temps", get_name());
}

void ExtrusionPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    spdlog::info("[{}] Setting up event handlers...", get_name());

    // Use standard overlay panel setup (wires header, back button, handles responsive padding)
    ui_overlay_panel_setup_standard(panel_, parent_screen_, "overlay_header", "overlay_content");

    // Setup all button groups
    setup_amount_buttons();
    setup_action_buttons();
    setup_temperature_observer();

    // Initialize visual state
    update_amount_buttons_visual();
    update_temp_status();
    update_warning_text();
    update_safety_state();

    spdlog::info("[{}] Setup complete!", get_name());
}

// ============================================================================
// PRIVATE SETUP HELPERS
// ============================================================================

void ExtrusionPanel::setup_amount_buttons() {
    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_, "overlay_content");
    if (!overlay_content) {
        spdlog::error("[{}] overlay_content not found!", get_name());
        return;
    }

    const char* amount_names[] = {"amount_5mm", "amount_10mm", "amount_25mm", "amount_50mm"};

    for (int i = 0; i < 4; i++) {
        amount_buttons_[i] = lv_obj_find_by_name(overlay_content, amount_names[i]);
        if (amount_buttons_[i]) {
            // Pass 'this' as user_data for trampoline
            lv_obj_add_event_cb(amount_buttons_[i], on_amount_button_clicked, LV_EVENT_CLICKED,
                                this);
        }
    }

    spdlog::debug("[{}] Amount selector (4 buttons)", get_name());
}

void ExtrusionPanel::setup_action_buttons() {
    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_, "overlay_content");
    if (!overlay_content) return;

    // Extrude button
    btn_extrude_ = lv_obj_find_by_name(overlay_content, "btn_extrude");
    if (btn_extrude_) {
        lv_obj_add_event_cb(btn_extrude_, on_extrude_clicked, LV_EVENT_CLICKED, this);
        spdlog::debug("[{}] Extrude button", get_name());
    }

    // Retract button
    btn_retract_ = lv_obj_find_by_name(overlay_content, "btn_retract");
    if (btn_retract_) {
        lv_obj_add_event_cb(btn_retract_, on_retract_clicked, LV_EVENT_CLICKED, this);
        spdlog::debug("[{}] Retract button", get_name());
    }

    // Safety warning card
    safety_warning_ = lv_obj_find_by_name(overlay_content, "safety_warning");
}

void ExtrusionPanel::setup_temperature_observer() {
    // Look up nozzle temperature subject from LVGL's global registry
    // This subject is owned by TempControlPanel (or PrinterState in the future)
    lv_subject_t* nozzle_temp = lv_xml_get_subject(NULL, "nozzle_temp_current");

    if (nozzle_temp) {
        // Add observer and register for RAII cleanup
        auto* obs = lv_subject_add_observer(nozzle_temp, on_nozzle_temp_changed, this);
        register_observer(obs);
        spdlog::debug("[{}] Subscribed to nozzle_temp_current subject", get_name());
    } else {
        spdlog::warn("[{}] nozzle_temp_current subject not found - temperature updates unavailable",
                     get_name());
    }
}

// ============================================================================
// PRIVATE UPDATE HELPERS
// ============================================================================

void ExtrusionPanel::update_temp_status() {
    // Status indicator: ✓ (ready), ⚠ (heating), ✗ (too cold)
    const char* status_icon;
    if (UITemperatureUtils::is_extrusion_safe(nozzle_current_,
                                              AppConstants::Temperature::MIN_EXTRUSION_TEMP)) {
        // Within 5°C of target and hot enough (safe range check without overflow)
        if (nozzle_target_ > 0 && nozzle_current_ >= nozzle_target_ - 5 &&
            nozzle_current_ <= nozzle_target_ + 5) {
            status_icon = "✓"; // Ready
        } else {
            status_icon = "✓"; // Hot enough
        }
    } else if (nozzle_target_ >= AppConstants::Temperature::MIN_EXTRUSION_TEMP) {
        status_icon = "⚠"; // Heating
    } else {
        status_icon = "✗"; // Too cold
    }

    std::snprintf(temp_status_buf_, sizeof(temp_status_buf_), "%d / %d°C %s", nozzle_current_,
                  nozzle_target_, status_icon);
    lv_subject_copy_string(&temp_status_subject_, temp_status_buf_);
}

void ExtrusionPanel::update_warning_text() {
    std::snprintf(warning_temps_buf_, sizeof(warning_temps_buf_), "Current: %d°C\nTarget: %d°C",
                  nozzle_current_, nozzle_target_);
    lv_subject_copy_string(&warning_temps_subject_, warning_temps_buf_);
}

void ExtrusionPanel::update_safety_state() {
    bool allowed = is_extrusion_allowed();

    // Enable/disable extrude and retract buttons
    if (btn_extrude_) {
        if (allowed) {
            lv_obj_clear_state(btn_extrude_, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btn_extrude_, LV_STATE_DISABLED);
        }
    }

    if (btn_retract_) {
        if (allowed) {
            lv_obj_clear_state(btn_retract_, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btn_retract_, LV_STATE_DISABLED);
        }
    }

    // Show/hide safety warning
    if (safety_warning_) {
        if (allowed) {
            lv_obj_add_flag(safety_warning_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(safety_warning_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    spdlog::debug("[{}] Safety state updated: allowed={} (temp={}°C)", get_name(), allowed,
                  nozzle_current_);
}

void ExtrusionPanel::update_amount_buttons_visual() {
    for (int i = 0; i < 4; i++) {
        if (amount_buttons_[i]) {
            if (AMOUNT_VALUES[i] == selected_amount_) {
                // Selected state - theme handles colors
                lv_obj_add_state(amount_buttons_[i], LV_STATE_CHECKED);
            } else {
                // Unselected state - theme handles colors
                lv_obj_remove_state(amount_buttons_[i], LV_STATE_CHECKED);
            }
        }
    }
}

// ============================================================================
// INSTANCE HANDLERS
// ============================================================================

void ExtrusionPanel::handle_amount_button(lv_obj_t* btn) {
    const char* name = lv_obj_get_name(btn);
    if (!name) return;

    if (std::strcmp(name, "amount_5mm") == 0) {
        selected_amount_ = 5;
    } else if (std::strcmp(name, "amount_10mm") == 0) {
        selected_amount_ = 10;
    } else if (std::strcmp(name, "amount_25mm") == 0) {
        selected_amount_ = 25;
    } else if (std::strcmp(name, "amount_50mm") == 0) {
        selected_amount_ = 50;
    }

    update_amount_buttons_visual();
    spdlog::debug("[{}] Amount selected: {}mm", get_name(), selected_amount_);
}

void ExtrusionPanel::handle_extrude() {
    if (!is_extrusion_allowed()) {
        NOTIFY_WARNING("Nozzle too cold for extrusion ({}°C, min: {}°C)", nozzle_current_,
                       AppConstants::Temperature::MIN_EXTRUSION_TEMP);
        return;
    }

    spdlog::info("[{}] Extruding {}mm of filament", get_name(), selected_amount_);

    // TODO: Send command via MoonrakerAPI
    // if (api_) {
    //     api_->extrude(selected_amount_);
    // }
}

void ExtrusionPanel::handle_retract() {
    if (!is_extrusion_allowed()) {
        NOTIFY_WARNING("Nozzle too cold for retraction ({}°C, min: {}°C)", nozzle_current_,
                       AppConstants::Temperature::MIN_EXTRUSION_TEMP);
        return;
    }

    spdlog::info("[{}] Retracting {}mm of filament", get_name(), selected_amount_);

    // TODO: Send command via MoonrakerAPI
    // if (api_) {
    //     api_->retract(selected_amount_);
    // }
}

// ============================================================================
// STATIC TRAMPOLINES
// ============================================================================

void ExtrusionPanel::on_amount_button_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ExtrusionPanel] on_amount_button_clicked");
    auto* self = static_cast<ExtrusionPanel*>(lv_event_get_user_data(e));
    if (self) {
        lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
        self->handle_amount_button(btn);
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ExtrusionPanel::on_extrude_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ExtrusionPanel] on_extrude_clicked");
    auto* self = static_cast<ExtrusionPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_extrude();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ExtrusionPanel::on_retract_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ExtrusionPanel] on_retract_clicked");
    auto* self = static_cast<ExtrusionPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_retract();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ExtrusionPanel::on_nozzle_temp_changed(lv_observer_t* observer, lv_subject_t* subject) {
    // Get user_data from observer (set when registering)
    auto* self = static_cast<ExtrusionPanel*>(lv_observer_get_user_data(observer));
    if (!self) return;

    // Get the new temperature value
    // The subject may be int or float depending on implementation
    int new_temp = lv_subject_get_int(subject);

    spdlog::debug("[{}] Nozzle temp update from subject: {}°C", self->get_name(), new_temp);

    // Update our local state and refresh UI
    self->nozzle_current_ = new_temp;
    self->update_temp_status();
    self->update_warning_text();
    self->update_safety_state();
}

// ============================================================================
// PUBLIC API
// ============================================================================

void ExtrusionPanel::set_temp(int current, int target) {
    // Validate temperature ranges using dynamic limits
    if (current < nozzle_min_temp_ || current > nozzle_max_temp_) {
        spdlog::warn("[{}] Invalid nozzle current temperature {}°C (valid: {}-{}°C), clamping",
                     get_name(), current, nozzle_min_temp_, nozzle_max_temp_);
        current = (current < nozzle_min_temp_) ? nozzle_min_temp_ : nozzle_max_temp_;
    }
    if (target < nozzle_min_temp_ || target > nozzle_max_temp_) {
        spdlog::warn("[{}] Invalid nozzle target temperature {}°C (valid: {}-{}°C), clamping",
                     get_name(), target, nozzle_min_temp_, nozzle_max_temp_);
        target = (target < nozzle_min_temp_) ? nozzle_min_temp_ : nozzle_max_temp_;
    }

    nozzle_current_ = current;
    nozzle_target_ = target;
    update_temp_status();
    update_warning_text();
    update_safety_state();
}

bool ExtrusionPanel::is_extrusion_allowed() const {
    return UITemperatureUtils::is_extrusion_safe(nozzle_current_,
                                                 AppConstants::Temperature::MIN_EXTRUSION_TEMP);
}

void ExtrusionPanel::set_limits(int min_temp, int max_temp) {
    nozzle_min_temp_ = min_temp;
    nozzle_max_temp_ = max_temp;
    spdlog::info("[{}] Nozzle temperature limits updated: {}-{}°C", get_name(), min_temp, max_temp);
}

// ============================================================================
// DEPRECATED LEGACY API
// ============================================================================
//
// These wrappers maintain backwards compatibility during the transition.
// They create a global ExtrusionPanel instance and delegate to its methods.
//
// TODO(clean-break): Remove after all callers updated to use ExtrusionPanel class
// ============================================================================

// Global instance for legacy API - created on first use
static std::unique_ptr<ExtrusionPanel> g_extrusion_panel;

// Helper to get or create the global instance
ExtrusionPanel& get_global_extrusion_panel() {
    if (!g_extrusion_panel) {
        g_extrusion_panel = std::make_unique<ExtrusionPanel>(get_printer_state(), nullptr);
    }
    return *g_extrusion_panel;
}


// ============================================================================
// GLOBAL INSTANCE (needed by main.cpp)
// ============================================================================

static std::unique_ptr<ExtrusionPanel> g_controls_extrusion_panel;

ExtrusionPanel& get_global_controls_extrusion_panel() {
    if (!g_controls_extrusion_panel) {
        g_controls_extrusion_panel = std::make_unique<ExtrusionPanel>(get_printer_state(), nullptr);
    }
    return *g_controls_extrusion_panel;
}
