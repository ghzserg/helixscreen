// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_print_select.h"

#include "ui_error_reporting.h"
#include "ui_fonts.h"
#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_panel_print_status.h"
#include "ui_subject_registry.h"
#include "ui_theme.h"
#include "ui_utils.h"

#include "app_globals.h"
#include "config.h"
#include "runtime_config.h"
#include "lvgl/src/xml/lv_xml.h"
#include "moonraker_api.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

// Forward declaration for class-based API
PrintStatusPanel& get_global_print_status_panel();

// ============================================================================
// Helper Functions
// ============================================================================

namespace {

/**
 * @brief Strip common G-code file extensions for display
 *
 * Removes extensions like .gcode, .g, .gco (case-insensitive) from filenames
 * for cleaner display in the UI. The actual filename is preserved for file operations.
 *
 * @param filename The original filename
 * @return Filename without the G-code extension, or original if no match
 */
std::string strip_gcode_extension(const std::string& filename) {
    // Common G-code extensions (case-insensitive check)
    static const std::vector<std::string> extensions = {".gcode", ".GCODE", ".Gcode", ".gco",
                                                        ".GCO",   ".Gco",   ".g",     ".G"};

    for (const auto& ext : extensions) {
        if (filename.size() > ext.size()) {
            size_t pos = filename.size() - ext.size();
            if (filename.compare(pos, ext.size(), ext) == 0) {
                return filename.substr(0, pos);
            }
        }
    }

    return filename;
}

} // namespace

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<PrintSelectPanel> g_print_select_panel;

PrintSelectPanel* get_print_select_panel(PrinterState& printer_state, MoonrakerAPI* api) {
    if (!g_print_select_panel) {
        g_print_select_panel = std::make_unique<PrintSelectPanel>(printer_state, api);
    }
    return g_print_select_panel.get();
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

PrintSelectPanel::PrintSelectPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    spdlog::debug("[{}] Constructed", get_name());
}

PrintSelectPanel::~PrintSelectPanel() {
    // CRITICAL: Do NOT call LVGL functions here - static destruction order!
    // LVGL may already be destroyed when exit() is called.
    // Just reset our pointers - the LVGL widget tree handles cleanup.
    card_view_container_ = nullptr;
    list_view_container_ = nullptr;
    list_rows_container_ = nullptr;
    empty_state_container_ = nullptr;
    view_toggle_btn_ = nullptr;
    view_toggle_icon_ = nullptr;
    detail_view_widget_ = nullptr;
    confirmation_dialog_widget_ = nullptr;
    print_status_panel_widget_ = nullptr;

    // NOTE: Do NOT log here - spdlog may be destroyed first
}

// ============================================================================
// PanelBase Implementation
// ============================================================================

void PrintSelectPanel::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    // Initialize selected file subjects
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_filename_subject_, selected_filename_buffer_, "",
                                        "selected_filename");

    // Thumbnail uses POINTER subject (required by lv_image_bind_src)
    strncpy(selected_thumbnail_buffer_, DEFAULT_PLACEHOLDER_THUMB,
            sizeof(selected_thumbnail_buffer_) - 1);
    UI_SUBJECT_INIT_AND_REGISTER_POINTER(selected_thumbnail_subject_, selected_thumbnail_buffer_,
                                         "selected_thumbnail");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_print_time_subject_, selected_print_time_buffer_,
                                        "", "selected_print_time");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_filament_weight_subject_,
                                        selected_filament_weight_buffer_, "",
                                        "selected_filament_weight");

    // Initialize detail view visibility subject (0 = hidden, 1 = visible)
    UI_SUBJECT_INIT_AND_REGISTER_INT(detail_view_visible_subject_, 0, "detail_view_visible");

    // Initialize view mode subject (0 = CARD, 1 = LIST) - XML bindings control container visibility
    UI_SUBJECT_INIT_AND_REGISTER_INT(view_mode_subject_, 0, "print_select_view_mode");

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void PrintSelectPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] Cannot setup: panel is null", get_name());
        return;
    }

    // Find widget references
    card_view_container_ = lv_obj_find_by_name(panel_, "card_view_container");
    list_view_container_ = lv_obj_find_by_name(panel_, "list_view_container");
    list_rows_container_ = lv_obj_find_by_name(panel_, "list_rows_container");
    empty_state_container_ = lv_obj_find_by_name(panel_, "empty_state_container");
    view_toggle_btn_ = lv_obj_find_by_name(panel_, "view_toggle_btn");
    view_toggle_icon_ = lv_obj_find_by_name(panel_, "view_toggle_icon");

    if (!card_view_container_ || !list_view_container_ || !list_rows_container_ ||
        !empty_state_container_ || !view_toggle_btn_ || !view_toggle_icon_) {
        spdlog::error("[{}] Failed to find required widgets", get_name());
        return;
    }

    // Wire up view toggle button
    lv_obj_add_event_cb(view_toggle_btn_, on_view_toggle_clicked_static, LV_EVENT_CLICKED, this);

    // Wire up column header click handlers
    // We need to pack both 'this' and column index into user_data
    // Use a simple encoding: store column index in the low bits of a pointer-sized int
    struct HeaderBinding {
        const char* name;
        PrintSelectSortColumn column;
    };
    static const HeaderBinding headers[] = {
        {"header_filename", PrintSelectSortColumn::FILENAME},
        {"header_size", PrintSelectSortColumn::SIZE},
        {"header_modified", PrintSelectSortColumn::MODIFIED},
        {"header_print_time", PrintSelectSortColumn::PRINT_TIME}};

    for (const auto& binding : headers) {
        lv_obj_t* header = lv_obj_find_by_name(panel_, binding.name);
        if (header) {
            // Encode column in user_data: we'll use the address of the static array entry
            // The callback will recover the column from the difference
            lv_obj_add_event_cb(header, on_header_clicked_static, LV_EVENT_CLICKED, this);
            // Store column index in the object's user_data for recovery
            lv_obj_set_user_data(header,
                                 reinterpret_cast<void*>(static_cast<intptr_t>(binding.column)));
        }
    }

    // Create detail view (confirmation dialog created on-demand)
    create_detail_view();

    // Register resize callback
    // Note: ui_resize_handler_register expects a C callback, so we use a static trampoline
    // We store 'this' in a static variable since the resize system doesn't support user_data
    // This is safe because there's only one PrintSelectPanel instance
    static PrintSelectPanel* resize_self = nullptr;
    resize_self = this;
    ui_resize_handler_register([]() {
        if (resize_self) {
            resize_self->handle_resize();
        }
    });

    // Mark panel as fully initialized (enables resize callbacks)
    panel_initialized_ = true;

    // Refresh from Moonraker when API becomes available (via set_api)
    // Don't populate anything here - wait for API connection
    if (api_) {
        refresh_files();
    } else {
        spdlog::debug("[{}] MoonrakerAPI not available yet, waiting for set_api()", get_name());
        update_empty_state();
    }

    spdlog::debug("[{}] Setup complete", get_name());
}

