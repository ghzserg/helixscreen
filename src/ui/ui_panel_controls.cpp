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

#include "app_globals.h"
#include "format_utils.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "moonraker_api.h"
#include "observer_factory.h"
#include "printer_state.h"
#include "standard_macros.h"
#include "static_panel_registry.h"
#include "subject_managed_panel.h"
#include "theme_manager.h"
#include "ui/ui_cleanup_helpers.h"
#include "ui/ui_event_trampoline.h"
#include "ui/ui_lazy_panel_helper.h"
#include "ui/ui_widget_helpers.h"

#include <spdlog/fmt/fmt.h>
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

    // Clean up lazily-created overlay panels to prevent dangling LVGL objects
    // Note: safe_delete_obj handles shutdown guards (lv_is_initialized, is_destroying_all, etc.)
    using helix::ui::safe_delete_obj;
    safe_delete_obj(motion_panel_);
    safe_delete_obj(nozzle_temp_panel_);
    safe_delete_obj(bed_temp_panel_);
    safe_delete_obj(fan_control_panel_);
    safe_delete_obj(bed_mesh_panel_);
    safe_delete_obj(zoffset_panel_);
    safe_delete_obj(screws_panel_);
    // Modal dialogs: ModalGuard handles cleanup automatically via RAII
    // See docs/DEVELOPER_QUICK_REFERENCE.md "Modal Dialog Lifecycle"
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

    // Z-offset display subject for live tuning
    std::strcpy(controls_z_offset_buf_, "+0.000mm");
    UI_MANAGED_SUBJECT_STRING(controls_z_offset_subject_, controls_z_offset_buf_, "+0.000mm",
                              "controls_z_offset", subjects_);

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

    // Quick Actions: Leveling buttons (QGL / Z-Tilt)
    lv_xml_register_event_cb(nullptr, "on_controls_qgl", on_qgl);
    lv_xml_register_event_cb(nullptr, "on_controls_z_tilt", on_z_tilt);

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

    // Z-Offset clickable row: Opens Print Tune overlay
    lv_xml_register_event_cb(nullptr, "on_zoffset_tune", on_zoffset_tune);

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
    FIND_WIDGET(secondary_fans_list_, panel_, "secondary_fans_list", get_name());
    if (secondary_fans_list_) {
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

    lv_obj_t* card_quick_actions = nullptr;
    lv_obj_t* card_temperatures = nullptr;
    lv_obj_t* card_cooling = nullptr;
    lv_obj_t* card_calibration = nullptr;

    FIND_WIDGET_OPTIONAL(card_quick_actions, panel_, "card_quick_actions");
    FIND_WIDGET_OPTIONAL(card_temperatures, panel_, "card_temperatures");
    FIND_WIDGET_OPTIONAL(card_cooling, panel_, "card_cooling");
    FIND_WIDGET_OPTIONAL(card_calibration, panel_, "card_calibration");

    if (!card_quick_actions || !card_temperatures || !card_cooling || !card_calibration) {
        spdlog::error("[{}] Failed to find all V2 cards", get_name());
        return;
    }

    spdlog::debug("[{}] V2 card navigation handlers validated (wired via XML event_cb)",
                  get_name());
}

