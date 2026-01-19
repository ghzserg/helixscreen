// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_controls.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_fan_control_overlay.h"
#include "ui_fonts.h"
#include "ui_icon_codepoints.h"
#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_notification.h"
#include "ui_panel_bed_mesh.h"
#include "ui_panel_calibration_zoffset.h"
#include "ui_panel_extrusion.h"
#include "ui_panel_motion.h"
#include "ui_panel_screws_tilt.h"
#include "ui_panel_temp_control.h"
#include "ui_position_utils.h"
#include "ui_subject_registry.h"
#include "ui_temperature_utils.h"
#include "ui_theme.h"

#include "app_globals.h"
#include "moonraker_api.h"
#include "observer_factory.h"
#include "printer_state.h"
#include "standard_macros.h"
#include "static_panel_registry.h"
#include "subject_managed_panel.h"

#include <spdlog/spdlog.h>

#include <algorithm> // std::clamp
#include <cstdio>
#include <cstring>
#include <memory>

using helix::ui::observe_int_sync;
using helix::ui::observe_string;

// Forward declarations for class-based API
class MotionPanel;
MotionPanel& get_global_motion_panel();
class ExtrusionPanel;
ExtrusionPanel& get_global_extrusion_panel();

using helix::ui::position::format_position;
using helix::ui::temperature::centi_to_degrees_f;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

ControlsPanel::ControlsPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // Dependencies passed for interface consistency
    // Child panels (motion, temp, extrusion) may use these when wired
}

ControlsPanel::~ControlsPanel() {
    deinit_subjects();

    // CRITICAL: Check if LVGL is still initialized before calling LVGL functions.
    // During static destruction, LVGL may already be torn down.
    if (!lv_is_initialized()) {
        return;
    }

    // Clean up lazily-created overlay panels to prevent dangling LVGL objects
    if (motion_panel_) {
        lv_obj_del(motion_panel_);
        motion_panel_ = nullptr;
    }
    if (nozzle_temp_panel_) {
        lv_obj_del(nozzle_temp_panel_);
        nozzle_temp_panel_ = nullptr;
    }
    if (bed_temp_panel_) {
        lv_obj_del(bed_temp_panel_);
        bed_temp_panel_ = nullptr;
    }
    if (fan_control_panel_) {
        lv_obj_del(fan_control_panel_);
        fan_control_panel_ = nullptr;
    }
    if (bed_mesh_panel_) {
        lv_obj_del(bed_mesh_panel_);
        bed_mesh_panel_ = nullptr;
    }
    if (zoffset_panel_) {
        lv_obj_del(zoffset_panel_);
        zoffset_panel_ = nullptr;
    }
    if (screws_panel_) {
        lv_obj_del(screws_panel_);
        screws_panel_ = nullptr;
    }
    // Modal dialogs: use ui_modal_hide() - NOT lv_obj_del()!
    // See docs/DEVELOPER_QUICK_REFERENCE.md "Modal Dialog Lifecycle"
    if (motors_confirmation_dialog_) {
        ui_modal_hide(motors_confirmation_dialog_);
        motors_confirmation_dialog_ = nullptr;
    }
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

    // Initialize dashboard display subjects for card live data
    // Using UI_MANAGED_SUBJECT_* macros for automatic RAII cleanup via SubjectManager

    // Nozzle temperature display
    UI_MANAGED_SUBJECT_STRING(nozzle_temp_subject_, nozzle_temp_buf_, "—°C", "controls_nozzle_temp",
                              subjects_);
    UI_MANAGED_SUBJECT_INT(nozzle_pct_subject_, 0, "controls_nozzle_pct", subjects_);
    UI_MANAGED_SUBJECT_STRING(nozzle_status_subject_, nozzle_status_buf_, "Off",
                              "controls_nozzle_status", subjects_);

    // Bed temperature display
    UI_MANAGED_SUBJECT_STRING(bed_temp_subject_, bed_temp_buf_, "—°C", "controls_bed_temp",
                              subjects_);
    UI_MANAGED_SUBJECT_INT(bed_pct_subject_, 0, "controls_bed_pct", subjects_);
    UI_MANAGED_SUBJECT_STRING(bed_status_subject_, bed_status_buf_, "Off", "controls_bed_status",
                              subjects_);

    // Fan speed display
    UI_MANAGED_SUBJECT_STRING(fan_speed_subject_, fan_speed_buf_, "Off", "controls_fan_speed",
                              subjects_);
    UI_MANAGED_SUBJECT_INT(fan_pct_subject_, 0, "controls_fan_pct", subjects_);

    // Macro button visibility and names (for declarative binding)
    UI_MANAGED_SUBJECT_INT(macro_1_visible_, 0, "macro_1_visible", subjects_);
    UI_MANAGED_SUBJECT_INT(macro_2_visible_, 0, "macro_2_visible", subjects_);
    UI_MANAGED_SUBJECT_STRING(macro_1_name_, macro_1_name_buf_, "", "macro_1_name", subjects_);
    UI_MANAGED_SUBJECT_STRING(macro_2_name_, macro_2_name_buf_, "", "macro_2_name", subjects_);

    // Z-Offset delta display (for banner showing unsaved adjustment)
    UI_MANAGED_SUBJECT_STRING(z_offset_delta_display_subject_, z_offset_delta_display_buf_, "",
                              "z_offset_delta_display", subjects_);

    // Homing status subjects for bind_style visual feedback
    UI_MANAGED_SUBJECT_INT(x_homed_, 0, "x_homed", subjects_);
    UI_MANAGED_SUBJECT_INT(y_homed_, 0, "y_homed", subjects_);
    UI_MANAGED_SUBJECT_INT(xy_homed_, 0, "xy_homed", subjects_);
    UI_MANAGED_SUBJECT_INT(z_homed_, 0, "z_homed", subjects_);
    UI_MANAGED_SUBJECT_INT(all_homed_, 0, "all_homed", subjects_);

    // Position display subjects for Position card
    // Format: numeric value only (axis label is static in XML for proper alignment)
    std::strcpy(controls_pos_x_buf_, "   —   mm");
    std::strcpy(controls_pos_y_buf_, "   —   mm");
    std::strcpy(controls_pos_z_buf_, "   —   mm");
    UI_MANAGED_SUBJECT_STRING(controls_pos_x_subject_, controls_pos_x_buf_, "   —   mm",
                              "controls_pos_x", subjects_);
    UI_MANAGED_SUBJECT_STRING(controls_pos_y_subject_, controls_pos_y_buf_, "   —   mm",
                              "controls_pos_y", subjects_);
    UI_MANAGED_SUBJECT_STRING(controls_pos_z_subject_, controls_pos_z_buf_, "   —   mm",
                              "controls_pos_z", subjects_);

    // Speed/Flow override display subjects
    std::strcpy(speed_override_buf_, "100%");
    std::strcpy(flow_override_buf_, "100%");
    UI_MANAGED_SUBJECT_STRING(speed_override_subject_, speed_override_buf_, "100%",
                              "controls_speed_pct", subjects_);
    UI_MANAGED_SUBJECT_STRING(flow_override_subject_, flow_override_buf_, "100%",
                              "controls_flow_pct", subjects_);

    // Macro buttons 3 & 4 visibility and names
    UI_MANAGED_SUBJECT_INT(macro_3_visible_, 0, "macro_3_visible", subjects_);
    UI_MANAGED_SUBJECT_INT(macro_4_visible_, 0, "macro_4_visible", subjects_);
    UI_MANAGED_SUBJECT_STRING(macro_3_name_, macro_3_name_buf_, "", "macro_3_name", subjects_);
    UI_MANAGED_SUBJECT_STRING(macro_4_name_, macro_4_name_buf_, "", "macro_4_name", subjects_);

    // Observe homed_axes from PrinterState to update homing subjects using string observer
    homed_axes_observer_ = observe_string<ControlsPanel>(
        printer_state_.get_homed_axes_subject(), this, [](ControlsPanel* self, const char* axes) {
            bool has_x = strchr(axes, 'x') != nullptr;
            bool has_y = strchr(axes, 'y') != nullptr;
            bool has_z = strchr(axes, 'z') != nullptr;

            int x = has_x ? 1 : 0;
            int y = has_y ? 1 : 0;
            int xy = (has_x && has_y) ? 1 : 0;
            int z = has_z ? 1 : 0;
            int all = (has_x && has_y && has_z) ? 1 : 0;

            // Only update if changed (avoid unnecessary redraws)
            bool changed = false;
            if (lv_subject_get_int(&self->x_homed_) != x) {
                lv_subject_set_int(&self->x_homed_, x);
                changed = true;
            }
            if (lv_subject_get_int(&self->y_homed_) != y) {
                lv_subject_set_int(&self->y_homed_, y);
                changed = true;
            }
            if (lv_subject_get_int(&self->xy_homed_) != xy) {
                lv_subject_set_int(&self->xy_homed_, xy);
                changed = true;
            }
            if (lv_subject_get_int(&self->z_homed_) != z) {
                lv_subject_set_int(&self->z_homed_, z);
                changed = true;
            }
            if (lv_subject_get_int(&self->all_homed_) != all) {
                lv_subject_set_int(&self->all_homed_, all);
                changed = true;
            }

            if (changed) {
                spdlog::info("[ControlsPanel] Homing status changed: x={}, y={}, z={}, all={} "
                             "(axes='{}')",
                             x, y, z, all, axes);
            }
        });

    // Register calibration button event callbacks (direct buttons in card, no modal)
    lv_xml_register_event_cb(nullptr, "on_calibration_bed_mesh", on_calibration_bed_mesh);
    lv_xml_register_event_cb(nullptr, "on_calibration_zoffset", on_calibration_zoffset);
    lv_xml_register_event_cb(nullptr, "on_calibration_screws", on_calibration_screws);
    lv_xml_register_event_cb(nullptr, "on_calibration_motors", on_calibration_motors);

    // Register V2 controls panel event callbacks (XML event_cb references)
    // Quick Actions: Home buttons
    lv_xml_register_event_cb(nullptr, "on_controls_home_all", on_home_all);
    lv_xml_register_event_cb(nullptr, "on_controls_home_xy", on_home_xy);
    lv_xml_register_event_cb(nullptr, "on_controls_home_z", on_home_z);

    // Quick Actions: Macro buttons
    lv_xml_register_event_cb(nullptr, "on_controls_macro_1", on_macro_1);
    lv_xml_register_event_cb(nullptr, "on_controls_macro_2", on_macro_2);
    lv_xml_register_event_cb(nullptr, "on_controls_macro_3", on_macro_3);
    lv_xml_register_event_cb(nullptr, "on_controls_macro_4", on_macro_4);

    // Speed/Flow override buttons
    lv_xml_register_event_cb(nullptr, "on_controls_speed_up", on_speed_up);
    lv_xml_register_event_cb(nullptr, "on_controls_speed_down", on_speed_down);
    lv_xml_register_event_cb(nullptr, "on_controls_flow_up", on_flow_up);
    lv_xml_register_event_cb(nullptr, "on_controls_flow_down", on_flow_down);

    // Cooling: Fan slider
    lv_xml_register_event_cb(nullptr, "on_controls_fan_slider", on_fan_slider_changed);

    // Z-Offset banner: Save button
    lv_xml_register_event_cb(nullptr, "on_controls_save_z_offset", on_save_z_offset);

    // Card click handlers (navigation to full overlay panels)
    lv_xml_register_event_cb(nullptr, "on_controls_quick_actions", on_quick_actions_clicked);
    lv_xml_register_event_cb(nullptr, "on_controls_temperatures", on_temperatures_clicked);
    lv_xml_register_event_cb(nullptr, "on_nozzle_temp_clicked", on_nozzle_temp_clicked);
    lv_xml_register_event_cb(nullptr, "on_bed_temp_clicked", on_bed_temp_clicked);
    lv_xml_register_event_cb(nullptr, "on_controls_cooling", on_cooling_clicked);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Dashboard subjects initialized", get_name());
}

