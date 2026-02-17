// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_power.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_nav_manager.h"
#include "ui_panel_common.h"
#include "ui_subject_registry.h"
#include "ui_utils.h"

#include "app_globals.h"
#include "device_display_name.h"
#include "moonraker_api.h"
#include "printer_state.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <memory>

using namespace helix;

PowerPanel::PowerPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    std::snprintf(status_buf_, sizeof(status_buf_), "Loading devices...");
}

PowerPanel::~PowerPanel() {
    deinit_subjects();
    // Guard against static destruction order fiasco (spdlog may be gone)
    if (!StaticPanelRegistry::is_destroyed()) {
        spdlog::trace("[PowerPanel] Destroyed");
    }
}

void PowerPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // Initialize status subject for reactive binding
    UI_MANAGED_SUBJECT_STRING(status_subject_, status_buf_, status_buf_, "power_status", subjects_);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized: power_status", get_name());
}

void PowerPanel::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::debug("[PowerPanel] Subjects deinitialized");
}

void PowerPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    spdlog::info("[{}] Setting up event handlers...", get_name());

    // Register XML event callback (once)
    static bool callbacks_registered = false;
    if (!callbacks_registered) {
        lv_xml_register_event_cb(nullptr, "on_power_device_toggle", on_power_device_toggle);
        callbacks_registered = true;
    }

    // Use standard overlay panel setup (wires header, back button, handles responsive padding)
    ui_overlay_panel_setup_standard(panel_, parent_screen_, "overlay_header", "overlay_content");

    // Find widget references
    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_, "overlay_content");
    if (overlay_content) {
        device_list_container_ = lv_obj_find_by_name(overlay_content, "device_list");
        empty_state_container_ = lv_obj_find_by_name(overlay_content, "empty_state");
        status_label_ = lv_obj_find_by_name(overlay_content, "status_message");
    }

    if (!device_list_container_) {
        spdlog::error("[{}] device_list container not found!", get_name());
        return;
    }

    // Fetch devices from Moonraker
    fetch_devices();

    spdlog::info("[{}] Setup complete!", get_name());
}

void PowerPanel::fetch_devices() {
    if (!api_) {
        spdlog::warn("[{}] No MoonrakerAPI available - cannot fetch devices", get_name());
        std::snprintf(status_buf_, sizeof(status_buf_), "Not connected to printer");
        lv_subject_copy_string(&status_subject_, status_buf_);
        return;
    }

    spdlog::debug("[{}] Fetching power devices...", get_name());
    std::snprintf(status_buf_, sizeof(status_buf_), "Loading devices...");
    lv_subject_copy_string(&status_subject_, status_buf_);

    api_->get_power_devices(
        [this](const std::vector<PowerDevice>& devices) {
            spdlog::info("[{}] Received {} power devices", get_name(), devices.size());
            populate_device_list(devices);
        },
        [this](const MoonrakerError& err) {
            spdlog::error("[{}] Failed to fetch power devices: {}", get_name(), err.message);
            std::snprintf(status_buf_, sizeof(status_buf_), "Failed to load devices");
            lv_subject_copy_string(&status_subject_, status_buf_);
        });
}

void PowerPanel::clear_device_list() {
    // Remove all device row widgets
    for (auto& row : device_rows_) {
        helix::ui::safe_delete(row.container);
    }
    device_rows_.clear();
}

