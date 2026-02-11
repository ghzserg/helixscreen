// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_temp_graph.h"

#include "overlay_base.h"
#include "subject_managed_panel.h"

#include <lvgl.h>
#include <string>

class MoonrakerAPI;
class TempControlPanel;

/**
 * @file ui_panel_calibration_pid.h
 * @brief PID Tuning Calibration Panel
 *
 * Interactive calibration using Klipper's PID_CALIBRATE command.
 * Supports both extruder and heated bed PID tuning.
 *
 * ## Klipper Commands Used:
 * - `PID_CALIBRATE HEATER=extruder TARGET=<temp>` - Extruder tuning
 * - `PID_CALIBRATE HEATER=heater_bed TARGET=<temp>` - Bed tuning
 * - `SAVE_CONFIG` - Persist results (restarts Klipper)
 *
 * ## State Machine:
 * IDLE → CALIBRATING → SAVING → COMPLETE
 *                   ↘ ERROR
 *
 * ## Typical Duration:
 * - Extruder: 3-5 minutes
 * - Heated Bed: 5-10 minutes (larger thermal mass)
 */
class PIDCalibrationPanel : public OverlayBase {
  public:
    /**
     * @brief Calibration state machine states
     */
    enum class State {
        IDLE,        ///< Ready to start, heater selection shown
        CALIBRATING, ///< PID_CALIBRATE running, showing progress
        SAVING,      ///< SAVE_CONFIG running, Klipper restarting
        COMPLETE,    ///< Calibration successful, showing results
        ERROR        ///< Something went wrong
    };

    /**
     * @brief Which heater is being calibrated
     */
    enum class Heater { EXTRUDER, BED };

    PIDCalibrationPanel();
    ~PIDCalibrationPanel() override;

    //
    // === OverlayBase Interface ===
    //

    /**
     * @brief Initialize LVGL subjects for XML data binding
     *
     * Call once at startup before any panel instances are created.
     * Registers the pid_cal_state subject and all XML event callbacks.
     */
    void init_subjects() override;

    /**
     * @brief Deinitialize LVGL subjects for clean shutdown
     *
     * Disconnects all observers and deinitializes subjects.
     * Called automatically by destructor, but can be called earlier
     * for explicit cleanup before LVGL deinit.
     */
    void deinit_subjects();

    /**
     * @brief Create overlay UI from XML
     *
     * @param parent Parent screen widget to attach overlay to
     * @return Root object of overlay, or nullptr on failure
     */
    lv_obj_t* create(lv_obj_t* parent) override;

    /**
     * @brief Get human-readable overlay name
     * @return "PID Calibration"
     */
    const char* get_name() const override {
        return "PID Calibration";
    }

    /**
     * @brief Called when overlay becomes visible
     *
     * Resets state to IDLE, refreshes UI to defaults.
     */
    void on_activate() override;

    /**
     * @brief Called when overlay is being hidden
     *
     * Cancels pending timers, aborts calibration if in progress.
     */
    void on_deactivate() override;

    /**
     * @brief Clean up resources for async-safe destruction
     */
    void cleanup() override;

    //
    // === Public API ===
    //

    /**
     * @brief Show overlay panel
     *
     * Pushes overlay onto navigation stack and registers with NavigationManager.
     * on_activate() will be called automatically after animation completes.
     */
    void show();

    /**
     * @brief Set the Moonraker API for high-level operations
     *
     * @param api MoonrakerAPI for PID calibrate and save_config
     */
    void set_api(MoonrakerAPI* api) {
        api_ = api;
    }

    /**
     * @brief Get current state
     */
    State get_state() const {
        return state_;
    }

    /**
     * @brief Set TempControlPanel for graph registration
     *
     * @param tcp TempControlPanel that manages temperature graph updates
     */
    void set_temp_control_panel(TempControlPanel* tcp);

    /**
     * @brief Called when calibration completes with results
     *
     * @param success True if calibration succeeded
     * @param kp Proportional gain (only valid if success)
     * @param ki Integral gain (only valid if success)
     * @param kd Derivative gain (only valid if success)
     * @param error_message Error description (only valid if !success)
     */
    void on_calibration_result(bool success, float kp = 0, float ki = 0, float kd = 0,
                               const std::string& error_message = "");

  private:
    // Client/API references
    // Note: overlay_root_ inherited from OverlayBase
    lv_obj_t* parent_screen_ = nullptr;
    MoonrakerAPI* api_ = nullptr;

    // State
    State state_ = State::IDLE;
    Heater selected_heater_ = Heater::EXTRUDER;
    int target_temp_ = 200;         // Default for extruder
    int fan_speed_ = 0;             // Part cooling fan speed for extruder PID
    std::string selected_material_; // Active material preset name

