// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "gcode_file_modifier.h"
#include "gcode_ops_detector.h"
#include "moonraker_api.h"
#include "print_start_analyzer.h"
#include "printer_detector.h"
#include "printer_state.h"

#include <functional>
#include <lvgl.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace helix::ui {

/**
 * @file ui_print_preparation_manager.h
 * @brief Manages pre-print operations and G-code modification
 *
 * Handles the print preparation workflow including:
 * - Scanning G-code files for embedded operations (bed leveling, QGL, etc.)
 * - Collecting user-selected pre-print options from checkboxes
 * - Building and executing pre-print operation sequences
 * - Modifying G-code to disable embedded operations when requested
 *
 * ## Usage:
 * ```cpp
 * PrintPreparationManager prep_manager;
 * prep_manager.set_dependencies(api, printer_state);
 * prep_manager.set_checkboxes(bed_cb, qgl_cb, z_tilt_cb, clean_cb, timelapse_cb);
 *
 * // When detail view opens:
 * prep_manager.scan_file_for_operations(filename, current_path);
 *
 * // When print button clicked:
 * prep_manager.start_print(filename, current_path, on_navigate_to_status);
 * ```
 */

/**
 * @brief Pre-print options read from UI checkboxes
 */
struct PrePrintOptions {
    // File-level operations (from checkboxes in detail view)
    bool bed_mesh = false;
    bool qgl = false;
    bool z_tilt = false;
    bool nozzle_clean = false;
    bool timelapse = false;

    // Macro-level skip flags (passed to PRINT_START as parameters)
    // These are only used when the macro supports the corresponding skip param
    bool skip_macro_bed_mesh = false;
    bool skip_macro_qgl = false;
    bool skip_macro_z_tilt = false;
    bool skip_macro_nozzle_clean = false;
};

/**
 * @brief Result of checking if G-code modification can be performed safely
 *
 * On resource-constrained devices (like AD5M with 512MB RAM), modifying large
 * G-code files can exhaust memory and crash both Moonraker and Klipper.
 * This struct captures whether modification is safe and why (or why not).
 */
struct ModificationCapability {
    bool can_modify = false;     ///< True if modification can be done safely
    bool has_plugin = false;     ///< True if helix_print plugin handles it server-side
    bool has_disk_space = false; ///< True if enough disk space for streaming fallback
    std::string reason;          ///< Human-readable reason if modification is disabled
    size_t available_bytes = 0;  ///< Available disk space in temp directory
    size_t required_bytes = 0;   ///< Estimated bytes needed for modification
};

/**
 * @brief Callback for navigating to print status panel
 */
using NavigateToStatusCallback = std::function<void()>;

/**
 * @brief Callback for print completion (success or failure)
 */
using PrintCompletionCallback = std::function<void(bool success, const std::string& error)>;

/**
 * @brief Callback when G-code scan completes with detected operations
 *
 * @param formatted_ops Human-readable string of detected operations
 *                      (e.g., "Contains: Bed Leveling, QGL" or "")
 */
using ScanCompleteCallback = std::function<void(const std::string& formatted_ops)>;

/**
 * @brief Callback when PRINT_START macro analysis completes
 *
 * @param analysis The analysis result (check .found for validity)
 */
using MacroAnalysisCallback = std::function<void(const helix::PrintStartAnalysis& analysis)>;

/**
 * @brief Manages print preparation workflow
 */
class PrintPreparationManager {
  public:
    PrintPreparationManager() = default;
    ~PrintPreparationManager();

    // Non-copyable, movable
    PrintPreparationManager(const PrintPreparationManager&) = delete;
    PrintPreparationManager& operator=(const PrintPreparationManager&) = delete;
    PrintPreparationManager(PrintPreparationManager&&) noexcept = default;
    PrintPreparationManager& operator=(PrintPreparationManager&&) noexcept = default;

    // === Setup ===

    /**
     * @brief Set API and printer state dependencies
     */
    void set_dependencies(MoonrakerAPI* api, PrinterState* printer_state);

