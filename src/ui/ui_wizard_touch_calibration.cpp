// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 356C LLC

#include "ui_wizard_touch_calibration.h"

#include "ui_subject_registry.h"

#include "config.h"
#include "display_manager.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <cstring>

// ============================================================================
// Constants
// ============================================================================

// Crosshair widget size in pixels (defined in XML as 50x50)
constexpr int CROSSHAIR_SIZE = 50;
constexpr int CROSSHAIR_HALF_SIZE = CROSSHAIR_SIZE / 2;

// Maximum instruction text length (matches instruction_buffer_ size)
constexpr size_t MAX_INSTRUCTION_LENGTH = 255;

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<WizardTouchCalibrationStep> g_wizard_touch_calibration_step;

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
    // Zero-initialize buffer
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
        [this](const helix::TouchCalibration* cal) { on_calibration_complete(cal); });

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

    // Default instruction text
    const char* initial_instruction = "Tap 'Start Calibration' to begin touchscreen calibration.";

    UI_SUBJECT_INIT_AND_REGISTER_STRING(instruction_text_, instruction_buffer_, initial_instruction,
                                        "touch_cal_instruction_text");
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

    lv_xml_register_event_cb(nullptr, "on_touch_cal_start_clicked", on_start_clicked_static);
    lv_xml_register_event_cb(nullptr, "on_touch_cal_accept_clicked", on_accept_clicked_static);
    lv_xml_register_event_cb(nullptr, "on_touch_cal_retry_clicked", on_retry_clicked_static);
    lv_xml_register_event_cb(nullptr, "on_touch_cal_skip_clicked", on_skip_clicked_static);
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

    // Reset panel state and update UI
    update_instruction_text();
    update_button_visibility();

    spdlog::debug("[{}] Screen created successfully", get_name());
    return screen_root_;
}

// ============================================================================
// Cleanup
// ============================================================================

void WizardTouchCalibrationStep::cleanup() {
    spdlog::debug("[{}] Cleaning up resources", get_name());

    // Clear screen pointer FIRST to prevent UI updates during cleanup
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
    // Skip if not on framebuffer display
#ifndef HELIX_DISPLAY_FBDEV
    spdlog::debug("[{}] Skipping: not on framebuffer display", get_name());
    return true;
#endif

    // Skip if already calibrated
    Config* config = Config::get_instance();
    if (config && config->get<bool>("/touch_calibrated", false)) {
        spdlog::debug("[{}] Skipping: already calibrated", get_name());
        return true;
    }

    return false;
}

// ============================================================================
// Static Event Handlers (Trampolines)
// ============================================================================

void WizardTouchCalibrationStep::on_start_clicked_static(lv_event_t* e) {
    (void)e;
    get_wizard_touch_calibration_step()->handle_start_clicked();
}

void WizardTouchCalibrationStep::on_accept_clicked_static(lv_event_t* e) {
    (void)e;
    get_wizard_touch_calibration_step()->handle_accept_clicked();
}

void WizardTouchCalibrationStep::on_retry_clicked_static(lv_event_t* e) {
    (void)e;
    get_wizard_touch_calibration_step()->handle_retry_clicked();
}

void WizardTouchCalibrationStep::on_skip_clicked_static(lv_event_t* e) {
    (void)e;
    get_wizard_touch_calibration_step()->handle_skip_clicked();
}

// ============================================================================
// Instance Event Handlers
// ============================================================================

void WizardTouchCalibrationStep::handle_start_clicked() {
    spdlog::info("[{}] Start calibration clicked", get_name());

    if (!panel_) {
        spdlog::error("[{}] Panel not initialized", get_name());
        return;
    }

    panel_->start();
    lv_subject_set_int(&current_step_, 0);
    update_instruction_text();
    update_crosshair_position();
    update_button_visibility();
}

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

    panel_->retry();
    lv_subject_set_int(&current_step_, 0);
    lv_subject_set_int(&calibration_valid_, 0);
    update_instruction_text();
    update_crosshair_position();
    update_button_visibility();
}

