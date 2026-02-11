// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "overlay_base.h"

#include <string>
#include <vector>

class UsbManager;

namespace helix::settings {

/**
 * @class PrinterImageOverlay
 * @brief Overlay for browsing and selecting printer images
 *
 * Displays shipped and custom printer images in a grid layout.
 * Users can select an image or choose auto-detect mode.
 *
 * ## Usage:
 *
 * @code
 * auto& overlay = helix::settings::get_printer_image_overlay();
 * overlay.show(parent_screen);
 * @endcode
 */
class PrinterImageOverlay : public OverlayBase {
  public:
    PrinterImageOverlay();
    ~PrinterImageOverlay() override;

    //
    // === OverlayBase Interface ===
    //

    void init_subjects() override;
    void register_callbacks() override;

    const char* get_name() const override {
        return "Printer Image";
    }

    void on_activate() override;
    void on_deactivate() override;

    //
    // === UI Creation ===
    //

    lv_obj_t* create(lv_obj_t* parent) override;
    void show(lv_obj_t* parent_screen);

    //
    // === Event Handlers (public for static callbacks) ===
    //

    void handle_auto_detect();
    void handle_image_selected(const std::string& image_id);

    /// Provide USB manager for USB image import
    void set_usb_manager(UsbManager* manager);

    /// Re-populate custom images grid (public for async callback)
    void refresh_custom_images();

  private:
    //
    // === Static Callbacks ===
    //

    static void on_auto_detect(lv_event_t* e);
    static void on_image_card_clicked(lv_event_t* e);
    static void on_usb_image_clicked(lv_event_t* e);

    //
    // === Internal Methods ===
    //

    void populate_shipped_images();
    void populate_custom_images();
    void scan_usb_drives();
    void populate_usb_images(const std::string& mount_path);
    void handle_usb_import(const std::string& source_path);
    lv_obj_t* create_image_card(lv_obj_t* parent, const std::string& image_id,
                                const std::string& display_name, const std::string& preview_path);
    void update_selection_indicator(const std::string& active_id);

    //
    // === Members ===
    //

    UsbManager* usb_manager_ = nullptr;
};

/**
 * @brief Global instance accessor
 *
 * Creates the overlay on first access and registers it for cleanup
 * with StaticPanelRegistry.
 *
 * @return Reference to singleton PrinterImageOverlay
 */
PrinterImageOverlay& get_printer_image_overlay();

} // namespace helix::settings