void ControlsPanel::register_observers() {
    // Subscribe to temperature updates using bundle (replaces 4 individual observers)
    temp_observers_.setup_sync(
        this, printer_state_,
        [](ControlsPanel* self, int value) {
            self->cached_extruder_temp_ = value;
            self->update_nozzle_temp_display();
        },
        [](ControlsPanel* self, int value) {
            self->cached_extruder_target_ = value;
            self->update_nozzle_temp_display();
        },
        [](ControlsPanel* self, int value) {
            self->cached_bed_temp_ = value;
            self->update_bed_temp_display();
        },
        [](ControlsPanel* self, int value) {
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

    // Subscribe to gcode position updates for Position card using bundle (commanded position in
    // centimillimeters)
    pos_observers_.setup_sync(
        this, printer_state_,
        [](ControlsPanel* self, int centimm) {
            format_position(centimm, self->controls_pos_x_buf_, sizeof(self->controls_pos_x_buf_));
            lv_subject_copy_string(&self->controls_pos_x_subject_, self->controls_pos_x_buf_);
        },
        [](ControlsPanel* self, int centimm) {
            format_position(centimm, self->controls_pos_y_buf_, sizeof(self->controls_pos_y_buf_));
            lv_subject_copy_string(&self->controls_pos_y_subject_, self->controls_pos_y_buf_);
        },
        [](ControlsPanel* self, int centimm) {
            format_position(centimm, self->controls_pos_z_buf_, sizeof(self->controls_pos_z_buf_));
            lv_subject_copy_string(&self->controls_pos_z_subject_, self->controls_pos_z_buf_);
        });

    // Subscribe to speed/flow factor updates
    speed_factor_observer_ = observe_int_sync<ControlsPanel>(
        printer_state_.get_speed_factor_subject(), this,
        [](ControlsPanel* self, int /* value */) { self->update_speed_display(); });

    // Subscribe to gcode Z-offset for live tuning display
    gcode_z_offset_observer_ =
        observe_int_sync<ControlsPanel>(printer_state_.get_gcode_z_offset_subject(), this,
                                        [](ControlsPanel* self, int offset_microns) {
                                            self->update_controls_z_offset_display(offset_microns);
                                        });

    spdlog::debug("[{}] Observers registered for dashboard live data", get_name());
}

// ============================================================================
// DISPLAY UPDATE HELPERS
// ============================================================================

void ControlsPanel::update_nozzle_temp_display() {
    auto result = helix::fmt::heater_display(cached_extruder_temp_, cached_extruder_target_);

    std::snprintf(nozzle_temp_buf_, sizeof(nozzle_temp_buf_), "%s", result.temp.c_str());
    lv_subject_copy_string(&nozzle_temp_subject_, nozzle_temp_buf_);

    lv_subject_set_int(&nozzle_pct_subject_, result.pct);

    std::snprintf(nozzle_status_buf_, sizeof(nozzle_status_buf_), "%s", result.status.c_str());
    lv_subject_copy_string(&nozzle_status_subject_, nozzle_status_buf_);
}

void ControlsPanel::update_bed_temp_display() {
    auto result = helix::fmt::heater_display(cached_bed_temp_, cached_bed_target_);

    std::snprintf(bed_temp_buf_, sizeof(bed_temp_buf_), "%s", result.temp.c_str());
    lv_subject_copy_string(&bed_temp_subject_, bed_temp_buf_);

    lv_subject_set_int(&bed_pct_subject_, result.pct);

    std::snprintf(bed_status_buf_, sizeof(bed_status_buf_), "%s", result.status.c_str());
    lv_subject_copy_string(&bed_status_subject_, bed_status_buf_);
}

void ControlsPanel::update_fan_display() {
    int fan_pct = printer_state_.get_fan_speed_subject()
                      ? lv_subject_get_int(printer_state_.get_fan_speed_subject())
                      : 0;

    if (fan_pct > 0) {
        helix::fmt::format_percent(fan_pct, fan_speed_buf_, sizeof(fan_speed_buf_));
    } else {
        std::snprintf(fan_speed_buf_, sizeof(fan_speed_buf_), "Off");
    }
    lv_subject_copy_string(&fan_speed_subject_, fan_speed_buf_);
    lv_subject_set_int(&fan_pct_subject_, fan_pct);
}

void ControlsPanel::update_macro_button(StandardMacros& macros,
                                        const std::optional<StandardMacroSlot>& slot,
                                        lv_subject_t& visible_subject, lv_subject_t& name_subject,
                                        int button_num) {
    if (!slot) {
        lv_subject_set_int(&visible_subject, 0);
        return;
    }

    const auto& info = macros.get(*slot);
    if (info.is_empty()) {
        lv_subject_set_int(&visible_subject, 0);
        spdlog::debug("[{}] Macro {} slot '{}' is empty, hiding button", get_name(), button_num,
                      info.slot_name);
    } else {
        lv_subject_set_int(&visible_subject, 1);
        lv_subject_copy_string(&name_subject, info.display_name.c_str());
        spdlog::debug("[{}] Macro {}: '{}' → {}", get_name(), button_num, info.display_name,
                      info.get_macro());
    }
}

void ControlsPanel::refresh_macro_buttons() {
    auto& macros = StandardMacros::instance();

    // Arrays for iteration - slots, visible subjects, name subjects, button numbers
    const std::optional<StandardMacroSlot>* slots[] = {&macro_1_slot_, &macro_2_slot_,
                                                       &macro_3_slot_, &macro_4_slot_};
    lv_subject_t* visible_subjects[] = {&macro_1_visible_, &macro_2_visible_, &macro_3_visible_,
                                        &macro_4_visible_};
    lv_subject_t* name_subjects[] = {&macro_1_name_, &macro_2_name_, &macro_3_name_,
                                     &macro_4_name_};

    for (size_t i = 0; i < 4; ++i) {
        update_macro_button(macros, *slots[i], *visible_subjects[i], *name_subjects[i],
                            static_cast<int>(i + 1));
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
        lv_obj_set_style_text_color(name_label, theme_manager_get_color("text_muted"), 0);
        lv_obj_set_style_text_font(name_label, theme_manager_get_font("font_small"), 0);
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);

        // Speed percentage label - right-aligned
        char speed_buf[16];
        if (fan.speed_percent > 0) {
            helix::fmt::format_percent(fan.speed_percent, speed_buf, sizeof(speed_buf));
        } else {
            std::snprintf(speed_buf, sizeof(speed_buf), "Off");
        }
        lv_obj_t* speed_label = lv_label_create(row);
        lv_label_set_text(speed_label, speed_buf);
        lv_obj_set_style_text_color(speed_label, theme_manager_get_color("text"), 0);
        lv_obj_set_style_text_font(speed_label, theme_manager_get_font("font_small"), 0);

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
        lv_obj_set_style_text_color(indicator, theme_manager_get_color("secondary"), 0);
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

void ControlsPanel::update_controls_z_offset_display(int offset_microns) {
    double offset_mm = static_cast<double>(offset_microns) / 1000.0;
    std::snprintf(controls_z_offset_buf_, sizeof(controls_z_offset_buf_), "%+.3fmm", offset_mm);
    lv_subject_copy_string(&controls_z_offset_subject_, controls_z_offset_buf_);
}

void ControlsPanel::handle_zoffset_tune() {
    spdlog::debug("[{}] Z-offset tune clicked - opening Print Tune overlay", get_name());

    // Use singleton - handles lazy init, subject registration, and nav push
    get_print_tune_overlay().show(parent_screen_, api_, printer_state_);
}

void ControlsPanel::handle_save_z_offset() {
    // Get current gcode Z-offset (displayed offset, not pending delta)
    int offset_microns = 0;
    if (auto* subj = printer_state_.get_gcode_z_offset_subject()) {
        offset_microns = lv_subject_get_int(subj);
    }

    if (offset_microns == 0) {
        spdlog::debug("[{}] No Z-offset adjustment to save", get_name());
        return;
    }

    double offset_mm = static_cast<double>(offset_microns) / 1000.0;
    spdlog::info("[{}] Save Z-offset clicked: {:+.3f}mm", get_name(), offset_mm);

    // Show confirmation dialog - SAVE_CONFIG will restart Klipper
    save_z_offset_confirmation_dialog_ = ui_modal_show_confirmation(
        lv_tr("Save Z-Offset?"),
        lv_tr("This will apply the Z-offset to your endstop and restart Klipper to save the "
              "configuration. The printer will briefly disconnect."),
        ModalSeverity::Warning, lv_tr("Save"), on_save_z_offset_confirm, on_save_z_offset_cancel,
        this);

    if (!save_z_offset_confirmation_dialog_) {
        LOG_ERROR_INTERNAL("Failed to create save Z-offset confirmation dialog");
        NOTIFY_ERROR("Failed to show confirmation dialog");
        return;
    }

    spdlog::info("[{}] Save Z-offset confirmation dialog shown", get_name());
}

void ControlsPanel::handle_save_z_offset_confirm() {
    spdlog::debug("[{}] Save Z-offset confirmed", get_name());

    // Guard against double-click race condition
    if (save_z_offset_in_progress_) {
        spdlog::warn("[{}] Save Z-offset already in progress, ignoring", get_name());
        return;
    }
    save_z_offset_in_progress_ = true;

    // Hide dialog first - ModalGuard handles cleanup
    save_z_offset_confirmation_dialog_.hide();

    if (!api_) {
        NOTIFY_ERROR("No printer connection");
        save_z_offset_in_progress_ = false;
        return;
    }

    // Get current gcode Z-offset for logging
    int offset_microns = 0;
    if (auto* subj = printer_state_.get_gcode_z_offset_subject()) {
        offset_microns = lv_subject_get_int(subj);
    }
    double offset_mm = static_cast<double>(offset_microns) / 1000.0;

    // Execute Z_OFFSET_APPLY_ENDSTOP first, then SAVE_CONFIG
    // Z_OFFSET_APPLY_ENDSTOP applies current gcode Z-offset to the endstop position
    // SAVE_CONFIG persists the config and restarts Klipper
    api_->execute_gcode(
        "Z_OFFSET_APPLY_ENDSTOP",
        [this, offset_mm]() {
            spdlog::info("[{}] Z_OFFSET_APPLY_ENDSTOP success, executing SAVE_CONFIG", get_name());

            // Now save the config (this will restart Klipper)
            api_->execute_gcode(
                "SAVE_CONFIG",
                [this, offset_mm]() {
                    NOTIFY_SUCCESS("Z-offset saved ({:+.3f}mm). Klipper restarting...", offset_mm);
                    save_z_offset_in_progress_ = false;
                },
                [this](const MoonrakerError& err) {
                    NOTIFY_ERROR("SAVE_CONFIG failed: {}. Z-offset was applied but not saved. "
                                 "Run SAVE_CONFIG manually or the offset will be lost on restart.",
                                 err.user_message());
                    save_z_offset_in_progress_ = false;
                });
        },
        [this](const MoonrakerError& err) {
            NOTIFY_ERROR("Z_OFFSET_APPLY_ENDSTOP failed: {}", err.user_message());
            save_z_offset_in_progress_ = false;
        });
}

void ControlsPanel::handle_save_z_offset_cancel() {
    spdlog::debug("[{}] Save Z-offset cancelled", get_name());

    // ModalGuard handles cleanup
    save_z_offset_confirmation_dialog_.hide();
}

// ============================================================================
// V2 CARD CLICK HANDLERS
// ============================================================================

void ControlsPanel::handle_quick_actions_clicked() {
    helix::ui::lazy_create_and_push_overlay<MotionPanel>(get_global_motion_panel, motion_panel_,
                                                         parent_screen_, "Motion", get_name());
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
            NavigationManager::instance().register_overlay_instance(
                nozzle_temp_panel_, temp_control_panel_->get_nozzle_lifecycle());
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
            NavigationManager::instance().register_overlay_instance(
                nozzle_temp_panel_, temp_control_panel_->get_nozzle_lifecycle());
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
            NavigationManager::instance().register_overlay_instance(
                bed_temp_panel_, temp_control_panel_->get_bed_lifecycle());
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

void ControlsPanel::handle_qgl() {
    spdlog::debug("[{}] QGL clicked", get_name());
    if (api_) {
        api_->execute_gcode(
            "QUAD_GANTRY_LEVEL", []() { NOTIFY_SUCCESS("Quad Gantry Level started"); },
            [](const MoonrakerError& err) { NOTIFY_ERROR("QGL failed: {}", err.user_message()); });
    }
}

void ControlsPanel::handle_z_tilt() {
    spdlog::debug("[{}] Z-Tilt clicked", get_name());
    if (api_) {
        api_->execute_gcode(
            "Z_TILT_ADJUST", []() { NOTIFY_SUCCESS("Z-Tilt Adjust started"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Z-Tilt failed: {}", err.user_message());
            });
    }
}

void ControlsPanel::execute_macro(size_t index) {
    // Array of slots for lookup by index
    const std::optional<StandardMacroSlot>* slots[] = {&macro_1_slot_, &macro_2_slot_,
                                                       &macro_3_slot_, &macro_4_slot_};
    if (index >= 4) {
        spdlog::warn("[{}] Invalid macro index: {}", get_name(), index);
        return;
    }

    const auto& slot = *slots[index];
    int button_num = static_cast<int>(index + 1);

    if (!slot) {
        spdlog::debug("[{}] Macro {} clicked but no slot configured", get_name(), button_num);
        return;
    }

    const auto& info = StandardMacros::instance().get(*slot);
    spdlog::debug("[{}] Macro {} clicked, executing slot '{}' → {}", get_name(), button_num,
                  info.slot_name, info.get_macro());

    if (!StandardMacros::instance().execute(
            *slot, api_, []() { NOTIFY_SUCCESS("Macro started"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Macro failed: {}", err.user_message());
            })) {
        NOTIFY_WARNING("{} macro not configured", info.display_name);
    }
}

void ControlsPanel::handle_macro_1() {
    execute_macro(0);
}
void ControlsPanel::handle_macro_2() {
    execute_macro(1);
}
void ControlsPanel::handle_macro_3() {
    execute_macro(2);
}
void ControlsPanel::handle_macro_4() {
    execute_macro(3);
}

// ============================================================================
// SPEED/FLOW OVERRIDE HANDLERS
// ============================================================================

void ControlsPanel::update_speed_display() {
    int speed_pct = 100;
    if (auto* speed_subj = printer_state_.get_speed_factor_subject()) {
        speed_pct = lv_subject_get_int(speed_subj);
    }
    helix::fmt::format_percent(speed_pct, speed_override_buf_, sizeof(speed_override_buf_));
    lv_subject_copy_string(&speed_override_subject_, speed_override_buf_);
}

void ControlsPanel::update_flow_display() {
    // Flow factor is stored as percentage (100 = 100%)
    int flow_pct = 100;
    // Note: PrinterState may need a get_extrude_factor_subject() method
    // For now, we'll initialize to 100% and update when that's available
    helix::fmt::format_percent(flow_pct, flow_override_buf_, sizeof(flow_override_buf_));
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
            helix::fmt::format_percent(new_flow, flow_override_buf_, sizeof(flow_override_buf_));
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
            helix::fmt::format_percent(new_flow, flow_override_buf_, sizeof(flow_override_buf_));
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

    // ModalGuard's operator= hides any previous dialog before assigning new one
    motors_confirmation_dialog_ = ui_modal_show_confirmation(
        lv_tr("Disable Motors?"), lv_tr("Release all stepper motors. Position will be lost."),
        ModalSeverity::Warning, lv_tr("Disable"), on_motors_confirm, on_motors_cancel, this);

    if (!motors_confirmation_dialog_) {
        LOG_ERROR_INTERNAL("Failed to create motors confirmation dialog");
        NOTIFY_ERROR("Failed to show confirmation dialog");
        return;
    }

    spdlog::info("[{}] Motors confirmation dialog shown", get_name());
}

void ControlsPanel::handle_motors_confirm() {
    spdlog::debug("[{}] Motors disable confirmed", get_name());

    // Hide dialog first - ModalGuard handles cleanup
    motors_confirmation_dialog_.hide();

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

    // ModalGuard handles cleanup
    motors_confirmation_dialog_.hide();
}

void ControlsPanel::handle_calibration_bed_mesh() {
    helix::ui::lazy_create_and_push_overlay<BedMeshPanel>(
        get_global_bed_mesh_panel, bed_mesh_panel_, parent_screen_, "Bed Mesh", get_name());
}

void ControlsPanel::handle_calibration_zoffset() {
    helix::ui::lazy_create_and_push_overlay<ZOffsetCalibrationPanel>(
        get_global_zoffset_cal_panel, zoffset_panel_, parent_screen_, "Z-Offset Calibration",
        get_name());
}

void ControlsPanel::handle_calibration_screws() {
    helix::ui::lazy_create_and_push_overlay<ScrewsTiltPanel>(
        get_global_screws_tilt_panel, screws_panel_, parent_screen_, "Bed Screws", get_name());
}

void ControlsPanel::handle_calibration_motors() {
    spdlog::debug("[{}] Disable Motors button clicked", get_name());
    handle_motors_clicked();
}

// ============================================================================
// V2 CARD CLICK TRAMPOLINES (XML event_cb - use global accessor)
// ============================================================================

PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, quick_actions_clicked)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, temperatures_clicked)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, nozzle_temp_clicked)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, bed_temp_clicked)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, cooling_clicked)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, secondary_fans_clicked)

