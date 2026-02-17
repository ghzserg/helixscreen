// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_print_tune_overlay.h"

#include "ui_error_reporting.h"
#include "ui_nav_manager.h"
#include "ui_panel_common.h"
#include "ui_toast.h"
#include "ui_z_offset_indicator.h"

#include "format_utils.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "moonraker_api.h"
#include "printer_state.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

// ============================================================================
// SINGLETON ACCESSOR
// ============================================================================

static std::unique_ptr<PrintTuneOverlay> g_print_tune_overlay;

PrintTuneOverlay& get_print_tune_overlay() {
    if (!g_print_tune_overlay) {
        g_print_tune_overlay = std::make_unique<PrintTuneOverlay>();
        StaticPanelRegistry::instance().register_destroy("PrintTuneOverlay",
                                                         []() { g_print_tune_overlay.reset(); });
    }
    return *g_print_tune_overlay;
}

// ============================================================================
// XML EVENT CALLBACKS (free functions using global accessor)
// ============================================================================

// Speed slider: display update while dragging (no G-code)
static void on_tune_speed_display_cb(lv_event_t* e) {
    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (slider) {
        int value = lv_slider_get_value(slider);
        get_print_tune_overlay().handle_speed_display(value);
    }
}

// Speed slider: send G-code on release
static void on_tune_speed_send_cb(lv_event_t* e) {
    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (slider) {
        int value = lv_slider_get_value(slider);
        spdlog::debug("[PrintTuneOverlay] Speed slider released at {}", value);
        get_print_tune_overlay().handle_speed_send(value);
    }
}

// Flow slider: display update while dragging (no G-code)
static void on_tune_flow_display_cb(lv_event_t* e) {
    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (slider) {
        int value = lv_slider_get_value(slider);
        get_print_tune_overlay().handle_flow_display(value);
    }
}

// Flow slider: send G-code on release
static void on_tune_flow_send_cb(lv_event_t* e) {
    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (slider) {
        int value = lv_slider_get_value(slider);
        spdlog::debug("[PrintTuneOverlay] Flow slider released at {}", value);
        get_print_tune_overlay().handle_flow_send(value);
    }
}

static void on_tune_reset_clicked_cb(lv_event_t* /*e*/) {
    get_print_tune_overlay().handle_reset();
}

// Z-offset step amount selector (user_data = index "0"-"3")
static void on_tune_z_step_cb(lv_event_t* e) {
    const char* idx_str = static_cast<const char*>(lv_event_get_user_data(e));
    if (!idx_str)
        return;
    get_print_tune_overlay().handle_z_step_select(atoi(idx_str));
}

// Z-offset direction adjust (user_data = "-1" closer or "1" farther)
static void on_tune_z_adjust_cb(lv_event_t* e) {
    const char* dir_str = static_cast<const char*>(lv_event_get_user_data(e));
    if (!dir_str)
        return;
    get_print_tune_overlay().handle_z_adjust(atoi(dir_str));
}

static void on_tune_save_z_offset_cb(lv_event_t* /*e*/) {
    get_print_tune_overlay().handle_save_z_offset();
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

PrintTuneOverlay::PrintTuneOverlay() {
    spdlog::debug("[PrintTuneOverlay] Created");
}

PrintTuneOverlay::~PrintTuneOverlay() {
    // Clean up subjects
    if (subjects_initialized_) {
        subjects_.deinit_all();
        subjects_initialized_ = false;
    }

    // Panel widget is owned by LVGL parent, will be cleaned up when parent is deleted
    tune_panel_ = nullptr;

    spdlog::trace("[PrintTuneOverlay] Destroyed");
}

// ============================================================================
// SHOW (PUBLIC ENTRY POINT)
// ============================================================================

void PrintTuneOverlay::show(lv_obj_t* parent_screen, MoonrakerAPI* api,
                            PrinterState& printer_state) {
    spdlog::debug("[PrintTuneOverlay] show() called");

    // Store dependencies
    parent_screen_ = parent_screen;
    api_ = api;
    printer_state_ = &printer_state;

    // Initialize subjects if not already done (before XML creation)
    if (!subjects_initialized_) {
        init_subjects_internal();
    }

    // Create panel lazily
    if (!tune_panel_ && parent_screen_) {
        tune_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "print_tune_panel", nullptr));
        if (!tune_panel_) {
            spdlog::error("[PrintTuneOverlay] Failed to create panel from XML");
            NOTIFY_ERROR("Failed to load print tune panel");
            return;
        }

        // Setup panel (back button, etc.)
        setup_panel();
        lv_obj_add_flag(tune_panel_, LV_OBJ_FLAG_HIDDEN);

        // Keep base class in sync for cleanup and get_root()
        overlay_root_ = tune_panel_;

        spdlog::info("[PrintTuneOverlay] Panel created");
    }

    if (!tune_panel_) {
        spdlog::error("[PrintTuneOverlay] Cannot show - panel not created");
        return;
    }

    // Register with NavigationManager for lifecycle callbacks
    NavigationManager::instance().register_overlay_instance(tune_panel_, this);

    // Push onto navigation stack (on_activate will be called after animation)
    ui_nav_push_overlay(tune_panel_);
}

