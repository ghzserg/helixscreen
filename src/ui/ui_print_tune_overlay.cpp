// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_print_tune_overlay.h"

#include "ui_error_reporting.h"
#include "ui_panel_common.h"
#include "ui_toast.h"

#include "moonraker_api.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>

// ============================================================================
// GLOBAL INSTANCE ACCESSOR
// ============================================================================

static PrintTuneOverlay* g_print_tune_overlay = nullptr;

PrintTuneOverlay& get_global_print_tune_overlay() {
    if (!g_print_tune_overlay) {
        spdlog::error("[PrintTuneOverlay] Global instance not set!");
        // This will crash, but it's a programming error that should never happen
        static PrintTuneOverlay fallback;
        return fallback;
    }
    return *g_print_tune_overlay;
}

void set_global_print_tune_overlay(PrintTuneOverlay* overlay) {
    g_print_tune_overlay = overlay;
}

// ============================================================================
// XML EVENT CALLBACKS (free functions using global accessor)
// ============================================================================

static void on_tune_speed_changed_cb(lv_event_t* e) {
    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (slider) {
        int value = lv_slider_get_value(slider);
        get_global_print_tune_overlay().handle_speed_changed(value);
    }
}

static void on_tune_flow_changed_cb(lv_event_t* e) {
    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (slider) {
        int value = lv_slider_get_value(slider);
        get_global_print_tune_overlay().handle_flow_changed(value);
    }
}

static void on_tune_reset_clicked_cb(lv_event_t* /*e*/) {
    get_global_print_tune_overlay().handle_reset();
}

/**
 * @brief Single callback for all Z-offset buttons
 *
 * Parses button name to determine direction and magnitude:
 *   - btn_z_closer_01  -> -0.1mm (closer = negative = more squish)
 *   - btn_z_closer_005 -> -0.05mm
 *   - btn_z_closer_001 -> -0.01mm
 *   - btn_z_farther_001 -> +0.01mm (farther = positive = less squish)
 *   - btn_z_farther_005 -> +0.05mm
 *   - btn_z_farther_01 -> +0.1mm
 */
static void on_tune_z_offset_cb(lv_event_t* e) {
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!btn) {
        return;
    }

    // Get button name to determine delta
    const char* name = lv_obj_get_name(btn);
    if (!name) {
        spdlog::warn("[on_tune_z_offset_cb] Button has no name");
        return;
    }

    // Parse direction: "closer" = negative, "farther" = positive
    double delta = 0.0;
    bool is_closer = (strstr(name, "closer") != nullptr);
    bool is_farther = (strstr(name, "farther") != nullptr);

    if (!is_closer && !is_farther) {
        spdlog::warn("[on_tune_z_offset_cb] Unknown button name: {}", name);
        return;
    }

    // Parse magnitude from suffix: "_01" = 0.1, "_005" = 0.05, "_001" = 0.01
    if (strstr(name, "_01") && !strstr(name, "_001")) {
        delta = 0.1;
    } else if (strstr(name, "_005")) {
        delta = 0.05;
    } else if (strstr(name, "_001")) {
        delta = 0.01;
    } else {
        spdlog::warn("[on_tune_z_offset_cb] Unknown delta in button name: {}", name);
        return;
    }

    // Apply direction: closer = more squish = negative Z adjust
    if (is_closer) {
        delta = -delta;
    }

    spdlog::trace("[on_tune_z_offset_cb] Button '{}' -> delta {:+.3f}mm", name, delta);
    get_global_print_tune_overlay().handle_z_offset_changed(delta);
}

static void on_tune_save_z_offset_cb(lv_event_t* /*e*/) {
    get_global_print_tune_overlay().handle_save_z_offset();
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

PrintTuneOverlay::PrintTuneOverlay() {
    spdlog::debug("[PrintTuneOverlay] Created");
}

PrintTuneOverlay::~PrintTuneOverlay() {
    deinit_subjects();

    // Clear global accessor if we're the current instance
    if (g_print_tune_overlay == this) {
        g_print_tune_overlay = nullptr;
    }

    spdlog::debug("[PrintTuneOverlay] Destroyed");
}

// ============================================================================
// SUBJECT MANAGEMENT
// ============================================================================

void PrintTuneOverlay::init_subjects(PrinterState& printer_state) {
    if (subjects_initialized_) {
        spdlog::warn("[PrintTuneOverlay] init_subjects() called twice - ignoring");
        return;
    }

    printer_state_ = &printer_state;

    // Initialize tune panel subjects
    UI_MANAGED_SUBJECT_STRING(tune_speed_subject_, tune_speed_buf_, "100%", "tune_speed_display",
                              subjects_);
    UI_MANAGED_SUBJECT_STRING(tune_flow_subject_, tune_flow_buf_, "100%", "tune_flow_display",
                              subjects_);
    UI_MANAGED_SUBJECT_STRING(tune_z_offset_subject_, tune_z_offset_buf_, "0.000mm",
                              "tune_z_offset_display", subjects_);

    // Register XML event callbacks
    lv_xml_register_event_cb(nullptr, "on_tune_speed_changed", on_tune_speed_changed_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_flow_changed", on_tune_flow_changed_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_reset_clicked", on_tune_reset_clicked_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_z_offset", on_tune_z_offset_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_save_z_offset", on_tune_save_z_offset_cb);

    subjects_initialized_ = true;
    spdlog::debug("[PrintTuneOverlay] Subjects initialized (3 subjects, 5 callbacks)");
}

void PrintTuneOverlay::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    subjects_.deinit_all();
    subjects_initialized_ = false;

    spdlog::debug("[PrintTuneOverlay] Subjects deinitialized");
}

