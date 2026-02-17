// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_calibration_zoffset.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_nav_manager.h"
#include "ui_update_queue.h"
#include "ui_z_offset_indicator.h"

#include "app_globals.h"
#include "config.h"
#include "moonraker_api.h"
#include "observer_factory.h"
#include "printer_state.h"
#include "standard_macros.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace helix;
using helix::ui::observe_int_sync;

// ============================================================================
// STATIC STATE
// ============================================================================

// State subject (0=IDLE, 1=PROBING, 2=ADJUSTING, 3=SAVING, 4=COMPLETE, 5=ERROR)
static lv_subject_t s_zoffset_cal_state;
// Warm bed toggle subject (0=off, 1=on)
static lv_subject_t s_zoffset_warm_bed;
static bool s_callbacks_registered = false;

/// Default bed temperature for calibration warm-up (°C)
static constexpr int DEFAULT_WARM_BED_TEMP = 45;

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

ZOffsetCalibrationPanel::ZOffsetCalibrationPanel() {
    spdlog::trace("[ZOffsetCal] Instance created");
}

ZOffsetCalibrationPanel::~ZOffsetCalibrationPanel() {
    // Applying [L011]: No mutex in destructors

    // Deinitialize subjects to disconnect observers before we're destroyed
    if (subjects_initialized_) {
        subjects_.deinit_all();
        subjects_initialized_ = false;
    }

    // ObserverGuard members automatically remove observers on destruction

    // Clear widget pointers (owned by LVGL)
    overlay_root_ = nullptr;
    parent_screen_ = nullptr;
    saved_z_offset_display_ = nullptr;
    z_position_display_ = nullptr;
    final_offset_label_ = nullptr;
    error_message_ = nullptr;

    // Guard against static destruction order fiasco (spdlog may be gone)
    if (!StaticPanelRegistry::is_destroyed()) {
        spdlog::trace("[ZOffsetCal] Destroyed");
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
    UI_MANAGED_SUBJECT_INT(s_zoffset_cal_state, 0, "zoffset_cal_state", subjects_);
    // Warm bed toggle (default off)
    UI_MANAGED_SUBJECT_INT(s_zoffset_warm_bed, 0, "zoffset_warm_bed", subjects_);

    subjects_initialized_ = true;

    // Register XML event callbacks (once globally)
    if (!s_callbacks_registered) {
        lv_xml_register_event_cb(nullptr, "on_zoffset_start_clicked", on_start_clicked);
        lv_xml_register_event_cb(nullptr, "on_zoffset_abort_clicked", on_abort_clicked);
        lv_xml_register_event_cb(nullptr, "on_zoffset_accept_clicked", on_accept_clicked);
        lv_xml_register_event_cb(nullptr, "on_zoffset_done_clicked", on_done_clicked);
        lv_xml_register_event_cb(nullptr, "on_zoffset_retry_clicked", on_retry_clicked);

        // Z adjustment (single callback — user_data carries the delta as a string)
        lv_xml_register_event_cb(nullptr, "on_zoffset_z_adjust", on_z_adjust);

        // Warm bed toggle
        lv_xml_register_event_cb(nullptr, "on_zoffset_warm_bed_toggled", on_warm_bed_toggled);

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
    saved_z_offset_display_ = lv_obj_find_by_name(overlay_root_, "saved_z_offset_display");
    z_position_display_ = lv_obj_find_by_name(overlay_root_, "z_position_display");
    final_offset_label_ = lv_obj_find_by_name(overlay_root_, "final_offset_label");
    error_message_ = lv_obj_find_by_name(overlay_root_, "error_message");

    // Set initial state
    set_state(State::IDLE);

    // Subscribe to manual_probe state changes from Klipper
    // This replaces the fake timer with real state tracking
    PrinterState& ps = get_printer_state();

    manual_probe_active_observer_ = observe_int_sync<ZOffsetCalibrationPanel>(
        ps.get_manual_probe_active_subject(), this,
        [](ZOffsetCalibrationPanel* self, int is_active) {
            spdlog::debug("[ZOffsetCal] manual_probe_active changed: {}", is_active);

            if (is_active && (self->state_ == State::PROBING || self->state_ == State::IDLE)) {
                // Klipper is in manual probe mode — either we initiated it (PROBING)
                // or it was already active when we opened (IDLE, e.g. started from Mainsail)
                spdlog::info("[ZOffsetCal] Manual probe active, entering adjustment phase "
                             "(was {})",
                             self->state_ == State::PROBING ? "PROBING" : "IDLE");
                self->set_state(State::ADJUSTING);

                // Populate saved z-offset display (snapshot value before calibration)
                if (self->saved_z_offset_display_) {
                    PrinterState& state = get_printer_state();
                    int saved_microns = state.get_configured_z_offset_microns();
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.3f mm", saved_microns / 1000.0);
                    lv_label_set_text(self->saved_z_offset_display_, buf);
                    spdlog::debug("[ZOffsetCal] Saved z-offset: {} microns ({} mm)", saved_microns,
                                  saved_microns / 1000.0);
                }
            } else if (!is_active && self->state_ == State::ADJUSTING) {
                // Manual probe mode ended externally (G28 from console, printer error, ABORT from
                // macros) The state should already have been changed by button handlers for
                // user-initiated actions, but this catches cases where Klipper ends the session
                // externally
                spdlog::info("[ZOffsetCal] Manual probe ended externally, returning to IDLE");
                self->set_state(State::IDLE);
            }
        });

    manual_probe_z_observer_ = observe_int_sync<ZOffsetCalibrationPanel>(
        ps.get_manual_probe_z_position_subject(), this,
        [](ZOffsetCalibrationPanel* self, int z_microns) {
            // Only update Z display when in ADJUSTING state
            if (self->state_ != State::ADJUSTING)
                return;

            // Z position is stored in microns (multiply by 0.001 to get mm)
            float z_mm = static_cast<float>(z_microns) * 0.001f;

            spdlog::trace("[ZOffsetCal] Z position from Klipper: {:.3f}mm", z_mm);
            self->update_z_position(z_mm);
        });

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
    NavigationManager::instance().push_overlay(overlay_root_);

    spdlog::info("[ZOffsetCal] Overlay shown");
}

// ============================================================================
// LIFECYCLE HOOKS
// ============================================================================

void ZOffsetCalibrationPanel::on_activate() {
    // Call base class first
    OverlayBase::on_activate();

    spdlog::debug("[ZOffsetCal] on_activate()");

    // If manual probe is already active (e.g., started from Mainsail before HelixScreen
    // launched), skip to ADJUSTING with the current Z position instead of resetting to IDLE
    auto& ps = get_printer_state();
    if (lv_subject_get_int(ps.get_manual_probe_active_subject()) == 1) {
        spdlog::info("[ZOffsetCal] Manual probe already active, resuming in ADJUSTING state");
        int z_microns = lv_subject_get_int(ps.get_manual_probe_z_position_subject());
        current_z_ = z_microns / 1000.0f;
        set_state(State::ADJUSTING);
        update_z_position(current_z_);
        return;
    }

    // Normal activation: reset to idle state
    set_state(State::IDLE);

    // Reset Z position display and tracking
    current_z_ = 0.0f;
    final_offset_ = 0.0f;
    cumulative_z_delta_ = 0.0f;
    if (z_position_display_) {
        lv_label_set_text(z_position_display_, "Z: 0.000");
    }

    // Reset the visual indicator
    if (overlay_root_) {
        lv_obj_t* indicator = lv_obj_find_by_name(overlay_root_, "z_offset_indicator");
        if (indicator) {
            ui_z_offset_indicator_set_value(indicator, 0);
        }
    }
}

void ZOffsetCalibrationPanel::on_deactivate() {
    spdlog::debug("[ZOffsetCal] on_deactivate()");

    // If calibration is in progress, abort it — but NOT during app shutdown
    // (shutdown calls on_deactivate on all overlays; we don't want to cancel
    // an in-progress calibration just because the UI is restarting)
    if (state_ == State::ADJUSTING || state_ == State::PROBING) {
        if (!NavigationManager::instance().is_shutting_down()) {
            spdlog::info("[ZOffsetCal] Aborting calibration on deactivate");
            send_abort(); // send_abort() calls turn_off_bed_if_needed()
        } else {
            spdlog::info("[ZOffsetCal] Skipping abort during app shutdown");
        }
    } else {
        // Safety net: turn off bed if navigating away after completion/error
        turn_off_bed_if_needed();
    }

    // Call base class
    OverlayBase::on_deactivate();
}

void ZOffsetCalibrationPanel::cleanup() {
    spdlog::debug("[ZOffsetCal] Cleaning up");

    // Cancel any pending operation timeout
    operation_guard_.end();

    // Reset ObserverGuards to remove observers before cleanup (applying [L020])
    manual_probe_active_observer_.reset();
    manual_probe_z_observer_.reset();
    bed_temp_observer_.reset();

    // Unregister from NavigationManager before cleaning up
    if (overlay_root_) {
        NavigationManager::instance().unregister_overlay_instance(overlay_root_);
    }

    // Call base class to set cleanup_called_ flag
    OverlayBase::cleanup();

    // Clear references
    parent_screen_ = nullptr;
    saved_z_offset_display_ = nullptr;
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

    // Manage operation timeout guard based on state transitions
    switch (new_state) {
    case State::WARMING:
        operation_guard_.begin(WARMING_TIMEOUT_MS, [this] {
            turn_off_bed_if_needed();
            set_state(State::ERROR);
            NOTIFY_WARNING("Bed warming timed out");
        });
        break;
    case State::PROBING:
        operation_guard_.begin(PROBING_TIMEOUT_MS, [this] {
            set_state(State::ERROR);
            NOTIFY_WARNING("Z-offset calibration timed out");
        });
        break;
    case State::SAVING:
        operation_guard_.begin(SAVING_TIMEOUT_MS, [this] {
            set_state(State::ERROR);
            NOTIFY_WARNING("Z-offset calibration timed out");
        });
        break;
    case State::ADJUSTING:
    case State::COMPLETE:
    case State::ERROR:
    case State::IDLE:
        operation_guard_.end();
        bed_temp_observer_.reset(); // Stop watching bed temp when not warming
        break;
    }

    // Update subject - XML bindings handle visibility automatically
    lv_subject_set_int(&s_zoffset_cal_state, static_cast<int>(new_state));
}

// ============================================================================
// GCODE COMMANDS (strategy-aware dispatch)
// ============================================================================

void ZOffsetCalibrationPanel::start_calibration() {
    if (!api_) {
        spdlog::error("[ZOffsetCal] No MoonrakerAPI");
        on_calibration_result(false, "No printer connection");
        return;
    }

    // Check if warm bed is requested
    bool warm_bed = lv_subject_get_int(&s_zoffset_warm_bed) == 1;
    if (warm_bed) {
        auto* cfg = Config::get_instance();
        int temp = cfg->get<int>("/calibration/warm_bed_temp", DEFAULT_WARM_BED_TEMP);
        warm_bed_target_centi_ = temp * 10; // Convert °C to centidegrees

        // Send non-blocking M140 to start heating
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "M140 S%d", temp);
        spdlog::info("[ZOffsetCal] Warming bed to {}°C for calibration", temp);
        bed_was_warmed_ = true;

        api_->execute_gcode(
            cmd, []() { spdlog::debug("[ZOffsetCal] M140 sent, bed heating"); },
            [this](const MoonrakerError& err) {
                spdlog::error("[ZOffsetCal] Failed to start bed heating: {}", err.message);
                helix::ui::async_call(
                    [](void* ud) {
                        static_cast<ZOffsetCalibrationPanel*>(ud)->on_calibration_result(
                            false, "Failed to start bed heating");
                    },
                    this);
            });

        // Enter WARMING state and observe bed temperature
        set_state(State::WARMING);

        PrinterState& ps = get_printer_state();
        bed_temp_observer_ = observe_int_sync<ZOffsetCalibrationPanel>(
            ps.get_bed_temp_subject(), this, [](ZOffsetCalibrationPanel* self, int temp_centi) {
                if (self->state_ != State::WARMING)
                    return;

                spdlog::trace("[ZOffsetCal] Bed temp: {}.{}°C (target: {}°C)", temp_centi / 10,
                              temp_centi % 10, self->warm_bed_target_centi_ / 10);

                if (temp_centi >= self->warm_bed_target_centi_) {
                    spdlog::info("[ZOffsetCal] Bed reached target temperature, proceeding");
                    self->bed_temp_observer_.reset();
                    self->begin_probe_sequence();
                }
            });
    } else {
        // No warming, go straight to probing
        begin_probe_sequence();
    }
}

void ZOffsetCalibrationPanel::begin_probe_sequence() {
    set_state(State::PROBING);

    PrinterState& ps = get_printer_state();
    auto strategy = ps.get_z_offset_calibration_strategy();

    // Check homing state (shared across all strategies)
    const char* homed = lv_subject_get_string(ps.get_homed_axes_subject());
    bool all_homed = homed && std::string(homed).find("xyz") != std::string::npos;

    if (strategy == ZOffsetCalibrationStrategy::GCODE_OFFSET) {
        // Manual Z calibrate: home, move to center, lower to Z0.1
        cumulative_z_delta_ = 0.0f;

        // Use hardcoded center (110, 110) as safe default for most printers
        float center_x = 110.0f;
        float center_y = 110.0f;

        std::string gcode;
        if (!all_homed) {
            gcode = "G28\n";
        }

        // Optional nozzle clean before calibration
        auto& clean_slot = StandardMacros::instance().get(StandardMacroSlot::CleanNozzle);
        if (!clean_slot.is_empty()) {
            gcode += clean_slot.get_macro() + "\n";
            spdlog::info("[ZOffsetCal] Adding nozzle clean: {}", clean_slot.get_macro());
        }

        char move_cmd[128];
        snprintf(move_cmd, sizeof(move_cmd), "G1 X%.1f Y%.1f Z5 F3000\nG1 Z0.1 F300", center_x,
                 center_y);
        gcode += move_cmd;

        spdlog::info("[ZOffsetCal] Starting gcode_offset calibration (center={:.1f},{:.1f})",
                     center_x, center_y);

        api_->execute_gcode(
            gcode,
            [this]() {
                spdlog::info("[ZOffsetCal] Moved to center at Z0.1, ready for adjustment");
                helix::ui::async_call(
                    [](void* ud) {
                        auto* self = static_cast<ZOffsetCalibrationPanel*>(ud);
                        self->set_state(State::ADJUSTING);
                        self->update_z_position(0.1f);
                    },
                    this);
            },
            [this](const MoonrakerError& err) {
                spdlog::error("[ZOffsetCal] Failed to move to position: {}", err.message);
                helix::ui::async_call(
                    [](void* ud) {
                        static_cast<ZOffsetCalibrationPanel*>(ud)->on_calibration_result(
                            false, "Failed to move to calibration position");
                    },
                    this);
            });
    } else {
        // Probe calibrate or endstop strategy
        std::string gcode;
        if (!all_homed) {
            spdlog::info("[ZOffsetCal] Axes not homed (homed_axes='{}'), homing first",
                         homed ? homed : "");
            gcode = "G28\n";
        }

        // Optional nozzle clean before calibration
        auto& clean_slot = StandardMacros::instance().get(StandardMacroSlot::CleanNozzle);
        if (!clean_slot.is_empty()) {
            gcode += clean_slot.get_macro() + "\n";
            spdlog::info("[ZOffsetCal] Adding nozzle clean: {}", clean_slot.get_macro());
        }

        const char* calibrate_cmd = (strategy == ZOffsetCalibrationStrategy::ENDSTOP)
                                        ? "Z_ENDSTOP_CALIBRATE"
                                        : "PROBE_CALIBRATE";
        gcode += calibrate_cmd;

        spdlog::info("[ZOffsetCal] Starting {} (strategy={})", calibrate_cmd,
                     strategy == ZOffsetCalibrationStrategy::ENDSTOP ? "endstop"
                                                                     : "probe_calibrate");

        api_->execute_gcode(
            gcode,
            [calibrate_cmd]() {
                spdlog::info("[ZOffsetCal] {} sent, waiting for manual_probe", calibrate_cmd);
                // State transition to ADJUSTING happens via manual_probe_active observer
            },
            [this](const MoonrakerError& err) {
                spdlog::error("[ZOffsetCal] Failed to start calibration: {}", err.message);
                helix::ui::async_call(
                    [](void* ud) {
                        static_cast<ZOffsetCalibrationPanel*>(ud)->on_calibration_result(
                            false, "Failed to start calibration");
                    },
                    this);
            });
    }
}

void ZOffsetCalibrationPanel::adjust_z(float delta) {
    if (!api_)
        return;

    auto strategy = get_printer_state().get_z_offset_calibration_strategy();

    if (strategy == ZOffsetCalibrationStrategy::GCODE_OFFSET) {
        // Direct G1 move using relative positioning
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "G91\nG1 Z%.3f F300\nG90", delta);

        api_->execute_gcode(
            cmd,
            [this, delta]() {
                struct Ctx {
                    ZOffsetCalibrationPanel* panel;
                    float delta;
                };
                auto ctx = std::make_unique<Ctx>(Ctx{this, delta});
                helix::ui::queue_update<Ctx>(std::move(ctx), [](Ctx* c) {
                    c->panel->cumulative_z_delta_ += c->delta;
                    c->panel->update_z_position(0.1f + c->panel->cumulative_z_delta_);
                    spdlog::debug("[ZOffsetCal] G1 Z adjust: delta={:.3f}, cumulative={:.3f}",
                                  c->delta, c->panel->cumulative_z_delta_);
                });
            },
            [](const MoonrakerError& err) {
                spdlog::warn("[ZOffsetCal] Z adjust failed: {}", err.message);
            });
    } else {
        // TESTZ for probe_calibrate/endstop strategies
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "TESTZ Z=%.3f", delta);
        spdlog::debug("[ZOffsetCal] Sending: {}", cmd);

        api_->execute_gcode(
            cmd, []() { spdlog::debug("[ZOffsetCal] TESTZ sent"); },
            [](const MoonrakerError& err) {
                spdlog::warn("[ZOffsetCal] TESTZ failed: {}", err.message);
            });
        // Z position display is updated by the manual_probe_z_position observer
    }
}

