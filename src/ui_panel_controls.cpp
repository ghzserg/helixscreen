// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_controls.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_notification.h"
#include "ui_panel_extrusion.h"
#include "ui_panel_fan.h"
#include "ui_panel_motion.h"
#include "ui_panel_temp_control.h"
#include "ui_subject_registry.h"
#include "ui_theme.h"

#include "app_globals.h"
#include "moonraker_api.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <algorithm> // std::clamp
#include <cstdio>
#include <cstring>
#include <memory>

// Forward declarations for class-based API
class MotionPanel;
MotionPanel& get_global_motion_panel();
class ExtrusionPanel;
ExtrusionPanel& get_global_extrusion_panel();
class FanPanel;
FanPanel& get_global_fan_panel();

// ============================================================================
// CONSTRUCTOR
// ============================================================================

ControlsPanel::ControlsPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // Dependencies passed for interface consistency
    // Child panels (motion, temp, extrusion) may use these when wired
}

ControlsPanel::~ControlsPanel() {
    // Clean up lazily-created overlay panels to prevent dangling LVGL objects
    if (motion_panel_) {
        lv_obj_del(motion_panel_);
    }
    if (nozzle_temp_panel_) {
        lv_obj_del(nozzle_temp_panel_);
    }
    if (bed_temp_panel_) {
        lv_obj_del(bed_temp_panel_);
    }
    if (extrusion_panel_) {
        lv_obj_del(extrusion_panel_);
    }
    if (fan_panel_) {
        lv_obj_del(fan_panel_);
    }
    if (calibration_modal_) {
        lv_obj_del(calibration_modal_);
    }
    if (bed_mesh_panel_) {
        lv_obj_del(bed_mesh_panel_);
    }
    if (zoffset_panel_) {
        lv_obj_del(zoffset_panel_);
    }
    if (screws_panel_) {
        lv_obj_del(screws_panel_);
    }
    if (motors_confirmation_dialog_) {
        lv_obj_del(motors_confirmation_dialog_);
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

    // Nozzle temperature display
    UI_SUBJECT_INIT_AND_REGISTER_STRING(nozzle_temp_subject_, nozzle_temp_buf_, "--°C",
                                        "controls_nozzle_temp");
    UI_SUBJECT_INIT_AND_REGISTER_INT(nozzle_pct_subject_, 0, "controls_nozzle_pct");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(nozzle_status_subject_, nozzle_status_buf_, "Off",
                                        "controls_nozzle_status");

    // Bed temperature display
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_temp_subject_, bed_temp_buf_, "--°C",
                                        "controls_bed_temp");
    UI_SUBJECT_INIT_AND_REGISTER_INT(bed_pct_subject_, 0, "controls_bed_pct");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_status_subject_, bed_status_buf_, "Off",
                                        "controls_bed_status");

    // Fan speed display
    UI_SUBJECT_INIT_AND_REGISTER_STRING(fan_speed_subject_, fan_speed_buf_, "Off",
                                        "controls_fan_speed");
    UI_SUBJECT_INIT_AND_REGISTER_INT(fan_pct_subject_, 0, "controls_fan_pct");

    // Preheat status (for Filament card)
    UI_SUBJECT_INIT_AND_REGISTER_STRING(preheat_status_subject_, preheat_status_buf_,
                                        "Noz: --°C  Bed: --°C", "controls_preheat_status");

    // Calibration modal visibility
    UI_SUBJECT_INIT_AND_REGISTER_INT(calibration_modal_visible_, 0, "calibration_modal_visible");

    // Register calibration modal event callbacks (XML event_cb references)
    lv_xml_register_event_cb(nullptr, "on_calibration_modal_close", on_calibration_modal_close);
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

    // Cooling: Fan slider
    lv_xml_register_event_cb(nullptr, "on_controls_fan_slider", on_fan_slider_changed);

    // Filament: Preheat buttons
    lv_xml_register_event_cb(nullptr, "on_controls_preheat_pla", on_preheat_pla);
    lv_xml_register_event_cb(nullptr, "on_controls_preheat_petg", on_preheat_petg);
    lv_xml_register_event_cb(nullptr, "on_controls_preheat_abs", on_preheat_abs);
    lv_xml_register_event_cb(nullptr, "on_controls_preheat_asa", on_preheat_asa);
    lv_xml_register_event_cb(nullptr, "on_controls_preheat_off", on_preheat_off);

    // Filament: Extrude/Retract buttons
    lv_xml_register_event_cb(nullptr, "on_controls_extrude", on_extrude);
    lv_xml_register_event_cb(nullptr, "on_controls_retract", on_retract);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Dashboard subjects initialized", get_name());
}

void ControlsPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    // Cache dynamic container for secondary fans
    secondary_fans_list_ = lv_obj_find_by_name(panel_, "secondary_fans_list");
    if (!secondary_fans_list_) {
        spdlog::warn("[{}] Could not find secondary_fans_list container", get_name());
    }

    // Wire up card click handlers (cards need manual wiring for navigation)
    setup_card_handlers();

    // Register observers for live data updates
    register_observers();

    // Populate secondary fans on initial setup (will be empty until discovery)
    populate_secondary_fans();

    spdlog::info("[{}] Setup complete", get_name());
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void ControlsPanel::setup_card_handlers() {
    // Card background clicks for navigation to full panels require manual wiring
    // because they need to pass 'this' for lazy panel creation context.
    // All button event handlers are wired via XML event_cb - see init_subjects().

    lv_obj_t* card_quick_actions = lv_obj_find_by_name(panel_, "card_quick_actions");
    lv_obj_t* card_temperatures = lv_obj_find_by_name(panel_, "card_temperatures");
    lv_obj_t* card_cooling = lv_obj_find_by_name(panel_, "card_cooling");
    lv_obj_t* card_filament = lv_obj_find_by_name(panel_, "card_filament");
    lv_obj_t* card_calibration = lv_obj_find_by_name(panel_, "card_calibration");

    if (!card_quick_actions || !card_temperatures || !card_cooling || !card_filament ||
        !card_calibration) {
        spdlog::error("[{}] Failed to find all V2 cards", get_name());
        return;
    }

    // Wire card background clicks (navigation to full overlay panels)
    lv_obj_add_event_cb(card_quick_actions, on_quick_actions_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(card_temperatures, on_temperatures_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(card_cooling, on_cooling_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(card_filament, on_filament_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(card_calibration, on_calibration_clicked, LV_EVENT_CLICKED, this);

    spdlog::debug("[{}] V2 card navigation handlers wired", get_name());
}

void ControlsPanel::register_observers() {
    // Subscribe to temperature updates
    if (auto* ext_temp = printer_state_.get_extruder_temp_subject()) {
        extruder_temp_observer_ = ObserverGuard(ext_temp, on_extruder_temp_changed, this);
    }
    if (auto* ext_target = printer_state_.get_extruder_target_subject()) {
        extruder_target_observer_ = ObserverGuard(ext_target, on_extruder_target_changed, this);
    }
    if (auto* bed_temp = printer_state_.get_bed_temp_subject()) {
        bed_temp_observer_ = ObserverGuard(bed_temp, on_bed_temp_changed, this);
    }
    if (auto* bed_target = printer_state_.get_bed_target_subject()) {
        bed_target_observer_ = ObserverGuard(bed_target, on_bed_target_changed, this);
    }

    // Subscribe to fan updates
    if (auto* fan = printer_state_.get_fan_speed_subject()) {
        fan_observer_ = ObserverGuard(fan, on_fan_changed, this);
    }

    // Subscribe to multi-fan list changes (fires when fans are discovered/updated)
    if (auto* fans_ver = printer_state_.get_fans_version_subject()) {
        fans_version_observer_ = ObserverGuard(fans_ver, on_fans_version_changed, this);
    }

    spdlog::debug("[{}] Observers registered for dashboard live data", get_name());
}

// ============================================================================
// DISPLAY UPDATE HELPERS
// ============================================================================

void ControlsPanel::update_nozzle_temp_display() {
    // Temps stored in centidegrees (°C * 10)
    float current = static_cast<float>(cached_extruder_temp_) / 10.0f;
    float target = static_cast<float>(cached_extruder_target_) / 10.0f;

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

    // Also update preheat status display
    update_preheat_status();
}

void ControlsPanel::update_bed_temp_display() {
    // Temps stored in centidegrees (°C * 10)
    float current = static_cast<float>(cached_bed_temp_) / 10.0f;
    float target = static_cast<float>(cached_bed_target_) / 10.0f;

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

    // Also update preheat status display (shows both temps)
    update_preheat_status();
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

void ControlsPanel::update_preheat_status() {
    // Format preheat status showing current nozzle and bed temps
    float nozzle_c = static_cast<float>(cached_extruder_temp_) / 10.0f;
    float bed_c = static_cast<float>(cached_bed_temp_) / 10.0f;

    std::snprintf(preheat_status_buf_, sizeof(preheat_status_buf_), "Noz: %.0f°C  Bed: %.0f°C",
                  nozzle_c, bed_c);
    lv_subject_copy_string(&preheat_status_subject_, preheat_status_buf_);
}

void ControlsPanel::populate_secondary_fans() {
    if (!secondary_fans_list_) {
        return;
    }

    // Clear existing fan rows
    lv_obj_clean(secondary_fans_list_);

    const auto& fans = printer_state_.get_fans();
    int secondary_count = 0;

    for (const auto& fan : fans) {
        // Skip part cooling fan (it's the hero slider)
        if (fan.type == FanType::PART_COOLING) {
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
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        // Fan name label
        lv_obj_t* name_label = lv_label_create(row);
        lv_label_set_text(name_label, fan.display_name.c_str());
        lv_obj_set_style_text_color(name_label, ui_theme_get_color("text_secondary"), 0);
        lv_obj_set_style_text_font(name_label, UI_FONT_SMALL, 0);

        // Speed + indicator container
        lv_obj_t* right_container = lv_obj_create(row);
        lv_obj_set_size(right_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(right_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(right_container, 0, 0);
        lv_obj_set_style_pad_all(right_container, 0, 0);
        lv_obj_remove_flag(right_container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(right_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(right_container, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(right_container, 4, 0);

        // Speed percentage
        char speed_buf[16];
        if (fan.speed_percent > 0) {
            std::snprintf(speed_buf, sizeof(speed_buf), "%d%%", fan.speed_percent);
        } else {
            std::snprintf(speed_buf, sizeof(speed_buf), "Off");
        }
        lv_obj_t* speed_label = lv_label_create(right_container);
        lv_label_set_text(speed_label, speed_buf);
        lv_obj_set_style_text_color(speed_label, ui_theme_get_color("text_secondary"), 0);
        lv_obj_set_style_text_font(speed_label, UI_FONT_SMALL, 0);

        // Indicator icon: ⚙ for auto-controlled, ▸ for controllable
        lv_obj_t* indicator = lv_label_create(right_container);
        if (fan.is_controllable) {
            lv_label_set_text(indicator, LV_SYMBOL_RIGHT); // ▸ means user can control
        } else {
            lv_label_set_text(indicator, LV_SYMBOL_SETTINGS); // ⚙ means auto/firmware
        }
        lv_obj_set_style_text_color(indicator, ui_theme_get_color("text_tertiary"), 0);
        lv_obj_set_style_text_font(indicator, UI_FONT_SMALL, 0);

        secondary_count++;

        // Limit to 2-3 visible fans to fit in card
        if (secondary_count >= 3) {
            break;
        }
    }

    spdlog::debug("[{}] Populated {} secondary fans", get_name(), secondary_count);
}

// ============================================================================
// V2 CARD CLICK HANDLERS
// ============================================================================

void ControlsPanel::handle_quick_actions_clicked() {
    spdlog::debug("[{}] Quick Actions card clicked - opening Motion panel", get_name());

    // Create motion panel on first access (lazy initialization)
    if (!motion_panel_ && parent_screen_) {
        motion_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "motion_panel", nullptr));
        if (motion_panel_) {
            get_global_motion_panel().setup(motion_panel_, parent_screen_);
            lv_obj_add_flag(motion_panel_, LV_OBJ_FLAG_HIDDEN);
        } else {
            NOTIFY_ERROR("Failed to load motion panel");
            return;
        }
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
            lv_obj_add_flag(nozzle_temp_panel_, LV_OBJ_FLAG_HIDDEN);
        } else {
            NOTIFY_ERROR("Failed to load temperature panel");
            return;
        }
    }

    if (nozzle_temp_panel_) {
        ui_nav_push_overlay(nozzle_temp_panel_);
    }
}

void ControlsPanel::handle_cooling_clicked() {
    spdlog::debug("[{}] Cooling card clicked - opening Fan panel", get_name());

    if (!fan_panel_ && parent_screen_) {
        auto& fan_panel_instance = get_global_fan_panel();
        if (!fan_panel_instance.are_subjects_initialized()) {
            fan_panel_instance.init_subjects();
        }

        fan_panel_ = static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "fan_panel", nullptr));
        if (fan_panel_) {
            fan_panel_instance.setup(fan_panel_, parent_screen_);
            lv_obj_add_flag(fan_panel_, LV_OBJ_FLAG_HIDDEN);
        } else {
            NOTIFY_ERROR("Failed to load fan panel");
            return;
        }
    }

    if (fan_panel_) {
        ui_nav_push_overlay(fan_panel_);
    }
}

void ControlsPanel::handle_filament_clicked() {
    spdlog::debug("[{}] Filament card clicked - opening Extrusion panel", get_name());

    if (!extrusion_panel_ && parent_screen_) {
        auto& extrusion_panel_instance = get_global_controls_extrusion_panel();
        if (!extrusion_panel_instance.are_subjects_initialized()) {
            extrusion_panel_instance.init_subjects();
        }

        extrusion_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "extrusion_panel", nullptr));
        if (extrusion_panel_) {
            extrusion_panel_instance.setup(extrusion_panel_, parent_screen_);
            lv_obj_add_flag(extrusion_panel_, LV_OBJ_FLAG_HIDDEN);
        } else {
            NOTIFY_ERROR("Failed to load extrusion panel");
            return;
        }
    }

    if (extrusion_panel_) {
        ui_nav_push_overlay(extrusion_panel_);
    }
}

// ============================================================================
// QUICK ACTION BUTTON HANDLERS
// ============================================================================

void ControlsPanel::handle_home_all() {
    spdlog::debug("[{}] Home All clicked", get_name());
    if (api_) {
        api_->home_axes(
            "XYZ", []() { ui_notification_success("Homing started"); },
            [](const MoonrakerError& err) {
                ui_notification_error("Homing failed", err.user_message().c_str());
            });
    }
}

void ControlsPanel::handle_home_xy() {
    spdlog::debug("[{}] Home XY clicked", get_name());
    if (api_) {
        api_->home_axes(
            "XY", []() { ui_notification_success("Homing XY started"); },
            [](const MoonrakerError& err) {
                ui_notification_error("Homing failed", err.user_message().c_str());
            });
    }
}

void ControlsPanel::handle_home_z() {
    spdlog::debug("[{}] Home Z clicked", get_name());
    if (api_) {
        api_->home_axes(
            "Z", []() { ui_notification_success("Homing Z started"); },
            [](const MoonrakerError& err) {
                ui_notification_error("Homing failed", err.user_message().c_str());
            });
    }
}

void ControlsPanel::handle_macro_1() {
    spdlog::debug("[{}] Macro 1 clicked", get_name());
    // TODO: Read from config - for now use HELIX_CLEAN_NOZZLE
    if (api_) {
        api_->execute_gcode(
            "HELIX_CLEAN_NOZZLE", []() { ui_notification_success("Macro started"); },
            [](const MoonrakerError& err) {
                ui_notification_error("Macro failed", err.user_message().c_str());
            });
    }
}

void ControlsPanel::handle_macro_2() {
    spdlog::debug("[{}] Macro 2 clicked", get_name());
    // TODO: Read from config - for now use HELIX_BED_LEVEL_IF_NEEDED
    if (api_) {
        api_->execute_gcode(
            "HELIX_BED_LEVEL_IF_NEEDED", []() { ui_notification_success("Macro started"); },
            [](const MoonrakerError& err) {
                ui_notification_error("Macro failed", err.user_message().c_str());
            });
    }
}

// ============================================================================
// PREHEAT HANDLERS
// ============================================================================

void ControlsPanel::handle_preheat(int nozzle_temp, int bed_temp, const char* material_name) {
    spdlog::debug("[{}] Preheat {} - nozzle {}°C, bed {}°C", get_name(), material_name, nozzle_temp,
                  bed_temp);

    if (api_) {
        // Set nozzle temperature
        api_->set_temperature(
            "extruder", static_cast<double>(nozzle_temp),
            [material_name, nozzle_temp]() {
                spdlog::info("[ControlsPanel] Preheat {} - nozzle set to {}°C", material_name,
                             nozzle_temp);
            },
            [](const MoonrakerError& err) {
                ui_notification_error("Preheat failed", err.user_message().c_str());
            });

        // Set bed temperature
        api_->set_temperature(
            "heater_bed", static_cast<double>(bed_temp),
            [material_name, bed_temp]() {
                spdlog::info("[ControlsPanel] Preheat {} - bed set to {}°C", material_name,
                             bed_temp);
            },
            [](const MoonrakerError& err) {
                ui_notification_error("Preheat failed", err.user_message().c_str());
            });

        if (nozzle_temp > 0 || bed_temp > 0) {
            char msg[64];
            std::snprintf(msg, sizeof(msg), "Preheating for %s", material_name);
            ui_notification_info(msg);
        } else {
            ui_notification_info("Heaters off");
        }
    }
}

// ============================================================================
// EXTRUSION HANDLERS
// ============================================================================

void ControlsPanel::handle_extrude() {
    spdlog::debug("[{}] Extrude clicked", get_name());
    if (api_) {
        // Extrude 10mm at 300mm/min using G-code
        // G1 E10 F300 - Extrude 10mm at 5mm/s (300mm/min)
        api_->execute_gcode(
            "M83\nG1 E10 F300", // Set relative extrusion, extrude 10mm
            []() { ui_notification_success("Extruding 10mm"); },
            [](const MoonrakerError& err) {
                ui_notification_error("Extrude failed", err.user_message().c_str());
            });
    }
}

void ControlsPanel::handle_retract() {
    spdlog::debug("[{}] Retract clicked", get_name());
    if (api_) {
        // Retract 10mm at 300mm/min using G-code
        // G1 E-10 F300 - Retract 10mm at 5mm/s (300mm/min)
        api_->execute_gcode(
            "M83\nG1 E-10 F300", // Set relative extrusion, retract 10mm
            []() { ui_notification_success("Retracting 10mm"); },
            [](const MoonrakerError& err) {
                ui_notification_error("Retract failed", err.user_message().c_str());
            });
    }
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
                ui_notification_error("Fan control failed", err.user_message().c_str());
            });
    }
}

// ============================================================================
// CALIBRATION HANDLERS
// ============================================================================

void ControlsPanel::handle_motors_clicked() {
    spdlog::debug("[{}] Motors Disable card clicked - showing confirmation", get_name());

    // Create modal config
    ui_modal_config_t config = {
        .position = {.use_alignment = true, .alignment = LV_ALIGN_CENTER, .x = 0, .y = 0},
        .backdrop_opa = 180,
        .keyboard = nullptr,
        .persistent = false,
        .on_close = nullptr};

    // Create attributes for title and message
    const char* attrs[] = {"title", "Disable Motors?", "message",
                           "Release all stepper motors. Position will be lost.", nullptr};

    // Configure modal_dialog: WARNING severity, confirm/cancel buttons
    ui_modal_configure(UI_MODAL_SEVERITY_WARNING, true, "Disable", "Cancel");

    // Show modal using unified modal_dialog component
    motors_confirmation_dialog_ = ui_modal_show("modal_dialog", &config, attrs);

    if (!motors_confirmation_dialog_) {
        LOG_ERROR_INTERNAL("Failed to create motors confirmation dialog");
        ui_notification_error("Error", "Failed to show confirmation dialog");
        return;
    }

    // Wire up cancel button (btn_secondary in modal_dialog)
    lv_obj_t* cancel_btn = lv_obj_find_by_name(motors_confirmation_dialog_, "btn_secondary");
    if (cancel_btn) {
        lv_obj_add_event_cb(cancel_btn, on_motors_cancel, LV_EVENT_CLICKED, this);
    }

    // Wire up confirm button (btn_primary in modal_dialog)
    lv_obj_t* confirm_btn = lv_obj_find_by_name(motors_confirmation_dialog_, "btn_primary");
    if (confirm_btn) {
        lv_obj_add_event_cb(confirm_btn, on_motors_confirm, LV_EVENT_CLICKED, this);
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
            []() {
                spdlog::info("[ControlsPanel] Motors disabled successfully");
                ui_notification_success("Motors disabled");
            },
            [](const MoonrakerError& err) {
                spdlog::error("[ControlsPanel] Motors disable failed: {}", err.user_message());
                ui_notification_error("Error", "Motors disable failed");
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

void ControlsPanel::handle_calibration_clicked() {
    spdlog::debug("[{}] Calibration & Tools card clicked - showing modal", get_name());

    // Create calibration modal on first access (lazy initialization)
    if (!calibration_modal_ && parent_screen_) {
        spdlog::debug("[{}] Creating calibration modal...", get_name());

        calibration_modal_ = static_cast<lv_obj_t*>(
            lv_xml_create(parent_screen_, "calibration_modal_root", nullptr));

        if (!calibration_modal_) {
            LOG_ERROR_INTERNAL("Failed to create calibration modal from XML");
            NOTIFY_ERROR("Failed to load calibration menu");
            return;
        }

        // Modal starts hidden (visibility controlled by subject)
        lv_obj_add_flag(calibration_modal_, LV_OBJ_FLAG_HIDDEN);
        spdlog::info("[{}] Calibration modal created", get_name());
    }

    // Show modal by setting visibility subject
    if (calibration_modal_) {
        lv_subject_set_int(&calibration_modal_visible_, 1);
        lv_obj_remove_flag(calibration_modal_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ControlsPanel::handle_calibration_modal_close() {
    spdlog::debug("[{}] Calibration modal close clicked", get_name());

    // Hide modal via visibility subject
    lv_subject_set_int(&calibration_modal_visible_, 0);
    if (calibration_modal_) {
        lv_obj_add_flag(calibration_modal_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ControlsPanel::handle_calibration_bed_mesh() {
    spdlog::debug("[{}] Bed Mesh button clicked", get_name());

    // Hide modal first
    handle_calibration_modal_close();

    // Create bed mesh panel on first access (lazy initialization)
    if (!bed_mesh_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating bed mesh panel...", get_name());

        bed_mesh_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "bed_mesh_panel", nullptr));

        if (bed_mesh_panel_) {
            lv_obj_add_flag(bed_mesh_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Bed mesh panel created", get_name());
        } else {
            LOG_ERROR_INTERNAL("Failed to create bed mesh panel from XML");
            NOTIFY_ERROR("Failed to load bed mesh panel");
            return;
        }
    }

    if (bed_mesh_panel_) {
        ui_nav_push_overlay(bed_mesh_panel_);
    }
}

void ControlsPanel::handle_calibration_zoffset() {
    spdlog::debug("[{}] Z-Offset Calibration button clicked", get_name());

    // Hide modal first
    handle_calibration_modal_close();

    // Create z-offset panel on first access (lazy initialization)
    if (!zoffset_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating z-offset panel...", get_name());

        zoffset_panel_ = static_cast<lv_obj_t*>(
            lv_xml_create(parent_screen_, "calibration_zoffset_panel", nullptr));

        if (zoffset_panel_) {
            lv_obj_add_flag(zoffset_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Z-offset panel created", get_name());
        } else {
            LOG_ERROR_INTERNAL("Failed to create z-offset panel from XML");
            NOTIFY_ERROR("Failed to load Z-offset panel");
            return;
        }
    }

    if (zoffset_panel_) {
        ui_nav_push_overlay(zoffset_panel_);
    }
}

void ControlsPanel::handle_calibration_screws() {
    spdlog::debug("[{}] Bed Screws button clicked", get_name());

    // Hide modal first
    handle_calibration_modal_close();

    // Create screws tilt panel on first access (lazy initialization)
    if (!screws_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating screws tilt panel...", get_name());

        screws_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "screws_tilt_panel", nullptr));

        if (screws_panel_) {
            lv_obj_add_flag(screws_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Screws tilt panel created", get_name());
        } else {
            LOG_ERROR_INTERNAL("Failed to create screws tilt panel from XML");
            NOTIFY_ERROR("Failed to load screws tilt panel");
            return;
        }
    }

    if (screws_panel_) {
        ui_nav_push_overlay(screws_panel_);
    }
}

void ControlsPanel::handle_calibration_motors() {
    spdlog::debug("[{}] Disable Motors button clicked from calibration modal", get_name());

    // Hide modal first
    handle_calibration_modal_close();

    // Reuse the existing motors confirmation dialog
    handle_motors_clicked();
}

// ============================================================================
// V2 CARD CLICK TRAMPOLINES (manual wiring with user_data)
// ============================================================================

void ControlsPanel::on_quick_actions_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_quick_actions_clicked");
    auto* self = static_cast<ControlsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_quick_actions_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_temperatures_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_temperatures_clicked");
    auto* self = static_cast<ControlsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_temperatures_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_cooling_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_cooling_clicked");
    auto* self = static_cast<ControlsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_cooling_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_filament_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_filament_clicked");
    auto* self = static_cast<ControlsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_filament_clicked();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_calibration_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_calibration_clicked");
    auto* self = static_cast<ControlsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_calibration_clicked();
    }
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
// CALIBRATION MODAL TRAMPOLINES (XML event_cb - use global accessor)
// ============================================================================

void ControlsPanel::on_calibration_modal_close(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_calibration_modal_close");
    (void)e; // XML event_cb doesn't pass user_data through
    get_global_controls_panel().handle_calibration_modal_close();
    LVGL_SAFE_EVENT_CB_END();
}

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

void ControlsPanel::on_fan_slider_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_fan_slider_changed");
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int value = lv_slider_get_value(slider);
    get_global_controls_panel().handle_fan_slider_changed(value);
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_preheat_pla(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_preheat_pla");
    (void)e;
    get_global_controls_panel().handle_preheat(210, 60, "PLA");
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_preheat_petg(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_preheat_petg");
    (void)e;
    get_global_controls_panel().handle_preheat(240, 80, "PETG");
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_preheat_abs(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_preheat_abs");
    (void)e;
    get_global_controls_panel().handle_preheat(250, 100, "ABS");
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_preheat_asa(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_preheat_asa");
    (void)e;
    get_global_controls_panel().handle_preheat(250, 100, "ASA");
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_preheat_off(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_preheat_off");
    (void)e;
    get_global_controls_panel().handle_preheat(0, 0, "Off");
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_extrude(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_extrude");
    (void)e;
    get_global_controls_panel().handle_extrude();
    LVGL_SAFE_EVENT_CB_END();
}

void ControlsPanel::on_retract(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_retract");
    (void)e;
    get_global_controls_panel().handle_retract();
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// OBSERVER CALLBACKS (Static - update dashboard display)
// ============================================================================

void ControlsPanel::on_extruder_temp_changed(lv_observer_t* obs, lv_subject_t* subject) {
    auto* self = static_cast<ControlsPanel*>(lv_observer_get_user_data(obs));
    if (self) {
        self->cached_extruder_temp_ = lv_subject_get_int(subject);
        self->update_nozzle_temp_display();
    }
}

void ControlsPanel::on_extruder_target_changed(lv_observer_t* obs, lv_subject_t* subject) {
    auto* self = static_cast<ControlsPanel*>(lv_observer_get_user_data(obs));
    if (self) {
        self->cached_extruder_target_ = lv_subject_get_int(subject);
        self->update_nozzle_temp_display();
    }
}

void ControlsPanel::on_bed_temp_changed(lv_observer_t* obs, lv_subject_t* subject) {
    auto* self = static_cast<ControlsPanel*>(lv_observer_get_user_data(obs));
    if (self) {
        self->cached_bed_temp_ = lv_subject_get_int(subject);
        self->update_bed_temp_display();
    }
}

void ControlsPanel::on_bed_target_changed(lv_observer_t* obs, lv_subject_t* subject) {
    auto* self = static_cast<ControlsPanel*>(lv_observer_get_user_data(obs));
    if (self) {
        self->cached_bed_target_ = lv_subject_get_int(subject);
        self->update_bed_temp_display();
    }
}

void ControlsPanel::on_fan_changed(lv_observer_t* obs, lv_subject_t* /* subject */) {
    auto* self = static_cast<ControlsPanel*>(lv_observer_get_user_data(obs));
    if (self) {
        self->update_fan_display();
    }
}

void ControlsPanel::on_fans_version_changed(lv_observer_t* obs, lv_subject_t* /* subject */) {
    auto* self = static_cast<ControlsPanel*>(lv_observer_get_user_data(obs));
    if (self) {
        // Rebuild the secondary fans list when fan discovery completes or speeds update
        self->populate_secondary_fans();
    }
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
