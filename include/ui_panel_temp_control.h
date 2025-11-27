// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"
#include "ui_heater_config.h"
#include "ui_temp_graph.h"

#include <array>
#include <functional>
#include <string>

// Forward declarations
class PrinterState;
class MoonrakerAPI;

/**
 * @brief Temperature Control Panel - manages nozzle and bed temperature UI
 *
 * Proper C++ class with:
 * - Constructor dependency injection (PrinterState, MoonrakerAPI)
 * - RAII-managed observers (auto-cleanup in destructor)
 * - Encapsulated state (no static globals)
 *
 * Usage:
 *   // In app initialization (after PrinterState is ready):
 *   auto temp_panel = std::make_unique<TempControlPanel>(
 *       get_printer_state(), get_moonraker_api());
 *
 *   // When XML panels are created:
 *   temp_panel->setup_nozzle_panel(nozzle_xml_obj, parent_screen);
 *   temp_panel->setup_bed_panel(bed_xml_obj, parent_screen);
 */
class TempControlPanel {
  public:
    /**
     * @brief Construct temperature control panel
     *
     * Automatically subscribes to PrinterState temperature subjects.
     * Observers are cleaned up in destructor (RAII).
     *
     * @param printer_state Reference to PrinterState singleton
     * @param api Pointer to MoonrakerAPI (may be null if not connected)
     */
    TempControlPanel(PrinterState& printer_state, MoonrakerAPI* api);

    /**
     * @brief Destructor - unsubscribes all observers
     */
    ~TempControlPanel();

    // Non-copyable (has observer state)
    TempControlPanel(const TempControlPanel&) = delete;
    TempControlPanel& operator=(const TempControlPanel&) = delete;

    // Movable
    TempControlPanel(TempControlPanel&&) noexcept;
    TempControlPanel& operator=(TempControlPanel&&) noexcept;

    /**
     * @brief Setup nozzle temperature panel after XML creation
     *
     * Wires up event handlers, creates graph, loads theme colors.
     *
     * @param panel Root object of nozzle_temp_panel (from lv_xml_create)
     * @param parent_screen Parent screen for navigation
     */
    void setup_nozzle_panel(lv_obj_t* panel, lv_obj_t* parent_screen);

    /**
     * @brief Setup bed temperature panel after XML creation
     *
     * Wires up event handlers, creates graph, loads theme colors.
     *
     * @param panel Root object of bed_temp_panel (from lv_xml_create)
     * @param parent_screen Parent screen for navigation
     */
    void setup_bed_panel(lv_obj_t* panel, lv_obj_t* parent_screen);

    /**
     * @brief Initialize LVGL subjects for XML data binding
     *
     * Must be called BEFORE creating XML components that bind to temperature subjects.
     * This registers subjects like "nozzle_temp_display" with the XML system.
     */
    void init_subjects();

    //
    // Public API for external updates
    //

    /**
     * @brief Update nozzle temperature display (external caller)
     */
    void set_nozzle(int current, int target);

    /**
     * @brief Update bed temperature display (external caller)
     */
    void set_bed(int current, int target);

    /**
     * @brief Get current nozzle target temperature
     */
    int get_nozzle_target() const { return nozzle_target_; }

    /**
     * @brief Get current bed target temperature
     */
    int get_bed_target() const { return bed_target_; }

    /**
     * @brief Set nozzle temperature limits (from Moonraker heater config)
     */
    void set_nozzle_limits(int min_temp, int max_temp);

    /**
     * @brief Set bed temperature limits (from Moonraker heater config)
     */
    void set_bed_limits(int min_temp, int max_temp);

    /**
     * @brief Update MoonrakerAPI pointer
     *
     * Call this when API becomes available after initial construction.
     */
    void set_api(MoonrakerAPI* api) { api_ = api; }

  private:
    //
    // Observer callbacks (static trampolines that call instance methods)
    //
    static void nozzle_temp_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void nozzle_target_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void bed_temp_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void bed_target_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    // Instance methods called by observers
    void on_nozzle_temp_changed(int temp);
    void on_nozzle_target_changed(int target);
    void on_bed_temp_changed(int temp);
    void on_bed_target_changed(int target);

    // Display update helpers
    void update_nozzle_display();
    void update_bed_display();

    // Graph creation helper
    ui_temp_graph_t* create_temp_graph(lv_obj_t* chart_area, const heater_config_t* config,
                                       int target_temp, int* series_id_out);

    // Y-axis label creation
    void create_y_axis_labels(lv_obj_t* container, const heater_config_t* config);

    // Button callback setup
    void setup_preset_buttons(lv_obj_t* panel, heater_type_t type);
    void setup_custom_button(lv_obj_t* panel, heater_type_t type);
    void setup_confirm_button(lv_obj_t* header, heater_type_t type);

    // Event handlers (static trampolines)
    static void nozzle_confirm_cb(lv_event_t* e);
    static void bed_confirm_cb(lv_event_t* e);
    static void preset_button_cb(lv_event_t* e);
    static void custom_button_cb(lv_event_t* e);

    // Keypad callback
    static void keypad_value_cb(float value, void* user_data);

    //
    // Dependencies (injected via constructor)
    //
    PrinterState& printer_state_;
    MoonrakerAPI* api_; // May be null

    //
    // Observer handles (for RAII cleanup)
    //
    lv_observer_t* nozzle_temp_observer_ = nullptr;
    lv_observer_t* nozzle_target_observer_ = nullptr;
    lv_observer_t* bed_temp_observer_ = nullptr;
    lv_observer_t* bed_target_observer_ = nullptr;

    //
    // Temperature state (from Moonraker)
    //
    int nozzle_current_ = 25;
    int nozzle_target_ = 0;
    int bed_current_ = 25;
    int bed_target_ = 0;

    // Pending selection (user picked but not confirmed yet)
    int nozzle_pending_ = -1;  // -1 = no pending selection
    int bed_pending_ = -1;     // -1 = no pending selection

    // Temperature limits
    int nozzle_min_temp_;
    int nozzle_max_temp_;
    int bed_min_temp_;
    int bed_max_temp_;

    //
    // LVGL subjects for XML data binding
    //
    lv_subject_t nozzle_current_subject_;
    lv_subject_t nozzle_target_subject_;
    lv_subject_t bed_current_subject_;
    lv_subject_t bed_target_subject_;
    lv_subject_t nozzle_display_subject_;
    lv_subject_t bed_display_subject_;

    // Subject string buffers
    std::array<char, 16> nozzle_current_buf_;
    std::array<char, 16> nozzle_target_buf_;
    std::array<char, 16> bed_current_buf_;
    std::array<char, 16> bed_target_buf_;
    std::array<char, 32> nozzle_display_buf_;
    std::array<char, 32> bed_display_buf_;

    //
    // Panel widgets
    //
    lv_obj_t* nozzle_panel_ = nullptr;
    lv_obj_t* bed_panel_ = nullptr;

    //
    // Graph widgets
    //
    ui_temp_graph_t* nozzle_graph_ = nullptr;
    ui_temp_graph_t* bed_graph_ = nullptr;
    int nozzle_series_id_ = -1;
    int bed_series_id_ = -1;

    //
    // Heater configurations
    //
    heater_config_t nozzle_config_;
    heater_config_t bed_config_;

    // Subjects initialized flag
    bool subjects_initialized_ = false;
};

