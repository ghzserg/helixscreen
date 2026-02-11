// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_overlay_printer_image.cpp
 * @brief Implementation of PrinterImageOverlay
 *
 * Displays shipped and custom printer images in a grid layout for selection.
 * Image cards are created dynamically since the list varies at runtime.
 */

#include "ui_overlay_printer_image.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_update_queue.h"

#include "printer_image_manager.h"
#include "static_panel_registry.h"
#include "theme_manager.h"
#include "usb_manager.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <filesystem>
#include <memory>

namespace helix::settings {

// ============================================================================
// SINGLETON ACCESSOR
// ============================================================================

static std::unique_ptr<PrinterImageOverlay> g_printer_image_overlay;

PrinterImageOverlay& get_printer_image_overlay() {
    if (!g_printer_image_overlay) {
        g_printer_image_overlay = std::make_unique<PrinterImageOverlay>();
        StaticPanelRegistry::instance().register_destroy("PrinterImageOverlay",
                                                         []() { g_printer_image_overlay.reset(); });
    }
    return *g_printer_image_overlay;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

PrinterImageOverlay::PrinterImageOverlay() {
    spdlog::debug("[{}] Created", get_name());
}

PrinterImageOverlay::~PrinterImageOverlay() {
    spdlog::trace("[{}] Destroyed", get_name());
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void PrinterImageOverlay::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    // No subjects needed — image grids are populated imperatively
    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void PrinterImageOverlay::register_callbacks() {
    lv_xml_register_event_cb(nullptr, "on_printer_image_auto_detect", on_auto_detect);
    spdlog::debug("[{}] Callbacks registered", get_name());
}

// ============================================================================
// UI CREATION
// ============================================================================

lv_obj_t* PrinterImageOverlay::create(lv_obj_t* parent) {
    if (overlay_root_) {
        spdlog::warn("[{}] create() called but overlay already exists", get_name());
        return overlay_root_;
    }

    spdlog::debug("[{}] Creating overlay...", get_name());

    overlay_root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "printer_image_overlay", nullptr));
    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    // Initially hidden until show() pushes it
    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    spdlog::info("[{}] Overlay created", get_name());
    return overlay_root_;
}

void PrinterImageOverlay::show(lv_obj_t* parent_screen) {
    spdlog::debug("[{}] show() called", get_name());

    parent_screen_ = parent_screen;

    // Ensure subjects and callbacks are initialized
    if (!subjects_initialized_) {
        init_subjects();
        register_callbacks();
    }

    // Lazy create overlay
    if (!overlay_root_ && parent_screen_) {
        create(parent_screen_);
    }

    if (!overlay_root_) {
        spdlog::error("[{}] Cannot show - overlay not created", get_name());
        return;
    }

    // Register for lifecycle callbacks
    NavigationManager::instance().register_overlay_instance(overlay_root_, this);

    // Push onto navigation stack (on_activate will populate grids)
    ui_nav_push_overlay(overlay_root_);
}

// ============================================================================
// LIFECYCLE
// ============================================================================

void PrinterImageOverlay::set_usb_manager(UsbManager* manager) {
    usb_manager_ = manager;
    spdlog::debug("[{}] USB manager set ({})", get_name(), manager ? "valid" : "null");
}

void PrinterImageOverlay::refresh_custom_images() {
    populate_custom_images();
    std::string active_id = helix::PrinterImageManager::instance().get_active_image_id();
    update_selection_indicator(active_id);
}

void PrinterImageOverlay::on_activate() {
    OverlayBase::on_activate();

    populate_shipped_images();
    populate_custom_images();
    scan_usb_drives();

    // Highlight the currently active image
    std::string active_id = helix::PrinterImageManager::instance().get_active_image_id();
    update_selection_indicator(active_id);
}

void PrinterImageOverlay::on_deactivate() {
    OverlayBase::on_deactivate();
}

// ============================================================================
// GRID POPULATION
// ============================================================================

void PrinterImageOverlay::populate_shipped_images() {
    if (!overlay_root_) {
        return;
    }

    lv_obj_t* grid = lv_obj_find_by_name(overlay_root_, "shipped_images_grid");
    if (!grid) {
        spdlog::warn("[{}] shipped_images_grid not found", get_name());
        return;
    }

    // Clear existing children
    lv_obj_clean(grid);

    auto images = helix::PrinterImageManager::instance().get_shipped_images();
    spdlog::debug("[{}] Populating {} shipped images", get_name(), images.size());

    for (const auto& img : images) {
        create_image_card(grid, img.id, img.display_name, img.preview_path);
    }
}

void PrinterImageOverlay::populate_custom_images() {
    if (!overlay_root_) {
        return;
    }

    lv_obj_t* grid = lv_obj_find_by_name(overlay_root_, "custom_images_grid");
    if (!grid) {
        spdlog::warn("[{}] custom_images_grid not found", get_name());
        return;
    }

    // Clear existing children
    lv_obj_clean(grid);

    auto images = helix::PrinterImageManager::instance().get_custom_images();
    spdlog::debug("[{}] Populating {} custom images", get_name(), images.size());

    for (const auto& img : images) {
        create_image_card(grid, img.id, img.display_name, img.preview_path);
    }
}

lv_obj_t* PrinterImageOverlay::create_image_card(lv_obj_t* parent, const std::string& image_id,
                                                 const std::string& display_name,
                                                 const std::string& preview_path) {
    // Card container: clickable, with padding and rounded corners
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, 100, 120);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(card, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, theme_manager_get_color("card_bg"), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Default border (will be overridden for active selection)
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_TRANSP, LV_PART_MAIN);

