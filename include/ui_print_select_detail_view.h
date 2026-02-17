// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_print_preparation_manager.h"

#include "overlay_base.h"
#include "print_file_data.h" // For FileHistoryStatus
#include "subject_managed_panel.h"

#include <atomic>
#include <functional>
#include <lvgl.h>
#include <memory>
#include <string>

// Forward declarations
class MoonrakerAPI;
namespace helix {
class PrinterState;
}
using helix::PrinterState;

namespace helix::ui {

/**
 * @file ui_print_select_detail_view.h
 * @brief Detail view overlay manager for print selection panel
 *
 * Handles the file detail overlay that appears when a file is selected,
 * including:
 * - Creating and positioning the detail view widget
 * - Showing/hiding with nav system integration
 * - Delete confirmation modal management
 * - Filament type dropdown synchronization
 *
 * ## Usage:
 * @code
 * PrintSelectDetailView detail_view;
 * detail_view.create(parent_screen);
 * detail_view.set_prep_manager(prep_manager);
 * detail_view.set_on_delete_confirmed([this]() { delete_file(); });
 *
 * // When file selected:
 * detail_view.show(filename, current_path, filament_type);
 *
 * // When back button clicked:
 * detail_view.hide();
 * @endcode
 */

/**
 * @brief Callback when delete is confirmed
 */
using DeleteConfirmedCallback = std::function<void()>;

/**
 * @brief Detail view overlay manager
 *
 * Inherits from OverlayBase for lifecycle management (on_activate/on_deactivate).
 * The NavigationManager calls these hooks automatically when the overlay is
 * pushed/popped from the stack.
 */
class PrintSelectDetailView : public OverlayBase {
  public:
    PrintSelectDetailView() = default;
    ~PrintSelectDetailView() override;

    // Non-copyable, non-movable (owns LVGL widgets with external references)
    PrintSelectDetailView(const PrintSelectDetailView&) = delete;
    PrintSelectDetailView& operator=(const PrintSelectDetailView&) = delete;
    PrintSelectDetailView(PrintSelectDetailView&&) = delete;
    PrintSelectDetailView& operator=(PrintSelectDetailView&&) = delete;

    // === OverlayBase Interface ===

    /**
     * @brief Initialize subjects for pre-print option switches
     *
     * Creates and registers subjects that control switch default states.
     * Skip switches (bed_mesh, qgl, z_tilt, nozzle_clean) default to ON.
     * Add-on switches (timelapse) default to OFF.
     *
     * MUST be called BEFORE create() so bindings can find subjects.
     */
    void init_subjects() override;

    /**
     * @brief Create the detail view widget
     *
     * Creates the print_file_detail XML component and configures it.
     * Must be called before show().
     *
     * @param parent_screen Screen to create detail view on
     * @return Root object of overlay, or nullptr on failure
     */
    lv_obj_t* create(lv_obj_t* parent_screen) override;

    /**
     * @brief Get human-readable overlay name
     * @return "Print File Details"
     */
    const char* get_name() const override {
        return "Print File Details";
    }

    /**
     * @brief Called when overlay becomes visible
     *
     * Resets pre-print subjects to defaults and starts async file scanning.
     */
    void on_activate() override;

    /**
     * @brief Called when overlay is being hidden
     *
     * Closes any open modals. Async scans will check alive_ flag.
     */
    void on_deactivate() override;

    /**
     * @brief Clean up resources for async-safe destruction
     *
     * Sets alive_ flag to false so async callbacks bail out.
     * Unregisters from NavigationManager and deinitializes subjects.
     */
    void cleanup() override;

    // === Setup ===

    /**
     * @brief Set dependencies for print preparation
     *
     * @param api MoonrakerAPI for file operations
     * @param printer_state PrinterState for capability detection
     */
    void set_dependencies(MoonrakerAPI* api, PrinterState* printer_state);

    /**
     * @brief Set callback for delete confirmation
     */
    void set_on_delete_confirmed(DeleteConfirmedCallback callback) {
        on_delete_confirmed_ = std::move(callback);
    }

    /**
     * @brief Set the visible subject for XML binding
     *
     * The subject should be initialized to 0 (hidden).
     */
    void set_visible_subject(lv_subject_t* subject) {
        visible_subject_ = subject;
    }

    // === Visibility ===

    /**
     * @brief Show the detail view overlay
     *
     * Pushes overlay via nav system and triggers G-code scanning.
     *
     * @param filename Selected filename (for G-code scanning)
     * @param current_path Current directory path
     * @param filament_type Filament type from metadata (for dropdown default)
     * @param filament_colors Optional tool colors for multi-color prints
     * @param file_size_bytes File size from Moonraker metadata (for safety checks)
     */
    void show(const std::string& filename, const std::string& current_path,
              const std::string& filament_type,
              const std::vector<std::string>& filament_colors = {}, size_t file_size_bytes = 0);

    /**
     * @brief Hide the detail view overlay
     *
     * Uses nav system to properly hide with backdrop management.
     */
    void hide();

    // Note: is_visible() inherited from OverlayBase

    // === Delete Confirmation ===

    /**
     * @brief Show delete confirmation dialog
     *
     * @param filename Filename to display in confirmation message
     */
    void show_delete_confirmation(const std::string& filename);

    /**
     * @brief Hide delete confirmation dialog
     */
    void hide_delete_confirmation();

    // === Widget Access ===