// ============================================================================
// SETUP
// ============================================================================

void PrintTuneOverlay::setup(lv_obj_t* panel, lv_obj_t* parent_screen, MoonrakerAPI* api,
                             PrinterState& printer_state) {
    tune_panel_ = panel;
    api_ = api;
    printer_state_ = &printer_state;

    // Use standard overlay panel setup for back button handling
    ui_overlay_panel_setup_standard(panel, parent_screen, "overlay_header", "overlay_content");

    // Update Z-offset icons based on printer kinematics
    update_z_offset_icons(panel);

    spdlog::debug("[PrintTuneOverlay] Setup complete (events wired via XML)");
}

// ============================================================================
// ICON UPDATES
// ============================================================================

void PrintTuneOverlay::update_z_offset_icons(lv_obj_t* panel) {
    if (!printer_state_) {
        spdlog::warn("[PrintTuneOverlay] Cannot update icons - no printer_state_");
        return;
    }

    // Get kinematics type from PrinterState
    // 0 = unknown, 1 = bed moves Z (CoreXY), 2 = head moves Z (Cartesian/Delta)
    int kin = lv_subject_get_int(printer_state_->get_printer_bed_moves_subject());
    bool bed_moves_z = (kin == 1);

    // Select icon codepoints based on kinematics
    // CoreXY (bed moves): expand icons show bed motion
    // Cartesian/Delta (head moves): arrow icons show head motion
    const char* closer_icon =
        bed_moves_z ? "\xF3\xB0\x9E\x93" : "\xF3\xB0\x81\x85"; // arrow-expand-down : arrow-down
    const char* farther_icon =
        bed_moves_z ? "\xF3\xB0\x9E\x96" : "\xF3\xB0\x81\x9D"; // arrow-expand-up : arrow-up

    // Find and update all closer icons (3 buttons)
    const char* closer_names[] = {"icon_z_closer_01", "icon_z_closer_005", "icon_z_closer_001"};
    for (const char* name : closer_names) {
        lv_obj_t* icon = lv_obj_find_by_name(panel, name);
        if (icon) {
            lv_label_set_text(icon, closer_icon);
        }
    }

    // Find and update all farther icons (3 buttons)
    const char* farther_names[] = {"icon_z_farther_001", "icon_z_farther_005", "icon_z_farther_01"};
    for (const char* name : farther_names) {
        lv_obj_t* icon = lv_obj_find_by_name(panel, name);
        if (icon) {
            lv_label_set_text(icon, farther_icon);
        }
    }

    spdlog::debug("[PrintTuneOverlay] Z-offset icons set for {} kinematics",
                  bed_moves_z ? "bed-moves-Z" : "head-moves-Z");
}

// ============================================================================
// DISPLAY UPDATES
// ============================================================================

void PrintTuneOverlay::update_display() {
    std::snprintf(tune_speed_buf_, sizeof(tune_speed_buf_), "%d%%", speed_percent_);
    lv_subject_copy_string(&tune_speed_subject_, tune_speed_buf_);

    std::snprintf(tune_flow_buf_, sizeof(tune_flow_buf_), "%d%%", flow_percent_);
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
        std::snprintf(tune_z_offset_buf_, sizeof(tune_z_offset_buf_), "%.3fmm", current_z_offset_);
        lv_subject_copy_string(&tune_z_offset_subject_, tune_z_offset_buf_);
    }

    spdlog::trace("[PrintTuneOverlay] Z-offset display updated: {}um ({}mm)", microns,
                  current_z_offset_);
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

void PrintTuneOverlay::handle_speed_changed(int value) {
    speed_percent_ = value;

    // Update display immediately for responsive feel
    std::snprintf(tune_speed_buf_, sizeof(tune_speed_buf_), "%d%%", value);
    lv_subject_copy_string(&tune_speed_subject_, tune_speed_buf_);

    // Send G-code command
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

void PrintTuneOverlay::handle_flow_changed(int value) {
    flow_percent_ = value;

    // Update display immediately for responsive feel
    std::snprintf(tune_flow_buf_, sizeof(tune_flow_buf_), "%d%%", value);
    lv_subject_copy_string(&tune_flow_subject_, tune_flow_buf_);

    // Send G-code command
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
    std::snprintf(tune_z_offset_buf_, sizeof(tune_z_offset_buf_), "%.3fmm", current_z_offset_);
    lv_subject_copy_string(&tune_z_offset_subject_, tune_z_offset_buf_);

    // Track pending delta for "unsaved adjustment" notification in Controls panel
    if (printer_state_) {
        int delta_microns = static_cast<int>(delta * 1000.0);
        printer_state_->add_pending_z_offset_delta(delta_microns);
    }

    spdlog::debug("[PrintTuneOverlay] Z-offset adjust: {:+.3f}mm (total: {:.3f}mm)", delta,
                  current_z_offset_);

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

void PrintTuneOverlay::handle_save_z_offset() {
    // Show warning modal - SAVE_CONFIG restarts Klipper and cancels active prints!
    save_z_offset_modal_.set_on_confirm([this]() {
        if (api_) {
            api_->execute_gcode(
                "SAVE_CONFIG",
                []() {
                    spdlog::info("[PrintTuneOverlay] Z-offset saved - Klipper restarting");
                    ui_toast_show(ToastSeverity::WARNING, "Z-offset saved - Klipper restarting...",
                                  5000);
                },
                [](const MoonrakerError& err) {
                    spdlog::error("[PrintTuneOverlay] SAVE_CONFIG failed: {}", err.message);
                    NOTIFY_ERROR("Save failed: {}", err.user_message());
                });
        }
    });
    save_z_offset_modal_.show(lv_screen_active());
}
