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

#include <spdlog/spdlog.h>

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

// Default placeholder thumbnail for print files
static const char* DEFAULT_PLACEHOLDER_THUMB = "A:assets/images/thumbnail-placeholder.png";

// ============================================================================
// Thumbnail helper functions
// ============================================================================

/**
 * @brief Construct Moonraker HTTP URL for thumbnail
 *
 * @param relative_path Relative path from metadata (e.g., ".thumbs/...")
 * @return Full HTTP URL or empty string on error
 */
static std::string construct_thumbnail_url(const std::string& relative_path) {
    Config* config = Config::get_instance();
    if (!config) {
        spdlog::error("Cannot construct thumbnail URL: Config not available");
        return "";
    }

    try {
        std::string host = config->get<std::string>(config->df() + "moonraker_host");
        int port = config->get<int>(config->df() + "moonraker_port");

        // Build URL: http://host:port/server/files/gcodes/{relative_path}
        std::string url =
            "http://" + host + ":" + std::to_string(port) + "/server/files/gcodes/" + relative_path;

        return url;
    } catch (const std::exception& e) {
        spdlog::error("Failed to construct thumbnail URL: {}", e.what());
        return "";
    }
}

// ============================================================================
// File data structure
// ============================================================================
struct PrintFileData {
    std::string filename;
    std::string thumbnail_path;
    size_t file_size_bytes;    // File size in bytes
    time_t modified_timestamp; // Last modified timestamp
    int print_time_minutes;    // Print time in minutes
    float filament_grams;      // Filament weight in grams

    // Formatted strings (cached for performance)
    std::string size_str;
    std::string modified_str;
    std::string print_time_str;
    std::string filament_str;
};

// ============================================================================
// Card layout calculation
// ============================================================================
static const char* CARD_COMPONENT_NAME = "print_file_card";
static const int CARD_GAP = 20;
static const int CARD_MIN_WIDTH = 165; // Increased to prevent metadata wrapping
static const int CARD_MAX_WIDTH = 220;
static const int CARD_DEFAULT_HEIGHT = 245; // Default height (overridden by dynamic calculation)

// Row count breakpoint: minimum container height for 3 rows vs 2 rows
static const int ROW_COUNT_3_MIN_HEIGHT = 520;

struct CardDimensions {
    int num_columns;
    int num_rows;
    int card_width;
    int card_height;
};

static CardDimensions calculate_card_dimensions(lv_obj_t* container) {
    if (!container) {
        spdlog::error("Cannot calculate dimensions: container is null");
        CardDimensions dims = {4, 2, CARD_MIN_WIDTH, CARD_DEFAULT_HEIGHT};
        return dims;
    }

    // Get container width
    lv_coord_t container_width = lv_obj_get_content_width(container);

    // Calculate available height from parent panel dimensions
    lv_obj_t* panel_root = lv_obj_get_parent(container);
    if (!panel_root) {
        spdlog::error("Cannot find panel root");
        CardDimensions dims = {4, 2, CARD_MIN_WIDTH, CARD_DEFAULT_HEIGHT};
        return dims;
    }

    // Get actual panel height
    lv_coord_t panel_height = lv_obj_get_height(panel_root);

    // Find the top bar to subtract its height
    lv_obj_t* top_bar = lv_obj_get_child(panel_root, 0); // First child is top bar
    lv_coord_t top_bar_height = top_bar ? lv_obj_get_height(top_bar) : 60;

    // Check for gap between panel children in flex layout
    lv_coord_t panel_gap = lv_obj_get_style_pad_row(panel_root, LV_PART_MAIN);

    // Calculate available height for cards (panel minus top bar minus container padding minus any
    // gaps)
    lv_coord_t container_padding = lv_obj_get_style_pad_top(container, LV_PART_MAIN) +
                                   lv_obj_get_style_pad_bottom(container, LV_PART_MAIN);
    lv_coord_t container_actual_height = lv_obj_get_height(container);
    lv_coord_t available_height = panel_height - top_bar_height - container_padding - panel_gap;

    spdlog::info("Heights: panel={}, top_bar={}, container_actual={}, container_padding={}, "
                 "panel_gap={}, available={}",
                 panel_height, top_bar_height, container_actual_height, container_padding,
                 panel_gap, available_height);

    CardDimensions dims;

    // Determine optimal number of rows based on available height
    if (available_height >= ROW_COUNT_3_MIN_HEIGHT) {
        dims.num_rows = 3;
    } else {
        dims.num_rows = 2;
    }

    // Calculate card height based on rows
    int total_row_gaps = (dims.num_rows - 1) * CARD_GAP;
    dims.card_height = (available_height - total_row_gaps) / dims.num_rows;

    // Try different column counts, starting from maximum
    for (int cols = 10; cols >= 1; cols--) {
        int total_gaps = (cols - 1) * CARD_GAP;
        int card_width = (container_width - total_gaps) / cols;

        if (card_width >= CARD_MIN_WIDTH && card_width <= CARD_MAX_WIDTH) {
            dims.num_columns = cols;
            dims.card_width = card_width;

            spdlog::info("Calculated card layout: {} rows × {} columns, card={}x{}", dims.num_rows,
                         dims.num_columns, dims.card_width, dims.card_height);
            return dims;
        }
    }

    // Fallback to minimum width if nothing fits perfectly
    dims.num_columns = container_width / (CARD_MIN_WIDTH + CARD_GAP);
    if (dims.num_columns < 1)
        dims.num_columns = 1; // Safety: always at least 1 column
    dims.card_width = CARD_MIN_WIDTH;

    spdlog::warn("No optimal card layout found, using fallback: {} columns", dims.num_columns);
    return dims;
}

