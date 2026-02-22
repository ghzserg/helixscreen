// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_heater_config.h"
#include "ui_heating_animator.h"
#include "ui_observer_guard.h"
#include "ui_temp_graph.h"

#include "lvgl/lvgl.h"
#include "panel_lifecycle.h"
#include "subject_managed_panel.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

// Forward declarations
namespace helix {
class PrinterState;
}
class MoonrakerAPI;
class TempControlPanel;

// ─────────────────────────────────────────────────────────────────────────────
// Per-heater state (replaces duplicated nozzle_*/bed_* fields)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Encapsulates all per-heater state for one temperature panel.
 *
 * One instance per heater type (nozzle, bed, chamber). Holds config,
 * temperature state, LVGL subjects, graph data, and observer handles.
 */
struct HeaterState {
    heater_config_t config{};

    // Temperature state (centidegrees)
    int current = 25;
    int target = 0;
    int pending = -1; // -1 = no pending selection (user picked but not confirmed)
    int min_temp = 0;
    int max_temp = 0;

    // Status thresholds
    int cooling_threshold_centi = 0; ///< Above this when target=0 → "Cooling down"

    // Chamber-specific: read-only when sensor-only (no heater present)
    bool read_only = false;

    // Klipper object name for set_temperature() API calls
    std::string klipper_name;

    // LVGL subjects for XML data binding
    lv_subject_t display_subject{};
    lv_subject_t status_subject{};
    lv_subject_t heating_subject{}; ///< 0=off, 1=on (for icon visibility)

    // Subject string buffers
    std::array<char, 32> display_buf{};
    std::array<char, 64> status_buf{};

    // Panel widget (the overlay lv_obj)
    lv_obj_t* panel = nullptr;

    // Heating icon animator (gradient color + pulse while heating)
    HeatingIconAnimator animator;

    // Graph widget
    ui_temp_graph_t* graph = nullptr;
    int series_id = -1;
    int64_t last_graph_update_ms = 0;

    // External graphs registered for this heater's temperature updates
    struct RegisteredGraph {
        ui_temp_graph_t* graph;
        int series_id;
    };
    std::vector<RegisteredGraph> temp_graphs;

    // Observer handles (RAII cleanup)
    ObserverGuard temp_observer;
    ObserverGuard target_observer;
};

// ─────────────────────────────────────────────────────────────────────────────
// Generic lifecycle wrapper (replaces NozzleTempPanelLifecycle + BedTempPanelLifecycle)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Generic lifecycle wrapper for heater temperature panels
 *
 * Thin wrapper that implements IPanelLifecycle and delegates to TempControlPanel
 * for the specified heater type. One instance per heater type.
 */
class HeaterTempPanelLifecycle : public IPanelLifecycle {
  public:
    HeaterTempPanelLifecycle(TempControlPanel* panel, helix::HeaterType type, const char* name)
        : panel_(panel), type_(type), name_(name) {}

    const char* get_name() const override {
        return name_;
    }
    void on_activate() override;
    void on_deactivate() override;

    helix::HeaterType type() const {
        return type_;
    }

  private:
    TempControlPanel* panel_;
    helix::HeaterType type_;
    const char* name_;
};

// Keep backward-compat type aliases for existing code
using NozzleTempPanelLifecycle = HeaterTempPanelLifecycle;
using BedTempPanelLifecycle = HeaterTempPanelLifecycle;

// ─────────────────────────────────────────────────────────────────────────────
// Preset button user_data (for generic preset callback)
// ─────────────────────────────────────────────────────────────────────────────

struct PresetButtonData {
    TempControlPanel* panel;
    helix::HeaterType heater_type;
    int preset_value; ///< Target temperature in degrees (0 = off)
};

// ─────────────────────────────────────────────────────────────────────────────
// TempControlPanel
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Temperature Control Panel - manages nozzle, bed, and chamber temperature UI
 *
 * Unified panel that handles all heater types through a HeaterState array.
 * Each heater has its own overlay panel, graph, presets, and lifecycle.
 */
class TempControlPanel {
  public:
    TempControlPanel(helix::PrinterState& printer_state, MoonrakerAPI* api);
    ~TempControlPanel();

    // Non-copyable, non-movable (has reference member and LVGL subject state)
    TempControlPanel(const TempControlPanel&) = delete;
    TempControlPanel& operator=(const TempControlPanel&) = delete;
    TempControlPanel(TempControlPanel&&) = delete;
    TempControlPanel& operator=(TempControlPanel&&) = delete;

    // ── Generic heater API ──────────────────────────────────────────────
    void setup_panel(helix::HeaterType type, lv_obj_t* panel, lv_obj_t* parent_screen);
    void on_panel_activate(helix::HeaterType type);
    void on_panel_deactivate(helix::HeaterType type);
    HeaterTempPanelLifecycle* get_lifecycle(helix::HeaterType type);

    // ── Backward-compat wrappers ────────────────────────────────────────
    void setup_nozzle_panel(lv_obj_t* panel, lv_obj_t* parent_screen) {
        setup_panel(helix::HeaterType::Nozzle, panel, parent_screen);
    }
    void setup_bed_panel(lv_obj_t* panel, lv_obj_t* parent_screen) {
        setup_panel(helix::HeaterType::Bed, panel, parent_screen);
    }
    void setup_chamber_panel(lv_obj_t* panel, lv_obj_t* parent_screen) {
        setup_panel(helix::HeaterType::Chamber, panel, parent_screen);
    }
    NozzleTempPanelLifecycle* get_nozzle_lifecycle() {
        return get_lifecycle(helix::HeaterType::Nozzle);
    }
    BedTempPanelLifecycle* get_bed_lifecycle() {
        return get_lifecycle(helix::HeaterType::Bed);
    }
    HeaterTempPanelLifecycle* get_chamber_lifecycle() {
        return get_lifecycle(helix::HeaterType::Chamber);
    }