void ZOffsetCalibrationPanel::send_accept() {
    if (!api_)
        return;

    auto strategy = get_printer_state().get_z_offset_calibration_strategy();
    final_offset_ = current_z_;
    on_calibration_result(true, "");

    if (strategy == ZOffsetCalibrationStrategy::GCODE_OFFSET) {
        // Apply cumulative delta as gcode Z offset
        set_state(State::SAVING);
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "SET_GCODE_OFFSET Z=%.3f", cumulative_z_delta_);
        spdlog::info("[ZOffsetCal] Applying gcode_offset: {}", cmd);

        api_->execute_gcode(
            cmd,
            [this]() {
                spdlog::info("[ZOffsetCal] SET_GCODE_OFFSET applied successfully");
                helix::ui::async_call(
                    [](void* ud) {
                        static_cast<ZOffsetCalibrationPanel*>(ud)->on_calibration_result(true, "");
                    },
                    this);
            },
            [this](const MoonrakerError& err) {
                spdlog::error("[ZOffsetCal] SET_GCODE_OFFSET failed: {}", err.message);
                helix::ui::async_call(
                    [](void* ud) {
                        static_cast<ZOffsetCalibrationPanel*>(ud)->on_calibration_result(
                            false, "Failed to set Z-offset");
                    },
                    this);
            });
    } else {
        // Probe/endstop: ACCEPT then SAVE_CONFIG
        spdlog::info("[ZOffsetCal] Sending ACCEPT");
        set_state(State::SAVING);

        api_->execute_gcode(
            "ACCEPT",
            [this, strategy]() {
                if (strategy == ZOffsetCalibrationStrategy::ENDSTOP) {
                    // Endstop needs Z_OFFSET_APPLY_ENDSTOP before SAVE_CONFIG
                    api_->execute_gcode(
                        "Z_OFFSET_APPLY_ENDSTOP",
                        [this]() {
                            spdlog::info(
                                "[ZOffsetCal] Z_OFFSET_APPLY_ENDSTOP success, saving config");
                            api_->execute_gcode(
                                "SAVE_CONFIG",
                                [this]() {
                                    helix::ui::async_call(
                                        [](void* ud) {
                                            static_cast<ZOffsetCalibrationPanel*>(ud)
                                                ->on_calibration_result(true, "");
                                        },
                                        this);
                                },
                                [this](const MoonrakerError& err) {
                                    struct Ctx {
                                        ZOffsetCalibrationPanel* panel;
                                        std::string msg;
                                    };
                                    auto ctx = std::make_unique<Ctx>(
                                        Ctx{this, "SAVE_CONFIG failed: " + err.user_message()});
                                    helix::ui::queue_update<Ctx>(std::move(ctx), [](Ctx* c) {
                                        c->panel->on_calibration_result(false, c->msg);
                                    });
                                });
                        },
                        [this](const MoonrakerError& err) {
                            struct Ctx {
                                ZOffsetCalibrationPanel* panel;
                                std::string msg;
                            };
                            auto ctx = std::make_unique<Ctx>(
                                Ctx{this, "Z_OFFSET_APPLY_ENDSTOP failed: " + err.user_message()});
                            helix::ui::queue_update<Ctx>(std::move(ctx), [](Ctx* c) {
                                c->panel->on_calibration_result(false, c->msg);
                            });
                        });
                } else {
                    // Probe calibrate: just SAVE_CONFIG
                    spdlog::info("[ZOffsetCal] Sending SAVE_CONFIG");
                    api_->execute_gcode(
                        "SAVE_CONFIG",
                        [this]() {
                            helix::ui::async_call(
                                [](void* ud) {
                                    static_cast<ZOffsetCalibrationPanel*>(ud)
                                        ->on_calibration_result(true, "");
                                },
                                this);
                        },
                        [this](const MoonrakerError& err) {
                            struct Ctx {
                                ZOffsetCalibrationPanel* panel;
                                std::string msg;
                            };
                            auto ctx = std::make_unique<Ctx>(
                                Ctx{this, "SAVE_CONFIG failed: " + err.user_message()});
                            helix::ui::queue_update<Ctx>(std::move(ctx), [](Ctx* c) {
                                c->panel->on_calibration_result(false, c->msg);
                            });
                        });
                }
            },
            [this](const MoonrakerError& err) {
                struct Ctx {
                    ZOffsetCalibrationPanel* panel;
                    std::string msg;
                };
                auto ctx = std::make_unique<Ctx>(Ctx{this, "ACCEPT failed: " + err.user_message()});
                helix::ui::queue_update<Ctx>(
                    std::move(ctx), [](Ctx* c) { c->panel->on_calibration_result(false, c->msg); });
            });
    }
}

