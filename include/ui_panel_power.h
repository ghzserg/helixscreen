// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"
#include "ui_panel_base.h"

#include "moonraker_api.h" // Need full definition for PowerDevice
#include "subject_managed_panel.h"

#include <map>
#include <string>
#include <vector>

/**
 * @brief Power device control panel
 *
 * Displays configured Moonraker power devices with on/off toggle controls.
 * Each device is shown as a row with the device name and a toggle switch.
 * Devices marked as "locked_while_printing" show a lock indicator during prints.
 */
class PowerPanel : public PanelBase {
  public:
    PowerPanel(helix::PrinterState& printer_state, MoonrakerAPI* api);
    ~PowerPanel() override;

    void init_subjects() override;
    void deinit_subjects();
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;

    const char* get_name() const override {
        return "Power Control";
    }
    const char* get_xml_component_name() const override {
        return "power_panel";
    }

  private:
    // Subject manager for automatic cleanup
    SubjectManager subjects_;

    // Subjects for reactive binding
    lv_subject_t status_subject_;
    char status_buf_[128] = "Loading devices...";

    // Widget references
    lv_obj_t* device_list_container_ = nullptr;
    lv_obj_t* empty_state_container_ = nullptr;
    lv_obj_t* status_label_ = nullptr;

    // Device state tracking
    struct DeviceRow {
        lv_obj_t* container = nullptr;
        lv_obj_t* toggle = nullptr;
        std::string device_name;
        bool locked = false;
    };
    std::vector<DeviceRow> device_rows_;

    // Setup helpers
    void fetch_devices();
    void populate_device_list(const std::vector<PowerDevice>& devices);
    void create_device_row(const PowerDevice& device);
    void clear_device_list();

    // Event handlers
    void handle_device_toggle(const std::string& device, bool power_on);

    // Static callback for XML event_cb
    static void on_power_device_toggle(lv_event_t* e);
};

/**
 * @brief Get global power panel instance
 * @return Reference to the singleton power panel
 */
PowerPanel& get_global_power_panel();
