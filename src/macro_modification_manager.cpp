// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "macro_modification_manager.h"

#include "ui_macro_enhance_wizard.h"
#include "ui_toast_manager.h"

#include "config.h"
#include "moonraker_api.h"
#include "printer_detector.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <functional>

namespace helix {

// ============================================================================
// Config Paths
// ============================================================================

static constexpr const char* CONFIG_PATH_DISMISSED = "/print_start_wizard/dismissed";
static constexpr const char* CONFIG_PATH_CONFIGURED = "/print_start_wizard/configured";
static constexpr const char* CONFIG_PATH_MACRO_HASH = "/print_start_wizard/macro_hash";

// ============================================================================
// Operation Category to Capability Database Key Mapping
// ============================================================================

/**
 * @brief Map PrintStartOpCategory to capability database key
 *
 * Uses category_to_string() as the single source of truth, with special
 * handling for categories that cannot be skipped (HOMING, UNKNOWN).
 *
 * @param category The operation category
 * @return Capability key string, or empty string if no mapping exists
 */
static std::string category_to_capability_key(PrintStartOpCategory category) {
    // These categories cannot be skipped - no capability key
    if (category == PrintStartOpCategory::HOMING || category == PrintStartOpCategory::UNKNOWN) {
        return "";
    }
    // All other categories use category_to_string() as the single source of truth
    return category_to_string(category);
}

// ============================================================================
// Hash Implementation (simple djb2)
// ============================================================================

std::string MacroModificationManager::compute_hash(const std::string& content) {
    if (content.empty()) {
        return "";
    }

    // djb2 hash - simple and fast
    unsigned long hash = 5381;
    for (char c : content) {
        hash = ((hash << 5) + hash) + static_cast<unsigned char>(c);
    }

    // Convert to hex string
    char buf[17];
    snprintf(buf, sizeof(buf), "%016lx", hash);
    return std::string(buf);
}

// ============================================================================
// Construction / Destruction
// ============================================================================

MacroModificationManager::MacroModificationManager(Config* config, MoonrakerAPI* api)
    : config_(config), api_(api), callback_guard_(std::make_shared<bool>(true)) {
    spdlog::debug("[MacroModificationManager] Created");
}

MacroModificationManager::~MacroModificationManager() {
    // Invalidate callback guard [L012]
    if (callback_guard_) {
        *callback_guard_ = false;
    }

    // Clean up wizard if visible
    wizard_.reset();
}

// ============================================================================
// Config Load/Save
// ============================================================================

PrintStartWizardConfig MacroModificationManager::load_config() const {
    PrintStartWizardConfig cfg;
    if (!config_) {
        return cfg;
    }

    cfg.dismissed = config_->get<bool>(CONFIG_PATH_DISMISSED, false);
    cfg.configured = config_->get<bool>(CONFIG_PATH_CONFIGURED, false);
    cfg.macro_hash = config_->get<std::string>(CONFIG_PATH_MACRO_HASH, "");

    return cfg;
}

void MacroModificationManager::save_config(const PrintStartWizardConfig& wizard_config) {
    if (!config_) {
        return;
    }

    config_->set<bool>(CONFIG_PATH_DISMISSED, wizard_config.dismissed);
    config_->set<bool>(CONFIG_PATH_CONFIGURED, wizard_config.configured);
    config_->set<std::string>(CONFIG_PATH_MACRO_HASH, wizard_config.macro_hash);
    config_->save();

    spdlog::debug("[MacroModificationManager] Config saved: dismissed={}, configured={}, hash={}",
                  wizard_config.dismissed, wizard_config.configured,
                  wizard_config.macro_hash.substr(0, 8));
}

// ============================================================================
// Primary API
// ============================================================================

void MacroModificationManager::check_and_notify() {
    if (!api_) {
        spdlog::warn("[MacroModificationManager] No API, skipping check");
        return;
    }

    auto wizard_config = load_config();
    if (wizard_config.dismissed) {
        spdlog::debug("[MacroModificationManager] User dismissed, skipping check");
        return;
    }

    analyzing_ = true;

    // Capture weak guard for async callback [L012]
    std::weak_ptr<bool> weak_guard = callback_guard_;

    analyzer_.analyze(
        api_,
        [this, weak_guard, wizard_config](const PrintStartAnalysis& analysis) {
            // Check if manager still exists
            auto guard = weak_guard.lock();
            if (!guard || !*guard) {
                return;
            }

            analyzing_ = false;
            cached_analysis_ = analysis;

            if (!analysis.found) {
                spdlog::debug("[MacroModificationManager] No PRINT_START macro found");
                return;
            }

            if (should_show_notification(analysis, wizard_config)) {
                show_configure_toast();
            } else {
                spdlog::debug(
                    "[MacroModificationManager] No notification needed (already configured "
                    "or no uncontrollable ops)");
            }
        },
        [this, weak_guard](const MoonrakerError& error) {
            auto guard = weak_guard.lock();
            if (!guard || !*guard) {
                return;
            }

            analyzing_ = false;
            spdlog::warn("[MacroModificationManager] Analysis failed: {}", error.message);
        });
}

void MacroModificationManager::analyze_and_launch_wizard() {
    if (!api_) {
        spdlog::warn("[MacroModificationManager] No API, cannot launch wizard");
        ui_toast_show(ToastSeverity::ERROR, "Not connected to printer", 3000);
        return;
    }

    analyzing_ = true;

    std::weak_ptr<bool> weak_guard = callback_guard_;

    analyzer_.analyze(
        api_,
        [this, weak_guard](const PrintStartAnalysis& analysis) {
            auto guard = weak_guard.lock();
            if (!guard || !*guard) {
                return;
            }

            analyzing_ = false;
            cached_analysis_ = analysis;

            if (!analysis.found) {
                ui_toast_show(ToastSeverity::INFO, "No PRINT_START macro found", 3000);
                return;
            }

            // Count uncontrollable operations (excluding HOMING which shouldn't be skippable)
            // Must match the filtering in MacroEnhanceWizard::set_analysis()
            size_t uncontrollable = 0;
            for (const auto* op : analysis.get_uncontrollable_operations()) {
                if (op->category != PrintStartOpCategory::HOMING) {
                    uncontrollable++;
                }
            }

            if (uncontrollable == 0) {
                ui_toast_show(ToastSeverity::SUCCESS,
                              "Your print start is already fully configured!", 3000);

                // Mark as configured since it's already good
                auto cfg = load_config();
                cfg.configured = true;
                cfg.macro_hash = compute_hash(analysis.raw_gcode);
                save_config(cfg);
                return;
            }

            launch_wizard();
        },
        [this, weak_guard](const MoonrakerError& error) {
            auto guard = weak_guard.lock();
            if (!guard || !*guard) {
                return;
            }

            analyzing_ = false;
            spdlog::warn("[MacroModificationManager] Analysis failed: {}", error.message);
            ui_toast_show(ToastSeverity::ERROR, "Failed to analyze PRINT_START macro", 3000);
        });
}

void MacroModificationManager::mark_dismissed() {
    auto cfg = load_config();
    cfg.dismissed = true;
    save_config(cfg);
    spdlog::info("[MacroModificationManager] User dismissed wizard permanently");
}

void MacroModificationManager::reset_dismissed() {
    auto cfg = load_config();
    cfg.dismissed = false;
    save_config(cfg);
    spdlog::info("[MacroModificationManager] Reset dismissed state");
}

// ============================================================================
// State Access
// ============================================================================

bool MacroModificationManager::is_wizard_visible() const {
    return wizard_ && wizard_->is_visible();
}

// ============================================================================
// Internal Methods
// ============================================================================

bool MacroModificationManager::should_show_notification(
    const PrintStartAnalysis& analysis, const PrintStartWizardConfig& wizard_config) const {
    // Use get_uncontrollable_operations() which excludes HOMING (same as wizard)
    auto uncontrollable_ops = analysis.get_uncontrollable_operations();
    size_t uncontrollable = uncontrollable_ops.size();

    if (uncontrollable == 0) {
        // All operations are already controllable (or only homing which can't be skipped)
        return false;
    }

    // Check if printer has native capabilities in database that cover these operations
    Config* config = Config::get_instance();
    if (config) {
        std::string printer_type = config->get<std::string>(helix::wizard::PRINTER_TYPE, "");
        if (!printer_type.empty()) {
            PrintStartCapabilities caps =
                PrinterDetector::get_print_start_capabilities(printer_type);

            if (!caps.empty()) {
                // Count how many uncontrollable ops are covered by native capabilities
                size_t covered_by_native = 0;
                for (const auto* op : uncontrollable_ops) {
                    std::string cap_key = category_to_capability_key(op->category);
                    if (!cap_key.empty() && caps.has_capability(cap_key)) {
                        covered_by_native++;
                    }
                }

                if (covered_by_native == uncontrollable) {
                    // All uncontrollable operations have native params - no wizard needed!
                    spdlog::info(
                        "[MacroModificationManager] Suppressing wizard toast: {} ops covered "
                        "by native {} capabilities for '{}'",
                        uncontrollable, caps.macro_name, printer_type);
                    return false;
                } else if (covered_by_native > 0) {
                    spdlog::debug(
                        "[MacroModificationManager] {}/{} ops covered by native capabilities",
                        covered_by_native, uncontrollable);
                }
            }
        }
    }

    // Compute current hash
    std::string current_hash = compute_hash(analysis.raw_gcode);

    // If already configured with same hash, no need to notify
    if (wizard_config.configured && wizard_config.macro_hash == current_hash) {
        return false;
    }

    // If hash changed, notify even if previously configured
    if (wizard_config.configured && wizard_config.macro_hash != current_hash) {
        spdlog::info("[MacroModificationManager] Macro changed since last configuration");
    }

    return true;
}

void MacroModificationManager::show_configure_toast() {
    // Use get_uncontrollable_operations() which excludes HOMING (same as wizard)
    size_t uncontrollable = cached_analysis_.get_uncontrollable_operations().size();

    char message[128];
    snprintf(message, sizeof(message), "PRINT_START has %zu skippable operation%s", uncontrollable,
             uncontrollable == 1 ? "" : "s");

    // Show toast with Configure action
    // Using raw pointer for callback since toast lifetime is short [L012]
    ui_toast_show_with_action(
        ToastSeverity::INFO, message, "Configure",
        [](void* user_data) {
            auto* manager = static_cast<MacroModificationManager*>(user_data);
            if (manager) {
                manager->launch_wizard();
            }
        },
        this, 8000); // Longer duration for important notification
}

void MacroModificationManager::launch_wizard() {
    spdlog::debug("[MacroModificationManager] launch_wizard() called");

    if (is_wizard_visible()) {
        spdlog::debug("[MacroModificationManager] Wizard already visible");
        return;
    }

    // Log cached analysis state
    spdlog::debug("[MacroModificationManager] Cached analysis: found={}, macro={}, ops={}, "
                  "uncontrollable={}",
                  cached_analysis_.found, cached_analysis_.macro_name,
                  cached_analysis_.operations.size(),
                  cached_analysis_.get_uncontrollable_operations().size());
    spdlog::debug("[MacroModificationManager] Analysis summary: {}", cached_analysis_.summary());

    // Create wizard
    wizard_ = std::make_unique<ui::MacroEnhanceWizard>();
    wizard_->set_api(api_);
    wizard_->set_analysis(cached_analysis_);

    // Capture weak guard for completion callback [L012]
    std::weak_ptr<bool> weak_guard = callback_guard_;

    wizard_->set_complete_callback([this, weak_guard](bool applied, size_t operations_enhanced) {
        auto guard = weak_guard.lock();
        if (!guard || !*guard) {
            return;
        }
        on_wizard_complete(applied, operations_enhanced);
    });

    // Show wizard
    if (!wizard_->show(lv_screen_active())) {
        spdlog::warn("[MacroModificationManager] Failed to show wizard");
        wizard_.reset();
        ui_toast_show(ToastSeverity::ERROR, "Failed to open wizard", 3000);
    }
}

void MacroModificationManager::on_wizard_complete(bool applied, size_t operations_enhanced) {
    spdlog::info("[MacroModificationManager] Wizard complete: applied={}, ops={}", applied,
                 operations_enhanced);

    if (applied && operations_enhanced > 0) {
        // Success! Update config
        auto cfg = load_config();
        cfg.configured = true;
        cfg.macro_hash = compute_hash(cached_analysis_.raw_gcode);
        save_config(cfg);

        char message[128];
        snprintf(message, sizeof(message), "Enhanced %zu operation%s in PRINT_START",
                 operations_enhanced, operations_enhanced == 1 ? "" : "s");
        ui_toast_show(ToastSeverity::SUCCESS, message, 4000);
    }

    // Clean up wizard
    wizard_.reset();
}

} // namespace helix
