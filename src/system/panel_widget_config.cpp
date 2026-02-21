// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "panel_widget_config.h"

#include "config.h"
#include "panel_widget_registry.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <set>

namespace helix {

PanelWidgetConfig::PanelWidgetConfig(const std::string& panel_id, Config& config)
    : panel_id_(panel_id), config_(config) {}

void PanelWidgetConfig::load() {
    entries_.clear();

    // Per-panel path: /panel_widgets/<panel_id>
    std::string panel_path = "/panel_widgets/" + panel_id_;
    auto saved = config_.get<json>(panel_path, json());

    // Migration: move legacy "home_widgets" to "panel_widgets.home"
    if (panel_id_ == "home" && (saved.is_null() || !saved.is_array())) {
        auto legacy = config_.get<json>("/home_widgets", json());
        if (legacy.is_array() && !legacy.empty()) {
            spdlog::info("[PanelWidgetConfig] Migrating legacy home_widgets to panel_widgets.home");
            config_.set<json>(panel_path, legacy);
            // Remove legacy key
            config_.get_json("").erase("home_widgets");
            config_.save();
            saved = legacy;
        }
    }

    if (!saved.is_array()) {
        entries_ = build_defaults();
        return;
    }

    std::set<std::string> seen_ids;

    for (const auto& item : saved) {
        if (!item.is_object() || !item.contains("id") || !item.contains("enabled")) {
            continue;
        }

        // Validate field types before extraction
        if (!item["id"].is_string() || !item["enabled"].is_boolean()) {
            spdlog::debug(
                "[PanelWidgetConfig] Skipping malformed widget entry (wrong field types)");
            continue;
        }

        std::string id = item["id"].get<std::string>();
        bool enabled = item["enabled"].get<bool>();

        // Skip duplicates
        if (seen_ids.count(id) > 0) {
            spdlog::debug("[PanelWidgetConfig] Skipping duplicate widget ID: {}", id);
            continue;
        }

        // Skip unknown widget IDs (not in registry)
        if (find_widget_def(id) == nullptr) {
            spdlog::debug("[PanelWidgetConfig] Dropping unknown widget ID: {}", id);
            continue;
        }

        // Load optional per-widget config
        nlohmann::json widget_config;
        if (item.contains("config") && item["config"].is_object()) {
            widget_config = item["config"];
        }

        seen_ids.insert(id);
        entries_.push_back({id, enabled, widget_config});
    }

    // Append any new widgets from registry that are not in saved config
    for (const auto& def : get_all_widget_defs()) {
        if (seen_ids.count(def.id) == 0) {
            spdlog::debug("[PanelWidgetConfig] Appending new widget: {} (default_enabled={})",
                          def.id, def.default_enabled);
            entries_.push_back({def.id, def.default_enabled, {}});
        }
    }

    // If saved array was empty or all entries were invalid, we still have
    // the appended defaults from above. If nothing was saved at all,
    // that gives us the full default set.
    if (entries_.empty()) {
        entries_ = build_defaults();
    }
}

void PanelWidgetConfig::save() {
    json widgets_array = json::array();
    for (const auto& entry : entries_) {
        json item = {{"id", entry.id}, {"enabled", entry.enabled}};
        if (!entry.config.empty()) {
            item["config"] = entry.config;
        }
        widgets_array.push_back(std::move(item));
    }
    config_.set<json>("/panel_widgets/" + panel_id_, widgets_array);
    config_.save();
}

void PanelWidgetConfig::reorder(size_t from_index, size_t to_index) {
    if (from_index >= entries_.size() || to_index >= entries_.size()) {
        return;
    }
    if (from_index == to_index) {
        return;
    }

    // Extract element, then insert at new position
    auto entry = std::move(entries_[from_index]);
    entries_.erase(entries_.begin() + static_cast<ptrdiff_t>(from_index));
    entries_.insert(entries_.begin() + static_cast<ptrdiff_t>(to_index), std::move(entry));
}

void PanelWidgetConfig::set_enabled(size_t index, bool enabled) {
    if (index >= entries_.size()) {
        return;
    }
    entries_[index].enabled = enabled;
}

void PanelWidgetConfig::reset_to_defaults() {
    entries_ = build_defaults();
}

bool PanelWidgetConfig::is_enabled(const std::string& id) const {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&id](const PanelWidgetEntry& e) { return e.id == id; });
    return it != entries_.end() && it->enabled;
}

nlohmann::json PanelWidgetConfig::get_widget_config(const std::string& id) const {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&id](const PanelWidgetEntry& e) { return e.id == id; });
    if (it != entries_.end() && !it->config.empty()) {
        return it->config;
    }
    return nlohmann::json::object();
}

void PanelWidgetConfig::set_widget_config(const std::string& id, const nlohmann::json& config) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&id](const PanelWidgetEntry& e) { return e.id == id; });
    if (it != entries_.end()) {
        it->config = config;
        save();
    } else {
        spdlog::debug("[PanelWidgetConfig] set_widget_config: widget '{}' not found", id);
    }
}

std::vector<PanelWidgetEntry> PanelWidgetConfig::build_defaults() {
    std::vector<PanelWidgetEntry> defaults;
    const auto& defs = get_all_widget_defs();
    defaults.reserve(defs.size());
    for (const auto& def : defs) {
        defaults.push_back({def.id, def.default_enabled, {}});
    }
    return defaults;
}

} // namespace helix
