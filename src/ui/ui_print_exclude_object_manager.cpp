// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_print_exclude_object_manager.h"

#include "ui_error_reporting.h"
#include "ui_gcode_viewer.h"
#include "ui_toast.h"
#include "ui_update_queue.h"

#include "lvgl/src/others/translation/lv_translation.h"
#include "moonraker_api.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

// ============================================================================
// Constants
// ============================================================================

/// Undo window duration in milliseconds
constexpr uint32_t EXCLUDE_UNDO_WINDOW_MS = 5000;

// ============================================================================
// PrintExcludeObjectManager Implementation
// ============================================================================

PrintExcludeObjectManager::PrintExcludeObjectManager(MoonrakerAPI* api, PrinterState& printer_state,
                                                     lv_obj_t* gcode_viewer)
    : api_(api), printer_state_(printer_state), gcode_viewer_(gcode_viewer) {
    spdlog::debug("[PrintExcludeObjectManager] Constructed");
}

PrintExcludeObjectManager::~PrintExcludeObjectManager() {
    // Signal async callbacks to abort
    alive_->store(false);

    // Clean up timer if LVGL is still active
    if (lv_is_initialized() && exclude_undo_timer_) {
        lv_timer_delete(exclude_undo_timer_);
        exclude_undo_timer_ = nullptr;
    }

    spdlog::debug("[PrintExcludeObjectManager] Destroyed");
}

void PrintExcludeObjectManager::init() {
    if (initialized_) {
        spdlog::warn("[PrintExcludeObjectManager] init() called twice - ignoring");
        return;
    }

    // Subscribe to excluded objects changes from PrinterState
    excluded_objects_observer_ = ObserverGuard(
        printer_state_.get_excluded_objects_version_subject(), excluded_objects_observer_cb, this);

    // Register long-press callback on gcode viewer
    if (gcode_viewer_) {
        ui_gcode_viewer_set_object_long_press_callback(gcode_viewer_, on_object_long_pressed, this);
        spdlog::debug("[PrintExcludeObjectManager] Registered long-press callback");
    }

    initialized_ = true;
    spdlog::debug("[PrintExcludeObjectManager] Initialized");
}

void PrintExcludeObjectManager::deinit() {
    if (!initialized_) {
        return;
    }

    // Clean up timer
    if (exclude_undo_timer_ && lv_is_initialized()) {
        lv_timer_delete(exclude_undo_timer_);
        exclude_undo_timer_ = nullptr;
    }

    // Unregister long-press callback
    if (gcode_viewer_ && lv_is_initialized()) {
        ui_gcode_viewer_set_object_long_press_callback(gcode_viewer_, nullptr, nullptr);
    }

    // Observer cleanup happens automatically via ObserverGuard destructor

    initialized_ = false;
    spdlog::debug("[PrintExcludeObjectManager] Deinitialized");
}

void PrintExcludeObjectManager::set_gcode_viewer(lv_obj_t* gcode_viewer) {
    // Unregister from old viewer
    if (gcode_viewer_ && initialized_ && lv_is_initialized()) {
        ui_gcode_viewer_set_object_long_press_callback(gcode_viewer_, nullptr, nullptr);
    }

    gcode_viewer_ = gcode_viewer;

    // Register on new viewer
    if (gcode_viewer_ && initialized_) {
        ui_gcode_viewer_set_object_long_press_callback(gcode_viewer_, on_object_long_pressed, this);
        spdlog::debug(
            "[PrintExcludeObjectManager] Re-registered long-press callback on new viewer");
    }
}

// ============================================================================
// Long-press Handler
// ============================================================================

void PrintExcludeObjectManager::on_object_long_pressed(lv_obj_t* viewer, const char* object_name,
                                                       void* user_data) {
    (void)viewer;
    auto* self = static_cast<PrintExcludeObjectManager*>(user_data);
    if (self && object_name && object_name[0] != '\0') {
        self->handle_object_long_press(object_name);
    }
}

