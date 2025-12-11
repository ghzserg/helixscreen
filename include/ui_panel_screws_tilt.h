// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "advanced_panel_types.h"
#include "lvgl/lvgl.h"

#include <array>
#include <string>
#include <vector>

class MoonrakerClient;
class MoonrakerAPI;

/**
 * @file ui_panel_screws_tilt.h
 * @brief Screws Tilt Adjust panel for manual bed leveling
 *
 * Interactive panel that guides the user through the SCREWS_TILT_CALCULATE
 * workflow. Shows a visual bed diagram with screw positions and adjustment
 * indicators, supporting an iterative probe-adjust-reprobe workflow.
 *
 * ## State Machine:
 * - IDLE: Shows instructions and Start button
 * - PROBING: Waiting for SCREWS_TILT_CALCULATE to complete
 * - RESULTS: Showing bed diagram and adjustment values
 * - LEVELED: All screws within tolerance
 * - ERROR: Something went wrong
 *
 * ## Usage:
 * ```cpp
 * ScrewsTiltPanel& panel = get_global_screws_tilt_panel();
 * panel.setup(lv_obj, parent_screen, moonraker_client, moonraker_api);
 * ui_nav_push_overlay(lv_obj);
 * ```
 */
class ScrewsTiltPanel {
  public:
    /**
     * @brief Panel state machine states
     */
    enum class State {
        IDLE,    ///< Ready to start, showing instructions
        PROBING, ///< SCREWS_TILT_CALCULATE running
        RESULTS, ///< Showing adjustment results
        LEVELED, ///< All screws within tolerance
        ERROR    ///< Error occurred
    };

    ScrewsTiltPanel() = default;
    ~ScrewsTiltPanel() = default;

    /**
     * @brief Setup the panel with event handlers
     *
     * @param panel Root panel object from lv_xml_create()
     * @param parent_screen Parent screen (for navigation)
     * @param client Moonraker client for sending commands
     * @param api Moonraker API for calculate_screws_tilt()
     */
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen, MoonrakerClient* client,
               MoonrakerAPI* api);

    /**
     * @brief Get current panel state
     * @return Current State
     */
    [[nodiscard]] State get_state() const {
        return state_;
    }

    /**
     * @brief Called when screws tilt calculation completes successfully
     * @param results Vector of screw adjustment results
     */
    void on_screws_tilt_results(const std::vector<ScrewTiltResult>& results);

    /**
     * @brief Called when screws tilt calculation fails
     * @param message Error message
     */
    void on_screws_tilt_error(const std::string& message);

    //
    // === Event Handlers (public for XML event_cb callbacks) ===
    //

    void handle_start_clicked();
    void handle_cancel_clicked();
    void handle_reprobe_clicked();
    void handle_done_clicked();
    void handle_retry_clicked();

    /**
     * @brief Initialize subjects for reactive XML bindings
     *
     * Must be called BEFORE XML creation (from register_callbacks).
     * Subject bindings are resolved at XML parse time.
     */
    void init_subjects();

  private:
    bool subjects_initialized_ = false;
    // State management
    State state_ = State::IDLE;
    void set_state(State new_state);
    void show_state_view(State state);

    // Command helpers
    void start_probing();
    void cancel_probing();

    // UI update helpers
    void populate_results(const std::vector<ScrewTiltResult>& results);
    void clear_results();
    void update_screw_diagram();
    void create_screw_indicator(size_t index, const ScrewTiltResult& screw, bool is_worst = false);
    [[nodiscard]] lv_color_t get_adjustment_color(const ScrewTiltResult& screw,
                                                  bool is_worst_screw = false) const;
    [[nodiscard]] size_t find_worst_screw_index() const;

    /**
     * @brief Check if all screws are within tolerance
     * @param tolerance_minutes Maximum adjustment in minutes (default 5 = ~0.04mm)
     * @return true if all screws are level
     */
    [[nodiscard]] bool check_all_level(int tolerance_minutes = 5) const;

    // Widget references
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;
    MoonrakerClient* client_ = nullptr;
    MoonrakerAPI* api_ = nullptr;

    // State views
    lv_obj_t* state_idle_ = nullptr;
    lv_obj_t* state_probing_ = nullptr;
    lv_obj_t* state_results_ = nullptr;
    lv_obj_t* state_leveled_ = nullptr;
    lv_obj_t* state_error_ = nullptr;

    // Results UI elements
    lv_obj_t* bed_diagram_container_ = nullptr;
    lv_obj_t* results_instruction_ = nullptr;
    lv_obj_t* probe_count_label_ = nullptr;
    lv_obj_t* error_message_ = nullptr;

    // Dynamic screw indicators (bed diagram only - positions vary)
    std::vector<lv_obj_t*> screw_indicators_;

    // Screw row dot widgets (for color updates - XML handles text via subjects)
    std::array<lv_obj_t*, 4> screw_dots_ = {nullptr, nullptr, nullptr, nullptr};

    // Subjects for reactive screw list (4 slots max)
    static constexpr size_t MAX_SCREWS = 4;
    static constexpr size_t SCREW_NAME_BUF_SIZE = 32;
    static constexpr size_t SCREW_ADJ_BUF_SIZE = 24; // "Tighten ¼ turn" etc.

    std::array<lv_subject_t, MAX_SCREWS> screw_visible_subjects_;
    std::array<lv_subject_t, MAX_SCREWS> screw_name_subjects_;
    std::array<lv_subject_t, MAX_SCREWS> screw_adjustment_subjects_;

    // Fixed char arrays for string subjects (LVGL requires stable buffers)
    char screw_name_bufs_[MAX_SCREWS][SCREW_NAME_BUF_SIZE] = {};
    char screw_adj_bufs_[MAX_SCREWS][SCREW_ADJ_BUF_SIZE] = {};

    // Screw data
    std::vector<ScrewTiltResult> screw_results_;

    // Tracking
    int probe_count_ = 0;
};

// Global instance accessor
ScrewsTiltPanel& get_global_screws_tilt_panel();

/**
 * @brief Register XML event callbacks for screws tilt panel
 *
 * Call this once at startup before creating any screws_tilt_panel XML.
 * Registers callbacks for all button events (start, cancel, done, reprobe, retry).
 */
void ui_panel_screws_tilt_register_callbacks();