void ControlsPanel::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    // SubjectManager handles deinitialization of all registered subjects (28 total)
    subjects_.deinit_all();

    subjects_initialized_ = false;
    spdlog::debug("[Controls Panel] Subjects deinitialized ({} subjects)", subjects_.count());
}

void ControlsPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    // Load quick button slot assignments from config
    // Config stores slot names like "clean_nozzle", "bed_level"
    if (Config* config = Config::get_instance()) {
        std::string slot1_name =
            config->get<std::string>("/standard_macros/quick_button_1", "clean_nozzle");
        std::string slot2_name =
            config->get<std::string>("/standard_macros/quick_button_2", "bed_level");
        std::string slot3_name = config->get<std::string>("/standard_macros/quick_button_3", "");
        std::string slot4_name = config->get<std::string>("/standard_macros/quick_button_4", "");

        macro_1_slot_ = StandardMacros::slot_from_name(slot1_name);
        macro_2_slot_ = StandardMacros::slot_from_name(slot2_name);
        macro_3_slot_ =
            slot3_name.empty() ? std::nullopt : StandardMacros::slot_from_name(slot3_name);
        macro_4_slot_ =
            slot4_name.empty() ? std::nullopt : StandardMacros::slot_from_name(slot4_name);

        spdlog::debug(
            "[{}] Quick buttons configured: slot1='{}', slot2='{}', slot3='{}', slot4='{}'",
            get_name(), slot1_name, slot2_name, slot3_name, slot4_name);
    } else {
        // Fallback: use CleanNozzle and BedLevel slots for 1 & 2, none for 3 & 4
        macro_1_slot_ = StandardMacroSlot::CleanNozzle;
        macro_2_slot_ = StandardMacroSlot::BedLevel;
        macro_3_slot_ = std::nullopt;
        macro_4_slot_ = std::nullopt;
        spdlog::warn("[{}] Config not available, using default macro slots", get_name());
    }

    // Refresh button labels and visibility based on current StandardMacros state
    refresh_macro_buttons();

    // Cache dynamic container for secondary fans
    secondary_fans_list_ = lv_obj_find_by_name(panel_, "secondary_fans_list");
    if (!secondary_fans_list_) {
        spdlog::warn("[{}] Could not find secondary_fans_list container", get_name());
    } else {
        // Make the secondary fans list clickable to open the fan control overlay
        lv_obj_add_flag(secondary_fans_list_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(secondary_fans_list_, on_secondary_fans_clicked, LV_EVENT_CLICKED,
                            this);
    }

    // Wire up card click handlers (cards need manual wiring for navigation)
    setup_card_handlers();

    // Register observers for live data updates
    register_observers();

    // Populate secondary fans on initial setup (will be empty until discovery)
    populate_secondary_fans();

    spdlog::info("[{}] Setup complete", get_name());
}