// ============================================================================
// Static state
// ============================================================================
static std::vector<PrintFileData> file_list;
static PrintSelectViewMode current_view_mode = PrintSelectViewMode::CARD; // Default view mode
static PrintSelectSortColumn current_sort_column = PrintSelectSortColumn::FILENAME;
static PrintSelectSortDirection current_sort_direction = PrintSelectSortDirection::ASCENDING;
static bool panel_initialized =
    false; // Guard flag to prevent resize callback before setup complete

// Widget references
static lv_obj_t* panel_root_widget = nullptr;
static lv_obj_t* parent_screen_widget = nullptr;
static lv_obj_t* card_view_container = nullptr;
static lv_obj_t* list_view_container = nullptr;
static lv_obj_t* list_rows_container = nullptr;
static lv_obj_t* empty_state_container = nullptr;
static lv_obj_t* view_toggle_btn = nullptr;
static lv_obj_t* view_toggle_icon = nullptr;
static lv_obj_t* detail_view_widget = nullptr;
static lv_obj_t* confirmation_dialog_widget = nullptr;
static lv_obj_t* print_status_panel_widget = nullptr;

// ============================================================================
// Reactive subjects for selected file state
// ============================================================================
static lv_subject_t selected_filename_subject;
static char selected_filename_buffer[128];

static lv_subject_t selected_thumbnail_subject;
static char selected_thumbnail_buffer[256];

static lv_subject_t selected_print_time_subject;
static char selected_print_time_buffer[32];

static lv_subject_t selected_filament_weight_subject;
static char selected_filament_weight_buffer[32];

static lv_subject_t detail_view_visible_subject;

// Forward declarations
static void populate_card_view();
static void populate_list_view();
static void apply_sort();
static void update_empty_state();
static void update_sort_indicators();
static void attach_card_click_handler(lv_obj_t* card, const PrintFileData& file_data);
static void attach_row_click_handler(lv_obj_t* row, const PrintFileData& file_data);
static void create_detail_view();
static void scale_detail_images();

// ============================================================================
// Resize handling callback
// ============================================================================
static void on_resize() {
    // CRITICAL: Don't run resize logic until panel is fully initialized (prevents segfault)
    if (!panel_initialized)
        return;

    spdlog::info("Print select panel handling resize event");

    // Only recalculate card view dimensions if currently in card view mode
    if (current_view_mode == PrintSelectViewMode::CARD && card_view_container) {
        populate_card_view();
    }

    // Update detail view content padding if detail view exists
    if (detail_view_widget && parent_screen_widget) {
        lv_obj_t* content_container = lv_obj_find_by_name(detail_view_widget, "content_container");
        if (content_container) {
            lv_coord_t padding =
                ui_get_header_content_padding(lv_obj_get_height(parent_screen_widget));
            lv_obj_set_style_pad_all(content_container, padding, 0);
        }
    }
}

// ============================================================================
// Subject initialization
// ============================================================================
void ui_panel_print_select_init_subjects() {
    // Initialize selected file subjects
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_filename_subject, selected_filename_buffer, "", "selected_filename");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_thumbnail_subject, selected_thumbnail_buffer, DEFAULT_PLACEHOLDER_THUMB, "selected_thumbnail");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_print_time_subject, selected_print_time_buffer, "", "selected_print_time");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_filament_weight_subject, selected_filament_weight_buffer, "", "selected_filament_weight");

    // Initialize detail view visibility subject (0 = hidden, 1 = visible)
    UI_SUBJECT_INIT_AND_REGISTER_INT(detail_view_visible_subject, 0, "detail_view_visible");

    spdlog::info("Print select panel subjects initialized");
}

