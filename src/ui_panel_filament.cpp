// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_filament.h"

#include "ui_component_keypad.h"
#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_icon.h"
#include "ui_nav.h"
#include "ui_subject_registry.h"
#include "ui_temperature_utils.h"
#include "ui_theme.h"
#include "ui_utils.h"

#include "app_constants.h"
#include "app_globals.h"
#include "moonraker_api.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <memory>

// Material temperature presets (indexed by material ID)
static const int MATERIAL_TEMPS[] = {
    AppConstants::MaterialPresets::PLA,           // 0 = PLA (210°C)
    AppConstants::MaterialPresets::PETG,          // 1 = PETG (240°C)
    AppConstants::MaterialPresets::ABS,           // 2 = ABS (250°C)
    AppConstants::MaterialPresets::CUSTOM_DEFAULT // 3 = Custom default (200°C)
};

// ============================================================================
// CONSTRUCTOR
// ============================================================================

FilamentPanel::FilamentPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // Initialize buffer contents with default values
    std::snprintf(temp_display_buf_, sizeof(temp_display_buf_), "%d / %d°C", nozzle_current_,
                  nozzle_target_);
    std::snprintf(status_buf_, sizeof(status_buf_), "%s", "Select material to begin");
    std::snprintf(warning_temps_buf_, sizeof(warning_temps_buf_), "Current: %d°C | Target: %d°C",
                  nozzle_current_, nozzle_target_);

    // Register XML event callbacks
    lv_xml_register_event_cb(nullptr, "filament_manage_slots_cb", on_manage_slots_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_load", on_load_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_unload", on_unload_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_purge", on_purge_clicked);
}

// ============================================================================
// PANELBASE IMPLEMENTATION
// ============================================================================

void FilamentPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // Initialize subjects with default values
    UI_SUBJECT_INIT_AND_REGISTER_STRING(temp_display_subject_, temp_display_buf_, temp_display_buf_,
                                        "filament_temp_display");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(status_subject_, status_buf_, status_buf_,
                                        "filament_status");
    UI_SUBJECT_INIT_AND_REGISTER_INT(material_selected_subject_, -1, "filament_material_selected");
    UI_SUBJECT_INIT_AND_REGISTER_INT(extrusion_allowed_subject_, 0,
                                     "filament_extrusion_allowed"); // false (cold at start)
    UI_SUBJECT_INIT_AND_REGISTER_INT(safety_warning_visible_subject_, 1,
                                     "filament_safety_warning_visible"); // true (cold at start)
    UI_SUBJECT_INIT_AND_REGISTER_STRING(warning_temps_subject_, warning_temps_buf_,
                                        warning_temps_buf_, "filament_warning_temps");

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized: temp={}/{}°C, material={}", get_name(),
                  nozzle_current_, nozzle_target_, selected_material_);
}

void FilamentPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    spdlog::debug("[{}] Setting up event handlers...", get_name());

    // Find and setup preset buttons
    const char* preset_names[] = {"preset_pla", "preset_petg", "preset_abs", "preset_custom"};
    for (int i = 0; i < 4; i++) {
        preset_buttons_[i] = lv_obj_find_by_name(panel_, preset_names[i]);
        if (preset_buttons_[i]) {
            if (i < 3) {
                // Standard presets (PLA, PETG, ABS) - use common trampoline
                lv_obj_add_event_cb(preset_buttons_[i], on_preset_button_clicked, LV_EVENT_CLICKED,
                                    this);
            } else {
                // Custom preset (opens keypad) - different handler
                lv_obj_add_event_cb(preset_buttons_[i], on_custom_button_clicked, LV_EVENT_CLICKED,
                                    this);
            }
        }
    }
    spdlog::debug("[{}] Preset buttons configured (4)", get_name());

    // Find action buttons (events handled by XML event_cb, but refs needed for state management)
    btn_load_ = lv_obj_find_by_name(panel_, "btn_load");
    btn_unload_ = lv_obj_find_by_name(panel_, "btn_unload");
    btn_purge_ = lv_obj_find_by_name(panel_, "btn_purge");
    spdlog::debug("[{}] Action buttons configured (events via XML)", get_name());

    // Find safety warning card
    safety_warning_ = lv_obj_find_by_name(panel_, "safety_warning");

    // Find status icon for dynamic updates
    status_icon_ = lv_obj_find_by_name(panel_, "status_icon");

    // Initialize visual state
    update_preset_buttons_visual();
    update_temp_display();
    update_status();
    update_warning_text();
    update_safety_state();

    spdlog::info("[{}] Setup complete!", get_name());
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void FilamentPanel::update_temp_display() {
    std::snprintf(temp_display_buf_, sizeof(temp_display_buf_), "%d / %d°C", nozzle_current_,
                  nozzle_target_);
    lv_subject_copy_string(&temp_display_subject_, temp_display_buf_);
}