    // Preview image
    if (!preview_path.empty()) {
        lv_obj_t* img = lv_image_create(card);
        lv_image_set_src(img, preview_path.c_str());
        lv_obj_set_size(img, 80, 80);
        lv_obj_set_style_image_recolor(img, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_image_recolor_opa(img, LV_OPA_TRANSP, LV_PART_MAIN);
    }

    // Display name label
    lv_obj_t* label = lv_label_create(card);
    lv_label_set_text(label, display_name.c_str());
    lv_obj_set_style_text_font(label, lv_font_get_default(), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, theme_manager_get_color("text_secondary"), LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, 90);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Store image_id in user_data (strdup for dynamic lifetime)
    char* id_copy = strdup(image_id.c_str());
    lv_obj_set_user_data(card, id_copy);

    // Free strdup'd user_data when LVGL destroys the card (e.g. via lv_obj_clean)
    lv_obj_add_event_cb(
        card,
        [](lv_event_t* e) {
            auto* obj = lv_event_get_target_obj(e);
            void* data = lv_obj_get_user_data(obj);
            if (data)
                free(data);
        },
        LV_EVENT_DELETE, nullptr);

    // Click handler (EXCEPTION: dynamic cards can't use XML callbacks)
    lv_obj_add_event_cb(card, on_image_card_clicked, LV_EVENT_CLICKED, id_copy);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    return card;
}

void PrinterImageOverlay::update_selection_indicator(const std::string& active_id) {
    if (!overlay_root_) {
        return;
    }

    lv_color_t highlight_color = theme_manager_get_color("primary_color");

    // Update shipped images grid
    lv_obj_t* shipped_grid = lv_obj_find_by_name(overlay_root_, "shipped_images_grid");
    if (shipped_grid) {
        uint32_t count = lv_obj_get_child_count(shipped_grid);
        for (uint32_t i = 0; i < count; i++) {
            lv_obj_t* child = lv_obj_get_child(shipped_grid, static_cast<int32_t>(i));
            auto* id = static_cast<const char*>(lv_obj_get_user_data(child));
            if (id && std::string(id) == active_id) {
                lv_obj_set_style_border_color(child, highlight_color, LV_PART_MAIN);
                lv_obj_set_style_border_opa(child, LV_OPA_COVER, LV_PART_MAIN);
            } else {
                lv_obj_set_style_border_opa(child, LV_OPA_TRANSP, LV_PART_MAIN);
            }
        }
    }

    // Update custom images grid
    lv_obj_t* custom_grid = lv_obj_find_by_name(overlay_root_, "custom_images_grid");
    if (custom_grid) {
        uint32_t count = lv_obj_get_child_count(custom_grid);
        for (uint32_t i = 0; i < count; i++) {
            lv_obj_t* child = lv_obj_get_child(custom_grid, static_cast<int32_t>(i));
            auto* id = static_cast<const char*>(lv_obj_get_user_data(child));
            if (id && std::string(id) == active_id) {
                lv_obj_set_style_border_color(child, highlight_color, LV_PART_MAIN);
                lv_obj_set_style_border_opa(child, LV_OPA_COVER, LV_PART_MAIN);
            } else {
                lv_obj_set_style_border_opa(child, LV_OPA_TRANSP, LV_PART_MAIN);
            }
        }
    }
}

// ============================================================================
// USB IMPORT
// ============================================================================

void PrinterImageOverlay::scan_usb_drives() {
    lv_obj_t* usb_section = lv_obj_find_by_name(overlay_root_, "usb_import_section");
    if (!usb_section) {
        return;
    }

    if (!usb_manager_ || !usb_manager_->is_running()) {
        lv_obj_add_flag(usb_section, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    auto drives = usb_manager_->get_drives();
    if (drives.empty()) {
        lv_obj_add_flag(usb_section, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(usb_section, LV_OBJ_FLAG_HIDDEN);
    spdlog::debug("[{}] Found {} USB drive(s), scanning first: {}", get_name(), drives.size(),
                  drives[0].mount_path);
    populate_usb_images(drives[0].mount_path);
}

void PrinterImageOverlay::populate_usb_images(const std::string& mount_path) {
    lv_obj_t* grid = lv_obj_find_by_name(overlay_root_, "usb_images_grid");
    if (!grid) {
        spdlog::warn("[{}] usb_images_grid not found", get_name());
        return;
    }

    lv_obj_t* status_label = lv_obj_find_by_name(overlay_root_, "usb_status_label");

    // Clear existing children
    lv_obj_clean(grid);

    auto image_paths = helix::PrinterImageManager::instance().scan_for_images(mount_path);
    spdlog::debug("[{}] Found {} importable images on USB", get_name(), image_paths.size());

    if (image_paths.empty()) {
        if (status_label) {
            lv_label_set_text(status_label, "No PNG or JPEG images found on USB drive");
        }
        return;
    }

    if (status_label) {
        lv_label_set_text(status_label, "");
    }

    for (const auto& path : image_paths) {
        // Extract filename for display
        std::string filename = std::filesystem::path(path).filename().string();

        // Create a card for each USB image (no preview — source is raw PNG/JPEG)
        lv_obj_t* card = lv_obj_create(grid);
        lv_obj_set_size(card, 100, 120);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(card, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
        lv_obj_set_style_bg_color(card, theme_manager_get_color("card_bg"), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, theme_manager_get_color("primary_color"), LV_PART_MAIN);
        lv_obj_set_style_border_opa(card, LV_OPA_50, LV_PART_MAIN);

        // "Import" icon/text
        lv_obj_t* icon_label = lv_label_create(card);
        lv_label_set_text(icon_label, LV_SYMBOL_DOWNLOAD);
        lv_obj_set_style_text_color(icon_label, theme_manager_get_color("primary_color"),
                                    LV_PART_MAIN);

        // Filename label
        lv_obj_t* label = lv_label_create(card);
        lv_label_set_text(label, filename.c_str());
        lv_obj_set_style_text_font(label, lv_font_get_default(), LV_PART_MAIN);
        lv_obj_set_style_text_color(label, theme_manager_get_color("text_secondary"), LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, 90);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

        // Store full path in user_data for the click handler
        char* path_copy = strdup(path.c_str());
        lv_obj_set_user_data(card, path_copy);

        // Free strdup'd user_data when LVGL destroys the card (e.g. via lv_obj_clean)
        lv_obj_add_event_cb(
            card,
            [](lv_event_t* e) {
                auto* obj = lv_event_get_target_obj(e);
                void* data = lv_obj_get_user_data(obj);
                if (data)
                    free(data);
            },
            LV_EVENT_DELETE, nullptr);

        // Click handler (EXCEPTION: dynamic cards can't use XML callbacks)
        lv_obj_add_event_cb(card, on_usb_image_clicked, LV_EVENT_CLICKED, path_copy);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    }
}

void PrinterImageOverlay::handle_usb_import(const std::string& source_path) {
    std::string filename = std::filesystem::path(source_path).filename().string();
    spdlog::info("[{}] Importing USB image: {}", get_name(), filename);

    // Update status label
    lv_obj_t* status_label = lv_obj_find_by_name(overlay_root_, "usb_status_label");
    if (status_label) {
        std::string msg = "Importing " + filename + "...";
        lv_label_set_text(status_label, msg.c_str());
    }

    // import_image_async() currently runs synchronously, but the callback is wrapped
    // in ui_queue_update() for safety in case the implementation becomes truly async
    helix::PrinterImageManager::instance().import_image_async(
        source_path, [filename](helix::PrinterImageManager::ImportResult result) {
            ui_queue_update([result = std::move(result), filename]() {
                auto& overlay = get_printer_image_overlay();
                lv_obj_t* status_label =
                    lv_obj_find_by_name(overlay.overlay_root_, "usb_status_label");

                if (result.success) {
                    spdlog::info("[Printer Image] USB import success: {}", result.id);
                    if (status_label) {
                        lv_label_set_text(status_label, "");
                    }
                    overlay.refresh_custom_images();
                    overlay.handle_image_selected(result.id);
                    NOTIFY_SUCCESS("Imported {}", filename);
                } else {
                    spdlog::warn("[Printer Image] USB import failed: {}", result.error);
                    if (status_label) {
                        lv_label_set_text(status_label, result.error.c_str());
                    }
                    NOTIFY_WARNING("Import failed: {}", result.error);
                }
            });
        });
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

void PrinterImageOverlay::handle_auto_detect() {
    spdlog::info("[{}] Auto-detect selected", get_name());
    helix::PrinterImageManager::instance().set_active_image("");
    update_selection_indicator("");
    NOTIFY_INFO("Printer image set to auto-detect");
}

void PrinterImageOverlay::handle_image_selected(const std::string& image_id) {
    spdlog::info("[{}] Image selected: {}", get_name(), image_id);
    helix::PrinterImageManager::instance().set_active_image(image_id);
    update_selection_indicator(image_id);

    // Extract display name from ID for notification
    std::string display_name = image_id;
    auto colon_pos = image_id.find(':');
    if (colon_pos != std::string::npos) {
        display_name = image_id.substr(colon_pos + 1);
    }
    NOTIFY_INFO("Printer image: {}", display_name);
}

// ============================================================================
// STATIC CALLBACKS
// ============================================================================

void PrinterImageOverlay::on_auto_detect(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrinterImageOverlay] on_auto_detect");
    get_printer_image_overlay().handle_auto_detect();
    LVGL_SAFE_EVENT_CB_END();
}

void PrinterImageOverlay::on_image_card_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrinterImageOverlay] on_image_card_clicked");
    auto* id = static_cast<const char*>(lv_event_get_user_data(e));
    if (id) {
        get_printer_image_overlay().handle_image_selected(std::string(id));
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PrinterImageOverlay::on_usb_image_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrinterImageOverlay] on_usb_image_clicked");
    auto* path = static_cast<const char*>(lv_event_get_user_data(e));
    if (path) {
        get_printer_image_overlay().handle_usb_import(std::string(path));
    }
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::settings