// ============================================================================
// Public API
// ============================================================================

void PrintSelectPanel::toggle_view() {
    if (current_view_mode_ == PrintSelectViewMode::CARD) {
        // Switch to list view
        current_view_mode_ = PrintSelectViewMode::LIST;

        // Update reactive subject - XML bindings handle container visibility
        lv_subject_set_int(&view_mode_subject_, 1);

        // Update icon to show grid_view (indicates you can switch back to card view)
        const void* grid_icon = lv_xml_get_image(NULL, "mat_grid_view");
        if (grid_icon) {
            lv_image_set_src(view_toggle_icon_, grid_icon);
        }
        spdlog::debug("[{}] Switched to list view", get_name());
    } else {
        // Switch to card view
        current_view_mode_ = PrintSelectViewMode::CARD;

        // Update reactive subject - XML bindings handle container visibility
        lv_subject_set_int(&view_mode_subject_, 0);

        // Update icon to show list (indicates you can switch to list view)
        const void* list_icon = lv_xml_get_image(NULL, "mat_list");
        if (list_icon) {
            lv_image_set_src(view_toggle_icon_, list_icon);
        }
        spdlog::debug("[{}] Switched to card view", get_name());
    }

    update_empty_state();
}

void PrintSelectPanel::sort_by(PrintSelectSortColumn column) {
    // Toggle direction if same column, otherwise default to ascending
    if (column == current_sort_column_) {
        current_sort_direction_ = (current_sort_direction_ == PrintSelectSortDirection::ASCENDING)
                                      ? PrintSelectSortDirection::DESCENDING
                                      : PrintSelectSortDirection::ASCENDING;
    } else {
        current_sort_column_ = column;
        current_sort_direction_ = PrintSelectSortDirection::ASCENDING;
    }

    apply_sort();
    update_sort_indicators();

    // Repopulate current view
    if (current_view_mode_ == PrintSelectViewMode::CARD) {
        populate_card_view();
    } else {
        populate_list_view();
    }

    spdlog::debug("[{}] Sorted by column {}, direction {}", get_name(), static_cast<int>(column),
                  static_cast<int>(current_sort_direction_));
}

void PrintSelectPanel::refresh_files() {
    if (!api_) {
        spdlog::warn("[{}] Cannot refresh files: MoonrakerAPI not initialized", get_name());
        return;
    }

    spdlog::info("[{}] Refreshing file list from Moonraker (path: '{}')...", get_name(),
                 current_path_.empty() ? "/" : current_path_);

    // Capture 'this' for async callbacks
    auto* self = this;

    // Request file list from current directory (non-recursive)
    api_->list_files(
        "gcodes", current_path_, false,
        // Success callback
        [self](const std::vector<FileInfo>& files) {
            spdlog::info("[{}] Received {} items from Moonraker", self->get_name(), files.size());

            // Clear existing file list
            self->file_list_.clear();

            // Add ".." parent directory entry if not at root
            if (!self->current_path_.empty()) {
                PrintFileData parent_dir;
                parent_dir.filename = "..";
                parent_dir.is_dir = true;
                parent_dir.thumbnail_path = self->FOLDER_UP_ICON;
                parent_dir.size_str = "Go up";
                parent_dir.print_time_str = "";
                parent_dir.filament_str = "";
                parent_dir.modified_str = "";
                self->file_list_.push_back(parent_dir);
            }

            // Convert FileInfo to PrintFileData (include directories)
            for (const auto& file : files) {
                PrintFileData data;
                data.filename = file.filename;
                data.is_dir = file.is_dir;
                data.file_size_bytes = file.size;
                data.modified_timestamp = static_cast<time_t>(file.modified);

                if (file.is_dir) {
                    // Directory - use folder icon
                    data.thumbnail_path = self->FOLDER_ICON;
                    data.print_time_minutes = 0;
                    data.filament_grams = 0.0f;
                    data.size_str = "Folder";
                    data.print_time_str = "";
                    data.filament_str = "";
                } else {
                    // Only process .gcode files
                    if (file.filename.find(".gcode") == std::string::npos &&
                        file.filename.find(".g") == std::string::npos) {
                        continue;
                    }

                    data.thumbnail_path = self->DEFAULT_PLACEHOLDER_THUMB;
                    data.print_time_minutes = 0;
                    data.filament_grams = 0.0f;
                    data.size_str = format_file_size(data.file_size_bytes);
                    data.print_time_str = format_print_time(data.print_time_minutes);
                    data.filament_str = format_filament_weight(data.filament_grams);
                }

                data.modified_str = format_modified_date(data.modified_timestamp);
                self->file_list_.push_back(data);
            }

            // Show files immediately with placeholder metadata
            self->apply_sort();
            self->update_sort_indicators();
            self->populate_card_view();
            self->populate_list_view();
            self->update_empty_state();

            // Check for pending file selection (--select-file flag or API call)
            std::string pending;
            if (!self->pending_file_selection_.empty()) {
                pending = self->pending_file_selection_;
                self->pending_file_selection_.clear(); // Clear to avoid repeat
            } else if (get_runtime_config().select_file != nullptr) {
                // Check runtime config on first load (--select-file CLI flag)
                static bool select_file_checked = false;
                if (!select_file_checked) {
                    pending = get_runtime_config().select_file;
                    select_file_checked = true;
                }
            }
            if (!pending.empty()) {
                if (!self->select_file_by_name(pending)) {
                    spdlog::warn("[{}] Pending file selection '{}' not found in file list",
                                 self->get_name(), pending);
                }
            }

            // Count files vs directories
            size_t dir_count = 0, file_count = 0;
            for (const auto& item : self->file_list_) {
                if (item.is_dir)
                    dir_count++;
                else
                    file_count++;
            }
            spdlog::info("[{}] File list updated: {} directories, {} G-code files",
                         self->get_name(), dir_count, file_count);

            // Fetch metadata for files only (not directories)
            for (size_t i = 0; i < self->file_list_.size(); i++) {
                if (self->file_list_[i].is_dir)
                    continue; // Skip directories

                const std::string filename = self->file_list_[i].filename;

                self->api_->get_file_metadata(
                    filename,
                    // Metadata success callback
                    [self, i, filename](const FileMetadata& metadata) {
                        // Bounds check (file_list could change during async operation)
                        if (i >= self->file_list_.size() ||
                            self->file_list_[i].filename != filename) {
                            spdlog::warn("[{}] File list changed during metadata fetch for {}",
                                         self->get_name(), filename);
                            return;
                        }

                        // Update metadata fields
                        self->file_list_[i].print_time_minutes =
                            static_cast<int>(metadata.estimated_time / 60.0);
                        self->file_list_[i].filament_grams =
                            static_cast<float>(metadata.filament_weight_total);
                        self->file_list_[i].filament_type = metadata.filament_type;

                        // Update formatted strings
                        self->file_list_[i].print_time_str =
                            format_print_time(self->file_list_[i].print_time_minutes);
                        self->file_list_[i].filament_str =
                            format_filament_weight(self->file_list_[i].filament_grams);

                        spdlog::debug(
                            "[{}] Updated metadata for {}: {}min, {}g, type={}", self->get_name(),
                            filename, self->file_list_[i].print_time_minutes,
                            self->file_list_[i].filament_grams, self->file_list_[i].filament_type);

                        // Use thumbnail if available (add LVGL filesystem prefix)
                        if (!metadata.thumbnails.empty()) {
                            self->file_list_[i].thumbnail_path = "A:" + metadata.thumbnails[0];
                            spdlog::debug("[{}] Thumbnail for {}: {}", self->get_name(), filename,
                                          self->file_list_[i].thumbnail_path);
                        }

                        // Re-render views to show updated metadata
                        self->populate_card_view();
                        self->populate_list_view();

                        // Also update detail view if this file is currently selected
                        if (strcmp(self->selected_filename_buffer_, filename.c_str()) == 0) {
                            spdlog::debug(
                                "[{}] Updating detail view for selected file: {}", self->get_name(),
                                filename);
                            self->set_selected_file(
                                filename.c_str(),
                                self->file_list_[i].thumbnail_path.c_str(),
                                self->file_list_[i].print_time_str.c_str(),
                                self->file_list_[i].filament_str.c_str());
                        }
                    },
                    // Metadata error callback
                    [self, filename](const MoonrakerError& error) {
                        spdlog::warn("[{}] Failed to get metadata for {}: {} ({})",
                                     self->get_name(), filename, error.message,
                                     error.get_type_string());
                    });
            }
        },
        // Error callback
        [self](const MoonrakerError& error) {
            NOTIFY_ERROR("Failed to refresh file list");
            LOG_ERROR_INTERNAL("[{}] File list refresh error: {} ({})", self->get_name(),
                               error.message, error.get_type_string());
        });
}

