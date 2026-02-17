// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "subject_managed_panel.h"

#include <functional>
#include <lvgl.h>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "hv/json.hpp"

class MoonrakerAPI;

namespace helix {

// Forward declaration
class PrinterDiscovery;

enum class DetectState {
    PRESENT = 0,
    ABSENT = 1,
    UNAVAILABLE = 2,
};

struct ToolInfo {
    int index = 0;
    std::string name = "T0";
    std::optional<std::string> extruder_name = "extruder";
    std::optional<std::string> heater_name;
    std::optional<std::string> fan_name;
    float gcode_x_offset = 0.0f;
    float gcode_y_offset = 0.0f;
    float gcode_z_offset = 0.0f;
    bool active = false;
    bool mounted = false;
    DetectState detect_state = DetectState::UNAVAILABLE;
    int backend_index = -1; ///< Which AMS backend feeds this tool (-1 = direct drive)
    int backend_slot = -1;  ///< Fixed slot in that backend (-1 = any/dynamic)

    // Spoolman spool assignment (persisted per-tool)
    int spoolman_id = 0;           ///< Spoolman spool ID (0=not tracked)
    std::string spool_name;        ///< Display name from Spoolman
    float remaining_weight_g = -1; ///< Remaining weight in grams (-1=unknown)
    float total_weight_g = -1;     ///< Total spool weight in grams (-1=unknown)

    [[nodiscard]] std::string effective_heater() const {
        if (heater_name)
            return *heater_name;
        if (extruder_name)
            return *extruder_name;
        return "extruder";
    }
};

/// Manages tool information for multi-tool printers (toolchangers, multi-extruder).
/// Thread safety: All public methods must be called from the LVGL/UI thread only.
/// Subject updates are routed through helix::ui::queue_update() from background threads.
class ToolState {
  public:
    static ToolState& instance();
    ToolState(const ToolState&) = delete;
    ToolState& operator=(const ToolState&) = delete;

    void init_subjects(bool register_xml = true);
    void deinit_subjects();

    void init_tools(const helix::PrinterDiscovery& hardware);
    void update_from_status(const nlohmann::json& status);

    [[nodiscard]] const std::vector<ToolInfo>& tools() const {
        return tools_;
    }
    [[nodiscard]] const ToolInfo* active_tool() const;
    [[nodiscard]] int active_tool_index() const {
        return active_tool_index_;
    }
    [[nodiscard]] int tool_count() const {
        return static_cast<int>(tools_.size());
    }
    [[nodiscard]] bool is_multi_tool() const {
        return tools_.size() > 1;
    }

    /// Returns "Nozzle" for single-tool, "Nozzle T0" for multi-tool (active tool).
    [[nodiscard]] std::string nozzle_label() const;

    /// Request a tool change, delegating to AMS backend or falling back to ACTIVATE_EXTRUDER.
    /// Callbacks are invoked asynchronously from the API response.
    void request_tool_change(int tool_index, MoonrakerAPI* api,
                             std::function<void()> on_success = nullptr,
                             std::function<void(const std::string&)> on_error = nullptr);

    /// Returns tool name (e.g. "T0") for the given extruder name, or empty if not found.
    [[nodiscard]] std::string tool_name_for_extruder(const std::string& extruder_name) const;

    /// Assign a Spoolman spool to a tool. Persists to local JSON + Moonraker DB.
    void assign_spool(int tool_index, int spoolman_id, const std::string& spool_name = "",
                      float remaining_g = -1, float total_g = -1);

    /// Clear spool assignment for a tool
    void clear_spool(int tool_index);

    /// Get set of Spoolman spool IDs currently assigned to tools,
    /// optionally excluding one tool index (e.g., the one being edited).
    [[nodiscard]] std::set<int> assigned_spool_ids(int exclude_tool = -1) const;

    /// Load persisted spool assignments (Moonraker DB → local JSON → empty)
    void load_spool_assignments(MoonrakerAPI* api);

    /// Save all spool assignments (local JSON + Moonraker DB fire-and-forget)
    void save_spool_assignments(MoonrakerAPI* api);

    /// Save spool assignments only if data has changed since last save
    void save_spool_assignments_if_dirty(MoonrakerAPI* api);

    /// Set the config directory for local JSON persistence (default: "config")
    void set_config_dir(const std::string& dir) {
        config_dir_ = dir;
    }

    lv_subject_t* get_active_tool_subject() {
        return &active_tool_;
    }
    lv_subject_t* get_tool_count_subject() {
        return &tool_count_;
    }
    lv_subject_t* get_tools_version_subject() {
        return &tools_version_;
    }

  private:
    ToolState() = default;
    SubjectManager subjects_;
    bool subjects_initialized_ = false;
    lv_subject_t active_tool_{};
    lv_subject_t tool_count_{};
    lv_subject_t tools_version_{};

    // Tool badge subjects for nozzle_icon component (XML-bound).
    // Updated automatically by update_from_status() and init_tools().
    lv_subject_t tool_badge_text_{};
    char tool_badge_text_buf_[16] = {};
    lv_subject_t show_tool_badge_{};

    void update_tool_badge();

    std::vector<ToolInfo> tools_;
    int active_tool_index_ = 0;
    std::string config_dir_ = "config"; ///< Directory for local JSON persistence
    bool spool_dirty_ = false;          ///< True when spool data changed since last save

    /// Save spool assignments to local JSON file
    void save_spool_json() const;

    /// Load spool assignments from local JSON file. Returns true on success.
    bool load_spool_json();

    /// Build JSON representation of current spool assignments
    [[nodiscard]] nlohmann::json spool_assignments_to_json() const;

    /// Apply spool assignments from JSON
    void apply_spool_assignments(const nlohmann::json& data);
};

} // namespace helix
