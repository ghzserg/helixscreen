// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_calibration_pid.h"

#include "ui_event_safety.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_theme.h"

#include "moonraker_client.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <lvgl.h>
#include <memory>

// ============================================================================
// STATIC SUBJECT
// ============================================================================

// State subject (0=IDLE, 1=CALIBRATING, 2=SAVING, 3=COMPLETE, 4=ERROR)
static lv_subject_t s_pid_cal_state;
static bool s_callbacks_registered = false;

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

PIDCalibrationPanel::PIDCalibrationPanel() {
    // Zero out buffers
    std::memset(buf_temp_display_, 0, sizeof(buf_temp_display_));
    std::memset(buf_temp_hint_, 0, sizeof(buf_temp_hint_));
    std::memset(buf_current_temp_display_, 0, sizeof(buf_current_temp_display_));
    std::memset(buf_calibrating_heater_, 0, sizeof(buf_calibrating_heater_));
    std::memset(buf_pid_kp_, 0, sizeof(buf_pid_kp_));
    std::memset(buf_pid_ki_, 0, sizeof(buf_pid_ki_));
    std::memset(buf_pid_kd_, 0, sizeof(buf_pid_kd_));
    std::memset(buf_error_message_, 0, sizeof(buf_error_message_));

    spdlog::debug("[PIDCal] Instance created");
}

PIDCalibrationPanel::~PIDCalibrationPanel() {
    // Applying [L011]: No mutex in destructors
    // Applying [L041]: deinit_subjects() as first line in destructor
    deinit_subjects();

    // Cancel any pending timers before destruction
    cancel_pending_timers();

    // Clear widget pointers (owned by LVGL)
    overlay_root_ = nullptr;
    parent_screen_ = nullptr;
    btn_heater_extruder_ = nullptr;
    btn_heater_bed_ = nullptr;

    // Guard against static destruction order fiasco (spdlog may be gone)
    if (!StaticPanelRegistry::is_destroyed()) {
        spdlog::debug("[PIDCal] Destroyed");
    }
}

// ============================================================================
// SUBJECT REGISTRATION
// ============================================================================

void PIDCalibrationPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::debug("[PIDCal] Subjects already initialized");
        return;
    }

    spdlog::debug("[PIDCal] Initializing subjects");

    // Register state subject (shared across all instances)
    lv_subject_init_int(&s_pid_cal_state, 0);
    lv_xml_register_subject(nullptr, "pid_cal_state", &s_pid_cal_state);

    // Initialize string subjects with initial values
    lv_subject_init_string(&subj_temp_display_, buf_temp_display_, nullptr,
                           sizeof(buf_temp_display_), "200°C");
    lv_xml_register_subject(nullptr, "pid_temp_display", &subj_temp_display_);

    lv_subject_init_string(&subj_temp_hint_, buf_temp_hint_, nullptr, sizeof(buf_temp_hint_),
                           "Recommended: 200°C for extruder");
    lv_xml_register_subject(nullptr, "pid_temp_hint", &subj_temp_hint_);

    lv_subject_init_string(&subj_current_temp_display_, buf_current_temp_display_, nullptr,
                           sizeof(buf_current_temp_display_), "0.0°C / 0°C");
    lv_xml_register_subject(nullptr, "pid_current_temp", &subj_current_temp_display_);

    lv_subject_init_string(&subj_calibrating_heater_, buf_calibrating_heater_, nullptr,
                           sizeof(buf_calibrating_heater_), "Extruder PID Tuning");
    lv_xml_register_subject(nullptr, "pid_calibrating_heater", &subj_calibrating_heater_);

    lv_subject_init_string(&subj_pid_kp_, buf_pid_kp_, nullptr, sizeof(buf_pid_kp_), "0.000");
    lv_xml_register_subject(nullptr, "pid_kp", &subj_pid_kp_);

    lv_subject_init_string(&subj_pid_ki_, buf_pid_ki_, nullptr, sizeof(buf_pid_ki_), "0.000");
    lv_xml_register_subject(nullptr, "pid_ki", &subj_pid_ki_);

    lv_subject_init_string(&subj_pid_kd_, buf_pid_kd_, nullptr, sizeof(buf_pid_kd_), "0.000");
    lv_xml_register_subject(nullptr, "pid_kd", &subj_pid_kd_);

    lv_subject_init_string(&subj_error_message_, buf_error_message_, nullptr,
                           sizeof(buf_error_message_), "An error occurred during calibration.");
    lv_xml_register_subject(nullptr, "pid_error_message", &subj_error_message_);

    subjects_initialized_ = true;

    // Register XML event callbacks (once globally)
    if (!s_callbacks_registered) {
        lv_xml_register_event_cb(nullptr, "on_pid_heater_extruder", on_heater_extruder_clicked);
        lv_xml_register_event_cb(nullptr, "on_pid_heater_bed", on_heater_bed_clicked);
        lv_xml_register_event_cb(nullptr, "on_pid_temp_up", on_temp_up);
        lv_xml_register_event_cb(nullptr, "on_pid_temp_down", on_temp_down);
        lv_xml_register_event_cb(nullptr, "on_pid_start", on_start_clicked);
        lv_xml_register_event_cb(nullptr, "on_pid_abort", on_abort_clicked);
        lv_xml_register_event_cb(nullptr, "on_pid_done", on_done_clicked);
        lv_xml_register_event_cb(nullptr, "on_pid_retry", on_retry_clicked);
        s_callbacks_registered = true;
    }

    spdlog::debug("[PIDCal] Subjects and callbacks registered");
}

