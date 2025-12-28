// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_modal.h"

#include "ui_event_safety.h"
#include "ui_keyboard.h"
#include "ui_update_queue.h"

#include "settings_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>

// ============================================================================
// MODAL STYLE CONSTANTS
// ============================================================================
// Backdrop opacity: 180 = ~70% opacity (dark overlay behind modal)
static constexpr uint8_t MODAL_BACKDROP_OPACITY = 180;

// ============================================================================
// ANIMATION CONSTANTS
// ============================================================================
// Duration values match globals.xml tokens for consistency
static constexpr int32_t MODAL_ENTRANCE_DURATION_MS = 250; // anim_normal
static constexpr int32_t MODAL_EXIT_DURATION_MS = 150;     // anim_fast

// Scale animation uses percentage values (0-256 in LVGL)
// We animate from 85% (218) to 100% (256), with slight overshoot for bounce
static constexpr int32_t MODAL_SCALE_START = 218; // ~85% scale
static constexpr int32_t MODAL_SCALE_END = 256;   // 100% scale

// ============================================================================
// MODAL DIALOG SUBJECTS (singleton state)
// ============================================================================
namespace {
bool g_subjects_initialized = false;
lv_subject_t g_dialog_severity{};
lv_subject_t g_dialog_show_cancel{};
lv_subject_t g_dialog_primary_text{};
lv_subject_t g_dialog_cancel_text{};
constexpr const char* DEFAULT_PRIMARY_TEXT = "OK";
constexpr const char* DEFAULT_CANCEL_TEXT = "Cancel";
} // namespace

// ============================================================================
// MODALSTACK IMPLEMENTATION
// ============================================================================

ModalStack& ModalStack::instance() {
    static ModalStack instance;
    return instance;
}

void ModalStack::push(lv_obj_t* backdrop, lv_obj_t* dialog, const std::string& component_name) {
    stack_.push_back({backdrop, dialog, component_name, false /* exiting */});
    spdlog::debug("[ModalStack] Pushed modal '{}' (stack depth: {})", component_name,
                  stack_.size());
}

void ModalStack::remove(lv_obj_t* backdrop) {
    auto it = std::find_if(stack_.begin(), stack_.end(),
                           [backdrop](const ModalEntry& e) { return e.backdrop == backdrop; });
    if (it != stack_.end()) {
        spdlog::debug("[ModalStack] Removed modal '{}' (stack depth: {})", it->component_name,
                      stack_.size() - 1);
        stack_.erase(it);
    }
}

lv_obj_t* ModalStack::top_dialog() const {
    // Return topmost non-exiting modal
    for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
        if (!it->exiting) {
            return it->dialog;
        }
    }
    return nullptr;
}

lv_obj_t* ModalStack::backdrop_for(lv_obj_t* dialog) const {
    for (const auto& entry : stack_) {
        if (entry.dialog == dialog) {
            return entry.backdrop;
        }
    }
    return nullptr;
}

bool ModalStack::empty() const {
    // Returns true if no visible (non-exiting) modals
    for (const auto& entry : stack_) {
        if (!entry.exiting) {
            return false;
        }
    }
    return true;
}

bool ModalStack::mark_exiting(lv_obj_t* backdrop) {
    for (auto& entry : stack_) {
        if (entry.backdrop == backdrop) {
            if (entry.exiting) {
                spdlog::debug("[ModalStack] Modal '{}' already exiting - ignoring",
                              entry.component_name);
                return false; // Already exiting
            }
            entry.exiting = true;
            spdlog::debug("[ModalStack] Marked modal '{}' as exiting", entry.component_name);
            return true;
        }
    }
    return false; // Not found
}

bool ModalStack::is_exiting(lv_obj_t* backdrop) const {
    for (const auto& entry : stack_) {
        if (entry.backdrop == backdrop) {
            return entry.exiting;
        }
    }
    return false;
}