void ControlsPanel::on_activate() {
    // Refresh secondary fans list when panel becomes visible
    // This handles edge cases where:
    // 1. Fan discovery completed after initial setup
    // 2. User switched from one printer connection to another
    // 3. Observer callback was missed due to timing
    populate_secondary_fans();

    // Refresh macro buttons in case StandardMacros was initialized after setup()
    // This ensures button labels reflect auto-detected macros, not just fallbacks
    refresh_macro_buttons();

    spdlog::debug("[{}] Panel activated, refreshed fans and macro buttons", get_name());
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void ControlsPanel::setup_card_handlers() {
    // All card click handlers are now wired via XML event_cb - see init_subjects().
    // This function is retained for validation and debugging purposes.

    lv_obj_t* card_quick_actions = lv_obj_find_by_name(panel_, "card_quick_actions");
    lv_obj_t* card_temperatures = lv_obj_find_by_name(panel_, "card_temperatures");
    lv_obj_t* card_cooling = lv_obj_find_by_name(panel_, "card_cooling");
    lv_obj_t* card_calibration = lv_obj_find_by_name(panel_, "card_calibration");

    if (!card_quick_actions || !card_temperatures || !card_cooling || !card_calibration) {
        spdlog::error("[{}] Failed to find all V2 cards", get_name());
        return;
    }

    spdlog::debug("[{}] V2 card navigation handlers validated (wired via XML event_cb)",
                  get_name());
}

void ControlsPanel::register_observers() {
    // Subscribe to temperature updates using observer factory (raw caching pattern)
    extruder_temp_observer_ = observe_int_sync<ControlsPanel>(
        printer_state_.get_extruder_temp_subject(), this, [](ControlsPanel* self, int value) {
            self->cached_extruder_temp_ = value;
            self->update_nozzle_temp_display();
        });

    extruder_target_observer_ = observe_int_sync<ControlsPanel>(
        printer_state_.get_extruder_target_subject(), this, [](ControlsPanel* self, int value) {
            self->cached_extruder_target_ = value;
            self->update_nozzle_temp_display();
        });

    bed_temp_observer_ = observe_int_sync<ControlsPanel>(printer_state_.get_bed_temp_subject(),
                                                         this, [](ControlsPanel* self, int value) {
                                                             self->cached_bed_temp_ = value;
                                                             self->update_bed_temp_display();
                                                         });

    bed_target_observer_ = observe_int_sync<ControlsPanel>(
        printer_state_.get_bed_target_subject(), this, [](ControlsPanel* self, int value) {
            self->cached_bed_target_ = value;
            self->update_bed_temp_display();
        });

    // Subscribe to fan updates
    fan_observer_ = observe_int_sync<ControlsPanel>(
        printer_state_.get_fan_speed_subject(), this,
        [](ControlsPanel* self, int /* value */) { self->update_fan_display(); });

    // Subscribe to multi-fan list changes (fires when fans are discovered/updated)
    fans_version_observer_ = observe_int_sync<ControlsPanel>(
        printer_state_.get_fans_version_subject(), this,
        [](ControlsPanel* self, int /* version */) { self->populate_secondary_fans(); });

    // Subscribe to pending Z-offset delta (for unsaved adjustment banner)
    pending_z_offset_observer_ =
        observe_int_sync<ControlsPanel>(printer_state_.get_pending_z_offset_delta_subject(), this,
                                        [](ControlsPanel* self, int delta_microns) {
                                            self->update_z_offset_delta_display(delta_microns);
                                        });

    // Subscribe to gcode position updates for Position card (commanded position in
    // centimillimeters)
    position_x_observer_ = observe_int_sync<ControlsPanel>(
        printer_state_.get_gcode_position_x_subject(), this, [](ControlsPanel* self, int centimm) {
            format_position(centimm, self->controls_pos_x_buf_, sizeof(self->controls_pos_x_buf_));
            lv_subject_copy_string(&self->controls_pos_x_subject_, self->controls_pos_x_buf_);
        });

    position_y_observer_ = observe_int_sync<ControlsPanel>(
        printer_state_.get_gcode_position_y_subject(), this, [](ControlsPanel* self, int centimm) {
            format_position(centimm, self->controls_pos_y_buf_, sizeof(self->controls_pos_y_buf_));
            lv_subject_copy_string(&self->controls_pos_y_subject_, self->controls_pos_y_buf_);
        });

    position_z_observer_ = observe_int_sync<ControlsPanel>(
        printer_state_.get_gcode_position_z_subject(), this, [](ControlsPanel* self, int centimm) {
            format_position(centimm, self->controls_pos_z_buf_, sizeof(self->controls_pos_z_buf_));
            lv_subject_copy_string(&self->controls_pos_z_subject_, self->controls_pos_z_buf_);
        });

    // Subscribe to speed/flow factor updates
    speed_factor_observer_ = observe_int_sync<ControlsPanel>(
        printer_state_.get_speed_factor_subject(), this,
        [](ControlsPanel* self, int /* value */) { self->update_speed_display(); });

    spdlog::debug("[{}] Observers registered for dashboard live data", get_name());
}

// ============================================================================
// DISPLAY UPDATE HELPERS
// ============================================================================

void ControlsPanel::update_nozzle_temp_display() {
    float current = centi_to_degrees_f(cached_extruder_temp_);
    float target = centi_to_degrees_f(cached_extruder_target_);

    // HERO display: Large temperature value
    if (cached_extruder_target_ > 0) {
        std::snprintf(nozzle_temp_buf_, sizeof(nozzle_temp_buf_), "%.0f / %.0f°C", current, target);
    } else {
        std::snprintf(nozzle_temp_buf_, sizeof(nozzle_temp_buf_), "%.0f°C", current);
    }
    lv_subject_copy_string(&nozzle_temp_subject_, nozzle_temp_buf_);

    // Compute percentage for progress bar (0-100)
    int pct = 0;
    if (cached_extruder_target_ > 0) {
        pct = (cached_extruder_temp_ * 100) / cached_extruder_target_;
        pct = std::clamp(pct, 0, 100);
    }
    lv_subject_set_int(&nozzle_pct_subject_, pct);

    // Status text with semantic meaning
    if (cached_extruder_target_ <= 0) {
        std::snprintf(nozzle_status_buf_, sizeof(nozzle_status_buf_), "Off");
    } else if (pct >= 98) {
        std::snprintf(nozzle_status_buf_, sizeof(nozzle_status_buf_), "Ready");
    } else {
        std::snprintf(nozzle_status_buf_, sizeof(nozzle_status_buf_), "Heating...");
    }
    lv_subject_copy_string(&nozzle_status_subject_, nozzle_status_buf_);
}

void ControlsPanel::update_bed_temp_display() {
    float current = centi_to_degrees_f(cached_bed_temp_);
    float target = centi_to_degrees_f(cached_bed_target_);

    // HERO display: Large temperature value
    if (cached_bed_target_ > 0) {
        std::snprintf(bed_temp_buf_, sizeof(bed_temp_buf_), "%.0f / %.0f°C", current, target);
    } else {
        std::snprintf(bed_temp_buf_, sizeof(bed_temp_buf_), "%.0f°C", current);
    }
    lv_subject_copy_string(&bed_temp_subject_, bed_temp_buf_);

    // Compute percentage for progress bar
    int pct = 0;
    if (cached_bed_target_ > 0) {
        pct = (cached_bed_temp_ * 100) / cached_bed_target_;
        pct = std::clamp(pct, 0, 100);
    }
    lv_subject_set_int(&bed_pct_subject_, pct);

    // Status text with semantic meaning
    if (cached_bed_target_ <= 0) {
        std::snprintf(bed_status_buf_, sizeof(bed_status_buf_), "Off");
    } else if (pct >= 98) {
        std::snprintf(bed_status_buf_, sizeof(bed_status_buf_), "Ready");
    } else {
        std::snprintf(bed_status_buf_, sizeof(bed_status_buf_), "Heating...");
    }
    lv_subject_copy_string(&bed_status_subject_, bed_status_buf_);
}

void ControlsPanel::update_fan_display() {
    int fan_pct = printer_state_.get_fan_speed_subject()
                      ? lv_subject_get_int(printer_state_.get_fan_speed_subject())
                      : 0;

    if (fan_pct > 0) {
        std::snprintf(fan_speed_buf_, sizeof(fan_speed_buf_), "%d%%", fan_pct);
    } else {
        std::snprintf(fan_speed_buf_, sizeof(fan_speed_buf_), "Off");
    }
    lv_subject_copy_string(&fan_speed_subject_, fan_speed_buf_);
    lv_subject_set_int(&fan_pct_subject_, fan_pct);
}

void ControlsPanel::refresh_macro_buttons() {
    auto& macros = StandardMacros::instance();

    // Update macro button 1 via subjects (declarative binding handles visibility/text)
    if (macro_1_slot_) {
        const auto& info = macros.get(*macro_1_slot_);
        if (info.is_empty()) {
            lv_subject_set_int(&macro_1_visible_, 0);
            spdlog::debug("[{}] Macro 1 slot '{}' is empty, hiding button", get_name(),
                          info.slot_name);
        } else {
            lv_subject_set_int(&macro_1_visible_, 1);
            lv_subject_copy_string(&macro_1_name_, info.display_name.c_str());
            spdlog::debug("[{}] Macro 1: '{}' → {}", get_name(), info.display_name,
                          info.get_macro());
        }
    } else {
        lv_subject_set_int(&macro_1_visible_, 0);
    }

    // Update macro button 2 via subjects (declarative binding handles visibility/text)
    if (macro_2_slot_) {
        const auto& info = macros.get(*macro_2_slot_);
        if (info.is_empty()) {
            lv_subject_set_int(&macro_2_visible_, 0);
            spdlog::debug("[{}] Macro 2 slot '{}' is empty, hiding button", get_name(),
                          info.slot_name);
        } else {
            lv_subject_set_int(&macro_2_visible_, 1);
            lv_subject_copy_string(&macro_2_name_, info.display_name.c_str());
            spdlog::debug("[{}] Macro 2: '{}' → {}", get_name(), info.display_name,
                          info.get_macro());
        }
    } else {
        lv_subject_set_int(&macro_2_visible_, 0);
    }

    // Update macro button 3 via subjects
    if (macro_3_slot_) {
        const auto& info = macros.get(*macro_3_slot_);
        if (info.is_empty()) {
            lv_subject_set_int(&macro_3_visible_, 0);
            spdlog::debug("[{}] Macro 3 slot '{}' is empty, hiding button", get_name(),
                          info.slot_name);
        } else {
            lv_subject_set_int(&macro_3_visible_, 1);
            lv_subject_copy_string(&macro_3_name_, info.display_name.c_str());
            spdlog::debug("[{}] Macro 3: '{}' → {}", get_name(), info.display_name,
                          info.get_macro());
        }
    } else {
        lv_subject_set_int(&macro_3_visible_, 0);
    }

    // Update macro button 4 via subjects
    if (macro_4_slot_) {
        const auto& info = macros.get(*macro_4_slot_);
        if (info.is_empty()) {
            lv_subject_set_int(&macro_4_visible_, 0);
            spdlog::debug("[{}] Macro 4 slot '{}' is empty, hiding button", get_name(),
                          info.slot_name);
        } else {
            lv_subject_set_int(&macro_4_visible_, 1);
            lv_subject_copy_string(&macro_4_name_, info.display_name.c_str());
            spdlog::debug("[{}] Macro 4: '{}' → {}", get_name(), info.display_name,
                          info.get_macro());
        }
    } else {
        lv_subject_set_int(&macro_4_visible_, 0);
    }
}

void ControlsPanel::populate_secondary_fans() {
    if (!secondary_fans_list_) {
        return;
    }

    // IMPORTANT: Cleanup order matters to avoid dangling pointers:
    // 1. Release observers first - they may reference subjects that were already deinit'd
    //    by PrinterState::init_fans() before it notified fans_version_ observers
    // 2. Clear row tracking (contains widget pointers that will become invalid)
    // 3. Clean widgets last (invalidates the pointers we just cleared)
    for (auto& obs : secondary_fan_observers_) {
        obs.release(); // Don't try to remove from potentially-invalid subjects
    }
    secondary_fan_observers_.clear();
    secondary_fan_rows_.clear();
    lv_obj_clean(secondary_fans_list_);

    const auto& fans = printer_state_.get_fans();
    int secondary_count = 0;

    for (const auto& fan : fans) {
        // Skip part cooling fan (it's the hero slider)
        if (fan.type == helix::FanType::PART_COOLING) {
            continue;
        }

        // Create a row for this fan: [Name] [Speed%] [Icon]
        lv_obj_t* row = lv_obj_create(secondary_fans_list_);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_row(row, 0, 0);
        // Pass clicks through to parent container
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        // Fan name label - 60% width, truncate with ellipsis if needed
        lv_obj_t* name_label = lv_label_create(row);
        lv_label_set_text(name_label, fan.display_name.c_str());
        lv_obj_set_width(name_label, LV_PCT(60));
        lv_obj_set_style_text_color(name_label, ui_theme_get_color("text_secondary"), 0);
        lv_obj_set_style_text_font(name_label, ui_theme_get_font("font_small"), 0);
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);

        // Speed percentage label - right-aligned
        char speed_buf[16];
        if (fan.speed_percent > 0) {
            std::snprintf(speed_buf, sizeof(speed_buf), "%d%%", fan.speed_percent);
        } else {
            std::snprintf(speed_buf, sizeof(speed_buf), "Off");
        }
        lv_obj_t* speed_label = lv_label_create(row);
        lv_label_set_text(speed_label, speed_buf);
        lv_obj_set_style_text_color(speed_label, ui_theme_get_color("text_secondary"), 0);
        lv_obj_set_style_text_font(speed_label, ui_theme_get_font("font_small"), 0);

        // Track this row for reactive speed updates
        secondary_fan_rows_.push_back({fan.object_name, speed_label});

        // Indicator icon: "A" circle for auto-controlled, › for controllable
        // Uses MDI icon font for proper glyph rendering
        lv_obj_t* indicator = lv_label_create(row);
        if (fan.is_controllable) {
            lv_label_set_text(indicator, LV_SYMBOL_RIGHT);
        } else {
            // "A" in circle indicates "auto-controlled by system"
            lv_label_set_text(indicator, ui_icon::lookup_codepoint("alpha_a_circle"));
        }
        lv_obj_set_style_text_color(indicator, ui_theme_get_color("text_secondary"), 0);
        lv_obj_set_style_text_font(indicator, &mdi_icons_16, 0);

        secondary_count++;

        // Limit to 2-3 visible fans to fit in card
        if (secondary_count >= 3) {
            break;
        }
    }

    // Subscribe to per-fan speed subjects for reactive updates
    subscribe_to_secondary_fan_speeds();

    spdlog::debug("[{}] Populated {} secondary fans", get_name(), secondary_count);
}