    /**
     * @brief Set checkbox widget references for reading user selections
     *
     * @param bed_mesh Bed mesh checkbox (may be nullptr)
     * @param qgl QGL checkbox (may be nullptr)
     * @param z_tilt Z-tilt checkbox (may be nullptr)
     * @param nozzle_clean Nozzle clean checkbox (may be nullptr)
     * @param timelapse Timelapse checkbox (may be nullptr)
     */
    void set_checkboxes(lv_obj_t* bed_mesh, lv_obj_t* qgl, lv_obj_t* z_tilt, lv_obj_t* nozzle_clean,
                        lv_obj_t* timelapse);

    /**
     * @brief Set callback for when G-code scan completes
     *
     * Called with formatted string of detected operations when scan finishes.
     */
    void set_scan_complete_callback(ScanCompleteCallback callback) {
        on_scan_complete_ = std::move(callback);
    }

    /**
     * @brief Set callback for when PRINT_START macro analysis completes
     */
    void set_macro_analysis_callback(MacroAnalysisCallback callback) {
        on_macro_analysis_complete_ = std::move(callback);
    }

    // === PRINT_START Macro Analysis ===

    /**
     * @brief Analyze the printer's PRINT_START macro (async)
     *
     * Fetches macro definition from printer config and detects operations
     * like bed mesh, QGL, etc. Result is cached and reused.
     *
     * Call this once when connecting to the printer or when the detail
     * view needs to show macro-level operations.
     */
    void analyze_print_start_macro();

    /**
     * @brief Check if PRINT_START analysis is available
     */
    [[nodiscard]] bool has_macro_analysis() const {
        return macro_analysis_.has_value() && macro_analysis_->found;
    }

    /**
     * @brief Check if macro analysis is currently in progress
     *
     * Used to disable Print button until analysis completes, preventing
     * race conditions where print starts before skip params are known.
     */
    [[nodiscard]] bool is_macro_analysis_in_progress() const {
        return macro_analysis_in_progress_;
    }

    /**
     * @brief Get cached PRINT_START analysis result
     */
    [[nodiscard]] const std::optional<helix::PrintStartAnalysis>& get_macro_analysis() const {
        return macro_analysis_;
    }

    /**
     * @brief Format macro-detected operations as human-readable string
     *
     * @return Formatted string like "PRINT_START contains: Bed Mesh, QGL" or ""
     */
    [[nodiscard]] std::string format_macro_operations() const;

    /**
     * @brief Check if a specific operation in PRINT_START is controllable
     *
     * @param category The operation category to check
     * @return true if the operation has a skip parameter in the macro
     */
    [[nodiscard]] bool is_macro_op_controllable(helix::PrintStartOpCategory category) const;

    /**
     * @brief Get the skip parameter name for a macro operation (if controllable)
     *
     * @param category The operation category
     * @return Parameter name (e.g., "SKIP_BED_MESH") or empty string if not controllable
     */
    [[nodiscard]] std::string get_macro_skip_param(helix::PrintStartOpCategory category) const;

    /**
     * @brief Get the parameter semantic for a macro operation
     *
     * @param category The operation category
     * @return ParameterSemantic (OPT_OUT for SKIP_*, OPT_IN for PERFORM_*)
     */
    [[nodiscard]] helix::ParameterSemantic
    get_macro_param_semantic(helix::PrintStartOpCategory category) const;

    // === G-code Scanning ===

    /**
     * @brief Scan a G-code file for embedded operations (async)
     *
     * Downloads file content and scans for operations like bed leveling, QGL, etc.
     * Result is cached until a different file is scanned.
     *
     * @param filename File name (relative to gcodes root)
     * @param current_path Current directory path (empty = root)
     */
    void scan_file_for_operations(const std::string& filename, const std::string& current_path);

    /**
     * @brief Clear cached scan result
     */
    void clear_scan_cache();

    /**
     * @brief Check if scan result is available for a file
     */
    [[nodiscard]] bool has_scan_result_for(const std::string& filename) const;

    /**
     * @brief Get cached scan result (if available)
     */
    [[nodiscard]] const std::optional<gcode::ScanResult>& get_scan_result() const {
        return cached_scan_result_;
    }

