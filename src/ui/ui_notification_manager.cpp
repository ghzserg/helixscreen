// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_notification_manager.h"

#include "ui_nav_manager.h"
#include "ui_panel_notification_history.h"
#include "ui_utils.h"

#include "settings_manager.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <cstring>

using namespace helix;

// Forward declaration for class-based API
NotificationHistoryPanel& get_global_notification_history_panel();

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

NotificationManager& NotificationManager::instance() {
    static NotificationManager instance;
    return instance;
}

// ============================================================================
// NOTIFICATION SEVERITY CONSTANTS
// ============================================================================

enum NotificationSeverityState {
    NOTIFICATION_SEVERITY_INFO = 0,    // Blue badge
    NOTIFICATION_SEVERITY_WARNING = 1, // Orange badge
    NOTIFICATION_SEVERITY_ERROR = 2    // Red badge
};

void NotificationManager::notification_history_clicked([[maybe_unused]] lv_event_t* e) {
    spdlog::info("[NotificationManager] Notification history button CLICKED!");

    auto& mgr = NotificationManager::instance();

    // Prevent multiple panel instances - if panel already exists and is visible, ignore click
    if (mgr.notification_panel_obj_ && lv_obj_is_valid(mgr.notification_panel_obj_) &&
        !lv_obj_has_flag(mgr.notification_panel_obj_, LV_OBJ_FLAG_HIDDEN)) {
        spdlog::debug("[NotificationManager] Notification panel already visible, ignoring click");
        return;
    }

    lv_obj_t* parent = lv_screen_active();

    // Get panel instance and init subjects BEFORE creating XML
    auto& panel = get_global_notification_history_panel();
    if (!panel.are_subjects_initialized()) {
        panel.init_subjects();
    }

    // Clean up old panel if it exists but is hidden/invalid
    helix::ui::safe_delete(mgr.notification_panel_obj_);

    // Now create XML component
    lv_obj_t* panel_obj =
        static_cast<lv_obj_t*>(lv_xml_create(parent, "notification_history_panel", nullptr));
    if (!panel_obj) {
        spdlog::error("[NotificationManager] Failed to create notification_history_panel from XML");
        return;
    }

    // Store reference for duplicate prevention
    mgr.notification_panel_obj_ = panel_obj;

    // Setup panel (wires buttons, refreshes list)
    panel.setup(panel_obj, parent);

    NavigationManager::instance().push_overlay(panel_obj);
}

// ============================================================================
// NOTIFICATION MANAGER IMPLEMENTATION
// ============================================================================

void NotificationManager::register_callbacks() {
    if (callbacks_registered_) {
        spdlog::warn("[NotificationManager] Callbacks already registered");
        return;
    }

    // Register notification history callback (must be called BEFORE app_layout XML is created)
    lv_xml_register_event_cb(nullptr, "status_notification_history_clicked",
                             notification_history_clicked);
    callbacks_registered_ = true;
    spdlog::debug("[NotificationManager] Event callbacks registered");
}

void NotificationManager::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[NotificationManager] Subjects already initialized");
        return;
    }

    spdlog::trace("[NotificationManager] Initializing notification subjects...");

    // Initialize all subjects with default values using managed macros
    // Notification badge starts hidden (count = 0)
    UI_MANAGED_SUBJECT_INT(notification_count_subject_, 0, "notification_count", subjects_);
    UI_MANAGED_SUBJECT_POINTER(notification_count_text_subject_, notification_count_text_buf_,
                               "notification_count_text", subjects_);
    UI_MANAGED_SUBJECT_INT(notification_severity_subject_, NOTIFICATION_SEVERITY_INFO,
                           "notification_severity", subjects_);

    subjects_initialized_ = true;

    // Self-register cleanup — ensures deinit runs before lv_deinit()
    StaticPanelRegistry::instance().register_destroy(
        "StatusBarSubjects", []() { NotificationManager::instance().deinit_subjects(); });

    spdlog::trace("[NotificationManager] Subjects initialized and registered");
}

void NotificationManager::init() {
    if (initialized_) {
        spdlog::warn("[NotificationManager] Already initialized");
        return;
    }

    spdlog::debug("[NotificationManager] init() called");

    // Ensure subjects are initialized
    if (!subjects_initialized_) {
        init_subjects();
    }

    initialized_ = true;
    spdlog::debug("[NotificationManager] Initialization complete");
}