void ControlsPanel::update_z_offset_delta_display(int delta_microns) {
    // Format the delta in millimeters with sign: e.g., "+0.050mm" or "-0.025mm"
    double delta_mm = static_cast<double>(delta_microns) / 1000.0;

    if (delta_microns == 0) {
        z_offset_delta_display_buf_[0] = '\0'; // Empty string when no delta
    } else {
        std::snprintf(z_offset_delta_display_buf_, sizeof(z_offset_delta_display_buf_), "%+.3fmm",
                      delta_mm);
    }
    lv_subject_copy_string(&z_offset_delta_display_subject_, z_offset_delta_display_buf_);

    spdlog::debug("[{}] Z-offset delta display updated: '{}'", get_name(),
                  z_offset_delta_display_buf_);
}

void ControlsPanel::handle_save_z_offset() {
    int delta_microns = printer_state_.get_pending_z_offset_delta();
    if (delta_microns == 0) {
        spdlog::debug("[{}] No Z-offset adjustment to save", get_name());
        return;
    }

    double delta_mm = static_cast<double>(delta_microns) / 1000.0;
    spdlog::info("[{}] Saving Z-offset adjustment: {:+.3f}mm", get_name(), delta_mm);

    if (!api_) {
        NOTIFY_ERROR("No printer connection");
        return;
    }

    // Use Z_OFFSET_APPLY_ENDSTOP to save the current gcode_offset to the endstop position
    // This is preferred over SAVE_CONFIG because it doesn't require a Klipper restart
    api_->execute_gcode(
        "Z_OFFSET_APPLY_ENDSTOP",
        [this, delta_mm]() {
            NOTIFY_SUCCESS("Z-offset saved ({:+.3f}mm)", delta_mm);

            // Clear the pending delta since it's now saved
            printer_state_.clear_pending_z_offset_delta();
        },
        [](const MoonrakerError& err) { NOTIFY_ERROR("Save failed: {}", err.user_message()); });
}

