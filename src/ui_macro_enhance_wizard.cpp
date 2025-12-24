// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_macro_enhance_wizard.h"

#include "ui_update_queue.h"

#include "moonraker_api.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>

namespace helix::ui {

// Static member initialization
bool MacroEnhanceWizard::callbacks_registered_ = false;

// ============================================================================
// Construction / Destruction
// ============================================================================

MacroEnhanceWizard::MacroEnhanceWizard() : callback_guard_(std::make_shared<bool>(true)) {
    init_subjects();
    register_callbacks();
    spdlog::debug("[MacroEnhanceWizard] Constructed");
}

MacroEnhanceWizard::~MacroEnhanceWizard() {
    *callback_guard_ = false;
    // Modal base class handles hide() in destructor
}

void MacroEnhanceWizard::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    // Initialize all subjects with empty strings
    lv_subject_init_pointer(&step_title_subject_, step_title_buf_);
    lv_subject_init_pointer(&step_progress_subject_, step_progress_buf_);
    lv_subject_init_pointer(&description_subject_, description_buf_);
    lv_subject_init_pointer(&diff_preview_subject_, diff_preview_buf_);
    lv_subject_init_pointer(&summary_subject_, summary_buf_);
    lv_subject_init_int(&state_subject_, static_cast<int>(MacroEnhanceState::OPERATION));

    // Register subjects for XML binding [CRITICAL: Without this, XML bindings silently fail]
    lv_xml_register_subject(nullptr, "macro_enhance_step_title", &step_title_subject_);
    lv_xml_register_subject(nullptr, "macro_enhance_step_progress", &step_progress_subject_);
    lv_xml_register_subject(nullptr, "macro_enhance_description", &description_subject_);
    lv_xml_register_subject(nullptr, "macro_enhance_diff_preview", &diff_preview_subject_);
    lv_xml_register_subject(nullptr, "macro_enhance_summary", &summary_subject_);
    lv_xml_register_subject(nullptr, "macro_enhance_state", &state_subject_);

    subjects_initialized_ = true;
}

void MacroEnhanceWizard::register_callbacks() {
    if (callbacks_registered_) {
        return;
    }

    lv_xml_register_event_cb(nullptr, "on_macro_enhance_skip", on_skip_cb);
    lv_xml_register_event_cb(nullptr, "on_macro_enhance_approve", on_approve_cb);
    lv_xml_register_event_cb(nullptr, "on_macro_enhance_cancel", on_cancel_cb);
    lv_xml_register_event_cb(nullptr, "on_macro_enhance_apply", on_apply_cb);
    lv_xml_register_event_cb(nullptr, "on_macro_enhance_close", on_close_cb);

    callbacks_registered_ = true;
}

// ============================================================================
// Setup
// ============================================================================

void MacroEnhanceWizard::set_analysis(const helix::PrintStartAnalysis& analysis) {
    analysis_ = analysis;
    operations_.clear();
    enhancements_.clear();
    current_op_index_ = 0;

    // Collect uncontrollable operations (excluding homing which shouldn't be skipped)
    for (const auto* op : analysis_.get_uncontrollable_operations()) {
        if (op->category != helix::PrintStartOpCategory::HOMING) {
            operations_.push_back(op);
        }
    }

    spdlog::debug("[MacroEnhanceWizard] Found {} operations to enhance", operations_.size());
}

// ============================================================================
// Show / Hide
// ============================================================================

bool MacroEnhanceWizard::show(lv_obj_t* parent) {
    if (is_visible()) {
        spdlog::warn("[MacroEnhanceWizard] Wizard already open");
        return false;
    }

    if (api_ == nullptr) {
        spdlog::error("[MacroEnhanceWizard] API not set");
        return false;
    }

    if (operations_.empty()) {
        spdlog::info("[MacroEnhanceWizard] No operations to enhance");
        return false;
    }

    // Reset state
    state_ = MacroEnhanceState::OPERATION;
    current_op_index_ = 0;
    enhancements_.clear();

    // Reset callback guard
    callback_guard_ = std::make_shared<bool>(true);

    // Use Modal base class to show
    if (!Modal::show(parent)) {
        spdlog::error("[MacroEnhanceWizard] Failed to show modal");
        return false;
    }

    spdlog::info("[MacroEnhanceWizard] Wizard opened with {} operations", operations_.size());
    return true;
}

