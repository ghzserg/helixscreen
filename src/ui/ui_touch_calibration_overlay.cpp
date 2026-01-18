// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_touch_calibration_overlay.h"

#include "ui_event_safety.h"
#include "ui_nav_manager.h"
#include "ui_toast_manager.h"

#include "config.h"
#include "display_manager.h"
#include "static_panel_registry.h"
#include "touch_calibration.h"

#include <spdlog/spdlog.h>

#include <cstring>

namespace helix::ui {

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<TouchCalibrationOverlay> g_touch_calibration_overlay;

TouchCalibrationOverlay& get_touch_calibration_overlay() {
    if (!g_touch_calibration_overlay) {
        g_touch_calibration_overlay = std::make_unique<TouchCalibrationOverlay>();
        StaticPanelRegistry::instance().register_destroy(
            "TouchCalibrationOverlay", []() { g_touch_calibration_overlay.reset(); });
    }
    return *g_touch_calibration_overlay;
}

// ============================================================================
// Static Trampolines for LVGL Callbacks
// ============================================================================

static void on_touch_cal_start_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[TouchCalibrationOverlay] start clicked");
    get_touch_calibration_overlay().handle_start_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

static void on_touch_cal_accept_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[TouchCalibrationOverlay] accept clicked");
    get_touch_calibration_overlay().handle_accept_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

static void on_touch_cal_retry_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[TouchCalibrationOverlay] retry clicked");
    get_touch_calibration_overlay().handle_retry_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

static void on_touch_cal_skip_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[TouchCalibrationOverlay] skip clicked");
    get_touch_calibration_overlay().handle_skip_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

static void on_touch_cal_overlay_touched(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[TouchCalibrationOverlay] screen touched");
    get_touch_calibration_overlay().handle_screen_touched(e);
    LVGL_SAFE_EVENT_CB_END();
}

static void on_touch_cal_back_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[TouchCalibrationOverlay] back clicked");
    get_touch_calibration_overlay().handle_back_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void register_touch_calibration_overlay_callbacks() {
    get_touch_calibration_overlay().register_callbacks();
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

TouchCalibrationOverlay::TouchCalibrationOverlay() {
    // Zero-initialize instruction buffer
    std::memset(instruction_buffer_, 0, sizeof(instruction_buffer_));

    // Create the calibration panel
    panel_ = std::make_unique<helix::TouchCalibrationPanel>();

    // Set screen size from DisplayManager
    DisplayManager* display_mgr = DisplayManager::instance();
    if (display_mgr && display_mgr->is_initialized()) {
        panel_->set_screen_size(display_mgr->width(), display_mgr->height());
        spdlog::debug("[{}] Screen size set to {}x{}", get_name(), display_mgr->width(),
                      display_mgr->height());
    } else {
        // Fallback to defaults
        panel_->set_screen_size(800, 480);
        spdlog::warn("[{}] DisplayManager not available, using default 800x480", get_name());
    }

    // Set completion callback
    panel_->set_completion_callback(
        [this](const TouchCalibration* cal) { on_calibration_complete(cal); });

    // Set failure callback to notify user of degenerate points
    panel_->set_failure_callback([this](const char* reason) {
        spdlog::warn("[{}] Calibration failed: {}", get_name(), reason);
        ui_toast_show(ToastSeverity::WARNING, reason, 3000);
        // State subject will be updated by capture_point flow
        update_state_subject();
        update_instruction_text();
        update_crosshair_position();
    });

    spdlog::debug("[{}] Instance created", get_name());
}

TouchCalibrationOverlay::~TouchCalibrationOverlay() {
    // Clean up managers before widget destruction
    if (panel_) {
        panel_->set_completion_callback(nullptr);
    }

    // Deinitialize subjects to disconnect observers
    if (subjects_initialized_) {
        subjects_.deinit_all();
        subjects_initialized_ = false;
    }

    // Clear widget pointers (owned by LVGL)
    overlay_root_ = nullptr;
    crosshair_ = nullptr;
    verify_marker_ = nullptr;
}

// ============================================================================
// Subject Initialization
// ============================================================================

void TouchCalibrationOverlay::init_subjects() {
    if (subjects_initialized_) {
        spdlog::debug("[{}] Subjects already initialized", get_name());
        return;
    }

    spdlog::debug("[{}] Initializing subjects", get_name());

    // State subject: 0=IDLE, 1=POINT_1, 2=POINT_2, 3=POINT_3, 4=VERIFY, 5=COMPLETE
    UI_MANAGED_SUBJECT_INT(state_subject_, STATE_IDLE, "touch_cal_state", subjects_);

    // Instruction text subject
    UI_MANAGED_SUBJECT_STRING(instruction_subject_, instruction_buffer_,
                              "Tap Start to begin calibration", "touch_cal_instruction", subjects_);

    // Skip button visibility subject (1 = visible, 0 = hidden)
    UI_MANAGED_SUBJECT_INT(skip_visible_subject_, 0, "touch_cal_skip_visible", subjects_);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

// ============================================================================
// Callback Registration
// ============================================================================

void TouchCalibrationOverlay::register_callbacks() {
    spdlog::debug("[{}] Registering event callbacks", get_name());

    lv_xml_register_event_cb(nullptr, "on_touch_cal_start_clicked", on_touch_cal_start_clicked);
    lv_xml_register_event_cb(nullptr, "on_touch_cal_accept_clicked", on_touch_cal_accept_clicked);
    lv_xml_register_event_cb(nullptr, "on_touch_cal_retry_clicked", on_touch_cal_retry_clicked);
    lv_xml_register_event_cb(nullptr, "on_touch_cal_skip_clicked", on_touch_cal_skip_clicked);
    lv_xml_register_event_cb(nullptr, "on_touch_cal_overlay_touched", on_touch_cal_overlay_touched);
    lv_xml_register_event_cb(nullptr, "on_touch_cal_back_clicked", on_touch_cal_back_clicked);

    spdlog::debug("[{}] Event callbacks registered", get_name());
}

// ============================================================================
// Screen Creation
// ============================================================================

lv_obj_t* TouchCalibrationOverlay::create(lv_obj_t* parent) {
    spdlog::debug("[{}] Creating overlay from XML", get_name());

    if (!parent) {
        spdlog::error("[{}] Cannot create: null parent", get_name());
        return nullptr;
    }

    // Reset cleanup flag when (re)creating
    cleanup_called_ = false;

    // Create overlay from XML
    overlay_root_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent, "touch_calibration_overlay", nullptr));

    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    // Find crosshair widget for positioning
    crosshair_ = lv_obj_find_by_name(overlay_root_, "crosshair");
    if (!crosshair_) {
        spdlog::warn("[{}] Crosshair widget not found in XML", get_name());
    }

    // Find verify marker widget (optional)
    verify_marker_ = lv_obj_find_by_name(overlay_root_, "verify_marker");

    // Initially hidden
    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    spdlog::info("[{}] Overlay created successfully", get_name());
    return overlay_root_;
}

// ============================================================================
// Show/Hide
// ============================================================================

void TouchCalibrationOverlay::show(CompletionCallback callback) {
    if (!overlay_root_) {
        spdlog::error("[{}] Cannot show: overlay not created", get_name());
        return;
    }

    spdlog::debug("[{}] Showing overlay", get_name());

    // Store completion callback
    completion_callback_ = std::move(callback);
    callback_invoked_ = false;

    // Reset state
    if (panel_) {
        panel_->cancel(); // Reset to IDLE
    }
    lv_subject_set_int(&state_subject_, STATE_IDLE);
    update_instruction_text();

    // Update skip visibility
    lv_subject_set_int(&skip_visible_subject_, allow_skip_ ? 1 : 0);

    // Register with NavigationManager for lifecycle callbacks
    NavigationManager::instance().register_overlay_instance(overlay_root_, this);

    // Push onto navigation stack - on_activate() will be called by NavigationManager
    ui_nav_push_overlay(overlay_root_);

    spdlog::info("[{}] Overlay shown", get_name());
}

void TouchCalibrationOverlay::hide() {
    if (!overlay_root_) {
        return;
    }

    spdlog::debug("[{}] Hiding overlay", get_name());

    // Pop from navigation stack - on_deactivate() will be called by NavigationManager
    ui_nav_go_back();

    spdlog::info("[{}] Overlay hidden", get_name());
}

// ============================================================================
// Lifecycle Hooks
// ============================================================================

void TouchCalibrationOverlay::on_activate() {
    // Call base class first
    OverlayBase::on_activate();

    spdlog::debug("[{}] on_activate()", get_name());

    // Initialize crosshair position if calibrating
    update_crosshair_position();
}

void TouchCalibrationOverlay::on_deactivate() {
    spdlog::debug("[{}] on_deactivate()", get_name());

    // Cancel any in-progress calibration
    if (panel_) {
        panel_->cancel();
    }

    // Call base class
    OverlayBase::on_deactivate();
}

// ============================================================================
// Cleanup
// ============================================================================

void TouchCalibrationOverlay::cleanup() {
    spdlog::debug("[{}] Cleaning up", get_name());

    // Unregister from NavigationManager before cleaning up
    if (overlay_root_) {
        NavigationManager::instance().unregister_overlay_instance(overlay_root_);
    }

    // Call base class to set cleanup_called_ flag
    OverlayBase::cleanup();

    // Cancel any in-progress calibration
    if (panel_) {
        panel_->set_completion_callback(nullptr);
        panel_->cancel();
    }

    // Clear widget pointers
    crosshair_ = nullptr;
    verify_marker_ = nullptr;

    // Clear callback
    completion_callback_ = nullptr;
    callback_invoked_ = false;

    spdlog::debug("[{}] Cleanup complete", get_name());
}

// ============================================================================
// Public API
// ============================================================================

void TouchCalibrationOverlay::set_allow_skip(bool allow) {
    allow_skip_ = allow;
    if (subjects_initialized_) {
        lv_subject_set_int(&skip_visible_subject_, allow ? 1 : 0);
    }
}

// ============================================================================
// Event Handlers
// ============================================================================

void TouchCalibrationOverlay::handle_start_clicked() {
    spdlog::info("[{}] Start calibration clicked", get_name());

    if (!panel_) {
        spdlog::error("[{}] Panel not initialized", get_name());
        return;
    }

    panel_->start();
    lv_subject_set_int(&state_subject_, STATE_POINT_1);
    update_instruction_text();
    update_crosshair_position();
}

void TouchCalibrationOverlay::handle_accept_clicked() {
    spdlog::info("[{}] Accept calibration clicked", get_name());

    if (!panel_) {
        return;
    }

    // Get calibration data before accepting
    const TouchCalibration* cal = panel_->get_calibration();
    if (!cal || !cal->valid) {
        spdlog::error("[{}] No valid calibration to accept", get_name());
        return;
    }

    // Save calibration to config
    Config* config = Config::get_instance();
    if (config) {
        config->set<bool>("/input/calibration/valid", true);
        config->set<double>("/input/calibration/a", static_cast<double>(cal->a));
        config->set<double>("/input/calibration/b", static_cast<double>(cal->b));
        config->set<double>("/input/calibration/c", static_cast<double>(cal->c));
        config->set<double>("/input/calibration/d", static_cast<double>(cal->d));
        config->set<double>("/input/calibration/e", static_cast<double>(cal->e));
        config->set<double>("/input/calibration/f", static_cast<double>(cal->f));
        config->save();
        spdlog::info("[{}] Calibration saved to config", get_name());
    }

    // Apply calibration immediately via DisplayManager
    DisplayManager* dm = DisplayManager::instance();
    if (dm && dm->apply_touch_calibration(*cal)) {
        spdlog::info("[{}] Calibration applied to touch input", get_name());
    } else {
#ifndef HELIX_DISPLAY_FBDEV
        // Show warning on SDL that calibration cannot be applied at runtime
        ui_toast_show(ToastSeverity::WARNING, "Calibration saved but cannot apply on SDL display",
                      3000);
#endif
        spdlog::debug("[{}] Could not apply calibration immediately (may require restart)",
                      get_name());
    }

    // Accept in panel (transitions to COMPLETE state)
    panel_->accept();
    lv_subject_set_int(&state_subject_, STATE_COMPLETE);

    // Invoke completion callback with success
    if (completion_callback_ && !callback_invoked_) {
        callback_invoked_ = true;
        completion_callback_(true, false);
    }

    hide();
}

void TouchCalibrationOverlay::handle_retry_clicked() {
    spdlog::info("[{}] Retry calibration clicked", get_name());

    if (!panel_) {
        return;
    }

    panel_->retry();
    lv_subject_set_int(&state_subject_, STATE_POINT_1);
    update_instruction_text();
    update_crosshair_position();
}

void TouchCalibrationOverlay::handle_skip_clicked() {
    spdlog::info("[{}] Skip calibration clicked", get_name());

    // Don't call panel_->cancel() here to avoid triggering completion callback
    // Just invoke our completion callback directly with skipped=true

    if (completion_callback_ && !callback_invoked_) {
        callback_invoked_ = true;
        completion_callback_(false, true);
    }

    hide();
}

void TouchCalibrationOverlay::handle_screen_touched(lv_event_t* e) {
    (void)e; // Event not used directly - we get touch position from active input device

    if (!panel_ || !overlay_root_) {
        return;
    }

    auto state = panel_->get_state();

    // Get click position relative to the screen
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);

