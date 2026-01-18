// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 356C LLC

#include "ui_wizard_touch_calibration.h"

#include "ui_subject_registry.h"
#include "ui_theme.h"

#include "config.h"
#include "display_manager.h"
#include "settings_manager.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

// ============================================================================
// Constants
// ============================================================================

// Crosshair widget size in pixels (defined in XML as 50x50)
constexpr int CROSSHAIR_SIZE = 50;
constexpr int CROSSHAIR_HALF_SIZE = CROSSHAIR_SIZE / 2;

// Ripple animation constants
constexpr int RIPPLE_START_SIZE = 20;
constexpr int RIPPLE_END_SIZE = 120;
constexpr int32_t RIPPLE_DURATION_MS = 400;

// External wizard subjects (defined in ui_wizard.cpp)
extern lv_subject_t connection_test_passed;
extern lv_subject_t wizard_next_button_text;
extern lv_subject_t wizard_subtitle;

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<WizardTouchCalibrationStep> g_wizard_touch_calibration_step;

// Flag to force touch calibration step to show (for visual testing on SDL)
static bool g_force_touch_calibration_step = false;

void force_touch_calibration_step(bool force) {
    g_force_touch_calibration_step = force;
    if (force) {
        spdlog::debug("[WizardTouchCalibration] Force-showing step for visual testing");
    }
}

WizardTouchCalibrationStep* get_wizard_touch_calibration_step() {
    if (!g_wizard_touch_calibration_step) {
        g_wizard_touch_calibration_step = std::make_unique<WizardTouchCalibrationStep>();
        StaticPanelRegistry::instance().register_destroy(
            "WizardTouchCalibrationStep", []() { g_wizard_touch_calibration_step.reset(); });
    }
    return g_wizard_touch_calibration_step.get();
}

void destroy_wizard_touch_calibration_step() {
    g_wizard_touch_calibration_step.reset();
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

WizardTouchCalibrationStep::WizardTouchCalibrationStep() {
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
        [this](const helix::TouchCalibration* cal) { on_calibration_complete(cal); });

    // Set failure callback for degenerate points (collinear/duplicate)
    // Panel auto-restarts to POINT_1, we show error with step instruction
    panel_->set_failure_callback([this](const char* reason) {
        spdlog::warn("[{}] Calibration failed: {}", get_name(), reason);

        if (screen_root_) {
            calibration_failed_ = true;
            update_instruction_text(); // Will concatenate error + step
            update_crosshair_position();
            update_button_visibility();
        }
    });

    spdlog::debug("[{}] Instance created", get_name());
}

WizardTouchCalibrationStep::~WizardTouchCalibrationStep() {
    screen_root_ = nullptr;
}

// ============================================================================
// Subject Initialization
// ============================================================================