size_t MacroEnhanceWizard::get_approved_count() const {
    return std::count_if(enhancements_.begin(), enhancements_.end(),
                         [](const helix::MacroEnhancement& e) { return e.user_approved; });
}

// ============================================================================
// Modal Hooks
// ============================================================================

void MacroEnhanceWizard::on_show() {
    // Store 'this' in modal's user_data for callback traversal
    lv_obj_set_user_data(dialog(), this);

    bind_subjects_to_widgets();
    show_current_operation();
}

void MacroEnhanceWizard::on_hide() {
    // Guard against cleanup during LVGL shutdown [SERIOUS-6]
    if (!lv_is_initialized()) {
        return;
    }

    // Remove observers before subjects become invalid [L020]
    if (step_title_observer_) {
        lv_observer_remove(step_title_observer_);
        step_title_observer_ = nullptr;
    }
    if (step_progress_observer_) {
        lv_observer_remove(step_progress_observer_);
        step_progress_observer_ = nullptr;
    }
    if (description_observer_) {
        lv_observer_remove(description_observer_);
        description_observer_ = nullptr;
    }
    if (diff_preview_observer_) {
        lv_observer_remove(diff_preview_observer_);
        diff_preview_observer_ = nullptr;
    }
    if (summary_observer_) {
        lv_observer_remove(summary_observer_);
        summary_observer_ = nullptr;
    }
    // Cleanup observers for shared description bindings
    if (applying_status_observer_) {
        lv_observer_remove(applying_status_observer_);
        applying_status_observer_ = nullptr;
    }
    if (success_message_observer_) {
        lv_observer_remove(success_message_observer_);
        success_message_observer_ = nullptr;
    }
    if (error_message_observer_) {
        lv_observer_remove(error_message_observer_);
        error_message_observer_ = nullptr;
    }
}

void MacroEnhanceWizard::bind_subjects_to_widgets() {
    // Bind subjects to UI elements using Modal's find_widget() helper
    lv_obj_t* step_title = find_widget("step_title");
    if (step_title != nullptr) {
        step_title_observer_ = lv_label_bind_text(step_title, &step_title_subject_, "%s");
    }

    lv_obj_t* step_progress = find_widget("step_progress");
    if (step_progress != nullptr) {
        step_progress_observer_ = lv_label_bind_text(step_progress, &step_progress_subject_, "%s");
    }

    lv_obj_t* description = find_widget("operation_description");
    if (description != nullptr) {
        description_observer_ = lv_label_bind_text(description, &description_subject_, "%s");
    }

    lv_obj_t* diff_preview = find_widget("diff_preview");
    if (diff_preview != nullptr) {
        diff_preview_observer_ = lv_label_bind_text(diff_preview, &diff_preview_subject_, "%s");
    }

    lv_obj_t* summary_list = find_widget("summary_list");
    if (summary_list != nullptr) {
        summary_observer_ = lv_label_bind_text(summary_list, &summary_subject_, "%s");
    }

    // Bind shared description subject to multiple widgets [L020: Track all observers]
    lv_obj_t* applying_status = find_widget("applying_status");
    if (applying_status != nullptr) {
        applying_status_observer_ =
            lv_label_bind_text(applying_status, &description_subject_, "%s");
    }

    lv_obj_t* success_message = find_widget("success_message");
    if (success_message != nullptr) {
        success_message_observer_ =
            lv_label_bind_text(success_message, &description_subject_, "%s");
    }

    lv_obj_t* error_message = find_widget("error_message");
    if (error_message != nullptr) {
        error_message_observer_ = lv_label_bind_text(error_message, &description_subject_, "%s");
    }

    // Set initial state
    lv_subject_set_int(&state_subject_, static_cast<int>(state_));
}

// ============================================================================
// UI Updates
// ============================================================================

void MacroEnhanceWizard::update_ui() {
    if (!is_visible()) {
        return;
    }

    // Update state subject to trigger visibility bindings
    lv_subject_set_int(&state_subject_, static_cast<int>(state_));

    // Update close button visibility for terminal states
    update_close_button_visibility();
}

