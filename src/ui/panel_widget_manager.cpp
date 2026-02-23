// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "panel_widget_manager.h"

#include "config.h"
#include "observer_factory.h"
#include "panel_widget.h"
#include "panel_widget_config.h"
#include "panel_widget_registry.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace helix {

PanelWidgetManager& PanelWidgetManager::instance() {
    static PanelWidgetManager instance;
    return instance;
}

void PanelWidgetManager::clear_shared_resources() {
    shared_resources_.clear();
}

void PanelWidgetManager::init_widget_subjects() {
    if (widget_subjects_initialized_) {
        return;
    }

    for (const auto& def : get_all_widget_defs()) {
        if (def.init_subjects) {
            spdlog::debug("[PanelWidgetManager] Initializing subjects for widget '{}'", def.id);
            def.init_subjects();
        }
    }

    widget_subjects_initialized_ = true;
    spdlog::debug("[PanelWidgetManager] Widget subjects initialized");
}

void PanelWidgetManager::register_rebuild_callback(const std::string& panel_id,
                                                   RebuildCallback cb) {
    rebuild_callbacks_[panel_id] = std::move(cb);
}

void PanelWidgetManager::unregister_rebuild_callback(const std::string& panel_id) {
    rebuild_callbacks_.erase(panel_id);
}

void PanelWidgetManager::notify_config_changed(const std::string& panel_id) {
    auto it = rebuild_callbacks_.find(panel_id);
    if (it != rebuild_callbacks_.end()) {
        it->second();
    }
}

static PanelWidgetConfig& get_widget_config(const std::string& panel_id) {
    // Per-panel config instances cached by panel ID
    static std::unordered_map<std::string, PanelWidgetConfig> configs;
    auto it = configs.find(panel_id);
    if (it == configs.end()) {
        it = configs.emplace(panel_id, PanelWidgetConfig(panel_id, *Config::get_instance())).first;
    }
    // Always reload to pick up changes from settings overlay
    it->second.load();
    return it->second;
}

std::vector<std::unique_ptr<PanelWidget>>
PanelWidgetManager::populate_widgets(const std::string& panel_id, lv_obj_t* container) {
    if (!container) {
        spdlog::debug("[PanelWidgetManager] populate_widgets: null container for '{}'", panel_id);
        return {};
    }

    // Clear existing children (for repopulation)
    lv_obj_clean(container);

    auto& widget_config = get_widget_config(panel_id);

    // Collect enabled + hardware-available widget component names
    std::vector<std::string> enabled_widgets;
    for (const auto& entry : widget_config.entries()) {
        if (!entry.enabled) {
            continue;
        }

        // Check hardware gate — skip widgets whose hardware isn't present.
        // Gates are defined in PanelWidgetDef::hardware_gate_subject and checked
        // here instead of XML bind_flag_if_eq to avoid orphaned dividers.
        const auto* def = find_widget_def(entry.id);
        if (def && def->hardware_gate_subject) {
            lv_subject_t* gate = lv_xml_get_subject(nullptr, def->hardware_gate_subject);
            if (gate && lv_subject_get_int(gate) == 0) {
                continue;
            }
        }

        enabled_widgets.push_back("panel_widget_" + entry.id);
    }

    // If firmware_restart is NOT already in the list (user disabled it),
    // conditionally inject it as the LAST widget when Klipper is in SHUTDOWN or ERROR.
    // This ensures the restart button is always reachable during a shutdown or error.
    bool has_firmware_restart = std::find(enabled_widgets.begin(), enabled_widgets.end(),
                                          "panel_widget_firmware_restart") != enabled_widgets.end();
    if (!has_firmware_restart) {
        lv_subject_t* klippy = lv_xml_get_subject(nullptr, "klippy_state");
        if (klippy) {
            int state = lv_subject_get_int(klippy);
            if (state == static_cast<int>(KlippyState::SHUTDOWN) ||
                state == static_cast<int>(KlippyState::ERROR)) {
                enabled_widgets.push_back("panel_widget_firmware_restart");
                spdlog::debug("[PanelWidgetManager] Injected firmware_restart (Klipper {})",
                              state == static_cast<int>(KlippyState::SHUTDOWN) ? "SHUTDOWN"
                                                                               : "ERROR");
            }
        }
    }

    if (enabled_widgets.empty()) {
        return {};
    }

    // Smart row layout:
    //   1-4 widgets  -> 1 row
    //   5-8 widgets  -> 2 rows, first row has 4
    //   9-10 widgets -> 2 rows, first row has 5
    size_t total = enabled_widgets.size();
    size_t first_row_count;
    if (total <= 4) {
        first_row_count = total; // Single row
    } else if (total <= 8) {
        first_row_count = 4; // 2 rows: 4 + remainder
    } else {
        first_row_count = 5; // 2 rows: 5 + remainder
    }

    std::vector<std::unique_ptr<PanelWidget>> result;

    auto create_row = [&](size_t start, size_t count) {
        lv_obj_t* row = lv_obj_create(container);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_flex_grow(row, 1);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, theme_manager_get_spacing("space_xs"), 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        bool first = true;
        for (size_t i = start; i < start + count && i < enabled_widgets.size(); ++i) {
            // Add divider between widgets (not before first)
            if (!first) {
                const char* div_attrs[] = {"height", "80%", nullptr, nullptr};
                lv_xml_create(row, "divider_vertical", div_attrs);
            }

            auto* widget =
                static_cast<lv_obj_t*>(lv_xml_create(row, enabled_widgets[i].c_str(), nullptr));
            if (widget) {
                first = false;
                spdlog::debug("[PanelWidgetManager] Created widget: {}", enabled_widgets[i]);

                // If this widget def has a factory, create and attach the PanelWidget instance
                const std::string widget_id =
                    enabled_widgets[i].substr(13); // strip "panel_widget_" prefix
                const auto* def = find_widget_def(widget_id);
                if (def && def->factory) {
                    auto hw = def->factory();
                    if (hw) {
                        hw->attach(widget, lv_scr_act());
                        hw->set_row_density(count);
                        result.push_back(std::move(hw));
                    }
                }
            } else {
                spdlog::warn("[PanelWidgetManager] Failed to create widget: {}",
                             enabled_widgets[i]);
            }
        }
    };

    // Create first row
    create_row(0, first_row_count);

    // Create second row if needed
    if (total > first_row_count) {
        create_row(first_row_count, total - first_row_count);
    }

    spdlog::debug("[PanelWidgetManager] Populated {} widgets ({} with factories) for '{}'", total,
                  result.size(), panel_id);

    return result;
}

