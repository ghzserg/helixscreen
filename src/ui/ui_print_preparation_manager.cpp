// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_print_preparation_manager.h"

#include "ui_busy_overlay.h"
#include "ui_error_reporting.h"
#include "ui_panel_print_status.h"
#include "ui_update_queue.h"

#include "active_print_media_manager.h"
#include "app_globals.h"
#include "config.h"
#include "memory_utils.h"
#include "printer_detector.h"
#include "wizard_config_paths.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <map>
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

void PrintPreparationManager::set_checkboxes(lv_obj_t* bed_mesh, lv_obj_t* qgl, lv_obj_t* z_tilt,
                                             lv_obj_t* nozzle_clean, lv_obj_t* timelapse) {
    bed_mesh_checkbox_ = bed_mesh;
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
        case helix::PrintStartOpCategory::BED_MESH:
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
        case helix::PrintStartOpCategory::BED_LEVEL:
            result += "Bed Level";
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

helix::ParameterSemantic
PrintPreparationManager::get_macro_param_semantic(helix::PrintStartOpCategory category) const {
    if (!macro_analysis_.has_value() || !macro_analysis_->found) {
        return helix::ParameterSemantic::OPT_OUT; // Default assumption
    }

    const auto* op = macro_analysis_->get_operation(category);
    if (op && op->has_skip_param) {
        return op->param_semantic;
    }
    return helix::ParameterSemantic::OPT_OUT; // Default assumption
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

    // Use partial download - only first 200KB is needed for preamble scanning
    // (thumbnails + slicer metadata + START_PRINT call + any early G-code ops)
    // This avoids downloading multi-MB files just to scan the first few hundred lines
    constexpr size_t SCAN_DOWNLOAD_LIMIT = 200 * 1024; // 200KB

    api_->download_file_partial(
        "gcodes", file_path, SCAN_DOWNLOAD_LIMIT,
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

std::string PrintPreparationManager::format_preprint_steps() const {
    // Unified operation categories with friendly names and skip status
    struct UnifiedOp {
        std::string friendly_name;
        bool can_skip = false;
    };

    // Use a map to deduplicate by category key
    std::map<std::string, UnifiedOp> ops;

    // Friendly name mapping for macro operations
    auto get_macro_friendly_name = [](helix::PrintStartOpCategory cat) -> std::string {
        switch (cat) {
        case helix::PrintStartOpCategory::BED_MESH:
            return "Bed mesh";
        case helix::PrintStartOpCategory::QGL:
            return "Quad gantry leveling";
        case helix::PrintStartOpCategory::Z_TILT:
            return "Z-tilt adjustment";
        case helix::PrintStartOpCategory::NOZZLE_CLEAN:
            return "Nozzle cleaning";
        case helix::PrintStartOpCategory::PRIMING:
            return "Nozzle priming";
        case helix::PrintStartOpCategory::SKEW_CORRECT:
            return "Skew correction";
        case helix::PrintStartOpCategory::CHAMBER_SOAK:
            return "Chamber heat soak";
        case helix::PrintStartOpCategory::BED_LEVEL:
            return "Bed leveling";
        default:
            return "";
        }
    };

    // Category key for deduplication
    auto get_category_key = [](helix::PrintStartOpCategory cat) -> std::string {
        switch (cat) {
        case helix::PrintStartOpCategory::BED_MESH:
            return "bed_mesh";
        case helix::PrintStartOpCategory::QGL:
            return "qgl";
        case helix::PrintStartOpCategory::Z_TILT:
            return "z_tilt";
        case helix::PrintStartOpCategory::NOZZLE_CLEAN:
            return "nozzle_clean";
        case helix::PrintStartOpCategory::PRIMING:
            return "priming";
        case helix::PrintStartOpCategory::SKEW_CORRECT:
            return "skew_correct";
        case helix::PrintStartOpCategory::CHAMBER_SOAK:
            return "chamber_soak";
        case helix::PrintStartOpCategory::BED_LEVEL:
            return "bed_level";
        default:
            return "";
        }
    };

    // 1. Add operations from PRINT_START macro analysis
    if (macro_analysis_.has_value() && macro_analysis_->found) {
        for (const auto& op : macro_analysis_->operations) {
            // Skip homing (always happens, not interesting)
            if (op.category == helix::PrintStartOpCategory::HOMING) {
                continue;
            }

            std::string key = get_category_key(op.category);
            if (key.empty()) {
                continue;
            }

            std::string name = get_macro_friendly_name(op.category);
            if (name.empty()) {
                name = op.name; // Fallback to raw name
            }

            spdlog::debug("[PrintPreparationManager] From MACRO: {} (key={}, skip={})", name, key,
                          op.has_skip_param);

            // If already in map, update skip status (macro can add skip capability)
            if (ops.find(key) != ops.end()) {
                if (op.has_skip_param) {
                    ops[key].can_skip = true;
                }
            } else {
                ops[key] = UnifiedOp{name, op.has_skip_param};
            }
        }
    }

    // 2. Add operations from G-code file scan (these are already in the file)
    if (cached_scan_result_.has_value()) {
        for (const auto& op : cached_scan_result_->operations) {
            // Map file operation types to category keys
            std::string key;
            std::string name;

            switch (op.type) {
            case gcode::OperationType::BED_MESH:
                key = "bed_mesh";
                name = "Bed mesh";
                break;
            case gcode::OperationType::QGL:
                key = "qgl";
                name = "Quad gantry leveling";
                break;
            case gcode::OperationType::Z_TILT:
                key = "z_tilt";
                name = "Z-tilt adjustment";
                break;
            case gcode::OperationType::NOZZLE_CLEAN:
                key = "nozzle_clean";
                name = "Nozzle cleaning";
                break;
            case gcode::OperationType::CHAMBER_SOAK:
                key = "chamber_soak";
                name = "Chamber heat soak";
                break;
            case gcode::OperationType::PURGE_LINE:
                key = "priming";
                name = "Nozzle priming";
                break;
            case gcode::OperationType::SKEW_CORRECT:
                key = "skew_correct";
                name = "Skew correction";
                break;
            case gcode::OperationType::BED_LEVEL:
                key = "bed_level";
                name = "Bed leveling";
                break;
            default:
                continue; // Skip homing, start_print, etc.
            }

            spdlog::debug("[PrintPreparationManager] From FILE: {} (key={}, raw={})", name, key,
                          op.display_name());

            // File operations are embedded in G-code, not skippable via macro
            // Only add if not already present from macro
            if (ops.find(key) == ops.end()) {
                ops[key] = UnifiedOp{name, false};
            }
        }
    }

    // 3. Add operations from printer capability database
    // This ensures operations are shown even if not detected in file/macro
    Config* config = Config::get_instance();
    if (config) {
        std::string printer_type = config->get<std::string>(helix::wizard::PRINTER_TYPE, "");
        if (!printer_type.empty()) {
            PrintStartCapabilities caps =
                PrinterDetector::get_print_start_capabilities(printer_type);

            if (!caps.empty()) {
                // Map capability keys to friendly names
                static const std::map<std::string, std::string> cap_friendly_names = {
                    {"bed_leveling", "Bed mesh"},          {"qgl", "Quad gantry leveling"},
                    {"z_tilt", "Z-tilt adjustment"},       {"nozzle_clean", "Nozzle cleaning"},
                    {"priming", "Nozzle priming"},         {"skew_correct", "Skew correction"},
                    {"chamber_soak", "Chamber heat soak"},
                };

                for (const auto& [cap_key, cap_info] : caps.params) {
                    // Only add if not already present from macro/file detection
                    if (ops.find(cap_key) == ops.end()) {
                        auto name_it = cap_friendly_names.find(cap_key);
                        std::string name =
                            (name_it != cap_friendly_names.end()) ? name_it->second : cap_key;

                        // Capabilities from database are skippable via macro params
                        ops[cap_key] = UnifiedOp{name, true};

                        spdlog::debug("[PrintPreparationManager] From CAPABILITY DB: {} (key={})",
                                      name, cap_key);
                    }
                }
            }
        }
    }

    // 4. Format as bulleted list
    if (ops.empty()) {
        return "";
    }

    std::string result;
    for (const auto& [key, op] : ops) {
        if (!result.empty()) {
            result += "\n";
        }
        result += "• " + op.friendly_name;
        if (op.can_skip) {
            result += " (optional)";
        }
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

    // Pre-print modifications require the HelixPrint plugin to keep print history clean.
    // Without the plugin, modified files show up as ugly temp file names in Moonraker's
    // job history (e.g., ".helix_temp/modified_1766807545_filename.gcode").
    // The plugin handles this by creating symlinks and patching history metadata.
    if (printer_state_ && printer_state_->service_has_helix_plugin()) {
        result.can_modify = true;
        result.has_plugin = true;
        result.has_disk_space = true;
        result.reason = "Using server-side plugin";
        spdlog::debug("[PrintPreparationManager] Plugin available - modifications enabled");
        return result;
    }

    // No plugin = no modifications. This prevents print history clutter.
    result.can_modify = false;
    result.has_plugin = false;
    result.has_disk_space = false;
    result.reason = "Requires HelixPrint plugin";
    spdlog::debug("[PrintPreparationManager] No plugin - modifications disabled");
    return result;
}

// ============================================================================
// Print Execution
// ============================================================================

PrePrintOptions PrintPreparationManager::read_options_from_checkboxes() const {
    PrePrintOptions options;

    // Only count as checked if checkbox exists, is VISIBLE, AND has checked state.
    // Hidden checkboxes (for unsupported capabilities like QGL on non-Voron printers)
    // should NOT contribute to pre-print operations.
    // NOTE: The hidden flag may be on the parent row container, not the checkbox itself.
    auto is_checked = [](lv_obj_t* checkbox) -> bool {
        if (!checkbox)
            return false;
        // Check if checkbox or any ancestor is hidden
        lv_obj_t* obj = checkbox;
        while (obj) {
            if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
                return false; // Hidden checkbox = not checked
            }
            obj = lv_obj_get_parent(obj);
        }
        return lv_obj_has_state(checkbox, LV_STATE_CHECKED);
    };

    options.bed_mesh = is_checked(bed_mesh_checkbox_);
    options.qgl = is_checked(qgl_checkbox_);
    options.z_tilt = is_checked(z_tilt_checkbox_);
    options.nozzle_clean = is_checked(nozzle_clean_checkbox_);
    options.timelapse = is_checked(timelapse_checkbox_);

    return options;
}

void PrintPreparationManager::start_print(const std::string& filename,
                                          const std::string& current_path,
                                          NavigateToStatusCallback on_navigate_to_status,
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

    // Read checkbox states for logging and timelapse
    PrePrintOptions options = read_options_from_checkboxes();

    spdlog::info(
        "[PrintPreparationManager] Starting print: {} (pre-print options: mesh={}, qgl={}, "
        "z_tilt={}, clean={}, timelapse={})",
        filename_to_print, options.bed_mesh, options.qgl, options.z_tilt, options.nozzle_clean,
        options.timelapse);

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

    // CHECKED checkboxes = trust the macro to handle the operation (do nothing extra)
    // UNCHECKED checkboxes = already handled above via file modification or skip params
    // No need for manual G-code execution - just start the print
    start_print_directly(filename_to_print, on_navigate_to_status, wrapped_completion);
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
    if (is_option_disabled(bed_mesh_checkbox_) &&
        cached_scan_result_->has_operation(gcode::OperationType::BED_MESH)) {
        ops_to_disable.push_back(gcode::OperationType::BED_MESH);
        spdlog::debug("[PrintPreparationManager] User disabled bed mesh, file has it embedded");
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

    // PRIORITY 1: Check printer capability database for known native params
    // If we have capabilities for this printer type, use them directly instead of
    // relying on macro analysis. This is faster and more reliable.
    Config* config = Config::get_instance();
    if (config) {
        std::string printer_type = config->get<std::string>(helix::wizard::PRINTER_TYPE, "");
        if (!printer_type.empty()) {
            PrintStartCapabilities caps =
                PrinterDetector::get_print_start_capabilities(printer_type);

            if (!caps.empty()) {
                spdlog::info("[PrintPreparationManager] Using capability database for '{}' ({} "
                             "capabilities)",
                             printer_type, caps.params.size());

                // Bed mesh - use database param if available
                // Use get_capability() for safe access without exception risk
                if (auto* cap = caps.get_capability("bed_mesh");
                    cap && is_option_disabled(bed_mesh_checkbox_)) {
                    skip_params.emplace_back(cap->param, cap->skip_value);
                    spdlog::debug("[PrintPreparationManager] Using database param: {}={}",
                                  cap->param, cap->skip_value);
                }

                // QGL - use database param if available
                if (auto* cap = caps.get_capability("qgl");
                    cap && is_option_disabled(qgl_checkbox_)) {
                    skip_params.emplace_back(cap->param, cap->skip_value);
                    spdlog::debug("[PrintPreparationManager] Using database param: {}={}",
                                  cap->param, cap->skip_value);
                }

                // Z Tilt - use database param if available
                if (auto* cap = caps.get_capability("z_tilt");
                    cap && is_option_disabled(z_tilt_checkbox_)) {
                    skip_params.emplace_back(cap->param, cap->skip_value);
                    spdlog::debug("[PrintPreparationManager] Using database param: {}={}",
                                  cap->param, cap->skip_value);
                }

                // Nozzle clean - use database param if available
                if (auto* cap = caps.get_capability("nozzle_clean");
                    cap && is_option_disabled(nozzle_clean_checkbox_)) {
                    skip_params.emplace_back(cap->param, cap->skip_value);
                    spdlog::debug("[PrintPreparationManager] Using database param: {}={}",
                                  cap->param, cap->skip_value);
                }

                // Priming - use database param if available
                // Note: We don't have a priming checkbox yet, but AD5M supports it
                // Future: Add priming_checkbox_ and use it here

                // If we found capabilities, return them and skip macro analysis
                if (!skip_params.empty()) {
                    spdlog::info(
                        "[PrintPreparationManager] Using {} params from capability database",
                        skip_params.size());
                    return skip_params;
                }
            }
        }
    }

    // PRIORITY 2: Fall back to macro analysis
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
    if (is_macro_op_controllable(helix::PrintStartOpCategory::BED_MESH) &&
        is_option_disabled(bed_mesh_checkbox_)) {
        std::string param = get_macro_skip_param(helix::PrintStartOpCategory::BED_MESH);
        if (!param.empty()) {
            auto semantic = get_macro_param_semantic(helix::PrintStartOpCategory::BED_MESH);
            // OPT_OUT (SKIP_*): "1" means skip. OPT_IN (PERFORM_*): "0" means don't do.
            std::string value = (semantic == helix::ParameterSemantic::OPT_OUT) ? "1" : "0";
            skip_params.emplace_back(param, value);
            spdlog::debug("[PrintPreparationManager] Adding skip param for bed mesh: {}={}", param,
                          value);
        }
    }

    // QGL
    if (is_macro_op_controllable(helix::PrintStartOpCategory::QGL) &&
        is_option_disabled(qgl_checkbox_)) {
        std::string param = get_macro_skip_param(helix::PrintStartOpCategory::QGL);
        if (!param.empty()) {
            auto semantic = get_macro_param_semantic(helix::PrintStartOpCategory::QGL);
            // OPT_OUT (SKIP_*): "1" means skip. OPT_IN (PERFORM_*): "0" means don't do.
            std::string value = (semantic == helix::ParameterSemantic::OPT_OUT) ? "1" : "0";
            skip_params.emplace_back(param, value);
            spdlog::debug("[PrintPreparationManager] Adding skip param for QGL: {}={}", param,
                          value);
        }
    }

    // Z-Tilt
    if (is_macro_op_controllable(helix::PrintStartOpCategory::Z_TILT) &&
        is_option_disabled(z_tilt_checkbox_)) {
        std::string param = get_macro_skip_param(helix::PrintStartOpCategory::Z_TILT);
        if (!param.empty()) {
            auto semantic = get_macro_param_semantic(helix::PrintStartOpCategory::Z_TILT);
            // OPT_OUT (SKIP_*): "1" means skip. OPT_IN (PERFORM_*): "0" means don't do.
            std::string value = (semantic == helix::ParameterSemantic::OPT_OUT) ? "1" : "0";
            skip_params.emplace_back(param, value);
            spdlog::debug("[PrintPreparationManager] Adding skip param for Z-tilt: {}={}", param,
                          value);
        }
    }

    // Nozzle clean
    if (is_macro_op_controllable(helix::PrintStartOpCategory::NOZZLE_CLEAN) &&
        is_option_disabled(nozzle_clean_checkbox_)) {
        std::string param = get_macro_skip_param(helix::PrintStartOpCategory::NOZZLE_CLEAN);
        if (!param.empty()) {
            auto semantic = get_macro_param_semantic(helix::PrintStartOpCategory::NOZZLE_CLEAN);
            // OPT_OUT (SKIP_*): "1" means skip. OPT_IN (PERFORM_*): "0" means don't do.
            std::string value = (semantic == helix::ParameterSemantic::OPT_OUT) ? "1" : "0";
            skip_params.emplace_back(param, value);
            spdlog::debug("[PrintPreparationManager] Adding skip param for nozzle clean: {}={}",
                          param, value);
        }
    }

    if (!skip_params.empty()) {
        spdlog::info("[PrintPreparationManager] Collected {} macro skip params (via analysis)",
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

    // UNIFIED STREAMING PATH: Always use streaming to avoid memory spikes
    // 1. Download to disk (streaming)
    // 2. Modify on disk (file-to-file, minimal memory)
    // 3. Upload modified file to server
    // 4. If plugin available: use path-based API for symlink/history patching
    //    Otherwise: use standard start_print
    //
    // This prevents TTC errors on memory-constrained devices like AD5M (512MB RAM)
    // by never loading the entire G-code file into memory.
    bool has_plugin = printer_state_ && printer_state_->service_has_helix_plugin();
    spdlog::info("[PrintPreparationManager] Using unified streaming modification flow (plugin: {})",
                 has_plugin);
    modify_and_print_streaming(file_path, display_filename, ops_to_disable, macro_skip_params,
                               mod_names, on_navigate_to_status, has_plugin);
}

void PrintPreparationManager::modify_and_print_streaming(
    const std::string& file_path, const std::string& display_filename,
    const std::vector<gcode::OperationType>& ops_to_disable,
    const std::vector<std::pair<std::string, std::string>>& macro_skip_params,
    const std::vector<std::string>& mod_names, NavigateToStatusCallback on_navigate_to_status,
    bool use_plugin) {
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

    // Show busy overlay (will appear after 300ms grace period if operation takes that long)
    BusyOverlay::show("Preparing print...");

    // Progress callback for download - NOTE: called from HTTP thread
    auto download_progress = [](size_t received, size_t total) {
        float pct = (total > 0)
                        ? (100.0f * static_cast<float>(received) / static_cast<float>(total))
                        : 0.0f;
        ui_async_call(
            [](void* data) {
                auto pct_val = static_cast<float>(reinterpret_cast<uintptr_t>(data)) / 100.0f;
                BusyOverlay::set_progress("Downloading", pct_val);
            },
            reinterpret_cast<void*>(static_cast<uintptr_t>(pct * 100.0f)));
    };

    // Step 1: Download file to disk (streaming, not memory)
    api_->download_file_to_path(
        "gcodes", file_path, local_download_path,
        // Download success - NOTE: runs on HTTP thread
        [self, alive, file_path, display_filename, ops_to_disable, macro_skip_params, mod_names,
         scan_result, local_download_path, remote_temp_path, on_navigate_to_status,
         use_plugin](const std::string& /*dest_path*/) {
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
                [self, alive, modified_path, display_filename, remote_temp_path, file_path,
                 mod_names, on_navigate_to_status, use_plugin]() {
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
                                 "print (use_plugin={})",
                                 use_plugin);

                    // Step 4: Start print with modified file
                    // If plugin available, use path-based API for symlink/history patching
                    // Otherwise, use standard start_print

                    // Define common callbacks to avoid code duplication
                    auto on_print_success = [self, on_navigate_to_status, display_filename,
                                             file_path]() {
                        spdlog::info("[PrintPreparationManager] Print started with "
                                     "modified G-code (streaming, original: {})",
                                     display_filename);

                        // Clear in-progress flag on success
                        if (self->printer_state_) {
                            self->printer_state_->set_print_in_progress(false);
                        }

                        // Defer LVGL operations to main thread
                        struct PrintStartedData {
                            std::string display_filename; // For display purposes
                            std::string original_path;    // Full path for metadata lookup
                            NavigateToStatusCallback navigate_cb;
                        };
                        ui_queue_update<PrintStartedData>(
                            std::make_unique<PrintStartedData>(PrintStartedData{
                                display_filename, file_path, on_navigate_to_status}),
                            [](PrintStartedData* d) {
                                // Hide overlay now that print is starting
                                BusyOverlay::hide();

                                // Set thumbnail source override for modified temp files
                                // Uses original_path (e.g., usb/flowrate_0.gcode) for metadata
                                // lookup
                                // - Panel: local gcode viewer and thumbnail display
                                // - Manager: shared subjects for HomePanel
                                get_global_print_status_panel().set_thumbnail_source(
                                    d->original_path);
                                helix::get_active_print_media_manager().set_thumbnail_source(
                                    d->original_path);

                                if (d->navigate_cb) {
                                    d->navigate_cb();
                                }
                            });
                    };

                    auto on_print_error = [self, alive,
                                           remote_temp_path](const MoonrakerError& error) {
                        // Hide overlay on error (defer to main thread)
                        ui_async_call([](void*) { BusyOverlay::hide(); }, nullptr);

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
                            spdlog::debug(
                                "[PrintPreparationManager] Skipping remote cleanup - manager "
                                "destroyed");
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
                    };

                    if (use_plugin) {
                        // Plugin path: Use path-based API (v2.0)
                        // The plugin will create symlink, patch history, and start print
                        self->api_->start_modified_print(
                            file_path,        // Original filename for history
                            remote_temp_path, // Path to uploaded modified file
                            mod_names,
                            [on_print_success](const ModifiedPrintResult& result) {
                                spdlog::info("[PrintPreparationManager] Plugin accepted print: "
                                             "{} -> {}",
                                             result.original_filename, result.print_filename);
                                on_print_success();
                            },
                            on_print_error);
                    } else {
                        // Standard path: Just start print with modified file
                        self->api_->start_print(remote_temp_path, on_print_success, on_print_error);
                    }
                },
                // Upload error - clean up local file
                [self, modified_path](const MoonrakerError& error) {
                    // Hide overlay on error (defer to main thread)
                    ui_async_call([](void*) { BusyOverlay::hide(); }, nullptr);

                    // Clean up local file even on error (safe - filesystem op)
                    std::error_code ec;
                    std::filesystem::remove(modified_path, ec);

                    NOTIFY_ERROR("Failed to upload modified G-code: {}", error.message);
                    LOG_ERROR_INTERNAL("[PrintPreparationManager] Upload failed: {}",
                                       error.message);
                    if (self->printer_state_)
                        self->printer_state_->set_print_in_progress(false);
                },
                // Upload progress callback
                [](size_t sent, size_t total) {
                    float pct =
                        (total > 0)
                            ? (100.0f * static_cast<float>(sent) / static_cast<float>(total))
                            : 0.0f;
                    ui_async_call(
                        [](void* data) {
                            auto pct_val =
                                static_cast<float>(reinterpret_cast<uintptr_t>(data)) / 100.0f;
                            BusyOverlay::set_progress("Uploading", pct_val);
                        },
                        reinterpret_cast<void*>(static_cast<uintptr_t>(pct * 100.0f)));
                });
        },
        // Download error - clean up partial download
        [self, file_path, local_download_path](const MoonrakerError& error) {
            // Hide overlay on error (defer to main thread)
            ui_async_call([](void*) { BusyOverlay::hide(); }, nullptr);

            // Clean up partial download if any (safe - filesystem op)
            std::error_code ec;
            std::filesystem::remove(local_download_path, ec);

            NOTIFY_ERROR("Failed to download G-code for modification: {}", error.message);
            LOG_ERROR_INTERNAL("[PrintPreparationManager] Download failed for {}: {}", file_path,
                               error.message);
            if (self->printer_state_)
                self->printer_state_->set_print_in_progress(false);
        },
        // Download progress callback
        download_progress);
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
