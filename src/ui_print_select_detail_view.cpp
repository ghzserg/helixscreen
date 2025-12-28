// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_print_select_detail_view.h"

#include "ui_error_reporting.h"
#include "ui_icon.h"
#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_print_preparation_manager.h"
#include "ui_theme.h"
#include "ui_utils.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

// ============================================================================
// Lifecycle
// ============================================================================

PrintSelectDetailView::~PrintSelectDetailView() {
    // CRITICAL: During static destruction (app exit), LVGL may already be gone.
    // We check if LVGL is still initialized before calling any LVGL functions.
    if (!lv_is_initialized()) {
        return;
    }

    // Deinitialize subjects to disconnect observers before widgets are deleted
    // This prevents dangling pointers and frees observer linked lists
    if (subjects_initialized_) {
        lv_subject_deinit(&preprint_bed_leveling_);
        lv_subject_deinit(&preprint_qgl_);
        lv_subject_deinit(&preprint_z_tilt_);
        lv_subject_deinit(&preprint_nozzle_clean_);
        lv_subject_deinit(&preprint_timelapse_);
        subjects_initialized_ = false;
    }

    // Clean up confirmation dialog if open
    if (confirmation_dialog_widget_) {
        ui_modal_hide(confirmation_dialog_widget_);
        confirmation_dialog_widget_ = nullptr;
    }

    // Clean up main widget if created
    if (detail_view_widget_) {
        lv_obj_delete(detail_view_widget_);
        detail_view_widget_ = nullptr;
    }
}

// ============================================================================
// Setup
// ============================================================================

void PrintSelectDetailView::init_subjects() {
    if (subjects_initialized_) {
        spdlog::debug("[DetailView] Subjects already initialized, skipping");
        return;
    }

    // Enable switches default ON (1) - "perform this operation"
    // Subject=1 means switch is checked, operation is enabled
    lv_subject_init_int(&preprint_bed_leveling_, 1);
    lv_subject_init_int(&preprint_qgl_, 1);
    lv_subject_init_int(&preprint_z_tilt_, 1);
    lv_subject_init_int(&preprint_nozzle_clean_, 1);

    // Add-on switches default OFF (0) - "don't add extras by default"
    lv_subject_init_int(&preprint_timelapse_, 0);

    // Register subjects with XML system so bindings can find them
    lv_xml_register_subject(nullptr, "preprint_bed_leveling", &preprint_bed_leveling_);
    lv_xml_register_subject(nullptr, "preprint_qgl", &preprint_qgl_);
    lv_xml_register_subject(nullptr, "preprint_z_tilt", &preprint_z_tilt_);
    lv_xml_register_subject(nullptr, "preprint_nozzle_clean", &preprint_nozzle_clean_);
    lv_xml_register_subject(nullptr, "preprint_timelapse", &preprint_timelapse_);

    subjects_initialized_ = true;
    spdlog::debug("[DetailView] Initialized pre-print option subjects");
}

bool PrintSelectDetailView::create(lv_obj_t* parent_screen) {
    if (!parent_screen) {
        spdlog::error("[DetailView] Cannot create: parent_screen is null");
        return false;
    }

    if (detail_view_widget_) {
        spdlog::warn("[DetailView] Detail view already exists");
        return true;
    }

    parent_screen_ = parent_screen;

    detail_view_widget_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "print_file_detail", nullptr));

    if (!detail_view_widget_) {
        LOG_ERROR_INTERNAL("[DetailView] Failed to create detail view from XML");
        NOTIFY_ERROR("Failed to load file details");
        return false;
    }

    // Set width to fill space after nav bar
    ui_set_overlay_width(detail_view_widget_, parent_screen_);

    // Set responsive padding for content area
    lv_obj_t* content_container = lv_obj_find_by_name(detail_view_widget_, "content_container");
    if (content_container) {
        lv_coord_t padding = ui_get_header_content_padding(lv_obj_get_height(parent_screen_));
        lv_obj_set_style_pad_all(content_container, padding, 0);
    }

    lv_obj_add_flag(detail_view_widget_, LV_OBJ_FLAG_HIDDEN);

    // Store reference to print button for enable/disable state management
    print_button_ = lv_obj_find_by_name(detail_view_widget_, "print_button");

    // Look up pre-print option checkboxes
    bed_leveling_checkbox_ = lv_obj_find_by_name(detail_view_widget_, "bed_leveling_checkbox");
    qgl_checkbox_ = lv_obj_find_by_name(detail_view_widget_, "qgl_checkbox");
    z_tilt_checkbox_ = lv_obj_find_by_name(detail_view_widget_, "z_tilt_checkbox");
    nozzle_clean_checkbox_ = lv_obj_find_by_name(detail_view_widget_, "nozzle_clean_checkbox");
    timelapse_checkbox_ = lv_obj_find_by_name(detail_view_widget_, "timelapse_checkbox");

    // Look up color requirements display
    color_requirements_card_ = lv_obj_find_by_name(detail_view_widget_, "color_requirements_card");
    color_swatches_row_ = lv_obj_find_by_name(detail_view_widget_, "color_swatches_row");

    // Look up history status display
    history_status_row_ = lv_obj_find_by_name(detail_view_widget_, "history_status_row");
    history_status_icon_ = lv_obj_find_by_name(detail_view_widget_, "history_status_icon");
    history_status_label_ = lv_obj_find_by_name(detail_view_widget_, "history_status_label");

    // Initialize print preparation manager
    prep_manager_ = std::make_unique<PrintPreparationManager>();

    spdlog::debug("[DetailView] Detail view created");
    return true;
}

