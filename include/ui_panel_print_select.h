// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_panel_base.h"

#include <ctime>
#include <string>
#include <vector>

/**
 * @file ui_panel_print_select.h
 * @brief Print file selection panel (class-based)
 *
 * Provides a file browser for G-code files with:
 * - Card view (grid of thumbnails) and list view (sortable table)
 * - View toggle button to switch between modes
 * - Sortable columns: filename, size, modified date, print time
 * - Detail overlay with file metadata and action buttons
 * - Delete confirmation dialog
 * - MoonrakerAPI integration for file listing, deletion, and print start
 *
 * ## Reactive Subjects (5):
 * - selected_filename - Currently selected file name
 * - selected_thumbnail - Thumbnail path for detail view
 * - selected_print_time - Formatted print time string
 * - selected_filament_weight - Formatted filament weight string
 * - detail_view_visible - Controls detail overlay visibility
 *
 * ## Migration Notes:
 * This is the largest panel in the codebase (1167 lines). Key patterns:
 * - Static PrintFileData allocations in attach_*_click_handler() are now managed
 *   by storing data in the file_list_ and passing indices via user_data
 * - All lambdas converted to static trampolines with this pointer
 * - Resize callback uses static trampoline pattern
 *
 * @see PanelBase for base class documentation
 * @see docs/PANEL_MIGRATION.md for migration procedure
 */

/**
 * @brief View mode for print select panel
 */
enum class PrintSelectViewMode {
    CARD = 0, ///< Card grid view (default)
    LIST = 1  ///< List view with columns
};

/**
 * @brief Sort column for list view
 */
enum class PrintSelectSortColumn {
    FILENAME,
    SIZE,
    MODIFIED,
    PRINT_TIME,
    FILAMENT
};

/**
 * @brief Sort direction
 */
enum class PrintSelectSortDirection {
    ASCENDING,
    DESCENDING
};

/**
 * @brief File data structure for print files
 */
struct PrintFileData {
    std::string filename;
    std::string thumbnail_path;
    size_t file_size_bytes;    ///< File size in bytes
    time_t modified_timestamp; ///< Last modified timestamp
    int print_time_minutes;    ///< Print time in minutes
    float filament_grams;      ///< Filament weight in grams

    // Formatted strings (cached for performance)
    std::string size_str;
    std::string modified_str;
    std::string print_time_str;
    std::string filament_str;
};

/**
 * @brief Card layout dimensions calculated from container size
 */
struct CardDimensions {
    int num_columns;
    int num_rows;
    int card_width;
    int card_height;
};

/**
 * @brief Print file selection panel with card/list views
 *
 * Displays G-code files from Moonraker with two view modes:
 * - Card view: Grid of file cards with thumbnails
 * - List view: Sortable table with file metadata
 *
 * Selected files show a detail overlay with print/delete options.
 */
class PrintSelectPanel : public PanelBase {
  public:
    /**
     * @brief Construct PrintSelectPanel with injected dependencies
     *
     * @param printer_state Reference to PrinterState for mock detection
     * @param api Pointer to MoonrakerAPI for file operations
     */
    PrintSelectPanel(PrinterState& printer_state, MoonrakerAPI* api);

    /**
     * @brief Destructor - cleanup observers
     *
     * @note Does NOT call LVGL functions (static destruction order safety).
     *       Widget tree is cleaned up by LVGL.
     */
    ~PrintSelectPanel() override;

    //
    // === PanelBase Implementation ===
    //

    /**
     * @brief Initialize 5 reactive subjects for file selection state
     */
    void init_subjects() override;

    /**
     * @brief Setup the print select panel
     *
     * Wires up view toggle, sort headers, creates detail view overlay,
     * registers resize callback, and initiates file loading.
     *
     * @param panel Root panel object from lv_xml_create()
     * @param parent_screen Parent screen for overlay positioning
     */
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;

    const char* get_name() const override { return "Print Select Panel"; }
    const char* get_xml_component_name() const override { return "print_select_panel"; }

    //
    // === Public API ===
    //

    /**
     * @brief Toggle between card and list view
     */
    void toggle_view();

    /**
     * @brief Sort files by specified column
     *
     * Toggles direction if same column, otherwise sorts ascending.
     *
     * @param column Column to sort by
     */
    void sort_by(PrintSelectSortColumn column);

    /**
     * @brief Refresh file list from Moonraker
     *
     * Fetches files from gcodes directory, updates both views.
     * Metadata (print time, filament) is fetched asynchronously.
     */
    void refresh_files();

    /**
     * @brief Populate with test data (when Moonraker unavailable)
     */
    void populate_test_data();

    /**
     * @brief Set selected file data and update subjects
     *
     * @param filename File name
     * @param thumbnail_src Thumbnail path
     * @param print_time Formatted print time
     * @param filament_weight Formatted filament weight
     */
    void set_selected_file(const char* filename, const char* thumbnail_src,
                           const char* print_time, const char* filament_weight);

    /**
     * @brief Show detail view overlay for selected file
     */
    void show_detail_view();

    /**
     * @brief Hide detail view overlay
     */
    void hide_detail_view();

    /**
     * @brief Show delete confirmation dialog
     */
    void show_delete_confirmation();