void PrintExcludeObjectManager::handle_object_long_press(const char* object_name) {
    if (!object_name || object_name[0] == '\0') {
        spdlog::debug("[PrintExcludeObjectManager] Long-press on empty area (no object)");
        return;
    }

    // Check if already excluded
    if (excluded_objects_.count(object_name) > 0) {
        spdlog::info("[PrintExcludeObjectManager] Object '{}' already excluded - ignoring",
                     object_name);
        return;
    }

    // Check if there's already a pending exclusion
    if (!pending_exclude_object_.empty()) {
        spdlog::warn(
            "[PrintExcludeObjectManager] Already have pending exclusion for '{}' - ignoring new",
            pending_exclude_object_);
        return;
    }

    spdlog::info("[PrintExcludeObjectManager] Long-press on object: '{}' - showing confirmation",
                 object_name);

    // Store the object name for when confirmation happens
    pending_exclude_object_ = object_name;

    // Configure and show the modal
    exclude_modal_.set_object_name(object_name);
    exclude_modal_.set_on_confirm([this]() { handle_exclude_confirmed(); });
    exclude_modal_.set_on_cancel([this]() { handle_exclude_cancelled(); });

    std::string message = "Stop printing \"" + std::string(object_name) +
                          "\"?\n\nThis cannot be undone after 5 seconds.";
    const char* attrs[] = {"title", "Exclude Object?", "message", message.c_str(), nullptr};

    if (!exclude_modal_.show(lv_screen_active(), attrs)) {
        spdlog::error("[PrintExcludeObjectManager] Failed to show exclude confirmation modal");
        pending_exclude_object_.clear();
    }
}

// ============================================================================
// Modal Confirmation Handlers
// ============================================================================

void PrintExcludeObjectManager::handle_exclude_confirmed() {
    spdlog::info("[PrintExcludeObjectManager] Exclusion confirmed for '{}'",
                 pending_exclude_object_);

    if (pending_exclude_object_.empty()) {
        spdlog::error("[PrintExcludeObjectManager] No pending object for exclusion");
        return;
    }

    // Immediately update visual state in G-code viewer (red/semi-transparent)
    if (gcode_viewer_) {
        std::unordered_set<std::string> visual_excluded = excluded_objects_;
        visual_excluded.insert(pending_exclude_object_);
        ui_gcode_viewer_set_excluded_objects(gcode_viewer_, visual_excluded);
        spdlog::debug("[PrintExcludeObjectManager] Updated viewer with visual exclusion");
    }

    // Start undo timer - when it fires, we send EXCLUDE_OBJECT to Klipper
    if (exclude_undo_timer_) {
        lv_timer_delete(exclude_undo_timer_);
    }
    exclude_undo_timer_ = lv_timer_create(exclude_undo_timer_cb, EXCLUDE_UNDO_WINDOW_MS, this);
    lv_timer_set_repeat_count(exclude_undo_timer_, 1);

    // Show toast with "Undo" action button
    std::string toast_msg = "Excluding \"" + pending_exclude_object_ + "\"...";
    ui_toast_show_with_action(
        ToastSeverity::WARNING, toast_msg.c_str(), "Undo",
        [](void* user_data) {
            auto* self = static_cast<PrintExcludeObjectManager*>(user_data);
            if (self) {
                self->handle_exclude_undo();
            }
        },
        this, EXCLUDE_UNDO_WINDOW_MS);

    spdlog::info("[PrintExcludeObjectManager] Started {}ms undo window for '{}'",
                 EXCLUDE_UNDO_WINDOW_MS, pending_exclude_object_);
}

void PrintExcludeObjectManager::handle_exclude_cancelled() {
    spdlog::info("[PrintExcludeObjectManager] Exclusion cancelled for '{}'",
                 pending_exclude_object_);

    // Clear pending state
    pending_exclude_object_.clear();

    // Clear selection in viewer
    if (gcode_viewer_) {
        std::unordered_set<std::string> empty_set;
        ui_gcode_viewer_set_highlighted_objects(gcode_viewer_, empty_set);
    }
}

void PrintExcludeObjectManager::handle_exclude_undo() {
    if (pending_exclude_object_.empty()) {
        spdlog::warn("[PrintExcludeObjectManager] Undo called but no pending exclusion");
        return;
    }

    spdlog::info("[PrintExcludeObjectManager] Undo pressed - cancelling exclusion of '{}'",
                 pending_exclude_object_);

    // Cancel the timer
    if (exclude_undo_timer_) {
        lv_timer_delete(exclude_undo_timer_);
        exclude_undo_timer_ = nullptr;
    }

    // Restore visual state - remove from visual exclusion
    if (gcode_viewer_) {
        ui_gcode_viewer_set_excluded_objects(gcode_viewer_, excluded_objects_);
    }

    // Clear pending
    pending_exclude_object_.clear();

    // Show confirmation that undo succeeded
    ui_toast_show(ToastSeverity::SUCCESS, lv_tr("Exclusion cancelled"), 2000);
}