void PIDCalibrationPanel::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    // Deinitialize subjects to disconnect observers (Applying [L041])
    lv_subject_deinit(&s_pid_cal_state); // File-static state machine subject
    lv_subject_deinit(&subj_temp_display_);
    lv_subject_deinit(&subj_temp_hint_);
    lv_subject_deinit(&subj_current_temp_display_);
    lv_subject_deinit(&subj_calibrating_heater_);
    lv_subject_deinit(&subj_pid_kp_);
    lv_subject_deinit(&subj_pid_ki_);
    lv_subject_deinit(&subj_pid_kd_);
    lv_subject_deinit(&subj_error_message_);

    subjects_initialized_ = false;
    spdlog::debug("[PIDCal] Subjects deinitialized");
}

// ============================================================================
// CREATE / SETUP
// ============================================================================

lv_obj_t* PIDCalibrationPanel::create(lv_obj_t* parent) {
    if (overlay_root_) {
        spdlog::debug("[PIDCal] Overlay already created");
        return overlay_root_;
    }

    parent_screen_ = parent;

    spdlog::debug("[PIDCal] Creating overlay from XML");

    // Create from XML
    overlay_root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "calibration_pid_panel", nullptr));
    if (!overlay_root_) {
        spdlog::error("[PIDCal] Failed to create panel from XML");
        return nullptr;
    }

    // Initially hidden (will be shown by show())
    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    // Setup widget references
    setup_widgets();

    spdlog::info("[PIDCal] Overlay created");
    return overlay_root_;
}

void PIDCalibrationPanel::setup_widgets() {
    if (!overlay_root_) {
        spdlog::error("[PIDCal] NULL overlay_root_");
        return;
    }

    // Find widgets in idle state (for heater selection styling)
    btn_heater_extruder_ = lv_obj_find_by_name(overlay_root_, "btn_heater_extruder");
    btn_heater_bed_ = lv_obj_find_by_name(overlay_root_, "btn_heater_bed");

    // Event callbacks are registered via XML <event_cb> elements
    // State visibility is controlled via subject binding in XML

    // Set initial state
    set_state(State::IDLE);
    update_heater_selection();
    update_temp_display();
    update_temp_hint();

    spdlog::debug("[PIDCal] Widget setup complete");
}

// ============================================================================
// SHOW
// ============================================================================

void PIDCalibrationPanel::show() {
    if (!overlay_root_) {
        spdlog::error("[PIDCal] Cannot show: overlay not created");
        return;
    }

    spdlog::debug("[PIDCal] Showing overlay");

    // Register with NavigationManager for lifecycle callbacks
    NavigationManager::instance().register_overlay_instance(overlay_root_, this);

    // Push onto navigation stack - on_activate() will be called by NavigationManager
    ui_nav_push_overlay(overlay_root_);

    spdlog::info("[PIDCal] Overlay shown");
}

