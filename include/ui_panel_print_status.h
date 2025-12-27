// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_modal.h"
#include "ui_observer_guard.h"
#include "ui_panel_base.h"

#include "printer_state.h"

#include <functional>
#include <string>
#include <unordered_set>

// Forward declarations
class TempControlPanel;
class PrintStatusPanel;

/**
 * @brief Confirmation dialog for canceling an active print
 *
 * Uses Modal for RAII lifecycle - dialog auto-hides when object is destroyed.
 * Shows a warning that all progress will be lost.
 *
 * Usage:
 *   cancel_modal_.set_on_confirm([this]() { execute_cancel_print(); });
 *   cancel_modal_.show(lv_screen_active());
 */
class PrintCancelModal : public Modal {
  public:
    using ConfirmCallback = std::function<void()>;

    const char* get_name() const override {
        return "Print Cancel";
    }
    const char* component_name() const override {
        return "print_cancel_confirm_modal";
    }

    void set_on_confirm(ConfirmCallback cb) {
        on_confirm_cb_ = std::move(cb);
    }

  protected:
    void on_show() override {
        wire_ok_button();     // "Stop" button
        wire_cancel_button(); // "Keep Printing" button
    }

    void on_ok() override {
        if (on_confirm_cb_) {
            on_confirm_cb_();
        }
        hide();
    }

  private:
    ConfirmCallback on_confirm_cb_;
};

/**
 * @brief Warning modal for saving Z-offset during print
 *
 * SAVE_CONFIG restarts Klipper and will CANCEL any active print!
 * Shows a strong warning with cancel/confirm options.
 */
class SaveZOffsetModal : public Modal {
  public:
    using ConfirmCallback = std::function<void()>;

    const char* get_name() const override {
        return "Save Z-Offset";
    }
    const char* component_name() const override {
        return "save_z_offset_modal";
    }

    void set_on_confirm(ConfirmCallback cb) {
        on_confirm_cb_ = std::move(cb);
    }

  protected:
    void on_show() override {
        wire_ok_button();     // "Save & Restart" button
        wire_cancel_button(); // "Cancel" button
    }

    void on_ok() override {
        if (on_confirm_cb_) {
            on_confirm_cb_();
        }
        hide();
    }

  private:
    ConfirmCallback on_confirm_cb_;
};

/**
 * @brief Print status panel - shows active print progress and controls
 *
 * Displays filename, thumbnail, progress, layers, times, temperatures,
 * speed/flow, and provides pause/tune/cancel buttons.
 */

/**
 * @brief Print state machine states
 */
enum class PrintState {
    Idle,      ///< No active print
    Preparing, ///< Running pre-print operations (homing, leveling, etc.)
    Printing,  ///< Actively printing
    Paused,    ///< Print paused
    Complete,  ///< Print finished successfully
    Cancelled, ///< Print cancelled by user
    Error      ///< Print failed with error
};

