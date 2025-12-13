// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_extrusion.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_subject_registry.h"
#include "ui_temperature_utils.h"
#include "ui_theme.h"
#include "ui_utils.h"

#include "app_constants.h"
#include "app_globals.h"
#include "moonraker_api.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <memory>

constexpr int ExtrusionPanel::AMOUNT_VALUES[4];

ExtrusionPanel::ExtrusionPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // Initialize buffer contents
    std::snprintf(temp_status_buf_, sizeof(temp_status_buf_), "%d / %d°C", nozzle_current_,
                  nozzle_target_);
    std::snprintf(warning_temps_buf_, sizeof(warning_temps_buf_), "Current: %d°C\nTarget: %d°C",
                  nozzle_current_, nozzle_target_);
    std::snprintf(speed_display_buf_, sizeof(speed_display_buf_), "%d mm/min",
                  extrusion_speed_mmpm_);
}

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
    UI_SUBJECT_INIT_AND_REGISTER_INT(
        safety_warning_visible_subject_, 1,
        "extrusion_safety_warning_visible"); // 1=visible (cold at start)
    UI_SUBJECT_INIT_AND_REGISTER_STRING(speed_display_subject_, speed_display_buf_,
                                        speed_display_buf_, "extrusion_speed_display");

    // Register XML event callbacks (declarative pattern)
    lv_xml_register_event_cb(nullptr, "on_extrusion_extrude", [](lv_event_t* /*e*/) {
        get_global_extrusion_panel().handle_extrude();
    });
    lv_xml_register_event_cb(nullptr, "on_extrusion_retract", [](lv_event_t* /*e*/) {
        get_global_extrusion_panel().handle_retract();
    });
    lv_xml_register_event_cb(nullptr, "on_extrusion_purge", [](lv_event_t* /*e*/) {
        get_global_extrusion_panel().handle_purge();
    });
    lv_xml_register_event_cb(nullptr, "on_extrusion_speed_changed", [](lv_event_t* e) {
        auto& panel = get_global_extrusion_panel();
        lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        if (slider) {
            panel.set_speed(lv_slider_get_value(slider));
        }
    });

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized and event callbacks registered", get_name());
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

    // Setup all controls
    setup_amount_buttons();
    setup_action_buttons();
    setup_speed_slider();
    setup_animation_widget();
    setup_temperature_observer();

    // Initialize visual state
    update_amount_buttons_visual();
    update_temp_status();
    update_warning_text();
    update_safety_state();
    update_speed_display();

    spdlog::info("[{}] Setup complete!", get_name());
}

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
    if (!overlay_content)
        return;

    // Store widget pointers for potential state updates
    // Event handlers are wired via XML event_cb (declarative pattern)
    btn_extrude_ = lv_obj_find_by_name(overlay_content, "btn_extrude");
    btn_retract_ = lv_obj_find_by_name(overlay_content, "btn_retract");
    btn_purge_ = lv_obj_find_by_name(overlay_content, "btn_purge");
    safety_warning_ = lv_obj_find_by_name(overlay_content, "safety_warning");

    spdlog::debug("[{}] Action buttons found (events wired via XML)", get_name());
}

void ExtrusionPanel::setup_temperature_observer() {
    // Look up nozzle temperature subject from LVGL's global registry
    // This subject is owned by TempControlPanel (or PrinterState in the future)
    lv_subject_t* nozzle_temp = lv_xml_get_subject(NULL, "nozzle_temp_current");

    if (nozzle_temp) {
        // ObserverGuard handles cleanup automatically in destructor
        nozzle_temp_observer_ = ObserverGuard(nozzle_temp, on_nozzle_temp_changed, this);
        spdlog::debug("[{}] Subscribed to nozzle_temp_current subject", get_name());
    } else {
        spdlog::warn("[{}] nozzle_temp_current subject not found - temperature updates unavailable",
                     get_name());
    }
}