// ============================================================================
// V2 CARD CLICK HANDLERS
// ============================================================================

void ControlsPanel::handle_quick_actions_clicked() {
    spdlog::debug("[{}] Quick Actions card clicked - opening Motion panel", get_name());

    // Create motion panel on first access (lazy initialization)
    if (!motion_panel_ && parent_screen_) {
        auto& motion = get_global_motion_panel();

        // Initialize subjects and callbacks if not already done
        if (!motion.are_subjects_initialized()) {
            motion.init_subjects();
        }
        motion.register_callbacks();

        // Create overlay UI
        motion_panel_ = motion.create(parent_screen_);
        if (!motion_panel_) {
            NOTIFY_ERROR("Failed to load motion panel");
            return;
        }

        // Register with NavigationManager for lifecycle callbacks
        NavigationManager::instance().register_overlay_instance(motion_panel_, &motion);
    }

    if (motion_panel_) {
        ui_nav_push_overlay(motion_panel_);
    }
}

void ControlsPanel::handle_temperatures_clicked() {
    spdlog::debug("[{}] Temperatures card clicked - opening nozzle temp panel", get_name());

    if (!temp_control_panel_) {
        NOTIFY_ERROR("Temperature panel not available");
        return;
    }

    // For combined temps card, open nozzle panel (user can switch to bed from there)
    if (!nozzle_temp_panel_ && parent_screen_) {
        nozzle_temp_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "nozzle_temp_panel", nullptr));
        if (nozzle_temp_panel_) {
            temp_control_panel_->setup_nozzle_panel(nozzle_temp_panel_, parent_screen_);
            // Panel starts hidden via XML hidden="true" attribute
        } else {
            NOTIFY_ERROR("Failed to load temperature panel");
            return;
        }
    }

    if (nozzle_temp_panel_) {
        ui_nav_push_overlay(nozzle_temp_panel_);
    }
}

void ControlsPanel::handle_nozzle_temp_clicked() {
    spdlog::debug("[{}] Nozzle temp clicked - opening nozzle temp panel", get_name());

    if (!temp_control_panel_) {
        NOTIFY_ERROR("Temperature panel not available");
        return;
    }

    if (!nozzle_temp_panel_ && parent_screen_) {
        nozzle_temp_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "nozzle_temp_panel", nullptr));
        if (nozzle_temp_panel_) {
            temp_control_panel_->setup_nozzle_panel(nozzle_temp_panel_, parent_screen_);
        } else {
            NOTIFY_ERROR("Failed to load nozzle temperature panel");
            return;
        }
    }

    if (nozzle_temp_panel_) {
        ui_nav_push_overlay(nozzle_temp_panel_);
    }
}