// ============================================================================
// LIFECYCLE HOOKS
// ============================================================================

void PIDCalibrationPanel::on_activate() {
    // Call base class first
    OverlayBase::on_activate();

    spdlog::debug("[PIDCal] on_activate()");

    // Reset to idle state with default values
    set_state(State::IDLE);
    selected_heater_ = Heater::EXTRUDER;
    target_temp_ = EXTRUDER_DEFAULT_TEMP;

    update_heater_selection();
    update_temp_display();
    update_temp_hint();
}

void PIDCalibrationPanel::on_deactivate() {
    spdlog::debug("[PIDCal] on_deactivate()");

    // Cancel pending timers to prevent use-after-free
    cancel_pending_timers();

    // If calibration is in progress, abort it
    if (state_ == State::CALIBRATING) {
        spdlog::info("[PIDCal] Aborting calibration on deactivate");
        if (client_) {
            client_->gcode_script("TURN_OFF_HEATERS");
        }
    }

    // Call base class
    OverlayBase::on_deactivate();
}

void PIDCalibrationPanel::cleanup() {
    spdlog::debug("[PIDCal] Cleaning up");

    // Unregister from NavigationManager before cleaning up
    if (overlay_root_) {
        NavigationManager::instance().unregister_overlay_instance(overlay_root_);
    }

    // Cancel any pending timers
    cancel_pending_timers();

    // Call base class to set cleanup_called_ flag
    OverlayBase::cleanup();

    // Clear references
    parent_screen_ = nullptr;
    btn_heater_extruder_ = nullptr;
    btn_heater_bed_ = nullptr;
}

// ============================================================================
// TIMER MANAGEMENT
// ============================================================================

void PIDCalibrationPanel::cancel_pending_timers() {
    if (calibrate_timer_) {
        lv_timer_delete(calibrate_timer_);
        calibrate_timer_ = nullptr;
        spdlog::debug("[PIDCal] Cancelled calibrate timer");
    }
    if (save_timer_) {
        lv_timer_delete(save_timer_);
        save_timer_ = nullptr;
        spdlog::debug("[PIDCal] Cancelled save timer");
    }
}

// ============================================================================
// STATE MANAGEMENT
// ============================================================================

void PIDCalibrationPanel::set_state(State new_state) {
    spdlog::debug("[PIDCal] State change: {} -> {}", static_cast<int>(state_),
                  static_cast<int>(new_state));
    state_ = new_state;

    // Update subject - XML bindings handle visibility automatically
    // State mapping: 0=IDLE, 1=CALIBRATING, 2=SAVING, 3=COMPLETE, 4=ERROR
    lv_subject_set_int(&s_pid_cal_state, static_cast<int>(new_state));
}

// ============================================================================
// UI UPDATES
// ============================================================================

void PIDCalibrationPanel::update_heater_selection() {
    if (!btn_heater_extruder_ || !btn_heater_bed_)
        return;

    // Use background color to indicate selection
    lv_color_t selected_color = ui_theme_get_color("primary_color");
    lv_color_t neutral_color = ui_theme_get_color("theme_grey");

    if (selected_heater_ == Heater::EXTRUDER) {
        lv_obj_set_style_bg_color(btn_heater_extruder_, selected_color, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn_heater_bed_, neutral_color, LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(btn_heater_extruder_, neutral_color, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn_heater_bed_, selected_color, LV_PART_MAIN);
    }
}

void PIDCalibrationPanel::update_temp_display() {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d°C", target_temp_);
    lv_subject_copy_string(&subj_temp_display_, buf);
}

void PIDCalibrationPanel::update_temp_hint() {
    const char* hint = (selected_heater_ == Heater::EXTRUDER) ? "Recommended: 200°C for extruder"
                                                              : "Recommended: 60°C for heated bed";
    lv_subject_copy_string(&subj_temp_hint_, hint);
}

void PIDCalibrationPanel::update_temperature(float current, float target) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f°C / %.0f°C", current, target);
    lv_subject_copy_string(&subj_current_temp_display_, buf);
}

// ============================================================================
// GCODE COMMANDS
// ============================================================================