void FilamentPanel::update_status_icon(const char* icon_name, const char* variant) {
    if (!status_icon_)
        return;

    // Update icon imperatively using ui_icon API
    ui_icon_set_source(status_icon_, icon_name);
    ui_icon_set_variant(status_icon_, variant);
}

void FilamentPanel::update_status() {
    const char* status_msg;

    if (helix::ui::temperature::is_extrusion_safe(nozzle_current_,
                                                  AppConstants::Temperature::MIN_EXTRUSION_TEMP)) {
        // Hot enough - ready to load
        status_msg = "Ready to load";
        update_status_icon("check", "success");
    } else if (nozzle_target_ >= AppConstants::Temperature::MIN_EXTRUSION_TEMP) {
        // Heating in progress
        std::snprintf(status_buf_, sizeof(status_buf_), "Heating to %d°C...", nozzle_target_);
        lv_subject_copy_string(&status_subject_, status_buf_);
        update_status_icon("flash", "warning");
        return; // Already updated, exit early
    } else {
        // Cold - needs material selection
        status_msg = "Select material to begin";
        update_status_icon("cooldown", "secondary");
    }

    lv_subject_copy_string(&status_subject_, status_msg);
}

void FilamentPanel::update_warning_text() {
    std::snprintf(warning_temps_buf_, sizeof(warning_temps_buf_), "Current: %d°C | Target: %d°C",
                  nozzle_current_, nozzle_target_);
    lv_subject_copy_string(&warning_temps_subject_, warning_temps_buf_);
}

void FilamentPanel::update_safety_state() {
    bool allowed = helix::ui::temperature::is_extrusion_safe(
        nozzle_current_, AppConstants::Temperature::MIN_EXTRUSION_TEMP);

    // Update reactive subjects
    lv_subject_set_int(&extrusion_allowed_subject_, allowed ? 1 : 0);
    lv_subject_set_int(&safety_warning_visible_subject_, allowed ? 0 : 1);

    // Imperative button state management (performance optimization)
    if (btn_load_) {
        if (allowed) {
            lv_obj_remove_state(btn_load_, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btn_load_, LV_STATE_DISABLED);
        }
    }

    if (btn_unload_) {
        if (allowed) {
            lv_obj_remove_state(btn_unload_, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btn_unload_, LV_STATE_DISABLED);
        }
    }

    if (btn_purge_) {
        if (allowed) {
            lv_obj_remove_state(btn_purge_, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btn_purge_, LV_STATE_DISABLED);
        }
    }

    // Safety warning visibility is handled by XML binding to safety_warning_visible_subject_
    // (updated at line 177 above)

    spdlog::debug("[{}] Safety state updated: allowed={} (temp={}°C)", get_name(), allowed,
                  nozzle_current_);
}

