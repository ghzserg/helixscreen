// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_controls.h"

#include "app_globals.h"
#include "printer_state.h"
#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_nav.h"
#include "ui_panel_extrusion.h"
#include "ui_panel_motion.h"
#include "ui_panel_temp_control.h"

#include <spdlog/spdlog.h>

#include <memory>

// Forward declarations for class-based API
class MotionPanel;
MotionPanel& get_global_motion_panel();
class ExtrusionPanel;
ExtrusionPanel& get_global_extrusion_panel();

// ============================================================================
// CONSTRUCTOR
// ============================================================================

ControlsPanel::ControlsPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // Dependencies passed for interface consistency
    // Child panels (motion, temp, extrusion) may use these when wired
}

// ============================================================================
// DEPENDENCY INJECTION
// ============================================================================

void ControlsPanel::set_temp_control_panel(TempControlPanel* temp_panel) {
    temp_control_panel_ = temp_panel;
    spdlog::debug("[{}] TempControlPanel reference set", get_name());
}

// ============================================================================
// PANELBASE IMPLEMENTATION
// ============================================================================

void ControlsPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // Launcher-level doesn't own subjects - child panels init their own
    // when lazily created. Nothing to do here.

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void ControlsPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    // Wire up card click handlers
    setup_card_handlers();

    spdlog::info("[{}] Setup complete", get_name());
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void ControlsPanel::setup_card_handlers() {
    // Find launcher card objects by name
    lv_obj_t* card_motion = lv_obj_find_by_name(panel_, "card_motion");
    lv_obj_t* card_nozzle_temp = lv_obj_find_by_name(panel_, "card_nozzle_temp");
    lv_obj_t* card_bed_temp = lv_obj_find_by_name(panel_, "card_bed_temp");
    lv_obj_t* card_extrusion = lv_obj_find_by_name(panel_, "card_extrusion");
    lv_obj_t* card_fan = lv_obj_find_by_name(panel_, "card_fan");
    lv_obj_t* card_motors = lv_obj_find_by_name(panel_, "card_motors");

    // Verify all cards found
    if (!card_motion || !card_nozzle_temp || !card_bed_temp || !card_extrusion || !card_fan ||
        !card_motors) {
        spdlog::error("[{}] Failed to find all launcher cards", get_name());
        return;
    }

    // Wire click event handlers - pass 'this' as user_data for trampolines
    lv_obj_add_event_cb(card_motion, on_motion_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(card_nozzle_temp, on_nozzle_temp_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(card_bed_temp, on_bed_temp_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(card_extrusion, on_extrusion_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(card_fan, on_fan_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(card_motors, on_motors_clicked, LV_EVENT_CLICKED, this);

    // Make cards clickable (except fan which is Phase 2 placeholder)
    lv_obj_add_flag(card_motion, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(card_nozzle_temp, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(card_bed_temp, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(card_extrusion, LV_OBJ_FLAG_CLICKABLE);
    // card_fan is disabled (Phase 2), don't make clickable
    lv_obj_add_flag(card_motors, LV_OBJ_FLAG_CLICKABLE);

    spdlog::debug("[{}] Card handlers wired", get_name());
}

// ============================================================================
// CARD CLICK HANDLERS
// ============================================================================

void ControlsPanel::handle_motion_clicked() {
    spdlog::debug("[{}] Motion card clicked - opening Motion sub-screen", get_name());

    // Create motion panel on first access (lazy initialization)
    if (!motion_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating motion panel...", get_name());

        // Create from XML
        motion_panel_ = static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "motion_panel", nullptr));
        if (motion_panel_) {
            // Setup event handlers for motion panel (class-based API)
            get_global_motion_panel().setup(motion_panel_, parent_screen_);

            // Initially hidden
            lv_obj_add_flag(motion_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Motion panel created and initialized", get_name());
        } else {
            LOG_ERROR_INTERNAL("Failed to create motion panel from XML");
            NOTIFY_ERROR("Failed to load motion panel");
            return;
        }
    }

    // Push motion panel onto navigation history and show it
    if (motion_panel_) {
        ui_nav_push_overlay(motion_panel_);
    }
}

void ControlsPanel::handle_nozzle_temp_clicked() {
    spdlog::debug("[{}] Nozzle Temp card clicked - opening Nozzle Temperature sub-screen", get_name());

    if (!temp_control_panel_) {
        LOG_ERROR_INTERNAL("TempControlPanel not initialized");
        NOTIFY_ERROR("Temperature panel not available");
        return;
    }

    // Create nozzle temp panel on first access (lazy initialization)
    if (!nozzle_temp_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating nozzle temperature panel...", get_name());

        // Create from XML
        nozzle_temp_panel_ = static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "nozzle_temp_panel", nullptr));
        if (nozzle_temp_panel_) {
            // Setup event handlers for nozzle temp panel via TempControlPanel
            temp_control_panel_->setup_nozzle_panel(nozzle_temp_panel_, parent_screen_);

            // Initially hidden
            lv_obj_add_flag(nozzle_temp_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Nozzle temp panel created and initialized", get_name());
        } else {
            LOG_ERROR_INTERNAL("Failed to create nozzle temp panel from XML");
            NOTIFY_ERROR("Failed to load temperature panel");
            return;
        }
    }

    // Push nozzle temp panel onto navigation history and show it
    if (nozzle_temp_panel_) {
        ui_nav_push_overlay(nozzle_temp_panel_);
    }
}

void ControlsPanel::handle_bed_temp_clicked() {
    spdlog::debug("[{}] Bed Temp card clicked - opening Heatbed Temperature sub-screen", get_name());

    if (!temp_control_panel_) {
        LOG_ERROR_INTERNAL("TempControlPanel not initialized");
        NOTIFY_ERROR("Temperature panel not available");
        return;
    }

    // Create bed temp panel on first access (lazy initialization)
    if (!bed_temp_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating bed temperature panel...", get_name());

        // Create from XML
        bed_temp_panel_ = static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "bed_temp_panel", nullptr));
        if (bed_temp_panel_) {
            // Setup event handlers for bed temp panel via TempControlPanel
            temp_control_panel_->setup_bed_panel(bed_temp_panel_, parent_screen_);

            // Initially hidden
            lv_obj_add_flag(bed_temp_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Bed temp panel created and initialized", get_name());
        } else {
            LOG_ERROR_INTERNAL("Failed to create bed temp panel from XML");
            NOTIFY_ERROR("Failed to load temperature panel");
            return;
        }
    }

    // Push bed temp panel onto navigation history and show it
    if (bed_temp_panel_) {
        ui_nav_push_overlay(bed_temp_panel_);
    }
}

void ControlsPanel::handle_extrusion_clicked() {
    spdlog::debug("[{}] Extrusion card clicked - opening Extrusion sub-screen", get_name());

    // Create extrusion panel on first access (lazy initialization)
    if (!extrusion_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating extrusion panel...", get_name());

        // Initialize extrusion panel subjects
        auto& extrusion_panel_instance = get_global_controls_extrusion_panel();
        if (!extrusion_panel_instance.are_subjects_initialized()) {
            extrusion_panel_instance.init_subjects();
        }

        // Create from XML
        extrusion_panel_ = static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "extrusion_panel", nullptr));
        if (extrusion_panel_) {
            // Setup event handlers for extrusion panel
            extrusion_panel_instance.setup(extrusion_panel_, parent_screen_);

            // Initially hidden
            lv_obj_add_flag(extrusion_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Extrusion panel created and initialized", get_name());
        } else {
            LOG_ERROR_INTERNAL("Failed to create extrusion panel from XML");
            NOTIFY_ERROR("Failed to load extrusion panel");
            return;
        }
    }

    // Push extrusion panel onto navigation history and show it
    if (extrusion_panel_) {
        ui_nav_push_overlay(extrusion_panel_);
    }
}

void ControlsPanel::handle_fan_clicked() {
    spdlog::debug("[{}] Fan card clicked - Phase 2 feature", get_name());
    // TODO: Create and show fan control sub-screen (Phase 2)
}

void ControlsPanel::handle_motors_clicked() {
    spdlog::debug("[{}] Motors Disable card clicked", get_name());
    // TODO: Show confirmation dialog, then send motors disable command
}

// ============================================================================
// STATIC TRAMPOLINES
// ============================================================================

void ControlsPanel::on_motion_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_motion_clicked");
    auto* self = static_cast<ControlsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_motion_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_nozzle_temp_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_nozzle_temp_clicked");
    auto* self = static_cast<ControlsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_nozzle_temp_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_bed_temp_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_bed_temp_clicked");
    auto* self = static_cast<ControlsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_bed_temp_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_extrusion_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_extrusion_clicked");
    auto* self = static_cast<ControlsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_extrusion_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_fan_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_fan_clicked");
    auto* self = static_cast<ControlsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_fan_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_motors_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_motors_clicked");
    auto* self = static_cast<ControlsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_motors_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// GLOBAL INSTANCE (needed by main.cpp)
// ============================================================================

static std::unique_ptr<ControlsPanel> g_controls_panel;

ControlsPanel& get_global_controls_panel() {
    if (!g_controls_panel) {
        g_controls_panel = std::make_unique<ControlsPanel>(get_printer_state(), nullptr);
    }
    return *g_controls_panel;
}