    // Temperature limits
    static constexpr int EXTRUDER_MIN_TEMP = 150;
    static constexpr int EXTRUDER_MAX_TEMP = 280;
    static constexpr int EXTRUDER_DEFAULT_TEMP = 200;
    static constexpr int BED_MIN_TEMP = 40;
    static constexpr int BED_MAX_TEMP = 110;
    static constexpr int BED_DEFAULT_TEMP = 60;

    // PID results
    float result_kp_ = 0;
    float result_ki_ = 0;
    float result_kd_ = 0;

    // Previous PID values (fetched before calibration starts)
    float old_kp_ = 0;
    float old_ki_ = 0;
    float old_kd_ = 0;
    bool has_old_values_ = false;

    // Subject manager for automatic cleanup
    SubjectManager subjects_;

    // String subjects and buffers for reactive text updates
    lv_subject_t subj_temp_display_;
    char buf_temp_display_[16];

    lv_subject_t subj_temp_hint_;
    char buf_temp_hint_[64];

    lv_subject_t subj_calibrating_heater_;
    char buf_calibrating_heater_[32];

    lv_subject_t subj_pid_kp_;
    char buf_pid_kp_[32];

    lv_subject_t subj_pid_ki_;
    char buf_pid_ki_[32];

    lv_subject_t subj_pid_kd_;
    char buf_pid_kd_[32];

    lv_subject_t subj_result_summary_;
    char buf_result_summary_[128];

    lv_subject_t subj_error_message_;
    char buf_error_message_[256];

    // Int subject for showing/hiding extruder-only sections
    lv_subject_t subj_heater_is_extruder_;

    // Int subject: 1 when not idle (disables Start button in header)
    lv_subject_t subj_cal_not_idle_;

    // Progress tracking for calibration
    lv_subject_t subj_pid_progress_; // int 0-100
    lv_subject_t subj_pid_progress_text_;
    char buf_pid_progress_text_[32];
    int pid_estimated_total_ = 3;      // Dynamic estimate, starts at 3
    bool has_kalico_progress_ = false; // True once first sample callback arrives

    // Fallback progress timer for standard Klipper (no sample callbacks)
    lv_timer_t* progress_fallback_timer_ = nullptr;
    int fallback_cycle_ = 0;
    void start_fallback_progress_timer();
    void stop_fallback_progress_timer();
    static void on_fallback_progress_tick(lv_timer_t* timer);

    // Progress handler (called from UI thread via queue)
    void on_pid_progress(int sample, float tolerance);

    // Widget references
    lv_obj_t* fan_slider_ = nullptr;
    lv_obj_t* fan_speed_label_ = nullptr;

    // Temperature graph for calibrating state
    TempControlPanel* temp_control_panel_ = nullptr;
    ui_temp_graph_t* pid_graph_ = nullptr;
    int pid_graph_series_id_ = -1;

    // State management
    void set_state(State new_state);

    // Fan control
    void turn_off_fan();

    // UI setup (called by create())
    void setup_widgets();

    // Temperature graph management
    void setup_pid_graph();
    void teardown_pid_graph();

    // UI updates
    void update_fan_slider(int speed);
    void update_temp_display();
    void update_temp_hint();

    // G-code commands
    void send_pid_calibrate();
    void send_save_config();
    void fetch_old_pid_values();

    // Event handlers
    void handle_heater_extruder_clicked();
    void handle_heater_bed_clicked();
    void handle_temp_up();
    void handle_temp_down();
    void handle_start_clicked();
    void handle_abort_clicked();
    void handle_done_clicked();
    void handle_retry_clicked();
    void handle_preset_clicked(int temp, const char* material_name);

    // Static trampolines
    static void on_heater_extruder_clicked(lv_event_t* e);
    static void on_heater_bed_clicked(lv_event_t* e);
    static void on_temp_up(lv_event_t* e);
    static void on_temp_down(lv_event_t* e);
    static void on_start_clicked(lv_event_t* e);
    static void on_abort_clicked(lv_event_t* e);
    static void on_done_clicked(lv_event_t* e);
    static void on_retry_clicked(lv_event_t* e);
    static void on_fan_slider_changed(lv_event_t* e);
    // Material preset trampolines (extruder)
    static void on_pid_preset_pla(lv_event_t* e);
    static void on_pid_preset_petg(lv_event_t* e);
    static void on_pid_preset_abs(lv_event_t* e);
    static void on_pid_preset_pa(lv_event_t* e);
    static void on_pid_preset_tpu(lv_event_t* e);
    // Material preset trampolines (bed)
    static void on_pid_preset_bed_pla(lv_event_t* e);
    static void on_pid_preset_bed_petg(lv_event_t* e);
    static void on_pid_preset_bed_abs(lv_event_t* e);
};

// Global instance accessor
PIDCalibrationPanel& get_global_pid_cal_panel();

// Destroy the global instance (call during shutdown)
void destroy_pid_cal_panel();