void FilamentPanel::update_preset_buttons_visual() {
    for (int i = 0; i < 4; i++) {
        if (preset_buttons_[i]) {
            if (i == selected_material_) {
                // Selected state - theme handles colors
                lv_obj_add_state(preset_buttons_[i], LV_STATE_CHECKED);
            } else {
                // Unselected state - theme handles colors
                lv_obj_remove_state(preset_buttons_[i], LV_STATE_CHECKED);
            }
        }
    }
}

// ============================================================================
// INSTANCE HANDLERS
// ============================================================================

void FilamentPanel::handle_preset_button(int material_id) {
    selected_material_ = material_id;
    nozzle_target_ = MATERIAL_TEMPS[material_id];

    lv_subject_set_int(&material_selected_subject_, selected_material_);
    update_preset_buttons_visual();
    update_temp_display();
    update_status();

    spdlog::info("[{}] Material selected: {} (target={}°C)", get_name(), material_id,
                 nozzle_target_);

    // Send temperature command to printer
    if (api_) {
        api_->set_temperature(
            "extruder", static_cast<double>(nozzle_target_),
            [target = nozzle_target_]() { NOTIFY_SUCCESS("Nozzle target set to {}°C", target); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Failed to set nozzle temp: {}", error.user_message());
            });
    }
}

void FilamentPanel::handle_custom_button() {
    spdlog::debug("[{}] Opening custom temperature keypad", get_name());

    ui_keypad_config_t config = {
        .initial_value = static_cast<float>(nozzle_target_ > 0 ? nozzle_target_ : 200),
        .min_value = 0.0f,
        .max_value = static_cast<float>(nozzle_max_temp_),
        .title_label = "Custom Temperature",
        .unit_label = "°C",
        .allow_decimal = false,
        .allow_negative = false,
        .callback = custom_temp_keypad_cb,
        .user_data = this // Pass 'this' for callback
    };

    ui_keypad_show(&config);
}

void FilamentPanel::handle_custom_temp_confirmed(float value) {
    spdlog::info("[{}] Custom temperature confirmed: {}°C", get_name(), static_cast<int>(value));

    selected_material_ = 3; // Custom
    nozzle_target_ = static_cast<int>(value);

    lv_subject_set_int(&material_selected_subject_, selected_material_);
    update_preset_buttons_visual();
    update_temp_display();
    update_status();

    // Send temperature command to printer
    if (api_) {
        api_->set_temperature(
            "extruder", static_cast<double>(nozzle_target_),
            [target = nozzle_target_]() { NOTIFY_SUCCESS("Nozzle target set to {}°C", target); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Failed to set nozzle temp: {}", error.user_message());
            });
    }
}

void FilamentPanel::handle_load_button() {
    if (!is_extrusion_allowed()) {
        NOTIFY_WARNING("Nozzle too cold for filament load ({}°C, min: {}°C)", nozzle_current_,
                       AppConstants::Temperature::MIN_EXTRUSION_TEMP);
        return;
    }

    spdlog::info("[{}] Loading filament", get_name());

    if (api_) {
        api_->execute_gcode(
            "LOAD_FILAMENT", []() { NOTIFY_SUCCESS("Filament load started"); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Filament load failed: {}", error.user_message());
            });
    }
}

void FilamentPanel::handle_unload_button() {
    if (!is_extrusion_allowed()) {
        NOTIFY_WARNING("Nozzle too cold for filament unload ({}°C, min: {}°C)", nozzle_current_,
                       AppConstants::Temperature::MIN_EXTRUSION_TEMP);
        return;
    }

    spdlog::info("[{}] Unloading filament", get_name());

    if (api_) {
        api_->execute_gcode(
            "UNLOAD_FILAMENT", []() { NOTIFY_SUCCESS("Filament unload started"); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Filament unload failed: {}", error.user_message());
            });
    }
}

