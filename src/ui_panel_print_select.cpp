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
#include "ui_panel_print_status.h"
#include "ui_utils.h"
#include "ui_fonts.h"
#include "ui_theme.h"
#include "ui_nav.h"
#include "lvgl/src/others/xml/lv_xml.h"
#include <string>
#include <vector>
#include <algorithm>
#include <ctime>

// Default placeholder thumbnail for print files
static const char* DEFAULT_PLACEHOLDER_THUMB = "A:assets/images/thumbnail-placeholder.png";

// ============================================================================
// File data structure
// ============================================================================
struct PrintFileData {
    std::string filename;
    std::string thumbnail_path;
    size_t file_size_bytes;      // File size in bytes
    time_t modified_timestamp;   // Last modified timestamp
    int print_time_minutes;      // Print time in minutes
    float filament_grams;        // Filament weight in grams

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
static const int CARD_MIN_WIDTH = 165;  // Increased to prevent metadata wrapping
static const int CARD_MAX_WIDTH = 220;
static const int CARD_DEFAULT_HEIGHT = 245;  // Default height (overridden by dynamic calculation)

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
        LV_LOG_ERROR("Cannot calculate dimensions: container is null");
        CardDimensions dims = {4, 2, CARD_MIN_WIDTH, CARD_DEFAULT_HEIGHT};
        return dims;
    }

    // Get container width
    lv_coord_t container_width = lv_obj_get_content_width(container);

    // Calculate available height from parent panel dimensions
    lv_obj_t* panel_root = lv_obj_get_parent(container);
    if (!panel_root) {
        LV_LOG_ERROR("Cannot find panel root");
        CardDimensions dims = {4, 2, CARD_MIN_WIDTH, CARD_DEFAULT_HEIGHT};
        return dims;
    }

    // Get actual panel height
    lv_coord_t panel_height = lv_obj_get_height(panel_root);

    // Find the top bar to subtract its height
    lv_obj_t* top_bar = lv_obj_get_child(panel_root, 0);  // First child is top bar
    lv_coord_t top_bar_height = top_bar ? lv_obj_get_height(top_bar) : 60;

    // Check for gap between panel children in flex layout
    lv_coord_t panel_gap = lv_obj_get_style_pad_row(panel_root, LV_PART_MAIN);

    // Calculate available height for cards (panel minus top bar minus container padding minus any gaps)
    lv_coord_t container_padding = lv_obj_get_style_pad_top(container, LV_PART_MAIN) +
                                   lv_obj_get_style_pad_bottom(container, LV_PART_MAIN);
    lv_coord_t container_actual_height = lv_obj_get_height(container);
    lv_coord_t available_height = panel_height - top_bar_height - container_padding - panel_gap;

    LV_LOG_USER("Heights: panel=%d, top_bar=%d, container_actual=%d, container_padding=%d, panel_gap=%d, available=%d",
               panel_height, top_bar_height, container_actual_height, container_padding, panel_gap, available_height);

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

            LV_LOG_USER("Calculated card layout: %d rows × %d columns, card=%dx%d",
                       dims.num_rows, dims.num_columns, dims.card_width, dims.card_height);
            return dims;
        }
    }

    // Fallback to minimum width if nothing fits perfectly
    dims.num_columns = container_width / (CARD_MIN_WIDTH + CARD_GAP);
    if (dims.num_columns < 1) dims.num_columns = 1;  // Safety: always at least 1 column
    dims.card_width = CARD_MIN_WIDTH;

    LV_LOG_WARN("No optimal card layout found, using fallback: %d columns", dims.num_columns);
    return dims;
}

// ============================================================================
// Static state
// ============================================================================
static std::vector<PrintFileData> file_list;
static PrintSelectViewMode current_view_mode = PrintSelectViewMode::CARD;  // Default view mode
static PrintSelectSortColumn current_sort_column = PrintSelectSortColumn::FILENAME;
static PrintSelectSortDirection current_sort_direction = PrintSelectSortDirection::ASCENDING;
static bool panel_initialized = false;  // Guard flag to prevent resize callback before setup complete

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
static void create_confirmation_dialog();
static void scale_detail_images();