    // Handle VERIFY state - show calibration accuracy visualization
    if (state == helix::TouchCalibrationPanel::State::VERIFY) {
        spdlog::debug("[{}] Verify touch at ({}, {})", get_name(), point.x, point.y);

        // Get calibration and transform the point
        const TouchCalibration* cal = panel_->get_calibration();
        DisplayManager* dm = DisplayManager::instance();
        if (cal && cal->valid && verify_marker_ && dm) {
            // Transform through calibration to show where system thinks touch landed
            helix::Point raw{point.x, point.y};
            helix::Point transformed =
                helix::transform_point(*cal, raw, dm->width() - 1, dm->height() - 1);

            // Position verify marker at transformed position (centered on 24x24 marker)
            lv_obj_set_pos(verify_marker_, transformed.x - 12, transformed.y - 12);
            lv_obj_remove_flag(verify_marker_, LV_OBJ_FLAG_HIDDEN);

            spdlog::debug("[{}] Verify: raw({},{}) -> transformed({},{})", get_name(), raw.x, raw.y,
                          transformed.x, transformed.y);
        }
        return;
    }

    // Only process touches during calibration point states
    if (state != helix::TouchCalibrationPanel::State::POINT_1 &&
        state != helix::TouchCalibrationPanel::State::POINT_2 &&
        state != helix::TouchCalibrationPanel::State::POINT_3) {
        return;
    }

