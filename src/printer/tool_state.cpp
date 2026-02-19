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

#include "ui_update_queue.h"

#include "ams_state.h"
#include "moonraker_api.h"
#include "printer_discovery.h"
#include "state/subject_macros.h"
#include "static_subject_registry.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

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
        // No tool changer: enumerate extruder heaters to support multi-extruder setups
        std::vector<std::string> extruder_names;
        for (const auto& h : hardware.heaters()) {
            if (h == "extruder" ||
                (h.size() > 8 && h.rfind("extruder", 0) == 0 && std::isdigit(h[8]))) {
                extruder_names.push_back(h);
            }
        }
        std::sort(extruder_names.begin(), extruder_names.end());
        if (extruder_names.empty())
            extruder_names.push_back("extruder");

        for (int i = 0; i < static_cast<int>(extruder_names.size()); ++i) {
            ToolInfo tool;
            tool.index = i;
            tool.name = ::fmt::format("T{}", i);
            tool.extruder_name = extruder_names[i];
            tool.heater_name = std::nullopt;
            tool.fan_name = (i == 0) ? std::optional<std::string>("fan") : std::nullopt;
            tool.active = (i == 0);

            spdlog::debug("[ToolState] Tool {}: name={}, extruder={}", i, tool.name,
                          tool.extruder_name.value_or("none"));
            tools_.push_back(std::move(tool));
        }
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

void ToolState::request_tool_change(int tool_index, MoonrakerAPI* api,
                                    std::function<void()> on_success,
                                    std::function<void(const std::string&)> on_error) {
    if (tool_index < 0 || tool_index >= static_cast<int>(tools_.size())) {
        if (on_error)
            on_error(
                ::fmt::format("Invalid tool index {} (have {} tools)", tool_index, tools_.size()));
        return;
    }

    if (tool_index == active_tool_index_) {
        spdlog::debug("[ToolState] Tool {} already active, ignoring", tool_index);
        if (on_success)
            on_success();
        return;
    }

    if (!api) {
        if (on_error)
            on_error("No API connection");
        return;
    }

    // Try AMS backend if it manages this tool (AFC, Happy Hare, etc.)
    // Skip the backend if it has no slots configured (e.g., AFC module loaded but no hardware)
    // or if this tool isn't in the backend's tool-to-slot map.
    auto* backend = AmsState::instance().get_backend();
    if (backend) {
        AmsSystemInfo info = backend->get_system_info();
        bool backend_manages_tool = info.total_slots > 0 &&
                                    tool_index < static_cast<int>(info.tool_to_slot_map.size()) &&
                                    info.tool_to_slot_map[tool_index] >= 0;

        if (backend_manages_tool) {
            spdlog::info("[ToolState] Requesting tool change to T{} via AMS backend", tool_index);
            auto result = backend->change_tool(tool_index);
            if (result) {
                if (on_success)
                    on_success();
            } else {
                if (on_error)
                    on_error(::fmt::format("Backend tool change failed: {}", result.user_msg));
            }
            return;
        }

        spdlog::debug("[ToolState] AMS backend present but doesn't manage T{}, using direct gcode",
                      tool_index);
    }

    // Fallback: ACTIVATE_EXTRUDER for simple multi-extruder setups
    const auto& extruder_name = tools_[tool_index].extruder_name.value_or("extruder");
    std::string gcode = ::fmt::format("ACTIVATE_EXTRUDER EXTRUDER={}", extruder_name);
    spdlog::info("[ToolState] Requesting tool change to T{} via ACTIVATE_EXTRUDER ({})", tool_index,
                 extruder_name);

    api->execute_gcode(
        gcode,
        [on_success]() {
            if (on_success)
                on_success();
        },
        [on_error](const MoonrakerError& error) {
            if (on_error)
                on_error(error.user_message());
        });
}

// ============================================================================
// Spool assignment persistence
// ============================================================================

