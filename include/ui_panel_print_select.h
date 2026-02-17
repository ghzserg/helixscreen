// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"
#include "ui_panel_base.h"
#include "ui_plugin_install_modal.h"
#include "ui_print_select_card_view.h"
#include "ui_print_select_detail_view.h"
#include "ui_print_select_file_provider.h"
#include "ui_print_select_file_sorter.h"
#include "ui_print_select_list_view.h"
#include "ui_print_select_path_navigator.h"
#include "ui_print_select_usb_source.h"
#include "ui_print_start_controller.h"

#include "helix_plugin_installer.h"
#include "print_file_data.h"
#include "print_history_manager.h"
#include "subject_managed_panel.h"
#include "usb_backend.h"

#include <atomic>
#include <ctime>
#include <memory>
#include <optional>
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
 * ## Reactive Subjects (6):
 * - selected_filename - Currently selected file name
 * - selected_thumbnail - Thumbnail path for detail view
 * - selected_print_time - Formatted print time string
 * - selected_filament_weight - Formatted filament weight string
 * - detail_view_visible - Controls detail overlay visibility
 * - print_select_view_mode - View mode (0=CARD, 1=LIST) - XML bindings control visibility
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
enum class PrintSelectSortColumn { FILENAME, SIZE, MODIFIED, PRINT_TIME, FILAMENT };

/**
 * @brief Sort direction
 */
enum class PrintSelectSortDirection { ASCENDING, DESCENDING };

// FileSource enum is defined in ui_print_select_usb_source.h