void WizardTouchCalibrationStep::init_subjects() {
    // Guard against double initialization
    if (subjects_initialized_) {
        spdlog::debug("[{}] Subjects already initialized, skipping", get_name());
        return;
    }

    spdlog::debug("[{}] Initializing subjects", get_name());

    // Note: instruction text now uses wizard_subtitle (in header) instead of local subject
    UI_SUBJECT_INIT_AND_REGISTER_INT(current_step_, 0, "touch_cal_current_step");
    UI_SUBJECT_INIT_AND_REGISTER_INT(calibration_valid_, 0, "touch_cal_valid");

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

// ============================================================================
// Callback Registration
// ============================================================================

void WizardTouchCalibrationStep::register_callbacks() {
    spdlog::debug("[{}] Registering callbacks", get_name());

    lv_xml_register_event_cb(nullptr, "on_touch_cal_accept_clicked", on_accept_clicked_static);
    lv_xml_register_event_cb(nullptr, "on_touch_cal_retry_clicked", on_retry_clicked_static);
    lv_xml_register_event_cb(nullptr, "on_touch_cal_screen_touched", on_screen_touched_static);
    lv_xml_register_event_cb(nullptr, "on_touch_cal_test_area_touched",
                             on_test_area_touched_static);
}

// ============================================================================
// Screen Creation
// ============================================================================

lv_obj_t* WizardTouchCalibrationStep::create(lv_obj_t* parent) {
    spdlog::debug("[{}] Creating touch calibration screen", get_name());

    // Safety check: cleanup should have been called by wizard navigation
    if (screen_root_) {
        spdlog::warn("[{}] Screen pointer not null - cleanup may not have been called properly",
                     get_name());
        screen_root_ = nullptr;
    }

    // Create screen from XML
    screen_root_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent, "wizard_touch_calibration", nullptr));
    if (!screen_root_) {
        spdlog::error("[{}] Failed to create screen from XML", get_name());
        return nullptr;
    }

    // Find and reparent crosshair to screen for absolute positioning
    // Calibration targets are screen-absolute coordinates, so crosshair must be
    // a direct child of the screen (not nested in wizard content container)
    crosshair_ = lv_obj_find_by_name(screen_root_, "crosshair");
    if (crosshair_) {
        lv_obj_set_parent(crosshair_, lv_screen_active());
        lv_obj_add_flag(crosshair_, LV_OBJ_FLAG_FLOATING); // Ensure no layout interference
        spdlog::debug("[{}] Crosshair reparented to screen for absolute positioning", get_name());
    }

    // Find test area widgets (shown in COMPLETE state)
    test_area_container_ = lv_obj_find_by_name(screen_root_, "test_area_container");
    test_touch_area_ = lv_obj_find_by_name(screen_root_, "test_touch_area");

    // Auto-start calibration immediately
    if (panel_) {
        panel_->start();
    }

    // Enable Next button and set initial text to "Skip"
    lv_subject_set_int(&connection_test_passed, 1);
    lv_subject_copy_string(&wizard_next_button_text, "Skip");

    // Update UI for calibration state
    update_instruction_text();
    update_crosshair_position();
    update_button_visibility();

    spdlog::debug("[{}] Screen created successfully", get_name());
    return screen_root_;
}

// ============================================================================
// Cleanup
// ============================================================================

void WizardTouchCalibrationStep::cleanup() {
    spdlog::debug("[{}] Cleaning up resources", get_name());

    // Reset button text to "Next" (in case user skipped without completing)
    lv_subject_copy_string(&wizard_next_button_text, "Next");

    // Delete crosshair (it was reparented to screen, not part of screen_root_)
    if (crosshair_) {
        lv_obj_delete(crosshair_);
        crosshair_ = nullptr;
    }

    // Clear widget pointers FIRST to prevent UI updates during cleanup
    // (test area widgets are children of screen_root_, so they're deleted with it)
    test_area_container_ = nullptr;
    test_touch_area_ = nullptr;
    screen_root_ = nullptr;

    // Reset panel state - clear callback before cancel to prevent updates to
    // destroyed UI widgets (callback would call update_instruction_text() etc.)
    if (panel_) {
        panel_->set_completion_callback(nullptr);
        panel_->cancel();
    }
}

// ============================================================================
// Skip Logic
// ============================================================================

bool WizardTouchCalibrationStep::should_skip() const {
    // Force show if explicitly requested (for visual testing on SDL)
    if (g_force_touch_calibration_step) {
        spdlog::debug("[{}] Force-showing: --wizard-step 0 requested", get_name());
        return false;
    }

    // Skip if not on framebuffer display
#ifndef HELIX_DISPLAY_FBDEV
    spdlog::debug("[{}] Skipping: not on framebuffer display", get_name());
    return true;
#endif

    // Skip if already calibrated
    Config* config = Config::get_instance();
    if (config && config->get<bool>("/input/calibration/valid", false)) {
        spdlog::debug("[{}] Skipping: already calibrated", get_name());
        return true;
    }

    return false;
}

// ============================================================================
// Static Event Handlers (Trampolines)
// ============================================================================

void WizardTouchCalibrationStep::on_accept_clicked_static(lv_event_t* e) {
    (void)e;
    get_wizard_touch_calibration_step()->handle_accept_clicked();
}

void WizardTouchCalibrationStep::on_retry_clicked_static(lv_event_t* e) {
    (void)e;
    get_wizard_touch_calibration_step()->handle_retry_clicked();
}

void WizardTouchCalibrationStep::on_screen_touched_static(lv_event_t* e) {
    get_wizard_touch_calibration_step()->handle_screen_touched(e);
}

