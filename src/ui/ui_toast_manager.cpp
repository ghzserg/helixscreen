// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_toast_manager.h"

#include "ui_notification_history.h"
#include "ui_notification_manager.h"
#include "ui_update_queue.h"
#include "ui_utils.h"

#include "settings_manager.h"
#include "sound_manager.h"
#include "static_subject_registry.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <string>

// ============================================================================
// ANIMATION CONSTANTS
// ============================================================================
// Duration values match globals.xml tokens for consistency
static constexpr int32_t TOAST_ENTRANCE_DURATION_MS = 200; // anim_normal - 50ms for snappier feel
static constexpr int32_t TOAST_EXIT_DURATION_MS = 150;     // anim_fast
static constexpr int32_t TOAST_ENTRANCE_OFFSET_Y = -30;    // Slide down from above

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

ToastManager& ToastManager::instance() {
    static ToastManager instance;
    return instance;
}

ToastManager::~ToastManager() {
    // Clean up timer - must be deleted explicitly before LVGL shutdown
    // Check lv_is_initialized() to avoid crash during static destruction
    if (lv_is_initialized()) {
        if (dismiss_timer_) {
            lv_timer_delete(dismiss_timer_);
            dismiss_timer_ = nullptr;
        }
    }
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Convert ToastSeverity enum to string for logging
static const char* severity_to_string(ToastSeverity severity) {
    switch (severity) {
    case ToastSeverity::ERROR:
        return "error";
    case ToastSeverity::WARNING:
        return "warning";
    case ToastSeverity::SUCCESS:
        return "success";
    case ToastSeverity::INFO:
    default:
        return "info";
    }
}

// Convert ToastSeverity enum to int for subject binding (0=info, 1=success, 2=warning, 3=error)
static int severity_to_int(ToastSeverity severity) {
    switch (severity) {
    case ToastSeverity::INFO:
        return 0;
    case ToastSeverity::SUCCESS:
        return 1;
    case ToastSeverity::WARNING:
        return 2;
    case ToastSeverity::ERROR:
        return 3;
    default:
        return 0;
    }
}

static NotificationStatus severity_to_notification_status(ToastSeverity severity) {
    switch (severity) {
    case ToastSeverity::INFO:
        return NotificationStatus::INFO;
    case ToastSeverity::SUCCESS:
        return NotificationStatus::INFO; // Treat success as info in status bar
    case ToastSeverity::WARNING:
        return NotificationStatus::WARNING;
    case ToastSeverity::ERROR:
        return NotificationStatus::ERROR;
    default:
        return NotificationStatus::NONE;
    }
}

// ============================================================================
// ANIMATION HELPERS
// ============================================================================

void ToastManager::animate_entrance(lv_obj_t* toast) {
    // Skip animation if disabled - just show toast in final state
    if (!SettingsManager::instance().get_animations_enabled()) {
        lv_obj_set_style_translate_y(toast, 0, LV_PART_MAIN);
        lv_obj_set_style_opa(toast, LV_OPA_COVER, LV_PART_MAIN);
        spdlog::debug("[ToastManager] Animations disabled - showing toast instantly");
        return;
    }

    // Start toast above its final position and transparent
    lv_obj_set_style_translate_y(toast, TOAST_ENTRANCE_OFFSET_Y, LV_PART_MAIN);
    lv_obj_set_style_opa(toast, LV_OPA_TRANSP, LV_PART_MAIN);

    // Slide down animation (translate_y: -30 → 0)
    lv_anim_t slide_anim;
    lv_anim_init(&slide_anim);
    lv_anim_set_var(&slide_anim, toast);
    lv_anim_set_values(&slide_anim, TOAST_ENTRANCE_OFFSET_Y, 0);
    lv_anim_set_duration(&slide_anim, TOAST_ENTRANCE_DURATION_MS);
    lv_anim_set_path_cb(&slide_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&slide_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_translate_y(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
    });
    lv_anim_start(&slide_anim);

    // Fade in animation (opacity: 0 → 255)
    lv_anim_t fade_anim;
    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, toast);
    lv_anim_set_values(&fade_anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&fade_anim, TOAST_ENTRANCE_DURATION_MS);
    lv_anim_set_path_cb(&fade_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&fade_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                             LV_PART_MAIN);
    });
    lv_anim_start(&fade_anim);

    spdlog::debug("[ToastManager] Started entrance animation");
}

