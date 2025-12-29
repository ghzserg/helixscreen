// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_filament.h"

#include "ui_component_keypad.h"
#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_icon.h"
#include "ui_nav.h"
#include "ui_panel_ams.h"
#include "ui_panel_temp_control.h"
#include "ui_subject_registry.h"
#include "ui_temperature_utils.h"
#include "ui_theme.h"
#include "ui_update_queue.h"
#include "ui_utils.h"

#include "app_constants.h"
#include "app_globals.h"
#include "filament_sensor_manager.h"
#include "moonraker_api.h"
#include "printer_state.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <memory>

// Material temperature presets (indexed by material ID: 0=PLA, 1=PETG, 2=ABS, 3=TPU)
static const int MATERIAL_NOZZLE_TEMPS[] = {210, 240, 250, 230};
static const int MATERIAL_BED_TEMPS[] = {60, 80, 100, 50};
static const char* MATERIAL_NAMES[] = {"PLA", "PETG", "ABS", "TPU"};

using helix::ui::temperature::centi_to_degrees;

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
    std::snprintf(safety_warning_text_buf_, sizeof(safety_warning_text_buf_),
                  "Heat to %d°C to load/unload", min_extrude_temp_);
    std::snprintf(material_nozzle_buf_, sizeof(material_nozzle_buf_), "--");
    std::snprintf(material_bed_buf_, sizeof(material_bed_buf_), "--");
    std::snprintf(nozzle_current_buf_, sizeof(nozzle_current_buf_), "%d°C", nozzle_current_);
    std::snprintf(nozzle_target_buf_, sizeof(nozzle_target_buf_), "--");
    std::snprintf(bed_current_buf_, sizeof(bed_current_buf_), "%d°C", bed_current_);
    std::snprintf(bed_target_buf_, sizeof(bed_target_buf_), "--");

    // Register XML event callbacks
    lv_xml_register_event_cb(nullptr, "filament_manage_slots_cb", on_manage_slots_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_load", on_load_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_unload", on_unload_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_purge", on_purge_clicked);

    // Material preset buttons
    lv_xml_register_event_cb(nullptr, "on_filament_preset_pla", on_preset_pla_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_preset_petg", on_preset_petg_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_preset_abs", on_preset_abs_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_preset_tpu", on_preset_tpu_clicked);

    // Temperature tap targets
    lv_xml_register_event_cb(nullptr, "on_filament_nozzle_temp_tap", on_nozzle_temp_tap_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_bed_temp_tap", on_bed_temp_tap_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_nozzle_target_tap",
                             on_nozzle_target_tap_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_bed_target_tap", on_bed_target_tap_clicked);

    // Purge amount buttons
    lv_xml_register_event_cb(nullptr, "on_filament_purge_5mm", on_purge_5mm_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_purge_10mm", on_purge_10mm_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_purge_25mm", on_purge_25mm_clicked);

    // Subscribe to PrinterState temperatures to show actual printer state
    // NOTE: Observers must defer UI updates via ui_async_call to avoid render-phase assertions
    // [L029]
    extruder_temp_observer_ = ObserverGuard(
        printer_state_.get_extruder_temp_subject(),
        [](lv_observer_t* observer, lv_subject_t* subject) {
            auto* self = static_cast<FilamentPanel*>(lv_observer_get_user_data(observer));
            if (self) {
                self->nozzle_current_ = centi_to_degrees(lv_subject_get_int(subject));
                ui_async_call(
                    [](void* ctx) {
                        auto* panel = static_cast<FilamentPanel*>(ctx);
                        panel->update_left_card_temps();
                        panel->update_temp_display();
                        panel->update_warning_text();
                        panel->update_safety_state();
                    },
                    self);
            }
        },
        this);

    extruder_target_observer_ = ObserverGuard(
        printer_state_.get_extruder_target_subject(),
        [](lv_observer_t* observer, lv_subject_t* subject) {
            auto* self = static_cast<FilamentPanel*>(lv_observer_get_user_data(observer));
            if (self) {
                self->nozzle_target_ = centi_to_degrees(lv_subject_get_int(subject));
                ui_async_call(
                    [](void* ctx) {
                        auto* panel = static_cast<FilamentPanel*>(ctx);
                        panel->update_left_card_temps();
                        panel->update_material_temp_display();
                        panel->update_warning_text();
                    },
                    self);
            }
        },
        this);

    bed_temp_observer_ = ObserverGuard(
        printer_state_.get_bed_temp_subject(),
        [](lv_observer_t* observer, lv_subject_t* subject) {
            auto* self = static_cast<FilamentPanel*>(lv_observer_get_user_data(observer));
            if (self) {
                self->bed_current_ = centi_to_degrees(lv_subject_get_int(subject));
                ui_async_call(
                    [](void* ctx) {
                        auto* panel = static_cast<FilamentPanel*>(ctx);
                        panel->update_left_card_temps();
                    },
                    self);
            }
        },
        this);

    bed_target_observer_ = ObserverGuard(
        printer_state_.get_bed_target_subject(),
        [](lv_observer_t* observer, lv_subject_t* subject) {
            auto* self = static_cast<FilamentPanel*>(lv_observer_get_user_data(observer));
            if (self) {
                self->bed_target_ = centi_to_degrees(lv_subject_get_int(subject));
                ui_async_call(
                    [](void* ctx) {
                        auto* panel = static_cast<FilamentPanel*>(ctx);
                        panel->update_left_card_temps();
                        panel->update_material_temp_display();
                    },
                    self);
            }
        },
        this);
}

FilamentPanel::~FilamentPanel() {
    // Clean up warning dialogs if open (prevents memory leak and use-after-free)
    if (lv_is_initialized()) {
        if (load_warning_dialog_) {
            ui_modal_hide(load_warning_dialog_);
            load_warning_dialog_ = nullptr;
        }
        if (unload_warning_dialog_) {
            ui_modal_hide(unload_warning_dialog_);
            unload_warning_dialog_ = nullptr;
        }
    }
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
    UI_SUBJECT_INIT_AND_REGISTER_STRING(safety_warning_text_subject_, safety_warning_text_buf_,
                                        safety_warning_text_buf_, "filament_safety_warning_text");

    // Material temperature display subjects (for right side preset displays)
    UI_SUBJECT_INIT_AND_REGISTER_STRING(material_nozzle_temp_subject_, material_nozzle_buf_,
                                        material_nozzle_buf_, "filament_material_nozzle_temp");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(material_bed_temp_subject_, material_bed_buf_,
                                        material_bed_buf_, "filament_material_bed_temp");

    // Left card temperature subjects (current and target for nozzle/bed)
    UI_SUBJECT_INIT_AND_REGISTER_STRING(nozzle_current_subject_, nozzle_current_buf_,
                                        nozzle_current_buf_, "filament_nozzle_current");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(nozzle_target_subject_, nozzle_target_buf_,
                                        nozzle_target_buf_, "filament_nozzle_target");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_current_subject_, bed_current_buf_, bed_current_buf_,
                                        "filament_bed_current");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_target_subject_, bed_target_buf_, bed_target_buf_,
                                        "filament_bed_target");

    // Operation in progress subject (for disabling buttons during filament ops)
    UI_SUBJECT_INIT_AND_REGISTER_INT(operation_in_progress_subject_, 0,
                                     "filament_operation_in_progress");

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

    // Load configurable filament macros from config
    if (Config* config = Config::get_instance()) {
        MacroConfig load_cfg = config->get_macro("load_filament", {"Load", "LOAD_FILAMENT"});
        MacroConfig unload_cfg =
            config->get_macro("unload_filament", {"Unload", "UNLOAD_FILAMENT"});

        load_filament_gcode_ = load_cfg.gcode;
        unload_filament_gcode_ = unload_cfg.gcode;

        spdlog::debug("[{}] Filament macros configured: load='{}', unload='{}'", get_name(),
                      load_filament_gcode_, unload_filament_gcode_);
    } else {
        // Fallback if config not available
        load_filament_gcode_ = "LOAD_FILAMENT";
        unload_filament_gcode_ = "UNLOAD_FILAMENT";
        spdlog::warn("[{}] Config not available, using default filament macros", get_name());
    }

    spdlog::debug("[{}] Setting up (events handled declaratively via XML)", get_name());

    // Find preset buttons (for visual state updates)
    const char* preset_names[] = {"preset_pla", "preset_petg", "preset_abs", "preset_tpu"};
    for (int i = 0; i < 4; i++) {
        preset_buttons_[i] = lv_obj_find_by_name(panel_, preset_names[i]);
    }

    // Find purge amount buttons (for visual state updates)
    purge_5mm_btn_ = lv_obj_find_by_name(panel_, "purge_5mm");
    purge_10mm_btn_ = lv_obj_find_by_name(panel_, "purge_10mm");
    purge_25mm_btn_ = lv_obj_find_by_name(panel_, "purge_25mm");

    // Find action buttons (for state management)
    btn_load_ = lv_obj_find_by_name(panel_, "btn_load");
    btn_unload_ = lv_obj_find_by_name(panel_, "btn_unload");
    btn_purge_ = lv_obj_find_by_name(panel_, "btn_purge");

    // Find safety warning card
    safety_warning_ = lv_obj_find_by_name(panel_, "safety_warning");

    // Find status icon for dynamic updates
    status_icon_ = lv_obj_find_by_name(panel_, "status_icon");

    // Initialize visual state
    update_preset_buttons_visual();
    update_temp_display();
    update_left_card_temps();
    update_material_temp_display();
    update_status();
    update_status_icon_for_state();
    update_warning_text();
    update_safety_state();
    update_purge_button_highlight();

    // Setup combined temperature graph if TempControlPanel is available
    if (temp_control_panel_) {
        lv_obj_t* graph_container = lv_obj_find_by_name(panel_, "temp_graph_container");
        if (graph_container) {
            temp_control_panel_->setup_mini_combined_graph(graph_container);
            spdlog::debug("[{}] Temperature graph initialized", get_name());
        } else {
            spdlog::warn("[{}] temp_graph_container not found in XML", get_name());
        }
    }

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

    if (helix::ui::temperature::is_extrusion_safe(nozzle_current_, min_extrude_temp_)) {
        // Hot enough - ready to load
        status_msg = "Ready to load";
        update_status_icon("check", "success");
    } else if (nozzle_target_ >= min_extrude_temp_) {
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
    bool allowed = helix::ui::temperature::is_extrusion_safe(nozzle_current_, min_extrude_temp_);

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

    spdlog::trace("[{}] Safety state updated: allowed={} (temp={}°C)", get_name(), allowed,
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
    nozzle_target_ = MATERIAL_NOZZLE_TEMPS[material_id];
    bed_target_ = MATERIAL_BED_TEMPS[material_id];

    lv_subject_set_int(&material_selected_subject_, selected_material_);
    update_preset_buttons_visual();
    update_temp_display();
    update_material_temp_display();
    update_status();

    spdlog::info("[{}] Material selected: {} (nozzle={}°C, bed={}°C)", get_name(),
                 MATERIAL_NAMES[material_id], nozzle_target_, bed_target_);

    // Send temperature commands to printer (both nozzle and bed)
    if (api_) {
        api_->set_temperature(
            "extruder", static_cast<double>(nozzle_target_),
            [target = nozzle_target_]() { NOTIFY_SUCCESS("Nozzle target set to {}°C", target); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Failed to set nozzle temp: {}", error.user_message());
            });
        api_->set_temperature(
            "heater_bed", static_cast<double>(bed_target_),
            [target = bed_target_]() { NOTIFY_SUCCESS("Bed target set to {}°C", target); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Failed to set bed temp: {}", error.user_message());
            });
    }
}

void FilamentPanel::handle_nozzle_temp_tap() {
    spdlog::debug("[{}] Opening custom nozzle temperature keypad", get_name());

    ui_keypad_config_t config = {.initial_value =
                                     static_cast<float>(nozzle_target_ > 0 ? nozzle_target_ : 200),
                                 .min_value = 0.0f,
                                 .max_value = static_cast<float>(nozzle_max_temp_),
                                 .title_label = "Nozzle Temperature",
                                 .unit_label = "°C",
                                 .allow_decimal = false,
                                 .allow_negative = false,
                                 .callback = custom_nozzle_keypad_cb,
                                 .user_data = this};

    ui_keypad_show(&config);
}

void FilamentPanel::handle_bed_temp_tap() {
    spdlog::debug("[{}] Opening custom bed temperature keypad", get_name());

    ui_keypad_config_t config = {.initial_value =
                                     static_cast<float>(bed_target_ > 0 ? bed_target_ : 60),
                                 .min_value = 0.0f,
                                 .max_value = static_cast<float>(bed_max_temp_),
                                 .title_label = "Bed Temperature",
                                 .unit_label = "°C",
                                 .allow_decimal = false,
                                 .allow_negative = false,
                                 .callback = custom_bed_keypad_cb,
                                 .user_data = this};

    ui_keypad_show(&config);
}

void FilamentPanel::handle_custom_nozzle_confirmed(float value) {
    spdlog::info("[{}] Custom nozzle temperature confirmed: {}°C", get_name(),
                 static_cast<int>(value));

    nozzle_target_ = static_cast<int>(value);
    // Deselect any preset since user set custom temp
    selected_material_ = -1;
    lv_subject_set_int(&material_selected_subject_, selected_material_);
    update_preset_buttons_visual();
    update_temp_display();
    update_material_temp_display();
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

void FilamentPanel::handle_custom_bed_confirmed(float value) {
    spdlog::info("[{}] Custom bed temperature confirmed: {}°C", get_name(),
                 static_cast<int>(value));

    bed_target_ = static_cast<int>(value);
    // Deselect any preset since user set custom temp
    selected_material_ = -1;
    lv_subject_set_int(&material_selected_subject_, selected_material_);
    update_preset_buttons_visual();
    update_material_temp_display();

    // Send temperature command to printer
    if (api_) {
        api_->set_temperature(
            "heater_bed", static_cast<double>(bed_target_),
            [target = bed_target_]() { NOTIFY_SUCCESS("Bed target set to {}°C", target); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Failed to set bed temp: {}", error.user_message());
            });
    }
}

void FilamentPanel::update_material_temp_display() {
    // Show "--" for unset targets, otherwise show temperature
    if (nozzle_target_ == 0) {
        std::snprintf(material_nozzle_buf_, sizeof(material_nozzle_buf_), "--");
    } else {
        std::snprintf(material_nozzle_buf_, sizeof(material_nozzle_buf_), "%d°C", nozzle_target_);
    }
    if (bed_target_ == 0) {
        std::snprintf(material_bed_buf_, sizeof(material_bed_buf_), "--");
    } else {
        std::snprintf(material_bed_buf_, sizeof(material_bed_buf_), "%d°C", bed_target_);
    }
    lv_subject_copy_string(&material_nozzle_temp_subject_, material_nozzle_buf_);
    lv_subject_copy_string(&material_bed_temp_subject_, material_bed_buf_);
}

void FilamentPanel::update_left_card_temps() {
    // Update current temps
    std::snprintf(nozzle_current_buf_, sizeof(nozzle_current_buf_), "%d°C", nozzle_current_);
    std::snprintf(bed_current_buf_, sizeof(bed_current_buf_), "%d°C", bed_current_);
    lv_subject_copy_string(&nozzle_current_subject_, nozzle_current_buf_);
    lv_subject_copy_string(&bed_current_subject_, bed_current_buf_);

    // Update target temps (show "--" for unset)
    if (nozzle_target_ == 0) {
        std::snprintf(nozzle_target_buf_, sizeof(nozzle_target_buf_), "--");
    } else {
        std::snprintf(nozzle_target_buf_, sizeof(nozzle_target_buf_), "%d°C", nozzle_target_);
    }
    if (bed_target_ == 0) {
        std::snprintf(bed_target_buf_, sizeof(bed_target_buf_), "--");
    } else {
        std::snprintf(bed_target_buf_, sizeof(bed_target_buf_), "%d°C", bed_target_);
    }
    lv_subject_copy_string(&nozzle_target_subject_, nozzle_target_buf_);
    lv_subject_copy_string(&bed_target_subject_, bed_target_buf_);
}

void FilamentPanel::update_status_icon_for_state() {
    // Determine icon and color based on current state
    if (nozzle_target_ == 0 && bed_target_ == 0) {
        // Idle - no target set
        update_status_icon("info", "secondary");
    } else if (nozzle_current_ < nozzle_target_ - 5 || bed_current_ < bed_target_ - 5) {
        // Heating
        update_status_icon("fire", "warning");
    } else if (nozzle_current_ > nozzle_target_ + 5 && nozzle_target_ > 0) {
        // Cooling down
        update_status_icon("cooldown", "info");
    } else {
        // At temperature
        update_status_icon("check", "success");
    }
}

void FilamentPanel::update_purge_button_highlight() {
    // Highlight selected purge amount button with primary color
    lv_color_t selected_color = ui_theme_get_color("primary_color");
    lv_color_t default_color = ui_theme_get_color("card_bg");

    if (purge_5mm_btn_) {
        lv_obj_set_style_bg_color(
            purge_5mm_btn_, purge_amount_ == 5 ? selected_color : default_color, LV_PART_MAIN);
    }
    if (purge_10mm_btn_) {
        lv_obj_set_style_bg_color(
            purge_10mm_btn_, purge_amount_ == 10 ? selected_color : default_color, LV_PART_MAIN);
    }
    if (purge_25mm_btn_) {
        lv_obj_set_style_bg_color(
            purge_25mm_btn_, purge_amount_ == 25 ? selected_color : default_color, LV_PART_MAIN);
    }
}

void FilamentPanel::set_operation_in_progress(bool in_progress) {
    operation_in_progress_ = in_progress;
    lv_subject_set_int(&operation_in_progress_subject_, in_progress ? 1 : 0);
}

void FilamentPanel::handle_purge_amount_select(int amount) {
    purge_amount_ = amount;
    update_purge_button_highlight();
    spdlog::info("[{}] Purge amount set to {}mm", get_name(), amount);
}

void FilamentPanel::handle_load_button() {
    if (!is_extrusion_allowed()) {
        NOTIFY_WARNING("Nozzle too cold for filament load ({}°C, min: {}°C)", nozzle_current_,
                       min_extrude_temp_);
        return;
    }

    // Check if toolhead sensor shows filament already present
    auto& sensor_mgr = helix::FilamentSensorManager::instance();
    if (sensor_mgr.is_master_enabled() &&
        sensor_mgr.is_sensor_available(helix::FilamentSensorRole::TOOLHEAD) &&
        sensor_mgr.is_filament_detected(helix::FilamentSensorRole::TOOLHEAD)) {
        // Filament appears to already be loaded - show warning
        spdlog::info("[{}] Toolhead sensor shows filament present - showing load warning",
                     get_name());
        show_load_warning();
        return;
    }

    // No sensor or no filament detected - proceed directly
    execute_load();
}

void FilamentPanel::handle_unload_button() {
    if (!is_extrusion_allowed()) {
        NOTIFY_WARNING("Nozzle too cold for filament unload ({}°C, min: {}°C)", nozzle_current_,
                       min_extrude_temp_);
        return;
    }

    // Check if toolhead sensor shows no filament (nothing to unload)
    auto& sensor_mgr = helix::FilamentSensorManager::instance();
    if (sensor_mgr.is_master_enabled() &&
        sensor_mgr.is_sensor_available(helix::FilamentSensorRole::TOOLHEAD) &&
        !sensor_mgr.is_filament_detected(helix::FilamentSensorRole::TOOLHEAD)) {
        // No filament detected - show warning
        spdlog::info("[{}] Toolhead sensor shows no filament - showing unload warning", get_name());
        show_unload_warning();
        return;
    }

    // Sensor not available or filament detected - proceed directly
    execute_unload();
}

void FilamentPanel::handle_purge_button() {
    if (!is_extrusion_allowed()) {
        NOTIFY_WARNING("Nozzle too cold for purge ({}°C, min: {}°C)", nozzle_current_,
                       min_extrude_temp_);
        return;
    }

    if (operation_in_progress_) {
        NOTIFY_WARNING("Operation already in progress");
        return;
    }

    set_operation_in_progress(true);
    spdlog::info("[{}] Purging {}mm", get_name(), purge_amount_);

    if (api_) {
        // TODO: Check for PURGE macro and use if available
        // For now, use direct gcode: M83 = relative extrusion, G1 E{amount} F300
        std::string gcode = fmt::format("M83\nG1 E{} F300", purge_amount_);

        NOTIFY_INFO("Purging {}mm...", purge_amount_);

        api_->execute_gcode(
            gcode,
            [this, amount = purge_amount_]() {
                ui_async_call(
                    [](void* ud) {
                        auto* self = static_cast<FilamentPanel*>(ud);
                        self->set_operation_in_progress(false);
                    },
                    this);
                NOTIFY_SUCCESS("Purge complete ({}mm)", amount);
            },
            [this](const MoonrakerError& error) {
                ui_async_call(
                    [](void* ud) {
                        auto* self = static_cast<FilamentPanel*>(ud);
                        self->set_operation_in_progress(false);
                    },
                    this);
                NOTIFY_ERROR("Purge failed: {}", error.user_message());
            });
    } else {
        set_operation_in_progress(false);
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

// Material preset callbacks (XML event_cb - use global singleton)
void FilamentPanel::on_preset_pla_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_preset_pla_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_preset_button(0);
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_preset_petg_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_preset_petg_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_preset_button(1);
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_preset_abs_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_preset_abs_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_preset_button(2);
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_preset_tpu_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_preset_tpu_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_preset_button(3);
    LVGL_SAFE_EVENT_CB_END();
}

// Temperature tap callbacks (XML event_cb - use global singleton)
void FilamentPanel::on_nozzle_temp_tap_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_nozzle_temp_tap_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_nozzle_temp_tap();
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_bed_temp_tap_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_bed_temp_tap_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_bed_temp_tap();
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::custom_nozzle_keypad_cb(float value, void* user_data) {
    auto* self = static_cast<FilamentPanel*>(user_data);
    if (self) {
        self->handle_custom_nozzle_confirmed(value);
    }
}

void FilamentPanel::custom_bed_keypad_cb(float value, void* user_data) {
    auto* self = static_cast<FilamentPanel*>(user_data);
    if (self) {
        self->handle_custom_bed_confirmed(value);
    }
}

void FilamentPanel::on_nozzle_target_tap_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_nozzle_target_tap_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_nozzle_temp_tap();
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_bed_target_tap_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_bed_target_tap_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_bed_temp_tap();
    LVGL_SAFE_EVENT_CB_END();
}

// Purge amount callbacks (XML event_cb - use global singleton)
void FilamentPanel::on_purge_5mm_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_purge_5mm_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_purge_amount_select(5);
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_purge_10mm_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_purge_10mm_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_purge_amount_select(10);
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_purge_25mm_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_purge_25mm_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_purge_amount_select(25);
    LVGL_SAFE_EVENT_CB_END();
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
    nozzle_target_ = MATERIAL_NOZZLE_TEMPS[material_id];
    bed_target_ = MATERIAL_BED_TEMPS[material_id];

    lv_subject_set_int(&material_selected_subject_, selected_material_);
    update_preset_buttons_visual();
    update_temp_display();
    update_material_temp_display();
    update_status();

    spdlog::info("[{}] Material set: {} (nozzle={}°C, bed={}°C)", get_name(),
                 MATERIAL_NAMES[material_id], nozzle_target_, bed_target_);
}

bool FilamentPanel::is_extrusion_allowed() const {
    return helix::ui::temperature::is_extrusion_safe(nozzle_current_, min_extrude_temp_);
}

void FilamentPanel::set_limits(int min_temp, int max_temp, int min_extrude_temp) {
    nozzle_min_temp_ = min_temp;
    nozzle_max_temp_ = max_temp;

    // Update min_extrude_temp and safety warning text if changed
    if (min_extrude_temp_ != min_extrude_temp) {
        min_extrude_temp_ = min_extrude_temp;
        std::snprintf(safety_warning_text_buf_, sizeof(safety_warning_text_buf_),
                      "Heat to %d°C to load/unload", min_extrude_temp_);
        lv_subject_set_pointer(&safety_warning_text_subject_, safety_warning_text_buf_);
        spdlog::info("[{}] Min extrusion temp updated: {}°C", get_name(), min_extrude_temp_);
    }

    spdlog::info("[{}] Nozzle temperature limits updated: {}-{}°C", get_name(), min_temp, max_temp);
}

// ============================================================================
// FILAMENT SENSOR WARNING HELPERS
// ============================================================================

void FilamentPanel::execute_load() {
    spdlog::info("[{}] Loading filament, executing: {}", get_name(), load_filament_gcode_);

    if (api_ && !load_filament_gcode_.empty()) {
        api_->execute_gcode(
            load_filament_gcode_, []() { NOTIFY_SUCCESS("Filament load started"); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Filament load failed: {}", error.user_message());
            });
    }
}

void FilamentPanel::execute_unload() {
    spdlog::info("[{}] Unloading filament, executing: {}", get_name(), unload_filament_gcode_);

    if (api_ && !unload_filament_gcode_.empty()) {
        api_->execute_gcode(
            unload_filament_gcode_, []() { NOTIFY_SUCCESS("Filament unload started"); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Filament unload failed: {}", error.user_message());
            });
    }
}

void FilamentPanel::show_load_warning() {
    // Close any existing dialog first
    if (load_warning_dialog_) {
        ui_modal_hide(load_warning_dialog_);
        load_warning_dialog_ = nullptr;
    }

    load_warning_dialog_ = ui_modal_show_confirmation(
        "Filament Detected",
        "The toolhead sensor indicates filament is already loaded. "
        "Proceed with load anyway?",
        ModalSeverity::Warning, "Proceed", on_load_warning_proceed, on_load_warning_cancel, this);

    if (!load_warning_dialog_) {
        spdlog::error("[{}] Failed to create load warning dialog", get_name());
        return;
    }

    spdlog::debug("[{}] Load warning dialog shown", get_name());
}

void FilamentPanel::show_unload_warning() {
    // Close any existing dialog first
    if (unload_warning_dialog_) {
        ui_modal_hide(unload_warning_dialog_);
        unload_warning_dialog_ = nullptr;
    }

    unload_warning_dialog_ =
        ui_modal_show_confirmation("No Filament Detected",
                                   "The toolhead sensor indicates no filament is present. "
                                   "Proceed with unload anyway?",
                                   ModalSeverity::Warning, "Proceed", on_unload_warning_proceed,
                                   on_unload_warning_cancel, this);

    if (!unload_warning_dialog_) {
        spdlog::error("[{}] Failed to create unload warning dialog", get_name());
        return;
    }

    spdlog::debug("[{}] Unload warning dialog shown", get_name());
}

void FilamentPanel::on_load_warning_proceed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_load_warning_proceed");
    auto* self = static_cast<FilamentPanel*>(lv_event_get_user_data(e));
    if (self) {
        // Hide dialog first
        if (self->load_warning_dialog_) {
            ui_modal_hide(self->load_warning_dialog_);
            self->load_warning_dialog_ = nullptr;
        }
        // Execute load
        self->execute_load();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_load_warning_cancel(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_load_warning_cancel");
    auto* self = static_cast<FilamentPanel*>(lv_event_get_user_data(e));
    if (self && self->load_warning_dialog_) {
        ui_modal_hide(self->load_warning_dialog_);
        self->load_warning_dialog_ = nullptr;
        spdlog::debug("[FilamentPanel] Load cancelled by user");
    }
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_unload_warning_proceed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_unload_warning_proceed");
    auto* self = static_cast<FilamentPanel*>(lv_event_get_user_data(e));
    if (self) {
        // Hide dialog first
        if (self->unload_warning_dialog_) {
            ui_modal_hide(self->unload_warning_dialog_);
            self->unload_warning_dialog_ = nullptr;
        }
        // Execute unload
        self->execute_unload();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_unload_warning_cancel(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_unload_warning_cancel");
    auto* self = static_cast<FilamentPanel*>(lv_event_get_user_data(e));
    if (self && self->unload_warning_dialog_) {
        ui_modal_hide(self->unload_warning_dialog_);
        self->unload_warning_dialog_ = nullptr;
        spdlog::debug("[FilamentPanel] Unload cancelled by user");
    }
    LVGL_SAFE_EVENT_CB_END();
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