    /**
     * @brief Set reference to print status panel
     *
     * Required for navigating to print status after starting a print.
     *
     * @param panel Print status panel widget
     */
    void set_print_status_panel(lv_obj_t* panel);

  private:
    //
    // === Constants ===
    //

    static constexpr const char* CARD_COMPONENT_NAME = "print_file_card";
    static constexpr int CARD_GAP = 20;
    static constexpr int CARD_MIN_WIDTH = 165;
    static constexpr int CARD_MAX_WIDTH = 230;
    static constexpr int CARD_DEFAULT_HEIGHT = 245;
    static constexpr int ROW_COUNT_3_MIN_HEIGHT = 520;
    static constexpr const char* DEFAULT_PLACEHOLDER_THUMB = "A:assets/images/thumbnail-placeholder.png";

    //
    // === Widget References ===
    //

    lv_obj_t* card_view_container_ = nullptr;
    lv_obj_t* list_view_container_ = nullptr;
    lv_obj_t* list_rows_container_ = nullptr;
    lv_obj_t* empty_state_container_ = nullptr;
    lv_obj_t* view_toggle_btn_ = nullptr;
    lv_obj_t* view_toggle_icon_ = nullptr;
    lv_obj_t* detail_view_widget_ = nullptr;
    lv_obj_t* confirmation_dialog_widget_ = nullptr;
    lv_obj_t* print_status_panel_widget_ = nullptr;

    //
    // === Subject Buffers ===
    //

    lv_subject_t selected_filename_subject_;
    char selected_filename_buffer_[128];

    lv_subject_t selected_thumbnail_subject_;
    char selected_thumbnail_buffer_[256];

    lv_subject_t selected_print_time_subject_;
    char selected_print_time_buffer_[32];

    lv_subject_t selected_filament_weight_subject_;
    char selected_filament_weight_buffer_[32];

    lv_subject_t detail_view_visible_subject_;

    //
    // === Panel State ===
    //

    std::vector<PrintFileData> file_list_;
    PrintSelectViewMode current_view_mode_ = PrintSelectViewMode::CARD;
    PrintSelectSortColumn current_sort_column_ = PrintSelectSortColumn::FILENAME;
    PrintSelectSortDirection current_sort_direction_ = PrintSelectSortDirection::ASCENDING;
    bool panel_initialized_ = false; ///< Guard flag for resize callback

    //
    // === Internal Methods ===
    //

    /**
     * @brief Calculate optimal card dimensions for current container size
     */
    CardDimensions calculate_card_dimensions();

    /**
     * @brief Repopulate card view with current file_list_
     */
    void populate_card_view();

    /**
     * @brief Repopulate list view with current file_list_
     */
    void populate_list_view();

    /**
     * @brief Apply current sort settings to file_list_
     */
    void apply_sort();

    /**
     * @brief Update empty state visibility based on file_list_ size
     */
    void update_empty_state();

    /**
     * @brief Update sort indicator icons on column headers
     */
    void update_sort_indicators();

    /**
     * @brief Create detail view overlay (called once during setup)
     */
    void create_detail_view();

    /**
     * @brief Scale images in detail view after showing
     */
    void scale_detail_images();

    /**
     * @brief Hide delete confirmation dialog
     */
    void hide_delete_confirmation();

    /**
     * @brief Handle resize event for responsive card layout
     */
    void handle_resize();

    /**
     * @brief Attach click handler to a file card
     *
     * @param card Card widget
     * @param file_index Index into file_list_
     */
    void attach_card_click_handler(lv_obj_t* card, size_t file_index);

    /**
     * @brief Attach click handler to a list row
     *
     * @param row Row widget
     * @param file_index Index into file_list_
     */
    void attach_row_click_handler(lv_obj_t* row, size_t file_index);

    /**
     * @brief Handle file card/row click
     *
     * @param file_index Index of clicked file in file_list_
     */
    void handle_file_click(size_t file_index);

    /**
     * @brief Start print of currently selected file
     */
    void start_print();

    /**
     * @brief Delete currently selected file
     */
    void delete_file();

    /**
     * @brief Construct Moonraker thumbnail URL from relative path
     */
    static std::string construct_thumbnail_url(const std::string& relative_path);

    //
    // === Static Callbacks (trampolines) ===
    //

    static void on_resize_static(void* user_data);
    static void on_view_toggle_clicked_static(lv_event_t* e);
    static void on_header_clicked_static(lv_event_t* e);
    static void on_file_clicked_static(lv_event_t* e);
    static void on_back_button_clicked_static(lv_event_t* e);
    static void on_delete_button_clicked_static(lv_event_t* e);
    static void on_print_button_clicked_static(lv_event_t* e);
    static void on_detail_backdrop_clicked_static(lv_event_t* e);
    static void on_confirm_delete_static(lv_event_t* e);
    static void on_cancel_delete_static(lv_event_t* e);
};

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================
//
// Single global instance for compatibility with main.cpp panel creation.
// Created lazily on first use.
// ============================================================================

/**
 * @brief Get or create the global PrintSelectPanel instance
 *
 * @param printer_state Reference to PrinterState
 * @param api Pointer to MoonrakerAPI (may be nullptr)
 * @return Pointer to the global instance
 */
PrintSelectPanel* get_print_select_panel(PrinterState& printer_state, MoonrakerAPI* api);