    /**
     * @brief Format detected operations as human-readable string
     *
     * @return Formatted string like "Contains: Bed Leveling, QGL" or "" if none
     */
    [[nodiscard]] std::string format_detected_operations() const;

    /**
     * @brief Format unified pre-print steps from both file scan and macro analysis
     *
     * Merges operations detected in the G-code file with operations found in the
     * PRINT_START macro, deduplicates them, and formats as a user-friendly list.
     *
     * @return Bulleted list like "• Bed leveling\n• Nozzle cleaning (optional)"
     *         or empty string if no operations detected
     */
    [[nodiscard]] std::string format_preprint_steps() const;

    // === Resource Safety ===

    /**
     * @brief Set the cached file size from Moonraker metadata
     *
     * Called when detail view fetches file metadata, allowing safety checks
     * to estimate memory/disk requirements for modification.
     *
     * @param size File size in bytes
     */
    void set_cached_file_size(size_t size);

    /**
     * @brief Check if G-code modification can be performed safely
     *
     * Evaluates whether the device has sufficient resources to modify the
     * currently selected G-code file. Returns detailed information about
     * what's available and what's needed.
     *
     * Safety priority:
     * 1. If helix_print plugin available → always safe (server-side)
     * 2. If disk space available for streaming → safe (disk-based modification)
     * 3. Otherwise → unsafe, modification disabled
     *
     * @return ModificationCapability with safety status and details
     */
    [[nodiscard]] ModificationCapability check_modification_capability() const;

    /**
     * @brief Get the temp directory path for streaming operations
     *
     * Uses same logic as ThumbnailCache: XDG → ~/.cache → TMPDIR → /tmp
     *
     * @return Path to usable temp directory, or empty string if none available
     */
    [[nodiscard]] std::string get_temp_directory() const;

    // === Print Execution ===

    /**
     * @brief Read pre-print options from checkbox states
     */
    [[nodiscard]] PrePrintOptions read_options_from_checkboxes() const;

    /**
     * @brief Start print with optional pre-print operations
     *
     * Handles the full workflow:
     * 1. Read checkbox states for pre-print options
     * 2. Check if user disabled operations embedded in G-code
     * 3. If so, modify file (add skip params or comment out embedded ops) and print
     * 4. Otherwise, start print directly
     *
     * Print is started by calling Moonraker's print API. The PRINT_START macro
     * handles all pre-print operations (homing, heating, bed mesh, etc.) internally.
     *
     * @param filename File to print
     * @param current_path Current directory path
     * @param on_navigate_to_status Callback to navigate to print status panel
     * @param on_completion Optional callback for print completion
     */
    void start_print(const std::string& filename, const std::string& current_path,
                     NavigateToStatusCallback on_navigate_to_status,
                     PrintCompletionCallback on_completion = nullptr);

    /**
     * @brief Check if a print is currently being started
     *
     * Delegates to PrinterState::is_print_in_progress(). Returns true from
     * when start_print() is called until the print actually starts or fails.
     * Used to prevent double-tap issues.
     */
    [[nodiscard]] bool is_print_in_progress() const;

  private:
    // === Dependencies ===
    MoonrakerAPI* api_ = nullptr;
    PrinterState* printer_state_ = nullptr;

    // === Checkbox References ===
    lv_obj_t* bed_mesh_checkbox_ = nullptr;
    lv_obj_t* qgl_checkbox_ = nullptr;
    lv_obj_t* z_tilt_checkbox_ = nullptr;
    lv_obj_t* nozzle_clean_checkbox_ = nullptr;
    lv_obj_t* timelapse_checkbox_ = nullptr;

    // === Scan Cache ===
    std::optional<gcode::ScanResult> cached_scan_result_;
    std::string cached_scan_filename_;
    std::optional<size_t> cached_file_size_; ///< File size from Moonraker metadata

    // === Capability Cache ===
    mutable std::optional<PrintStartCapabilities> cached_capabilities_;
    mutable std::string cached_capabilities_printer_type_;

