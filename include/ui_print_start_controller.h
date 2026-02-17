// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <lvgl.h>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
namespace helix {
class PrinterState;
}
class MoonrakerAPI;
struct PrintFileData;

namespace helix::ui {

class PrintSelectDetailView;

/**
 * @brief Controller for print initiation workflow
 *
 * Handles the print start process including:
 * - Filament availability warnings (runout sensor)
 * - AMS color matching validation
 * - Actual print start via PrintPreparationManager
 *
 * This controller does NOT own the file selection state or the detail view.
 * It receives file information via set_file() and delegates the actual
 * print start to PrintPreparationManager (owned by the detail view).
 *
 * @pattern Extracted controller pattern - separates print initiation workflow
 *          from the larger PrintSelectPanel.
 */
class PrintStartController {
  public:
    using PrintStartedCallback = std::function<void()>;
    using PrintCancelledCallback = std::function<void()>;
    using UpdatePrintButtonCallback = std::function<void()>;
    using HideDetailViewCallback = std::function<void()>;
    using ShowDetailViewCallback = std::function<void()>;
    using NavigateToPrintStatusCallback = std::function<void()>;

    /**
     * @brief Construct controller with required dependencies
     *
     * @param printer_state Reference to PrinterState for capability queries
     * @param api Pointer to MoonrakerAPI (may be nullptr initially)
     */
    PrintStartController(PrinterState& printer_state, MoonrakerAPI* api);
    ~PrintStartController();

    // Non-copyable
    PrintStartController(const PrintStartController&) = delete;
    PrintStartController& operator=(const PrintStartController&) = delete;

    /**
     * @brief Set the API (can be null initially, set later)
     */
    void set_api(MoonrakerAPI* api);

    /**
     * @brief Set the detail view for prep manager access
     *
     * The detail view owns the PrintPreparationManager which is needed
     * for the actual print start sequence.
     */
    void set_detail_view(PrintSelectDetailView* detail_view);

    /**
     * @brief Set the file to print
     *
     * @param filename Raw filename (e.g., "benchy.gcode")
     * @param path Directory path relative to gcodes root
     * @param filament_colors Hex colors per tool for AMS matching
     * @param thumbnail_path Extracted thumbnail path (for USB/embedded thumbnails)
     */
    void set_file(const std::string& filename, const std::string& path,
                  const std::vector<std::string>& filament_colors,
                  const std::string& thumbnail_path = "");

    /**
     * @brief Initiate print workflow
     *
     * Entry point for starting a print. Performs checks:
     * 1. Printer state validation (not already printing)
     * 2. Filament runout sensor check (warns if no filament)
     * 3. AMS color match check (warns on mismatches)
     *
     * If all checks pass (or user confirms warnings), executes the print.
     */
    void initiate();

    /**
     * @brief Check if controller is ready to start a print
     *
     * @return true if filename is set and detail view is available
     */
    [[nodiscard]] bool is_ready() const;

    // === Callbacks ===

    void set_on_print_started(PrintStartedCallback cb) {
        on_print_started_ = std::move(cb);
    }
    void set_on_print_cancelled(PrintCancelledCallback cb) {
        on_print_cancelled_ = std::move(cb);
    }
    void set_update_print_button(UpdatePrintButtonCallback cb) {
        update_print_button_ = std::move(cb);
    }
    void set_hide_detail_view(HideDetailViewCallback cb) {
        hide_detail_view_ = std::move(cb);
    }
    void set_show_detail_view(ShowDetailViewCallback cb) {
        show_detail_view_ = std::move(cb);
    }
    void set_navigate_to_print_status(NavigateToPrintStatusCallback cb) {
        navigate_to_print_status_ = std::move(cb);
    }

    /**
     * @brief Set the subject that controls print button enabled state
     *
     * The controller sets this to 0 when print is initiated and relies
     * on update_print_button_ callback for re-enabling on cancel/failure.
     */
    void set_can_print_subject(lv_subject_t* subject) {
        can_print_subject_ = subject;
    }

  private:
    /**
     * @brief Execute the actual print start
     *
     * Called directly when no warning needed, or after user confirms warning dialog.
     * Delegates to PrintPreparationManager for file operations and Moonraker API calls.
     */
    void execute_print_start();

    /**
     * @brief Show filament warning dialog
     *
     * Called when runout sensor indicates no filament. User can proceed or cancel.
     */
    void show_filament_warning();

    /**
     * @brief Check if G-code tool colors match available AMS slot colors
     *
     * @return Vector of tool indices (T0, T1, etc.) that have no matching slot color.
     *         Empty vector if all colors match or AMS is not available.
     */
    std::vector<int> check_ams_color_match();

    /**
     * @brief Show color mismatch warning dialog
     *
     * @param missing_tools Tool indices without matching slot colors
     */
    void show_color_mismatch_warning(const std::vector<int>& missing_tools);

    // Static callbacks for LVGL modal
    static void on_filament_warning_proceed_static(lv_event_t* e);
    static void on_filament_warning_cancel_static(lv_event_t* e);
    static void on_color_mismatch_proceed_static(lv_event_t* e);
    static void on_color_mismatch_cancel_static(lv_event_t* e);

    // === Dependencies ===
    PrinterState& printer_state_;
    MoonrakerAPI* api_ = nullptr;
    PrintSelectDetailView* detail_view_ = nullptr;
    lv_subject_t* can_print_subject_ = nullptr;

    // === File State ===
    std::string filename_;
    std::string path_;
    std::vector<std::string> filament_colors_;
    std::string thumbnail_path_; ///< Pre-extracted thumbnail for USB/embedded files

    // === Modal References ===
    lv_obj_t* filament_warning_modal_ = nullptr;
    lv_obj_t* color_mismatch_modal_ = nullptr;

    // === Callbacks ===
    PrintStartedCallback on_print_started_;
    PrintCancelledCallback on_print_cancelled_;
    UpdatePrintButtonCallback update_print_button_;
    HideDetailViewCallback hide_detail_view_;
    ShowDetailViewCallback show_detail_view_;
    NavigateToPrintStatusCallback navigate_to_print_status_;
};

} // namespace helix::ui
