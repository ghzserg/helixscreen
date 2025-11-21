// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#include "ui_wizard_hardware_selector.h"

#include "app_globals.h"
#include "config.h"
#include "ui_wizard_helpers.h"

#include <spdlog/spdlog.h>

void wizard_hardware_dropdown_changed_cb(lv_event_t* e) {
    lv_obj_t* dropdown = (lv_obj_t*)lv_event_get_target(e);
    lv_subject_t* subject = (lv_subject_t*)lv_event_get_user_data(e);

    if (!subject) {
        spdlog::error("[Wizard Hardware] Dropdown callback missing subject user_data");
        return;
    }

    uint16_t selected_index = lv_dropdown_get_selected(dropdown);
    lv_subject_set_int(subject, selected_index);
}

bool wizard_populate_hardware_dropdown(
    lv_obj_t* root,
    const char* dropdown_name,
    lv_subject_t* subject,
    std::vector<std::string>& items_out,
    std::function<const std::vector<std::string>&(MoonrakerClient*)> moonraker_getter,
    const char* prefix_filter,
    bool allow_none,
    const char* config_key,
    std::function<std::string(MoonrakerClient*)> guess_fallback,
    const char* log_prefix
) {
    if (!root || !dropdown_name || !subject) {
        spdlog::error("{} Invalid parameters for dropdown population", log_prefix);
        return false;
    }

    // Get Moonraker client
    MoonrakerClient* client = get_moonraker_client();

    // Clear and build items list
    items_out.clear();
    if (client) {
        const auto& hardware_list = moonraker_getter(client);
        for (const auto& item : hardware_list) {
            // Apply prefix filter if specified
            if (prefix_filter && item.find(prefix_filter) == std::string::npos) {
                continue;
            }
            items_out.push_back(item);
        }
    }

    // Build dropdown options string
    std::string options_str = WizardHelpers::build_dropdown_options(
        items_out,
        nullptr, // No additional filter (already filtered above)
        allow_none
    );

    // Add "None" to items vector if needed (to match dropdown)
    if (allow_none) {
        items_out.push_back("None");
    }

    // Find and configure dropdown
    lv_obj_t* dropdown = lv_obj_find_by_name(root, dropdown_name);
    if (!dropdown) {
        spdlog::warn("{} Dropdown '{}' not found in screen", log_prefix, dropdown_name);
        return false;
    }

    lv_dropdown_set_options(dropdown, options_str.c_str());

    // Restore saved selection with guessing fallback
    WizardHelpers::restore_dropdown_selection(
        dropdown, subject, items_out,
        config_key, client,
        guess_fallback, log_prefix
    );

    spdlog::debug("{} Populated dropdown '{}' with {} items",
                  log_prefix, dropdown_name, items_out.size());
    return true;
}