// ============================================================================
// Timer Callback
// ============================================================================

void PrintExcludeObjectManager::exclude_undo_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<PrintExcludeObjectManager*>(lv_timer_get_user_data(timer));
    if (!self) {
        return;
    }

    self->exclude_undo_timer_ = nullptr; // Timer auto-deletes after single shot

    if (self->pending_exclude_object_.empty()) {
        spdlog::warn("[PrintExcludeObjectManager] Undo timer fired but no pending object");
        return;
    }

    std::string object_name = self->pending_exclude_object_;
    self->pending_exclude_object_.clear();

    spdlog::info(
        "[PrintExcludeObjectManager] Undo window expired - sending EXCLUDE_OBJECT for '{}'",
        object_name);

    // Capture alive guard for async callback safety
    auto alive = self->alive_;

    // Actually send the command to Klipper via MoonrakerAPI
    if (self->api_) {
        self->api_->exclude_object(
            object_name,
            [self, alive, object_name]() {
                if (!alive->load()) {
                    return; // Manager was destroyed
                }
                spdlog::info("[PrintExcludeObjectManager] EXCLUDE_OBJECT '{}' sent successfully",
                             object_name);
                // Move to confirmed excluded set
                self->excluded_objects_.insert(object_name);
            },
            [self, alive, object_name](const MoonrakerError& err) {
                if (!alive->load()) {
                    return; // Manager was destroyed
                }
                spdlog::error("[PrintExcludeObjectManager] Failed to exclude '{}': {}", object_name,
                              err.message);

                // UI operations must happen on the main thread
                ui_queue_update([self, alive, object_name, user_msg = err.user_message()]() {
                    if (!alive->load()) {
                        return;
                    }
                    NOTIFY_ERROR("Failed to exclude '{}': {}", object_name, user_msg);

                    // Revert visual state - refresh viewer with only confirmed exclusions
                    if (self->gcode_viewer_) {
                        ui_gcode_viewer_set_excluded_objects(self->gcode_viewer_,
                                                             self->excluded_objects_);
                        spdlog::debug(
                            "[PrintExcludeObjectManager] Reverted visual exclusion for '{}'",
                            object_name);
                    }
                });
            });
    } else {
        spdlog::warn("[PrintExcludeObjectManager] No API available - simulating exclusion");
        self->excluded_objects_.insert(object_name);
    }
}

// ============================================================================
// Observer Callback
// ============================================================================

void PrintExcludeObjectManager::excluded_objects_observer_cb(lv_observer_t* observer,
                                                             lv_subject_t* subject) {
    (void)subject;
    auto* self = static_cast<PrintExcludeObjectManager*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_excluded_objects_changed();
    }
}

void PrintExcludeObjectManager::on_excluded_objects_changed() {
    // Sync excluded objects from PrinterState (Klipper/Moonraker)
    const auto& klipper_excluded = printer_state_.get_excluded_objects();

    // Merge Klipper's excluded set with our local set
    // This ensures objects excluded via Klipper (e.g., from another client) are shown
    for (const auto& obj : klipper_excluded) {
        if (excluded_objects_.count(obj) == 0) {
            excluded_objects_.insert(obj);
            spdlog::info("[PrintExcludeObjectManager] Synced excluded object from Klipper: '{}'",
                         obj);
        }
    }

    // Update the G-code viewer visual state
    if (gcode_viewer_) {
        // Combine confirmed excluded with any pending exclusion for visual display
        std::unordered_set<std::string> visual_excluded = excluded_objects_;
        if (!pending_exclude_object_.empty()) {
            visual_excluded.insert(pending_exclude_object_);
        }
        ui_gcode_viewer_set_excluded_objects(gcode_viewer_, visual_excluded);
        spdlog::debug("[PrintExcludeObjectManager] Updated viewer with {} excluded objects",
                      visual_excluded.size());
    }
}

} // namespace helix::ui
