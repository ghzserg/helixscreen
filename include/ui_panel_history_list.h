// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_panel_base.h"

#include "print_history_data.h"

#include <string>
#include <vector>

/**
 * @file ui_panel_history_list.h
 * @brief Print History List Panel - Scrollable list of print jobs with filter/sort
 *
 * The History List Panel displays a scrollable list of all print history jobs
 * with metadata (filename, date, duration, filament type, status).
 *
 * ## Features (Stage 4):
 * - Search: Case-insensitive filename search with 300ms debounce
 * - Status Filter: All, Completed, Failed, Cancelled
 * - Sort: Date (newest/oldest), Duration, Filename
 * - Filters chain: search → status → sort → display
 *
 * ## Navigation:
 * - Entry: History Dashboard → "View Full History" button
 * - Back: Returns to History Dashboard
 * - Row click: Opens Detail Overlay with job metadata
 *
 * ## Features (Stage 5):
 * - Detail Overlay: Shows full print metadata when clicking a row
 * - Reprint: Start the same print again (if file still exists)
 * - Delete: Remove job from history with confirmation
 *
 * ## Data Flow:
 * 1. On activate, receives job list from HistoryDashboardPanel
 * 2. Applies search/filter/sort to create filtered_jobs_ for display
 * 3. Dynamically creates row widgets for filtered jobs
 * 4. Caches job data for row click handling (indexes into filtered_jobs_)
 *
 * @see print_history_data.h for PrintHistoryJob struct
 * @see PanelBase for base class documentation
 */

/**
 * @brief Sort column for history list
 */
enum class HistorySortColumn {
    DATE,     ///< Sort by start_time (default)
    DURATION, ///< Sort by total_duration
    FILENAME  ///< Sort by filename alphabetically
};

/**
 * @brief Sort direction
 */
enum class HistorySortDirection {
    DESC, ///< Descending (newest first, longest first, Z-A)
    ASC   ///< Ascending (oldest first, shortest first, A-Z)
};

/**
 * @brief Status filter options (maps to dropdown indices)
 */
enum class HistoryStatusFilter {
    ALL = 0,       ///< Show all statuses
    COMPLETED = 1, ///< Only completed jobs
    FAILED = 2,    ///< Only failed/error jobs
    CANCELLED = 3  ///< Only cancelled jobs
};
class HistoryListPanel : public PanelBase {
  public:
    /**
     * @brief Construct HistoryListPanel with injected dependencies
     *
     * @param printer_state Reference to PrinterState
     * @param api Pointer to MoonrakerAPI
     */
    HistoryListPanel(PrinterState& printer_state, MoonrakerAPI* api);

    ~HistoryListPanel() override = default;

    //
    // === PanelBase Implementation ===
    //

    /**
     * @brief Initialize subjects for reactive bindings
     *
     * Creates:
     * - history_list_has_jobs: 0 = no history, 1 = has history (for empty state)
     */
    void init_subjects() override;

    /**
     * @brief Setup the list panel with widget references and event handlers
     *
     * @param panel Root panel object from lv_xml_create()
     * @param parent_screen Parent screen for overlay creation
     */
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;

    const char* get_name() const override {
        return "History List";
    }
    const char* get_xml_component_name() const override {
        return "history_list_panel";
    }

    //
    // === Lifecycle Hooks ===
    //

    /**
     * @brief Called when panel becomes visible
     *
     * If jobs haven't been set externally, fetches history from API.
     */
    void on_activate() override;

    /**
     * @brief Called when panel is hidden
     */
    void on_deactivate() override;

    //
    // === Public API ===
    //

    /**
     * @brief Set the jobs to display (called by dashboard when navigating)
     *
     * This avoids redundant API calls since the dashboard already has the data.
     *
     * @param jobs Vector of print history jobs
     */
    void set_jobs(const std::vector<PrintHistoryJob>& jobs);

    /**
     * @brief Refresh the list from the API (fetches first page)
     */
    void refresh_from_api();

    /**
     * @brief Load more jobs (infinite scroll)
     *
     * Called when user scrolls near the bottom of the list.
     * Appends next page of results to existing jobs.
     */
    void load_more();

    //
    // === Static Event Callbacks (public for XML registration) ===
    //

    /**
     * @brief Static callback for search text changes
     * @note Registered with lv_xml_register_event_cb() in init_global_history_list_panel()
     */
    static void on_search_changed_static(lv_event_t* e);

    /**
     * @brief Static callback for status filter dropdown changes
     */
    static void on_status_filter_changed_static(lv_event_t* e);

    /**
     * @brief Static callback for sort dropdown changes
     */
    static void on_sort_changed_static(lv_event_t* e);