// ============================================================================
// INTERNAL: INITIALIZATION
// ============================================================================

void PrintTuneOverlay::init_subjects_internal() {
    if (subjects_initialized_) {
        return;
    }

    // Initialize tune panel subjects
    UI_MANAGED_SUBJECT_STRING(tune_speed_subject_, tune_speed_buf_, "100%", "tune_speed_display",
                              subjects_);
    UI_MANAGED_SUBJECT_STRING(tune_flow_subject_, tune_flow_buf_, "100%", "tune_flow_display",
                              subjects_);
    UI_MANAGED_SUBJECT_STRING(tune_z_offset_subject_, tune_z_offset_buf_, "0.000mm",
                              "tune_z_offset_display", subjects_);

    // Z-offset direction button icons (kinematic-aware, like motion panel)
    UI_MANAGED_SUBJECT_STRING(z_closer_icon_subject_, z_closer_icon_buf_, "arrow_down",
                              "tune_z_closer_icon", subjects_);
    UI_MANAGED_SUBJECT_STRING(z_farther_icon_subject_, z_farther_icon_buf_, "arrow_up",
                              "tune_z_farther_icon", subjects_);

    // Z-offset step amount boolean subjects (L040: one per button for bind_style radio pattern)
    UI_MANAGED_SUBJECT_INT(z_step_active_subjects_[0], 0, "z_step_0_active", subjects_);
    UI_MANAGED_SUBJECT_INT(z_step_active_subjects_[1], 0, "z_step_1_active", subjects_);
    UI_MANAGED_SUBJECT_INT(z_step_active_subjects_[2], 1, "z_step_2_active", subjects_); // default
    UI_MANAGED_SUBJECT_INT(z_step_active_subjects_[3], 0, "z_step_3_active", subjects_);

    // Register XML event callbacks
    lv_xml_register_event_cb(nullptr, "on_tune_speed_display", on_tune_speed_display_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_speed_send", on_tune_speed_send_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_flow_display", on_tune_flow_display_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_flow_send", on_tune_flow_send_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_reset_clicked", on_tune_reset_clicked_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_save_z_offset", on_tune_save_z_offset_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_z_step", on_tune_z_step_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_z_adjust", on_tune_z_adjust_cb);

    subjects_initialized_ = true;
    spdlog::debug("[PrintTuneOverlay] Subjects initialized");
}

// ============================================================================
// LIFECYCLE HOOKS
// ============================================================================

void PrintTuneOverlay::on_activate() {
    OverlayBase::on_activate();
    sync_sliders_to_state();
    spdlog::debug("[PrintTuneOverlay] Activated - sliders synced to state");
}

void PrintTuneOverlay::on_deactivate() {
    OverlayBase::on_deactivate();
    spdlog::debug("[PrintTuneOverlay] Deactivated");
}

void PrintTuneOverlay::setup_panel() {
    if (!tune_panel_ || !parent_screen_) {
        return;
    }

    // Use standard overlay panel setup for back button handling
    ui_overlay_panel_setup_standard(tune_panel_, parent_screen_, "overlay_header",
                                    "overlay_content");

    // Update Z-offset icons based on printer kinematics
    update_z_offset_icons(tune_panel_);

    spdlog::debug("[PrintTuneOverlay] Panel setup complete");
}

void PrintTuneOverlay::sync_sliders_to_state() {
    if (!tune_panel_ || !printer_state_) {
        return;
    }

    // Get current values from PrinterState
    int speed = lv_subject_get_int(printer_state_->get_speed_factor_subject());
    int flow = lv_subject_get_int(printer_state_->get_flow_factor_subject());

    // Update our cached values
    speed_percent_ = speed;
    flow_percent_ = flow;

    // Sync Z offset from PrinterState
    int z_offset_microns = lv_subject_get_int(printer_state_->get_gcode_z_offset_subject());
    update_z_offset_display(z_offset_microns);

    // Sync the visual indicator
    lv_obj_t* indicator = lv_obj_find_by_name(tune_panel_, "z_offset_indicator");
    if (indicator) {
        ui_z_offset_indicator_set_value(indicator, z_offset_microns);
    }

    // Update displays
    update_display();

    // Set slider positions
    lv_obj_t* overlay_content = lv_obj_find_by_name(tune_panel_, "overlay_content");
    if (overlay_content) {
        lv_obj_t* speed_slider = lv_obj_find_by_name(overlay_content, "speed_slider");
        lv_obj_t* flow_slider = lv_obj_find_by_name(overlay_content, "flow_slider");

        if (speed_slider) {
            lv_slider_set_value(speed_slider, speed, LV_ANIM_OFF);
        }
        if (flow_slider) {
            lv_slider_set_value(flow_slider, flow, LV_ANIM_OFF);
        }
    }

    spdlog::debug("[PrintTuneOverlay] Synced to state: speed={}%, flow={}%", speed, flow);
}

// ============================================================================
// ICON UPDATES
// ============================================================================

void PrintTuneOverlay::update_z_offset_icons(lv_obj_t* /*panel*/) {
    if (!printer_state_) {
        spdlog::warn("[PrintTuneOverlay] Cannot update icons - no printer_state_");
        return;
    }

    // Get kinematics type from PrinterState
    // 0 = unknown, 1 = bed moves Z (CoreXY), 2 = head moves Z (Cartesian/Delta)
    int kin = lv_subject_get_int(printer_state_->get_printer_bed_moves_subject());
    bool bed_moves_z = (kin == 1);

    // Set icon names via string subjects (bind_icon in XML)
    const char* closer_icon = bed_moves_z ? "arrow_expand_down" : "arrow_down";
    const char* farther_icon = bed_moves_z ? "arrow_expand_up" : "arrow_up";

    std::strncpy(z_closer_icon_buf_, closer_icon, sizeof(z_closer_icon_buf_) - 1);
    lv_subject_copy_string(&z_closer_icon_subject_, z_closer_icon_buf_);
    std::strncpy(z_farther_icon_buf_, farther_icon, sizeof(z_farther_icon_buf_) - 1);
    lv_subject_copy_string(&z_farther_icon_subject_, z_farther_icon_buf_);

    spdlog::debug("[PrintTuneOverlay] Z-offset icons: closer={}, farther={}", closer_icon,
                  farther_icon);
}

// ============================================================================
// DISPLAY UPDATES
// ============================================================================

void PrintTuneOverlay::update_display() {
    helix::format::format_percent(speed_percent_, tune_speed_buf_, sizeof(tune_speed_buf_));
    lv_subject_copy_string(&tune_speed_subject_, tune_speed_buf_);

    helix::format::format_percent(flow_percent_, tune_flow_buf_, sizeof(tune_flow_buf_));
    lv_subject_copy_string(&tune_flow_subject_, tune_flow_buf_);
}

void PrintTuneOverlay::update_speed_flow_display(int speed_percent, int flow_percent) {
    speed_percent_ = speed_percent;
    flow_percent_ = flow_percent;

    if (subjects_initialized_) {
        update_display();
    }
}

void PrintTuneOverlay::update_z_offset_display(int microns) {
    // Update display from PrinterState (microns -> mm)
    current_z_offset_ = microns / 1000.0;

    if (subjects_initialized_) {
        helix::format::format_distance_mm(current_z_offset_, 3, tune_z_offset_buf_,
                                          sizeof(tune_z_offset_buf_));
        lv_subject_copy_string(&tune_z_offset_subject_, tune_z_offset_buf_);
    }

    spdlog::trace("[PrintTuneOverlay] Z-offset display updated: {}um ({}mm)", microns,
                  current_z_offset_);
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

void PrintTuneOverlay::handle_speed_display(int value) {
    speed_percent_ = value;

    // Update display while dragging (no G-code)
    helix::format::format_percent(value, tune_speed_buf_, sizeof(tune_speed_buf_));
    lv_subject_copy_string(&tune_speed_subject_, tune_speed_buf_);
}

void PrintTuneOverlay::handle_speed_send(int value) {
    // Send G-code when slider is released
    if (api_) {
        std::string gcode = "M220 S" + std::to_string(value);
        api_->execute_gcode(
            gcode, [value]() { spdlog::debug("[PrintTuneOverlay] Speed set to {}%", value); },
            [](const MoonrakerError& err) {
                spdlog::error("[PrintTuneOverlay] Failed to set speed: {}", err.message);
                NOTIFY_ERROR("Failed to set print speed: {}", err.user_message());
            });
    }
}

void PrintTuneOverlay::handle_flow_display(int value) {
    flow_percent_ = value;

    // Update display while dragging (no G-code)
    helix::format::format_percent(value, tune_flow_buf_, sizeof(tune_flow_buf_));
    lv_subject_copy_string(&tune_flow_subject_, tune_flow_buf_);
}

void PrintTuneOverlay::handle_flow_send(int value) {
    // Send G-code when slider is released
    if (api_) {
        std::string gcode = "M221 S" + std::to_string(value);
        api_->execute_gcode(
            gcode, [value]() { spdlog::debug("[PrintTuneOverlay] Flow set to {}%", value); },
            [](const MoonrakerError& err) {
                spdlog::error("[PrintTuneOverlay] Failed to set flow: {}", err.message);
                NOTIFY_ERROR("Failed to set flow rate: {}", err.user_message());
            });
    }
}

void PrintTuneOverlay::handle_reset() {
    if (!tune_panel_) {
        return;
    }

    lv_obj_t* overlay_content = lv_obj_find_by_name(tune_panel_, "overlay_content");
    if (!overlay_content) {
        return;
    }

    // Reset sliders to 100%
    lv_obj_t* speed_slider = lv_obj_find_by_name(overlay_content, "speed_slider");
    lv_obj_t* flow_slider = lv_obj_find_by_name(overlay_content, "flow_slider");

    if (speed_slider) {
        lv_slider_set_value(speed_slider, 100, LV_ANIM_ON);
    }
    if (flow_slider) {
        lv_slider_set_value(flow_slider, 100, LV_ANIM_ON);
    }

    // Update displays
    speed_percent_ = 100;
    flow_percent_ = 100;
    std::snprintf(tune_speed_buf_, sizeof(tune_speed_buf_), "100%%");
    lv_subject_copy_string(&tune_speed_subject_, tune_speed_buf_);
    std::snprintf(tune_flow_buf_, sizeof(tune_flow_buf_), "100%%");
    lv_subject_copy_string(&tune_flow_subject_, tune_flow_buf_);

    // Send G-code commands
    if (api_) {
        api_->execute_gcode(
            "M220 S100", []() { spdlog::debug("[PrintTuneOverlay] Speed reset to 100%"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Failed to reset speed: {}", err.user_message());
            });
        api_->execute_gcode(
            "M221 S100", []() { spdlog::debug("[PrintTuneOverlay] Flow reset to 100%"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Failed to reset flow: {}", err.user_message());
            });
    }
}

void PrintTuneOverlay::handle_z_offset_changed(double delta) {
    // Update local display immediately for responsive feel
    current_z_offset_ += delta;
    helix::format::format_distance_mm(current_z_offset_, 3, tune_z_offset_buf_,
                                      sizeof(tune_z_offset_buf_));
    lv_subject_copy_string(&tune_z_offset_subject_, tune_z_offset_buf_);

    // Track pending delta for "unsaved adjustment" notification in Controls panel
    if (printer_state_) {
        int delta_microns = static_cast<int>(delta * 1000.0);
        printer_state_->add_pending_z_offset_delta(delta_microns);
    }

    spdlog::debug("[PrintTuneOverlay] Z-offset adjust: {:+.3f}mm (total: {:.3f}mm)", delta,
                  current_z_offset_);

    // Update the visual indicator
    if (tune_panel_) {
        lv_obj_t* indicator = lv_obj_find_by_name(tune_panel_, "z_offset_indicator");
        if (indicator) {
            int microns = static_cast<int>(current_z_offset_ * 1000.0);
            ui_z_offset_indicator_set_value(indicator, microns);
            ui_z_offset_indicator_flash_direction(indicator, delta > 0 ? 1 : -1);
        }
    }

    // Send SET_GCODE_OFFSET Z_ADJUST command to Klipper
    if (api_) {
        char gcode[64];
        std::snprintf(gcode, sizeof(gcode), "SET_GCODE_OFFSET Z_ADJUST=%.3f", delta);
        api_->execute_gcode(
            gcode, [delta]() { spdlog::debug("[PrintTuneOverlay] Z adjusted {:+.3f}mm", delta); },
            [](const MoonrakerError& err) {
                spdlog::error("[PrintTuneOverlay] Z-offset adjust failed: {}", err.message);
                NOTIFY_ERROR("Z-offset failed: {}", err.user_message());
            });
    }
}

void PrintTuneOverlay::handle_z_step_select(int idx) {
    if (idx < 0 || idx >= static_cast<int>(std::size(Z_STEP_AMOUNTS))) {
        spdlog::warn("[PrintTuneOverlay] Invalid step index: {}", idx);
        return;
    }
    selected_z_step_idx_ = idx;

    // Update boolean subjects (only one active at a time, like filament panel)
    for (int i = 0; i < static_cast<int>(std::size(Z_STEP_AMOUNTS)); i++) {
        lv_subject_set_int(&z_step_active_subjects_[i], i == idx ? 1 : 0);
    }

    spdlog::debug("[PrintTuneOverlay] Z-offset step selected: {}mm", Z_STEP_AMOUNTS[idx]);
}

void PrintTuneOverlay::handle_z_adjust(int direction) {
    double amount = Z_STEP_AMOUNTS[selected_z_step_idx_];
    handle_z_offset_changed(direction * amount);
}

void PrintTuneOverlay::handle_save_z_offset() {
    // gcode_offset strategy auto-persists via firmware macro
    if (printer_state_) {
        auto strategy = printer_state_->get_z_offset_calibration_strategy();
        if (strategy == ZOffsetCalibrationStrategy::GCODE_OFFSET) {
            spdlog::debug(
                "[PrintTuneOverlay] Z-offset auto-saved by firmware (gcode_offset strategy)");
            ui_toast_show(ToastSeverity::INFO, lv_tr("Z-offset is auto-saved by firmware"), 3000);
            return;
        }
    }

    // Show warning modal - SAVE_CONFIG restarts Klipper and cancels active prints!
    save_z_offset_modal_.set_on_confirm([this]() {
        if (api_) {
            api_->execute_gcode(
                "SAVE_CONFIG",
                []() {
                    spdlog::info("[PrintTuneOverlay] Z-offset saved - Klipper restarting");
                    ui_toast_show(ToastSeverity::WARNING,
                                  lv_tr("Z-offset saved - Klipper restarting..."), 5000);
                },
                [](const MoonrakerError& err) {
                    spdlog::error("[PrintTuneOverlay] SAVE_CONFIG failed: {}", err.message);
                    NOTIFY_ERROR("Save failed: {}", err.user_message());
                });
        }
    });
    save_z_offset_modal_.show(lv_screen_active());
}