void ToastManager::animate_exit(lv_obj_t* toast) {
    // Skip animation if disabled - directly clean up
    if (!SettingsManager::instance().get_animations_enabled()) {
        // Directly delete the toast - no animation
        if (toast && active_toast_ == toast) {
            lv_obj_safe_delete(active_toast_);
            animating_exit_ = false;
            spdlog::debug("[ToastManager] Animations disabled - hiding toast instantly");
        }
        return;
    }

    // Fade out animation (opacity: current → 0)
    lv_anim_t fade_anim;
    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, toast);
    lv_anim_set_values(&fade_anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&fade_anim, TOAST_EXIT_DURATION_MS);
    lv_anim_set_path_cb(&fade_anim, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&fade_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                             LV_PART_MAIN);
    });
    lv_anim_set_completed_cb(&fade_anim, exit_animation_complete_cb);
    lv_anim_start(&fade_anim);

    spdlog::debug("[ToastManager] Started exit animation");
}

void ToastManager::exit_animation_complete_cb(lv_anim_t* anim) {
    lv_obj_t* toast = static_cast<lv_obj_t*>(anim->var);
    auto& mgr = ToastManager::instance();

    // Delete the toast widget now that animation is complete
    if (toast && mgr.active_toast_ == toast) {
        // Remove from focus group BEFORE deleting to prevent LVGL from
        // auto-focusing the next element (which triggers scroll-on-focus)
        lv_group_t* group = lv_group_get_default();
        if (group) {
            lv_group_remove_obj(toast);
        }

        lv_obj_safe_delete(mgr.active_toast_);
        mgr.animating_exit_ = false;
        spdlog::debug("[ToastManager] Exit animation complete, toast deleted");
    }
}

// ============================================================================
// TOAST MANAGER IMPLEMENTATION
// ============================================================================

void ToastManager::init() {
    if (initialized_) {
        spdlog::warn("[ToastManager] Already initialized - skipping");
        return;
    }

    // Action button subjects
    lv_subject_init_int(&action_visible_subject_, 0);
    lv_xml_register_subject(nullptr, "toast_action_visible", &action_visible_subject_);

    lv_subject_init_pointer(&action_text_subject_, action_text_buf_);
    lv_xml_register_subject(nullptr, "toast_action_text", &action_text_subject_);

    // Severity subject (0=info, 1=success, 2=warning, 3=error)
    lv_subject_init_int(&severity_subject_, 0);
    lv_xml_register_subject(nullptr, "toast_severity", &severity_subject_);

    // Register callback for XML event_cb to work
    lv_xml_register_event_cb(nullptr, "toast_close_btn_clicked", close_btn_clicked);

    // Register subject cleanup for proper shutdown ordering
    StaticSubjectRegistry::instance().register_deinit(
        "ToastManager", []() { ToastManager::instance().deinit_subjects(); });

    initialized_ = true;
    spdlog::debug("[ToastManager] Toast notification system initialized");
}

void ToastManager::deinit_subjects() {
    if (!initialized_) {
        return;
    }

    if (!lv_is_initialized()) {
        initialized_ = false;
        return;
    }

    // Deinit subjects - removes all observers AND their event callbacks from LVGL objects.
    // Must happen before lv_deinit() so widget deletion doesn't hit dangling observer pointers.
    lv_subject_deinit(&severity_subject_);
    lv_subject_deinit(&action_text_subject_);
    lv_subject_deinit(&action_visible_subject_);

    initialized_ = false;
    spdlog::debug("[ToastManager] Subjects deinitialized");
}