void MacroEnhanceWizard::show_current_operation() {
    if (current_op_index_ >= operations_.size()) {
        // No more operations, show summary
        show_summary();
        return;
    }

    state_ = MacroEnhanceState::OPERATION;
    const auto* op = operations_[current_op_index_];

    // Update title
    snprintf(step_title_buf_, sizeof(step_title_buf_), "Make %s Optional?", op->name.c_str());
    lv_subject_set_pointer(&step_title_subject_, step_title_buf_);

    // Update progress
    snprintf(step_progress_buf_, sizeof(step_progress_buf_), "%zu of %zu", current_op_index_ + 1,
             operations_.size());
    lv_subject_set_pointer(&step_progress_subject_, step_progress_buf_);

    // Update description
    std::string category_desc;
    switch (op->category) {
    case helix::PrintStartOpCategory::BED_LEVELING:
        category_desc = "bed leveling operation";
        break;
    case helix::PrintStartOpCategory::QGL:
        category_desc = "quad gantry leveling";
        break;
    case helix::PrintStartOpCategory::Z_TILT:
        category_desc = "Z-tilt adjustment";
        break;
    case helix::PrintStartOpCategory::NOZZLE_CLEAN:
        category_desc = "nozzle cleaning routine";
        break;
    default:
        category_desc = "operation";
        break;
    }

    snprintf(description_buf_, sizeof(description_buf_),
             "Your PRINT_START macro runs %s (%s). Would you like to make it "
             "skippable so you can control it from the print settings?",
             op->name.c_str(), category_desc.c_str());
    lv_subject_set_pointer(&description_subject_, description_buf_);

    // Generate and show the wrapper code
    std::string skip_param = helix::PrintStartEnhancer::get_skip_param_for_category(op->category);
    if (skip_param.empty()) {
        skip_param = "SKIP_" + op->name;
    }

    std::string wrapper =
        helix::PrintStartEnhancer::generate_conditional_block(op->name, skip_param, true);
    snprintf(diff_preview_buf_, sizeof(diff_preview_buf_), "%s", wrapper.c_str());
    lv_subject_set_pointer(&diff_preview_subject_, diff_preview_buf_);

    update_ui();
}

void MacroEnhanceWizard::show_summary() {
    state_ = MacroEnhanceState::SUMMARY;

    size_t approved_count = get_approved_count();

    // Update title
    snprintf(step_title_buf_, sizeof(step_title_buf_), "Ready to Apply");
    lv_subject_set_pointer(&step_title_subject_, step_title_buf_);

    // Update progress
    snprintf(step_progress_buf_, sizeof(step_progress_buf_), "%zu changes", approved_count);
    lv_subject_set_pointer(&step_progress_subject_, step_progress_buf_);

    // Build summary list
    std::string summary;
    if (approved_count == 0) {
        summary = "No changes selected.\n\nClick Cancel to close.";
    } else {
        summary = "The following operations will be made skippable:\n\n";
        for (const auto& e : enhancements_) {
            if (e.user_approved) {
                summary += "  " + e.operation_name + " -> " + e.skip_param_name + "\n";
            }
        }
    }

    snprintf(summary_buf_, sizeof(summary_buf_), "%s", summary.c_str());
    lv_subject_set_pointer(&summary_subject_, summary_buf_);

    update_ui();
}

void MacroEnhanceWizard::show_applying(const std::string& status) {
    state_ = MacroEnhanceState::APPLYING;

    snprintf(step_title_buf_, sizeof(step_title_buf_), "Applying Changes");
    lv_subject_set_pointer(&step_title_subject_, step_title_buf_);

    snprintf(step_progress_buf_, sizeof(step_progress_buf_), "");
    lv_subject_set_pointer(&step_progress_subject_, step_progress_buf_);

    snprintf(description_buf_, sizeof(description_buf_), "%s", status.c_str());
    lv_subject_set_pointer(&description_subject_, description_buf_);

    update_ui();
}

void MacroEnhanceWizard::show_success(const std::string& message) {
    state_ = MacroEnhanceState::SUCCESS;

    snprintf(step_title_buf_, sizeof(step_title_buf_), "Complete");
    lv_subject_set_pointer(&step_title_subject_, step_title_buf_);

    snprintf(step_progress_buf_, sizeof(step_progress_buf_), "");
    lv_subject_set_pointer(&step_progress_subject_, step_progress_buf_);

    snprintf(description_buf_, sizeof(description_buf_), "%s", message.c_str());
    lv_subject_set_pointer(&description_subject_, description_buf_);

    update_ui();
}