void PrintSelectDetailView::set_dependencies(MoonrakerAPI* api, PrinterState* printer_state) {
    api_ = api;
    printer_state_ = printer_state;

    if (prep_manager_) {
        prep_manager_->set_dependencies(api_, printer_state_);
        prep_manager_->set_checkboxes(bed_leveling_checkbox_, qgl_checkbox_, z_tilt_checkbox_,
                                      nozzle_clean_checkbox_, timelapse_checkbox_);
    }
}

// ============================================================================
// Visibility
// ============================================================================

void PrintSelectDetailView::show(const std::string& filename, const std::string& current_path,
                                 const std::string& filament_type,
                                 const std::vector<std::string>& filament_colors,
                                 size_t file_size_bytes) {
    // TODO: Use filament_type to set default in filament dropdown
    (void)filament_type;

    if (!detail_view_widget_) {
        spdlog::warn("[DetailView] Cannot show: widget not created");
        return;
    }

    // Reset pre-print option subjects to defaults for new file
    // Skip switches default ON (don't skip = preserve file's original behavior)
    lv_subject_set_int(&preprint_bed_leveling_, 1);
    lv_subject_set_int(&preprint_qgl_, 1);
    lv_subject_set_int(&preprint_z_tilt_, 1);
    lv_subject_set_int(&preprint_nozzle_clean_, 1);
    // Timelapse stays OFF by default (it's an add-on feature)
    lv_subject_set_int(&preprint_timelapse_, 0);

    // Cache file size for safety checks (before modification attempts)
    if (prep_manager_ && file_size_bytes > 0) {
        prep_manager_->set_cached_file_size(file_size_bytes);
    }

    // Trigger async scan for embedded G-code operations (for conflict detection)
    if (!filename.empty() && prep_manager_) {
        prep_manager_->scan_file_for_operations(filename, current_path);
    }

    // Update color requirements display
    update_color_swatches(filament_colors);

    // Use nav system for consistent backdrop and z-order management
    ui_nav_push_overlay(detail_view_widget_);

    if (visible_subject_) {
        lv_subject_set_int(visible_subject_, 1);
    }

    spdlog::debug("[DetailView] Showing detail view for: {} ({} colors)", filename,
                  filament_colors.size());
}

void PrintSelectDetailView::hide() {
    if (!detail_view_widget_) {
        return;
    }

    // Use nav system to properly hide and manage backdrop
    ui_nav_go_back();

    if (visible_subject_) {
        lv_subject_set_int(visible_subject_, 0);
    }

    spdlog::debug("[DetailView] Detail view hidden");
}

bool PrintSelectDetailView::is_visible() const {
    if (visible_subject_) {
        return lv_subject_get_int(visible_subject_) != 0;
    }
    return detail_view_widget_ && !lv_obj_has_flag(detail_view_widget_, LV_OBJ_FLAG_HIDDEN);
}

// ============================================================================
// Delete Confirmation
// ============================================================================

void PrintSelectDetailView::show_delete_confirmation(const std::string& filename) {
    // Create message with current filename
    char msg_buf[256];
    snprintf(msg_buf, sizeof(msg_buf),
             "Are you sure you want to delete '%s'? This action cannot be undone.",
             filename.c_str());

    confirmation_dialog_widget_ =
        ui_modal_show_confirmation("Delete File?", msg_buf, ModalSeverity::Warning, "Delete",
                                   on_confirm_delete_static, on_cancel_delete_static, this);

    if (!confirmation_dialog_widget_) {
        spdlog::error("[DetailView] Failed to create confirmation dialog");
        return;
    }

    spdlog::info("[DetailView] Delete confirmation dialog shown for: {}", filename);
}

void PrintSelectDetailView::hide_delete_confirmation() {
    if (confirmation_dialog_widget_) {
        ui_modal_hide(confirmation_dialog_widget_);
        confirmation_dialog_widget_ = nullptr;
    }
}

// ============================================================================
// Resize Handling
// ============================================================================

void PrintSelectDetailView::handle_resize(lv_obj_t* parent_screen) {
    if (!detail_view_widget_ || !parent_screen) {
        return;
    }

    lv_obj_t* content_container = lv_obj_find_by_name(detail_view_widget_, "content_container");
    if (content_container) {
        lv_coord_t padding = ui_get_header_content_padding(lv_obj_get_height(parent_screen));
        lv_obj_set_style_pad_all(content_container, padding, 0);
    }
}

