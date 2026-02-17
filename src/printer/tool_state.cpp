// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file tool_state.cpp
 * @brief ToolState singleton — models physical print heads (tools)
 *
 * Manages tool discovery from PrinterDiscovery and status updates
 * from Klipper's toolchanger / tool objects.
 */

#include "tool_state.h"

#include "printer_discovery.h"
#include "state/subject_macros.h"
#include "static_subject_registry.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>

namespace helix {

ToolState& ToolState::instance() {
    static ToolState instance;
    return instance;
}

void ToolState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        spdlog::debug("[ToolState] Subjects already initialized, skipping");
        return;
    }

    spdlog::trace("[ToolState] Initializing subjects (register_xml={})", register_xml);

    INIT_SUBJECT_INT(active_tool, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(tool_count, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(tools_version, 0, subjects_, register_xml);
    INIT_SUBJECT_STRING(tool_badge_text, "", subjects_, register_xml);
    INIT_SUBJECT_INT(show_tool_badge, 0, subjects_, register_xml);

    subjects_initialized_ = true;

    // Self-register cleanup — ensures deinit runs before lv_deinit()
    StaticSubjectRegistry::instance().register_deinit(
        "ToolState", []() { ToolState::instance().deinit_subjects(); });

    spdlog::trace("[ToolState] Subjects initialized successfully");
}

void ToolState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[ToolState] Deinitializing subjects");

    tools_.clear();
    active_tool_index_ = 0;

    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void ToolState::init_tools(const helix::PrinterDiscovery& hardware) {
    // Clear existing tools
    tools_.clear();

    if (hardware.has_tool_changer() && !hardware.tool_names().empty()) {
        // Tool changer: create N tools from discovered tool names
        const auto& tool_names = hardware.tool_names();

        // Collect extruder names from heaters (sorted for index mapping)
        std::vector<std::string> extruder_names;
        for (const auto& h : hardware.heaters()) {
            if (h == "extruder" ||
                (h.size() > 8 && h.rfind("extruder", 0) == 0 && std::isdigit(h[8]))) {
                extruder_names.push_back(h);
            }
        }
        std::sort(extruder_names.begin(), extruder_names.end());

        for (int i = 0; i < static_cast<int>(tool_names.size()); ++i) {
            ToolInfo tool;
            tool.index = i;
            tool.name = tool_names[i];

            // Map extruder by index if available
            if (i < static_cast<int>(extruder_names.size())) {
                tool.extruder_name = extruder_names[i];
            } else {
                tool.extruder_name = std::nullopt;
            }

            tool.heater_name = std::nullopt;
            tool.fan_name = std::nullopt;

            spdlog::debug("[ToolState] Tool {}: name={}, extruder={}", i, tool.name,
                          tool.extruder_name.value_or("none"));
            tools_.push_back(std::move(tool));
        }
    } else {
        // No tool changer: create 1 implicit tool
        ToolInfo tool;
        tool.index = 0;
        tool.name = "T0";
        tool.extruder_name = "extruder";
        tool.heater_name = std::nullopt;
        tool.fan_name = "fan";
        tool.active = true;

        spdlog::debug("[ToolState] Implicit single tool: T0");
        tools_.push_back(std::move(tool));
    }

    active_tool_index_ = 0;

    // Update subjects
    lv_subject_set_int(&tool_count_, static_cast<int>(tools_.size()));
    lv_subject_set_int(&active_tool_, active_tool_index_);
    int version = lv_subject_get_int(&tools_version_) + 1;
    lv_subject_set_int(&tools_version_, version);

    update_tool_badge();

    spdlog::info("[ToolState] Initialized {} tools (version {})", tools_.size(), version);
}

void ToolState::update_from_status(const nlohmann::json& status) {
    if (tools_.empty()) {
        return;
    }

    bool changed = false;

    // Parse active tool from toolchanger object
    if (status.contains("toolchanger") && status["toolchanger"].is_object()) {
        const auto& tc = status["toolchanger"];
        if (tc.contains("tool_number") && tc["tool_number"].is_number_integer()) {
            int new_index = tc["tool_number"].get<int>();
            if (new_index != active_tool_index_) {
                active_tool_index_ = new_index;
                lv_subject_set_int(&active_tool_, active_tool_index_);
                changed = true;
                spdlog::debug("[ToolState] Active tool changed to {}", active_tool_index_);
            }
        }
    }

    // Cross-check active tool from toolhead.extruder field
    // This handles non-toolchanger multi-extruder setups where the active
    // extruder changes but there's no "toolchanger" object in status
    if (status.contains("toolhead") && status["toolhead"].is_object()) {
        const auto& toolhead = status["toolhead"];
        if (toolhead.contains("extruder") && toolhead["extruder"].is_string()) {
            std::string ext_name = toolhead["extruder"].get<std::string>();
            // Find which tool maps to this extruder
            for (int i = 0; i < static_cast<int>(tools_.size()); ++i) {
                if (tools_[i].extruder_name.has_value() &&
                    tools_[i].extruder_name.value() == ext_name) {
                    if (i != active_tool_index_) {
                        active_tool_index_ = i;
                        lv_subject_set_int(&active_tool_, active_tool_index_);
                        changed = true;
                        spdlog::debug(
                            "[ToolState] Active tool updated to {} (from toolhead.extruder={})", i,
                            ext_name);
                    }
                    break;
                }
            }
        }
    }

    // Parse per-tool status updates
    for (auto& tool : tools_) {
        std::string key = "tool " + tool.name;
        if (!status.contains(key) || !status[key].is_object()) {
            continue;
        }
        const auto& tool_status = status[key];

        if (tool_status.contains("active") && tool_status["active"].is_boolean()) {
            bool val = tool_status["active"].get<bool>();
            if (val != tool.active) {
                tool.active = val;
                changed = true;
            }
        }

        if (tool_status.contains("mounted") && tool_status["mounted"].is_boolean()) {
            bool val = tool_status["mounted"].get<bool>();
            if (val != tool.mounted) {
                tool.mounted = val;
                changed = true;
            }
        }

        if (tool_status.contains("detect_state") && tool_status["detect_state"].is_string()) {
            std::string ds = tool_status["detect_state"].get<std::string>();
            DetectState new_state = DetectState::UNAVAILABLE;
            if (ds == "present") {
                new_state = DetectState::PRESENT;
            } else if (ds == "absent") {
                new_state = DetectState::ABSENT;
            }
            if (new_state != tool.detect_state) {
                tool.detect_state = new_state;
                changed = true;
            }
        }

        if (tool_status.contains("gcode_x_offset") && tool_status["gcode_x_offset"].is_number()) {
            tool.gcode_x_offset = tool_status["gcode_x_offset"].get<float>();
            changed = true;
        }
        if (tool_status.contains("gcode_y_offset") && tool_status["gcode_y_offset"].is_number()) {
            tool.gcode_y_offset = tool_status["gcode_y_offset"].get<float>();
            changed = true;
        }
        if (tool_status.contains("gcode_z_offset") && tool_status["gcode_z_offset"].is_number()) {
            tool.gcode_z_offset = tool_status["gcode_z_offset"].get<float>();
            changed = true;
        }

        if (tool_status.contains("extruder") && tool_status["extruder"].is_string()) {
            std::string ext = tool_status["extruder"].get<std::string>();
            std::optional<std::string> new_val = ext.empty() ? std::nullopt : std::optional(ext);
            if (new_val != tool.extruder_name) {
                tool.extruder_name = new_val;
                changed = true;
            }
        }

        if (tool_status.contains("fan") && tool_status["fan"].is_string()) {
            std::string fan = tool_status["fan"].get<std::string>();
            std::optional<std::string> new_val = fan.empty() ? std::nullopt : std::optional(fan);
            if (new_val != tool.fan_name) {
                tool.fan_name = new_val;
                changed = true;
            }
        }
    }

    if (changed) {
        update_tool_badge();
        int version = lv_subject_get_int(&tools_version_) + 1;
        lv_subject_set_int(&tools_version_, version);
        spdlog::trace("[ToolState] Status updated, version {}", version);
    }
}

const ToolInfo* ToolState::active_tool() const {
    if (active_tool_index_ < 0 || active_tool_index_ >= static_cast<int>(tools_.size())) {
        return nullptr;
    }
    return &tools_[active_tool_index_];
}

std::string ToolState::nozzle_label() const {
    if (!is_multi_tool()) {
        return "Nozzle";
    }
    const auto* tool = active_tool();
    if (tool) {
        return "Nozzle " + tool->name;
    }
    return "Nozzle";
}

void ToolState::update_tool_badge() {
    if (!subjects_initialized_) {
        return;
    }

    const auto* tool = is_multi_tool() ? active_tool() : nullptr;
    if (tool) {
        std::snprintf(tool_badge_text_buf_, sizeof(tool_badge_text_buf_), "%s", tool->name.c_str());
    } else {
        tool_badge_text_buf_[0] = '\0';
    }

    lv_subject_copy_string(&tool_badge_text_, tool_badge_text_buf_);
    lv_subject_set_int(&show_tool_badge_, tool ? 1 : 0);
}

std::string ToolState::tool_name_for_extruder(const std::string& extruder_name) const {
    for (const auto& tool : tools_) {
        if (tool.extruder_name && *tool.extruder_name == extruder_name) {
            return tool.name;
        }
    }
    return {};
}

} // namespace helix
