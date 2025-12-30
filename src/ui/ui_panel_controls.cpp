// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_controls.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_fonts.h"
#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_notification.h"
#include "ui_panel_extrusion.h"
#include "ui_panel_fan.h"
#include "ui_panel_motion.h"
#include "ui_panel_temp_control.h"
#include "ui_subject_registry.h"
#include "ui_temperature_utils.h"
#include "ui_theme.h"

#include "app_globals.h"
#include "moonraker_api.h"
#include "printer_state.h"
#include "standard_macros.h"

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
    if (fan_panel_) {
        lv_obj_del(fan_panel_);
        fan_panel_ = nullptr;
    }
    // Modal dialogs: use ui_modal_hide() - NOT lv_obj_del()!
    if (calibration_modal_) {
        ui_modal_hide(calibration_modal_);
        calibration_modal_ = nullptr;
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
    // See docs/QUICK_REFERENCE.md "Modal Dialog Lifecycle"
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

    // Macro button visibility and names (for declarative binding)
    UI_SUBJECT_INIT_AND_REGISTER_INT(macro_1_visible_, 0, "macro_1_visible");
    UI_SUBJECT_INIT_AND_REGISTER_INT(macro_2_visible_, 0, "macro_2_visible");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(macro_1_name_, macro_1_name_buf_, "", "macro_1_name");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(macro_2_name_, macro_2_name_buf_, "", "macro_2_name");

    // Z-Offset delta display (for banner showing unsaved adjustment)
    UI_SUBJECT_INIT_AND_REGISTER_STRING(z_offset_delta_display_subject_,
                                        z_offset_delta_display_buf_, "", "z_offset_delta_display");

    // Note: Calibration modal uses ui_modal_show() pattern, no visibility subject needed

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

    // Z-Offset banner: Save button
    lv_xml_register_event_cb(nullptr, "on_controls_save_z_offset", on_save_z_offset);

    // Card click handlers (navigation to full overlay panels)
    lv_xml_register_event_cb(nullptr, "on_controls_quick_actions", on_quick_actions_clicked);
    lv_xml_register_event_cb(nullptr, "on_controls_temperatures", on_temperatures_clicked);
    lv_xml_register_event_cb(nullptr, "on_nozzle_temp_clicked", on_nozzle_temp_clicked);
    lv_xml_register_event_cb(nullptr, "on_bed_temp_clicked", on_bed_temp_clicked);
    lv_xml_register_event_cb(nullptr, "on_controls_cooling", on_cooling_clicked);
    lv_xml_register_event_cb(nullptr, "on_controls_calibration", on_calibration_clicked);

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

    // Load quick button slot assignments from config
    // Config stores slot names like "clean_nozzle", "bed_level"
    if (Config* config = Config::get_instance()) {
        std::string slot1_name =
            config->get<std::string>("/standard_macros/quick_button_1", "clean_nozzle");
        std::string slot2_name =
            config->get<std::string>("/standard_macros/quick_button_2", "bed_level");

        macro_1_slot_ = StandardMacros::slot_from_name(slot1_name);
        macro_2_slot_ = StandardMacros::slot_from_name(slot2_name);

        spdlog::debug("[{}] Quick buttons configured: slot1='{}', slot2='{}'", get_name(),
                      slot1_name, slot2_name);
    } else {
        // Fallback: use CleanNozzle and BedLevel slots
        macro_1_slot_ = StandardMacroSlot::CleanNozzle;
        macro_2_slot_ = StandardMacroSlot::BedLevel;
        spdlog::warn("[{}] Config not available, using default macro slots", get_name());
    }

    // Refresh button labels and visibility based on current StandardMacros state
    refresh_macro_buttons();

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

    // Subscribe to pending Z-offset delta (for unsaved adjustment banner)
    if (auto* pending_delta = printer_state_.get_pending_z_offset_delta_subject()) {
        pending_z_offset_observer_ =
            ObserverGuard(pending_delta, on_pending_z_offset_changed, this);
    }

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

        // Indicator icon: ⚙ for auto-controlled, › for controllable
        // Uses MDI icon font for proper glyph rendering
        lv_obj_t* indicator = lv_label_create(right_container);
        if (fan.is_controllable) {
            lv_label_set_text(indicator, LV_SYMBOL_RIGHT);
        } else {
            lv_label_set_text(indicator, LV_SYMBOL_SETTINGS);
        }
        lv_obj_set_style_text_color(indicator, ui_theme_get_color("text_secondary"), 0);
        lv_obj_set_style_text_font(indicator, &mdi_icons_16, 0);

        secondary_count++;

        // Limit to 2-3 visible fans to fit in card
        if (secondary_count >= 3) {
            break;
        }
    }

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
    spdlog::debug("[{}] Cooling card clicked - opening Fan panel", get_name());

    if (!fan_panel_ && parent_screen_) {
        auto& fan_panel_instance = get_global_fan_panel();
        if (!fan_panel_instance.are_subjects_initialized()) {
            fan_panel_instance.init_subjects();
        }

        fan_panel_ = static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "fan_panel", nullptr));
        if (fan_panel_) {
            fan_panel_instance.setup(fan_panel_, parent_screen_);
            // Panel starts hidden via XML hidden="true" attribute
        } else {
            NOTIFY_ERROR("Failed to load fan panel");
            return;
        }
    }

    if (fan_panel_) {
        ui_nav_push_overlay(fan_panel_);
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

void ControlsPanel::handle_calibration_clicked() {
    spdlog::debug("[{}] Calibration & Tools card clicked - showing modal", get_name());

    // Show calibration modal via Modal system (creates backdrop programmatically)
    calibration_modal_ = ui_modal_show("calibration_modal");

    if (!calibration_modal_) {
        LOG_ERROR_INTERNAL("Failed to create calibration modal from XML");
        NOTIFY_ERROR("Failed to load calibration menu");
        return;
    }

    spdlog::debug("[{}] Calibration modal shown", get_name());
}

void ControlsPanel::handle_calibration_modal_close() {
    spdlog::debug("[{}] Calibration modal close clicked", get_name());

    // Hide modal via Modal system
    if (calibration_modal_) {
        ui_modal_hide(calibration_modal_);
        calibration_modal_ = nullptr;
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
            // Panel starts hidden via XML hidden="true" attribute
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
            // Panel starts hidden via XML hidden="true" attribute
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
            // Panel starts hidden via XML hidden="true" attribute
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

void ControlsPanel::on_calibration_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_calibration_clicked");
    (void)e;
    get_global_controls_panel().handle_calibration_clicked();
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

void ControlsPanel::on_save_z_offset(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ControlsPanel] on_save_z_offset");
    (void)e;
    get_global_controls_panel().handle_save_z_offset();
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

void ControlsPanel::on_pending_z_offset_changed(lv_observer_t* obs, lv_subject_t* subject) {
    auto* self = static_cast<ControlsPanel*>(lv_observer_get_user_data(obs));
    if (self) {
        int delta_microns = lv_subject_get_int(subject);
        self->update_z_offset_delta_display(delta_microns);
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