void WizardTouchCalibrationStep::on_test_area_touched_static(lv_event_t* e) {
    get_wizard_touch_calibration_step()->handle_test_area_touched(e);
}

// ============================================================================
// Instance Event Handlers
// ============================================================================

void WizardTouchCalibrationStep::handle_accept_clicked() {
    spdlog::info("[{}] Accept calibration clicked", get_name());

    if (!panel_) {
        return;
    }

    // Accept triggers the completion callback with calibration data
    panel_->accept();
}

void WizardTouchCalibrationStep::handle_retry_clicked() {
    spdlog::info("[{}] Retry calibration clicked", get_name());

    if (!panel_) {
        return;
    }

    // Use start() to restart calibration (works from any state including COMPLETE)
    panel_->start();

    // Reset button text back to "Skip" since calibration is starting over
    lv_subject_copy_string(&wizard_next_button_text, "Skip");

    lv_subject_set_int(&current_step_, 0);
    lv_subject_set_int(&calibration_valid_, 0);
    update_instruction_text();
    update_crosshair_position();
    update_button_visibility();
}

void WizardTouchCalibrationStep::handle_screen_touched(lv_event_t* e) {
    (void)e; // Event not used directly - we get touch position from active input device

    if (!panel_ || !screen_root_) {
        return;
    }

    // Only process touches during calibration point states
    auto state = panel_->get_state();
    if (state != helix::TouchCalibrationPanel::State::POINT_1 &&
        state != helix::TouchCalibrationPanel::State::POINT_2 &&
        state != helix::TouchCalibrationPanel::State::POINT_3) {
        return;
    }

    // Get click position relative to the screen
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);

    spdlog::info("[{}] Screen touched at ({}, {}) during state {}", get_name(), point.x, point.y,
                 static_cast<int>(state));

    // Capture the raw touch point (for SDL, screen coords == raw coords)
    panel_->capture_point({point.x, point.y});

    // Auto-accept when VERIFY state is reached (wizard doesn't need user to click Accept)
    // The overlay has a different flow with explicit Accept/Verify
    if (panel_->get_state() == helix::TouchCalibrationPanel::State::VERIFY) {
        spdlog::info("[{}] Auto-accepting calibration (wizard mode)", get_name());
        panel_->accept();
    }

    // Update UI for next step
    update_instruction_text();
    update_crosshair_position();
    update_button_visibility();
}

void WizardTouchCalibrationStep::handle_test_area_touched(lv_event_t* e) {
    (void)e;

    if (!test_touch_area_) {
        return;
    }

    lv_indev_t* indev = lv_indev_active();
    if (!indev) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    // Convert screen coords to test_touch_area local coords
    // lv_obj_get_coords returns screen-absolute coordinates of the object
    lv_area_t area_coords;
    lv_obj_get_coords(test_touch_area_, &area_coords);
    lv_coord_t local_x = point.x - area_coords.x1;
    lv_coord_t local_y = point.y - area_coords.y1;

    spdlog::debug("[{}] Test area touched at screen ({}, {}), local ({}, {})", get_name(), point.x,
                  point.y, local_x, local_y);

    create_ripple_at(local_x, local_y);
}

