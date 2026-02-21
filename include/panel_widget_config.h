// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include "hv/json.hpp"

namespace helix {

class Config;

struct PanelWidgetEntry {
    std::string id;
    bool enabled;
    nlohmann::json config; // Optional per-widget config (empty object = no config)

    bool operator==(const PanelWidgetEntry& other) const {
        return id == other.id && enabled == other.enabled && config == other.config;
    }
};

class PanelWidgetConfig {
  public:
    PanelWidgetConfig(const std::string& panel_id, Config& config);

    /// Load widget order from config, merging with registry defaults
    void load();

    /// Save current order to config
    void save();

    const std::vector<PanelWidgetEntry>& entries() const {
        return entries_;
    }

    /// Move widget between positions. No-op if indices are equal or out of bounds.
    void reorder(size_t from_index, size_t to_index);

    /// No-op if index out of bounds.
    void set_enabled(size_t index, bool enabled);

    void reset_to_defaults();

    bool is_enabled(const std::string& id) const;

    /// Get per-widget config for a given widget ID (empty object if not set)
    nlohmann::json get_widget_config(const std::string& id) const;

    /// Set per-widget config for a given widget ID, then save
    void set_widget_config(const std::string& id, const nlohmann::json& config);

  private:
    std::string panel_id_;
    Config& config_;
    std::vector<PanelWidgetEntry> entries_;

    static std::vector<PanelWidgetEntry> build_defaults();
};

} // namespace helix