void WizardTouchCalibrationStep::handle_skip_clicked() {
    spdlog::info("[{}] Skip calibration clicked", get_name());

    // Enable the Next button by signaling calibration is complete (even though skipped)
    lv_subject_set_int(&calibration_valid_, 1);

    // Cancel any in-progress calibration
    if (panel_) {
        panel_->cancel();
    }
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
            lv_subject_set_int(&calibration_valid_, 0);
            update_instruction_text();
            update_button_visibility();
            return;
        }

        spdlog::info("[{}] Calibration complete and valid", get_name());

        // Save calibration to config
        Config* config = Config::get_instance();
        if (config) {
            config->set<bool>("/touch_calibrated", true);
            config->set<double>("/touch_calibration/a", static_cast<double>(cal->a));
            config->set<double>("/touch_calibration/b", static_cast<double>(cal->b));
            config->set<double>("/touch_calibration/c", static_cast<double>(cal->c));
            config->set<double>("/touch_calibration/d", static_cast<double>(cal->d));
            config->set<double>("/touch_calibration/e", static_cast<double>(cal->e));
            config->set<double>("/touch_calibration/f", static_cast<double>(cal->f));
            config->save();
            spdlog::info("[{}] Calibration saved to config", get_name());
        }

        lv_subject_set_int(&calibration_valid_, 1);
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

    const char* text = "";
    auto state = panel_->get_state();

    switch (state) {
    case helix::TouchCalibrationPanel::State::IDLE:
        text = "Tap 'Start Calibration' to begin touchscreen calibration.";
        break;
    case helix::TouchCalibrationPanel::State::POINT_1:
        text = "Touch the target crosshair (point 1 of 3)";
        break;
    case helix::TouchCalibrationPanel::State::POINT_2:
        text = "Touch the target crosshair (point 2 of 3)";
        break;
    case helix::TouchCalibrationPanel::State::POINT_3:
        text = "Touch the target crosshair (point 3 of 3)";
        break;
    case helix::TouchCalibrationPanel::State::VERIFY:
        text = "Calibration complete. Touch anywhere to verify, then Accept or Retry.";
        break;
    case helix::TouchCalibrationPanel::State::COMPLETE:
        text = "Calibration saved successfully. Click Next to continue.";
        break;
    }

    // Bounds check: ensure text fits in buffer (truncate if needed)
    if (std::strlen(text) > MAX_INSTRUCTION_LENGTH) {
        spdlog::warn("[{}] Instruction text truncated from {} to {} chars", get_name(),
                     std::strlen(text), MAX_INSTRUCTION_LENGTH);
        // Copy truncated portion manually
        std::strncpy(instruction_buffer_, text, MAX_INSTRUCTION_LENGTH);
        instruction_buffer_[MAX_INSTRUCTION_LENGTH] = '\0';
        lv_subject_set_pointer(&instruction_text_, instruction_buffer_);
    } else {
        lv_subject_copy_string(&instruction_text_, text);
    }
}

void WizardTouchCalibrationStep::update_crosshair_position() {
    if (!screen_root_ || !panel_) {
        return;
    }

    lv_obj_t* crosshair = lv_obj_find_by_name(screen_root_, "crosshair");
    if (!crosshair) {
        spdlog::warn("[{}] Crosshair widget not found", get_name());
        return;
    }

    auto state = panel_->get_state();

    // Hide crosshair in IDLE, VERIFY, and COMPLETE states
    if (state == helix::TouchCalibrationPanel::State::IDLE ||
        state == helix::TouchCalibrationPanel::State::VERIFY ||
        state == helix::TouchCalibrationPanel::State::COMPLETE) {
        lv_obj_add_flag(crosshair, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Show and position crosshair for calibration points
    lv_obj_remove_flag(crosshair, LV_OBJ_FLAG_HIDDEN);

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

    // Center crosshair on target
    lv_obj_set_pos(crosshair, target.x - CROSSHAIR_HALF_SIZE, target.y - CROSSHAIR_HALF_SIZE);
    lv_subject_set_int(&current_step_, step);

    spdlog::debug("[{}] Crosshair positioned at ({}, {}) for step {}", get_name(), target.x,
                  target.y, step);
}

void WizardTouchCalibrationStep::update_button_visibility() {
    if (!screen_root_ || !panel_) {
        return;
    }

    // Find buttons
    lv_obj_t* btn_start = lv_obj_find_by_name(screen_root_, "btn_start");
    lv_obj_t* btn_accept = lv_obj_find_by_name(screen_root_, "btn_accept");
    lv_obj_t* btn_retry = lv_obj_find_by_name(screen_root_, "btn_retry");

    auto state = panel_->get_state();

    // Start button: visible in IDLE state
    if (btn_start) {
        if (state == helix::TouchCalibrationPanel::State::IDLE) {
            lv_obj_remove_flag(btn_start, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(btn_start, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Accept/Retry buttons: visible in VERIFY state
    if (btn_accept) {
        if (state == helix::TouchCalibrationPanel::State::VERIFY) {
            lv_obj_remove_flag(btn_accept, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(btn_accept, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (btn_retry) {
        if (state == helix::TouchCalibrationPanel::State::VERIFY) {
            lv_obj_remove_flag(btn_retry, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(btn_retry, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