void PanelWidgetManager::setup_gate_observers(const std::string& panel_id,
                                              RebuildCallback rebuild_cb) {
    using helix::ui::observe_int_sync;

    // Clear any existing observers for this panel
    gate_observers_.erase(panel_id);
    auto& observers = gate_observers_[panel_id];

    // Collect unique gate subject names from the widget registry
    std::vector<const char*> gate_names;
    for (const auto& def : get_all_widget_defs()) {
        if (def.hardware_gate_subject) {
            // Avoid duplicates
            bool found = false;
            for (const auto* existing : gate_names) {
                if (std::strcmp(existing, def.hardware_gate_subject) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                gate_names.push_back(def.hardware_gate_subject);
            }
        }
    }

    // Also observe klippy_state for firmware_restart conditional injection
    gate_names.push_back("klippy_state");

    for (const auto* name : gate_names) {
        lv_subject_t* subject = lv_xml_get_subject(nullptr, name);
        if (!subject) {
            spdlog::trace("[PanelWidgetManager] Gate subject '{}' not registered yet", name);
            continue;
        }

        // Use observe_int_sync with PanelWidgetManager as the class template parameter.
        // The callback ignores the value and just triggers the panel's rebuild.
        observers.push_back(observe_int_sync<PanelWidgetManager>(
            subject, this,
            [rebuild_cb](PanelWidgetManager* /*self*/, int /*value*/) { rebuild_cb(); }));

        spdlog::trace("[PanelWidgetManager] Observing gate subject '{}' for panel '{}'", name,
                      panel_id);
    }

    spdlog::debug("[PanelWidgetManager] Set up {} gate observers for panel '{}'", observers.size(),
                  panel_id);
}

void PanelWidgetManager::clear_gate_observers(const std::string& panel_id) {
    auto it = gate_observers_.find(panel_id);
    if (it != gate_observers_.end()) {
        spdlog::debug("[PanelWidgetManager] Clearing {} gate observers for panel '{}'",
                      it->second.size(), panel_id);
        gate_observers_.erase(it);
    }
}

} // namespace helix
