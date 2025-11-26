// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_panel_base.h"

/**
 * @file ui_panel_print_status.h
 * @brief Print status panel - shows active print progress and controls
 *
 * Displays comprehensive print status including:
 * - Filename and thumbnail
 * - Progress bar and percentage
 * - Layer count (current / total)
 * - Elapsed and remaining time
 * - Nozzle and bed temperatures
 * - Speed and flow percentages
 * - Pause/Resume, Tune, and Cancel buttons
 *
 * ## Phase 5 Migration - High Complexity Panel:
 *
 * This panel has the most subjects (10) and includes mock print simulation
 * for testing without a real printer connection.
 *
 * Key patterns:
 * - 10 reactive subjects for all display fields
 * - Resize callback registration for responsive thumbnail scaling
 * - Mock print simulation with tick-based progression
 * - State machine for print lifecycle
 *
 * ## Reactive Subjects (owned by this panel):
 * - `print_filename` - Current print filename
 * - `print_progress_text` - Progress percentage text (e.g., "45%")
 * - `print_layer_text` - Layer text (e.g., "Layer 42 / 100")
 * - `print_elapsed` - Elapsed time (e.g., "1h 23m")
 * - `print_remaining` - Remaining time (e.g., "2h 45m")
 * - `nozzle_temp_text` - Nozzle temperature (e.g., "215 / 215°C")
 * - `bed_temp_text` - Bed temperature (e.g., "60 / 60°C")
 * - `print_speed_text` - Speed percentage (e.g., "100%")
 * - `print_flow_text` - Flow percentage (e.g., "100%")
 * - `pause_button_text` - Pause/Resume button label
 *
 * @see PanelBase for base class documentation
 */

/**
 * @brief Print state machine states
 */
enum class PrintState {
    Idle,       ///< No active print
    Printing,   ///< Actively printing
    Paused,     ///< Print paused
    Complete,   ///< Print finished successfully
    Cancelled,  ///< Print cancelled by user
    Error       ///< Print failed with error
};

// Legacy C-style enum for backwards compatibility
typedef enum {
    PRINT_STATE_IDLE = static_cast<int>(PrintState::Idle),
    PRINT_STATE_PRINTING = static_cast<int>(PrintState::Printing),
    PRINT_STATE_PAUSED = static_cast<int>(PrintState::Paused),
    PRINT_STATE_COMPLETE = static_cast<int>(PrintState::Complete),
    PRINT_STATE_CANCELLED = static_cast<int>(PrintState::Cancelled),
    PRINT_STATE_ERROR = static_cast<int>(PrintState::Error)
} print_state_t;

class PrintStatusPanel : public PanelBase {
  public:
    /**
     * @brief Construct PrintStatusPanel with injected dependencies
     *
     * @param printer_state Reference to PrinterState
     * @param api Pointer to MoonrakerAPI (for pause/cancel commands)
     */
    PrintStatusPanel(PrinterState& printer_state, MoonrakerAPI* api);

    ~PrintStatusPanel() override;

    //
    // === PanelBase Implementation ===
    //

    /**
     * @brief Initialize subjects for XML binding
     *
     * Registers all 10 subjects for reactive data binding.
     */
    void init_subjects() override;

    /**
     * @brief Setup button handlers and image scaling
     *
     * - Wires pause, tune, cancel, light buttons
     * - Wires temperature card click handlers
     * - Configures progress bar
     * - Registers resize callback for thumbnail scaling
     *
     * @param panel Root panel object from lv_xml_create()
     * @param parent_screen Parent screen for navigation
     */
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;

    const char* get_name() const override { return "Print Status"; }
    const char* get_xml_component_name() const override { return "print_status_panel"; }

    //
    // === Public API - Print State Updates ===
    //

    /**
     * @brief Set the current print filename
     * @param filename Print file name to display
     */
    void set_filename(const char* filename);

    /**
     * @brief Set print progress percentage
     * @param percent Progress 0-100 (clamped to valid range)
     */
    void set_progress(int percent);

    /**
     * @brief Set layer progress
     * @param current Current layer number
     * @param total Total layers in print
     */
    void set_layer(int current, int total);

    /**
     * @brief Set elapsed and remaining time
     * @param elapsed_secs Elapsed time in seconds
     * @param remaining_secs Estimated remaining time in seconds
     */
    void set_times(int elapsed_secs, int remaining_secs);