void ToastManager::show(ToastSeverity severity, const char* message, uint32_t duration_ms) {
    create_toast_internal(severity, message, duration_ms, false);
}

void ToastManager::show_with_action(ToastSeverity severity, const char* message,
                                    const char* action_text, toast_action_callback_t callback,
                                    void* user_data, uint32_t duration_ms) {
    if (!action_text || !callback) {
        spdlog::warn("[ToastManager] Toast action requires action_text and callback");
        show(severity, message, duration_ms);
        return;
    }

    // Store callback for when action button is clicked
    action_callback_ = callback;
    action_user_data_ = user_data;

    // Update action button text and visibility via subjects
    snprintf(action_text_buf_, sizeof(action_text_buf_), "%s", action_text);
    lv_subject_set_pointer(&action_text_subject_, action_text_buf_);
    lv_subject_set_int(&action_visible_subject_, 1);

    create_toast_internal(severity, message, duration_ms, true);
}

void ToastManager::hide() {
    if (!active_toast_ || animating_exit_) {
        return;
    }

    // Cancel dismiss timer if active
    if (dismiss_timer_) {
        lv_timer_delete(dismiss_timer_);
        dismiss_timer_ = nullptr;
    }

    // Clear action state
    action_callback_ = nullptr;
    action_user_data_ = nullptr;
    lv_subject_set_int(&action_visible_subject_, 0);

    // Update bell color based on highest unread severity in history
    ToastSeverity highest = NotificationHistory::instance().get_highest_unread_severity();
    size_t unread = NotificationHistory::instance().get_unread_count();

    if (unread == 0) {
        ui_status_bar_update_notification(NotificationStatus::NONE);
    } else {
        ui_status_bar_update_notification(severity_to_notification_status(highest));
    }

    // Animate exit (widget deletion happens in completion callback)
    animating_exit_ = true;
    animate_exit(active_toast_);

    spdlog::debug("[ToastManager] Toast hiding with animation");
}

bool ToastManager::is_visible() const {
    return active_toast_ != nullptr;
}

void ToastManager::create_toast_internal(ToastSeverity severity, const char* message,
                                         uint32_t duration_ms, bool with_action) {
    if (!message) {
        spdlog::warn("[ToastManager] Attempted to show toast with null message");
        return;
    }

    // Immediately delete existing toast if any (skip animation for replacement)
    if (active_toast_) {
        // Take ownership of the old toast pointer and nullify the member FIRST.
        // This prevents exit_animation_complete_cb from also deleting the object
        // if lv_anim_delete triggers the completion callback synchronously.
        lv_obj_t* old_toast = active_toast_;
        active_toast_ = nullptr;
        animating_exit_ = false;

        // Cancel any running animations on the old toast
        lv_anim_delete(old_toast, nullptr);

        // Cancel dismiss timer if active
        if (dismiss_timer_) {
            lv_timer_delete(dismiss_timer_);
            dismiss_timer_ = nullptr;
        }

        // Remove from focus group before deleting
        lv_group_t* group = lv_group_get_default();
        if (group) {
            lv_group_remove_obj(old_toast);
        }

        lv_obj_delete(old_toast);
    }

    // Clear action state for basic toasts, keep for action toasts
    if (!with_action) {
        action_callback_ = nullptr;
        action_user_data_ = nullptr;
        lv_subject_set_int(&action_visible_subject_, 0);
    }

    // Set severity subject BEFORE creating toast (XML bindings read it during creation)
    lv_subject_set_int(&severity_subject_, severity_to_int(severity));

    // Create toast via XML component
    const char* attrs[] = {"message", message, nullptr};

    lv_obj_t* layer = lv_layer_top();
    active_toast_ = static_cast<lv_obj_t*>(lv_xml_create(layer, "toast_notification", attrs));

    if (!active_toast_) {
        spdlog::error("[ToastManager] Failed to create toast notification widget");
        return;
    }

    // Wire up action button callback (if showing action toast)
    if (with_action) {
        lv_obj_t* action_btn = lv_obj_find_by_name(active_toast_, "toast_action_btn");
        if (action_btn) {
            lv_obj_add_event_cb(action_btn, action_btn_clicked, LV_EVENT_CLICKED, nullptr);
        }
    }

    // Start entrance animation (slide down + fade in)
    animate_entrance(active_toast_);

    // Create auto-dismiss timer
    dismiss_timer_ = lv_timer_create(dismiss_timer_cb, duration_ms, nullptr);
    lv_timer_set_repeat_count(dismiss_timer_, 1); // Run once then stop

    // Update status bar notification icon
    ui_status_bar_update_notification(severity_to_notification_status(severity));

    // Play error sound for error toasts (uses EVENT priority so it's not affected by
    // ui_sounds_enabled)
    if (severity == ToastSeverity::ERROR) {
        SoundManager::instance().play("error_tone", SoundPriority::EVENT);
    }

    spdlog::debug("[ToastManager] Toast shown: [{}] {} ({}ms, action={})",
                  severity_to_string(severity), message, duration_ms, with_action);
}

