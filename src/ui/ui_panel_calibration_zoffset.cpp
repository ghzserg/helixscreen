// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_calibration_zoffset.h"

#include "ui_event_safety.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"

#include "app_globals.h"
#include "moonraker_client.h"
#include "printer_state.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <memory>

// ============================================================================
// STATIC STATE
// ============================================================================

// State subject (0=IDLE, 1=PROBING, 2=ADJUSTING, 3=SAVING, 4=COMPLETE, 5=ERROR)
static lv_subject_t s_zoffset_cal_state;
static bool s_callbacks_registered = false;

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

ZOffsetCalibrationPanel::ZOffsetCalibrationPanel() {
    spdlog::debug("[ZOffsetCal] Instance created");
}

ZOffsetCalibrationPanel::~ZOffsetCalibrationPanel() {
    // Applying [L011]: No mutex in destructors

    // Deinitialize subjects to disconnect observers before we're destroyed
    if (subjects_initialized_) {
        lv_subject_deinit(&s_zoffset_cal_state);
        subjects_initialized_ = false;
    }

    // Remove observers to prevent use-after-free if subjects outlive us
    if (manual_probe_active_observer_) {
        lv_observer_remove(manual_probe_active_observer_);
        manual_probe_active_observer_ = nullptr;
    }
    if (manual_probe_z_observer_) {
        lv_observer_remove(manual_probe_z_observer_);
        manual_probe_z_observer_ = nullptr;
    }

    // Clear widget pointers (owned by LVGL)
    overlay_root_ = nullptr;
    parent_screen_ = nullptr;
    z_position_display_ = nullptr;
    final_offset_label_ = nullptr;
    error_message_ = nullptr;

    // Guard against static destruction order fiasco (spdlog may be gone)
    if (!StaticPanelRegistry::is_destroyed()) {
        spdlog::debug("[ZOffsetCal] Destroyed");
    }
}

// ============================================================================
// SUBJECT REGISTRATION
// ============================================================================

void ZOffsetCalibrationPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::debug("[ZOffsetCal] Subjects already initialized");
        return;
    }

    spdlog::debug("[ZOffsetCal] Initializing subjects");

    // Register state subject (shared across all instances)
    lv_subject_init_int(&s_zoffset_cal_state, 0);
    lv_xml_register_subject(nullptr, "zoffset_cal_state", &s_zoffset_cal_state);

    subjects_initialized_ = true;

    // Register XML event callbacks (once globally)
    if (!s_callbacks_registered) {
        lv_xml_register_event_cb(nullptr, "on_zoffset_start_clicked", on_start_clicked);
        lv_xml_register_event_cb(nullptr, "on_zoffset_abort_clicked", on_abort_clicked);
        lv_xml_register_event_cb(nullptr, "on_zoffset_accept_clicked", on_accept_clicked);
        lv_xml_register_event_cb(nullptr, "on_zoffset_done_clicked", on_done_clicked);
        lv_xml_register_event_cb(nullptr, "on_zoffset_retry_clicked", on_retry_clicked);

        // Z adjustment buttons
        lv_xml_register_event_cb(nullptr, "on_zoffset_z_down_1", on_z_down_1);
        lv_xml_register_event_cb(nullptr, "on_zoffset_z_down_01", on_z_down_01);
        lv_xml_register_event_cb(nullptr, "on_zoffset_z_down_005", on_z_down_005);
        lv_xml_register_event_cb(nullptr, "on_zoffset_z_down_001", on_z_down_001);
        lv_xml_register_event_cb(nullptr, "on_zoffset_z_up_001", on_z_up_001);
        lv_xml_register_event_cb(nullptr, "on_zoffset_z_up_005", on_z_up_005);
        lv_xml_register_event_cb(nullptr, "on_zoffset_z_up_01", on_z_up_01);
        lv_xml_register_event_cb(nullptr, "on_zoffset_z_up_1", on_z_up_1);

        s_callbacks_registered = true;
    }

    spdlog::debug("[ZOffsetCal] Subjects and callbacks registered");
}

// ============================================================================
// CREATE / SETUP
// ============================================================================