void ControlsPanel::handle_bed_temp_clicked() {
    spdlog::debug("[{}] Bed temp clicked - opening bed temp panel", get_name());

    if (!temp_control_panel_) {
        NOTIFY_ERROR("Temperature panel not available");
        return;
    }

    if (!bed_temp_panel_ && parent_screen_) {
        bed_temp_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "bed_temp_panel", nullptr));
        if (bed_temp_panel_) {
            temp_control_panel_->setup_bed_panel(bed_temp_panel_, parent_screen_);
        } else {
            NOTIFY_ERROR("Failed to load bed temperature panel");
            return;
        }
    }

    if (bed_temp_panel_) {
        ui_nav_push_overlay(bed_temp_panel_);
    }
}

void ControlsPanel::handle_cooling_clicked() {
    // Redirect to FanControlOverlay which handles all fans (part cooling + secondary)
    spdlog::debug("[{}] Cooling card clicked - opening Fan Control overlay", get_name());
    handle_secondary_fans_clicked();
}

void ControlsPanel::handle_secondary_fans_clicked() {
    spdlog::debug("[{}] Secondary fans clicked - opening Fan Control overlay", get_name());

    // Create fan control overlay on first access (lazy initialization)
    if (!fan_control_panel_ && parent_screen_) {
        auto& overlay = get_fan_control_overlay();

        // Initialize subjects and callbacks if not already done
        if (!overlay.are_subjects_initialized()) {
            overlay.init_subjects();
        }
        overlay.register_callbacks();

        // Pass the API reference for fan commands
        overlay.set_api(api_);

        // Create overlay UI
        fan_control_panel_ = overlay.create(parent_screen_);
        if (!fan_control_panel_) {
            NOTIFY_ERROR("Failed to load fan control overlay");
            return;
        }

        // Register with NavigationManager for lifecycle callbacks
        NavigationManager::instance().register_overlay_instance(fan_control_panel_, &overlay);
    }

    if (fan_control_panel_) {
        // Update API reference in case it changed
        get_fan_control_overlay().set_api(api_);
        ui_nav_push_overlay(fan_control_panel_);
    }
}

// ============================================================================
// QUICK ACTION BUTTON HANDLERS
// ============================================================================

