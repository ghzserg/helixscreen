// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_filament_runout_handler.h"
#include "ui_modal.h"
#include "ui_observer_guard.h"
#include "ui_print_cancel_modal.h"
#include "ui_print_exclude_object_manager.h"
#include "ui_print_light_timelapse.h"
#include "ui_print_tune_overlay.h"
#include "ui_save_z_offset_modal.h"

#include "overlay_base.h"
#include "printer_state.h"
#include "subject_managed_panel.h"
#include "ui/temperature_observer_bundle.h"

// Forward declaration
class MoonrakerAPI;

#include <atomic>
#include <functional>
#include <memory>
#include <string>

// Forward declarations
class TempControlPanel;
class PrintStatusPanel;

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

class PrintStatusPanel : public OverlayBase {
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
    // === OverlayBase Implementation ===
    //

    /**
     * @brief Initialize subjects for XML binding
     *
     * Registers all 10 subjects for reactive data binding.
     */
    void init_subjects() override;

    /**
     * @brief Deinitialize subjects for clean shutdown
     *
     * Calls lv_subject_deinit() on all local lv_subject_t members.
     */
    void deinit_subjects();

    /**
     * @brief Create overlay UI from XML
     *
     * @param parent Parent widget to attach overlay to (usually screen)
     * @return Root object of overlay, or nullptr on failure
     */
    lv_obj_t* create(lv_obj_t* parent) override;

    /**
     * @brief Get human-readable overlay name
     * @return "Print Status"
     */
    const char* get_name() const override {
        return "Print Status";
    }

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

    /**
     * @brief Clean up resources for async-safe destruction
     */
    void cleanup() override;

    //
    // === Legacy Compatibility ===
    //

    /**
     * @brief Get XML component name for lv_xml_create()
     * @return "print_status_panel"
     */
    const char* get_xml_component_name() const {
        return "print_status_panel";
    }

    /**
     * @brief Get root panel object (alias for get_root())
     * @return Panel object, or nullptr if not yet created
     */
    lv_obj_t* get_panel() const {
        return overlay_root_;
    }