void ZOffsetCalibrationPanel::send_abort() {
    if (!api_)
        return;

    // If warming, just cancel heating — no calibration gcode was sent yet
    if (state_ == State::WARMING) {
        spdlog::info("[ZOffsetCal] Aborting during bed warming");
        turn_off_bed_if_needed();
        set_state(State::IDLE);
        return;
    }

    auto strategy = get_printer_state().get_z_offset_calibration_strategy();

    if (strategy == ZOffsetCalibrationStrategy::GCODE_OFFSET) {
        // Retract nozzle without applying any offset
        spdlog::info("[ZOffsetCal] Aborting gcode_offset mode, retracting");
        api_->execute_gcode(
            "G90\nG1 Z5 F1000", []() { spdlog::info("[ZOffsetCal] Retracted after abort"); },
            [](const MoonrakerError& err) {
                spdlog::warn("[ZOffsetCal] Retract failed: {}", err.message);
            });
    } else {
        spdlog::info("[ZOffsetCal] Sending ABORT");
        api_->execute_gcode(
            "ABORT", []() { spdlog::info("[ZOffsetCal] Aborted"); },
            [](const MoonrakerError& err) {
                spdlog::warn("[ZOffsetCal] ABORT failed: {}", err.message);
            });
    }

    turn_off_bed_if_needed();
    set_state(State::IDLE);
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

void ZOffsetCalibrationPanel::handle_start_clicked() {
    spdlog::debug("[ZOffsetCal] Start clicked");
    start_calibration(); // Enters WARMING or PROBING depending on warm bed toggle
}

void ZOffsetCalibrationPanel::handle_z_adjust(float delta) {
    if (state_ != State::ADJUSTING)
        return;
    adjust_z(delta);

    // Flash the direction indicator
    if (overlay_root_) {
        lv_obj_t* indicator = lv_obj_find_by_name(overlay_root_, "z_offset_indicator");
        if (indicator) {
            ui_z_offset_indicator_flash_direction(indicator, delta > 0 ? 1 : -1);
        }
    }
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
    NavigationManager::instance().go_back();
}

void ZOffsetCalibrationPanel::handle_retry_clicked() {
    spdlog::debug("[ZOffsetCal] Retry clicked");
    set_state(State::IDLE);
}

void ZOffsetCalibrationPanel::handle_warm_bed_toggled() {
    int current = lv_subject_get_int(&s_zoffset_warm_bed);
    int toggled = current ? 0 : 1;
    lv_subject_set_int(&s_zoffset_warm_bed, toggled);
    spdlog::info("[ZOffsetCal] Warm bed toggled: {}", toggled ? "on" : "off");
}

void ZOffsetCalibrationPanel::turn_off_bed_if_needed() {
    if (!bed_was_warmed_ || !api_)
        return;

    bed_was_warmed_ = false;
    spdlog::info("[ZOffsetCal] Turning off bed heater after calibration");
    api_->execute_gcode(
        "M140 S0", []() { spdlog::debug("[ZOffsetCal] Bed heater off"); },
        [](const MoonrakerError& err) {
            spdlog::warn("[ZOffsetCal] Failed to turn off bed: {}", err.message);
        });
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

    // Update the visual indicator (convert mm to microns)
    if (overlay_root_) {
        lv_obj_t* indicator = lv_obj_find_by_name(overlay_root_, "z_offset_indicator");
        if (indicator) {
            int microns = static_cast<int>(z_position * 1000.0f);
            ui_z_offset_indicator_set_value(indicator, microns);
        }
    }
}

void ZOffsetCalibrationPanel::on_calibration_result(bool success, const std::string& message) {
    if (success) {
        // Update final offset display
        if (final_offset_label_) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Accepted Z Position: %.3f", final_offset_);
            lv_label_set_text(final_offset_label_, buf);
        }
        turn_off_bed_if_needed();
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

void ZOffsetCalibrationPanel::on_z_adjust(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ZOffsetCal] on_z_adjust");
    const char* delta_str = static_cast<const char*>(lv_event_get_user_data(e));
    if (delta_str) {
        float delta = strtof(delta_str, nullptr);
        spdlog::debug("[ZOffsetCal] Z adjust: {} (from user_data \"{}\")", delta, delta_str);
        get_global_zoffset_cal_panel().handle_z_adjust(delta);
    }
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

void ZOffsetCalibrationPanel::on_warm_bed_toggled(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ZOffsetCal] on_warm_bed_toggled");
    get_global_zoffset_cal_panel().handle_warm_bed_toggled();
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// GLOBAL INSTANCE AND ROW CLICK HANDLER
// ============================================================================

static std::unique_ptr<ZOffsetCalibrationPanel> g_zoffset_cal_panel;

// Forward declarations
static void on_zoffset_row_clicked(lv_event_t* e);

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
    spdlog::trace("[ZOffsetCal] Row click callback registered");
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
        overlay.set_api(get_moonraker_api());
        overlay.create(lv_display_get_screen_active(nullptr));
    }

    overlay.show();
}