void PrintSelectPanel::set_api(MoonrakerAPI* api) {
    api_ = api;

    // Automatically refresh file list when API becomes available
    if (api_ && panel_initialized_) {
        spdlog::info("[{}] API connected, refreshing file list", get_name());
        refresh_files();
    }
}

void PrintSelectPanel::navigate_to_directory(const std::string& dirname) {
    // Build new path
    if (current_path_.empty()) {
        current_path_ = dirname;
    } else {
        current_path_ = current_path_ + "/" + dirname;
    }

    spdlog::info("[{}] Navigating to directory: {}", get_name(), current_path_);
    refresh_files();
}

void PrintSelectPanel::navigate_up() {
    // Don't navigate above root
    if (current_path_.empty()) {
        spdlog::debug("[{}] Already at root, cannot navigate up", get_name());
        return;
    }

    // Find last path separator and truncate
    size_t last_slash = current_path_.rfind('/');
    if (last_slash == std::string::npos) {
        // No slash found - we're one level deep, go to root
        current_path_.clear();
    } else {
        // Truncate at last slash
        current_path_ = current_path_.substr(0, last_slash);
    }

    spdlog::info("[{}] Navigating up to: {}", get_name(),
                 current_path_.empty() ? "/" : current_path_);
    refresh_files();
}

void PrintSelectPanel::set_selected_file(const char* filename, const char* thumbnail_src,
                                         const char* print_time, const char* filament_weight) {
    lv_subject_copy_string(&selected_filename_subject_, filename);

    // Thumbnail uses POINTER subject - copy to buffer then update pointer
    strncpy(selected_thumbnail_buffer_, thumbnail_src, sizeof(selected_thumbnail_buffer_) - 1);
    selected_thumbnail_buffer_[sizeof(selected_thumbnail_buffer_) - 1] = '\0';
    lv_subject_set_pointer(&selected_thumbnail_subject_, selected_thumbnail_buffer_);

    lv_subject_copy_string(&selected_print_time_subject_, print_time);
    lv_subject_copy_string(&selected_filament_weight_subject_, filament_weight);

    spdlog::info("[{}] Selected file: {}", get_name(), filename);
}

void PrintSelectPanel::show_detail_view() {
    if (detail_view_widget_) {
        // Trigger async scan for embedded G-code operations (for conflict detection)
        std::string filename(selected_filename_buffer_);
        if (!filename.empty()) {
            scan_gcode_for_operations(filename);
        }

        // Set filament type dropdown to match file metadata
        lv_obj_t* dropdown = lv_obj_find_by_name(detail_view_widget_, "filament_dropdown");
        if (dropdown && !selected_filament_type_.empty()) {
            // Map filament type string to dropdown index
            // Options: PLA(0), PETG(1), ABS(2), TPU(3), Nylon(4), ASA(5), PC(6)
            uint32_t index = 0; // Default to PLA
            if (selected_filament_type_ == "PETG") {
                index = 1;
            } else if (selected_filament_type_ == "ABS") {
                index = 2;
            } else if (selected_filament_type_ == "TPU") {
                index = 3;
            } else if (selected_filament_type_ == "Nylon" || selected_filament_type_ == "NYLON" ||
                       selected_filament_type_ == "PA") {
                index = 4;
            } else if (selected_filament_type_ == "ASA") {
                index = 5;
            } else if (selected_filament_type_ == "PC") {
                index = 6;
            }
            // PLA is index 0 (default) for "PLA" or any unrecognized type
            lv_dropdown_set_selected(dropdown, index);
            spdlog::debug("[{}] Set filament dropdown to {} (index {})", get_name(),
                          selected_filament_type_, index);
        }

        // Use nav system for consistent backdrop and z-order management
        ui_nav_push_overlay(detail_view_widget_);
        lv_subject_set_int(&detail_view_visible_subject_, 1);
    }
}