    /**
     * @brief Set temperature readings
     * @param nozzle_cur Current nozzle temperature
     * @param nozzle_tgt Target nozzle temperature
     * @param bed_cur Current bed temperature
     * @param bed_tgt Target bed temperature
     */
    void set_temperatures(int nozzle_cur, int nozzle_tgt, int bed_cur, int bed_tgt);

    /**
     * @brief Set speed and flow percentages
     * @param speed_pct Speed multiplier percentage
     * @param flow_pct Flow multiplier percentage
     */
    void set_speeds(int speed_pct, int flow_pct);

    /**
     * @brief Set print state
     * @param state New print state
     */
    void set_state(PrintState state);

    /**
     * @brief Get current print state
     * @return Current PrintState
     */
    PrintState get_state() const { return current_state_; }

    /**
     * @brief Get current progress percentage
     * @return Progress 0-100
     */
    int get_progress() const { return current_progress_; }

    //
    // === Mock Print Simulation ===
    //

    /**
     * @brief Start a simulated print for testing
     *
     * @param filename Display name for the mock print
     * @param layers Total layers to simulate
     * @param duration_secs Total simulated print time
     */
    void start_mock_print(const char* filename, int layers, int duration_secs);

    /**
     * @brief Stop the mock print simulation
     */
    void stop_mock_print();

    /**
     * @brief Advance mock print by one tick (call periodically)
     *
     * Updates progress, layer, time, and temperature simulation.
     * Automatically completes when elapsed >= duration.
     */
    void tick_mock_print();

    /**
     * @brief Check if mock print is active
     * @return true if mock simulation is running
     */
    bool is_mock_active() const { return mock_active_; }

  private:
    //
    // === Subjects (owned by this panel) ===
    //

    lv_subject_t filename_subject_;
    lv_subject_t progress_text_subject_;
    lv_subject_t layer_text_subject_;
    lv_subject_t elapsed_subject_;
    lv_subject_t remaining_subject_;
    lv_subject_t nozzle_temp_subject_;
    lv_subject_t bed_temp_subject_;
    lv_subject_t speed_subject_;
    lv_subject_t flow_subject_;
    lv_subject_t pause_button_subject_;

    // Subject storage buffers
    char filename_buf_[128] = "No print active";
    char progress_text_buf_[32] = "0%";
    char layer_text_buf_[64] = "Layer 0 / 0";
    char elapsed_buf_[32] = "0h 00m";
    char remaining_buf_[32] = "0h 00m";
    char nozzle_temp_buf_[32] = "0 / 0°C";
    char bed_temp_buf_[32] = "0 / 0°C";
    char speed_buf_[32] = "100%";
    char flow_buf_[32] = "100%";
    char pause_button_buf_[32] = "Pause";

    //
    // === Instance State ===
    //

    PrintState current_state_ = PrintState::Idle;
    int current_progress_ = 0;
    int current_layer_ = 0;
    int total_layers_ = 0;
    int elapsed_seconds_ = 0;
    int remaining_seconds_ = 0;
    int nozzle_current_ = 0;
    int nozzle_target_ = 0;
    int bed_current_ = 0;
    int bed_target_ = 0;
    int speed_percent_ = 100;
    int flow_percent_ = 100;

    // Mock simulation state
    bool mock_active_ = false;
    int mock_total_seconds_ = 0;
    int mock_elapsed_seconds_ = 0;
    int mock_total_layers_ = 0;

    // Child widgets
    lv_obj_t* progress_bar_ = nullptr;

    // Resize callback registration flag
    bool resize_registered_ = false;

    //
    // === Private Helpers ===
    //

    void update_all_displays();
    void scale_thumbnail_images();

    static void format_time(int seconds, char* buf, size_t buf_size);

    //
    // === Instance Handlers ===
    //

    void handle_nozzle_card_click();
    void handle_bed_card_click();
    void handle_light_button();
    void handle_pause_button();
    void handle_tune_button();
    void handle_cancel_button();
    void handle_resize();

    //
    // === Static Trampolines ===
    //

    static void on_nozzle_card_clicked(lv_event_t* e);
    static void on_bed_card_clicked(lv_event_t* e);
    static void on_light_clicked(lv_event_t* e);
    static void on_pause_clicked(lv_event_t* e);
    static void on_tune_clicked(lv_event_t* e);
    static void on_cancel_clicked(lv_event_t* e);

    // Static resize callback (registered with ui_resize_handler)
    static void on_resize_static();
};


// Global instance accessor (needed by main.cpp)
PrintStatusPanel& get_global_print_status_panel();

// Temporary wrapper for tick function (still called by main.cpp)
void ui_panel_print_status_tick_mock_print();