void PIDCalibrationPanel::send_pid_calibrate() {
    if (!client_) {
        spdlog::error("[PIDCal] No Moonraker client");
        on_calibration_result(false, 0, 0, 0, "No printer connection");
        return;
    }

    const char* heater_name = (selected_heater_ == Heater::EXTRUDER) ? "extruder" : "heater_bed";

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "PID_CALIBRATE HEATER=%s TARGET=%d", heater_name, target_temp_);

    spdlog::info("[PIDCal] Sending: {}", cmd);
    int result = client_->gcode_script(cmd);
    if (result <= 0) {
        spdlog::error("[PIDCal] Failed to send PID_CALIBRATE");
        on_calibration_result(false, 0, 0, 0, "Failed to start calibration");
    }

    // Update calibrating state label
    const char* label =
        (selected_heater_ == Heater::EXTRUDER) ? "Extruder PID Tuning" : "Heated Bed PID Tuning";
    lv_subject_copy_string(&subj_calibrating_heater_, label);

    // For demo purposes, simulate completion after a delay
    // In real implementation, this would be triggered by Moonraker events
    // Applying [L012]: Guard async callbacks
    calibrate_timer_ = lv_timer_create(
        [](lv_timer_t* t) {
            auto* self = static_cast<PIDCalibrationPanel*>(lv_timer_get_user_data(t));
            if (!self || self->cleanup_called()) {
                // Panel was destroyed or cleaning up, just delete timer
                lv_timer_delete(t);
                return;
            }
            // Clear our timer pointer before deleting
            self->calibrate_timer_ = nullptr;
            if (self->get_state() == State::CALIBRATING) {
                // Simulate successful calibration with typical values
                self->on_calibration_result(true, 22.865f, 1.292f, 101.178f);
            }
            lv_timer_delete(t);
        },
        5000, this); // 5 second delay to simulate calibration
    lv_timer_set_repeat_count(calibrate_timer_, 1);
}