void PrintSelectPanel::hide_detail_view() {
    if (detail_view_widget_) {
        // Use nav system to properly hide and manage backdrop
        ui_nav_go_back();
        lv_subject_set_int(&detail_view_visible_subject_, 0);
    }
}

void PrintSelectPanel::show_delete_confirmation() {
    // Configure modal
    ui_modal_config_t config = {.position = {.use_alignment = true, .alignment = LV_ALIGN_CENTER},
                                .backdrop_opa = 180,
                                .keyboard = nullptr,
                                .persistent = false,
                                .on_close = nullptr};

    // Create message with current filename
    char msg_buf[256];
    snprintf(msg_buf, sizeof(msg_buf),
             "Are you sure you want to delete '%s'? This action cannot be undone.",
             selected_filename_buffer_);

    const char* attrs[] = {"title", "Delete File?", "message", msg_buf, NULL};

    confirmation_dialog_widget_ = ui_modal_show("confirmation_dialog", &config, attrs);

    if (!confirmation_dialog_widget_) {
        spdlog::error("[{}] Failed to create confirmation dialog", get_name());
        return;
    }

    // Wire up cancel button
    lv_obj_t* cancel_btn = lv_obj_find_by_name(confirmation_dialog_widget_, "dialog_cancel_btn");
    if (cancel_btn) {
        lv_obj_add_event_cb(cancel_btn, on_cancel_delete_static, LV_EVENT_CLICKED, this);
    }

    // Wire up confirm button
    lv_obj_t* confirm_btn = lv_obj_find_by_name(confirmation_dialog_widget_, "dialog_confirm_btn");
    if (confirm_btn) {
        lv_obj_add_event_cb(confirm_btn, on_confirm_delete_static, LV_EVENT_CLICKED, this);
    }

    spdlog::info("[{}] Delete confirmation dialog shown", get_name());
}

void PrintSelectPanel::set_print_status_panel(lv_obj_t* panel) {
    print_status_panel_widget_ = panel;
    spdlog::debug("[{}] Print status panel reference set", get_name());
}

// ============================================================================
// Internal Methods
// ============================================================================

CardDimensions PrintSelectPanel::calculate_card_dimensions() {
    if (!card_view_container_) {
        spdlog::error("[{}] Cannot calculate dimensions: container is null", get_name());
        return {4, 2, CARD_MIN_WIDTH, CARD_DEFAULT_HEIGHT};
    }

    lv_coord_t container_width = lv_obj_get_content_width(card_view_container_);

    // Calculate available height from parent panel dimensions
    lv_obj_t* panel_root = lv_obj_get_parent(card_view_container_);
    if (!panel_root) {
        spdlog::error("[{}] Cannot find panel root", get_name());
        return {4, 2, CARD_MIN_WIDTH, CARD_DEFAULT_HEIGHT};
    }

    lv_coord_t panel_height = lv_obj_get_height(panel_root);
    lv_obj_t* top_bar = lv_obj_get_child(panel_root, 0);
    lv_coord_t top_bar_height = top_bar ? lv_obj_get_height(top_bar) : 60;
    lv_coord_t panel_gap = lv_obj_get_style_pad_row(panel_root, LV_PART_MAIN);
    lv_coord_t container_padding = lv_obj_get_style_pad_top(card_view_container_, LV_PART_MAIN) +
                                   lv_obj_get_style_pad_bottom(card_view_container_, LV_PART_MAIN);
    lv_coord_t available_height = panel_height - top_bar_height - container_padding - panel_gap;

    CardDimensions dims;

    // Determine optimal number of rows based on available height
    dims.num_rows = (available_height >= ROW_COUNT_3_MIN_HEIGHT) ? 3 : 2;

    // Calculate card height based on rows
    int total_row_gaps = (dims.num_rows - 1) * CARD_GAP;
    dims.card_height = (available_height - total_row_gaps) / dims.num_rows;

    // Try different column counts
    for (int cols = 10; cols >= 1; cols--) {
        int total_gaps = (cols - 1) * CARD_GAP;
        int card_width = (container_width - total_gaps) / cols;

        if (card_width >= CARD_MIN_WIDTH && card_width <= CARD_MAX_WIDTH) {
            dims.num_columns = cols;
            dims.card_width = card_width;

            spdlog::debug("[{}] Calculated card layout: {} rows x {} columns, card={}x{}",
                          get_name(), dims.num_rows, dims.num_columns, dims.card_width,
                          dims.card_height);
            return dims;
        }
    }

    // Fallback
    dims.num_columns = container_width / (CARD_MIN_WIDTH + CARD_GAP);
    if (dims.num_columns < 1)
        dims.num_columns = 1;
    dims.card_width = CARD_MIN_WIDTH;

    spdlog::warn("[{}] No optimal card layout found, using fallback: {} columns", get_name(),
                 dims.num_columns);
    return dims;
}

