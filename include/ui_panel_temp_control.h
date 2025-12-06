// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_heater_config.h"
#include "ui_heating_animator.h"
#include "ui_observer_guard.h"
#include "ui_temp_graph.h"

#include "lvgl/lvgl.h"

#include <array>
#include <functional>
#include <string>

// Forward declarations
class PrinterState;
class MoonrakerAPI;

/**
 * @brief Temperature Control Panel - manages nozzle and bed temperature UI
 */
class TempControlPanel {
  public:
    TempControlPanel(PrinterState& printer_state, MoonrakerAPI* api);
    ~TempControlPanel() = default;

    // Non-copyable, non-movable (has reference member and LVGL subject state)
    TempControlPanel(const TempControlPanel&) = delete;
    TempControlPanel& operator=(const TempControlPanel&) = delete;
    TempControlPanel(TempControlPanel&&) = delete;
    TempControlPanel& operator=(TempControlPanel&&) = delete;

    void setup_nozzle_panel(lv_obj_t* panel, lv_obj_t* parent_screen);
    void setup_bed_panel(lv_obj_t* panel, lv_obj_t* parent_screen);
    void init_subjects();

    void set_nozzle(int current, int target);
    void set_bed(int current, int target);
    int get_nozzle_target() const {
        return nozzle_target_;
    }
    int get_bed_target() const {
        return bed_target_;
    }
    int get_nozzle_current() const {
        return nozzle_current_;
    }
    int get_bed_current() const {
        return bed_current_;
    }
    void set_nozzle_limits(int min_temp, int max_temp);
    void set_bed_limits(int min_temp, int max_temp);
    void set_api(MoonrakerAPI* api) {
        api_ = api;
    }

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

    // Status text and icon color update helpers
    void update_nozzle_status();
    void update_bed_status();
    void update_nozzle_icon_color();
    void update_bed_icon_color();

    // Graph creation helper
    ui_temp_graph_t* create_temp_graph(lv_obj_t* chart_area, const heater_config_t* config,
                                       int target_temp, int* series_id_out);

    // Y-axis label creation
    void create_y_axis_labels(lv_obj_t* container, const heater_config_t* config);

    // X-axis time label creation and update
    static constexpr int X_AXIS_LABEL_COUNT = 6;
    void create_x_axis_labels(lv_obj_t* container,
                              std::array<lv_obj_t*, X_AXIS_LABEL_COUNT>& labels);
    void update_x_axis_labels(std::array<lv_obj_t*, X_AXIS_LABEL_COUNT>& labels,
                              int64_t start_time_ms, int point_count);

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

    PrinterState& printer_state_;
    MoonrakerAPI* api_;

    // Observer handles (RAII cleanup via ObserverGuard)
    ObserverGuard nozzle_temp_observer_;
    ObserverGuard nozzle_target_observer_;
    ObserverGuard bed_temp_observer_;
    ObserverGuard bed_target_observer_;

    // Temperature state
    int nozzle_current_ = 25;
    int nozzle_target_ = 0;
    int bed_current_ = 25;
    int bed_target_ = 0;

    // Pending selection (user picked but not confirmed yet)
    int nozzle_pending_ = -1; // -1 = no pending selection
    int bed_pending_ = -1;    // -1 = no pending selection

    // Temperature limits
    int nozzle_min_temp_;
    int nozzle_max_temp_;
    int bed_min_temp_;
    int bed_max_temp_;

    // LVGL subjects for XML data binding
    lv_subject_t nozzle_current_subject_;
    lv_subject_t nozzle_target_subject_;
    lv_subject_t bed_current_subject_;
    lv_subject_t bed_target_subject_;
    lv_subject_t nozzle_display_subject_;
    lv_subject_t bed_display_subject_;

    // Graph point count subjects (for reactive X-axis label visibility)
    lv_subject_t nozzle_graph_points_subject_;
    lv_subject_t bed_graph_points_subject_;

    // Status text subjects (for reactive status messages)
    lv_subject_t nozzle_status_subject_;
    lv_subject_t bed_status_subject_;

    // Heating state subjects (0=off, 1=on) for reactive icon visibility in XML
    lv_subject_t nozzle_heating_subject_;
    lv_subject_t bed_heating_subject_;

    // Subject string buffers
    std::array<char, 16> nozzle_current_buf_;
    std::array<char, 16> nozzle_target_buf_;
    std::array<char, 16> bed_current_buf_;
    std::array<char, 16> bed_target_buf_;
    std::array<char, 32> nozzle_display_buf_;
    std::array<char, 32> bed_display_buf_;
    std::array<char, 64> nozzle_status_buf_;
    std::array<char, 64> bed_status_buf_;

    // Panel widgets
    lv_obj_t* nozzle_panel_ = nullptr;
    lv_obj_t* bed_panel_ = nullptr;

    // Heating icon animators (gradient color + pulse while heating)
    HeatingIconAnimator nozzle_animator_;
    HeatingIconAnimator bed_animator_;

    // Graph widgets
    ui_temp_graph_t* nozzle_graph_ = nullptr;
    ui_temp_graph_t* bed_graph_ = nullptr;
    int nozzle_series_id_ = -1;
    int bed_series_id_ = -1;

    // X-axis time label storage
    std::array<lv_obj_t*, X_AXIS_LABEL_COUNT> nozzle_x_labels_{};
    std::array<lv_obj_t*, X_AXIS_LABEL_COUNT> bed_x_labels_{};

    // Timestamp tracking for X-axis labels
    int64_t nozzle_start_time_ms_ = 0;
    int64_t bed_start_time_ms_ = 0;
    int nozzle_point_count_ = 0;
    int bed_point_count_ = 0;

    heater_config_t nozzle_config_;
    heater_config_t bed_config_;

    // Subjects initialized flag
    bool subjects_initialized_ = false;

    // ─────────────────────────────────────────────────────────────────────────
    // Background temperature history buffers
    // Store temperature readings from app start so graphs show data immediately
    // ─────────────────────────────────────────────────────────────────────────
    static constexpr int TEMP_HISTORY_SIZE = 300; // Match graph point count (5 min @ 1/sec)

    // Circular buffer storage (temp values + timestamps)
    struct TempSample {
        int temp;
        int64_t timestamp_ms;
    };
    std::array<TempSample, TEMP_HISTORY_SIZE> nozzle_history_{};
    std::array<TempSample, TEMP_HISTORY_SIZE> bed_history_{};

    // Circular buffer write indices
    int nozzle_history_count_ = 0; // Total samples received (for wrap detection)
    int bed_history_count_ = 0;

    // Helper to replay buffered history to graph when panel opens
    void replay_nozzle_history_to_graph();
    void replay_bed_history_to_graph();
};