    /**
     * @brief Get cached printer capabilities, fetching if needed
     *
     * Caches PrinterDetector lookup to avoid repeated database parsing.
     * Cache is invalidated when printer type changes.
     */
    [[nodiscard]] const PrintStartCapabilities& get_cached_capabilities() const;

    // === Callbacks ===
    ScanCompleteCallback on_scan_complete_;
    MacroAnalysisCallback on_macro_analysis_complete_;

    // === PRINT_START Analysis Cache ===
    std::optional<helix::PrintStartAnalysis> macro_analysis_;
    bool macro_analysis_in_progress_ = false;

    // === Lifetime Guard for Async Callbacks ===
    // Shared pointer to track if this object is still alive when async callbacks execute.
    // Callbacks capture this shared_ptr; if *alive_guard_ is false, the callback bails out.
    std::shared_ptr<bool> alive_guard_ = std::make_shared<bool>(true);

    // === Connection Observer ===
    // Triggers macro analysis when printer connection becomes CONNECTED
    ObserverGuard connection_observer_;

    // === Internal Methods ===

    /**
     * @brief Collect operations that user wants to disable
     *
     * Compares checkbox states against cached scan result to identify
     * operations that are embedded in the file but disabled by user.
     */
    [[nodiscard]] std::vector<gcode::OperationType> collect_ops_to_disable() const;

    /**
     * @brief Download, modify, and print a G-code file
     *
     * Used when user disabled an operation that's embedded in the G-code
     * or when macro skip parameters need to be added to PRINT_START.
     *
     * @param file_path Full path to file relative to gcodes root
     * @param ops_to_disable Operations to comment out in the file
     * @param macro_skip_params Skip params to append to PRINT_START call
     * @param on_navigate_to_status Callback to navigate to print status panel
     */
    void modify_and_print(const std::string& file_path,
                          const std::vector<gcode::OperationType>& ops_to_disable,
                          const std::vector<std::pair<std::string, std::string>>& macro_skip_params,
                          NavigateToStatusCallback on_navigate_to_status);

    /**
     * @brief Unified streaming modification and print flow
     *
     * Downloads file to disk, applies streaming modification (file-to-file),
     * then uploads from disk. This is the single path for all G-code modifications,
     * avoiding memory spikes that cause TTC errors on constrained devices.
     *
     * If use_plugin is true and helix_print plugin is available, the plugin's
     * path-based API is used after upload for symlink creation and history patching.
     *
     * @param file_path Full path to original file relative to gcodes root
     * @param display_filename Filename for display purposes
     * @param ops_to_disable Operations to comment out in the file
     * @param macro_skip_params Skip params to append to PRINT_START call
     * @param mod_names Modification identifiers for tracking
     * @param on_navigate_to_status Callback to navigate to print status panel
     * @param use_plugin Whether to use helix_print plugin for print start
     */
    void modify_and_print_streaming(
        const std::string& file_path, const std::string& display_filename,
        const std::vector<gcode::OperationType>& ops_to_disable,
        const std::vector<std::pair<std::string, std::string>>& macro_skip_params,
        const std::vector<std::string>& mod_names, NavigateToStatusCallback on_navigate_to_status,
        bool use_plugin);

    /**
     * @brief Start print directly (no pre-print operations)
     */
    void start_print_directly(const std::string& filename,
                              NavigateToStatusCallback on_navigate_to_status,
                              PrintCompletionCallback on_completion);

    /**
     * @brief Helper to check if a checkbox is visible and unchecked
     */
    static bool is_option_disabled(lv_obj_t* checkbox);

    /**
     * @brief Static callback for connection state observer
     */
    static void on_connection_state_changed(lv_observer_t* observer, lv_subject_t* subject);

    /**
     * @brief Collect macro skip parameters based on user checkboxes and macro analysis
     *
     * Checks which macro operations the user disabled (checkbox unchecked) and
     * are controllable (have skip parameters). Returns the params to add to PRINT_START.
     *
     * @return Vector of (param_name, value) pairs like {"SKIP_BED_MESH", "1"}
     */
    [[nodiscard]] std::vector<std::pair<std::string, std::string>>
    collect_macro_skip_params() const;
};

} // namespace helix::ui