// Legacy C-style enum for backwards compatibility
typedef enum {
    PRINT_STATE_IDLE = static_cast<int>(PrintState::Idle),
    PRINT_STATE_PREPARING = static_cast<int>(PrintState::Preparing),
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

    /**
     * @brief Called when panel becomes visible
     *
     * Resumes G-code viewer rendering if viewer mode is active.
     */
    void on_activate() override;

    /**
     * @brief Called when panel is hidden
     *
     * Pauses G-code viewer rendering to save CPU cycles.
     */
    void on_deactivate() override;

    const char* get_name() const override {
        return "Print Status";
    }
    const char* get_xml_component_name() const override {
        return "print_status_panel";
    }

    //
    // === Public API - Print State Updates ===
    //

    /**
     * @brief Set the current print filename
     * @param filename Print file name to display
     */
    void set_filename(const char* filename);

    /**
     * @brief Set the original filename for thumbnail loading
     *
     * Use this when starting a print with a modified temp file. The panel will
     * use this filename (instead of the temp file path) for thumbnail lookup.
     * Cleared automatically when print ends or is cancelled.
     *
     * @param filename Original filename (e.g., "3DBenchy.gcode")
     */
    void set_thumbnail_source(const std::string& filename);

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
    PrintState get_state() const {
        return current_state_;
    }

    //
    // === Pre-Print Preparation State ===
    //

    /**
     * @brief Set the preparing state with operation details
     *
     * Call this when starting a pre-print sequence. The panel will show
     * "Preparing..." with the current operation name and progress.
     *
     * @param operation_name Display name of current operation (e.g., "Homing", "Leveling Bed")
     * @param current_step Current step number (1-based)
     * @param total_steps Total number of steps
     */
    void set_preparing(const std::string& operation_name, int current_step, int total_steps);

    /**
     * @brief Update preparing progress
     *
     * Lighter-weight update for progress during a single operation.
     *
     * @param progress Fractional progress (0.0 to 1.0)
     */
    void set_preparing_progress(float progress);

    /**
     * @brief Clear preparing state and transition to Idle or Printing
     *
     * Call this when the pre-print sequence completes or is cancelled.
     *
     * @param success If true, transitions to Printing; if false, transitions to Idle
     */
    void end_preparing(bool success);

    /**
     * @brief Get current progress percentage
     * @return Progress 0-100
     */
    int get_progress() const {
        return current_progress_;
    }

    /**
     * @brief Set reference to TempControlPanel for temperature overlays
     *
     * Must be called before temp card click handlers can work.
     * @param temp_panel Pointer to shared TempControlPanel instance
     */
    void set_temp_control_panel(TempControlPanel* temp_panel);

    /**
     * @brief Auto-configure LED if not explicitly set in config
     *
     * Called after hardware discovery completes. If no LED is configured
     * but LEDs were detected, automatically selects the first one.
     *
     * @param leds Vector of discovered LED names (e.g., "neopixel chamber_light")
     */
    void auto_configure_led_if_needed(const std::vector<std::string>& leds);

    //
    // === Tune Panel Handlers (called by XML event callbacks) ===
    //

    /**
     * @brief Handle speed slider value change
     * @param value New speed percentage (50-200)
     */
    void handle_tune_speed_changed(int value);

    /**
     * @brief Handle flow slider value change
     * @param value New flow percentage (75-125)
     */
    void handle_tune_flow_changed(int value);

    /**
     * @brief Handle reset button click - resets speed/flow to 100%
     */
    void handle_tune_reset();

    /**
     * @brief Handle Z-offset button click (baby stepping)
     * @param delta Z-offset change in mm (negative = closer/more squish)
     */
    void handle_tune_z_offset_changed(double delta);

    /**
     * @brief Handle save Z-offset button click
     * Shows warning modal since SAVE_CONFIG will restart Klipper
     */
    void handle_tune_save_z_offset();

  private:
    //
    // === Subjects (owned by this panel) ===
    // Note: Display filename uses shared print_display_filename from PrinterState
    //       (populated by ActivePrintMediaManager)
    //

    lv_subject_t progress_text_subject_;
    lv_subject_t layer_text_subject_;
    lv_subject_t elapsed_subject_;
    lv_subject_t remaining_subject_;
    lv_subject_t nozzle_temp_subject_;
    lv_subject_t bed_temp_subject_;
    lv_subject_t speed_subject_;
    lv_subject_t flow_subject_;
    lv_subject_t pause_button_subject_;
    lv_subject_t pause_label_subject_;      ///< Pause button label ("Pause"/"Resume")
    lv_subject_t timelapse_button_subject_; ///< Timelapse icon (video/video-off)
    lv_subject_t timelapse_label_subject_;  ///< Timelapse button label ("On"/"Off")

    // Preparing state subjects
    lv_subject_t preparing_visible_subject_;   // int: 1 if preparing, 0 otherwise
    lv_subject_t preparing_operation_subject_; // string: current operation name
    lv_subject_t preparing_progress_subject_;  // int: 0-100 progress percentage

    // Viewer mode subject (0=thumbnail mode, 1=gcode viewer mode)
    lv_subject_t gcode_viewer_mode_subject_;

    // Print complete overlay visibility (0=hidden, 1=visible)
    lv_subject_t print_complete_visible_subject_;

    // Subject storage buffers
    char progress_text_buf_[32] = "0%";
    char layer_text_buf_[64] = "Layer 0 / 0";
    char preparing_operation_buf_[64] = "Preparing...";
    char elapsed_buf_[32] = "0h 00m";
    char remaining_buf_[32] = "0h 00m";
    char nozzle_temp_buf_[32] = "0 / 0°C";
    char bed_temp_buf_[32] = "0 / 0°C";
    char speed_buf_[32] = "100%";
    char flow_buf_[32] = "100%";
    char pause_button_buf_[32] = "\xF3\xB0\x8F\xA4"; // MDI pause icon (F03E4)
    char pause_label_buf_[16] = "Pause";             ///< Pause button label
    char timelapse_button_buf_[8] = "";              ///< MDI icon codepoint for timelapse state
    char timelapse_label_buf_[16] = "Off";           ///< Timelapse button label

    //
    // === Instance State ===
    //

    bool timelapse_enabled_ = false; ///< Current timelapse recording state
    PrintState current_state_ = PrintState::Idle;
    int current_progress_ = 0;

    // Thumbnail loading state
    std::string current_print_filename_; ///< Full path to current print file (for metadata fetch)
    std::string cached_thumbnail_path_;  ///< Local cache path for downloaded thumbnail
    uint32_t thumbnail_load_generation_ = 0; ///< Generation counter for async callback safety
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

    // Child widgets
    lv_obj_t* progress_bar_ = nullptr;
    lv_obj_t* preparing_progress_bar_ = nullptr;
    lv_obj_t* gcode_viewer_ = nullptr;
    lv_obj_t* print_thumbnail_ = nullptr;
    lv_obj_t* gradient_background_ = nullptr;

    // Thumbnail source override - used when printing modified temp files
    // When set, load_thumbnail_for_file() uses this instead of the actual filename
    std::string thumbnail_source_filename_;

    // Track what thumbnail is currently loaded to make set_filename() idempotent
    // Prevents redundant thumbnail loads when observer fires repeatedly with same filename
    std::string loaded_thumbnail_filename_;

    // Track whether G-code was successfully loaded into the viewer
    // When false (memory check failed), don't switch to viewer mode on state changes
    bool gcode_loaded_ = false;

    // Path to temp G-code file downloaded for viewing (cleaned up on print end)
    std::string temp_gcode_path_;

    // Control buttons (stored for enable/disable on state changes)
    lv_obj_t* btn_timelapse_ = nullptr;
    lv_obj_t* btn_pause_ = nullptr;
    lv_obj_t* btn_tune_ = nullptr;
    lv_obj_t* btn_cancel_ = nullptr;

    // Print completion celebration badge (animated on print complete)
    lv_obj_t* success_badge_ = nullptr;

    // Header bar (for e-stop visibility control)
    lv_obj_t* overlay_header_ = nullptr;

    //
    // === Temperature & Tuning Overlays ===
    //

    TempControlPanel* temp_control_panel_ = nullptr;
    lv_obj_t* nozzle_temp_panel_ = nullptr;
    lv_obj_t* bed_temp_panel_ = nullptr;
    lv_obj_t* tune_panel_ = nullptr;

    // Tuning panel subjects
    lv_subject_t tune_speed_subject_;
    lv_subject_t tune_flow_subject_;
    lv_subject_t tune_z_offset_subject_;
    char tune_speed_buf_[16] = "100%";
    char tune_flow_buf_[16] = "100%";
    char tune_z_offset_buf_[16] = "0.000mm";
    double current_z_offset_ = 0.0; ///< Current Z-offset in mm (for display)

    // Z-offset save warning modal
    SaveZOffsetModal save_z_offset_modal_;

    // Resize callback registration flag
    bool resize_registered_ = false;

    //
    // === Private Helpers ===
    //

    void update_all_displays();
    void show_gcode_viewer(bool show);
    void load_gcode_file(const char* file_path);
    void load_thumbnail_for_file(const std::string& filename); ///< Fetch and display thumbnail
    void
    load_gcode_for_viewing(const std::string& filename); ///< Download and load G-code into viewer
    void setup_tune_panel(lv_obj_t* panel);
    void update_tune_display();
    void update_z_offset_icons(lv_obj_t* panel); ///< Update Z-offset icons based on kinematics
    void update_button_states();   ///< Enable/disable buttons based on current print state
    void animate_print_complete(); ///< Celebratory animation when print finishes
    void cleanup_temp_gcode();     ///< Remove temp G-code file downloaded for viewing

    static void format_time(int seconds, char* buf, size_t buf_size);

    //
    // === Instance Handlers ===
    //

    void handle_nozzle_card_click();
    void handle_bed_card_click();
    void handle_light_button();
    void handle_timelapse_button();
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
    static void on_timelapse_clicked(lv_event_t* e);
    static void on_pause_clicked(lv_event_t* e);
    static void on_tune_clicked(lv_event_t* e);
    static void on_cancel_clicked(lv_event_t* e);

    // Static resize callback (registered with ui_resize_handler)
    static void on_resize_static();

    //
    // === PrinterState Observer Callbacks ===
    //

    static void extruder_temp_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void extruder_target_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void bed_temp_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void bed_target_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void print_progress_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void print_state_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void print_filename_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void speed_factor_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void flow_factor_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void gcode_z_offset_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void led_state_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void print_layer_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void excluded_objects_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void print_duration_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void print_time_left_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void print_start_phase_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void print_start_message_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void print_start_progress_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    //
    // === Observer Instance Methods ===
    //

    void on_temperature_changed();
    void on_print_progress_changed(int progress);
    void on_print_state_changed(PrintJobState state);
    void on_print_filename_changed(const char* filename);
    void on_speed_factor_changed(int speed);
    void on_flow_factor_changed(int flow);
    void on_gcode_z_offset_changed(int microns);
    void on_led_state_changed(int state);
    void on_print_layer_changed(int current_layer);
    void on_excluded_objects_changed();
    void on_print_duration_changed(int seconds);
    void on_print_time_left_changed(int seconds);
    void on_print_start_phase_changed(int phase);
    void on_print_start_message_changed(const char* message);
    void on_print_start_progress_changed(int progress);

    // PrinterState observers (ObserverGuard handles cleanup)
    ObserverGuard extruder_temp_observer_;
    ObserverGuard extruder_target_observer_;
    ObserverGuard bed_temp_observer_;
    ObserverGuard bed_target_observer_;
    ObserverGuard print_progress_observer_;
    ObserverGuard print_state_observer_;
    ObserverGuard print_filename_observer_;
    ObserverGuard speed_factor_observer_;
    ObserverGuard flow_factor_observer_;
    ObserverGuard gcode_z_offset_observer_;
    ObserverGuard led_state_observer_;
    ObserverGuard print_layer_observer_;
    ObserverGuard excluded_objects_observer_;
    ObserverGuard print_duration_observer_;
    ObserverGuard print_time_left_observer_;
    ObserverGuard print_start_phase_observer_;
    ObserverGuard print_start_message_observer_;
    ObserverGuard print_start_progress_observer_;

    bool led_on_ = false;
    std::string configured_led_;

    //
    // === Exclude Object State ===
    //

    /// Objects already excluded (sent to Klipper, cannot be undone)
    std::unordered_set<std::string> excluded_objects_;

    /// Object pending exclusion (in undo window, not yet sent to Klipper)
    std::string pending_exclude_object_;

    /// Timer for undo window (5 seconds before sending EXCLUDE_OBJECT to Klipper)
    lv_timer_t* exclude_undo_timer_{nullptr};

    /// Active confirmation dialog (if showing)
    lv_obj_t* exclude_confirm_dialog_{nullptr};

    /// Print cancel confirmation modal (RAII - auto-hides when destroyed)
    PrintCancelModal cancel_modal_;

    /// Runout guidance modal (shown when print pauses + no filament detected)
    lv_obj_t* runout_guidance_modal_ = nullptr;

    /// Flag to track if runout modal was shown for current pause
    /// Reset when print resumes or ends, prevents repeated modal popups
    bool runout_modal_shown_for_pause_ = false;

    //
    // === Exclude Object Handlers ===
    //

    /**
     * @brief Handle long-press on object in G-code viewer
     * Shows confirmation dialog for excluding the object
     */
    void handle_object_long_press(const char* object_name);

    /**
     * @brief Handle confirmation of object exclusion
     * Starts the delayed undo window and shows undo toast
     */
    void handle_exclude_confirmed();

    /**
     * @brief Handle cancellation of exclusion dialog
     */
    void handle_exclude_cancelled();

    /**
     * @brief Handle undo button press on toast (cancels pending exclusion)
     */
    void handle_exclude_undo();

    /**
     * @brief Timer callback when undo window expires - sends EXCLUDE_OBJECT to Klipper
     */
    static void exclude_undo_timer_cb(lv_timer_t* timer);

    //
    // === Exclude Object Static Trampolines ===
    //

    static void on_object_long_pressed(lv_obj_t* viewer, const char* object_name, void* user_data);
    static void on_exclude_confirm_clicked(lv_event_t* e);
    static void on_exclude_cancel_clicked(lv_event_t* e);

    //
    // === Runout Guidance Modal ===
    //

    /**
     * @brief Show the runout guidance modal
     *
     * Called when print pauses and runout sensor shows no filament.
     * Shows three options: Load Filament, Resume Print, Cancel Print.
     */
    void show_runout_guidance_modal();

    /**
     * @brief Hide and cleanup the runout guidance modal
     */
    void hide_runout_guidance_modal();

    /**
     * @brief Check if runout condition exists and show guidance modal if appropriate
     *
     * Called when print transitions to Paused state. Checks if runout sensor
     * is available and shows no filament - if so, displays guidance modal.
     */
    void check_and_show_runout_guidance();

    // Runout modal button callbacks
    static void on_runout_load_filament_clicked(lv_event_t* e);
    static void on_runout_resume_clicked(lv_event_t* e);
    static void on_runout_cancel_print_clicked(lv_event_t* e);
};

// Global instance accessor (needed by main.cpp)
PrintStatusPanel& get_global_print_status_panel();