// FileHistoryStatus and PrintFileData are now in print_file_data.h

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
     * @param printer_state Reference to helix::PrinterState for mock detection
     * @param api Pointer to MoonrakerAPI for file operations
     */
    PrintSelectPanel(helix::PrinterState& printer_state, MoonrakerAPI* api);

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
     * @brief Initialize 15 reactive subjects for file selection state
     */
    void init_subjects() override;

    /**
     * @brief Deinitialize all subjects (call before destruction)
     *
     * Follows [L041] pattern for safe subject cleanup.
     */
    void deinit_subjects();

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

    const char* get_name() const override {
        return "Print Select Panel";
    }
    const char* get_xml_component_name() const override {
        return "print_select_panel";
    }

    /**
     * @brief Called when panel becomes visible
     *
     * Triggers lazy file refresh if file list is empty and API is connected.
     * This handles the case where set_api() was called before WebSocket connection.
     */
    void on_activate() override;
    void on_deactivate() override;

    //
    // === Public API ===
    //

    /**
     * @brief Toggle between card and list view
     */
    void toggle_view();

    /**
     * @brief Handle Printer source button click
     *
     * Switches to Printer (Moonraker) file source and refreshes file list.
     * Called from XML event_cb handler.
     */
    void on_source_printer_clicked();

    /**
     * @brief Handle USB source button click
     *
     * Switches to USB file source and refreshes file list.
     * Called from XML event_cb handler.
     */
    void on_source_usb_clicked();

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
     * Fetches files from current directory, updates both views.
     * Metadata (print time, filament) is fetched asynchronously.
     */
    void refresh_files();

    /**
     * @brief Fetch metadata for a range of files (lazy loading)
     *
     * Only fetches metadata for files that haven't been fetched yet.
     * Called initially for visible items, then on scroll for newly visible items.
     *
     * @param start Start index (inclusive)
     * @param end End index (exclusive)
     */
    void fetch_metadata_range(size_t start, size_t end);

    /**
     * @brief Process metadata result and update file list
     *
     * Extracted from fetch_metadata_range to allow reuse from metascan fallback.
     * Handles thumbnail fetching, UI updates, and detail view synchronization.
     *
     * @param i Index in file_list_
     * @param filename Filename for validation
     * @param metadata Metadata to process
     */
    void process_metadata_result(size_t i, const std::string& filename,
                                 const FileMetadata& metadata);

    /**
     * @brief Navigate into a subdirectory
     *
     * @param dirname Directory name to navigate into
     */
    void navigate_to_directory(const std::string& dirname);

    /**
     * @brief Navigate up to parent directory
     *
     * Does nothing if already at root gcodes directory.
     */
    void navigate_up();

    /**
     * @brief Check if currently at root directory
     *
     * @return true if at root gcodes directory
     */
    bool is_at_root() const {
        return current_path_.empty();
    }

    /**
     * @brief Get current directory path
     *
     * @return Current path relative to gcodes root (empty = root)
     */
    const std::string& get_current_path() const {
        return current_path_;
    }

    /**
     * @brief Set MoonrakerAPI and trigger file refresh
     *
     * Overrides base class to automatically refresh file list when API becomes available.
     *
     * @param api Pointer to MoonrakerAPI (may be nullptr to disconnect)
     */
    void set_api(MoonrakerAPI* api);

    /**
     * @brief Set selected file data and update subjects
     *
     * @param filename File name
     * @param thumbnail_src Pre-scaled thumbnail path for cards (.bin)
     * @param original_url Moonraker thumbnail URL (for detail view PNG lookup)
     * @param print_time Formatted print time
     * @param filament_weight Formatted filament weight
     * @param layer_count Formatted layer count (or "--" if unknown)
     * @param print_height Formatted print height (or "--" if unknown)
     * @param modified_timestamp File modification time (for cache validation)
     * @param layer_height Formatted layer height string (e.g., "0.24 mm")
     * @param filament_type Filament type/name (e.g., "ABS" or "PolyMaker PolyLite ABS")
     */
    void set_selected_file(const char* filename, const char* thumbnail_src,
                           const char* original_url, const char* print_time,
                           const char* filament_weight, const char* layer_count,
                           const char* print_height, time_t modified_timestamp,
                           const char* layer_height = "", const char* filament_type = "");

    /**
     * @brief Show detail view overlay for selected file
     */
    void show_detail_view();

    /**
     * @brief Programmatically select a file by name and show detail view
     *
     * Searches for the file in the current file list and opens its detail view.
     * Used by --select-file CLI flag for testing the detail view.
     *
     * @param filename File name to select (matches against filename field)
     * @return true if file was found and selected, false otherwise
     */
    bool select_file_by_name(const std::string& filename);

    /**
     * @brief Set a pending file selection (for --select-file flag)
     *
     * The file will be auto-selected when the file list is populated.
     * Used because files are loaded asynchronously.
     *
     * @param filename File name to select when list is loaded
     */
    void set_pending_file_selection(const std::string& filename);

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

    /**
     * @brief Set UsbManager for USB file access
     *
     * @param manager Pointer to UsbManager (may be nullptr)
     */
    void set_usb_manager(UsbManager* manager);

    /**
     * @brief Handle USB drive inserted event
     *
     * Called when a USB drive is inserted. Shows the USB tab in the source selector.
     */
    void on_usb_drive_inserted();

    /**
     * @brief Handle USB drive removal event
     *
     * Called when a USB drive is removed. Hides the USB tab in the source selector.
     * If currently viewing USB source, switches to Printer source and clears file list.
     */
    void on_usb_drive_removed();

    /**
     * @brief Start print of currently selected file
     */
    void start_print();

    /**
     * @brief Delete currently selected file
     */
    void delete_file();

    /**
     * @brief Hide delete confirmation dialog
     */
    void hide_delete_confirmation();

  private:
    //
    // === Constants ===
    //

    // Card layout constants (used by calculate_card_dimensions)
    static constexpr int CARD_MIN_WIDTH = 150;
    static constexpr int CARD_MAX_WIDTH = 230;
    static constexpr int CARD_DEFAULT_HEIGHT = 245;
    static constexpr int ROW_COUNT_3_MIN_HEIGHT = 520;
    static constexpr const char* FOLDER_UP_ICON = "A:assets/images/folder-up.png";

    //
    // === Widget References ===
    //

    lv_obj_t* card_view_container_ = nullptr;
    lv_obj_t* list_view_container_ = nullptr;
    lv_obj_t* list_rows_container_ = nullptr;
    lv_obj_t* empty_state_container_ = nullptr;
    lv_obj_t* view_toggle_btn_ = nullptr;
    lv_obj_t* view_toggle_icon_ = nullptr;
    lv_obj_t* print_status_panel_widget_ = nullptr;

    //
    // === Subject Buffers ===
    //

    SubjectManager subjects_; ///< RAII manager for automatic subject cleanup

    lv_subject_t selected_filename_subject_; ///< Raw filename (for API/lookups)
    char selected_filename_buffer_[128];

    lv_subject_t selected_display_filename_subject_; ///< Display name (no .gcode extension)
    char selected_display_filename_buffer_[128];

    lv_subject_t selected_thumbnail_subject_;
    char selected_thumbnail_buffer_[256];

    lv_subject_t selected_detail_thumbnail_subject_; ///< Full-res PNG for detail view
    char selected_detail_thumbnail_buffer_[256];

    lv_subject_t selected_print_time_subject_;
    char selected_print_time_buffer_[32];

    lv_subject_t selected_filament_weight_subject_;
    char selected_filament_weight_buffer_[32];

    lv_subject_t selected_layer_count_subject_;
    char selected_layer_count_buffer_[32];

    lv_subject_t selected_print_height_subject_;
    char selected_print_height_buffer_[32];

    lv_subject_t selected_layer_height_subject_;
    char selected_layer_height_buffer_[32];

    lv_subject_t selected_filament_type_subject_;
    char selected_filament_type_buffer_[64]; // Longer for full names like "PolyMaker PolyLite ABS"

    /// Unified pre-print steps (merged file + macro ops, bulleted list)
    lv_subject_t selected_preprint_steps_subject_;
    char selected_preprint_steps_buffer_[256]; ///< Larger buffer for multi-line bulleted list
    lv_subject_t selected_preprint_steps_visible_subject_; ///< 1 if section should be visible

    lv_subject_t detail_view_visible_subject_;

    /// View mode subject: 0 = CARD, 1 = LIST (XML bindings control visibility)
    lv_subject_t view_mode_subject_;

    /// Can start print subject: 1 = can print, 0 = print in progress (disables button via XML
    /// binding)
    lv_subject_t can_print_subject_;

    //
    // === Panel State ===
    //

    std::vector<PrintFileData> file_list_;
    std::string current_path_;           ///< Current directory path (empty = root gcodes dir)
    std::string last_populated_path_;    ///< Track path for scroll preservation on refresh
    std::string selected_filament_type_; ///< Filament type of selected file (for dropdown default)
    std::vector<std::string> selected_filament_colors_; ///< Tool colors of selected file
    size_t selected_file_size_bytes_ = 0; ///< File size of selected file (for safety checks)
    FileHistoryStatus selected_history_status_ =
        FileHistoryStatus::NEVER_PRINTED; ///< History status of selected file
    int selected_success_count_ = 0;      ///< Success count of selected file
    std::string
        pending_file_selection_; ///< File to auto-select when list is populated (--select-file)
    PrintSelectViewMode current_view_mode_ = PrintSelectViewMode::CARD;
    PrintSelectSortColumn current_sort_column_ = PrintSelectSortColumn::MODIFIED;
    PrintSelectSortDirection current_sort_direction_ = PrintSelectSortDirection::DESCENDING;
    bool panel_initialized_ = false;               ///< Guard flag for resize callback
    bool first_activation_ = true;                 ///< Skip redundant refresh on first activation
    bool detail_view_open_ = false;                ///< True while detail view overlay is showing
    bool files_changed_while_detail_open_ = false; ///< True if filelist changed while detail open

    // Debounce timer for view refresh (prevents rebuilding views for each metadata callback)
    lv_timer_t* refresh_timer_ = nullptr;
    static constexpr uint32_t REFRESH_DEBOUNCE_MS = 50; ///< Debounce delay for view refresh

    // Periodic polling timer for file list (fallback when WebSocket notifications are missed)
    lv_timer_t* file_poll_timer_ = nullptr;
    static constexpr uint32_t FILE_POLL_INTERVAL_MS = 15000; ///< 15s polling fallback

    // Virtualized view modules (extracted for maintainability)
    std::unique_ptr<helix::ui::PrintSelectCardView> card_view_;
    std::unique_ptr<helix::ui::PrintSelectListView> list_view_;

    // Detail view overlay manager (handles file detail display, delete confirmation)
    std::unique_ptr<helix::ui::PrintSelectDetailView> detail_view_;

    // USB file source manager (handles Printer/USB source switching)
    std::unique_ptr<helix::ui::PrintSelectUsbSource> usb_source_;

    // File data provider (handles Moonraker file fetching and metadata)
    std::unique_ptr<helix::ui::PrintSelectFileProvider> file_provider_;

    // Print start controller (handles print initiation workflow, warnings)
    std::unique_ptr<helix::ui::PrintStartController> print_controller_;

    // File sorter (handles sorting logic for file list)
    helix::ui::PrintSelectFileSorter file_sorter_;

    // Path navigator (handles directory navigation logic)
    helix::ui::PrintSelectPathNavigator path_navigator_;

    // Observers for reactive updates (ObserverGuard handles cleanup)
    ObserverGuard connection_observer_;
    ObserverGuard print_state_observer_; ///< Observes print state to enable/disable print button
    ObserverGuard
        print_in_progress_observer_;      ///< Observes workflow in-progress for immediate disable
    ObserverGuard helix_plugin_observer_; ///< Observes plugin status for install prompt

    /// Observer for PrintHistoryManager - updates file status when history changes
    helix::HistoryChangedCallback history_observer_;

    /// Destruction flag for async callback safety [L012]
    /// Shared pointer allows callbacks to check if panel is still alive
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    /// Navigation generation counter: incremented on each directory change.
    /// Metadata callbacks capture the current value and discard results
    /// if the generation has changed (user navigated away).
    std::atomic<uint32_t> nav_generation_{0};

    // File list change notification handler name (for unregistering)
    std::string filelist_handler_name_;

    // Plugin installer for helix_print Moonraker plugin
    helix::HelixPluginInstaller plugin_installer_;
    PluginInstallModal plugin_install_modal_;

    //
    // === Internal Methods ===
    //

    /**
     * @brief Calculate optimal card dimensions for current container size
     */
    CardDimensions calculate_card_dimensions();

    /**
     * @brief Populate card view with file list (delegates to card_view_)
     * @param preserve_scroll If true, preserve scroll position; otherwise reset to top
     */
    void populate_card_view(bool preserve_scroll = false);

    /**
     * @brief Populate list view with file list (delegates to list_view_)
     * @param preserve_scroll If true, preserve scroll position; otherwise reset to top
     */
    void populate_list_view(bool preserve_scroll = false);

    /**
     * @brief Animate view container entrance with fade-in
     * @param container The view container to animate (card or list)
     */
    void animate_view_entrance(lv_obj_t* container);

    /**
     * @brief Handle scroll event for virtualization
     * @param container The scrolled container (card or list view)
     */
    void handle_scroll(lv_obj_t* container);

    /**
     * @brief Refresh content of currently visible cards/rows
     */
    void refresh_visible_content();

    /**
     * @brief Check if Moonraker has symlink access to USB files
     *
     * Queries Moonraker for files in the "usb/" path. If files exist,
     * it means there's a symlink (gcodes/usb -> /media/sda1) and the
     * USB tab should be hidden since files are accessible via Printer source.
     */
    void check_moonraker_usb_symlink();

    /**
     * @brief Schedule a debounced view refresh
     *
     * Instead of rebuilding views immediately, this schedules a single refresh
     * after REFRESH_DEBOUNCE_MS. Multiple calls within the debounce window
     * only trigger one actual refresh. This prevents O(n²) widget rebuilds
     * when metadata arrives for each file.
     */
    void schedule_view_refresh();

    /**
     * @brief Apply current sort settings to file_list_
     */
    void apply_sort();

    /**
     * @brief Update empty state visibility based on file_list_ size
     */
    void update_empty_state();

    /**
     * @brief Update print button enabled/disabled state based on print job state
     *
     * Disables the print button when a print is in progress to prevent concurrent prints.
     */
    void update_print_button_state();

    /**
     * @brief Update the unified preprint steps subject
     *
     * Called when either scan_complete or macro_analysis_complete fires to refresh
     * the unified preprint steps display with merged and deduplicated operations.
     */
    void update_preprint_steps_subject();

    /**
     * @brief Update sort indicator icons on column headers
     */
    void update_sort_indicators();

    /**
     * @brief Create detail view overlay (called once during setup)
     */
    void create_detail_view();

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
     * @brief Merge print history status into file list
     *
     * Uses PrintHistoryManager to populate FileHistoryStatus and success_count
     * for each file. Also detects "currently printing" by comparing with
     * PrinterState's print_filename.
     */
    void merge_history_into_file_list();

    //
    // === Static Callbacks (trampolines) ===
    //

    static void on_resize_static(void* user_data);
    static void on_scroll_static(lv_event_t* e);
    static void on_file_clicked_static(lv_event_t* e);
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
 * @param printer_state Reference to helix::PrinterState
 * @param api Pointer to MoonrakerAPI (may be nullptr)
 * @return Pointer to the global instance
 */
PrintSelectPanel* get_print_select_panel(helix::PrinterState& printer_state, MoonrakerAPI* api);

/**
 * @brief Get reference to the global PrintSelectPanel instance
 *
 * @note Must be called after get_print_select_panel() has been called at least once
 * @return Reference to the global instance
 */
PrintSelectPanel& get_global_print_select_panel();