    void init_subjects();
    void deinit_subjects();

    // ── Setters (centidegrees, used by tests and PrinterState observers) ──
    void set_heater(helix::HeaterType type, int current, int target);
    void set_heater_limits(helix::HeaterType type, int min_temp, int max_temp);

    // Backward-compat
    void set_nozzle(int current, int target) {
        set_heater(helix::HeaterType::Nozzle, current, target);
    }
    void set_bed(int current, int target) {
        set_heater(helix::HeaterType::Bed, current, target);
    }
    void set_nozzle_limits(int min_temp, int max_temp) {
        set_heater_limits(helix::HeaterType::Nozzle, min_temp, max_temp);
    }
    void set_bed_limits(int min_temp, int max_temp) {
        set_heater_limits(helix::HeaterType::Bed, min_temp, max_temp);
    }

    // Getters (centidegrees)
    int get_nozzle_target() const {
        return heaters_[static_cast<int>(helix::HeaterType::Nozzle)].target;
    }
    int get_bed_target() const {
        return heaters_[static_cast<int>(helix::HeaterType::Bed)].target;
    }
    int get_nozzle_current() const {
        return heaters_[static_cast<int>(helix::HeaterType::Nozzle)].current;
    }
    int get_bed_current() const {
        return heaters_[static_cast<int>(helix::HeaterType::Bed)].current;
    }

    void set_api(MoonrakerAPI* api) {
        api_ = api;
    }

    // ── Mini combined graph (filament panel) ────────────────────────────
    void setup_mini_combined_graph(lv_obj_t* container);

    // ── External graph registration ─────────────────────────────────────
    void register_heater_graph(ui_temp_graph_t* graph, int series_id, const std::string& heater);
    void unregister_heater_graph(ui_temp_graph_t* graph);

    // ── XML event callbacks (public static for XML registration) ────────
    static void on_heater_preset_clicked(lv_event_t* e);
    static void on_heater_confirm_clicked(lv_event_t* e);
    static void on_heater_custom_clicked(lv_event_t* e);

    // Backward-compat callbacks (still registered for old XML files during transition)
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

    // ── Access to HeaterState for lazy overlay helper ────────────────────
    HeaterState& heater(helix::HeaterType type) {
        return heaters_[static_cast<int>(type)];
    }
    const char* xml_component_name(helix::HeaterType type) const;

  private:
    // ── Generic instance methods ────────────────────────────────────────
    void on_temp_changed(helix::HeaterType type, int temp_centi);
    void on_target_changed(helix::HeaterType type, int target_centi);
    void update_display(helix::HeaterType type);
    void update_status(helix::HeaterType type);
    void send_temperature(helix::HeaterType type, int target);
    void update_graphs(helix::HeaterType type, float temp_deg, int64_t now_ms);
    void replay_history_to_graph(helix::HeaterType type);

    // ── Graph helpers ───────────────────────────────────────────────────
    ui_temp_graph_t* create_temp_graph(lv_obj_t* chart_area, const heater_config_t* config,
                                       int target_temp, int* series_id_out);
    void update_mini_graph_y_axis(float nozzle_deg, float bed_deg);
    void replay_history_to_mini_graph();
    void replay_history_from_manager(ui_temp_graph_t* graph, int series_id,
                                     const std::string& heater_name);

    // Keypad callback
    static void keypad_value_cb(float value, void* user_data);

    helix::PrinterState& printer_state_;
    MoonrakerAPI* api_;

    // ── Per-heater state (indexed by HeaterType) ────────────────────────
    std::array<HeaterState, helix::HEATER_TYPE_COUNT> heaters_;

    // ── Multi-extruder support (nozzle-specific) ────────────────────────
    std::string active_extruder_name_ = "extruder";
    ObserverGuard extruder_version_observer_;
    ObserverGuard active_tool_observer_;

    void select_extruder(const std::string& name);
    void rebuild_extruder_segments();
    void rebuild_extruder_segments_impl();

    // ── Mini combined graph (filament panel) ────────────────────────────
    ui_temp_graph_t* mini_graph_ = nullptr;
    int mini_nozzle_series_id_ = -1;
    int mini_bed_series_id_ = -1;
    float mini_graph_y_max_ = 150.0f;

    // ── Graph update throttling ─────────────────────────────────────────
    static constexpr int64_t GRAPH_SAMPLE_INTERVAL_MS = 1000;

    // ── Subject management ──────────────────────────────────────────────
    SubjectManager subjects_;
    bool subjects_initialized_ = false;

    // ── Lifecycle wrappers (owned by this object) ───────────────────────
    HeaterTempPanelLifecycle nozzle_lifecycle_{this, helix::HeaterType::Nozzle,
                                               "Nozzle Temperature"};
    HeaterTempPanelLifecycle bed_lifecycle_{this, helix::HeaterType::Bed, "Bed Temperature"};
    HeaterTempPanelLifecycle chamber_lifecycle_{this, helix::HeaterType::Chamber,
                                                "Chamber Temperature"};

    // ── Static preset button data (LVGL holds raw pointers) ─────────────
    // 4 presets per heater × 3 heater types = 12 slots
    static constexpr int PRESETS_PER_HEATER = 4;
    std::array<PresetButtonData, helix::HEATER_TYPE_COUNT * PRESETS_PER_HEATER> preset_data_{};
};