void WizardTouchCalibrationStep::create_ripple_at(lv_coord_t x, lv_coord_t y) {
    // Skip animation if disabled
    if (!SettingsManager::instance().get_animations_enabled()) {
        spdlog::debug("[{}] Animations disabled - skipping ripple", get_name());
        return;
    }

    if (!test_touch_area_) {
        return;
    }

    // Create circle object for ripple effect
    lv_obj_t* ripple = lv_obj_create(test_touch_area_);
    lv_obj_remove_style_all(ripple);

    // Initial size (small circle)
    lv_obj_set_size(ripple, RIPPLE_START_SIZE, RIPPLE_START_SIZE);
    lv_obj_set_style_radius(ripple, LV_RADIUS_CIRCLE, 0);

    // Style: primary color, semi-transparent
    lv_obj_set_style_bg_color(ripple, ui_theme_get_color("primary_color"), 0);
    lv_obj_set_style_bg_opa(ripple, LV_OPA_50, 0);
    lv_obj_set_style_border_width(ripple, 0, 0);

    // Take out of flex layout so position works, and make non-clickable
    lv_obj_add_flag(ripple, LV_OBJ_FLAG_FLOATING);
    lv_obj_remove_flag(ripple, LV_OBJ_FLAG_CLICKABLE);

    // Position centered on touch point
    lv_obj_set_pos(ripple, x - RIPPLE_START_SIZE / 2, y - RIPPLE_START_SIZE / 2);

    // Animation 1: Scale (grow)
    lv_anim_t scale_anim;
    lv_anim_init(&scale_anim);
    lv_anim_set_var(&scale_anim, ripple);
    lv_anim_set_values(&scale_anim, RIPPLE_START_SIZE, RIPPLE_END_SIZE);
    lv_anim_set_duration(&scale_anim, RIPPLE_DURATION_MS);
    lv_anim_set_path_cb(&scale_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&scale_anim, [](void* var, int32_t size) {
        auto* obj = static_cast<lv_obj_t*>(var);
        lv_coord_t old_size = lv_obj_get_width(obj);
        lv_coord_t delta = (size - old_size) / 2;
        lv_obj_set_size(obj, size, size);
        // Use style values (not coords) - coords aren't updated until layout refresh
        int32_t current_x = lv_obj_get_style_x(obj, LV_PART_MAIN);
        int32_t current_y = lv_obj_get_style_y(obj, LV_PART_MAIN);
        lv_obj_set_pos(obj, current_x - delta, current_y - delta);
    });
    lv_anim_start(&scale_anim);

    // Animation 2: Fade out
    lv_anim_t fade_anim;
    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, ripple);
    lv_anim_set_values(&fade_anim, LV_OPA_50, LV_OPA_TRANSP);
    lv_anim_set_duration(&fade_anim, RIPPLE_DURATION_MS);
    lv_anim_set_path_cb(&fade_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&fade_anim, [](void* var, int32_t opa) {
        lv_obj_set_style_bg_opa(static_cast<lv_obj_t*>(var), static_cast<lv_opa_t>(opa), 0);
    });
    lv_anim_set_completed_cb(&fade_anim, [](lv_anim_t* a) {
        // Delete ripple object when animation completes
        lv_obj_delete(static_cast<lv_obj_t*>(a->var));
    });
    lv_anim_start(&fade_anim);
}

// ============================================================================
// Calibration Complete Callback
// ============================================================================

void WizardTouchCalibrationStep::on_calibration_complete(const helix::TouchCalibration* cal) {
    // Guard against callback during cleanup (screen_root_ is nulled first in cleanup())
    if (!screen_root_) {
        spdlog::debug("[{}] Ignoring callback during cleanup", get_name());
        return;
    }

    if (cal && cal->valid) {
        // Additional validation: check coefficients are finite and within bounds
        if (!helix::is_calibration_valid(*cal)) {
            spdlog::error("[{}] Calibration coefficients failed validation (NaN/Inf/out of bounds)",
                          get_name());

            // Mark failure so update_instruction_text() shows error with step
            calibration_failed_ = true;

            lv_subject_set_int(&calibration_valid_, 0);
            lv_subject_copy_string(&wizard_next_button_text, "Skip");

            panel_->start();
            update_instruction_text(); // Will concatenate error + step
            update_crosshair_position();
            update_button_visibility();
            return;
        }

        spdlog::info("[{}] Calibration complete and valid", get_name());

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

        // Apply calibration immediately (no restart required)
        DisplayManager* dm = DisplayManager::instance();
        if (dm && dm->apply_touch_calibration(*cal)) {
            spdlog::info("[{}] Calibration applied to touch input", get_name());
        } else {
            spdlog::debug("[{}] Could not apply calibration immediately (may require restart)",
                          get_name());
        }

        lv_subject_set_int(&calibration_valid_, 1);

        // Update header subtitle to show success
        lv_subject_copy_string(&wizard_subtitle, "Calibration complete! Press 'Next' to continue.");

        // Change button text from "Skip" to "Next" since calibration is complete
        lv_subject_copy_string(&wizard_next_button_text, "Next");
    } else {
        spdlog::warn("[{}] Calibration cancelled or invalid", get_name());
        lv_subject_set_int(&calibration_valid_, 0);
    }

    update_instruction_text();
    update_button_visibility();
}