void PrintSelectPanel::populate_card_view() {
    if (!card_view_container_)
        return;

    // Clear existing cards
    lv_obj_clean(card_view_container_);

    // Force layout calculation
    lv_obj_update_layout(card_view_container_);

    // Calculate optimal card dimensions
    CardDimensions dims = calculate_card_dimensions();

    // Update container gap
    lv_obj_set_style_pad_gap(card_view_container_, CARD_GAP, LV_PART_MAIN);

    for (size_t i = 0; i < file_list_.size(); i++) {
        const auto& file = file_list_[i];

        // For directories, append "/" to indicate navigable folder
        // For files, strip extension for cleaner display
        std::string display_name =
            file.is_dir ? file.filename + "/" : strip_gcode_extension(file.filename);

        const char* attrs[] = {"thumbnail_src",
                               file.thumbnail_path.c_str(),
                               "filename",
                               display_name.c_str(),
                               "print_time",
                               file.print_time_str.c_str(),
                               "filament_weight",
                               file.filament_str.c_str(),
                               NULL};

        lv_obj_t* card =
            static_cast<lv_obj_t*>(lv_xml_create(card_view_container_, CARD_COMPONENT_NAME, attrs));

        if (card) {
            // Manually set thumbnail src - XML can only resolve pre-registered images,
            // not dynamic file paths like cached thumbnails
            lv_obj_t* thumb_img = lv_obj_find_by_name(card, "thumbnail");
            if (thumb_img && !file.thumbnail_path.empty()) {
                lv_image_set_src(thumb_img, file.thumbnail_path.c_str());
            }

            lv_obj_set_width(card, dims.card_width);
            lv_obj_set_height(card, dims.card_height);
            lv_obj_set_style_flex_grow(card, 0, LV_PART_MAIN);

            // For directories: recolor icon, hide metadata, reduce overlay height
            if (file.is_dir) {
                // Recolor the folder icon to amber/yellow (classic folder color)
                if (thumb_img) {
                    lv_obj_set_style_image_recolor(thumb_img, lv_color_hex(0xFFB74D), 0);
                    lv_obj_set_style_image_recolor_opa(thumb_img, LV_OPA_COVER, 0);
                }

                lv_obj_t* metadata_row = lv_obj_find_by_name(card, "metadata_row");
                if (metadata_row) {
                    lv_obj_add_flag(metadata_row, LV_OBJ_FLAG_HIDDEN);
                }

                // Reduce overlay heights for cleaner folder appearance
                lv_obj_t* metadata_clip = lv_obj_find_by_name(card, "metadata_clip");
                lv_obj_t* metadata_overlay = lv_obj_find_by_name(card, "metadata_overlay");
                if (metadata_clip && metadata_overlay) {
                    lv_obj_set_height(metadata_clip, 40);
                    lv_obj_set_height(metadata_overlay, 48);
                }
            }

            // Attach click handler
            attach_card_click_handler(card, i);
        }
    }
}

void PrintSelectPanel::populate_list_view() {
    if (!list_rows_container_)
        return;

    // Clear existing rows
    lv_obj_clean(list_rows_container_);

    for (size_t i = 0; i < file_list_.size(); i++) {
        const auto& file = file_list_[i];

        // For directories, append "/" to indicate navigable folder
        // For files, strip extension for cleaner display
        std::string display_name =
            file.is_dir ? file.filename + "/" : strip_gcode_extension(file.filename);

        const char* attrs[] = {"filename",
                               display_name.c_str(),
                               "file_size",
                               file.size_str.c_str(),
                               "modified_date",
                               file.modified_str.c_str(),
                               "print_time",
                               file.print_time_str.c_str(),
                               NULL};

        lv_obj_t* row = static_cast<lv_obj_t*>(
            lv_xml_create(list_rows_container_, "print_file_list_row", attrs));

        if (row) {
            attach_row_click_handler(row, i);
        }
    }
}

void PrintSelectPanel::apply_sort() {
    auto sort_column = current_sort_column_;
    auto sort_direction = current_sort_direction_;

    std::sort(file_list_.begin(), file_list_.end(),
              [sort_column, sort_direction](const PrintFileData& a, const PrintFileData& b) {
                  bool result = false;

                  switch (sort_column) {
                  case PrintSelectSortColumn::FILENAME:
                      result = a.filename < b.filename;
                      break;
                  case PrintSelectSortColumn::SIZE:
                      result = a.file_size_bytes < b.file_size_bytes;
                      break;
                  case PrintSelectSortColumn::MODIFIED:
                      result = a.modified_timestamp > b.modified_timestamp;
                      break;
                  case PrintSelectSortColumn::PRINT_TIME:
                      result = a.print_time_minutes < b.print_time_minutes;
                      break;
                  case PrintSelectSortColumn::FILAMENT:
                      result = a.filament_grams < b.filament_grams;
                      break;
                  }

                  if (sort_direction == PrintSelectSortDirection::DESCENDING) {
                      result = !result;
                  }

                  return result;
              });
}