    /**
     * @brief Update MoonrakerAPI pointer
     * @param api New API pointer (may be nullptr)
     */
    void set_api(MoonrakerAPI* api) {
        api_ = api;
        if (exclude_manager_) {
            exclude_manager_->set_api(api);
        }
        if (runout_handler_) {
            runout_handler_->set_api(api);
        }
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
     * @brief Clear preparing state and transition to Idle or Printing
     *
     * Call this when the print start API call completes or fails.
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

    // Tune panel handlers delegated to PrintTuneOverlay (tune_overlay_ member)

  private:
    //
    // === Injected Dependencies ===
    //

    PrinterState& printer_state_;
    MoonrakerAPI* api_;
    lv_obj_t* parent_screen_ = nullptr;

    //
    // === Subjects (owned by this panel) ===
    // Note: Display filename uses shared print_display_filename from PrinterState
    //       (populated by ActivePrintMediaManager)
    //

    SubjectManager subjects_; ///< RAII manager for automatic subject cleanup

    lv_subject_t progress_text_subject_;
    lv_subject_t layer_text_subject_;
    lv_subject_t filament_used_text_subject_;
    lv_subject_t elapsed_subject_;
    lv_subject_t remaining_subject_;
    lv_subject_t nozzle_temp_subject_;
    lv_subject_t bed_temp_subject_;
    lv_subject_t nozzle_status_subject_;
    lv_subject_t bed_status_subject_;
    lv_subject_t speed_subject_;
    lv_subject_t flow_subject_;
    lv_subject_t pause_button_subject_;
    lv_subject_t pause_label_subject_; ///< Pause button label ("Pause"/"Resume")

    // Preparing state subjects
    lv_subject_t preparing_visible_subject_;   // int: 1 if preparing, 0 otherwise
    lv_subject_t preparing_operation_subject_; // string: current operation name
    lv_subject_t preparing_progress_subject_;  // int: 0-100 progress percentage

    // Viewer mode subject (0=thumbnail mode, 1=gcode viewer mode)
    lv_subject_t gcode_viewer_mode_subject_;

    lv_subject_t exclude_objects_available_subject_; ///< Int: 1 if multi-object print
    lv_subject_t objects_text_subject_;              ///< String: "X of Y obj" display text

    // Subject storage buffers
    char progress_text_buf_[32] = "0%";
    char layer_text_buf_[64] = "Layer 0 / 0";
    char filament_used_text_buf_[32] = "";
    char preparing_operation_buf_[64] = "Preparing...";
    char elapsed_buf_[32] = "0h 00m";
    char remaining_buf_[32] = "0h 00m";
    char nozzle_temp_buf_[32] = "0 / 0°C";
    char bed_temp_buf_[32] = "0 / 0°C";
    char nozzle_status_buf_[16] = "Off";
    char bed_status_buf_[16] = "Off";
    char speed_buf_[32] = "100%";
    char flow_buf_[32] = "100%";
    char pause_button_buf_[32] = "\xF3\xB0\x8F\xA4"; // MDI pause icon (F03E4)
    char pause_label_buf_[16] = "Pause";             ///< Pause button label
    char objects_text_buf_[32] = "";                 ///< "X of Y obj" buffer

    //
    // === Instance State ===
    //

    /// Shutdown guard for async callbacks - set false in destructor
    /// Captured by lambdas to prevent use-after-free on shutdown [L012]
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
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
    int preprint_remaining_seconds_ = 0;
    int preprint_elapsed_seconds_ =
        0; ///< Pre-print elapsed time (used only during Preparing state)
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

    // Deferred G-code loading: filename to load when panel becomes visible
    // Set in set_filename(), consumed in on_activate() - avoids downloading
    // large files unless user actually navigates to print status panel
    std::string pending_gcode_filename_;

    // Track whether G-code was successfully loaded into the viewer
    // When false (memory check failed), don't switch to viewer mode on state changes
    bool gcode_loaded_ = false;

    // Track whether panel is currently active (visible and receiving updates)
    // Used to load gcode immediately if already active when print starts
    bool is_active_ = false;

    // Path to temp G-code file downloaded for viewing (cleaned up on print end)
    std::string temp_gcode_path_;

    // Control buttons (stored for enable/disable on state changes)
    lv_obj_t* btn_timelapse_ = nullptr;
    lv_obj_t* btn_pause_ = nullptr;
    lv_obj_t* btn_tune_ = nullptr;
    lv_obj_t* btn_cancel_ = nullptr;
    lv_obj_t* btn_reprint_ = nullptr;

    // Print completion celebration badge (animated on print complete)
    lv_obj_t* success_badge_ = nullptr;

    // Print cancelled badge (animated on print cancel)
    lv_obj_t* cancel_badge_ = nullptr;

    // Print error badge (animated on print error)
    lv_obj_t* error_badge_ = nullptr;

    // Header bar (for e-stop visibility control)
    lv_obj_t* overlay_header_ = nullptr;

    //
    // === Temperature & Tuning Overlays ===
    //

    TempControlPanel* temp_control_panel_ = nullptr;
    lv_obj_t* nozzle_temp_panel_ = nullptr;
    lv_obj_t* bed_temp_panel_ = nullptr;

    // Light/timelapse controls (extracted Phase 2 functionality)
    PrintLightTimelapseControls light_timelapse_controls_;

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
    void update_button_states(); ///< Enable/disable buttons based on current print state
    void update_objects_text();  ///< Update "X of Y obj" display from exclude state
    void animate_badge_pop_in(lv_obj_t* badge, const char* label); ///< Pop-in animation for badges
    void animate_print_complete();  ///< Celebratory animation when print finishes
    void animate_print_cancelled(); ///< Warning animation when print is cancelled
    void animate_print_error();     ///< Error animation when print fails
    void cleanup_temp_gcode();      ///< Remove temp G-code file downloaded for viewing
    void apply_filament_color_override(
        uint32_t color_rgb); ///< Apply AMS/Spoolman filament color to gcode viewer

    static void format_time(int seconds, char* buf, size_t buf_size);

    //
    // === Instance Handlers ===
    //

    void handle_nozzle_card_click();
    void handle_bed_card_click();
    void handle_pause_button();
    void handle_tune_button();
    void handle_cancel_button();
    void handle_reprint_button(); ///< Reprint the cancelled file
    void handle_resize();

    //
    // === Static Trampolines ===
    //

    static void on_nozzle_card_clicked(lv_event_t* e);
    static void on_bed_card_clicked(lv_event_t* e);
    static void on_pause_clicked(lv_event_t* e);
    static void on_tune_clicked(lv_event_t* e);
    static void on_cancel_clicked(lv_event_t* e);
    static void on_reprint_clicked(lv_event_t* e);
    static void on_objects_clicked(lv_event_t* e);

    // Static resize callback (registered with ui_resize_handler)
    static void on_resize_static();

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
    void on_print_duration_changed(int seconds);
    void on_print_time_left_changed(int seconds);
    void on_print_start_phase_changed(int phase);
    void on_print_start_message_changed(const char* message);
    void on_print_start_progress_changed(int progress);
    void on_preprint_remaining_changed(int seconds);
    void on_preprint_elapsed_changed(int seconds);

    // PrinterState observers (ObserverGuard handles cleanup)
    /// @brief Temperature observer bundle (nozzle + bed temps)
    helix::ui::TemperatureObserverBundle<PrintStatusPanel> temp_observers_;
    ObserverGuard print_progress_observer_;
    ObserverGuard print_state_observer_;
    ObserverGuard print_filename_observer_;
    ObserverGuard speed_factor_observer_;
    ObserverGuard flow_factor_observer_;
    ObserverGuard gcode_z_offset_observer_;
    ObserverGuard led_state_observer_;
    ObserverGuard print_layer_observer_;
    ObserverGuard print_duration_observer_;
    ObserverGuard print_time_left_observer_;
    ObserverGuard print_start_phase_observer_;
    ObserverGuard print_start_message_observer_;
    ObserverGuard print_start_progress_observer_;
    ObserverGuard preprint_remaining_observer_;
    ObserverGuard preprint_elapsed_observer_;
    ObserverGuard exclude_objects_observer_;
    ObserverGuard excluded_objects_version_observer_;
    ObserverGuard ams_color_observer_; ///< Tracks AMS/Spoolman filament color for gcode viewer

    //
    // === Exclude Object Manager ===
    //

    /// Manages exclude object feature (extracted from PrintStatusPanel)
    std::unique_ptr<helix::ui::PrintExcludeObjectManager> exclude_manager_;

    /// Print cancel confirmation modal (RAII - auto-hides when destroyed)
    PrintCancelModal cancel_modal_;

    //
    // === Filament Runout Handler ===
    //

    /// Manages filament runout guidance (extracted from PrintStatusPanel)
    std::unique_ptr<helix::ui::FilamentRunoutHandler> runout_handler_;
};

// Global instance accessor (needed by main.cpp)
PrintStatusPanel& get_global_print_status_panel();