PANEL_TRAMPOLINE_USERDATA(ControlsPanel, motors_confirm)
PANEL_TRAMPOLINE_USERDATA(ControlsPanel, motors_cancel)
PANEL_TRAMPOLINE_USERDATA(ControlsPanel, save_z_offset_confirm)
PANEL_TRAMPOLINE_USERDATA(ControlsPanel, save_z_offset_cancel)

// ============================================================================
// CALIBRATION BUTTON TRAMPOLINES (XML event_cb - use global accessor)
// ============================================================================

PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, calibration_bed_mesh)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, calibration_zoffset)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, calibration_screws)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, calibration_motors)

// ============================================================================
// V2 BUTTON TRAMPOLINES (XML event_cb - use global accessor)
// ============================================================================

PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, home_all)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, home_xy)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, home_z)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, qgl)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, z_tilt)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, macro_1)

PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, macro_2)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, macro_3)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, macro_4)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, speed_up)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, speed_down)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, flow_up)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, flow_down)
PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, zoffset_tune)

// Cannot use macro - has extra logic to extract slider value
void ControlsPanel::on_fan_slider_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_fan_slider_changed");
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int value = lv_slider_get_value(slider);
    get_global_controls_panel().handle_fan_slider_changed(value);
    LVGL_SAFE_EVENT_CB_END();
}

PANEL_TRAMPOLINE(ControlsPanel, get_global_controls_panel, save_z_offset)

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
                helix::fmt::format_percent(speed_pct, speed_buf, sizeof(speed_buf));
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