    spdlog::info("[{}] Screen touched at ({}, {}) during state {}", get_name(), point.x, point.y,
                 static_cast<int>(state));

    // Capture the raw touch point
    panel_->capture_point({point.x, point.y});

    // Map panel state to subject state
    update_state_subject();
    update_instruction_text();
    update_crosshair_position();
}

void TouchCalibrationOverlay::handle_back_clicked() {
    spdlog::info("[{}] Back button clicked", get_name());

    // Invoke completion callback with cancelled (not skipped)
    if (completion_callback_ && !callback_invoked_) {
        callback_invoked_ = true;
        completion_callback_(false, false);
    }

    hide();
}

// ============================================================================
// UI Update Helpers
// ============================================================================

void TouchCalibrationOverlay::update_state_subject() {
    if (!panel_) {
        return;
    }

    auto state = panel_->get_state();
    int state_value = STATE_IDLE;

    switch (state) {
    case helix::TouchCalibrationPanel::State::IDLE:
        state_value = STATE_IDLE;
        break;
    case helix::TouchCalibrationPanel::State::POINT_1:
        state_value = STATE_POINT_1;
        break;
    case helix::TouchCalibrationPanel::State::POINT_2:
        state_value = STATE_POINT_2;
        break;
    case helix::TouchCalibrationPanel::State::POINT_3:
        state_value = STATE_POINT_3;
        break;
    case helix::TouchCalibrationPanel::State::VERIFY:
        state_value = STATE_VERIFY;
        break;
    case helix::TouchCalibrationPanel::State::COMPLETE:
        state_value = STATE_COMPLETE;
        break;
    }

    lv_subject_set_int(&state_subject_, state_value);
}