static constexpr const char* SPOOL_JSON_FILENAME = "tool_spools.json";
static constexpr const char* MOONRAKER_DB_NAMESPACE = "helix-screen";
static constexpr const char* MOONRAKER_DB_KEY = "tool_spool_assignments";

void ToolState::assign_spool(int tool_index, int spoolman_id, const std::string& spool_name,
                             float remaining_g, float total_g) {
    if (tool_index < 0 || tool_index >= static_cast<int>(tools_.size())) {
        // Normal on single-extruder AFC/MMU setups where lanes map to virtual
        // tools (T0-T3) but only one real extruder exists
        spdlog::trace("[ToolState] assign_spool: skipping tool index {} (have {} tools)",
                      tool_index, tools_.size());
        return;
    }

    auto& tool = tools_[tool_index];

    // Skip if nothing changed (avoids unnecessary saves from frequent syncs)
    if (tool.spoolman_id == spoolman_id && tool.spool_name == spool_name &&
        tool.remaining_weight_g == remaining_g && tool.total_weight_g == total_g) {
        return;
    }

    tool.spoolman_id = spoolman_id;
    tool.spool_name = spool_name;
    tool.remaining_weight_g = remaining_g;
    tool.total_weight_g = total_g;
    spool_dirty_ = true;

    spdlog::info("[ToolState] Assigned spool {} ({}) to tool {}", spoolman_id, spool_name,
                 tool_index);

    // Bump version so UI observers update
    if (subjects_initialized_) {
        int version = lv_subject_get_int(&tools_version_) + 1;
        lv_subject_set_int(&tools_version_, version);
    }
}

void ToolState::clear_spool(int tool_index) {
    if (tool_index < 0 || tool_index >= static_cast<int>(tools_.size())) {
        spdlog::warn("[ToolState] clear_spool: invalid tool index {}", tool_index);
        return;
    }

    auto& tool = tools_[tool_index];

    // Skip if already cleared
    if (tool.spoolman_id == 0) {
        return;
    }

    tool.spoolman_id = 0;
    tool.spool_name.clear();
    tool.remaining_weight_g = -1;
    tool.total_weight_g = -1;
    spool_dirty_ = true;

    spdlog::info("[ToolState] Cleared spool assignment for tool {}", tool_index);

    if (subjects_initialized_) {
        int version = lv_subject_get_int(&tools_version_) + 1;
        lv_subject_set_int(&tools_version_, version);
    }
}

std::set<int> ToolState::assigned_spool_ids(int exclude_tool) const {
    std::set<int> ids;
    for (int i = 0; i < static_cast<int>(tools_.size()); ++i) {
        if (i == exclude_tool)
            continue;
        if (tools_[i].spoolman_id > 0) {
            ids.insert(tools_[i].spoolman_id);
        }
    }
    return ids;
}

nlohmann::json ToolState::spool_assignments_to_json() const {
    nlohmann::json result = nlohmann::json::object();

    for (const auto& tool : tools_) {
        if (tool.spoolman_id <= 0)
            continue;

        nlohmann::json entry;
        entry["spoolman_id"] = tool.spoolman_id;
        entry["spool_name"] = tool.spool_name;
        if (tool.remaining_weight_g >= 0)
            entry["remaining_weight_g"] = tool.remaining_weight_g;
        if (tool.total_weight_g >= 0)
            entry["total_weight_g"] = tool.total_weight_g;

        result[std::to_string(tool.index)] = entry;
    }

    return result;
}