lv_obj_t* ZOffsetCalibrationPanel::create(lv_obj_t* parent) {
    if (overlay_root_) {
        spdlog::debug("[ZOffsetCal] Overlay already created");
        return overlay_root_;
    }

    parent_screen_ = parent;

    spdlog::debug("[ZOffsetCal] Creating overlay from XML");

    // Create from XML
    overlay_root_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent, "calibration_zoffset_panel", nullptr));
    if (!overlay_root_) {
        spdlog::error("[ZOffsetCal] Failed to create panel from XML");
        return nullptr;
    }

    // Initially hidden (will be shown by show())
    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    // Setup widget references
    setup_widgets();

    spdlog::info("[ZOffsetCal] Overlay created");
    return overlay_root_;
}

void ZOffsetCalibrationPanel::setup_widgets() {
    if (!overlay_root_) {
        spdlog::error("[ZOffsetCal] NULL overlay_root_");
        return;
    }

    // State visibility is handled via XML subject bindings
    // Event handlers are registered via init_subjects() before XML creation

    // Find display elements (for programmatic updates not covered by subject bindings)
    z_position_display_ = lv_obj_find_by_name(overlay_root_, "z_position_display");
    final_offset_label_ = lv_obj_find_by_name(overlay_root_, "final_offset_label");
    error_message_ = lv_obj_find_by_name(overlay_root_, "error_message");

    // Set initial state
    set_state(State::IDLE);

    // Subscribe to manual_probe state changes from Klipper
    // This replaces the fake timer with real state tracking
    PrinterState& ps = get_printer_state();

    manual_probe_active_observer_ = lv_subject_add_observer(ps.get_manual_probe_active_subject(),
                                                            on_manual_probe_active_changed, this);

    manual_probe_z_observer_ = lv_subject_add_observer(ps.get_manual_probe_z_position_subject(),
                                                       on_manual_probe_z_changed, this);

    spdlog::debug("[ZOffsetCal] Widget setup complete");
}

// ============================================================================
// SHOW
// ============================================================================

void ZOffsetCalibrationPanel::show() {
    if (!overlay_root_) {
        spdlog::error("[ZOffsetCal] Cannot show: overlay not created");
        return;
    }

    spdlog::debug("[ZOffsetCal] Showing overlay");

    // Register with NavigationManager for lifecycle callbacks
    NavigationManager::instance().register_overlay_instance(overlay_root_, this);

    // Push onto navigation stack - on_activate() will be called by NavigationManager
    ui_nav_push_overlay(overlay_root_);

    spdlog::info("[ZOffsetCal] Overlay shown");
}

// ============================================================================
// LIFECYCLE HOOKS
// ============================================================================

void ZOffsetCalibrationPanel::on_activate() {
    // Call base class first
    OverlayBase::on_activate();

    spdlog::debug("[ZOffsetCal] on_activate()");

    // Reset to idle state
    set_state(State::IDLE);

    // Reset Z position display
    current_z_ = 0.0f;
    final_offset_ = 0.0f;
    if (z_position_display_) {
        lv_label_set_text(z_position_display_, "Z: 0.000");
    }
}

void ZOffsetCalibrationPanel::on_deactivate() {
    spdlog::debug("[ZOffsetCal] on_deactivate()");

    // If calibration is in progress, abort it
    if (state_ == State::ADJUSTING || state_ == State::PROBING) {
        spdlog::info("[ZOffsetCal] Aborting calibration on deactivate");
        send_abort();
    }

    // Call base class
    OverlayBase::on_deactivate();
}

void ZOffsetCalibrationPanel::cleanup() {
    spdlog::debug("[ZOffsetCal] Cleaning up");

    // Remove observers before cleanup (applying [L020])
    if (manual_probe_active_observer_) {
        lv_observer_remove(manual_probe_active_observer_);
        manual_probe_active_observer_ = nullptr;
    }
    if (manual_probe_z_observer_) {
        lv_observer_remove(manual_probe_z_observer_);
        manual_probe_z_observer_ = nullptr;
    }

    // Unregister from NavigationManager before cleaning up
    if (overlay_root_) {
        NavigationManager::instance().unregister_overlay_instance(overlay_root_);
    }

    // Call base class to set cleanup_called_ flag
    OverlayBase::cleanup();

    // Clear references
    parent_screen_ = nullptr;
    z_position_display_ = nullptr;
    final_offset_label_ = nullptr;
    error_message_ = nullptr;
}