// ============================================================================
// Resize handling callback
// ============================================================================
static void on_resize() {
    // CRITICAL: Don't run resize logic until panel is fully initialized (prevents segfault)
    if (!panel_initialized) return;

    LV_LOG_USER("Print select panel handling resize event");

    // Only recalculate card view dimensions if currently in card view mode
    if (current_view_mode == PrintSelectViewMode::CARD && card_view_container) {
        populate_card_view();
    }

    // Update detail view content padding if detail view exists
    if (detail_view_widget && parent_screen_widget) {
        lv_obj_t* content_container = lv_obj_find_by_name(detail_view_widget, "content_container");
        if (content_container) {
            lv_coord_t padding = ui_get_header_content_padding(lv_obj_get_height(parent_screen_widget));
            lv_obj_set_style_pad_all(content_container, padding, 0);
        }
    }
}

// ============================================================================
// Subject initialization
// ============================================================================
void ui_panel_print_select_init_subjects() {
    // Initialize selected file subjects
    lv_subject_init_string(&selected_filename_subject, selected_filename_buffer,
                          nullptr, sizeof(selected_filename_buffer), "");
    lv_xml_register_subject(nullptr, "selected_filename", &selected_filename_subject);

    lv_subject_init_string(&selected_thumbnail_subject, selected_thumbnail_buffer,
                          nullptr, sizeof(selected_thumbnail_buffer), DEFAULT_PLACEHOLDER_THUMB);
    lv_xml_register_subject(nullptr, "selected_thumbnail", &selected_thumbnail_subject);

    lv_subject_init_string(&selected_print_time_subject, selected_print_time_buffer,
                          nullptr, sizeof(selected_print_time_subject), "");
    lv_xml_register_subject(nullptr, "selected_print_time", &selected_print_time_subject);

    lv_subject_init_string(&selected_filament_weight_subject, selected_filament_weight_buffer,
                          nullptr, sizeof(selected_filament_weight_buffer), "");
    lv_xml_register_subject(nullptr, "selected_filament_weight", &selected_filament_weight_subject);

    // Initialize detail view visibility subject (0 = hidden, 1 = visible)
    lv_subject_init_int(&detail_view_visible_subject, 0);
    lv_xml_register_subject(nullptr, "detail_view_visible", &detail_view_visible_subject);

    LV_LOG_USER("Print select panel subjects initialized");
}

// ============================================================================
// Panel setup (call after XML creation)
// ============================================================================
void ui_panel_print_select_setup(lv_obj_t* panel_root, lv_obj_t* parent_screen) {
    if (!panel_root) {
        LV_LOG_ERROR("Cannot setup print select panel: panel_root is null");
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
        LV_LOG_ERROR("Failed to find required widgets in print select panel");
        return;
    }

    // Wire up view toggle button
    lv_obj_add_event_cb(view_toggle_btn, [](lv_event_t* e) {
        (void)e;
        ui_panel_print_select_toggle_view();
    }, LV_EVENT_CLICKED, nullptr);

    // Wire up column header click handlers
    static const char* header_names[] = {"header_filename", "header_size", "header_modified",
                                         "header_print_time"};
    // IMPORTANT: Static storage so pointers remain valid after function returns
    static PrintSelectSortColumn columns[] = {PrintSelectSortColumn::FILENAME,
                                              PrintSelectSortColumn::SIZE,
                                              PrintSelectSortColumn::MODIFIED,
                                              PrintSelectSortColumn::PRINT_TIME};

    for (int i = 0; i < 4; i++) {
        lv_obj_t* header = lv_obj_find_by_name(panel_root, header_names[i]);
        if (header) {
            lv_obj_add_event_cb(header, [](lv_event_t* e) {
                PrintSelectSortColumn* col = (PrintSelectSortColumn*)lv_event_get_user_data(e);
                ui_panel_print_select_sort_by(*col);
            }, LV_EVENT_CLICKED, &columns[i]);
        }
    }

    // Create detail view and confirmation dialog
    create_detail_view();
    create_confirmation_dialog();

    // Register resize callback for responsive card layout
    ui_resize_handler_register(on_resize);

    // Mark panel as fully initialized (enables resize callbacks)
    panel_initialized = true;

    LV_LOG_USER("Print select panel setup complete");
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
        LV_LOG_USER("Switched to list view");
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
        LV_LOG_USER("Switched to card view");
    }

    update_empty_state();
}