    /**
     * @brief Static callback for detail overlay reprint button
     * @note Registered with lv_xml_register_event_cb() in init_global_history_list_panel()
     */
    static void on_detail_reprint_static(lv_event_t* e);

    /**
     * @brief Static callback for detail overlay delete button
     */
    static void on_detail_delete_static(lv_event_t* e);

  private:
    //
    // === Widget References ===
    //

    lv_obj_t* list_content_ = nullptr;  ///< Scrollable content area
    lv_obj_t* list_rows_ = nullptr;     ///< Container for row widgets
    lv_obj_t* empty_state_ = nullptr;   ///< Empty state message container
    lv_obj_t* search_box_ = nullptr;    ///< Search textarea
    lv_obj_t* filter_status_ = nullptr; ///< Status filter dropdown
    lv_obj_t* sort_dropdown_ = nullptr; ///< Sort dropdown
    lv_obj_t* empty_message_ = nullptr; ///< Empty state message label
    lv_obj_t* empty_hint_ = nullptr;    ///< Empty state hint label

    //
    // === State ===
    //

    std::vector<PrintHistoryJob> jobs_;          ///< Source of truth - all jobs
    std::vector<PrintHistoryJob> filtered_jobs_; ///< Filtered/sorted for display
    bool jobs_received_ = false;                 ///< True if jobs were set externally
    bool is_active_ = false;                     ///< True if panel is currently visible

    // Connection state observer to auto-refresh when connected
    lv_observer_t* connection_observer_ = nullptr;

    // Pagination state for infinite scroll
    static constexpr int PAGE_SIZE = 100; ///< Jobs per API request
    uint64_t total_job_count_ = 0;        ///< Total jobs on server (from API)
    bool is_loading_more_ = false;        ///< True while fetching next page
    bool has_more_data_ = true;           ///< False when all jobs loaded

    // Filter/sort state
    std::string search_query_;                                         ///< Current search text
    HistoryStatusFilter status_filter_ = HistoryStatusFilter::ALL;     ///< Current status filter
    HistorySortColumn sort_column_ = HistorySortColumn::DATE;          ///< Current sort column
    HistorySortDirection sort_direction_ = HistorySortDirection::DESC; ///< Current sort direction

    // Search debounce timer
    lv_timer_t* search_timer_ = nullptr; ///< Timer for debounced search (300ms)

    //
    // === Subject for empty state binding ===
    //

    lv_subject_t subject_has_jobs_; ///< 0 = no jobs (show empty), 1 = has jobs (hide empty)

    //
    // === Detail Overlay State ===
    //

    lv_obj_t* detail_overlay_ = nullptr;     ///< Detail overlay widget (created on first use)
    size_t selected_job_index_ = 0;          ///< Index of currently selected job in filtered_jobs_
    uint64_t detail_overlay_generation_ = 0; ///< Generation counter for async callback safety

    // Detail overlay subjects (string subjects for reactive binding)
    lv_subject_t detail_filename_;
    lv_subject_t detail_status_;
    lv_subject_t detail_status_icon_;
    lv_subject_t detail_status_variant_;
    lv_subject_t detail_start_time_;
    lv_subject_t detail_end_time_;
    lv_subject_t detail_duration_;
    lv_subject_t detail_layers_;
    lv_subject_t detail_layer_height_;
    lv_subject_t detail_nozzle_temp_;
    lv_subject_t detail_bed_temp_;
    lv_subject_t detail_filament_;
    lv_subject_t detail_filament_type_;
    lv_subject_t detail_can_reprint_; ///< 1 if file exists, 0 otherwise
    lv_subject_t detail_status_code_; ///< 0=completed, 1=cancelled, 2=error, 3=in_progress

    // Buffers for string subjects (LVGL 9.4 requires pre-allocated buffers)
    static constexpr size_t DETAIL_BUF_SIZE = 128;
    static constexpr size_t SMALL_BUF_SIZE = 32;
    char detail_filename_buf_[256] = {};
    char detail_status_buf_[SMALL_BUF_SIZE] = {};
    char detail_status_icon_buf_[SMALL_BUF_SIZE] = {};
    char detail_status_variant_buf_[SMALL_BUF_SIZE] = {};
    char detail_start_time_buf_[SMALL_BUF_SIZE] = {};
    char detail_end_time_buf_[SMALL_BUF_SIZE] = {};
    char detail_duration_buf_[SMALL_BUF_SIZE] = {};
    char detail_layers_buf_[SMALL_BUF_SIZE] = {};
    char detail_layer_height_buf_[SMALL_BUF_SIZE] = {};
    char detail_nozzle_temp_buf_[SMALL_BUF_SIZE] = {};
    char detail_bed_temp_buf_[SMALL_BUF_SIZE] = {};
    char detail_filament_buf_[SMALL_BUF_SIZE] = {};
    char detail_filament_type_buf_[SMALL_BUF_SIZE] = {};