// ============================================================================
// STATE MANAGEMENT
// ============================================================================

void ZOffsetCalibrationPanel::set_state(State new_state) {
    spdlog::debug("[ZOffsetCal] State change: {} -> {}", static_cast<int>(state_),
                  static_cast<int>(new_state));
    state_ = new_state;

    // Update subject - XML bindings handle visibility automatically
    lv_subject_set_int(&s_zoffset_cal_state, static_cast<int>(new_state));
}

// ============================================================================
// GCODE COMMANDS
// ============================================================================

void ZOffsetCalibrationPanel::send_probe_calibrate() {
    if (!client_) {
        spdlog::error("[ZOffsetCal] No Moonraker client");
        on_calibration_result(false, "No printer connection");
        return;
    }

    // Auto-detect calibration method based on printer capabilities:
    // - Printers with probe (BLTouch, inductive, etc.) use PROBE_CALIBRATE
    // - Printers with only mechanical endstop use Z_ENDSTOP_CALIBRATE
    // Both commands enter manual_probe mode and work identically from UI perspective
    PrinterState& ps = get_printer_state();
    const char* calibrate_cmd = ps.has_probe() ? "PROBE_CALIBRATE" : "Z_ENDSTOP_CALIBRATE";

    spdlog::info("[ZOffsetCal] Sending {} (has_probe={})", calibrate_cmd, ps.has_probe());
    int result = client_->gcode_script(calibrate_cmd);
    if (result != 0) {
        spdlog::error("[ZOffsetCal] Failed to send {} (error {})", calibrate_cmd, result);
        on_calibration_result(false, "Failed to start calibration");
    }
    // State transition to ADJUSTING is handled by on_manual_probe_active_changed
    // when Klipper reports manual_probe.is_active=true
}

void ZOffsetCalibrationPanel::send_testz(float delta) {
    if (!client_)
        return;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "TESTZ Z=%.3f", delta);
    spdlog::debug("[ZOffsetCal] Sending: {}", cmd);

    int result = client_->gcode_script(cmd);
    if (result != 0) {
        spdlog::warn("[ZOffsetCal] Failed to send TESTZ (error {})", result);
    }

    // Z position display is updated by the manual_probe_z_position observer
    // when Klipper reports the new position after processing the TESTZ command
}

void ZOffsetCalibrationPanel::send_accept() {
    if (!client_)
        return;

    spdlog::info("[ZOffsetCal] Sending ACCEPT");
    int result = client_->gcode_script("ACCEPT");
    if (result != 0) {
        spdlog::error("[ZOffsetCal] Failed to send ACCEPT (error {})", result);
        on_calibration_result(false, "Failed to accept calibration");
        return;
    }

    // Save the final offset
    final_offset_ = current_z_;

    // Now save config (this will restart Klipper)
    set_state(State::SAVING);

    spdlog::info("[ZOffsetCal] Sending SAVE_CONFIG");
    result = client_->gcode_script("SAVE_CONFIG");
    if (result != 0) {
        spdlog::error("[ZOffsetCal] Failed to send SAVE_CONFIG (error {})", result);
        on_calibration_result(false, "Failed to save configuration");
    }
    // Note: SAVE_CONFIG restarts Klipper, we need to wait for reconnection
    // For now, just show complete state
    on_calibration_result(true, "");
}

