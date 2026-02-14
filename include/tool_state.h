// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "subject_managed_panel.h"

#include <lvgl.h>
#include <optional>
#include <string>
#include <vector>

#include "hv/json.hpp"

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
/// Subject updates are routed through ui_queue_update() from background threads.
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

    std::vector<ToolInfo> tools_;
    int active_tool_index_ = 0;
};

} // namespace helix