void ExtrusionPanel::update_temp_status() {
    // Status indicator: ✓ (ready), ⚠ (heating), ✗ (too cold)
    const char* status_icon;
    if (helix::ui::temperature::is_extrusion_safe(nozzle_current_,
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

    // Update subject - XML bindings handle both:
    // 1. Safety warning card visibility (bind_flag_if_eq hidden when value=0)
    // 2. Action button disabled state (bind_state_if_eq disabled when value=1)
    lv_subject_set_int(&safety_warning_visible_subject_, allowed ? 0 : 1);

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

void ExtrusionPanel::handle_amount_button(lv_obj_t* btn) {
    const char* name = lv_obj_get_name(btn);
    if (!name)
        return;

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

    spdlog::info("[{}] Extruding {}mm at {} mm/min", get_name(), selected_amount_,
                 extrusion_speed_mmpm_);

    start_extrusion_animation(true);

    if (api_) {
        // M83 = relative extrusion mode, G1 E{amount} F{speed}
        std::string gcode = fmt::format("M83\nG1 E{} F{}", selected_amount_, extrusion_speed_mmpm_);
        api_->execute_gcode(
            gcode,
            [this, amount = selected_amount_]() {
                stop_extrusion_animation();
                NOTIFY_SUCCESS("Extruded {}mm", amount);
            },
            [this](const MoonrakerError& error) {
                stop_extrusion_animation();
                NOTIFY_ERROR("Extrusion failed: {}", error.user_message());
            });
    }
}

void ExtrusionPanel::handle_retract() {
    if (!is_extrusion_allowed()) {
        NOTIFY_WARNING("Nozzle too cold for retraction ({}°C, min: {}°C)", nozzle_current_,
                       AppConstants::Temperature::MIN_EXTRUSION_TEMP);
        return;
    }

    spdlog::info("[{}] Retracting {}mm at {} mm/min", get_name(), selected_amount_,
                 extrusion_speed_mmpm_);

    start_extrusion_animation(false);

    if (api_) {
        // M83 = relative extrusion mode, G1 E-{amount} F{speed}
        std::string gcode =
            fmt::format("M83\nG1 E-{} F{}", selected_amount_, extrusion_speed_mmpm_);
        api_->execute_gcode(
            gcode,
            [this, amount = selected_amount_]() {
                stop_extrusion_animation();
                NOTIFY_SUCCESS("Retracted {}mm", amount);
            },
            [this](const MoonrakerError& error) {
                stop_extrusion_animation();
                NOTIFY_ERROR("Retraction failed: {}", error.user_message());
            });
    }
}

void ExtrusionPanel::handle_purge() {
    if (!is_extrusion_allowed()) {
        NOTIFY_WARNING("Nozzle too cold for purge ({}°C, min: {}°C)", nozzle_current_,
                       AppConstants::Temperature::MIN_EXTRUSION_TEMP);
        return;
    }

    spdlog::info("[{}] Purging {}mm at {} mm/min", get_name(), PURGE_AMOUNT_MM,
                 extrusion_speed_mmpm_);

    start_extrusion_animation(true);

    if (api_) {
        // M83 = relative extrusion mode, G1 E{amount} F{speed}
        std::string gcode = fmt::format("M83\nG1 E{} F{}", PURGE_AMOUNT_MM, extrusion_speed_mmpm_);
        api_->execute_gcode(
            gcode,
            [this]() {
                stop_extrusion_animation();
                NOTIFY_SUCCESS("Purged {}mm", PURGE_AMOUNT_MM);
            },
            [this](const MoonrakerError& error) {
                stop_extrusion_animation();
                NOTIFY_ERROR("Purge failed: {}", error.user_message());
            });
    }
}

void ExtrusionPanel::on_amount_button_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ExtrusionPanel] on_amount_button_clicked");
    auto* self = static_cast<ExtrusionPanel*>(lv_event_get_user_data(e));
    if (self) {
        lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
        self->handle_amount_button(btn);
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ExtrusionPanel::on_nozzle_temp_changed(lv_observer_t* observer, lv_subject_t* subject) {
    // Get user_data from observer (set when registering)
    auto* self = static_cast<ExtrusionPanel*>(lv_observer_get_user_data(observer));
    if (!self)
        return;

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
    return helix::ui::temperature::is_extrusion_safe(nozzle_current_,
                                                     AppConstants::Temperature::MIN_EXTRUSION_TEMP);
}

void ExtrusionPanel::set_limits(int min_temp, int max_temp) {
    nozzle_min_temp_ = min_temp;
    nozzle_max_temp_ = max_temp;
    spdlog::info("[{}] Nozzle temperature limits updated: {}-{}°C", get_name(), min_temp, max_temp);
}

void ExtrusionPanel::setup_speed_slider() {
    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_, "overlay_content");
    if (!overlay_content)
        return;

    // Store pointer and set initial value
    // Event handler is wired via XML event_cb (declarative pattern)
    speed_slider_ = lv_obj_find_by_name(overlay_content, "speed_slider");
    if (speed_slider_) {
        lv_slider_set_value(speed_slider_, extrusion_speed_mmpm_, LV_ANIM_OFF);
        spdlog::debug("[{}] Speed slider found (events wired via XML)", get_name());
    }
}

void ExtrusionPanel::update_speed_display() {
    std::snprintf(speed_display_buf_, sizeof(speed_display_buf_), "%d mm/min",
                  extrusion_speed_mmpm_);
    lv_subject_copy_string(&speed_display_subject_, speed_display_buf_);
}

void ExtrusionPanel::set_speed(int speed_mmpm) {
    extrusion_speed_mmpm_ = speed_mmpm;
    update_speed_display();
    spdlog::debug("[{}] Speed changed: {} mm/min", get_name(), extrusion_speed_mmpm_);
}

void ExtrusionPanel::setup_animation_widget() {
    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_, "overlay_content");
    if (!overlay_content)
        return;

    filament_anim_obj_ = lv_obj_find_by_name(overlay_content, "filament_animation");
    if (filament_anim_obj_) {
        spdlog::debug("[{}] Animation widget found", get_name());
    }
}

void ExtrusionPanel::start_extrusion_animation(bool is_extruding) {
    if (!filament_anim_obj_ || animation_active_)
        return;

    animation_active_ = true;

    // Make visible and set color based on direction
    lv_obj_remove_flag(filament_anim_obj_, LV_OBJ_FLAG_HIDDEN);

    // Green for extrude (pushing filament down), orange for retract (pulling up)
    lv_color_t color =
        is_extruding ? ui_theme_get_color("success_color") : ui_theme_get_color("warning_color");
    lv_obj_set_style_bg_color(filament_anim_obj_, color, 0);
    lv_obj_set_style_bg_opa(filament_anim_obj_, LV_OPA_COVER, 0);

    // Create looping animation
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, filament_anim_obj_);

    // Animate Y position to simulate flow
    if (is_extruding) {
        lv_anim_set_values(&anim, 0, 20); // Move down for extrusion
    } else {
        lv_anim_set_values(&anim, 20, 0); // Move up for retraction
    }

    lv_anim_set_duration(&anim, 400);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_set_exec_cb(&anim, [](void* obj, int32_t value) {
        lv_obj_set_style_translate_y(static_cast<lv_obj_t*>(obj), value, 0);
    });
    lv_anim_start(&anim);

    spdlog::debug("[{}] Animation started ({})", get_name(), is_extruding ? "extrude" : "retract");
}

void ExtrusionPanel::stop_extrusion_animation() {
    if (!filament_anim_obj_ || !animation_active_)
        return;

    animation_active_ = false;

    // Stop animation and hide widget
    lv_anim_delete(filament_anim_obj_, nullptr);
    lv_obj_set_style_translate_y(filament_anim_obj_, 0, 0);
    lv_obj_add_flag(filament_anim_obj_, LV_OBJ_FLAG_HIDDEN);

    spdlog::debug("[{}] Animation stopped", get_name());
}

static std::unique_ptr<ExtrusionPanel> g_extrusion_panel;

ExtrusionPanel& get_global_extrusion_panel() {
    if (!g_extrusion_panel) {
        g_extrusion_panel = std::make_unique<ExtrusionPanel>(get_printer_state(), nullptr);
    }
    return *g_extrusion_panel;
}

static std::unique_ptr<ExtrusionPanel> g_controls_extrusion_panel;

ExtrusionPanel& get_global_controls_extrusion_panel() {
    if (!g_controls_extrusion_panel) {
        g_controls_extrusion_panel = std::make_unique<ExtrusionPanel>(get_printer_state(), nullptr);
    }
    return *g_controls_extrusion_panel;
}