void ZOffsetCalibrationPanel::send_abort() {
    if (!client_)
        return;

    spdlog::info("[ZOffsetCal] Sending ABORT");
    client_->gcode_script("ABORT");

    set_state(State::IDLE);
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

void ZOffsetCalibrationPanel::handle_start_clicked() {
    spdlog::debug("[ZOffsetCal] Start clicked");
    set_state(State::PROBING);
    send_probe_calibrate();

    // State transition from PROBING -> ADJUSTING is now handled by the
    // manual_probe_active observer when Klipper reports is_active=true
}

void ZOffsetCalibrationPanel::handle_z_adjust(float delta) {
    if (state_ != State::ADJUSTING)
        return;
    send_testz(delta);
}

void ZOffsetCalibrationPanel::handle_accept_clicked() {
    spdlog::debug("[ZOffsetCal] Accept clicked");
    send_accept();
}

void ZOffsetCalibrationPanel::handle_abort_clicked() {
    spdlog::debug("[ZOffsetCal] Abort clicked");
    send_abort();
}

void ZOffsetCalibrationPanel::handle_done_clicked() {
    spdlog::debug("[ZOffsetCal] Done clicked");
    set_state(State::IDLE);
    ui_nav_go_back();
}

void ZOffsetCalibrationPanel::handle_retry_clicked() {
    spdlog::debug("[ZOffsetCal] Retry clicked");
    set_state(State::IDLE);
}

// ============================================================================
// PUBLIC METHODS
// ============================================================================

void ZOffsetCalibrationPanel::update_z_position(float z_position) {
    current_z_ = z_position;
    if (z_position_display_) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Z: %.3f", z_position);
        lv_label_set_text(z_position_display_, buf);
    }
}

void ZOffsetCalibrationPanel::on_calibration_result(bool success, const std::string& message) {
    if (success) {
        // Update final offset display
        if (final_offset_label_) {
            char buf[64];
            snprintf(buf, sizeof(buf), "New Z-Offset: %.3f", final_offset_);
            lv_label_set_text(final_offset_label_, buf);
        }
        set_state(State::COMPLETE);
    } else {
        if (error_message_) {
            lv_label_set_text(error_message_, message.c_str());
        }
        set_state(State::ERROR);
    }
}

// ============================================================================
// STATIC TRAMPOLINES
// ============================================================================

void ZOffsetCalibrationPanel::on_start_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ZOffsetCal] on_start_clicked");
    get_global_zoffset_cal_panel().handle_start_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void ZOffsetCalibrationPanel::on_z_down_1(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ZOffsetCal] on_z_down_1");
    get_global_zoffset_cal_panel().handle_z_adjust(-1.0f);
    LVGL_SAFE_EVENT_CB_END();
}

void ZOffsetCalibrationPanel::on_z_down_01(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ZOffsetCal] on_z_down_01");
    get_global_zoffset_cal_panel().handle_z_adjust(-0.1f);
    LVGL_SAFE_EVENT_CB_END();
}

void ZOffsetCalibrationPanel::on_z_down_005(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ZOffsetCal] on_z_down_005");
    get_global_zoffset_cal_panel().handle_z_adjust(-0.05f);
    LVGL_SAFE_EVENT_CB_END();
}

void ZOffsetCalibrationPanel::on_z_down_001(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ZOffsetCal] on_z_down_001");
    get_global_zoffset_cal_panel().handle_z_adjust(-0.01f);
    LVGL_SAFE_EVENT_CB_END();
}

void ZOffsetCalibrationPanel::on_z_up_001(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ZOffsetCal] on_z_up_001");
    get_global_zoffset_cal_panel().handle_z_adjust(0.01f);
    LVGL_SAFE_EVENT_CB_END();
}

void ZOffsetCalibrationPanel::on_z_up_005(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ZOffsetCal] on_z_up_005");
    get_global_zoffset_cal_panel().handle_z_adjust(0.05f);
    LVGL_SAFE_EVENT_CB_END();
}

void ZOffsetCalibrationPanel::on_z_up_01(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ZOffsetCal] on_z_up_01");
    get_global_zoffset_cal_panel().handle_z_adjust(0.1f);
    LVGL_SAFE_EVENT_CB_END();
}

void ZOffsetCalibrationPanel::on_z_up_1(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ZOffsetCal] on_z_up_1");
    get_global_zoffset_cal_panel().handle_z_adjust(1.0f);
    LVGL_SAFE_EVENT_CB_END();
}

void ZOffsetCalibrationPanel::on_accept_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ZOffsetCal] on_accept_clicked");
    get_global_zoffset_cal_panel().handle_accept_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void ZOffsetCalibrationPanel::on_abort_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ZOffsetCal] on_abort_clicked");
    get_global_zoffset_cal_panel().handle_abort_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void ZOffsetCalibrationPanel::on_done_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ZOffsetCal] on_done_clicked");
    get_global_zoffset_cal_panel().handle_done_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void ZOffsetCalibrationPanel::on_retry_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ZOffsetCal] on_retry_clicked");
    get_global_zoffset_cal_panel().handle_retry_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// OBSERVER CALLBACKS (for real Klipper manual_probe state)