void PIDCalibrationPanel::send_save_config() {
    if (!client_)
        return;

    spdlog::info("[PIDCal] Sending SAVE_CONFIG");
    int result = client_->gcode_script("SAVE_CONFIG");
    if (result <= 0) {
        spdlog::error("[PIDCal] Failed to send SAVE_CONFIG");
        on_calibration_result(false, 0, 0, 0, "Failed to save configuration");
        return;
    }

    // Simulate save completing
    // Applying [L012]: Guard async callbacks
    save_timer_ = lv_timer_create(
        [](lv_timer_t* t) {
            auto* self = static_cast<PIDCalibrationPanel*>(lv_timer_get_user_data(t));
            if (!self || self->cleanup_called()) {
                // Panel was destroyed or cleaning up, just delete timer
                lv_timer_delete(t);
                return;
            }
            // Clear our timer pointer before deleting
            self->save_timer_ = nullptr;
            if (self->get_state() == State::SAVING) {
                self->set_state(State::COMPLETE);
            }
            lv_timer_delete(t);
        },
        2000, this);
    lv_timer_set_repeat_count(save_timer_, 1);
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

void PIDCalibrationPanel::handle_heater_extruder_clicked() {
    if (state_ != State::IDLE)
        return;

    spdlog::debug("[PIDCal] Extruder selected");
    selected_heater_ = Heater::EXTRUDER;
    target_temp_ = EXTRUDER_DEFAULT_TEMP;
    update_heater_selection();
    update_temp_display();
    update_temp_hint();
}

void PIDCalibrationPanel::handle_heater_bed_clicked() {
    if (state_ != State::IDLE)
        return;

    spdlog::debug("[PIDCal] Heated bed selected");
    selected_heater_ = Heater::BED;
    target_temp_ = BED_DEFAULT_TEMP;
    update_heater_selection();
    update_temp_display();
    update_temp_hint();
}

void PIDCalibrationPanel::handle_temp_up() {
    if (state_ != State::IDLE)
        return;

    int max_temp = (selected_heater_ == Heater::EXTRUDER) ? EXTRUDER_MAX_TEMP : BED_MAX_TEMP;

    if (target_temp_ < max_temp) {
        target_temp_ += 5;
        update_temp_display();
    }
}

void PIDCalibrationPanel::handle_temp_down() {
    if (state_ != State::IDLE)
        return;

    int min_temp = (selected_heater_ == Heater::EXTRUDER) ? EXTRUDER_MIN_TEMP : BED_MIN_TEMP;

    if (target_temp_ > min_temp) {
        target_temp_ -= 5;
        update_temp_display();
    }
}

void PIDCalibrationPanel::handle_start_clicked() {
    spdlog::debug("[PIDCal] Start clicked");
    set_state(State::CALIBRATING);
    send_pid_calibrate();
}

void PIDCalibrationPanel::handle_abort_clicked() {
    spdlog::debug("[PIDCal] Abort clicked");
    // Send TURN_OFF_HEATERS to abort
    if (client_) {
        client_->gcode_script("TURN_OFF_HEATERS");
    }
    set_state(State::IDLE);
}

void PIDCalibrationPanel::handle_done_clicked() {
    spdlog::debug("[PIDCal] Done clicked");
    set_state(State::IDLE);
    ui_nav_go_back();
}

void PIDCalibrationPanel::handle_retry_clicked() {
    spdlog::debug("[PIDCal] Retry clicked");
    set_state(State::IDLE);
}

// ============================================================================
// PUBLIC METHODS
// ============================================================================

void PIDCalibrationPanel::on_calibration_result(bool success, float kp, float ki, float kd,
                                                const std::string& error_message) {
    if (success) {
        // Store results
        result_kp_ = kp;
        result_ki_ = ki;
        result_kd_ = kd;

        // Update display using subjects
        char buf[16];
        snprintf(buf, sizeof(buf), "%.3f", kp);
        lv_subject_copy_string(&subj_pid_kp_, buf);

        snprintf(buf, sizeof(buf), "%.3f", ki);
        lv_subject_copy_string(&subj_pid_ki_, buf);

        snprintf(buf, sizeof(buf), "%.3f", kd);
        lv_subject_copy_string(&subj_pid_kd_, buf);

        // Save config (will transition to COMPLETE when done)
        set_state(State::SAVING);
        send_save_config();
    } else {
        lv_subject_copy_string(&subj_error_message_, error_message.c_str());
        set_state(State::ERROR);
    }
}

// ============================================================================
// STATIC TRAMPOLINES (for XML event_cb)
// ============================================================================

void PIDCalibrationPanel::on_heater_extruder_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PIDCal] on_heater_extruder_clicked");
    (void)e;
    get_global_pid_cal_panel().handle_heater_extruder_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void PIDCalibrationPanel::on_heater_bed_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PIDCal] on_heater_bed_clicked");
    (void)e;
    get_global_pid_cal_panel().handle_heater_bed_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void PIDCalibrationPanel::on_temp_up(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PIDCal] on_temp_up");
    (void)e;
    get_global_pid_cal_panel().handle_temp_up();
    LVGL_SAFE_EVENT_CB_END();
}

void PIDCalibrationPanel::on_temp_down(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PIDCal] on_temp_down");
    (void)e;
    get_global_pid_cal_panel().handle_temp_down();
    LVGL_SAFE_EVENT_CB_END();
}

void PIDCalibrationPanel::on_start_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PIDCal] on_start_clicked");
    (void)e;
    get_global_pid_cal_panel().handle_start_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void PIDCalibrationPanel::on_abort_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PIDCal] on_abort_clicked");
    (void)e;
    get_global_pid_cal_panel().handle_abort_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void PIDCalibrationPanel::on_done_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PIDCal] on_done_clicked");
    (void)e;
    get_global_pid_cal_panel().handle_done_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void PIDCalibrationPanel::on_retry_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PIDCal] on_retry_clicked");
    (void)e;
    get_global_pid_cal_panel().handle_retry_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================

static std::unique_ptr<PIDCalibrationPanel> g_pid_cal_panel;

PIDCalibrationPanel& get_global_pid_cal_panel() {
    if (!g_pid_cal_panel) {
        g_pid_cal_panel = std::make_unique<PIDCalibrationPanel>();
        StaticPanelRegistry::instance().register_destroy("PIDCalibrationPanel",
                                                         []() { g_pid_cal_panel.reset(); });
    }
    return *g_pid_cal_panel;
}

void destroy_pid_cal_panel() {
    g_pid_cal_panel.reset();
}