void ModalStack::animate_entrance(lv_obj_t* dialog) {
    // Find backdrop for this dialog
    lv_obj_t* backdrop = backdrop_for(dialog);
    if (!backdrop) {
        return;
    }

    // Set transform pivot to center so scaling happens from center, not corner
    lv_obj_set_style_transform_pivot_x(dialog, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(dialog, LV_PCT(50), LV_PART_MAIN);

    // Skip animation if disabled - show in final state
    if (!SettingsManager::instance().get_animations_enabled()) {
        lv_obj_set_style_opa(backdrop, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_transform_scale(dialog, MODAL_SCALE_END, LV_PART_MAIN);
        lv_obj_set_style_opa(dialog, LV_OPA_COVER, LV_PART_MAIN);
        spdlog::debug("[ModalStack] Animations disabled - showing modal instantly");
        return;
    }

    // Start backdrop transparent
    lv_obj_set_style_opa(backdrop, LV_OPA_TRANSP, LV_PART_MAIN);

    // Start dialog scaled down and transparent
    lv_obj_set_style_transform_scale(dialog, MODAL_SCALE_START, LV_PART_MAIN);
    lv_obj_set_style_opa(dialog, LV_OPA_TRANSP, LV_PART_MAIN);

    // Fade in backdrop
    lv_anim_t backdrop_anim;
    lv_anim_init(&backdrop_anim);
    lv_anim_set_var(&backdrop_anim, backdrop);
    lv_anim_set_values(&backdrop_anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&backdrop_anim, MODAL_ENTRANCE_DURATION_MS);
    lv_anim_set_path_cb(&backdrop_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&backdrop_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                             LV_PART_MAIN);
    });
    lv_anim_start(&backdrop_anim);

    // Scale up dialog with overshoot
    lv_anim_t scale_anim;
    lv_anim_init(&scale_anim);
    lv_anim_set_var(&scale_anim, dialog);
    lv_anim_set_values(&scale_anim, MODAL_SCALE_START, MODAL_SCALE_END);
    lv_anim_set_duration(&scale_anim, MODAL_ENTRANCE_DURATION_MS);
    lv_anim_set_path_cb(&scale_anim, lv_anim_path_overshoot);
    lv_anim_set_exec_cb(&scale_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(obj), static_cast<int16_t>(value),
                                         LV_PART_MAIN);
    });
    lv_anim_start(&scale_anim);

    // Fade in dialog
    lv_anim_t fade_anim;
    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, dialog);
    lv_anim_set_values(&fade_anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&fade_anim, MODAL_ENTRANCE_DURATION_MS);
    lv_anim_set_path_cb(&fade_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&fade_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                             LV_PART_MAIN);
    });
    lv_anim_start(&fade_anim);

    spdlog::debug("[ModalStack] Started entrance animation");
}

void ModalStack::exit_animation_done(lv_anim_t* anim) {
    lv_obj_t* backdrop = static_cast<lv_obj_t*>(anim->var);

    // Safety check: ensure backdrop is still valid (could be deleted by another path)
    if (!lv_obj_is_valid(backdrop)) {
        spdlog::debug("[ModalStack] Exit animation complete - backdrop already deleted");
        return;
    }

    // Remove from stack (animation is complete, safe to remove)
    ModalStack::instance().remove(backdrop);

    // Delete the backdrop using our safe queue (not lv_obj_delete_async which uses
    // LVGL's internal timer and could potentially fire during rendering)
    spdlog::debug("[ModalStack] Exit animation complete - deleting backdrop");
    ui_async_call(
        [](void* obj) {
            if (lv_obj_is_valid(static_cast<lv_obj_t*>(obj))) {
                lv_obj_delete(static_cast<lv_obj_t*>(obj));
            }
        },
        backdrop);
}