// ============================================================================

void ZOffsetCalibrationPanel::on_manual_probe_active_changed(lv_observer_t* observer,
                                                             lv_subject_t* subject) {
    auto* self = static_cast<ZOffsetCalibrationPanel*>(lv_observer_get_user_data(observer));
    if (!self)
        return;

    int is_active = lv_subject_get_int(subject);
    spdlog::debug("[ZOffsetCal] manual_probe_active changed: {}", is_active);

    if (is_active && self->state_ == State::PROBING) {
        // Klipper has entered manual probe mode - transition to ADJUSTING
        spdlog::info("[ZOffsetCal] PROBE_CALIBRATE complete, entering adjustment phase");
        self->set_state(State::ADJUSTING);
    } else if (!is_active && self->state_ == State::ADJUSTING) {
        // Manual probe mode ended externally (G28 from console, printer error, ABORT from macros)
        // The state should already have been changed by button handlers for user-initiated actions,
        // but this catches cases where Klipper ends the session externally
        spdlog::info("[ZOffsetCal] Manual probe ended externally, returning to IDLE");
        self->set_state(State::IDLE);
    }
}

void ZOffsetCalibrationPanel::on_manual_probe_z_changed(lv_observer_t* observer,
                                                        lv_subject_t* subject) {
    auto* self = static_cast<ZOffsetCalibrationPanel*>(lv_observer_get_user_data(observer));
    if (!self)
        return;

    // Only update Z display when in ADJUSTING state
    if (self->state_ != State::ADJUSTING)
        return;

    // Z position is stored in microns (multiply by 0.001 to get mm)
    int z_microns = lv_subject_get_int(subject);
    float z_mm = static_cast<float>(z_microns) * 0.001f;

    spdlog::trace("[ZOffsetCal] Z position from Klipper: {:.3f}mm", z_mm);
    self->update_z_position(z_mm);
}

// ============================================================================
// GLOBAL INSTANCE AND ROW CLICK HANDLER
// ============================================================================

static std::unique_ptr<ZOffsetCalibrationPanel> g_zoffset_cal_panel;

// Forward declarations
static void on_zoffset_row_clicked(lv_event_t* e);
MoonrakerClient* get_moonraker_client();

ZOffsetCalibrationPanel& get_global_zoffset_cal_panel() {
    if (!g_zoffset_cal_panel) {
        g_zoffset_cal_panel = std::make_unique<ZOffsetCalibrationPanel>();
        StaticPanelRegistry::instance().register_destroy("ZOffsetCalibrationPanel",
                                                         []() { g_zoffset_cal_panel.reset(); });
    }
    return *g_zoffset_cal_panel;
}

void destroy_zoffset_cal_panel() {
    g_zoffset_cal_panel.reset();
}

void init_zoffset_row_handler() {
    lv_xml_register_event_cb(nullptr, "on_zoffset_row_clicked", on_zoffset_row_clicked);
    spdlog::debug("[ZOffsetCal] Row click callback registered");
}

void init_zoffset_event_callbacks() {
    // NOTE: Event callbacks are now registered by init_subjects() in the global instance.
    // This function is kept for backward compatibility but is effectively a no-op
    // if init_subjects() has already been called.
    auto& overlay = get_global_zoffset_cal_panel();
    if (!overlay.are_subjects_initialized()) {
        overlay.init_subjects();
    }
    spdlog::debug("[ZOffsetCal] Event callbacks registration verified");
}

/**
 * @brief Row click handler for opening Z-Offset calibration from Advanced panel
 *
 * Registered via init_zoffset_row_handler().
 * Uses OverlayBase pattern with lazy creation.
 */
static void on_zoffset_row_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[ZOffsetCal] Z-Offset row clicked");

    auto& overlay = get_global_zoffset_cal_panel();

    // Lazy-create the Z-Offset calibration panel
    if (!overlay.get_root()) {
        overlay.init_subjects();
        overlay.set_client(get_moonraker_client());
        overlay.create(lv_display_get_screen_active(NULL));
    }

    overlay.show();
}