void NotificationManager::update_notification(NotificationStatus status) {
    if (!subjects_initialized_) {
        spdlog::warn("[NotificationManager] Subjects not initialized, cannot update notification");
        return;
    }

    int32_t severity;

    switch (status) {
    case NotificationStatus::ERROR:
        severity = NOTIFICATION_SEVERITY_ERROR;
        spdlog::debug("[NotificationManager] Notification severity ERROR -> state 2");
        break;
    case NotificationStatus::WARNING:
        severity = NOTIFICATION_SEVERITY_WARNING;
        spdlog::debug("[NotificationManager] Notification severity WARNING -> state 1");
        break;
    case NotificationStatus::INFO:
    case NotificationStatus::NONE:
    default:
        severity = NOTIFICATION_SEVERITY_INFO;
        spdlog::debug("[NotificationManager] Notification severity INFO -> state 0");
        break;
    }

    lv_subject_set_int(&notification_severity_subject_, severity);
}

void NotificationManager::update_notification_count(size_t count) {
    if (!subjects_initialized_) {
        spdlog::trace(
            "[NotificationManager] Subjects not initialized, cannot update notification count");
        return;
    }

    // Trigger pulse animation if count increased (new notification arrived)
    bool should_pulse = (count > previous_notification_count_) && (count > 0);
    previous_notification_count_ = count;

    lv_subject_set_int(&notification_count_subject_, static_cast<int32_t>(count));

    snprintf(notification_count_text_buf_, sizeof(notification_count_text_buf_), "%zu", count);
    lv_subject_set_pointer(&notification_count_text_subject_, notification_count_text_buf_);

    // Pulse the badge to draw attention
    if (should_pulse) {
        animate_notification_badge();
    }

    spdlog::trace("[NotificationManager] Notification count updated: {}", count);
}

void NotificationManager::animate_notification_badge() {
    // Skip animation if disabled
    if (!SettingsManager::instance().get_animations_enabled()) {
        spdlog::debug("[NotificationManager] Animations disabled - skipping badge pulse");
        return;
    }

    // Find the notification badge on the active screen
    lv_obj_t* screen = lv_screen_active();
    if (!screen)
        return;

    lv_obj_t* badge = lv_obj_find_by_name(screen, "notification_badge");
    if (!badge)
        return;

    // Animation constants for attention pulse
    // Stage 1: Scale up to 130% (300ms with overshoot)
    // Stage 2: Scale back to 100% (implicit - overshoot settles naturally)
    constexpr int32_t PULSE_DURATION_MS = 300;
    constexpr int32_t SCALE_NORMAL = 256; // 100%
    constexpr int32_t SCALE_PULSE = 333;  // ~130%

    // Scale up animation with overshoot easing (automatically bounces back)
    lv_anim_t scale_anim;
    lv_anim_init(&scale_anim);
    lv_anim_set_var(&scale_anim, badge);
    lv_anim_set_values(&scale_anim, SCALE_NORMAL, SCALE_PULSE);
    lv_anim_set_duration(&scale_anim, PULSE_DURATION_MS);
    lv_anim_set_path_cb(&scale_anim, lv_anim_path_overshoot);
    lv_anim_set_playback_duration(&scale_anim, PULSE_DURATION_MS / 2);
    lv_anim_set_exec_cb(&scale_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
    });
    lv_anim_start(&scale_anim);

    spdlog::debug("[NotificationManager] Notification badge pulse animation started");
}

void NotificationManager::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::debug("[NotificationManager] Subjects deinitialized");
}

// ============================================================================
// FREE FUNCTIONS (helix::ui namespace)
// ============================================================================

void helix::ui::notification_register_callbacks() {
    NotificationManager::instance().register_callbacks();
}

void helix::ui::notification_init_subjects() {
    NotificationManager::instance().init_subjects();
}

void helix::ui::notification_manager_init() {
    NotificationManager::instance().init();
}

void helix::ui::notification_update(NotificationStatus status) {
    NotificationManager::instance().update_notification(status);
}

void helix::ui::notification_update_count(size_t count) {
    NotificationManager::instance().update_notification_count(count);
}

void helix::ui::notification_deinit_subjects() {
    NotificationManager::instance().deinit_subjects();
}