void FilamentPanel::handle_purge_button() {
    if (!is_extrusion_allowed()) {
        NOTIFY_WARNING("Nozzle too cold for purge ({}°C, min: {}°C)", nozzle_current_,
                       AppConstants::Temperature::MIN_EXTRUSION_TEMP);
        return;
    }

    spdlog::info("[{}] Purging 10mm", get_name());

    if (api_) {
        // M83 = relative extrusion mode, G1 E10 F300 = extrude 10mm at 300mm/min
        api_->execute_gcode(
            "M83\nG1 E10 F300", []() { NOTIFY_SUCCESS("Purging 10mm"); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Purge failed: {}", error.user_message());
            });
    }
}

// ============================================================================
// STATIC TRAMPOLINES
// ============================================================================

void FilamentPanel::on_manage_slots_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_manage_slots_clicked");
    LV_UNUSED(e);
    // TODO: Navigate to AMS panel when implemented
    NOTIFY_INFO("AMS slot management coming soon");
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_load_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_load_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_load_button();
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_unload_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_unload_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_unload_button();
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_purge_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_purge_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_purge_button();
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_preset_button_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_preset_button_clicked");
    auto* self = static_cast<FilamentPanel*>(lv_event_get_user_data(e));
    if (self) {
        // Determine which preset was clicked by checking button name
        lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const char* name = lv_obj_get_name(btn);

        int material_id = -1;
        if (name) {
            if (strcmp(name, "preset_pla") == 0)
                material_id = 0;
            else if (strcmp(name, "preset_petg") == 0)
                material_id = 1;
            else if (strcmp(name, "preset_abs") == 0)
                material_id = 2;
        }

        if (material_id >= 0) {
            self->handle_preset_button(material_id);
        }
    }
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_custom_button_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_custom_button_clicked");
    auto* self = static_cast<FilamentPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_custom_button();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::custom_temp_keypad_cb(float value, void* user_data) {
    auto* self = static_cast<FilamentPanel*>(user_data);
    if (self) {
        self->handle_custom_temp_confirmed(value);
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

void FilamentPanel::set_temp(int current, int target) {
    // Validate temperature ranges
    helix::ui::temperature::validate_and_clamp_pair(current, target, nozzle_min_temp_,
                                                    nozzle_max_temp_, "Filament");

    nozzle_current_ = current;
    nozzle_target_ = target;

    update_temp_display();
    update_status();
    update_warning_text();
    update_safety_state();
}

void FilamentPanel::get_temp(int* current, int* target) const {
    if (current)
        *current = nozzle_current_;
    if (target)
        *target = nozzle_target_;
}

void FilamentPanel::set_material(int material_id) {
    if (material_id < 0 || material_id > 3) {
        spdlog::error("[{}] Invalid material ID {} (valid: 0-3)", get_name(), material_id);
        return;
    }

    selected_material_ = material_id;
    nozzle_target_ = MATERIAL_TEMPS[material_id];

    lv_subject_set_int(&material_selected_subject_, selected_material_);
    update_preset_buttons_visual();
    update_temp_display();
    update_status();

    spdlog::info("[{}] Material set: {} (target={}°C)", get_name(), material_id, nozzle_target_);
}

bool FilamentPanel::is_extrusion_allowed() const {
    return helix::ui::temperature::is_extrusion_safe(nozzle_current_,
                                                     AppConstants::Temperature::MIN_EXTRUSION_TEMP);
}

void FilamentPanel::set_limits(int min_temp, int max_temp) {
    nozzle_min_temp_ = min_temp;
    nozzle_max_temp_ = max_temp;
    spdlog::info("[{}] Nozzle temperature limits updated: {}-{}°C", get_name(), min_temp, max_temp);
}

// ============================================================================
// GLOBAL INSTANCE (needed by main.cpp)
// ============================================================================

static std::unique_ptr<FilamentPanel> g_filament_panel;

FilamentPanel& get_global_filament_panel() {
    if (!g_filament_panel) {
        g_filament_panel = std::make_unique<FilamentPanel>(get_printer_state(), nullptr);
    }
    return *g_filament_panel;
}