void ToolState::apply_spool_assignments(const nlohmann::json& data) {
    if (!data.is_object()) {
        spdlog::warn("[ToolState] apply_spool_assignments: expected JSON object");
        return;
    }

    for (auto& tool : tools_) {
        auto key = std::to_string(tool.index);
        if (!data.contains(key) || !data[key].is_object()) {
            continue;
        }

        const auto& entry = data[key];
        tool.spoolman_id = entry.value("spoolman_id", 0);
        tool.spool_name = entry.value("spool_name", std::string{});
        tool.remaining_weight_g = entry.value("remaining_weight_g", -1.0f);
        tool.total_weight_g = entry.value("total_weight_g", -1.0f);

        if (tool.spoolman_id > 0) {
            spdlog::debug("[ToolState] Loaded spool {} ({}) for tool {}", tool.spoolman_id,
                          tool.spool_name, tool.index);
        }
    }
}

void ToolState::save_spool_json() const {
    namespace fs = std::filesystem;

    auto json_data = spool_assignments_to_json();
    auto path = fs::path(config_dir_) / SPOOL_JSON_FILENAME;

    try {
        // Ensure directory exists
        fs::create_directories(config_dir_);

        std::ofstream ofs(path);
        if (!ofs.is_open()) {
            spdlog::warn("[ToolState] Failed to open {} for writing", path.string());
            return;
        }
        ofs << json_data.dump(2);
        spdlog::debug("[ToolState] Saved spool assignments to {}", path.string());
    } catch (const std::exception& e) {
        spdlog::warn("[ToolState] Error saving spool JSON: {}", e.what());
    }
}

bool ToolState::load_spool_json() {
    namespace fs = std::filesystem;

    auto path = fs::path(config_dir_) / SPOOL_JSON_FILENAME;

    if (!fs::exists(path)) {
        spdlog::debug("[ToolState] No spool JSON file at {}", path.string());
        return false;
    }

    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            spdlog::warn("[ToolState] Failed to open {}", path.string());
            return false;
        }

        auto data = nlohmann::json::parse(ifs);
        apply_spool_assignments(data);
        spdlog::info("[ToolState] Loaded spool assignments from {}", path.string());
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("[ToolState] Error loading spool JSON: {}", e.what());
        return false;
    }
}

void ToolState::save_spool_assignments_if_dirty(MoonrakerAPI* api) {
    if (!spool_dirty_) {
        return;
    }
    save_spool_assignments(api);
}

void ToolState::save_spool_assignments(MoonrakerAPI* api) {
    // Always save to local JSON (fast, reliable)
    save_spool_json();
    spool_dirty_ = false;

    // Fire-and-forget to Moonraker DB (async, best-effort)
    if (api) {
        auto json_data = spool_assignments_to_json();
        api->database_post_item(
            MOONRAKER_DB_NAMESPACE, MOONRAKER_DB_KEY, json_data,
            []() { spdlog::debug("[ToolState] Spool assignments saved to Moonraker DB"); },
            [](const MoonrakerError& err) {
                spdlog::warn("[ToolState] Failed to save to Moonraker DB: {}", err.user_message());
            });
    }
}

void ToolState::load_spool_assignments(MoonrakerAPI* api) {
    if (!api) {
        // No API — try local JSON only
        load_spool_json();
        return;
    }

    // Try Moonraker DB first. Callbacks fire from WebSocket thread,
    // so we marshal back to UI thread via queue_update().
    api->database_get_item(
        MOONRAKER_DB_NAMESPACE, MOONRAKER_DB_KEY,
        [this](const nlohmann::json& data) {
            // Copy data for thread-safe transfer to UI thread
            auto data_copy = std::make_unique<nlohmann::json>(data);
            helix::ui::queue_update<nlohmann::json>(
                std::move(data_copy), [this](nlohmann::json* d) {
                    apply_spool_assignments(*d);
                    save_spool_json();
                    spdlog::info("[ToolState] Loaded spool assignments from Moonraker DB");
                });
        },
        [this](const MoonrakerError& err) {
            spdlog::debug("[ToolState] Moonraker DB load failed ({}), trying local JSON",
                          err.user_message());
            helix::ui::queue_update<int>(std::make_unique<int>(0),
                                         [this](int*) { load_spool_json(); });
        });
}

} // namespace helix