void ToastManager::dismiss_timer_cb(lv_timer_t* timer) {
    (void)timer;
    ToastManager::instance().hide();
}

void ToastManager::close_btn_clicked(lv_event_t* e) {
    (void)e;
    ToastManager::instance().hide();
}

void ToastManager::action_btn_clicked(lv_event_t* e) {
    (void)e;

    auto& mgr = ToastManager::instance();

    // Store callback before hiding (hide clears action_callback)
    toast_action_callback_t cb = mgr.action_callback_;
    void* data = mgr.action_user_data_;

    // Hide the toast first
    mgr.hide();

    // Then invoke the callback
    if (cb) {
        spdlog::debug("[ToastManager] Toast action button clicked - invoking callback");
        cb(data);
    }
}

// ============================================================================
// LEGACY API (forwards to ToastManager)
// ============================================================================

void ui_toast_init() {
    ToastManager::instance().init();
}

// Thread-safe toast showing - can be called from any thread
// Uses ui_queue_update to defer to main thread if needed
void ui_toast_show(ToastSeverity severity, const char* message, uint32_t duration_ms) {
    // Capture parameters by value (copy strings to heap)
    struct ToastParams {
        ToastSeverity severity;
        std::string message;
        uint32_t duration_ms;
    };

    auto params =
        std::make_unique<ToastParams>(ToastParams{severity, message ? message : "", duration_ms});

    ui_queue_update<ToastParams>(std::move(params), [](ToastParams* p) {
        ToastManager::instance().show(p->severity, p->message.c_str(), p->duration_ms);
    });
}

void ui_toast_show_with_action(ToastSeverity severity, const char* message, const char* action_text,
                               toast_action_callback_t action_callback, void* user_data,
                               uint32_t duration_ms) {
    // Capture parameters by value (copy strings to heap)
    struct ToastActionParams {
        ToastSeverity severity;
        std::string message;
        std::string action_text;
        toast_action_callback_t action_callback;
        void* user_data;
        uint32_t duration_ms;
    };

    auto params = std::make_unique<ToastActionParams>(
        ToastActionParams{severity, message ? message : "", action_text ? action_text : "",
                          action_callback, user_data, duration_ms});

    ui_queue_update<ToastActionParams>(std::move(params), [](ToastActionParams* p) {
        ToastManager::instance().show_with_action(p->severity, p->message.c_str(),
                                                  p->action_text.c_str(), p->action_callback,
                                                  p->user_data, p->duration_ms);
    });
}

void ui_toast_hide() {
    ui_async_call([](void*) { ToastManager::instance().hide(); }, nullptr);
}

bool ui_toast_is_visible() {
    return ToastManager::instance().is_visible();
}