void TouchCalibrationOverlay::update_instruction_text() {
    if (!panel_) {
        return;
    }

    const char* text = "";
    auto state = panel_->get_state();

    switch (state) {
    case helix::TouchCalibrationPanel::State::IDLE:
        text = "Tap Start to begin calibration";
        break;
    case helix::TouchCalibrationPanel::State::POINT_1:
        text = "Tap the crosshair (point 1 of 3)";
        break;
    case helix::TouchCalibrationPanel::State::POINT_2:
        text = "Tap the crosshair (point 2 of 3)";
        break;
    case helix::TouchCalibrationPanel::State::POINT_3:
        text = "Tap the crosshair (point 3 of 3)";
        break;
    case helix::TouchCalibrationPanel::State::VERIFY:
        text = "Touch anywhere to verify accuracy";
        break;
    case helix::TouchCalibrationPanel::State::COMPLETE:
        text = "Calibration complete";
        break;
    }

    lv_subject_copy_string(&instruction_subject_, text);
}

void TouchCalibrationOverlay::update_crosshair_position() {
    if (!crosshair_ || !panel_) {
        return;
    }

    auto state = panel_->get_state();

    // Hide crosshair in IDLE, VERIFY, and COMPLETE states
    if (state == helix::TouchCalibrationPanel::State::IDLE ||
        state == helix::TouchCalibrationPanel::State::VERIFY ||
        state == helix::TouchCalibrationPanel::State::COMPLETE) {
        lv_obj_add_flag(crosshair_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Show crosshair for calibration points
    lv_obj_remove_flag(crosshair_, LV_OBJ_FLAG_HIDDEN);

    // Determine which step we're on
    int step = 0;
    switch (state) {
    case helix::TouchCalibrationPanel::State::POINT_1:
        step = 0;
        break;
    case helix::TouchCalibrationPanel::State::POINT_2:
        step = 1;
        break;
    case helix::TouchCalibrationPanel::State::POINT_3:
        step = 2;
        break;
    default:
        return;
    }

    // Get target position from panel
    helix::Point target = panel_->get_target_position(step);

    // Center crosshair on target
    lv_obj_set_pos(crosshair_, target.x - CROSSHAIR_HALF_SIZE, target.y - CROSSHAIR_HALF_SIZE);

    spdlog::debug("[{}] Crosshair positioned at ({}, {}) for step {}", get_name(), target.x,
                  target.y, step);
}

void TouchCalibrationOverlay::on_calibration_complete(const TouchCalibration* cal) {
    // Guard against callback during cleanup
    if (cleanup_called_ || !overlay_root_) {
        spdlog::debug("[{}] Ignoring callback during cleanup", get_name());
        return;
    }

    if (cal && cal->valid) {
        spdlog::info("[{}] Calibration complete callback received (valid)", get_name());
    } else {
        spdlog::debug("[{}] Calibration cancelled or invalid", get_name());
    }

    // UI updates are handled in handle_accept_clicked() where save logic lives
    // This callback is primarily for logging/debugging
}

} // namespace helix::ui