// ============================================================================
// Sorting
// ============================================================================
static void apply_sort() {
    std::sort(file_list.begin(), file_list.end(), [](const PrintFileData& a, const PrintFileData& b) {
        bool result = false;

        switch (current_sort_column) {
            case PrintSelectSortColumn::FILENAME:
                result = a.filename < b.filename;
                break;
            case PrintSelectSortColumn::SIZE:
                result = a.file_size_bytes < b.file_size_bytes;
                break;
            case PrintSelectSortColumn::MODIFIED:
                result = a.modified_timestamp > b.modified_timestamp;  // Newer first by default
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

    LV_LOG_USER("Sorted by column %d, direction %d", (int)column, (int)current_sort_direction);
}

static void update_sort_indicators() {
    // Update column header icons to show sort direction
    const char* header_names[] = {"header_filename", "header_size", "header_modified",
                                  "header_print_time"};
    PrintSelectSortColumn columns[] = {PrintSelectSortColumn::FILENAME,
                                       PrintSelectSortColumn::SIZE,
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
// Populate views
// ============================================================================
void ui_panel_print_select_populate_test_data(lv_obj_t* panel_root) {
    if (!panel_root) {
        LV_LOG_ERROR("Cannot populate: panel_root is null");
        return;
    }

    // Clear existing file list
    file_list.clear();

    // Generate test file data
    struct TestFile {
        const char* filename;
        size_t size_bytes;
        int days_ago;         // For timestamp calculation
        int print_time_mins;
        float filament_grams;
    };

    TestFile test_files[] = {
        {"Benchy.gcode", 1024 * 512, 1, 150, 45.0f},
        {"Calibration_Cube.gcode", 1024 * 128, 2, 45, 12.0f},
        {"Large_Vase_With_Very_Long_Filename_That_Should_Truncate.gcode", 1024 * 1024 * 2, 3, 30, 8.0f},
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
        data.modified_timestamp = now - (file.days_ago * 86400);  // 86400 seconds per day
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

    LV_LOG_USER("Populated print select panel with %d test files", (int)file_list.size());
}

static void populate_card_view() {
    if (!card_view_container) return;

    // Clear existing cards
    lv_obj_clean(card_view_container);

    // CRITICAL: Force layout calculation before querying dimensions (prevents segfault on tiny screens)
    lv_obj_update_layout(card_view_container);

    // Calculate optimal card dimensions based on actual container width
    CardDimensions dims = calculate_card_dimensions(card_view_container);

    // Update container gap
    lv_obj_set_style_pad_gap(card_view_container, CARD_GAP, LV_PART_MAIN);

    for (const auto& file : file_list) {
        // Create XML attributes array
        const char* attrs[] = {
            "thumbnail_src", file.thumbnail_path.c_str(),
            "filename", file.filename.c_str(),
            "print_time", file.print_time_str.c_str(),
            "filament_weight", file.filament_str.c_str(),
            NULL
        };

        // Create card component
        lv_obj_t* card = (lv_obj_t*)lv_xml_create(card_view_container, CARD_COMPONENT_NAME, attrs);

        if (card) {
            // Override card dimensions to calculated optimal size
            lv_obj_set_width(card, dims.card_width);
            lv_obj_set_height(card, dims.card_height);
            lv_obj_set_style_flex_grow(card, 0, LV_PART_MAIN);  // Disable flex_grow

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
                    float scale = (scale_w > scale_h) ? scale_w : scale_h;  // Use larger scale to cover

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
                    float scale = (scale_w > scale_h) ? scale_w : scale_h;  // Use larger scale to cover

                    uint16_t zoom = (uint16_t)(scale * 256);
                    lv_image_set_scale(thumbnail, zoom);
                    lv_image_set_inner_align(thumbnail, LV_IMAGE_ALIGN_CENTER);

                    LV_LOG_USER("Thumbnail scale: img=%dx%d, card=%dx%d, zoom=%d (%.1f%%)",
                               header.w, header.h, dims.card_width, dims.card_height, zoom, scale * 100);
                }
            }

            // Attach click handler
            attach_card_click_handler(card, file);
        }
    }
}

static void populate_list_view() {
    if (!list_rows_container) return;

    // Clear existing rows
    lv_obj_clean(list_rows_container);

    for (const auto& file : file_list) {
        // Create XML attributes array
        const char* attrs[] = {
            "filename", file.filename.c_str(),
            "file_size", file.size_str.c_str(),
            "modified_date", file.modified_str.c_str(),
            "print_time", file.print_time_str.c_str(),
            NULL
        };

        // Create list row from XML component
        lv_obj_t* row = (lv_obj_t*)lv_xml_create(list_rows_container, "print_file_list_row", attrs);

        // Attach click handler
        if (row) {
            attach_row_click_handler(row, file);
        }
    }
}

static void update_empty_state() {
    if (!empty_state_container) return;

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

    LV_LOG_USER("Selected file: %s", filename);
}

// ============================================================================
// Detail view image scaling helper
// ============================================================================
static void scale_detail_images() {
    if (!detail_view_widget) return;

    // Find thumbnail section to get target dimensions
    lv_obj_t* thumbnail_section = lv_obj_find_by_name(detail_view_widget, "thumbnail_section");
    if (!thumbnail_section) {
        LV_LOG_WARN("Thumbnail section not found in detail view, cannot scale images");
        return;
    }

    // Force layout update to get accurate dimensions (CRITICAL for flex layouts)
    lv_obj_update_layout(thumbnail_section);

    lv_coord_t section_width = lv_obj_get_content_width(thumbnail_section);
    lv_coord_t section_height = lv_obj_get_content_height(thumbnail_section);

    LV_LOG_USER("Detail view thumbnail section: %dx%d", section_width, section_height);

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
        LV_LOG_ERROR("Cannot create detail view: parent_screen_widget is null");
        return;
    }

    if (detail_view_widget) {
        LV_LOG_WARN("Detail view already exists");
        return;
    }

    // Create detail view from XML component (as child of screen for full overlay)
    detail_view_widget = (lv_obj_t*)lv_xml_create(parent_screen_widget, "print_file_detail", nullptr);

    if (!detail_view_widget) {
        LV_LOG_ERROR("Failed to create detail view from XML");
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
        LV_LOG_USER("[PrintSelect] Detail view content padding: %dpx (responsive)", padding);
    }

    lv_obj_add_flag(detail_view_widget, LV_OBJ_FLAG_HIDDEN);

    // NOTE: Image scaling is done in scale_detail_images() when the detail view is shown,
    // not here during creation, because the flex layout dimensions aren't calculated yet.

    // Wire up back button
    lv_obj_t* back_button = lv_obj_find_by_name(detail_view_widget, "back_button");
    if (back_button) {
        lv_obj_add_event_cb(back_button, [](lv_event_t* e) {
            (void)e;
            ui_panel_print_select_hide_detail_view();
        }, LV_EVENT_CLICKED, nullptr);
    }

    // Wire up delete button
    lv_obj_t* delete_button = lv_obj_find_by_name(detail_view_widget, "delete_button");
    if (delete_button) {
        lv_obj_add_event_cb(delete_button, [](lv_event_t* e) {
            (void)e;
            ui_panel_print_select_show_delete_confirmation();
        }, LV_EVENT_CLICKED, nullptr);
    }

    // Wire up print button
    lv_obj_t* print_button = lv_obj_find_by_name(detail_view_widget, "print_button");
    if (print_button) {
        lv_obj_add_event_cb(print_button, [](lv_event_t* e) {
            (void)e;
            if (print_status_panel_widget) {
                // Hide detail view
                ui_panel_print_select_hide_detail_view();

                // Push print status panel onto navigation history
                ui_nav_push_overlay(print_status_panel_widget);

                // Hide print select panel (will be restored by nav history when going back)
                if (panel_root_widget) {
                    lv_obj_add_flag(panel_root_widget, LV_OBJ_FLAG_HIDDEN);
                }

                // Start mock print with selected file
                const char* filename = selected_filename_buffer;

                // Show print status panel
                lv_obj_remove_flag(print_status_panel_widget, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(print_status_panel_widget);

                // Start mock print (250 layers, 3 hours = 10800 seconds)
                ui_panel_print_status_start_mock_print(filename, 250, 10800);

                LV_LOG_USER("Started mock print for: %s", filename);
            } else {
                LV_LOG_WARN("Print status panel not set - cannot start print");
            }
        }, LV_EVENT_CLICKED, nullptr);
    }

    // Click backdrop to close
    lv_obj_add_event_cb(detail_view_widget, [](lv_event_t* e) {
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* current_target = (lv_obj_t*)lv_event_get_current_target(e);
        if (target == current_target) {
            ui_panel_print_select_hide_detail_view();
        }
    }, LV_EVENT_CLICKED, nullptr);

    LV_LOG_USER("Detail view created");
}

// ============================================================================
// Delete confirmation dialog
// ============================================================================
void ui_panel_print_select_show_delete_confirmation() {
    if (!confirmation_dialog_widget) {
        LV_LOG_ERROR("Confirmation dialog not created");
        return;
    }

    // Update dialog message with current filename
    lv_obj_t* message_label = lv_obj_find_by_name(confirmation_dialog_widget, "dialog_message");
    if (message_label) {
        char msg_buf[256];
        snprintf(msg_buf, sizeof(msg_buf), "Are you sure you want to delete '%s'? This action cannot be undone.",
                 selected_filename_buffer);
        lv_label_set_text(message_label, msg_buf);
    }

    lv_obj_remove_flag(confirmation_dialog_widget, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(confirmation_dialog_widget);
    LV_LOG_USER("Delete confirmation dialog shown");
}

static void hide_delete_confirmation() {
    if (confirmation_dialog_widget) {
        lv_obj_add_flag(confirmation_dialog_widget, LV_OBJ_FLAG_HIDDEN);
    }
}

static void create_confirmation_dialog() {
    if (!parent_screen_widget) {
        LV_LOG_ERROR("Cannot create confirmation dialog: parent_screen_widget is null");
        return;
    }

    if (confirmation_dialog_widget) {
        LV_LOG_WARN("Confirmation dialog already exists");
        return;
    }

    // Create confirmation dialog from XML component (as child of screen for correct z-order)
    const char* attrs[] = {
        "title", "Delete File?",
        "message", "Are you sure you want to delete this file? This action cannot be undone.",
        NULL
    };

    confirmation_dialog_widget = (lv_obj_t*)lv_xml_create(parent_screen_widget, "confirmation_dialog", attrs);

    if (!confirmation_dialog_widget) {
        LV_LOG_ERROR("Failed to create confirmation dialog from XML");
        return;
    }

    lv_obj_add_flag(confirmation_dialog_widget, LV_OBJ_FLAG_HIDDEN);

    // Wire up cancel button
    lv_obj_t* cancel_btn = lv_obj_find_by_name(confirmation_dialog_widget, "dialog_cancel_btn");
    if (cancel_btn) {
        lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
            (void)e;
            hide_delete_confirmation();
        }, LV_EVENT_CLICKED, nullptr);
    }

    // Wire up confirm button
    lv_obj_t* confirm_btn = lv_obj_find_by_name(confirmation_dialog_widget, "dialog_confirm_btn");
    if (confirm_btn) {
        lv_obj_add_event_cb(confirm_btn, [](lv_event_t* e) {
            (void)e;
            // TODO: Implement actual delete functionality
            LV_LOG_USER("File deleted (placeholder action)");
            hide_delete_confirmation();
            ui_panel_print_select_hide_detail_view();
        }, LV_EVENT_CLICKED, nullptr);
    }

    // Click backdrop to cancel
    lv_obj_add_event_cb(confirmation_dialog_widget, [](lv_event_t* e) {
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* current_target = (lv_obj_t*)lv_event_get_current_target(e);
        if (target == current_target) {
            hide_delete_confirmation();
        }
    }, LV_EVENT_CLICKED, nullptr);

    LV_LOG_USER("Confirmation dialog created");
}

// ============================================================================
// Event handlers
// ============================================================================
static void attach_card_click_handler(lv_obj_t* card, const PrintFileData& file_data) {
    // Allocate persistent data
    PrintFileData* data = new PrintFileData(file_data);

    lv_obj_add_event_cb(card, [](lv_event_t* e) {
        PrintFileData* data = (PrintFileData*)lv_event_get_user_data(e);

        ui_panel_print_select_set_file(
            data->filename.c_str(),
            data->thumbnail_path.c_str(),
            data->print_time_str.c_str(),
            data->filament_str.c_str()
        );

        ui_panel_print_select_show_detail_view();
    }, LV_EVENT_CLICKED, data);
}

static void attach_row_click_handler(lv_obj_t* row, const PrintFileData& file_data) {
    // Allocate persistent data
    PrintFileData* data = new PrintFileData(file_data);

    lv_obj_add_event_cb(row, [](lv_event_t* e) {
        PrintFileData* data = (PrintFileData*)lv_event_get_user_data(e);

        ui_panel_print_select_set_file(
            data->filename.c_str(),
            data->thumbnail_path.c_str(),
            data->print_time_str.c_str(),
            data->filament_str.c_str()
        );

        ui_panel_print_select_show_detail_view();
    }, LV_EVENT_CLICKED, data);
}

// ============================================================================
// Print status panel integration
// ============================================================================
void ui_panel_print_select_set_print_status_panel(lv_obj_t* panel) {
    print_status_panel_widget = panel;
    LV_LOG_USER("Print status panel reference set");
}