void PowerPanel::populate_device_list(const std::vector<PowerDevice>& devices) {
    clear_device_list();

    bool has_devices = !devices.empty();

    // Toggle visibility: show device list OR empty state
    if (device_list_container_) {
        if (has_devices) {
            lv_obj_remove_flag(device_list_container_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(device_list_container_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (empty_state_container_) {
        if (has_devices) {
            lv_obj_add_flag(empty_state_container_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(empty_state_container_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (!has_devices) {
        status_buf_[0] = '\0'; // Clear status
        lv_subject_copy_string(&status_subject_, status_buf_);
        return;
    }

    for (const auto& device : devices) {
        create_device_row(device);
    }

    // Clear status message on success
    status_buf_[0] = '\0';
    lv_subject_copy_string(&status_subject_, status_buf_);
}

void PowerPanel::create_device_row(const PowerDevice& device) {
    if (!device_list_container_) {
        return;
    }

    // Convert technical name to user-friendly label
    std::string friendly_name =
        helix::get_display_name(device.device, helix::DeviceType::POWER_DEVICE);

    // Create row using XML component with prettified device_name prop
    const char* attrs[] = {"device_name", friendly_name.c_str(), nullptr, nullptr};
    lv_obj_t* row =
        static_cast<lv_obj_t*>(lv_xml_create(device_list_container_, "power_device_row", attrs));

    if (!row) {
        spdlog::error("[{}] Failed to create power_device_row for '{}'", get_name(), device.device);
        return;
    }

    // Find the toggle within the component
    lv_obj_t* toggle = lv_obj_find_by_name(row, "device_toggle");
    if (!toggle) {
        spdlog::error("[{}] device_toggle not found in row", get_name());
        helix::ui::safe_delete(row);
        return;
    }

    // Set initial state based on device status
    if (device.status == "on") {
        lv_obj_add_state(toggle, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(toggle, LV_STATE_CHECKED);
    }

    // Check if device is locked during printing
    PrintJobState job_state = printer_state_.get_print_job_state();
    bool is_printing = (job_state == PrintJobState::PRINTING || job_state == PrintJobState::PAUSED);
    bool is_locked = device.locked_while_printing && is_printing;

    if (is_locked) {
        // Disable toggle interaction
        lv_obj_add_state(toggle, LV_STATE_DISABLED);

        // Show lock icon
        lv_obj_t* lock_icon = lv_obj_find_by_name(row, "lock_icon");
        if (lock_icon) {
            lv_obj_remove_flag(lock_icon, LV_OBJ_FLAG_HIDDEN);
        }

        // Show status text explaining why it's locked
        lv_obj_t* status_label = lv_obj_find_by_name(row, "device_status");
        if (status_label) {
            lv_label_set_text(status_label, "Locked during print");
            lv_obj_remove_flag(status_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Store device row info (use technical name for API calls)
    DeviceRow device_row;
    device_row.container = row;
    device_row.toggle = toggle;
    device_row.device_name = device.device; // Keep technical name for API
    device_row.locked = is_locked;
    device_rows_.push_back(device_row);

    // Store index to DeviceRow in the row's user_data (avoids dangling pointer when vector resizes)
    size_t index = device_rows_.size() - 1;
    lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<intptr_t>(index)));

    spdlog::debug("[{}] Created row for device '{}' (status: {}, locked: {})", get_name(),
                  device.device, device.status, is_locked);
}

void PowerPanel::handle_device_toggle(const std::string& device, bool power_on) {
    if (!api_) {
        spdlog::warn("[{}] No MoonrakerAPI available - cannot toggle device", get_name());
        return;
    }

    const char* action = power_on ? "on" : "off";
    spdlog::info("[{}] Toggling device '{}' to {}", get_name(), device, action);

    api_->set_device_power(
        device, action,
        [this, device, power_on]() {
            spdlog::debug("[{}] Device '{}' set to {} successfully", get_name(), device,
                          power_on ? "on" : "off");
        },
        [this, device](const MoonrakerError& err) {
            spdlog::error("[{}] Failed to toggle device '{}': {}", get_name(), device, err.message);
            std::snprintf(status_buf_, sizeof(status_buf_), "Failed to toggle %s", device.c_str());
            lv_subject_copy_string(&status_subject_, status_buf_);

            // Revert the toggle to previous state by re-fetching
            fetch_devices();
        });
}

void PowerPanel::on_power_device_toggle(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PowerPanel] on_power_device_toggle");

    // Get the global instance (XML callbacks can't pass instance via user_data)
    auto& self = get_global_power_panel();

    lv_obj_t* toggle = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!toggle) {
        spdlog::warn("[PowerPanel] No target in toggle event");
    } else {
        // Navigate from toggle to parent row to get DeviceRow index
        lv_obj_t* row = lv_obj_get_parent(toggle);
        if (!row) {
            spdlog::warn("[PowerPanel] Toggle has no parent row");
        } else {
            // Retrieve index from user_data
            auto index = static_cast<size_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(row)));

            // Bounds check before accessing vector
            if (index >= self.device_rows_.size()) {
                spdlog::warn("[PowerPanel] Invalid device_row index {} (size: {})", index,
                             self.device_rows_.size());
            } else {
                auto& device_row = self.device_rows_[index];

                if (device_row.locked) {
                    spdlog::debug("[PowerPanel] Device '{}' is locked - ignoring toggle",
                                  device_row.device_name);
                } else {
                    bool is_on = lv_obj_has_state(toggle, LV_STATE_CHECKED);
                    self.handle_device_toggle(device_row.device_name, is_on);
                }
            }
        }
    }

    LVGL_SAFE_EVENT_CB_END();
}

// Global instance accessor
static std::unique_ptr<PowerPanel> g_power_panel;

PowerPanel& get_global_power_panel() {
    if (!g_power_panel) {
        g_power_panel = std::make_unique<PowerPanel>(get_printer_state(), get_moonraker_api());
        StaticPanelRegistry::instance().register_destroy("PowerPanel",
                                                         []() { g_power_panel.reset(); });
    }
    return *g_power_panel;
}
