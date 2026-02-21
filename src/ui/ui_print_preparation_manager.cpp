// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_print_preparation_manager.h"

#include "ui_busy_overlay.h"
#include "ui_error_reporting.h"
#include "ui_panel_print_status.h"
#include "ui_update_queue.h"

#include "active_print_media_manager.h"
#include "app_globals.h"
#include "memory_utils.h"
#include "observer_factory.h"
#include "operation_registry.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <set>

// Forward declaration for global print status panel (declared in ui_panel_print_status.h)
PrintStatusPanel& get_global_print_status_panel();

namespace helix::ui {

// Bring helix:: types into scope for cleaner code
using helix::CapabilityOrigin;
using helix::OperationCategory;

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
// Capability Cache Helper
// ============================================================================

const PrintStartCapabilities& PrintPreparationManager::get_cached_capabilities() const {
    // Delegate to PrinterState which owns the capability cache
    if (printer_state_) {
        return printer_state_->get_print_start_capabilities();
    }

    // Return empty capabilities if PrinterState not set
    static const PrintStartCapabilities empty_caps;
    return empty_caps;
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
        connection_observer_ = helix::ui::observe_int_sync<PrintPreparationManager>(
            printer_state_->get_printer_connection_state_subject(), this,
            [](PrintPreparationManager* self, int state) {
                if (state == static_cast<int>(ConnectionState::CONNECTED)) {
                    self->analyze_print_start_macro();
                }
            });
    }
}

void PrintPreparationManager::set_preprint_subjects(lv_subject_t* bed_mesh, lv_subject_t* qgl,
                                                    lv_subject_t* z_tilt,
                                                    lv_subject_t* nozzle_clean,
                                                    lv_subject_t* purge_line,
                                                    lv_subject_t* timelapse) {
    preprint_bed_mesh_subject_ = bed_mesh;
    preprint_qgl_subject_ = qgl;
    preprint_z_tilt_subject_ = z_tilt;
    preprint_nozzle_clean_subject_ = nozzle_clean;
    preprint_purge_line_subject_ = purge_line;
    preprint_timelapse_subject_ = timelapse;
    spdlog::debug("[PrintPreparationManager] Pre-print subjects set");
}

void PrintPreparationManager::set_preprint_visibility_subjects(lv_subject_t* can_show_bed_mesh,
                                                               lv_subject_t* can_show_qgl,
                                                               lv_subject_t* can_show_z_tilt,
                                                               lv_subject_t* can_show_nozzle_clean,
                                                               lv_subject_t* can_show_purge_line,
                                                               lv_subject_t* can_show_timelapse) {
    can_show_bed_mesh_subject_ = can_show_bed_mesh;
    can_show_qgl_subject_ = can_show_qgl;
    can_show_z_tilt_subject_ = can_show_z_tilt;
    can_show_nozzle_clean_subject_ = can_show_nozzle_clean;
    can_show_purge_line_subject_ = can_show_purge_line;
    can_show_timelapse_subject_ = can_show_timelapse;
    spdlog::debug("[PrintPreparationManager] Visibility subjects set");
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

    // Reset retry counter when starting fresh
    macro_analysis_retry_count_ = 0;

    // Delegate to internal implementation
    analyze_print_start_macro_internal();
}

