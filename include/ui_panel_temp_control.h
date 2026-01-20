// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_heater_config.h"
#include "ui_heating_animator.h"
#include "ui_observer_guard.h"
#include "ui_temp_graph.h"

#include "lvgl/lvgl.h"
#include "subject_managed_panel.h"
#include "ui/temperature_observer_bundle.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

// Forward declarations
class PrinterState;
class MoonrakerAPI;

/**
 * @brief Temperature Control Panel - manages nozzle and bed temperature UI
 */
class TempControlPanel {
  public:
    TempControlPanel(PrinterState& printer_state, MoonrakerAPI* api);
    ~TempControlPanel();

    // Non-copyable, non-movable (has reference member and LVGL subject state)
    TempControlPanel(const TempControlPanel&) = delete;
    TempControlPanel& operator=(const TempControlPanel&) = delete;
    TempControlPanel(TempControlPanel&&) = delete;
    TempControlPanel& operator=(TempControlPanel&&) = delete;

    void setup_nozzle_panel(lv_obj_t* panel, lv_obj_t* parent_screen);
    void setup_bed_panel(lv_obj_t* panel, lv_obj_t* parent_screen);
    void init_subjects();
    void deinit_subjects();

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

    /**
     * @brief Setup a compact combined temperature graph for the filament panel
     *
     * Creates a 5-minute graph with both nozzle and bed temperature series.
     * Replays recent history from internal buffers and updates in real-time.
     *
     * @param container The LVGL object to create the graph in
     */
    void setup_mini_combined_graph(lv_obj_t* container);

    // XML event callbacks (public static for XML registration)
    static void on_nozzle_confirm_clicked(lv_event_t* e);
    static void on_bed_confirm_clicked(lv_event_t* e);
    static void on_nozzle_preset_off_clicked(lv_event_t* e);
    static void on_nozzle_preset_pla_clicked(lv_event_t* e);
    static void on_nozzle_preset_petg_clicked(lv_event_t* e);
    static void on_nozzle_preset_abs_clicked(lv_event_t* e);
    static void on_bed_preset_off_clicked(lv_event_t* e);
    static void on_bed_preset_pla_clicked(lv_event_t* e);
    static void on_bed_preset_petg_clicked(lv_event_t* e);
    static void on_bed_preset_abs_clicked(lv_event_t* e);
    static void on_nozzle_custom_clicked(lv_event_t* e);
    static void on_bed_custom_clicked(lv_event_t* e);

  private:
    // Instance methods called by observers (via observer factory pattern)
    void on_nozzle_temp_changed(int temp);
    void on_nozzle_target_changed(int target);
    void on_bed_temp_changed(int temp);
    void on_bed_target_changed(int target);

    // Display update helpers
    void update_nozzle_display();
    void update_bed_display();

    // Send temperature command to printer (immediate action)
    void send_nozzle_temperature(int target);
    void send_bed_temperature(int target);

    // Status text and icon color update helpers
    void update_nozzle_status();
    void update_bed_status();
    void update_nozzle_icon_color();
    void update_bed_icon_color();

    // Graph creation helper
    ui_temp_graph_t* create_temp_graph(lv_obj_t* chart_area, const heater_config_t* config,
                                       int target_temp, int* series_id_out);

    // Keypad callback
    static void keypad_value_cb(float value, void* user_data);

    PrinterState& printer_state_;
    MoonrakerAPI* api_;

    // Observer handles (RAII cleanup via ObserverGuard)
    /// @brief Temperature observer bundle (nozzle + bed temps)
    helix::ui::TemperatureObserverBundle<TempControlPanel> temp_observers_;

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

    // Graph widgets (main panels)
    ui_temp_graph_t* nozzle_graph_ = nullptr;
    ui_temp_graph_t* bed_graph_ = nullptr;
    int nozzle_series_id_ = -1;
    int bed_series_id_ = -1;

    // Mini combined graph (filament panel)
    ui_temp_graph_t* mini_graph_ = nullptr;
    int mini_nozzle_series_id_ = -1;
    int mini_bed_series_id_ = -1;
    float mini_graph_y_max_ = 150.0f; // Dynamic Y-max, starts at 150°C

    // ─────────────────────────────────────────────────────────────────────────
    // Graph Registration System
    // Allows multiple graphs to receive temperature updates with a single loop
    // ─────────────────────────────────────────────────────────────────────────
    struct RegisteredGraph {
        ui_temp_graph_t* graph;
        int series_id;
    };
    std::vector<RegisteredGraph> nozzle_temp_graphs_; // Graphs receiving nozzle temp updates
    std::vector<RegisteredGraph> bed_temp_graphs_;    // Graphs receiving bed temp updates

    // Internal helpers for unified graph updates
    void update_nozzle_graphs(float temp_deg, int64_t now_ms);
    void update_bed_graphs(float temp_deg, int64_t now_ms);
    void update_mini_graph_y_axis(float nozzle_deg, float bed_deg);

    // Graph update throttling (1 sample per second max)
    // Moonraker sends at ~4Hz, but we only graph at 1Hz to show 20 minutes
    static constexpr int64_t GRAPH_SAMPLE_INTERVAL_MS = 1000; // 1 second
    int64_t nozzle_last_graph_update_ms_ = 0;
    int64_t bed_last_graph_update_ms_ = 0;

    heater_config_t nozzle_config_;
    heater_config_t bed_config_;

    // Subject manager for RAII cleanup
    SubjectManager subjects_;

    // Subjects initialized flag
    bool subjects_initialized_ = false;

    // Helper to replay buffered history to graph when panel opens
    void replay_nozzle_history_to_graph();
    void replay_bed_history_to_graph();
    void replay_history_to_mini_graph();

    // Helper to replay history from TemperatureHistoryManager
    void replay_history_from_manager(ui_temp_graph_t* graph, int series_id,
                                     const std::string& heater_name);
};