void ControlsPanel::handle_home_all() {
    spdlog::debug("[{}] Home All clicked", get_name());
    if (api_) {
        api_->home_axes(
            "XYZ", []() { NOTIFY_SUCCESS("Homing started"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Homing failed: {}", err.user_message());
            });
    }
}

void ControlsPanel::handle_home_xy() {
    spdlog::debug("[{}] Home XY clicked", get_name());
    if (api_) {
        api_->home_axes(
            "XY", []() { NOTIFY_SUCCESS("Homing XY started"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Homing failed: {}", err.user_message());
            });
    }
}

void ControlsPanel::handle_home_z() {
    spdlog::debug("[{}] Home Z clicked", get_name());
    if (api_) {
        api_->home_axes(
            "Z", []() { NOTIFY_SUCCESS("Homing Z started"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Homing failed: {}", err.user_message());
            });
    }
}

void ControlsPanel::handle_macro_1() {
    if (!macro_1_slot_) {
        spdlog::debug("[{}] Macro 1 clicked but no slot configured", get_name());
        return;
    }

    const auto& info = StandardMacros::instance().get(*macro_1_slot_);
    spdlog::debug("[{}] Macro 1 clicked, executing slot '{}' → {}", get_name(), info.slot_name,
                  info.get_macro());

    if (!StandardMacros::instance().execute(
            *macro_1_slot_, api_, []() { NOTIFY_SUCCESS("Macro started"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Macro failed: {}", err.user_message());
            })) {
        NOTIFY_WARNING("{} macro not configured", info.display_name);
    }
}

void ControlsPanel::handle_macro_2() {
    if (!macro_2_slot_) {
        spdlog::debug("[{}] Macro 2 clicked but no slot configured", get_name());
        return;
    }

    const auto& info = StandardMacros::instance().get(*macro_2_slot_);
    spdlog::debug("[{}] Macro 2 clicked, executing slot '{}' → {}", get_name(), info.slot_name,
                  info.get_macro());

    if (!StandardMacros::instance().execute(
            *macro_2_slot_, api_, []() { NOTIFY_SUCCESS("Macro started"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Macro failed: {}", err.user_message());
            })) {
        NOTIFY_WARNING("{} macro not configured", info.display_name);
    }
}

void ControlsPanel::handle_macro_3() {
    if (!macro_3_slot_) {
        spdlog::debug("[{}] Macro 3 clicked but no slot configured", get_name());
        return;
    }

    const auto& info = StandardMacros::instance().get(*macro_3_slot_);
    spdlog::debug("[{}] Macro 3 clicked, executing slot '{}' → {}", get_name(), info.slot_name,
                  info.get_macro());

    if (!StandardMacros::instance().execute(
            *macro_3_slot_, api_, []() { NOTIFY_SUCCESS("Macro started"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Macro failed: {}", err.user_message());
            })) {
        NOTIFY_WARNING("{} macro not configured", info.display_name);
    }
}

void ControlsPanel::handle_macro_4() {
    if (!macro_4_slot_) {
        spdlog::debug("[{}] Macro 4 clicked but no slot configured", get_name());
        return;
    }

    const auto& info = StandardMacros::instance().get(*macro_4_slot_);
    spdlog::debug("[{}] Macro 4 clicked, executing slot '{}' → {}", get_name(), info.slot_name,
                  info.get_macro());

    if (!StandardMacros::instance().execute(
            *macro_4_slot_, api_, []() { NOTIFY_SUCCESS("Macro started"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Macro failed: {}", err.user_message());
            })) {
        NOTIFY_WARNING("{} macro not configured", info.display_name);
    }
}

// ============================================================================
// SPEED/FLOW OVERRIDE HANDLERS
// ============================================================================

void ControlsPanel::update_speed_display() {
    int speed_pct = 100;
    if (auto* speed_subj = printer_state_.get_speed_factor_subject()) {
        speed_pct = lv_subject_get_int(speed_subj);
    }
    std::snprintf(speed_override_buf_, sizeof(speed_override_buf_), "%d%%", speed_pct);
    lv_subject_copy_string(&speed_override_subject_, speed_override_buf_);
}

void ControlsPanel::update_flow_display() {
    // Flow factor is stored as percentage (100 = 100%)
    int flow_pct = 100;
    // Note: PrinterState may need a get_extrude_factor_subject() method
    // For now, we'll initialize to 100% and update when that's available
    std::snprintf(flow_override_buf_, sizeof(flow_override_buf_), "%d%%", flow_pct);
    lv_subject_copy_string(&flow_override_subject_, flow_override_buf_);
}

void ControlsPanel::handle_speed_up() {
    if (!api_) {
        NOTIFY_ERROR("No printer connection");
        return;
    }

    int current = 100;
    if (auto* speed_subj = printer_state_.get_speed_factor_subject()) {
        current = lv_subject_get_int(speed_subj);
    }

    int new_speed = std::min(current + 10, 200); // Cap at 200%
    spdlog::debug("[{}] Speed up: {} → {}", get_name(), current, new_speed);

    char gcode[32];
    std::snprintf(gcode, sizeof(gcode), "M220 S%d", new_speed);
    api_->execute_gcode(
        gcode, []() { /* Silent success */ },
        [](const MoonrakerError& err) {
            NOTIFY_ERROR("Speed change failed: {}", err.user_message());
        });
}

void ControlsPanel::handle_speed_down() {
    if (!api_) {
        NOTIFY_ERROR("No printer connection");
        return;
    }

    int current = 100;
    if (auto* speed_subj = printer_state_.get_speed_factor_subject()) {
        current = lv_subject_get_int(speed_subj);
    }

    int new_speed = std::max(current - 10, 10); // Floor at 10%
    spdlog::debug("[{}] Speed down: {} → {}", get_name(), current, new_speed);

    char gcode[32];
    std::snprintf(gcode, sizeof(gcode), "M220 S%d", new_speed);
    api_->execute_gcode(
        gcode, []() { /* Silent success */ },
        [](const MoonrakerError& err) {
            NOTIFY_ERROR("Speed change failed: {}", err.user_message());
        });
}

void ControlsPanel::handle_flow_up() {
    if (!api_) {
        NOTIFY_ERROR("No printer connection");
        return;
    }

    // For now, track locally; ideally this would come from PrinterState
    static int current_flow = 100;
    int new_flow = std::min(current_flow + 5, 150); // Cap at 150%
    spdlog::debug("[{}] Flow up: {} → {}", get_name(), current_flow, new_flow);
    current_flow = new_flow;

    char gcode[32];
    std::snprintf(gcode, sizeof(gcode), "M221 S%d", new_flow);
    api_->execute_gcode(
        gcode,
        [this, new_flow]() {
            std::snprintf(flow_override_buf_, sizeof(flow_override_buf_), "%d%%", new_flow);
            lv_subject_copy_string(&flow_override_subject_, flow_override_buf_);
        },
        [](const MoonrakerError& err) {
            NOTIFY_ERROR("Flow change failed: {}", err.user_message());
        });
}

void ControlsPanel::handle_flow_down() {
    if (!api_) {
        NOTIFY_ERROR("No printer connection");
        return;
    }

    // For now, track locally; ideally this would come from PrinterState
    static int current_flow = 100;
    int new_flow = std::max(current_flow - 5, 50); // Floor at 50%
    spdlog::debug("[{}] Flow down: {} → {}", get_name(), current_flow, new_flow);
    current_flow = new_flow;

    char gcode[32];
    std::snprintf(gcode, sizeof(gcode), "M221 S%d", new_flow);
    api_->execute_gcode(
        gcode,
        [this, new_flow]() {
            std::snprintf(flow_override_buf_, sizeof(flow_override_buf_), "%d%%", new_flow);
            lv_subject_copy_string(&flow_override_subject_, flow_override_buf_);
        },
        [](const MoonrakerError& err) {
            NOTIFY_ERROR("Flow change failed: {}", err.user_message());
        });
}

// ============================================================================
// FAN SLIDER HANDLER
// ============================================================================

void ControlsPanel::handle_fan_slider_changed(int value) {
    // Defensive validation - slider should already be 0-100 but clamp anyway
    value = std::clamp(value, 0, 100);
    spdlog::debug("[{}] Fan slider changed to {}%", get_name(), value);
    if (api_) {
        api_->set_fan_speed(
            "fan", static_cast<double>(value), []() { /* Silent success */ },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Fan control failed: {}", err.user_message());
            });
    }
}

// ============================================================================
// CALIBRATION HANDLERS
// ============================================================================

void ControlsPanel::handle_motors_clicked() {
    spdlog::debug("[{}] Motors Disable card clicked - showing confirmation", get_name());

    motors_confirmation_dialog_ = ui_modal_show_confirmation(
        "Disable Motors?", "Release all stepper motors. Position will be lost.",
        ModalSeverity::Warning, "Disable", on_motors_confirm, on_motors_cancel, this);

    if (!motors_confirmation_dialog_) {
        LOG_ERROR_INTERNAL("Failed to create motors confirmation dialog");
        NOTIFY_ERROR("Failed to show confirmation dialog");
        return;
    }

    spdlog::info("[{}] Motors confirmation dialog shown", get_name());
}

void ControlsPanel::handle_motors_confirm() {
    spdlog::debug("[{}] Motors disable confirmed", get_name());

    // Hide dialog first
    if (motors_confirmation_dialog_) {
        ui_modal_hide(motors_confirmation_dialog_);
        motors_confirmation_dialog_ = nullptr;
    }

    // Send M84 command to disable motors
    if (api_) {
        api_->execute_gcode(
            "M84", // Klipper command to disable steppers
            []() { NOTIFY_SUCCESS("Motors disabled"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Motors disable failed: {}", err.message);
            });
    }
}

void ControlsPanel::handle_motors_cancel() {
    spdlog::debug("[{}] Motors disable cancelled", get_name());

    if (motors_confirmation_dialog_) {
        ui_modal_hide(motors_confirmation_dialog_);
        motors_confirmation_dialog_ = nullptr;
    }
}

void ControlsPanel::handle_calibration_bed_mesh() {
    spdlog::debug("[{}] Bed Mesh button clicked", get_name());

    if (!bed_mesh_panel_ && parent_screen_) {
        auto& overlay = get_global_bed_mesh_panel();
        if (!overlay.are_subjects_initialized()) {
            overlay.init_subjects();
        }
        overlay.register_callbacks();
        bed_mesh_panel_ = overlay.create(parent_screen_);
        if (!bed_mesh_panel_) {
            NOTIFY_ERROR("Failed to create bed mesh panel");
            return;
        }
        NavigationManager::instance().register_overlay_instance(bed_mesh_panel_, &overlay);
    }

    if (bed_mesh_panel_) {
        ui_nav_push_overlay(bed_mesh_panel_);
    }
}

void ControlsPanel::handle_calibration_zoffset() {
    spdlog::debug("[{}] Z-Offset Calibration button clicked", get_name());

    if (!zoffset_panel_ && parent_screen_) {
        auto& overlay = get_global_zoffset_cal_panel();
        if (!overlay.are_subjects_initialized()) {
            overlay.init_subjects();
        }
        overlay.register_callbacks();
        zoffset_panel_ = overlay.create(parent_screen_);
        if (!zoffset_panel_) {
            NOTIFY_ERROR("Failed to create Z-offset panel");
            return;
        }
        NavigationManager::instance().register_overlay_instance(zoffset_panel_, &overlay);
    }

    if (zoffset_panel_) {
        ui_nav_push_overlay(zoffset_panel_);
    }
}

void ControlsPanel::handle_calibration_screws() {
    spdlog::debug("[{}] Bed Screws button clicked", get_name());

    if (!screws_panel_ && parent_screen_) {
        auto& overlay = get_global_screws_tilt_panel();
        if (!overlay.are_subjects_initialized()) {
            overlay.init_subjects();
        }
        overlay.register_callbacks();
        screws_panel_ = overlay.create(parent_screen_);
        if (!screws_panel_) {
            NOTIFY_ERROR("Failed to create screws tilt panel");
            return;
        }
        NavigationManager::instance().register_overlay_instance(screws_panel_, &overlay);
    }

    if (screws_panel_) {
        ui_nav_push_overlay(screws_panel_);
    }
}

void ControlsPanel::handle_calibration_motors() {
    spdlog::debug("[{}] Disable Motors button clicked", get_name());
    handle_motors_clicked();
}

// ============================================================================
// V2 CARD CLICK TRAMPOLINES (XML event_cb - use global accessor)
// ============================================================================

void ControlsPanel::on_quick_actions_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_quick_actions_clicked");
    (void)e;
    get_global_controls_panel().handle_quick_actions_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_temperatures_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_temperatures_clicked");
    (void)e;
    get_global_controls_panel().handle_temperatures_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_nozzle_temp_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_nozzle_temp_clicked");
    (void)e;
    get_global_controls_panel().handle_nozzle_temp_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_bed_temp_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_bed_temp_clicked");
    (void)e;
    get_global_controls_panel().handle_bed_temp_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_cooling_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_cooling_clicked");
    (void)e;
    get_global_controls_panel().handle_cooling_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_secondary_fans_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_secondary_fans_clicked");
    (void)e;
    get_global_controls_panel().handle_secondary_fans_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_motors_confirm(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_motors_confirm");
    auto* self = static_cast<ControlsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_motors_confirm();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_motors_cancel(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_motors_cancel");
    auto* self = static_cast<ControlsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_motors_cancel();
    }
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// CALIBRATION BUTTON TRAMPOLINES (XML event_cb - use global accessor)
// ============================================================================

void ControlsPanel::on_calibration_bed_mesh(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_calibration_bed_mesh");
    (void)e;
    get_global_controls_panel().handle_calibration_bed_mesh();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_calibration_zoffset(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_calibration_zoffset");
    (void)e;
    get_global_controls_panel().handle_calibration_zoffset();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_calibration_screws(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_calibration_screws");
    (void)e;
    get_global_controls_panel().handle_calibration_screws();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_calibration_motors(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_calibration_motors");
    (void)e;
    get_global_controls_panel().handle_calibration_motors();
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// V2 BUTTON TRAMPOLINES (XML event_cb - use global accessor)
// ============================================================================

void ControlsPanel::on_home_all(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_home_all");
    (void)e;
    get_global_controls_panel().handle_home_all();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_home_xy(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_home_xy");
    (void)e;
    get_global_controls_panel().handle_home_xy();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_home_z(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_home_z");
    (void)e;
    get_global_controls_panel().handle_home_z();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_macro_1(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_macro_1");
    (void)e;
    get_global_controls_panel().handle_macro_1();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_macro_2(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_macro_2");
    (void)e;
    get_global_controls_panel().handle_macro_2();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_macro_3(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_macro_3");
    (void)e;
    get_global_controls_panel().handle_macro_3();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_macro_4(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_macro_4");
    (void)e;
    get_global_controls_panel().handle_macro_4();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_speed_up(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_speed_up");
    (void)e;
    get_global_controls_panel().handle_speed_up();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_speed_down(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_speed_down");
    (void)e;
    get_global_controls_panel().handle_speed_down();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_flow_up(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_flow_up");
    (void)e;
    get_global_controls_panel().handle_flow_up();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_flow_down(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_flow_down");
    (void)e;
    get_global_controls_panel().handle_flow_down();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_fan_slider_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_fan_slider_changed");
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int value = lv_slider_get_value(slider);
    get_global_controls_panel().handle_fan_slider_changed(value);
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_save_z_offset(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_save_z_offset");
    (void)e;
    get_global_controls_panel().handle_save_z_offset();
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// OBSERVER CALLBACKS (Static - only for complex cases not using factory)
// ============================================================================

void ControlsPanel::on_secondary_fan_speed_changed(lv_observer_t* obs, lv_subject_t* subject) {
    auto* self = static_cast<ControlsPanel*>(lv_observer_get_user_data(obs));
    if (self) {
        int speed_pct = lv_subject_get_int(subject);
        // Find which fan this subject belongs to and update its label
        for (const auto& row : self->secondary_fan_rows_) {
            auto* fan_subject = self->printer_state_.get_fan_speed_subject(row.object_name);
            if (fan_subject == subject) {
                self->update_secondary_fan_speed(row.object_name, speed_pct);
                break;
            }
        }
    }
}

void ControlsPanel::subscribe_to_secondary_fan_speeds() {
    secondary_fan_observers_.reserve(secondary_fan_rows_.size());

    for (const auto& row : secondary_fan_rows_) {
        if (auto* subject = printer_state_.get_fan_speed_subject(row.object_name)) {
            secondary_fan_observers_.emplace_back(subject, on_secondary_fan_speed_changed, this);
            spdlog::trace("[{}] Subscribed to speed subject for secondary fan '{}'", get_name(),
                          row.object_name);
        }
    }

    spdlog::debug("[{}] Subscribed to {} secondary fan speed subjects", get_name(),
                  secondary_fan_observers_.size());
}

void ControlsPanel::update_secondary_fan_speed(const std::string& object_name, int speed_pct) {
    for (const auto& row : secondary_fan_rows_) {
        if (row.object_name == object_name && row.speed_label) {
            char speed_buf[16];
            if (speed_pct > 0) {
                std::snprintf(speed_buf, sizeof(speed_buf), "%d%%", speed_pct);
            } else {
                std::snprintf(speed_buf, sizeof(speed_buf), "Off");
            }
            lv_label_set_text(row.speed_label, speed_buf);
            spdlog::trace("[{}] Updated secondary fan '{}' speed to {}", get_name(), object_name,
                          speed_buf);
            break;
        }
    }
}

// ============================================================================
// GLOBAL INSTANCE (needed by main.cpp)
// ============================================================================

static std::unique_ptr<ControlsPanel> g_controls_panel;

ControlsPanel& get_global_controls_panel() {
    if (!g_controls_panel) {
        g_controls_panel = std::make_unique<ControlsPanel>(get_printer_state(), nullptr);
        StaticPanelRegistry::instance().register_destroy("ControlsPanel",
                                                         []() { g_controls_panel.reset(); });
    }
    return *g_controls_panel;
}