void PrintPreparationManager::analyze_print_start_macro_internal() {
    if (!api_) {
        spdlog::warn("[PrintPreparationManager] Cannot analyze PRINT_START - no API connection");
        return;
    }

    // Check if WebSocket connection is actually established
    if (api_->get_connection_state() != ConnectionState::CONNECTED) {
        spdlog::debug("[PrintPreparationManager] Deferring PRINT_START analysis - not connected");
        return;
    }

    macro_analysis_in_progress_ = true;
    spdlog::debug(
        "[PrintPreparationManager] Starting PRINT_START macro analysis (attempt {} of {})",
        macro_analysis_retry_count_ + 1, MAX_MACRO_ANALYSIS_RETRIES + 1);

    auto* self = this;
    auto alive = alive_guard_; // Capture shared_ptr to detect destruction
    helix::PrintStartAnalyzer analyzer;

    analyzer.analyze(
        api_,
        // Success callback - NOTE: runs on HTTP thread
        [self, alive](const helix::PrintStartAnalysis& analysis) {
            spdlog::debug("[PrintPreparationManager] PRINT_START analysis complete: {}",
                          analysis.summary());

            // Defer shared state updates to main LVGL thread
            struct MacroAnalysisData {
                PrintPreparationManager* mgr;
                std::shared_ptr<bool> alive_guard;
                helix::PrintStartAnalysis result;
            };
            helix::ui::queue_update<MacroAnalysisData>(
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
            spdlog::warn("[PrintPreparationManager] PRINT_START analysis failed (attempt {}): {}",
                         self->macro_analysis_retry_count_ + 1, error.message);

            // Defer shared state updates to main LVGL thread
            struct MacroErrorData {
                PrintPreparationManager* mgr;
                std::shared_ptr<bool> alive_guard;
            };
            helix::ui::queue_update<MacroErrorData>(
                std::make_unique<MacroErrorData>(MacroErrorData{self, alive}),
                [](MacroErrorData* d) {
                    // Check if manager was destroyed before this callback executed
                    if (!d->alive_guard || !*d->alive_guard) {
                        spdlog::debug("[PrintPreparationManager] Skipping macro error callback - "
                                      "manager destroyed");
                        return;
                    }

                    auto* mgr = d->mgr;

                    // Check if we should retry
                    if (mgr->macro_analysis_retry_count_ < MAX_MACRO_ANALYSIS_RETRIES) {
                        mgr->macro_analysis_retry_count_++;
                        // Exponential backoff: 1s, 2s
                        int delay_ms = 1000 * (1 << (mgr->macro_analysis_retry_count_ - 1));

                        spdlog::info("[PrintPreparationManager] Retrying PRINT_START analysis in "
                                     "{}ms (attempt {} of {})",
                                     delay_ms, mgr->macro_analysis_retry_count_ + 1,
                                     MAX_MACRO_ANALYSIS_RETRIES + 1);

                        // Schedule retry via LVGL timer
                        // Capture alive_guard to avoid use-after-free if manager destroyed
                        struct RetryTimerData {
                            PrintPreparationManager* mgr;
                            std::shared_ptr<bool> alive_guard;
                        };
                        auto timer_data_ptr = std::make_unique<RetryTimerData>(
                            RetryTimerData{mgr, mgr->alive_guard_});

                        lv_timer_t* retry_timer = lv_timer_create(
                            [](lv_timer_t* timer) {
                                // Wrap raw pointer in unique_ptr for RAII cleanup
                                std::unique_ptr<RetryTimerData> data(
                                    static_cast<RetryTimerData*>(lv_timer_get_user_data(timer)));
                                // Check alive_guard BEFORE dereferencing manager
                                if (data && data->alive_guard && *data->alive_guard) {
                                    data->mgr->analyze_print_start_macro_internal();
                                }
                                // data automatically freed via ~unique_ptr()
                                lv_timer_delete(timer);
                            },
                            delay_ms, timer_data_ptr.release());
                        lv_timer_set_repeat_count(retry_timer, 1);
                        return;
                    }

                    // Final failure - notify user
                    spdlog::error(
                        "[PrintPreparationManager] PRINT_START analysis failed after {} attempts",
                        MAX_MACRO_ANALYSIS_RETRIES + 1);
                    NOTIFY_ERROR("Could not analyze PRINT_START macro. Some print options may be "
                                 "unavailable.");

                    // Set empty result
                    mgr->macro_analysis_in_progress_ = false;
                    helix::PrintStartAnalysis not_found;
                    not_found.found = false;
                    mgr->macro_analysis_ = not_found;
                    if (mgr->on_macro_analysis_complete_) {
                        mgr->on_macro_analysis_complete_(not_found);
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

        // Get user-friendly name for the operation from shared definitions
        const char* name = helix::category_name(op.category);
        if (name && op.category != helix::PrintStartOpCategory::UNKNOWN) {
            result += name;
        } else {
            result += op.name;
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
// CapabilityMatrix Integration
// ============================================================================

CapabilityMatrix PrintPreparationManager::build_capability_matrix() const {
    CapabilityMatrix matrix;

    // Layer 1: Database capabilities (highest priority)
    const auto& db_caps = get_cached_capabilities();
    if (!db_caps.empty()) {
        matrix.add_from_database(db_caps);
    }

    // Layer 2: Macro analysis (medium priority)
    if (macro_analysis_ && macro_analysis_->found) {
        matrix.add_from_macro_analysis(*macro_analysis_);
    }

    // Layer 3: File scan (lowest priority)
    if (cached_scan_result_) {
        matrix.add_from_file_scan(*cached_scan_result_);
    }

    return matrix;
}

void PrintPreparationManager::set_macro_analysis(const helix::PrintStartAnalysis& analysis) {
    macro_analysis_ = analysis;
}

void PrintPreparationManager::set_cached_scan_result(const gcode::ScanResult& scan,
                                                     const std::string& filename) {
    cached_scan_result_ = scan;
    cached_scan_filename_ = filename;
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
            helix::ui::queue_update<ScanUpdateData>(
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
            helix::ui::queue_update<ScanErrorData>(
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
    // Uses the shared category_name() from operation_patterns.h
    auto get_macro_friendly_name = [](helix::PrintStartOpCategory cat) -> std::string {
        return helix::category_name(cat);
    };

    // Category key for deduplication
    // Uses the shared category_key() from operation_patterns.h
    auto get_category_key = [](helix::PrintStartOpCategory cat) -> std::string {
        return helix::category_key(cat);
    };

    // Priority order matches collect_macro_skip_params() for consistency:
    // 1. Printer capability database (authoritative for known printers)
    // 2. PRINT_START macro analysis (detected from printer config)
    // 3. G-code file scan (embedded operations)

    // 1. Add operations from printer capability database (highest priority)
    // Database entries are curated and provide correct parameter names
    const auto& caps = get_cached_capabilities();
    if (!caps.empty()) {
        for (const auto& [cap_key, cap_info] : caps.params) {
            // Look up friendly name from OperationRegistry (for controllable ops)
            // or fall back to the cap_key if not found
            std::string name;
            if (auto info = helix::OperationRegistry::get_by_key(cap_key)) {
                name = info->friendly_name;
            } else {
                // Non-controllable operations (priming, chamber_soak, skew_correct)
                // still need friendly names - use hardcoded fallback for these edge cases
                // since they're not in OperationRegistry
                if (cap_key == "priming") {
                    name = "Nozzle priming";
                } else if (cap_key == "chamber_soak") {
                    name = helix::category_name(helix::OperationCategory::CHAMBER_SOAK);
                } else if (cap_key == "skew_correct") {
                    name = helix::category_name(helix::OperationCategory::SKEW_CORRECT);
                } else {
                    name = cap_key;
                }
            }

            // Capabilities from database are skippable via macro params
            ops[cap_key] = UnifiedOp{name, true};

            spdlog::debug("[PrintPreparationManager] From CAPABILITY DB: {} (key={})", name,
                          cap_key);
        }
    }

    // 2. Add operations from PRINT_START macro analysis
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

            // If already in map from database, update skip status if macro adds capability
            if (ops.find(key) != ops.end()) {
                if (op.has_skip_param) {
                    ops[key].can_skip = true;
                }
                spdlog::debug("[PrintPreparationManager] From MACRO (merged): {} (key={}, skip={})",
                              name, key, op.has_skip_param);
            } else {
                ops[key] = UnifiedOp{name, op.has_skip_param};
                spdlog::debug("[PrintPreparationManager] From MACRO: {} (key={}, skip={})", name,
                              key, op.has_skip_param);
            }
        }
    }

    // 3. Add operations from G-code file scan (these are already in the file)
    if (cached_scan_result_.has_value()) {
        for (const auto& op : cached_scan_result_->operations) {
            // Skip operations we don't want to display (homing, start_print, unknown)
            if (op.type == gcode::OperationType::HOMING ||
                op.type == gcode::OperationType::START_PRINT ||
                op.type == gcode::OperationType::UNKNOWN) {
                continue;
            }

            // Get key and name from OperationRegistry for controllable operations,
            // or use category_key/category_name for non-controllable ones
            std::string key;
            std::string name;

            if (auto info = helix::OperationRegistry::get(op.type)) {
                // Controllable operation - use registry data
                key = info->capability_key;
                name = info->friendly_name;
            } else {
                // Non-controllable operation (CHAMBER_SOAK, SKEW_CORRECT, BED_LEVEL)
                // Use shared category_key/category_name functions
                key = helix::category_key(op.type);
                name = helix::category_name(op.type);
            }

            // Special case: PURGE_LINE maps to "priming" key in capability database
            if (op.type == gcode::OperationType::PURGE_LINE) {
                key = "priming";
                name = "Nozzle priming";
            }

            // File operations are embedded in G-code, not skippable via macro
            // Only add if not already present from database/macro
            if (ops.find(key) == ops.end()) {
                ops[key] = UnifiedOp{name, false};
                spdlog::debug("[PrintPreparationManager] From FILE: {} (key={}, raw={})", name, key,
                              op.display_name());
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

PrePrintOptions PrintPreparationManager::read_options_from_subjects() const {
    PrePrintOptions options;

    options.bed_mesh = (get_option_state(can_show_bed_mesh_subject_, preprint_bed_mesh_subject_) ==
                        PrePrintOptionState::ENABLED);
    options.qgl = (get_option_state(can_show_qgl_subject_, preprint_qgl_subject_) ==
                   PrePrintOptionState::ENABLED);
    options.z_tilt = (get_option_state(can_show_z_tilt_subject_, preprint_z_tilt_subject_) ==
                      PrePrintOptionState::ENABLED);
    options.nozzle_clean =
        (get_option_state(can_show_nozzle_clean_subject_, preprint_nozzle_clean_subject_) ==
         PrePrintOptionState::ENABLED);
    options.purge_line =
        (get_option_state(can_show_purge_line_subject_, preprint_purge_line_subject_) ==
         PrePrintOptionState::ENABLED);
    options.timelapse =
        (get_option_state(can_show_timelapse_subject_, preprint_timelapse_subject_) ==
         PrePrintOptionState::ENABLED);

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
    PrePrintOptions options = read_options_from_subjects();

    spdlog::debug(
        "[PrintPreparationManager] Starting print: {} (pre-print options: mesh={}, qgl={}, "
        "z_tilt={}, clean={}, timelapse={})",
        filename_to_print, options.bed_mesh, options.qgl, options.z_tilt, options.nozzle_clean,
        options.timelapse);

    // Enable timelapse recording if requested (Moonraker-Timelapse plugin)
    if (options.timelapse) {
        api_->timelapse().set_timelapse_enabled(
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

PrePrintOptionState PrintPreparationManager::get_option_state(lv_subject_t* visibility_subject,
                                                              lv_subject_t* checked_subject) const {
    // Hidden = not applicable (e.g., plugin not installed, printer lacks capability)
    if (visibility_subject && lv_subject_get_int(visibility_subject) == 0) {
        return PrePrintOptionState::NOT_APPLICABLE;
    }
    // Visible + checked = user wants this operation
    if (checked_subject && lv_subject_get_int(checked_subject) == 1) {
        return PrePrintOptionState::ENABLED;
    }
    // Visible + unchecked = user explicitly disabled this
    if (checked_subject && lv_subject_get_int(checked_subject) == 0) {
        return PrePrintOptionState::DISABLED;
    }
    // No checked subject = can't determine
    return PrePrintOptionState::NOT_APPLICABLE;
}

std::pair<lv_subject_t*, lv_subject_t*>
PrintPreparationManager::get_subjects_for_category(OperationCategory cat) const {
    switch (cat) {
    case OperationCategory::BED_MESH:
        return {can_show_bed_mesh_subject_, preprint_bed_mesh_subject_};
    case OperationCategory::QGL:
        return {can_show_qgl_subject_, preprint_qgl_subject_};
    case OperationCategory::Z_TILT:
        return {can_show_z_tilt_subject_, preprint_z_tilt_subject_};
    case OperationCategory::NOZZLE_CLEAN:
        return {can_show_nozzle_clean_subject_, preprint_nozzle_clean_subject_};
    case OperationCategory::PURGE_LINE:
        return {can_show_purge_line_subject_, preprint_purge_line_subject_};
    // Note: We don't have timelapse in OperationCategory - it's separate
    default:
        return {nullptr, nullptr};
    }
}

bool PrintPreparationManager::is_operation_visible(OperationCategory cat) const {
    auto [visibility_subject, checked_subject] = get_subjects_for_category(cat);

    // If we don't have a visibility subject for this category, consider it not applicable
    if (!visibility_subject) {
        return false;
    }

    // Visible if value is 1 (or non-zero)
    return lv_subject_get_int(visibility_subject) != 0;
}

bool PrintPreparationManager::is_option_disabled_from_subject(OperationCategory cat) const {
    auto [visibility_subject, checked_subject] = get_subjects_for_category(cat);

    // If we don't have a checkbox subject for this category, can't determine state
    if (!checked_subject) {
        return false;
    }

    // Disabled = unchecked (value 0)
    return lv_subject_get_int(checked_subject) == 0;
}

std::optional<OperationCapabilityResult>
PrintPreparationManager::lookup_operation_capability(OperationCategory cat) const {
    // 1. Check if we have subjects for this category
    auto [visibility_subject, checked_subject] = get_subjects_for_category(cat);

    // If we don't have subjects for this category, can't determine user intent
    if (!visibility_subject || !checked_subject) {
        return std::nullopt;
    }

    // 2. Check if operation is hidden (not applicable to this printer)
    if (!is_operation_visible(cat)) {
        return std::nullopt;
    }

    // 3. Check if operation is enabled (user wants it to run)
    if (!is_option_disabled_from_subject(cat)) {
        return std::nullopt;
    }

    // 4. Get skip param from CapabilityMatrix
    auto matrix = build_capability_matrix();
    auto skip_param = matrix.get_skip_param(cat);

    if (!skip_param.has_value()) {
        return std::nullopt; // No capability source for this operation
    }

    // 5. Build result
    OperationCapabilityResult result;
    result.should_skip = true;
    result.param_name = skip_param->first;
    result.skip_value = skip_param->second;

    // Get source info
    auto source = matrix.get_best_source(cat);
    if (source) {
        result.source = source->origin;
    }

    return result;
}

std::vector<gcode::OperationType> PrintPreparationManager::collect_ops_to_disable() const {
    std::vector<gcode::OperationType> ops_to_disable;

    if (!cached_scan_result_.has_value()) {
        return ops_to_disable; // No scan result, nothing to disable
    }

    // Check each operation type: if file has it embedded AND user explicitly disabled it
    // Note: hidden (NOT_APPLICABLE) options are NOT candidates for disabling
    if (get_option_state(can_show_bed_mesh_subject_, preprint_bed_mesh_subject_) ==
            PrePrintOptionState::DISABLED &&
        cached_scan_result_->has_operation(gcode::OperationType::BED_MESH)) {
        ops_to_disable.push_back(gcode::OperationType::BED_MESH);
        spdlog::debug("[PrintPreparationManager] User disabled bed mesh, file has it embedded");
    }

    if (get_option_state(can_show_qgl_subject_, preprint_qgl_subject_) ==
            PrePrintOptionState::DISABLED &&
        cached_scan_result_->has_operation(gcode::OperationType::QGL)) {
        ops_to_disable.push_back(gcode::OperationType::QGL);
        spdlog::debug("[PrintPreparationManager] User disabled QGL, file has it embedded");
    }

    if (get_option_state(can_show_z_tilt_subject_, preprint_z_tilt_subject_) ==
            PrePrintOptionState::DISABLED &&
        cached_scan_result_->has_operation(gcode::OperationType::Z_TILT)) {
        ops_to_disable.push_back(gcode::OperationType::Z_TILT);
        spdlog::debug("[PrintPreparationManager] User disabled Z-tilt, file has it embedded");
    }

    if (get_option_state(can_show_nozzle_clean_subject_, preprint_nozzle_clean_subject_) ==
            PrePrintOptionState::DISABLED &&
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
    const auto& caps = get_cached_capabilities();
    if (!caps.empty()) {
        spdlog::info("[PrintPreparationManager] Using capability database ({} capabilities)",
                     caps.params.size());

        // Bed mesh - use database param if available
        // Use get_capability() for safe access without exception risk
        // Only add skip param if user explicitly DISABLED (not if hidden/not applicable)
        if (auto* cap = caps.get_capability("bed_mesh");
            cap && get_option_state(can_show_bed_mesh_subject_, preprint_bed_mesh_subject_) ==
                       PrePrintOptionState::DISABLED) {
            skip_params.emplace_back(cap->param, cap->skip_value);
            spdlog::debug("[PrintPreparationManager] Using database param: {}={}", cap->param,
                          cap->skip_value);
        }

        // QGL - use database param if available
        if (auto* cap = caps.get_capability("qgl");
            cap && get_option_state(can_show_qgl_subject_, preprint_qgl_subject_) ==
                       PrePrintOptionState::DISABLED) {
            skip_params.emplace_back(cap->param, cap->skip_value);
            spdlog::debug("[PrintPreparationManager] Using database param: {}={}", cap->param,
                          cap->skip_value);
        }

        // Z Tilt - use database param if available
        if (auto* cap = caps.get_capability("z_tilt");
            cap && get_option_state(can_show_z_tilt_subject_, preprint_z_tilt_subject_) ==
                       PrePrintOptionState::DISABLED) {
            skip_params.emplace_back(cap->param, cap->skip_value);
            spdlog::debug("[PrintPreparationManager] Using database param: {}={}", cap->param,
                          cap->skip_value);
        }

        // Nozzle clean - use database param if available
        if (auto* cap = caps.get_capability("nozzle_clean");
            cap &&
            get_option_state(can_show_nozzle_clean_subject_, preprint_nozzle_clean_subject_) ==
                PrePrintOptionState::DISABLED) {
            skip_params.emplace_back(cap->param, cap->skip_value);
            spdlog::debug("[PrintPreparationManager] Using database param: {}={}", cap->param,
                          cap->skip_value);
        }

        // Priming - use database param if available
        // Note: We don't have a priming checkbox yet, but AD5M supports it
        // Future: Add priming_checkbox_ and use it here

        // If we found capabilities, return them and skip macro analysis
        if (!skip_params.empty()) {
            spdlog::info("[PrintPreparationManager] Using {} params from capability database",
                         skip_params.size());
            return skip_params;
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
        get_option_state(can_show_bed_mesh_subject_, preprint_bed_mesh_subject_) ==
            PrePrintOptionState::DISABLED) {
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
        get_option_state(can_show_qgl_subject_, preprint_qgl_subject_) ==
            PrePrintOptionState::DISABLED) {
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
        get_option_state(can_show_z_tilt_subject_, preprint_z_tilt_subject_) ==
            PrePrintOptionState::DISABLED) {
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
        get_option_state(can_show_nozzle_clean_subject_, preprint_nozzle_clean_subject_) ==
            PrePrintOptionState::DISABLED) {
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
        helix::ui::async_call(
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
                        helix::ui::queue_update<PrintStartedData>(
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
                        helix::ui::async_call([](void*) { BusyOverlay::hide(); }, nullptr);

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
                    helix::ui::async_call([](void*) { BusyOverlay::hide(); }, nullptr);

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
                    helix::ui::async_call(
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
            helix::ui::async_call([](void*) { BusyOverlay::hide(); }, nullptr);

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
            spdlog::debug("[PrintPreparationManager] Print started successfully");

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