void MacroEnhanceWizard::show_error(const std::string& message) {
    state_ = MacroEnhanceState::ERROR;

    snprintf(step_title_buf_, sizeof(step_title_buf_), "Error");
    lv_subject_set_pointer(&step_title_subject_, step_title_buf_);

    snprintf(step_progress_buf_, sizeof(step_progress_buf_), "");
    lv_subject_set_pointer(&step_progress_subject_, step_progress_buf_);

    snprintf(description_buf_, sizeof(description_buf_), "%s", message.c_str());
    lv_subject_set_pointer(&description_subject_, description_buf_);

    update_ui();
}

void MacroEnhanceWizard::update_close_button_visibility() {
    if (!is_visible()) {
        return;
    }

    // The close button should be visible in SUCCESS or ERROR states
    lv_obj_t* close_buttons = find_widget("close_buttons");
    if (close_buttons != nullptr) {
        if (state_ == MacroEnhanceState::SUCCESS || state_ == MacroEnhanceState::ERROR) {
            lv_obj_remove_flag(close_buttons, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(close_buttons, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ============================================================================
// Navigation
// ============================================================================

void MacroEnhanceWizard::advance_to_next() {
    current_op_index_++;
    show_current_operation();
}

// ============================================================================
// Apply Enhancements
// ============================================================================

void MacroEnhanceWizard::apply_enhancements() {
    if (api_ == nullptr) {
        show_error("API connection not available");
        return;
    }

    // Filter to only approved enhancements
    std::vector<helix::MacroEnhancement> approved;
    for (const auto& e : enhancements_) {
        if (e.user_approved) {
            approved.push_back(e);
        }
    }

    if (approved.empty()) {
        show_error("No changes to apply");
        return;
    }

    show_applying("Creating backup...");

    // Check if backup checkbox is checked
    lv_obj_t* checkbox = find_widget("backup_checkbox");
    bool create_backup = true;
    if (checkbox != nullptr) {
        create_backup = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
    }
    // TODO: Pass create_backup to enhancer when API supports it
    (void)create_backup;

    auto guard = callback_guard_;

    // Async context structures for thread-safe callbacks [L012]
    // Uses weak_ptr to safely check if wizard is still valid
    struct AsyncProgressCtx {
        std::weak_ptr<bool> guard;
        MacroEnhanceWizard* wizard;
        std::string message;
    };
    struct AsyncSuccessCtx {
        std::weak_ptr<bool> guard;
        MacroEnhanceWizard* wizard;
        size_t count;
        std::string backup;
    };
    struct AsyncErrorCtx {
        std::weak_ptr<bool> guard;
        MacroEnhanceWizard* wizard;
        std::string message;
    };

    // Apply enhancements using PrintStartEnhancer
    enhancer_.apply_enhancements(
        api_, analysis_.macro_name, approved,
        // Progress callback
        [this, guard](const std::string& step, int /*current*/, int /*total*/) {
            if (!*guard) {
                return;
            }
            // Allocate context - will be freed in async handler
            auto* ctx = new AsyncProgressCtx{guard, this, step};
            ui_async_call(
                [](void* user_data) {
                    auto* ctx = static_cast<AsyncProgressCtx*>(user_data);
                    // Check guard in main thread [SERIOUS-5: Thread safety]
                    auto guard_locked = ctx->guard.lock();
                    if (guard_locked && *guard_locked && ctx->wizard->is_visible()) {
                        ctx->wizard->show_applying(ctx->message);
                    }
                    delete ctx;
                },
                ctx);
        },
        // Success callback
        [this, guard, approved_count = approved.size()](const helix::EnhancementResult& result) {
            if (!*guard) {
                return;
            }
            auto* ctx = new AsyncSuccessCtx{guard, this, approved_count, result.backup_filename};
            ui_async_call(
                [](void* user_data) {
                    auto* ctx = static_cast<AsyncSuccessCtx*>(user_data);
                    auto guard_locked = ctx->guard.lock();
                    if (guard_locked && *guard_locked && ctx->wizard->is_visible()) {
                        std::string msg = "Successfully enhanced " + std::to_string(ctx->count) +
                                          " operation(s).\n\nBackup: " + ctx->backup +
                                          "\n\nKlipper is restarting...";
                        ctx->wizard->show_success(msg);
                    }
                    delete ctx;
                },
                ctx);
        },
        // Error callback
        [this, guard](const MoonrakerError& err) {
            if (!*guard) {
                return;
            }
            auto* ctx = new AsyncErrorCtx{guard, this, err.user_message()};
            ui_async_call(
                [](void* user_data) {
                    auto* ctx = static_cast<AsyncErrorCtx*>(user_data);
                    auto guard_locked = ctx->guard.lock();
                    if (guard_locked && *guard_locked && ctx->wizard->is_visible()) {
                        ctx->wizard->show_error(ctx->message);
                    }
                    delete ctx;
                },
                ctx);
        });
}

// ============================================================================
// Event Handlers
// ============================================================================

void MacroEnhanceWizard::handle_skip() {
    if (current_op_index_ >= operations_.size()) {
        return;
    }

    spdlog::debug("[MacroEnhanceWizard] Skipped operation: {}",
                  operations_[current_op_index_]->name);

    // Create enhancement marked as not approved
    const auto* op = operations_[current_op_index_];
    std::string skip_param = helix::PrintStartEnhancer::get_skip_param_for_category(op->category);
    if (skip_param.empty()) {
        skip_param = "SKIP_" + op->name;
    }

    helix::MacroEnhancement enhancement;
    enhancement.operation_name = op->name;
    enhancement.category = op->category;
    enhancement.skip_param_name = skip_param;
    enhancement.user_approved = false;
    enhancements_.push_back(enhancement);

    advance_to_next();
}

void MacroEnhanceWizard::handle_approve() {
    if (current_op_index_ >= operations_.size()) {
        return;
    }

    const auto* op = operations_[current_op_index_];
    spdlog::debug("[MacroEnhanceWizard] Approved operation: {}", op->name);

    // Generate enhancement
    std::string skip_param = helix::PrintStartEnhancer::get_skip_param_for_category(op->category);
    if (skip_param.empty()) {
        skip_param = "SKIP_" + op->name;
    }

    // Create a temporary PrintStartOperation for generate_wrapper
    helix::PrintStartOperation temp_op;
    temp_op.name = op->name;
    temp_op.category = op->category;
    temp_op.line_number = op->line_number;

    helix::MacroEnhancement enhancement =
        helix::PrintStartEnhancer::generate_wrapper(temp_op, skip_param);
    enhancement.user_approved = true;
    enhancements_.push_back(enhancement);

    advance_to_next();
}

void MacroEnhanceWizard::handle_cancel() {
    spdlog::info("[MacroEnhanceWizard] Wizard cancelled");

    if (on_complete_) {
        on_complete_(false, 0);
    }

    hide();
}

void MacroEnhanceWizard::handle_apply() {
    spdlog::info("[MacroEnhanceWizard] Applying {} approved enhancements", get_approved_count());
    apply_enhancements();
}

void MacroEnhanceWizard::handle_close() {
    bool applied = (state_ == MacroEnhanceState::SUCCESS);
    size_t count = applied ? get_approved_count() : 0;

    spdlog::info("[MacroEnhanceWizard] Wizard closed (applied: {}, count: {})", applied, count);

    if (on_complete_) {
        on_complete_(applied, count);
    }

    hide();
}

// ============================================================================
// Static Callbacks
// ============================================================================

MacroEnhanceWizard* MacroEnhanceWizard::get_instance_from_event(lv_event_t* e) {
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* modal = lv_obj_get_parent(target);

    // Navigate up to find the modal with user data
    while (modal != nullptr && lv_obj_get_user_data(modal) == nullptr) {
        modal = lv_obj_get_parent(modal);
    }

    if (modal == nullptr) {
        return nullptr;
    }

    return static_cast<MacroEnhanceWizard*>(lv_obj_get_user_data(modal));
}

void MacroEnhanceWizard::on_skip_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self != nullptr) {
        self->handle_skip();
    }
}

void MacroEnhanceWizard::on_approve_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self != nullptr) {
        self->handle_approve();
    }
}

void MacroEnhanceWizard::on_cancel_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self != nullptr) {
        self->handle_cancel();
    }
}

void MacroEnhanceWizard::on_apply_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self != nullptr) {
        self->handle_apply();
    }
}

void MacroEnhanceWizard::on_close_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self != nullptr) {
        self->handle_close();
    }
}

} // namespace helix::ui