    /**
     * @brief Get the detail view widget
     * @note Returns overlay_root_ from OverlayBase
     */
    [[nodiscard]] lv_obj_t* get_widget() const {
        return overlay_root_;
    }

    /**
     * @brief Get the print button (for enable/disable state)
     */
    [[nodiscard]] lv_obj_t* get_print_button() const {
        return print_button_;
    }

    /**
     * @brief Get the print preparation manager
     */
    [[nodiscard]] PrintPreparationManager* get_prep_manager() const {
        return prep_manager_.get();
    }

    // === Checkbox Access (for prep manager setup) ===

    [[nodiscard]] lv_obj_t* get_bed_mesh_checkbox() const {
        return bed_mesh_checkbox_;
    }
    [[nodiscard]] lv_obj_t* get_qgl_checkbox() const {
        return qgl_checkbox_;
    }
    [[nodiscard]] lv_obj_t* get_z_tilt_checkbox() const {
        return z_tilt_checkbox_;
    }
    [[nodiscard]] lv_obj_t* get_nozzle_clean_checkbox() const {
        return nozzle_clean_checkbox_;
    }
    [[nodiscard]] lv_obj_t* get_timelapse_checkbox() const {
        return timelapse_checkbox_;
    }

    // === Subject Access (for prep manager to read toggle state - LT2) ===

    [[nodiscard]] lv_subject_t* get_preprint_bed_mesh_subject() {
        return &preprint_bed_mesh_;
    }
    [[nodiscard]] lv_subject_t* get_preprint_qgl_subject() {
        return &preprint_qgl_;
    }
    [[nodiscard]] lv_subject_t* get_preprint_z_tilt_subject() {
        return &preprint_z_tilt_;
    }
    [[nodiscard]] lv_subject_t* get_preprint_nozzle_clean_subject() {
        return &preprint_nozzle_clean_;
    }
    [[nodiscard]] lv_subject_t* get_preprint_timelapse_subject() {
        return &preprint_timelapse_;
    }
    [[nodiscard]] lv_subject_t* get_preprint_purge_line_subject() {
        return &preprint_purge_line_;
    }

    // === Resize Handling ===

    /**
     * @brief Handle resize event - update responsive padding
     *
     * @param parent_screen Parent screen for height calculation
     */
    void handle_resize(lv_obj_t* parent_screen);

    /**
     * @brief Update the print history status display
     *
     * @param status The history status (NEVER_PRINTED, CURRENTLY_PRINTING, COMPLETED, FAILED)
     * @param success_count Number of successful prints (used when status is COMPLETED)
     */
    void update_history_status(FileHistoryStatus status, int success_count);

  private:
    // === Dependencies ===
    MoonrakerAPI* api_ = nullptr;
    PrinterState* printer_state_ = nullptr;
    lv_subject_t* visible_subject_ = nullptr;

    // === Widget References ===
    // Note: overlay_root_ inherited from OverlayBase holds the main widget
    lv_obj_t* parent_screen_ = nullptr;
    lv_obj_t* confirmation_dialog_widget_ = nullptr;
    lv_obj_t* print_button_ = nullptr;

    // Pre-print option checkboxes
    lv_obj_t* bed_mesh_checkbox_ = nullptr;
    lv_obj_t* qgl_checkbox_ = nullptr;
    lv_obj_t* z_tilt_checkbox_ = nullptr;
    lv_obj_t* nozzle_clean_checkbox_ = nullptr;
    lv_obj_t* purge_line_checkbox_ = nullptr;
    lv_obj_t* timelapse_checkbox_ = nullptr;

    // Color requirements display
    lv_obj_t* color_requirements_card_ = nullptr;
    lv_obj_t* color_swatches_row_ = nullptr;

    // History status display
    lv_obj_t* history_status_row_ = nullptr;
    lv_obj_t* history_status_icon_ = nullptr;
    lv_obj_t* history_status_label_ = nullptr;

    // Pre-print option subjects (1 = checked/enabled, 0 = unchecked/disabled)
    // Enable switches default ON, add-on switches default OFF
    lv_subject_t preprint_bed_mesh_{};
    lv_subject_t preprint_qgl_{};
    lv_subject_t preprint_z_tilt_{};
    lv_subject_t preprint_nozzle_clean_{};
    lv_subject_t preprint_purge_line_{};
    lv_subject_t preprint_timelapse_{};
    SubjectManager subjects_; // RAII manager for subject cleanup
    // Note: subjects_initialized_ inherited from OverlayBase

    // Print preparation manager (owns it)
    std::unique_ptr<PrintPreparationManager> prep_manager_;

    // === Cached show() parameters (used by on_activate) ===
    std::string current_filename_;
    std::string current_path_;
    std::string current_filament_type_;
    std::vector<std::string> current_filament_colors_;
    size_t current_file_size_bytes_ = 0;

    // === Async Safety [L012] ===
    // Shared pointer to track if this object is still alive when async callbacks execute.
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    // === Callbacks ===
    DeleteConfirmedCallback on_delete_confirmed_;

    // === Internal Methods ===

    /**
     * @brief Static callback for delete confirmation
     */
    static void on_confirm_delete_static(lv_event_t* e);

    /**
     * @brief Static callback for cancel delete
     */
    static void on_cancel_delete_static(lv_event_t* e);

    /**
     * @brief Update color swatches display
     *
     * @param colors Hex color strings (e.g., "#ED1C24")
     */
    void update_color_swatches(const std::vector<std::string>& colors);
};

} // namespace helix::ui
