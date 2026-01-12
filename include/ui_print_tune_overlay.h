// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_save_z_offset_modal.h"

#include "lvgl/lvgl.h"
#include "subject_managed_panel.h"

// Forward declarations
class MoonrakerAPI;
class PrinterState;

/**
 * @file ui_print_tune_overlay.h
 * @brief Tune panel functionality extracted from PrintStatusPanel
 *
 * Manages the tune overlay panel that allows adjusting:
 * - Print speed (M220 command)
 * - Flow rate (M221 command)
 * - Z-offset / baby stepping (SET_GCODE_OFFSET command)
 *
 * This class is a helper owned by PrintStatusPanel, not a standalone overlay.
 * It manages the subjects and callbacks for the tune panel's reactive UI.
 *
 * @pattern Helper class with subject management
 * @threading Main thread only (LVGL)
 */
class PrintTuneOverlay {
  public:
    PrintTuneOverlay();
    ~PrintTuneOverlay();

    // Non-copyable
    PrintTuneOverlay(const PrintTuneOverlay&) = delete;
    PrintTuneOverlay& operator=(const PrintTuneOverlay&) = delete;

    /**
     * @brief Initialize subjects for XML binding
     *
     * Registers tune_speed_display, tune_flow_display, tune_z_offset_display
     * subjects and XML event callbacks.
     *
     * @param printer_state Reference to PrinterState for kinematics info
     */
    void init_subjects(PrinterState& printer_state);

    /**
     * @brief Deinitialize subjects
     *
     * Called during cleanup. Safe to call multiple times.
     */
    void deinit_subjects();

    /**
     * @brief Setup the tune panel after it's created
     *
     * Configures back button handling and updates Z-offset icons based on kinematics.
     *
     * @param panel The tune panel widget (tune_overlay from XML)
     * @param parent_screen The parent screen for overlay panel setup
     * @param api MoonrakerAPI for sending commands
     * @param printer_state Reference to PrinterState
     */
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen, MoonrakerAPI* api,
               PrinterState& printer_state);

    /**
     * @brief Handle speed slider value change
     * @param value New speed percentage (50-200)
     */
    void handle_speed_changed(int value);

    /**
     * @brief Handle flow slider value change
     * @param value New flow percentage (75-125)
     */
    void handle_flow_changed(int value);

    /**
     * @brief Handle reset button click - resets speed/flow to 100%
     */
    void handle_reset();

    /**
     * @brief Handle Z-offset button click (baby stepping)
     * @param delta Z-offset change in mm (negative = closer/more squish)
     */
    void handle_z_offset_changed(double delta);

    /**
     * @brief Handle save Z-offset button click
     * Shows warning modal since SAVE_CONFIG will restart Klipper
     */
    void handle_save_z_offset();

    /**
     * @brief Update Z-offset icons based on printer kinematics
     *
     * Sets appropriate icons for CoreXY (bed moves) vs Cartesian (head moves).
     *
     * @param panel The tune panel widget
     */
    void update_z_offset_icons(lv_obj_t* panel);

    /**
     * @brief Update display from current speed/flow values
     *
     * Called by PrintStatusPanel when PrinterState values change.
     *
     * @param speed_percent Current speed percentage
     * @param flow_percent Current flow percentage
     */
    void update_speed_flow_display(int speed_percent, int flow_percent);

    /**
     * @brief Update Z-offset display from PrinterState
     * @param microns Z-offset in microns from PrinterState
     */
    void update_z_offset_display(int microns);

    /**
     * @brief Get the tune panel widget
     * @return Tune panel widget, or nullptr if not set up
     */
    lv_obj_t* get_panel() const {
        return tune_panel_;
    }

    /**
     * @brief Check if subjects have been initialized
     * @return true if init_subjects() was called
     */
    bool are_subjects_initialized() const {
        return subjects_initialized_;
    }

  private:
    void update_display();

    //
    // === Dependencies ===
    //

    MoonrakerAPI* api_ = nullptr;
    PrinterState* printer_state_ = nullptr;
    lv_obj_t* tune_panel_ = nullptr;

    //
    // === Subject Management ===
    //

    SubjectManager subjects_;
    bool subjects_initialized_ = false;

    // Subjects for reactive UI
    lv_subject_t tune_speed_subject_;
    lv_subject_t tune_flow_subject_;
    lv_subject_t tune_z_offset_subject_;

    // Subject storage buffers
    char tune_speed_buf_[16] = "100%";
    char tune_flow_buf_[16] = "100%";
    char tune_z_offset_buf_[16] = "0.000mm";

    //
    // === State ===
    //

    double current_z_offset_ = 0.0;
    int speed_percent_ = 100;
    int flow_percent_ = 100;

    //
    // === Modals ===
    //

    SaveZOffsetModal save_z_offset_modal_;
};

/**
 * @brief Get global PrintTuneOverlay instance
 *
 * Used by XML event callbacks to route events to the overlay instance.
 * The instance is managed by PrintStatusPanel.
 *
 * @return Reference to PrintTuneOverlay
 */
PrintTuneOverlay& get_global_print_tune_overlay();

/**
 * @brief Set global PrintTuneOverlay instance
 *
 * Called by PrintStatusPanel during initialization.
 *
 * @param overlay Pointer to PrintTuneOverlay (or nullptr to clear)
 */
void set_global_print_tune_overlay(PrintTuneOverlay* overlay);