void ModalStack::animate_exit(lv_obj_t* backdrop, lv_obj_t* dialog) {
    if (!backdrop || !dialog) {
        return;
    }

    // Skip animation if disabled
    if (!SettingsManager::instance().get_animations_enabled()) {
        lv_obj_set_style_transform_scale(dialog, MODAL_SCALE_END, LV_PART_MAIN);
        lv_obj_set_style_opa(dialog, LV_OPA_COVER, LV_PART_MAIN);
        spdlog::debug("[ModalStack] Animations disabled - deleting modal instantly");
        ui_async_call(
            [](void* obj) {
                if (lv_obj_is_valid(static_cast<lv_obj_t*>(obj))) {
                    lv_obj_delete(static_cast<lv_obj_t*>(obj));
                }
            },
            backdrop);
        return;
    }

    // Fade out backdrop (triggers deletion on completion)
    lv_anim_t backdrop_anim;
    lv_anim_init(&backdrop_anim);
    lv_anim_set_var(&backdrop_anim, backdrop);
    lv_anim_set_values(&backdrop_anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&backdrop_anim, MODAL_EXIT_DURATION_MS);
    lv_anim_set_path_cb(&backdrop_anim, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&backdrop_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                             LV_PART_MAIN);
    });
    lv_anim_set_completed_cb(&backdrop_anim, exit_animation_done);
    lv_anim_start(&backdrop_anim);

    // Scale down dialog
    lv_anim_t scale_anim;
    lv_anim_init(&scale_anim);
    lv_anim_set_var(&scale_anim, dialog);
    lv_anim_set_values(&scale_anim, MODAL_SCALE_END, MODAL_SCALE_START);
    lv_anim_set_duration(&scale_anim, MODAL_EXIT_DURATION_MS);
    lv_anim_set_path_cb(&scale_anim, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&scale_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(obj), static_cast<int16_t>(value),
                                         LV_PART_MAIN);
    });
    lv_anim_start(&scale_anim);

    // Fade out dialog
    lv_anim_t fade_anim;
    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, dialog);
    lv_anim_set_values(&fade_anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&fade_anim, MODAL_EXIT_DURATION_MS);
    lv_anim_set_path_cb(&fade_anim, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&fade_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                             LV_PART_MAIN);
    });
    lv_anim_start(&fade_anim);

    spdlog::debug("[ModalStack] Started exit animation");
}

// ============================================================================
// MODAL CLASS - CONSTRUCTOR / DESTRUCTOR
// ============================================================================

Modal::Modal() = default;

Modal::~Modal() {
    // RAII: auto-hide if still visible
    // CRITICAL: Check if LVGL is initialized - may be during static destruction [L010]
    if (backdrop_ && lv_is_initialized()) {
        // Hide immediately without calling virtual on_hide() - derived class already destroyed
        ModalStack::instance().remove(backdrop_);
        lv_obj_delete(backdrop_);
        backdrop_ = nullptr;
        dialog_ = nullptr;
    }
    // Note: No spdlog here - logger may be destroyed during static destruction [L010]
}

// ============================================================================
// MODAL CLASS - MOVE SEMANTICS
// ============================================================================

Modal::Modal(Modal&& other) noexcept
    : backdrop_(other.backdrop_), dialog_(other.dialog_), parent_(other.parent_) {
    other.backdrop_ = nullptr;
    other.dialog_ = nullptr;
    other.parent_ = nullptr;
}

Modal& Modal::operator=(Modal&& other) noexcept {
    if (this != &other) {
        // Clean up our modal first
        // NOTE: Do NOT call virtual on_hide() here - it's unsafe to call virtual
        // functions during move operations. Callers should call hide() before
        // move-assigning if they need lifecycle hooks.
        if (backdrop_ && lv_is_initialized()) {
            ModalStack::instance().remove(backdrop_);
            lv_obj_delete(backdrop_);
        }

        // Move state
        backdrop_ = other.backdrop_;
        dialog_ = other.dialog_;
        parent_ = other.parent_;

        // Clear source
        other.backdrop_ = nullptr;
        other.dialog_ = nullptr;
        other.parent_ = nullptr;
    }
    return *this;
}

// ============================================================================
// MODAL CLASS - STATIC FACTORY API
// ============================================================================

