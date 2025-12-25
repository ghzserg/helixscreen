// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_print_preparation_manager.h"

#include "ui_error_reporting.h"
#include "ui_panel_print_status.h"
#include "ui_update_queue.h"

#include "app_globals.h"
#include "memory_utils.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <set>

// Forward declaration for global print status panel (declared in ui_panel_print_status.h)
PrintStatusPanel& get_global_print_status_panel();

namespace helix::ui {

// ============================================================================
// Construction / Destruction
// ============================================================================

PrintPreparationManager::~PrintPreparationManager() {
    // Invalidate lifetime guard so pending async callbacks bail out safely
    if (alive_guard_) {
        *alive_guard_ = false;
    }

    // Cancel any running sequence
    if (pre_print_sequencer_) {
        pre_print_sequencer_.reset();
    }
}

// ============================================================================
// Setup
// ============================================================================

void PrintPreparationManager::set_dependencies(MoonrakerAPI* api, PrinterState* printer_state) {
    api_ = api;
    printer_state_ = printer_state;

    // Set up observer to trigger PRINT_START analysis when connected
    // This avoids making requests before the WebSocket connection is established
    if (printer_state_) {
        connection_observer_ = ObserverGuard(printer_state_->get_printer_connection_state_subject(),
                                             on_connection_state_changed, this);
    }
}

void PrintPreparationManager::on_connection_state_changed(lv_observer_t* observer,
                                                          lv_subject_t* subject) {
    auto* self = static_cast<PrintPreparationManager*>(lv_observer_get_user_data(observer));
    if (!self) {
        return;
    }

    int state = lv_subject_get_int(subject);
    if (state == static_cast<int>(ConnectionState::CONNECTED)) {
        self->analyze_print_start_macro();
    }
}

void PrintPreparationManager::set_checkboxes(lv_obj_t* bed_leveling, lv_obj_t* qgl,
                                             lv_obj_t* z_tilt, lv_obj_t* nozzle_clean,
                                             lv_obj_t* timelapse) {
    bed_leveling_checkbox_ = bed_leveling;
    qgl_checkbox_ = qgl;
    z_tilt_checkbox_ = z_tilt;
    nozzle_clean_checkbox_ = nozzle_clean;
    timelapse_checkbox_ = timelapse;
}

// ============================================================================
// PRINT_START Macro Analysis
// ============================================================================

void PrintPreparationManager::analyze_print_start_macro() {
    // Skip if analysis already in progress
    if (macro_analysis_in_progress_) {
        spdlog::debug("[PrintPreparationManager] PRINT_START analysis already in progress");
        return;
    }

    // Skip if we already have a cached result
    if (macro_analysis_.has_value()) {
        spdlog::debug("[PrintPreparationManager] Using cached PRINT_START analysis");
        if (on_macro_analysis_complete_) {
            on_macro_analysis_complete_(*macro_analysis_);
        }
        return;
    }

    if (!api_) {
        spdlog::warn("[PrintPreparationManager] Cannot analyze PRINT_START - no API connection");
        return;
    }

    // Check if WebSocket connection is actually established
    if (api_->get_client().get_connection_state() != ConnectionState::CONNECTED) {
        spdlog::debug("[PrintPreparationManager] Deferring PRINT_START analysis - not connected");
        return;
    }

    macro_analysis_in_progress_ = true;
    spdlog::info("[PrintPreparationManager] Starting PRINT_START macro analysis");

    auto* self = this;
    auto alive = alive_guard_; // Capture shared_ptr to detect destruction
    helix::PrintStartAnalyzer analyzer;

    analyzer.analyze(
        api_,
        // Success callback - NOTE: runs on HTTP thread
        [self, alive](const helix::PrintStartAnalysis& analysis) {
            spdlog::info("[PrintPreparationManager] PRINT_START analysis complete: {}",
                         analysis.summary());

            // Defer shared state updates to main LVGL thread
            struct MacroAnalysisData {
                PrintPreparationManager* mgr;
                std::shared_ptr<bool> alive_guard;
                helix::PrintStartAnalysis result;
            };
            ui_queue_update<MacroAnalysisData>(
                std::make_unique<MacroAnalysisData>(MacroAnalysisData{self, alive, analysis}),
                [](MacroAnalysisData* d) {
                    // Check if manager was destroyed before this callback executed
                    if (!d->alive_guard || !*d->alive_guard) {
                        spdlog::debug(
                            "[PrintPreparationManager] Skipping macro analysis callback - "
                            "manager destroyed");
                        return;
                    }
                    d->mgr->macro_analysis_ = d->result;
                    d->mgr->macro_analysis_in_progress_ = false;
                    if (d->mgr->on_macro_analysis_complete_) {
                        d->mgr->on_macro_analysis_complete_(d->result);
                    }
                });
        },
        // Error callback - NOTE: runs on HTTP thread
        [self, alive](const MoonrakerError& error) {
            spdlog::warn("[PrintPreparationManager] PRINT_START analysis failed: {}",
                         error.message);

            // Defer shared state updates to main LVGL thread
            struct MacroErrorData {
                PrintPreparationManager* mgr;
                std::shared_ptr<bool> alive_guard;
            };
            ui_queue_update<MacroErrorData>(
                std::make_unique<MacroErrorData>(MacroErrorData{self, alive}),
                [](MacroErrorData* d) {
                    // Check if manager was destroyed before this callback executed
                    if (!d->alive_guard || !*d->alive_guard) {
                        spdlog::debug("[PrintPreparationManager] Skipping macro error callback - "
                                      "manager destroyed");
                        return;
                    }
                    d->mgr->macro_analysis_in_progress_ = false;
                    // Create empty "not found" result
                    helix::PrintStartAnalysis not_found;
                    not_found.found = false;
                    d->mgr->macro_analysis_ = not_found;
                    if (d->mgr->on_macro_analysis_complete_) {
                        d->mgr->on_macro_analysis_complete_(not_found);
                    }
                });
        });
}

std::string PrintPreparationManager::format_macro_operations() const {
    if (!macro_analysis_.has_value() || !macro_analysis_->found ||
        macro_analysis_->operations.empty()) {
        return "";
    }

    // Build operation list, noting controllability
    std::string result = macro_analysis_->macro_name + " contains: ";
    bool first = true;

    for (const auto& op : macro_analysis_->operations) {
        // Skip homing (always present, not interesting to display)
        if (op.category == helix::PrintStartOpCategory::HOMING) {
            continue;
        }

        if (!first) {
            result += ", ";
        }
        first = false;

        // Get user-friendly name for the operation
        switch (op.category) {
        case helix::PrintStartOpCategory::BED_LEVELING:
            result += "Bed Mesh";
            break;
        case helix::PrintStartOpCategory::QGL:
            result += "QGL";
            break;
        case helix::PrintStartOpCategory::Z_TILT:
            result += "Z-Tilt";
            break;
        case helix::PrintStartOpCategory::NOZZLE_CLEAN:
            result += "Nozzle Clean";
            break;
        case helix::PrintStartOpCategory::CHAMBER_SOAK:
            result += "Chamber Soak";
            break;
        default:
            result += op.name;
            break;
        }

        // Add skip indicator if controllable
        if (op.has_skip_param) {
            result += " (skippable)";
        }
    }

    return first ? "" : result; // Return empty if we only had homing
}

bool PrintPreparationManager::is_macro_op_controllable(helix::PrintStartOpCategory category) const {
    if (!macro_analysis_.has_value() || !macro_analysis_->found) {
        return false;
    }

    const auto* op = macro_analysis_->get_operation(category);
    return op && op->has_skip_param;
}

std::string
PrintPreparationManager::get_macro_skip_param(helix::PrintStartOpCategory category) const {
    if (!macro_analysis_.has_value() || !macro_analysis_->found) {
        return "";
    }

    const auto* op = macro_analysis_->get_operation(category);
    if (op && op->has_skip_param) {
        return op->skip_param_name;
    }
    return "";
}

// ============================================================================
// G-code Scanning
// ============================================================================

void PrintPreparationManager::scan_file_for_operations(const std::string& filename,
                                                       const std::string& current_path) {
    // Skip if already cached for this file
    if (cached_scan_filename_ == filename && cached_scan_result_.has_value()) {
        spdlog::debug("[PrintPreparationManager] Using cached scan result for {}", filename);
        // Still notify callback with cached result
        if (on_scan_complete_) {
            on_scan_complete_(format_detected_operations());
        }
        return;
    }

    if (!api_) {
        spdlog::warn("[PrintPreparationManager] Cannot scan G-code - no API connection");
        if (on_scan_complete_) {
            on_scan_complete_("");
        }
        return;
    }

    // Build path for download
    std::string file_path = current_path.empty() ? filename : current_path + "/" + filename;

    spdlog::info("[PrintPreparationManager] Scanning G-code for embedded operations: {}",
                 file_path);

    auto* self = this;
    auto alive = alive_guard_; // Capture shared_ptr to detect destruction
    api_->download_file(
        "gcodes", file_path,
        // Success: parse content and cache result
        // NOTE: This callback runs on a background HTTP thread, so we must defer
        // shared state updates and LVGL calls to the main thread via lv_async_call
        [self, alive, filename](const std::string& content) {
            // Parse on background thread (safe - no shared state access)
            gcode::GCodeOpsDetector detector;
            auto scan_result = detector.scan_content(content);

            // Log on background thread (spdlog is thread-safe)
            if (scan_result.operations.empty()) {
                spdlog::debug("[PrintPreparationManager] No embedded operations found in {}",
                              filename);
            } else {
                spdlog::info("[PrintPreparationManager] Found {} embedded operations in {}:",
                             scan_result.operations.size(), filename);
                for (const auto& op : scan_result.operations) {
                    spdlog::info("[PrintPreparationManager]   - {} at line {} ({})",
                                 op.display_name(), op.line_number, op.raw_line.substr(0, 50));
                }
            }

            // Defer shared state updates to main LVGL thread
            struct ScanUpdateData {
                PrintPreparationManager* mgr;
                std::shared_ptr<bool> alive_guard;
                std::string filename;
                gcode::ScanResult result;
            };
            ui_queue_update<ScanUpdateData>(
                std::make_unique<ScanUpdateData>(
                    ScanUpdateData{self, alive, filename, scan_result}),
                [](ScanUpdateData* d) {
                    // Check if manager was destroyed before this callback executed
                    if (!d->alive_guard || !*d->alive_guard) {
                        spdlog::debug("[PrintPreparationManager] Skipping scan callback - "
                                      "manager destroyed");
                        return;
                    }
                    d->mgr->cached_scan_result_ = d->result;
                    d->mgr->cached_scan_filename_ = d->filename;
                    if (d->mgr->on_scan_complete_) {
                        d->mgr->on_scan_complete_(d->mgr->format_detected_operations());
                    }
                });
        },
        // Error: just log, don't block the UI
        // NOTE: Also runs on background thread
        [self, alive, filename](const MoonrakerError& error) {
            spdlog::warn("[PrintPreparationManager] Failed to scan G-code {}: {}", filename,
                         error.message);

            // Defer shared state updates to main LVGL thread
            struct ScanErrorData {
                PrintPreparationManager* mgr;
                std::shared_ptr<bool> alive_guard;
            };
            ui_queue_update<ScanErrorData>(
                std::make_unique<ScanErrorData>(ScanErrorData{self, alive}), [](ScanErrorData* d) {
                    // Check if manager was destroyed before this callback executed
                    if (!d->alive_guard || !*d->alive_guard) {
                        spdlog::debug("[PrintPreparationManager] Skipping scan error callback - "
                                      "manager destroyed");
                        return;
                    }
                    d->mgr->cached_scan_result_.reset();
                    d->mgr->cached_scan_filename_.clear();
                    if (d->mgr->on_scan_complete_) {
                        d->mgr->on_scan_complete_("");
                    }
                });
        });
}

std::string PrintPreparationManager::format_detected_operations() const {
    if (!cached_scan_result_.has_value() || cached_scan_result_->operations.empty()) {
        return "";
    }

    // Build unique list of operation names (some files may have duplicates)
    std::vector<std::string> op_names;
    std::set<gcode::OperationType> seen_types;

    for (const auto& op : cached_scan_result_->operations) {
        if (seen_types.find(op.type) == seen_types.end()) {
            seen_types.insert(op.type);
            op_names.push_back(op.display_name());
        }
    }

    if (op_names.empty()) {
        return "";
    }

    // Format as "Contains: Op1, Op2, Op3"
    std::string result = "Contains: ";
    for (size_t i = 0; i < op_names.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += op_names[i];
    }

    return result;
}

void PrintPreparationManager::clear_scan_cache() {
    cached_scan_result_.reset();
    cached_scan_filename_.clear();
    cached_file_size_.reset();
}

bool PrintPreparationManager::has_scan_result_for(const std::string& filename) const {
    return cached_scan_filename_ == filename && cached_scan_result_.has_value();
}

// ============================================================================
// Resource Safety
// ============================================================================

void PrintPreparationManager::set_cached_file_size(size_t size) {
    cached_file_size_ = size;
    spdlog::debug("[PrintPreparationManager] Cached file size: {} bytes ({:.1f} MB)", size,
                  static_cast<double>(size) / (1024.0 * 1024.0));
}

std::string PrintPreparationManager::get_temp_directory() const {
    // Delegate to global helper for consistent cache directory selection
    return get_helix_cache_dir("gcode_temp");
}

ModificationCapability PrintPreparationManager::check_modification_capability() const {
    ModificationCapability result;

    // Priority 1: If helix_print plugin is available, always safe (server-side modification)
    if (printer_state_ && printer_state_->service_has_helix_plugin()) {
        result.can_modify = true;
        result.has_plugin = true;
        result.has_disk_space = true; // N/A when using plugin
        result.reason = "Using server-side plugin";
        spdlog::debug("[PrintPreparationManager] Plugin available - modification always safe");
        return result;
    }

    // Priority 2: Check if we have enough disk space for streaming modification
    std::string temp_dir = get_temp_directory();
    if (temp_dir.empty()) {
        result.can_modify = false;
        result.has_plugin = false;
        result.has_disk_space = false;
        result.reason = "No writable temp directory available";
        spdlog::warn("[PrintPreparationManager] No temp directory - modification disabled");
        return result;
    }

    // Check available disk space
    std::error_code ec;
    std::filesystem::space_info space = std::filesystem::space(temp_dir, ec);
    if (ec) {
        result.can_modify = false;
        result.has_plugin = false;
        result.has_disk_space = false;
        result.reason = "Cannot check disk space";
        spdlog::warn("[PrintPreparationManager] Failed to check disk space: {}", ec.message());
        return result;
    }

    result.available_bytes = space.available;

    // Get file size (required for calculation)
    size_t file_size = cached_file_size_.value_or(0);

    if (file_size == 0) {
        // If we don't know file size, assume conservative 10MB (not 100MB)
        // This is more reasonable for resource-constrained devices
        file_size = 10 * 1024 * 1024;
        spdlog::debug("[PrintPreparationManager] Unknown file size, assuming 10MB");
    }

    // Calculate dynamic safety margin based on available disk space
    // - Use 10% of available space as margin, with min 2MB and max 20MB
    // - This scales appropriately: 20MB disk → 2MB margin, 200MB disk → 20MB margin
    constexpr size_t MIN_MARGIN_BYTES = 2 * 1024 * 1024;  // 2MB minimum
    constexpr size_t MAX_MARGIN_BYTES = 20 * 1024 * 1024; // 20MB maximum
    size_t dynamic_margin =
        std::clamp(static_cast<size_t>(space.available / 10), MIN_MARGIN_BYTES, MAX_MARGIN_BYTES);

    // Required space: 2x file size (download + modified) + dynamic margin
    result.required_bytes = (file_size * 2) + dynamic_margin;

    spdlog::debug("[PrintPreparationManager] Disk space calculation: file={}MB, margin={}MB, "
                  "required={}MB",
                  file_size / (1024 * 1024), dynamic_margin / (1024 * 1024),
                  result.required_bytes / (1024 * 1024));

    if (result.available_bytes >= result.required_bytes) {
        result.can_modify = true;
        result.has_plugin = false;
        result.has_disk_space = true;
        result.reason = "Streaming modification available";
        spdlog::debug("[PrintPreparationManager] Sufficient disk space: {:.1f} MB available, "
                      "{:.1f} MB required",
                      static_cast<double>(result.available_bytes) / (1024.0 * 1024.0),
                      static_cast<double>(result.required_bytes) / (1024.0 * 1024.0));
    } else {
        result.can_modify = false;
        result.has_plugin = false;
        result.has_disk_space = false;
        result.reason =
            fmt::format("Insufficient disk space: {:.0f} MB available, {:.0f} MB needed",
                        static_cast<double>(result.available_bytes) / (1024.0 * 1024.0),
                        static_cast<double>(result.required_bytes) / (1024.0 * 1024.0));
        spdlog::warn("[PrintPreparationManager] {}", result.reason);
    }

    return result;
}

// ============================================================================
// Print Execution
// ============================================================================

PrePrintOptions PrintPreparationManager::read_options_from_checkboxes() const {
    PrePrintOptions options;

    auto is_checked = [](lv_obj_t* checkbox) -> bool {
        return checkbox && lv_obj_has_state(checkbox, LV_STATE_CHECKED);
    };

    options.bed_leveling = is_checked(bed_leveling_checkbox_);
    options.qgl = is_checked(qgl_checkbox_);
    options.z_tilt = is_checked(z_tilt_checkbox_);
    options.nozzle_clean = is_checked(nozzle_clean_checkbox_);
    options.timelapse = is_checked(timelapse_checkbox_);

    return options;
}

void PrintPreparationManager::start_print(const std::string& filename,
                                          const std::string& current_path,
                                          NavigateToStatusCallback on_navigate_to_status,
                                          PreparingCallback on_preparing,
                                          PreparingProgressCallback on_progress,
                                          PrintCompletionCallback on_completion) {
    if (!api_) {
        spdlog::error("[PrintPreparationManager] Cannot start print - not connected to printer");
        NOTIFY_ERROR("Cannot start print: not connected to printer");
        if (on_completion) {
            on_completion(false, "Not connected to printer");
        }
        return;
    }

    // Prevent double-tap: reject if a print is already being started
    // This uses PrinterState's flag which is also checked by can_start_new_print()
    if (printer_state_ && printer_state_->is_print_in_progress()) {
        spdlog::warn(
            "[PrintPreparationManager] Ignoring duplicate print request - already in progress");
        return;
    }
    if (printer_state_) {
        printer_state_->set_print_in_progress(true);
    }

    // Wrap the completion callback to always clear the in-progress flag
    // This ensures the flag is cleared whether print succeeds or fails
    PrinterState* state_ptr = printer_state_;
    PrintCompletionCallback wrapped_completion =
        [state_ptr, on_completion](bool success, const std::string& error) {
            if (state_ptr) {
                state_ptr->set_print_in_progress(false);
            }
            if (on_completion) {
                on_completion(success, error);
            }
        };

    // Build full path for print
    std::string filename_to_print = current_path.empty() ? filename : current_path + "/" + filename;

    // Read checkbox states
    PrePrintOptions options = read_options_from_checkboxes();
    bool has_pre_print_ops =
        options.bed_leveling || options.qgl || options.z_tilt || options.nozzle_clean;

    spdlog::info("[PrintPreparationManager] Starting print: {} (pre-print: mesh={}, qgl={}, "
                 "z_tilt={}, clean={}, timelapse={})",
                 filename_to_print, options.bed_leveling, options.qgl, options.z_tilt,
                 options.nozzle_clean, options.timelapse);

    // Enable timelapse recording if requested (Moonraker-Timelapse plugin)
    if (options.timelapse) {
        api_->set_timelapse_enabled(
            true,
            []() { spdlog::info("[PrintPreparationManager] Timelapse enabled for this print"); },
            [](const MoonrakerError& err) {
                spdlog::error("[PrintPreparationManager] Failed to enable timelapse: {}",
                              err.message);
            });
    }

    // Check if user disabled operations that are embedded in the G-code file
    std::vector<gcode::OperationType> ops_to_disable = collect_ops_to_disable();

    // Check if user disabled operations that are in the PRINT_START macro
    // These need skip params appended to the PRINT_START call
    std::vector<std::pair<std::string, std::string>> macro_skip_params =
        collect_macro_skip_params();

    // Determine if we need to modify the G-code file
    bool needs_file_modification = !ops_to_disable.empty();
    bool needs_macro_params = !macro_skip_params.empty();

    if (needs_file_modification || needs_macro_params) {
        // SAFETY CHECK: Verify we can safely modify the G-code file
        // On resource-constrained devices (e.g., AD5M with 512MB RAM), loading large
        // G-code files into memory can exhaust resources and crash both Moonraker and Klipper.
        ModificationCapability capability = check_modification_capability();

        if (!capability.can_modify) {
            spdlog::warn("[PrintPreparationManager] Cannot modify G-code safely: {}",
                         capability.reason);
            spdlog::warn(
                "[PrintPreparationManager] Skipping modification - printing original file");
            // Clear modifications so we fall through to normal print path
            ops_to_disable.clear();
            macro_skip_params.clear();
            // Show user notification about skipped modification
            NOTIFY_WARNING("Cannot modify G-code: {}. Printing original file.", capability.reason);
        } else {
            spdlog::info("[PrintPreparationManager] Modifying G-code: {} file ops, {} macro params "
                         "(method: {})",
                         ops_to_disable.size(), macro_skip_params.size(),
                         capability.has_plugin ? "server-side plugin" : "streaming fallback");
            modify_and_print(filename_to_print, ops_to_disable, macro_skip_params,
                             on_navigate_to_status);
            return; // modify_and_print handles everything including navigation
        }
    }

    if (has_pre_print_ops) {
        execute_pre_print_sequence(filename_to_print, options, on_navigate_to_status, on_preparing,
                                   on_progress, wrapped_completion);
    } else {
        start_print_directly(filename_to_print, on_navigate_to_status, wrapped_completion);
    }
}

void PrintPreparationManager::cancel_preparation() {
    if (pre_print_sequencer_) {
        spdlog::info("[PrintPreparationManager] Cancelling pre-print sequence");
        pre_print_sequencer_.reset();
    }
}

bool PrintPreparationManager::is_print_in_progress() const {
    return printer_state_ && printer_state_->is_print_in_progress();
}

// ============================================================================
// Internal Methods
// ============================================================================

bool PrintPreparationManager::is_option_disabled(lv_obj_t* checkbox) {
    if (!checkbox)
        return false;
    bool is_visible = !lv_obj_has_flag(checkbox, LV_OBJ_FLAG_HIDDEN);
    bool is_checked = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
    return is_visible && !is_checked; // Visible but NOT checked = disabled
}

std::vector<gcode::OperationType> PrintPreparationManager::collect_ops_to_disable() const {
    std::vector<gcode::OperationType> ops_to_disable;

    if (!cached_scan_result_.has_value()) {
        return ops_to_disable; // No scan result, nothing to disable
    }

    // Check each operation type: if file has it embedded AND user disabled it
    if (is_option_disabled(bed_leveling_checkbox_) &&
        cached_scan_result_->has_operation(gcode::OperationType::BED_LEVELING)) {
        ops_to_disable.push_back(gcode::OperationType::BED_LEVELING);
        spdlog::debug("[PrintPreparationManager] User disabled bed leveling, file has it embedded");
    }

    if (is_option_disabled(qgl_checkbox_) &&
        cached_scan_result_->has_operation(gcode::OperationType::QGL)) {
        ops_to_disable.push_back(gcode::OperationType::QGL);
        spdlog::debug("[PrintPreparationManager] User disabled QGL, file has it embedded");
    }

    if (is_option_disabled(z_tilt_checkbox_) &&
        cached_scan_result_->has_operation(gcode::OperationType::Z_TILT)) {
        ops_to_disable.push_back(gcode::OperationType::Z_TILT);
        spdlog::debug("[PrintPreparationManager] User disabled Z-tilt, file has it embedded");
    }

    if (is_option_disabled(nozzle_clean_checkbox_) &&
        cached_scan_result_->has_operation(gcode::OperationType::NOZZLE_CLEAN)) {
        ops_to_disable.push_back(gcode::OperationType::NOZZLE_CLEAN);
        spdlog::debug("[PrintPreparationManager] User disabled nozzle clean, file has it embedded");
    }

    return ops_to_disable;
}

std::vector<std::pair<std::string, std::string>>
PrintPreparationManager::collect_macro_skip_params() const {
    // THREADING: This method reads macro_analysis_ and checkbox states.
    // Must be called from the main LVGL thread (same thread that updates these via
    // ui_async_call_safe callbacks). LVGL's single-threaded model ensures no races.

    std::vector<std::pair<std::string, std::string>> skip_params;

    // If no macro analysis, nothing to skip
    if (!macro_analysis_.has_value() || !macro_analysis_->found) {
        return skip_params;
    }

    // Check each controllable macro operation against user's checkbox state
    // Note: We only add skip params for operations that:
    // 1. Exist in PRINT_START macro (detected by analyzer)
    // 2. Have a skip parameter (controllable)
    // 3. User has disabled (checkbox unchecked)

    // Bed mesh
    if (is_macro_op_controllable(helix::PrintStartOpCategory::BED_LEVELING) &&
        is_option_disabled(bed_leveling_checkbox_)) {
        std::string param = get_macro_skip_param(helix::PrintStartOpCategory::BED_LEVELING);
        if (!param.empty()) {
            skip_params.emplace_back(param, "1");
            spdlog::debug("[PrintPreparationManager] Adding skip param for bed mesh: {}=1", param);
        }
    }

    // QGL
    if (is_macro_op_controllable(helix::PrintStartOpCategory::QGL) &&
        is_option_disabled(qgl_checkbox_)) {
        std::string param = get_macro_skip_param(helix::PrintStartOpCategory::QGL);
        if (!param.empty()) {
            skip_params.emplace_back(param, "1");
            spdlog::debug("[PrintPreparationManager] Adding skip param for QGL: {}=1", param);
        }
    }

    // Z-Tilt
    if (is_macro_op_controllable(helix::PrintStartOpCategory::Z_TILT) &&
        is_option_disabled(z_tilt_checkbox_)) {
        std::string param = get_macro_skip_param(helix::PrintStartOpCategory::Z_TILT);
        if (!param.empty()) {
            skip_params.emplace_back(param, "1");
            spdlog::debug("[PrintPreparationManager] Adding skip param for Z-tilt: {}=1", param);
        }
    }

    // Nozzle clean
    if (is_macro_op_controllable(helix::PrintStartOpCategory::NOZZLE_CLEAN) &&
        is_option_disabled(nozzle_clean_checkbox_)) {
        std::string param = get_macro_skip_param(helix::PrintStartOpCategory::NOZZLE_CLEAN);
        if (!param.empty()) {
            skip_params.emplace_back(param, "1");
            spdlog::debug("[PrintPreparationManager] Adding skip param for nozzle clean: {}=1",
                          param);
        }
    }

    if (!skip_params.empty()) {
        spdlog::info("[PrintPreparationManager] Collected {} macro skip params",
                     skip_params.size());
    }

    return skip_params;
}

void PrintPreparationManager::modify_and_print(
    const std::string& file_path, const std::vector<gcode::OperationType>& ops_to_disable,
    const std::vector<std::pair<std::string, std::string>>& macro_skip_params,
    NavigateToStatusCallback on_navigate_to_status) {
    if (!api_) {
        NOTIFY_ERROR("Cannot start print - not connected to printer");
        if (printer_state_)
            printer_state_->set_print_in_progress(false);
        return;
    }

    if (!cached_scan_result_.has_value()) {
        spdlog::error("[PrintPreparationManager] modify_and_print called without scan result");
        NOTIFY_ERROR("Internal error: no scan result");
        if (printer_state_)
            printer_state_->set_print_in_progress(false);
        return;
    }

    spdlog::info("[PrintPreparationManager] Modifying G-code: {} file ops to disable, {} macro "
                 "skip params",
                 ops_to_disable.size(), macro_skip_params.size());

    // Extract just the filename for display purposes
    size_t last_slash = file_path.rfind('/');
    std::string display_filename =
        (last_slash != std::string::npos) ? file_path.substr(last_slash + 1) : file_path;

    // Build modification identifiers for plugin
    std::vector<std::string> mod_names;
    for (const auto& op : ops_to_disable) {
        mod_names.push_back(gcode::GCodeOpsDetector::operation_type_name(op) + "_disabled");
    }
    // Add skip params to mod_names for tracking
    for (const auto& [param_name, param_value] : macro_skip_params) {
        mod_names.push_back("skip_" + param_name);
    }

    // Check if helix_print plugin is available FIRST (before downloading)
    // Plugin path uses server-side modification, so memory isn't a concern
    if (printer_state_ && printer_state_->service_has_helix_plugin()) {
        spdlog::info("[PrintPreparationManager] Using helix_print plugin for modified print");
        modify_and_print_via_plugin(file_path, display_filename, ops_to_disable, macro_skip_params,
                                    mod_names, on_navigate_to_status);
    } else {
        // STREAMING PATH: Download to disk, modify file-to-file, upload from disk
        // This avoids loading the entire G-code file into memory
        spdlog::info("[PrintPreparationManager] Using streaming fallback (helix_print plugin not "
                     "available)");
        modify_and_print_streaming(file_path, display_filename, ops_to_disable, macro_skip_params,
                                   on_navigate_to_status);
    }
}

void PrintPreparationManager::modify_and_print_via_plugin(
    const std::string& file_path, const std::string& display_filename,
    const std::vector<gcode::OperationType>& ops_to_disable,
    const std::vector<std::pair<std::string, std::string>>& macro_skip_params,
    const std::vector<std::string>& mod_names, NavigateToStatusCallback on_navigate_to_status) {
    auto* self = this;
    auto alive = alive_guard_;              // Capture for lifetime checking in async callbacks
    auto scan_result = cached_scan_result_; // Copy for lambda capture

    // Validate scan_result before proceeding
    if (!scan_result.has_value()) {
        NOTIFY_ERROR("Cannot modify G-code: scan result not available");
        if (printer_state_)
            printer_state_->set_print_in_progress(false);
        return;
    }

    // Download file to memory (acceptable for plugin path since server does the work)
    api_->download_file(
        "gcodes", file_path,
        [self, alive, file_path, display_filename, ops_to_disable, macro_skip_params, mod_names,
         on_navigate_to_status, scan_result](const std::string& content) {
            // Check if manager was destroyed before this callback executed
            if (!alive || !*alive) {
                spdlog::debug("[PrintPreparationManager] Skipping plugin download callback - "
                              "manager destroyed");
                return;
            }
            // Apply modifications in memory (safe - no shared state)
            gcode::GCodeFileModifier modifier;

            // Disable file-embedded operations (comment them out)
            modifier.disable_operations(*scan_result, ops_to_disable);

            // Add skip parameters to PRINT_START call (if any)
            if (!macro_skip_params.empty()) {
                if (modifier.add_print_start_skip_params(*scan_result, macro_skip_params)) {
                    spdlog::info("[PrintPreparationManager] Added {} skip params to PRINT_START",
                                 macro_skip_params.size());
                } else {
                    spdlog::warn("[PrintPreparationManager] Could not add skip params - "
                                 "PRINT_START not found in G-code");
                }
            }

            std::string modified_content = modifier.apply_to_content(content);
            if (modified_content.empty()) {
                NOTIFY_ERROR("Failed to modify G-code file");
                if (self->printer_state_)
                    self->printer_state_->set_print_in_progress(false);
                return;
            }

            // Use helix_print plugin (single API call, server handles the rest)
            self->api_->start_modified_print(
                file_path, modified_content, mod_names,
                // Success callback - runs on HTTP thread, must defer LVGL ops
                [self, on_navigate_to_status, display_filename](const ModifiedPrintResult& result) {
                    spdlog::info("[PrintPreparationManager] Print started via helix_print "
                                 "plugin: {} -> {}",
                                 result.original_filename, result.print_filename);

                    // Clear in-progress flag on success
                    if (self->printer_state_)
                        self->printer_state_->set_print_in_progress(false);

                    // Defer LVGL operations to main thread
                    struct PrintStartedData {
                        std::string filename;
                        NavigateToStatusCallback navigate_cb;
                    };
                    ui_queue_update<PrintStartedData>(
                        std::make_unique<PrintStartedData>(
                            PrintStartedData{display_filename, on_navigate_to_status}),
                        [](PrintStartedData* d) {
                            get_global_print_status_panel().set_thumbnail_source(d->filename);
                            if (d->navigate_cb) {
                                d->navigate_cb();
                            }
                        });
                },
                [self, file_path](const MoonrakerError& error) {
                    // NOTIFY_ERROR is already thread-safe
                    NOTIFY_ERROR("Failed to start modified print: {}", error.message);
                    LOG_ERROR_INTERNAL(
                        "[PrintPreparationManager] helix_print plugin error for {}: {}", file_path,
                        error.message);
                    if (self->printer_state_)
                        self->printer_state_->set_print_in_progress(false);
                });
        },
        [self, file_path](const MoonrakerError& error) {
            // NOTIFY_ERROR is already thread-safe
            NOTIFY_ERROR("Failed to download G-code for modification: {}", error.message);
            LOG_ERROR_INTERNAL("[PrintPreparationManager] Download failed for {}: {}", file_path,
                               error.message);
            if (self->printer_state_)
                self->printer_state_->set_print_in_progress(false);
        });
}

void PrintPreparationManager::modify_and_print_streaming(
    const std::string& file_path, const std::string& display_filename,
    const std::vector<gcode::OperationType>& ops_to_disable,
    const std::vector<std::pair<std::string, std::string>>& macro_skip_params,
    NavigateToStatusCallback on_navigate_to_status) {
    auto* self = this;
    auto alive = alive_guard_;              // Capture for lifetime checking in async callbacks
    auto scan_result = cached_scan_result_; // Copy for lambda capture

    // Validate scan_result before proceeding (SERIOUS-3 fix)
    if (!scan_result.has_value()) {
        NOTIFY_ERROR("Cannot modify G-code: scan result not available");
        if (printer_state_)
            printer_state_->set_print_in_progress(false);
        return;
    }

    // Get temp directory for intermediate files
    std::string temp_dir = get_temp_directory();
    if (temp_dir.empty()) {
        NOTIFY_ERROR("Cannot modify G-code: no temp directory available");
        if (printer_state_)
            printer_state_->set_print_in_progress(false);
        return;
    }

    // Generate unique temp file paths
    auto timestamp = std::to_string(std::time(nullptr));
    std::string local_download_path = temp_dir + "/helix_download_" + timestamp + ".gcode";
    std::string remote_temp_path = ".helix_temp/modified_" + timestamp + "_" + display_filename;

    spdlog::info("[PrintPreparationManager] Streaming modification: downloading to {}",
                 local_download_path);

    // Step 1: Download file to disk (streaming, not memory)
    api_->download_file_to_path(
        "gcodes", file_path, local_download_path,
        // Download success - NOTE: runs on HTTP thread
        [self, alive, file_path, display_filename, ops_to_disable, macro_skip_params, scan_result,
         local_download_path, remote_temp_path,
         on_navigate_to_status](const std::string& /*dest_path*/) {
            // Check if manager was destroyed before this callback executed
            if (!alive || !*alive) {
                spdlog::debug("[PrintPreparationManager] Skipping streaming download callback - "
                              "manager destroyed");
                // Clean up download file since we're bailing out
                std::error_code ec;
                std::filesystem::remove(local_download_path, ec);
                return;
            }
            spdlog::info("[PrintPreparationManager] Downloaded to disk, applying streaming "
                         "modification");

            // Step 2: Apply streaming modification (file-to-file, minimal memory)
            gcode::GCodeFileModifier modifier;

            // Disable file-embedded operations (comment them out)
            modifier.disable_operations(*scan_result, ops_to_disable);

            // Add skip parameters to PRINT_START call (if any)
            if (!macro_skip_params.empty()) {
                if (modifier.add_print_start_skip_params(*scan_result, macro_skip_params)) {
                    spdlog::info("[PrintPreparationManager] Added {} skip params to PRINT_START",
                                 macro_skip_params.size());
                } else {
                    spdlog::warn("[PrintPreparationManager] Could not add skip params - "
                                 "PRINT_START not found in G-code");
                }
            }

            auto result = modifier.apply_streaming(local_download_path);

            // Clean up download file (no longer needed)
            std::error_code ec;
            std::filesystem::remove(local_download_path, ec);
            if (ec) {
                spdlog::warn("[PrintPreparationManager] Failed to clean up download file: {}",
                             ec.message());
            }

            if (!result.success) {
                NOTIFY_ERROR("Failed to modify G-code: {}", result.error_message);
                if (self->printer_state_)
                    self->printer_state_->set_print_in_progress(false);
                return;
            }

            spdlog::info("[PrintPreparationManager] Modification complete ({} lines modified), "
                         "uploading {}",
                         result.lines_modified, result.modified_path);

            // Step 3: Upload modified file from disk
            std::string modified_path = result.modified_path; // Copy for lambda
            self->api_->upload_file_from_path(
                "gcodes", remote_temp_path, modified_path,
                // Upload success - NOTE: runs on HTTP thread, defer LVGL ops
                [self, alive, modified_path, display_filename, remote_temp_path,
                 on_navigate_to_status]() {
                    // Clean up local modified file (safe - filesystem op, always do it)
                    std::error_code ec;
                    std::filesystem::remove(modified_path, ec);
                    if (ec) {
                        spdlog::warn(
                            "[PrintPreparationManager] Failed to clean up modified file: {}",
                            ec.message());
                    }

                    // Check if manager was destroyed before this callback executed
                    if (!alive || !*alive) {
                        spdlog::debug("[PrintPreparationManager] Skipping upload callback - "
                                      "manager destroyed");
                        return;
                    }

                    spdlog::info("[PrintPreparationManager] Modified file uploaded, starting "
                                 "print");

                    // Step 4: Start print with modified file
                    self->api_->start_print(
                        remote_temp_path,
                        // Print success - defer LVGL ops to main thread
                        [self, on_navigate_to_status, display_filename]() {
                            spdlog::info("[PrintPreparationManager] Print started with "
                                         "modified G-code (streaming, original: {})",
                                         display_filename);

                            // Clear in-progress flag on success
                            if (self->printer_state_) {
                                self->printer_state_->set_print_in_progress(false);
                            }

                            // Defer LVGL operations to main thread
                            struct PrintStartedData {
                                std::string filename;
                                NavigateToStatusCallback navigate_cb;
                            };
                            ui_queue_update<PrintStartedData>(
                                std::make_unique<PrintStartedData>(
                                    PrintStartedData{display_filename, on_navigate_to_status}),
                                [](PrintStartedData* d) {
                                    get_global_print_status_panel().set_thumbnail_source(
                                        d->filename);
                                    if (d->navigate_cb) {
                                        d->navigate_cb();
                                    }
                                });
                        },
                        // Print error - also try to clean up remote temp file
                        [self, alive, remote_temp_path](const MoonrakerError& error) {
                            NOTIFY_ERROR("Failed to start print: {}", error.message);
                            LOG_ERROR_INTERNAL(
                                "[PrintPreparationManager] Print start failed for {}: {}",
                                remote_temp_path, error.message);

                            // Clear in-progress flag on error
                            if (self->printer_state_) {
                                self->printer_state_->set_print_in_progress(false);
                            }

                            // Check if manager still valid before cleanup
                            if (!alive || !*alive) {
                                spdlog::debug("[PrintPreparationManager] Skipping remote "
                                              "cleanup - manager destroyed");
                                return;
                            }

                            // Clean up remote temp file on failure
                            // Moonraker's delete_file requires full path including root
                            std::string full_path = "gcodes/" + remote_temp_path;
                            self->api_->delete_file(
                                full_path,
                                []() {
                                    spdlog::debug("[PrintPreparationManager] Cleaned up "
                                                  "remote temp file after print failure");
                                },
                                [](const MoonrakerError& /*del_err*/) {
                                    // Ignore delete errors - file may not exist or cleanup
                                    // isn't critical
                                });
                        });
                },
                // Upload error - clean up local file
                [self, modified_path](const MoonrakerError& error) {
                    // Clean up local file even on error (safe - filesystem op)
                    std::error_code ec;
                    std::filesystem::remove(modified_path, ec);

                    NOTIFY_ERROR("Failed to upload modified G-code: {}", error.message);
                    LOG_ERROR_INTERNAL("[PrintPreparationManager] Upload failed: {}",
                                       error.message);
                    if (self->printer_state_)
                        self->printer_state_->set_print_in_progress(false);
                });
        },
        // Download error - clean up partial download
        [self, file_path, local_download_path](const MoonrakerError& error) {
            // Clean up partial download if any (safe - filesystem op)
            std::error_code ec;
            std::filesystem::remove(local_download_path, ec);

            NOTIFY_ERROR("Failed to download G-code for modification: {}", error.message);
            LOG_ERROR_INTERNAL("[PrintPreparationManager] Download failed for {}: {}", file_path,
                               error.message);
            if (self->printer_state_)
                self->printer_state_->set_print_in_progress(false);
        });
}

void PrintPreparationManager::execute_pre_print_sequence(
    const std::string& filename, const PrePrintOptions& options,
    NavigateToStatusCallback on_navigate_to_status, PreparingCallback on_preparing,
    PreparingProgressCallback on_progress, PrintCompletionCallback on_completion) {
    // Create command sequencer for pre-print operations
    pre_print_sequencer_ =
        std::make_unique<gcode::CommandSequencer>(api_->get_client(), *api_, *printer_state_);

    // Always home first if doing any pre-print operations
    pre_print_sequencer_->add_operation(gcode::OperationType::HOMING, {}, "Homing");

    // Add selected operations in logical order
    if (options.qgl) {
        pre_print_sequencer_->add_operation(gcode::OperationType::QGL, {}, "Quad Gantry Level");
    }
    if (options.z_tilt) {
        pre_print_sequencer_->add_operation(gcode::OperationType::Z_TILT, {}, "Z-Tilt Adjust");
    }
    if (options.bed_leveling) {
        pre_print_sequencer_->add_operation(gcode::OperationType::BED_LEVELING, {},
                                            "Bed Mesh Calibration");
    }
    if (options.nozzle_clean) {
        pre_print_sequencer_->add_operation(gcode::OperationType::NOZZLE_CLEAN, {}, "Clean Nozzle");
    }

    // Add the actual print start as the final operation
    gcode::OperationParams print_params;
    print_params.filename = filename;
    pre_print_sequencer_->add_operation(gcode::OperationType::START_PRINT, print_params,
                                        "Starting Print");

    int queue_size = static_cast<int>(pre_print_sequencer_->queue_size());

    // Navigate to print status panel in "Preparing" state
    if (on_navigate_to_status) {
        on_navigate_to_status();
    }

    // Initialize the preparing state
    auto& status_panel = get_global_print_status_panel();
    status_panel.set_preparing("Starting...", 0, queue_size);

    auto* self = this;

    // Start the sequence
    pre_print_sequencer_->start(
        // Progress callback - update the Preparing UI
        [on_preparing, on_progress](const std::string& op_name, int step, int total,
                                    float progress) {
            spdlog::info("[PrintPreparationManager] Pre-print progress: {} ({}/{}, {:.0f}%)",
                         op_name, step, total, progress * 100.0f);

            // Update PrintStatusPanel's preparing state
            auto& status_panel = get_global_print_status_panel();
            status_panel.set_preparing(op_name, step, total);
            status_panel.set_preparing_progress(progress);

            if (on_preparing) {
                on_preparing(op_name, step, total);
            }
            if (on_progress) {
                on_progress(progress);
            }
        },
        // Completion callback
        [self, on_completion](bool success, const std::string& error) {
            auto& status_panel = get_global_print_status_panel();

            if (success) {
                spdlog::info(
                    "[PrintPreparationManager] Pre-print sequence complete, print started");
                // Transition from Preparing → Printing state
                status_panel.end_preparing(true);
            } else {
                NOTIFY_ERROR("Pre-print failed: {}", error);
                LOG_ERROR_INTERNAL("[PrintPreparationManager] Pre-print sequence failed: {}",
                                   error);
                // Transition from Preparing → Idle state
                status_panel.end_preparing(false);
            }

            // Clean up sequencer
            self->pre_print_sequencer_.reset();

            if (on_completion) {
                on_completion(success, error);
            }
        });
}

void PrintPreparationManager::start_print_directly(const std::string& filename,
                                                   NavigateToStatusCallback on_navigate_to_status,
                                                   PrintCompletionCallback on_completion) {
    api_->start_print(
        filename,
        // Success callback
        [on_navigate_to_status, on_completion]() {
            spdlog::info("[PrintPreparationManager] Print started successfully");

            if (on_navigate_to_status) {
                on_navigate_to_status();
            }

            if (on_completion) {
                on_completion(true, "");
            }
        },
        // Error callback
        [filename, on_completion](const MoonrakerError& error) {
            NOTIFY_ERROR("Failed to start print: {}", error.message);
            LOG_ERROR_INTERNAL("[PrintPreparationManager] Print start failed for {}: {} ({})",
                               filename, error.message, error.get_type_string());

            if (on_completion) {
                on_completion(false, error.message);
            }
        });
}

} // namespace helix::ui