// ============================================================================
// UI Update Helpers
// ============================================================================

void WizardTouchCalibrationStep::update_instruction_text() {
    if (!panel_) {
        return;
    }

    auto state = panel_->get_state();

    // Clear failure flag once user successfully captures a point (moved past POINT_1)
    if (state != helix::TouchCalibrationPanel::State::POINT_1 &&
        state != helix::TouchCalibrationPanel::State::IDLE) {
        calibration_failed_ = false;
    }

    const char* step_text = "";
    switch (state) {
    case helix::TouchCalibrationPanel::State::IDLE:
        step_text = "Touch the target crosshair to calibrate your touchscreen.";
        break;
    case helix::TouchCalibrationPanel::State::POINT_1:
        step_text = "Touch the target (point 1 of 3)";
        break;
    case helix::TouchCalibrationPanel::State::POINT_2:
        step_text = "Touch the target (point 2 of 3)";
        break;
    case helix::TouchCalibrationPanel::State::POINT_3:
        step_text = "Touch the target (point 3 of 3)";
        break;
    case helix::TouchCalibrationPanel::State::VERIFY:
        step_text = "Computing calibration...";
        break;
    case helix::TouchCalibrationPanel::State::COMPLETE:
        step_text = "Calibration complete! Press 'Next' to continue, or 'Retry' to recalibrate.";
        break;
    }

    // Prepend error message if calibration just failed
    if (calibration_failed_ && state == helix::TouchCalibrationPanel::State::POINT_1) {
        static char combined[128];
        snprintf(combined, sizeof(combined),
                 "Calibration failed - touch targets more precisely. %s", step_text);
        lv_subject_copy_string(&wizard_subtitle, combined);
    } else {
        lv_subject_copy_string(&wizard_subtitle, step_text);
    }
}

void WizardTouchCalibrationStep::update_crosshair_position() {
    if (!panel_) {
        return;
    }

    // Touch overlay remains in screen_root_ for event capture
    lv_obj_t* touch_overlay =
        screen_root_ ? lv_obj_find_by_name(screen_root_, "touch_capture_overlay") : nullptr;

    auto state = panel_->get_state();

    // Hide crosshair and touch overlay in IDLE, VERIFY, and COMPLETE states
    if (state == helix::TouchCalibrationPanel::State::IDLE ||
        state == helix::TouchCalibrationPanel::State::VERIFY ||
        state == helix::TouchCalibrationPanel::State::COMPLETE) {
        if (crosshair_) {
            lv_obj_add_flag(crosshair_, LV_OBJ_FLAG_HIDDEN);
        }
        if (touch_overlay) {
            lv_obj_add_flag(touch_overlay, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    // Show crosshair and touch overlay for calibration points
    if (crosshair_) {
        lv_obj_remove_flag(crosshair_, LV_OBJ_FLAG_HIDDEN);
    }
    if (touch_overlay) {
        lv_obj_remove_flag(touch_overlay, LV_OBJ_FLAG_HIDDEN);
    }

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

    helix::Point target = panel_->get_target_position(step);

    // Crosshair is a direct child of the screen, so we can use screen-absolute coordinates
    if (crosshair_) {
        lv_obj_set_pos(crosshair_, target.x - CROSSHAIR_HALF_SIZE, target.y - CROSSHAIR_HALF_SIZE);
    }
    lv_subject_set_int(&current_step_, step);

    spdlog::debug("[{}] Crosshair positioned at screen ({}, {}) for step {}", get_name(), target.x,
                  target.y, step);
}

void WizardTouchCalibrationStep::update_button_visibility() {
    if (!screen_root_ || !panel_) {
        return;
    }

    auto state = panel_->get_state();
    bool is_complete = (state == helix::TouchCalibrationPanel::State::COMPLETE);

    // Show test area container only in COMPLETE state
    if (test_area_container_) {
        if (is_complete) {
            lv_obj_remove_flag(test_area_container_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(test_area_container_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