    //
    // === Internal Methods ===
    //

    /**
     * @brief Populate the list with row widgets from filtered_jobs_
     *
     * Clears existing rows and creates new ones from filtered_jobs_ vector.
     */
    void populate_list();

    /**
     * @brief Clear all row widgets from the list
     */
    void clear_list();

    /**
     * @brief Update the empty state visibility and message
     *
     * Shows appropriate message based on whether filters are active.
     */
    void update_empty_state();

    /**
     * @brief Apply all filters and sort, then populate list
     *
     * Chain: search → status filter → sort → populate_list()
     */
    void apply_filters_and_sort();

    /**
     * @brief Apply search filter to jobs
     *
     * Case-insensitive substring match on filename.
     *
     * @param source Source job list
     * @return Filtered job list
     */
    std::vector<PrintHistoryJob> apply_search_filter(const std::vector<PrintHistoryJob>& source);

    /**
     * @brief Apply status filter to jobs
     *
     * @param source Source job list
     * @return Filtered job list
     */
    std::vector<PrintHistoryJob> apply_status_filter(const std::vector<PrintHistoryJob>& source);

    /**
     * @brief Sort jobs in place
     *
     * @param jobs Jobs to sort (modified in place)
     */
    void apply_sort(std::vector<PrintHistoryJob>& jobs);

    /**
     * @brief Get status color for a job status
     *
     * @param status Job status enum
     * @return Hex color string (e.g., "#00C853")
     */
    static const char* get_status_color(PrintJobStatus status);

    /**
     * @brief Get display text for a job status
     *
     * @param status Job status enum
     * @return Display string (e.g., "Completed", "Failed")
     */
    static const char* get_status_text(PrintJobStatus status);

    //
    // === Click Handlers ===
    //

    /**
     * @brief Attach click handler to a row widget
     *
     * @param row Row widget
     * @param index Index into filtered_jobs_ vector
     */
    void attach_row_click_handler(lv_obj_t* row, size_t index);

    /**
     * @brief Handle row click - opens detail overlay
     *
     * @param index Index of clicked row in filtered_jobs_
     */
    void handle_row_click(size_t index);

    // Static callback wrapper for row clicks
    static void on_row_clicked_static(lv_event_t* e);

    //
    // === Detail Overlay Methods ===
    //

    /**
     * @brief Initialize subjects for the detail overlay
     *
     * Called during init_subjects() to set up all binding subjects.
     */
    void init_detail_subjects();

    /**
     * @brief Show the detail overlay for a job
     *
     * Updates all detail subjects with job data and pushes the overlay.
     *
     * @param job The job to display
     */
    void show_detail_overlay(const PrintHistoryJob& job);

    /**
     * @brief Update detail subjects with job data
     *
     * @param job The job to display
     */
    void update_detail_subjects(const PrintHistoryJob& job);

    /**
     * @brief Handle reprint button click
     */
    void handle_reprint();

    /**
     * @brief Handle delete button click
     */
    void handle_delete();

    /**
     * @brief Actually delete the job after confirmation
     */
    void confirm_delete();

    //
    // === Filter/Sort Event Handlers ===
    //

    /**
     * @brief Handle search text change (debounced)
     */
    void on_search_changed();

    /**
     * @brief Debounced search callback (called after 300ms)
     */
    void do_debounced_search();

    /**
     * @brief Handle status filter dropdown change
     *
     * @param index Selected dropdown index (maps to HistoryStatusFilter)
     */
    void on_status_filter_changed(int index);

    /**
     * @brief Handle sort dropdown change
     *
     * @param index Selected dropdown index
     */
    void on_sort_changed(int index);

    // Search timer callback (private - not used for XML registration)
    static void on_search_timer_static(lv_timer_t* timer);

    //
    // === Infinite Scroll ===
    //

    /**
     * @brief Check if scroll is near bottom and load more if needed
     */
    void check_scroll_position();

    /**
     * @brief Static callback for scroll events
     */
    static void on_scroll_static(lv_event_t* e);

    /**
     * @brief Append rows for newly loaded jobs (without clearing existing rows)
     *
     * @param start_index Index in filtered_jobs_ to start appending from
     */
    void append_rows(size_t start_index);
};

/**
 * @brief Global instance accessor
 *
 * Returns reference to singleton HistoryListPanel used by main.cpp.
 */
HistoryListPanel& get_global_history_list_panel();

/**
 * @brief Initialize the global HistoryListPanel instance
 *
 * Must be called by main.cpp before accessing get_global_history_list_panel().
 *
 * @param printer_state Reference to PrinterState
 * @param api Pointer to MoonrakerAPI
 */
void init_global_history_list_panel(PrinterState& printer_state, MoonrakerAPI* api);
