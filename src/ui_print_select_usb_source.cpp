// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_print_select_usb_source.h"

#include "ui_panel_print_select.h" // For PrintFileData
#include "ui_print_select_card_view.h"

#include "print_file_data.h"
#include "usb_manager.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

// ============================================================================
// Setup
// ============================================================================

bool PrintSelectUsbSource::setup(lv_obj_t* panel) {
    if (!panel) {
        return false;
    }

    // Find source selector buttons by name
    source_printer_btn_ = lv_obj_find_by_name(panel, "source_printer_btn");
    source_usb_btn_ = lv_obj_find_by_name(panel, "source_usb_btn");

    if (!source_printer_btn_ || !source_usb_btn_) {
        spdlog::warn("[UsbSource] Source selector buttons not found");
        return false;
    }

    // Hide USB tab by default - will be shown when USB drive is inserted
    lv_obj_add_flag(source_usb_btn_, LV_OBJ_FLAG_HIDDEN);

    // Set initial state - Printer is selected by default
    update_button_states();

    spdlog::debug("[UsbSource] Source selector buttons configured (USB tab hidden until drive "
                  "inserted)");
    return true;
}

void PrintSelectUsbSource::set_usb_manager(UsbManager* manager) {
    usb_manager_ = manager;

    // If USB source is currently active, refresh the file list
    if (current_source_ == FileSource::USB && usb_manager_) {
        refresh_files();
    }

    spdlog::debug("[UsbSource] UsbManager set");
}

// ============================================================================
// Source Selection
// ============================================================================

void PrintSelectUsbSource::select_printer_source() {
    if (current_source_ == FileSource::PRINTER) {
        return; // Already on Printer source
    }

    spdlog::debug("[UsbSource] Switching to Printer source");
    current_source_ = FileSource::PRINTER;
    update_button_states();

    if (on_source_changed_) {
        on_source_changed_(FileSource::PRINTER);
    }
}

void PrintSelectUsbSource::select_usb_source() {
    if (current_source_ == FileSource::USB) {
        return; // Already on USB source
    }

    spdlog::debug("[UsbSource] Switching to USB source");
    current_source_ = FileSource::USB;
    update_button_states();

    if (on_source_changed_) {
        on_source_changed_(FileSource::USB);
    }

    // Refresh USB files
    refresh_files();
}

// ============================================================================
// USB Drive Events
// ============================================================================

void PrintSelectUsbSource::on_drive_inserted() {
    if (!source_usb_btn_) {
        return;
    }

    // If Moonraker has symlink access to USB files, don't show the USB tab
    // (files are already accessible via the Printer source)
    if (moonraker_has_usb_access_) {
        spdlog::info(
            "[UsbSource] USB drive inserted - but Moonraker has symlink access, keeping USB "
            "tab hidden");
        return;
    }

    spdlog::info("[UsbSource] USB drive inserted - showing USB tab");
    lv_obj_remove_flag(source_usb_btn_, LV_OBJ_FLAG_HIDDEN);
}

void PrintSelectUsbSource::set_moonraker_has_usb_access(bool has_access) {
    moonraker_has_usb_access_ = has_access;

    if (has_access && source_usb_btn_) {
        // Hide USB tab permanently - files are accessible via Printer source
        spdlog::info("[UsbSource] Moonraker has USB symlink access - hiding USB tab permanently");
        lv_obj_add_flag(source_usb_btn_, LV_OBJ_FLAG_HIDDEN);

        // If currently viewing USB source, switch to Printer
        if (current_source_ == FileSource::USB) {
            current_source_ = FileSource::PRINTER;
            update_button_states();
            if (on_source_changed_) {
                on_source_changed_(FileSource::PRINTER);
            }
        }
    }
}

void PrintSelectUsbSource::on_drive_removed() {
    spdlog::info("[UsbSource] USB drive removed - hiding USB tab");

    // Hide the USB tab
    if (source_usb_btn_) {
        lv_obj_add_flag(source_usb_btn_, LV_OBJ_FLAG_HIDDEN);
    }

    // If USB source is currently active, switch to Printer source
    if (current_source_ == FileSource::USB) {
        spdlog::debug("[UsbSource] Was viewing USB source - switching to Printer");

        // Clear USB files
        usb_files_.clear();

        // Switch to Printer source
        current_source_ = FileSource::PRINTER;
        update_button_states();

        if (on_source_changed_) {
            on_source_changed_(FileSource::PRINTER);
        }
    }
}

// ============================================================================
// File Operations
// ============================================================================

void PrintSelectUsbSource::refresh_files() {
    usb_files_.clear();

    if (!usb_manager_) {
        spdlog::warn("[UsbSource] UsbManager not available");
        if (on_files_ready_) {
            on_files_ready_(std::vector<PrintFileData>{});
        }
        return;
    }

    // Get connected USB drives
    auto drives = usb_manager_->get_drives();
    if (drives.empty()) {
        spdlog::debug("[UsbSource] No USB drives detected");
        if (on_files_ready_) {
            on_files_ready_(std::vector<PrintFileData>{});
        }
        return;
    }

    // Scan first drive for G-code files
    // TODO: If multiple drives, show a drive selector
    usb_files_ = usb_manager_->scan_for_gcode(drives[0].mount_path);

    spdlog::info("[UsbSource] Found {} G-code files on USB drive '{}'", usb_files_.size(),
                 drives[0].label);

    if (on_files_ready_) {
        on_files_ready_(convert_to_print_file_data());
    }
}

// ============================================================================
// Internal Methods
// ============================================================================

void PrintSelectUsbSource::update_button_states() {
    if (!source_printer_btn_ || !source_usb_btn_) {
        return;
    }

    // Apply LV_STATE_CHECKED to the active source button
    // Make inactive button transparent for segmented control appearance
    if (current_source_ == FileSource::PRINTER) {
        lv_obj_add_state(source_printer_btn_, LV_STATE_CHECKED);
        lv_obj_remove_state(source_usb_btn_, LV_STATE_CHECKED);
        // Active tab: normal opacity, Inactive tab: transparent
        lv_obj_set_style_bg_opa(source_printer_btn_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(source_usb_btn_, LV_OPA_TRANSP, LV_PART_MAIN);
    } else {
        lv_obj_remove_state(source_printer_btn_, LV_STATE_CHECKED);
        lv_obj_add_state(source_usb_btn_, LV_STATE_CHECKED);
        // Active tab: normal opacity, Inactive tab: transparent
        lv_obj_set_style_bg_opa(source_printer_btn_, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(source_usb_btn_, LV_OPA_COVER, LV_PART_MAIN);
    }
}

std::vector<PrintFileData> PrintSelectUsbSource::convert_to_print_file_data() const {
    std::vector<PrintFileData> result;
    result.reserve(usb_files_.size());

    const std::string default_thumbnail = PrintSelectCardView::get_default_thumbnail();
    for (const auto& usb_file : usb_files_) {
        result.push_back(PrintFileData::from_usb_file(usb_file, default_thumbnail));
    }

    return result;
}

} // namespace helix::ui