// ============================================================================
// Panel setup (call after XML creation)
// ============================================================================
void ui_panel_print_select_setup(lv_obj_t* panel_root, lv_obj_t* parent_screen) {
    if (!panel_root) {
        spdlog::error("Cannot setup print select panel: panel_root is null");
        return;
    }

    panel_root_widget = panel_root;
    parent_screen_widget = parent_screen;

    // Find widget references
    card_view_container = lv_obj_find_by_name(panel_root, "card_view_container");
    list_view_container = lv_obj_find_by_name(panel_root, "list_view_container");
    list_rows_container = lv_obj_find_by_name(panel_root, "list_rows_container");
    empty_state_container = lv_obj_find_by_name(panel_root, "empty_state_container");
    view_toggle_btn = lv_obj_find_by_name(panel_root, "view_toggle_btn");
    view_toggle_icon = lv_obj_find_by_name(panel_root, "view_toggle_icon");

    if (!card_view_container || !list_view_container || !list_rows_container ||
        !empty_state_container || !view_toggle_btn || !view_toggle_icon) {
        spdlog::error("Failed to find required widgets in print select panel");
        return;
    }

    // Wire up view toggle button
    lv_obj_add_event_cb(
        view_toggle_btn,
        [](lv_event_t* e) {
            (void)e;
            ui_panel_print_select_toggle_view();
        },
        LV_EVENT_CLICKED, nullptr);

    // Wire up column header click handlers
    static const char* header_names[] = {"header_filename", "header_size", "header_modified",
                                         "header_print_time"};
    // IMPORTANT: Static storage so pointers remain valid after function returns
    static PrintSelectSortColumn columns[] = {
        PrintSelectSortColumn::FILENAME, PrintSelectSortColumn::SIZE,
        PrintSelectSortColumn::MODIFIED, PrintSelectSortColumn::PRINT_TIME};

    for (int i = 0; i < 4; i++) {
        lv_obj_t* header = lv_obj_find_by_name(panel_root, header_names[i]);
        if (header) {
            lv_obj_add_event_cb(
                header,
                [](lv_event_t* e) {
                    PrintSelectSortColumn* col = (PrintSelectSortColumn*)lv_event_get_user_data(e);
                    ui_panel_print_select_sort_by(*col);
                },
                LV_EVENT_CLICKED, &columns[i]);
        }
    }

    // Create detail view (confirmation dialog created on-demand)
    create_detail_view();

    // Register resize callback for responsive card layout
    ui_resize_handler_register(on_resize);

    // Mark panel as fully initialized (enables resize callbacks)
    panel_initialized = true;

    // Try to refresh from Moonraker, fall back to test data if not connected
    MoonrakerAPI* api = get_moonraker_api();
    if (api) {
        // Refresh real files from Moonraker
        ui_panel_print_select_refresh_files();
    } else {
        // Fall back to test data if Moonraker not available
        spdlog::info("MoonrakerAPI not available, using test data");
        ui_panel_print_select_populate_test_data(panel_root);
    }

    spdlog::info("Print select panel setup complete");
}

