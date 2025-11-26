// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_print_select.h"

#include "ui_fonts.h"
#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_panel_print_status.h"
#include "ui_subject_registry.h"
#include "ui_theme.h"
#include "ui_utils.h"

#include "app_globals.h"
#include "config.h"
#include "lvgl/src/xml/lv_xml.h"
#include "moonraker_api.h"
#include "printer_state.h"
#include "ui_error_reporting.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

// Forward declaration for class-based API
PrintStatusPanel& get_global_print_status_panel();

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
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_thumbnail_subject_, selected_thumbnail_buffer_,
                                        DEFAULT_PLACEHOLDER_THUMB, "selected_thumbnail");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_print_time_subject_, selected_print_time_buffer_,
                                        "", "selected_print_time");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_filament_weight_subject_,
                                        selected_filament_weight_buffer_, "",
                                        "selected_filament_weight");

    // Initialize detail view visibility subject (0 = hidden, 1 = visible)
    UI_SUBJECT_INIT_AND_REGISTER_INT(detail_view_visible_subject_, 0, "detail_view_visible");

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
    static const HeaderBinding headers[] = {{"header_filename", PrintSelectSortColumn::FILENAME},
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
            lv_obj_set_user_data(header, reinterpret_cast<void*>(static_cast<intptr_t>(binding.column)));
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

    // Try to refresh from Moonraker, fall back to test data if not connected
    if (api_) {
        refresh_files();
    } else {
        spdlog::info("[{}] MoonrakerAPI not available, using test data", get_name());
        populate_test_data();
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
        lv_obj_add_flag(card_view_container_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(list_view_container_, LV_OBJ_FLAG_HIDDEN);

        // Update icon to show grid_view (indicates you can switch back to card view)
        const void* grid_icon = lv_xml_get_image(NULL, "mat_grid_view");
        if (grid_icon) {
            lv_image_set_src(view_toggle_icon_, grid_icon);
        }
        spdlog::debug("[{}] Switched to list view", get_name());
    } else {
        // Switch to card view
        current_view_mode_ = PrintSelectViewMode::CARD;
        lv_obj_remove_flag(card_view_container_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(list_view_container_, LV_OBJ_FLAG_HIDDEN);

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

    spdlog::info("[{}] Refreshing file list from Moonraker...", get_name());

    // Capture 'this' for async callbacks
    auto* self = this;

    // Request file list from gcodes directory (non-recursive for now)
    api_->list_files(
        "gcodes", "", false,
        // Success callback
        [self](const std::vector<FileInfo>& files) {
            spdlog::info("[{}] Received {} files from Moonraker", self->get_name(), files.size());

            // Clear existing file list
            self->file_list_.clear();

            // Convert FileInfo to PrintFileData
            for (const auto& file : files) {
                // Skip directories
                if (file.is_dir)
                    continue;

                // Only process .gcode files
                if (file.filename.find(".gcode") == std::string::npos &&
                    file.filename.find(".g") == std::string::npos) {
                    continue;
                }

                PrintFileData data;
                data.filename = file.filename;
                data.thumbnail_path = self->DEFAULT_PLACEHOLDER_THUMB;
                data.file_size_bytes = file.size;
                data.modified_timestamp = static_cast<time_t>(file.modified);
                data.print_time_minutes = 0;
                data.filament_grams = 0.0f;

                // Format strings (will be updated when metadata arrives)
                data.size_str = format_file_size(data.file_size_bytes);
                data.modified_str = format_modified_date(data.modified_timestamp);
                data.print_time_str = format_print_time(data.print_time_minutes);
                data.filament_str = format_filament_weight(data.filament_grams);

                self->file_list_.push_back(data);
            }

            // Show files immediately with placeholder metadata
            self->apply_sort();
            self->update_sort_indicators();
            self->populate_card_view();
            self->populate_list_view();
            self->update_empty_state();

            spdlog::info("[{}] File list updated with {} G-code files (fetching metadata...)",
                         self->get_name(), self->file_list_.size());

            // Now fetch metadata for each file asynchronously
            for (size_t i = 0; i < self->file_list_.size(); i++) {
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

                        // Update formatted strings
                        self->file_list_[i].print_time_str =
                            format_print_time(self->file_list_[i].print_time_minutes);
                        self->file_list_[i].filament_str =
                            format_filament_weight(self->file_list_[i].filament_grams);

                        spdlog::debug("[{}] Updated metadata for {}: {}min, {}g", self->get_name(),
                                      filename, self->file_list_[i].print_time_minutes,
                                      self->file_list_[i].filament_grams);

                        // Handle thumbnails if available
                        if (!metadata.thumbnails.empty()) {
                            std::string thumbnail_url =
                                construct_thumbnail_url(metadata.thumbnails[0]);
                            if (!thumbnail_url.empty()) {
                                spdlog::info("[{}] Thumbnail URL for {}: {}", self->get_name(),
                                             filename, thumbnail_url);
                                // TODO: Download thumbnail from URL to local file
                            }
                        }

                        // Re-render views to show updated metadata
                        self->populate_card_view();
                        self->populate_list_view();
                    },
                    // Metadata error callback
                    [self, filename](const MoonrakerError& error) {
                        spdlog::warn("[{}] Failed to get metadata for {}: {} ({})", self->get_name(),
                                     filename, error.message, error.get_type_string());
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

void PrintSelectPanel::populate_test_data() {
    // Clear existing file list
    file_list_.clear();

    // Generate test file data
    struct TestFile {
        const char* filename;
        size_t size_bytes;
        int days_ago;
        int print_time_mins;
        float filament_grams;
    };

    TestFile test_files[] = {
        {"Benchy.gcode", 1024 * 512, 1, 150, 45.0f},
        {"Calibration_Cube.gcode", 1024 * 128, 2, 45, 12.0f},
        {"Large_Vase_With_Very_Long_Filename_That_Should_Truncate.gcode", 1024 * 1024 * 2, 3, 30,
         8.0f},
        {"Gear_Assembly.gcode", 1024 * 768, 5, 150, 45.0f},
        {"Flower_Pot.gcode", 1024 * 1024, 7, 240, 85.0f},
        {"Keychain.gcode", 1024 * 64, 10, 480, 120.0f},
        {"Lithophane_Test.gcode", 1024 * 1024 * 5, 14, 5, 2.0f},
        {"Headphone_Stand.gcode", 1024 * 256, 20, 15, 4.5f},
    };

    time_t now = time(nullptr);
    for (const auto& file : test_files) {
        PrintFileData data;
        data.filename = file.filename;
        data.thumbnail_path = DEFAULT_PLACEHOLDER_THUMB;
        data.file_size_bytes = file.size_bytes;
        data.modified_timestamp = now - (file.days_ago * 86400);
        data.print_time_minutes = file.print_time_mins;
        data.filament_grams = file.filament_grams;

        // Format strings
        data.size_str = format_file_size(data.file_size_bytes);
        data.modified_str = format_modified_date(data.modified_timestamp);
        data.print_time_str = format_print_time(data.print_time_minutes);
        data.filament_str = format_filament_weight(data.filament_grams);

        file_list_.push_back(data);
    }

    // Apply initial sort and populate views
    apply_sort();
    update_sort_indicators();
    populate_card_view();
    populate_list_view();
    update_empty_state();

    spdlog::debug("[{}] Populated with {} test files", get_name(), file_list_.size());
}

void PrintSelectPanel::set_selected_file(const char* filename, const char* thumbnail_src,
                                          const char* print_time, const char* filament_weight) {
    lv_subject_copy_string(&selected_filename_subject_, filename);
    lv_subject_copy_string(&selected_thumbnail_subject_, thumbnail_src);
    lv_subject_copy_string(&selected_print_time_subject_, print_time);
    lv_subject_copy_string(&selected_filament_weight_subject_, filament_weight);

    spdlog::info("[{}] Selected file: {}", get_name(), filename);
}

void PrintSelectPanel::show_detail_view() {
    if (detail_view_widget_) {
        lv_obj_remove_flag(detail_view_widget_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(detail_view_widget_);
        lv_subject_set_int(&detail_view_visible_subject_, 1);

        // Scale images after showing (layout must be calculated first)
        scale_detail_images();
    }
}

void PrintSelectPanel::hide_detail_view() {
    if (detail_view_widget_) {
        lv_obj_add_flag(detail_view_widget_, LV_OBJ_FLAG_HIDDEN);
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

std::string PrintSelectPanel::construct_thumbnail_url(const std::string& relative_path) {
    Config* config = Config::get_instance();
    if (!config) {
        spdlog::error("Cannot construct thumbnail URL: Config not available");
        return "";
    }

    try {
        std::string host = config->get<std::string>(config->df() + "moonraker_host");
        int port = config->get<int>(config->df() + "moonraker_port");
        return "http://" + host + ":" + std::to_string(port) + "/server/files/gcodes/" +
               relative_path;
    } catch (const std::exception& e) {
        spdlog::error("Failed to construct thumbnail URL: {}", e.what());
        return "";
    }
}

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

        const char* attrs[] = {"thumbnail_src",   file.thumbnail_path.c_str(),
                               "filename",        file.filename.c_str(),
                               "print_time",      file.print_time_str.c_str(),
                               "filament_weight", file.filament_str.c_str(),
                               NULL};

        lv_obj_t* card = static_cast<lv_obj_t*>(lv_xml_create(card_view_container_, CARD_COMPONENT_NAME, attrs));

        if (card) {
            lv_obj_set_width(card, dims.card_width);
            lv_obj_set_height(card, dims.card_height);
            lv_obj_set_style_flex_grow(card, 0, LV_PART_MAIN);

            // Calculate proper filename label height
            lv_obj_t* filename_label = lv_obj_find_by_name(card, "filename_label");
            if (filename_label) {
                const lv_font_t* font = lv_obj_get_style_text_font(filename_label, LV_PART_MAIN);
                if (font) {
                    lv_coord_t line_height = lv_font_get_line_height(font);
                    lv_obj_set_height(filename_label, line_height);
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

        const char* attrs[] = {"filename",      file.filename.c_str(),
                               "file_size",     file.size_str.c_str(),
                               "modified_date", file.modified_str.c_str(),
                               "print_time",    file.print_time_str.c_str(),
                               NULL};

        lv_obj_t* row = static_cast<lv_obj_t*>(lv_xml_create(list_rows_container_, "print_file_list_row", attrs));

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

    detail_view_widget_ = static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "print_file_detail", nullptr));

    if (!detail_view_widget_) {
        LOG_ERROR_INTERNAL("[{}] Failed to create detail view from XML", get_name());
        NOTIFY_ERROR("Failed to load file details");
        return;
    }

    // Calculate width to fill remaining space after navigation bar
    lv_coord_t screen_width = lv_obj_get_width(parent_screen_);
    lv_coord_t nav_width = UI_NAV_WIDTH(screen_width);
    lv_obj_set_width(detail_view_widget_, screen_width - nav_width);

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

    spdlog::debug("[{}] Detail view created", get_name());
}

void PrintSelectPanel::scale_detail_images() {
    if (!detail_view_widget_)
        return;

    lv_obj_t* thumbnail_section = lv_obj_find_by_name(detail_view_widget_, "thumbnail_section");
    if (!thumbnail_section) {
        spdlog::warn("[{}] Thumbnail section not found in detail view", get_name());
        return;
    }

    lv_obj_update_layout(thumbnail_section);

    lv_coord_t section_width = lv_obj_get_content_width(thumbnail_section);
    lv_coord_t section_height = lv_obj_get_content_height(thumbnail_section);

    spdlog::debug("[{}] Detail view thumbnail section: {}x{}", get_name(), section_width,
                  section_height);

    lv_obj_t* gradient_bg = lv_obj_find_by_name(detail_view_widget_, "gradient_background");
    if (gradient_bg) {
        ui_image_scale_to_cover(gradient_bg, section_width, section_height);
    }

    lv_obj_t* thumbnail = lv_obj_find_by_name(detail_view_widget_, "detail_thumbnail");
    if (thumbnail) {
        ui_image_scale_to_contain(thumbnail, section_width, section_height, LV_IMAGE_ALIGN_TOP_MID);
    }
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
    set_selected_file(file.filename.c_str(), file.thumbnail_path.c_str(),
                      file.print_time_str.c_str(), file.filament_str.c_str());
    show_detail_view();
}

void PrintSelectPanel::start_print() {
    std::string filename_to_print(selected_filename_buffer_);
    auto* self = this;

    if (api_) {
        spdlog::info("[{}] Starting print: {}", get_name(), filename_to_print);

        api_->start_print(
            filename_to_print,
            // Success callback
            [self]() {
                spdlog::info("[{}] Print started successfully", self->get_name());

                if (self->print_status_panel_widget_) {
                    self->hide_detail_view();
                    ui_nav_push_overlay(self->print_status_panel_widget_);

                    if (self->panel_) {
                        lv_obj_add_flag(self->panel_, LV_OBJ_FLAG_HIDDEN);
                    }

                    lv_obj_remove_flag(self->print_status_panel_widget_, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_move_foreground(self->print_status_panel_widget_);
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
    } else {
        // Fall back to mock print
        spdlog::warn("[{}] MoonrakerAPI not available - using mock print", get_name());

        if (print_status_panel_widget_) {
            hide_detail_view();
            ui_nav_push_overlay(print_status_panel_widget_);

            if (panel_) {
                lv_obj_add_flag(panel_, LV_OBJ_FLAG_HIDDEN);
            }

            lv_obj_remove_flag(print_status_panel_widget_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(print_status_panel_widget_);

            get_global_print_status_panel().start_mock_print(selected_filename_buffer_, 250, 10800);

            spdlog::info("[{}] Started mock print for: {}", get_name(), selected_filename_buffer_);
        }
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
        auto column =
            static_cast<PrintSelectSortColumn>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)));
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