lv_obj_t* Modal::show(const char* component_name, const char** attrs) {
    if (!component_name) {
        spdlog::error("[Modal] show() called with null component_name");
        return nullptr;
    }

    spdlog::info("[Modal] Showing modal: {}", component_name);

    lv_obj_t* parent = lv_screen_active();

    // Create backdrop programmatically (the key insight from the plan!)
    lv_obj_t* backdrop = lv_obj_create(parent);
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_align(backdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(backdrop, MODAL_BACKDROP_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(backdrop, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(backdrop, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(backdrop, 0, LV_PART_MAIN);
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);

    // Create XML component inside backdrop
    lv_obj_t* dialog = static_cast<lv_obj_t*>(lv_xml_create(backdrop, component_name, attrs));
    if (!dialog) {
        spdlog::error("[Modal] Failed to create modal from XML: {}", component_name);
        lv_obj_delete(backdrop);
        return nullptr;
    }

    // Position dialog centered
    lv_obj_align(dialog, LV_ALIGN_CENTER, 0, 0);

    // Add backdrop click handler
    lv_obj_add_event_cb(backdrop, backdrop_click_cb, LV_EVENT_CLICKED, nullptr);

    // Add ESC key handler
    lv_obj_add_event_cb(backdrop, esc_key_cb, LV_EVENT_KEY, nullptr);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_group_t* group = lv_group_get_default();
    if (group) {
        lv_group_add_obj(group, backdrop);
    }

    // Bring to foreground
    lv_obj_move_foreground(backdrop);

    // Add to stack
    ModalStack::instance().push(backdrop, dialog, component_name);

    // Animate entrance
    ModalStack::instance().animate_entrance(dialog);

    spdlog::info("[Modal] Modal shown successfully");
    return dialog;
}

void Modal::hide(lv_obj_t* dialog) {
    if (!dialog) {
        spdlog::error("[Modal] hide() called with null dialog");
        return;
    }

    // Check if LVGL is initialized (may be called during shutdown)
    if (!lv_is_initialized()) {
        spdlog::debug("[Modal] hide() called after LVGL shutdown - ignoring");
        return;
    }

    // Find backdrop for this dialog
    auto& stack = ModalStack::instance();
    lv_obj_t* backdrop = stack.backdrop_for(dialog);
    if (!backdrop) {
        spdlog::warn("[Modal] Dialog not found in stack");
        if (lv_obj_is_valid(dialog)) {
            ui_async_call(
                [](void* obj) {
                    if (lv_obj_is_valid(static_cast<lv_obj_t*>(obj))) {
                        lv_obj_delete(static_cast<lv_obj_t*>(obj));
                    }
                },
                dialog);
        }
        return;
    }

    // Check if already exiting (animation in progress) - ignore double-hide
    if (stack.is_exiting(backdrop)) {
        spdlog::debug("[Modal] Modal already exiting - ignoring hide()");
        return;
    }

    spdlog::info("[Modal] Hiding modal");

    // Mark as exiting (stays in stack until animation completes)
    stack.mark_exiting(backdrop);

    // Animate exit (animation callback will remove from stack when done)
    stack.animate_exit(backdrop, dialog);

    // If there are more visible (non-exiting) modals, bring the new topmost to foreground
    lv_obj_t* top = stack.top_dialog();
    if (top) {
        lv_obj_t* top_backdrop = stack.backdrop_for(top);
        if (top_backdrop && !stack.is_exiting(top_backdrop)) {
            lv_obj_move_foreground(top_backdrop);
        }
    }
}

lv_obj_t* Modal::get_top() {
    return ModalStack::instance().top_dialog();
}

bool Modal::any_visible() {
    return !ModalStack::instance().empty();
}

// ============================================================================
// MODAL CLASS - INSTANCE API (for subclasses)
// ============================================================================

bool Modal::show(lv_obj_t* parent, const char** attrs) {
    if (backdrop_) {
        spdlog::warn("[{}] show() called while already visible - hiding first", get_name());
        hide();
    }

    parent_ = parent ? parent : lv_screen_active();

    spdlog::info("[{}] Showing modal", get_name());

    // Register event callbacks for XML components
    lv_xml_register_event_cb(nullptr, "on_modal_ok_clicked", ok_button_cb);
    lv_xml_register_event_cb(nullptr, "on_modal_cancel_clicked", cancel_button_cb);

    // Use internal create method
    if (!create_and_show(parent_, component_name(), attrs)) {
        return false;
    }

    // Call hook for subclass customization
    on_show();

    spdlog::debug("[{}] Modal shown successfully", get_name());
    return true;
}

void Modal::hide() {
    if (!backdrop_) {
        return; // Already hidden, safe to call multiple times
    }

    // Check if already exiting (animation in progress)
    if (ModalStack::instance().is_exiting(backdrop_)) {
        spdlog::debug("[{}] Modal already exiting - ignoring hide()", get_name());
        return;
    }

    spdlog::info("[{}] Hiding modal", get_name());

    // Call hook before destruction
    on_hide();

    // Save pointers before clearing (needed for animation)
    lv_obj_t* backdrop = backdrop_;
    lv_obj_t* dialog = dialog_;

    // Clear our pointers first (so is_visible() returns false during animation)
    backdrop_ = nullptr;
    dialog_ = nullptr;

    // Mark as exiting (stays in stack until animation completes)
    ModalStack::instance().mark_exiting(backdrop);

    // Animate exit (animation callback will remove from stack when done)
    ModalStack::instance().animate_exit(backdrop, dialog);

    spdlog::debug("[{}] Modal hidden", get_name());
}

// ============================================================================
// MODAL CLASS - HELPERS
// ============================================================================

lv_obj_t* Modal::find_widget(const char* name) {
    if (!dialog_ || !name) {
        return nullptr;
    }
    return lv_obj_find_by_name(dialog_, name);
}

void Modal::wire_ok_button(const char* name) {
    lv_obj_t* btn = find_widget(name);
    if (btn) {
        lv_obj_set_user_data(btn, this);
        spdlog::trace("[{}] Wired Ok button '{}'", get_name(), name);
    } else {
        spdlog::warn("[{}] Ok button '{}' not found", get_name(), name);
    }
}

void Modal::wire_cancel_button(const char* name) {
    lv_obj_t* btn = find_widget(name);
    if (btn) {
        lv_obj_set_user_data(btn, this);
        spdlog::trace("[{}] Wired Cancel button '{}'", get_name(), name);
    } else {
        spdlog::warn("[{}] Cancel button '{}' not found", get_name(), name);
    }
}

// ============================================================================
// MODAL CLASS - INTERNAL
// ============================================================================

bool Modal::create_and_show(lv_obj_t* parent, const char* comp_name, const char** attrs) {
    // Create backdrop programmatically
    backdrop_ = lv_obj_create(parent);
    lv_obj_set_size(backdrop_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(backdrop_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(backdrop_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(backdrop_, MODAL_BACKDROP_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(backdrop_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(backdrop_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(backdrop_, 0, LV_PART_MAIN);
    lv_obj_remove_flag(backdrop_, LV_OBJ_FLAG_SCROLLABLE);

    // Create XML component inside backdrop
    dialog_ = static_cast<lv_obj_t*>(lv_xml_create(backdrop_, comp_name, attrs));
    if (!dialog_) {
        spdlog::error("[{}] Failed to create modal from XML component '{}'", get_name(), comp_name);
        lv_obj_delete(backdrop_);
        backdrop_ = nullptr;
        return false;
    }

    // Position dialog centered
    lv_obj_align(dialog_, LV_ALIGN_CENTER, 0, 0);

    // Add backdrop click handler
    lv_obj_add_event_cb(backdrop_, backdrop_click_cb, LV_EVENT_CLICKED, this);

    // Add ESC key handler
    lv_obj_add_event_cb(backdrop_, esc_key_cb, LV_EVENT_KEY, this);
    lv_obj_add_flag(backdrop_, LV_OBJ_FLAG_CLICKABLE);
    lv_group_t* group = lv_group_get_default();
    if (group) {
        lv_group_add_obj(group, backdrop_);
    }

    // Bring to foreground
    lv_obj_move_foreground(backdrop_);

    // Add to stack
    ModalStack::instance().push(backdrop_, dialog_, comp_name);

    // Animate entrance
    ModalStack::instance().animate_entrance(dialog_);

    return true;
}

void Modal::destroy() {
    if (backdrop_) {
        ModalStack::instance().remove(backdrop_);
        lv_obj_delete(backdrop_);
        backdrop_ = nullptr;
        dialog_ = nullptr;
    }
}

// ============================================================================
// MODAL CLASS - STATIC EVENT HANDLERS
// ============================================================================

void Modal::backdrop_click_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[Modal] backdrop_click_cb");

    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* current_target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));

    // Only respond if click was directly on backdrop (not bubbled from child)
    if (target != current_target) {
        return;
    }

    auto* self = static_cast<Modal*>(lv_event_get_user_data(e));
    if (self) {
        // Instance modal
        spdlog::debug("[{}] Backdrop clicked - closing", self->get_name());
        self->hide();
    } else {
        // Static modal - find in stack and close topmost
        auto& stack = ModalStack::instance();
        lv_obj_t* top_dialog = stack.top_dialog();
        lv_obj_t* top_backdrop = top_dialog ? stack.backdrop_for(top_dialog) : nullptr;

        if (top_backdrop == current_target) {
            spdlog::debug("[Modal] Backdrop clicked on topmost modal - closing");
            Modal::hide(top_dialog);
        }
    }

    LVGL_SAFE_EVENT_CB_END();
}

void Modal::esc_key_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[Modal] esc_key_cb");

    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_ESC) {
        return;
    }

    auto* self = static_cast<Modal*>(lv_event_get_user_data(e));
    if (self) {
        // Instance modal - call on_cancel to allow override
        spdlog::debug("[{}] ESC key pressed - closing", self->get_name());
        self->on_cancel();
    } else {
        // Static modal - hide topmost
        lv_obj_t* top = ModalStack::instance().top_dialog();
        if (top) {
            spdlog::debug("[Modal] ESC key pressed - closing topmost modal");
            Modal::hide(top);
        }
    }

    LVGL_SAFE_EVENT_CB_END();
}

void Modal::ok_button_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[Modal] ok_button_cb");

    // Get Modal instance from button's user_data (set by wire_ok_button)
    // Note: lv_event_get_user_data returns NULL for XML-registered callbacks,
    // so we use lv_obj_get_user_data on the target button instead
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto* self = static_cast<Modal*>(lv_obj_get_user_data(btn));
    if (self) {
        spdlog::debug("[{}] Ok button clicked", self->get_name());
        self->on_ok();
    }

    LVGL_SAFE_EVENT_CB_END();
}

void Modal::cancel_button_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[Modal] cancel_button_cb");

    // Get Modal instance from button's user_data (set by wire_cancel_button)
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto* self = static_cast<Modal*>(lv_obj_get_user_data(btn));
    if (self) {
        spdlog::debug("[{}] Cancel button clicked", self->get_name());
        self->on_cancel();
    }

    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// MODAL DIALOG SUBJECTS
// ============================================================================

void modal_init_subjects() {
    if (g_subjects_initialized) {
        spdlog::warn("[Modal] Subjects already initialized - skipping");
        return;
    }

    spdlog::debug("[Modal] Initializing modal dialog subjects");

    // Initialize integer subjects
    lv_subject_init_int(&g_dialog_severity, static_cast<int>(ModalSeverity::Info));
    lv_subject_init_int(&g_dialog_show_cancel, 0);

    // Initialize string subjects (pointer type)
    lv_subject_init_pointer(&g_dialog_primary_text, const_cast<char*>(DEFAULT_PRIMARY_TEXT));
    lv_subject_init_pointer(&g_dialog_cancel_text, const_cast<char*>(DEFAULT_CANCEL_TEXT));

    // Register with LVGL XML system for binding
    lv_xml_register_subject(nullptr, "dialog_severity", &g_dialog_severity);
    lv_xml_register_subject(nullptr, "dialog_show_cancel", &g_dialog_show_cancel);
    lv_xml_register_subject(nullptr, "dialog_primary_text", &g_dialog_primary_text);
    lv_xml_register_subject(nullptr, "dialog_cancel_text", &g_dialog_cancel_text);

    g_subjects_initialized = true;
    spdlog::debug("[Modal] Modal dialog subjects registered");
}

void modal_configure(ModalSeverity severity, bool show_cancel, const char* primary_text,
                     const char* cancel_text) {
    if (!g_subjects_initialized) {
        spdlog::error("[Modal] Cannot configure - subjects not initialized!");
        return;
    }

    spdlog::debug("[Modal] Configuring dialog: severity={}, show_cancel={}, primary='{}', "
                  "cancel='{}'",
                  static_cast<int>(severity), show_cancel, primary_text ? primary_text : "(null)",
                  cancel_text ? cancel_text : "(null)");

    lv_subject_set_int(&g_dialog_severity, static_cast<int>(severity));
    lv_subject_set_int(&g_dialog_show_cancel, show_cancel ? 1 : 0);

    if (primary_text) {
        lv_subject_set_pointer(&g_dialog_primary_text, const_cast<char*>(primary_text));
    }
    if (cancel_text) {
        lv_subject_set_pointer(&g_dialog_cancel_text, const_cast<char*>(cancel_text));
    }
}

lv_subject_t* modal_severity_subject() {
    return &g_dialog_severity;
}

lv_subject_t* modal_show_cancel_subject() {
    return &g_dialog_show_cancel;
}

lv_subject_t* modal_primary_text_subject() {
    return &g_dialog_primary_text;
}

lv_subject_t* modal_cancel_text_subject() {
    return &g_dialog_cancel_text;
}

// ============================================================================
// KEYBOARD REGISTRATION
// ============================================================================

void ui_modal_register_keyboard(lv_obj_t* modal, lv_obj_t* textarea) {
    if (!modal || !textarea) {
        spdlog::error("[Modal] Cannot register keyboard: modal={}, textarea={}", (void*)modal,
                      (void*)textarea);
        return;
    }

    // Position keyboard at bottom-center (default for modals)
    ui_keyboard_set_position(LV_ALIGN_BOTTOM_MID, 0, 0);

    // Check if this is a password textarea
    bool is_password = lv_textarea_get_password_mode(textarea);

    if (is_password) {
        ui_keyboard_register_textarea_ex(textarea, true);
        spdlog::debug("[Modal] Registered PASSWORD textarea with keyboard");
    } else {
        ui_keyboard_register_textarea(textarea);
        spdlog::debug("[Modal] Registered textarea with keyboard");
    }
}

// ============================================================================
// CONFIRMATION DIALOG HELPERS
// ============================================================================

lv_obj_t* ui_modal_show_confirmation(const char* title, const char* message, ModalSeverity severity,
                                     const char* confirm_text, lv_event_cb_t on_confirm,
                                     lv_event_cb_t on_cancel, void* user_data) {
    if (!title || !message) {
        spdlog::error("[Modal] show_confirmation: title and message are required");
        return nullptr;
    }

    // Build attributes array for modal_dialog
    const char* attrs[] = {"title", title, "message", message, nullptr};

    // Configure modal with severity and button text
    modal_configure(severity, true, confirm_text ? confirm_text : "OK", "Cancel");

    // Show the modal
    lv_obj_t* dialog = Modal::show("modal_dialog", attrs);
    if (!dialog) {
        spdlog::error("[Modal] Failed to create confirmation dialog: '{}'", title);
        return nullptr;
    }

    // Wire up cancel button (btn_secondary in modal_dialog)
    if (on_cancel) {
        lv_obj_t* cancel_btn = lv_obj_find_by_name(dialog, "btn_secondary");
        if (cancel_btn) {
            lv_obj_add_event_cb(cancel_btn, on_cancel, LV_EVENT_CLICKED, user_data);
        } else {
            spdlog::warn("[Modal] btn_secondary not found - cancel callback not wired");
        }
    }

    // Wire up confirm button (btn_primary in modal_dialog)
    if (on_confirm) {
        lv_obj_t* confirm_btn = lv_obj_find_by_name(dialog, "btn_primary");
        if (confirm_btn) {
            lv_obj_add_event_cb(confirm_btn, on_confirm, LV_EVENT_CLICKED, user_data);
        } else {
            spdlog::warn("[Modal] btn_primary not found - confirm callback not wired");
        }
    }

    spdlog::debug("[Modal] Confirmation dialog shown: '{}'", title);
    return dialog;
}

lv_obj_t* ui_modal_show_alert(const char* title, const char* message, ModalSeverity severity,
                              const char* ok_text, lv_event_cb_t on_ok, void* user_data) {
    if (!title || !message) {
        spdlog::error("[Modal] show_alert: title and message are required");
        return nullptr;
    }

    // Build attributes array for modal_dialog
    const char* attrs[] = {"title", title, "message", message, nullptr};

    // Configure modal: no cancel button
    modal_configure(severity, false, ok_text ? ok_text : "OK", nullptr);

    // Show the modal
    lv_obj_t* dialog = Modal::show("modal_dialog", attrs);
    if (!dialog) {
        spdlog::error("[Modal] Failed to create alert dialog: '{}'", title);
        return nullptr;
    }

    // Wire up OK button if callback provided
    if (on_ok) {
        lv_obj_t* ok_btn = lv_obj_find_by_name(dialog, "btn_primary");
        if (ok_btn) {
            lv_obj_add_event_cb(ok_btn, on_ok, LV_EVENT_CLICKED, user_data);
        } else {
            spdlog::warn("[Modal] btn_primary not found - OK callback not wired");
        }
    }

    spdlog::debug("[Modal] Alert dialog shown: '{}'", title);
    return dialog;
}