// ============================================================================
// View toggle
// ============================================================================
void ui_panel_print_select_toggle_view() {
    if (current_view_mode == PrintSelectViewMode::CARD) {
        // Switch to list view
        current_view_mode = PrintSelectViewMode::LIST;
        lv_obj_add_flag(card_view_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(list_view_container, LV_OBJ_FLAG_HIDDEN);

        // Update icon to show grid_view (indicates you can switch back to card view)
        const void* grid_icon = lv_xml_get_image(NULL, "mat_grid_view");
        if (grid_icon) {
            lv_image_set_src(view_toggle_icon, grid_icon);
        }
        spdlog::debug("Switched to list view");
    } else {
        // Switch to card view
        current_view_mode = PrintSelectViewMode::CARD;
        lv_obj_remove_flag(card_view_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(list_view_container, LV_OBJ_FLAG_HIDDEN);

        // Update icon to show list (indicates you can switch to list view)
        const void* list_icon = lv_xml_get_image(NULL, "mat_list");
        if (list_icon) {
            lv_image_set_src(view_toggle_icon, list_icon);
        }
        spdlog::debug("Switched to card view");
    }

    update_empty_state();
}

// ============================================================================
// Sorting
// ============================================================================
static void apply_sort() {
    std::sort(
        file_list.begin(), file_list.end(), [](const PrintFileData& a, const PrintFileData& b) {
            bool result = false;

            switch (current_sort_column) {
            case PrintSelectSortColumn::FILENAME:
                result = a.filename < b.filename;
                break;
            case PrintSelectSortColumn::SIZE:
                result = a.file_size_bytes < b.file_size_bytes;
                break;
            case PrintSelectSortColumn::MODIFIED:
                result = a.modified_timestamp > b.modified_timestamp; // Newer first by default
                break;
            case PrintSelectSortColumn::PRINT_TIME:
                result = a.print_time_minutes < b.print_time_minutes;
                break;
            case PrintSelectSortColumn::FILAMENT:
                result = a.filament_grams < b.filament_grams;
                break;
            }

            // Reverse if descending
            if (current_sort_direction == PrintSelectSortDirection::DESCENDING) {
                result = !result;
            }

            return result;
        });
}

void ui_panel_print_select_sort_by(PrintSelectSortColumn column) {
    // Toggle direction if same column, otherwise default to ascending
    if (column == current_sort_column) {
        current_sort_direction = (current_sort_direction == PrintSelectSortDirection::ASCENDING)
                                     ? PrintSelectSortDirection::DESCENDING
                                     : PrintSelectSortDirection::ASCENDING;
    } else {
        current_sort_column = column;
        current_sort_direction = PrintSelectSortDirection::ASCENDING;
    }

    apply_sort();
    update_sort_indicators();

    // Repopulate current view
    if (current_view_mode == PrintSelectViewMode::CARD) {
        populate_card_view();
    } else {
        populate_list_view();
    }

    spdlog::debug("Sorted by column {}, direction {}", (int)column, (int)current_sort_direction);
}

static void update_sort_indicators() {
    // Update column header icons to show sort direction
    const char* header_names[] = {"header_filename", "header_size", "header_modified",
                                  "header_print_time"};
    PrintSelectSortColumn columns[] = {PrintSelectSortColumn::FILENAME, PrintSelectSortColumn::SIZE,
                                       PrintSelectSortColumn::MODIFIED,
                                       PrintSelectSortColumn::PRINT_TIME};

    for (int i = 0; i < 4; i++) {
        // Find both up and down icon components
        char icon_up_name[64];
        char icon_down_name[64];
        snprintf(icon_up_name, sizeof(icon_up_name), "%s_icon_up", header_names[i]);
        snprintf(icon_down_name, sizeof(icon_down_name), "%s_icon_down", header_names[i]);

        lv_obj_t* icon_up = lv_obj_find_by_name(panel_root_widget, icon_up_name);
        lv_obj_t* icon_down = lv_obj_find_by_name(panel_root_widget, icon_down_name);

        if (icon_up && icon_down) {
            if (columns[i] == current_sort_column) {
                // Show appropriate arrow icon, hide the other
                if (current_sort_direction == PrintSelectSortDirection::ASCENDING) {
                    lv_obj_remove_flag(icon_up, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(icon_down, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(icon_up, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(icon_down, LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                // Hide both icons for non-sorted columns
                lv_obj_add_flag(icon_up, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(icon_down, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

// ============================================================================
// Refresh files from Moonraker
// ============================================================================
void ui_panel_print_select_refresh_files() {
    MoonrakerAPI* api = get_moonraker_api();
    if (!api) {
        spdlog::warn("Cannot refresh files: MoonrakerAPI not initialized");
        return;
    }

    spdlog::info("Refreshing file list from Moonraker...");

    // Request file list from gcodes directory (non-recursive for now)
    api->list_files(
        "gcodes", "", false,
        // Success callback
        [api](const std::vector<FileInfo>& files) {
            spdlog::info("Received {} files from Moonraker", files.size());

            // Clear existing file list
            file_list.clear();

            // Convert FileInfo to PrintFileData (with placeholder values initially)
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
                data.thumbnail_path = DEFAULT_PLACEHOLDER_THUMB;
                data.file_size_bytes = file.size;
                data.modified_timestamp = static_cast<time_t>(file.modified);
                data.print_time_minutes = 0;
                data.filament_grams = 0.0f;

                // Format strings (will be updated when metadata arrives)
                data.size_str = format_file_size(data.file_size_bytes);
                data.modified_str = format_modified_date(data.modified_timestamp);
                data.print_time_str = format_print_time(data.print_time_minutes);
                data.filament_str = format_filament_weight(data.filament_grams);

                file_list.push_back(data);
            }

            // Show files immediately with placeholder metadata
            apply_sort();
            update_sort_indicators();
            populate_card_view();
            populate_list_view();
            update_empty_state();

            spdlog::info("File list updated with {} G-code files (fetching metadata...)",
                         file_list.size());

            // Now fetch metadata for each file asynchronously
            for (size_t i = 0; i < file_list.size(); i++) {
                const std::string filename = file_list[i].filename;

                api->get_file_metadata(
                    filename,
                    // Metadata success callback
                    [i, filename](const FileMetadata& metadata) {
                        // Bounds check (file_list could change during async operation)
                        if (i >= file_list.size() || file_list[i].filename != filename) {
                            spdlog::warn("File list changed during metadata fetch for {}",
                                         filename);
                            return;
                        }

                        // Update metadata fields
                        file_list[i].print_time_minutes =
                            static_cast<int>(metadata.estimated_time / 60.0);
                        file_list[i].filament_grams =
                            static_cast<float>(metadata.filament_weight_total);

                        // Update formatted strings
                        file_list[i].print_time_str =
                            format_print_time(file_list[i].print_time_minutes);
                        file_list[i].filament_str =
                            format_filament_weight(file_list[i].filament_grams);

                        spdlog::debug("Updated metadata for {}: {}min, {}g", filename,
                                      file_list[i].print_time_minutes, file_list[i].filament_grams);

                        // Handle thumbnails if available
                        if (!metadata.thumbnails.empty()) {
                            // Use the first (typically largest) thumbnail
                            std::string thumbnail_url =
                                construct_thumbnail_url(metadata.thumbnails[0]);

                            if (!thumbnail_url.empty()) {
                                spdlog::info("Thumbnail URL for {}: {}", filename, thumbnail_url);

                                // TODO: Download thumbnail from URL to local file
                                // For now, keep using placeholder
                                // Future enhancement: Download to /tmp/helix_thumbs/ and update
                                // file_list[i].thumbnail_path
                            }
                        }

                        // Re-render views to show updated metadata
                        populate_card_view();
                        populate_list_view();
                    },
                    // Metadata error callback
                    [filename](const MoonrakerError& error) {
                        spdlog::warn("Failed to get metadata for {}: {} ({})", filename,
                                     error.message, error.get_type_string());
                        // Keep placeholder values, no need to do anything
                    });
            }
        },
        // Error callback
        [](const MoonrakerError& error) {
            spdlog::error("Failed to refresh file list: {} ({})", error.message,
                          error.get_type_string());

            // Show error to user (you might want to display this in UI)
            // For now, just log it
        });
}

// ============================================================================
// Populate views
// ============================================================================
void ui_panel_print_select_populate_test_data(lv_obj_t* panel_root) {
    if (!panel_root) {
        spdlog::error("Cannot populate: panel_root is null");
        return;
    }

    // Clear existing file list
    file_list.clear();

    // Generate test file data
    struct TestFile {
        const char* filename;
        size_t size_bytes;
        int days_ago; // For timestamp calculation
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
        data.modified_timestamp = now - (file.days_ago * 86400); // 86400 seconds per day
        data.print_time_minutes = file.print_time_mins;
        data.filament_grams = file.filament_grams;

        // Format strings
        data.size_str = format_file_size(data.file_size_bytes);
        data.modified_str = format_modified_date(data.modified_timestamp);
        data.print_time_str = format_print_time(data.print_time_minutes);
        data.filament_str = format_filament_weight(data.filament_grams);

        file_list.push_back(data);
    }

    // Apply initial sort
    apply_sort();
    update_sort_indicators();

    // Populate both views
    populate_card_view();
    populate_list_view();

    // Update empty state
    update_empty_state();

    spdlog::info("Populated print select panel with {} test files", (int)file_list.size());
}

static void populate_card_view() {
    if (!card_view_container)
        return;

    // Clear existing cards
    lv_obj_clean(card_view_container);

    // CRITICAL: Force layout calculation before querying dimensions (prevents segfault on tiny
    // screens)
    lv_obj_update_layout(card_view_container);

    // Calculate optimal card dimensions based on actual container width
    CardDimensions dims = calculate_card_dimensions(card_view_container);

    // Update container gap
    lv_obj_set_style_pad_gap(card_view_container, CARD_GAP, LV_PART_MAIN);

    for (const auto& file : file_list) {
        // Create XML attributes array
        const char* attrs[] = {"thumbnail_src",
                               file.thumbnail_path.c_str(),
                               "filename",
                               file.filename.c_str(),
                               "print_time",
                               file.print_time_str.c_str(),
                               "filament_weight",
                               file.filament_str.c_str(),
                               NULL};

        // Create card component
        lv_obj_t* card = (lv_obj_t*)lv_xml_create(card_view_container, CARD_COMPONENT_NAME, attrs);

        if (card) {
            // Override card dimensions to calculated optimal size
            lv_obj_set_width(card, dims.card_width);
            lv_obj_set_height(card, dims.card_height);
            lv_obj_set_style_flex_grow(card, 0, LV_PART_MAIN); // Disable flex_grow

            // Calculate proper filename label height based on font
            lv_obj_t* filename_label = lv_obj_find_by_name(card, "filename_label");
            if (filename_label) {
                const lv_font_t* font = lv_obj_get_style_text_font(filename_label, LV_PART_MAIN);
                if (font) {
                    lv_coord_t line_height = lv_font_get_line_height(font);
                    lv_obj_set_height(filename_label, line_height);
                }
            }

            // Scale gradient background to cover card
            lv_obj_t* gradient_bg = lv_obj_find_by_name(card, "gradient_background");
            if (gradient_bg) {
                lv_image_header_t header;
                lv_result_t res = lv_image_decoder_get_info(lv_image_get_src(gradient_bg), &header);

                if (res == LV_RESULT_OK && header.w > 0 && header.h > 0) {
                    // Calculate scale to cover the card
                    float scale_w = (float)dims.card_width / header.w;
                    float scale_h = (float)dims.card_height / header.h;
                    float scale =
                        (scale_w > scale_h) ? scale_w : scale_h; // Use larger scale to cover

                    uint16_t zoom = (uint16_t)(scale * 256);
                    lv_image_set_scale(gradient_bg, zoom);
                    lv_image_set_inner_align(gradient_bg, LV_IMAGE_ALIGN_CENTER);
                }
            }

            // Scale thumbnail to fill card (cover fit - may crop)
            lv_obj_t* thumbnail = lv_obj_find_by_name(card, "thumbnail");
            if (thumbnail) {
                // Get the source image dimensions
                lv_image_header_t header;
                lv_result_t res = lv_image_decoder_get_info(lv_image_get_src(thumbnail), &header);

                if (res == LV_RESULT_OK && header.w > 0 && header.h > 0) {
                    // Calculate scale to cover the card (like CSS object-fit: cover)
                    float scale_w = (float)dims.card_width / header.w;
                    float scale_h = (float)dims.card_height / header.h;
                    float scale =
                        (scale_w > scale_h) ? scale_w : scale_h; // Use larger scale to cover

                    uint16_t zoom = (uint16_t)(scale * 256);
                    lv_image_set_scale(thumbnail, zoom);
                    lv_image_set_inner_align(thumbnail, LV_IMAGE_ALIGN_CENTER);

                    int img_w = header.w, img_h = header.h; // Copy bitfields for formatting
                    spdlog::debug("Thumbnail scale: img={}x{}, card={}x{}, zoom={} ({:.1f}%)",
                                  img_w, img_h, dims.card_width, dims.card_height, zoom,
                                  scale * 100);
                }
            }

            // Attach click handler
            attach_card_click_handler(card, file);
        }
    }
}

static void populate_list_view() {
    if (!list_rows_container)
        return;

    // Clear existing rows
    lv_obj_clean(list_rows_container);

    for (const auto& file : file_list) {
        // Create XML attributes array
        const char* attrs[] = {"filename",
                               file.filename.c_str(),
                               "file_size",
                               file.size_str.c_str(),
                               "modified_date",
                               file.modified_str.c_str(),
                               "print_time",
                               file.print_time_str.c_str(),
                               NULL};

        // Create list row from XML component
        lv_obj_t* row = (lv_obj_t*)lv_xml_create(list_rows_container, "print_file_list_row", attrs);

        // Attach click handler
        if (row) {
            attach_row_click_handler(row, file);
        }
    }
}

static void update_empty_state() {
    if (!empty_state_container)
        return;

    bool is_empty = file_list.empty();

    if (is_empty) {
        // Show empty state, hide views
        lv_obj_remove_flag(empty_state_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(card_view_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(list_view_container, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Hide empty state, show current view
        lv_obj_add_flag(empty_state_container, LV_OBJ_FLAG_HIDDEN);

        if (current_view_mode == PrintSelectViewMode::CARD) {
            lv_obj_remove_flag(card_view_container, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(list_view_container, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(card_view_container, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(list_view_container, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ============================================================================
// Detail view management
// ============================================================================
void ui_panel_print_select_set_file(const char* filename, const char* thumbnail_src,
                                    const char* print_time, const char* filament_weight) {
    lv_subject_copy_string(&selected_filename_subject, filename);
    lv_subject_copy_string(&selected_thumbnail_subject, thumbnail_src);
    lv_subject_copy_string(&selected_print_time_subject, print_time);
    lv_subject_copy_string(&selected_filament_weight_subject, filament_weight);

    spdlog::info("Selected file: %s", filename);
}

// ============================================================================
// Detail view image scaling helper
// ============================================================================
static void scale_detail_images() {
    if (!detail_view_widget)
        return;

    // Find thumbnail section to get target dimensions
    lv_obj_t* thumbnail_section = lv_obj_find_by_name(detail_view_widget, "thumbnail_section");
    if (!thumbnail_section) {
        spdlog::warn("Thumbnail section not found in detail view, cannot scale images");
        return;
    }

    // Force layout update to get accurate dimensions (CRITICAL for flex layouts)
    lv_obj_update_layout(thumbnail_section);

    lv_coord_t section_width = lv_obj_get_content_width(thumbnail_section);
    lv_coord_t section_height = lv_obj_get_content_height(thumbnail_section);

    spdlog::debug("Detail view thumbnail section: %dx%d", section_width, section_height);

    // Scale gradient background to cover the entire section
    lv_obj_t* gradient_bg = lv_obj_find_by_name(detail_view_widget, "gradient_background");
    if (gradient_bg) {
        ui_image_scale_to_cover(gradient_bg, section_width, section_height);
    }

    // Scale thumbnail to contain within section (no cropping)
    lv_obj_t* thumbnail = lv_obj_find_by_name(detail_view_widget, "detail_thumbnail");
    if (thumbnail) {
        ui_image_scale_to_contain(thumbnail, section_width, section_height, LV_IMAGE_ALIGN_TOP_MID);
    }
}

void ui_panel_print_select_show_detail_view() {
    if (detail_view_widget) {
        lv_obj_remove_flag(detail_view_widget, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(detail_view_widget);
        lv_subject_set_int(&detail_view_visible_subject, 1);

        // Scale images after showing (layout must be calculated first)
        scale_detail_images();
    }
}

void ui_panel_print_select_hide_detail_view() {
    if (detail_view_widget) {
        lv_obj_add_flag(detail_view_widget, LV_OBJ_FLAG_HIDDEN);
        lv_subject_set_int(&detail_view_visible_subject, 0);
    }
}

static void create_detail_view() {
    if (!parent_screen_widget) {
        spdlog::error("Cannot create detail view: parent_screen_widget is null");
        return;
    }

    if (detail_view_widget) {
        spdlog::error("Detail view already exists");
        return;
    }

    // Create detail view from XML component (as child of screen for full overlay)
    detail_view_widget =
        (lv_obj_t*)lv_xml_create(parent_screen_widget, "print_file_detail", nullptr);

    if (!detail_view_widget) {
        spdlog::error("Failed to create detail view from XML");
        return;
    }

    // Calculate width to fill remaining space after navigation bar (screen-size agnostic)
    lv_coord_t screen_width = lv_obj_get_width(parent_screen_widget);
    lv_coord_t nav_width = UI_NAV_WIDTH(screen_width);
    lv_obj_set_width(detail_view_widget, screen_width - nav_width);

    // Set responsive padding for content area
    lv_obj_t* content_container = lv_obj_find_by_name(detail_view_widget, "content_container");
    if (content_container) {
        lv_coord_t padding = ui_get_header_content_padding(lv_obj_get_height(parent_screen_widget));
        lv_obj_set_style_pad_all(content_container, padding, 0);
        spdlog::debug("[PrintSelect] Detail view content padding: %dpx (responsive)", padding);
    }

    lv_obj_add_flag(detail_view_widget, LV_OBJ_FLAG_HIDDEN);

    // NOTE: Image scaling is done in scale_detail_images() when the detail view is shown,
    // not here during creation, because the flex layout dimensions aren't calculated yet.

    // Wire up back button
    lv_obj_t* back_button = lv_obj_find_by_name(detail_view_widget, "back_button");
    if (back_button) {
        lv_obj_add_event_cb(
            back_button,
            [](lv_event_t* e) {
                (void)e;
                ui_panel_print_select_hide_detail_view();
            },
            LV_EVENT_CLICKED, nullptr);
    }

    // Wire up delete button
    lv_obj_t* delete_button = lv_obj_find_by_name(detail_view_widget, "delete_button");
    if (delete_button) {
        lv_obj_add_event_cb(
            delete_button,
            [](lv_event_t* e) {
                (void)e;
                ui_panel_print_select_show_delete_confirmation();
            },
            LV_EVENT_CLICKED, nullptr);
    }

    // Wire up print button
    lv_obj_t* print_button = lv_obj_find_by_name(detail_view_widget, "print_button");
    if (print_button) {
        lv_obj_add_event_cb(
            print_button,
            [](lv_event_t* e) {
                (void)e;

                // Get the filename to print
                std::string filename_to_print(selected_filename_buffer);

                // Get MoonrakerAPI instance
                MoonrakerAPI* api = get_moonraker_api();
                if (api) {
                    spdlog::info("Starting print: {}", filename_to_print);

                    // Start the print via Moonraker
                    api->start_print(
                        filename_to_print,
                        // Success callback
                        []() {
                            spdlog::info("Print started successfully");

                            if (print_status_panel_widget) {
                                // Hide detail view
                                ui_panel_print_select_hide_detail_view();

                                // Push print status panel onto navigation history
                                ui_nav_push_overlay(print_status_panel_widget);

                                // Hide print select panel (will be restored by nav history when
                                // going back)
                                if (panel_root_widget) {
                                    lv_obj_add_flag(panel_root_widget, LV_OBJ_FLAG_HIDDEN);
                                }

                                // Show print status panel
                                lv_obj_remove_flag(print_status_panel_widget, LV_OBJ_FLAG_HIDDEN);
                                lv_obj_move_foreground(print_status_panel_widget);

                                // Note: Print status panel will now show real print progress from
                                // Moonraker via the PrinterState subjects that are updated from
                                // notifications
                            } else {
                                spdlog::error(
                                    "Print status panel not set - cannot show print progress");
                            }
                        },
                        // Error callback
                        [filename_to_print](const MoonrakerError& error) {
                            spdlog::error("Failed to start print for {}: {} ({})",
                                          filename_to_print, error.message,
                                          error.get_type_string());

                            // TODO: Show error message to user
                        });
                } else {
                    // Fall back to mock print if MoonrakerAPI not available
                    spdlog::warn("MoonrakerAPI not available - using mock print");

                    if (print_status_panel_widget) {
                        ui_panel_print_select_hide_detail_view();
                        ui_nav_push_overlay(print_status_panel_widget);

                        if (panel_root_widget) {
                            lv_obj_add_flag(panel_root_widget, LV_OBJ_FLAG_HIDDEN);
                        }

                        const char* filename = selected_filename_buffer;
                        lv_obj_remove_flag(print_status_panel_widget, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_move_foreground(print_status_panel_widget);

                        // Start mock print (250 layers, 3 hours = 10800 seconds)
                        ui_panel_print_status_start_mock_print(filename, 250, 10800);

                        spdlog::info("Started mock print for: %s", filename);
                    }
                }
            },
            LV_EVENT_CLICKED, nullptr);
    }

    // Click backdrop to close
    lv_obj_add_event_cb(
        detail_view_widget,
        [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            lv_obj_t* current_target = (lv_obj_t*)lv_event_get_current_target(e);
            if (target == current_target) {
                ui_panel_print_select_hide_detail_view();
            }
        },
        LV_EVENT_CLICKED, nullptr);

    spdlog::debug("Detail view created");
}

// ============================================================================
// Delete confirmation dialog
// ============================================================================
static void hide_delete_confirmation() {
    if (confirmation_dialog_widget) {
        ui_modal_hide(confirmation_dialog_widget);
        confirmation_dialog_widget = nullptr;
    }
}

void ui_panel_print_select_show_delete_confirmation() {
    // Configure modal: centered, non-persistent (create on demand), no keyboard
    ui_modal_config_t config = {.position = {.use_alignment = true, .alignment = LV_ALIGN_CENTER},
                                .backdrop_opa = 180,
                                .keyboard = nullptr,
                                .persistent = false, // Create on demand
                                .on_close = nullptr};

    // Create message with current filename
    char msg_buf[256];
    snprintf(msg_buf, sizeof(msg_buf),
             "Are you sure you want to delete '%s'? This action cannot be undone.",
             selected_filename_buffer);

    const char* attrs[] = {"title", "Delete File?", "message", msg_buf, NULL};

    confirmation_dialog_widget = ui_modal_show("confirmation_dialog", &config, attrs);

    if (!confirmation_dialog_widget) {
        spdlog::error("Failed to create confirmation dialog");
        return;
    }

    // Wire up cancel button
    lv_obj_t* cancel_btn = lv_obj_find_by_name(confirmation_dialog_widget, "dialog_cancel_btn");
    if (cancel_btn) {
        lv_obj_add_event_cb(
            cancel_btn,
            [](lv_event_t* e) {
                (void)e;
                hide_delete_confirmation();
            },
            LV_EVENT_CLICKED, nullptr);
    }

    // Wire up confirm button
    lv_obj_t* confirm_btn = lv_obj_find_by_name(confirmation_dialog_widget, "dialog_confirm_btn");
    if (confirm_btn) {
        lv_obj_add_event_cb(
            confirm_btn,
            [](lv_event_t* e) {
                (void)e;

                // Get the filename to delete
                std::string filename_to_delete(selected_filename_buffer);

                // Get MoonrakerAPI instance
                MoonrakerAPI* api = get_moonraker_api();
                if (api) {
                    spdlog::info("Deleting file: {}", filename_to_delete);

                    // Delete the file via Moonraker
                    api->delete_file(
                        filename_to_delete,
                        // Success callback
                        []() {
                            spdlog::info("File deleted successfully");

                            // Hide dialogs
                            hide_delete_confirmation();
                            ui_panel_print_select_hide_detail_view();

                            // Refresh the file list
                            ui_panel_print_select_refresh_files();
                        },
                        // Error callback
                        [](const MoonrakerError& error) {
                            spdlog::error("Failed to delete file: {} ({})", error.message,
                                          error.get_type_string());

                            // Hide confirmation dialog but keep detail view open
                            hide_delete_confirmation();

                            // TODO: Show error message to user
                        });
                } else {
                    spdlog::warn("MoonrakerAPI not available - cannot delete file");
                    hide_delete_confirmation();
                }
            },
            LV_EVENT_CLICKED, nullptr);
    }

    spdlog::info("Delete confirmation dialog shown");
}

// ============================================================================
// Event handlers
// ============================================================================
static void attach_card_click_handler(lv_obj_t* card, const PrintFileData& file_data) {
    // Allocate persistent data
    PrintFileData* data = new PrintFileData(file_data);

    lv_obj_add_event_cb(
        card,
        [](lv_event_t* e) {
            PrintFileData* data = (PrintFileData*)lv_event_get_user_data(e);

            ui_panel_print_select_set_file(data->filename.c_str(), data->thumbnail_path.c_str(),
                                           data->print_time_str.c_str(),
                                           data->filament_str.c_str());

            ui_panel_print_select_show_detail_view();
        },
        LV_EVENT_CLICKED, data);
}

static void attach_row_click_handler(lv_obj_t* row, const PrintFileData& file_data) {
    // Allocate persistent data
    PrintFileData* data = new PrintFileData(file_data);

    lv_obj_add_event_cb(
        row,
        [](lv_event_t* e) {
            PrintFileData* data = (PrintFileData*)lv_event_get_user_data(e);

            ui_panel_print_select_set_file(data->filename.c_str(), data->thumbnail_path.c_str(),
                                           data->print_time_str.c_str(),
                                           data->filament_str.c_str());

            ui_panel_print_select_show_detail_view();
        },
        LV_EVENT_CLICKED, data);
}

// ============================================================================
// Print status panel integration
// ============================================================================
void ui_panel_print_select_set_print_status_panel(lv_obj_t* panel) {
    print_status_panel_widget = panel;
    spdlog::debug("Print status panel reference set");
}