void PrintSelectPanel::update_empty_state() {
    if (!empty_state_container_)
        return;

    bool is_empty = file_list_.empty();

    if (is_empty) {
        lv_obj_remove_flag(empty_state_container_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(card_view_container_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(list_view_container_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(empty_state_container_, LV_OBJ_FLAG_HIDDEN);

        if (current_view_mode_ == PrintSelectViewMode::CARD) {
            lv_obj_remove_flag(card_view_container_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(list_view_container_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(card_view_container_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(list_view_container_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void PrintSelectPanel::update_sort_indicators() {
    const char* header_names[] = {"header_filename", "header_size", "header_modified",
                                  "header_print_time"};
    PrintSelectSortColumn columns[] = {PrintSelectSortColumn::FILENAME, PrintSelectSortColumn::SIZE,
                                       PrintSelectSortColumn::MODIFIED,
                                       PrintSelectSortColumn::PRINT_TIME};

    for (int i = 0; i < 4; i++) {
        char icon_up_name[64];
        char icon_down_name[64];
        snprintf(icon_up_name, sizeof(icon_up_name), "%s_icon_up", header_names[i]);
        snprintf(icon_down_name, sizeof(icon_down_name), "%s_icon_down", header_names[i]);

        lv_obj_t* icon_up = lv_obj_find_by_name(panel_, icon_up_name);
        lv_obj_t* icon_down = lv_obj_find_by_name(panel_, icon_down_name);

        if (icon_up && icon_down) {
            if (columns[i] == current_sort_column_) {
                if (current_sort_direction_ == PrintSelectSortDirection::ASCENDING) {
                    lv_obj_remove_flag(icon_up, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(icon_down, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(icon_up, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(icon_down, LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                lv_obj_add_flag(icon_up, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(icon_down, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

void PrintSelectPanel::create_detail_view() {
    if (!parent_screen_) {
        spdlog::error("[{}] Cannot create detail view: parent_screen is null", get_name());
        return;
    }

    if (detail_view_widget_) {
        spdlog::error("[{}] Detail view already exists", get_name());
        return;
    }

    detail_view_widget_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "print_file_detail", nullptr));

    if (!detail_view_widget_) {
        LOG_ERROR_INTERNAL("[{}] Failed to create detail view from XML", get_name());
        NOTIFY_ERROR("Failed to load file details");
        return;
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

    // Wire up back button
    lv_obj_t* back_button = lv_obj_find_by_name(detail_view_widget_, "back_button");
    if (back_button) {
        lv_obj_add_event_cb(back_button, on_back_button_clicked_static, LV_EVENT_CLICKED, this);
    }

    // Wire up delete button
    lv_obj_t* delete_button = lv_obj_find_by_name(detail_view_widget_, "delete_button");
    if (delete_button) {
        lv_obj_add_event_cb(delete_button, on_delete_button_clicked_static, LV_EVENT_CLICKED, this);
    }

    // Wire up print button
    lv_obj_t* print_button = lv_obj_find_by_name(detail_view_widget_, "print_button");
    if (print_button) {
        lv_obj_add_event_cb(print_button, on_print_button_clicked_static, LV_EVENT_CLICKED, this);
    }

    // Click backdrop to close
    lv_obj_add_event_cb(detail_view_widget_, on_detail_backdrop_clicked_static, LV_EVENT_CLICKED,
                        this);

    // Look up pre-print option checkboxes
    bed_leveling_checkbox_ = lv_obj_find_by_name(detail_view_widget_, "bed_leveling_checkbox");
    qgl_checkbox_ = lv_obj_find_by_name(detail_view_widget_, "qgl_checkbox");
    z_tilt_checkbox_ = lv_obj_find_by_name(detail_view_widget_, "z_tilt_checkbox");
    nozzle_clean_checkbox_ = lv_obj_find_by_name(detail_view_widget_, "nozzle_clean_checkbox");

    spdlog::debug("[{}] Detail view created", get_name());
}

void PrintSelectPanel::hide_delete_confirmation() {
    if (confirmation_dialog_widget_) {
        ui_modal_hide(confirmation_dialog_widget_);
        confirmation_dialog_widget_ = nullptr;
    }
}

void PrintSelectPanel::handle_resize() {
    if (!panel_initialized_)
        return;

    spdlog::info("[{}] Handling resize event", get_name());

    if (current_view_mode_ == PrintSelectViewMode::CARD && card_view_container_) {
        populate_card_view();
    }

    if (detail_view_widget_ && parent_screen_) {
        lv_obj_t* content_container = lv_obj_find_by_name(detail_view_widget_, "content_container");
        if (content_container) {
            lv_coord_t padding = ui_get_header_content_padding(lv_obj_get_height(parent_screen_));
            lv_obj_set_style_pad_all(content_container, padding, 0);
        }
    }
}

void PrintSelectPanel::attach_card_click_handler(lv_obj_t* card, size_t file_index) {
    // Store file index in widget user_data
    lv_obj_set_user_data(card, reinterpret_cast<void*>(file_index));
    lv_obj_add_event_cb(card, on_file_clicked_static, LV_EVENT_CLICKED, this);
}

void PrintSelectPanel::attach_row_click_handler(lv_obj_t* row, size_t file_index) {
    lv_obj_set_user_data(row, reinterpret_cast<void*>(file_index));
    lv_obj_add_event_cb(row, on_file_clicked_static, LV_EVENT_CLICKED, this);
}

void PrintSelectPanel::handle_file_click(size_t file_index) {
    if (file_index >= file_list_.size()) {
        spdlog::error("[{}] Invalid file index: {}", get_name(), file_index);
        return;
    }

    const auto& file = file_list_[file_index];

    if (file.is_dir) {
        if (file.filename == "..") {
            // Parent directory - navigate up
            navigate_up();
        } else {
            // Directory clicked - navigate into it
            navigate_to_directory(file.filename);
        }
    } else {
        // File clicked - show detail view
        set_selected_file(file.filename.c_str(), file.thumbnail_path.c_str(),
                          file.print_time_str.c_str(), file.filament_str.c_str());
        selected_filament_type_ = file.filament_type;
        show_detail_view();
    }
}

void PrintSelectPanel::start_print() {
    // Stage 9: Concurrent Print Prevention
    // Check if a print is already active before allowing a new one to start
    if (!printer_state_.can_start_new_print()) {
        PrintJobState current_state = printer_state_.get_print_job_state();
        const char* state_str = print_job_state_to_string(current_state);
        NOTIFY_ERROR("Cannot start print: printer is {}", state_str);
        spdlog::warn("[{}] Attempted to start print while printer is in {} state",
                     get_name(), state_str);
        return;
    }

    std::string filename_to_print(selected_filename_buffer_);
    auto* self = this;

    // Helper to check if checkbox is visible and checked
    auto is_option_enabled = [](lv_obj_t* checkbox) -> bool {
        if (!checkbox)
            return false;
        // Only consider it enabled if visible (not hidden) and checked
        bool is_visible = !lv_obj_has_flag(checkbox, LV_OBJ_FLAG_HIDDEN);
        bool is_checked = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
        return is_visible && is_checked;
    };

    // Read pre-print option states
    bool do_bed_leveling = is_option_enabled(bed_leveling_checkbox_);
    bool do_qgl = is_option_enabled(qgl_checkbox_);
    bool do_z_tilt = is_option_enabled(z_tilt_checkbox_);
    bool do_nozzle_clean = is_option_enabled(nozzle_clean_checkbox_);

    bool has_pre_print_ops = do_bed_leveling || do_qgl || do_z_tilt || do_nozzle_clean;

    spdlog::info("[{}] Starting print: {} (pre-print: mesh={}, qgl={}, z_tilt={}, clean={})",
                 get_name(), filename_to_print, do_bed_leveling, do_qgl, do_z_tilt, do_nozzle_clean);

    if (api_) {
        // Check if user disabled operations that are embedded in the G-code file.
        // If so, we need to modify the file before printing to comment out those operations.
        std::vector<gcode::OperationType> ops_to_disable = collect_ops_to_disable();

        if (!ops_to_disable.empty()) {
            spdlog::info("[{}] User disabled {} embedded operations - modifying G-code",
                         get_name(), ops_to_disable.size());
            modify_and_print(filename_to_print, ops_to_disable);
            return; // modify_and_print handles everything including navigation
        }
        if (has_pre_print_ops) {
            // Create command sequencer for pre-print operations
            pre_print_sequencer_ = std::make_unique<gcode::CommandSequencer>(
                api_->get_client(), *api_, printer_state_);

            // Always home first if doing any pre-print operations
            pre_print_sequencer_->add_operation(gcode::OperationType::HOMING, {}, "Homing");

            // Add selected operations in logical order
            if (do_qgl) {
                pre_print_sequencer_->add_operation(gcode::OperationType::QGL, {},
                                                    "Quad Gantry Level");
            }
            if (do_z_tilt) {
                pre_print_sequencer_->add_operation(gcode::OperationType::Z_TILT, {},
                                                    "Z-Tilt Adjust");
            }
            if (do_bed_leveling) {
                pre_print_sequencer_->add_operation(gcode::OperationType::BED_LEVELING, {},
                                                    "Bed Mesh Calibration");
            }
            if (do_nozzle_clean) {
                pre_print_sequencer_->add_operation(gcode::OperationType::NOZZLE_CLEAN, {},
                                                    "Clean Nozzle");
            }

            // Add the actual print start as the final operation
            gcode::OperationParams print_params;
            print_params.filename = filename_to_print;
            pre_print_sequencer_->add_operation(gcode::OperationType::START_PRINT, print_params,
                                                "Starting Print");

            // Show the print status panel immediately in "Preparing" state
            if (print_status_panel_widget_) {
                hide_detail_view();
                ui_nav_push_overlay(print_status_panel_widget_);

                // Initialize the preparing state
                auto& status_panel = get_global_print_status_panel();
                status_panel.set_preparing("Starting...", 0,
                                           static_cast<int>(pre_print_sequencer_->queue_size()));
            }

            // Start the sequence
            pre_print_sequencer_->start(
                // Progress callback - update the Preparing UI
                [self](const std::string& op_name, int step, int total, float progress) {
                    spdlog::info("[{}] Pre-print progress: {} ({}/{}, {:.0f}%)", self->get_name(),
                                 op_name, step, total, progress * 100.0f);

                    // Update PrintStatusPanel's preparing state
                    auto& status_panel = get_global_print_status_panel();
                    status_panel.set_preparing(op_name, step, total);
                    status_panel.set_preparing_progress(progress);
                },
                // Completion callback
                [self](bool success, const std::string& error) {
                    auto& status_panel = get_global_print_status_panel();

                    if (success) {
                        spdlog::info("[{}] Pre-print sequence complete, print started",
                                     self->get_name());
                        // Transition from Preparing → Printing state
                        status_panel.end_preparing(true);
                    } else {
                        NOTIFY_ERROR("Pre-print failed: {}", error);
                        LOG_ERROR_INTERNAL("[{}] Pre-print sequence failed: {}", self->get_name(),
                                           error);
                        // Transition from Preparing → Idle state
                        status_panel.end_preparing(false);
                    }
                    // Clean up sequencer
                    self->pre_print_sequencer_.reset();
                });
        } else {
            // No pre-print operations - start print directly
            api_->start_print(
                filename_to_print,
                // Success callback
                [self]() {
                    spdlog::info("[{}] Print started successfully", self->get_name());

                    if (self->print_status_panel_widget_) {
                        self->hide_detail_view();
                        ui_nav_push_overlay(self->print_status_panel_widget_);
                    } else {
                        spdlog::error("[{}] Print status panel not set", self->get_name());
                    }
                },
                // Error callback
                [self, filename_to_print](const MoonrakerError& error) {
                    NOTIFY_ERROR("Failed to start print: {}", error.message);
                    LOG_ERROR_INTERNAL("[{}] Print start failed for {}: {} ({})", self->get_name(),
                                       filename_to_print, error.message, error.get_type_string());
                });
        }
    } else {
        // Cannot start print without API connection
        spdlog::error("[{}] Cannot start print - not connected to printer", get_name());
        NOTIFY_ERROR("Cannot start print: not connected to printer");
    }
}

void PrintSelectPanel::delete_file() {
    std::string filename_to_delete(selected_filename_buffer_);
    auto* self = this;

    if (api_) {
        spdlog::info("[{}] Deleting file: {}", get_name(), filename_to_delete);

        api_->delete_file(
            filename_to_delete,
            // Success callback
            [self]() {
                spdlog::info("[{}] File deleted successfully", self->get_name());
                self->hide_delete_confirmation();
                self->hide_detail_view();
                self->refresh_files();
            },
            // Error callback
            [self](const MoonrakerError& error) {
                NOTIFY_ERROR("Failed to delete file");
                LOG_ERROR_INTERNAL("[{}] File delete error: {} ({})", self->get_name(),
                                   error.message, error.get_type_string());
                self->hide_delete_confirmation();
            });
    } else {
        NOTIFY_WARNING("Cannot delete file: printer not connected");
        hide_delete_confirmation();
    }
}

// ============================================================================
// Static Callbacks (trampolines)
// ============================================================================

void PrintSelectPanel::on_view_toggle_clicked_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->toggle_view();
    }
}

void PrintSelectPanel::on_header_clicked_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (self && target) {
        // Recover column from widget's user_data
        auto column = static_cast<PrintSelectSortColumn>(
            reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)));
        self->sort_by(column);
    }
}

void PrintSelectPanel::on_file_clicked_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (self && target) {
        size_t file_index = reinterpret_cast<size_t>(lv_obj_get_user_data(target));
        self->handle_file_click(file_index);
    }
}

void PrintSelectPanel::on_back_button_clicked_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->hide_detail_view();
    }
}

void PrintSelectPanel::on_delete_button_clicked_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->show_delete_confirmation();
    }
}

void PrintSelectPanel::on_print_button_clicked_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->start_print();
    }
}

void PrintSelectPanel::on_detail_backdrop_clicked_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto* current_target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    // Only close if clicking the backdrop itself, not child widgets
    if (self && target == current_target) {
        self->hide_detail_view();
    }
}

void PrintSelectPanel::on_confirm_delete_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->delete_file();
    }
}

void PrintSelectPanel::on_cancel_delete_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->hide_delete_confirmation();
    }
}

bool PrintSelectPanel::select_file_by_name(const std::string& filename) {
    // Search for the file in the current file list
    for (size_t i = 0; i < file_list_.size(); ++i) {
        const auto& file = file_list_[i];
        if (!file.is_dir && file.filename == filename) {
            // Found it - simulate a file click
            spdlog::info("[{}] Programmatically selecting file: {}", get_name(), filename);
            handle_file_click(i);
            return true;
        }
    }

    spdlog::warn("[{}] File not found for selection: {}", get_name(), filename);
    return false;
}

void PrintSelectPanel::set_pending_file_selection(const std::string& filename) {
    pending_file_selection_ = filename;
    spdlog::info("[{}] Set pending file selection: '{}'", get_name(), filename);
}

// ============================================================================
// G-code Operations Integration (Stage 7)
// ============================================================================

void PrintSelectPanel::scan_gcode_for_operations(const std::string& filename) {
    // Skip if already cached for this file
    if (cached_scan_filename_ == filename && cached_scan_result_.has_value()) {
        spdlog::debug("[{}] Using cached scan result for {}", get_name(), filename);
        return;
    }

    if (!api_) {
        spdlog::warn("[{}] Cannot scan G-code - no API connection", get_name());
        return;
    }

    // Build path for download
    std::string file_path = current_path_.empty() ? filename : current_path_ + "/" + filename;

    spdlog::info("[{}] Scanning G-code for embedded operations: {}", get_name(), file_path);

    auto* self = this;
    api_->download_file(
        "gcodes", file_path,
        // Success: parse content and cache result
        [self, filename](const std::string& content) {
            gcode::GCodeOpsDetector detector;
            self->cached_scan_result_ = detector.scan_content(content);
            self->cached_scan_filename_ = filename;

            if (self->cached_scan_result_->operations.empty()) {
                spdlog::debug("[{}] No embedded operations found in {}", self->get_name(), filename);
            } else {
                spdlog::info("[{}] Found {} embedded operations in {}:", self->get_name(),
                             self->cached_scan_result_->operations.size(), filename);
                for (const auto& op : self->cached_scan_result_->operations) {
                    spdlog::info("[{}]   - {} at line {} ({})", self->get_name(), op.display_name(),
                                 op.line_number, op.raw_line.substr(0, 50));
                }
            }
        },
        // Error: just log, don't block the UI
        [self, filename](const MoonrakerError& error) {
            spdlog::warn("[{}] Failed to scan G-code {}: {}", self->get_name(), filename,
                         error.message);
            self->cached_scan_result_.reset();
            self->cached_scan_filename_.clear();
        });
}

std::vector<gcode::OperationType> PrintSelectPanel::collect_ops_to_disable() const {
    std::vector<gcode::OperationType> ops_to_disable;

    if (!cached_scan_result_.has_value()) {
        return ops_to_disable; // No scan result, nothing to disable
    }

    // Helper to check if checkbox is visible and UNCHECKED
    auto is_option_disabled = [](lv_obj_t* checkbox) -> bool {
        if (!checkbox)
            return false;
        bool is_visible = !lv_obj_has_flag(checkbox, LV_OBJ_FLAG_HIDDEN);
        bool is_checked = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
        return is_visible && !is_checked; // Visible but NOT checked = disabled
    };

    // Check each operation type: if file has it embedded AND user disabled it
    if (is_option_disabled(bed_leveling_checkbox_) &&
        cached_scan_result_->has_operation(gcode::OperationType::BED_LEVELING)) {
        ops_to_disable.push_back(gcode::OperationType::BED_LEVELING);
        spdlog::debug("[{}] User disabled bed leveling, file has it embedded", get_name());
    }

    if (is_option_disabled(qgl_checkbox_) &&
        cached_scan_result_->has_operation(gcode::OperationType::QGL)) {
        ops_to_disable.push_back(gcode::OperationType::QGL);
        spdlog::debug("[{}] User disabled QGL, file has it embedded", get_name());
    }

    if (is_option_disabled(z_tilt_checkbox_) &&
        cached_scan_result_->has_operation(gcode::OperationType::Z_TILT)) {
        ops_to_disable.push_back(gcode::OperationType::Z_TILT);
        spdlog::debug("[{}] User disabled Z-tilt, file has it embedded", get_name());
    }

    if (is_option_disabled(nozzle_clean_checkbox_) &&
        cached_scan_result_->has_operation(gcode::OperationType::NOZZLE_CLEAN)) {
        ops_to_disable.push_back(gcode::OperationType::NOZZLE_CLEAN);
        spdlog::debug("[{}] User disabled nozzle clean, file has it embedded", get_name());
    }

    return ops_to_disable;
}

void PrintSelectPanel::modify_and_print(const std::string& original_filename,
                                         const std::vector<gcode::OperationType>& ops_to_disable) {
    if (!api_) {
        NOTIFY_ERROR("Cannot start print - not connected to printer");
        return;
    }

    if (!cached_scan_result_.has_value()) {
        spdlog::error("[{}] modify_and_print called without scan result", get_name());
        NOTIFY_ERROR("Internal error: no scan result");
        return;
    }

    // Build path for download
    std::string file_path =
        current_path_.empty() ? original_filename : current_path_ + "/" + original_filename;

    spdlog::info("[{}] Modifying G-code to disable {} operations", get_name(), ops_to_disable.size());

    auto* self = this;

    // Step 1: Download the original file
    api_->download_file(
        "gcodes", file_path,
        // Success: modify and upload
        [self, original_filename, ops_to_disable](const std::string& content) {
            // Step 2: Apply modifications
            gcode::GCodeFileModifier modifier;
            modifier.disable_operations(*self->cached_scan_result_, ops_to_disable);

            std::string modified_content = modifier.apply_to_content(content);
            if (modified_content.empty()) {
                NOTIFY_ERROR("Failed to modify G-code file");
                return;
            }

            // Step 3: Upload to .helix_temp directory with unique name
            // Using timestamp to avoid conflicts
            std::string temp_filename =
                ".helix_temp/modified_" + std::to_string(std::time(nullptr)) + "_" + original_filename;

            spdlog::info("[{}] Uploading modified G-code to {}", self->get_name(), temp_filename);

            self->api_->upload_file_with_name(
                "gcodes", temp_filename, temp_filename, modified_content,
                // Success: start print with modified file
                [self, temp_filename, original_filename]() {
                    spdlog::info("[{}] Modified file uploaded, starting print", self->get_name());

                    // Start print with the modified file
                    self->api_->start_print(
                        temp_filename,
                        [self, original_filename]() {
                            spdlog::info("[{}] Print started with modified G-code (original: {})",
                                         self->get_name(), original_filename);
                            if (self->print_status_panel_widget_) {
                                self->hide_detail_view();
                                ui_nav_push_overlay(self->print_status_panel_widget_);
                            }
                        },
                        [self, temp_filename](const MoonrakerError& error) {
                            NOTIFY_ERROR("Failed to start print: {}", error.message);
                            LOG_ERROR_INTERNAL("[{}] Print start failed for {}: {}",
                                               self->get_name(), temp_filename, error.message);
                        });
                },
                // Error uploading
                [self](const MoonrakerError& error) {
                    NOTIFY_ERROR("Failed to upload modified G-code: {}", error.message);
                    LOG_ERROR_INTERNAL("[{}] Upload failed: {}", self->get_name(), error.message);
                });
        },
        // Error downloading
        [self, original_filename](const MoonrakerError& error) {
            NOTIFY_ERROR("Failed to download G-code for modification: {}", error.message);
            LOG_ERROR_INTERNAL("[{}] Download failed for {}: {}", self->get_name(),
                               original_filename, error.message);
        });
}