// ============================================================================
// Internal Methods
// ============================================================================

void PrintSelectDetailView::on_confirm_delete_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectDetailView*>(lv_event_get_user_data(e));
    if (self) {
        self->hide_delete_confirmation();
        if (self->on_delete_confirmed_) {
            self->on_delete_confirmed_();
        }
    }
}

void PrintSelectDetailView::on_cancel_delete_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectDetailView*>(lv_event_get_user_data(e));
    if (self) {
        self->hide_delete_confirmation();
    }
}

void PrintSelectDetailView::update_color_swatches(const std::vector<std::string>& colors) {
    if (!color_requirements_card_ || !color_swatches_row_) {
        return;
    }

    // Hide card if no colors or single color (single-color prints don't need this display)
    if (colors.size() <= 1) {
        lv_obj_add_flag(color_requirements_card_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Clear existing swatches
    lv_obj_clean(color_swatches_row_);

    // Create swatches for each color
    for (size_t i = 0; i < colors.size(); ++i) {
        const std::string& hex_color = colors[i];

        // Create swatch container with tool number label
        lv_obj_t* swatch = lv_obj_create(color_swatches_row_);
        lv_obj_remove_style_all(swatch);
        lv_obj_set_flex_grow(swatch, 1);
        lv_obj_set_height(swatch, LV_PCT(100));
        lv_obj_set_style_radius(swatch, 4, 0);
        lv_obj_set_style_border_width(swatch, 1, 0);
        lv_obj_set_style_border_color(swatch, lv_color_white(), 0);
        lv_obj_set_style_border_opa(swatch, 30, 0);
        lv_obj_remove_flag(swatch, LV_OBJ_FLAG_SCROLLABLE);

        // Parse and set background color
        if (!hex_color.empty()) {
            lv_color_t color = ui_theme_parse_hex_color(hex_color.c_str());
            lv_obj_set_style_bg_color(swatch, color, 0);
            lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
        } else {
            // Empty color - show gray placeholder
            lv_obj_set_style_bg_color(swatch, lv_color_hex(0x808080), 0);
            lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
        }

        // Add tool number label (T0, T1, etc.)
        lv_obj_t* label = lv_label_create(swatch);
        char tool_str[8];
        snprintf(tool_str, sizeof(tool_str), "T%zu", i);
        lv_label_set_text(label, tool_str);
        lv_obj_center(label);
        lv_obj_set_style_text_font(label, UI_FONT_SMALL, 0);

        // Use contrasting text color based on background brightness
        auto parsed_color = ui_parse_hex_color(hex_color);
        if (parsed_color) {
            uint32_t rgb = *parsed_color;
            int r = (rgb >> 16) & 0xFF;
            int g = (rgb >> 8) & 0xFF;
            int b = rgb & 0xFF;
            // Simple brightness check using luminance weights
            int brightness = (r * 299 + g * 587 + b * 114) / 1000;
            lv_color_t text_color = brightness > 128 ? lv_color_black() : lv_color_white();
            lv_obj_set_style_text_color(label, text_color, 0);
        }
    }

    // Show the card
    lv_obj_remove_flag(color_requirements_card_, LV_OBJ_FLAG_HIDDEN);

    spdlog::debug("[DetailView] Updated color swatches: {} colors", colors.size());
}

void PrintSelectDetailView::update_history_status(FileHistoryStatus status, int success_count) {
    if (!history_status_row_ || !history_status_icon_ || !history_status_label_) {
        return;
    }

    switch (status) {
    case FileHistoryStatus::NEVER_PRINTED:
        // Hide the row entirely for files with no history
        lv_obj_add_flag(history_status_row_, LV_OBJ_FLAG_HIDDEN);
        break;

    case FileHistoryStatus::CURRENTLY_PRINTING:
        lv_obj_remove_flag(history_status_row_, LV_OBJ_FLAG_HIDDEN);
        ui_icon_set_source(history_status_icon_, "clock");
        ui_icon_set_variant(history_status_icon_, "accent");
        lv_label_set_text(history_status_label_, "Currently printing");
        break;

    case FileHistoryStatus::COMPLETED: {
        lv_obj_remove_flag(history_status_row_, LV_OBJ_FLAG_HIDDEN);
        ui_icon_set_source(history_status_icon_, "check");
        ui_icon_set_variant(history_status_icon_, "success");
        // Format: "Printed N time(s)"
        char buf[64];
        snprintf(buf, sizeof(buf), "Printed %d time%s", success_count,
                 success_count == 1 ? "" : "s");
        lv_label_set_text(history_status_label_, buf);
        break;
    }

    case FileHistoryStatus::FAILED:
        lv_obj_remove_flag(history_status_row_, LV_OBJ_FLAG_HIDDEN);
        ui_icon_set_source(history_status_icon_, "alert");
        ui_icon_set_variant(history_status_icon_, "warning");
        lv_label_set_text(history_status_label_, "Last print failed");
        break;
    }
}

} // namespace helix::ui
